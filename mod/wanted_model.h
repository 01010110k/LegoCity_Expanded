// =============================================================================================
//  wanted_model.h - the wanted level itself (Notes\34). Pure: numbers in, the level out, so
//  test\wanted_harness.cpp runs it as is. wanted.h turns the level into police pursuits.
//
//  * Heat 0..5.99 from crimes; the level is its whole part, the next brick fills with the rest.
//    Each brick is harder to earn than the last: a crime counts 1 / (1 + levelDamp x level)
//    (2026-11-13: ten pedestrians in 26 s had made five bricks). A crime no police saw counts
//    x unseenMul and can't take you past unseenCap (witnesses phone in a brick, not a manhunt).
//    Heat under one brick fades away while you're not wanted.
//  * The police need a moment to find you: traffic police only join once they pass near you, so
//    until the first one has, nothing counts down - up to `searchS`, then the level gives up.
//  * Once seen, losing every pursuer for evadeBase + evadePerLevel * level seconds drops ONE
//    level (GTA's flashing bricks), and the countdown starts over for the next.
//  * On foot near a police officer who is on foot too, now and then one fires his grapple: a
//    tie-up. Never from inside a car (2026-10-25: a shot needs an officer outside his vehicle
//    to fire it), and only a small chance (the rope is the rare case): up
//    close they mainly fight you with the game's own melee (wanted_officers.h). Down - tied, or knocked
//    down in the fight - with the police right there (an officer or a car) for arrestS, or stopped
//    in a vehicle with them at your door for carArrestS, is BUSTED.
//  * Beaten down for good (the game's own death and respawn): the police lose interest, level 0.
//  * A mission or side mission running its own pursuit (a record that isn't ours), or a story
//    mission, stands it down to 0 at once: wanted mode never fights a script.
//  * After a bust nothing counts for graceS: the police still ramming your stopped car, the trip
//    to the station, and a head start once you're there (the 2026-10-25 test re-wanted you before
//    the loading screen). The grace runs on game time, so a loading screen doesn't use it up.
//  * On the water (police boats, 2026-11-13): the boats come out once you've been on the water a
//    moment and go home after a while back on land (WaterLatch). A swimmer can't be rammed, stunned
//    or tied (the chase AI only rams a target in a vehicle, Notes\33 §1.5; the gadgets need you on
//    your feet), so without a rule of its own swimming would be immune: a MANNED police boat
//    alongside you (boatRange) for boatArrestS is BUSTED - the boat and its officer are the cause
//    you can see. Stopped in your own boat, a police boat alongside counts like a car at your door.
//  * Which police units run at which level (kUnits / WantUnit), and how long a police helicopter
//    alone can hold the level (HeliHold) - both pure, both harness-tested (2026-11-13).
//  * Co-op: a State per player, and the police shared out between the wanted players (PickPrimary /
//    SplitUnits, 2026-11-14).
// =============================================================================================
#pragma once
#include <cstdint>

namespace wanted {

struct Params
{
    float evadeBase     = 10.0f;   // s with no pursuer before a level drops...
    float evadePerLevel = 4.0f;    // ...plus this per level
    float searchS       = 60.0f;   // s for the first police to find you before the level is dropped
    float levelDamp     = 1.0f;    // a crime counts 1 / (1 + levelDamp x the level you're at)
    float unseenMul     = 0.35f;   // a crime no police saw...
    float unseenCap     = 1.99f;   // ...can't raise the heat past this
    float coolPerS      = 0.03f;   // heat under one brick fades this fast while not wanted
    // the officers' gadgets (on foot) and the arrest
    float zapRange      = 12.0f;   // u from a police officer on foot to you on foot
    float zapPerS       = 0.06f;   // chance per second of a grapple at level 1 (+ zapPerLevel each level)
    float zapPerLevel   = 0.02f;
    float zapCooldown   = 8.0f;    // s after you get up before the next one
    float stunS         = 3.0f;    // ray gun stun (levels 1-2)
    float tieS          = 4.0f;    // grapple tie-up (level 3+)
    float arrestRange   = 4.5f;    // u: the police at your vehicle's door
    float cuffRange     = 3.0f;    // u: an officer ON FOOT at your side (he does the cuffing)
    float arrestS       = 2.5f;    // s down with an officer there = busted
    float carArrestSpeed = 1.0f;   // u/s: "stopped"
    float carArrestS    = 3.5f;    // s stopped in a vehicle with them at the door = busted
    float graceS        = 15.0f;   // s after a bust when crimes don't count
    float boatRange     = 6.0f;    // u: a manned police boat "alongside" (hull to hull is wider than a car door)
    float boatArrestS   = 3.0f;    // s swimming with one alongside = busted
};

struct State
{
    int   level;
    float heat;
    float evadeT, searchT;
    bool  seen;                    // a pursuer has joined since the level was set
    float zapCool, arrestT;
    float graceT;                  // s left of the after-bust grace
};

inline void Reset(State& s)
{
    s.level = 0; s.heat = 0.0f; s.evadeT = s.searchT = 0.0f; s.seen = false;
    s.zapCool = 0.0f; s.arrestT = 0.0f; s.graceT = 0.0f;
}

struct Input
{
    float dt;
    int   requested;               // -1 = none; 0..5 = set the level (debug / hotkey)
    int   pursuers;                // live pursuers of our records
    bool  blocked;                 // a script is running a pursuit, or a story mission is on
    float crime;                   // raw heat from crimes since the last update
    bool  policeNear;              // a police unit saw it (pursuer or police traffic close by)
    float nearest;                 // u to the nearest chasing police vehicle (1e9 = none)
    float officer = 1e9f;          // u to the nearest police officer ON FOOT (1e9 = none)
    bool  onFoot;                  // the player is on foot (and able to be hit: not climbing...)
    bool  down;                    // stunned or tied right now
    bool  inVehicle;
    float speed;                   // the player's vehicle speed, u/s
    float roll;                    // 0..1 random for the hit chance this update
    bool  dead;                    // the player is dying (the game's own death, then a respawn)
    bool  swimming = false;        // in the water, on foot
    float boatNear = 1e9f;         // u to the nearest MANNED chasing police boat (1e9 = none / you're on land)
};

enum Why : uint8_t { kNone, kRequested, kEvaded, kNotFound, kBlocked, kCrime, kBusted, kWasted };
enum Zap : uint8_t { kNoZap, kStun, kTie };

struct Output
{
    int     level;
    float   heat;
    uint8_t changed, why;
    float   evadeLeft;             // s until the next drop (0 when not counting)
    uint8_t zap;                   // a gadget hit to apply now (kStun / kTie)
    float   zapS;                  // ... for this long
    uint8_t busted;                // arrested now: clear everything, lose studs, go to the station
    float   arrest;                // 0..1: an arrest is in progress and this far along (the meter you see)
    uint8_t arrestKind;            // 0 none, 1 on foot (an officer is cuffing you), 2 stopped in a vehicle,
                                   // 3 swimming with a police boat alongside
    float   graceLeft;             // s of the after-bust grace left (crimes don't count)
};

inline void Update(const Params& p, State& s, const Input& in, Output& o)
{
    o.changed = 0; o.why = kNone; o.evadeLeft = 0.0f; o.zap = kNoZap; o.zapS = 0.0f; o.busted = 0;
    o.arrest = 0.0f; o.arrestKind = 0;
    const float dt = in.dt > 0.0f && in.dt < 1.0f ? in.dt : 0.0f;
    const int before = s.level;
    if (s.graceT > 0.0f) { s.graceT -= dt; if (s.graceT < 0.0f) s.graceT = 0.0f; }
    if (in.blocked)
    {
        if (s.level != 0) { s.level = 0; o.why = kBlocked; }
        s.heat = 0.0f; s.evadeT = s.searchT = 0.0f; s.seen = false; s.arrestT = 0.0f;
    }
    else if (in.dead)
    {
        if (s.level != 0) { s.level = 0; o.why = kWasted; }
        s.heat = 0.0f; s.evadeT = s.searchT = 0.0f; s.seen = false; s.arrestT = 0.0f;
    }
    else
    {
        if (in.requested >= 0)
        {
            s.level = in.requested < 5 ? in.requested : 5;
            s.heat = static_cast<float>(s.level);
            s.evadeT = s.searchT = 0.0f; s.seen = false;
            o.why = kRequested;
        }
        if (in.crime > 0.0f && s.graceT <= 0.0f)
        {
            float add = in.crime / (1.0f + p.levelDamp * static_cast<float>(s.level));
            const bool unseen = !in.policeNear && in.pursuers <= 0;
            if (unseen) add *= p.unseenMul;
            const float cap = unseen ? (s.heat > p.unseenCap ? s.heat : p.unseenCap) : 5.99f;
            s.heat += add;
            if (s.heat > cap) s.heat = cap;
            if (s.heat > 5.99f) s.heat = 5.99f;
            const int lv = static_cast<int>(s.heat);
            if (lv > s.level)
            {
                if (s.level == 0) { s.seen = false; s.searchT = 0.0f; }
                s.level = lv > 5 ? 5 : lv; s.evadeT = 0.0f; o.why = kCrime;
            }
            else if (s.level > 0) s.evadeT = 0.0f;        // a fresh crime stops the countdown
        }
        if (s.level > 0)
        {
            if (in.pursuers > 0) { s.seen = true; s.evadeT = 0.0f; s.searchT = 0.0f; }
            else if (s.seen)
            {
                s.evadeT += dt;
                const float need = p.evadeBase + p.evadePerLevel * static_cast<float>(s.level);
                o.evadeLeft = need - s.evadeT;
                if (s.evadeT >= need) { --s.level; s.heat = static_cast<float>(s.level); s.evadeT = 0.0f; o.why = kEvaded; }
            }
            else
            {
                s.searchT += dt;
                if (s.searchT >= p.searchS) { s.level = 0; s.heat = 0.0f; s.searchT = 0.0f; o.why = kNotFound; }
            }
        }
        else if (s.heat > 0.0f)
        {
            s.heat -= p.coolPerS * dt;
            if (s.heat < 0.0f) s.heat = 0.0f;
        }

        // the officers' gadgets: on foot, an officer on foot close by, a hit now and then
        if (s.zapCool > 0.0f && !in.down) s.zapCool -= dt;
        if (s.level > 0 && in.onFoot && !in.down && s.zapCool <= 0.0f && in.officer < p.zapRange)
        {
            const float chance = (p.zapPerS + p.zapPerLevel * static_cast<float>(s.level - 1)) * dt;
            if (in.roll < chance)
            {
                o.zap = kTie;                                          // the officers carry grapple guns
                o.zapS = p.tieS;
                s.zapCool = p.zapCooldown;
            }
        }
        // The arrest. On foot: down (tied, or knocked down) with an OFFICER ON FOOT at your side - he cuffs
        // you, and that takes arrestS; a police car merely parked nearby arrests nobody (2026-11-11: the cause of
        // an arrest has to be visible - a tie with a car 4 u away was a bust 1 s later).
        // In a vehicle: stopped, with a police car or an officer at the door, for carArrestS (in your
        // own boat, a police boat alongside counts too). In the water: a police boat alongside, boatArrestS.
        // (boatNear is only ever set while you're on the water, so a boat at a quay can't arrest a car.)
        const bool footArrest = in.down && !in.inVehicle && in.officer < p.cuffRange;
        const bool carArrest = in.inVehicle && in.speed < p.carArrestSpeed &&
                               (in.nearest < p.arrestRange || in.officer < p.arrestRange || in.boatNear < p.boatRange);
        const bool waterArrest = in.swimming && !in.inVehicle && in.boatNear < p.boatRange;
        if (s.level > 0 && (footArrest || carArrest || waterArrest))
        {
            const float need = footArrest ? p.arrestS : waterArrest ? p.boatArrestS : p.carArrestS;
            s.arrestT += dt;
            o.arrest = need > 0.0f ? (s.arrestT < need ? s.arrestT / need : 1.0f) : 1.0f;
            o.arrestKind = footArrest ? 1 : waterArrest ? 3 : 2;
            if (s.arrestT >= need)
            {
                o.busted = 1;
                s.level = 0; s.heat = 0.0f; s.evadeT = s.searchT = 0.0f; s.seen = false;
                s.arrestT = 0.0f; s.zapCool = p.zapCooldown; s.graceT = p.graceS;
                o.why = kBusted;
            }
        }
        else s.arrestT = 0.0f;
    }
    o.level = s.level;
    o.heat = s.heat;
    o.graceLeft = s.graceT;
    o.changed = s.level != before ? 1 : 0;
}

// ---------------------------------------------------------------------------------------------
//  The water latch: are the police boats out? On after startS straight on the water (in a boat,
//  or swimming), off after lingerS straight on land. A jump off a jetty or a dip mustn't launch
//  boats - each one takes a traffic-manager vehicle slot, and a boat weighted in while you're on
//  land is a boat wasted (Notes\33 §1, the 24-vehicle cap) - and stepping onto a pier for a moment
//  mustn't call them off. t > 0: seconds on the water; t < 0: seconds on land. Returns true when
//  `on` changed this step.
// ---------------------------------------------------------------------------------------------
struct WaterLatch { bool on; float t; };

inline void ResetWater(WaterLatch& w) { w.on = false; w.t = 0.0f; }

inline bool WaterStep(WaterLatch& w, bool wet, float dt, float startS, float lingerS)
{
    if (!(dt > 0.0f && dt < 1.0f)) dt = 0.0f;
    if (wet) w.t = (w.t > 0.0f ? w.t : 0.0f) + dt;
    else     w.t = (w.t < 0.0f ? w.t : 0.0f) - dt;
    if (w.t > 3600.0f) w.t = 3600.0f;
    if (w.t < -3600.0f) w.t = -3600.0f;
    const bool on = w.on ? w.t > -lingerS : w.t >= startS;
    if (on == w.on) return false;
    w.on = on;
    return true;
}

// ---------------------------------------------------------------------------------------------
//  The police per level: MaxInstances of each model at levels 0..5 (Notes\33 §7.3; no Bastion yet,
//  and the helicopters are no traffic unit: wanted_heli.h spawns and flies them outside the pursuit
//  records). wanted.h starts one pursuit record per unit that WantUnit says should run. The PM's
//  pursuit list holds FOUR records and AddPursuit silently drops a fifth (Notes\33 §1.3,
//  0x1406FCB70) [P], so no level may ever want more than kMaxRecords units - test\wanted_harness.cpp
//  walks every combination. At level 3 on the water Squaddie + Trooper + Dasher + Drifter fill it; at
//  4-5 the motorbikes - which can't follow you onto the water - give their record to a second boat,
//  Police_Minnow ([wanted] boat_minnow). Minnow is in no shipped script, so its model is [U].
//    kRoad:     always (at its level)
//    kYields:   a road unit that stands down while the second boat runs
//    kBoat:     only while the suspect is on the water and the boat map is loaded (boatsUp)
//    kBoat2:    kBoat, and only in the record kYields gave up
// ---------------------------------------------------------------------------------------------
enum UnitKind : uint8_t { kRoad, kYields, kBoat, kBoat2 };
struct Unit { const char* name; int maxInst[6]; uint8_t kind; };
static const Unit kUnits[] = {
    { "police_Squaddie",    { 0, 3, 4, 4, 5, 6 }, kRoad },
    { "Police_Trooper",     { 0, 0, 2, 3, 4, 5 }, kRoad },
    { "2016_Police_Dasher", { 0, 0, 0, 2, 3, 4 }, kYields },
    { "Police_Drifter",     { 0, 1, 2, 2, 3, 3 }, kBoat },    // Enforcer/Drifter (9,4), VehicleDefBoat [P]
    { "Police_Minnow",      { 0, 0, 0, 0, 1, 2 }, kBoat2 },   // Enforcer/Minnow (9,9), VehicleDefBoat [P]
};
static const int kNumUnits = static_cast<int>(sizeof(kUnits) / sizeof(kUnits[0]));
static const int kMaxRecords = 4;

inline bool IsBoatUnit(int u) { return kUnits[u].kind == kBoat || kUnits[u].kind == kBoat2; }

// Should unit u run at this level right now? boatsUp: on the water, boats on in the ini, the boat
// map loaded. minnow: [wanted] boat_minnow. boat2Bad: the second boat failed to start in this world
// (the bikes keep their record).
inline bool WantUnit(int u, int level, bool boatsUp, bool minnow, bool boat2Bad)
{
    if (u < 0 || u >= kNumUnits || level <= 0 || level > 5 || kUnits[u].maxInst[level] <= 0) return false;
    bool second = false;                                     // the second boat takes the bikes' record
    for (int k = 0; k < kNumUnits; ++k)
        if (kUnits[k].kind == kBoat2 && kUnits[k].maxInst[level] > 0) second = true;
    second = second && boatsUp && minnow && !boat2Bad;
    switch (kUnits[u].kind)
    {
    case kBoat:   return boatsUp;
    case kBoat2:  return second;
    case kYields: return !second;
    default:      return true;
    }
}

// ---------------------------------------------------------------------------------------------
//  Co-op (2026-11-14: each player has their own wanted level, so one can be wanted and not the
//  other). Each player has a State of their own; the police are one force - the PM's four pursuit
//  records, one target each - so they are shared out between the wanted players, a whole unit (one
//  police model) to one player:
//    * one player wanted: every unit WantUnit gives them, at their level - exactly the single-player
//      police;
//    * both wanted: the PRIMARY player gets the first road unit (Squaddie) and the bikes (from their
//      level 3), each at the primary's level; the other player gets the second road unit (Trooper),
//      as many as their own level has of it - at level 1, where it has none, the first level that does.
//      So each wanted player always has a ground unit, the primary has at least as many units, and
//      the two together never field more police than one player alone can (the trim at the end);
//    * the boats go to ONE player on the water (the primary first); the second boat only to the
//      primary, in the bikes' record, as in WantUnit.
//  The primary is the player with the higher level, and it only changes hands when the other player's
//  level is strictly higher (a tie stays where it is: nothing is re-made when two players trade
//  levels). The officers on foot and the helicopters serve the primary only (wanted.h, a v1 limit).
// ---------------------------------------------------------------------------------------------
struct UnitPlan { int8_t owner; int8_t level; int16_t inst; };   // owner -1: the unit doesn't run

inline bool SamePlan(const UnitPlan& a, const UnitPlan& b) { return a.owner == b.owner && a.level == b.level && a.inst == b.inst; }

inline int PickPrimary(int cur, int level0, int level1, bool p2Here)
{
    if (!p2Here) return 0;
    cur = cur == 1 ? 1 : 0;
    const int mine = cur ? level1 : level0, other = cur ? level0 : level1;
    return other > mine ? 1 - cur : cur;
}

inline void SplitUnits(const int level[2], int primary, const bool boatsUp[2], bool minnow, bool boat2Bad,
                       UnitPlan out[kNumUnits])
{
    for (int u = 0; u < kNumUnits; ++u) { out[u].owner = -1; out[u].level = 0; out[u].inst = 0; }
    int lv[2];
    for (int i = 0; i < 2; ++i) lv[i] = level[i] < 0 ? 0 : (level[i] > 5 ? 5 : level[i]);
    if (lv[0] <= 0 && lv[1] <= 0) return;
    auto give = [&](int u, int who, int inst) {
        out[u].owner = static_cast<int8_t>(who); out[u].level = static_cast<int8_t>(lv[who]);
        out[u].inst = static_cast<int16_t>(inst);
    };
    if (lv[0] <= 0 || lv[1] <= 0)                            // one player wanted: today's police, all theirs
    {
        const int who = lv[0] > 0 ? 0 : 1;
        for (int u = 0; u < kNumUnits; ++u)
            if (WantUnit(u, lv[who], boatsUp[who], minnow, boat2Bad)) give(u, who, kUnits[u].maxInst[lv[who]]);
        return;
    }
    const int a = primary == 1 ? 1 : 0, b = 1 - a;
    const int wet = boatsUp[a] ? a : (boatsUp[b] ? b : -1);  // whose water the boats work
    int road = 0;
    for (int u = 0; u < kNumUnits; ++u)
    {
        const Unit& U = kUnits[u];
        switch (U.kind)
        {
        case kRoad:
            if (road++ == 1)                                 // the second road unit is the other player's
            {
                int n = 0;
                for (int l = lv[b]; l <= 5 && n <= 0; ++l) n = U.maxInst[l];
                if (n > 0) give(u, b, n);
            }
            else if (U.maxInst[lv[a]] > 0) give(u, a, U.maxInst[lv[a]]);
            break;
        case kYields:
        case kBoat2:
            if (WantUnit(u, lv[a], wet == a, minnow, boat2Bad)) give(u, a, U.maxInst[lv[a]]);
            break;
        case kBoat:
            if (wet >= 0 && U.maxInst[lv[wet]] > 0) give(u, wet, U.maxInst[lv[wet]]);
            break;
        default: break;
        }
    }
    // Never more police in all than one player can have (level 5 at sea, the most WantUnit fields):
    // every one of them takes a traffic-manager vehicle slot (Notes\33 §1, the 24-vehicle cap). Over
    // it - only the primary at 5 on land with the other at 4-5 at sea - the biggest unit gives one up.
    int budget = 0;
    for (int l = 1; l <= 5; ++l)
        for (int water = 0; water < 2; ++water)
        {
            int n = 0;
            for (int u = 0; u < kNumUnits; ++u) if (WantUnit(u, l, water != 0, true, false)) n += kUnits[u].maxInst[l];
            if (n > budget) budget = n;
        }
    for (;;)
    {
        int total = 0, big = -1;
        for (int u = 0; u < kNumUnits; ++u)
        {
            if (out[u].owner < 0) continue;
            total += out[u].inst;
            if (big < 0 || out[u].inst > out[big].inst) big = u;
        }
        if (total <= budget || big < 0 || out[big].inst <= 1) break;
        --out[big].inst;
    }
}

// ---------------------------------------------------------------------------------------------
//  The police helicopters' share of the pursuers (2026-11-13 review). A helicopter that has you in
//  sight counts as a pursuer - while it watches, the countdown can't start - but on its own only for
//  holdS: it flies 36 u/s, every car and bike tops out near 17, and there's no line-of-sight test
//  (no ray cast is researched), so without a limit levels 4-5 could never be shaken off on land.
//  Once the cars have lost you and the helicopter alone has held you for holdS, the countdown runs
//  while it still follows (the bricks blink); the level drops, and below heli_level it flies off.
//  The clock only starts over when a car or bike finds you again: a helicopter that loses you for a
//  moment, or its replacement, gets no new hold. holdS <= 0: it holds you for as long as it sees you.
// ---------------------------------------------------------------------------------------------
struct HeliHold { float t; };

inline void ResetHeliHold(HeliHold& h) { h.t = 0.0f; }

inline int HeliPursuers(HeliHold& h, int ground, int watching, float dt, float holdS)
{
    if (!(dt > 0.0f && dt < 1.0f)) dt = 0.0f;
    if (ground > 0) { h.t = 0.0f; return watching > 0 ? watching : 0; }
    if (watching <= 0) return 0;                             // (the clock keeps what it has)
    h.t += dt;
    if (h.t > 3600.0f) h.t = 3600.0f;
    return (holdS <= 0.0f || h.t < holdS) ? watching : 0;
}

// ---------------------------------------------------------------------------------------------
//  The ray gun as a crime (2026-11-14). The game keys its bolt types (stuff\BoltTypes.txt) by a
//  hash of their name - FNV-1 from 0x811C9DC5, lower case folded to upper, each char sign-extended
//  (0x1402D9DD0) - in the bolt system's type table; a bolt's type id is its row. The ray gun fires
//  "legoBolt"; "LaserCannon" is its twin (same hit reaction). The grapple, the paint and chicken
//  guns and the hoses are not crimes.
//  A street prop is smashed by a hit when its respawn timer starts (prop+0x318: 0 -> 3 s).
// ---------------------------------------------------------------------------------------------
inline uint32_t BoltNameHash(const char* s)
{
    uint32_t h = 0x811C9DC5u;
    for (; s && *s; ++s)
    {
        int c = static_cast<signed char>(*s);
        if (static_cast<unsigned>(c - 'a') <= 25u) c -= 0x20;
        h = (h * 0x01000193u) ^ static_cast<uint32_t>(c);
    }
    return h;
}

static const uint32_t kBoltLego = 0xD8B4B257u, kBoltLaser = 0x2FED6381u;   // = BoltNameHash (harness-checked)

inline bool IsRayGunBolt(uint32_t nameHash) { return nameHash == kBoltLego || nameHash == kBoltLaser; }

inline bool PropSmashed(float timerBefore, float timerAfter) { return !(timerBefore > 0.0f) && timerAfter > 0.0f; }

// A vehicle the ray gun hit and that is destroyed within `windowMs` of it: the shooter's wreck.
inline bool ShotThenWrecked(uint32_t shotMs, uint32_t nowMs, uint32_t windowMs)
{
    return shotMs != 0 && nowMs - shotMs <= windowMs;
}

// ---------------------------------------------------------------------------------------------
//  Traffic that flees a chase (2026-11-15, Notes\46 §1 item 8: the cut Vehicle.DriverlessFlee;
//  wanted_flee.h is the engine side). While a player at `minLevel` or more drives at `speedMin` u/s or
//  faster, the manned civilian cars within `radius` of them break off and race away from them - one
//  new car every `every` s at most, `maxCars` at once - for `holdS` s, or until `awayU` from that
//  player, or the chase is over (no wanted player that high any more); then they go back to traffic.
//  A car let go is not picked again for `restS` s.
// ---------------------------------------------------------------------------------------------
struct FleeParams
{
    int   minLevel = 2;
    float radius = 30.0f, speedMin = 8.0f, every = 1.5f, holdS = 9.0f, awayU = 110.0f, restS = 60.0f;
    int   maxCars = 3;
};

struct FleeCand                    // a car near a chased player, as the engine side sees it
{
    float dist;                    // u to the chased player
    bool  manned, police, car;     // a crowd / character driver; a police model; a car (not a boat, bike, aircraft)
    bool  nearFoot;                // within 6 u of a player on foot (he may be getting in)
    bool  fleeing, rested;         // fleeing now (ours); let go less than restS ago
};

// The chase that makes traffic flee: this player's level and their driving speed.
inline bool FleeChase(const FleeParams& p, int level, bool driving, float speed)
{
    return level >= p.minLevel && driving && speed >= p.speedMin;
}

inline bool FleePick(const FleeParams& p, const FleeCand& c, int fleeingNow, float sinceLast)
{
    return c.manned && c.car && !c.police && !c.nearFoot && !c.fleeing && !c.rested && c.dist <= p.radius &&
           fleeingNow < p.maxCars && sinceLast >= p.every;
}

enum FleeEnd : int { kFleeOn = 0, kFleeGone, kFleeTime, kFleeAway, kFleeChaseOver };
inline const char* FleeEndName(int e)
{
    static const char* n[] = { "fleeing", "gone", "time up", "far away", "the chase is over" };
    return e >= 0 && e <= kFleeChaseOver ? n[e] : "?";
}
// alive: the car is still there; t: s since it was sent; dist: u to the player it flees (1e9 = gone);
// chase: that player is still wanted at minLevel or more
inline int FleeCheck(const FleeParams& p, bool alive, float t, float dist, bool chase)
{
    if (!alive) return kFleeGone;
    if (!chase) return kFleeChaseOver;
    if (dist > p.awayU) return kFleeAway;
    if (t >= p.holdS) return kFleeTime;
    return kFleeOn;
}

} // namespace wanted
