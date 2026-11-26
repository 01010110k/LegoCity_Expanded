// =============================================================================================
//  dino_model.h - when a T-Rex appears on its stand at the airport, waits, is ridden, is left
//  somewhere and goes (dino.h is the engine side). Pure, the same contract as airport_model.h:
//  plain facts in, the actions through the caller's World, so test\dino_harness.cpp runs it as is.
//
//  One Mem per T-Rex ([dino] count of them, each on its own stand):
//    idle      a player within `near` of the stand, no story mission, no cool-down, nobody (no
//              player, no other T-Rex) within `clear` of the stand, no other stand spawning ->
//              resolve the class / type (unknown: 10 min cool-down) and
//    spawning  the character spawner: 0 = there -> standing; 0xC (its definition is streaming in)
//              and 0xB (the population manager had no free slot and made room) = again next tick,
//              for up to 20 s, as long as a player is still near, no story mission started and it is
//              the same world (else abandoned, 5 s); anything else = failed, retry in 60 s. A
//              preload the streaming took is given back whenever an attempt ends without a T-Rex.
//    standing  a player got on -> ridden; it is no longer on its stand -> away; a story mission
//              started, or nobody within `far` of the stand AND of the T-Rex -> removed
//    ridden    the rider got off: on the stand -> standing, else -> away
//    away      ridden again -> ridden; back on its stand -> standing; nobody within `leave` of it
//              for 10 s -> removed (5 s later a fresh one may stand on the stand)
//    leaving   removed: waiting for the game to take it away (the weak ref dies -> idle); still
//              there after 10 s -> the population manager's own removal; after 30 s -> given up
//              (ours no longer; nothing new on this stand for 10 min)
//    any       the weak ref died (the game removed it) -> idle
//  The one rule everything else bends to: a T-Rex is NEVER removed while a player rides it or
//  stands within `keep` of it (the game's kill first kills, type 3, the character its ch+0xD00
//  names [P 0x14079AD2D..0x14079AD76] - the rider [I]) - Remove() is only reached through
//  TryRemove / Release, and the harness fuzzes that.
// =============================================================================================
#pragma once
#include <cmath>

namespace dnm {

enum State : int { kIdle = 0, kSpawning, kStanding, kRidden, kAway, kLeaving };
inline const char* StateName(int s)
{
    static const char* n[] = { "idle", "spawning", "standing", "ridden", "away", "leaving" };
    return s >= 0 && s <= kLeaving ? n[s] : "?";
}

// what the character spawner 0x1406FE180 writes to its status [P]
enum Status : int { kOk = 0, kNoStreamer = 4, kRefused = 0xA, kNoSlot = 0xB, kStreaming = 0xC, kNotLoaded = 0xE, kCreateFailed = 0xF };
inline bool Retry(int st) { return st == kStreaming || st == kNoSlot; }

struct Params
{
    float nearU = 90.0f;      // [dino] near
    float farU = 220.0f;      // [dino] far
    float leaveU = 60.0f;     // [dino] leave
    float onStandU = 4.0f;    // a T-Rex this close to its stand is "on the stand"
    float clearU = 3.0f;      // never drop one on a player (or another T-Rex) standing this close
    float keepU = 8.0f;       // never remove one with a player this close (ridden or not)
};

struct Facts                  // what the caller saw this tick
{
    float dt;
    bool  on;                 // lcudrive on and [dino] enable
    bool  world;              // player 1 exists and this is the level's live population manager
    bool  have;               // our weak ref is alive
    float best;               // the nearest player to the stand, u (1e9 = none)
    float nearDino;           // the nearest player to the T-Rex, u (1e9 = none / no T-Rex)
    bool  someoneClose;       // a player or another T-Rex within clearU of the stand
    bool  ridden;             // a player rides it
    bool  onStand;            // it is within onStandU of the stand
    bool  mission;            // a story mission runs
    bool  samePm;             // this tick's population manager is the one the spawn attempt began with
    bool  hold;               // another stand is spawning / spawned this tick: do not begin now
};

struct Mem { int state; float t, cool; int tries; };
inline void Reset(Mem& m) { m.state = kIdle; m.t = 0.0f; m.cool = 0.0f; m.tries = 0; }

// W must have: bool Resolve(); void BeginSpawn(); int Spawn(float t); void AbandonSpawn();
// void Remove(const char* why); void Purge(); void Forget(const char* why); void Dropped();
// void Say(const char* fmt, ...).
template <class W> inline void SetState(W& w, Mem& m, int s)
{
    if (s != m.state) w.Say(">>> dino: %s -> %s\n", StateName(m.state), StateName(s));
    m.state = s; m.t = 0.0f;
}

// May it go now? Never with a rider, never with a player within keepU of it.
inline bool MayRemove(const Params& p, const Facts& f) { return f.have && !f.ridden && f.nearDino > p.keepU; }

template <class W> inline bool TryRemove(W& w, Mem& m, const Params& p, const Facts& f, const char* why)
{
    if (!MayRemove(p, f)) return false;
    w.Remove(why);                                                      // the script's own Kill; the ref stays until it is gone
    m.tries = 0;
    SetState(w, m, kLeaving);
    return true;
}

template <class W> inline void Abandon(W& w, Mem& m, const char* why)
{
    w.Say(">>> dino: spawn abandoned after %.1f s - %s\n", m.t, why);
    w.AbandonSpawn();                                                   // a preload the streaming took goes back
    m.cool = 5.0f;
    SetState(w, m, kIdle);
}

template <class W> inline void Tick(W& w, Mem& m, const Params& p, const Facts& f)
{
    if (!f.have && m.state != kIdle && m.state != kSpawning)            // the game removed it (or our Remove landed)
    {
        if (m.state != kLeaving) w.Say(">>> dino: the T-Rex is gone (state %s)\n", StateName(m.state));
        w.Dropped();
        SetState(w, m, kIdle);
    }
    m.t += f.dt;
    if (m.cool > 0.0f) m.cool -= f.dt;
    if (m.state == kSpawning && (!f.on || !f.world)) Abandon(w, m, !f.on ? "switched off" : "the world changed");
    if (!f.on || !f.world) return;
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
        if (st == kOk) SetState(w, m, kStanding);
        else if (Retry(st) && m.t < 20.0f) { /* streaming / no slot: again next tick */ }
        else
        {
            w.Say(">>> dino: spawn failed, status %d after %.1f s - retry in 60 s\n", st, m.t);
            w.AbandonSpawn();
            m.cool = 60.0f;
            SetState(w, m, kIdle);
        }
        break;
    }
    case kStanding:
        if (!f.have) break;
        if (f.ridden) { w.Say(">>> dino: a player got on\n"); SetState(w, m, kRidden); }
        else if (!f.onStand) SetState(w, m, kAway);
        else if (f.mission) { if (TryRemove(w, m, p, f, "a story mission started")) m.cool = 30.0f; }
        else if (f.best > p.farU && f.nearDino > p.farU) TryRemove(w, m, p, f, "nobody near");
        break;
    case kRidden:
        if (!f.have) break;
        if (!f.ridden)
        {
            w.Say(">>> dino: the rider got off %s\n", f.onStand ? "on the stand" : "away from the stand");
            SetState(w, m, f.onStand ? kStanding : kAway);
        }
        break;
    case kAway:
        if (!f.have) break;
        if (f.ridden) { SetState(w, m, kRidden); break; }
        if (f.onStand) { SetState(w, m, kStanding); break; }
        if (f.nearDino > p.leaveU && m.t > 10.0f) { if (TryRemove(w, m, p, f, "left behind")) m.cool = 5.0f; }
        break;
    case kLeaving:
        if (!f.have) break;
        if (f.ridden) { w.Say(">>> dino: it is being ridden again - it stays\n"); SetState(w, m, kRidden); break; }
        if (m.t > 10.0f && m.tries == 0 && MayRemove(p, f))
        {
            m.tries = 1;
            w.Say(">>> dino: still there 10 s after its removal - the population manager's own removal now\n");
            w.Purge();
        }
        else if (m.t > 30.0f)
        {
            w.Forget("it would not go - ours no longer; nothing new on this stand for 10 min");
            m.cool = 600.0f;
            SetState(w, m, kIdle);
        }
        break;
    default:
        break;
    }
}

// Letting go of one (unhook, a fault, its stand switched off): removed if nobody rides it or stands
// within keepU of it, else it stays in the world with its rider and is ours no longer.
template <class W> inline void Release(W& w, Mem& m, const Params& p, const Facts& f, const char* why)
{
    if (m.state == kSpawning) w.AbandonSpawn();
    if (f.have)
    {
        if (MayRemove(p, f)) w.Remove(why);
        w.Forget(MayRemove(p, f) ? why : "it stays with its rider, no longer ours");
    }
    else w.Dropped();
    Reset(m);
}

// ---------------------------------------------------------------------------------------------
//  The stands: the first at ([dino] x, y, z), each further one `stepX, stepZ` on from the last.
//  The defaults (183, 1.64, 324) and (189, 1.64, 323) are the open apron south-west of the dock
//  basin [P: planes2_work\spotcheck.py over the cells' KdTerrain at 0.25 u: y 1.637 everywhere
//  inside 3.6 u of both (4 samples of the second fall off the basin's edge 3.4 u away), 5.1 u or
//  more to every render obstacle, 4.9 u to the nearest prop, 8.3 u to the nearest static plane's
//  centre]; 12.5 u or more from the three default plane stands of airport_model.h, 13.5 u from
//  the other three, 26 u south of the runway corridor.
// ---------------------------------------------------------------------------------------------
enum : int { kMaxDinos = 4 };
inline int Count(int n) { return n < 0 ? 0 : n > kMaxDinos ? kMaxDinos : n; }
inline void StandPos(int i, const float base[3], float stepX, float stepZ, float out[3])
{
    const float k = static_cast<float>(i < 0 ? 0 : i >= kMaxDinos ? kMaxDinos - 1 : i);
    out[0] = base[0] + k * stepX; out[1] = base[1]; out[2] = base[2] + k * stepZ;
}

// [dino] heading (degrees; 0 = +z, 90 = +x) -> the direction the spawner takes (x, z). It turns the
// pair back into an angle with atan2(x, z) [P 0x1406FE368..0x1406FE393], so this is its inverse.
inline void HeadingDir(float deg, float out[2])
{
    const float r = deg * 0.017453292f;
    out[0] = std::sin(r); out[1] = std::cos(r);
}

// The stands' own check: the first n are at least minSep apart (a step of 0 would stack them).
inline bool StandsOk(int n, const float base[3], float stepX, float stepZ, float minSep)
{
    n = Count(n);
    for (int i = 0; i < n; ++i)
        for (int j = i + 1; j < n; ++j)
        {
            float a[3], b[3];
            StandPos(i, base, stepX, stepZ, a); StandPos(j, base, stepX, stepZ, b);
            const float dx = a[0] - b[0], dz = a[2] - b[2];
            if (std::sqrt(dx * dx + dz * dz) < minSep) return false;
        }
    return true;
}

// ---------------------------------------------------------------------------------------------
//  Groups (2026-11-15, the cut-content pass): the stands come in kGroups groups, each with its own
//  creature, place and heading - group 0 is [dino] model / count / x.. (the T-Rex, unchanged), group 1
//  is [dino] model2 / count2 / x2.. (the Pteranodon on Port Atlas's lookout). Slot s is stand
//  SlotStand(s) of group SlotGroup(s); every rule above runs per slot, and the cross-stand checks
//  (nobody dropped onto another creature, one spawn at a time) run over every slot of every group.
// ---------------------------------------------------------------------------------------------
enum : int { kGroups = 2, kSlots = kGroups * kMaxDinos };
inline int SlotGroup(int s) { return s < 0 ? 0 : s >= kSlots ? kGroups - 1 : s / kMaxDinos; }
inline int SlotStand(int s) { return s < 0 ? 0 : s >= kSlots ? kMaxDinos - 1 : s % kMaxDinos; }

// Two groups' first na / nb stands are all at least minSep apart from each other.
inline bool GroupsApart(int na, const float baseA[3], float sxA, float szA,
                        int nb, const float baseB[3], float sxB, float szB, float minSep)
{
    na = Count(na); nb = Count(nb);
    for (int i = 0; i < na; ++i)
        for (int j = 0; j < nb; ++j)
        {
            float a[3], b[3];
            StandPos(i, baseA, sxA, szA, a); StandPos(j, baseB, sxB, szB, b);
            const float dx = a[0] - b[0], dz = a[2] - b[2];
            if (std::sqrt(dx * dx + dz * dz) < minSep) return false;
        }
    return true;
}

// What the log calls a creature: the T-Rex by its own name, any other by its AITypes type name.
inline const char* CreatureName(const char* model)
{
    if (!model || !*model) return "creature";
    const char* b = "behemoth";
    const char* m = model;
    for (; *m && *b; ++m, ++b)
    {
        const char c = (*m >= 'A' && *m <= 'Z') ? static_cast<char>(*m - 'A' + 'a') : *m;
        if (c != *b) return model;
    }
    return (*m == 0 && *b == 0) ? "T-Rex" : model;
}

} // namespace dnm
