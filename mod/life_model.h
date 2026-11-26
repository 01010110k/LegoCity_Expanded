// =============================================================================================
//  life_model.h - the ambient life director's choices (Notes\40). Pure: the moment in, the next
//  street scene out, so test\life_harness.cpp runs it as is. life.h casts and plays the scenes.
//
//  2026-10-25: an overhaul of the NPCs' routines and behaviours in general, towards GTA-like
//  street life: beach parties, jogging groups, occasional fights, people having
//  conversations, etc. Every few seconds, while there's room in the budget (scenes, people), one
//  scene is drawn by weight. The weights follow the place (the game's 23 districts), the hour (the
//  sun's phase), and you (on foot, driving fast). A scene type rests a while after it ran. Nothing
//  starts during story missions or while you're wanted.
// =============================================================================================
#pragma once
#include <cstdint>

namespace life {

enum Scene : uint8_t
{
    kChat, kPhone, kPhoto, kCheerUp, kQueue, kJog, kWorkout, kDance, kBeach, kBusker, kCops, kFight,
    kFans, kGag, kOnlookers, kThief, kSceneCount
};

static const char* const kSceneName[kSceneCount] = {
    "conversation", "phone call", "tourist photos", "cheer-up", "queue", "jogging group", "workout class",
    "street dance", "beach life", "busker", "cops on a break", "argument", "fans", "gag", "onlookers", "pickpocket",
};

struct SceneDef
{
    float   weight;            // base
    uint8_t minCast, maxCast;  // people (the onlookers some scenes gather come on top)
    bool    cops;              // cast from cop krawlies
    bool    youOnFoot;         // only while you're on foot (it's played right by you)
};

static const SceneDef kDefs[kSceneCount] = {
    { 3.0f, 2, 3, false, false },   // conversation
    { 2.0f, 1, 1, false, false },   // phone call
    { 1.2f, 2, 2, false, false },   // tourist photos
    { 0.5f, 2, 2, false, false },   // cheer-up
    { 0.8f, 3, 5, false, false },   // queue
    { 1.0f, 2, 4, false, false },   // jogging group
    { 0.6f, 3, 5, false, false },   // workout class
    { 0.6f, 4, 8, false, false },   // street dance
    { 0.0f, 3, 6, false, false },   // beach life (paradise sands only)
    { 0.7f, 1, 1, false, false },   // busker (+ an audience)
    { 0.0f, 2, 2, true,  false },   // cops on a break (cops near only)
    { 0.35f, 2, 2, false, false },  // argument (a third become fights; + onlookers)
    { 0.5f, 1, 2, false, true  },   // fans of Chase McCain
    { 0.6f, 1, 1, false, false },   // gag
    { 0.0f, 2, 3, false, false },   // onlookers (never drawn: your crashes call them, life.h)
    { 0.4f, 2, 2, false, false },   // pickpocket: a thief, a victim (+ a cop from the crowd if one's near)
};

// The game's district index (the radio's kDistrictFill order).
enum District { kParadiseSands = 11, kCrescentPark = 13, kPagoda = 14, kDowntown = 15, kKingsCourt = 16,
                kFestivalSquare = 17, kFresco = 18, kBrightLights = 19, kUptown = 21, kLibertyIsland = 7,
                kAlbatrossIsland = 8, kApolloIsland = 9 };

enum DayPart : uint8_t { kUnknown, kMorning, kMidday, kEvening, kNight };

// The sun's phase (lights.h: 0 sunrise, pi/2 noon, pi sunset) as a part of the day.
inline uint8_t PartOfDay(float sunPhase)
{
    if (sunPhase < 0.0f) return kUnknown;
    const float h = sunPhase / 6.2831853f * 24.0f;   // hours since sunrise (~06:00)
    if (h < 4.0f) return kMorning;                    // 06-10
    if (h < 10.0f) return kMidday;                    // 10-16
    if (h < 15.0f) return kEvening;                   // 16-21
    return kNight;                                    // 21-06
}

struct Ctx
{
    int     district = -1;     // -1 = not known
    uint8_t part = kUnknown;   // PartOfDay
    bool    onFoot = true;
    float   speed = 0.0f;      // u/s, yours (driving)
    bool    blocked = false;   // a story mission, or you're wanted
    int     people = 0;        // citizens in casting range
    int     cops = 0;          // cop krawlies in casting range
};

struct Params
{
    int   maxScenes = 3;
    int   maxPeople = 10;
    float every     = 10.0f;   // s between scene starts, on average
    float rest      = 45.0f;   // s a scene type rests after it ran
    float fights    = 1.0f;    // x the arguments' weight (0 = none)
    // [round3] NPC overhaul stage 2 (npc_glue.h Tune): x each scene's weight by the hour / weather / place (1 = as before)
    float mult[kSceneCount] = { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 };
};

struct State
{
    float    wait;             // s to the next draw
    float    rest[kSceneCount];
    uint32_t rng;
};

inline void Reset(State& s, uint32_t seed = 0x9E3779B9u)
{
    s.wait = 6.0f; s.rng = seed ? seed : 1u;
    for (float& r : s.rest) r = 0.0f;
}

inline float Rand(State& s)
{
    s.rng ^= s.rng << 13; s.rng ^= s.rng >> 17; s.rng ^= s.rng << 5;
    return (s.rng & 0xFFFFFF) / 16777216.0f;
}

// How much this scene fits the moment (0 = not now).
inline float Weight(const Params& p, int sc, const Ctx& c)
{
    const SceneDef& d = kDefs[sc];
    float w = d.weight;
    const int dist = c.district;
    const bool plaza = dist == kFestivalSquare || dist == kBrightLights;
    const bool park = dist == kCrescentPark;
    const bool beach = dist == kParadiseSands;
    const bool sights = plaza || dist == kPagoda || dist == kLibertyIsland || dist == kApolloIsland;
    switch (sc)
    {
    case kBeach:   w = beach ? 6.0f : 0.0f; break;
    case kCops:    w = c.cops >= 2 ? 2.0f : 0.0f; break;
    case kJog:     w *= park ? 3.0f : beach ? 2.0f : 1.0f; break;
    case kWorkout: w *= park || beach ? 3.0f : 0.5f; break;
    case kDance:   w *= plaza ? 3.0f : beach ? 2.0f : 1.0f; break;
    case kBusker:  w *= plaza || dist == kPagoda ? 3.0f : 1.0f; break;
    case kPhoto:   w *= sights ? 3.0f : 1.0f; break;
    case kPhone:   w *= dist == kDowntown || dist == kKingsCourt ? 2.0f : 1.0f; break;
    case kQueue:   w *= dist == kDowntown || dist == kUptown || dist == kFresco ? 1.5f : 1.0f; break;
    case kFight:   w *= p.fights; break;
    case kThief:   w *= dist == kDowntown || dist == kUptown || dist == kFresco || plaza ? 2.0f : 1.0f; break;
    case kOnlookers: return 0.0f;
    default: break;
    }
    switch (c.part)                                   // the daily rhythm
    {
    case kMorning:
        if (sc == kJog) w *= 3.0f; else if (sc == kWorkout) w *= 2.5f; else if (sc == kPhone) w *= 1.5f;
        else if (sc == kDance) w *= 0.2f;
        break;
    case kMidday:
        if (sc == kPhoto || sc == kBusker) w *= 2.0f; else if (sc == kQueue || sc == kChat) w *= 1.3f;
        break;
    case kEvening:
        if (sc == kDance) w *= 2.5f; else if (sc == kJog) w *= 1.5f; else if (sc == kBusker) w *= 1.5f;
        break;
    case kNight:
        if (sc == kDance) w *= 2.0f; else if (sc == kFight) w *= 1.5f; else if (sc == kThief) w *= 0.6f;
        else if (sc == kJog || sc == kWorkout || sc == kPhoto || sc == kQueue) w *= 0.15f;
        break;
    default: break;
    }
    if (d.youOnFoot && !c.onFoot) w = 0.0f;
    const int pool = d.cops ? c.cops : c.people;
    if (pool < d.minCast) w = 0.0f;                  // nobody to cast
    return w;
}

// Once a tick. Returns the scene to start now (-1 = none) and how many to cast. The glue reports a
// cast that fell through with Failed(), which rests that scene a little and redraws soon.
inline int Pick(const Params& p, State& s, const Ctx& c, float dt, int liveScenes, int livePeople, int* cast)
{
    const float t = dt > 0.0f && dt < 1.0f ? dt : 0.0f;
    for (float& r : s.rest) if (r > 0.0f) r -= t;
    s.wait -= t;
    if (c.blocked) { if (s.wait < 4.0f) s.wait = 4.0f; return -1; }   // a moment after it clears
    if (s.wait > 0.0f || liveScenes >= p.maxScenes) return -1;
    // driving fast, scenes go by: fewer of them
    const float pace = c.onFoot ? 1.0f : c.speed > 12.0f ? 0.35f : 0.7f;
    float w[kSceneCount], total = 0.0f;
    for (int i = 0; i < kSceneCount; ++i)
    {
        w[i] = s.rest[i] > 0.0f || livePeople + kDefs[i].minCast > p.maxPeople ? 0.0f : Weight(p, i, c) * p.mult[i];   // [round3] * mult
        total += w[i];
    }
    s.wait = p.every * (0.5f + Rand(s)) / pace;
    if (total <= 0.0f) return -1;
    float r = Rand(s) * total;
    int sc = kSceneCount - 1;
    for (int i = 0; i < kSceneCount; ++i) { if (r < w[i]) { sc = i; break; } r -= w[i]; }
    const SceneDef& d = kDefs[sc];
    int n = d.minCast + static_cast<int>(Rand(s) * (d.maxCast - d.minCast + 1));
    if (n > d.maxCast) n = d.maxCast;
    if (livePeople + n > p.maxPeople) n = p.maxPeople - livePeople;
    const int pool = d.cops ? c.cops : c.people;
    if (n > pool) n = pool;
    if (n < d.minCast) return -1;
    s.rest[sc] = p.rest;
    *cast = n;
    return sc;
}

inline void Failed(State& s, int sc)
{
    if (sc >= 0 && sc < kSceneCount) s.rest[sc] = 15.0f;
    if (s.wait > 2.0f) s.wait = 2.0f;
}

// ---------------------------------------------------------------------------------------------
//  The krawlie dance party (2026-11-15, Notes\46 §1 item 1: the cut PumpUpTheKrawlieJam). life.h
//  sends the game's own "DanceDance" hook to the crowd round a point; these are its two triggers.
//
//  PadHold: a controller chord held for `need` s fires once; it re-arms only once released.
//  Ambient: while a player is by the beach party (there), no mission / wanted level (blocked), the
//  party starts at random, on average once every `every` s spent there (0 = never), never sooner
//  than `rest` s after the last one. `rnd` is a uniform 0..1 draw per call.
// ---------------------------------------------------------------------------------------------
struct PadHold { float held; bool fired; };
inline bool PadHoldTick(PadHold& h, bool down, float dt, float need)
{
    if (!down) { h.held = 0.0f; h.fired = false; return false; }
    h.held += dt;
    if (h.fired || h.held < need) return false;
    h.fired = true;
    return true;
}

struct PartyMem { float since; };          // s since the last party (starts "long ago")
inline void PartyReset(PartyMem& m) { m.since = 1e6f; }
inline bool PartyAmbient(PartyMem& m, float dt, bool there, bool blocked, float every, float rest, float rnd)
{
    m.since += dt;
    if (!there || blocked || every <= 0.0f || m.since < rest) return false;
    if (rnd >= dt / every) return false;
    m.since = 0.0f;
    return true;
}
inline void PartyHappened(PartyMem& m) { m.since = 0.0f; }   // the hotkey's party counts too

} // namespace life
