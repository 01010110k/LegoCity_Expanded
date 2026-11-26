// =============================================================================================
//  bike.h - the engine side of the motorbike stunts (the model is bike_model.h, Notes\31).
//
//  ApiVehicleBike::Update (slot 91, 0x1404EAFC0) runs the base vehicle update (FlightAlign is in
//  there), the wheels, the drivetrain, the steering and the lean drive. For the player's bike
//  lcudrive takes the body's velocities BEFORE that update and, after it, replaces only the
//  pitch part of the angular velocity (in the air the yaw part too) with bike_model.h's, plus the
//  small linear correction that makes a wheelie turn about the rear tyre. Roll (the lean), the
//  tyres, the engine and the steering stay the game's own.
// =============================================================================================
#pragma once
#include "bike_model.h"

namespace bk {

static bike::State g_state;
static void*       g_veh = nullptr;
static double      g_t = 0.0;
static bool        g_headerDone = false;

static bike::Params ParamsFrom(const Config& c)
{
    bike::Params p;
    p.wheelieSpeed  = c.bike_wheelie_speed;  p.wheelieDeg  = c.bike_wheelie_angle;
    p.stoppieDeg    = c.bike_stoppie_angle;  p.airPitchAccel = c.bike_air_flip;
    p.airYawAccel   = c.bike_air_spin;       p.gripG = c.bike_grip_g;
    return p;
}

// The skid byte the game's marks / squeal / smoke read (wheel+0x1B0, Notes\36): the game sets it
// from the wheel's spin against the ground, and the spin is still the stock engine's (capped near
// 6.5 u/s) - at lcudrive's speed it would always "skid". Ours: the tyre really sliding sideways.
static void SkidByte(void* wheel, const float lv[3], const float av[3], const float cm[3], const float right[3])
{
    if (At<uint8_t>(wheel, eng::WHL_AIRBORNE) != 0) { At<uint8_t>(wheel, 0x1B0) = 0; return; }
    float P[3], r[3], w[3];
    for (int k = 0; k < 3; ++k) { P[k] = At<float>(wheel, eng::WHL_CONTACT + 4 * k); r[k] = P[k] - cm[k]; }
    w[0] = av[1] * r[2] - av[2] * r[1]; w[1] = av[2] * r[0] - av[0] * r[2]; w[2] = av[0] * r[1] - av[1] * r[0];
    const float vLat = (lv[0] + w[0]) * right[0] + (lv[1] + w[1]) * right[1] + (lv[2] + w[2]) * right[2];
    At<uint8_t>(wheel, 0x1B0) = std::fabs(vLat) > 0.75f ? 1 : 0;      // the game's own bike threshold
}

// At speed the stock steering (tuned for 6.5 u/s) asks for far more turn than any tyre holds: the
// front wheel's angle is held to what `grip` allows at this speed, with 50% slip allowance. The
// wheel joint reads it next frame (the wheels are set up before the steering runs).
static void LimitSteer(void* veh, void* front, void* rear, float vFwd, const Config& cfg)
{
    if (!(cfg.bike_grip_g > 0.0f) || vFwd < 4.0f) return;
    const float L = std::fabs(At<float>(front, eng::WHL_REST + 8) - At<float>(rear, eng::WHL_REST + 8));
    if (!(L > 0.05f)) return;
    const float lim = std::fmax(1.0f, 1.5f * std::atan(L * cfg.bike_grip_g * 9.8f / (vFwd * vFwd)) * 57.2958f);
    const float deg = At<float>(veh, eng::VEH_STEER_DEG);
    if (!IsFin(deg) || std::fabs(deg) <= lim) return;
    const float d2 = deg > 0.0f ? lim : -lim;
    At<float>(veh, eng::VEH_STEER_DEG) = d2;
    At<int32_t>(veh, eng::VEH_STEER_BAM) = static_cast<int32_t>(d2 * (65536.0f / 360.0f));
    At<float>(front, eng::WHL_STEERDEG) = d2;
}

// The lean: the controller's left stick (pulled back = -1), or W / S on the keyboard - the game
// drives with J / K / A / D, so W and S are free while riding.
static float LeanInput(const Config& cfg)
{
    pad::Poll();
    if (!mdl::KeyDownInGame(-1)) return 0.0f;
    float s = pad::g_ly;
    if (std::fabs(s) < 0.25f && cfg.bike_lean_keys)
    {
        // (W / S are the keyboard: player 1's)
        const float k = pad::g_player ? 0.0f : (mdl::KeyDownInGame('W') ? 1.0f : 0.0f) - (mdl::KeyDownInGame('S') ? 1.0f : 0.0f);
        if (k != 0.0f) s = k;
    }
    return s;
}

static void LogNewBike(void* veh, void* front, void* rear)
{
    char name[64];
    void* def = At<void*>(veh, eng::VEH_DEF);
    if (!PtrOk(def) || !ReadModelName(def, name, sizeof(name))) strcpy_s(name, "?");
    void* inst = At<void*>(veh, eng::VEH_INSTANCE);
    void* body = PtrOk(inst) ? At<void*>(inst, eng::INST_BODY) : nullptr;
    Log(">>> motorbike: %s  veh=%p  mass=%.1f  wheels front z=%.2f rear z=%.2f  FlightAlign=%d\n", name, veh,
        PtrOk(body) ? At<float>(body, eng::BODY_MASS) : -1.0f,
        At<float>(front, eng::WHL_REST + 8), At<float>(rear, eng::WHL_REST + 8),
        PtrOk(def) ? (int)At<uint8_t>(def, 0x185) : -1);
}

// ---------------------------------------------------------------------------------------------
//  The engine. The stock bike's drivetrain tops out near 6.5 u/s (2026-10-23 ride; lcudrive's cars
//  do 20). Like the cars' (grip is a solver constraint), the speed has to go in through
//  the tyre: after the game sets up the REAR tyre's joint, its longitudinal row is pinned to our
//  drive force (exact, both limits = F); the lateral row, the front tyre, braking and reversing
//  stay the game's. The stock linear drag (the syrup) is skipped for this bike and replaced by
//  quadratic aero sized so drive = drag exactly at top_speed.
// ---------------------------------------------------------------------------------------------
struct Drive
{
    void* veh;             // the player's bike while its update runs (the drag / wheel hooks look)
    void* rear;
    float F, c, mass;
    bool  on;              // drive force this frame (throttle, rolling forward, rear tyre down)
};
static Drive g_drive = {};

// Player 2 (lcudrive.cpp SwapCtx): the ride state, the bike and the drive plan are per player.
struct Parked { bike::State state; void* veh; Drive drive; };
static Parked g_p2 = {};
static void SwapP2()
{
    Parked t;
    t.state = g_state; t.veh = g_veh; t.drive = g_drive;
    g_state = g_p2.state; g_veh = g_p2.veh; g_drive = g_p2.drive;
    g_p2 = t;
}

static void* RearWheel(void* veh)
{
    void* w0 = At<void*>(veh, eng::VEH_WHEELS);
    void* w1 = PtrOk(w0) ? At<void*>(w0, eng::WHL_NEXT) : nullptr;
    if (!PtrOk(w0) || !PtrOk(w1)) return nullptr;
    return At<float>(w0, eng::WHL_REST + 8) < At<float>(w1, eng::WHL_REST + 8) ? w1 : w0;
}

// Before the engine's update. The game delivers the brake button, while rolling forward, in the
// HANDBRAKE field - on a bike that is its power slide: the rear lets go, and at lcudrive's speeds it
// spins the bike round (2026-10-24: every spin-out in the ride telemetry was braking with the stick
// over). Moved to the service brake in the source block (the input path rewrites the whole block
// every frame), it just brakes.
static void BrakeNotSlide(void* veh, const Config& cfg)
{
    if (cfg.bike_brake_slide) return;
    void* src = At<void*>(veh, eng::VEH_SRCCTRL);
    void* inst = At<void*>(veh, eng::VEH_INSTANCE);
    void* body = PtrOk(inst) ? At<void*>(inst, eng::INST_BODY) : nullptr;
    if (!PtrOk(src) || !PtrOk(body)) return;
    const float hb = At<float>(src, eng::CTRL_HANDBRAKE);
    if (!(hb > 0.05f)) return;
    float v = 0.0f;
    for (int k = 0; k < 3; ++k) v -= At<float>(body, eng::BODY_LINVEL + 4 * k) * At<float>(inst, eng::INST_ROW2 + 4 * k);
    if (!(v > 1.5f)) return;                                           // stopping / reversing: the game's
    At<float>(src, eng::CTRL_BRAKE) = std::fmax(At<float>(src, eng::CTRL_BRAKE), hb);
    At<float>(src, eng::CTRL_HANDBRAKE) = 0.0f;
}

// Before the engine's update.
static void PlanDrive(void* veh, const Config& cfg)
{
    g_drive.on = false;
    g_drive.veh = nullptr;
    if (!cfg.bike_drive) return;
    void* inst = At<void*>(veh, eng::VEH_INSTANCE);
    void* body = PtrOk(inst) ? At<void*>(inst, eng::INST_BODY) : nullptr;
    void* src = At<void*>(veh, eng::VEH_SRCCTRL);
    void* rear = RearWheel(veh);
    if (!PtrOk(body) || !PtrOk(src) || !PtrOk(rear)) return;
    const float m = At<float>(body, eng::BODY_MASS);
    if (!IsFin(m) || m < 10.0f) return;
    const float V = cfg.bike_top_speed;
    const float P = m * 55.0f * cfg.bike_power;                       // 0->15 u/s in ~3 s
    g_drive.veh = veh; g_drive.mass = m; g_drive.rear = rear;
    g_drive.c = P / (V * V * V);                                       // c V^2 = P / V at top speed

    const float thr = At<float>(src, eng::CTRL_THROTTLE);
    const float brk = std::fmax(At<float>(src, eng::CTRL_BRAKE), At<float>(src, eng::CTRL_HANDBRAKE));
    const float rev = At<float>(src, eng::CTRL_REVERSE);
    float v = 0.0f;
    for (int k = 0; k < 3; ++k) v -= At<float>(body, eng::BODY_LINVEL + 4 * k) * At<float>(inst, eng::INST_ROW2 + 4 * k);
    if (!(thr > 0.05f) || brk > 0.05f || rev > 0.05f || v < -0.5f) return;   // braking / reversing: the game's
    if (At<uint8_t>(rear, eng::WHL_AIRBORNE) != 0) return;
    float F = std::fmin(m * 6.0f * cfg.bike_power, P / std::fmax(v, 1.0f)) * Clampf(thr, 0.0f, 1.0f);
    const float N = At<float>(rear, eng::WHL_SPRINGF);                 // traction: what the tyre can carry
    if (IsFin(N) && N > 0.0f) F = std::fmin(F, 1.2f * N);
    g_drive.F = F;
    g_drive.on = F > 0.0f;
}

// In the wheel-joint hook, right after the game's own setup of the rear tyre.
static void DriveRear(void* wheel)
{
    void* joint = At<void*>(wheel, eng::WHL_JOINT);
    void* inst = At<void*>(g_drive.veh, eng::VEH_INSTANCE);
    if (!PtrOk(joint) || !PtrOk(inst)) return;
    uint8_t* d = At<uint8_t*>(joint, eng::JNT_PARAMS);
    if (!PtrOk(d)) return;
    float n[3], axis[4] = {};
    for (int k = 0; k < 3; ++k) { n[k] = At<float>(wheel, eng::WHL_NORMAL + 4 * k); axis[k] = -At<float>(inst, eng::INST_ROW2 + 4 * k); }
    const float an = axis[0] * n[0] + axis[1] * n[1] + axis[2] * n[2];
    for (int k = 0; k < 3; ++k) axis[k] -= an * n[k];
    const float len = std::sqrt(axis[0] * axis[0] + axis[1] * axis[1] + axis[2] * axis[2]);
    if (!(len > 1e-3f)) return;
    for (int k = 0; k < 3; ++k) axis[k] /= len;
    const float latHi = At<float>(d, eng::JP_LAT_HI), latLo = At<float>(d, eng::JP_LAT_LO);
    mdl::g_fn.jointLimits(joint, axis, latHi, g_drive.F, latLo, -g_drive.F);
}

// After the engine's update: the aero drag that replaces the stock one.
static void Aero(void* veh, float dt)
{
    void* inst = At<void*>(veh, eng::VEH_INSTANCE);
    void* body = PtrOk(inst) ? At<void*>(inst, eng::INST_BODY) : nullptr;
    if (!PtrOk(body) || !(dt > 0.0f) || dt > 0.1f) return;
    float v[3], v2 = 0.0f;
    for (int k = 0; k < 3; ++k) { v[k] = At<float>(body, eng::BODY_LINVEL + 4 * k); v2 += v[k] * v[k]; }
    const float sp = std::sqrt(v2);
    if (!(sp > 0.1f)) return;
    const float dvAbs = std::fmin(g_drive.c * v2 / g_drive.mass * dt, 0.5f * sp);
    for (int k = 0; k < 3; ++k) At<float>(body, eng::BODY_LINVEL + 4 * k) = v[k] - v[k] / sp * dvAbs;
}

// After the engine's Update. Returns false when it stood down (the engine's frame stands).
static bool Ride(void* veh, const float lv0[3], const float av0[3], const Config& cfg, bool telemetry)
{
    if (At<int32_t>(veh, eng::VEH_SIMLOD) == 10) return false;         // asleep (sim LOD 10)
    if (!(At<uint8_t>(veh, eng::VEH_FLAGS4) & 1)) return false;        // not spawned yet
    if (At<uint8_t>(veh, eng::VEH_FLAGS5) & 0xA0) return false;        // parked / disabled
    if (PtrOk(At<void*>(veh, eng::VEH_PATHFOLLOW))) return false;      // a scripted ride
    void* src = At<void*>(veh, eng::VEH_SRCCTRL);
    if (!PtrOk(src) || !At<uint8_t>(src, eng::CTRL_ACTIVE)) return false;   // no player control
    void* inst = At<void*>(veh, eng::VEH_INSTANCE);
    void* ctrl = At<void*>(veh, eng::VEH_CTRL);
    if (!PtrOk(inst) || !PtrOk(ctrl)) return false;
    void* body = At<void*>(inst, eng::INST_BODY);
    if (!PtrOk(body)) return false;

    // the two wheels: the front one sits further forward (more negative model z; forward = -z)
    void* w0 = At<void*>(veh, eng::VEH_WHEELS);
    void* w1 = PtrOk(w0) ? At<void*>(w0, eng::WHL_NEXT) : nullptr;
    if (!PtrOk(w0) || !PtrOk(w1)) return false;
    void* front = At<float>(w0, eng::WHL_REST + 8) < At<float>(w1, eng::WHL_REST + 8) ? w0 : w1;
    void* rear = front == w0 ? w1 : w0;
    if (veh != g_veh) { g_veh = veh; bike::Reset(g_state); LogNewBike(veh, front, rear); }

    bike::Input in{};
    in.dt = At<float>(veh, eng::VEH_DT);
    for (int k = 0; k < 3; ++k)
    {
        in.right[k] = At<float>(inst, eng::INST_ROW0 + 4 * k);
        in.up[k]    = At<float>(inst, eng::INST_ROW1 + 4 * k);
        in.back[k]  = At<float>(inst, eng::INST_ROW2 + 4 * k);
        in.av0[k] = av0[k];
        in.av1[k] = At<float>(body, eng::BODY_ANGVEL + 4 * k);
        in.lv1[k] = At<float>(body, eng::BODY_LINVEL + 4 * k);
        in.cm[k]  = At<float>(body, eng::BODY_POS + 4 * k);
        in.frontP[k] = At<float>(front, eng::WHL_CONTACT + 4 * k);
        in.rearP[k]  = At<float>(rear, eng::WHL_CONTACT + 4 * k);
    }
    (void)lv0;
    in.frontDown = At<uint8_t>(front, eng::WHL_AIRBORNE) == 0;
    in.rearDown  = At<uint8_t>(rear, eng::WHL_AIRBORNE) == 0;
    in.vFwd = -(in.lv1[0] * in.back[0] + in.lv1[1] * in.back[1] + in.lv1[2] * in.back[2]);
    in.stick = LeanInput(cfg);
    in.steer = At<float>(ctrl, eng::CTRL_STEER);
    in.throttle = At<float>(ctrl, eng::CTRL_THROTTLE);
    // braking while rolling forward arrives in the brake field or (classic scheme) the handbrake one
    in.brake = std::fmax(At<float>(ctrl, eng::CTRL_BRAKE), At<float>(ctrl, eng::CTRL_HANDBRAKE));

    bike::Output o;
    bike::Update(ParamsFrom(cfg), g_state, in, o);
    if (!o.valid) return false;
    if (o.capLearned)
        Log(">>> motorbike: the collision box touches past %.0f deg of wheelie - wheelies stop there now\n",
            g_state.capRad * 57.2958f);
    if (o.mode != 0 || o.yawHeld)
        for (int k = 0; k < 3; ++k)
        {
            At<float>(body, eng::BODY_ANGVEL + 4 * k) = o.av[k];
            At<float>(body, eng::BODY_LINVEL + 4 * k) = o.lv[k];
        }
    LimitSteer(veh, front, rear, in.vFwd, cfg);
    if (cfg.bike_drive)
    {
        const float* lvW = (o.mode != 0 || o.yawHeld) ? o.lv : in.lv1;
        const float* avW = (o.mode != 0 || o.yawHeld) ? o.av : in.av1;
        SkidByte(front, lvW, avW, in.cm, in.right);
        SkidByte(rear, lvW, avW, in.cm, in.right);
    }

    if (telemetry)
    {
        if (!g_headerDone)
        {
            g_headerDone = true;
            mdl::TelemetryLine("B,t,mode,stick,steer,thr,brk,vfwd,pitch,q,target,fd,rd,px,py,pz,wx,wy,wz,dt,driveF,cap\n");
        }
        g_t += in.dt;
        mdl::TelemetryLine("B,%.4f,%d,%.2f,%.2f,%.2f,%.2f,%.2f,%.1f,%.3f,%.2f,%d,%d,%.2f,%.2f,%.2f,%.3f,%.3f,%.3f,%.4f,%.0f,%.1f\n",
            g_t, o.mode, in.stick, in.steer, in.throttle, in.brake, in.vFwd, o.pitchDeg, o.pitchRate, o.target,
            in.frontDown, in.rearDown, in.cm[0], in.cm[1], in.cm[2], o.av[0], o.av[1], o.av[2], in.dt,
            g_drive.on ? g_drive.F : 0.0f, g_state.capRad * 57.2958f);
    }
    return true;
}

} // namespace bk
