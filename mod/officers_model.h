// =============================================================================================
//  officers_model.h - one police officer on foot, the decisions only (Notes\38). Pure, so
//  test\wanted_harness.cpp runs it as is; wanted_officers.h carries the orders out with the
//  game's own AI.
//
//  Until 2026-10-25 the police never left their vehicles to pursue the player, so a
//  stun or tie with nobody outside to fire it looked wrong; and cops walking about did not
//  join in on the chase. An officer here comes either out of a chasing car (GetOut first) or
//  off the street (already on foot). While you're wanted and on foot he runs after you, and the
//  order is renewed as you move. Up close he fights you, like the game's hostile NPCs do (the
//  design, same day: the officers on foot mainly fight the player the way hostile NPCs would,
//  with a small chance that one fires the grapple). Still closing in, the director may
//  pick him to fire the grapple: he takes aim for aimS - the wind-up you can see - and the tie
//  lands only if you're still within reach at the end. You drive off or the chase ends: he goes
//  back to his car if it's close (Back); otherwise he's let go (Free).
// =============================================================================================
#pragma once
#include <cstdint>

namespace officers {

struct Params
{
    float getOutS     = 5.0f;    // s for the exit before he's forgotten (the car carries on)
    float retaskS     = 0.75f;   // s between renewals of the run order
    float retaskMove  = 3.0f;    // u you move that renews it at once
    float aimS        = 0.6f;    // s of aiming before the hit lands
    float fireRange   = 10.0f;   // u: close enough to be picked to fire
    float reachMul    = 1.3f;    // ...and the hit lands if you're within fireRange x this at the end
    float giveUpRange = 70.0f;   // u: an officer this far behind gives up
    float backRange   = 30.0f;   // u from his car: he goes back to it when the chase on foot ends
    float backS       = 12.0f;   // s to get back in before he's let go
    float fightRange  = 2.5f;    // u: close enough to fight (0 = he never fights)
    float breakRange  = 5.0f;    // u: you got this far away mid-fight: he runs after you again
    float refightS    = 3.0f;    // s between renewals of the fight order
};

enum Phase : uint8_t { kGetOut, kRun, kAim, kBack, kGone, kFight };

// What the glue does this tick for him.
enum Order : uint8_t
{
    kNone,
    kRunOrder,     // (re)issue "run to the player"
    kAimOrder,     // stop, face the player, play the aim; the shot lands after aimS
    kFire,         // the shot lands now: the stun / tie on the player; then he runs on
    kMissed,       // the wind-up ended with you out of reach: no hit; he runs on
    kBackOrder,    // walk back and get into his car
    kRelease,      // let him go (the game's own on-foot routine) and forget him
    kForget,       // forget him, no call (he's back in his car, or never got out)
    kFightOrder,   // (re)issue "fight the player" (the game's own attack task)
};

struct Unit
{
    uint8_t ph;
    float   t;          // s in this phase
    float   retask;     // s until the run order is renewed
    bool    hasCar;     // he came out of a car
};

inline void Start(Unit& u, bool fromCar)
{
    u.ph = fromCar ? kGetOut : kRun; u.t = 0.0f; u.retask = 0.0f; u.hasCar = fromCar;
}

struct In
{
    float dt;
    bool  chase;        // you're wanted and on foot (not busted, not in a vehicle)
    bool  wanted;       // the level is above 0
    bool  youDown;      // stunned or tied right now
    float dist;         // u from him to you
    bool  onFoot;       // he's on foot now (out of any vehicle)
    bool  carAlive;     // his car still exists
    float carDist;      // u from him to his car
    float youMoved;     // u you moved since his last run order
    bool  fire;         // the director picked him to fire now
};

inline Order Step(const Params& p, Unit& u, const In& in)
{
    const float dt = in.dt > 0.0f && in.dt < 1.0f ? in.dt : 0.0f;
    u.t += dt;
    switch (u.ph)
    {
    case kGetOut:
        if (in.onFoot)
        {
            if (!in.chase) { u.ph = kBack; u.t = 0.0f; return kBackOrder; }   // you drove off meanwhile
            u.ph = kRun; u.t = 0.0f; u.retask = p.retaskS; return kRunOrder;
        }
        if (!in.wanted || u.t > p.getOutS) { u.ph = kGone; return kForget; }  // still in: the car carries on
        return kNone;

    case kRun:
        if (!in.chase || in.dist > p.giveUpRange)
        {
            if (u.hasCar && in.carAlive && in.carDist < p.backRange && in.wanted)
            { u.ph = kBack; u.t = 0.0f; return kBackOrder; }
            u.ph = kGone; return kRelease;
        }
        if (in.fire && in.dist < p.fireRange && in.dist >= p.fightRange && !in.youDown)   // the grapple: from range
        { u.ph = kAim; u.t = 0.0f; return kAimOrder; }
        if (in.dist < p.fightRange) { u.ph = kFight; u.t = 0.0f; u.retask = p.refightS; return kFightOrder; }
        u.retask -= dt;
        if (u.retask <= 0.0f || in.youMoved > p.retaskMove) { u.retask = p.retaskS; return kRunOrder; }
        return kNone;

    case kFight:
        if (!in.chase || in.dist > p.giveUpRange)
        {
            if (u.hasCar && in.carAlive && in.carDist < p.backRange && in.wanted)
            { u.ph = kBack; u.t = 0.0f; return kBackOrder; }
            u.ph = kGone; return kRelease;
        }
        if (in.dist > p.breakRange) { u.ph = kRun; u.t = 0.0f; u.retask = p.retaskS; return kRunOrder; }
        u.retask -= dt;
        if (u.retask <= 0.0f) { u.retask = p.refightS; return kFightOrder; }
        return kNone;

    case kAim:
        if (u.t < p.aimS) return kNone;
        u.ph = kRun; u.t = 0.0f; u.retask = p.retaskS;
        return in.chase && !in.youDown && in.dist < p.fireRange * p.reachMul ? kFire : kMissed;

    case kBack:
        if (!in.onFoot) { u.ph = kGone; return kForget; }                     // back in: the pursuit has him
        if (in.chase) { u.ph = kRun; u.t = 0.0f; u.retask = p.retaskS; return kRunOrder; }   // you're out again
        if (!in.carAlive || u.t > p.backS) { u.ph = kGone; return kRelease; }
        return kNone;

    default:
        return kNone;
    }
}

// How many officers may be on foot after you at each level (index = level).
static const int kMaxOnFoot[6] = { 0, 2, 3, 4, 5, 6 };

} // namespace officers
