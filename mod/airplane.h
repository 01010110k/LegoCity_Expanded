// =============================================================================================
//  airplane.h - real aeroplanes on their own wheels (namespace fw = fixed wing). The model is
//  plane_model.h in gearMode 1; the airport spawner is airport.h.
//
//  The 2026-11-13 goal: flyable aeroplanes with their own mechanics and physics - taking off and
//  landing like an aeroplane, not a helicopter, and not looking like helicopters: a whole separate
//  plane system with actual plane models.
//  The game has exactly ONE winged vehicle: AIRCRAFT_ELIFANT, the white twin-jet airliner with the
//  five-wheel gear - and the planes parked at the airport are copies of it [P: models report]. It is a
//  CAR-class vehicle (VehicleDefCar, one DriverSeat) "rigged to taxi", so it reaches ApiVehicleCar::
//  Update (lcudrive.cpp CarThunk), never the helicopter's hover path. Nothing here is helicopter code:
//  no HoveringAircraft pilot data, no heli exits, no rotor, no heli camera, no hover height drive.
//  (plane.h, the Hover-class "planes", is built on all of those; it stays as an opt-in.)
//
//  Per frame, the player's plane (CarThunk's plane branch; never the car model):
//    Pre   before ApiVehicleCar::Update: gates, a snapshot (mdl::Snapshot, a pure reader) and the
//          physical velocities; publishes the plane to the four redirected calls.
//    Steer in place of UpdateSteering 0x1404E0E50: the nose wheel's angle, and ON THE WHEELS the
//          aerodynamic force m * acc at the centre of mass (FN_APPLY_FORCE) - the site model.h
//          already applies its own body forces from [P: model.h FrameUpdate].
//    Wheel in place of WheelConstraint 0x1404DB720: free-rolling wheels with side grip and brakes
//          (plane::GearWheelRows); never a drive motor - the stock one would hold the plane at the
//          definition's Max Velocity [P: Notes\20 §1, Notes\23 §1].
//    Spin / Drag: skipped (no donut yaw; the stock linear drag is a FORCE that would survive every
//          write and hold a plane near 15 u/s [I: physics report]); the model's drag replaces it.
//    Post  after Update: this frame's wheel rays -> plane::Update.
//          ON THE WHEELS (any wheel down, or up < 0.05 s): nothing writes the velocity. The car's own
//          gravity (integrated inside Update, Notes\23 #1), springs and wheel joints stay; lift comes
//          in as a force and unloads the springs, so the plane lifts off when the wing carries it.
//          The rotation at Vr is a pitch rate about the rear wheels' contact (bike.h's wheelie
//          recipe, plane::GearRotate).
//          IN THE AIR (no wheel down for 0.05 s): the proven plane.h recipe - the velocities from
//          before Update plus the model's, written after it, which discards the engine's gravity
//          step, FlightAlign, the jump and the collision hop [P: Notes\23 §0-§2, Notes\28]; the
//          model's own gravity (veh+0x2EC) is the only one. And the flipped timer veh+0x554 = 0, or
//          a bank past 45 deg held 2 s takes LCGeneric's periodic flipped damage [I: Notes\23 §3].
//  Damage: collisions are the game's own car damage (OnCollision 0x140194760: 1 / 2 / 4 points at
//  0.3 / 0.6 / 0.8 x the definition's Max Velocity [P]), so Max Velocity goes from the shipped 5
//  (a 4 u/s bump destroys it) to [airplane] max_velocity in the plane's own definition (DefTick);
//  NO dv ladder here, or a crash would count twice. A bad touchdown (sink / bank / nose or tail first)
//  is plane::LandingDamage (plane.h's TouchdownDamage table, harness-tested) through vd::Damage.
//  Getting out: the stock car exit. In the air, if the game lets you out [U], the parachute is armed
//  and the empty plane glides down with the throttle shut (UnoccupiedPre / Post).
//  Co-op: everything per player lives in Ctx (SwapP2); the frame scratch is shared (one Update at a
//  time on the game thread).
//  The landing gear (2026-11-14): the HORN in the air is the gear lever (plane::GearSwitch; the horn is
//  the byte the game's own horn update reads, [veh+0x368]+0x35 / +0x36 [P: 0x1404C6D90 -> 0x1404C03B0 /
//  0x1404C03C0], or the controller's LB / left-stick click read directly - the game's Horn_1 / Horn_2
//  [P: Notes\37 3.3]; the horn sound is left alone). Gear up = less drag (plane_model.h). With the
//  gear up the wheels must not carry the plane: the suspension's force this frame is F = wheel+0x120
//  along wheel+0x100 at the contact wheel+0xD0, applied through ApplyForceAtPoint [P: 0x1404DAB30,
//  0x1404DB232..0x1404DB26B] - the wheel hook, which runs right after it, applies the opposite force
//  (the same call, the same point) scaled by how far up the gear is, and gives the tyre rows only the
//  load that is left. So the hull's own collision shapes meet the ground: a belly landing. Nothing of
//  the wheel, its definition or the ray is written. Per player (Ctx.gear); down on every boarding.
//  What is DRAWN (2026-11-14: retracting wheels, spinning propellers):
//  WheelVisual below. The car class draws each wheel mesh on its wheel joint, and poses that joint in the
//  post-physics pass from wheel+0x50 - the wheel's matrix in the vehicle's space, written by the per-wheel
//  update and read by nothing but the joint modifier [P: 0x1404DA57D writes, 0x1404DA160 reads; phase
//  0x29 0x1404AA910 -> 0x1404AC4C0 -> 0x140469C30, after every Update]. After the Update of an Elifant
//  whose gear is (partly) up the wheels' positions there are moved up into the hull (plane::GearStowPos);
//  on the ThunderRoc, whose main wheel joints carry the propellers (contrib_planes v2), they are put on
//  the propeller hubs and the visual spin angle wheel+0x128 is ours. Physics never sees either: the ray,
//  the contact, the spring and the ContactJoint use wheel+0x10 / +0xD0.. and the rest matrix. The
//  Elifant's gear LEGS are part of its hull mesh and stay drawn (a shipped model: not overridden).
//  2026-11-15 (the landing gear of the two new planes did not move): with the planes3 PATCH.DAT the
//  Pegasus' and the ThunderRoc's wheels AND legs are meshes of wheel joints (plane::GearPartFor); those
//  joints' whole matrix is ours every frame - gear down at the anchor, gear up inside the hull - and
//  their roll angle wheel+0x128 is held at 0, so a leg never rolls with its wheel (those wheels do not
//  visibly turn on the ground). Their steering is not drawn: the .cd's SteeringVisible = 0 on the nose
//  wheel [P: 0x1404DA28B tests VehicleDefWheel +0x114 before it turns the joint].
// =============================================================================================
#pragma once
#include "plane_model.h"

namespace fw {

// ---- engine facts (car class) ----------------------------------------------------------------
static const uintptr_t CTRL_HORN = 0x35, CTRL_HORN_B = 0x36;   // bytes of the working input block: the horn buttons held
                                            // [P: 0x1404C03B0 / 0x1404C03C0, called from the horn update 0x1404C6D90]
static const uintptr_t WHL_SPRINGDIR = 0x100;   // vec3: the direction the suspension force is applied along [P: 0x1404DB04E, 0x1404DB23D]
static const uintptr_t VEH_FLIPT = 0x554;   // float, s not upright (+dt, cap 10, else 0) [P: Notes\23 §7 order];
                                            // the player's periodic damage while >= 2 s [LIKELY: Notes\23 §3]
// eng::VEH_FLAGS7 bit0 = fc6 bit7 (all wheels airborne) of the previous frame, i.e. FlightAlign ran in
// THIS Update's base part (it is gated on fc6 bit7 before 0x1404E0D30 moves it) [I: Notes\23 §6 / §7]
// The chase camera (MechVehicles slot 49, 0x1406CAD00) re-reads these from [veh+0x358] on every call [P]:
static const uintptr_t DEF_OFF[7] = {
    0x174,          // Max Velocity: OnCollision's damage ladder, the camera's speed blend [P]
    0x188, 0x18C,   // FollowHeight / FollowDistance (slow)
    0x194, 0x198,   // ... Max (blended in by (|v| - 4) / (Max Velocity - 4))
    0x1A0, 0x1A4,   // ... Boost
};
static const float  SHIPPED[3] = { 5.0f, 0.75f, 2.0f };   // Max Velocity, FollowHeight, FollowDistance [P: cdedit]
static const float  STAGED[3]  = { 25.0f, 1.6f, 6.5f };   // planes_stage\airplane_v1 aircraft_elifant.cd

enum Mode : int { kOff = 0, kGround = 1, kAir = 2 };

// ---------------------------------------------------------------------------------------------
//  Registry: which car-class vehicles fly ([airplane] models, the names after AIRCRAFT_)
// ---------------------------------------------------------------------------------------------
// 1 = an airliner (ELIFANT, PEGASUS... -> plane::Elifant()), 0 = not ours. The parsing is pure and
// harness-tested (plane::AircraftListed).
static int KindFromName(const char* name, const Config& cfg)
{
    return plane::AircraftListed(name, cfg.airplane_models) ? 1 : 0;
}

// Every car comes through CarThunk every frame (traffic too): a 64-entry hashed cache, keyed on the
// vehicle, its definition (an address can be reused by another model) and the ini generation.
struct KindCache { void* veh; void* def; int kind; uint32_t gen; };
static KindCache g_kc[64] = {};
static uint32_t  g_kcGen = 1;                     // ++ on an ini reload ([airplane] models may change)

static int KindOf(void* veh, const Config& cfg)  // caller: SEH
{
    void* def = At<void*>(veh, eng::VEH_DEF);
    KindCache& c = g_kc[(reinterpret_cast<uintptr_t>(veh) * 0x9E3779B97F4A7C15ull) >> 58];
    if (c.veh == veh && c.def == def && c.gen == g_kcGen) return c.kind;
    char name[64];
    const int kind = PtrOk(def) && ReadModelName(def, name, sizeof(name)) ? KindFromName(name, cfg) : 0;
    c = KindCache{ veh, def, kind, g_kcGen };
    return kind;
}

static plane::Params ParamsFor(const Config& c)
{
    plane::Params p = plane::Elifant();
    p.turnSign  = c.airplane_turn_sign;
    p.fwdSign   = c.airplane_fwd_sign;
    p.rotateDeg = c.airplane_rotate_deg;
    p.reverseG  = c.airplane_reverse_g;
    p.taxiReverseSpeed = c.airplane_taxi_reverse_speed;
    p.gearUpTopSpeed = c.airplane_gear_retract ? c.airplane_gear_up_speed : 0.0f;
    p.gearTimeS  = c.airplane_gear_time;
    p.bellySpeed = c.airplane_gear_belly_speed;
    p.destroySpeed = c.airplane_crash_destroy_speed;
    p.bellyDecel = c.airplane_gear_belly_decel;
    return p;
}

// ---------------------------------------------------------------------------------------------
//  Per player (lcudrive.cpp SwapCtx) and the shared frame scratch
// ---------------------------------------------------------------------------------------------
struct Ctx
{
    plane::State  st;
    plane::Output last;           // last frame's model output: the plan the hooks use this frame
    bool          lastValid;
    plane::SteerWatch watch;
    void*    veh;                 // the plane this player flies / flew last
    int      mode;                // kGround / kAir of the last active frame
    uint32_t lastT;               // GetTickCount of the last frame in it
    uint32_t boardT;              // ... of the boarding
    uint32_t airSeenT;            // ... of the last air-mode frame
    bool     left;                // the player got out of it since (UnoccupiedPre): the next frame in it is a new boarding
    bool     logBoard;            // the boarding line is still to be written
    uint32_t refused;             // stand-down reasons already logged this boarding (bits)
    bool     lodLogged, lodAirLogged, ctrlLogged, fallbackLogged;
    float    lastPos[3];          // where it was at the last frame in it (the new-boarding test)
    bool     lastPosOk;
    float    contactY, hEst;      // body y at the last frame with a wheel down; height above it
    float    cool;                // s before another touchdown can do damage
    float    rollStart[3], rollT; // where / since when the take-off roll ran (for the LIFT-OFF line)
    bool     rolling;
    float    tdPos[3];            // where it touched down (for the "stopped" line)
    bool     tdTrack;
    float    stallLogT, impactLogT, waterT;
    bool     wet;                 // went into the water: stands down until the next boarding
    bool     glide;               // the plane this player left in the air glides down empty
    float    glideT;
    plane::GearSw gear;           // the gear lever (down on every boarding: Ctx{})
    float    gearWarnT;           // s until the next "gear!" beep
    bool     gearWarned;          // the warning line was written for this approach
};
static Ctx g = {};
static Ctx g_p2 = {};
static void SwapP2() { const Ctx t = g; g = g_p2; g_p2 = t; }

// While player 1's context is live: is this empty plane player 2's - the one they were last in, until
// their exit is seen (UnoccupiedPre: bail-out, parachute, glide), or the one gliding down after it?
static bool P2Owns(const void* veh)
{
    return veh && g_p2.veh == veh && (!g_p2.left || g_p2.glide);
}

struct Frame
{
    mdl::CarSnap s;               // Pre's snapshot: rows, centre of mass, mass, the wheel list
    bool  ok;                     // the hooks may act this Update
    bool  steerRan;               // the steer hook ran (else Post applies the ground force itself)
    bool  force;                  // apply m * acc this Update
    bool  ctrlOff;                // the game's controls are inactive (a cut, the exit) - in the air only
    bool  lodHeld;                // Pre wrote sim LOD 1 (force_lod1): Post checks that it stuck
    float acc[3];
    float noseDeg;                // the angle to publish (steer_sign applied)
    float wheelBrake;
    float gearUp;                 // 0 = down .. 1 = up: the share of each wheel's suspension force to cancel
    const Config* cfg;
};
static Frame g_f = {};

static double          g_t = 0.0;                // telemetry clock
static bool            g_headerDone = false;
static volatile LONG   g_horn = 0;               // the hotkey thread plays the stall beeps
static volatile LONG   g_gearBeep = 0;           // ... and the gear's: 1 up, 2 down, 3 "gear!" (low over the ground)
static uint32_t        g_hornT = 0;
static volatile bool   g_releaseReq = false;     // unhook: give the definition back, then stand down
static bool            g_lodOff = false;         // force_lod1 did not stick once: off for the session
static const uintptr_t BODY_FLAGS = 0xC0;        // bit5 = not integrated (frozen: parked / asleep)
                                                 // [I: body+0xC0 per Notes\18's probe fields; Notes\23 #14/#15]

// ---------------------------------------------------------------------------------------------
//  The vehicle-only boundary blockers (Dyno group 4, Notes\27 §5: real contact walls for every
//  ApiVehicle; whether the city has any is [U]) - ignored by the plane while it flies or glides, as
//  heli.h BoundaryMask does for the flown helicopter; given back on the wheels, on a stand-down and on
//  `[` / unhook / a fault, always from inside that plane's own Update. On the wheels it stays stock:
//  a ground blocker still stops a taxiing plane.
// ---------------------------------------------------------------------------------------------
struct MaskRec { void* veh; uint16_t orig; };
static MaskRec g_mask[4] = {};

static void Mask(void* veh, bool on)
{
    MaskRec* rec = nullptr;
    for (MaskRec& r : g_mask) if (r.veh == veh) { rec = &r; break; }
    if (!on && !rec) return;
    void* inst = At<void*>(veh, eng::VEH_INSTANCE);
    if (!PtrOk(inst)) { if (rec && !on) rec->veh = nullptr; return; }
    uint16_t& m = At<uint16_t>(inst, eng::INST_IGNORE);
    if (on)
    {
        if (!rec) for (MaskRec& r : g_mask) if (!r.veh) { r.veh = veh; r.orig = m; rec = &r; break; }
        if (rec) m = static_cast<uint16_t>(m | eng::GROUP_BOUNDARY_BIT);
    }
    else
    {
        if (!(rec->orig & eng::GROUP_BOUNDARY_BIT)) m = static_cast<uint16_t>(m & ~eng::GROUP_BOUNDARY_BIT);
        rec->veh = nullptr;
    }
}

// ---------------------------------------------------------------------------------------------
//  The plane's own definition: Max Velocity (collision damage, camera blend) and the chase camera.
//  A deliberate, bounded exception to engine.h's "VehicleDefCar SHARED per model: never written":
//  only a definition whose model is in [airplane] models (the only live Elifants are the ones airport.h
//  spawns - no stock script spawns one [P: airport report]), only after a signature check, saved and
//  given back - and both only from inside an Update of a vehicle with that definition (alive, so is
//  its definition). FOV, BoostShakeScale and FocusHeightOffset are never written.
// ---------------------------------------------------------------------------------------------
// The decisions are pure and harness-tested (plane::DefAdopt / DefApply / DefGiveBack); this is only
// the reading and writing around them.
// One record per definition (2026-11-14: several models may stand at the airport - Elifant, Pegasus,
// ThunderRoc - each with its own VehicleDefCar; planes of one model share theirs).
struct DefRec { plane::DefHold h; void* ptr; uint32_t seen; };
static DefRec   g_defs[4] = {};                   // ptr == nullptr = free
static void*    g_defBad[4] = {};                 // definitions with an unexpected signature (logged once each)
static int      g_defBadN = 0;

static DefRec* DefFind(void* def) { if (def) for (DefRec& r : g_defs) if (r.ptr == def) return &r; return nullptr; }
static bool AnyDefHeld() { for (const DefRec& r : g_defs) if (r.ptr) return true; return false; }

static void ReadDef(void* def, float v[7]) { for (int i = 0; i < 7; ++i) v[i] = At<float>(def, DEF_OFF[i]); }
static void WriteDef(void* def, const float v[7], const float was[7])
{
    for (int i = 0; i < 7; ++i) if (v[i] != was[i]) At<float>(def, DEF_OFF[i]) = v[i];   // only what changed
}

static void DefRestore(void* def)
{
    DefRec* r = DefFind(def);
    if (!r) return;
    float cur[7], was[7];
    ReadDef(def, cur);
    for (int i = 0; i < 7; ++i) was[i] = cur[i];
    const int n = plane::DefGiveBack(r->h, cur);                           // only what is still ours
    WriteDef(def, cur, was);
    *r = DefRec{};
    Log(">>> airplane: def %p restored (%d field%s) - Max Velocity %.1f, camera %.2f/%.2f\n", def, n, n == 1 ? "" : "s",
        cur[0], cur[1], cur[2]);
}

// The plane leaves airplane.h's hands (`[`, [airplane] enable = 0, off the list, a fault, unhook):
// its definition and its boundary bit back. From inside its own Update.
static void Release(void* veh)
{
    DefRestore(At<void*>(veh, eng::VEH_DEF));
    Mask(veh, false);
}

// Is this a vehicle whose definition is held? (a cheap test for every car while one is)
static bool HoldsDefOf(void* veh) { return DefFind(At<void*>(veh, eng::VEH_DEF)) != nullptr; }

// Once per Update of any plane in [airplane] models (the parked one too: its damage ladder counts).
static void DefTick(void* veh, const Config& cfg, bool on)
{
    void* def = At<void*>(veh, eng::VEH_DEF);
    const uint32_t now = GetTickCount();
    if (!PtrOk(def)) return;
    DefRec* r = DefFind(def);
    if (!on) { if (r) DefRestore(def); return; }
    const float want[7] = { cfg.airplane_max_velocity, cfg.airplane_cam_height, cfg.airplane_cam_distance,
                            cfg.airplane_cam_height_fast, cfg.airplane_cam_distance_fast,
                            cfg.airplane_cam_height_fast, cfg.airplane_cam_distance_fast };
    float cur[7], was[7];
    ReadDef(def, cur);
    for (int i = 0; i < 7; ++i) was[i] = cur[i];
    if (!r)
    {
        for (int i = 0; i < g_defBadN; ++i) if (g_defBad[i] == def) return;
        // a free record; else one whose definition has had no Update for 10 s - it may be unloaded, so it
        // is forgotten, never written (a reload is the file's values anyway)
        for (DefRec& c : g_defs) if (!c.ptr) { r = &c; break; }
        if (!r) for (DefRec& c : g_defs) if (now - c.seen > 10000) { c = DefRec{}; r = &c; break; }
        if (!r) return;                                                    // four live plane definitions already
        plane::DefHold h;
        const int a = plane::DefAdopt(h, cur, want, SHIPPED, STAGED);
        if (a == plane::DEF_BAD)
        {
            if (g_defBadN < 4) g_defBad[g_defBadN++] = def;
            Log(">>> airplane: def %p signature unexpected (maxvel %.2f cam %.2f/%.2f) - left stock\n", def, cur[0], cur[1], cur[2]);
            return;
        }
        r->h = h; r->ptr = def;
        char name[64];
        if (!ReadModelName(def, name, sizeof(name))) strcpy_s(name, "?");
        if (a == plane::DEF_SAME) Log(">>> airplane: def %s already carries the plane values (staged record) - nothing written\n", name);
        else Log(">>> airplane: def %s patched - Max Velocity %g -> %g, camera %g/%g -> %g/%g (fast %g/%g)%s\n", name,
                 cur[0], want[0] > 0.0f ? want[0] : cur[0], cur[1], cur[2], want[1] > 0.0f ? want[1] : cur[1],
                 want[2] > 0.0f ? want[2] : cur[2], want[3] > 0.0f ? want[3] : cur[3], want[4] > 0.0f ? want[4] : cur[4],
                 r->h.staged ? " (over the staged record)" : "");
    }
    r->seen = now;
    if (plane::DefApply(r->h, cur, want)) WriteDef(def, cur, was);
}

// ---------------------------------------------------------------------------------------------
//  Small readers
// ---------------------------------------------------------------------------------------------
static void Rows(void* inst, float right[3], float up[3], float back[3])
{
    for (int k = 0; k < 3; ++k)
    {
        right[k] = At<float>(inst, eng::INST_ROW0 + 4 * k);
        up[k]    = At<float>(inst, eng::INST_ROW1 + 4 * k);
        back[k]  = At<float>(inst, eng::INST_ROW2 + 4 * k);
    }
}

// The pitch axis the model uses: right = forward x up (plane_model.h conventions).
static bool PitchAxis(void* inst, float r[3], float f[3], float u[3])
{
    float rr[3], b[3];
    Rows(inst, rr, u, b);
    for (int k = 0; k < 3; ++k) f[k] = -b[k];
    if (!plane::Norm(u) || !plane::Norm(f)) return false;
    plane::Cross(f, u, r);
    return plane::Norm(r);
}

// The controls. A controller: left stick = bank / pitch, RT / LT = the
// lever (and LT the brakes on the wheels) - pad.h follows the current player. The keyboard is player
// 1's: A / D (the game's steer field), J / K (the throttle / brake fields of the SOURCE block - the
// working block's +0x0C / +0x14 carry the tilt throttle cut and the auto park-brake [I: physics report,
// 0x1404AC93A, 0x1404E08B0]), W / S nose down / up [U: may clash with a game binding].
static void Controls(void* veh, const Config& cfg, plane::Input& in)
{
    void* src = At<void*>(veh, eng::VEH_SRCCTRL);
    pad::Poll();
    const bool havePad = pad::g_user >= 0 && mdl::KeyDownInGame(-1);
    if (cfg.airplane_stick_direct && havePad)
    {
        in.cTurn = Clampf(pad::g_lx, -1.0f, 1.0f);
        in.cFwd  = Clampf(pad::g_ly, -1.0f, 1.0f);
        in.throttle = pad::g_rt / 255.0f;
        in.brake    = pad::g_lt / 255.0f;
        return;
    }
    // the game's controls are off (a cut, the exit animation - in the air only, Pre): sticks centred,
    // the lever holds where it is, the path hold flies on
    if (g_f.ctrlOff) return;
    if (PtrOk(src))
    {
        in.cTurn = At<float>(src, eng::CTRL_STEER);
        in.throttle = At<float>(src, eng::CTRL_THROTTLE);
        in.brake = std::fmax(At<float>(src, eng::CTRL_REVERSE), std::fmax(At<float>(src, eng::CTRL_BRAKE), At<float>(src, eng::CTRL_HANDBRAKE)));
    }
    if (havePad) in.cFwd = Clampf(pad::g_ly, -1.0f, 1.0f);              // the game has no pitch field
    else if (g_ctx == 0) in.cFwd = (mdl::KeyDownInGame('W') ? 1.0f : 0.0f) - (mdl::KeyDownInGame('S') ? 1.0f : 0.0f);
}

// The horn buttons, held: the game's own field (keyboard and controller, whoever drives), or this
// player's controller read directly. Not while the game's controls are off (a cut, the exit).
static bool HornHeld(void* veh)
{
    if (g_f.ctrlOff) return false;
    void* ctl = At<void*>(veh, eng::VEH_CTRL);
    if (PtrOk(ctl) && (At<uint8_t>(ctl, CTRL_HORN) || At<uint8_t>(ctl, CTRL_HORN_B))) return true;
    void* src = At<void*>(veh, eng::VEH_SRCCTRL);                        // the block the working one is copied from
    if (PtrOk(src) && At<uint8_t>(src, CTRL_HORN)) return true;         // [P: 0x1404EC102..0x1404EC10A, +0x35 -> +0x35]
    const bool havePad = pad::g_user >= 0 && mdl::KeyDownInGame(-1);       // (Controls polled it this frame)
    return havePad && (pad::g_buttons & (XINPUT_GAMEPAD_LEFT_SHOULDER | XINPUT_GAMEPAD_LEFT_THUMB)) != 0;
}

static void TelemetryHeader()
{
    if (g_headerDone) return;
    g_headerDone = true;
    mdl::TelemetryLine("A,t,player,lod,mode,wheelsDown,wow,cx,cy,thr,brk,lever,spool,px,py,pz,vx,vy,vz,airspeed,alpha,beta,"
                       "bank,pitch,gamma,liftG,stall,noseDeg,rotRate,hEst,crash,dt,gear\n");
}

static void LogBoard(void* veh, int player, const mdl::CarSnap& s)
{
    char name[64];
    if (!ReadModelName(s.def, name, sizeof(name))) strcpy_s(name, "?");
    int nSteer = 0, nBrake = 0;
    float zF = 1e9f, zR = -1e9f;
    for (int i = 0; i < s.nw; ++i)
    {
        if (s.wh[i].steerable > 0.0f) ++nSteer;
        if (s.wh[i].brake) ++nBrake;
        zF = std::fmin(zF, s.wh[i].rest[2]); zR = std::fmax(zR, s.wh[i].rest[2]);
    }
    int hc = -1, hm = -1;
    vd::Health(veh, hc, hm);
    Log(">>> airplane: %s boarded by player %d  veh=%p mass=%.1f wheels=%d (steer %d, brake %d) wheelbase %.2f "
        "health %d/%d lod %d\n", name, player, veh, s.mass, s.nw, nSteer, nBrake, s.nw > 1 ? zR - zF : 0.0f, hc, hm,
        At<int32_t>(veh, eng::VEH_SIMLOD));
    for (int i = 0; i < s.nw; ++i)
        Log("    wheel %d rest=(%+.2f %+.2f %+.2f) r=%.2f steer=%.2f brake=%d drive=%d\n", i, s.wh[i].rest[0],
            s.wh[i].rest[1], s.wh[i].rest[2], s.wh[i].radius, s.wh[i].steerable, s.wh[i].brake, s.wh[i].drive);
}

// once per reason per boarding
static void StandDown(int bit, const char* why)
{
    if (g.refused & (1u << bit)) return;
    g.refused |= 1u << bit;
    Log(">>> airplane: stood down - %s\n", why);
}

// ---------------------------------------------------------------------------------------------
//  Pre: before ApiVehicleCar::Update of the player's plane. True = ours this frame.
// ---------------------------------------------------------------------------------------------
static bool Pre(void* veh, int player, const Config& cfg, bool armed, float lv0[3], float av0[3])
{
    const uint32_t now = GetTickCount();
    g_f.ok = false;
    g_f.ctrlOff = false; g_f.lodHeld = false;
    void* inst0 = At<void*>(veh, eng::VEH_INSTANCE);
    void* body0 = PtrOk(inst0) ? At<void*>(inst0, eng::INST_BODY) : nullptr;
    float here[3] = {};
    bool hereOk = false;
    if (PtrOk(inst0))
    {
        for (int k = 0; k < 3; ++k) here[k] = At<float>(inst0, eng::INST_POS + 4 * k);
        hereOk = IsFin(here[0]) && IsFin(here[1]) && IsFin(here[2]);
    }
    // A new boarding: another plane, or back in after getting out. After 30 s with no Update of it, also
    // a plane that is somewhere else now (the address reused after the old one streamed out and its exit
    // was never seen). A pause menu stops every Update but moves nothing: the flight goes on, only
    // un-primed by the 100 ms rule below (the GetTickCount clock runs on through a pause).
    bool fresh = veh != g.veh || g.left;
    if (!fresh && now - g.lastT > 30000)
    {
        const float dx = here[0] - g.lastPos[0], dy = here[1] - g.lastPos[1], dz = here[2] - g.lastPos[2];
        fresh = !hereOk || !g.lastPosOk || dx * dx + dy * dy + dz * dz > 30.0f * 30.0f;
    }
    if (fresh)
    {
        g = Ctx{};
        plane::Reset(g.st);
        g.veh = veh; g.mode = kGround; g.logBoard = true; g.boardT = now;
        g.last.wheelBrake = 1.0f;
    }
    if (now - g.lastT > 100) g.st.primed = 0;                           // a gap: last write isn't last frame's
    g.lastT = now;
    if (hereOk) { for (int k = 0; k < 3; ++k) g.lastPos[k] = here[k]; g.lastPosOk = true; }

    const int32_t lod = At<int32_t>(veh, eng::VEH_SIMLOD);
    void* src = At<void*>(veh, eng::VEH_SRCCTRL);
    int hc = 1, hm = 1;
    const bool hasHealth = vd::Health(veh, hc, hm);
    const bool air = g.mode == kAir;
    const bool ctrlOn = PtrOk(src) && At<uint8_t>(src, eng::CTRL_ACTIVE);
    const bool frozen = PtrOk(body0) && (At<uint8_t>(body0, BODY_FLAGS) & 0x20);
    // Reduced simulation (sim LOD 5: far from / not seen by a camera - player 2's helicopter was seen
    // there, heli.h): the car part of Update - suspension, wheel joints, steering - runs only at LOD 1
    // [P: Notes\22 §1], so on the wheels there is no ground. In the air nothing of it is needed: fly on
    // (velocity writes). Holding it at 1 (force_lod1, OFF by default) is an experiment: +0xEF8 belongs to
    // the life-state machine 0x1404BEE70, whose park / unpark freeze the body or snap the velocity
    // [P: Notes\23 #15] - so only at LOD 5, on the wheels or within 5 u of the last ground contact (the
    // touchdown needs the wheel rays), never a frozen body, and off for the session the first time it
    // does not stick (Post).
    const bool nearGround = !air || g.hEst < 5.0f;
    const bool lodHold = lod == 5 && nearGround && cfg.airplane_force_lod1 && !g_lodOff && !frozen;
    bool refuse = true;
    if (!armed)                                         StandDown(0, "call patches missing (lcudrive is observe-only)");
    else if (!(At<uint8_t>(veh, eng::VEH_FLAGS4) & 1))  StandDown(1, "not spawned yet");
    else if (At<uint8_t>(veh, eng::VEH_FLAGS5) & 0xA0)
    {
        // airport.h unparks it in the population-manager pass right after boarding: a frame, no line
        if (now - g.boardT > 1000) StandDown(2, "parked");
    }
    else if (PtrOk(At<void*>(veh, eng::VEH_PATHFOLLOW)))  StandDown(3, "path follower (a script drives it)");
    else if (!ctrlOn && !air)                           StandDown(4, "no player control");   // in the air: flies on
    else if (hasHealth && hc < 1)                       StandDown(5, "destroyed");
    else if (lod == 10)                                 StandDown(6, "asleep (sim LOD 10)");
    else if (g.wet)                                     StandDown(7, "down in the water");
    else if (lod != 1 && !air && !lodHold)
    {
        char why[160];
        _snprintf_s(why, sizeof(why), _TRUNCATE, "sim LOD %d on the wheels (no wheel physics there)%s", lod,
                    lod != 5 ? "" : frozen ? " - its body is frozen" : g_lodOff ? " - the LOD hold did not stick this session"
                    : !cfg.airplane_force_lod1 ? " - to try holding it at 1, set [airplane] force_lod1 = 1 (Ctrl+Alt+R)" : "");
        StandDown(8, why);
    }
    else refuse = false;
    if (refuse) { g.st.primed = 0; return false; }
    if (!ctrlOn)
    {
        g_f.ctrlOff = true;
        if (!g.ctrlLogged) { g.ctrlLogged = true; Log(">>> airplane: controls inactive in the air - flying on (sticks centred%s)\n",
                                                      cfg.airplane_stick_direct ? " unless the controller is read directly" : ""); }
    }
    if (lod != 1 && lodHold)
    {
        At<int32_t>(veh, eng::VEH_SIMLOD) = 1;
        g_f.lodHeld = true;
        if (!g.lodLogged) { g.lodLogged = true; Log(">>> airplane: player %d's plane at sim LOD %d %s - held at 1 (force_lod1)\n",
                                                    player, lod, air ? "near the ground" : "on the wheels"); }
    }
    else if (lod != 1 && air && !g.lodAirLogged)
    {
        g.lodAirLogged = true;
        Log(">>> airplane: player %d's plane at sim LOD %d in the air - flying on; the wheels only work at LOD 1\n", player, lod);
    }
    if (!mdl::Snapshot(veh, g_f.s)) { StandDown(9, "no snapshot (body / wheels unreadable)"); return false; }
    for (int k = 0; k < 3; ++k) { lv0[k] = g_f.s.linvel[k]; av0[k] = g_f.s.angvel[k]; }
    if (g.logBoard) { g.logBoard = false; LogBoard(veh, player, g_f.s); g.contactY = g_f.s.com[1]; }

    // this Update's plan, from last frame's model output
    const bool ground = g.mode != kAir;
    g_f.noseDeg = ground && g.lastValid ? cfg.airplane_steer_sign * g.last.noseWheelDeg : 0.0f;
    g_f.wheelBrake = g.lastValid ? g.last.wheelBrake : 1.0f;
    g_f.gearUp = g.lastValid ? Clampf(g.last.gearUp, 0.0f, 1.0f) : 0.0f;
    g_f.force = ground && g.lastValid && plane::Fin3(g.last.acc);
    for (int k = 0; k < 3; ++k) g_f.acc[k] = g_f.force ? g.last.acc[k] : 0.0f;
    g_f.steerRan = false;
    g_f.cfg = &cfg;
    g_f.ok = true;
    return true;
}

// ---------------------------------------------------------------------------------------------
//  The redirected calls (inside Update)
// ---------------------------------------------------------------------------------------------
static void ApplyGroundForce()
{
    const mdl::CarSnap& s = g_f.s;
    if (!g_f.force || !mdl::g_fn.applyForce || !(s.mass > 1.0f)) return;
    const float F[3] = { s.mass * g_f.acc[0], s.mass * g_f.acc[1], s.mass * g_f.acc[2] };
    if (!plane::Fin3(F)) return;
    mdl::g_fn.applyForce(s.body, F, s.com);
    g_f.force = false;                                                   // once per Update
}

// In place of UpdateSteering: the nose wheel, and on the wheels the aerodynamic force.
static bool Steer(void* veh)
{
    if (!g_f.ok) return false;
    const mdl::CarSnap& s = g_f.s;
    const float deg = Clampf(g_f.noseDeg, -60.0f, 60.0f);
    At<float>(veh, eng::VEH_STEER_DEG) = deg;
    At<int32_t>(veh, eng::VEH_STEER_BAM) = static_cast<int32_t>(deg * (65536.0f / 360.0f));
    for (int i = 0; i < s.nw; ++i) mdl::g_fn.wheelSetSteer(s.wh[i].w, s.wh[i].steerable > 0.0f ? deg : 0.0f);
    ApplyGroundForce();
    g_f.steerRan = true;
    return true;
}

// In place of WheelConstraint (the call site is already patched, lcudrive.cpp g_patches). This wheel's
// suspension ran just before: its ray, contact and spring force are this frame's.
static bool Wheel(void* wheel, const Config& cfg)
{
    if (!g_f.ok) return false;
    const mdl::CarSnap& s = g_f.s;
    void* joint = At<void*>(wheel, eng::WHL_JOINT);
    if (!PtrOk(joint)) return true;                                      // the engine's own early-out
    int i = -1;
    for (int k = 0; k < s.nw; ++k) if (s.wh[k].w == wheel) { i = k; break; }
    if (i < 0) return false;                                             // not in the snapshot: stock
    const mdl::WheelSnap& ws = s.wh[i];

    float P[3], n[3];
    for (int k = 0; k < 3; ++k)
    {
        P[k] = At<float>(wheel, eng::WHL_CONTACT + 4 * k);
        n[k] = At<float>(wheel, eng::WHL_NORMAL + 4 * k);
    }
    const bool ground   = At<uint8_t>(wheel, eng::WHL_AIRBORNE) == 0;
    float springF = At<float>(wheel, eng::WHL_SPRINGF);
    // The gear is (partly) up: this wheel's suspension force, just applied by the engine, is taken off
    // again by that share - the same call at the same point with the opposite sign - and the tyre rows
    // below get only the load that is left. Fully up: the wheel carries nothing; the hull lands.
    if (g_f.gearUp > 0.0f && ground && IsFin(springF) && springF != 0.0f && mdl::g_fn.applyForce)
    {
        float dir[3];
        for (int k = 0; k < 3; ++k) dir[k] = At<float>(wheel, WHL_SPRINGDIR + 4 * k);
        const float F[3] = { -springF * g_f.gearUp * dir[0], -springF * g_f.gearUp * dir[1], -springF * g_f.gearUp * dir[2] };
        if (plane::Fin3(F) && plane::Fin3(P) && std::fabs(F[0]) + std::fabs(F[1]) + std::fabs(F[2]) < 1.0e7f)
        {
            mdl::g_fn.applyForce(s.body, F, P);
            springF *= 1.0f - g_f.gearUp;
        }
    }
    const float steer   = At<float>(wheel, eng::WHL_STEERDEG) * mdl::kDeg;
    for (int k = 0; k < 3; ++k)
    {
        At<float>(joint, eng::JNT_POINT + 4 * k)  = P[k];
        At<float>(joint, eng::JNT_NORMAL + 4 * k) = n[k];
    }
    // heading: forward turned by the wheel's angle about up (model.h's construction), onto the ground
    float axis[4] = {};
    for (int k = 0; k < 3; ++k) axis[k] = std::cos(steer) * s.fwd[k] - std::sin(steer) * s.right[k];
    const bool nOk = mdl::Norm(n);
    if (nOk) { const float an = mdl::Dot(axis, n); for (int k = 0; k < 3; ++k) axis[k] -= an * n[k]; }
    const bool aOk = mdl::Norm(axis);
    if (!aOk) for (int k = 0; k < 3; ++k) axis[k] = s.fwd[k];
    if (!ground || !nOk || !aOk || !IsFin(springF))
    {
        // airborne: inert rows, the joint stays (model.h's pattern; destroying it is never ours)
        mdl::g_fn.jointLimits(joint, axis, 0.0f, 0.0f, 0.0f, 0.0f);
        mdl::g_fn.jointSoft(joint, 0.0f, 0.0f);
        mdl::g_fn.jointTarget(joint, 0.0f, 0.0f);
        At<uint8_t>(joint, eng::JNT_ENABLED) = 1;
        return true;
    }
    // the surface's grip (model.h's lookup)
    float mu = cfg.airplane_side_grip;
    {
        const int sid = At<int>(wheel, eng::WHL_SURFACE);
        if (sid >= 0 && sid < 64)
        {
            void* sf = At<void*>(s.veh, eng::VEH_SURFACES + 8 * sid);
            void* sf2 = PtrOk(sf) ? At<void*>(sf, 0x10) : nullptr;
            if (PtrOk(sf2)) { const float gr = At<float>(sf2, 0x28); if (IsFin(gr) && gr > 0.0f) mu *= Clampf(gr, 0.3f, 1.2f); }
        }
    }
    float pv[3] = {};
    mdl::g_fn.pointVel(s.body, P, pv);
    const float vLong = mdl::Dot(pv, axis);
    const plane::WheelRows w = plane::GearWheelRows(springF, mu, ws.steerable > 0.0f ? 0.13f : 0.08f, vLong,
                                                    cfg.airplane_rolling, cfg.airplane_brake_mu, g_f.wheelBrake, ws.brake);
    mdl::g_fn.jointLimits(joint, axis, w.latCap, w.longCap, w.latCap, w.longCap);
    mdl::g_fn.jointSoft(joint, w.latSoft, 0.0f);
    mdl::g_fn.jointTarget(joint, 0.0f, 0.0f);
    At<uint8_t>(joint, eng::JNT_ENABLED) = 1;
    return true;
}

// ---------------------------------------------------------------------------------------------
//  Post: after ApiVehicleCar::Update of the player's plane
// ---------------------------------------------------------------------------------------------
struct Wheels { int down; float wow; float gn[3]; float pivot[3]; bool pivotOk; };

static Wheels ReadWheels(const mdl::CarSnap& s, float grav)
{
    Wheels r = {};
    float zMax = -1e9f, sumF = 0.0f;
    for (int i = 0; i < s.nw; ++i) zMax = std::fmax(zMax, s.wh[i].rest[2]);
    float piv[3] = {}, piv2[3] = {};
    int nPiv = 0, nPiv2 = 0;
    for (int i = 0; i < s.nw; ++i)
    {
        void* w = s.wh[i].w;
        if (At<uint8_t>(w, eng::WHL_AIRBORNE) != 0) continue;
        ++r.down;
        const float f = At<float>(w, eng::WHL_SPRINGF);
        if (IsFin(f) && f > 0.0f) sumF += f;
        float c[3];
        for (int k = 0; k < 3; ++k) { r.gn[k] += At<float>(w, eng::WHL_NORMAL + 4 * k); c[k] = At<float>(w, eng::WHL_CONTACT + 4 * k); }
        if (s.wh[i].rest[2] >= zMax - 0.25f) { for (int k = 0; k < 3; ++k) piv[k] += c[k]; ++nPiv; }        // the rearmost pair
        if (!(s.wh[i].steerable > 0.0f)) { for (int k = 0; k < 3; ++k) piv2[k] += c[k]; ++nPiv2; }           // any main wheel
    }
    if (!plane::Norm(r.gn) || r.gn[1] < 0.5f) { r.gn[0] = 0.0f; r.gn[1] = 1.0f; r.gn[2] = 0.0f; }
    r.wow = s.mass > 1.0f ? sumF / (s.mass * grav) : 0.0f;
    if (nPiv)       { for (int k = 0; k < 3; ++k) r.pivot[k] = piv[k] / nPiv;   r.pivotOk = true; }
    else if (nPiv2) { for (int k = 0; k < 3; ++k) r.pivot[k] = piv2[k] / nPiv2; r.pivotOk = true; }
    return r;
}

static void Telemetry(void* veh, int player, const plane::Input& in, const plane::Output& o, const float pos[3])
{
    TelemetryHeader();
    g_t += in.dt;
    mdl::TelemetryLine("A,%.4f,%d,%d,%d,%d,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.3f,%.3f,%.3f,%.2f,%.2f,%.2f,"
                       "%.1f,%.1f,%.1f,%.2f,%.2f,%.1f,%.3f,%.2f,%d,%.4f,%.2f\n",
        g_t, player, At<int32_t>(veh, eng::VEH_SIMLOD), g.mode, in.wheelsDown, in.wow, in.cTurn, in.cFwd, in.throttle, in.brake,
        o.lever, o.spool, pos[0], pos[1], pos[2], in.lv[0], in.lv[1], in.lv[2], o.airspeed, o.alphaDeg, o.betaDeg,
        o.bankDeg, o.pitchDeg, o.gammaDeg, o.liftG, o.stall, g_f.noseDeg, o.rotateRate, g.hEst, (int)o.crash, in.dt, o.gearUp);
}

static void EngineRpm(void* veh, float spool)
{
    void* d = At<void*>(veh, eng::VEH_DRIVETRAIN);
    if (!PtrOk(d)) return;
    const float rpm = 1500.0f + 4500.0f * Clampf(spool, 0.0f, 1.0f);   // silent unless the record names engine sounds [P: none]
    At<float>(d, eng::DRV_RPM) = rpm; At<float>(d, eng::DRV_RPM_LAG) = rpm; At<float>(d, eng::DRV_RPM_SFX) = rpm;
}

static void Post(void* veh, int player, const float lv0[3], const float av0[3], const Config& cfg, bool telemetry)
{
    if (!g_f.ok) return;
    g_f.ok = false;
    const mdl::CarSnap& s = g_f.s;
    void* inst = s.inst;
    void* body = s.body;
    const uint32_t now = GetTickCount();
    // The Update may have changed what Pre saw: the game destroys / parks / despawns a vehicle from
    // inside it [P: vehdamage.h], and whether the instance or body is swapped then is [U] - so nothing is
    // written unless it is still the same live, unparked plane (heli.h / plane.h re-check the same).
    {
        void* inst2 = At<void*>(veh, eng::VEH_INSTANCE);
        void* body2 = PtrOk(inst2) ? At<void*>(inst2, eng::INST_BODY) : nullptr;
        if (inst2 != inst || body2 != body || !(At<uint8_t>(veh, eng::VEH_FLAGS4) & 1) || (At<uint8_t>(veh, eng::VEH_FLAGS5) & 0xA0))
        {
            g.st.primed = 0; g.lastValid = false;
            Mask(veh, false);
            return;
        }
    }
    if (g_f.lodHeld)
    {
        const int32_t lodNow = At<int32_t>(veh, eng::VEH_SIMLOD);
        const bool frozenNow = (At<uint8_t>(body, BODY_FLAGS) & 0x20) != 0;
        if ((lodNow != 1 || frozenNow) && !g_lodOff)
        {
            g_lodOff = true;
            Log(">>> airplane: the sim LOD hold did not stick (lod %d after the update, body %s) - force_lod1 is off for this "
                "session\n", lodNow, frozenNow ? "frozen" : "free");
        }
    }
    plane::Input in{};
    in.dt = At<float>(veh, eng::VEH_DT);
    Rows(inst, in.right, in.up, in.back);
    for (int k = 0; k < 3; ++k) { in.lv[k] = lv0[k]; in.av[k] = av0[k]; }
    in.gravity = At<float>(veh, eng::VEH_GRAVITY);
    const float gUse = (IsFin(in.gravity) && in.gravity > 1.0f && in.gravity < 50.0f) ? in.gravity : 9.8f;
    const Wheels wh = ReadWheels(s, gUse);
    in.hag = 3.0e38f;                                                    // a car has no height probe [P]
    for (int k = 0; k < 3; ++k) in.gn[k] = wh.gn[k];
    in.wheelsDown = wh.down;
    in.wow = wh.wow;
    in.occupied = 1;
    Controls(veh, cfg, in);

    const float pos[3] = { s.com[0], s.com[1], s.com[2] };
    if (wh.down) g.contactY = pos[1];
    g.hEst = pos[1] - g.contactY;

    const plane::Params P = ParamsFor(cfg);
    const int prevMode = g.mode;
    // ---- the gear lever: the horn, in the air only (plane::GearSwitch) ----
    {
        const float sp0 = std::sqrt(mdl::Dot(lv0, lv0));
        const bool airborne = prevMode == kAir;
        const bool low = plane::GearWarn(g.gear.up != 0, airborne, g.hEst, lv0[1], cfg.airplane_gear_warn_height > 0.0f
                                         ? cfg.airplane_gear_warn_height : 6.0f);
        const int ev = plane::GearSwitch(g.gear, cfg.airplane_gear_retract != 0, HornHeld(veh), airborne,
                                         prevMode == kGround && sp0 < 1.0f, low && cfg.airplane_gear_auto_down != 0);
        if (ev == plane::GEAR_UP)   { Log(">>> airplane: GEAR UP (player %d) at %.1f u/s, %.1f u up\n", player, sp0, g.hEst); InterlockedExchange(&g_gearBeep, 1); }
        if (ev == plane::GEAR_DOWN) { Log(">>> airplane: GEAR DOWN (player %d) at %.1f u/s, %.1f u up\n", player, sp0, g.hEst); InterlockedExchange(&g_gearBeep, 2); }
        if (ev == plane::GEAR_DOWN_AUTO) { Log(">>> airplane: GEAR DOWN by itself (gear_auto_down) %.1f u up\n", g.hEst); InterlockedExchange(&g_gearBeep, 2); }
        if (ev == plane::GEAR_DOWN_STOPPED) { Log(">>> airplane: stopped on the belly - the gear comes down again\n"); InterlockedExchange(&g_gearBeep, 2); }
        // "gear!": up, low, descending - fast low beeps (and one line per approach)
        g.gearWarnT -= in.dt;
        if (g.gear.up && plane::GearWarn(true, airborne, g.hEst, lv0[1], cfg.airplane_gear_warn_height))
        {
            if (!g.gearWarned) { g.gearWarned = true; Log(">>> airplane: GEAR IS UP %.1f u over the ground, descending - horn = gear down\n", g.hEst); }
            if (g.gearWarnT <= 0.0f) { g.gearWarnT = 0.7f; InterlockedExchange(&g_gearBeep, 3); }
        }
        else if (!g.gear.up || g.hEst > cfg.airplane_gear_warn_height + 3.0f) g.gearWarned = false;
        in.gearUp = g.gear.up;
    }
    plane::Output out;
    plane::Update(P, g.st, in, out);
    if (!out.valid) { g.lastValid = false; return; }                    // the engine's frame stands
    g.mode = out.writeLv ? kAir : kGround;
    const float speed = std::sqrt(mdl::Dot(lv0, lv0));
    const float dt = in.dt;

    // ---- transitions, and the lines the test plan reads ----
    if (g.mode == kGround)
    {
        if (speed < 0.5f) { g.rolling = false; g.rollT = 0.0f; }
        else if (!g.rolling) { g.rolling = true; g.rollT = 0.0f; for (int k = 0; k < 3; ++k) g.rollStart[k] = pos[k]; }
        else g.rollT += dt;
        if (g.tdTrack && speed < 0.5f)
        {
            const float dx = pos[0] - g.tdPos[0], dz = pos[2] - g.tdPos[2];
            Log(">>> airplane: stopped %.0f u after touchdown (x %.1f z %.1f)\n", std::sqrt(dx * dx + dz * dz), pos[0], pos[2]);
            g.tdTrack = false;
        }
    }
    if (prevMode != kAir && g.mode == kAir)
    {
        const float dx = pos[0] - g.rollStart[0], dz = pos[2] - g.rollStart[2];
        Log(">>> airplane: LIFT-OFF at %.1f u/s after %.0f u (%.1f s), pitch %.1f, x %.1f (quay edge 209.6)\n", out.airspeed,
            g.rolling ? std::sqrt(dx * dx + dz * dz) : 0.0f, g.rollT, out.pitchDeg, pos[0]);
        g.rolling = false; g.tdTrack = false;
    }

    // ---- crashes: the touchdown checks (the collisions are the game's own car damage) ----
    bool destroyed = false;
    {
        g.cool -= dt;
        const uint8_t why = g.st.crash;
        if (out.touchdown)
        {
            int n = 0, h0 = -1, h1 = -1, hm = -1;
            const bool fatal = cfg.airplane_crash_damage && plane::FatalCrash(why, g.st.impactSpeed, out.sink, speed, P);
            if (fatal || (why != plane::CRASH_NONE && why != plane::CRASH_IMPACT && cfg.airplane_crash_damage))
            {
                n = plane::LandingDamage(why, g.st.impactSpeed, out.sink, P);
                if (fatal) { int hc = 0, hx = 0; n = vd::Health(veh, hc, hx) && hc > 0 ? hc : 99; g.cool = 0.0f; }
                if (n > 0 && g.cool <= 0.0f)
                {
                    g.cool = 0.5f;
                    vd::Health(veh, h0, hm);
                    h1 = vd::Damage(veh, n);
                    destroyed = h1 == 0;
                }
                else n = 0;
            }
            // (an impact in the step before the touchdown frame latches CRASH_IMPACT first: a belly or very
            // steep arrival - the game's own collision damage has already dealt with it)
            static const char* kWhy[] = { "ok", "hit hard (the game's collision damage)", "landed too hard", "a wingtip hit",
                                          "nose / tail first", "BELLY LANDING (the gear was up)" };
            char extra[48] = "";
            if (why == plane::CRASH_IMPACT) _snprintf_s(extra, sizeof(extra), _TRUNCATE, " (dv %.1f u/s)", g.st.impactSpeed);
            Log(">>> airplane: TOUCHDOWN at %.1f u/s, sink %.2f, bank %.1f, pitch %.1f - %s%s: %d damage, health %d -> %d%s\n",
                speed, out.sink, out.bankDeg, out.pitchDeg, kWhy[why < 6 ? why : 0], extra, n, h0, h1,
                h1 == 0 ? " - DESTROYED" : (n > 0 && h1 < 0) ? " (no damage: off or invulnerable)" : "");
            for (int k = 0; k < 3; ++k) g.tdPos[k] = pos[k];
            g.tdTrack = true;
        }
        else if (why == plane::CRASH_IMPACT && cfg.airplane_crash_damage && plane::FatalCrash(why, g.st.impactSpeed, 0.0f, speed, P))
        {
            int hc = 0, hx = 0;
            const int n = vd::Health(veh, hc, hx) && hc > 0 ? hc : 99;
            const int h1 = vd::Damage(veh, n);
            destroyed = h1 == 0;
            Log(">>> airplane: CRASHED at full tilt (dv %.1f u/s, destroy at %.0f) - health %d -> %d%s\n", g.st.impactSpeed,
                P.destroySpeed, hc, h1, h1 == 0 ? " - DESTROYED" : " (no damage: off or invulnerable)");
        }
        else if (why == plane::CRASH_IMPACT)
        {
            g.impactLogT -= dt;
            if (g.impactLogT <= 0.0f)
            {
                g.impactLogT = 2.0f;
                Log(">>> airplane: hit something in the air (dv %.1f u/s) - the game's own collision damage applies\n", g.st.impactSpeed);
            }
        }
        else g.impactLogT -= dt;
        g.st.crash = plane::CRASH_NONE;                                 // re-armed: the next one counts too
    }

    // ---- the writes ----
    if (destroyed)
    {
        // our own touchdown damage just destroyed it: the game's destruction owns it now - no velocity,
        // no RPM, the boundary bit back
        Mask(veh, false);
        g.st.primed = 0; g.lastValid = false;
        Log(">>> airplane: destroyed in the crash - the game's wreck from here\n");
        return;
    }
    Mask(veh, g.mode == kAir);                                         // the boundary-only blockers: flying only
    if (g.mode == kAir)
    {
        for (int k = 0; k < 3; ++k)
        {
            At<float>(body, eng::BODY_LINVEL + 4 * k) = out.lv[k];
            At<float>(body, eng::BODY_ANGVEL + 4 * k) = out.av[k];
        }
        At<float>(veh, VEH_FLIPT) = 0.0f;
        g.airSeenT = now;
    }
    else
    {
        float w[3], v[3];
        for (int k = 0; k < 3; ++k) { w[k] = At<float>(body, eng::BODY_ANGVEL + 4 * k); v[k] = At<float>(body, eng::BODY_LINVEL + 4 * k); }
        bool writeW = false, writeV = false;
        if (At<uint8_t>(veh, eng::VEH_FLAGS7) & 1)                        // FlightAlign ran (all wheels were up)
        {
            for (int k = 0; k < 3; ++k) w[k] = av0[k];
            writeW = true;
        }
        if (out.rotateRate != 0.0f && wh.pivotOk)
        {
            float r[3], f[3], u[3];
            if (PitchAxis(inst, r, f, u))
            {
                plane::GearRotate(r, out.rotateRate, s.com, wh.pivot, w, v);
                writeW = writeV = true;
            }
        }
        if (writeW && plane::Fin3(w)) for (int k = 0; k < 3; ++k) At<float>(body, eng::BODY_ANGVEL + 4 * k) = w[k];
        if (writeV && plane::Fin3(v)) for (int k = 0; k < 3; ++k) At<float>(body, eng::BODY_LINVEL + 4 * k) = v[k];
        if (!g_f.steerRan && g_f.force)
        {
            // the steer hook did not run this Update: the force after it - still integrated by the
            // solver [I: Notes\20 §4]
            ApplyGroundForce();
            if (!g.fallbackLogged) { g.fallbackLogged = true; Log(">>> airplane: the steer hook did not run - ground forces applied after the update\n"); }
        }
    }
    if (out.backing && !(g.lastValid && g.last.backing))
        Log(">>> airplane: player %d taxiing backwards (stopped, lever idle, brake held; up to %.1f u/s - taxi_reverse_speed)\n",
            player, cfg.airplane_taxi_reverse_speed);
    g.last = out;
    g.lastValid = true;

    // ---- the nose-wheel watchdog (log only; steer_sign stays as configured) ----
    {
        float f[3] = { -in.back[0], -in.back[1], -in.back[2] }, u[3] = { in.up[0], in.up[1], in.up[2] };
        plane::Norm(f); plane::Norm(u);
        const float vl = mdl::Dot(lv0, f), yaw = mdl::Dot(av0, u);
        const float turn = plane::Dead(plane::Axis(in.cTurn), 0.08f) * P.turnSign;
        if (plane::SteerWatchStep(g.watch, g.mode == kGround && wh.down > 0, vl, g_f.noseDeg, turn, yaw, dt))
            Log(">>> airplane: nose-wheel steering looks reversed (yaw %+.2f for %+.1f deg at %.1f u/s) - set [airplane] "
                "steer_sign = %d\n", yaw, g_f.noseDeg, vl, cfg.airplane_steer_sign < 0.0f ? 1 : -1);
    }

    // ---- water: [U] what the game does with a car-class vehicle in the sea; we stand down ----
    if (pos[1] < 0.8f && wh.down == 0) g.waterT += dt; else g.waterT = 0.0f;
    if (g.waterT > 1.0f && !g.wet) { g.wet = true; Log(">>> airplane: down in the water (y %.2f) - it stands down\n", pos[1]); }

    // ---- stall: the line and the horn (beeps are the primary feedback) ----
    g.stallLogT -= dt;
    if (g.mode == kAir && out.stall > 0.3f)
    {
        if (g.stallLogT <= 0.0f) { g.stallLogT = 3.0f; Log(">>> airplane: STALL - alpha %.1f at %.1f u/s\n", out.alphaDeg, out.airspeed); }
        if (cfg.airplane_stall_horn && now - g_hornT > 1500) { g_hornT = now; InterlockedExchange(&g_horn, 1); }
    }
    if (cfg.airplane_engine_rpm) EngineRpm(veh, out.spool);
    if (telemetry) Telemetry(veh, player, in, out, pos);
}

// ---------------------------------------------------------------------------------------------
//  The plane nobody flies: the one this player just left in the air glides down, throttle shut
// ---------------------------------------------------------------------------------------------
static bool UnoccupiedPre(void* veh, const Config& cfg, float lv0[3], float av0[3])
{
    g_f.ok = false;
    if (veh != g.veh || (g.left && !g.glide)) return false;
    if (!g.left)
    {
        // The player's context is live (CarThunk swapped player 2's in). Their plane updates without
        // them: they got out (the stock car exit). Flying when they did [U: whether the game lets you out
        // of a car in the air at all] - the parachute (chute.h, character-side) and the glide. (2 s, not
        // the spec's 0.3: a margin for an exit animation the length of which is [U]; Pre flies on through
        // it in the air, with the controls centred, until the vehicle ref lets go.)
        void* cv = CtxVehicle();
        if (cv == veh) return false;
        g.left = true;
        uint8_t* ch = CtxChar();
        const float sp = std::sqrt(mdl::Dot(g.last.lv, g.last.lv));
        if (g.mode == kAir && GetTickCount() - g.airSeenT < 2000)
        {
            if (PtrOk(ch) && !cv && g.hEst > 2.0f)
            {
                Log(">>> airplane: player %d bailed out %.1f u up at %.1f u/s%s\n", g_ctx + 1, g.hEst, sp,
                    cfg.airplane_bail_parachute ? " - parachute armed" : "");
                if (cfg.airplane_bail_parachute) chute::OnBailOut(ch, g.hEst);
            }
            if (cfg.airplane_abandon_glide && g.hEst > 0.5f) { g.glide = true; g.glideT = 0.0f; g.st.primed = 0; }
        }
    }
    if (!g.glide) return false;
    int hc = 1, hm = 1;
    const bool dead = vd::Health(veh, hc, hm) && hc < 1;
    if (dead || !(At<uint8_t>(veh, eng::VEH_FLAGS4) & 1) || (At<uint8_t>(veh, eng::VEH_FLAGS5) & 0xA0) ||
        At<int32_t>(veh, eng::VEH_SIMLOD) == 10 || CtxVehicle() == veh)
    { g.glide = false; return false; }                                  // (the caller gives the boundary bit back)
    if (!mdl::Snapshot(veh, g_f.s)) { g.glide = false; return false; }
    for (int k = 0; k < 3; ++k) { lv0[k] = g_f.s.linvel[k]; av0[k] = g_f.s.angvel[k]; }
    g_f.noseDeg = 0.0f; g_f.wheelBrake = 0.0f; g_f.force = false; g_f.steerRan = false; g_f.cfg = &cfg;
    g_f.gearUp = 0.0f;                                                  // (the empty plane's gear comes down: in.gearUp = 0)
    g_f.ok = true;
    return true;
}

static void UnoccupiedPost(void* veh, const float lv0[3], const float av0[3], const Config& cfg, bool telemetry)
{
    if (!g_f.ok) return;
    g_f.ok = false;
    const mdl::CarSnap& s = g_f.s;
    {
        // the same re-check as Post: nothing is written into a plane the Update destroyed / parked / swapped
        void* inst2 = At<void*>(veh, eng::VEH_INSTANCE);
        void* body2 = PtrOk(inst2) ? At<void*>(inst2, eng::INST_BODY) : nullptr;
        int hc = 1, hm = 1;
        if (inst2 != s.inst || body2 != s.body || !(At<uint8_t>(veh, eng::VEH_FLAGS4) & 1) ||
            (At<uint8_t>(veh, eng::VEH_FLAGS5) & 0xA0) || (vd::Health(veh, hc, hm) && hc < 1))
        {
            g.glide = false;
            Mask(veh, false);
            Log(">>> airplane: the empty plane is down - stock (destroyed or put away)\n");
            return;
        }
    }
    plane::Input in{};
    in.dt = At<float>(veh, eng::VEH_DT);
    Rows(s.inst, in.right, in.up, in.back);
    for (int k = 0; k < 3; ++k) { in.lv[k] = lv0[k]; in.av[k] = av0[k]; }
    in.gravity = At<float>(veh, eng::VEH_GRAVITY);
    const Wheels wh = ReadWheels(s, 9.8f);
    const float speed = std::sqrt(mdl::Dot(lv0, lv0));
    const bool first = g.glideT == 0.0f;
    g.glideT += IsFin(in.dt) ? in.dt : 0.0f;
    if (wh.down > 0 || g.glideT > 30.0f)
    {
        g.glide = false;
        Mask(veh, false);
        Log(">>> airplane: the empty plane is down - stock (%.1f u/s, %.1f s)\n", speed, g.glideT);
        return;
    }
    if (first) Log(">>> airplane: the empty plane glides (%.1f u/s, %.1f u up)\n", speed, g.hEst);
    in.hag = 3.0e38f; in.gn[1] = 1.0f; in.wheelsDown = 0; in.wow = 0.0f; in.occupied = 0;
    plane::Output out;
    plane::Update(ParamsFor(cfg), g.st, in, out);
    if (!out.valid || !out.writeLv) return;
    Mask(veh, true);
    for (int k = 0; k < 3; ++k)
    {
        At<float>(s.body, eng::BODY_LINVEL + 4 * k) = out.lv[k];
        At<float>(s.body, eng::BODY_ANGVEL + 4 * k) = out.av[k];
    }
    At<float>(veh, VEH_FLIPT) = 0.0f;
    g.st.crash = plane::CRASH_NONE;
    if (telemetry) { const float pos[3] = { s.com[0], s.com[1], s.com[2] }; Telemetry(veh, 0, in, out, pos); }
}

// ---------------------------------------------------------------------------------------------
//  What the wheel joints show: the gear going up (Elifant), the propellers turning (ThunderRoc).
//  The engine, per wheel, inside Update at sim LOD 1 only:   wheel+0x50 = [wheel+0x10 world] x inverse(body)
//  [P: 0x1404DA553..0x1404DA593]; and for every drawn vehicle, in the post-physics pass (phase 0x29, after
//  all Updates and the solver), ApiVehicleJointModifier 0x1404DA160 poses the wheel joint as
//      spin(local X, wheel+0x128 x wheel+0x1A8)  x  steer(wheel+0x130)  x  [wheel+0x50]  x  [veh+0x210]
//  (rotation + position; a scale would be lost in 0x140465930's quaternion). wheel+0x50 and +0x128 have
//  no other reader in the vehicle code [I: gear_work\sweep.py over 0x1404A0000..0x1404F8000].
//  So, after the Update of that vehicle (the engine's own write site - never another thread's moment):
//    the engine's matrix is kept as `base` whenever it differs from what we wrote last (= the engine
//    refreshed it; at LOD 5 it does not, and the pose is still drawn), ours is written over it, and it is
//    given back - only if ours is still there - when the gear is down again, the feature is switched
//    off, the plane leaves airplane.h's hands, on the unhook, and after a fault (`[`: the gear travels down).
//  One record per vehicle (two players, two planes); everything by the vehicle's own wheel list, never a
//  stored pointer dereferenced.
// ---------------------------------------------------------------------------------------------
static const uintptr_t WHL_VISMTX  = 0x50;    // float[16]: the wheel in the vehicle's space; row 3 (+0x80) = position
static const uintptr_t WHL_VISSPIN = 0x128;   // float: the drawn wheel's roll; angle = this x wheel+0x1A8 (1 / radius)
static const uintptr_t WDEF_JOINT  = 0x10;    // char[0x40]: "Wheel joint", the joint the wheel mesh hangs on
static const uintptr_t WDEF_STEERVIS = 0x114; // byte: "SteeringVisible" - the joint turns with wheel+0x130 [P: 0x1404DA28B]
                                              // [P: 0x1404D8B44 looks it up; 0x1404F0724 copies it as a string]
static const uintptr_t VEH_VISBLEND = 0xF14;  // float 0..1: while inside (0, 1) the joints blend from the rest pose
                                              // to wheel+0x50 (0.5 / s after a wake) [P: 0x1404BFAA0, 0x1404DA36A]

struct VisWheel { void* w; float base[16]; float wrote[16]; };
struct VisRec
{
    void*    veh;                 // nullptr = free
    void*    def;
    int      kind;                // plane::WHEELVIS_GEAR / _PROPS / _PARTS
    float    vis;                 // the drawn gear, 0 = down .. 1 = up
    float    rps, angle;          // the propellers
    int      n;
    uint32_t seen;                // GetTickCount of its last Update
    VisWheel wh[6];
    bool     moved;               // (log) something of ours is in the wheels
};
static VisRec g_vis[4] = {};

static bool AnyWheelVis() { for (const VisRec& r : g_vis) if (r.veh) return true; return false; }
static void WheelVisForget() { for (VisRec& r : g_vis) r = VisRec{}; }   // after a fault inside it: no more writes

static VisRec* VisFind(void* veh) { for (VisRec& r : g_vis) if (r.veh == veh) return &r; return nullptr; }

// The engine's pose back into every wheel that still holds ours. From inside that vehicle's Update.
static void WheelVisRestore(VisRec& r, void* veh, const char* why)
{
    int back = 0, n = 0;
    void* w = At<void*>(veh, eng::VEH_WHEELS);
    for (int i = 0; i < 8 && PtrOk(w); ++i, w = At<void*>(w, eng::WHL_NEXT))
        for (int j = 0; j < r.n; ++j)
        {
            if (r.wh[j].w != w) continue;
            ++n;
            if (!memcmp(&At<float>(w, WHL_VISMTX), r.wh[j].wrote, sizeof(r.wh[j].wrote)))
            { memcpy(&At<float>(w, WHL_VISMTX), r.wh[j].base, sizeof(r.wh[j].base)); ++back; }
        }
    if (r.moved)
        Log(">>> airplane: wheel visuals of %p released (%s) - %d of %d wheel poses given back, the rest were the "
            "engine's already\n", veh, why, back, n);
    r = VisRec{};
}

// The skid byte of every wheel (wheel+0x1B0, written by 0x1404DA930 inside Update, read by the marks / smoke /
// sound passes after it [P: Notes\36]) from plane::PlaneSkid, for the plane a player is flying. caller: SEH.
static uint32_t g_skidTdTick = 0;
static void SkidWrite(void* veh, const Config& cfg, bool flying)
{
    if (!cfg.airplane_skid_fix || !flying || g.veh != veh || !g.lastValid) return;
    const uint32_t now = GetTickCount();
    if (g.last.touchdown) g_skidTdTick = now;
    const float since = g_skidTdTick ? (now - g_skidTdTick) * 0.001f : -1.0f;
    const float v = g.last.airspeed;
    const float lat = IsFin(g.last.betaDeg) ? v * std::sin(g.last.betaDeg * 0.0174533f) : 0.0f;
    void* w = At<void*>(veh, eng::VEH_WHEELS);
    for (int i = 0; i < 8 && PtrOk(w); ++i, w = At<void*>(w, eng::WHL_NEXT))
        At<uint8_t>(w, 0x1B0) = plane::PlaneSkid(At<uint8_t>(w, eng::WHL_AIRBORNE) == 0, v, lat, g.last.wheelBrake, since) ? 1 : 0;
}

struct VisIn
{
    bool  release;                // everything back now (a fault, the unhook, off the list, [airplane] enable = 0)
    bool  flying;                 // airplane.h ran this Update for the player in it (g is theirs, g.last is this frame's)
    bool  occupied;               // a player is in it
};

// After ApiVehicleCar::Update of any vehicle in [airplane] models. caller: SEH (lcudrive.cpp RunWheelVisual).
static void WheelVisual(void* veh, const Config& cfg, const VisIn& in)
{
    VisRec* r = VisFind(veh);
    void* def = At<void*>(veh, eng::VEH_DEF);
    if (r && r->def != def) { *r = VisRec{}; r = nullptr; }             // the address is another vehicle now: nothing written
    if (in.release) { if (r) WheelVisRestore(*r, veh, "airplane.h let go of it"); return; }
    int kind = r ? r->kind : plane::WHEELVIS_NONE;
    if (!r)
    {
        char name[64];
        if (!PtrOk(def) || !ReadModelName(def, name, sizeof(name))) return;
        kind = plane::WheelVisKind(name);
    }
    const bool mine = in.flying && g.veh == veh && g.lastValid;
    float target = 0.0f;                                                // the gear to draw
    if (kind == plane::WHEELVIS_GEAR)
    {
        if (!cfg.airplane_gear_visual) { if (r) WheelVisRestore(*r, veh, "gear_visual = 0"); return; }
        target = mine ? Clampf(g.last.gearUp, 0.0f, 1.0f) : 0.0f;
        if (!r && !(target > 0.0f)) return;                             // gear down, nothing of ours anywhere: the usual case
    }
    else if (kind != plane::WHEELVIS_PROPS && kind != plane::WHEELVIS_PARTS) return;   // anything else: never touched
    else target = (mine && cfg.airplane_gear_visual) ? Clampf(g.last.gearUp, 0.0f, 1.0f) : 0.0f;   // gear parts: always posed
    void* w0 = At<void*>(veh, eng::VEH_WHEELS);
    if (!PtrOk(w0)) { if (r) *r = VisRec{}; return; }
    if (!r)
    {
        for (VisRec& c : g_vis) if (!c.veh) { r = &c; break; }
        // none free: one whose vehicle has had no Update for 10 s is gone (streamed out) - forgotten, never written
        if (!r) for (VisRec& c : g_vis) if (GetTickCount() - c.seen > 10000) { r = &c; break; }
        if (!r) return;                                                 // four planes already: this one is drawn stock
        *r = VisRec{};
        r->veh = veh; r->def = def; r->kind = kind;
    }
    r->seen = GetTickCount();
    float dt = At<float>(veh, eng::VEH_DT);
    if (!IsFin(dt) || dt < 0.0f) dt = 0.0f;
    if (kind == plane::WHEELVIS_GEAR)
    {
        r->vis = plane::GearVisStep(r->vis, target, dt, cfg.airplane_gear_time);
        if (!(r->vis > 0.0f)) { WheelVisRestore(*r, veh, "the gear is down"); return; }
    }
    else
    {
        // the gear parts follow the gear like the Elifant's wheels (and come down by themselves when the
        // pilot is gone or gear_visual = 0); they stay posed with the gear down - the anchor is ours
        r->vis = plane::GearVisStep(r->vis, target, dt, cfg.airplane_gear_time);
        if (kind == plane::WHEELVIS_PROPS)
        {
        // nobody flying it: idle while a player sits in it, else it runs down
        const float want = !cfg.airplane_props ? 0.0f
            : plane::PropTargetRps(in.occupied, mine ? g.last.spool : 0.0f, mine ? g.last.airspeed : 0.0f, plane::Elifant().topSpeed,
                                   cfg.airplane_prop_idle, cfg.airplane_prop_max);
        r->rps = plane::PropRpsStep(r->rps, want, dt, cfg.airplane_prop_max);
        r->angle = plane::PropAngleStep(r->angle, r->rps, dt);
        }
        // a just-woken vehicle blends its joints in from the rest pose (the wheels) for 2 s: the propellers
        // would fly in from under the belly - the blend is finished at once (what 0x1404BEFBD writes)
        const float bl = At<float>(veh, VEH_VISBLEND);
        if (bl > 0.0f && bl < 1.0f) At<float>(veh, VEH_VISBLEND) = 1.0f;
    }
    const plane::GearStow stow = plane::ElifantStow();
    VisWheel now[6];
    int nn = 0, posed = 0, parts = 0;
    bool steerVis = false;
    void* w = w0;
    for (int i = 0; i < 8 && PtrOk(w) && nn < 6; ++i, w = At<void*>(w, eng::WHL_NEXT))
    {
        VisWheel v{};
        v.w = w;
        float cur[16];
        memcpy(cur, &At<float>(w, WHL_VISMTX), sizeof(cur));
        const VisWheel* was = nullptr;
        for (int j = 0; j < r->n; ++j) if (r->wh[j].w == w) { was = &r->wh[j]; break; }
        const bool ours = was && !memcmp(cur, was->wrote, sizeof(cur));  // the engine has not refreshed it (sim LOD 5)
        memcpy(v.base, ours ? was->base : cur, sizeof(v.base));
        float out[16];
        memcpy(out, v.base, sizeof(out));
        bool ok = false;
        if (kind == plane::WHEELVIS_GEAR) ok = plane::GearStowPos(&v.base[12], r->vis, stow, &out[12]);
        else
        {
            void* wd = At<void*>(w, eng::WHL_DEF);
            char jn[64] = {};
            if (PtrOk(wd))
                for (int k = 0; k < 63; ++k)
                {
                    const char ch = At<char>(wd, WDEF_JOINT + k);
                    if (!ch) break;
                    if (ch < 0x20 || ch > 0x7E) { jn[0] = 0; break; }
                    jn[k] = ch;
                }
            float hub[3];
            plane::GearPart part;
            if (kind == plane::WHEELVIS_PROPS && plane::PropHubFor(jn, hub))
            {
                plane::PropMatrix(hub, out);
                const float rad = At<float>(w, eng::WHL_RADIUS);
                if (IsFin(rad) && rad > 0.001f && rad < 10.0f) At<float>(w, WHL_VISSPIN) = r->angle * rad;
                ok = true;
            }
            else if (plane::GearPartFor(kind, &v.base[12], part))
            {
                // a gear part: the whole matrix, and no roll (the legs are on this joint too)
                plane::GearPartMatrix(part, r->vis, out);
                At<float>(w, WHL_VISSPIN) = 0.0f;
                if (PtrOk(wd) && At<uint8_t>(wd, WDEF_STEERVIS) && std::fabs(v.base[12]) < 0.05f) steerVis = true;
                ok = true; ++parts;
            }
        }
        if (ok) { memcpy(&At<float>(w, WHL_VISMTX), out, sizeof(out)); ++posed; }
        memcpy(v.wrote, ok ? out : v.base, sizeof(v.wrote));
        now[nn++] = v;
    }
    for (int j = 0; j < nn; ++j) r->wh[j] = now[j];
    r->n = nn;
    if (!r->moved && posed)
    {
        r->moved = true;
        if (kind == plane::WHEELVIS_GEAR)
            Log(">>> airplane: gear visual - %d wheels of %p go up into the hull as the gear travels\n", posed, veh);
        else
        {
            if (kind == plane::WHEELVIS_PROPS)
                Log(">>> airplane: propellers - %d wheel joints of %p hold the propeller hubs%s\n", posed - parts, veh,
                    cfg.airplane_props ? "" : " (props = 0: they stand still)");
            Log(">>> airplane: gear visual - %d wheel joint(s) of %p carry the model's gear parts (planes3 PATCH.DAT; with an "
                "older one nothing is on them)%s%s\n", parts, veh, cfg.airplane_gear_visual ? "" : " - gear_visual = 0: drawn down",
                steerVis ? " - the nose wheel's SteeringVisible is still 1: the .cd is not planes3's, the nose gear will turn about the wrong point" : "");
        }
    }
}

static void Init()
{
    WheelVisForget();
    g = Ctx{}; g_p2 = Ctx{};
    plane::Reset(g.st); plane::Reset(g_p2.st);
    g_f = Frame{};
}

} // namespace fw
