// =============================================================================================
//  animals_model.h - animals at the Wilds, the farm, the beaches and the quays (round 3, 2026-11-19,
//  Notes\64-animals.md; the herds Notes\60 section 6 proposed). PURE, the spots_model.h contract:
//  plain facts in, the actions through the caller's World, so test\animals_harness.cpp runs it as is.
//  animals.h is the engine side.
//
//  The data (lcudrive_animals.txt beside lcudrive.ini, re-read with the ini; one herd a line):
//
//      herd, <name>, <class>, <type>, <count>, <x>, <y>, <z>, <radius>, <behaviour>
//
//    class      Creature | RideableCreature (stuff\ai\AITypes.txt "Character.<class>")
//    type       an AITypes type of that class that is an everyday animal (kTypes below). Stag, Squirrel,
//               Badger, Donkey ... have models but NO AITypes line: such a herd is skipped (counted, logged).
//    count      how many at most (also capped by [animals] per_herd)
//    x y z      the herd's centre ON its ground (y: the surface a spawned animal is put just above; for
//               swim_lake the water level)
//    radius     how far from the centre the herd roams (random points within it, straight lines)
//    behaviour  graze | graze_shy | dash | peck | scuttle | hop_shore | idle_paddock | swim_lake |
//               perch_flyoff | perch_night | wander_night | sleep_wake_flee   (kBeh below)
//
//  What an animal does (always a task in its brain's top slot, so its own default brain never runs):
//    rest   an idle / graze / peck anim on a loop, 4-12 s, then a short walk to a random point within the
//           herd's radius (or another gesture)
//    shy    a player closer than the behaviour's shy distance: it runs 10-15 u straight away from him (kept
//           near its herd); a BIRD (perch_*) flies off instead and is removed a moment later, its place
//           filled again after [animals] respawn seconds - the stock seagull's own way
//    sleep  sleep_wake_flee (the bear): a sleep anim on a loop; a player within 8 u wakes it, it ambles
//           off AWAY from him. It is never given an attack, and every animal is invulnerable ([animals]
//           invulnerable) and harmless: nothing here ever orders anything at a player.
//    night  perch_night / wander_night herds only appear after dark and leave at dawn.
//  Spawning: one at a time, only when the nearest player is between min_spawn and near of the herd's centre
//  (never popping up beside anyone); nobody within far -> it goes. Story missions, lcudrive off, the
//  Backrooms, [animals] enable = 0 -> they go, nothing new. Nothing is spawned, removed or ordered for 4 s
//  after a player teleports (spm::Settle, the 2026-11-16 crash).
// =============================================================================================
#pragma once
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <cstdint>

namespace anm {

enum : int { kMaxHerds = 64, kMaxAnimals = 16 };

// ---- what a herd does -----------------------------------------------------------------------------
enum Beh : int { bGraze = 0, bGrazeShy, bDash, bPeck, bScuttle, bHopShore, bIdlePaddock, bSwimLake, bPerchFlyoff,
                 bPerchNight, bWanderNight, bSleepWakeFlee, kBehs };
struct BehDef
{
    const char* name;
    float shyU;        // a player this close: away (0 = never shy)
    bool  flies;       // shy = fly off and be removed (a bird)
    bool  night;       // only after dark
    bool  sleeps;      // rests asleep until woken
    float moveShare;   // of its turns, this share is a walk
    float restMin, restMax;
};
inline const BehDef& Def(int b)
{
    static const BehDef d[kBehs] = {
        { "graze",           4.0f, false, false, false, 0.45f, 5.0f, 12.0f },
        { "graze_shy",       9.0f, false, false, false, 0.45f, 5.0f, 12.0f },
        { "dash",            6.0f, false, false, false, 0.60f, 3.0f,  8.0f },
        { "peck",            2.5f, false, false, false, 0.40f, 4.0f, 10.0f },
        { "scuttle",         3.0f, false, false, false, 0.60f, 3.0f,  8.0f },
        { "hop_shore",       3.0f, false, false, false, 0.50f, 4.0f, 10.0f },
        { "idle_paddock",    0.0f, false, false, false, 0.25f, 6.0f, 14.0f },
        { "swim_lake",       5.0f, false, false, false, 0.70f, 3.0f,  8.0f },
        { "perch_flyoff",    5.0f, true,  false, false, 0.25f, 4.0f, 12.0f },
        { "perch_night",     5.0f, true,  true,  false, 0.10f, 6.0f, 14.0f },
        { "wander_night",    6.0f, false, true,  false, 0.55f, 4.0f, 10.0f },
        { "sleep_wake_flee", 8.0f, false, false, true,  0.00f, 8.0f, 16.0f },
    };
    return d[b >= 0 && b < kBehs ? b : 0];
}
inline int BehFromName(const char* s) { for (int b = 0; b < kBehs; ++b) if (_stricmp(s, Def(b).name) == 0) return b; return -1; }

// ---- which animals the game has as characters (AITypes.txt, Character.Creature / RideableCreature) [P data] ---
// Only everyday animals: the shark, the crocodile, the dragon, the rat / bug / spider, the novelty balls and the
// T-Rex are left out on purpose (not what a meadow should have; the T-Rex is dino.h's).
enum Cls : int { kCreature = 0, kRideable = 1 };
inline const char* ClsName(int c) { return c == kRideable ? "RideableCreature" : "Creature"; }
inline int ClsFromName(const char* s)
{
    if (_stricmp(s, "Creature") == 0) return kCreature;
    if (_stricmp(s, "RideableCreature") == 0) return kRideable;
    return -1;
}
struct Type { int cls; const char* name; };
inline int TypeCount() { return 19; }
inline const Type& TypeAt(int i)
{
    static const Type t[19] = {
        { kCreature, "Rabbit" }, { kCreature, "Frog" }, { kCreature, "Bear" }, { kCreature, "Owl" }, { kCreature, "Crow" },
        { kCreature, "Bat" }, { kCreature, "Cow" }, { kCreature, "Goat" }, { kCreature, "Chicken" }, { kCreature, "Duck" },
        { kCreature, "Seagull" }, { kCreature, "Pigeon" }, { kCreature, "Crab" }, { kCreature, "Cat" }, { kCreature, "Dog" },
        { kCreature, "Canary" }, { kRideable, "Pig" }, { kRideable, "Horse" }, { kRideable, "Horse_Brown" },
    };
    return t[i >= 0 && i < 19 ? i : 0];
}
// The index of a type of that class (-1 = no AITypes entry of that class).
inline int TypeFrom(int cls, const char* s)
{
    for (int i = 0; i < TypeCount(); ++i) if (TypeAt(i).cls == cls && _stricmp(TypeAt(i).name, s) == 0) return i;
    return -1;
}

// ---- the data ---------------------------------------------------------------------------------------
struct Herd { char name[32]; int type; int count; float pos[3]; float radius; int beh; };
struct Data
{
    Herd herds[kMaxHerds]; int nHerds;
    int  bad;                 // lines refused (format)
    int  skipped;             // herds of a type the game has no character for (Stag ...)
    int  firstBadLine; char firstBad[96];
    char firstSkipped[32];
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
inline bool NameOk(const char* s, size_t cap)
{
    if (!*s || strlen(s) >= cap) return false;
    for (const char* c = s; *c; ++c)
        if (!((*c >= 'A' && *c <= 'Z') || (*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9') || *c == '_' || *c == '-' || *c == ' ' || *c == '\'')) return false;
    return true;
}
} // namespace detail

// One line. 0 = taken, 1 = blank / comment, 2 = bad (why), 3 = skipped (a type the game does not have).
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
    char f[11][48] = {};
    int n = 0;
    for (const char* s = buf; n < 11; )
    {
        s = Skip(s);
        const char* e = s;
        while (*e && *e != ',') ++e;
        const char* t = e;
        while (t > s && (t[-1] == ' ' || t[-1] == '\t')) --t;
        const size_t len = static_cast<size_t>(t - s);
        if (len >= sizeof(f[0])) { *why = "a field is too long"; return 2; }
        memcpy(f[n], s, len); f[n][len] = 0; ++n;
        if (!*e) break;
        s = e + 1;
    }
    if (_stricmp(f[0], "herd") != 0) { *why = "an unknown record (only herd lines)"; return 2; }
    if (n != 10) { *why = "herd needs: name, class, type, count, x, y, z, radius, behaviour"; return 2; }
    if (!NameOk(f[1], sizeof(d.herds[0].name))) { *why = "a herd needs a plain name"; return 2; }
    const int cls = ClsFromName(f[2]);
    if (cls < 0) { *why = "class must be Creature or RideableCreature"; return 2; }
    float v[5];
    for (int i = 0; i < 5; ++i) if (!Num(f[4 + i], v[i])) { *why = "count, x, y, z and radius must be numbers"; return 2; }
    if (std::fabs(v[1]) > 5000.0f || std::fabs(v[3]) > 5000.0f || v[2] < -100.0f || v[2] > 500.0f) { *why = "the position is outside the world"; return 2; }
    if (v[0] < 1.0f || v[0] > 12.0f || v[4] < 1.0f || v[4] > 60.0f) { *why = "count 1..12, radius 1..60"; return 2; }
    const int beh = BehFromName(f[9]);
    if (beh < 0) { *why = "unknown behaviour"; return 2; }
    const int type = TypeFrom(cls, f[3]);
    if (type < 0) { *why = "not an animal the game has (no AITypes entry of that class)"; return 3; }
    if (d.nHerds >= kMaxHerds) { *why = "too many herds"; return 2; }
    Herd& h = d.herds[d.nHerds++];
    h = Herd{};
    strcpy_s(h.name, f[1]); h.type = type; h.count = static_cast<int>(v[0]);
    h.pos[0] = v[1]; h.pos[1] = v[2]; h.pos[2] = v[3]; h.radius = v[4]; h.beh = beh;
    return 0;
}

inline int Parse(const char* text, Data& d)
{
    d = Data{};
    int taken = 0, lineNo = 0;
    for (const char* s = text; s && *s; )
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
        else if (r == 3) { if (!d.skipped) { char nm[32] = {}; const char* c = strchr(line, ','); if (c) { c = detail::Skip(c + 1); size_t k = 0; while (c[k] && c[k] != ',' && k + 1 < sizeof nm) { nm[k] = c[k]; ++k; } } strcpy_s(d.firstSkipped, nm); } ++d.skipped; }
        else if (r == 2) { if (!d.bad) { d.firstBadLine = lineNo; strncpy_s(d.firstBad, why, _TRUNCATE); } ++d.bad; }
        s = *e ? e + 1 : e;
    }
    return taken;
}

// ---- small maths ----------------------------------------------------------------------------------
inline float Dist2(const float a[3], const float b[3]) { const float dx = a[0] - b[0], dz = a[2] - b[2]; return std::sqrt(dx * dx + dz * dz); }
inline uint32_t Next(uint32_t& r) { r ^= r << 13; r ^= r >> 17; r ^= r << 5; return r; }
inline float Rnd(uint32_t& r) { return (Next(r) & 0xFFFFFF) / 16777216.0f; }

// A random point of the herd's disc (within frac of its radius), on the herd's level.
inline void PointIn(const Herd& h, float frac, uint32_t& r, float out[3])
{
    const float a = Rnd(r) * 6.2831853f, d = std::sqrt(Rnd(r)) * h.radius * frac;
    out[0] = h.pos[0] + std::sin(a) * d; out[1] = h.pos[1]; out[2] = h.pos[2] + std::cos(a) * d;
}
// Away from `from`, 10-15 u, kept within the herd's radius + 10 of its centre.
inline void AwayFrom(const Herd& h, const float at[3], const float from[3], uint32_t& r, float out[3])
{
    float dx = at[0] - from[0], dz = at[2] - from[2];
    float l = std::sqrt(dx * dx + dz * dz);
    if (l < 0.05f) { const float a = Rnd(r) * 6.2831853f; dx = std::sin(a); dz = std::cos(a); l = 1.0f; }
    const float go = 10.0f + 5.0f * Rnd(r);
    out[0] = at[0] + dx / l * go; out[1] = h.pos[1]; out[2] = at[2] + dz / l * go;
    const float cx = out[0] - h.pos[0], cz = out[2] - h.pos[2];
    const float cl = std::sqrt(cx * cx + cz * cz), lim = h.radius + 10.0f;
    if (cl > lim) { out[0] = h.pos[0] + cx / cl * lim; out[2] = h.pos[2] + cz / cl * lim; }
}

// ---------------------------------------------------------------------------------------------
//  Animals
// ---------------------------------------------------------------------------------------------
struct Params
{
    int   maxAnimals = 10;     // [animals] max: at once, everywhere
    int   perHerd = 6;         // [animals] per_herd
    float nearU = 70.0f;       // [animals] near: a player this close to a herd's centre fills it...
    float minSpawnU = 25.0f;   // [animals] min_spawn: ...but only while every player is further than this from it
    float farU = 100.0f;       // [animals] far: nobody this close -> it goes
    float shy = 1.0f;          // [animals] shy: x every behaviour's shy distance (0 = never shy)
    float respawnS = 60.0f;    // [animals] respawn: a bird that flew off is back after this
    float keepU = 8.0f;        // a strayed animal is not removed with a player this close
    float wakeU = 8.0f;        // the sleeping bear wakes at this
    float arriveU = 0.6f;
    float walkSpeed = 0.5f;    // u/s, for a walk's time-out only
};

enum AState : int { aIdle = 0, aSpawning, aRest, aMove, aShy, aFlying, aSleep, aLeaving };
inline const char* AStateName(int s)
{
    static const char* n[] = { "idle", "spawning", "resting", "moving", "running off", "flying off", "asleep", "leaving" };
    return s >= 0 && s <= aLeaving ? n[s] : "?";
}

struct Animal
{
    int   state;
    int   herd;
    float t, turnT, cool;
    float to[3];               // where it is going
    float moveS;               // the time-out of this move
    int   tries;
    bool  posed;               // its resting loop is in force
    uint32_t rng;
};
inline void Reset(Animal& m) { const uint32_t r = m.rng; m = Animal{}; m.herd = -1; m.rng = r ? r : 0x9E3779B9u; }

struct Facts
{
    float dt;
    bool  on, world, mission, settling, samePm, night;
    bool  have, posKnown, busy;
    float pos[3];
    float nearPlayer;          // nearest player to the animal (1e9 = none)
    float playerPos[3];        // ... where he is
    float nearHerd;            // nearest player to its herd's centre
    bool  hold;                // another one is spawning
    int   wantHerd;            // idle: PickSpawn's herd for this tick (-1 = none)
};

struct HerdMem { float cool[kMaxHerds]; };     // s until a herd may be filled again (a bird flew off)

// The herd's limit right now (0 = not now: a night herd by day).
inline int HerdLimit(const Herd& h, const Params& p, bool night)
{
    if (Def(h.beh).night && !night) return 0;
    const int n = h.count < p.perHerd ? h.count : p.perHerd;
    return n < 0 ? 0 : n;
}

// The herd the next animal should join: under its limit, not resting, the nearest player between minSpawnU
// and nearU of its centre (the nearest such herd first). -1 = nothing to do.
inline int PickSpawn(const Data& d, const Animal* a, int nA, const HerdMem& hm, const float pp[2][3], const bool ppOk[2],
                     const Params& p, bool night)
{
    int live = 0;
    for (int i = 0; i < nA; ++i) if (a[i].state != aIdle) ++live;
    if (live >= p.maxAnimals || live >= nA) return -1;
    int best = -1; float bestD = 1e9f;
    for (int h = 0; h < d.nHerds; ++h)
    {
        if (hm.cool[h] > 0.0f) continue;
        const int lim = HerdLimit(d.herds[h], p, night);
        if (lim <= 0) continue;
        int in = 0;
        for (int i = 0; i < nA; ++i) if (a[i].state != aIdle && a[i].herd == h) ++in;
        if (in >= lim) continue;
        float nd = 1e9f;
        for (int k = 0; k < 2; ++k) if (ppOk[k]) nd = std::fmin(nd, Dist2(pp[k], d.herds[h].pos));
        if (nd < p.minSpawnU || nd > p.nearU) continue;
        if (nd < bestD) { bestD = nd; best = h; }
    }
    return best;
}

// W must have: int Spawn(int herd, const float pos[3], float t) (0 ok, 1 again next tick, else failed);
// void AbandonSpawn(); bool Rest(int beh, bool asleep) (an idle loop); bool Gesture(int beh);
// bool Goto(const float p[3], bool fast); bool FlyOff(const float p[3]); void Remove(const char*); void Purge();
// void Forget(const char*); void Dropped(); void Say(const char* fmt, ...).
template <class W> inline void SetState(W&, Animal& m, int s) { m.state = s; m.t = 0.0f; }
template <class W> inline void Leave(W& w, Animal& m, const char* why) { w.Remove(why); m.tries = 0; SetState(w, m, aLeaving); }

template <class W> inline void Tick(W& w, const Data& d, Animal* all, int nA, int self, HerdMem& hm, const Params& p, const Facts& f)
{
    Animal& m = all[self];
    (void)nA;
    if (!f.have && m.state != aIdle && m.state != aSpawning)
    {
        if (m.state != aLeaving && m.state != aFlying) w.Say(">>> animals: animal %d is gone (state %s)\n", self, AStateName(m.state));
        w.Dropped();
        const float cool = m.state == aLeaving || m.state == aFlying ? 3.0f : 20.0f;
        Reset(m); m.cool = cool;
    }
    m.t += f.dt;
    if (m.cool > 0.0f && m.state == aIdle) m.cool -= f.dt;
    if (m.state == aSpawning && (!f.on || !f.world)) { w.AbandonSpawn(); Reset(m); m.cool = 5.0f; }
    if (!f.world) return;
    if (f.settling)
    {
        if (m.state == aSpawning) { w.AbandonSpawn(); Reset(m); m.cool = 1.0f; }
        return;
    }
    const bool wanted = f.on && !f.mission;
    switch (m.state)
    {
    case aIdle:
    {
        if (!wanted || m.cool > 0.0f || f.hold || f.wantHerd < 0 || f.wantHerd >= d.nHerds) break;
        m.herd = f.wantHerd;
        SetState(w, m, aSpawning);
        [[fallthrough]];
    }
    case aSpawning:
    {
        const Herd& h = d.herds[m.herd];
        if (m.t > 0.0f && (!wanted || !f.samePm || f.nearHerd > p.farU)) { w.AbandonSpawn(); Reset(m); m.cool = 5.0f; break; }
        if (m.t <= 0.0f) PointIn(h, 0.4f, m.rng, m.to);                   // where it appears (near the centre)
        const int st = w.Spawn(m.herd, m.to, m.t);
        if (st == 0)
        {
            m.posed = false; m.turnT = Def(h.beh).restMin * (0.5f + Rnd(m.rng));
            SetState(w, m, Def(h.beh).sleeps ? aSleep : aRest);
        }
        else if (st == 1 && m.t < 20.0f) { }
        else
        {
            w.Say(">>> animals: %s could not be spawned (status %d after %.1f s) - that slot rests 60 s\n", h.name, st, m.t);
            w.AbandonSpawn(); Reset(m); m.cool = 60.0f;
        }
        break;
    }
    case aRest: case aMove: case aShy: case aSleep:
    {
        if (!f.have) break;
        const Herd& h = d.herds[m.herd];
        const BehDef& bd = Def(h.beh);
        if (!wanted) { Leave(w, m, !f.on ? "switched off" : "a story mission started"); break; }
        if (f.nearHerd > p.farU && f.nearPlayer > p.farU) { Leave(w, m, "nobody near"); break; }
        if (bd.night && !f.night && f.nearPlayer > p.keepU * 2.0f) { Leave(w, m, "morning"); break; }
        if (f.posKnown && (Dist2(f.pos, h.pos) > h.radius + 25.0f || f.pos[1] < h.pos[1] - 6.0f))
        {
            if (f.nearPlayer > p.keepU) Leave(w, m, "strayed from its herd");
            break;
        }
        // a player close: away (a bird flies off; the bear wakes first)
        const float shyU = bd.sleeps ? p.wakeU : bd.shyU * p.shy;
        if (m.state != aShy && shyU > 0.0f && f.nearPlayer < shyU && f.posKnown)
        {
            float away[3];
            AwayFrom(h, f.pos, f.playerPos, m.rng, away);
            if (bd.flies)
            {
                w.FlyOff(away);
                hm.cool[m.herd] = p.respawnS;                             // the rest of the flock stays; its place is filled after respawn s
                SetState(w, m, aFlying);
                break;
            }
            if (w.Goto(away, true))
            {
                for (int k = 0; k < 3; ++k) m.to[k] = away[k];
                m.moveS = Dist2(f.pos, away) / 1.5f + 5.0f; m.posed = false;
                SetState(w, m, aShy);
                break;
            }
        }
        if (m.state == aMove || m.state == aShy)
        {
            const float left = f.posKnown ? Dist2(f.pos, m.to) : 1e9f;
            if (left < p.arriveU || (!f.busy && m.t > 1.0f) || m.t > m.moveS)
            {
                m.posed = false; m.turnT = bd.restMin + (bd.restMax - bd.restMin) * Rnd(m.rng);
                SetState(w, m, aRest);                                    // (a woken bear stays awake)
            }
            break;
        }
        if (!m.posed) { w.Rest(h.beh, m.state == aSleep); m.posed = true; }
        if (m.state == aSleep) break;                                     // asleep until a player comes
        m.turnT -= f.dt;
        if (m.turnT <= 0.0f)
        {
            m.turnT = bd.restMin + (bd.restMax - bd.restMin) * Rnd(m.rng);
            if (Rnd(m.rng) < bd.moveShare)
            {
                float to[3]; PointIn(h, 1.0f, m.rng, to);
                if (w.Goto(to, false))
                {
                    for (int k = 0; k < 3; ++k) m.to[k] = to[k];
                    m.moveS = (f.posKnown ? Dist2(f.pos, to) : h.radius) / p.walkSpeed + 6.0f; m.posed = false;
                    SetState(w, m, aMove);
                    break;
                }
            }
            if (w.Gesture(h.beh)) m.posed = false;                       // the loop again once it has played
        }
        break;
    }
    case aFlying:
        if (!f.have) break;
        if (m.t > 2.5f) { w.Remove("flew off"); m.tries = 0; SetState(w, m, aLeaving); }
        break;
    case aLeaving:
        if (!f.have) break;
        if (m.t > 10.0f && m.tries == 0) { m.tries = 1; w.Say(">>> animals: animal %d still there 10 s after its removal - the population manager's own removal now\n", self); w.Purge(); }
        else if (m.t > 30.0f) { w.Forget("it would not go - ours no longer"); Reset(m); m.cool = 600.0f; }
        break;
    default: break;
    }
}

template <class W> inline void Release(W& w, Animal& m, bool have, const char* why)
{
    if (m.state == aSpawning) w.AbandonSpawn();
    if (have) { if (m.state != aLeaving) w.Remove(why); w.Forget(why); }
    else w.Dropped();
    Reset(m);
}

inline void CoolHerds(HerdMem& hm, float dt) { for (float& c : hm.cool) c = c > dt ? c - dt : 0.0f; }

} // namespace anm
