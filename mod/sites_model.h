// =============================================================================================
//  sites_model.h - vehicles waiting at stands anywhere in the world (sites.h is the engine side),
//  and Port Atlas as a place. Pure, the same contract as airport_model.h / dino_model.h: plain
//  facts in, the actions through the caller's World, so test\sites_harness.cpp runs it as is.
//
//  The problem (2026-11-14/15, Port Atlas): there were no boats or helicopters there, so it was
//  close to impossible to leave the island. So: up to 12 stands, each one line of [sites] in
//  lcudrive.ini - a model, what it is (boat / heli / car / plane), where, which way it faces:
//      stand1 = Naval_Arowana, boat, 393.0, 0.35, -75.0, 90, Port Atlas quay
//  One Mem per stand:
//    idle      a player within `near` of the stand, no story mission, no cool-down, nobody (no
//              player, no other stand's vehicle) within `clear` of it, no other stand spawning ->
//              resolve the model (unknown: 10 min cool-down) and
//    spawning  SpawnAIVehicle: 0 = there -> parked (the parking itself on the NEXT tick, as
//              airport_model.h); 1 / 2 / 8 (streaming, no room) = again next tick for up to 20 s
//              while a player is still near, no story mission started, the same world (else
//              abandoned, 5 s); anything else = failed, retry in 60 s. A preload the streaming took
//              is given back whenever an attempt ends without a vehicle.
//    parked    the tick after the spawn: its facing is checked (the watchdog of airport_model.h,
//              NextFacing: wrong -> faded and respawned turned, twice at most), then parked (when
//              the stand's kind is parked at all: ParkKind), pinned on the map, a helicopter settled.
//              Sunk (3 u under its stand: the ground or the water was not there) -> faded, again in
//              5 s, three times, then 10 min. Pushed off its stand -> away. Nobody within `far` of
//              the stand AND of it -> faded. A story mission -> faded (30 s).
//    aboard    a player is in it (from ANY state, and also while lcudrive is off: unparked, pin off
//              - a parked vehicle must never stay parked under its driver). Got out: wrecked ->
//              wreck; on the stand and still -> parked again; else away
//    away      back on its stand and still -> parked; wrecked -> wreck; nobody within `leave` of it
//              for 10 s -> faded (5 s later the stand may be filled again, once it is clear)
//    wreck     6 s later -> faded (20 s)
//    any       the weak ref died (the game removed it) -> idle
//  The one rule everything else bends to (dino_model.h's): a vehicle is NEVER removed while a
//  player is in it or within `keep` of it - Fade() is only reached through TryFade / Release, and
//  the harness fuzzes that. Switched off (`[`, [sites] enable = 0) the waiting ones go by the same
//  rule and nothing new appears.
// =============================================================================================
#pragma once
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace sitem {

// kMaxStands = the ini's stand1..stand12; kMaxExtra more slots are filled from the life-spot data's "car" lines
// (spots_model.h: parked scenery cars, kind "scenery" = a car with no map pin); kMaxSlots = both.
enum : int { kMaxStands = 12, kMaxExtra = 48, kMaxSlots = kMaxStands + kMaxExtra };
enum Kind : int { kNone = 0, kBoat, kHeli, kCar, kPlane };
inline const char* KindName(int k)
{
    static const char* n[] = { "none", "boat", "heli", "car", "plane" };
    return k >= 0 && k <= kPlane ? n[k] : "?";
}

enum State : int { kIdle = 0, kSpawning, kParked, kAboard, kAway, kWreck };
inline const char* StateName(int s)
{
    static const char* n[] = { "idle", "spawning", "parked", "aboard", "away", "wreck" };
    return s >= 0 && s <= kWreck ? n[s] : "?";
}

// ---------------------------------------------------------------------------------------------
//  One stand, as [sites] standN spells it:  model, kind, x, y, z, heading [, label]
//    model    a vehicle the game knows by name (AITypes.txt's second column: Naval_Arowana ...)
//    kind     boat | heli (helicopter) | car | plane - what it is treated as: how it is put down
//             (a boat AT y, the water's surface; the others 0.3 u above y, the ground), whether
//             it is parked, whether a helicopter is settled
//    x y z    world units; heading in degrees, 0 = +z, 90 = +x (dino_model.h's convention)
//    label    free text for the log (optional)
//  An empty value = no stand. Anything else that does not parse = no stand, with a reason.
// ---------------------------------------------------------------------------------------------
struct Stand
{
    bool  on;
    char  model[48];
    int   kind;
    float pos[3];
    float heading;
    char  label[32];
    bool  quiet;        // scenery: no map pin (kind "scenery" / "parked" = a car that is just there)
};

inline int KindFromName(const char* s)
{
    auto eq = [&](const char* b) { return _stricmp(s, b) == 0; };
    if (eq("boat") || eq("ship")) return kBoat;
    if (eq("heli") || eq("helicopter") || eq("copter")) return kHeli;
    if (eq("car") || eq("bike") || eq("vehicle")) return kCar;
    if (eq("plane") || eq("airplane") || eq("aeroplane")) return kPlane;
    return kNone;
}

// 0 = a stand, 1 = empty (no stand, not an error), 2 = malformed (err says why)
inline int ParseStand(const char* text, Stand& out, char* err, size_t errCap)
{
    auto fail = [&](const char* why) { if (err && errCap) strncpy_s(err, errCap, why, _TRUNCATE); return 2; };
    out = Stand{};
    if (err && errCap) err[0] = 0;
    char f[7][64] = {};
    int n = 0;
    const char* s = text ? text : "";
    while (*s == ' ' || *s == '\t') ++s;
    if (!*s) return 1;
    for (;;)
    {
        while (*s == ' ' || *s == '\t') ++s;
        const char* e = s;
        if (n == 6) while (*e) ++e;                                      // the label takes the rest, commas and all
        else while (*e && *e != ',') ++e;
        const char* t = e;
        while (t > s && (t[-1] == ' ' || t[-1] == '\t')) --t;
        size_t len = static_cast<size_t>(t - s);
        if (len >= sizeof(f[0])) { if (n < 6) return fail("a field is too long"); len = sizeof(f[0]) - 1; }
        memcpy(f[n], s, len); f[n][len] = 0;
        ++n;
        if (!*e || n == 7) break;
        s = e + 1;
    }
    if (n < 6) return fail("needs: model, kind, x, y, z, heading");
    if (!f[0][0] || strlen(f[0]) >= sizeof(out.model)) return fail("no model name (or one too long)");
    for (const char* c = f[0]; *c; ++c)
        if (!((*c >= 'A' && *c <= 'Z') || (*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9') || *c == '_'))
            return fail("the model name has a character no model name has");
    const bool quiet = _stricmp(f[1], "scenery") == 0 || _stricmp(f[1], "parked") == 0;
    const int kind = quiet ? static_cast<int>(kCar) : KindFromName(f[1]);
    if (kind == kNone) return fail("the kind must be boat, heli, car, plane or scenery");
    float v[4];
    for (int i = 0; i < 4; ++i)
    {
        char* end = nullptr;
        v[i] = strtof(f[2 + i], &end);
        if (end == f[2 + i] || *end || !(v[i] == v[i]) || std::fabs(v[i]) > 1.0e5f) return fail("x, y, z and heading must be numbers");
    }
    if (std::fabs(v[0]) > 5000.0f || std::fabs(v[2]) > 5000.0f || v[1] < -100.0f || v[1] > 500.0f) return fail("the position is outside the world");
    strcpy_s(out.model, f[0]);
    out.kind = kind;
    out.pos[0] = v[0]; out.pos[1] = v[1]; out.pos[2] = v[2];
    out.heading = v[3];
    strncpy_s(out.label, f[6], _TRUNCATE);
    out.quiet = quiet;
    out.on = true;
    return 0;
}

// The same stand? (a reload of the ini that changed a line lets go of that stand's vehicle)
inline bool SameStand(const Stand& a, const Stand& b)
{
    if (a.on != b.on) return false;
    if (!a.on) return true;
    return a.kind == b.kind && _stricmp(a.model, b.model) == 0 && a.pos[0] == b.pos[0] && a.pos[1] == b.pos[1] &&
           a.pos[2] == b.pos[2] && a.heading == b.heading && a.quiet == b.quiet;
}

// heading (degrees; 0 = +z, 90 = +x) -> the flat direction (x, z) the nose should point along
inline void HeadingDir(float deg, float out[2])
{
    const float r = deg * 0.017453292f;
    out[0] = std::sin(r); out[1] = std::cos(r);
}

// How far above the stand's y it is put down: a boat at the water's surface it was given (its own
// buoyancy finds the rest), everything else a little above the ground (airport.h's 0.3).
inline float SpawnLift(int kind) { return kind == kBoat ? 0.0f : 0.3f; }

// Parked on its stand (VehicleAction_SetParked's call)? [sites] park: 0 nothing; 1 (the default) cars
// and planes, as airport.h parks its planes; 2 boats as well. A boat is not parked by default because
// the scripts' boats for the player are not (ny_little_italy_2217_-_canal.sf: CreateAIVehicle and no
// more) - the only script boat that IS SetParked is a locked blocker (br_twin_bridge_1103_-_boats.sf),
// though SetParked has a boat branch of its own [P 0x1401941AE]. A helicopter never: no shipped
// script parks one (sf_residential_0822_helicoper.sf); it is settled instead (sites.h).
inline bool ParkKind(int kind, int level)
{
    return kind == kCar || kind == kPlane ? level >= 1 : kind == kBoat ? level >= 2 : false;
}

// What stands in for a model the game cannot give (unknown name, or its spawn failed for good):
// the game's own free-roam script boat and its commonest helicopter. "" = nothing stands in.
inline const char* FallbackModel(int kind)
{
    return kind == kBoat ? "Naval_Arowana" : kind == kHeli ? "Police_Chopper" : "";
}

// The first n stands that are on: any two closer than minSep (they would spawn into each other)?
// Returns the index of the first stand of such a pair, or -1.
inline int FirstCrowded(const Stand* s, int n, float minSep, int* other = nullptr)
{
    for (int i = 0; i < n; ++i)
        for (int j = i + 1; j < n; ++j)
        {
            if (!s[i].on || !s[j].on) continue;
            const float dx = s[i].pos[0] - s[j].pos[0], dz = s[i].pos[2] - s[j].pos[2];
            if (std::sqrt(dx * dx + dz * dz) < minSep) { if (other) *other = j; return i; }
        }
    return -1;
}

// ---------------------------------------------------------------------------------------------
//  The decisions
// ---------------------------------------------------------------------------------------------
struct Params
{
    float nearU = 70.0f;      // [sites] near
    float farU = 200.0f;      // [sites] far
    float leaveU = 120.0f;    // [sites] leave
    float onStandU = 6.0f;    // a vehicle stopped this close to its stand is "on the stand"
    float clearU = 5.0f;      // never put one down on a player (or another stand's vehicle) this close
    float keepU = 8.0f;       // never remove one with a player this close (aboard or not)
    float facingMin = 0.9f;   // cos of the most a fresh one may face off its heading (~26 deg)
    float turnWaitS = 4.0f;   // a wrongly-facing one fades; the turned one comes this much later
    float sunkU = 3.0f;       // this far under its stand = it fell through
    bool  park = true;        // this stand's kind is parked while it waits (ParkKind)
};

struct Facts                  // what the caller saw this tick
{
    float dt;
    bool  on;                 // lcudrive on and [sites] enable
    bool  world;              // player 1 exists and this is the level's live population manager
    bool  haveVeh;            // our weak ref is alive
    float best;               // the nearest player to the stand, u (1e9 = none)
    bool  someoneClose;       // a player or another stand's vehicle within clearU of the stand
    bool  aboard;             // a player is in it
    bool  onStand;            // it is within onStandU of the stand
    float speed;              // its speed, u/s
    bool  wrecked;            // its health is 0
    float nearVeh;            // the nearest player to it, u (1e9 = none; 0 = its position can't be read)
    bool  sunk;               // it is more than sunkU under its stand
    bool  mission;            // a story mission runs
    bool  samePm;             // this tick's population manager is the one the spawn attempt began with
    bool  facingKnown;        // its heading could be read
    float facingDot;          // cos between its nose and the stand's heading
    bool  hold;               // another stand is spawning / spawned this tick: do not begin now
    bool  settling;           // a player jumped (a teleport: the train tunnels, a fast travel) a moment ago
};

struct Mem { int state; float t, cool; bool parkPending; int turnTries, sunkTries; };
inline void Reset(Mem& m) { m.state = kIdle; m.t = 0.0f; m.cool = 0.0f; m.parkPending = false; m.turnTries = 0; m.sunkTries = 0; }

// W must have: bool Resolve(); void BeginSpawn(); int Spawn(float t); void AbandonSpawn();
// void Park(bool); void Pin(bool); void Settle(); void Fade(const char* why); void Disown(const char* why);
// void Dropped(); void Turn(int attempt); void Say(const char* fmt, ...).
template <class W> inline void SetState(W& w, Mem& m, int s)
{
    if (s != m.state) w.Say(">>> sites: %s -> %s\n", StateName(m.state), StateName(s));
    m.state = s; m.t = 0.0f;
}

// May it go now? Never with a player in it, never with a player within keepU of it.
inline bool MayRemove(const Params& p, const Facts& f) { return f.haveVeh && !f.aboard && f.nearVeh > p.keepU; }

template <class W> inline bool TryFade(W& w, Mem& m, const Params& p, const Facts& f, const char* why)
{
    if (!MayRemove(p, f)) return false;
    w.Fade(why);                                                        // pin off, VehicleAction_Destroy #FadeOut, ref dropped
    m.state = kIdle; m.t = 0.0f; m.parkPending = false;
    return true;
}

template <class W> inline void Abandon(W& w, Mem& m, const char* why)
{
    w.Say(">>> sites: spawn abandoned after %.1f s - %s\n", m.t, why);
    w.AbandonSpawn();                                                   // a preload the streaming took goes back
    m.cool = 5.0f;
    SetState(w, m, kIdle);
}

inline bool RetryStatus(int st) { return st == 1 || st == 2 || st == 8; }   // streaming / no room (SpawnAIVehicle)

template <class W> inline void Tick(W& w, Mem& m, const Params& p, const Facts& f)
{
    if (!f.haveVeh && m.state != kIdle && m.state != kSpawning)         // the game removed it
    {
        w.Say(">>> sites: the vehicle is gone (state %s)\n", StateName(m.state));
        w.Dropped();
        SetState(w, m, kIdle);
    }
    m.t += f.dt;
    if (m.cool > 0.0f) m.cool -= f.dt;
    // Boarded: from any state, switched on or not - it must not stay parked or pinned under its driver.
    if (f.haveVeh && f.aboard && m.state != kAboard)
    {
        w.Park(false); w.Pin(false);
        w.Say(">>> sites: boarded - it is the player's now\n");
        m.parkPending = false;
        SetState(w, m, kAboard);
    }
    if (m.state == kSpawning && (!f.on || !f.world)) Abandon(w, m, !f.on ? "switched off" : "the world changed");
    if (!f.on)
    {
        // stood down: the waiting ones go (never one a player is in or beside); nothing new
        if (m.state == kAboard && f.haveVeh && !f.aboard) SetState(w, m, kAway);    // he got out while it is off
        if (f.haveVeh && m.state != kAboard) TryFade(w, m, p, f, "switched off");
        return;
    }
    if (!f.world) return;
    // Just after a teleport (2026-11-16 crash, Notes 48 §4.2): the vehicles the teleport's script destroyed
    // are being freed this very frame. Nothing new is put down and nothing is faded until it has settled -
    // twice a boat spawned in that frame (in the train cab's freed block) later crashed the game when
    // destroyed. A spawn already under way finishes; boarding is handled above as always.
    if (f.settling && m.state != kSpawning && m.state != kAboard) return;
    const bool atStand = f.best < p.nearU;

    bool fresh = false;                                                 // the attempt began this tick
    switch (m.state)
    {
    case kIdle:
        if (!atStand || m.cool > 0.0f || f.mission) break;
        if (f.someoneClose || f.hold) break;                            // not onto a player; one spawn at a time
        if (!w.Resolve()) { m.cool = 600.0f; break; }
        w.BeginSpawn();                                                 // remembers this world
        SetState(w, m, kSpawning);
        fresh = true;
        [[fallthrough]];                                                // first attempt this tick
    case kSpawning:
    {
        if (!fresh)
        {
            if (!atStand)   { Abandon(w, m, "nobody near any more"); break; }
            if (f.mission)  { Abandon(w, m, "a story mission started"); break; }
            if (!f.samePm)  { Abandon(w, m, "another world"); break; }
        }
        const int st = w.Spawn(m.t);
        if (st == 0) { SetState(w, m, kParked); m.parkPending = true; }
        else if (RetryStatus(st) && m.t < 20.0f) { /* streaming / no room: again next tick */ }
        else
        {
            w.Say(">>> sites: spawn failed, status %d after %.1f s - retry in 60 s\n", st, m.t);
            w.AbandonSpawn();
            m.cool = 60.0f;
            SetState(w, m, kIdle);
        }
        break;
    }
    case kParked:
        if (!f.haveVeh) break;
        if (m.parkPending && m.t > 0.0f)
        {
            m.parkPending = false;
            if (f.facingKnown && f.facingDot < p.facingMin)
            {
                if (m.turnTries < 2 && MayRemove(p, f))
                {
                    w.Turn(m.turnTries++);                              // the next facing to send (reads this vehicle)
                    TryFade(w, m, p, f, "it faces off its heading - a turned one comes next");
                    m.cool = p.turnWaitS;
                    break;
                }
                if (m.turnTries >= 2)
                    w.Say(">>> sites: it still faces off its heading after 2 turned spawns (cos %.2f) - kept\n", f.facingDot);
            }
            if (p.park) w.Park(true);
            w.Pin(true);
            w.Settle();
        }
        if (f.sunk)
        {
            if (TryFade(w, m, p, f, "it sank under its stand - the ground or the water was not there"))
            {
                if (++m.sunkTries >= 3)
                {
                    w.Say(">>> sites: sunk 3 times - check this stand's y in [sites] (nothing here for 10 min)\n");
                    m.sunkTries = 0; m.cool = 600.0f;
                }
                else m.cool = 5.0f;
            }
        }
        else if (!f.onStand) { w.Pin(false); SetState(w, m, kAway); }
        else if (f.mission) { if (TryFade(w, m, p, f, "a story mission started")) m.cool = 30.0f; }
        else if (f.best > p.farU && f.nearVeh > p.farU) TryFade(w, m, p, f, "nobody near");
        else if (m.t > 5.0f) m.sunkTries = 0;                           // it stood 5 s: the stand holds
        break;
    case kAboard:
        if (!f.haveVeh) break;
        if (!f.aboard)
        {
            if (f.wrecked) SetState(w, m, kWreck);
            else if (f.onStand && f.speed < 0.5f)
            {
                if (p.park) w.Park(true);
                w.Pin(true);
                w.Say(">>> sites: back on its stand - it waits again\n");
                SetState(w, m, kParked);
            }
            else SetState(w, m, kAway);
        }
        break;
    case kAway:
        if (!f.haveVeh) break;
        if (f.wrecked) { SetState(w, m, kWreck); break; }
        if (f.onStand && f.speed < 0.5f)
        {
            if (p.park) w.Park(true);
            w.Pin(true);
            SetState(w, m, kParked);
            break;
        }
        if (f.nearVeh > p.leaveU && m.t > 10.0f) { if (TryFade(w, m, p, f, "left behind")) m.cool = 5.0f; }
        break;
    case kWreck:
        if (f.haveVeh && m.t > 6.0f) { if (TryFade(w, m, p, f, "wrecked")) m.cool = 20.0f; }
        break;
    default:
        break;
    }
}

// Letting go of one (unhook, a fault, its stand switched off or changed): faded if nobody is in it or
// within keepU of it; else it stays in the world - unparked, unpinned - and is ours no longer.
template <class W> inline void Release(W& w, Mem& m, const Params& p, const Facts& f, const char* why)
{
    if (m.state == kSpawning) w.AbandonSpawn();
    if (f.haveVeh)
    {
        w.Pin(false);
        if (MayRemove(p, f)) w.Fade(why);
        else { w.Park(false); w.Disown(why); }
    }
    else w.Dropped();
    Reset(m);
}

// ---------------------------------------------------------------------------------------------
//  Port Atlas as a place: a box on the map (x0..x1, z0..z1). A player is "at Port Atlas" from the
//  moment he is inside it until he is more than `margin` outside it (no flicker on the edge).
//  kDistrict is the index the radio gets there - one past the game's own 23 (radio.h kDistrictFill).
// ---------------------------------------------------------------------------------------------
enum : int { kDistrictPortAtlas = 23 };
struct Box { float x0, x1, z0, z1; };
inline Box MakeBox(float xa, float xb, float za, float zb)
{
    Box b = { xa < xb ? xa : xb, xa < xb ? xb : xa, za < zb ? za : zb, za < zb ? zb : za };
    return b;
}
inline bool InBox(const Box& b, float x, float z, float grow = 0.0f)
{
    return x >= b.x0 - grow && x <= b.x1 + grow && z >= b.z0 - grow && z <= b.z1 + grow;
}
// 0 = no change, 1 = arrived, -1 = left. `there` is the caller's memory for this player.
inline int Presence(const Box& b, float margin, bool known, float x, float z, bool& there)
{
    if (!known) return 0;                                               // no position: he is where he was
    if (!there && InBox(b, x, z)) { there = true; return 1; }
    if (there && !InBox(b, x, z, margin)) { there = false; return -1; }
    return 0;
}

// ---------------------------------------------------------------------------------------------
//  One spawner call per world update, shared by the airport planes (0), the T-Rexes (1) and the
//  stands (2): the turn goes round those that are running (mask bit i), one update each, so none
//  starves and no two call the game's spawners in the same update. -1 = nobody is running.
// ---------------------------------------------------------------------------------------------
enum : int { kTurnAirport = 0, kTurnDino = 1, kTurnSites = 2, kTurnSpots = 3, kTurnBackrooms = 4, kTurnAnimals = 5, kTurners = 6 };   // [round3] 5 = animals.h   // 3 = spots.h (people, route cars)  [backrooms1] 4 = backrooms.h (statues)
inline int NextTurn(int prev, unsigned mask)
{
    for (int k = 1; k <= kTurners; ++k)
    {
        const int i = ((prev < 0 ? -1 : prev) + k) % kTurners;
        if (mask & (1u << i)) return i;
    }
    return -1;
}

} // namespace sitem
