// =============================================================================================
//  npc_model.h - the NPC behaviour overhaul's decisions (World\npc\DESIGN.md §2-§3, Notes\62).
//  PURE: positions, flags and times in; who does what, where and when out. npc_harness.cpp runs it.
//
//    Clock / Rhythm   the hour from the sun's phase; how busy and how hurried the street is
//                     (hour x rain x place): density, scene rate, walking pace, bark rate.
//    Scheduler        which activity starts next, with whom, at which point of interest: weights by
//                     hour, place, weather and what the players are doing; budgets shared by the two
//                     players (apart = each their own half, together = one pool round the pair).
//    Talk             a conversation's turn-taking: who speaks which line when, what the listener
//                     does meanwhile (nod, laugh, ponder), pauses that vary with the mood, the
//                     goodbye; interrupted by an event and resumed (or not).
//    Reactions        events (stunt, crash, near miss, bump, chase, super build, odd vehicle,
//                     disguise) -> who reacts how (flinch / step aside / look / point / film / clap /
//                     gather / glare) with a staggered wave of attention, per-person and global
//                     cooldowns, at most one bark and maybe one echo.
//    Crowd            curiosity crowds that gather in a ring and drift apart.
//    Memory           grudges (someone you bumped glares for a while, then forgives) and city
//                     gossip (a police chase is talked about for the next minutes).
//
//  Everything it orders is one of the verbs life.h / spots.h already use in game (Notes 40b §8,
//  Notes 51 §2.3): goto a position or character, face, timed / looping anim, native talk, wander,
//  flee / panic broadcast. Feasibility per activity: DESIGN.md §2 and kActDef below.
// =============================================================================================
#pragma once
#include <cstdint>
#include <cmath>
#include <cstring>

namespace npc {

// ---- small maths ---------------------------------------------------------------------------------------
struct V3 { float x = 0, y = 0, z = 0; };
inline V3 Mk(float x, float y, float z) { V3 v; v.x = x; v.y = y; v.z = z; return v; }
inline float Dist(const V3& a, const V3& b) { const float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z; return std::sqrt(dx * dx + dy * dy + dz * dz); }
inline float Dist2D(const V3& a, const V3& b) { const float dx = a.x - b.x, dz = a.z - b.z; return std::sqrt(dx * dx + dz * dz); }
inline float Clamp(float v, float a, float b) { return v < a ? a : v > b ? b : v; }

struct Rng
{
    uint32_t s = 0x9E3779B9u;
    uint32_t U() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return s; }
    float F() { return (U() & 0xFFFFFF) / 16777216.0f; }
    float R(float a, float b) { return a + (b - a) * F(); }
    int I(int n) { return n > 0 ? static_cast<int>(U() % static_cast<uint32_t>(n)) : 0; }
};

// ---- clock and rhythm ------------------------------------------------------------------------------------
// lights.h g_sunPhase: 0 sunrise (~06:00), pi/2 noon, pi sunset (~18:00), -1 = no clock (treated as 13:00).
inline float HourOf(float sunPhase)
{
    if (sunPhase < 0.0f) return 13.0f;
    float h = 6.0f + sunPhase / 6.2831853f * 24.0f;
    while (h >= 24.0f) h -= 24.0f;
    return h;
}
enum Part : uint8_t { kDawn, kMorning, kMidday, kAfternoon, kEvening, kNight, kLate, kPartCount };
static const char* const kPartName[kPartCount] = { "dawn", "morning", "midday", "afternoon", "evening", "night", "late" };
inline uint8_t PartOf(float hour)
{
    if (hour < 5.0f) return kLate;
    if (hour < 7.0f) return kDawn;
    if (hour < 11.0f) return kMorning;
    if (hour < 14.0f) return kMidday;
    if (hour < 17.5f) return kAfternoon;
    if (hour < 21.5f) return kEvening;
    return kNight;
}

// The kind of place (the 23 game districts + Port Atlas, folded; the new places by spots / sites).
enum Place : uint8_t { kTown, kDowntownP, kPlaza, kParkP, kBeachP, kCountry, kHarbourP, kIsland, kNewTown, kWoodsP, kPlaceCount };
static const char* const kPlaceName[kPlaceCount] = { "town", "downtown", "plaza", "park", "beach", "country", "harbour", "island", "port atlas", "woods" };
// radio.h kDistrictFill order (0..22 the game's, 23 Port Atlas).
inline uint8_t PlaceOfDistrict(int d)
{
    switch (d)
    {
    case 15: case 16: case 21: return kDowntownP;          // downtown, kings court, uptown
    case 17: case 19: return kPlaza;                       // festival square, bright lights plaza
    case 13: case 14: return kParkP;                       // crescent park, pagoda (gardens)
    case 11: return kBeachP;                               // paradise sands
    case 2: case 3: return kCountry;                       // fort meadows, bluebell
    case 8: return kHarbourP;                              // albatross island (the docks)
    case 7: case 9: case 10: return kIsland;               // liberty, apollo, airport
    case 23: return kNewTown;                              // port atlas
    default: return kTown;
    }
}

struct Weather { bool rain = false; float amount = 0.0f; };   // weather.h -> rd::g_rain (0/1); amount if published later

struct Rhythm
{
    float density;     // 0..1.3: how many scenes the street can carry now (x the budgets)
    float sceneEvery;  // x the seconds between scene starts (>1 = rarer)
    float pace;        // x walking speed (hurrying in rain, strolling on a sunny afternoon)
    float barks;       // x how often people speak up
    bool  shelter;     // rain heavy enough that people head for cover
};

// Hour x weather x place. Shaped from what a city looks like: a morning rush, a lunchtime peak, an
// afternoon stroll, an evening out, a thin night and an empty small hours.
inline Rhythm RhythmOf(float hour, const Weather& w, uint8_t place)
{
    static const float kDensity[kPartCount] = { 0.35f, 0.85f, 1.0f, 0.9f, 0.8f, 0.45f, 0.2f };
    static const float kPace[kPartCount]    = { 1.0f, 1.12f, 1.05f, 0.95f, 1.0f, 1.0f, 1.05f };
    const uint8_t part = PartOf(hour);
    Rhythm r;
    r.density = kDensity[part];
    r.pace = kPace[part];
    r.barks = part == kLate ? 0.5f : 1.0f;
    if (place == kPlaza && (part == kEvening || part == kNight)) r.density += 0.3f;   // the night life
    if (place == kBeachP && (part == kMidday || part == kAfternoon)) r.density += 0.2f;
    if (place == kCountry || place == kWoodsP) r.density *= 0.6f;
    if (place == kDowntownP && part == kMorning) r.pace += 0.08f;                       // commuters
    r.shelter = false;
    if (w.rain)
    {
        const float a = w.amount > 0.0f ? Clamp(w.amount, 0.0f, 1.0f) : 0.7f;
        r.density *= 1.0f - 0.55f * a;               // fewer people out
        r.pace *= 1.0f + 0.3f * a;                   // and the rest hurrying
        r.barks *= 0.8f;
        r.shelter = a >= 0.5f;
    }
    r.density = Clamp(r.density, 0.05f, 1.3f);
    r.sceneEvery = 1.0f / Clamp(r.density, 0.25f, 1.3f);
    return r;
}

// ---- activities ------------------------------------------------------------------------------------------
enum Act : uint8_t
{
    // social
    A_CHAT, A_CHAT3, A_PHONE, A_GREET_FRIEND, A_GOSSIP,
    // leisure
    A_WINDOW_SHOP, A_PHOTO, A_SIGHTSEE, A_BENCH, A_QUEUE_FOOD, A_BUSKER, A_DANCE, A_JOG, A_WORKOUT, A_BEACH,
    A_PLAY_GROWNUPS,
    // work
    A_WORKERS_BREAK, A_COPS_BEAT, A_VENDOR, A_CLOSING_UP,
    // time and weather
    A_HURRY_RAIN, A_SHELTER, A_NIGHT_OWL, A_DAWN_RUN,
    // the new places
    A_TOURISTS_NEWBUILD,
    // judged not doable with what the game has (kept so the table documents them)
    A_DOG_WALK, A_KIDS_PLAY, A_UMBRELLAS,
    A_COUNT
};
static const char* const kActName[A_COUNT] = {
    "chat", "chat (three)", "phone call", "friends meet", "gossip",
    "window-shopping", "tourist photos", "sightseeing", "bench", "food queue", "busker", "street dance", "jogging",
    "workout", "beach life", "grown-ups at play",
    "workers' break", "cops on the beat", "street vendor", "closing up",
    "hurrying in the rain", "sheltering", "night owls", "dawn run",
    "tourists at the new builds",
    "dog walking", "kids playing", "umbrellas",
};

enum Route : uint8_t
{
    R_STOCK,     // the game's own system does it (we only nudge: weights, the krawlie broadcast)
    R_LIFE,      // an existing life.h scene, re-weighted and voiced (verbs proven in game)
    R_NEW,       // new, built from the same proven verbs (goto / face / anim / talk / wander)
    R_SPOTS,     // at spots.h points (the new places, and any place a data line gives)
    R_NO,        // infeasible now: the reason is in DESIGN.md §2.4
};
static const char* const kRouteName[5] = { "stock", "life", "new", "spots", "infeasible" };

struct ActDef
{
    uint8_t route;
    uint8_t minCast, maxCast;
    float   base;                  // weight before the moment
    float   part[kPartCount];      // x by part of day
    float   place[kPlaceCount];    // x by place
    float   rain;                  // x when raining (0 = never in rain)
    uint8_t needPoi;               // 0 none, else a Poi kind (below)
    bool    cops, workers;         // cast from cop / worker figures only
    float   minS, maxS;            // how long it lasts
    const char* lines;             // the voice category it speaks from (npc_lines), or "" for none
};

enum PoiKind : uint8_t { POI_NONE, POI_SHOP, POI_LANDMARK, POI_SEAT, POI_STALL, POI_STAGE, POI_SHELTER, POI_NEWBUILD, POI_COUNT };
static const char* const kPoiName[POI_COUNT] = { "-", "shop front", "landmark", "seat", "food stall", "stage", "shelter", "new build" };

//                         dawn morn midd aft  eve  nigh late                town down plaz park beac coun harb isla newt wood
static const ActDef kActDef[A_COUNT] = {
    { R_LIFE,  2, 2, 3.0f, { .5f, 1.f, 1.3f, 1.2f, 1.2f, .8f, .3f }, { 1, 1.2f, 1.2f, 1, .8f, 1, 1, 1, 1, .7f }, .4f, POI_NONE, false, false, 18, 40, "smalltalk" },
    { R_LIFE,  3, 3, 1.0f, { .3f, .8f, 1.2f, 1.2f, 1.3f, .8f, .2f }, { 1, 1, 1.4f, 1.2f, 1, .8f, 1, 1, 1, .5f }, .3f, POI_NONE, false, false, 20, 45, "smalltalk" },
    { R_LIFE,  1, 1, 2.0f, { .6f, 1.5f, 1.f, 1.f, 1.f, .7f, .3f }, { 1, 2, 1, 1, .8f, .7f, 1, 1, 1, .5f }, 1.2f, POI_NONE, false, false, 15, 35, "phone" },
    { R_NEW,   2, 2, 0.8f, { .2f, .8f, 1.2f, 1.2f, 1.5f, .8f, .2f }, { 1, 1, 1.3f, 1.3f, 1, 1, 1, 1, 1, .6f }, .3f, POI_NONE, false, false, 20, 40, "greet_friend" },
    { R_NEW,   2, 3, 0.0f, { 1, 1, 1, 1, 1, 1, 1 }, { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 }, .5f, POI_NONE, false, false, 20, 40, "gossip" },  // weight from Memory::GossipHeat
    { R_NEW,   1, 2, 1.4f, { 0, .7f, 1.3f, 1.4f, 1.1f, .3f, 0 }, { 1, 1.6f, 1.4f, .2f, .3f, .3f, .4f, .4f, 1.2f, 0 }, .2f, POI_SHOP, false, false, 12, 30, "window_shop" },
    { R_LIFE,  2, 2, 1.2f, { .2f, .8f, 1.5f, 1.5f, 1.f, .4f, 0 }, { .6f, .8f, 2.5f, 1.5f, 1.2f, .5f, 1, 2, 2.2f, 1 }, .1f, POI_LANDMARK, false, false, 15, 30, "tourist" },
    { R_NEW,   2, 3, 0.7f, { .2f, .7f, 1.4f, 1.4f, 1.2f, .3f, 0 }, { .5f, .6f, 2, 1.2f, 1.4f, .6f, 1.5f, 2, 2.2f, 1.5f }, .1f, POI_LANDMARK, false, false, 15, 30, "tourist" },
    { R_SPOTS, 1, 2, 1.0f, { .6f, 1.f, 1.4f, 1.2f, 1.f, .4f, .1f }, { 1, .8f, 1, 2, 1, 1, 1, 1, 1, 1 }, 0.f, POI_SEAT, false, false, 30, 90, "bench" },
    { R_LIFE,  3, 5, 0.8f, { .2f, 1.2f, 2.0f, 1.0f, 1.2f, .5f, .1f }, { 1, 1.5f, 1.3f, 1, 1.3f, .6f, 1, 1, 1.2f, .3f }, .5f, POI_STALL, false, false, 25, 60, "queue" },
    { R_LIFE,  1, 1, 0.7f, { 0, .5f, 1.5f, 1.5f, 1.6f, .7f, 0 }, { .7f, 1, 3, 1.5f, 1.2f, .3f, .8f, .8f, 1.5f, 0 }, .1f, POI_STAGE, false, false, 40, 90, "busker" },
    { R_LIFE,  4, 8, 0.6f, { 0, .1f, .5f, .8f, 2.5f, 2.f, .5f }, { .6f, .6f, 3, 1, 2, .3f, .6f, .6f, 1.5f, 0 }, 0.f, POI_NONE, false, false, 30, 60, "dance" },
    { R_LIFE,  2, 4, 1.0f, { 2.5f, 3.f, .6f, .8f, 1.5f, .3f, 0 }, { .8f, .5f, .6f, 3, 2, .8f, .6f, .6f, .8f, 1.2f }, .2f, POI_NONE, false, false, 60, 95, "jogger" },
    { R_LIFE,  3, 5, 0.6f, { 1, 2.5f, .8f, 1, 1, .1f, 0 }, { .5f, .3f, .8f, 3, 3, .5f, .3f, .3f, .5f, .5f }, 0.f, POI_NONE, false, false, 45, 80, "workout" },
    { R_LIFE,  3, 6, 1.0f, { 0, .6f, 1.5f, 1.5f, 1.f, .8f, 0 }, { 0, 0, 0, 0, 6, 0, 0, 0, 0, 0 }, 0.f, POI_NONE, false, false, 60, 120, "beach" },
    { R_SPOTS, 2, 3, 0.6f, { 0, .8f, 1.2f, 1.5f, .8f, 0, 0 }, { 1, .3f, 1, 2, 1.5f, .8f, .5f, .5f, 1.5f, 1 }, 0.f, POI_NONE, false, false, 30, 60, "play" },
    { R_NEW,   2, 3, 0.5f, { .5f, 1.f, 2.0f, 1.f, .3f, 0, 0 }, { 1, 1.5f, .5f, .3f, .2f, .8f, 1.5f, 1, 1.5f, .5f }, .3f, POI_NONE, false, true, 30, 60, "workers" },
    { R_LIFE,  2, 2, 1.5f, { .5f, 1, 1, 1, 1, 1, .8f }, { 1, 1.4f, 1.2f, 1, 1, .6f, 1, 1, 1, .4f }, .8f, POI_NONE, true, false, 30, 60, "cop_beat" },
    { R_SPOTS, 1, 1, 0.5f, { 0, .8f, 1.6f, 1.4f, 1.2f, .4f, 0 }, { .8f, 1.2f, 2, 1.3f, 1.5f, .5f, 1.2f, 1, 1.5f, 0 }, .3f, POI_STALL, false, false, 60, 120, "vendor" },
    { R_SPOTS, 1, 1, 0.6f, { 0, 0, 0, 0, 3.f, 1.f, 0 }, { 1, 1.4f, 1.2f, .5f, .5f, .8f, 1, .5f, 1.2f, 0 }, 1.f, POI_STALL, false, false, 20, 40, "closing" },
    { R_STOCK, 0, 0, 0.0f, { 1, 1, 1, 1, 1, 1, 1 }, { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 }, 1.f, POI_NONE, false, false, 0, 0, "rain" },     // pace only
    { R_NO,    2, 4, 0.0f, { 1, 1, 1, 1, 1, 1, 1 }, { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 }, 1.f, POI_SHELTER, false, false, 30, 90, "rain" },
    { R_NEW,   2, 3, 0.8f, { 0, 0, 0, 0, .3f, 2.f, 2.5f }, { .8f, 1, 2, .5f, 1.2f, .3f, .8f, .5f, 1, 0 }, .2f, POI_NONE, false, false, 30, 70, "night" },
    { R_LIFE,  1, 2, 0.6f, { 3.f, .5f, 0, 0, 0, 0, 0 }, { .5f, .3f, .5f, 3, 2, .8f, .5f, .5f, .8f, 1.5f }, .1f, POI_NONE, false, false, 50, 90, "jogger" },
    { R_SPOTS, 2, 4, 1.5f, { 0, .8f, 1.5f, 1.5f, 1.f, .2f, 0 }, { 0, 0, 0, 0, 0, 0, 0, 0, 3, 2 }, .3f, POI_NEWBUILD, false, false, 30, 70, "newplace" },
    { R_NO,    1, 1, 0.0f, { 1, 1, 1, 1, 1, 1, 1 }, { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 }, 0.f, POI_NONE, false, false, 0, 0, "" },
    { R_NO,    2, 4, 0.0f, { 1, 1, 1, 1, 1, 1, 1 }, { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 }, 0.f, POI_NONE, false, false, 0, 0, "" },
    { R_NO,    1, 1, 0.0f, { 1, 1, 1, 1, 1, 1, 1 }, { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 }, 0.f, POI_NONE, false, false, 0, 0, "" },
};

// ---- the world as the scheduler sees it ---------------------------------------------------------------------
enum PFlag : uint16_t { PF_COP = 1, PF_WORKER = 2, PF_FEMALE = 4, PF_BUSY = 8, PF_BEACH = 16, PF_SPOT = 32, PF_CONVERTED = 64 };
struct Person { uint64_t id = 0; V3 pos; uint16_t flags = 0; };
struct Poi { V3 pos; uint8_t kind = POI_NONE; float radius = 3.0f; uint8_t users = 0, cap = 2; };

enum Vehicle : uint8_t { VH_NONE, VH_CAR, VH_POLICE, VH_BIKE, VH_HELI, VH_PLANE, VH_TRAIN, VH_SKYRAIL, VH_DINO, VH_BOAT, VH_ODD, VH_COUNT };
enum Disguise : uint8_t { DG_POLICE, DG_ROBBER, DG_MINER, DG_ASTRONAUT, DG_FIREMAN, DG_FARMER, DG_WORKER, DG_OTHER, DG_COUNT };
static const char* const kDisguiseName[DG_COUNT] = { "police", "robber", "miner", "astronaut", "fireman", "farmer", "construction", "other" };

struct PlayerIn { bool on = false; V3 pos; float yaw = 0.0f; float speed = 0.0f; uint8_t vehicle = VH_NONE; uint8_t disguise = DG_POLICE; int wanted = 0; };

struct Ctx
{
    float   hour = 13.0f;
    Weather weather;
    uint8_t place = kTown;
    bool    mission = false;     // a story mission: nothing new starts
    bool    cutscene = false;
    PlayerIn pl[2];
};

struct Params
{
    int   maxScenes   = 4;       // at once, both players
    int   maxPeople   = 12;
    float every       = 9.0f;    // s between scene starts on average (x Rhythm::sceneEvery)
    float rest        = 50.0f;   // s an activity rests after it started
    float apartU      = 70.0f;   // players further apart than this get half the budget each
    float castMinU    = 16.0f;   // casting ring round a player (the swap happens out of your face)
    float castMaxU    = 45.0f;
    float clusterU    = 10.0f;   // a cast's members within this of its seed
    float poiU        = 30.0f;   // a POI activity's point within this of the seed
    float drivingFast = 12.0f;   // u/s: driving faster than this, only big things near the road
    bool  enable[A_COUNT];
    Params() { for (bool& e : enable) e = true; }
};

struct Plan
{
    uint8_t act = A_COUNT;
    int     member[8]; int n = 0;
    int     poi = -1;
    int     forPlayer = -1;      // whose budget pays for it
    float   seconds = 0.0f;
};

struct SchedState
{
    float wait = 5.0f;
    float rest[A_COUNT];
    int   scenes = 0, people = 0;               // running now (the caller keeps these current)
    int   scenesOf[2] = { 0, 0 }, peopleOf[2] = { 0, 0 };
    Rng   rng;
    SchedState() { for (float& r : rest) r = 0.0f; }
};

inline bool Together(const Params& p, const Ctx& c)
{
    return c.pl[0].on && c.pl[1].on && Dist2D(c.pl[0].pos, c.pl[1].pos) <= p.apartU;
}

// The weight of an activity now (0 = not now). heat: the gossip heat (Memory) for A_GOSSIP.
inline float ActWeight(const Params& p, const Ctx& c, int a, float heat)
{
    const ActDef& d = kActDef[a];
    if (d.route == R_NO || d.route == R_STOCK || !p.enable[a]) return 0.0f;
    const uint8_t part = PartOf(c.hour);
    float w = (a == A_GOSSIP ? heat * 2.5f : d.base) * d.part[part] * d.place[c.place < kPlaceCount ? c.place : 0];
    if (c.weather.rain) w *= d.rain;
    return w > 0.0f ? w : 0.0f;
}

// Pick the next activity and its cast. people: everyone castable near either player this tick;
// pois: points of interest near them. Returns false while waiting / nothing fits.
inline bool Schedule(const Params& p, SchedState& s, const Ctx& c, const Person* people, int nPeople,
                     Poi* pois, int nPoi, float heat, float dt, Plan& out)
{
    for (float& r : s.rest) if (r > 0.0f) r -= dt;
    if (c.mission || c.cutscene) { s.wait = s.wait < 4.0f ? 4.0f : s.wait; return false; }
    s.wait -= dt;
    if (s.wait > 0.0f) return false;
    const Rhythm rh = RhythmOf(c.hour, c.weather, c.place);
    s.wait = p.every * rh.sceneEvery * s.rng.R(0.6f, 1.4f);

    // whose turn: the player with the smaller share (together = one pool)
    const bool both = c.pl[0].on && c.pl[1].on;
    const bool tog = Together(p, c);
    int who = c.pl[0].on ? 0 : c.pl[1].on ? 1 : -1;
    if (who < 0) return false;
    if (both && !tog) who = (s.scenesOf[0] <= s.scenesOf[1]) ? 0 : 1;
    const int scenesCap = static_cast<int>(p.maxScenes * Clamp(rh.density, 0.3f, 1.0f) + 0.5f);
    const int peopleCap = static_cast<int>(p.maxPeople * Clamp(rh.density, 0.3f, 1.0f) + 0.5f);
    const int sCap = both && !tog ? (scenesCap + 1) / 2 : scenesCap;
    const int pCap = both && !tog ? (peopleCap + 1) / 2 : peopleCap;
    const int sNow = both && !tog ? s.scenesOf[who] : s.scenes;
    const int pNow = both && !tog ? s.peopleOf[who] : s.people;
    if (sNow >= sCap || pNow >= pCap) return false;
    const PlayerIn& P = c.pl[who];
    V3 centre = P.pos;
    if (tog) { centre.x = (c.pl[0].pos.x + c.pl[1].pos.x) * 0.5f; centre.z = (c.pl[0].pos.z + c.pl[1].pos.z) * 0.5f; }
    const bool fast = P.vehicle != VH_NONE && P.speed > p.drivingFast;

    // who could be cast: in the ring round the centre (and never right by either player)
    int cand[96]; int nc = 0;
    for (int i = 0; i < nPeople && nc < 96; ++i)
    {
        const Person& q = people[i];
        if (q.flags & PF_BUSY) continue;
        const float d = Dist2D(q.pos, centre);
        if (d < p.castMinU || d > p.castMaxU) continue;
        bool tooNear = false;
        for (int k = 0; k < 2; ++k) if (c.pl[k].on && Dist2D(q.pos, c.pl[k].pos) < p.castMinU * 0.75f) tooNear = true;
        if (!tooNear) cand[nc++] = i;
    }
    if (!nc) return false;
    int cops = 0, workers = 0, civ = 0;
    for (int k = 0; k < nc; ++k) { const uint16_t f = people[cand[k]].flags; if (f & PF_COP) ++cops; else if (f & PF_WORKER) ++workers; else ++civ; }

    float w[A_COUNT]; float tot = 0.0f;
    for (int a = 0; a < A_COUNT; ++a)
    {
        w[a] = 0.0f;
        const ActDef& d = kActDef[a];
        if (s.rest[a] > 0.0f) continue;
        float x = ActWeight(p, c, a, heat);
        if (x <= 0.0f) continue;
        const int pool = d.cops ? cops : d.workers ? workers : civ;
        if (pool < d.minCast || d.minCast > pCap - pNow) continue;
        if (d.needPoi && (d.route == R_NEW || d.route == R_SPOTS))   // a life scene takes a POI if one is free, needs none
        {
            bool any = false;
            for (int k = 0; k < nPoi; ++k) if (pois[k].kind == d.needPoi && pois[k].users < pois[k].cap && Dist2D(pois[k].pos, centre) <= p.castMaxU) any = true;
            if (!any) continue;
        }
        if (fast) x *= (a == A_DANCE || a == A_BUSKER || a == A_JOG) ? 0.6f : 0.2f;   // seen from the road
        w[a] = x; tot += x;
    }
    if (tot <= 0.0f) return false;
    float r = s.rng.F() * tot; int act = -1;
    for (int a = 0; a < A_COUNT; ++a) { if (w[a] <= 0.0f) continue; act = a; if (r < w[a]) break; r -= w[a]; }
    if (act < 0) return false;
    const ActDef& d = kActDef[act];

    // the seed: a random fitting person; for POI activities the one nearest a free POI
    out = Plan(); out.act = static_cast<uint8_t>(act); out.forPlayer = tog ? -1 : who;
    auto fits = [&](int i) { const uint16_t f = people[i].flags; return d.cops ? (f & PF_COP) != 0 : d.workers ? (f & PF_WORKER) != 0 : !(f & (PF_COP | PF_WORKER)); };
    int seed = -1;
    const bool poiRequired = d.needPoi && (d.route == R_NEW || d.route == R_SPOTS);
    if (d.needPoi)
    {
        int pickI[96], pickP[96], np = 0;                           // anyone within reach of a free POI, at random
        for (int k = 0; k < nPoi; ++k)
        {
            if (pois[k].kind != d.needPoi || pois[k].users >= pois[k].cap) continue;
            for (int j = 0; j < nc && np < 96; ++j)
                if (fits(cand[j]) && Dist2D(people[cand[j]].pos, pois[k].pos) <= p.poiU) { pickI[np] = cand[j]; pickP[np] = k; ++np; }
        }
        if (np) { const int x = s.rng.I(np); seed = pickI[x]; out.poi = pickP[x]; }
        if (seed < 0 && poiRequired) { s.rest[act] = 5.0f; return false; }
    }
    if (seed < 0)
    {
        int tries = 0;
        do { seed = cand[s.rng.I(nc)]; } while (!fits(seed) && ++tries < 24);
        if (!fits(seed)) return false;
    }
    out.member[out.n++] = seed;
    const int want = d.minCast + s.rng.I(d.maxCast - d.minCast + 1);
    while (out.n < want && out.n < 8)
    {
        int best = -1; float bd = p.clusterU;
        for (int j = 0; j < nc; ++j)
        {
            const int i = cand[j]; bool dup = false;
            for (int m = 0; m < out.n; ++m) if (out.member[m] == i) dup = true;
            if (dup || !fits(i)) continue;
            const float dd = Dist2D(people[i].pos, people[seed].pos);
            if (dd < bd) { bd = dd; best = i; }
        }
        if (best < 0) break;
        out.member[out.n++] = best;
    }
    if (out.n < d.minCast) { s.rest[act] = 8.0f; return false; }
    if (out.poi >= 0) ++pois[out.poi].users;
    out.seconds = s.rng.R(d.minS, d.maxS);
    s.rest[act] = p.rest;
    return true;
}

// ---- a conversation's turn-taking ----------------------------------------------------------------------------
// The lines are an exchange from the voice table (npc_voice_model.h) - or none (gestures only).
// Mood sets the rhythm: chatty = quick turns, slow = long pauses, cross = quick and loud.
enum Mood : uint8_t { M_CHATTY, M_SLOW, M_CROSS, M_EXCITED };
enum TalkDo : uint8_t { TD_NONE, TD_FACE, TD_SAY, TD_LISTEN, TD_REACT, TD_GOODBYE, TD_LEAVE, TD_LOOK_EVENT, TD_RESUME, TD_DONE };
static const char* const kTalkDoName[] = { "none", "face", "say", "listen", "react", "goodbye", "leave", "look at event", "resume", "done" };

struct TalkStep { uint8_t what = TD_NONE; int who = -1; int turn = -1; float seconds = 0.0f; };

struct Talk
{
    int     n = 2;                    // people: 0 and 1 speak, 2 listens
    int     turns = 0;                // lines in the exchange (0 = gestures only, `silentTurns` of them)
    float   lineS[6] = { 0 };         // each line's length
    uint8_t mood = M_CHATTY;
    int     first = 0;                // who says turn 0 (0 or 1: the exchange may be acted swapped)
    // state
    uint8_t phase = 0;                // 0 facing, 1 turn speaking, 2 gap, 3 goodbye, 4 leaving, 5 done, 6 interrupted
    int     turn = 0;
    float   t = 0.0f, phaseLen = 0.0f;
    bool    reacted = false;          // the listener's mid-line reaction this turn
    int     silentTurns = 4;
    float   interruptS = 0.0f;
    bool    resumed = false;
    bool    started = false;
};

inline float GapOf(uint8_t mood, Rng& r)
{
    switch (mood)
    {
    case M_SLOW: return r.R(0.6f, 1.3f);
    case M_CROSS: return r.R(0.1f, 0.35f);
    case M_EXCITED: return r.R(0.05f, 0.3f);
    default: return r.R(0.25f, 0.8f);
    }
}

inline int SpeakerOf(const Talk& k, int turn) { return ((turn & 1) == 0) ? k.first : 1 - k.first; }

// Advance; writes up to 3 steps (the orders for this frame). Returns how many.
inline int StepTalk(Talk& k, float dt, Rng& r, TalkStep* out)
{
    int n = 0;
    auto put = [&](uint8_t what, int who, int turn, float s) { if (n < 3) { out[n].what = what; out[n].who = who; out[n].turn = turn; out[n].seconds = s; ++n; } };
    const int total = k.turns > 0 ? k.turns : k.silentTurns;
    k.t += dt;
    switch (k.phase)
    {
    case 0:   // face each other (0.6 s), then the first turn
        if (!k.started) { k.started = true; put(TD_FACE, -1, -1, 0.6f); }
        if (k.t >= 0.6f) { k.phase = 1; k.t = 0.0f; k.turn = 0; k.reacted = false;
            k.phaseLen = k.turns > 0 ? k.lineS[0] : r.R(1.8f, 3.2f);
            put(TD_SAY, SpeakerOf(k, 0), 0, k.phaseLen); put(TD_LISTEN, 1 - SpeakerOf(k, 0), 0, k.phaseLen);
            if (k.n > 2) put(TD_LISTEN, 2, 0, k.phaseLen); }
        break;
    case 1:   // a line is being said
        if (!k.reacted && k.t >= k.phaseLen * 0.6f) { k.reacted = true; if (r.F() < 0.45f) put(TD_REACT, 1 - SpeakerOf(k, k.turn), k.turn, 0.8f); }
        if (k.t >= k.phaseLen)
        {
            k.t = 0.0f;
            if (k.turn + 1 >= total) { k.phase = 3; k.phaseLen = 1.2f; put(TD_GOODBYE, -1, -1, 1.2f); }
            else { k.phase = 2; k.phaseLen = GapOf(k.mood, r); }
        }
        break;
    case 2:   // the pause between turns
        if (k.t >= k.phaseLen)
        {
            ++k.turn; k.t = 0.0f; k.reacted = false; k.phase = 1;
            k.phaseLen = k.turns > 0 ? k.lineS[k.turn < 6 ? k.turn : 5] : r.R(1.8f, 3.2f);
            put(TD_SAY, SpeakerOf(k, k.turn), k.turn, k.phaseLen); put(TD_LISTEN, 1 - SpeakerOf(k, k.turn), k.turn, k.phaseLen);
        }
        break;
    case 3:   // goodbye wave
        if (k.t >= k.phaseLen) { k.phase = 4; k.t = 0.0f; k.phaseLen = 0.5f; put(TD_LEAVE, -1, -1, 0.0f); }
        break;
    case 4:
        if (k.t >= k.phaseLen) { k.phase = 5; put(TD_DONE, -1, -1, 0.0f); }
        break;
    case 6:   // interrupted: both look at what happened; resume once (if a line remains), else leave
        if (k.t >= k.interruptS)
        {
            k.t = 0.0f;
            if (!k.resumed && k.turn + 1 < total) { k.resumed = true; k.phase = 2; k.phaseLen = 0.4f; put(TD_RESUME, SpeakerOf(k, k.turn + 1), k.turn + 1, 0.0f); }
            else { k.phase = 3; k.phaseLen = 1.0f; put(TD_GOODBYE, -1, -1, 1.0f); }
        }
        break;
    default: break;
    }
    return n;
}

// An event near the talkers: they stop and look (the line playing is let finish by the audio side).
inline void Interrupt(Talk& k, float lookS)
{
    if (k.phase >= 3 && k.phase != 6) return;
    k.phase = 6; k.t = 0.0f; k.interruptS = lookS;
}

// ---- reactions ---------------------------------------------------------------------------------------------------
enum Ev : uint8_t { EV_STUNT, EV_CRASH, EV_NEAR_MISS, EV_BUMP, EV_CHASE, EV_SUPERBUILD, EV_ODD_VEHICLE, EV_DISGUISE, EV_PUNCH, EV_WRECK, EV_COUNT };
static const char* const kEvName[EV_COUNT] = { "stunt", "crash", "near miss", "bump", "police chase", "super build", "odd vehicle", "disguise", "punch", "wreck" };

enum Re : uint8_t { RE_NONE, RE_FLINCH, RE_STEP_ASIDE, RE_LOOK, RE_POINT, RE_FILM, RE_CLAP, RE_CHEER, RE_GATHER, RE_GLARE, RE_WAVE, RE_SHAKE_HEAD, RE_COUNT };
static const char* const kReName[RE_COUNT] = { "-", "flinch", "step aside", "look", "point", "film", "clap", "cheer", "gather", "glare", "wave", "shake head" };
// The anim (life.h names, all HasAnim-checked there) for each: the first that exists plays.
static const char* const kReAnim[RE_COUNT][3] = {
    { "", "", "" }, { "Trip", "Shrug", "" }, { "", "", "" }, { "LookAtObject", "Ponder", "" }, { "point", "Point2", "point3" },
    { "TakePhoto", "TakePhoto2", "point" }, { "Cheer_Clap", "Celebration_Low", "" }, { "Celebration", "Cheer_Clap", "" },
    { "", "", "" }, { "Talk_Angry", "Shrug", "point" }, { "Wave", "Walk_wave", "" }, { "Shrug", "Ponder", "" },
};

struct EvRule
{
    float radius;            // who notices
    float flinchU;           // nearer than this: flinch / step aside instead
    int   maxReact;          // people (bark speaker included)
    float chance;            // each person in range, before distance falloff
    float globalCd;          // s between two reactions to this kind of event
    float personCd;          // s before the same person reacts to anything again
    uint8_t reNear, reMid, reFar;  // reaction by distance band (not near/far: windows.h defines those)
    float gatherSeverity;    // >= this severity: a curiosity crowd may form
    const char* bark;        // the voice category
};
static const EvRule kEvRule[EV_COUNT] = {
    //  r    flinch max  chance gcd  pcd  near           mid        far        gather bark
    { 30.f, 0.f, 4, 0.55f, 12.f, 40.f, RE_CHEER,      RE_POINT,  RE_LOOK,   2.0f, "react_stunt" },
    { 22.f, 6.f, 4, 0.75f, 6.f,  30.f, RE_FLINCH,     RE_POINT,  RE_LOOK,   1.0f, "react_crash" },
    {  9.f, 4.f, 2, 0.9f,  5.f,  20.f, RE_STEP_ASIDE, RE_SHAKE_HEAD, RE_LOOK, 9.0f, "react_near_miss" },
    {  3.f, 0.f, 1, 1.0f,  0.f,  25.f, RE_GLARE,      RE_GLARE,  RE_GLARE,  9.0f, "react_bumped" },
    { 26.f, 5.f, 4, 0.6f,  10.f, 40.f, RE_STEP_ASIDE, RE_POINT,  RE_LOOK,   9.0f, "react_chase" },
    { 40.f, 0.f, 6, 0.85f, 30.f, 60.f, RE_CHEER,      RE_CLAP,   RE_CLAP,   1.5f, "react_superbuild" },
    { 24.f, 0.f, 3, 0.5f,  25.f, 90.f, RE_POINT,      RE_FILM,   RE_LOOK,   2.5f, "react_vehicle" },
    {  8.f, 0.f, 1, 0.35f, 20.f, 120.f, RE_LOOK,      RE_LOOK,   RE_LOOK,   9.0f, "react_disguise" },
    { 12.f, 3.f, 3, 0.8f,  8.f,  30.f, RE_STEP_ASIDE, RE_SHAKE_HEAD, RE_LOOK, 9.0f, "react_punch" },
    { 26.f, 7.f, 5, 0.8f,  10.f, 30.f, RE_FLINCH,     RE_FILM,   RE_POINT,  1.0f, "react_crash" },
};

struct Event { uint8_t kind = EV_COUNT; V3 pos; int player = 0; float severity = 1.0f; uint64_t target = 0; uint8_t sub = 0; };
struct Reaction { int who = -1; uint8_t re = RE_NONE; float delay = 0.0f; float seconds = 0.0f; bool barks = false; bool echo = false; };

struct ReactState
{
    float lastKind[EV_COUNT];
    uint64_t pid[128]; float pUntil[128]; int pn = 0;   // per-person cooldowns (a ring)
    int   perMinute = 0; float minuteT = 0.0f;
    ReactState() { for (float& t : lastKind) t = -1e9f; for (int i = 0; i < 128; ++i) { pid[i] = 0; pUntil[i] = -1e9f; } }
};
inline bool PersonFree(const ReactState& s, uint64_t id, float now)
{
    for (int i = 0; i < 128; ++i) if (s.pid[i] == id && id) return now >= s.pUntil[i];
    return true;
}
inline void PersonBusy(ReactState& s, uint64_t id, float until)
{
    for (int i = 0; i < 128; ++i) if (s.pid[i] == id) { s.pUntil[i] = until; return; }
    s.pid[s.pn] = id; s.pUntil[s.pn] = until; s.pn = (s.pn + 1) & 127;
}

// Who reacts and how. people: the crowd figures and our characters near the event (any player).
// Nearer people notice first (a wave of heads turning: delay grows with distance plus a jitter).
inline int PlanReaction(ReactState& s, const Event& e, const Person* people, int nPeople, float now, Rng& r,
                        Reaction* out, int cap, int maxPerMinute = 6)
{
    if (e.kind >= EV_COUNT) return 0;
    const EvRule& R = kEvRule[e.kind];
    if (now - s.lastKind[e.kind] < R.globalCd) return 0;
    if (now - s.minuteT >= 60.0f) { s.minuteT = now; s.perMinute = 0; }
    if (s.perMinute >= maxPerMinute && e.kind != EV_BUMP) return 0;
    struct C { int i; float d; } c[64]; int nc = 0;
    for (int i = 0; i < nPeople && nc < 64; ++i)
    {
        const Person& q = people[i];
        if (q.flags & PF_BUSY) continue;
        if (e.kind == EV_BUMP && q.id != e.target) continue;          // a bump: only the one bumped
        const float d = Dist(q.pos, e.pos);
        if (d > R.radius || !PersonFree(s, q.id, now)) continue;
        c[nc].i = i; c[nc].d = d; ++nc;
    }
    for (int a = 1; a < nc; ++a) { C t = c[a]; int b = a - 1; while (b >= 0 && c[b].d > t.d) { c[b + 1] = c[b]; --b; } c[b + 1] = t; }
    int n = 0; bool barkGiven = false, echoGiven = false;
    const float sev = Clamp(e.severity, 0.2f, 3.0f);
    for (int k = 0; k < nc && n < cap && n < R.maxReact; ++k)
    {
        const float d = c[k].d;
        const float p = R.chance * Clamp(1.2f - d / R.radius, 0.15f, 1.0f) * Clamp(sev, 0.5f, 1.5f);
        if (e.kind != EV_BUMP && r.F() > p) continue;
        Reaction& o = out[n];
        o = Reaction(); o.who = c[k].i;
        const float band = d / R.radius;
        o.re = (R.flinchU > 0.0f && d < R.flinchU) ? R.reNear : band < 0.45f ? R.reMid : R.reFar;
        if (R.flinchU <= 0.0f && band < 0.3f) o.re = R.reNear;
        if (e.kind == EV_STUNT && sev < 1.0f && o.re == RE_CHEER) o.re = RE_POINT;      // a small jump: a point, not a cheer
        o.delay = d * 0.05f + r.R(0.05f, 0.45f);                                      // sound and attention travel
        o.seconds = o.re == RE_FLINCH || o.re == RE_STEP_ASIDE ? r.R(1.0f, 1.8f) : r.R(2.5f, 5.0f);
        if (!barkGiven && d <= 14.0f) { o.barks = true; barkGiven = true; }
        else if (barkGiven && !echoGiven && d <= 14.0f && r.F() < 0.35f) { o.echo = true; echoGiven = true; o.delay += 1.6f; }
        PersonBusy(s, people[c[k].i].id, now + R.personCd);
        ++n;
    }
    if (n) { s.lastKind[e.kind] = now; ++s.perMinute; }
    return n;
}

// ---- curiosity crowds -------------------------------------------------------------------------------------------
struct Crowd
{
    bool    on = false;
    V3      centre;
    float   interest = 0.0f;   // decays; a new event near the centre tops it up
    float   age = 0.0f;
    uint64_t member[8]; int n = 0;
    float   slotAng[8];
    int     wantN = 0;
    float   joinT = 0.0f;
    int     leaving = 0;
};

struct CrowdParams { float ringU = 3.6f; float ringJitter = 0.8f; float decay = 0.08f; float joinEvery = 1.2f; float maxAge = 45.0f; float playerLeftU = 50.0f; };

inline void StartCrowd(Crowd& c, const V3& at, float severity, Rng& r)
{
    c = Crowd(); c.on = true; c.centre = at; c.interest = Clamp(severity, 0.5f, 3.0f);
    c.wantN = 2 + static_cast<int>(Clamp(severity, 0.0f, 3.0f) * 1.3f + r.F());
    if (c.wantN > 8) c.wantN = 8;
    const float base = r.R(0.0f, 6.2831853f);
    for (int k = 0; k < 8; ++k) c.slotAng[k] = base + k * (6.2831853f / 8.0f) * 1.0f + r.R(-0.2f, 0.2f);
}
// Where member k stands (on the ring, facing the centre).
inline V3 SlotOf(const CrowdParams& p, const Crowd& c, int k, float jitter)
{
    const float rr = p.ringU + jitter * p.ringJitter;
    return Mk(c.centre.x + std::sin(c.slotAng[k & 7]) * rr, c.centre.y, c.centre.z + std::cos(c.slotAng[k & 7]) * rr);
}
inline void TopUp(Crowd& c, float severity) { c.interest = Clamp(c.interest + severity * 0.7f, 0.0f, 3.0f); if (c.wantN < 8 && severity > 1.0f) ++c.wantN; }
// One tick: returns +1 when someone should join now (the caller picks the nearest free person),
// -1 when the last one should leave (one at a time, then the crowd is off), 0 otherwise.
inline int StepCrowd(const CrowdParams& p, Crowd& c, float dt, float nearestPlayerU)
{
    if (!c.on) return 0;
    c.age += dt;
    c.interest -= p.decay * dt;
    const bool over = c.interest <= 0.0f || c.age > p.maxAge || nearestPlayerU > p.playerLeftU;
    if (over)
    {
        if (c.n <= 0) { c.on = false; return 0; }
        c.joinT += dt;
        if (c.joinT >= 0.9f) { c.joinT = 0.0f; ++c.leaving; return -1; }
        return 0;
    }
    c.joinT += dt;
    if (c.n < c.wantN && c.joinT >= p.joinEvery) { c.joinT = 0.0f; return 1; }
    return 0;
}
inline void Joined(Crowd& c, uint64_t id) { if (c.n < 8) c.member[c.n++] = id; }
inline uint64_t Leave(Crowd& c) { if (c.n <= 0) return 0; const uint64_t id = c.member[--c.n]; if (!c.n) c.on = false; return id; }

// ---- memory: grudges and gossip ---------------------------------------------------------------------------------
struct Grudge { uint64_t id = 0; int player = -1; float until = -1e9f; uint8_t level = 0; float lastBark = -1e9f; int barks = 0; };
struct Memory
{
    Grudge g[32];
    float  heat[EV_COUNT];          // how much each kind of event is being talked about (gossip)
    float  heatT = 0.0f;
    Memory() { for (float& h : heat) h = 0.0f; }
};
struct MemParams { float glareS = 30.0f; float glare2S = 60.0f; float repeatWindowS = 90.0f; float glareNearU = 7.0f; float barkEveryS = 15.0f; int maxBarks = 2; float halfLifeS = 420.0f; };

inline Grudge* FindGrudge(Memory& m, uint64_t id) { for (Grudge& x : m.g) if (x.id == id && id) return &x; return nullptr; }
// You bumped them (or knocked them over, or splashed them ...). A second time within the window: crosser.
inline const Grudge& Bumped(Memory& m, const MemParams& p, uint64_t id, int player, float now)
{
    Grudge* x = FindGrudge(m, id);
    if (x && x->player == player && now < x->until + p.repeatWindowS) { x->level = x->level < 3 ? x->level + 1 : 3; x->until = now + p.glare2S; return *x; }
    if (!x) { x = &m.g[0]; for (Grudge& y : m.g) if (y.until < x->until) x = &y; }
    *x = Grudge(); x->id = id; x->player = player; x->until = now + p.glareS; x->level = 1;
    return *x;
}
// Each tick for someone with a grudge: should they glare now (the player is near and they're still
// cross), and say something? out: 0 nothing, 1 glare, 2 glare + bark.
inline int GrudgeStep(Memory& m, const MemParams& p, uint64_t id, float distToPlayer, float now)
{
    Grudge* x = FindGrudge(m, id);
    if (!x) return 0;
    if (now >= x->until) { x->id = 0; return 0; }                  // forgiven
    if (distToPlayer > p.glareNearU) return 0;
    if (x->barks < p.maxBarks && now - x->lastBark >= p.barkEveryS) { x->lastBark = now; ++x->barks; return 2; }
    return 1;
}
inline int Grudges(const Memory& m, float now) { int n = 0; for (const Grudge& x : m.g) n += x.id && now < x.until; return n; }

// Gossip: an event adds heat to its kind; heat halves every halfLifeS; the hottest kind is the
// gossip topic and its heat the gossip activity's weight.
inline void Heard(Memory& m, uint8_t kind, float severity) { if (kind < EV_COUNT) m.heat[kind] = Clamp(m.heat[kind] + severity, 0.0f, 5.0f); }
inline void Cool(Memory& m, const MemParams& p, float dt) { const float f = std::pow(0.5f, dt / p.halfLifeS); for (float& h : m.heat) h *= f; }
inline int GossipTopic(const Memory& m, float* heatOut)
{
    int best = -1; float h = 0.25f;
    for (int k = 0; k < EV_COUNT; ++k)
        if (k != EV_BUMP && k != EV_DISGUISE && m.heat[k] > h) { h = m.heat[k]; best = k; }
    if (heatOut) *heatOut = best >= 0 ? h : 0.0f;
    return best;
}

// ---- co-op ---------------------------------------------------------------------------------------------------------
// The player a line or a look is aimed at: the one who caused it if near, else the nearer one.
inline int AddressOf(const Ctx& c, const V3& from, int causer, float nearU = 18.0f)
{
    if (causer >= 0 && causer < 2 && c.pl[causer].on && Dist(c.pl[causer].pos, from) <= nearU) return causer;
    int best = -1; float bd = 1e9f;
    for (int k = 0; k < 2; ++k) if (c.pl[k].on) { const float d = Dist(c.pl[k].pos, from); if (d < bd) { bd = d; best = k; } }
    return best;
}

// Greetings and comments to a player on foot: not more than one every `everyS` per player, only
// when walking (not running past), and the remark fits what they wear / drive.
struct GreetState { float last[2] = { -1e9f, -1e9f }; };
inline bool MayGreet(const GreetState& g, const Ctx& c, int player, float now, float everyS = 40.0f)
{
    if (player < 0 || player > 1 || !c.pl[player].on || c.mission || c.cutscene) return false;
    if (c.pl[player].wanted > 0) return false;
    if (now - g.last[player] < everyS) return false;
    if (c.pl[player].vehicle == VH_NONE && c.pl[player].speed > 4.5f) return false;   // sprinting by
    return true;
}

}  // namespace npc
