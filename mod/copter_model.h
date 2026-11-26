// =============================================================================================
//  copter_model.h - the police helicopter's autopilot (wanted_heli.h flies it). Pure model: plain
//  floats in, new body velocities out, so test\copter_harness.cpp runs it as is.
//
//  Why lcudrive flies it at all (Notes\33 §3, Notes\43 §2): nothing in the game AI-flies a
//  helicopter. No script follows or circles anything with one; there is no flying AI task (the task
//  factory has none); the traffic pursuit can't drive a Hover vehicle (ChaseTask's decomposition
//  switches on def+0x1D4 = 1 car / 2 bike / 3 boat, and VehicleDefHover writes 6, 0x1404CBBB2) [P];
//  and the engine's own servo would fly a Police_Responder at its Max Velocity 6 u/s, 0.5 u off the
//  ground [P]. So, as with the player's helicopter (heli.h, Notes\28): the body's velocities are
//  taken before ApiVehicleHover::Update, the update runs (rotor, sound, siren, the terrain probe),
//  and this model's velocities are written over what it wrote.
//
//  What it does:
//    * a suspect on the move: it trails him - behind, a little to one side (each helicopter its
//      own side), `height` above him and never less than `groundClear` above the ground under it;
//    * a suspect (nearly) stopped: it circles him at `orbitR`, nose on him;
//    * a suspect flying a helicopter or plane himself - or the other co-op player in one - is kept
//      at least `sep` away and `sepAbove` below it: the solver would make a touch a crash (heli.h's
//      crash detector damages the player's helicopter on any jolt);
//    * the velocity it asks for is reached through an acceleration limit, and the body tilts into
//      that acceleration like a real rotor (thrust = m (a + g)), so it banks into turns and noses
//      down to speed up; the nose turns toward the suspect (near) or the way it flies (far);
//    * stuck (slow while asking to be fast: a wall, a roof edge) it climbs `climbExtra` for a
//      while; still stuck after `respawnS`, it raises `respawn` and the director replaces it
//      out of sight;
//    * leaving (the level dropped, the chase is over): it climbs `leaveClimb` and flies off
//      away from the suspect at `leaveSpeed`; the director fades it out once it's far.
//  No gravity term: the hover update keeps the engine's gravity at 0 while it flies (0x1404AB840 ->
//  veh+0x2EC, Notes\28), and the base update's gravity write is overwritten with the rest.
// =============================================================================================
#pragma once
#include <cmath>
#include <cstdint>

namespace copter {

struct Params
{
    float height      = 16.0f;   // u above the suspect ([wanted] heli_height4 / heli_height5)
    float groundClear = 9.0f;    // u above the ground under the helicopter, at least
    float orbitR      = 14.0f;   // u: the circle round a suspect who is (nearly) stopped ([wanted] heli_radius)
    float orbitRate   = 0.35f;   // rad/s round him
    float chaseSpeed  = 5.0f;    // u/s: faster than this he's on the move (trail), slower: orbit (+-1 hysteresis)
    float trail       = 10.0f;   // u behind a suspect on the move...
    float sideOff     = 8.0f;    // ...and this far to one side
    float lead        = 0.3f;    // s of his velocity aimed ahead (what the position loop lags by)
    float vMax        = 36.0f;   // u/s horizontal ([wanted] heli_speed; the player's helicopter does 45)
    float vUp         = 10.0f;   // u/s climb...
    float vDown       = 7.0f;    // ...and sink
    float aMax        = 12.0f;   // u/s^2 horizontal (~ the tilt limit's worth)
    float ayMax       = 10.0f;   // u/s^2 vertical
    float kPos        = 0.8f;    // 1/s: position error -> horizontal velocity
    float kY          = 1.2f;    // 1/s: height error -> vertical velocity
    float tauV        = 0.6f;    // s: horizontal velocity error -> acceleration
    float tauY        = 0.4f;    // s: vertical
    float faceDist    = 40.0f;   // u: nearer than this the nose is on the suspect, else the way it flies
    float yawGain     = 2.0f;    // 1/s
    float yawMax      = 1.4f;    // rad/s
    float tiltMaxDeg  = 25.0f;
    float attTau      = 0.25f;   // s: attitude error -> rate (heli_model.h's loop)
    float attLag      = 0.05f;   // s: how fast the body takes the commanded rate
    float sep         = 14.0f;   // u from an aircraft a player flies, at least...
    float sepAbove    = 10.0f;   // ...and this far above it
    float peerSep     = 12.0f;   // u between the two police helicopters, at least
    float stuckV      = 1.5f;    // u/s: making less headway than this along the way it asks for...
    float stuckCmd    = 8.0f;    // ...while asking for more than this...
    float stuckS      = 2.5f;    // ...this long = stuck: climb
    float climbExtra  = 15.0f;   // u added to the height for the climb out
    float climbS      = 4.0f;    // s the climb is held before it eases back down
    float respawnS    = 8.0f;    // s stuck in all = give up on this one (respawn)
    float leaveClimb  = 25.0f;   // u it climbs on the way out
    float leaveSpeed  = 30.0f;   // u/s it flies off at
};

struct State
{
    float   theta;               // rad: where on its circle it is going round (the orbit angle)
    float   stuckT;              // s stuck so far
    float   climbT;              // s of the climb-out left
    float   yExtra;              // u the climb-out adds to the height
    float   leaveDir[2];         // x, z: where it flies off to
    float   leaveY;              // the height it climbs to on the way out
    uint8_t chasing;             // 1 trail, 0 orbit (with hysteresis)
    uint8_t leaving;             // the director's leaving, latched (the way out is picked once)
    uint8_t respawn;             // stuck too long: replace it (sticky until Reset)
    uint8_t primed;
};

inline void Reset(State& s)
{
    s.theta = 0.0f; s.stuckT = 0.0f; s.climbT = 0.0f; s.yExtra = 0.0f;
    s.leaveDir[0] = 1.0f; s.leaveDir[1] = 0.0f; s.leaveY = 0.0f;
    s.chasing = 0; s.leaving = 0; s.respawn = 0; s.primed = 0;
}

struct Input
{
    float   dt;
    float   pos[3];                   // body position
    float   right[3], up[3], back[3]; // body rows (engine forward = -back)
    float   lv[3], av[3];             // the velocities the physics step left (the engine's writes undone)
    float   hag;                      // height above the ground under it (the engine's probe); > 1e30 = unknown
    float   tgt[3];                   // the suspect (non-finite = none: it holds where it is)
    float   tgtV[3];                  // his velocity (0 on foot)
    uint8_t tgtFlying;                // he flies a helicopter / plane: keep clear of it
    float   avoid[2][3];              // other aircraft a player flies (co-op), kept clear of too
    int     nAvoid;
    float   side;                     // -1 / +1: the side it trails on
    float   orbitAt;                  // rad: where on the circle to be (the second helicopter: opposite the
                                      // first's); non-finite = its own circle, started where it is
    float   peer[3];                  // the other police helicopter (non-finite = none): not too close
    uint8_t leaving;                  // fly off
};

enum Mode : uint8_t { kOrbit = 0, kTrail = 1, kLeave = 2, kHold = 3 };

struct Output
{
    float   lv[3], av[3];             // what to write back
    float   aim[3];                   // the point it flies to (aim[1] = its height target)
    float   vCmd[3];                  // the velocity it asks for
    float   distH, dist;              // to the suspect (1e9 = none)
    float   yawErr;                   // rad
    float   tiltDeg;                  // the attitude it asks for
    uint8_t mode;
    uint8_t stuck;                    // climbing out of something
    uint8_t respawn;                  // copy of the state's
    uint8_t valid;
};

// ---- small vector helpers ---------------------------------------------------------------------
inline float Dot3(const float a[3], const float b[3]) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
inline void  Cross3(const float a[3], const float b[3], float o[3])
{
    o[0] = a[1] * b[2] - a[2] * b[1]; o[1] = a[2] * b[0] - a[0] * b[2]; o[2] = a[0] * b[1] - a[1] * b[0];
}
inline bool  Norm3(float v[3])
{
    const float m = std::sqrt(Dot3(v, v));
    if (!(m > 1e-6f) || !std::isfinite(m)) return false;
    v[0] /= m; v[1] /= m; v[2] /= m;
    return true;
}
inline float Clampf(float v, float lo, float hi) { return v != v ? lo : (v < lo ? lo : (v > hi ? hi : v)); }
inline float LagK(float dt, float tau) { return tau > 1e-4f ? Clampf(1.0f - std::exp(-dt / tau), 0.0f, 1.0f) : 1.0f; }
inline bool  Fin3v(const float v[3]) { return std::isfinite(v[0]) && std::isfinite(v[1]) && std::isfinite(v[2]); }
inline float WrapPi(float a)
{
    if (!std::isfinite(a)) return 0.0f;
    a = std::fmod(a + 3.14159265f, 6.28318531f);
    if (a < 0.0f) a += 6.28318531f;
    return a - 3.14159265f;
}
// scale (x, z) down to at most `lim` long
inline void ClampLen2(float& x, float& z, float lim)
{
    const float m = std::sqrt(x * x + z * z);
    if (m > lim && m > 1e-6f) { x *= lim / m; z *= lim / m; }
}

inline void Step(const Params& p, State& s, const Input& in, Output& o)
{
    const float g = 9.8f;
    for (int k = 0; k < 3; ++k) { o.lv[k] = in.lv[k]; o.av[k] = in.av[k]; o.aim[k] = 0.0f; o.vCmd[k] = 0.0f; }
    o.distH = o.dist = 1e9f; o.yawErr = 0.0f; o.tiltDeg = 0.0f;
    o.mode = kHold; o.stuck = 0; o.respawn = s.respawn; o.valid = 0;
    if (!(in.dt > 0.0f) || in.dt > 0.1f || !Fin3v(in.pos) || !Fin3v(in.right) || !Fin3v(in.up) ||
        !Fin3v(in.back) || !Fin3v(in.lv) || !Fin3v(in.av))
        return;                                                     // the engine's frame stands
    const float dt = in.dt;
    float up[3] = { in.up[0], in.up[1], in.up[2] };
    float fwd[3] = { -in.back[0], 0.0f, -in.back[2] };
    if (!Norm3(up)) return;
    if (!Norm3(fwd)) { fwd[0] = -up[0]; fwd[1] = 0.0f; fwd[2] = -up[2]; if (!Norm3(fwd)) { fwd[0] = 0.0f; fwd[2] = -1.0f; } }

    const bool haveT = Fin3v(in.tgt);
    float tv[3] = { 0.0f, 0.0f, 0.0f };
    if (haveT) for (int k = 0; k < 3; ++k) tv[k] = std::isfinite(in.tgtV[k]) ? Clampf(in.tgtV[k], -120.0f, 120.0f) : 0.0f;
    const bool hagOk = std::isfinite(in.hag) && in.hag < 1e30f;
    const float ground = hagOk ? in.pos[1] - std::fmax(in.hag, 0.0f) : -1e30f;
    float dxT = 0.0f, dzT = 0.0f, distH = 1e9f;
    if (haveT)
    {
        dxT = in.tgt[0] - in.pos[0]; dzT = in.tgt[2] - in.pos[2];
        distH = std::sqrt(dxT * dxT + dzT * dzT);
        const float dy = in.tgt[1] - in.pos[1];
        o.dist = std::sqrt(distH * distH + dy * dy);
        o.distH = distH;
    }

    // ---- first frame / leaving latch ------------------------------------------------------------
    const float vth = std::sqrt(tv[0] * tv[0] + tv[2] * tv[2]);
    if (!s.primed)
    {
        s.primed = 1;
        s.chasing = vth > p.chaseSpeed ? 1 : 0;
        s.theta = haveT ? std::atan2(in.pos[2] - in.tgt[2], in.pos[0] - in.tgt[0]) : 0.0f;
    }
    if (in.leaving && !s.leaving)
    {
        // the way out: straight away from him (or on along its nose), climbing
        float lx = haveT ? -dxT : fwd[0], lz = haveT ? -dzT : fwd[2];
        const float m = std::sqrt(lx * lx + lz * lz);
        if (m > 1e-3f) { lx /= m; lz /= m; } else { lx = fwd[0]; lz = fwd[2]; }
        s.leaveDir[0] = lx; s.leaveDir[1] = lz;
        s.leaveY = std::fmax(in.pos[1], haveT ? in.tgt[1] + p.height : in.pos[1]) + p.leaveClimb;
    }
    s.leaving = in.leaving ? 1 : 0;

    // ---- where it wants to be -------------------------------------------------------------------
    float P[3] = { in.pos[0], in.pos[1], in.pos[2] };              // hold: right here
    float vff[2] = { 0.0f, 0.0f };                                  // feed-forward (horizontal)
    float yDes = in.pos[1];
    uint8_t mode = kHold;
    if (s.leaving)
    {
        mode = kLeave;
        yDes = s.leaveY;
    }
    else if (haveT)
    {
        if (!s.chasing && vth > p.chaseSpeed + 1.0f) s.chasing = 1;
        else if (s.chasing && vth < p.chaseSpeed - 1.0f)
        {
            s.chasing = 0;                                          // orbit from where it is now
            s.theta = std::atan2(in.pos[2] - in.tgt[2], in.pos[0] - in.tgt[0]);
        }
        if (s.chasing)
        {
            mode = kTrail;
            const float fx = tv[0] / vth, fz = tv[2] / vth;         // his way, and its right (up x back)
            const float rx = -fz, rz = fx;
            const float sd = (in.side < 0.0f ? -1.0f : 1.0f) * p.sideOff;
            P[0] = in.tgt[0] + p.lead * tv[0] - p.trail * fx + sd * rx;
            P[2] = in.tgt[2] + p.lead * tv[2] - p.trail * fz + sd * rz;
            vff[0] = tv[0]; vff[1] = tv[2];
        }
        else
        {
            mode = kOrbit;
            // its own circle, or the place it's given (opposite the other helicopter); kept in step
            // either way, so dropping the given place later carries on from it without a jump
            s.theta = std::isfinite(in.orbitAt) ? WrapPi(in.orbitAt) : WrapPi(s.theta + p.orbitRate * dt);
            const float a = s.theta, ca = std::cos(a), sa = std::sin(a);
            P[0] = in.tgt[0] + p.orbitR * ca;
            P[2] = in.tgt[2] + p.orbitR * sa;
            vff[0] = tv[0] - p.orbitR * p.orbitRate * sa;           // the orbit point's own motion
            vff[1] = tv[2] + p.orbitR * p.orbitRate * ca;
        }
        yDes = in.tgt[1] + p.height;
    }

    // keep clear of what a player flies: the suspect himself, and the other player (co-op)
    float av3[3][3]; int nA = 0;
    if (haveT && in.tgtFlying) { for (int k = 0; k < 3; ++k) av3[nA][k] = in.tgt[k]; ++nA; }
    for (int i = 0; i < in.nAvoid && i < 2; ++i)
        if (Fin3v(in.avoid[i])) { for (int k = 0; k < 3; ++k) av3[nA][k] = in.avoid[i][k]; ++nA; }
    if (mode != kLeave && mode != kHold)
        for (int i = 0; i < nA; ++i)
        {
            yDes = std::fmax(yDes, av3[i][1] + p.sepAbove);
            float hx = P[0] - av3[i][0], hz = P[2] - av3[i][2];
            float hm = std::sqrt(hx * hx + hz * hz);
            if (hm < p.sep)
            {
                if (hm < 1e-3f)
                {
                    hx = in.pos[0] - av3[i][0]; hz = in.pos[2] - av3[i][2]; hm = std::sqrt(hx * hx + hz * hz);
                    if (hm < 1e-3f) { hx = -fwd[0]; hz = -fwd[2]; hm = 1.0f; }
                }
                const float nx = hx / hm, nz = hz / hm;
                P[0] = av3[i][0] + nx * p.sep;
                P[2] = av3[i][2] + nz * p.sep;
                // and the aim point's own motion no longer carries it in toward the aircraft
                const float in2 = vff[0] * nx + vff[1] * nz;
                if (in2 < 0.0f) { vff[0] -= in2 * nx; vff[1] -= in2 * nz; }
            }
        }
    // the ground, and the climb out of whatever it's stuck on
    if (hagOk) yDes = std::fmax(yDes, ground + p.groundClear);
    if (mode != kHold) yDes += s.yExtra;
    P[1] = yDes;

    // ---- the velocity it asks for ---------------------------------------------------------------
    float vx, vz;
    if (mode == kLeave) { vx = s.leaveDir[0] * p.leaveSpeed; vz = s.leaveDir[1] * p.leaveSpeed; }
    else
    {
        vx = p.kPos * (P[0] - in.pos[0]) + vff[0];
        vz = p.kPos * (P[2] - in.pos[2]) + vff[1];
    }
    ClampLen2(vx, vz, std::fmax(p.vMax, mode == kLeave ? p.leaveSpeed : 0.0f));
    float vy = Clampf(p.kY * (yDes - in.pos[1]) + (mode == kLeave || mode == kHold ? 0.0f : Clampf(tv[1], -p.vDown, p.vUp)),
                      -p.vDown, p.vUp);
    // too close to one of them right now (it closed in faster than the aim point moved): straight out,
    // and up when it's level with or above it (below it, out and down - never up through it)
    // (from a little outside sep, so the push has begun before the line is crossed)
    for (int i = 0; i < nA; ++i)
    {
        const float d[3] = { in.pos[0] - av3[i][0], in.pos[1] - av3[i][1], in.pos[2] - av3[i][2] };
        const float dist = std::sqrt(Dot3(d, d)), edge = p.sep * 1.2f;
        if (!(dist < edge)) continue;
        const float push = (edge - dist) * 3.0f;
        const float dh = std::sqrt(d[0] * d[0] + d[2] * d[2]);
        if (dh > 1e-3f) { vx += d[0] / dh * push; vz += d[2] / dh * push; }
        else { vx -= fwd[0] * push; vz -= fwd[2] * push; }
        vy += d[1] >= -1.0f ? push : -push;
    }
    // the other police helicopter: apart, sideways (the higher one a little up)
    if (Fin3v(in.peer) && mode != kHold)
    {
        const float d[3] = { in.pos[0] - in.peer[0], in.pos[1] - in.peer[1], in.pos[2] - in.peer[2] };
        const float dist = std::sqrt(Dot3(d, d));
        if (dist < p.peerSep)
        {
            const float push = (p.peerSep - dist) * 1.5f;
            const float dh = std::sqrt(d[0] * d[0] + d[2] * d[2]);
            if (dh > 1e-3f) { vx += d[0] / dh * push; vz += d[2] / dh * push; }
            else { vx += fwd[2] * push; vz -= fwd[0] * push; }            // dead on top: sideways
            vy += (d[1] >= 0.0f ? 0.5f : -0.5f) * push;
        }
    }
    ClampLen2(vx, vz, p.vMax * 1.2f);
    vy = Clampf(vy, -p.vDown, p.vUp * 1.5f);
    o.vCmd[0] = vx; o.vCmd[1] = vy; o.vCmd[2] = vz;
    for (int k = 0; k < 3; ++k) o.aim[k] = P[k];

    // ---- through the acceleration limits ----------------------------------------------------------
    float ax = (vx - in.lv[0]) / Clampf(p.tauV, 0.05f, 5.0f), az = (vz - in.lv[2]) / Clampf(p.tauV, 0.05f, 5.0f);
    ClampLen2(ax, az, p.aMax);
    const float ay = Clampf((vy - in.lv[1]) / Clampf(p.tauY, 0.05f, 5.0f), -p.ayMax, p.ayMax);
    o.lv[0] = in.lv[0] + ax * dt;
    o.lv[1] = in.lv[1] + ay * dt;
    o.lv[2] = in.lv[2] + az * dt;

    // ---- the nose: on him when near, else the way it flies ----------------------------------------
    float dd[2] = { 0.0f, 0.0f }; bool haveD = false;
    if (mode == kLeave) { dd[0] = s.leaveDir[0]; dd[1] = s.leaveDir[1]; haveD = true; }
    else if (haveT && distH < p.faceDist && distH > 0.5f) { dd[0] = dxT / distH; dd[1] = dzT / distH; haveD = true; }
    else
    {
        const float vm = std::sqrt(vx * vx + vz * vz);
        if (vm > 2.0f) { dd[0] = vx / vm; dd[1] = vz / vm; haveD = true; }
    }
    float yawRate = 0.0f;
    if (haveD)
    {
        // + about world up turns the nose LEFT (heli_model.h's convention): cross(f, d).y = f.z d.x - f.x d.z
        const float c = fwd[2] * dd[0] - fwd[0] * dd[1], dp = fwd[0] * dd[0] + fwd[2] * dd[1];
        o.yawErr = std::atan2(c, dp);
        yawRate = Clampf(p.yawGain * o.yawErr, -p.yawMax, p.yawMax);
    }

    // ---- the attitude: the rotor tilted into the acceleration (thrust = m (a + g)) -----------------
    // The body is flown by its velocity, so the tilt is how it looks, not what moves it: aMax (12) is
    // more than a 25 deg rotor could give (g tan 25 = 4.6), and the look is held to tiltMaxDeg.
    const float tMax = Clampf(p.tiltMaxDeg, 1.0f, 60.0f) * 0.0174533f;
    float tx = ax, tz = az;
    ClampLen2(tx, tz, g * std::tan(tMax));
    float upT[3] = { tx, g, tz };
    Norm3(upT);
    o.tiltDeg = std::acos(Clampf(upT[1], -1.0f, 1.0f)) * 57.2958f;
    float e[3]; Cross3(up, upT, e);
    const float kAtt = 1.0f / Clampf(p.attTau, 0.03f, 2.0f);
    float wCmd[3] = { e[0] * kAtt, e[1] * kAtt + yawRate, e[2] * kAtt };
    {
        const float tilt = std::sqrt(wCmd[0] * wCmd[0] + wCmd[2] * wCmd[2]);
        if (tilt > 3.0f) { wCmd[0] *= 3.0f / tilt; wCmd[2] *= 3.0f / tilt; }   // rad/s, heli_model.h's lid
    }
    const float aw = LagK(dt, p.attLag);
    for (int k = 0; k < 3; ++k) o.av[k] = in.av[k] + (wCmd[k] - in.av[k]) * aw;

    // ---- stuck: no headway the way it asks to go, while asking to be fast -----------------------------
    // (headway, not speed: pressed against a wall it can still slide along it at the orbit's pace)
    const float cmd = std::sqrt(vx * vx + vy * vy + vz * vz);
    const float headway = cmd > 1e-3f ? (in.lv[0] * vx + in.lv[1] * vy + in.lv[2] * vz) / cmd : 0.0f;
    if (mode != kHold && headway < p.stuckV && cmd > p.stuckCmd) s.stuckT += dt;
    else if (headway > 2.0f * p.stuckV || cmd < 0.5f * p.stuckCmd) s.stuckT = 0.0f;
    if (s.stuckT >= p.stuckS && s.climbT <= 0.0f && s.yExtra < 2.0f * p.climbExtra)
    {
        s.climbT = p.climbS;
        s.yExtra += p.climbExtra;
    }
    if (s.stuckT >= p.respawnS) s.respawn = 1;
    if (s.climbT > 0.0f) s.climbT -= dt;
    else if (s.yExtra > 0.0f) s.yExtra = std::fmax(0.0f, s.yExtra - 3.0f * dt);   // eases back down
    o.stuck = s.climbT > 0.0f ? 1 : 0;
    o.respawn = s.respawn;

    // ---- sanity lids ----------------------------------------------------------------------------------
    for (int k = 0; k < 3; ++k) { o.lv[k] = Clampf(o.lv[k], -90.0f, 90.0f); o.av[k] = Clampf(o.av[k], -8.0f, 8.0f); }
    o.mode = mode;
    o.valid = Fin3v(o.lv) && Fin3v(o.av);
    if (!o.valid) for (int k = 0; k < 3; ++k) { o.lv[k] = in.lv[k]; o.av[k] = in.av[k]; }
    if (!std::isfinite(s.theta)) s.theta = 0.0f;
    if (!std::isfinite(s.stuckT)) s.stuckT = 0.0f;
    if (!std::isfinite(s.yExtra)) s.yExtra = 0.0f;
}

// Has a police helicopter the suspect in sight? Flying for him - not leaving, not climbing out of a
// jam - and within `sight` of him along the ground (sight <= 0: never). The one test for both things
// a helicopter does to the level (wanted.h): it counts as a pursuer (for wanted_model.h's HeliHold),
// and a crime it sees is a crime the police saw. distH 1e9 (not flown this tick) or NaN: not in sight.
inline bool InSight(float distH, bool leaving, bool stuck, float sight)
{
    return !leaving && !stuck && sight > 0.0f && distH < sight;
}

} // namespace copter
