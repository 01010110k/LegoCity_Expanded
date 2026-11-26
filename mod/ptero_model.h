// =============================================================================================
//  ptero_model.h - the Pteranodon's flight (2026-11-15: the Pteranodon upgraded to
//  really fly, the way such a creature would). ptero.h is the engine side; this file is pure - plain
//  facts in, a velocity and an attitude out - so test\ptero_harness.cpp runs it as is.
//
//  A big gliding creature, not a helicopter. Its state is an airspeed V along a flight path angle
//  gamma (up +), a heading (rad, the game's yaw: forward = (sin h, 0, cos h), + = turning right) and
//  a bank. Per physics step:
//    * wingbeats - a JUMP press (or JUMP held: one beat every flap_rate s) adds a bump up and forward
//      and, while the wings are working, tilts the commanded path up by flap_climb and holds the
//      speed near cruise: holding A climbs at about cruise * sin(flap_climb - glide);
//    * the stick's Y commands the flight path: centred = the glide slope (sink about
//      cruise * sin(glide)), pulled = up to climb deg (speed bleeds away), pushed = down to dive deg
//      (speed builds); the path follows at pitch_rate scaled by the lift the speed gives;
//    * energy: dV/dt = -g sin(gamma) - c V^2 (+ flap power), c sized so the glide slope settles at
//      cruise. Below stall the lift fades: the path falls ballistically (the nose drops), speed
//      builds in the dive and the wings bite again - the stall and its recovery;
//    * the stick's X banks it (bank deg at full stick) and the bank turns it (turn deg/s at full
//      bank, less when slow);
//    * limits: a soft ceiling (climb fades over ceiling_zone, nothing above ceiling), a world box it
//      turns back from, the sea (never below sea_y + skim: it skims and bounces off the water, and
//      a creature the game puts in the water is lifted straight out).
//  Modes: ground (the game's own walking; a JUMP press there, or a fall faster than auto_glide,
//  starts a flight), takeoff (the leap; ground contact ignored for takeoff_grace s), fly, coast (the
//  rider got off in the air: hands-off glide to the ground), landed (one step: hand back). After
//  the engine's sweep, AfterSweep compares where it went with where it was sent: a wall stops it
//  (no damage - the game's sweep already kept it out), ground under it ends the flight.
// =============================================================================================
#pragma once
#include <cmath>

namespace ptm {

static const float kPi = 3.14159265f;
static const float kDeg = 0.0174532925f;

inline float Clamp(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline float Wrap(float a)
{
    if (!(a == a) || a > 1.0e4f || a < -1.0e4f) return 0.0f;
    while (a > kPi) a -= 2.0f * kPi;
    while (a <= -kPi) a += 2.0f * kPi;
    return a;
}
inline float Approach(float v, float t, float step) { return v < t ? (v + step < t ? v + step : t) : (v - step > t ? v - step : t); }
inline bool Fin(float v) { return v == v && v < 3.0e38f && v > -3.0e38f; }

enum Mode : int { kGround = 0, kTakeoff, kFly, kCoast, kLanded };
inline const char* ModeName(int m)
{
    static const char* n[] = { "ground", "takeoff", "fly", "coast", "landed" };
    return m >= 0 && m <= kLanded ? n[m] : "?";
}

enum Event : int { kEvNone = 0, kEvTakeoff, kEvGlideStart, kEvFlap, kEvStall, kEvRecover, kEvSkim, kEvSwimEscape,
                   kEvWall, kEvRoof, kEvCeiling, kEvBounds, kEvCoast, kEvCoastEnd, kEvLand, kEvDead };
inline const char* EventName(int e)
{
    static const char* n[] = { "none", "takeoff", "glide-start", "flap", "stall", "recover", "skim", "swim-escape",
                               "wall", "roof", "ceiling", "bounds", "coast", "coast-end", "land", "dead" };
    return e >= 0 && e <= kEvDead ? n[e] : "?";
}

struct Params
{
    float cruise       = 11.0f;    // u/s: the hands-off glide speed
    float stall        = 5.5f;     // u/s: below this the wings stop carrying it
    float maxSpeed     = 26.0f;    // u/s
    float glideDeg     = 6.0f;     // deg: the hands-off glide slope
    float climbDeg     = 24.0f;    // deg: the path at full pull
    float diveDeg      = 55.0f;    // deg: the path at full push
    float pitchRate    = 1.8f;     // rad/s: how fast the path follows the stick, at cruise
    float gravity      = 7.0f;     // u/s^2: the speed / height exchange
    float flapLift     = 2.5f;     // u/s up per wingbeat
    float flapThrust   = 1.2f;     // u/s forward per wingbeat
    float flapPeriod   = 0.45f;    // s between beats while JUMP is held
    float flapGap      = 0.18f;    // s: the quickest a press can beat again
    float flapClimbDeg = 16.0f;    // deg added to the commanded path while the wings work
    float flapPower    = 2.4f;     // u/s^2 the working wings add (fading out toward 1.2 x cruise)
    float stallDropDeg = 25.0f;    // deg: the path a stall drops the nose to
    float takeoffUp    = 5.5f;     // u/s up at the take-off leap
    float takeoffSpeed = 7.0f;     // u/s forward at the take-off leap (at least)
    float takeoffGrace = 0.35f;    // s after the leap that ground contact is ignored
    float bankDeg      = 40.0f;    // deg of bank at full stick
    float bankRate     = 3.0f;     // rad/s
    float turnDeg      = 80.0f;    // deg/s of heading change at full bank and full lift
    float turnDrag     = 0.8f;     // u/s^2 a full-bank turn costs
    float ceiling      = 200.0f;   // world y: nothing climbs above it
    float ceilingZone  = 15.0f;    // u below the ceiling over which the climb fades out
    float seaY         = 0.35f;    // world y of the sea's surface (< -1000 = no sea floor)
    float skim         = 0.6f;     // u above the water its feet are held
    float skimDrag     = 0.6f;     // 1/s of speed lost while skimming
    float skimBounce   = 0.35f;    // of the sink rate given back as a bounce off the water
    float tilt         = 1.0f;     // 0..1: how much of the bank / path pitch the body shows
    float tiltPitchDeg = 25.0f;    // deg: the body's pitch limit
    float autoGlide    = 2.5f;     // u/s: a ridden one falling faster than this spreads its wings (0 = never)
    float coastTime    = 30.0f;    // s a riderless one glides before it is left to the game
    float wallKeep     = 0.5f;     // a step that got less than this share of its horizontal travel hit something
    float wallLogLoss  = 4.0f;     // u/s lost that counts as a wall hit worth an event
    float landClimb    = 0.5f;     // u/s: ground under it while not climbing faster than this = landed
    int   bounds       = 1;
    float minX = -400.0f, maxX = 760.0f, minZ = -460.0f, maxZ = 520.0f;   // the world box it turns back from
};

struct Input
{
    float turn;          // -1 left .. +1 right
    float pitch;         // -1 pushed (dive) .. +1 pulled (climb)
    bool  flapHeld;      // JUMP held
    int   flapPresses;   // JUMP presses since the last step
    bool  ridden;        // a player is on it
};

struct Facts
{
    float dt;
    float pos[3];        // where it is (feet)
    float vel[3];        // the game's velocity for this step (before ours)
    float heading;       // the game's yaw (rad)
    bool  grounded;      // standing on something (before this step's sweep)
    float waterY;        // a water surface the game reports at this spot (< -1e5 = none)
    bool  swimming;      // the game has it in the water
    bool  dead;          // death / doomed
};

struct State
{
    int   mode;
    float V, gamma, heading, bank;
    float beatT, flapT, airT, fallT, coastT, stuckT;
    float waterY, waterT;
    float cmd[3];        // the velocity sent last step
    bool  stalled, skimLatch, ceilLatch, boundsLatch, swimLatch;
    int   flaps, skims, walls, stalls;
    float topSpeed, topY, lastWallLoss;
};

struct Out
{
    bool  write;         // write vel and the attitude this step
    bool  release;       // hand back to the game (no velocity; an upright body facing heading)
    float vel[3];
    float heading, pitch, roll;   // the body's attitude (rad): yaw, nose up +, right wing down +
    int   event;
};

inline void Reset(State& s)
{
    s = State{};
    s.mode = kGround;
    s.waterY = -1.0e9f;
    s.beatT = 10.0f;
}

inline bool Airborne(const State& s) { return s.mode == kTakeoff || s.mode == kFly || s.mode == kCoast; }

// The drag constant that makes the hands-off glide settle at cruise.
inline float DragK(const Params& p)
{
    const float cr = p.cruise > 1.0f ? p.cruise : 1.0f;
    return p.gravity * std::sin(Clamp(p.glideDeg, 0.5f, 30.0f) * kDeg) / (cr * cr);
}

// Lift authority 0 (stalled) .. 1 (cruise and faster).
inline float Authority(const Params& p, float V)
{
    const float span = p.cruise - p.stall > 0.5f ? p.cruise - p.stall : 0.5f;
    return Clamp((V - p.stall) / span, 0.0f, 1.0f);
}

inline void FromCartesian(State& s, float vh, float vy)
{
    if (vh < 0.0f) vh = 0.0f;
    s.V = std::sqrt(vh * vh + vy * vy);
    s.gamma = s.V > 0.01f ? std::atan2(vy, vh > 0.01f ? vh : 0.01f) : 0.0f;
}

inline float SeaFloor(const Params& p, const State& s)
{
    float f = p.seaY > -1000.0f ? p.seaY + p.skim : -1.0e9f;
    if (s.waterT > 0.0f && s.waterY > -1.0e5f && s.waterY + p.skim > f) f = s.waterY + p.skim;
    return f;
}

// One physics step. dt must be > 0.
inline Out Step(const Params& p, State& s, const Input& in, const Facts& f)
{
    Out o = {};
    o.heading = s.heading;
    const float dt = Clamp(f.dt, 0.0f, 0.1f);
    if (dt <= 0.0f) return o;
    if (f.waterY > -1.0e5f && f.waterY < 1.0e5f) { s.waterY = f.waterY; s.waterT = 3.0f; }
    else if (s.waterT > 0.0f) s.waterT -= dt;

    if (f.dead)
    {
        if (s.mode != kGround) { s.mode = kGround; o.release = true; o.heading = s.heading; o.event = kEvDead; }
        return o;
    }
    if (s.mode == kLanded)
    {
        s.mode = kGround; s.fallT = 0.0f;
        o.release = true; o.heading = s.heading;
        return o;
    }

    // ---- on the ground: the game walks it; we wait for a take-off --------------------------------
    if (s.mode == kGround)
    {
        s.heading = Wrap(f.heading);
        o.heading = s.heading;
        if (!in.ridden) { s.fallT = 0.0f; return o; }
        const float sh = std::sin(s.heading), ch = std::cos(s.heading);
        const float along = f.vel[0] * sh + f.vel[2] * ch;
        if ((in.flapPresses > 0 && f.grounded) || f.swimming)
        {
            // the leap: from standing (or out of the water - a ridden one never swims)
            FromCartesian(s, along > p.takeoffSpeed ? along : p.takeoffSpeed, p.takeoffUp);
            s.mode = kTakeoff; s.airT = 0.0f; s.bank = 0.0f; s.beatT = 0.0f; s.flapT = p.flapPeriod * 1.3f;
            s.stuckT = 0.0f; s.stalled = false; ++s.flaps;
            o.event = kEvTakeoff;
        }
        else if (in.flapPresses > 0)
        {
            // already off the ground (a jump, a ledge): the wings open with a beat, this step
            const float h = std::sqrt(f.vel[0] * f.vel[0] + f.vel[2] * f.vel[2]);
            FromCartesian(s, h > p.stall + 1.0f ? h : p.stall + 1.0f, f.vel[1] > -8.0f ? f.vel[1] : -8.0f);
            s.mode = kFly; s.airT = p.takeoffGrace; s.bank = 0.0f; s.beatT = 10.0f; s.flapT = 0.0f; s.stuckT = 0.0f;
            s.stalled = false;
            o.event = kEvGlideStart;
        }
        else if (p.autoGlide > 0.0f && !f.grounded && f.vel[1] < -p.autoGlide)
        {
            s.fallT += dt;
            if (s.fallT < 0.15f) return o;
            const float h = std::sqrt(f.vel[0] * f.vel[0] + f.vel[2] * f.vel[2]);
            FromCartesian(s, h > p.stall + 1.0f ? h : p.stall + 1.0f, f.vel[1] > -8.0f ? f.vel[1] : -8.0f);
            if (s.gamma < -60.0f * kDeg) s.gamma = -60.0f * kDeg;
            s.mode = kFly; s.airT = p.takeoffGrace; s.bank = 0.0f; s.beatT = 10.0f; s.flapT = 0.0f; s.stuckT = 0.0f;
            s.stalled = false;
            o.event = kEvGlideStart;
        }
        else { s.fallT = 0.0f; return o; }
    }
    else if (!in.ridden && s.mode != kCoast)
    {
        s.mode = kCoast; s.coastT = 0.0f;
        o.event = kEvCoast;
    }

    // ---- in the air ------------------------------------------------------------------------------
    const bool coast = s.mode == kCoast;
    const float turnIn = coast ? 0.0f : Clamp(in.turn, -1.0f, 1.0f);
    const float pitchIn = coast ? 0.0f : Clamp(in.pitch, -1.0f, 1.0f);
    s.airT += dt; s.beatT += dt;
    if (s.flapT > 0.0f) s.flapT -= dt;
    if (coast)
    {
        s.coastT += dt;
        if (s.coastT > p.coastTime)
        {
            s.mode = kGround;
            o.release = true; o.heading = s.heading; o.event = kEvCoastEnd;
            return o;
        }
    }
    const float y = f.pos[1];
    if (!Fin(s.V) || !Fin(s.gamma)) { s.V = p.cruise; s.gamma = 0.0f; }

    // in the water: straight back out
    if (f.swimming)
    {
        if (s.gamma < 25.0f * kDeg) s.gamma = 25.0f * kDeg;
        if (s.V < p.stall + 2.0f) s.V = p.stall + 2.0f;
        s.flapT = p.flapPeriod;
        if (!s.swimLatch && !o.event) o.event = kEvSwimEscape;
        s.swimLatch = true;
    }
    else s.swimLatch = false;

    const float liftScale = Clamp((p.ceiling - y) / (p.ceilingZone > 0.1f ? p.ceilingZone : 0.1f), 0.0f, 1.0f);

    // wingbeats
    bool beat = false;
    if (!coast)
    {
        if (in.flapPresses > 0 && s.beatT >= p.flapGap) beat = true;
        else if (in.flapHeld && s.beatT >= p.flapPeriod) beat = true;
    }
    if (beat)
    {
        // a beat: a bump up (and a little forward). Slow, it is lift the wings make on their own
        // (vertical speed added); at speed it mostly turns the path up - the energy comes from the
        // wings' work over time (flap_power), not from the bump.
        s.beatT = 0.0f; s.flapT = p.flapPeriod * 1.3f; ++s.flaps;
        const float a0 = Authority(p, s.V), V0 = s.V;
        const float vh = s.V * std::cos(s.gamma) + p.flapThrust, vy = s.V * std::sin(s.gamma) + p.flapLift * liftScale;
        FromCartesian(s, vh, vy);
        const float vKeep = V0 + 0.3f * p.flapThrust;
        s.V = s.V + (vKeep - s.V) * a0;
        if (!o.event) o.event = kEvFlap;
    }
    const bool flapping = s.flapT > 0.0f;

    // lift, stall: a stall (lift nearly gone) drops the nose whatever the stick says, until the
    // speed is back halfway to cruise
    const float a = Authority(p, s.V);
    if (a <= 0.15f && !s.stalled) { s.stalled = true; ++s.stalls; if (!o.event) o.event = kEvStall; }
    else if (a >= 0.5f && s.stalled) { s.stalled = false; if (!o.event) o.event = kEvRecover; }

    // the commanded path
    float gc = -p.glideDeg * kDeg + (pitchIn > 0.0f ? pitchIn * p.climbDeg : pitchIn * p.diveDeg) * kDeg;
    if (flapping) gc += p.flapClimbDeg * kDeg;
    if (gc > 0.0f) gc *= liftScale;
    gc = Clamp(gc, -p.diveDeg * kDeg, (p.climbDeg + p.flapClimbDeg) * kDeg);
    if (s.stalled && !flapping && gc > -p.stallDropDeg * kDeg) gc = -p.stallDropDeg * kDeg;
    const float rate = p.pitchRate * (s.stalled ? (a > 0.5f ? a : 0.5f) : a) * dt;
    s.gamma += Clamp(gc - s.gamma, -rate, rate);
    s.gamma -= (p.gravity / (s.V > 1.0f ? s.V : 1.0f)) * std::cos(s.gamma) * (1.0f - a) * dt;
    s.gamma = Clamp(s.gamma, -80.0f * kDeg, 60.0f * kDeg);

    // speed
    const float bankMax = (p.bankDeg > 1.0f ? p.bankDeg : 1.0f) * kDeg;
    float dV = -p.gravity * std::sin(s.gamma) - DragK(p) * s.V * s.V - p.turnDrag * std::fabs(s.bank) / bankMax;
    if (flapping) dV += p.flapPower * Clamp((1.2f * p.cruise - s.V) / (0.2f * p.cruise > 0.5f ? 0.2f * p.cruise : 0.5f), 0.0f, 1.0f);
    s.V = Clamp(s.V + dV * dt, 0.0f, p.maxSpeed);

    // bank and turn (the world box overrides the stick: back toward the middle)
    float bankT = turnIn * bankMax;
    if (p.bounds)
    {
        const bool out = f.pos[0] < p.minX || f.pos[0] > p.maxX || f.pos[2] < p.minZ || f.pos[2] > p.maxZ;
        if (out)
        {
            const float hc = std::atan2(0.5f * (p.minX + p.maxX) - f.pos[0], 0.5f * (p.minZ + p.maxZ) - f.pos[2]);
            const float err = Wrap(hc - s.heading);
            if (std::fabs(err) > 15.0f * kDeg) bankT = err > 0.0f ? bankMax : -bankMax;
            if (!s.boundsLatch && !o.event) o.event = kEvBounds;
            s.boundsLatch = true;
        }
        else s.boundsLatch = false;
    }
    s.bank = Approach(s.bank, bankT, p.bankRate * dt);
    const float yawRate = p.turnDeg * kDeg * (s.bank / bankMax) * (0.35f + 0.65f * a);
    s.heading = Wrap(s.heading + yawRate * dt);

    // the velocity, then the hard limits
    float vh = s.V * std::cos(s.gamma), vy = s.V * std::sin(s.gamma);
    if (y >= p.ceiling && vy > 0.0f)
    {
        vy = 0.0f;
        if (s.gamma > 0.0f) s.gamma = 0.0f;
        s.V = vh;
        if (!s.ceilLatch && !o.event) o.event = kEvCeiling;
        s.ceilLatch = true;
    }
    else if (y < p.ceiling - p.ceilingZone) s.ceilLatch = false;
    const float floorY = SeaFloor(p, s);
    if (y + vy * dt < floorY)
    {
        // this step would end under the skim floor: under it already -> bounce back up; above it ->
        // come down onto it exactly and skim along
        if (y < floorY)
        {
            if (vy < 0.0f) vy = -vy * p.skimBounce;
            const float push = Clamp((floorY - y) * 4.0f, 0.0f, 3.0f);
            if (vy < push) vy = push;
        }
        else vy = (floorY - y) / dt;
        vh *= Clamp(1.0f - p.skimDrag * dt, 0.0f, 1.0f);
        FromCartesian(s, vh, vy);
        if (!s.skimLatch) { ++s.skims; if (!o.event) o.event = kEvSkim; }
        s.skimLatch = true;
    }
    else if (y > floorY + 0.5f) s.skimLatch = false;

    const float sh = std::sin(s.heading), chd = std::cos(s.heading);
    o.vel[0] = vh * sh; o.vel[1] = vy; o.vel[2] = vh * chd;
    for (int k = 0; k < 3; ++k) { if (!Fin(o.vel[k])) o.vel[k] = 0.0f; s.cmd[k] = o.vel[k]; }
    o.write = true;
    o.heading = s.heading;
    const float tilt = Clamp(p.tilt, 0.0f, 1.0f);
    o.pitch = Clamp(s.gamma, -p.tiltPitchDeg * kDeg, p.tiltPitchDeg * kDeg) * tilt;
    o.roll = s.bank * tilt;
    if (s.V > s.topSpeed) s.topSpeed = s.V;
    if (y > s.topY) s.topY = y;
    if (s.mode == kTakeoff && s.airT >= p.takeoffGrace) s.mode = kFly;

    // a fallback for the landing AfterSweep normally finds: standing on something, not climbing
    if (f.grounded && s.airT >= p.takeoffGrace && s.mode != kTakeoff && o.vel[1] <= p.landClimb && !f.swimming)
    {
        s.mode = kLanded;
        if (!o.event) o.event = kEvLand;
    }
    return o;
}

// After the engine's sweep moved it: pre / post = where it was / is, groundAfter = it stands on
// something now. Returns an event (wall, roof, land) or kEvNone.
inline int AfterSweep(const Params& p, State& s, const float pre[3], const float post[3], bool groundAfter, float dt)
{
    if (!Airborne(s) || !(dt > 0.0f)) return kEvNone;
    const float ax = (post[0] - pre[0]) / dt, ay = (post[1] - pre[1]) / dt, az = (post[2] - pre[2]) / dt;
    if (!Fin(ax) || !Fin(ay) || !Fin(az)) return kEvNone;
    int ev = kEvNone;
    const float hc = std::sqrt(s.cmd[0] * s.cmd[0] + s.cmd[2] * s.cmd[2]);
    if (hc > 3.0f)
    {
        const float along = (ax * s.cmd[0] + az * s.cmd[2]) / hc;
        if (along < p.wallKeep * hc)
        {
            const float kept = along > 0.0f ? along : 0.0f;
            const float lost = hc - kept;
            const float vy = s.V * std::sin(s.gamma);
            FromCartesian(s, kept, vy < 0.0f ? vy : 0.0f);
            if (lost >= p.wallLogLoss) { ++s.walls; s.lastWallLoss = lost; ev = kEvWall; }
        }
    }
    if (s.cmd[1] > 1.0f && ay < 0.3f * s.cmd[1])
    {
        if (s.gamma > 0.0f) { FromCartesian(s, s.V * std::cos(s.gamma), 0.0f); }
        if (ev == kEvNone) ev = kEvRoof;
    }
    // descending but not going down (on something the probe missed) for a moment: landed too
    if (s.cmd[1] < -0.5f && ay > -0.1f) s.stuckT += dt; else s.stuckT = 0.0f;
    if (s.mode != kTakeoff && s.airT >= p.takeoffGrace && s.cmd[1] <= p.landClimb && (groundAfter || s.stuckT > 0.3f))
    {
        s.mode = kLanded;
        return kEvLand;
    }
    return ev;
}

// The body's rotation rows for an attitude, in the game's layout (rows X, Y, Z; forward = -Z,
// X = Y x Z): the yaw-only rows match what the game builds itself (0x1404A0020) [P].
inline void Rows(float heading, float pitch, float roll, float x[3], float yv[3], float z[3])
{
    const float sh = std::sin(heading), ch = std::cos(heading), sp = std::sin(pitch), cp = std::cos(pitch);
    const float fwd[3] = { sh * cp, sp, ch * cp };
    const float up0[3] = { -sh * sp, cp, -ch * sp };
    const float rt0[3] = { ch, 0.0f, -sh };
    const float sr = std::sin(roll), cr = std::cos(roll);
    for (int k = 0; k < 3; ++k) { yv[k] = cr * up0[k] + sr * rt0[k]; z[k] = -fwd[k]; }
    x[0] = yv[1] * z[2] - yv[2] * z[1];
    x[1] = yv[2] * z[0] - yv[0] * z[2];
    x[2] = yv[0] * z[1] - yv[1] * z[0];
}

// The game's heading from its rows (atan2(-Z.x, -Z.z), as the turn code 0x14049F033 reads it).
inline float HeadingOf(const float z[3]) { return std::atan2(-z[0], -z[2]); }

// ---- animations (2026-11-15 evening, Notes\46 §4.5) ------------------------------------------
// What should be playing. The creature's own set has two actions: "fly" (the wingbeat loop) and
// "glide" (wings spread); the rider's (Ability_Ride_Pteranodon) has Ride_Pteranodon_Fly (one action,
// three anims: _LEFT, centre, _RIGHT) and Ride_Pteranodon_Idle. In the air the creature beats its
// wings while they work (take-off, a wingbeat's flap_t - holding A keeps them going) and glides
// otherwise; on the ground the game's own walking plays (the .txt's idle/walk/run = glide), so
// nothing is asked for (kAnimNone). The rider: Fly while flying or moving, Idle standing - the
// same choice the game's own rider-anim pick makes from the .txt (rider_jump / rider_run while
// moving faster than 0.1 u/s, else rider_action; LegoCityGameObjects slot 85 = 0x14004BCA0 [P]).
enum Anim : int { kAnimNone = 0, kAnimGlide = 1, kAnimFly = 2, kRideIdle = 3, kRideFly = 4 };

struct AnimPick { int creature, rider; };

inline AnimPick PickAnims(const State& s, bool ridden, float groundSpeed)
{
    AnimPick a = { kAnimNone, kAnimNone };
    const bool air = Airborne(s);
    if (air) a.creature = (s.mode == kTakeoff || s.flapT > 0.0f) ? kAnimFly : kAnimGlide;
    if (ridden) a.rider = (air || (Fin(groundSpeed) && groundSpeed > 0.1f)) ? kRideFly : kRideIdle;
    return a;
}

// Asking the game for an action without fighting it. The game sets a character's action itself
// (its contexts, its rider pick); we ask only when what plays is not what we want, at most every
// 0.25 s, and if the game takes it back three times in a row within a second of each ask, we stop
// asking for that anim for the rest of the ride (the caller logs it once: the game wants something
// else, and that is a finding, not a fight). Per anim kind (1..7), so a flap's on/off does not
// reset the give-up.
struct Nudger
{
    int      want;
    float    sinceAsk;
    bool     asked;
    int      fights;
    unsigned gaveUp;     // bit k: stopped asking for kind k
    unsigned logged;     // bit k: the give-up was reported
};

inline void ResetNudger(Nudger& n) { n = Nudger{}; n.want = kAnimNone; n.sinceAsk = 1.0e9f; }

// cur: the kind that plays now (kAnimNone = something else); want: the kind we want (kAnimNone =
// nothing). Returns true = ask for `want` now.
inline bool NudgeStep(Nudger& n, int cur, int want, float dt)
{
    if (want != n.want) { n.want = want; n.asked = false; n.fights = 0; n.sinceAsk = 1.0e9f; }
    if (Fin(dt) && dt > 0.0f) n.sinceAsk += dt;
    if (want <= kAnimNone || want > 7) return false;
    if (cur == want) { if (n.asked && n.sinceAsk > 1.0f) n.fights = 0; return false; }
    if (n.gaveUp & (1u << want)) return false;
    if (n.asked && n.sinceAsk < 0.25f) return false;
    if (n.asked && n.sinceAsk < 1.0f && ++n.fights >= 3) { n.gaveUp |= 1u << want; return false; }
    if (n.asked && n.sinceAsk >= 1.0f) n.fights = 0;
    n.asked = true; n.sinceAsk = 0.0f;
    return true;
}

} // namespace ptm
