// =============================================================================================
//  plane_model.h - a fixed-wing flight model for the player's plane. Pure model, the same contract
//  as heli_model.h: plain floats in, new body velocities out, no engine pointers, so
//  test\plane_harness.cpp runs it as is.
//
//  The game's only winged model is AIRCRAFT_ELIFANT, the car-class airliner the airport shows parked
//  (gearMode 1, airplane.h; corrected 2026-11-13 - Notes\18 §0 said there was no wing anywhere). The
//  four Hover AIRCRAFT_* (CLOUD, HERA, JUPITER, UFO) are a jetpack, a helicopter and two saucers,
//  which the hover update flies on a height rail; plane.h can still fly them with this model as an
//  opt-in (gearMode 0). Then the caller does exactly what heli.h Fly() does: snapshot the body's
//  velocities BEFORE the engine's update, run the update, then write this model's out.lv / out.av
//  over whatever it did.
//
//  The model, mass-free (accelerations, u/s^2; 1 u ~ 1 m; gravity is the engine's veh+0x2EC):
//    * a wing: lift ~ v^2 * c(AoA), c rounding off into a SOFT stall (no cliff) and fading to
//      nothing past 90 deg; parasitic + induced (c^2) + form drag (sin^2 AoA); drag sized at load
//      time so full throttle in level flight settles at topSpeed;
//    * thrust from a throttle lever (held or direct), spooling, fading with airspeed;
//    * the stick asks for LIFT, not for an attitude: back = more lift (g), forward = less. The
//      fly-by-wire assist adds the lift that holds the flight path when the stick is centred,
//      turn-compensated (a bank does not lose height) and slowly easing back to level. The nose is
//      then driven to the angle of attack that gives that lift, on top of following the path's own
//      rotation - so the body always tracks the air (weathervane), whatever hits it;
//    * roll: stick = bank angle with auto-level when centred (autoLevel > 0), or stick = roll rate
//      (autoLevel = 0, sim). Bank turns the plane; the yaw follows the air (sideslip is yawed out
//      and the keel bleeds the rest) - no flat skidding unless the rudder / flatTurn asks for it;
//    * angular velocity is written through a 50 ms lag like the heli's attitude controller: rate
//      damping as strong as it gets, a collision only disturbs it for a moment, and nothing here
//      can wind up a spin (every rotation is a bounded command toward the airflow / the horizon);
//    * stall protection below ~70% stick; full stick back can stall it, and a stall breaks by
//      itself after stallBreakS (the nose drops through), so it always recovers;
//    * wheels: a virtual gear holds the height-above-ground probe at gearHeight (no hull contact,
//      no solver friction, no micro-collisions), rolling resistance, brakes, tyre side grip,
//      nose-wheel steering from the turn input, rotation on the stick from near rotateSpeed;
//    * crash detector: the velocity jump the solver made since our last write (dv, as in heli
//      Wreck), and the touchdown check (sink rate, bank, nose-down / tail strike).
//  Optional hover (UFO): an anti-gravity vertical-rate servo that fades out with airspeed.
//
//  Real wheels (gearMode 1, 2026-11-13, airplane.h): for a CAR-class airframe (AIRCRAFT_ELIFANT, the
//  airliner parked at the airport) the engine's own suspension and wheel joints carry it on the
//  ground. Then the model only decides: on the wheels it hands back the aerodynamic acceleration
//  WITHOUT gravity (out.acc: lift, drag, thrust, reverser - the caller applies m * acc as a body force;
//  the car update integrates gravity itself, Notes\23 #1), a pitch rate for the rotation on the main
//  gear (out.rotateRate), the nose-wheel angle and the wheel brake; it writes no velocity
//  (out.writeLv = 0). In the air it is the same wing as gearMode 0 (out.writeLv = 1). Ground / air
//  comes from the wheels (in.wheelsDown), debounced 0.05 s. gearMode 0 is unchanged, bit for bit
//  (test\plane_regress.cpp).
//
//  Retractable gear (gearMode 1 with gearUpTopSpeed > 0; 2026-11-14: the horn button in flight
//  moves it in and out, and it changes the flight speed): in.gearUp is the lever (GearSwitch
//  below decides it: the horn toggles it in the air only), s.gearUp the gear itself, 0 = down .. 1 = up,
//  travelling in gearTimeS. Gear DOWN is the model as it was (same numbers, bit for bit); gear UP takes
//  the parasitic drag down to what makes full throttle settle at gearUpTopSpeed instead of topSpeed.
//  A touchdown with the gear up is a belly landing (CRASH_BELLY; LandingDamage scales it with the
//  speed); then the caller cancels the engine's suspension force by s.gearUp (out.gearUp), so the hull
//  lies on the ground, nothing rolls or steers, and the model adds the belly's friction (bellyDecel).
//
//  Conventions (heli.h's, live-calibrated there):
//    rows: right / up / back, forward = -back; the model rebuilds right = forward x up.
//    cTurn: the heli's turn axis, same field, same sign: +1 turns toward -right (the heli's +yaw
//           about world up). heli.h Fly() feeds it the left stick's raw X; do the same.
//    cFwd:  the heli's forward axis: +1 = stick pushed forward = nose DOWN; stick back = nose up.
//    bank:  + = the right row dipping. +cTurn banks to -right and turns to -right.
//    Impact detection needs Update every flying frame (or Reset after a teleport / respawn):
//    the dv is measured against the velocity written the frame before.
// =============================================================================================
#pragma once
#include <cmath>
#include <cstdint>

namespace plane {

enum : uint8_t { CRASH_NONE = 0, CRASH_IMPACT = 1, CRASH_SINK = 2, CRASH_BANK = 3, CRASH_PITCH = 4, CRASH_BELLY = 5 };

struct Params
{
    // ---- speeds, u/s (cars ~17-20, the helicopter 45) ----------------------------------------------
    float stallSpeed      = 17.0f;   // level flight at the lift peak (1 g at the stall AoA)
    float rotateSpeed     = 21.0f;   // the nose wheel comes up on the stick from ~85% of this
    float topSpeed        = 50.0f;   // level flight at full throttle (sizes the parasitic drag)
    // ---- engine ------------------------------------------------------------------------------------
    float thrustG         = 0.45f;   // static thrust at full throttle, multiples of gravity
    float thrustFade      = 0.35f;   // share of the thrust gone at topSpeed (prop ~0.35, jet ~0.1)
    float spoolS          = 0.5f;    // engine time constant
    float leverRate       = 0.6f;    // /s: throttle/brake move a lever that stays put (0 = throttle IS the lever)
    // ---- wing --------------------------------------------------------------------------------------
    float clZero          = 0.25f;   // lift coefficient at zero AoA (camber)
    float clSlope         = 5.0f;    // per radian
    float stallDeg        = 15.0f;   // AoA of the lift peak
    float kneeDeg         = 4.0f;    // the curve rounds off over this much before the peak
    float negStallDeg     = 12.0f;   // the negative-AoA peak (pushing / inverted)
    float postStall       = 0.65f;   // share of the peak lift left deep in the stall (soft, no cliff)
    float stallWidthDeg   = 7.0f;    // how quickly the lift sags past the peak
    float inducedK        = 0.09f;   // induced drag = inducedK * c^2 of the peak-lift force
    float formDrag        = 0.6f;    // extra drag coefficient * sin^2(AoA)
    float airBrake        = 0.08f;   // extra drag coefficient at full brake in the air
    float liftMaxG        = 5.0f;    // lift cap, g
    float keel            = 1.5f;    // 1/s at the stall speed: sideways air bleeds off (fuselage + fin)
    float keelReaim       = 0.3f;    // share of that sideways speed turned into forward speed
    // ---- handling ----------------------------------------------------------------------------------
    float rollRateDeg     = 110.0f;  // deg/s
    float maxBankDeg      = 65.0f;   // bank at full stick (autoLevel > 0); soft bank limit in rate mode (>= 179 = none)
    float autoLevel       = 1.0f;    // 0 = stick is roll RATE, bank stays; > 0 = stick is BANK, centred levels (x speed)
    float pitchRateDeg    = 60.0f;   // limit on the nose's AoA correction, deg/s
    float pullG           = 2.0f;    // extra lift at full stick back, g
    float pushG           = 1.2f;    // less lift at full stick forward, g
    float alphaS          = 0.15f;   // the nose reaches the asked-for AoA in ~this time
    float pathHold        = 1.0f;    // 0..1 centred stick holds the flight path, turn-compensated (FBW assist)
    float levelPathRate   = 0.25f;   // 1/s centred stick eases the held path back to level (0 = holds any climb)
    float holdMaxG        = 3.0f;    // the most the path hold may ask for, g
    float stallProtect    = 1.0f;    // 0..1: below ~70% stick back the AoA stays under the stall
    float overStallDeg    = 8.0f;    // how far past the stall full stick back may go
    float stallBreakS     = 1.0f;    // stalled this long, the nose drops through regardless of the stick
    float coordK          = 3.0f;    // 1/s sideslip -> yaw into the air (weathervane / auto-coordination)
    float rudderDeg       = 25.0f;   // yaw rate at full rudder, deg/s
    float flatTurn        = 0.0f;    // share of the turn input that yaws directly (UFO)
    float flatYawDeg      = 0.0f;    // its yaw rate, deg/s
    // ---- ground ------------------------------------------------------------------------------------
    float gearHeight      = 0.15f;   // the virtual gear holds the height-above-ground probe here
    float wheelBase       = 3.0f;
    float steerDeg        = 35.0f;   // nose wheel at full turn input, standing
    float steerFadeV      = 10.0f;   // the nose wheel's angle halves at this speed
    float rotateDeg       = 11.0f;   // nose-up attitude the stick can rotate to on the wheels
    float rollDecel       = 0.5f;    // u/s^2 rolling resistance
    float brakeDecel      = 8.0f;    // u/s^2 wheel brakes at full brake
    float tyreGrip        = 14.0f;   // 1/s sideways speed lost on the wheels
    // ---- landing and crashes -----------------------------------------------------------------------
    float safeSink        = 4.0f;    // u/s vertical at touchdown
    float safeBankDeg     = 15.0f;
    float safeNoseDownDeg = 6.0f;
    float tailStrikeDeg   = 18.0f;
    float bumpDv          = 3.0f;    // velocity jump in one frame (u/s) = hit something (impact flag)
    float crashDv         = 11.0f;   // this hard = crash
    // ---- hover (UFO) -------------------------------------------------------------------------------
    float hoverG          = 0.0f;    // 0..1 of the weight carried with no airspeed
    float hoverSpeed      = 0.0f;    // the hover fades out between 50% and 100% of this airspeed (0 = never)
    float hoverClimb      = 6.0f;    // u/s at full ascend input (cVert)
    float hoverDamp       = 0.6f;    // 1/s horizontal drift killed while hovering with the throttle shut
    // ---- input polarity (live calibration, like heli::Params::yawSign / fwdSign) --------------------
    float turnSign        = 1.0f;
    float fwdSign         = 1.0f;
    // ---- real wheels (airplane.h; the header) ------------------------------------------------------
    int   gearMode        = 0;       // 0 = the virtual gear (Hover airframes, plane.h); 1 = the engine's wheels
    float reverseG        = 0.0f;    // gearMode 1: reverse thrust at idle lever + brake on the wheels, g (0 = none)
    float taxiReverseSpeed = 0.0f;   // gearMode 1: stopped, lever idle, brake held -> it backs up to this speed, u/s (0 = never)
    float taxiReverseAccel = 2.0f;   // u/s^2 of that push
    float taxiReverseDwell = 0.4f;   // s at rest with the brake held before it starts (a braking stop does not flow into backing)
    // ---- retractable gear (gearMode 1 only; the header) -----------------------------------------------
    float gearUpTopSpeed  = 0.0f;    // level flight at full throttle with the gear UP (0 = the gear does not retract)
    float gearTimeS       = 2.0f;    // the gear travels this long
    float bellyDecel      = 5.0f;    // u/s^2: the hull sliding on the ground with the gear up
    float bellySpeed      = 10.0f;   // a belly landing costs 1 point + 1 per this much speed (u/s)
    float destroySpeed    = 15.0f;   // a crash this hard destroys the plane outright (FatalCrash); 0 = never
};

// ---- airframes ---------------------------------------------------------------------------------
// (Which game model flies with which preset is plane.h's table and airplane.h's; the four Hover
//  "planes" are a jetpack, a helicopter and two saucers, not aeroplanes - planes_research.)
inline Params LightPlane() { return Params(); }                         // a small propeller plane

inline Params Jet()                                                     // a small jet
{
    Params p;
    p.stallSpeed = 21.0f; p.rotateSpeed = 25.0f; p.topSpeed = 70.0f;
    p.thrustG = 0.6f; p.thrustFade = 0.1f; p.spoolS = 1.2f; p.leverRate = 0.5f;
    p.clZero = 0.15f; p.clSlope = 4.5f; p.stallDeg = 16.0f; p.inducedK = 0.12f; p.airBrake = 0.1f;
    p.rollRateDeg = 180.0f; p.maxBankDeg = 80.0f; p.autoLevel = 0.6f; p.pitchRateDeg = 80.0f;
    p.pullG = 3.5f; p.pushG = 1.5f; p.alphaS = 0.12f; p.levelPathRate = 0.15f; p.holdMaxG = 4.0f;
    p.coordK = 3.5f; p.rudderDeg = 20.0f;
    p.wheelBase = 5.0f; p.steerDeg = 30.0f; p.steerFadeV = 12.0f; p.rotateDeg = 10.0f; p.brakeDecel = 7.0f;
    p.safeSink = 4.5f; p.tailStrikeDeg = 15.0f; p.crashDv = 13.0f;
    return p;
}

inline Params Airliner()                                                // a long-field airliner (reference)
{
    Params p;
    p.stallSpeed = 20.0f; p.rotateSpeed = 24.0f; p.topSpeed = 55.0f;
    p.thrustG = 0.38f; p.thrustFade = 0.2f; p.spoolS = 1.5f; p.leverRate = 0.4f;
    p.clZero = 0.3f; p.inducedK = 0.07f;
    p.rollRateDeg = 60.0f; p.maxBankDeg = 35.0f; p.pitchRateDeg = 30.0f; p.pullG = 1.0f; p.pushG = 0.7f;
    p.alphaS = 0.25f; p.levelPathRate = 0.2f; p.holdMaxG = 2.0f; p.coordK = 2.5f; p.rudderDeg = 12.0f;
    p.wheelBase = 7.0f; p.steerDeg = 25.0f; p.steerFadeV = 8.0f; p.rotateDeg = 9.0f; p.brakeDecel = 6.0f;
    p.safeSink = 3.5f; p.safeBankDeg = 10.0f; p.tailStrikeDeg = 12.0f; p.crashDv = 10.0f;
    return p;
}

inline Params Ufo()                                                     // AIRCRAFT_UFO
{
    Params p;
    p.stallSpeed = 10.0f; p.rotateSpeed = 12.0f; p.topSpeed = 60.0f;
    p.thrustG = 0.8f; p.thrustFade = 0.1f; p.spoolS = 0.4f; p.leverRate = 0.8f;
    p.clZero = 0.3f; p.stallDeg = 25.0f; p.postStall = 0.9f; p.inducedK = 0.05f;
    p.rollRateDeg = 90.0f; p.maxBankDeg = 20.0f; p.pullG = 2.5f; p.pushG = 2.0f;
    p.keel = 3.0f; p.keelReaim = 0.9f; p.coordK = 4.0f; p.flatTurn = 0.8f; p.flatYawDeg = 90.0f;
    p.hoverG = 1.0f; p.hoverSpeed = 18.0f; p.hoverClimb = 7.0f;
    p.gearHeight = 0.2f; p.rotateDeg = 5.0f; p.wheelBase = 2.0f; p.steerDeg = 20.0f;
    p.safeSink = 5.0f; p.safeBankDeg = 25.0f; p.safeNoseDownDeg = 15.0f; p.tailStrikeDeg = 25.0f;
    return p;
}

// AIRCRAFT_ELIFANT, the white twin-jet airliner parked at the airport, on its own five wheels
// (airplane.h, gearMode 1). Short-field numbers: the runway is the 57 u north-quay apron (airport.h),
// and the airport sim lifted stall 12-13 / thrust 0.6-0.8 airframes off in 43-51 u [P sim]. The
// numbers are [I] until test\airplane_harness.cpp and a flight say otherwise; [airplane] rotate_deg
// overrides rotateDeg. wheelBase / rollDecel / brakeDecel / gearHeight are unused in gearMode 1 (the
// engine's wheels and lcudrive's joint rows do that).
inline Params Elifant()
{
    Params p;
    p.stallSpeed = 12.5f; p.rotateSpeed = 15.0f; p.topSpeed = 45.0f;
    p.thrustG = 0.75f; p.thrustFade = 0.15f; p.spoolS = 1.0f; p.leverRate = 0.6f;
    p.clZero = 0.3f; p.clSlope = 5.0f; p.stallDeg = 15.0f; p.inducedK = 0.08f; p.airBrake = 0.1f;
    p.rollRateDeg = 70.0f; p.maxBankDeg = 45.0f; p.autoLevel = 1.0f; p.pitchRateDeg = 40.0f;
    p.pullG = 1.5f; p.pushG = 1.0f; p.alphaS = 0.2f; p.levelPathRate = 0.2f; p.holdMaxG = 2.5f;
    p.coordK = 3.0f; p.rudderDeg = 15.0f;
    p.steerDeg = 30.0f; p.steerFadeV = 8.0f; p.rotateDeg = 8.0f; p.wheelBase = 4.0f;
    p.safeSink = 3.5f; p.safeBankDeg = 12.0f; p.safeNoseDownDeg = 5.0f; p.tailStrikeDeg = 11.0f; p.crashDv = 10.0f;
    p.gearMode = 1; p.reverseG = 0.3f; p.taxiReverseSpeed = 3.0f;
    p.gearUpTopSpeed = 56.0f;                      // gear down 45 (as before), gear up 56
    return p;
}

struct State
{
    float lever, spool;      // throttle lever 0..1 (< 0 = not yet set), engine output 0..1
    float gammaHold;         // flight-path angle the centred stick holds, rad
    float stallT;            // s beyond the stall AoA (drives the stall break)
    float airT;              // s since the wheels left the ground
    float lastLv[3];         // what was written last frame: the solver's impulse shows against it
    float impactSpeed;       // speed of the crash (latched with crash)
    uint8_t ground;          // 1 on the wheels, 0 flying, 2 not yet known
    uint8_t primed;          // lastLv is valid
    uint8_t crash;           // CRASH_* reason, latched until Reset
    float upT;               // gearMode 1: s with every wheel up while still counted on the ground
    float gearUp;            // gearMode 1: the gear, 0 = down .. 1 = up (0 always unless gearUpTopSpeed > 0)
    float stopT;             // gearMode 1: s at rest on the wheels with the lever idle and the brake held
    uint8_t backing;         // gearMode 1: taxiing backwards (TaxiReverse)
};

inline void Reset(State& s)
{
    s.lever = -1.0f; s.spool = 0.0f; s.gammaHold = 0.0f; s.stallT = 0.0f; s.airT = 0.0f;
    s.lastLv[0] = s.lastLv[1] = s.lastLv[2] = 0.0f; s.impactSpeed = 0.0f;
    s.ground = 2; s.primed = 0; s.crash = CRASH_NONE; s.upT = 0.0f; s.gearUp = 0.0f;
    s.stopT = 0.0f; s.backing = 0;
}

struct Input
{
    float dt;
    float right[3], up[3], back[3];   // body rows (engine forward = -back)
    float lv[3], av[3];               // physical velocities (the engine's writes already undone)
    float hag;                        // height above ground (engine probe), > 1e30 = unknown
    float gn[3];                      // ground normal if known; (0,0,0) = world up
    float gravity;                    // the engine's (veh+0x2EC); not in (1, 50) = 9.8
    float cTurn, cFwd;                // stick X / Y, -1..1 (heli conventions, see the header)
    float cYaw;                       // rudder -1..1, same sign as cTurn (0 if unmapped)
    float cVert;                      // ascend / descend -1..1 (hover airframes only)
    float throttle, brake;            // 0..1
    uint8_t occupied;
    // gearMode 1 only (gearMode 0 ignores them, so `Input in{}` callers are unaffected):
    int   wheelsDown;                 // wheels touching this frame (the engine's own rays); -1 = unknown
    float wow;                        // weight on the wheels, share of the weight (telemetry / harness only)
    uint8_t gearUp;                   // the gear lever: 1 = up (GearSwitch), 0 = down
};

struct Output
{
    float lv[3], av[3];               // what to write back
    float airspeed, alphaDeg, betaDeg, bankDeg, pitchDeg, gammaDeg;
    float liftG, thrust, lever, spool, stall;   // stall: 0..1 how deep past the stall AoA
    float dv;                         // this frame's velocity jump since our last write (solver impulse)
    float sink;                       // vertical speed at this frame's touchdown
    float impactSpeed;                // the crash's speed (latched)
    uint8_t ground, touchdown, impact, crash, valid;
    // gearMode 1 (gearMode 0: writeLv = 1, the rest 0):
    uint8_t writeLv;                  // 1 = write lv / av (flying); 0 = the wheels carry it, write nothing
    float acc[3];                     // on the wheels: lift + drag + thrust - reverser, NO gravity (u/s^2)
    float rotateRate;                 // on the wheels: pitch rate about right (+ nose up) for the rotation, rad/s
    float noseWheelDeg;               // nose-wheel angle, + = the turn input's side (0 in the air)
    float wheelBrake;                 // 0..1 on the brake wheels
    float reverse;                    // reverse thrust in use, g
    float wow;                        // in.wow passed through
    float gearUp;                     // the gear, 0 = down .. 1 = up: the share of the suspension force to cancel
    uint8_t belly;                    // on the ground on the hull (the gear more than half up)
    uint8_t backing;                  // taxiing backwards: the brake input is the reverse command, the wheel brake is off
};

// ---- small vector helpers (heli_model.h's) ------------------------------------------------------
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
inline float Axis(float v) { return std::isfinite(v) ? Clamp(v, -1.0f, 1.0f) : 0.0f; }   // NaN = centred
inline float Wrap(float a) { while (a > 3.14159265f) a -= 6.2831853f; while (a < -3.14159265f) a += 6.2831853f; return a; }

static const float kD2R = 0.01745329f;

// ---- the lift curve ------------------------------------------------------------------------------
struct LiftShape { float cl0, slope, as, an, kw, sig, post, peak, negPeak; };

inline LiftShape Shape(const Params& p)
{
    LiftShape k;
    k.cl0   = Clamp(p.clZero, -0.5f, 1.0f);
    k.slope = Clamp(p.clSlope, 0.5f, 20.0f);
    k.as    = Clamp(p.stallDeg, 4.0f, 40.0f) * kD2R;
    k.an    = -Clamp(p.negStallDeg, 2.0f, 40.0f) * kD2R;
    k.kw    = Clamp(p.kneeDeg * kD2R, 0.005f, 0.8f * std::fmin(k.as, -k.an));
    k.sig   = Clamp(p.stallWidthDeg, 1.0f, 40.0f) * kD2R;
    k.post  = Clamp(p.postStall, 0.2f, 1.0f);
    k.peak  = std::fmax(k.cl0 + k.slope * (k.as - 0.5f * k.kw), 0.1f);
    k.negPeak = std::fmin(k.cl0 + k.slope * (k.an + 0.5f * k.kw), -0.05f);
    return k;
}

// Lift coefficient divided by the peak: 1 at the stall AoA. Linear, rounded into the peak (C1),
// a smooth sag to postStall beyond it, faded to 0 between 40 and 90 deg, 0 flying backwards.
inline float LiftCurve(const Params& p, float a)
{
    const LiftShape k = Shape(p);
    const float aK = k.as - k.kw, aN = k.an + k.kw;
    float cl;
    if (a > k.as)       { const float d = a - k.as; cl = k.peak * (k.post + (1.0f - k.post) * std::exp(-d * d / (2.0f * k.sig * k.sig))); }
    else if (a > aK)    { const float x = a - aK; cl = k.cl0 + k.slope * aK + k.slope * x - k.slope / (2.0f * k.kw) * x * x; }
    else if (a >= aN)   { cl = k.cl0 + k.slope * a; }
    else if (a >= k.an) { const float x = a - aN; cl = k.cl0 + k.slope * aN + k.slope * x + k.slope / (2.0f * k.kw) * x * x; }
    else                { const float d = k.an - a; cl = k.negPeak * (k.post + (1.0f - k.post) * std::exp(-d * d / (2.0f * k.sig * k.sig))); }
    const float aa = std::fabs(a);
    if (aa >= 1.5708f) return 0.0f;
    if (aa > 0.698f)
    {
        const float t = Clamp((aa - 0.698f) / (1.5708f - 0.698f), 0.0f, 1.0f), c = std::cos(t * 1.5708f);
        cl *= c * c;
    }
    return cl / k.peak;
}

// The AoA (rad) that gives lift coefficient c (of the peak) on the unstalled curve, [an, as].
inline float InvLift(const Params& p, float c)
{
    const LiftShape k = Shape(p);
    const float cl = c * k.peak;
    if (!std::isfinite(cl)) return 0.0f;
    const float aK = k.as - k.kw, clK = k.cl0 + k.slope * aK;
    const float aN = k.an + k.kw, clN = k.cl0 + k.slope * aN;
    if (cl >= k.peak) return k.as;
    if (cl > clK) return aK + k.kw * (1.0f - std::sqrt(std::fmax(0.0f, 1.0f - 2.0f * (cl - clK) / (k.slope * k.kw))));
    if (cl <= k.negPeak) return k.an;
    if (cl < clN) return aN - k.kw * (1.0f - std::sqrt(std::fmax(0.0f, 1.0f - 2.0f * (clN - cl) / (k.slope * k.kw))));
    return (cl - k.cl0) / k.slope;
}

// Parasitic drag coefficient (of the peak-lift force) that makes full throttle settle at topSpeed.
inline float ParasiticDrag(const Params& p, float g)
{
    const float Vs = Clamp(p.stallSpeed, 3.0f, 80.0f), Vt = Clamp(p.topSpeed, Vs * 1.3f, 150.0f);
    const float cLvl = (Vs / Vt) * (Vs / Vt);
    const float Ttop = Clamp(p.thrustG, 0.0f, 5.0f) * g * std::fmax(0.05f, 1.0f - Clamp(p.thrustFade, 0.0f, 0.95f));
    const float qTop = g * (Vt / Vs) * (Vt / Vs);
    return std::fmax(Ttop / qTop - p.inducedK * cLvl * cLvl, 0.002f);
}

// The same with the gear up: the parasitic drag that makes full throttle settle at gearUpTopSpeed (the
// thrust still fades against topSpeed, as in Update). Never more than the gear-down drag.
inline float ParasiticDragClean(const Params& p, float g)
{
    const float down = ParasiticDrag(p, g);
    const float Vs = Clamp(p.stallSpeed, 3.0f, 80.0f), Vt = Clamp(p.topSpeed, Vs * 1.3f, 150.0f);
    const float Vc = Clamp(p.gearUpTopSpeed, Vt, 1.5f * Vt);
    const float cLvl = (Vs / Vc) * (Vs / Vc);
    const float T = Clamp(p.thrustG, 0.0f, 5.0f) * g * std::fmax(0.05f, 1.0f - Clamp(p.thrustFade, 0.0f, 0.95f) * std::fmin(Vc / Vt, 1.5f));
    const float q = g * (Vc / Vs) * (Vc / Vs);
    return Clamp(T / q - p.inducedK * cLvl * cLvl, 0.002f, down);
}

inline void Update(const Params& p, State& s, const Input& in, Output& out)
{
    for (int k = 0; k < 3; ++k) { out.lv[k] = in.lv[k]; out.av[k] = in.av[k]; }
    out.airspeed = out.alphaDeg = out.betaDeg = out.bankDeg = out.pitchDeg = out.gammaDeg = 0.0f;
    out.liftG = out.thrust = out.stall = out.dv = out.sink = 0.0f;
    out.lever = s.lever; out.spool = s.spool; out.impactSpeed = s.impactSpeed;
    out.ground = s.ground == 1; out.touchdown = out.impact = 0; out.crash = s.crash; out.valid = 0;
    out.writeLv = 0; out.acc[0] = out.acc[1] = out.acc[2] = 0.0f;
    out.rotateRate = out.noseWheelDeg = out.wheelBrake = out.reverse = 0.0f;
    out.wow = std::isfinite(in.wow) ? in.wow : 0.0f;
    out.gearUp = 0.0f; out.belly = 0; out.backing = 0;
    if (!(in.dt > 0.0f) || in.dt > 0.1f || !Fin3(in.right) || !Fin3(in.up) || !Fin3(in.back) ||
        !Fin3(in.lv) || !Fin3(in.av))
    { s.primed = 0; return; }
    const float dt = in.dt;
    const float g = (std::isfinite(in.gravity) && in.gravity > 1.0f && in.gravity < 50.0f) ? in.gravity : 9.8f;

    // ---- body frame (right rebuilt as forward x up, like the heli's heading frame) --------------
    float u[3] = { in.up[0], in.up[1], in.up[2] };
    float f[3] = { -in.back[0], -in.back[1], -in.back[2] };
    if (!Norm(u) || !Norm(f)) { s.primed = 0; return; }
    { const float d = Dot(f, u); for (int k = 0; k < 3; ++k) f[k] -= u[k] * d; }
    if (!Norm(f)) { s.primed = 0; return; }
    float r[3]; Cross(f, u, r);
    const bool hagOk = in.hag < 1e30f && std::isfinite(in.hag);
    float G[3] = { 0.0f, 1.0f, 0.0f };                                   // the ground's up
    if (Fin3(in.gn)) { float n[3] = { in.gn[0], in.gn[1], in.gn[2] }; if (Norm(n) && n[1] > 0.5f) { G[0] = n[0]; G[1] = n[1]; G[2] = n[2]; } }

    // ---- inputs (hands off when unoccupied: throttle shut, parking brake on) ---------------------
    const bool occ = in.occupied != 0;
    const float turn = occ ? Dead(Axis(in.cTurn), 0.08f) * p.turnSign : 0.0f;
    const float pull = occ ? -Dead(Axis(in.cFwd), 0.08f) * p.fwdSign : 0.0f;      // + = stick back
    const float yaw  = occ ? Dead(Axis(in.cYaw), 0.1f) * p.turnSign : 0.0f;
    const float vert = occ ? Dead(Axis(in.cVert), 0.1f) : 0.0f;
    const float thr  = occ && std::isfinite(in.throttle) ? Clamp(in.throttle, 0.0f, 1.0f) : 0.0f;
    const float brk  = !occ ? 1.0f : (std::isfinite(in.brake) ? Clamp(in.brake, 0.0f, 1.0f) : 0.0f);

    // ---- air state ------------------------------------------------------------------------------
    const float* v = in.lv;
    const float V = std::sqrt(Dot(v, v));
    const bool moving = V > 0.5f;
    const float vf = Dot(v, f), vu = Dot(v, u), vr = Dot(v, r);
    const float alpha = moving ? std::atan2(-vu, vf) : 0.0f;                  // + = air from below
    const float beta  = moving ? std::atan2(vr, std::fmax(vf, 1.0f)) : 0.0f;   // + = sliding to +right
    const float bank  = std::atan2(-r[1], u[1]);                               // + = right row down
    const float pitch = std::asin(Clamp(f[1], -1.0f, 1.0f));
    const float gamma = moving ? std::asin(Clamp(v[1] / V, -1.0f, 1.0f)) : 0.0f;
    float vh[3] = { f[0], f[1], f[2] };
    if (moving) for (int k = 0; k < 3; ++k) vh[k] = v[k] / V;
    const LiftShape ls = Shape(p);
    const float gh = Clamp(p.gearHeight, 0.0f, 3.0f);
    const float Vs = Clamp(p.stallSpeed, 3.0f, 80.0f), Vt = Clamp(p.topSpeed, Vs * 1.3f, 150.0f);

    // ---- first frame: settle what Reset left open -------------------------------------------------
    // (gearMode 1: on the wheels or not is the engine's suspension rays, in.wheelsDown; unknown (-1)
    //  falls back to the height probe, which a car does not have - so to "flying")
    const bool real = p.gearMode == 1;
    const int  wdn  = real ? in.wheelsDown : -1;
    const bool havePrev = s.primed != 0;
    if (s.ground > 1) s.ground = (real && wdn >= 0) ? (wdn > 0 ? 1 : 0) : ((hagOk && in.hag < gh + 0.15f) ? 1 : 0);
    if (!(s.lever >= 0.0f)) { s.lever = s.ground ? 0.0f : 0.7f; s.spool = s.lever; s.gammaHold = s.ground ? 0.0f : gamma; }

    // ---- the gear (gearMode 1 with a retractable gear): travels toward the lever in gearTimeS ----------
    float gu = 0.0f;
    if (real && p.gearUpTopSpeed > 0.0f)
    {
        const float step = dt / Clamp(p.gearTimeS, 0.1f, 20.0f);
        s.gearUp = in.gearUp ? std::fmin(s.gearUp + step, 1.0f) : std::fmax(s.gearUp - step, 0.0f);
        if (!(s.gearUp >= 0.0f)) s.gearUp = 0.0f;
        gu = s.gearUp;
    }
    else if (real) s.gearUp = 0.0f;
    out.gearUp = gu;

    // ---- impact: what the solver did to the velocity we wrote last frame --------------------------
    float dv = 0.0f;
    if (havePrev)
    {
        float d2 = 0.0f;
        for (int k = 0; k < 3; ++k) { const float d = in.lv[k] - s.lastLv[k]; d2 += d * d; }
        dv = std::sqrt(d2);
    }
    out.dv = dv;
    if (dv > p.bumpDv) out.impact = 1;
    if (dv > p.crashDv && !s.crash) { s.crash = CRASH_IMPACT; s.impactSpeed = dv; }

    // ---- wheels on the ground? touchdown checks ---------------------------------------------------
    const float gBank  = std::atan2(-Dot(r, G), Dot(u, G));
    const float gPitch = std::asin(Clamp(Dot(f, G), -1.0f, 1.0f));
    if (s.ground == 0)
    {
        // gearMode 1: the first frame a wheel touches (the sink is the velocity from before the springs)
        const bool touch = (real && wdn >= 0) ? wdn > 0 : (hagOk && in.hag < gh + 0.05f && v[1] < 0.5f);
        if (touch)
        {
            s.ground = 1; s.upT = 0.0f;
            const float sink = std::fmax(-Dot(v, G), 0.0f);
            out.touchdown = 1; out.sink = sink;
            if (!s.crash)
            {
                uint8_t why = CRASH_NONE;
                if (sink > p.safeSink) why = CRASH_SINK;
                else if (std::fabs(gBank) > p.safeBankDeg * kD2R) why = CRASH_BANK;
                else if (gPitch < -p.safeNoseDownDeg * kD2R || gPitch > p.tailStrikeDeg * kD2R) why = CRASH_PITCH;
                if (gu > 0.5f) { s.crash = CRASH_BELLY; s.impactSpeed = V; }   // on the hull: whatever else it was
                else
                if (why) { s.crash = why; s.impactSpeed = why == CRASH_SINK ? sink : V; }
            }
        }
    }
    else if (real && wdn >= 0)
    {
        // gearMode 1: airborne once no wheel has touched for 0.05 s - a gear bounce or a bump on the
        // take-off roll (1-2 frames) is not a lift-off
        if (wdn > 0) s.upT = 0.0f;
        else if ((s.upT += dt) >= 0.05f) { s.ground = 0; s.airT = 0.0f; s.gammaHold = gamma; s.upT = 0.0f; }
    }
    else if (hagOk && in.hag > gh + 0.35f) { s.ground = 0; s.airT = 0.0f; s.gammaHold = gamma; }

    // ---- throttle lever and engine ----------------------------------------------------------------
    if (p.leverRate > 0.0f) s.lever = Clamp(s.lever + (thr - brk) * p.leverRate * dt, 0.0f, 1.0f);
    else s.lever = thr;
    if (!occ) s.lever = 0.0f;
    s.spool = Clamp(s.spool + (s.lever - s.spool) * Lag(dt, p.spoolS), 0.0f, 1.0f);

    // ---- aerodynamics ---------------------------------------------------------------------------
    const float Va = std::fmin(V, 3.0f * Vt);
    const float qn = g * (Va / Vs) * (Va / Vs);                              // force of the peak lift, per mass
    const float c = LiftCurve(p, alpha);
    float ld[3] = { u[0], u[1], u[2] };                                      // lift: perpendicular to the air,
    float L = 0.0f;                                                          // in the plane of symmetry
    float vsh[3] = { f[0], f[1], f[2] };
    bool vsOk = false;
    {
        float vs[3] = { v[0] - r[0] * vr, v[1] - r[1] * vr, v[2] - r[2] * vr };
        const float vsn = std::sqrt(Dot(vs, vs));
        if (vsn > 0.5f)
        {
            for (int k = 0; k < 3; ++k) vsh[k] = vs[k] / vsn;
            vsOk = true;
            float t[3]; Cross(r, vsh, t);
            if (Norm(t))
            {
                for (int k = 0; k < 3; ++k) ld[k] = t[k];
                L = Clamp(qn * c, -p.liftMaxG * g, p.liftMaxG * g);
                // never swing the air past the zero-lift line in one frame (stiff wings, long frames)
                const float cap = 0.8f * std::fabs(alpha + ls.cl0 / ls.slope) * vsn / dt;
                L = Clamp(L, -cap, cap);
            }
        }
    }
    const float sa = std::sin(alpha);
    float cdp = ParasiticDrag(p, g);
    if (gu > 0.0f) cdp += (ParasiticDragClean(p, g) - cdp) * gu;          // gear up: cleaner
    const float CD = cdp + p.inducedK * c * c + p.formDrag * sa * sa + (s.ground == 1 ? 0.0f : p.airBrake * brk);
    const float D = std::fmin(qn * CD, 0.5f * V / dt);
    const float T = s.spool * Clamp(p.thrustG, 0.0f, 5.0f) * g * std::fmax(0.05f, 1.0f - Clamp(p.thrustFade, 0.0f, 0.95f) * std::fmin(V / Vt, 1.5f));

    // ---- hover (UFO): a vertical-rate servo on top of everything else, fading with airspeed ---------
    float hov = 0.0f, hshare = 0.0f, hvyT = 0.0f;
    if (p.hoverG > 0.0f)
    {
        hshare = Clamp(p.hoverG, 0.0f, 1.0f);
        if (p.hoverSpeed > 0.0f) hshare *= Clamp((p.hoverSpeed - V) / (0.5f * p.hoverSpeed), 0.0f, 1.0f);
        if (hshare > 0.0f)
        {
            hvyT = vert * p.hoverClimb;
            if (hagOk && hvyT < 0.0f) hvyT = std::fmax(hvyT, -(0.8f + 0.8f * std::fmax(in.hag - gh, 0.0f)));   // flare
            const float ayOther = L * ld[1] + T * f[1] - D * vh[1] - g;
            hov = hshare * Clamp(4.0f * (hvyT - v[1]) - ayOther, -0.5f * g, 2.5f * g);
        }
    }

    float a[3];
    for (int k = 0; k < 3; ++k) a[k] = ld[k] * L - vh[k] * D + f[k] * T;
    if (real) for (int k = 0; k < 3; ++k) out.acc[k] = a[k];               // (telemetry; on the wheels: below)
    a[1] += hov - g;
    float nv[3] = { v[0] + a[0] * dt, v[1] + a[1] * dt, v[2] + a[2] * dt };
    if (hshare > 0.0f && s.ground != 1)                                    // hover-stop: drift dies with the throttle shut
    {
        const float kd = 1.0f - std::exp(-Clamp(p.hoverDamp, 0.0f, 10.0f) * hshare * (1.0f - s.lever) * dt);
        nv[0] -= nv[0] * kd; nv[2] -= nv[2] * kd;
    }

    const float rollRate  = Clamp(p.rollRateDeg, 10.0f, 360.0f) * kD2R;
    const float pitchRate = Clamp(p.pitchRateDeg, 10.0f, 360.0f) * kD2R;
    const float flatYaw   = Clamp(p.flatTurn, 0.0f, 1.0f) * Clamp(p.flatYawDeg, 0.0f, 360.0f) * kD2R;
    float wCmd[3] = { 0.0f, 0.0f, 0.0f };

    if (s.ground != 1 || !real) { s.stopT = 0.0f; s.backing = 0; }        // backwards taxi: on the real wheels only
    if (s.ground == 1)
    {
        // =========================== on the wheels ===========================
        s.stallT = 0.0f; s.gammaHold = 0.0f;
        float fg[3] = { f[0] - G[0] * Dot(f, G), f[1] - G[1] * Dot(f, G), f[2] - G[2] * Dot(f, G) };
        if (!Norm(fg)) { fg[0] = -u[0]; fg[1] = 0.0f; fg[2] = -u[2]; if (!Norm(fg)) { fg[0] = 0; fg[1] = 0; fg[2] = -1; } }
        float rg[3]; Cross(fg, G, rg); Norm(rg);
        if (real)
        {
            // ---- gearMode 1: the engine's wheels carry it; nothing here moves the body directly ----
            const float vl = Dot(v, fg);
            // reverse thrust: idle lever, the brake held, rolling forward (it never backs the plane up - that is
            // the backwards taxi below, from rest)
            float R = 0.0f;
            if (p.reverseG > 0.0f && s.lever <= 0.05f && vl > 2.0f && brk > 0.5f) R = Clamp(p.reverseG, 0.0f, 2.0f) * g * brk;
            for (int k = 0; k < 3; ++k) out.acc[k] = ld[k] * L - vh[k] * D + f[k] * (T - R);
            out.acc[1] += hov;
            out.reverse = R / g;
            // rotation: from 85% of rotate speed the stick lifts the nose onto the main gear, never past
            // the tail's clearance (tailStrikeDeg - 2); centred stick or slower = nothing (the springs)
            const float Vr = Clamp(p.rotateSpeed, 1.0f, 150.0f);
            const float rotMax = std::fmax(std::fmin(Clamp(p.rotateDeg, 0.0f, 30.0f), p.tailStrikeDeg - 2.0f), 0.0f) * kD2R;
            const float th = Clamp(pull, 0.0f, 1.0f) * rotMax * Clamp((vl - 0.85f * Vr) / (0.15f * Vr), 0.0f, 1.0f);
            out.rotateRate = th > 0.0f ? Clamp((th - gPitch) / 0.12f, -pitchRate, pitchRate) : 0.0f;
            // nose wheel: less angle the faster it rolls (the joint rows turn it into a turn)
            const float fv = std::fmax(p.steerFadeV, 0.5f);
            out.noseWheelDeg = Clamp(turn + yaw, -1.0f, 1.0f) * Clamp(p.steerDeg, 0.0f, 60.0f) / (1.0f + (vl / fv) * (vl / fv));
            // brakes: LT; parked (lever idle, standing, hands off) = the parking brake, no creep
            const bool parked = s.lever <= 0.0f && V < 0.3f && thr < 0.05f && turn == 0.0f && pull == 0.0f && yaw == 0.0f;
            out.wheelBrake = (parked || !occ) ? 1.0f : brk;
            // backwards taxi (2026-11-15: a plane that bumped into something was easily stuck): the car
            // convention - stopped, the brake still held = reverse. Lever idle, no throttle, the gear down, a wheel
            // on the ground, and at rest for taxiReverseDwell with the brake held (so a braking stop from a landing
            // roll does not flow straight into backing): then a gentle push backwards up to taxiReverseSpeed and
            // the wheel brake OFF - here the brake input is the reverse command. Letting go of the brake ends the
            // push (it rolls to rest on the rolling resistance; the parking brake holds it once it stands); any
            // throttle ends it at once. The nose wheel steers as it does forwards (out.noseWheelDeg above does
            // not look at the direction), so backing with the stick left swings the tail left, like a car.
            {
                const float cap = Clamp(p.taxiReverseSpeed, 0.0f, 10.0f);
                const bool want = occ && cap > 0.0f && gu <= 0.0f && wdn != 0 && s.lever <= 0.05f && thr < 0.05f && brk > 0.5f;
                if (!want || vl > 1.0f) { s.backing = 0; s.stopT = 0.0f; }
                else if (!s.backing)
                {
                    if (std::fabs(vl) < 0.5f && V < 1.0f) s.stopT += dt; else s.stopT = 0.0f;
                    if (s.stopT >= Clamp(p.taxiReverseDwell, 0.0f, 5.0f)) s.backing = 1;
                }
                if (s.backing)
                {
                    // full push until 0.5 u/s short of the cap, fading to nothing at it; faster than the cap
                    // (a slope): the wheel brake comes back in
                    const float room = cap + vl;                              // vl < 0 backing
                    const float A = Clamp(p.taxiReverseAccel, 0.0f, 10.0f) * Clamp(room / 0.5f, 0.0f, 1.0f);
                    for (int k = 0; k < 3; ++k) out.acc[k] -= fg[k] * A;
                    out.wheelBrake = Clamp(-room, 0.0f, 1.0f);
                    out.backing = 1;
                }
                // not (or no longer) backing but rolling backwards with the engines at idle: a light brake, so
                // letting go of the brake button ends in a stop within a second or two instead of a long coast
                else if (occ && vl < -0.2f && s.lever <= 0.05f && thr < 0.05f) out.wheelBrake = std::fmax(out.wheelBrake, 0.35f);
            }
            if (gu > 0.0f)
            {
                // the gear is (partly) up on the ground: a belly landing, or the gear coming back down
                // after one. The hull's friction, never reversing the slide; no rotation, no steering
                // while it is more than half up (the caller takes the same share off the suspension).
                const float vg[3] = { v[0] - G[0] * Dot(v, G), v[1] - G[1] * Dot(v, G), v[2] - G[2] * Dot(v, G) };
                const float sg = std::sqrt(Dot(vg, vg));
                if (sg > 0.05f)
                {
                    const float dec = std::fmin(Clamp(p.bellyDecel, 0.0f, 50.0f) * gu, 0.5f * sg / dt);
                    for (int k = 0; k < 3; ++k) out.acc[k] -= vg[k] / sg * dec;
                }
                if (gu > 0.5f) { out.rotateRate = 0.0f; out.noseWheelDeg = 0.0f; out.belly = 1; }
                s.backing = 0; s.stopT = 0.0f; out.backing = 0;             // never on (or getting off) the belly
            }
            for (int k = 0; k < 3; ++k) { nv[k] = v[k]; wCmd[k] = in.av[k]; }
        }
        else
        {
        // rolling resistance and brakes along the heading (never reversing it), tyres across it
        const float vl = Dot(nv, fg);
        const float dec = (Clamp(p.rollDecel, 0.0f, 20.0f) + brk * Clamp(p.brakeDecel, 0.0f, 50.0f)) * dt;
        const float vl2 = std::fabs(vl) <= dec ? 0.0f : vl - (vl > 0.0f ? dec : -dec);
        for (int k = 0; k < 3; ++k) nv[k] += fg[k] * (vl2 - vl);
        const float vlat = Dot(nv, rg), kl = 1.0f - std::exp(-Clamp(p.tyreGrip, 0.0f, 50.0f) * dt);
        for (int k = 0; k < 3; ++k) nv[k] -= rg[k] * vlat * kl;
        // the virtual gear: holds the probe at gearHeight while the wing cannot carry the weight,
        // lets go (never pulls) once it can. Unknown height: gravity and the solver.
        if (hagOk)
        {
            const float servo = Clamp((gh - in.hag) * 8.0f, -2.0f, 3.0f);
            const float vn = Dot(nv, G);
            const float vn2 = Dot(a, G) <= 0.0f ? servo : (in.hag < gh ? std::fmax(vn, servo) : vn);
            for (int k = 0; k < 3; ++k) nv[k] += G[k] * (vn2 - vn);
        }
        // attitude: wings level on the wheels; the stick rotates the nose from near rotate speed
        const float Vr = Clamp(p.rotateSpeed, 1.0f, 150.0f);
        const float th = Clamp(pull, 0.0f, 1.0f) * Clamp(p.rotateDeg, 0.0f, 30.0f) * kD2R *
                         Clamp((vl - 0.85f * Vr) / (0.15f * Vr), 0.0f, 1.0f);
        float upT[3] = { G[0] * std::cos(th) - fg[0] * std::sin(th), G[1] * std::cos(th) - fg[1] * std::sin(th),
                         G[2] * std::cos(th) - fg[2] * std::sin(th) };
        float e[3]; Cross(u, upT, e);
        const float kG = 1.0f / 0.12f;
        float tilt = std::sqrt(Dot(e, e)) * kG, lim = 3.0f;
        const float ks = tilt > lim ? lim / tilt : 1.0f;
        // nose wheel: a bicycle model on the heading speed; less angle the faster it rolls
        const float fv = std::fmax(p.steerFadeV, 0.5f);
        const float st = Clamp(turn + yaw, -1.0f, 1.0f) * Clamp(p.steerDeg, 0.0f, 60.0f) * kD2R / (1.0f + (vl / fv) * (vl / fv));
        const float yr = Clamp(vl * std::tan(st) / std::fmax(p.wheelBase, 0.5f) + turn * flatYaw * 0.5f, -2.5f, 2.5f);
        for (int k = 0; k < 3; ++k) wCmd[k] = e[k] * kG * ks + G[k] * yr;
        }   // (gearMode 0 virtual gear)
    }
    else
    {
        // =========================== flying ===========================
        s.airT += dt;
        // keel: sideways air bleeds off, some of it re-aimed along the nose
        {
            const float rate = Clamp(p.keel, 0.0f, 20.0f) * Clamp(V / Vs, 0.2f, 2.0f);
            const float nvr = Dot(nv, r), kk = 1.0f - std::exp(-rate * dt), re = Clamp(p.keelReaim, 0.0f, 1.0f);
            for (int k = 0; k < 3; ++k) nv[k] += -r[k] * nvr * kk + f[k] * std::fabs(nvr) * kk * re;
        }
        // the path's own rotation this frame (the nose follows it)
        float wp[3] = { 0.0f, 0.0f, 0.0f };
        if (moving)
        {
            float acc[3], ap[3];
            for (int k = 0; k < 3; ++k) acc[k] = (nv[k] - v[k]) / dt;
            const float al = Dot(acc, vh);
            for (int k = 0; k < 3; ++k) ap[k] = acc[k] - vh[k] * al;
            Cross(vh, ap, wp);
            const float iv = 1.0f / std::fmax(V, 3.0f);
            for (int k = 0; k < 3; ++k) wp[k] = Clamp(wp[k] * iv, -3.0f, 3.0f);
        }
        const float qPath = Dot(wp, r), rPath = Dot(wp, u);

        // ---- pitch: the stick and the path hold ask for a lift; the nose goes to the AoA for it ----
        const float ph = Clamp(p.pathHold, 0.0f, 1.0f);
        // (tracks the path while the stick moves it and in knife-edge; while the hover owns the
        // vertical it is the hover's own climb-rate target, so the wing takes over level)
        if (hshare > 0.05f) s.gammaHold = moving ? std::asin(Clamp(hvyT / V, -1.0f, 1.0f)) : 0.0f;
        else if (pull != 0.0f || std::fabs(bank) > 80.0f * kD2R || !(ph > 0.0f)) s.gammaHold = gamma;
        else s.gammaHold -= s.gammaHold * Clamp(p.levelPathRate * dt, 0.0f, 1.0f);
        s.gammaHold = Clamp(s.gammaHold, -1.2f, 1.2f);
        float ng[3] = { -vh[0] * vh[1], 1.0f - vh[1] * vh[1], -vh[2] * vh[1] };   // up, square to the path
        if (!Norm(ng)) { ng[0] = u[0]; ng[1] = u[1]; ng[2] = u[2]; }
        const float cn = Dot(ld, ng);
        const float inv = cn / std::fmax(cn * cn, 0.0625f);                      // 1/cn, 0 at knife edge
        const float gdot = Clamp(1.2f * (s.gammaHold - gamma), -0.4f, 0.4f);
        const float Lhold = Clamp((g * std::cos(gamma) - T * Dot(f, ng) - hov * ng[1] + Va * gdot) * inv,
                                  -p.holdMaxG * g, p.holdMaxG * g);
        const float Lreq = ph * Lhold + (1.0f - ph) * qn * c + (pull > 0.0f ? pull * p.pullG : pull * p.pushG) * g;
        const float cReq = Lreq / std::fmax(qn, 1e-3f);
        float aReq = InvLift(p, cReq);
        // asking for more than the peak: past the stall AoA (the cap below decides how far)
        if (cReq > 1.0f) aReq = ls.as + Clamp((cReq - 1.0f) * 2.0f, 0.0f, 1.0f) * Clamp(p.overStallDeg, 0.0f, 30.0f) * kD2R;
        // AoA limits: protected up to ~70% stick back; full stick may stall; the stall breaks by itself
        const float prot = Clamp(p.stallProtect, 0.0f, 1.0f);
        const float capN = ls.as * (1.0f - 0.1f * prot);
        const float capF = ls.as + Clamp(p.overStallDeg, 0.0f, 30.0f) * kD2R;
        float cap = capN + Clamp((pull - 0.7f * prot) / (1.0f - 0.7f * prot), 0.0f, 1.0f) * (capF - capN);
        if (alpha > ls.as && moving) s.stallT += dt; else s.stallT = std::fmax(0.0f, s.stallT - 2.0f * dt);
        const float drop = Clamp(s.stallT / std::fmax(p.stallBreakS, 0.1f), 0.0f, 1.0f);
        if (cap > 0.6f * ls.as) cap -= drop * (cap - 0.6f * ls.as);
        aReq = Clamp(aReq, ls.an * (1.0f - 0.1f * prot), cap);
        float q = qPath + Clamp((aReq - alpha) / Clamp(p.alphaS, 0.03f, 2.0f), -pitchRate, pitchRate);

        // ---- yaw: follow the path sideways, yaw the sideslip out; rudder / flat turn on top ----
        float yr = rPath - Clamp(p.coordK * beta, -1.5f, 1.5f) + yaw * Clamp(p.rudderDeg, 0.0f, 180.0f) * kD2R + turn * flatYaw;

        // ---- roll ----
        const float maxBank = Clamp(p.maxBankDeg, 5.0f, 180.0f) * kD2R;
        const float pRate = -turn * rollRate;                                  // +turn rolls to -right
        float pr = pRate;
        if (p.autoLevel > 0.0f)
        {
            const float kb = turn != 0.0f ? 4.0f : 2.5f * Clamp(p.autoLevel, 0.0f, 2.0f);
            const float pb = Clamp(Wrap(-turn * maxBank - bank) * kb, -rollRate, rollRate);
            const float cth = std::sqrt(std::fmax(0.0f, 1.0f - f[1] * f[1]));
            const float wv = Clamp((cth - 0.2f) / 0.3f, 0.0f, 1.0f);             // nose near vertical: bank means nothing
            pr = wv * pb + (1.0f - wv) * pRate;
        }
        else if (maxBank < 179.0f * kD2R)
        {
            if (pRate * bank > 0.0f) pr *= Clamp((maxBank - std::fabs(bank)) / (15.0f * kD2R), 0.0f, 1.0f);
        }

        // ---- low airspeed (or hovering): AoA and sideslip mean nothing; attitude flying instead ----
        const float wA = Clamp((V - 1.0f) / 3.0f, 0.0f, 1.0f) * (1.0f - hshare);
        if (wA < 1.0f)
        {
            const float qFb = Clamp(2.5f * (pull * 0.45f - pitch), -pitchRate, pitchRate);   // stick = pitch attitude, +-26 deg
            const float yFb = yaw * Clamp(p.rudderDeg, 0.0f, 180.0f) * kD2R + turn * flatYaw;
            q = wA * q + (1.0f - wA) * qFb;
            yr = wA * yr + (1.0f - wA) * yFb;
        }
        q = Clamp(q, -4.0f, 4.0f); yr = Clamp(yr, -3.0f, 3.0f);
        // roll about the air (stability axis) when it means something, else about the nose
        float ra[3];
        const float wr = (vsOk && Dot(vsh, f) > 0.0f) ? wA : 0.0f;
        for (int k = 0; k < 3; ++k) ra[k] = f[k] * (1.0f - wr) + vsh[k] * wr;
        if (!Norm(ra)) { ra[0] = f[0]; ra[1] = f[1]; ra[2] = f[2]; }
        for (int k = 0; k < 3; ++k) wCmd[k] = ra[k] * pr + r[k] * q + u[k] * yr;
        out.stall = alpha > ls.as ? Clamp((alpha - ls.as) / std::fmax(capF - ls.as, 0.02f), 0.0f, 1.0f) : 0.0f;
    }

    // ---- write: angular velocity through the 50 ms lag; sanity lids -------------------------------
    const float aw = Lag(dt, 0.05f);
    for (int k = 0; k < 3; ++k)
    {
        out.lv[k] = nv[k];
        out.av[k] = Clamp(in.av[k] + (wCmd[k] - in.av[k]) * aw, -8.0f, 8.0f);
    }
    {
        const float vmax = std::fmin(2.0f * Vt, 150.0f), sp = std::sqrt(Dot(out.lv, out.lv));
        if (sp > vmax) for (int k = 0; k < 3; ++k) out.lv[k] *= vmax / sp;
    }
    out.airspeed = V; out.alphaDeg = alpha / kD2R; out.betaDeg = beta / kD2R; out.bankDeg = bank / kD2R;
    out.pitchDeg = pitch / kD2R; out.gammaDeg = gamma / kD2R; out.liftG = L / g; out.thrust = T;
    out.lever = s.lever; out.spool = s.spool; out.ground = s.ground == 1; out.crash = s.crash;
    out.impactSpeed = s.impactSpeed;
    out.valid = Fin3(out.lv) && Fin3(out.av) && std::isfinite(out.liftG) && std::isfinite(out.alphaDeg);
    if (real) out.valid = out.valid && Fin3(out.acc) && std::isfinite(out.rotateRate) && std::isfinite(out.noseWheelDeg);
    if (!out.valid)
    {
        for (int k = 0; k < 3; ++k) { out.lv[k] = in.lv[k]; out.av[k] = in.av[k]; }
        s.primed = 0;
        return;
    }
    for (int k = 0; k < 3; ++k) s.lastLv[k] = out.lv[k];
    s.primed = 1;
    // gearMode 1 on the wheels: nothing written, so there is no impulse to measure against next frame
    // (the first air frame after a lift-off reports dv 0)
    out.writeLv = (!real || s.ground != 1) ? 1 : 0;
    if (!out.writeLv) s.primed = 0;
}

// =============================================================================================
//  gearMode 1 helpers: pure, shared by airplane.h and test\airplane_harness.cpp
// =============================================================================================

// The rotation on the main gear (bike.h's wheelie recipe, proven in game on ApiVehicleBike, which
// shares the car wheel code): give the body pitch rate q about r (+ nose up) and move the centre of
// mass as the body turning about the pivot (the rear wheels' contact) would - so the rear wheels stay
// put and the nose wheel comes up, instead of the whole plane rocking about its middle.
inline void GearRotate(const float r[3], float q, const float com[3], const float pivot[3], float av[3], float lv[3])
{
    const float d = q - Dot(av, r);
    const float dw[3] = { r[0] * d, r[1] * d, r[2] * d };
    const float arm[3] = { com[0] - pivot[0], com[1] - pivot[1], com[2] - pivot[2] };
    float dv[3]; Cross(dw, arm, dv);
    for (int k = 0; k < 3; ++k) { av[k] += dw[k]; lv[k] += dv[k]; }
}

// Is a plane's tyre really skidding (2026-11-15: the planes' wheels skidded on every roll because they
// did not spin)? The game decides marks / squeal / smoke per wheel from its own spin rate, which the
// stock drivetrain caps far below a plane's rolling speed [P: Notes\36 - the same fault the cars had], so
// every take-off and landing roll "skidded". Ours: on the ground and - the first moment after touchdown (the
// chirp), braking hard while still fast, or sliding sideways. Pure.
inline bool PlaneSkid(bool onGround, float speed, float latSpeed, float wheelBrake, float sinceTouchdown)
{
    if (!onGround) return false;
    if (sinceTouchdown >= 0.0f && sinceTouchdown < 0.3f && speed > 5.0f) return true;
    if (wheelBrake > 0.8f && speed > 12.0f) return true;
    return (latSpeed < 0.0f ? -latSpeed : latSpeed) > 2.5f;
}

// A crash nothing flies away from (2026-11-15: a plane flown straight into the ground at full speed
// took a lot to explode - the log: a 33.8 u/s hit in the air cost nothing, a belly arrival
// sinking at 8.1 u/s cost 4 of 8). Fatal = the velocity changed by destroySpeed or more in one step (hitting
// a wall, the ground, the sea floor), or a touchdown sinking at half of it or more (twice and more the worst
// landing the gear takes), or nose / tail first at destroySpeed. A wingtip scrape and a fast but flat belly
// slide are never fatal by themselves. Pure; airplane.h takes all the health that is left.
inline bool FatalCrash(uint8_t why, float impactSpeed, float sink, float speed, const Params& p)
{
    const float f = p.destroySpeed;
    if (!(f > 0.0f) || why == CRASH_NONE) return false;
    if (why == CRASH_IMPACT) return impactSpeed >= f;
    if (sink >= 0.5f * f) return true;
    return why == CRASH_PITCH && speed >= f;
}

// The health a bad touchdown costs (plane.h's table; the collision ladder is the game's own for a car):
// a hard sink 2 (4 at twice the safe rate), a wingtip 1, nose / tail first 2 (4 at 10 u/s or more).
inline int LandingDamage(uint8_t why, float speed, float sink, const Params& p)
{
    switch (why)
    {
    case CRASH_SINK:  return sink >= 2.0f * p.safeSink ? 4 : 2;
    case CRASH_BANK:  return 1;
    case CRASH_PITCH: return speed >= 10.0f ? 4 : 2;
    case CRASH_BELLY:
    {
        // on the hull: 1 point, + 1 per bellySpeed of speed; a hard sink counts as it would on the wheels
        int n = 1 + static_cast<int>(Clamp(speed, 0.0f, 1000.0f) / Clamp(p.bellySpeed, 1.0f, 1000.0f));
        const int hard = sink >= 2.0f * p.safeSink ? 4 : (sink > p.safeSink ? 2 : 0);
        if (hard > n) n = hard;
        return n > 4 ? 4 : n;
    }
    default:          return 0;
    }
}

// The gear lever (airplane.h; pure, harness-tested). The horn button toggles it IN THE AIR only - on
// the ground the horn is a horn and the lever stays where it is: down, or up after a belly landing,
// and then it goes down by itself once the plane has stopped (so it stands up and can taxi again).
// autoDown: the caller's "low over the ground and descending" with [airplane] gear_auto_down = 1.
// Returns 0 nothing, 1 = up selected, 2 = down selected, 3 = down after a belly stop, 4 = auto down.
struct GearSw { uint8_t up, hornWas; };
enum : int { GEAR_NONE = 0, GEAR_UP = 1, GEAR_DOWN = 2, GEAR_DOWN_STOPPED = 3, GEAR_DOWN_AUTO = 4 };
inline int GearSwitch(GearSw& g, bool enabled, bool horn, bool airborne, bool stoppedOnGround, bool autoDown)
{
    const bool edge = horn && !g.hornWas;
    g.hornWas = horn ? 1 : 0;
    if (!enabled) { const bool was = g.up != 0; g.up = 0; return was ? GEAR_DOWN : GEAR_NONE; }
    if (airborne)
    {
        if (g.up && autoDown) { g.up = 0; return GEAR_DOWN_AUTO; }
        if (edge) { g.up = g.up ? 0 : 1; return g.up ? GEAR_UP : GEAR_DOWN; }
        return GEAR_NONE;
    }
    if (g.up && stoppedOnGround) { g.up = 0; return GEAR_DOWN_STOPPED; }
    return GEAR_NONE;
}

// "Gear!" - the lever is up, low over the ground and coming down. heightM is the caller's best height
// (airplane.h: above the last ground contact - crude, a car has no height probe).
inline bool GearWarn(bool leverUp, bool airborne, float height, float vy, float warnHeight)
{
    return leverUp && airborne && warnHeight > 0.0f && std::isfinite(height) && height < warnHeight && vy < -0.3f;
}

// ---------------------------------------------------------------------------------------------
//  What the wheel JOINTS show (airplane.h WheelVisual; pure, harness-tested). The engine draws each
//  wheel mesh on its joint, posed every frame from the wheel's matrix in the vehicle's own space
//  (wheel+0x50: rows = axes, row 3 = position; x right, y up from the ground, forward = -z):
//      joint = spin about local X by wheel+0x128 / radius  x  steer  x  [wheel+0x50]  x  body
//  [P: ApiVehicleJointModifier 0x1404DA160]. A pose is a rotation and a position - no scale - so a
//  wheel cannot be shrunk away: the gear goes up INTO the hull, and a propeller is a "wheel" whose
//  joint sits on the propeller hub with its axle along the shaft.
// ---------------------------------------------------------------------------------------------
enum : int { WHEELVIS_NONE = 0, WHEELVIS_GEAR = 1, WHEELVIS_PROPS = 2, WHEELVIS_PARTS = 3 };

// By model (the definition's name): the Elifant has real wheel meshes - its gear retracts; the
// ThunderRoc's four main wheel joints carry the propellers (World\planes contrib_planes v2) and its
// nose wheel joint the whole landing gear (v3); the Pegasus' nose and main wheel joints carry its
// gear, wheels and legs (v3: WHEELVIS_PARTS, GearPartFor below); everything else is left alone.
inline int WheelVisKind(const char* name)
{
    if (!name) return WHEELVIS_NONE;
    auto same = [](const char* a, const char* b) {
        for (;; ++a, ++b)
        {
            const char x = (*a >= 'a' && *a <= 'z') ? static_cast<char>(*a - 32) : *a;
            if (x != *b) return false;
            if (!x) return true;
        }
    };
    if (same(name, "AIRCRAFT_ELIFANT")) return WHEELVIS_GEAR;
    if (same(name, "AIRCRAFT_THUNDERROC")) return WHEELVIS_PROPS;
    if (same(name, "AIRCRAFT_PEGASUS")) return WHEELVIS_PARTS;
    return WHEELVIS_NONE;
}

// Where a wheel is stowed, in the vehicle's space. The Elifant [P: its .ghg re-parsed, gear_work\g4.py -
// at these joint positions every vertex of the wheel mesh has hull triangles in all 26 directions, on
// both LODs]: the four main wheels (x +-0.38, y 0.07) go up and in under the cabin floor, the nose wheel
// (z -1.32) up and 0.17 aft.
struct GearStow { float mainX, mainY, noseY, noseAft; };
inline GearStow ElifantStow() { return GearStow{ 0.10f, 0.574f, 0.674f, 0.17f }; }

// The drawn gear follows the gear (0 = down .. 1 = up) at the gear's own speed: it tracks a travelling
// gear exactly, and travels by itself when the target jumps (the pilot got out with the gear up).
inline float GearVisStep(float vis, float target, float dt, float timeS)
{
    if (!(vis >= 0.0f)) vis = 0.0f;
    if (!(target >= 0.0f)) target = 0.0f;
    if (vis > 1.0f) vis = 1.0f;
    if (target > 1.0f) target = 1.0f;
    if (!(dt > 0.0f) || !std::isfinite(dt)) return vis;
    const float step = Clamp(dt, 0.0f, 0.1f) / Clamp(timeS, 0.1f, 20.0f);
    return target > vis ? std::fmin(vis + step, target) : std::fmax(vis - step, target);
}

// The stowed position for a wheel whose engine position is base[3]: eased (no jerk at either end),
// exactly base at 0 and exactly the stow point at 1. False (out = base) for anything not finite.
inline bool GearStowPos(const float base[3], float vis, const GearStow& st, float out[3])
{
    for (int k = 0; k < 3; ++k) out[k] = base[k];
    if (!Fin3(base) || !(vis > 0.0f)) return false;
    const float u = vis >= 1.0f ? 1.0f : vis;
    const float e = u * u * (3.0f - 2.0f * u);
    const bool nose = std::fabs(base[0]) < 0.05f;                      // the centreline wheel
    float tgt[3];
    if (nose) { tgt[0] = base[0]; tgt[1] = st.noseY; tgt[2] = base[2] + st.noseAft; }
    else
    {
        const float ax = std::fmin(std::fabs(base[0]), st.mainX);       // inward, never across the centreline
        tgt[0] = base[0] < 0.0f ? -ax : ax; tgt[1] = st.mainY; tgt[2] = base[2];
    }
    for (int k = 0; k < 3; ++k) out[k] = u >= 1.0f ? tgt[k] : base[k] + (tgt[k] - base[k]) * e;
    return true;
}

// ---- gear that is a PART of the model (contrib_planes3, planekit.py GEAR / GEAR_ANCHOR - the same
// numbers) ---------------------------------------------------------------------------------------
// The Pegasus' and the ThunderRoc's wheels and gear legs were cut out of the body mesh and are the
// meshes of wheel joints, stored relative to an anchor with no rotation. The joint's whole matrix is
// ours every frame: at the anchor with the gear down, and with it up moved by `move` and pitched by
// pitchDeg about the lateral axis through (pivotY, pivotZ) - [P: World\planes verify_gear.py on the
// re-parsed .ghg: at vis 1 every vertex of the Pegasus' three parts has hull triangles in all 26
// directions; the ThunderRoc's one part (a rigid part cannot be mirrored: straight up, tilted 0.85
// degrees so the nose leg rises 0.025 more) is inside the fuselage and the wheel pods - 91 % of its
// vertices fully enclosed, the rest seen from below (the pods are open underneath) and, 1.5 %, the
// crowns of the outer wheels standing up to 0.03 through the pods' rounded shoulders]. Which part a wheel carries goes by where the engine has the wheel (base = its own
// wheel+0x50 position): the centreline wheel the nose gear, a +x wheel the +x main gear [P: mesh slot
// 1 is a +x joint - the Elifant's own wheel mesh 1 only fits its legs there], -x the mirror image.
struct GearPart { float anchor[3]; float move[3]; float pivotY, pivotZ; float pitchDeg; };

inline bool GearPartFor(int kind, const float base[3], GearPart& p)
{
    if (!Fin3(base)) return false;
    const bool nose = std::fabs(base[0]) < 0.05f;
    if (kind == WHEELVIS_PARTS)                                        // the Pegasus
    {
        if (nose) p = GearPart{ { 0.0f, 0.0686f, -1.3192f }, { 0.0f, 0.27f, 0.12f }, 0.0f, 0.0f, 0.0f };
        else
        {
            const float sg = base[0] > 0.0f ? 1.0f : -1.0f;
            p = GearPart{ { 0.3812f * sg, 0.0686f, 0.32f }, { -0.21f * sg, 0.30f, 0.0f }, 0.0f, 0.0f, 0.0f };
        }
        return true;
    }
    if (kind == WHEELVIS_PROPS && nose)                                // the ThunderRoc: all of its gear, one part
    {
        p = GearPart{ { 0.0f, 0.0686f, -1.3192f }, { 0.0f, 0.185f, 0.0f }, 0.07f, -0.015f, 0.85f };
        return true;
    }
    return false;
}

// The joint's matrix (rows = the images of local X, Y, Z; row 3 = position) with the gear drawn
// vis = 0 (down) .. 1 (up), eased like GearStowPos: model = local x R + position, R = the pitch about
// x, position = (anchor - pivot) x R + pivot + move x e. Exactly the identity at the anchor at 0.
inline void GearPartMatrix(const GearPart& p, float vis, float m[16])
{
    const float u = !(vis > 0.0f) ? 0.0f : (vis >= 1.0f ? 1.0f : vis);
    const float e = u * u * (3.0f - 2.0f * u);
    const float th = p.pitchDeg * 0.017453293f * e, c = std::cos(th), s = std::sin(th);
    const float R[3][3] = { { 1.0f, 0.0f, 0.0f }, { 0.0f, c, s }, { 0.0f, 0.0f - s, c } };   // (0 - s: +0 at rest, bit for bit)
    for (int r = 0; r < 3; ++r) { for (int k = 0; k < 3; ++k) m[4 * r + k] = R[r][k]; m[4 * r + 3] = 0.0f; }
    const float d[3] = { p.anchor[0], p.anchor[1] - p.pivotY, p.anchor[2] - p.pivotZ };
    const float piv[3] = { 0.0f, p.pivotY, p.pivotZ };
    for (int k = 0; k < 3; ++k)
        m[12 + k] = d[0] * R[0][k] + d[1] * R[1][k] + d[2] * R[2][k] + piv[k] + p.move[k] * e;
    m[15] = 1.0f;
}

// ---- propellers ------------------------------------------------------------------------------
// The hub each main wheel joint carries, by the joint's name in the wheel definition ("WheelPosML" ..):
// planekit.py PROP_HUBS, the same table - the propeller meshes are stored around these points.
inline bool PropHubFor(const char* joint, float hub[3])
{
    if (!joint) return false;
    int n = 0;
    while (n < 64 && joint[n]) ++n;
    if (n < 3 || n >= 64) return false;
    const char a = static_cast<char>(joint[n - 2] & ~0x20), b = static_cast<char>(joint[n - 1] & ~0x20);
    if ((a != 'M' && a != 'R') || (b != 'L' && b != 'R')) return false;
    const bool outer = a == 'R';
    hub[0] = (outer ? 1.4478f : 0.8382f) * (b == 'L' ? -1.0f : 1.0f);
    hub[1] = 0.7033f;
    hub[2] = outer ? -0.5715f : -0.8763f;
    return true;
}

// The joint's matrix on a hub: local X (the axis the engine spins a wheel about) along the shaft
// (the vehicle's z), local Y up, local Z = -x. planekit.py PROP_ROT; a proper rotation.
inline void PropMatrix(const float hub[3], float m[16])
{
    static const float R[3][3] = { { 0.0f, 0.0f, 1.0f }, { 0.0f, 1.0f, 0.0f }, { -1.0f, 0.0f, 0.0f } };
    for (int r = 0; r < 3; ++r) { for (int c = 0; c < 3; ++c) m[4 * r + c] = R[r][c]; m[4 * r + 3] = 0.0f; }
    m[12] = hub[0]; m[13] = hub[1]; m[14] = hub[2]; m[15] = 1.0f;
}

// Revolutions per second the propellers should turn at: idle with someone aboard, up to maxRps with
// the engine (spool 0..1) or the airspeed (a windmilling share), 0 when nobody is aboard.
inline float PropTargetRps(bool occupied, float spool, float airspeed, float topSpeed, float idleRps, float maxRps)
{
    if (!occupied) return 0.0f;
    const float idle = Clamp(idleRps, 0.0f, 20.0f), top = std::fmax(Clamp(maxRps, 0.0f, 20.0f), idle);
    const float e = std::isfinite(spool) ? Clamp(spool, 0.0f, 1.0f) : 0.0f;
    const float w = (std::isfinite(airspeed) && topSpeed > 1.0f) ? 0.6f * Clamp(airspeed / topSpeed, 0.0f, 1.0f) : 0.0f;
    return idle + (top - idle) * std::fmax(e, w);
}

// Spin up in about 1.5 s, run down in about 5 (a propeller keeps turning after the engine is cut).
inline float PropRpsStep(float rps, float target, float dt, float maxRps)
{
    if (!(rps >= 0.0f)) rps = 0.0f;
    if (!(dt > 0.0f) || !std::isfinite(dt)) return rps;
    const float top = std::fmax(Clamp(maxRps, 0.0f, 20.0f), 0.5f), h = Clamp(dt, 0.0f, 0.1f);
    return target > rps ? std::fmin(rps + h * top / 1.5f, target) : std::fmax(rps - h * top / 5.0f, target);
}

// The angle, radians in [0, 2 pi). One frame never turns it more than 50 degrees: a three-blade
// propeller that steps 60 looks stopped and one that steps more looks as if it ran backwards.
inline float PropAngleStep(float angle, float rps, float dt)
{
    const float twoPi = 6.2831853f;
    if (!(angle >= 0.0f) || !(angle < twoPi)) angle = 0.0f;
    if (!(rps > 0.0f) || !(dt > 0.0f) || !std::isfinite(dt)) return angle;
    angle += std::fmin(rps * twoPi * dt, 50.0f * 0.017453293f);
    if (angle >= twoPi) angle -= twoPi;
    return angle >= twoPi || !(angle >= 0.0f) ? 0.0f : angle;
}

// One wheel's ContactJoint rows (semantics: Notes\20; model.h WheelConstraint):
//   lateral: target 0, |force| <= latCap, softness latSoft (force ~ 8 * slip speed / latSoft): a tyre
//            that holds up to mu * N sideways with a slip angle of ~alpha at the limit;
//   long:    target 0, |force| <= longCap - a FREE-ROLLING wheel (rolling resistance) plus the brake.
//            Never a motor: the thrust is a body force, and the stock drive motor would hold the
//            plane at the definition's Max Velocity (Notes\20 §1, Notes\23 §1).
struct WheelRows { float latCap, latSoft, longCap; };
inline WheelRows GearWheelRows(float N, float mu, float alpha, float vLong, float rolling, float brakeMu,
                               float wheelBrake, bool brakeWheel)
{
    WheelRows w;
    const float n = (std::isfinite(N) && N > 0.0f) ? N : 0.0f;
    const float F = Clamp(mu, 0.0f, 5.0f) * n;
    w.latCap = F;
    const float visc = std::fmax(F, 1.0f) / Clamp(alpha, 0.01f, 1.0f) / std::fmax(std::isfinite(vLong) ? std::fabs(vLong) : 0.0f, 1.2f);
    w.latSoft = 8.0f / visc;
    w.longCap = (Clamp(rolling, 0.0f, 1.0f) + (brakeWheel ? Clamp(wheelBrake, 0.0f, 1.0f) * Clamp(brakeMu, 0.0f, 3.0f) : 0.0f)) * n;
    return w;
}

// The nose-wheel watchdog (the rule: a config key plus a watchdog for every sign composition).
// In the air +turn yaws toward -right, +(av . up) - heli.h's live calibration. On the wheels the same
// stick must yaw the same way; if it yaws the other way for 1 s while steering at 2-15 u/s, the
// nose-wheel sign is reversed. Fires once (true); never flips anything itself.
struct SteerWatch { float wrongT; uint8_t fired; };
inline bool SteerWatchStep(SteerWatch& w, bool ground, float vl, float noseDeg, float turn, float yawRate, float dt)
{
    if (w.fired || !(dt > 0.0f) || dt > 0.25f) return false;
    const bool judge = ground && vl > 2.0f && vl < 15.0f && std::fabs(noseDeg) > 5.0f && turn != 0.0f &&
                       std::isfinite(yawRate) && std::fabs(yawRate) > 0.05f;
    if (judge && yawRate * turn < 0.0f) w.wrongT += dt;
    else w.wrongT = std::fmax(0.0f, w.wrongT - dt);
    if (w.wrongT >= 1.0f) { w.fired = 1; return true; }
    return false;
}

// [airplane] models (airplane.h KindFromName): is this model name an AIRCRAFT_* listed in `list` - the
// names after AIRCRAFT_, separated by commas / spaces, case-insensitive, whole names only - or is the
// list "all"? An empty list flies nothing.
inline bool AircraftListed(const char* name, const char* list)
{
    if (!name || !list) return false;
    auto low = [](char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; };
    auto same = [&](const char* a, const char* b, size_t n) { for (size_t i = 0; i < n; ++i) if (low(a[i]) != low(b[i])) return false; return true; };
    static const char kPre[] = "AIRCRAFT_";
    for (size_t i = 0; i < 9; ++i) if (!name[i] || low(name[i]) != low(kPre[i])) return false;
    const char* m = name + 9;
    size_t n = 0; while (m[n]) ++n;
    if (n == 0) return false;
    {
        const char* s = list; while (*s == ' ') ++s;
        size_t k = 0; while (s[k] && s[k] != ' ' && s[k] != ',') ++k;
        const char* t = s + k; while (*t == ' ') ++t;
        if (k == 3 && same(s, "all", 3) && !*t) return true;
    }
    const char* s = list;
    while (*s)
    {
        while (*s == ',' || *s == ' ') ++s;
        const char* e = s;
        while (*e && *e != ',' && *e != ' ') ++e;
        if (static_cast<size_t>(e - s) == n && same(s, m, n)) return true;
        s = e;
    }
    return false;
}

// The airliner's own definition (airplane.h DefTick): [0] Max Velocity (OnCollision's damage ladder,
// the camera's speed blend), [1] [2] FollowHeight / FollowDistance, [3] [4] ... Max, [5] [6] ... Boost.
// want[i] <= 0 = leave that field alone. Only a definition with the shipped (5 / 0.75 / 2.0) or the
// staged record's (25 / 1.6 / 6.5) signature is ever adopted; what it held is kept and given back,
// and a field is given back only while it still holds what was written (someone else's value stays).
struct DefHold { uint8_t held, staged, mask; float orig[7], wrote[7]; };
enum : int { DEF_BAD = -1, DEF_HELD = 0, DEF_SAME = 1, DEF_PATCH = 2 };

inline int DefAdopt(DefHold& h, const float cur[7], const float want[7], const float shipped[3], const float staged[3])
{
    h = DefHold{};
    const bool isShipped = cur[0] == shipped[0] && cur[1] == shipped[1] && cur[2] == shipped[2];
    const bool isStaged  = cur[0] == staged[0] && cur[1] == staged[1] && cur[2] == staged[2];
    if (!isShipped && !isStaged) return DEF_BAD;
    h.held = 1; h.staged = isStaged ? 1 : 0;
    for (int i = 0; i < 7; ++i) h.orig[i] = cur[i];
    for (int i = 0; i < 7; ++i) if (want[i] > 0.0f && cur[i] != want[i]) return DEF_PATCH;
    return DEF_SAME;
}

// Every frame while held: write what the ini wants; a field set to 0 in the ini since is given back.
// Returns how many fields changed now.
inline int DefApply(DefHold& h, float cur[7], const float want[7])
{
    if (!h.held) return 0;
    int n = 0;
    for (int i = 0; i < 7; ++i)
    {
        const uint8_t bit = static_cast<uint8_t>(1u << i);
        if (want[i] > 0.0f)
        {
            if (cur[i] != want[i]) { cur[i] = want[i]; h.wrote[i] = want[i]; h.mask = static_cast<uint8_t>(h.mask | bit); ++n; }
        }
        else if (h.mask & bit)
        {
            if (cur[i] == h.wrote[i]) { cur[i] = h.orig[i]; ++n; }
            h.mask = static_cast<uint8_t>(h.mask & ~bit);
        }
    }
    return n;
}

// Give it all back (`[`, [airplane] enable = 0, off the list, a fault, unhook). Returns the fields restored.
inline int DefGiveBack(DefHold& h, float cur[7])
{
    int n = 0;
    if (h.held)
        for (int i = 0; i < 7; ++i)
            if ((h.mask & (1u << i)) && cur[i] == h.wrote[i]) { cur[i] = h.orig[i]; ++n; }
    h = DefHold{};
    return n;
}

} // namespace plane
