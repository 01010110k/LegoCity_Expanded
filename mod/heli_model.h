// =============================================================================================
//  heli_model.h - a physical helicopter for the player's Hover-class vehicle. Pure model: plain
//  floats in, new body velocities out. No engine pointers, so test\heli_harness.cpp runs it as is.
//
//  What the engine does (Notes\28-heli-flight.md): the helicopter is a normal dynamic rigid body
//  that ApiVehicleHover::Update (0x1404AEA60) flies by OVERWRITING velocities every frame - the
//  height servo sets vy, a forward-speed servo sets the velocity along the heading, the
//  orientation drive writes the angular velocity (that is why it can never tip), and gravity is
//  switched off. lcudrive snapshots the body's velocities before that update, puts them back
//  after it (which cancels all of it at once), and hands this model the physical state.
//
//  The model, all of it physical:
//    * gravity is real;
//    * the rotor pushes along the body's ACTUAL up axis - tilting is what moves the helicopter;
//    * the stick commands an attitude (forward = nose down, turning banks into the turn), and
//      an attitude controller writes the angular velocity that gets there; collisions disturb it
//      like any rigid body, the controller recovers;
//    * collective is a climb-rate loop with a thrust limit (centred = hold height);
//    * drag sets the top speed; a brake assist tilts back to stop when the stick is released;
//    * on the ground with no lift asked for, nothing is controlled: gravity and the collision
//      solver own it, so a bad landing or a slope can tip it over.
// =============================================================================================
#pragma once
#include <cmath>
#include <cstdint>

namespace heli {

struct Params
{
    float topSpeed     = 45.0f;   // u/s level flight at full forward tilt
    float maxTiltDeg   = 34.0f;   // stick tilt (pitch/roll target) at full deflection
    float climbRate    = 6.0f;    // u/s at full collective up
    float descendRate  = 8.0f;    // u/s at full collective down
    float maxThrustG   = 2.0f;    // rotor thrust capacity, multiples of gravity
    float yawRateDeg   = 100.0f;  // deg/s at full turn input
    float turnBank     = 0.6f;    // 0..1 share of the coordinated-turn bank
    float brakeAssist  = 0.7f;    // 0..1 tilt back against the motion when the stick is centred
    float sideslip     = 1.6f;    // 1/s: how fast sideways sliding turns into the heading (keel)
    float responseS    = 0.15f;   // attitude reaches a new target in ~this time
    float spoolS       = 1.2f;    // rotor spool time constant
    float landHeight   = 0.35f;   // height above ground that counts as touching
    float vertGain     = 4.0f;    // 1/s: climb-rate loop gain
    float yawSign      = 1.0f;    // input polarity (live-calibrated)
    float fwdSign      = 1.0f;
    float vertSign     = 1.0f;
};

struct State
{
    float spool;
    uint8_t landed;
    uint8_t primed;
};

inline void Reset(State& s) { s.spool = 0.0f; s.landed = 1; s.primed = 0; }

struct Input
{
    float dt;
    float right[3], up[3], back[3];   // body rows (engine forward = -back)
    float lv[3], av[3];               // physical velocities (the engine's writes already undone)
    float hag;                        // height above ground (engine probe), > 1e30 = unknown
    float cTurn, cFwd, cVert;         // raw input axes -1..1 (block +0x00 / +0x04 / +0x08)
    uint8_t occupied;
};

struct Output
{
    float lv[3], av[3];               // what to write back
    float thrust, pitchT, rollT, vyT; // for telemetry
    uint8_t landed, valid;
};

// ---- small vector helpers ---------------------------------------------------------------------
inline float Dot(const float a[3], const float b[3]) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
inline void  Cross(const float a[3], const float b[3], float o[3])
{
    o[0] = a[1] * b[2] - a[2] * b[1]; o[1] = a[2] * b[0] - a[0] * b[2]; o[2] = a[0] * b[1] - a[1] * b[0];
}
inline bool  Norm(float v[3])
{
    const float m = std::sqrt(Dot(v, v));
    if (!(m > 1e-6f) || !std::isfinite(m)) return false;
    v[0] /= m; v[1] /= m; v[2] /= m;
    return true;
}
inline float Clamp(float v, float lo, float hi) { return v != v ? lo : (v < lo ? lo : (v > hi ? hi : v)); }
inline float Lag(float dt, float tau) { return tau > 1e-4f ? Clamp(1.0f - std::exp(-dt / tau), 0.0f, 1.0f) : 1.0f; }
inline float Dead(float v, float dz) { const float a = std::fabs(v); return a <= dz ? 0.0f : (v > 0 ? 1.0f : -1.0f) * (a - dz) / (1.0f - dz); }
inline bool  Fin3(const float v[3]) { return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]); }

inline void Update(const Params& p, State& s, const Input& in, Output& out)
{
    const float g = 9.8f;
    for (int k = 0; k < 3; ++k) { out.lv[k] = in.lv[k]; out.av[k] = in.av[k]; }
    out.thrust = out.pitchT = out.rollT = out.vyT = 0.0f;
    out.landed = s.landed;
    out.valid = 0;
    if (!(in.dt > 0.0f) || in.dt > 0.1f || !Fin3(in.right) || !Fin3(in.up) || !Fin3(in.back) ||
        !Fin3(in.lv) || !Fin3(in.av))
        return;
    const float dt = in.dt;

    float up[3] = { in.up[0], in.up[1], in.up[2] };
    float fwd[3] = { -in.back[0], -in.back[1], -in.back[2] };
    if (!Norm(up) || !Norm(fwd)) return;

    // heading frame: horizontal forward / right (forward falls back to the up vector's lean when
    // the nose points straight up or down)
    float fh[3] = { fwd[0], 0.0f, fwd[2] };
    if (!Norm(fh)) { fh[0] = -up[0]; fh[1] = 0.0f; fh[2] = -up[2]; if (!Norm(fh)) { fh[0] = 0; fh[2] = -1; } }
    const float wup[3] = { 0.0f, 1.0f, 0.0f };
    float rh[3]; Cross(fh, wup, rh); Norm(rh);

    const float vf = Dot(in.lv, fh), vr = Dot(in.lv, rh), vy = in.lv[1];
    const float vh = std::sqrt(in.lv[0] * in.lv[0] + in.lv[2] * in.lv[2]);
    const bool  hagOk = in.hag < 1e30f && std::isfinite(in.hag);

    const float cFwd  = Dead(Clamp(in.cFwd, -1, 1), 0.08f) * p.fwdSign;
    const float cTurn = Dead(Clamp(in.cTurn, -1, 1), 0.08f) * p.yawSign;
    const float cVert = Dead(Clamp(in.cVert, -1, 1), 0.08f) * p.vertSign;

    // ---- touching the ground ------------------------------------------------------------------
    if (!s.landed)
    {
        // near the ground AND no longer sinking (resting on it), not merely close while still descending
        if (hagOk && in.hag < p.landHeight && cVert <= 0.05f && vy > -0.5f && vh < 3.0f) s.landed = 1;
    }
    else if (cVert > 0.15f || (hagOk && in.hag > 3.0f * p.landHeight)) s.landed = 0;
    out.landed = s.landed;

    // ---- rotor spool --------------------------------------------------------------------------
    const float spoolT = !in.occupied ? 0.0f : (s.landed && cVert <= 0.05f ? 0.25f : 1.0f);
    if (!s.primed) { s.spool = s.landed ? 0.25f : 1.0f; s.primed = 1; }
    s.spool = Clamp(s.spool + (spoolT - s.spool) * Lag(dt, p.spoolS), 0.0f, 1.0f);

    // gravity always
    out.lv[1] -= g * dt;

    if (s.landed)
    {
        // Skids down, nothing asked of the rotor: the collision solver owns the body - it can
        // slide, and a slope or a bad landing can tip it. What the solver does NOT do well is rest:
        // its push-out lifts the body ~1-3 cm off the contact, gravity drops it back, and it lands
        // again at ~0.3 u/s - 10-15 times a second, every one of them a collision (the "micro-
        // collisions" sitting on a pad, 2026-10-23 flight). So while it sits upright, the fall is
        // replaced by a creep proportional to the gap: it closes the 1-3 cm in a few frames and
        // meets the ground at a few cm/s, which the contact simply holds. Tipped or on a steep
        // slope (up < 0.9), real gravity, so it can still fall over.
        if (up[1] > 0.9f && hagOk)
        {
            out.lv[1] = -Clamp(0.03f + 2.0f * std::fmax(in.hag, 0.0f), 0.03f, 1.0f);
        }
        else if (out.lv[1] > 0.0f && in.lv[1] <= 0.0f) out.lv[1] = 0.0f;
        out.valid = 1;
        return;
    }

    // ---- attitude targets (radians, in the heading frame) --------------------------------------
    const float tMax = Clamp(p.maxTiltDeg, 1.0f, 60.0f) * 0.0174533f;
    float pitchT = cFwd * tMax;                                             // + = nose down = go
    if (std::fabs(cFwd) < 0.05f) pitchT = -Clamp(vf / 8.0f, -1.0f, 1.0f) * tMax * p.brakeAssist;
    float rollT = -Clamp(vr / 6.0f, -1.0f, 1.0f) * tMax * p.brakeAssist;    // + = roll right
    // + about world up = left. The turn axis is +1 for LEFT, the same convention as the car's
    // steering field (model.h publishes + = left); the 22:00 flight had it negated: left turned right.
    const float yawRate = cTurn * p.yawRateDeg * 0.0174533f;
    // coordinated turn: the bank that makes the lift curve the path at this speed and yaw rate
    rollT += -std::atan(Clamp(vf * yawRate / g, -1.5f, 1.5f)) * Clamp(p.turnBank, 0.0f, 1.0f);
    pitchT = Clamp(pitchT, -tMax, tMax);
    rollT  = Clamp(rollT, -tMax, tMax);

    float upT[3] = { fh[0] * std::tan(pitchT) + rh[0] * std::tan(rollT), 1.0f,
                     fh[2] * std::tan(pitchT) + rh[2] * std::tan(rollT) };
    Norm(upT);

    // ---- attitude controller -> angular velocity ------------------------------------------------
    float e[3]; Cross(up, upT, e);                                          // axis * sin(error)
    const float kAtt = 1.0f / Clamp(p.responseS, 0.03f, 2.0f);
    float wCmd[3] = { e[0] * kAtt, e[1] * kAtt + yawRate, e[2] * kAtt };
    // tilt-rate limit (the yaw part is limited by yawRateDeg already)
    {
        const float tilt = std::sqrt(e[0] * e[0] + e[2] * e[2]) * kAtt;
        const float lim = 3.0f;                                             // rad/s
        if (tilt > lim) { const float k = lim / tilt; wCmd[0] *= k; wCmd[2] *= k; }
    }
    const float aw = Lag(dt, 0.05f);
    for (int k = 0; k < 3; ++k) out.av[k] = in.av[k] + (wCmd[k] - in.av[k]) * aw;

    // ---- collective: a climb-rate loop through a real thrust limit ------------------------------
    float vyT = cVert >= 0.0f ? cVert * p.climbRate : cVert * p.descendRate;
    // flare: the sink rate allowed shrinks with height, so any descent ends in a soft touchdown
    // (slope 0.8/s: the loop lags a moving target by slope*|v|/gain, which sets the touchdown speed)
    if (hagOk && vyT < 0.0f) vyT = std::fmax(vyT, -(0.8f + 0.8f * std::fmax(in.hag, 0.0f)));
    const float cosT = std::fmax(up[1], 0.3f);
    const float Treq = (g + p.vertGain * (vyT - vy)) / cosT;
    const float T = Clamp(Treq, 0.0f, p.maxThrustG * g) * s.spool * (up[1] > 0.15f ? 1.0f : 0.0f);
    for (int k = 0; k < 3; ++k) out.lv[k] += up[k] * T * dt;

    // ---- drag: sized so full forward tilt settles at topSpeed ------------------------------------
    {
        const float V = Clamp(p.topSpeed, 3.0f, 80.0f);
        const float cLin = 0.05f;
        const float cQuad = std::fmax((g * std::tan(tMax) - cLin * V) / (V * V), 0.0f);
        const float k = (cLin + cQuad * vh) * dt;
        out.lv[0] -= in.lv[0] * Clamp(k, 0.0f, 0.5f);
        out.lv[2] -= in.lv[2] * Clamp(k, 0.0f, 0.5f);
        out.lv[1] -= in.lv[1] * Clamp(0.05f * dt, 0.0f, 0.5f);
        // the fuselage and tail fin resist sliding sideways (a keel): sideslip turns into the
        // heading over ~0.6 s, so the path follows the nose in a turn instead of skidding
        const float ks = Clamp(p.sideslip * dt, 0.0f, 0.5f);
        out.lv[0] -= rh[0] * vr * ks;
        out.lv[2] -= rh[2] * vr * ks;
        out.lv[0] += fh[0] * std::fabs(vr) * ks * 0.6f;                 // most of it re-aimed, some lost
        out.lv[2] += fh[2] * std::fabs(vr) * ks * 0.6f;
    }

    // ---- sanity lids ------------------------------------------------------------------------
    for (int k = 0; k < 3; ++k) { out.lv[k] = Clamp(out.lv[k], -90.0f, 90.0f); out.av[k] = Clamp(out.av[k], -8.0f, 8.0f); }
    out.thrust = T; out.pitchT = pitchT; out.rollT = rollT; out.vyT = vyT;
    out.valid = Fin3(out.lv) && Fin3(out.av);
    if (!out.valid) for (int k = 0; k < 3; ++k) { out.lv[k] = in.lv[k]; out.av[k] = in.av[k]; }
}

// =============================================================================================
//  The abandoned helicopter: the pilot bailed out in the air. Nothing holds the collective, so
//  the rotor's lift winds down from a hover; nothing holds the tail, so the fuselage spins up
//  about its own rotor shaft (the main rotor's torque, unopposed); and the rotor disc leans over
//  toward where it was going, the lean slowly wandering round - a corkscrew descent. The first
//  hard contact - a jump in velocity the solver made, or the ground right below - ends all
//  control: from then on it is a plain rigid body under gravity. Once it rests upright it is
//  handed back to the game, which parks it; tipped over, it stays a plain body (the game's hover
//  code would stand it back up). Same Input as Update (the engine's writes already undone).
// =============================================================================================
struct WreckParams
{
    float spinMax      = 5.0f;    // rad/s the fuselage spins at once the tail is lost (~0.8 rev/s)
    float spinUpS      = 0.9f;    // s, time constant of the spin-up
    float tiltMaxDeg   = 28.0f;   // how far the rotor disc leans as it loses control
    float tiltRateDeg  = 18.0f;   // deg/s
    float wanderK      = 0.25f;   // the lean direction turns at this share of the spin rate
    float thrustDecayS = 1.0f;    // s, the rotor's lift falls from hover with this time constant (was 1.6: a
                                  // 29 u fall only hit at 9.6 u/s, 2026-11-13)
    float impactDv     = 3.0f;    // u/s velocity jump in one frame that means it hit something
    float groundHag    = 0.5f;    // or the ground is this close below
};

struct WreckState
{
    float   t, spinSign;
    float   tdir[2];              // horizontal (x, z) direction the disc leans toward
    float   lastLv[3];            // what was written last frame: the solver's impulse shows against it
    float   impactSpeed, restT;
    uint8_t phase;                // 0 falling (controlled), 1 down (plain physics), 2 at rest (release)
    uint8_t primed;
};

struct WreckOutput
{
    float   lv[3], av[3];
    float   thrust, spin, tiltDeg;
    float   dv;                   // this frame's velocity jump (the solver's impulse): how hard it hit
    uint8_t phase, impact, valid;
};

// spinSign: +1 / -1 about world up. lean: horizontal direction to lean toward (need not be unit).
inline void WreckReset(WreckState& w, float spinSign, float leanX, float leanZ)
{
    w.t = 0.0f; w.spinSign = spinSign >= 0.0f ? 1.0f : -1.0f;
    float m = std::sqrt(leanX * leanX + leanZ * leanZ);
    if (!(m > 1e-4f)) { leanX = 0.0f; leanZ = -1.0f; m = 1.0f; }
    w.tdir[0] = leanX / m; w.tdir[1] = leanZ / m;
    w.lastLv[0] = w.lastLv[1] = w.lastLv[2] = 0.0f;
    w.impactSpeed = 0.0f; w.restT = 0.0f; w.phase = 0; w.primed = 0;
}

inline void Wreck(const WreckParams& p, WreckState& w, const Input& in, WreckOutput& o)
{
    const float g = 9.8f;
    for (int k = 0; k < 3; ++k) { o.lv[k] = in.lv[k]; o.av[k] = in.av[k]; }
    o.thrust = o.spin = o.tiltDeg = o.dv = 0.0f;
    o.impact = 0; o.valid = 0; o.phase = w.phase;
    if (!(in.dt > 0.0f) || in.dt > 0.1f || !Fin3(in.up) || !Fin3(in.lv) || !Fin3(in.av)) return;
    if (w.phase >= 2) return;
    const float dt = in.dt;
    float up[3] = { in.up[0], in.up[1], in.up[2] };
    if (!Norm(up)) return;
    o.tiltDeg = std::acos(Clamp(up[1], -1.0f, 1.0f)) * 57.2958f;
    const bool hagOk = in.hag < 1e30f && std::isfinite(in.hag);

    if (!w.primed) { w.primed = 1; for (int k = 0; k < 3; ++k) w.lastLv[k] = in.lv[k]; }
    w.t += dt;
    float dv2 = 0.0f;
    for (int k = 0; k < 3; ++k) { const float d = in.lv[k] - w.lastLv[k]; dv2 += d * d; }
    const float dv = std::sqrt(dv2);
    o.dv = dv;

    if (w.phase == 0)
    {
        if (w.t > 0.1f && (dv > p.impactDv || (hagOk && in.hag < p.groundHag)))
        {
            w.phase = 1; o.impact = 1;
            w.impactSpeed = dv > p.impactDv ? dv : std::sqrt(Dot(w.lastLv, w.lastLv));
        }
    }

    if (w.phase == 0)
    {
        const float ws = w.spinSign * p.spinMax * (1.0f - std::exp(-w.t / Clamp(p.spinUpS, 0.05f, 10.0f)));
        // the lean direction wanders round slowly; the fuselage spins about its own shaft, which
        // turns the nose without moving the disc
        const float a = ws * Clamp(p.wanderK, 0.0f, 1.0f) * dt, ca = std::cos(a), sa = std::sin(a);
        const float tx = w.tdir[0] * ca + w.tdir[1] * sa, tz = -w.tdir[0] * sa + w.tdir[1] * ca;
        w.tdir[0] = tx; w.tdir[1] = tz;
        const float thd = std::fmin(Clamp(p.tiltMaxDeg, 0.0f, 80.0f), p.tiltRateDeg * w.t) * 0.0174533f;
        const float upT[3] = { tx * std::sin(thd), std::cos(thd), tz * std::sin(thd) };
        float e[3]; Cross(up, upT, e);
        const float wCmd[3] = { e[0] / 0.2f + up[0] * ws, e[1] / 0.2f + up[1] * ws, e[2] / 0.2f + up[2] * ws };
        const float aw = Lag(dt, 0.12f);
        for (int k = 0; k < 3; ++k) o.av[k] = in.av[k] + (wCmd[k] - in.av[k]) * aw;

        const float T = g * std::exp(-w.t / Clamp(p.thrustDecayS, 0.1f, 20.0f)) * (up[1] > 0.15f ? 1.0f : 0.0f);
        for (int k = 0; k < 3; ++k) o.lv[k] += up[k] * T * dt;
        o.lv[1] -= g * dt;
        const float v = std::sqrt(Dot(in.lv, in.lv));
        const float kd = Clamp((0.02f + 0.008f * v) * dt, 0.0f, 0.5f);
        for (int k = 0; k < 3; ++k) o.lv[k] -= in.lv[k] * kd;
        o.thrust = T; o.spin = ws;
    }
    else
    {
        // down: a plain rigid body. The engine's hover code has gravity switched off and its
        // writes are undone, so gravity is the only thing added; rotation is the solver's.
        o.lv[1] -= g * dt;
        // at rest upright = back to the game (it parks an empty helicopter properly). The vertical
        // limit is loose: a body resting on the solver hops a few cm/s (see the landed branch).
        const float vh2 = in.lv[0] * in.lv[0] + in.lv[2] * in.lv[2];
        const bool still = up[1] > 0.9f && vh2 < 0.3f * 0.3f && std::fabs(in.lv[1]) < 0.6f &&
                           Dot(in.av, in.av) < 0.3f * 0.3f;
        w.restT = still ? w.restT + dt : 0.0f;
        if (w.restT > 1.0f) w.phase = 2;
    }

    for (int k = 0; k < 3; ++k) { o.lv[k] = Clamp(o.lv[k], -90.0f, 90.0f); o.av[k] = Clamp(o.av[k], -8.0f, 8.0f); }
    o.valid = Fin3(o.lv) && Fin3(o.av);
    if (!o.valid) for (int k = 0; k < 3; ++k) { o.lv[k] = in.lv[k]; o.av[k] = in.av[k]; }
    for (int k = 0; k < 3; ++k) w.lastLv[k] = o.lv[k];
    o.phase = w.phase;
}

} // namespace heli
