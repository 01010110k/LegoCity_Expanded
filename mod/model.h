// =============================================================================================
//  model.h - the lcudrive vehicle model.
//
//  One rule governs this file: the engine's Dyno solver is the only thing that moves the car.
//  We never add velocity and never fight a constraint. For the player's car, lcudrive.cpp
//  redirects the engine routines that DECIDE what the solver enforces:
//
//      UpdateSteering  0x1404E0E50 -> FrameUpdate() + our steering rack
//      SpinDonut       0x1404E1350 -> skipped (it writes yaw directly and rewrites rear grip)
//      VehicleDrag     0x1404AC080 -> skipped (stock drag is LINEAR, -0.375*v: the syrup)
//      WheelConstraint 0x1404DB720 -> WheelConstraint(): our tyre + drivetrain, written into
//                                     the wheel's ContactJoint (semantics: Notes\20)
//
//  ContactJoint rows as the solver uses them (forces in engine units = mass * u/s^2):
//      lateral : target 0, force in [-latCap, +latCap], softness s -> force ~ 8 * v_slip / s
//      long    : exact force F when driving (lo == hi == F); otherwise target 0 (stop the
//                contact patch) with force in [-cap, +cap] for brakes / rolling resistance
//
//  Per-instance only: shared VehicleDef records are READ, never written. The body fields we
//  change (yaw inertia, angular damping) are saved on entry and restored on exit/disable.
// =============================================================================================
#pragma once

namespace mdl {

static const float kPi  = 3.14159265358979f;
static const float kDeg = kPi / 180.0f;
static const int   kMaxWheels = 8;

static inline float Dot(const float* a, const float* b) { return a[0]*b[0] + a[1]*b[1] + a[2]*b[2]; }
static inline void  Cross(const float* a, const float* b, float* o)
{
    o[0] = a[1]*b[2] - a[2]*b[1]; o[1] = a[2]*b[0] - a[0]*b[2]; o[2] = a[0]*b[1] - a[1]*b[0];
}
static inline float Len(const float* a) { return std::sqrt(Dot(a, a)); }
static inline bool  Norm(float* a)
{
    const float l = Len(a);
    if (!(l > 1e-6f)) return false;
    a[0] /= l; a[1] /= l; a[2] /= l;
    return true;
}
static inline float Lerp(float a, float b, float t) { return a + (b - a) * t; }
static inline float Smooth01(float x) { x = Clampf(x, 0.0f, 1.0f); return x * x * (3.0f - 2.0f * x); }
static inline float Sgn(float x) { return x < 0.0f ? -1.0f : 1.0f; }
static inline float MoveToward(float cur, float tgt, float step)
{
    const float d = tgt - cur;
    return d > step ? cur + step : (d < -step ? cur - step : tgt);
}

// ---------------------------------------------------------------------------------------------
//  Engine entry points
// ---------------------------------------------------------------------------------------------
typedef void(__fastcall* FnVeh)(void* veh);
typedef void(__fastcall* FnWheel)(void* wheel);
typedef void(__fastcall* FnSetSteer)(void* wheel, float deg);
typedef void(__fastcall* FnJointLimits)(void* c, const float* axis, float latHi, float longHi,
                                         float latLo, float longLo);
typedef void(__fastcall* FnJointPair)(void* c, float a, float b);
typedef void(__fastcall* FnApplyForce)(void* body, const float* f, const float* p);
typedef void(__fastcall* FnPointVel)(void* body, const float* p, float* out);

struct Fns
{
    FnVeh         steerOrig, spinOrig, dragOrig;
    FnWheel       constraintOrig;
    FnSetSteer    wheelSetSteer;
    FnJointLimits jointLimits;
    FnJointPair   jointSoft, jointTarget;
    FnApplyForce  applyForce;
    FnPointVel    pointVel;
};
static Fns g_fn = {};

// ---------------------------------------------------------------------------------------------
//  Snapshot: what we read from the engine each frame, copied to POD.
// ---------------------------------------------------------------------------------------------
struct WheelSnap
{
    void* w; void* wdef; void* joint;
    bool  ground, drive, brake, handbrake;
    float steerable, tyreGrip, radius;
    float rest[3];       // model-space mount (x right, y up, z back)
    float springF;
};

struct CarSnap
{
    void* veh; void* inst; void* body; void* def; void* ctrl; void* src; void* drivetrain;
    float row0[3], row1[3], row2[3], pos[3];
    float com[3], linvel[3], angvel[3];
    float mass, gravity, dt;
    float inSteer, inThrottle, inReverse, inBrake, inHandbrake;
    bool  srcActive, directSteer, pathFollow, idle;
    int   nw;
    WheelSnap wh[kMaxWheels];
    float fwd[3], right[3], up[3];      // unit
    float vFwd, vLat, speed, yawRate;
    float defMaxVel, defPower, defRpm, defMaxAngle;
};

static bool Snapshot(void* veh, CarSnap& s)
{
    std::memset(&s, 0, sizeof(s));
    __try
    {
        s.veh  = veh;
        s.inst = At<void*>(veh, eng::VEH_INSTANCE);
        s.def  = At<void*>(veh, eng::VEH_DEF);
        s.ctrl = At<void*>(veh, eng::VEH_CTRL);
        s.src  = At<void*>(veh, eng::VEH_SRCCTRL);
        s.drivetrain = At<void*>(veh, eng::VEH_DRIVETRAIN);
        if (!PtrOk(s.inst) || !PtrOk(s.def)) return false;
        s.body = At<void*>(s.inst, eng::INST_BODY);
        if (!PtrOk(s.body)) return false;
        for (int i = 0; i < 3; ++i)
        {
            s.row0[i]   = At<float>(s.inst, eng::INST_ROW0 + 4 * i);
            s.row1[i]   = At<float>(s.inst, eng::INST_ROW1 + 4 * i);
            s.row2[i]   = At<float>(s.inst, eng::INST_ROW2 + 4 * i);
            s.pos[i]    = At<float>(s.inst, eng::INST_POS  + 4 * i);
            s.com[i]    = At<float>(s.body, eng::BODY_POS    + 4 * i);
            s.linvel[i] = At<float>(s.body, eng::BODY_LINVEL + 4 * i);
            s.angvel[i] = At<float>(s.body, eng::BODY_ANGVEL + 4 * i);
        }
        s.mass    = At<float>(s.body, eng::BODY_MASS);
        s.gravity = At<float>(veh, eng::VEH_GRAVITY);
        s.dt      = At<float>(veh, eng::VEH_DT);
        s.idle    = ((At<uint8_t>(veh, eng::VEH_FLAGS7) >> 1) & 1) != 0;
        s.pathFollow  = PtrOk(At<void*>(veh, eng::VEH_PATHFOLLOW));
        s.defMaxVel   = At<float>(s.def, eng::DEF_MAXVEL);
        s.defPower    = At<float>(s.def, eng::DEF_POWERMAX);
        s.defRpm      = At<float>(s.def, eng::DEF_RPMLIMIT);
        s.defMaxAngle = At<float>(s.def, eng::DEF_MAXANGLE);

        if (PtrOk(s.ctrl))
        {
            s.inSteer     = At<float>(s.ctrl, eng::CTRL_STEER);
            s.inThrottle  = At<float>(s.ctrl, eng::CTRL_THROTTLE);
            s.inReverse   = At<float>(s.ctrl, eng::CTRL_REVERSE);
            s.inBrake     = At<float>(s.ctrl, eng::CTRL_BRAKE);
            s.inHandbrake = At<float>(s.ctrl, eng::CTRL_HANDBRAKE);
            s.directSteer = At<uint8_t>(s.ctrl, eng::CTRL_DIRECT) != 0;
        }
        s.srcActive = PtrOk(s.src) && At<uint8_t>(s.src, eng::CTRL_ACTIVE) != 0;

        void* w = At<void*>(veh, eng::VEH_WHEELS);
        int n = 0;
        while (PtrOk(w) && n < kMaxWheels)
        {
            WheelSnap& ws = s.wh[n];
            ws.w      = w;
            ws.wdef   = At<void*>(w, eng::WHL_DEF);
            ws.joint  = At<void*>(w, eng::WHL_JOINT);
            ws.ground = At<uint8_t>(w, eng::WHL_AIRBORNE) == 0;
            for (int i = 0; i < 3; ++i) ws.rest[i] = At<float>(w, eng::WHL_REST + 4 * i);
            ws.springF = At<float>(w, eng::WHL_SPRINGF);
            ws.radius  = At<float>(w, eng::WHL_RADIUS);
            if (PtrOk(ws.wdef))
            {
                ws.drive     = At<uint8_t>(ws.wdef, eng::WDEF_DRIVE) != 0;
                ws.handbrake = At<uint8_t>(ws.wdef, eng::WDEF_HANDBRAKE) != 0;
                ws.brake     = At<uint8_t>(ws.wdef, eng::WDEF_BRAKE) != 0;
                ws.steerable = At<float>(ws.wdef, eng::WDEF_STEERABLE);
                ws.tyreGrip  = At<float>(ws.wdef, eng::WDEF_TYREGRIP);
            }
            ++n;
            w = At<void*>(w, eng::WHL_NEXT);
        }
        s.nw = n;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }

    for (int i = 0; i < 3; ++i) { s.fwd[i] = -s.row2[i]; s.right[i] = s.row0[i]; s.up[i] = s.row1[i]; }
    if (!Norm(s.fwd) || !Norm(s.right) || !Norm(s.up)) return false;
    s.vFwd    = Dot(s.linvel, s.fwd);
    s.vLat    = Dot(s.linvel, s.right);
    s.speed   = Len(s.linvel);
    s.yawRate = Dot(s.angvel, s.up);
    return IsFin(s.vFwd) && IsFin(s.mass) && s.mass > 1.0f && s.nw > 0;
}

// ---------------------------------------------------------------------------------------------
//  Per-car parameters, derived once when the player gets in. The stock def supplies the car's
//  CHARACTER (relative top speed, power, steering lock, tyre grip); the numbers we drive with
//  are ours.
// ---------------------------------------------------------------------------------------------
struct CarParams
{
    float mass;
    int   nw;
    bool  front[kMaxWheels];
    float gripBase[kMaxWheels];   // per-wheel friction character from the def's TyreGrip
    float wheelbase, track;
    int   nDriven, nFront, nRear;
    float wheelR;
    float vTop, pMax, wRed, wIdle, wLaunch;
    int   nGears;
    float ratio[8];               // overall (gear * final), index 1..nGears
    float revRatio;
    float dragK;
    float lockDeg;
    float aLat;                   // design peak lateral acceleration, u/s^2
    float heft;                   // 1 = an ordinary car; >1 = long and/or heavy (limo, bus, truck)
};

struct CarState
{
    void*     veh;
    void*     body;
    CarParams p;
    bool      ok;
    // controls
    float     rackDeg, lockNow;
    int       gear;               // 1..n forward, -1 reverse
    float     engW;               // engine speed, rad/s
    float     shiftT;             // > 0 during a shift (torque cut)
    float     thr;                // shaped throttle 0..1
    bool      reversing;
    bool      accelFromBrake;     // the brake field is the accelerator (pressed while reversing)
    bool      driftOn;            // a deliberate slide on the throttle (rear stays loose)
    float     driftBeta;          // |body slip angle|, degrees
    float     driftTarget;        // drift angle the steering asks for, degrees
    bool      pedalHb;            // the handbrake is the two pedals: a power slide (the rear stays driven)
    bool      brakeDrift;         // the brake with the steering wound on at speed: a drift entry
    float     driftPow;           // engine power x while drifting ([feel] power below 1 comes back)
    // per frame, consumed by the wheel hook
    float     driveF;             // total drive force along forward (signed)
    float     brakeF;             // total service-brake capacity
    bool      handbrake;
    float     engBrakeF;          // engine-braking capacity on the driven wheels (total)
    int       drivenOnGround;
    // per wheel
    float     load[kMaxWheels];
    float     loadAvg, frontAvg, rearAvg;
    bool      slide[kMaxWheels];
    uint8_t   skid[kMaxWheels];       // this frame's honest "tyre is skidding" (marks, squeal, smoke)
    float     tLatCap[kMaxWheels], tLongF[kMaxWheels], tN[kMaxWheels], tVLat[kMaxWheels], tVisc[kMaxWheels];
    // the body fields we change, and must give back
    float     angDampOrig, invIyOrig;
    float     angDampW, invIyW;   // what we last wrote: a give-back checks they are still there
    bool      bodySaved;
    // effective gravity, measured from the springs (should read ~9.8)
    float     gEff;
    // transient yaw damper
    float     yawSlow;
    float     hbHold;             // seconds since the handbrake was last held (damper stays off)
};

// internal shape constants (kept out of the ini on purpose; the harness sweeps them)
struct Tune
{
    float slipAllow = 0.08f;   // steering limiter: tyre slip allowed on top of the kinematic angle
    float dropF = 0.06f, dropR = 0.08f;   // grip lost once a tyre slides
    float aLatBase = 10.0f;     // design lateral acceleration at grip = 1
    float yawDampHi = 1.2f;     // extra solver angular damping at top speed (yaw mode loses damping with speed)
    float rateHi = 55.0f;       // steering rack, deg/s at speed
    float alphaF = 0.13f, alphaR = 0.05f;   // slip angle (rad) at peak lateral force
    float yawDamper = 6.0f;     // transient yaw damper gain at top speed, stability = 1
    float rearBrakeUse = 0.55f; // share of rear grip the brakes may use
};
static Tune g_tune;

static CarState g_car = {};
static CarSnap  g_snap;
static bool     g_snapOk = false;
static char     g_modelName[64] = {};

// Player 2's car (2026-11-11: player 2's car gets the same model, and so the same speed). The model is written
// around ONE car: g_car / g_snap / g_snapOk / g_modelName. Rather than index every use, player 2's copies
// are parked here and swapped in for the length of that car's own Update (lcudrive.cpp CarThunk), then
// swapped back out. Player 1's path never swaps, so it runs exactly as before.
struct Parked { CarState car; CarSnap snap; bool snapOk; char name[64]; };
static Parked g_p2 = {};
static bool   g_p2In = false;        // the globals hold player 2's car right now (game thread only)
static void SwapP2()
{
    Parked t;
    t.car = g_car; t.snap = g_snap; t.snapOk = g_snapOk; std::memcpy(t.name, g_modelName, sizeof(t.name));
    g_car = g_p2.car; g_snap = g_p2.snap; g_snapOk = g_p2.snapOk; std::memcpy(g_modelName, g_p2.name, sizeof(g_modelName));
    g_p2 = t;
    g_p2In = !g_p2In;
}

// Power curve, 1.0 at x = 0.85 of redline; strong low end, gentle past the peak.
static float PowerShape(float x)
{
    x = Clampf(x, 0.0f, 1.1f);
    if (x <= 0.85f) { const float u = (x - 0.85f) / 0.85f; return 1.0f - u * u; }
    const float u = x - 0.85f;
    return 1.0f - 2.5f * u * u;
}

static void DeriveParams(const CarSnap& s, const Config& cfg, CarParams& p)
{
    std::memset(&p, 0, sizeof(p));
    p.mass = s.mass;
    p.nw = s.nw;

    // axle classification and geometry from the wheel mounts (forward = -z)
    float zf = 0, zr = 0, xmin = 1e9f, xmax = -1e9f, rsum = 0;
    for (int i = 0; i < s.nw; ++i)
    {
        const WheelSnap& w = s.wh[i];
        p.front[i] = w.rest[2] < 0.0f;
        if (p.front[i]) { zf += w.rest[2]; ++p.nFront; } else { zr += w.rest[2]; ++p.nRear; }
        if (w.rest[0] < xmin) xmin = w.rest[0];
        if (w.rest[0] > xmax) xmax = w.rest[0];
        p.gripBase[i] = 0.92f + 0.32f * Clampf(IsFin(w.tyreGrip) ? w.tyreGrip : 0.5f, 0.0f, 1.0f);
        if (w.drive) { ++p.nDriven; rsum += w.radius; }
    }
    p.wheelbase = (p.nFront && p.nRear) ? (zr / p.nRear - zf / p.nFront) : 0.8f;
    p.wheelbase = Clampf(p.wheelbase, 0.3f, 4.0f);
    p.track     = Clampf(xmax - xmin, 0.1f, 3.0f);
    p.wheelR    = p.nDriven ? Clampf(rsum / p.nDriven, 0.02f, 1.0f) : 0.08f;
    if (p.nDriven == 0) p.nDriven = 1;

    // performance: keep the stock car's relative character, raise the ceiling
    const float maxVel = (IsFin(s.defMaxVel) && s.defMaxVel > 0.5f) ? s.defMaxVel : 9.0f;
    const float power  = (IsFin(s.defPower) && s.defPower > 1.0f) ? s.defPower : 100.0f;
    // 1.65x the stock top speed, but at least +3 u/s, so slow work vehicles stay slow-ish
    p.vTop = cfg.top_speed * Clampf(std::fmax(1.65f * maxVel, maxVel + 3.0f), 4.0f, 30.0f);
    const float spec = cfg.power * Clampf(16.0f + 28.0f * std::sqrt(power / p.mass / 0.6f), 12.0f, 70.0f);
    p.pMax = spec * p.mass;                                   // mass * u^2/s^3

    const float rpm = (IsFin(s.defRpm) && s.defRpm > 1000.0f) ? s.defRpm : 6500.0f;
    p.wRed    = rpm * (2.0f * kPi / 60.0f);
    p.wIdle   = 0.14f * p.wRed;
    p.wLaunch = 0.55f * p.wRed;

    // gearbox: top gear puts v_top at 0.9 of redline (just past the power peak)
    p.nGears = (p.vTop > 20.0f) ? 6 : 5;
    const float gTop   = 0.90f * p.wRed * p.wheelR / p.vTop;
    const float spread = (p.nGears == 6) ? 3.6f : 3.2f;
    const float g1     = gTop * spread;
    for (int k = 1; k <= p.nGears; ++k)
        p.ratio[k] = g1 * std::pow(gTop / g1, (float)(k - 1) / (float)(p.nGears - 1));
    p.revRatio = g1 * 1.1f;

    // heft: how big and heavy this vehicle is next to an ordinary LEGO City car (wheelbase ~0.70,
    // ~420 mass). The engine's own yaw inertia barely grows with length (the 1.53-wheelbase limo
    // has 1.7x a sedan's, where m*L^2 says ~5x) and the stock steering lock is just as large, so a
    // limo turned like a sports car. Floored at 1: ordinary and small cars are untouched.
    {
        const float lenF  = std::pow(Clampf(p.wheelbase / 0.70f, 1.0f, 4.0f), 0.8f);
        const float massF = std::sqrt(Clampf(p.mass / 420.0f, 1.0f, 6.0f));
        p.heft = 1.0f + (lenF * massF - 1.0f) * Clampf(cfg.size_effect, 0.0f, 2.0f);
    }

    p.aLat = cfg.grip * g_tune.aLatBase * std::pow(p.heft, -0.2f);   // u/s^2 at the limit

    // aero: the quadratic drag that makes v_top the equilibrium at peak power
    const float rollAll = 0.012f * p.mass * 9.8f;
    p.dragK = Clampf((p.pMax * PowerShape(0.90f) / p.vTop - rollAll) / (p.vTop * p.vTop), 0.0f, 1e6f);

    const float stockLock = (IsFin(s.defMaxAngle) && s.defMaxAngle > 5.0f) ? s.defMaxAngle : 40.0f;
    // a long vehicle's turning circle grows with its length: tan(lock) shrinks with heft
    const float lockHeft = std::atan(std::tan(Clampf(stockLock, 5.0f, 70.0f) * kDeg) / p.heft) / kDeg;
    p.lockDeg = Clampf(lockHeft * cfg.steer_lock, 15.0f, 55.0f);
}

static void LogParams(const CarParams& p)
{
    Log("    lcudrive: m=%.0f wb=%.3f track=%.3f r=%.3f driven=%d front=%d rear=%d | vTop=%.1f "
        "P/m=%.1f wRed=%.0f gears=%d (1st %.1f top %.1f) dragK=%.4f lock=%.1f aLat=%.1f heft=%.2f\n",
        p.mass, p.wheelbase, p.track, p.wheelR, p.nDriven, p.nFront, p.nRear, p.vTop,
        p.pMax / p.mass, p.wRed, p.nGears, p.ratio[1], p.ratio[p.nGears], p.dragK, p.lockDeg, p.aLat, p.heft);
}

static float DriveForce(const CarParams& p, int g, float w, float throttle)
{
    if (g == 0 || throttle <= 0.0f) return 0.0f;
    const float G = (g > 0) ? p.ratio[g] : p.revRatio;
    const float x = w / p.wRed;
    float P = p.pMax * PowerShape(x) * throttle;
    if (x > 1.0f) P *= Clampf((1.05f - x) / 0.05f, 0.0f, 1.0f);   // limiter
    const float T = P / (w > 1.0f ? w : 1.0f);
    return T * G / p.wheelR;
}

// ---------------------------------------------------------------------------------------------
//  The two body fields we change. Saved on the first frame we drive a car; given back whenever we
//  stop driving it (`[`, a different car, unhook, fault) - but only ever INSIDE that car's own
//  Update, which proves it is alive, and only while the body still holds exactly what we last
//  wrote. A car can be destroyed (crashed to bricks, despawned) without telling us, and its memory
//  handed to the next vehicle; writing the old values then would be a use-after-free. A car that
//  never updates again within 10 s is gone, and so is its body: nothing to give back.
// ---------------------------------------------------------------------------------------------
struct BodyRec { void* veh; void* body; float adOrig, iyOrig, adW, iyW; uint32_t t; };
static BodyRec g_retired[4];

static void RetireBody()
{
    if (!g_car.bodySaved) return;
    g_car.bodySaved = false;
    BodyRec* slot = nullptr;
    for (auto& r : g_retired) if (r.veh == g_car.veh) { slot = &r; break; }
    if (!slot) for (auto& r : g_retired) if (!r.veh) { slot = &r; break; }
    if (!slot) { slot = &g_retired[0]; for (auto& r : g_retired) if (r.t < slot->t) slot = &r; }
    *slot = { g_car.veh, g_car.body, g_car.angDampOrig, g_car.invIyOrig, g_car.angDampW, g_car.invIyW, GetTickCount() };
}

// At the start of every vehicle Update: give a retired car its fields back, if it is this one.
static void RestoreRetired(void* veh, uint32_t now)
{
    for (auto& r : g_retired)
    {
        if (!r.veh) continue;
        if (now - r.t > 10000) { r.veh = nullptr; continue; }
        if (r.veh != veh) continue;
        __try
        {
            void* inst = At<void*>(veh, eng::VEH_INSTANCE);
            void* body = PtrOk(inst) ? At<void*>(inst, eng::INST_BODY) : nullptr;
            if (body == r.body)
            {
                float& ad = At<float>(body, eng::BODY_ANGDAMP);
                float& iy = At<float>(body, eng::BODY_INVINERTIA_Y);
                if (ad == r.adW) ad = r.adOrig;
                if (iy == r.iyW) iy = r.iyOrig;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
        r.veh = nullptr;
    }
}

static void TuneBody(const CarSnap& s, const Config& cfg)
{
    __try
    {
        float& ad = At<float>(s.body, eng::BODY_ANGDAMP);
        float& iy = At<float>(s.body, eng::BODY_INVINERTIA_Y);
        const float inertiaK = cfg.yaw_inertia * (g_car.p.heft > 1.0f ? g_car.p.heft : 1.0f);
        if (!g_car.bodySaved)
        {
            g_car.angDampOrig = ad; g_car.invIyOrig = iy; g_car.body = s.body; g_car.bodySaved = true;
            Log("    body: angular damping %.3f -> %.3f, yaw inverse inertia %.5f -> %.5f (heft %.2f)\n",
                ad, cfg.yaw_damping, iy, iy / inertiaK, g_car.p.heft);
        }
        const float vr = Clampf(s.speed / (g_car.p.vTop > 1.0f ? g_car.p.vTop : 18.0f), 0.0f, 1.2f);
        ad = cfg.yaw_damping + g_tune.yawDampHi * vr * vr;
        if (IsFin(g_car.invIyOrig) && g_car.invIyOrig > 0.0f) iy = g_car.invIyOrig / inertiaK;
        g_car.angDampW = ad; g_car.invIyW = iy;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
}

// vk < 0 asks only "does the game have focus" (used to gate the controller the same way)
static bool KeyDownInGame(int vk)
{
    if (vk == 0 || vk > 0xFE) return false;
    HWND fg = GetForegroundWindow();
    DWORD pid = 0;
    if (fg) GetWindowThreadProcessId(fg, &pid);
    if (pid != GetCurrentProcessId()) return false;
    return vk < 0 || (GetAsyncKeyState(vk) & 0x8000) != 0;
}

// ---------------------------------------------------------------------------------------------
//  Once per frame, from the steering hook (the engine has copied this frame's inputs and run the
//  stock drivetrain). Returns the steering angle to publish, degrees, + = left.
// ---------------------------------------------------------------------------------------------
static float FrameUpdate(void* veh, const Config& cfg)
{
    CarSnap& s = g_snap;
    g_snapOk = Snapshot(veh, s);
    if (!g_snapOk) return 0.0f;
    CarState& c = g_car;

    if (c.veh != veh || !c.ok)
    {
        RetireBody();
        std::memset(&c, 0, sizeof(c));
        c.veh = veh; c.body = s.body;
        DeriveParams(s, cfg, c.p);
        c.ok = true;
        c.gear = 1;
        c.driftPow = 1.0f;
        c.engW = c.p.wIdle;
        c.gEff = 9.8f;
        for (int i = 0; i < s.nw; ++i) c.load[i] = s.mass * 9.8f / s.nw;
        Log(">>> lcudrive now driving %s\n", g_modelName);
        LogParams(c.p);
    }
    const CarParams& p = c.p;
    const float dt = Clampf(IsFin(s.dt) ? s.dt : 0.0166f, 0.001f, 0.05f);

    // ---- loads ----
    {
        float sum = 0, la = 0; int ng = 0;
        c.drivenOnGround = 0;
        for (int i = 0; i < s.nw; ++i)
        {
            if (!s.wh[i].ground) continue;
            sum += s.wh[i].springF > 0 ? s.wh[i].springF : 0;
            la  += c.load[i];
            ++ng;
            if (s.wh[i].drive) ++c.drivenOnGround;
        }
        if (ng == s.nw && s.speed < 40.0f)
            c.gEff = Clampf(Lerp(c.gEff, sum / s.mass, 0.02f), 2.0f, 30.0f);
        c.loadAvg = ng ? la / ng : s.mass * 9.8f / s.nw;
        float fs = 0, rs = 0; int nf = 0, nr = 0;
        for (int i = 0; i < s.nw; ++i)
        {
            if (!s.wh[i].ground) continue;
            if (p.front[i]) { fs += c.load[i]; ++nf; } else { rs += c.load[i]; ++nr; }
        }
        c.frontAvg = nf ? fs / nf : c.loadAvg;
        c.rearAvg  = nr ? rs / nr : c.loadAvg;
    }

    // ---- pedals: the forward key / back key mean drive or brake by direction of travel ----
    // The game puts the pedal that OPPOSES the motion into the brake field: the brake key while
    // rolling forward, but the ACCELERATOR while rolling backwards - until its own drivetrain sees
    // the car stop. Read as "back key" that reverses forever (telemetry 2026-10-23: -6 u/s held,
    // brake field = 1, accelerator down). So a brake field that lights up while we are reversing
    // is latched as the accelerator until it is released: it brakes to a stop, then pulls away.
    float fwdKey = Clampf(s.inThrottle, 0.0f, 1.0f);
    float backKey = Clampf(s.inReverse, 0.0f, 1.0f);
    const float brkField = s.idle ? 0.0f : Clampf(s.inBrake, 0.0f, 1.0f);       // idle: park brake
    // (only while actually rolling backwards: braking INTO reverse, the brake key is still in that
    // field for a moment after we start reversing, and it must not read as the accelerator)
    if (brkField <= 0.05f) c.accelFromBrake = false;
    else if (c.reversing && s.inReverse <= 0.05f && s.vFwd < -0.3f) c.accelFromBrake = true;
    if (c.accelFromBrake) fwdKey = std::fmax(fwdKey, brkField);
    else if (brkField > backKey) backKey = brkField;
    if (cfg.brake_key_brakes && s.inHandbrake > backKey) backKey = Clampf(s.inHandbrake, 0.0f, 1.0f);
    pad::Poll();
    // accelerator + brake together while rolling forward = the handbrake: a drift entry that needs
    // no extra button (RB is the game's own car jump; RT+LT on a pad, J+K on the keyboard)
    const float brakeHeld = std::fmax(brkField, cfg.brake_key_brakes ? Clampf(s.inHandbrake, 0.0f, 1.0f) : 0.0f);
    const bool pedalHb = cfg.pedal_handbrake && !c.reversing && !c.accelFromBrake &&
                         fwdKey > 0.3f && brakeHeld > 0.3f && s.vFwd > 2.0f;
    // the brake with the steering wound on at speed: a drift entry - the rear lets go, the way the
    // game's own power slide does - instead of a straight-line stop; braking with the wheel near
    // straight still brakes. Held until the brake comes up. (2026-10-24, real brakes and power 0.75:
    // cars were almost impossible to drift, they just came to a stop)
    // Only the brake that arrives in the handbrake field (the game's power-slide button); a real
    // service brake (+0x14: the park brake, the other control scheme) always brakes.
    {
        const float steerAbs = std::fabs(IsFin(s.inSteer) ? s.inSteer : 0.0f);
        const float slideKey = cfg.brake_key_brakes ? Clampf(s.inHandbrake, 0.0f, 1.0f) : 0.0f;
        const bool can = cfg.brake_drift > 0.0f && !c.reversing && !pedalHb && slideKey > 0.3f && fwdKey <= 0.3f;
        c.brakeDrift = can && (c.brakeDrift ? s.vFwd > 3.0f : (s.vFwd > 6.0f && steerAbs > cfg.brake_drift));
    }
    // (the handbrake KEY is the keyboard, player 1's; the pad button is read from the current player's own pad)
    c.handbrake = (!g_p2In && KeyDownInGame(cfg.handbrake_key)) ||
                  (cfg.pad_handbrake && pad::Down(static_cast<uint32_t>(cfg.pad_handbrake)) && KeyDownInGame(-1)) ||
                  (!cfg.brake_key_brakes && s.inHandbrake > 0.05f) || pedalHb || c.brakeDrift;
    c.pedalHb = pedalHb;

    float throttle = 0.0f, brake = s.idle ? 1.0f : 0.0f;
    const float v = s.vFwd;
    if (c.reversing)
    {
        if (fwdKey > 0.05f) { if (v < -0.6f) brake = fwdKey; else c.reversing = false; }
        else if (backKey > 0.05f) throttle = backKey;
        if (v > 1.5f) c.reversing = false;
    }
    else if (backKey > 0.05f)
    {
        if (v > 0.5f) brake = backKey;
        else { c.reversing = true; throttle = backKey; }
    }
    if (!c.reversing && fwdKey > 0.05f) throttle = fwdKey;
    if (pedalHb || c.brakeDrift) brake = 0.0f;        // the two pedals / a brake-drift mean handbrake, not brake
    c.thr = MoveToward(c.thr, throttle, dt * 7.0f);

    // a deliberate slide: real body slip angle, moving, on the throttle. The rear then stays loose
    // under power (WheelConstraint) and the yaw damper stands aside, so a drift can be held
    {
        const float beta = std::atan2(std::fabs(s.vLat), std::fmax(std::fabs(s.vFwd), 0.5f)) / kDeg;
        c.driftBeta = beta;
        c.driftOn = cfg.drift > 0.0f && !c.reversing && s.speed > 3.0f && beta > 8.0f && c.thr > 0.2f;
        // in a drift, or a power slide on the two pedals, the engine has its full power back:
        // [feel] power below 1 is for straight-line pace; without this a slide bled speed until
        // the car stopped (2026-10-24, seen at power 0.75)
        const float full = 1.0f / Clampf(cfg.power, 0.5f, 1.0f);
        c.driftPow = MoveToward(c.driftPow, (c.driftOn || c.pedalHb) ? full : 1.0f, dt * 2.0f);
    }

    // ---- steering rack ----
    {
        const float sp = s.speed;
        // lock at which the front axle reaches its grip limit, plus a slip allowance
        const float v2 = sp * sp > 0.25f ? sp * sp : 0.25f;
        const float satDeg = (std::atan(p.wheelbase * p.aLat * 1.0f / v2) + g_tune.slipAllow) / kDeg;
        float lock = std::fmin(p.lockDeg, satDeg * cfg.steer_lock);
        // analog stick: deadzone + response curve (a keyboard's +-1 passes through unchanged)
        float in = Clampf(IsFin(s.inSteer) ? s.inSteer : 0.0f, -1.0f, 1.0f);
        {
            const float a = std::fabs(in), dz = cfg.stick_deadzone;
            in = (a <= dz) ? 0.0f : Sgn(in) * std::pow((a - dz) / (1.0f - dz), cfg.steer_curve);
        }
        // sliding: hand lock back for COUNTER-steer only (input opposing the slide), so a slide
        // can be caught - steering further into a spin gets nothing extra
        const float beta = std::atan2(s.vLat, std::fabs(s.vFwd) + 0.5f) / kDeg;   // + = tail out, left turn
        if (in * beta < 0.0f && std::fabs(beta) > 4.0f)
            lock = std::fmin(p.lockDeg, lock + (std::fabs(beta) - 4.0f) * 1.3f);
        lock = Clampf(lock, 3.0f, p.lockDeg);
        c.lockNow = lock;
        const float target = in * lock;
        // drift angle the driver is asking for: neutral stick holds ~22 deg, steering into the turn
        // asks for up to 35, countersteer down to 9 (and out). WheelConstraint steers the rear's grip
        // toward it; this is what makes a slide something you drive rather than something you catch.
        c.driftTarget = 22.0f + 13.0f * Clampf(in * Sgn(beta), -1.0f, 1.0f);
        // fast at parking speeds, progressively calmer with speed (a step input at speed is the
        // one thing that makes a light car dart)
        float rate = cfg.steer_speed * Lerp(320.0f, g_tune.rateHi, Smooth01(sp / (0.7f * p.vTop)))
                   / std::sqrt(p.heft > 1.0f ? p.heft : 1.0f);              // big vehicles: slower wheel
        if (std::fabs(target) < std::fabs(c.rackDeg) || target * c.rackDeg < 0.0f) rate *= 1.5f;
        // in a slide, countersteer is a reflex: the wheel gets there at once
        if (std::fabs(beta) > 8.0f && target * beta < 0.0f) rate = std::fmax(rate, 300.0f);
        c.rackDeg = MoveToward(c.rackDeg, target, rate * dt);
        c.rackDeg = Clampf(c.rackDeg, -p.lockDeg, p.lockDeg);
    }

    // ---- engine + gearbox ----
    {
        const float wWheel = std::fabs(v) / p.wheelR;
        if (c.shiftT > 0.0f) c.shiftT -= dt;
        if (c.reversing) c.gear = -1;
        else if (c.gear < 1) c.gear = 1;
        if (c.gear >= 1 && c.shiftT <= 0.0f && c.drivenOnGround > 0)
        {
            const float x = p.ratio[c.gear] * wWheel / p.wRed;
            if (x > 0.97f && c.gear < p.nGears) { ++c.gear; c.shiftT = 0.15f; }
            else if (c.gear > 1)
            {
                const float xDown = p.ratio[c.gear - 1] * wWheel / p.wRed;
                if (x < 0.42f || (c.thr > 0.9f && xDown < 0.80f && x < 0.6f)) { --c.gear; c.shiftT = 0.10f; }
            }
        }
        const float G = (c.gear > 0) ? p.ratio[c.gear] : p.revRatio;
        float w = G * wWheel;
        if (c.thr > 0.02f && (c.gear == 1 || c.gear == -1) && w < p.wLaunch) w = p.wLaunch;  // clutch slip
        if (w < p.wIdle) w = p.wIdle;
        c.engW = Lerp(c.engW, w, 0.35f);
        const float F = (c.shiftT > 0.0f) ? 0.0f : DriveForce(p, c.gear, w, c.thr) * (c.gear > 0 ? c.driftPow : 1.0f);
        c.driveF = (c.gear < 0) ? -F : F;
        c.engBrakeF = (c.thr < 0.02f && !c.reversing) ? 0.045f * p.mass * 9.8f * Clampf(w / p.wRed, 0.0f, 1.0f) : 0.0f;
    }

    // ---- brakes ----
    c.brakeF = brake * cfg.brakes * p.mass * p.aLat * 1.3f;

    // ---- aero drag (replaces the stock linear drag, which is skipped for this car) ----
    if (p.dragK > 0.0f && s.speed > 0.05f && g_fn.applyForce)
    {
        const float k = -p.dragK * s.speed;
        const float f[3] = { k * s.linvel[0], k * s.linvel[1], k * s.linvel[2] };
        g_fn.applyForce(s.body, f, s.com);
    }

    // ---- transient yaw damper: opposes only CHANGES in yaw rate (yaw rate minus its 0.25 s
    // low-pass), so steady cornering and slides are untouched while the turn-in overshoot of a
    // light car at speed is damped. A pure couple: two opposite forces, no net push.
    {
        const float a = Clampf(dt / 0.25f, 0.0f, 1.0f);
        c.yawSlow = Lerp(c.yawSlow, s.yawRate, a);
        const float invIy = At<float>(s.body, eng::BODY_INVINERTIA_Y);
        const float vr = Clampf(s.speed / p.vTop, 0.0f, 1.2f);
        c.hbHold = c.handbrake ? 0.0f : c.hbHold + dt;
        const float hbFade = Smooth01((c.hbHold - 0.3f) / 0.5f);          // off during handbrake turns
        const float driftFade = 1.0f - Smooth01((c.driftBeta - 6.0f) / 6.0f);   // and while drifting
        const float k = cfg.stability * g_tune.yawDamper * vr * hbFade * driftFade;   // 1/s
        if (k > 0.0f && IsFin(invIy) && invIy > 1e-6f && g_fn.applyForce && c.drivenOnGround >= 0)
        {
            bool anyGround = false;
            for (int i = 0; i < s.nw; ++i) anyGround |= s.wh[i].ground;
            if (anyGround)
            {
                // torque vector along up; realised as a couple +F at com+r, -F at com-r with
                // F = (tau x r) / (2 |r|^2) - pure vector algebra, so it needs no assumption about
                // the handedness of the instance matrix
                const float tauMag = -k * (s.yawRate - c.yawSlow) / invIy;
                const float arm = 0.5f * p.wheelbase;
                float tau[3], r[3], F[3];
                for (int k3 = 0; k3 < 3; ++k3) { tau[k3] = s.up[k3] * tauMag; r[k3] = s.fwd[k3] * arm; }
                Cross(tau, r, F);
                const float inv = 1.0f / (2.0f * arm * arm);
                float pa[3], pb[3], fa[3], fb[3];
                for (int k3 = 0; k3 < 3; ++k3)
                {
                    pa[k3] = s.com[k3] + r[k3];  pb[k3] = s.com[k3] - r[k3];
                    fa[k3] = F[k3] * inv;        fb[k3] = -F[k3] * inv;
                }
                g_fn.applyForce(s.body, fa, pa);
                g_fn.applyForce(s.body, fb, pb);
            }
        }
    }

    TuneBody(s, cfg);

    // ---- engine sound follows our engine: the stock drivetrain's RPM fields ----
    if (cfg.engine_sound && PtrOk(s.drivetrain))
    {
        const float rpm = c.engW * (60.0f / (2.0f * kPi));
        At<float>(s.drivetrain, eng::DRV_RPM)     = rpm;
        At<float>(s.drivetrain, eng::DRV_RPM_LAG) = rpm;
        At<float>(s.drivetrain, eng::DRV_RPM_SFX) = rpm;
    }
    return c.rackDeg;
}

// ---------------------------------------------------------------------------------------------
//  Per wheel, in place of 0x1404DB720. This wheel's suspension ran just before us, so its spring
//  force (= the load the road puts through this tyre right now) is fresh.
// ---------------------------------------------------------------------------------------------
static void WheelConstraint(void* wheel, const Config& cfg)
{
    CarState& c = g_car;
    const CarParams& p = c.p;
    void* joint = At<void*>(wheel, eng::WHL_JOINT);
    if (!PtrOk(joint)) return;                                  // same early-out as the engine

    int i = -1;
    for (int k = 0; k < g_snap.nw; ++k) if (g_snap.wh[k].w == wheel) { i = k; break; }
    if (i < 0 || !c.ok) { g_fn.constraintOrig(wheel); return; }
    const WheelSnap& ws = g_snap.wh[i];

    float P[3], n[3];
    for (int k = 0; k < 3; ++k)
    {
        P[k] = At<float>(wheel, eng::WHL_CONTACT + 4 * k);
        n[k] = At<float>(wheel, eng::WHL_NORMAL + 4 * k);
    }
    const bool ground   = At<uint8_t>(wheel, eng::WHL_AIRBORNE) == 0;
    const float springF = At<float>(wheel, eng::WHL_SPRINGF);
    const float steer   = At<float>(wheel, eng::WHL_STEERDEG) * kDeg;
    for (int k = 0; k < 3; ++k)
    {
        At<float>(joint, eng::JNT_POINT + 4 * k)  = P[k];
        At<float>(joint, eng::JNT_NORMAL + 4 * k) = n[k];
    }

    // heading: the car's forward rotated by the wheel's steer angle about the body up axis
    // (the engine's RotY(angle + 180) construction), then laid onto the ground plane
    float axis[4] = {};
    for (int k = 0; k < 3; ++k) axis[k] = std::cos(steer) * g_snap.fwd[k] - std::sin(steer) * g_snap.right[k];
    const bool nOk = Norm(n);
    if (nOk)
    {
        const float an = Dot(axis, n);
        for (int k = 0; k < 3; ++k) axis[k] -= an * n[k];
    }
    const bool aOk = Norm(axis);
    if (!aOk) { for (int k = 0; k < 3; ++k) axis[k] = g_snap.fwd[k]; }

    if (!ground || !nOk || !aOk)
    {
        g_fn.jointLimits(joint, axis, 0.0f, 0.0f, 0.0f, 0.0f);
        g_fn.jointSoft(joint, 0.0f, 0.0f);
        g_fn.jointTarget(joint, 0.0f, 0.0f);
        At<uint8_t>(joint, eng::JNT_ENABLED) = 1;
        c.slide[i] = false;
        c.skid[i] = 0;
        c.tLatCap[i] = c.tLongF[i] = c.tN[i] = c.tVLat[i] = c.tVisc[i] = 0.0f;
        c.load[i] = 0.0f;
        return;
    }

    // ---- normal load (weight transfer is real: it comes from the engine's own springs) ----
    c.load[i] = Lerp(c.load[i], springF > 0.0f ? springF : 0.0f, 0.6f);
    // pitch transfer = axle average vs car average; roll transfer = this wheel vs its axle,
    // shared front-heavy (like a stiffer front anti-roll bar) so the car understeers at the limit
    const float axleAvg = p.front[i] ? c.frontAvg : c.rearAvg;
    const float rollK   = p.front[i] ? 1.25f : 0.75f;
    float N = c.loadAvg + cfg.weight * ((axleAvg - c.loadAvg) + rollK * (c.load[i] - axleAvg));
    if (N < 0.0f) N = 0.0f;

    // ---- friction coefficient ----
    const float gNorm = c.gEff > 2.0f ? c.gEff : 9.8f;
    float mu = (p.aLat / gNorm) * p.gripBase[i] / 1.08f;
    mu *= p.front[i] ? (0.97f - 0.3f * cfg.balance) : (1.05f + 0.3f * cfg.balance);
    if (c.loadAvg > 1.0f) mu *= Clampf(1.0f - 0.12f * (N / c.loadAvg - 1.0f), 0.75f, 1.12f);  // load sensitivity
    {
        const int sid = At<int>(wheel, eng::WHL_SURFACE);
        if (sid >= 0 && sid < 64)
        {
            void* sf = At<void*>(c.veh, eng::VEH_SURFACES + 8 * sid);
            void* sf2 = PtrOk(sf) ? At<void*>(sf, 0x10) : nullptr;
            if (PtrOk(sf2))
            {
                const float g = At<float>(sf2, 0x28);
                if (IsFin(g) && g > 0.0f) mu *= Clampf(g, 0.3f, 1.2f);
            }
        }
    }
    const float Fmax  = mu * N;                    // lateral
    const float FmaxL = 1.1f * Fmax;               // longitudinal: tyres grip a little more in line
    const float Fsafe = Fmax > 1.0f ? Fmax : 1.0f;

    // how much of its lateral grip this tyre was using last frame (0..1)
    const float latUse = (c.tLatCap[i] > 1.0f) ? Clampf(std::fabs(c.tVLat[i]) * c.tVisc[i] / c.tLatCap[i], 0.0f, 1.0f) : 0.0f;

    // ---- longitudinal ----
    float driveF = 0.0f, stopCap = 0.012f * N;
    if (ws.drive && c.drivenOnGround > 0)
    {
        driveF = c.driveF / c.drivenOnGround;
        stopCap += c.engBrakeF / c.drivenOnGround;
    }
    bool spinning = false;
    if (std::fabs(driveF) > FmaxL)
    {
        // traction control: never spin (straight-line launch at the grip limit); in a corner keep
        // some lateral grip in reserve. Below 0.5 it lets the wheels go - burnouts, power slides.
        const bool allowSpin = cfg.traction_control < 0.5f &&
                               std::fabs(driveF) > FmaxL * (1.0f + 2.0f * cfg.traction_control);
        spinning = allowSpin;
        const float keep = spinning ? 0.85f : 1.0f - 0.3f * cfg.traction_control * latUse;
        driveF = Sgn(driveF) * FmaxL * keep;
    }
    float brakeCap = 0.0f;
    if (ws.brake && c.brakeF > 0.0f)
    {
        const float bias = p.front[i] ? 0.68f : 0.32f;
        const int nAx = p.front[i] ? (p.nFront ? p.nFront : 1) : (p.nRear ? p.nRear : 1);
        // ABS-like: never use the whole tyre, so the car still steers under full braking, and the
        // rears keep the most in reserve so braking into a corner rotates the car without spinning it
        brakeCap = std::fmin(c.brakeF * bias / nAx, FmaxL * (p.front[i] ? 0.85f : g_tune.rearBrakeUse));
    }
    // handbrake: the rear breaks loose more than it brakes - you keep your speed into the slide.
    // On the two pedals it is a power slide: a driven rear tyre keeps its drive, just loose (it
    // used to brake too, and a held slide dragged the car to a stop)
    const bool hb = c.handbrake && ws.handbrake;
    const bool powerSlide = hb && c.pedalHb;
    const float hbBrake = powerSlide ? (driveF > 0.0f ? 0.0f : 0.1f) : 0.4f;
    if (hb) brakeCap = std::fmax(brakeCap, hbBrake * FmaxL * Clampf(cfg.handbrake, 0.2f, 1.5f));

    const bool exactDrive = (!hb || powerSlide) && brakeCap <= 0.0f && driveF != 0.0f;
    const float longUsed = exactDrive ? std::fabs(driveF) : (brakeCap > stopCap ? brakeCap : stopCap);

    // ---- lateral: slip compliance (cornering stiffness), friction ellipse, breakaway ----
    float tLat[3];
    Cross(axis, n, tLat);
    Norm(tLat);
    float pv[3] = {};
    g_fn.pointVel(c.body, P, pv);
    const float vLat  = Dot(pv, tLat);
    const float vLong = Dot(pv, axis);
    // cornering stiffness: peak near 6.9 deg of slip at the front, 4.6 deg at the rear (a stiffer
    // rear damps the yaw response, the way real tyre sizes and toe-in do)
    const float Calpha = Fsafe / (p.front[i] ? g_tune.alphaF : g_tune.alphaR);
    const float vRef = std::fabs(vLong) > 1.2f ? std::fabs(vLong) : 1.2f;
    const float visc = Calpha / vRef;                                    // N per (u/s) of slip
    const float soft = 8.0f / visc;                                      // solver: F ~ 8 v / s

    // the friction ellipse sees the drive at full power: [feel] power below 1 means less pace, not
    // more grip, so a power-on slide still starts where it did at power 1
    const float longEll = exactDrive ? std::fmin(std::fabs(driveF) / (Clampf(cfg.power, 0.5f, 1.0f) *
                                                                      Clampf(c.driftPow, 1.0f, 2.0f)), FmaxL)
                                     : longUsed;
    const float u = longEll / (FmaxL > 1.0f ? FmaxL : 1.0f);
    float latPeak = Fmax * std::sqrt(Clampf(1.0f - u * u, 0.04f, 1.0f));
    if (spinning) latPeak *= 0.4f;
    const float slideRatio = 1.0f - (p.front[i] ? g_tune.dropF : g_tune.dropR) * cfg.breakaway;
    const float latSlide = latPeak * slideRatio;
    const float Freq = visc * std::fabs(vLat);
    if (hb) c.slide[i] = true;
    else if (!c.slide[i] && Freq > latPeak) c.slide[i] = true;
    else if (c.slide[i] && Freq < latSlide * 0.9f) c.slide[i] = false;
    float latCap = c.slide[i] ? latSlide : latPeak;
    // the rear's grip as a share of Fmax (1 = untouched)
    float rearScale = 1.0f;
    const float hbK = Clampf(0.40f - 0.10f * cfg.handbrake, 0.08f, 0.8f);         // 0.30 by default
    if (hb) rearScale = hbK;
    // just released: the rear's grip comes back over 0.6 s instead of snapping the car straight
    else if (ws.handbrake && c.hbHold < 0.6f) rearScale = Lerp(hbK, 1.0f, Smooth01(c.hbHold / 0.6f));
    // drift assist (rear axle, handbrake or a slide on the throttle, from ~6 u/s): the rear's grip
    // is steered toward the drift angle the driver asks for - looser while the angle is short of
    // it, gripping up to catch it when past it. Below ~6 u/s it fades out, so handbrake spins
    // in a car park are still free.
    if (!p.front[i] && (c.driftOn || c.handbrake) && cfg.drift > 0.0f)
    {
        const float e = c.driftTarget - c.driftBeta;                                // + = needs more angle
        const float minK = 1.0f - 0.65f * Clampf(cfg.drift, 0.0f, 1.0f);           // 0.61 at drift 0.6
        float assist = Clampf(0.80f - 0.04f * e, minK, 1.0f);
        const float on = Smooth01((std::fabs(g_snap.vFwd) - 6.0f) / 4.0f);
        assist = Lerp(1.0f, assist, on);
        rearScale = (e < 0.0f) ? std::fmax(rearScale, Lerp(rearScale, assist, on)) : std::fmin(rearScale, assist);
    }
    if (rearScale < 1.0f) latCap = std::fmin(latCap, Fmax * rearScale);

    // ---- write the joint ----
    if (exactDrive) g_fn.jointLimits(joint, axis, latCap, driveF, latCap, -driveF);
    else            g_fn.jointLimits(joint, axis, latCap, longUsed, latCap, longUsed);
    g_fn.jointSoft(joint, soft, 0.0f);
    g_fn.jointTarget(joint, 0.0f, 0.0f);
    At<uint8_t>(joint, eng::JNT_ENABLED) = 1;

    c.tLatCap[i] = latCap; c.tLongF[i] = exactDrive ? driveF : -longUsed; c.tN[i] = N; c.tVLat[i] = vLat;
    c.tVisc[i] = visc;
    // The game decides skid marks / squeal / smoke per wheel from ITS spin rate (still the stock
    // drivetrain's, capped at the stock top speed) against the ground speed - so past the stock car's
    // speed every tyre "skidded" (Notes\36). Ours: really sliding sideways, spinning under power, or
    // the handbrake dragging a rolling rear tyre. Written over the game's byte after Update.
    c.skid[i] = (c.slide[i] && std::fabs(vLat) > 1.0f) || spinning || (hb && std::fabs(vLong) > 1.5f) ? 1 : 0;
}

// After the car's whole Update (every consumer of the byte runs later in the frame, Notes\36 §4).
static void WriteSkid()
{
    const CarSnap& s = g_snap;
    for (int i = 0; i < s.nw; ++i)
        if (PtrOk(s.wh[i].w)) At<uint8_t>(s.wh[i].w, 0x1B0) = g_car.skid[i];
}

// =============================================================================================
//  Telemetry: one CSV row per player frame into a ring, written out by the hotkey thread.
// =============================================================================================
static const int kRingLines = 4096;
static const int kLineCap   = 1024;
static char      g_ring[kRingLines][kLineCap];
static volatile LONG g_ringHead = 0;
static LONG      g_ringTail = 0;
static volatile bool g_telemetry = false;
static FILE*     g_csv = nullptr;
static char      g_csvPath[MAX_PATH] = {};
static bool      g_csvHeaderPending = true;
static volatile LONG g_testPhase = 0;   // the autotest phase running (0 = none), a telemetry column
static double    g_time = 0.0;

static void TelemetryLine(const char* fmt, ...)
{
    const LONG h = g_ringHead;
    if (h - g_ringTail >= kRingLines) return;          // flusher behind: drop, never block
    char* dst = g_ring[h % kRingLines];
    va_list ap; va_start(ap, fmt);
    _vsnprintf_s(dst, kLineCap, _TRUNCATE, fmt, ap);
    va_end(ap);
    InterlockedExchange(&g_ringHead, h + 1);
}

static void ReadFloats(void* p, uintptr_t off, int n, float* out)
{
    __try { for (int i = 0; i < n; ++i) out[i] = At<float>(p, off + 4 * i); }
    __except (EXCEPTION_EXECUTE_HANDLER) { for (int i = 0; i < n; ++i) out[i] = 0.0f; }
}

static void* JointParams(void* wheel)
{
    __try
    {
        void* j = At<void*>(wheel, eng::WHL_JOINT);
        return PtrOk(j) ? At<void*>(j, eng::JNT_PARAMS) : nullptr;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}

} // namespace mdl

// =============================================================================================
//  Lifecycle + observation, used by lcudrive.cpp
// =============================================================================================
static bool ModelInit(uintptr_t base, const char* dir)
{
    using namespace mdl;
    g_fn.steerOrig      = reinterpret_cast<FnVeh>(base + eng::FN_STEER);
    g_fn.spinOrig       = reinterpret_cast<FnVeh>(base + eng::FN_SPIN);
    g_fn.dragOrig       = reinterpret_cast<FnVeh>(base + eng::FN_DRAG);
    g_fn.constraintOrig = reinterpret_cast<FnWheel>(base + eng::FN_CONSTRAINT);
    g_fn.wheelSetSteer  = reinterpret_cast<FnSetSteer>(base + eng::FN_WHEEL_SETSTEER);
    g_fn.jointLimits    = reinterpret_cast<FnJointLimits>(base + eng::FN_JOINT_LIMITS);
    g_fn.jointSoft      = reinterpret_cast<FnJointPair>(base + eng::FN_JOINT_SOFT);
    g_fn.jointTarget    = reinterpret_cast<FnJointPair>(base + eng::FN_JOINT_TARGET);
    g_fn.applyForce     = reinterpret_cast<FnApplyForce>(base + eng::FN_APPLY_FORCE);
    g_fn.pointVel       = reinterpret_cast<FnPointVel>(base + eng::FN_POINT_VELOCITY);
    _snprintf_s(g_csvPath, sizeof(g_csvPath), _TRUNCATE, "%slcudrive_telemetry.csv", dir);
    return true;
}

static void ModelOnNewCar(void* veh)
{
    void* def = nullptr;
    __try { def = At<void*>(veh, eng::VEH_DEF); } __except (EXCEPTION_EXECUTE_HANDLER) {}
    if (!PtrOk(def) || !ReadModelName(def, mdl::g_modelName, sizeof(mdl::g_modelName)))
        strcpy_s(mdl::g_modelName, "?");
    mdl::g_csvHeaderPending = true;
    mdl::RetireBody();
    mdl::g_car.ok = false;
    mdl::CarSnap s;
    if (mdl::Snapshot(veh, s))
    {
        Log(">>> player car: %s  veh=%p  mass=%.1f  wheels=%d  g=%.2f  maxVel=%.1f power=%.1f "
            "rpm=%.0f lock=%.1f\n", mdl::g_modelName, veh, s.mass, s.nw, s.gravity, s.defMaxVel,
            s.defPower, s.defRpm, s.defMaxAngle);
        for (int i = 0; i < s.nw; ++i)
        {
            const mdl::WheelSnap& w = s.wh[i];
            Log("    wheel %d rest=(%+.3f %+.3f %+.3f) r=%.3f drive=%d brake=%d hb=%d steer=%.2f "
                "grip=%.2f\n", i, w.rest[0], w.rest[1], w.rest[2], w.radius, w.drive, w.brake,
                w.handbrake, w.steerable, w.tyreGrip);
        }
    }
    else Log(">>> player car: %s  veh=%p  (snapshot failed)\n", mdl::g_modelName, veh);
}

// Stop driving the current car: its body fields go back in its own next Update (RestoreRetired).
// Game thread only.
static void ModelRestoreAll()
{
    mdl::RetireBody();
    mdl::g_car.ok = false;
}

static void ModelRestoreRetired(void* veh, uint32_t now) { mdl::RestoreRetired(veh, now); }

static void* ModelTunedCar() { return mdl::g_car.bodySaved ? mdl::g_car.veh : nullptr; }
// Player 2's tuned car, while it is parked (i.e. outside its own Update).
static void* ModelTunedCar2() { return !mdl::g_p2In && mdl::g_p2.car.bodySaved ? mdl::g_p2.car.veh : nullptr; }

static void ModelToggleTelemetry()
{
    mdl::g_telemetry = !mdl::g_telemetry;
    Log(">>> telemetry %s (%s)\n", mdl::g_telemetry ? "ON" : "off", mdl::g_csvPath);
    Beep(mdl::g_telemetry ? 1200 : 600, 60);
}

// Lines that start "H," are helicopter rows (heli.h) and "B," motorbike rows (bike.h): they go
// to lcudrive_telemetry_heli.csv / _bike.csv instead.
static FILE* g_heliCsv = nullptr;
static FILE* g_bikeCsv = nullptr;

static FILE* SideCsv(FILE*& f, const char* suffix)
{
    if (!f)
    {
        char p[MAX_PATH];
        strcpy_s(p, mdl::g_csvPath);
        char* dot = strrchr(p, '.');
        if (dot) strcpy_s(dot, p + sizeof(p) - dot, suffix);
        f = _fsopen(p, "a", _SH_DENYWR);
    }
    return f;
}

static void ModelHotkeyThreadTick()
{
    using namespace mdl;
    const LONG head = g_ringHead;
    if (head == g_ringTail) return;
    if (!g_csv) g_csv = _fsopen(g_csvPath, "a", _SH_DENYWR);
    if (!g_csv) { g_ringTail = head; return; }
    bool heli = false, bike = false;
    while (g_ringTail != head)
    {
        const char* line = g_ring[g_ringTail % kRingLines];
        if (line[0] == 'H' && line[1] == ',')
        {
            if (SideCsv(g_heliCsv, "_heli.csv")) { fputs(line + 2, g_heliCsv); heli = true; }
        }
        else if (line[0] == 'B' && line[1] == ',')
        {
            if (SideCsv(g_bikeCsv, "_bike.csv")) { fputs(line + 2, g_bikeCsv); bike = true; }
        }
        else fputs(line, g_csv);
        ++g_ringTail;
    }
    fflush(g_csv);
    if (heli) fflush(g_heliCsv);
    if (bike) fflush(g_bikeCsv);
}

// After every player-car Update, stock or ours, so the A/B is always measurable.
static void ModelObserve(void* veh, const Config& cfg, bool active)
{
    using namespace mdl;
    if (!active) g_snapOk = Snapshot(veh, g_snap);       // when active, FrameUpdate refreshed it
    if (!g_snapOk) return;
    const CarSnap& s = g_snap;
    g_time += (IsFin(s.dt) && s.dt > 0.0f && s.dt < 0.25f) ? s.dt : 0.0;
    const CarState& c = g_car;
    float sd[4] = {}, sf[4] = {};
    for (int i = 0; i < 4 && i < s.nw; ++i)
    {
        __try { sd[i] = At<float>(s.wh[i].w, eng::WHL_STEERDEG); } __except (EXCEPTION_EXECUTE_HANDLER) {}
        sf[i] = s.wh[i].springF;
    }
    const float rpm = c.engW * 60.0f / (2.0f * kPi);

    if (cfg.log_every > 0 && (g_frame % (uint32_t)cfg.log_every) == 0)
        Log("[%s %s] v=%5.2f fwd=%+6.2f lat=%+5.2f yaw=%+5.2f | in st=%+.2f th=%.2f rv=%.2f br=%.2f "
            "hb=%.2f idle=%d | steer=%+5.1f gear=%d rpm=%4.0f F=%6.0f B=%5.0f gEff=%.2f\n",
            g_modelName, active ? "LCU" : "stock", s.speed, s.vFwd, s.vLat, s.yawRate,
            s.inSteer, s.inThrottle, s.inReverse, s.inBrake, s.inHandbrake, s.idle ? 1 : 0,
            sd[0], c.gear, rpm, c.driveF, c.brakeF, c.gEff);

    const bool teleOn = g_telemetry || cfg.telemetry;
    static bool s_teleWas = false;
    if (teleOn && !s_teleWas) g_csvHeaderPending = true;
    s_teleWas = teleOn;
    if (!teleOn) return;

    if (g_csvHeaderPending)
    {
        g_csvHeaderPending = false;
        TelemetryLine("# %s %s built %s %s\n", g_modelName, active ? "lcudrive" : "stock", __DATE__, __TIME__);
        TelemetryLine("t,active,dt,speed,vfwd,vlat,yaw,steer,thr,rev,brk,hb,idle,wsteer0,gear,rpm,driveF,"
                      "brakeF,gEff,sf0,sf1,sf2,sf3,n0,n1,n2,n3,cap0,cap1,cap2,cap3,sl0,sl1,sl2,sl3,"
                      "vl0,vl1,vl2,vl3,d0_lath,d0_longh,d0_longl,d0_soft,d0_tgt,d2_lath,d2_longh,d2_longl,"
                      "d2_soft,d2_tgt,x,z,phase\n");
    }
    float d0[17] = {}, d2[17] = {};
    if (s.nw > 0) { void* d = JointParams(s.wh[0].w); if (PtrOk(d)) ReadFloats(d, 0, 17, d0); }
    if (s.nw > 2) { void* d = JointParams(s.wh[2].w); if (PtrOk(d)) ReadFloats(d, 0, 17, d2); }
    TelemetryLine("%.4f,%d,%.4f,%.3f,%.3f,%.3f,%.3f,%.3f,%.2f,%.2f,%.2f,%.2f,%d,%.2f,%d,%.0f,%.0f,%.0f,"
                  "%.2f,%.0f,%.0f,%.0f,%.0f,%.0f,%.0f,%.0f,%.0f,%.0f,%.0f,%.0f,%.0f,%d,%d,%d,%d,"
                  "%.3f,%.3f,%.3f,%.3f,%.0f,%.0f,%.0f,%.5f,%.2f,%.0f,%.0f,%.0f,%.5f,%.2f,%.2f,%.2f,%d\n",
        g_time, active ? 1 : 0, s.dt, s.speed, s.vFwd, s.vLat, s.yawRate,
        s.inSteer, s.inThrottle, s.inReverse, s.inBrake, s.inHandbrake, s.idle ? 1 : 0,
        sd[0], c.gear, rpm, c.driveF, c.brakeF, c.gEff,
        sf[0], sf[1], sf[2], sf[3], c.tN[0], c.tN[1], c.tN[2], c.tN[3],
        c.tLatCap[0], c.tLatCap[1], c.tLatCap[2], c.tLatCap[3],
        c.slide[0] ? 1 : 0, c.slide[1] ? 1 : 0, c.slide[2] ? 1 : 0, c.slide[3] ? 1 : 0,
        c.tVLat[0], c.tVLat[1], c.tVLat[2], c.tVLat[3],
        d0[4], d0[5], d0[7], d0[8], d0[16], d2[4], d2[5], d2[7], d2[8], d2[16],
        s.com[0], s.com[2], static_cast<int>(g_testPhase));
}

// Speed for the motion blur, and the headlight frame (front-axle midpoint) for the lamp shader.
static bool ModelBridgeSlots(void* veh, const Config& cfg, vsim::BridgeSlots& bs)
{
    using namespace mdl;
    (void)veh;
    if (!g_snapOk) return false;
    const CarSnap& s = g_snap;
    bs.speed01 = Clampf(s.speed / cfg.bridge_speed, 0.0f, 1.0f);
    float mid[3] = {}; int nf = 0; float xmin = 0, xmax = 0;
    for (int i = 0; i < s.nw; ++i)
    {
        if (s.wh[i].rest[2] >= -0.05f) continue;
        for (int k = 0; k < 3; ++k) mid[k] += s.wh[i].rest[k];
        if (nf == 0) xmin = xmax = s.wh[i].rest[0];
        else { if (s.wh[i].rest[0] < xmin) xmin = s.wh[i].rest[0]; if (s.wh[i].rest[0] > xmax) xmax = s.wh[i].rest[0]; }
        ++nf;
    }
    if (nf > 0)
    {
        for (int k = 0; k < 3; ++k) mid[k] /= (float)nf;
        for (int k = 0; k < 3; ++k)
            bs.headlightPos[k] = s.pos[k] + s.row0[k] * mid[0] + s.row1[k] * mid[1] + s.row2[k] * mid[2];
        const float half = 0.5f * (xmax - xmin);
        bs.lampHalfSpacing = half > 0.05f ? Clampf(half, 0.2f, 3.0f) : 0.0f;
    }
    else
    {
        for (int k = 0; k < 3; ++k) bs.headlightPos[k] = s.pos[k] + s.fwd[k] * 0.8f;
        bs.lampHalfSpacing = -1.0f;
    }
    for (int k = 0; k < 3; ++k) { bs.fwd[k] = s.fwd[k]; bs.right[k] = s.right[k]; }
    bs.lampIntensity = 1.0f;
    bs.beamHalfAngleRad = 0.5f;
    return true;
}
