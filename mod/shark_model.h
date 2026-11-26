// =============================================================================================
//  shark_model.h - the rare shark (Notes\61). Pure logic: no Windows, no game memory.
//  The design (2026-11-19): the shark is not guaranteed. A long time in the water brings a chance
//  of being eaten, but in general a player can swim across.
//
//  One State per player (co-op: each player has their own clock; one player's swim never puts the
//  other at risk). Each game tick shark.h fills an In from the game and calls Step(); Allow() then
//  says whether the game's own deep-water rule (the stock shark) may act on that player right now.
//
//  The clock:  deepT = seconds of CONTINUOUS swimming over deep water ("deadly" terrain, the sea
//  floor that is type 18 "Sharks!" in stuff/game.txt). It only grows while swimming over deep
//  water; shallow water (any other floor - the stock coasts' own shelves) pauses it and resets it
//  after shallowResetS; ground / a pier / a boat (dryResetS) resets it; a teleport or a respawn
//  resets it and starts the guard.
//  Before graceS nothing can happen. After it, a per-minute chance
//      h(t) = min(maxPerMin, chancePerMin * (1 + ramp * minutes_past_grace))
//  is rolled every tick as 1 - (1 - h)^(dt / 60). A hit starts the warning (warnS, 0 = none), then
//  the strike: Allow() turns true and the game's own test drowns the swimmer (the stock shark). If
//  the game has not taken the player within strikeTimeoutS, the strike is cancelled and the clock
//  starts again from 0 (fail safe: never stuck "doomed").
//  classic = the stock rule (Allow() always true); chancePerMin = 0 = never.
// =============================================================================================
#pragma once
#include <cstdint>
#include <cmath>

namespace shm {

struct Params
{
    bool  classic        = false;   // true: the game's own rule - the shark the moment you are over deep water
    float graceS         = 180.0f;  // s of continuous deep-water swimming before any chance at all
    float chancePerMin   = 0.05f;   // chance per minute right after the grace (0 = never)
    float ramp           = 0.5f;    // + this x chancePerMin for every further minute
    float maxPerMin      = 0.5f;    // the per-minute chance never goes above this
    float warnS          = 0.0f;    // s between the roll and the strike (the warning cue's time; 0 = none)
    float guardS         = 15.0f;   // never within this many s of a teleport / respawn / new world
    float dryResetS      = 1.0f;    // on ground / a pier / in a vehicle this long = the clock back to 0
    float shallowResetS  = 8.0f;    // swimming over shallow (non-deep) water this long = the clock back to 0
    float teleportU      = 15.0f;   // a jump this far within one tick is a teleport (respawn, fast travel)
    float teleportSpeed  = 80.0f;   // ...or faster than this many u/s (long ticks)
    float strikeTimeoutS = 10.0f;   // a strike the game has not acted on in this long is cancelled
};

// What shark.h reads from the game for one player, once a tick.
struct In
{
    bool  valid    = false;   // a live player character (else: everything resets)
    bool  swimming = false;   // the Swimming context
    bool  drowning = false;   // the Drowning context (the stock shark has him)
    bool  dead     = false;   // the Death context / dead
    bool  feet     = false;   // the Default context (standing, walking, running - on something solid)
    bool  vehicle  = false;   // in or on a vehicle (a boat counts)
    bool  deep     = false;   // the floor under him is deep ("deadly") water, as the game's own test sees it
    bool  armed    = true;    // the level's deep water is deadly at all (deadly_deep_water > 0)
    float pos[3]   = { 0.0f, 0.0f, 0.0f };
};

enum Phase : int { kDry = 0, kSwim = 1, kWarn = 2, kStrike = 3 };
static const char* const kPhaseName[] = { "dry", "swimming", "warned", "strike" };

// Step() result bits (for the log)
enum : uint32_t
{
    kEvReset     = 1u << 0,   // the clock went back to 0 (ground, vehicle, shallows, respawn)
    kEvTeleport  = 1u << 1,   // a teleport / respawn jump was seen (guard restarted)
    kEvGrace     = 1u << 2,   // the grace just ran out: from now on there is a chance
    kEvWarn      = 1u << 3,   // the roll hit: the warning starts
    kEvStrike    = 1u << 4,   // the strike: Allow() is true now
    kEvCancel    = 1u << 5,   // a warning / strike was called off (reached ground or shallows, or timed out)
    kEvTaken     = 1u << 6,   // the game drowned him while the strike was on (the shark, as intended)
    kEvStockTook = 1u << 7,   // the game drowned him WITHOUT our strike (classic, or a path we don't gate)
};

struct State
{
    int   phase    = kDry;
    float deepT    = 0.0f;    // continuous deep-water swim time
    float shallowT = 0.0f;    // continuous shallow-water swim time
    float dryT     = 0.0f;    // continuous ground / vehicle time
    float phaseT   = 0.0f;    // time in kWarn / kStrike
    float sinceTp  = 0.0f;    // time since the last teleport / respawn / new world (starts guarded)
    bool  havePos  = false;
    float last[3]  = { 0.0f, 0.0f, 0.0f };
    bool  wasDown  = false;   // drowning / dead last tick
    bool  graceSaid = false;
    // counters for the log / tests
    int   warns = 0, strikes = 0, cancels = 0, taken = 0;
};

inline float Clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

// The per-minute chance after t s of continuous deep-water swimming (0 inside the grace).
inline float HazardPerMin(const Params& p, float deepT)
{
    if (p.chancePerMin <= 0.0f || deepT < p.graceS) return 0.0f;
    const float past = (deepT - p.graceS) / 60.0f;
    float h = p.chancePerMin * (1.0f + (p.ramp > 0.0f ? p.ramp : 0.0f) * past);
    const float cap = Clamp01(p.maxPerMin);
    if (h > cap) h = cap;
    return h > 0.999f ? 0.999f : h;
}

// The chance of the roll hitting within one tick of dt s.
inline float TickChance(float hPerMin, float dt)
{
    if (hPerMin <= 0.0f || dt <= 0.0f) return 0.0f;
    return 1.0f - std::exp(std::log(1.0f - hPerMin) * (dt / 60.0f));
}

// The chance that a non-stop deep-water swim of totalS s meets the shark (closed form of the above,
// warning ignored) - for the docs and the tests.
inline double ChanceWithin(const Params& p, double totalS, double stepS = 0.05)
{
    double survive = 1.0;
    for (double t = 0.0; t < totalS; t += stepS)
    {
        const double h = HazardPerMin(p, static_cast<float>(t));
        if (h > 0.0) survive *= std::exp(std::log(1.0 - h) * (stepS / 60.0));
    }
    return 1.0 - survive;
}

inline void ClockReset(State& s, uint32_t& ev)
{
    if (s.phase == kWarn || s.phase == kStrike) { ev |= kEvCancel; ++s.cancels; }
    if (s.deepT > 0.0f || s.phase != kDry) ev |= kEvReset;
    s.deepT = s.shallowT = s.phaseT = 0.0f;
    s.phase = kDry;
    s.graceSaid = false;
}

// A new world / level load / the mod switched on: forget everything, guard from now.
inline void Forget(State& s) { s = State(); }

// One tick for one player. u01 = a uniform random number in [0, 1) (the caller's RNG: testable).
inline uint32_t Step(const Params& p, State& s, const In& in, float dt, float u01)
{
    uint32_t ev = 0;
    if (!(dt > 0.0f) || dt > 1.0f) dt = 1.0f / 60.0f;
    if (!in.valid) { ClockReset(s, ev); s.havePos = false; s.sinceTp = 0.0f; s.dryT = 0.0f; s.wasDown = false; return ev; }

    // a teleport / respawn: a jump no swimmer, car or plane makes in one tick
    if (s.havePos)
    {
        const float dx = in.pos[0] - s.last[0], dy = in.pos[1] - s.last[1], dz = in.pos[2] - s.last[2];
        const float d = std::sqrt(dx * dx + dy * dy + dz * dz);
        const float lim = p.teleportU > p.teleportSpeed * dt ? p.teleportU : p.teleportSpeed * dt;
        if (!(d <= lim)) { ClockReset(s, ev); s.sinceTp = 0.0f; ev |= kEvTeleport; }   // (NaN counts as a jump)
    }
    for (int k = 0; k < 3; ++k) s.last[k] = in.pos[k];
    s.havePos = true;
    s.sinceTp += dt;

    // the game has him (the shark, or any other death): note who did it, then start over
    const bool down = in.drowning || in.dead;
    if (down)
    {
        if (!s.wasDown)
        {
            if (s.phase == kStrike) { ev |= kEvTaken; ++s.taken; s.phase = kDry; }
            else if (in.drowning) ev |= kEvStockTook;
        }
        s.wasDown = true;
        s.deepT = s.shallowT = s.phaseT = 0.0f; s.phase = kDry; s.graceSaid = false;
        s.sinceTp = 0.0f;                       // the respawn guard runs from the moment he is back
        s.dryT = 0.0f;
        return ev;
    }
    s.wasDown = false;

    // ground, a pier, a boat: the swim is over
    if (in.vehicle || (in.feet && !in.swimming))
    {
        s.dryT += dt;
        if (s.dryT >= p.dryResetS) ClockReset(s, ev);
        return ev;
    }
    if (!in.swimming) return ev;               // in the air, climbing out, a cut-scene: hold everything
    s.dryT = 0.0f;

    if (!in.deep)
    {
        // shallow water near a shore: no shark here ever; a warning / strike is called off at once
        if (s.phase == kWarn || s.phase == kStrike) { ev |= kEvCancel; ++s.cancels; s.phase = kSwim; s.phaseT = 0.0f; }
        if (s.phase == kDry) s.phase = kSwim;
        s.shallowT += dt;
        if (s.shallowT >= p.shallowResetS && s.deepT > 0.0f) { s.deepT = 0.0f; s.graceSaid = false; ev |= kEvReset; }
        return ev;
    }

    // deep water
    s.shallowT = 0.0f;
    if (s.phase == kDry) s.phase = kSwim;
    s.deepT += dt;
    if (!s.graceSaid && p.chancePerMin > 0.0f && s.deepT >= p.graceS) { s.graceSaid = true; ev |= kEvGrace; }

    switch (s.phase)
    {
    case kSwim:
    {
        if (!in.armed || s.sinceTp < p.guardS) break;
        const float c = TickChance(HazardPerMin(p, s.deepT), dt);
        if (c > 0.0f && u01 < c)
        {
            s.phaseT = 0.0f;
            if (p.warnS > 0.0f) { s.phase = kWarn; ++s.warns; ev |= kEvWarn; }
            else { s.phase = kStrike; ++s.strikes; ev |= kEvStrike; }
        }
        break;
    }
    case kWarn:
        s.phaseT += dt;
        if (s.phaseT >= p.warnS) { s.phase = kStrike; s.phaseT = 0.0f; ++s.strikes; ev |= kEvStrike; }
        break;
    case kStrike:
        s.phaseT += dt;
        if (s.phaseT >= p.strikeTimeoutS)
        {
            // the game never acted on it (it asks only at certain moments): call it off, fresh grace
            ++s.cancels; ev |= kEvCancel | kEvReset;
            s.phase = kSwim; s.phaseT = 0.0f; s.deepT = 0.0f; s.graceSaid = false;
        }
        break;
    default: break;
    }
    return ev;
}

// May the game's own deep-water rule act on this player now? (The engine glue asks this from the
// game's floating test; false = he floats and swims as over any other water.)
inline bool Allow(const Params& p, const State& s) { return p.classic || s.phase == kStrike; }

// Is a warning running (for a cue)?
inline bool Warned(const State& s) { return s.phase == kWarn; }

} // namespace shm
