// =============================================================================================
//  spots_model.h - life at the new places (2026-11-18, Notes\51): the data ("life spots") and every
//  decision, pure - the same contract as sites_model.h / dino_model.h: plain facts in, the actions
//  through the caller's World, so test\spots_harness.cpp runs it as is. spots.h is the engine side.
//
//  The goal: more life at the new places as part of a beauty and environment pass - people and
//  cars there, doing their thing (trains too). The new land (Port Atlas, Woods West, the brick sets)
//  has collision but no pedestrian network, no lane graph and no crowd zones, so the stock crowd and
//  traffic never appear there. This puts people and cars there from DATA:
//
//      spot,  <name>, <cast>, <people>, <x>, <y>, <z>, <radius>
//      pt,    <x>, <y>, <z>, <act>, <group> [, <look x>, <look z>]      (belongs to the spot above)
//      car,   <model>, <x>, <y>, <z>, <heading> [, <label>]             (a parked car: a quiet [sites] stand)
//      route, <name>, <model>, <speed>, <loop 0|1>
//      wp,    <x>, <y>, <z>                                             (belongs to the route above)
//
//  One record a line, fields separated by commas, '#' or ';' starts a comment. The built-in table
//  (spots_table.h) is this same text; a file lcudrive_spots.txt beside lcudrive.ini replaces it.
//  Drive\tools\make_spots.py writes both (and turns a sites.json into this format).
//
//    spot   a place with people. cast = who (CastName); people = how many at most; x y z = its
//           centre; radius = how far from the centre its people may be before they count as strayed.
//    pt     a place to stand. act = what is done there (ActName); group > 0 = may walk in a straight
//           line to any other point of the same spot with the same group (the tool has checked the
//           ground between them); 0 = stays. look = what is looked at (default: the spot's centre).
//    car    parked scenery: handed to sites.h as an extra stand (no map pin), so it follows every
//           rule of the stands (appears when a player is near, never on top of anyone, held 4 s
//           after a teleport, never removed with a player in or beside it).
//    route  a car that DRIVES: waypoints followed in order by a steering controller (DriveStep);
//           loop 1 = after the last waypoint the first again, 0 = it fades at the end.
//
//  People: each of [spots] max_people slots is a Person. Idle slots are filled one at a time
//  (PickSpawn: the nearest spot to a player that is within `near` and under its count, a free point
//  of it at least `min_spawn` from every player). A person stands at his point facing what is to
//  be looked at, makes a gesture of his point's act every few seconds, and now and then walks to
//  another point of his group. Nobody within `far` -> removed. Strayed (off the spot, fallen) ->
//  removed once no player is within `keep`. A story mission, lcudrive off, [spots] enable = 0 ->
//  the same removal, nothing new. NOTHING is spawned, removed or ordered for `settle` seconds after
//  a player teleports (Facts.settling - the 2026-11-16 crash, Notes 48 §4.2).
// =============================================================================================
#pragma once
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include "sites_model.h"

namespace spm {

enum : int { kMaxSpots = 96, kMaxPts = 512,   // [round3] was 40 / 240: env1 + fresco + marina + southwest + the Wilds + headroom
              kMaxCars = 48, kMaxRoutes = 8, kMaxWps = 1024, kMaxPeople = 16 };

// ---- what a point is for ---------------------------------------------------------------------
enum Act : int { aLook = 0, aPhoto, aWait, aWork, aSit, aEat, aFish, aScope, aPlay, aSun, aChat, kActs };
inline const char* ActName(int a)
{
    static const char* n[] = { "look", "photo", "wait", "work", "sit", "eat", "fish", "scope", "play", "sun", "chat" };
    return a >= 0 && a < kActs ? n[a] : "?";
}
inline int ActFromName(const char* s)
{
    for (int a = 0; a < kActs; ++a) if (_stricmp(s, ActName(a)) == 0) return a;
    return -1;
}
// An act that is one long pose (a loop held until he moves on) rather than standing with gestures.
inline bool ActIsPose(int a) { return a == aSit || a == aEat || a == aFish || a == aScope || a == aSun; }

// ---- who --------------------------------------------------------------------------------------
enum Cast : int { cVisitors = 0, cFamilies, cPassengers, cWorkers, cStaff, cPicnic, cBeach, cWalkers, cRangers, cAnglers, kCasts };   // [round3] + rangers, anglers
inline const char* CastName(int c)
{
    static const char* n[] = { "visitors", "families", "passengers", "workers", "staff", "picnic", "beach", "walkers", "rangers", "anglers" };
    return c >= 0 && c < kCasts ? n[c] : "?";
}
inline int CastFromName(const char* s)
{
    for (int c = 0; c < kCasts; ++c) if (_stricmp(s, CastName(c)) == 0) return c;
    return -1;
}
// The characters of a cast: stuff\ai\AITypes.txt "Character.<cls>" { "<type>", ... } pairs [P data].
struct Member { const char* cls; const char* type; };
inline int CastMembers(int cast, const Member** out)
{
    static const Member citizens[] = {
        { "Pedestrian", "FemaleA" }, { "Pedestrian", "MaleA" }, { "Pedestrian", "FemaleC" }, { "Pedestrian", "MaleC" },
        { "Pedestrian", "FemaleE" }, { "Pedestrian", "MaleE" }, { "Pedestrian", "FemaleG" }, { "Pedestrian", "MaleG" },
        { "Pedestrian", "FemaleB" }, { "Pedestrian", "MaleB" }, { "Pedestrian", "FemaleD" }, { "Pedestrian", "MaleD" },
    };
    static const Member passengers[] = {
        { "Pedestrian", "BusinessManA" }, { "Pedestrian", "FemaleB" }, { "Pedestrian", "MaleD" }, { "Pedestrian", "FemaleF" },
        { "Pedestrian", "MaleF" }, { "Pedestrian", "FemaleH" }, { "Pedestrian", "Graduate" }, { "Pedestrian", "MaleA" },
    };
    static const Member workers[] = {
        { "Pedestrian_Harbour", "HarbourWorker" }, { "Pedestrian_Harbour", "DocksForeman" },
        { "Pedestrian_Harbour", "DocksCraneDriver" }, { "Pedestrian_Harbour", "Sailor" },
    };
    static const Member staff[] = { { "TrainWorkers", "TrainDriver" }, { "Pedestrian", "MaintenanceWorker" } };
    static const Member picnic[] = {
        { "Pedestrian", "FemaleD" }, { "Pedestrian", "MaleB" }, { "Pedestrian_Farmlands", "FarmerA" }, { "Pedestrian", "FemaleA" },
        { "Pedestrian", "MaleH" }, { "Pedestrian_Farmlands", "FarmerB" },
    };
    static const Member beach[] = {
        { "Pedestrian_Beach", "Surfer" }, { "Pedestrian_Beach", "SurferGirl" }, { "Pedestrian_Beach", "Lifeguard" },
        { "Pedestrian_Beach", "SurferDude" }, { "Pedestrian", "FemaleC" }, { "Pedestrian", "MaleE" },
    };
    // [round3] the Wilds (Notes 60 section 6): forest rangers (AITypes Character.Enforcer_Forest, the civilian
    // NPC_ForestRanger01-03 - not "ForestRanger" = Cop_ForestRanger) and anglers (the Fisherman minifigure is a
    // collectable character, Character.Collectable; country folk and a sailor make up the rest) [P data, I spawn].
    static const Member rangers[] = {
        { "Enforcer_Forest", "ForestRangerA" }, { "Enforcer_Forest", "ForestRangerB" }, { "Enforcer_Forest", "ForestRangerC" },
    };
    static const Member anglers[] = {
        { "Collectable", "Fisherman" }, { "Pedestrian_Farmlands", "FarmerC" }, { "Pedestrian_Harbour", "Sailor" },
        { "Pedestrian_Farmlands", "Hillbilly" }, { "Pedestrian", "MaleB" },
    };
    switch (cast)
    {
    case cRangers:    *out = rangers; return 3;
    case cAnglers:    *out = anglers; return 5;
    case cPassengers: *out = passengers; return 8;
    case cWorkers:    *out = workers; return 4;
    case cStaff:      *out = staff; return 2;
    case cPicnic:     *out = picnic; return 6;
    case cBeach:      *out = beach; return 6;
    default:          *out = citizens; return 12;      // visitors, families, walkers
    }
}

// ---- the data ---------------------------------------------------------------------------------
struct Pt { float pos[3]; int act; int group; float look[2]; bool hasLook; int spot; };
struct Spot { char name[32]; int cast; int people; float pos[3]; float radius; int pt0, nPt; };
struct Route { char name[32]; char model[48]; float speed; bool loop; int wp0, nWp; };
struct Data
{
    Spot  spots[kMaxSpots];   int nSpots;
    Pt    pts[kMaxPts];       int nPts;
    sitem::Stand cars[kMaxCars]; int nCars;
    Route routes[kMaxRoutes]; int nRoutes;
    float wps[kMaxWps][3];    int nWps;
    int   bad;                // lines that did not parse
    bool  spotOpen, routeOpen; // the last spot / route line was taken (a refused one's pt / wp lines are refused too)
    int   firstBadLine;       // 1-based, 0 = none
    char  firstBad[96];       // why
};

namespace detail {
inline const char* Skip(const char* s) { while (*s == ' ' || *s == '\t') ++s; return s; }
inline bool Num(const char* f, float& out)
{
    char* end = nullptr;
    out = strtof(f, &end);
    if (end == f) return false;
    while (*end == ' ' || *end == '\t') ++end;
    return *end == 0 && out == out && std::fabs(out) < 1.0e5f;
}
inline bool PosOk(float x, float y, float z) { return std::fabs(x) <= 5000.0f && std::fabs(z) <= 5000.0f && y >= -100.0f && y <= 500.0f; }
inline bool NameOk(const char* s, size_t cap)
{
    if (!*s || strlen(s) >= cap) return false;
    for (const char* c = s; *c; ++c)
        if (!((*c >= 'A' && *c <= 'Z') || (*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9') || *c == '_' || *c == '-' || *c == ' ' || *c == '\'')) return false;
    return true;
}
} // namespace detail

// One line into d. 0 = taken, 1 = blank / comment, 2 = bad (why says why).
inline int ParseLine(const char* line, Data& d, const char** why)
{
    using namespace detail;
    static const char* dummy; if (!why) why = &dummy;
    char buf[256];
    {
        size_t n = 0;
        for (const char* c = line; *c && *c != '\r' && *c != '\n' && *c != '#' && *c != ';' && n + 1 < sizeof(buf); ++c) buf[n++] = *c;
        buf[n] = 0;
    }
    if (!*Skip(buf)) return 1;
    char f[9][64] = {};
    int n = 0;
    for (const char* s = buf; n < 9; )
    {
        s = Skip(s);
        const char* e = s;
        while (*e && *e != ',') ++e;
        const char* t = e;
        while (t > s && (t[-1] == ' ' || t[-1] == '\t')) --t;
        size_t len = static_cast<size_t>(t - s);
        if (len >= sizeof(f[0])) { *why = "a field is too long"; return 2; }
        memcpy(f[n], s, len); f[n][len] = 0; ++n;
        if (!*e) break;
        s = e + 1;
    }
    float v[6] = {};
    if (_stricmp(f[0], "spot") == 0)
    {
        d.spotOpen = false;
        if (n < 8) { *why = "spot needs: name, cast, people, x, y, z, radius"; return 2; }
        if (d.nSpots >= kMaxSpots) { *why = "too many spots"; return 2; }
        if (!NameOk(f[1], sizeof(d.spots[0].name))) { *why = "a spot needs a plain name"; return 2; }
        const int cast = CastFromName(f[2]);
        if (cast < 0) { *why = "unknown cast"; return 2; }
        for (int i = 0; i < 5; ++i) if (!Num(f[3 + i], v[i])) { *why = "people, x, y, z and radius must be numbers"; return 2; }
        if (!PosOk(v[1], v[2], v[3])) { *why = "the position is outside the world"; return 2; }
        if (v[0] < 0.0f || v[0] > 12.0f || v[4] < 0.5f || v[4] > 200.0f) { *why = "people 0..12, radius 0.5..200"; return 2; }
        Spot& s = d.spots[d.nSpots++];
        s = Spot{};
        strcpy_s(s.name, f[1]); s.cast = cast; s.people = static_cast<int>(v[0]);
        s.pos[0] = v[1]; s.pos[1] = v[2]; s.pos[2] = v[3]; s.radius = v[4];
        s.pt0 = d.nPts; s.nPt = 0;
        d.spotOpen = true;
        return 0;
    }
    if (_stricmp(f[0], "pt") == 0)
    {
        if (n < 6) { *why = "pt needs: x, y, z, act, group"; return 2; }
        if (d.nSpots == 0) { *why = "a pt before any spot"; return 2; }
        if (!d.spotOpen) { *why = "this pt's spot line was refused"; return 2; }
        if (d.nPts >= kMaxPts) { *why = "too many points"; return 2; }
        Spot& s = d.spots[d.nSpots - 1];
        if (s.pt0 + s.nPt != d.nPts) { *why = "a pt must follow its spot"; return 2; }
        for (int i = 0; i < 3; ++i) if (!Num(f[1 + i], v[i])) { *why = "x, y and z must be numbers"; return 2; }
        if (!PosOk(v[0], v[1], v[2])) { *why = "the position is outside the world"; return 2; }
        const int act = ActFromName(f[4]);
        if (act < 0) { *why = "unknown act"; return 2; }
        if (!Num(f[5], v[3]) || v[3] < 0.0f || v[3] > 99.0f) { *why = "group must be 0..99"; return 2; }
        Pt& p = d.pts[d.nPts];
        p = Pt{};
        p.pos[0] = v[0]; p.pos[1] = v[1]; p.pos[2] = v[2]; p.act = act; p.group = static_cast<int>(v[3]); p.spot = d.nSpots - 1;
        if (n >= 8)
        {
            if (!Num(f[6], v[4]) || !Num(f[7], v[5])) { *why = "look x and z must be numbers"; return 2; }
            p.look[0] = v[4]; p.look[1] = v[5]; p.hasLook = true;
        }
        else { p.look[0] = s.pos[0]; p.look[1] = s.pos[2]; p.hasLook = false; }
        ++d.nPts; ++s.nPt;
        return 0;
    }
    if (_stricmp(f[0], "car") == 0)
    {
        if (n < 6) { *why = "car needs: model, x, y, z, heading"; return 2; }
        if (d.nCars >= kMaxCars) { *why = "too many cars"; return 2; }
        char text[256];
        _snprintf_s(text, sizeof(text), _TRUNCATE, "%s, scenery, %s, %s, %s, %s, %s", f[1], f[2], f[3], f[4], f[5], n >= 7 ? f[6] : "");
        char err[80];
        sitem::Stand st;
        if (sitem::ParseStand(text, st, err, sizeof(err)) != 0) { *why = "the car is not a stand (model, x, y, z, heading)"; return 2; }
        d.cars[d.nCars++] = st;
        return 0;
    }
    if (_stricmp(f[0], "route") == 0)
    {
        d.routeOpen = false;
        if (n < 5) { *why = "route needs: name, model, speed, loop"; return 2; }
        if (d.nRoutes >= kMaxRoutes) { *why = "too many routes"; return 2; }
        if (!NameOk(f[1], sizeof(d.routes[0].name))) { *why = "a route needs a plain name"; return 2; }
        if (!f[2][0] || strlen(f[2]) >= sizeof(d.routes[0].model)) { *why = "a route needs a model"; return 2; }
        for (const char* c = f[2]; *c; ++c)
            if (!((*c >= 'A' && *c <= 'Z') || (*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9') || *c == '_')) { *why = "the model name has a character no model name has"; return 2; }
        if (!Num(f[3], v[0]) || !Num(f[4], v[1]) || v[0] < 0.5f || v[0] > 20.0f) { *why = "speed must be 0.5..20, loop 0 or 1"; return 2; }
        Route& r = d.routes[d.nRoutes++];
        r = Route{};
        strcpy_s(r.name, f[1]); strcpy_s(r.model, f[2]); r.speed = v[0]; r.loop = v[1] != 0.0f; r.wp0 = d.nWps; r.nWp = 0;
        d.routeOpen = true;
        return 0;
    }
    if (_stricmp(f[0], "wp") == 0)
    {
        if (n < 4) { *why = "wp needs: x, y, z"; return 2; }
        if (d.nRoutes == 0) { *why = "a wp before any route"; return 2; }
        if (!d.routeOpen) { *why = "this wp's route line was refused"; return 2; }
        if (d.nWps >= kMaxWps) { *why = "too many waypoints"; return 2; }
        Route& r = d.routes[d.nRoutes - 1];
        if (r.wp0 + r.nWp != d.nWps) { *why = "a wp must follow its route"; return 2; }
        for (int i = 0; i < 3; ++i) if (!Num(f[1 + i], v[i])) { *why = "x, y and z must be numbers"; return 2; }
        if (!PosOk(v[0], v[1], v[2])) { *why = "the position is outside the world"; return 2; }
        d.wps[d.nWps][0] = v[0]; d.wps[d.nWps][1] = v[1]; d.wps[d.nWps][2] = v[2];
        ++d.nWps; ++r.nWp;
        return 0;
    }
    *why = "a line must begin with spot, pt, car, route or wp";
    return 2;
}

// The whole text. Returns the number of records taken; d.bad counts the lines refused.
inline int Parse(const char* text, Data& d)
{
    memset(&d, 0, sizeof(d));
    int taken = 0, lineNo = 0;
    const char* s = text ? text : "";
    while (*s)
    {
        const char* e = s;
        while (*e && *e != '\n') ++e;
        char line[300];
        size_t len = static_cast<size_t>(e - s);
        if (len >= sizeof(line)) len = sizeof(line) - 1;
        memcpy(line, s, len); line[len] = 0;
        ++lineNo;
        const char* why = "";
        const int r = ParseLine(line, d, &why);
        if (r == 0) ++taken;
        else if (r == 2)
        {
            if (!d.bad) { d.firstBadLine = lineNo; strncpy_s(d.firstBad, why, _TRUNCATE); }
            ++d.bad;
        }
        s = *e ? e + 1 : e;
    }
    return taken;
}

// What is wrong with data that parsed (nullptr = nothing): a spot with people but no points, a route
// too short to follow, two points of one spot on top of each other.
inline const char* Validate(const Data& d, int* which = nullptr)
{
    for (int i = 0; i < d.nSpots; ++i)
    {
        const Spot& s = d.spots[i];
        if (which) *which = i;
        if (s.people > 0 && s.nPt == 0) return "a spot has people but no points";
        for (int a = 0; a < s.nPt; ++a)
        {
            const Pt& p = d.pts[s.pt0 + a];
            const float dx = p.pos[0] - s.pos[0], dz = p.pos[2] - s.pos[2];
            if (std::sqrt(dx * dx + dz * dz) > s.radius) return "a point is outside its spot's radius";
            for (int b = a + 1; b < s.nPt; ++b)
            {
                const Pt& q = d.pts[s.pt0 + b];
                const float ex = p.pos[0] - q.pos[0], ez = p.pos[2] - q.pos[2];
                if (std::sqrt(ex * ex + ez * ez) < 0.35f) return "two points of a spot are less than 0.35 u apart";
            }
        }
    }
    for (int i = 0; i < d.nRoutes; ++i)
    {
        if (which) *which = i;
        if (d.routes[i].nWp < 2) return "a route needs two waypoints or more";
        if (d.routes[i].loop && d.routes[i].nWp < 3) return "a looping route needs three waypoints or more";
    }
    return nullptr;
}

// ---- small maths -------------------------------------------------------------------------------
inline float Dist2(const float a[3], const float b[3]) { const float dx = a[0] - b[0], dz = a[2] - b[2]; return std::sqrt(dx * dx + dz * dz); }
inline uint32_t Next(uint32_t& r) { r ^= r << 13; r ^= r >> 17; r ^= r << 5; return r; }
inline float Rnd(uint32_t& r) { return (Next(r) & 0xFFFFFF) / 16777216.0f; }
inline float Wrap180(float deg) { while (deg > 180.0f) deg -= 360.0f; while (deg < -180.0f) deg += 360.0f; return deg; }
// The heading of a flat direction, degrees: 0 = +z, 90 = +x (sites_model.h HeadingDir's inverse).
inline float HeadingOf(float dx, float dz) { return std::atan2(dx, dz) * 57.29578f; }

// ---------------------------------------------------------------------------------------------
//  People
// ---------------------------------------------------------------------------------------------
struct Params
{
    int   maxPeople = 10;      // [spots] max_people: at once, everywhere
    int   perSpot = 4;         // [spots] per_spot: at one spot (a spot's own count is the other limit)
    float nearU = 55.0f;       // [spots] near: a player this close to a spot fills it
    float farU = 85.0f;        // [spots] far: nobody this close -> its people go
    float minSpawnU = 12.0f;   // [spots] min_spawn: nobody appears closer to a player than this
    float keepU = 10.0f;       // a strayed person is not removed with a player this close
    float strayU = 4.0f;       // this far outside his spot's radius = strayed
    float fellU = 3.0f;        // this far under his spot = fell through / off
    float arriveU = 0.45f;     // this close to the point = there
    float walkShare = 0.35f;   // [spots] walk: of a group point's turns, this share is a walk
    float actMinS = 5.0f, actMaxS = 14.0f;   // between a person's turns
    float walkSpeed = 0.55f;   // u/s, for the walk's time-out only (the game's walk is ~0.6)
    float nightShare = 0.5f;   // [spots] night: the share of each spot's people out after dark
};

enum PState : int { pIdle = 0, pSpawning, pActing, pWalking, pLeaving };
inline const char* PStateName(int s)
{
    static const char* n[] = { "idle", "spawning", "acting", "walking", "leaving" };
    return s >= 0 && s <= pLeaving ? n[s] : "?";
}

struct Person
{
    int   state;
    int   spot, pt;            // where he belongs / stands (indices into Data)
    int   from;                // walking: the point he left (he goes back there if the walk fails)
    float t;                   // s in this state
    float turnT;               // s until his next turn (a gesture or a walk)
    float cool;                // s until this slot may be filled again
    int   walkFails;           // walks that timed out; 2 = he stays put from now on
    int   tries;               // leaving: the fall-back removal was tried
    bool  posed;               // his standing order (face / pose) is in force
    bool  gesturing;           // a one-shot gesture runs
    uint32_t rng;
};
inline void Reset(Person& m) { const uint32_t r = m.rng; m = Person{}; m.spot = m.pt = m.from = -1; m.rng = r ? r : 0x9E3779B9u; }

struct Facts                   // one person, this tick
{
    float dt;
    bool  on;                  // lcudrive on, [spots] enable, people wanted
    bool  world;               // the live world
    bool  have;                // our character is alive
    bool  posKnown;
    float pos[3];              // where he is
    bool  busy;                // his top task slot runs something (a walk, a gesture)
    float nearPerson;          // the nearest player to him, u (1e9 = none)
    float nearSpot;            // the nearest player to his spot's centre, u (1e9 = none)
    bool  mission;
    bool  settling;            // a player teleported a moment ago
    bool  samePm;
    bool  hold;                // another slot is spawning: do not begin
    int   wantSpot, wantPt;    // idle slots: PickSpawn's answer for this tick (-1 = nothing to do)
};

// The spot's people limit right now (its own count, [spots] per_spot, the night share).
inline int SpotLimit(const Spot& s, const Params& p, bool night)
{
    int n = s.people < p.perSpot ? s.people : p.perSpot;
    if (night && n > 0)
    {
        const int m = static_cast<int>(std::floor(n * p.nightShare + 0.5f));
        n = m < 1 && p.nightShare > 0.0f ? 1 : m;
    }
    return n < 0 ? 0 : n;
}

// Where the next person should appear: the spot nearest to a player among those within nearU and under
// their limit (and the total under max_people), and a free point of it at least minSpawnU from every
// player. false = nothing to do now.
inline bool PickSpawn(const Data& d, const Person* people, int nPeople, const float pp[2][3], const bool ppOk[2],
                      const Params& p, bool night, uint32_t& rng, int* spotOut, int* ptOut)
{
    int live = 0;
    for (int i = 0; i < nPeople; ++i) if (people[i].state != pIdle) ++live;
    if (live >= p.maxPeople || live >= nPeople) return false;
    int order[kMaxSpots]; float dist[kMaxSpots]; int n = 0;
    for (int s = 0; s < d.nSpots; ++s)
    {
        float best = 1e9f;
        for (int k = 0; k < 2; ++k) if (ppOk[k]) best = std::fmin(best, Dist2(pp[k], d.spots[s].pos));
        if (best >= p.nearU) continue;
        int here = 0;
        for (int i = 0; i < nPeople; ++i) if (people[i].state != pIdle && people[i].spot == s) ++here;
        if (here >= SpotLimit(d.spots[s], p, night)) continue;
        int k = n++;
        while (k > 0 && dist[k - 1] > best) { dist[k] = dist[k - 1]; order[k] = order[k - 1]; --k; }
        dist[k] = best; order[k] = s;
    }
    for (int oi = 0; oi < n; ++oi)
    {
        const Spot& s = d.spots[order[oi]];
        if (s.nPt <= 0) continue;
        const int start = static_cast<int>(Rnd(rng) * s.nPt) % s.nPt;
        for (int a = 0; a < s.nPt; ++a)
        {
            const int pi = s.pt0 + (start + a) % s.nPt;
            bool taken = false;
            for (int i = 0; i < nPeople && !taken; ++i)
                if (people[i].state != pIdle && (people[i].pt == pi || (people[i].state == pWalking && people[i].from == pi))) taken = true;
            if (taken) continue;
            bool close = false;
            for (int k = 0; k < 2; ++k) if (ppOk[k] && Dist2(pp[k], d.pts[pi].pos) < p.minSpawnU) close = true;
            if (close) continue;
            *spotOut = order[oi]; *ptOut = pi;
            return true;
        }
    }
    return false;
}

// A free point of his group to walk to (-1 = none): same spot, same group > 0, nobody on it or bound for it.
inline int PickWalk(const Data& d, const Person* people, int nPeople, int self, uint32_t& rng)
{
    const Person& m = people[self];
    if (m.spot < 0 || m.pt < 0) return -1;
    const Spot& s = d.spots[m.spot];
    const int g = d.pts[m.pt].group;
    if (g <= 0 || s.nPt < 2) return -1;
    const int start = static_cast<int>(Rnd(rng) * s.nPt) % s.nPt;
    for (int a = 0; a < s.nPt; ++a)
    {
        const int pi = s.pt0 + (start + a) % s.nPt;
        if (pi == m.pt || d.pts[pi].group != g) continue;
        bool taken = false;
        for (int i = 0; i < nPeople && !taken; ++i)
            if (i != self && people[i].state != pIdle && (people[i].pt == pi || (people[i].state == pWalking && people[i].from == pi))) taken = true;
        if (!taken) return pi;
    }
    return -1;
}

// May he go now? Out of sight is not known; "nobody within keepU of him" is the rule for a strayed one.
inline bool Strayed(const Data& d, const Person& m, const Facts& f, const Params& p)
{
    if (!f.posKnown || m.spot < 0) return false;
    const Spot& s = d.spots[m.spot];
    return Dist2(f.pos, s.pos) > s.radius + p.strayU || f.pos[1] < s.pos[1] - s.radius * 0.25f - p.fellU;
}

// W must have: int Spawn(int spot, int pt, float t)  (0 ok, 1 again next tick, else failed);
// void AbandonSpawn(); void Stand(int pt); bool Gesture(int pt); bool Walk(int pt); void Remove(const char*);
// void Purge(); void Forget(const char*); void Dropped(); void Say(const char* fmt, ...).
template <class W> inline void SetState(W&, Person& m, int s) { m.state = s; m.t = 0.0f; }

template <class W> inline void Leave(W& w, Person& m, const char* why)
{
    w.Remove(why);
    m.tries = 0;
    SetState(w, m, pLeaving);
}

template <class W> inline void Tick(W& w, const Data& d, Person* people, int nPeople, int self, const Params& p, const Facts& f)
{
    Person& m = people[self];
    if (!f.have && m.state != pIdle && m.state != pSpawning)            // the game removed him (or our Remove landed)
    {
        if (m.state != pLeaving) w.Say(">>> spots: person %d is gone (state %s)\n", self, PStateName(m.state));
        w.Dropped();
        const float cool = m.state == pLeaving ? 3.0f : 20.0f;
        Reset(m); m.cool = cool;
    }
    m.t += f.dt;
    if (m.cool > 0.0f) m.cool -= f.dt;
    if (m.state == pSpawning && (!f.on || !f.world))
    {
        w.AbandonSpawn();
        Reset(m); m.cool = 5.0f;
    }
    if (!f.world) return;
    // Just after a teleport (Notes 48 §4.2): nothing new, nothing removed, no orders. A spawn already
    // under way is dropped (its preload given back) - it begins again once things have settled.
    if (f.settling)
    {
        if (m.state == pSpawning) { w.AbandonSpawn(); Reset(m); m.cool = 1.0f; }
        return;
    }
    const bool wanted = f.on && !f.mission;
    switch (m.state)
    {
    case pIdle:
        if (!wanted || m.cool > 0.0f || f.hold || f.wantSpot < 0 || f.wantPt < 0) break;
        m.spot = f.wantSpot; m.pt = f.wantPt; m.from = -1; m.walkFails = 0;
        SetState(w, m, pSpawning);
        [[fallthrough]];
    case pSpawning:
    {
        if (m.t > 0.0f && (!wanted || !f.samePm || f.nearSpot > p.farU))
        {
            w.AbandonSpawn();
            Reset(m); m.cool = 5.0f;
            break;
        }
        const int st = w.Spawn(m.spot, m.pt, m.t);
        if (st == 0)
        {
            m.posed = false; m.gesturing = false;
            m.turnT = p.actMinS * (0.4f + Rnd(m.rng));
            SetState(w, m, pActing);
        }
        else if (st == 1 && m.t < 20.0f) { /* streaming / no slot / not this update's spawner: again next tick */ }
        else
        {
            w.Say(">>> spots: person %d could not be spawned (status %d after %.1f s) - that slot rests 60 s\n", self, st, m.t);
            w.AbandonSpawn();
            Reset(m); m.cool = 60.0f;
        }
        break;
    }
    case pActing:
    case pWalking:
    {
        if (!f.have) break;
        if (!wanted) { Leave(w, m, !f.on ? "switched off" : "a story mission started"); break; }
        if (f.nearSpot > p.farU && f.nearPerson > p.farU) { Leave(w, m, "nobody near"); break; }
        if (Strayed(d, m, f, p))
        {
            if (f.nearPerson > p.keepU) Leave(w, m, "strayed from the spot");
            break;                                                      // (a player is beside him: left alone)
        }
        if (m.state == pWalking)
        {
            const Pt& to = d.pts[m.pt];
            const float left = f.posKnown ? Dist2(f.pos, to.pos) : 1e9f;
            const float whole = m.from >= 0 ? Dist2(d.pts[m.from].pos, to.pos) : 0.0f;
            if (left < p.arriveU || (!f.busy && m.t > 1.0f && left < 1.5f))
            {
                m.from = -1; m.posed = false; m.gesturing = false;
                m.turnT = p.actMinS + (p.actMaxS - p.actMinS) * Rnd(m.rng);
                SetState(w, m, pActing);
            }
            else if (m.t > whole / p.walkSpeed + 6.0f || (!f.busy && m.t > 2.0f))
            {
                // it did not get there: back to where he came from (the same checked line), then he stays put
                ++m.walkFails;
                const int back = m.from;
                if (back >= 0 && m.walkFails < 2 && w.Walk(back)) { m.from = m.pt; m.pt = back; m.t = 0.0f; }
                else
                {
                    m.walkFails = 2; m.from = -1; m.posed = false; m.gesturing = false;
                    m.turnT = p.actMaxS;
                    SetState(w, m, pActing);
                }
            }
            break;
        }
        // acting: the standing order first (and again whenever a gesture has ended)
        if (m.gesturing && (!f.busy || m.t > 12.0f)) { m.gesturing = false; m.posed = false; }
        if (!m.posed && !m.gesturing) { w.Stand(m.pt); m.posed = true; m.t = 0.0f; }
        m.turnT -= f.dt;
        if (m.turnT <= 0.0f && !m.gesturing)
        {
            m.turnT = p.actMinS + (p.actMaxS - p.actMinS) * Rnd(m.rng);
            const int to = m.walkFails < 2 && Rnd(m.rng) < p.walkShare ? PickWalk(d, people, nPeople, self, m.rng) : -1;
            if (to >= 0 && w.Walk(to))
            {
                m.from = m.pt; m.pt = to; m.posed = false;
                SetState(w, m, pWalking);
            }
            else if (!ActIsPose(d.pts[m.pt].act) && w.Gesture(m.pt)) { m.gesturing = true; m.posed = false; m.t = 0.0f; }
        }
        break;
    }
    case pLeaving:
        if (!f.have) break;
        if (m.t > 10.0f && m.tries == 0)
        {
            m.tries = 1;
            w.Say(">>> spots: person %d still there 10 s after his removal - the population manager's own removal now\n", self);
            w.Purge();
        }
        else if (m.t > 30.0f)
        {
            w.Forget("he would not go - ours no longer");
            Reset(m); m.cool = 600.0f;
        }
        break;
    default:
        break;
    }
}

// Letting go of one (unhook, a fault, the data reloaded): removed, the watch released.
template <class W> inline void Release(W& w, Person& m, bool have, const char* why)
{
    if (m.state == pSpawning) w.AbandonSpawn();
    if (have) { if (m.state != pLeaving) w.Remove(why); w.Forget(why); }
    else w.Dropped();
    Reset(m);
}

// ---------------------------------------------------------------------------------------------
//  The teleport hold, shared by people and driving cars (sites.h has its own copy for the stands):
//  a player more than jumpU from where he was a tick ago -> hold for holdS.
// ---------------------------------------------------------------------------------------------
struct Settle { float left; float prev[2][3]; bool prevOk[2]; };
inline bool SettleStep(Settle& s, float dt, const float pp[2][3], const bool ppOk[2], float jumpU = 12.0f, float holdS = 4.0f)
{
    bool jumped = false;
    if (s.left > 0.0f) s.left -= dt;
    for (int i = 0; i < 2; ++i)
    {
        if (ppOk[i] && s.prevOk[i])
        {
            const float dx = pp[i][0] - s.prev[i][0], dy = pp[i][1] - s.prev[i][1], dz = pp[i][2] - s.prev[i][2];
            if (std::sqrt(dx * dx + dy * dy + dz * dz) > jumpU) { s.left = holdS; jumped = true; }
        }
        // a player who appears (co-op drop-in, a respawn) counts as a jump too: his objects are new this frame
        if (ppOk[i] != s.prevOk[i]) { s.left = holdS; jumped = true; }
        s.prevOk[i] = ppOk[i];
        if (ppOk[i]) { s.prev[i][0] = pp[i][0]; s.prev[i][1] = pp[i][1]; s.prev[i][2] = pp[i][2]; }
    }
    return jumped;
}
inline bool Settling(const Settle& s) { return s.left > 0.0f; }

// ---------------------------------------------------------------------------------------------
//  A car that drives a route: a pure-pursuit steering controller on the waypoints.
//    in:  where the car is, where its nose points, its speed and turn rate, how close the nearest
//         player is and whether he is ahead
//    out: steer [-1, 1] (+ = right), throttle [0, 1], brake [0, 1]
//  It aims at the current waypoint and takes the next when within reachU of it or past it. It slows
//  for the bend (the angle to the waypoint after the current one) and STOPS for a player ahead
//  within stopU, or anywhere within bumperU. The steering sign is checked against what the car does
//  (the steer-handedness class of silent sign-composition bug): if it keeps turning away
//  from the side it is steered to, the sign is flipped, once, and said.
// ---------------------------------------------------------------------------------------------
struct DriveParams
{
    float reachU = 2.0f;       // a waypoint this close is reached
    float stopU = 7.0f;        // a player ahead within this -> stop
    float bumperU = 3.0f;      // a player anywhere within this -> stop
    float steerGain = 1.6f;    // steer per radian of heading error
    float bendSlow = 0.45f;    // the share of the speed kept through a 90-degree bend
    float stuckS = 9.0f;       // not moving this long while it should -> stuck
    float offRouteU = 9.0f;    // this far from its waypoint leg -> lost
};
struct Drive
{
    int   wp;                  // the waypoint aimed at (index within the route)
    float steerSign;           // +1, or -1 after the watchdog flipped it
    float against, with;       // s of turning against / with the steer (the watchdog's evidence)
    bool  flipped;
    float stuckT;
    float yieldT;              // s spent stopped for a player (logged by the caller)
    int   laps;
};
inline void DriveReset(Drive& s, int startWp) { s = Drive{}; s.wp = startWp; s.steerSign = 1.0f; }

struct DriveIn
{
    float dt;
    float pos[3];
    float fwd[2];              // the nose, flat unit (x, z)
    float speed;               // u/s along the nose (>= 0 forward)
    float yawRate;             // deg/s, + = turning right (heading growing)
    float nearest;             // the nearest player / obstacle, u (1e9 = none)
    bool  nearestAhead;        // ... is in front of the bumper
};
enum DriveStatus : int { dDriving = 0, dYield, dArrived, dStuck, dLost };
struct DriveOut { float steer, throttle, brake; int status; float errDeg; };

inline int DriveStep(const Route& r, const float (*wps)[3], Drive& s, const DriveIn& in, const DriveParams& p, DriveOut& out)
{
    out = DriveOut{};
    out.status = dDriving;
    if (r.nWp < 2) { out.brake = 1.0f; out.status = dArrived; return out.status; }
    if (s.wp < 0) s.wp = 0;
    if (s.wp >= r.nWp) s.wp = r.loop ? 0 : r.nWp - 1;
    const float (*w)[3] = wps + r.wp0;
    // take the next waypoint when this one is reached or behind us along the leg that leads to it
    for (int guard = 0; guard < 4; ++guard)
    {
        const int prev = s.wp > 0 ? s.wp - 1 : (r.loop ? r.nWp - 1 : 0);
        const float tx = w[s.wp][0] - in.pos[0], tz = w[s.wp][2] - in.pos[2];
        const float lx = w[s.wp][0] - w[prev][0], lz = w[s.wp][2] - w[prev][2];
        const bool reached = std::sqrt(tx * tx + tz * tz) < p.reachU || (prev != s.wp && tx * lx + tz * lz < 0.0f);
        if (!reached) break;
        if (s.wp + 1 < r.nWp) ++s.wp;
        else if (r.loop) { s.wp = 0; ++s.laps; }
        else { out.brake = 1.0f; out.status = dArrived; return out.status; }
    }
    const float tx = w[s.wp][0] - in.pos[0], tz = w[s.wp][2] - in.pos[2];
    const float dist = std::sqrt(tx * tx + tz * tz);
    const float err = Wrap180(HeadingOf(tx, tz) - HeadingOf(in.fwd[0], in.fwd[1]));   // + = the waypoint is to the right
    out.errDeg = err;
    // lost: far from the leg it should be on (pushed off, fell)
    {
        const int prev = s.wp > 0 ? s.wp - 1 : (r.loop ? r.nWp - 1 : 0);
        const float lx = w[s.wp][0] - w[prev][0], lz = w[s.wp][2] - w[prev][2];
        const float l2 = lx * lx + lz * lz;
        float off = dist;
        if (l2 > 1e-4f)
        {
            float u = ((in.pos[0] - w[prev][0]) * lx + (in.pos[2] - w[prev][2]) * lz) / l2;
            u = u < 0.0f ? 0.0f : u > 1.0f ? 1.0f : u;
            const float qx = w[prev][0] + u * lx - in.pos[0], qz = w[prev][2] + u * lz - in.pos[2];
            off = std::sqrt(qx * qx + qz * qz);
        }
        if (off > p.offRouteU || in.pos[1] < w[s.wp][1] - 6.0f) { out.brake = 1.0f; out.status = dLost; return out.status; }
    }
    // the bend after this waypoint
    float want = r.speed;
    {
        const int nxt = s.wp + 1 < r.nWp ? s.wp + 1 : (r.loop ? 0 : s.wp);
        if (nxt != s.wp)
        {
            const float bend = std::fabs(Wrap180(HeadingOf(w[nxt][0] - w[s.wp][0], w[nxt][2] - w[s.wp][2]) - HeadingOf(tx, tz)));
            const float k = bend > 90.0f ? 1.0f : bend / 90.0f;
            const float nearK = dist < 6.0f ? 1.0f : 6.0f / dist;       // only when the bend is close
            want *= 1.0f - (1.0f - p.bendSlow) * k * nearK;
        }
        const float off = std::fabs(err);
        if (off > 20.0f) want *= off > 60.0f ? 0.4f : 1.0f - 0.6f * (off - 20.0f) / 40.0f;
        if (!r.loop && s.wp == r.nWp - 1 && dist < 8.0f) want *= 0.3f + 0.7f * dist / 8.0f;   // easing to the end
    }
    const bool yield = in.nearest < p.bumperU || (in.nearestAhead && in.nearest < p.stopU);
    if (yield)
    {
        out.steer = 0.0f; out.throttle = 0.0f; out.brake = 1.0f;
        out.status = dYield;
        s.yieldT += in.dt; s.stuckT = 0.0f;
        return out.status;
    }
    s.yieldT = 0.0f;
    float steer = p.steerGain * err * 0.017453292f;
    steer = steer > 1.0f ? 1.0f : steer < -1.0f ? -1.0f : steer;
    // the handedness watchdog: steered clearly one way and moving, which way does it turn?
    if (!s.flipped && std::fabs(steer) > 0.35f && in.speed > 1.0f && std::fabs(in.yawRate) > 4.0f)
    {
        if ((in.yawRate > 0.0f) == (steer > 0.0f)) s.with += in.dt; else s.against += in.dt;
        if (s.against > 1.5f && s.against > 3.0f * s.with) { s.steerSign = -s.steerSign; s.flipped = true; }
        else if (s.with > 3.0f) s.flipped = true;                       // settled: it steers the way it is told
    }
    out.steer = steer * s.steerSign;
    const float dv = want - in.speed;
    out.throttle = dv > 0.0f ? (dv * 0.6f > 0.7f ? 0.7f : dv * 0.6f) : 0.0f;
    if (out.throttle > 0.0f && out.throttle < 0.12f) out.throttle = 0.12f;
    out.brake = dv < -1.0f ? (-dv * 0.4f > 1.0f ? 1.0f : -dv * 0.4f) : 0.0f;
    if (in.speed < 0.3f) s.stuckT += in.dt; else s.stuckT = 0.0f;
    if (s.stuckT > p.stuckS) out.status = dStuck;
    return out.status;
}

// Where a route's car starts, and which way it faces there: waypoint 0 towards waypoint 1.
inline void RouteStart(const Route& r, const float (*wps)[3], float pos[3], float* heading)
{
    const float (*w)[3] = wps + r.wp0;
    pos[0] = w[0][0]; pos[1] = w[0][1]; pos[2] = w[0][2];
    *heading = r.nWp > 1 ? HeadingOf(w[1][0] - w[0][0], w[1][2] - w[0][2]) : 0.0f;
}

// A driving car's own little state machine (the engine side spawns / fades through sites-like calls).
enum CState : int { cIdle = 0, cSpawning, cDriving, cDone };
struct CarMem { int state; float t, cool; int fails; Drive drive; };
struct CarParams { float nearU = 80.0f; float farU = 130.0f; float clearU = 6.0f; float keepU = 8.0f; };
struct CarFacts
{
    float dt; bool on, world, have, aboard, mission, settling, samePm, hold;
    float nearStart;           // the nearest player to the route's start
    float nearCar;             // the nearest player to the car (1e9 = none; 0 = unknown)
    bool  startBlocked;        // a player or another vehicle of ours within clearU of the start
    int   status;              // the last DriveStep status of this car (dDriving ...)
};
inline bool CarMayRemove(const CarParams& p, const CarFacts& f) { return f.have && !f.aboard && f.nearCar > p.keepU; }

// W: int Spawn(float t) (0 ok, 1 again, else failed); void AbandonSpawn(); void Fade(const char*);
// void Disown(const char*); void Dropped(); void Say(fmt, ...).
template <class W> inline void CarTick(W& w, CarMem& m, const CarParams& p, const CarFacts& f)
{
    if (!f.have && m.state != cIdle && m.state != cSpawning) { w.Dropped(); m.state = cIdle; m.t = 0.0f; if (m.cool < 5.0f) m.cool = 5.0f; }
    m.t += f.dt;
    if (m.cool > 0.0f) m.cool -= f.dt;
    if (f.have && f.aboard)                                             // a player took it: it is his, for good
    {
        w.Disown("a player got in");
        m.state = cIdle; m.t = 0.0f; m.cool = 30.0f;
        return;
    }
    if (m.state == cSpawning && (!f.on || !f.world)) { w.AbandonSpawn(); m.state = cIdle; m.t = 0.0f; m.cool = 5.0f; }
    if (!f.world) return;
    if (f.settling && m.state != cSpawning) return;                     // nothing new, nothing faded (Notes 48 §4.2)
    const bool wanted = f.on && !f.mission;
    switch (m.state)
    {
    case cIdle:
        if (!wanted || m.cool > 0.0f || f.hold || f.startBlocked || f.settling || f.nearStart > p.nearU) break;
        if (m.fails >= 3) break;                                        // it got stuck / lost three times: not again this session
        m.state = cSpawning; m.t = 0.0f;
        [[fallthrough]];
    case cSpawning:
    {
        if (m.t > 0.0f && (!wanted || !f.samePm || f.nearStart > p.farU)) { w.AbandonSpawn(); m.state = cIdle; m.t = 0.0f; m.cool = 5.0f; break; }
        const int st = w.Spawn(m.t);
        if (st == 0) { m.state = cDriving; m.t = 0.0f; }
        else if (st == 1 && m.t < 20.0f) {}
        else
        {
            w.Say(">>> spots: a route's car could not be spawned (status %d after %.1f s) - retry in 60 s\n", st, m.t);
            w.AbandonSpawn(); m.state = cIdle; m.t = 0.0f; m.cool = 60.0f;
        }
        break;
    }
    case cDriving:
    case cDone:
        if (!f.have) break;
        if (!wanted || (f.nearCar > p.farU && f.nearStart > p.farU))
        {
            if (CarMayRemove(p, f)) { w.Fade(!f.on ? "switched off" : f.mission ? "a story mission started" : "nobody near"); m.state = cIdle; m.t = 0.0f; m.cool = 8.0f; }
            break;
        }
        if (f.status == dStuck || f.status == dLost)
        {
            if (CarMayRemove(p, f))
            {
                ++m.fails;
                w.Say(">>> spots: the route's car %s (%d of 3) - it goes, a fresh one starts later\n", f.status == dStuck ? "is stuck" : "left its route", m.fails);
                w.Fade(f.status == dStuck ? "stuck" : "off its route");
                m.state = cIdle; m.t = 0.0f; m.cool = 30.0f;
            }
            break;
        }
        if (f.status == dArrived)
        {
            if (m.state != cDone) { m.state = cDone; m.t = 0.0f; }
            else if (m.t > 20.0f && CarMayRemove(p, f)) { w.Fade("its trip is over"); m.state = cIdle; m.t = 0.0f; m.cool = 20.0f; }
        }
        else if (m.t > 60.0f) m.fails = 0;                              // a minute of driving: the route works
        break;
    default:
        break;
    }
}

} // namespace spm
