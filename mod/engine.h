// =============================================================================================
//  engine.h - everything lcudrive knows about LEGOLCUR_DX11.exe, in one place.
//
//  Build fingerprint: TimeDateStamp 0x591B08C3, SizeOfImage 0x223E000 (Steam build).
//  Every RVA is relative to the module base; preferred-base VA = 0x140000000 + RVA, which is what
//  the notes and Tools\re2 print. Evidence: Notes\20 (constraint/solver), 22 (steering),
//  21 (drivetrain), 23 (assists), and Tools\re2\dumps.
// =============================================================================================
#pragma once
#include <cstdint>

namespace eng {

static const uint32_t kImageStamp = 0x591B08C3u;
static const uint32_t kImageSize  = 0x223E000u;

// ---- vtables (slot 91 = Update) ------------------------------------------------------------
static const uintptr_t RVA_VT_CAR  = 0xCE3368;   // ApiVehicleCar
static const uintptr_t RVA_VT_HOVER = 0xCDD638;  // ApiVehicleHover (flyable helicopters; also the planes)
static const uintptr_t RVA_VT_BIKE  = 0xCE51D8;  // ApiVehicleBike (motorbikes; RTTI-checked)
static const uintptr_t FN_BIKE_UPDATE = 0x4EAFC0;  // what its slot 91 must hold
static const size_t    SLOT_UPDATE = 91;

// ---- helicopters (Notes\11, 26, 27, 28) -----------------------------------------------------
// int SIMULATION LEVEL OF DETAIL (not a damage stage - corrected 2026-11-13): 1 full physics, 5 reduced
// (far / off-screen: the hover update skips its servo and terrain probe), 10 asleep. Set every frame from
// camera distance by 0x1404BE0E0; the player's own vehicle is held at 1. Health: vehdamage.h.
static const uintptr_t VEH_SIMLOD    = 0xEF8;
static const uintptr_t VEH_CLASS     = 0x340;    // int; 6 = Hover (helicopters), 4 = the other flyers
static const uintptr_t VEH_USERDATA  = 0x378;    // -> user data + 0x10 (ud = *(veh+0x378) - 0x10)
static const uintptr_t VEH_FLAGS4    = 0xFC4;    // bit0 = spawned
static const uintptr_t VEH_FLAGS5    = 0xFC5;    // 0x80 | 0x20 = parked / disabled
static const uintptr_t VEH_FLAGS8    = 0xFC8;    // bit2 = occupied (= ud state != 0)
static const uintptr_t VEH_HDRIVE    = 0xFE0;    // -> height drive; +0x44 height above ground (terrain probe)
static const uintptr_t HD_HEIGHT_ACT = 0x14;     // HeightTargetActive (the pad exit zeroes it)
static const uintptr_t HD_OVERRIDE   = 0x1C;     // one-shot hover height override
static const uintptr_t HD_HAG        = 0x44;
static const uintptr_t IN_X          = 0x00;     // input block axes: turn
static const uintptr_t IN_Y          = 0x04;     //                   forward / back
static const uintptr_t IN_Z          = 0x08;     //                   ascend / descend
static const uintptr_t INST_IGNORE   = 0x1E;     // u16 on the Dyno entity [veh+0x348]: groups it ignores
static const uint16_t  GROUP_BOUNDARY_BIT = 0x0010;   // group 4 = vehicle-only boundary blockers (Notes\27 5)

// ApiVehicleUserData_HoveringAircraft: the pilot side of every Hover vehicle (Notes\26)
static const uintptr_t RVA_VT_HELIUD = 0xC60F08;  // HoveringAircraft
static const uintptr_t RVA_VT_FIREUD = 0xC61208;  // FireHelicopter (derives from it)
static const uintptr_t UD_SEATREF    = 0x18;      // weak ref: object; control block at +0x20 ([ctl+4] != 0 = alive)
static const uintptr_t UD_SEATCTL    = 0x20;
static const uintptr_t UD_SEATLOCK   = 0x34;      // on the seat object: bit0 = pilot seat script-locked
static const uintptr_t UD_STATE      = 0x12E0;    // 0 parked, 1 boarding, 2 FLYING, 3 pad glide, 4 landed/exit,
                                                  // 5 exit finishing, 6/7 crash mat, 8 lift-away + pinned, 9 turn-around

// the pilot (MechCharacter) and the ride (MechRidingDataVehicle)
static const uintptr_t CH_RIDE       = 0x340;     // -> ride; vt[68] = Dismount(params*), +0xD0 ride state (4 = riding)
static const uintptr_t RIDE_STATE    = 0xD0;
static const size_t    SLOT_DISMOUNT = 68;

// ---- helicopter patches (every one a single byte, so it can be switched on and off live) -----
static const uintptr_t SITE_HELI_NOEXIT = 0x1A9154;  // call HintFlag(0x36 = HINT_HELI_NOEXIT): the off-pad exit
static const uintptr_t FN_HINT_FLAG     = 0x877CA0;
static const uintptr_t RVA_HELI_YMAX    = 0x101156C; // .data float 35.0: ascend input zeroed above this world Y
static const uintptr_t FN_CHAR_SETSTATE = 0x66FCB0;  // (char, stateId, -1, 0, 0)
static const uintptr_t RVA_STATE_FALLING = 0x116AD80; // int: the "Falling" state id (registered at boot)
static const uintptr_t FN_VEH_SLEEP     = 0x4BE6C0;  // (veh, bool force): sim LOD 10 + vt[88] Sleep. NOT a wreck (2026-11-13)
static const uintptr_t FN_HELI_TERRAIN  = 0x4E1B60;  // (drive): raycast -> drive+0x40 ground y, +0x44 height above it.
                                                     // The hover update runs it only at damage stage 1.

// ---- player character -> vehicle (proven across every vehsim session since 2026-08-18) ------
static const uintptr_t RVA_PLAYER1 = 0x1C7D3E0;  // NuMechPtr cache -> MechCharacter*
static const uintptr_t RVA_PLAYERS = 0x1C74920;  // MechCharacter* g_players[2]
static const uintptr_t MC_VEHREF   = 0x428;      // handle; *handle == ApiVehicle*
static const uintptr_t MC_VEHBLK   = 0x430;      // ref-control block; [blk+4] == 0 -> dead

// ---- call sites we redirect (E8 rel32 at siteRva, must resolve to calleeRva) -----------------
static const uintptr_t SITE_STEER      = 0x4E08C1;  // in vslot 0x310: call UpdateSteering
static const uintptr_t FN_STEER        = 0x4E0E50;  //   UpdateSteering(veh)
static const uintptr_t SITE_SPIN       = 0x4DFC7B;  // in ApiVehicleCar::Update: call SpinDonut
static const uintptr_t FN_SPIN         = 0x4E1350;  //   wheel-spin / donut state machine(veh)
static const uintptr_t SITE_CONSTRAINT = 0x4DA626;  // in per-wheel 0x1404DA540: call WheelConstraint
static const uintptr_t FN_CONSTRAINT   = 0x4DB720;  //   configures the wheel ContactJoint(wheel)
static const uintptr_t SITE_DRAG       = 0x4AA740;  // in base ApiVehicle::Update: call VehicleDrag
static const uintptr_t FN_DRAG         = 0x4AC080;  //   F = -0.5*m*lerp3(Drag;0.1,0.75,2)*|v_h|*v_hat (LINEAR)

// ---- engine functions we call ------------------------------------------------------------------
static const uintptr_t FN_WHEEL_SETSTEER = 0x4DA7D0;  // (wheel, float deg): wheel+0x130, rear counter-steer
static const uintptr_t FN_JOINT_LIMITS   = 0xAD2BB0;  // (c, const float* axis, latHi, longHi, latLo, longLo)
static const uintptr_t FN_JOINT_SOFT     = 0xAD2C00;  // (c, latSoft, longSoft)
static const uintptr_t FN_JOINT_TARGET   = 0xAD2C80;  // (c, latTarget, longTarget)
static const uintptr_t FN_APPLY_FORCE    = 0xABDDB0;  // (body, const float* F, const float* P)
static const uintptr_t FN_POINT_VELOCITY = 0xABEBF0;  // (body, const float* P, float* out)

// ---- ApiVehicle -----------------------------------------------------------------------------
static const uintptr_t VEH_INSTANCE   = 0x348;   // -> instance (world matrix +0x60, body +0xC8)
static const uintptr_t VEH_DEF        = 0x358;   // -> VehicleDefCar, SHARED per model: never written
static const uintptr_t VEH_SRCCTRL    = 0x360;   // -> source input block (written by player/AI)
static const uintptr_t VEH_CTRL       = 0x368;   // -> working input block (copied from source in Update)
static const uintptr_t VEH_WHEELS     = 0x388;   // -> first ApiVehicleWheel
static const uintptr_t VEH_DRIVETRAIN = 0x3A0;
static const uintptr_t VEH_SURFACES   = 0x3E0;   // per-surface table: [veh+0x3E0+id*8] -> +0x10 -> +0x28 grip
static const uintptr_t VEH_PATHFOLLOW = 0x4C8;   // non-null while a scripted path drives the car
static const uintptr_t VEH_GRAVITY    = 0x2EC;
static const uintptr_t VEH_DT         = 0xF48;
static const uintptr_t VEH_FLAGS7     = 0xFC7;   // bit1 = idle (engine forces the park brake)
static const uintptr_t VEH_STEER_BAM  = 0x102C;  // int, steering angle in 16-bit units (65536 = 360 deg)
static const uintptr_t VEH_STEER_DEG  = 0x1028;  // float, the same in degrees (+ = left)

// ---- instance --------------------------------------------------------------------------------
static const uintptr_t INST_ROW0 = 0x60;   // right
static const uintptr_t INST_ROW1 = 0x70;   // up
static const uintptr_t INST_ROW2 = 0x80;   // back   (engine forward = -row2)
static const uintptr_t INST_POS  = 0x90;
static const uintptr_t INST_BODY = 0xC8;   // -> rigid body (MotionState)

// ---- rigid body (MotionState) ---------------------------------------------------------------
static const uintptr_t BODY_POS     = 0x00;
static const uintptr_t BODY_LINVEL  = 0x20;
static const uintptr_t BODY_ANGVEL  = 0x30;
static const uintptr_t BODY_MASS    = 0x70;
static const uintptr_t BODY_INVMASS = 0x74;
static const uintptr_t BODY_INVINERTIA_Y = 0x7C;   // local inverse inertia, yaw (spawn divides by 1.5)
static const uintptr_t BODY_LINDAMP = 0xA0;   // v *= 1 - h*k  (0 for cars)
static const uintptr_t BODY_ANGDAMP = 0xA4;   // w *= 1 - h*k  (engine default 1.0 /s)

// ---- drivetrain [veh+0x3A0] (Notes) ----------------------------------------------------------
static const uintptr_t DRV_RPM     = 0x08;   // RPM8, the engine
static const uintptr_t DRV_RPM_LAG = 0x0C;   // RPMc, lagged copy
static const uintptr_t DRV_RPM_SFX = 0x10;   // what the engine sound plays

// ---- input block ------------------------------------------------------------------------------
static const uintptr_t CTRL_STEER     = 0x00;   // [-1, 1]
static const uintptr_t CTRL_THROTTLE  = 0x0C;   // [0, 1]
static const uintptr_t CTRL_REVERSE   = 0x10;   // brake/reverse button when stopped or reversing
static const uintptr_t CTRL_BRAKE     = 0x14;   // service brake (auto park brake when idle)
static const uintptr_t CTRL_HANDBRAKE = 0x18;   // classic scheme: the brake button while moving forward
static const uintptr_t CTRL_ACTIVE    = 0x34;   // byte (source block)
static const uintptr_t CTRL_DIRECT    = 0x3B;   // byte, AI/script direct steer

// ---- ApiVehicleWheel ---------------------------------------------------------------------------
static const uintptr_t WHL_NEXT       = 0x008;
static const uintptr_t WHL_REST       = 0x0C0;   // row3 of the rest (mount) matrix, model space
static const uintptr_t WHL_CONTACT    = 0x0D0;   // world contact point
static const uintptr_t WHL_NORMAL     = 0x0F0;   // contact normal
static const uintptr_t WHL_SPRINGF    = 0x120;   // this frame's suspension force (as applied)
static const uintptr_t WHL_SPINRATE   = 0x12C;
static const uintptr_t WHL_STEERDEG   = 0x130;
static const uintptr_t WHL_OWNER      = 0x158;
static const uintptr_t WHL_DEF        = 0x160;   // -> VehicleDefWheel (shared per model)
static const uintptr_t WHL_JOINT      = 0x188;   // -> ContactJoint
static const uintptr_t WHL_RAYLEN     = 0x1A0;
static const uintptr_t WHL_RADIUS     = 0x1A4;
static const uintptr_t WHL_AIRBORNE   = 0x1B8;   // byte, 0 = on the ground
static const uintptr_t WHL_SURFACE    = 0x1C0;   // int

// ---- ContactJoint / its parameter block d = [c+0x90] -------------------------------------------
static const uintptr_t JNT_ENABLED = 0x28;   // byte
static const uintptr_t JNT_POINT   = 0x40;
static const uintptr_t JNT_NORMAL  = 0x50;
static const uintptr_t JNT_PARAMS  = 0x90;   // -> d
static const uintptr_t JP_LAT_HI   = 0x10, JP_LONG_HI = 0x14, JP_LAT_LO = 0x18, JP_LONG_LO = 0x1C;
static const uintptr_t JP_LAT_SOFT = 0x20, JP_LONG_SOFT = 0x24;
static const uintptr_t JP_LAT_TGT  = 0x3C, JP_LONG_TGT = 0x40;

// ---- VehicleDefWheel ---------------------------------------------------------------------------
static const uintptr_t WDEF_DRIVE     = 0x111;  // byte
static const uintptr_t WDEF_HANDBRAKE = 0x112;  // byte
static const uintptr_t WDEF_BRAKE     = 0x113;  // byte
static const uintptr_t WDEF_STEERABLE = 0x118;  // float
static const uintptr_t WDEF_TYREGRIP  = 0x138;  // float 0..1 (stock remap: 0.5 -> 1.0)

// ---- VehicleDefCar (READ ONLY) -----------------------------------------------------------------
static const uintptr_t DEF_MAXVEL   = 0x174;
static const uintptr_t DEF_POWERMAX = 0x1F0;
static const uintptr_t DEF_RPMLIMIT = 0x1F8;
static const uintptr_t DEF_MAXANGLE = 0x1AF4;

// model name: def -> +0x58 -> +0x48 -> +0x20 -> char*
static const uintptr_t NAME_A = 0x58, NAME_B = 0x48, NAME_C = 0x20;

} // namespace eng
