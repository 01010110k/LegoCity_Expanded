// =============================================================================================
//  bike_model.h - stunts for the player's motorbike: wheelies, stoppies, flips and spins in the
//  air. Pure model: plain floats in, a new angular / linear velocity out. No engine pointers, so
//  test\bike_harness.cpp runs it as is.
//
//  What the engine does (Notes\31): ApiVehicleBike::Update (0x1404EAFC0) keeps the bike up with
//  the shared orientation drive 0x1404DDBB0, called from 0x1404EB460 with per-axis gains
//  (0, 0, 1) - it corrects ROLL only (the lean into a turn). Pitch and yaw are left to the
//  tyres, the suspension and gravity; nothing in the stock bike ever asks for a pitch, which is
//  why it can't wheelie. In the air the base update's FlightAlign (0x1404A9650, def+0x185) uses
//  the same drive to point the nose along the flight path - that one would undo a flip.
//
//  So this model leaves roll to the engine and owns pitch (and, in the air, yaw):
//    * wheelie: rolling, on the throttle, stick pulled back -> the nose rises toward an angle set
//      by how far back the stick is, about the REAR contact patch (the rear stays planted); let go
//      and it comes back down under control;
//    * stoppie: braking hard, stick pushed forward -> the rear lifts, about the front contact;
//    * in the air: the stick is pitch torque (back = backflip, forward = front flip), steering is
//      yaw torque (spins); the physical spin carries on with the stick centred (no auto-level),
//      and the engine's flight-path alignment is discarded on those two axes.
// =============================================================================================
#pragma once
#include <cmath>
#include <cstdint>

namespace bike {

struct Params
{
    float wheelieSpeed  = 4.0f;    // u/s forward before pulling back lifts the front
    float wheelieDeg    = 26.0f;   // pitch at full pull-back (MOTORCYCLES_BUZZER's box scrapes from ~28)
    float stoppieDeg    = 22.0f;   // nose-down at full push on hard braking
    float groundRate    = 2.6f;    // rad/s: fastest the nose moves on the ground
    float groundGain    = 5.0f;    // 1/s: angle error -> pitch rate
    float airPitchAccel = 11.0f;   // rad/s^2 at full stick in the air
    float airPitchMax   = 7.5f;    // rad/s
    float airYawAccel   = 8.0f;    // rad/s^2 at full steer in the air (0 = no spins)
    float airYawMax     = 6.0f;    // rad/s
    float airDamp       = 0.4f;    // 1/s: the spin fades this slowly with the stick centred
    float stickDead     = 0.2f;    // stick travel ignored around centre
    float gripG         = 1.0f;    // the turn the tyres hold, in g: on the ground at speed the yaw
                                   // rate is held to what that allows (0 = no spin guard)
    float spinTau       = 0.015f;  // s: how fast the guard takes back yaw past that (0.06 let the
                                   // engine's push hold ~1.8x the limit; still spun out, 2026-10-24)
};

struct State
{
    uint8_t mode;                  // 0 stock, 1 wheelie, 2 stoppie, 3 air
    float   airT;                  // time airborne
    float   capRad;                // learned wheelie limit (0 = none yet): the collision box touches past it
    float   stallT;
    float   qWritten;              // pitch rate written last frame while owning pitch (else 0)
    float   integ;                 // angle-error integral (rad s): gravity would leave the nose ~4 deg short
    float   bestPitch, bestT;      // highest pitch this wheelie, and how long since it last rose
};

inline void Reset(State& s)
{
    s.mode = 0; s.airT = 0.0f; s.capRad = 0.0f; s.stallT = 0.0f; s.qWritten = 0.0f;
    s.integ = 0.0f; s.bestPitch = -1.0f; s.bestT = 0.0f;
}

struct Input
{
    float dt;
    float right[3], up[3], back[3];   // body rows (engine forward = -back)
    float av0[3];                     // angular velocity BEFORE the engine's update (physical)
    float av1[3], lv1[3];             // after the engine's update (its lean drive, flight align)
    float cm[3];                      // body origin, world
    float frontP[3], rearP[3];        // wheel contact points, world (valid when on the ground)
    uint8_t frontDown, rearDown;      // wheel touching the ground
    float vFwd;                       // forward speed, u/s
    float stick;                      // -1 pulled back .. +1 pushed forward
    float steer;                      // -1 .. 1, + = left (the game's steering field)
    float throttle, brake;            // 0..1
};

struct Output
{
    float av[3], lv[3];
    float pitchDeg, pitchRate, target;  // telemetry
    uint8_t mode, valid;
    uint8_t capLearned;                 // this frame set State::capRad (log it)
    uint8_t yawHeld;                    // the spin guard acted: write even in mode 0
};

inline float Dot(const float a[3], const float b[3]) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
inline void  Cross(const float a[3], const float b[3], float o[3])
{
    o[0] = a[1] * b[2] - a[2] * b[1]; o[1] = a[2] * b[0] - a[0] * b[2]; o[2] = a[0] * b[1] - a[1] * b[0];
}
inline float Clamp(float v, float lo, float hi) { return v != v ? lo : (v < lo ? lo : (v > hi ? hi : v)); }
inline bool  Fin3(const float v[3]) { return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]); }
inline float Dead(float v, float dz) { const float a = std::fabs(v); return a <= dz ? 0.0f : (v > 0 ? 1.0f : -1.0f) * (a - dz) / (1.0f - dz); }

inline void Update(const Params& p, State& s, const Input& in, Output& o)
{
    for (int k = 0; k < 3; ++k) { o.av[k] = in.av1[k]; o.lv[k] = in.lv1[k]; }
    o.pitchDeg = o.pitchRate = o.target = 0.0f;
    o.valid = 0; o.mode = 0; o.capLearned = 0; o.yawHeld = 0;
    if (!(in.dt > 0.0f) || in.dt > 0.1f || !Fin3(in.right) || !Fin3(in.up) || !Fin3(in.back) ||
        !Fin3(in.av0) || !Fin3(in.av1) || !Fin3(in.lv1))
        return;
    const float dt = in.dt;
    const float* r = in.right;                                // pitch axis: + = nose up
    const float* u = in.up;
    const float fy = -in.back[1];                             // forward's height
    const float pitch = std::asin(Clamp(fy, -1.0f, 1.0f));    // + = nose up
    const float q1 = Dot(in.av1, r);                          // pitch rate after the engine
    const float y1 = Dot(in.av1, u);
    const float stick = Dead(Clamp(in.stick, -1, 1), p.stickDead);
    const float steer = Dead(Clamp(in.steer, -1, 1), 0.1f);
    float q = q1, yw = y1;
    const float* pivot = nullptr;

    const bool air = !in.frontDown && !in.rearDown;
    s.airT = air ? s.airT + dt : 0.0f;

    if (air && s.mode == 1 && s.airT < 0.35f && pitch > 0.17f)
    {
        // Mid-wheelie and the REAR tyre came off too: not a jump - it is pivoting on the collision
        // box under the tail (in game: sparks, then a loop-out if the stick still pulls). Learn the
        // limit at this angle and bring the nose down below it; the next lift, if any, lowers it again.
        if (s.capRad <= 0.0f || pitch - 0.05f < s.capRad) { s.capRad = pitch - 0.05f; o.capLearned = 1; }
        const float target = s.capRad - 0.05f;
        const float qc = Clamp(p.groundGain * (target - pitch), -p.groundRate, p.groundRate);
        q = q1 + (qc - q1) * Clamp(dt / 0.06f, 0.0f, 1.0f);
        s.integ = 0.0f;
        o.target = target * 57.2958f;
    }
    else if (air && s.airT > 0.08f)                           // a real jump, not a bump
    {
        s.mode = 3;
        // the physical spin before the engine's flight-path alignment, plus the stick's torque
        q = Dot(in.av0, r) - stick * p.airPitchAccel * dt;    // back (-) = nose up = backflip
        yw = Dot(in.av0, u) + steer * p.airYawAccel * dt;     // + = left
        if (stick == 0.0f) q *= 1.0f - Clamp(p.airDamp * dt, 0.0f, 0.5f);
        if (steer == 0.0f) yw *= 1.0f - Clamp(p.airDamp * dt, 0.0f, 0.5f);
        q = Clamp(q, -p.airPitchMax, p.airPitchMax);
        if (p.airYawAccel <= 0.0f) yw = y1;
        else yw = Clamp(yw, -p.airYawMax, p.airYawMax);
        o.target = stick;
    }
    else if (air)
    {
        // a short hop: keep whatever the stunt was doing (a wheelie over a kerb stays a wheelie)
        if (s.mode == 1 || s.mode == 2) q = Dot(in.av0, r);
    }
    else
    {
        // on the ground. Wheelie: rolling forward on the throttle with the stick back; it stays a
        // wheelie while the stick is held, whatever the throttle does; let go and it is brought
        // back down. Stoppie: braking with the stick forward.
        float target = 0.0f;
        bool  own = false;
        if (in.rearDown && stick < 0.0f && in.vFwd > p.wheelieSpeed && (in.throttle > 0.4f || s.mode == 1))
        {
            s.mode = 1; own = true; pivot = in.rearP;
            target = -stick * p.wheelieDeg * 0.0174533f;
        }
        else if (in.frontDown && stick > 0.0f && in.vFwd > 3.0f && (in.brake > 0.4f || s.mode == 2))
        {
            s.mode = 2; own = true; pivot = in.frontP;
            target = -stick * p.stoppieDeg * 0.0174533f;
        }
        else if ((s.mode == 1 && pitch > 0.05f) || (s.mode == 2 && pitch < -0.05f))
        {
            own = true;                                       // coming back down
            pivot = s.mode == 1 ? in.rearP : in.frontP;
            if (!(s.mode == 1 ? in.rearDown : in.frontDown)) pivot = nullptr;
        }
        else s.mode = 0;                                      // includes landing after a jump
        if (own)
        {
            if (s.mode == 1 && s.capRad > 0.0f) target = std::fmin(target, s.capRad);
            const float e = target - pitch;
            s.integ = Clamp(s.integ + e * dt, -0.25f, 0.25f);
            const float qc = Clamp(p.groundGain * e + 4.0f * s.integ, -p.groundRate, p.groundRate);
            q = q1 + (qc - q1) * Clamp(dt / 0.06f, 0.0f, 1.0f);
            o.target = target * 57.2958f;
            // Something under the tail - the collision box reaching past the rear tyre: the nose
            // stops rising well short of what is asked (the integral would otherwise reach it), or
            // the rear tyre lifts off (pivoting on the box). Remember the angle, a little lower, and
            // never ask past it again (this bike, this session).
            if (s.mode == 1)
            {
                if (pitch > s.bestPitch + 0.009f) { s.bestPitch = pitch; s.bestT = 0.0f; } else s.bestT += dt;
                // (8 deg short for 0.4 s: a plateau 4.6 deg short at 16 u/s set a 19 deg cap in game,
                // with no tail strike - the rear-tyre lift below is the sure sign)
                const bool plateau = e > 0.14f && s.bestT > 0.4f;
                const bool lifted = !in.rearDown;
                s.stallT = lifted ? s.stallT + dt : 0.0f;
                if (pitch > 0.17f && (plateau || s.stallT > 0.06f))
                {
                    s.capRad = pitch - 0.05f; s.stallT = 0.0f; s.bestT = 0.0f; s.integ = 0.0f;
                    o.capLearned = 1;
                }
            }
        }
        else { s.integ = 0.0f; s.bestPitch = -1.0f; s.bestT = 0.0f; }
    }

    // spin guard: on the ground at speed, no yaw faster than the tyres can carry in a turn
    // (r = a / v). The stock bike's steering was tuned for its own 6.5 u/s; at 17 it asked for
    // 2-3.6 rad/s - 4 g - and the bike spun out against the rider's countersteer (2026-10-24).
    if (!air && p.gripG > 0.0f && in.vFwd > 4.0f && in.frontDown && in.rearDown)
    {
        const float rMax = Clamp(1.25f * p.gripG * 9.8f / in.vFwd, 0.3f, 4.0f);
        if (std::fabs(yw) > rMax)
        {
            const float excess = std::fabs(yw) - rMax;
            yw = (yw > 0.0f ? 1.0f : -1.0f) * (rMax + excess * std::exp(-dt / Clamp(p.spinTau, 0.001f, 1.0f)));
            o.yawHeld = 1;
        }
    }

    // write: replace the pitch (and in the air the yaw) components of the engine's result
    const float dq = q - q1, dy = yw - y1;
    s.qWritten = (s.mode == 1 || s.mode == 2) ? q : 0.0f;
    for (int k = 0; k < 3; ++k) o.av[k] = in.av1[k] + r[k] * dq + u[k] * dy;
    if (pivot && Fin3(pivot))
    {
        // turning about the wheel's contact patch instead of the body origin: v += dw x (cm - P)
        const float dw[3] = { r[0] * dq, r[1] * dq, r[2] * dq };
        const float arm[3] = { in.cm[0] - pivot[0], in.cm[1] - pivot[1], in.cm[2] - pivot[2] };
        if (Dot(arm, arm) < 9.0f)                             // a sane lever (< 3 u)
        {
            float dv[3]; Cross(dw, arm, dv);
            for (int k = 0; k < 3; ++k) o.lv[k] += dv[k];
        }
    }
    for (int k = 0; k < 3; ++k) { o.av[k] = Clamp(o.av[k], -12.0f, 12.0f); o.lv[k] = Clamp(o.lv[k], -120.0f, 120.0f); }
    o.mode = s.mode;
    o.pitchDeg = pitch * 57.2958f;
    o.pitchRate = q;
    o.valid = Fin3(o.av) && Fin3(o.lv);
    if (!o.valid) for (int k = 0; k < 3; ++k) { o.av[k] = in.av1[k]; o.lv[k] = in.lv1[k]; }
}

} // namespace bike
