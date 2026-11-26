// =============================================================================================
//  airport_model.h - when the airport plane appears, waits, follows its pilot and goes (airport.h
//  is the engine side). Pure, the same contract as the other *_model.h: plain facts in, the actions
//  through the caller's World, so test\airplane_harness.cpp runs it as is.
//
//  The decisions are the staged airport.h Tick (planes_stage\airport, 2026-11-13),
//  moved here so they can be harness-tested, plus the review fixes of the same day:
//    idle      a player within `near` of the stand, no story mission, no cool-down, nobody standing
//              on the stand (within `clear`) -> resolve the model (bad name: 10 min cool-down) and
//    spawning  SpawnAIVehicle: 0 = spawned -> parked (park + pin on the NEXT tick, the UFO script's
//              wait(0)); 1 / 2 / 8 (streaming, no room) = again next tick for up to 20 s, as long as a
//              player is still at the airport, no story mission started and it is the same world
//              (else abandoned, 5 s); anything else = failed, retry in 60 s. A preload the streaming
//              took is given back whenever an attempt ends without a plane.
//    parked    the tick after the spawn: which way it faces is checked (a sign composition through
//              SpawnAIVehicle's facing - the rule: a watchdog); wrong -> faded and respawned turned,
//              at most twice. Boarded -> unparked, pin off -> aboard; nobody within `far` -> fade; a
//              story mission -> fade (30 s cool-down)
//    aboard    got out: wrecked -> wreck; on the stand and still -> parked again (+ pin); else away
//    away      back in -> aboard; wrecked -> wreck; nobody within `leave` for 10 s -> fade (5 s)
//    wreck     6 s later, nobody aboard -> fade (20 s)
//    any       the weak ref died (the game removed it) -> idle
//    Ctrl+Alt+F (recall): nobody at the airport / you are in it / a story mission / someone on the
//              stand / the model can't be spawned -> refused (low beep), the old plane stays; the
//              plane on the stand -> done (high beeps); otherwise the old one fades and a fresh one
//              comes now (high beeps).
//
//  Several stands (2026-11-14: several flyable planes at the airport): the airport keeps
//  [plane] airport_count planes (1..6), one per stand of StandAt, each with its own Mem and its own
//  Tick - so each appears, is boarded, left, wrecked and replaced on its own, by the rules above, with
//  `best` measured from ITS stand. What the stands share:
//    * one spawn at a time: a stand does not begin while another is spawning or spawned this tick
//      (Facts.hold) - one SpawnAIVehicle call per world update, one preload outstanding;
//    * a stand is not filled while a player OR another airport plane stands on it (someoneClose);
//    * Ctrl+Alt+F goes to EVERY stand: each applies the recall rule above to its own plane (the one a
//      player sits in and the ones already on their stands are left alone); the beep is high if any
//      stand answered "done", low only if every stand refused (RecallBeep);
//    * each stand names its model ([plane] airport_models, StandModel); a name the game does not know,
//      or one no flight system flies, falls back to [plane] airport_model (airport.h Resolve).
// =============================================================================================
#pragma once
#include <cmath>

namespace apm {

enum State : int { kIdle = 0, kSpawning, kParked, kAboard, kAway, kWreck };
inline const char* StateName(int s)
{
    static const char* n[] = { "idle", "spawning", "parked", "aboard", "away", "wreck" };
    return s >= 0 && s <= kWreck ? n[s] : "?";
}

struct Params
{
    float nearU = 90.0f;      // [plane] airport_near
    float farU = 220.0f;      // [plane] airport_far
    float leaveU = 60.0f;     // [plane] airport_leave
    float onStandU = 20.0f;   // a plane stopped this close to the stand is "on the stand"
    float clearU = 7.0f;      // never drop it on a player standing this close
    float facingMin = 0.9f;   // cos of the most a fresh plane may face off the runway (~26 deg)
    float turnWaitS = 4.0f;   // a wrongly-facing plane fades; the turned one comes this much later
};

struct Facts                  // what the caller saw this tick
{
    float dt;
    bool  on;                 // lcudrive on and the airport plane enabled
    bool  world;              // player 1 exists and this is the level's live population manager
    bool  recall;             // Ctrl+Alt+F since the last tick
    bool  haveVeh;            // our weak ref is alive
    float best;               // the nearest player to the stand, u (1e9 = none)
    bool  someoneClose;       // a player within clearU of the stand
    bool  aboard;             // a player is in the plane
    bool  onStand;            // the plane is within onStandU of the stand
    float speed;              // the plane's speed, u/s
    bool  wrecked;            // its health is 0
    float nearPlane;          // the nearest player to the plane, u
    bool  mission;            // a story mission runs
    bool  samePm;             // this tick's population manager is the one the spawn attempt began with
    bool  facingKnown;        // the plane's heading could be read
    float facingDot;          // cos between its nose and the stand's heading (on the parking tick)
    bool  hold;               // another stand is spawning / spawned this tick: do not begin now
};

struct Mem { int state; float t, cool; bool parkPending; bool badModel; int turnTries; };
inline void Reset(Mem& m) { m.state = kIdle; m.t = 0.0f; m.cool = 0.0f; m.parkPending = false; m.badModel = false; m.turnTries = 0; }

// W must have: bool Resolve(); void BeginSpawn(); int Spawn(float t); void AbandonSpawn(); void Park(bool);
// void Pin(bool); void Fade(const char* why); void Dropped(); void Turn(int attempt);
// void Beep(int code /* 1 done, 2 refused */); void Say(const char* fmt, ...).
template <class W> inline void SetState(W& w, Mem& m, int s)
{
    if (s != m.state) w.Say(">>> airport: %s -> %s\n", StateName(m.state), StateName(s));
    m.state = s; m.t = 0.0f;
}

template <class W> inline void Fade(W& w, Mem& m, const char* why)
{
    w.Fade(why);                                                        // pin off, VehicleAction_Destroy #FadeOut, ref dropped
    m.state = kIdle; m.t = 0.0f;
}

template <class W> inline void Abandon(W& w, Mem& m, const char* why)
{
    w.Say(">>> airport: spawn abandoned after %.1f s - %s\n", m.t, why);
    w.AbandonSpawn();                                                   // a preload the streaming took goes back
    m.cool = 5.0f;
    SetState(w, m, kIdle);
}

template <class W> inline void Tick(W& w, Mem& m, const Params& p, const Facts& in)
{
    Facts f = in;
    if (!f.haveVeh && m.state != kIdle && m.state != kSpawning)         // the game removed it
    {
        w.Say(">>> airport: the plane is gone (state %s)\n", StateName(m.state));
        w.Dropped();
        SetState(w, m, kIdle);
    }
    m.t += f.dt;
    if (m.cool > 0.0f) m.cool -= f.dt;
    if (m.state == kSpawning && (!f.on || !f.world)) Abandon(w, m, !f.on ? "switched off" : "the world changed");
    if (!f.on) { if (f.recall) w.Beep(2); return; }
    if (!f.world) return;
    const bool atAirport = f.best < p.nearU;

    if (f.recall)
    {
        if (!atAirport) { w.Say(">>> airport: Ctrl+Alt+F - nobody is at the airport (nearest %.0f u)\n", f.best); w.Beep(2); }
        else if (f.aboard) w.Beep(2);
        else if (f.haveVeh && f.onStand) { w.Say(">>> airport: Ctrl+Alt+F - the plane is already on its stand\n"); w.Beep(1); }
        else if (f.mission) { w.Say(">>> airport: Ctrl+Alt+F - not during a story mission\n"); w.Beep(2); }
        else if (f.someoneClose) { w.Say(">>> airport: Ctrl+Alt+F - step off the stand first (%.0f u)\n", p.clearU); w.Beep(2); }
        else if (m.badModel && !w.Resolve()) w.Beep(2);                  // (Resolve says why; an ini fix is picked up here)
        else
        {
            if (f.haveVeh) { Fade(w, m, "Ctrl+Alt+F: a fresh one on the stand"); f.haveVeh = false; f.aboard = false; }
            m.cool = 0.0f; m.badModel = false;
            w.Beep(1);
        }
    }

    bool fresh = false;                                                 // the attempt began this tick
    switch (m.state)
    {
    case kIdle:
        if (!atAirport || m.cool > 0.0f || f.mission) break;
        if (f.someoneClose) return;                                     // don't drop it on a player (or a plane)
        if (f.hold) break;                                              // one spawn at a time (the stands take turns)
        if (!w.Resolve()) { m.cool = 600.0f; m.badModel = true; break; }
        m.badModel = false;
        w.BeginSpawn();                                                 // remembers this world
        SetState(w, m, kSpawning);
        fresh = true;
        [[fallthrough]];                                                // first attempt this tick
    case kSpawning:
    {
        if (!fresh)
        {
            if (!atAirport) { Abandon(w, m, "nobody at the airport any more"); break; }
            if (f.mission)  { Abandon(w, m, "a story mission started"); break; }
            if (!f.samePm)  { Abandon(w, m, "another world"); break; }
        }
        const int st = w.Spawn(m.t);
        if (st == 0) { SetState(w, m, kParked); m.parkPending = true; }
        else if ((st == 1 || st == 2 || st == 8) && m.t < 20.0f) { /* streaming / no room: again next tick */ }
        else
        {
            w.Say(">>> airport: spawn failed, status %d after %.1f s - retry in 60 s\n", st, m.t);
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
            if (!f.aboard && f.facingKnown && f.facingDot < p.facingMin)
            {
                if (m.turnTries < 2)
                {
                    w.Turn(m.turnTries++);                              // the next facing to send (reads this plane)
                    Fade(w, m, "it faces off its heading - a turned one comes next");
                    m.cool = p.turnWaitS;
                    break;
                }
                w.Say(">>> airport: the plane still faces off its heading after 2 turned spawns (cos %.2f) - kept\n", f.facingDot);
            }
            if (!f.aboard) { w.Park(true); w.Pin(true); }
        }
        if (f.aboard)
        {
            w.Park(false); w.Pin(false);
            w.Say(">>> airport: boarded - unparked; the runway is the north quay (z 352): take off toward +x, quay edge at x 210\n");
            SetState(w, m, kAboard);
        }
        else if (f.best > p.farU) Fade(w, m, "nobody near the airport");
        else if (f.mission) { Fade(w, m, "a story mission started"); m.cool = 30.0f; }
        break;
    case kAboard:
        if (!f.haveVeh) break;
        if (!f.aboard)
        {
            if (f.wrecked) SetState(w, m, kWreck);
            else if (f.onStand && f.speed < 0.5f)
            {
                w.Park(true); w.Pin(true);
                w.Say(">>> airport: back on the stand - parked\n");
                SetState(w, m, kParked);
            }
            else SetState(w, m, kAway);
        }
        break;
    case kAway:
        if (!f.haveVeh) break;
        if (f.aboard) { SetState(w, m, kAboard); break; }
        if (f.wrecked) SetState(w, m, kWreck);
        else if (f.nearPlane > p.leaveU && m.t > 10.0f) { Fade(w, m, "left behind"); m.cool = 5.0f; }
        break;
    case kWreck:
        if (f.haveVeh && m.t > 6.0f && !f.aboard) { Fade(w, m, "wrecked"); m.cool = 20.0f; }
        break;
    default:
        break;
    }
}

// ---------------------------------------------------------------------------------------------
//  The stands. World coordinates (u), y = the apron (KdTerrain 1.637 inside 3.6 u of every stand
//  but the runway start, whose west rim is the garage kerb) [P: planes2_work\spotcheck.py over the
//  cells' KdTerrain at 0.25 u, the render obstacles, the props and the 12 static planes' bodies].
//  Stand 0 is the runway start (the only one on the quay's centre line z 352). The others stand on
//  the apron south of the quay, hulls clear of the runway corridor (z 349.6..355.6): centres at least
//  13.4 u from each other (an Elifant spans 4.27 u and is 4.94 u long: two spans of air or more
//  between any two), at least 8.2 u from the centre of every static plane (bodies of r 3.4: 1.4 u or
//  more between hulls, 2.5 u or more for stands 1-3), 4.5 u or more from every prop.
//    1  (181.5, 344.0) the apron just south of the quay, nose north-west: roll onto the quay toward
//       the runway start;
//    2  (172.0, 330.0) the middle apron, nose north-east: out through the gap east of the green
//       static plane (177.5, 339.5);
//    3  (207.5, 322.5) the east pier head;  4  (188.5, 301.5) and 5  (198.0, 313.0) the south apron
//       (overflow: tighter, between static planes; taxi north past stand 2 to the quay).
// ---------------------------------------------------------------------------------------------
enum : int { kMaxStands = 6 };
struct Stand { float pos[3]; float facing[2]; bool runway; const char* what; };
inline const Stand& StandAt(int i)
{
    static const Stand k[kMaxStands] = {
        { { 150.0f, 1.64f, 352.0f }, {  1.0f,        0.0f        }, true,  "the runway start" },
        { { 181.5f, 1.64f, 344.0f }, { -0.70710678f, 0.70710678f }, false, "the quay apron" },
        { { 172.0f, 1.64f, 330.0f }, {  0.70710678f, 0.70710678f }, false, "the middle apron" },
        { { 207.5f, 1.64f, 322.5f }, { -1.0f,        0.0f        }, false, "the east pier" },
        { { 188.5f, 1.64f, 301.5f }, {  0.0f,        1.0f        }, false, "the south apron" },
        { { 198.0f, 1.64f, 313.0f }, { -0.70710678f, 0.70710678f }, false, "the south-east apron" },
    };
    return k[i < 0 ? 0 : i >= kMaxStands ? kMaxStands - 1 : i];
}
inline int StandCount(int n) { return n < 1 ? 1 : n > kMaxStands ? kMaxStands : n; }

// How close to its stand a stopped plane counts as "back on the stand" (parked again, pinned): the
// runway start keeps the 20 u it always had; an apron stand 8 u (they are 13 u apart).
inline float StandRadius(int i, float runwayU) { return StandAt(i).runway ? runwayU : (runwayU < 8.0f ? runwayU : 8.0f); }

// The i-th name of a comma separated list ([plane] airport_models); an empty or missing entry, or
// one too long for `out`, gives `fallback`. Returns true when the list itself named it.
inline bool StandModel(const char* list, int i, const char* fallback, char* out, size_t cap)
{
    auto put = [&](const char* s, size_t n) { size_t k = 0; for (; k < n && k + 1 < cap; ++k) out[k] = s[k]; if (cap) out[k] = 0; };
    size_t fn = 0; while (fallback && fallback[fn]) ++fn;
    const char* s = list ? list : "";
    for (int k = 0; ; ++k)
    {
        while (*s == ' ' || *s == '\t') ++s;
        const char* e = s;
        while (*e && *e != ',' && *e != ' ' && *e != '\t') ++e;
        if (k == i)
        {
            const size_t n = static_cast<size_t>(e - s);
            if (n > 0 && n + 1 <= cap) { put(s, n); return true; }
            break;
        }
        while (*e == ' ' || *e == '\t') ++e;
        if (*e != ',') break;                                           // the list is shorter than i
        s = e + 1;
    }
    put(fallback ? fallback : "", fn);
    return false;
}

// Ctrl+Alt+F went to every stand; each answered 0 (nothing), 1 (done / already there) or 2 (refused).
// One beep for the lot: done if any stand is done, refused only if none is.
inline int RecallBeep(const int* codes, int n)
{
    int out = 0;
    for (int i = 0; i < n; ++i) { if (codes[i] == 1) return 1; if (codes[i] == 2) out = 2; }
    return out;
}

// The stand table's own invariants (harness-tested, and checked once by airport.h): the first n stands
// are at least minSep apart, only stand 0 is on the runway, every other stand's hull circle (r) stays
// south of the runway corridor's edge.
inline bool StandsOk(int n, float minSep, float corridorSouthZ, float r)
{
    n = StandCount(n);
    for (int i = 0; i < n; ++i)
    {
        const Stand& a = StandAt(i);
        const float l = std::sqrt(a.facing[0] * a.facing[0] + a.facing[1] * a.facing[1]);
        if (!(l > 0.999f && l < 1.001f)) return false;
        if (a.runway != (i == 0)) return false;
        if (i > 0 && a.pos[2] + r > corridorSouthZ) return false;
        for (int j = i + 1; j < n; ++j)
        {
            const Stand& b = StandAt(j);
            const float dx = a.pos[0] - b.pos[0], dz = a.pos[2] - b.pos[2];
            if (std::sqrt(dx * dx + dz * dz) < minSep) return false;
        }
    }
    return true;
}

// Which system flies [plane] airport_model - the airport plane is on only while one does: 0 = [airplane]
// (a car-class airliner on its own wheels, the default Aircraft_Elifant), 1 = [plane] (a Hover model),
// 2 = neither (it would be a parked car or a helicopter: no airport plane).
inline int Owner(bool airplaneOn, bool airplaneListed, bool planeOn, bool planeListed)
{
    return airplaneOn && airplaneListed ? 0 : planeOn && planeListed ? 1 : 2;
}

// The facing watchdog's correction (x, z only). SpawnAIVehicle maps the facing we send to the nose it
// builds through an orthogonal 2D map M - the identity if the row construction is what the disassembly
// says [P rows, I-strong atan2 order]. One sample (sent -> got) cannot tell a rotation from a reflection,
// so: attempt 0 assumes a rotation (send want turned back by the error), attempt 1 a reflection about the
// bisector of sent and got (its own inverse: send M(want)). Writes the next facing to send; false if the
// sample is unusable.
inline bool NextFacing(int attempt, const float sent[2], const float got[2], const float want[2], float out[2])
{
    const float ls = std::sqrt(sent[0] * sent[0] + sent[1] * sent[1]), lg = std::sqrt(got[0] * got[0] + got[1] * got[1]);
    if (!(ls > 1e-3f) || !(lg > 1e-3f)) return false;
    const float s[2] = { sent[0] / ls, sent[1] / ls }, g[2] = { got[0] / lg, got[1] / lg };
    if (attempt == 0)
    {
        // M = rotation by e (sent -> got); send R(-e) want
        const float c = s[0] * g[0] + s[1] * g[1], sn = s[0] * g[1] - s[1] * g[0];
        out[0] = c * want[0] + sn * want[1];
        out[1] = -sn * want[0] + c * want[1];
    }
    else
    {
        // M = reflection about the unit axis a between sent and got; M(x) = 2 (x.a) a - x
        float a[2] = { s[0] + g[0], s[1] + g[1] };
        float la = std::sqrt(a[0] * a[0] + a[1] * a[1]);
        if (la < 1e-3f) { a[0] = -s[1]; a[1] = s[0]; la = 1.0f; }      // got = -sent: the axis is perpendicular
        a[0] /= la; a[1] /= la;
        const float d = want[0] * a[0] + want[1] * a[1];
        out[0] = 2.0f * d * a[0] - want[0];
        out[1] = 2.0f * d * a[1] - want[1];
    }
    const float lo = std::sqrt(out[0] * out[0] + out[1] * out[1]);
    if (!(lo > 1e-3f)) return false;
    out[0] /= lo; out[1] /= lo;
    return true;
}

} // namespace apm
