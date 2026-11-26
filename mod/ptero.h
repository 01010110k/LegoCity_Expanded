// =============================================================================================
//  ptero.h - the Pteranodon flies (2026-11-15). The flight itself is ptero_model.h (pure, run by
//  test\ptero_harness.cpp); this is the engine side. The creature is the cut rideable Pteranodon
//  dino.h puts on Port Atlas's lookout ([dino] model2); anything ridden whose definition name holds
//  [pteranodon] creature flies the same way, for each player on their own.
//
//  Where the write goes, and why it sticks [P, static]. A character's per-frame update is
//  MechCharacter vtable 0x140D1C7E8 slot 65 = 0x14066C6A0 (this = ch+0x10; the only vtable with that
//  slot [P xref]): its contexts and add-ons set the velocity (messages 3 and 0x21 - the jetpack's and
//  the skydive's Move write it there), then its tail 0x14049FCF0 runs the movement integrator
//  0x14049EE00. That integrator still CHANGES the velocity before using it: + gravity * dt when
//  this+0x213 is set (0x14049EF78..0x14049F00B), the animation's root motion added or substituted
//  (this+0x210 0x14049EEA0, this+0x215 / +0x217 0x14049F216..0x14049F321), a ground platform's
//  motion (0x14049F4C5..0x14049F582) - and then makes the ONE call that moves the body:
//  0x14049F5B5 `call 0x140AB7A00` (rcx = the character's physics object ch+0x210, xmm1 = dt), the
//  swept move: position phys+0x90 += velocity phys+0xF0 * dt with collision, then the ground probe
//  0x140AB71A0 (phys+0x158 = the ground body, cleared and re-found) [P]. lcudrive retargets that
//  call (hooks.h CallPatch "ptero sweep"; 0x140AB7A00's only other caller is 0x14068EEB0, not the
//  per-frame path). For the creatures a player rides (two pointer compares; everyone else goes
//  straight through), our velocity is written to phys+0xF0 immediately before the original runs -
//  nothing can come between the write and the move, so it sticks by construction. The jetpack and
//  the skydive prove the game itself flies characters by velocity writes in that same update
//  (Notes\29, \30, played). After the move we read where it got to: the sweep's collisions (a wall,
//  the ground) show as a shortfall against where it was sent.
//  The body's attitude: the character's matrix rows are ch+0x1C0 (X), +0x1D0 (Y), +0x1E0 (Z,
//  forward = -Z), +0x1F0 (position) - built from a yaw by 0x1404A0020 [P]; the integrator only turns
//  them about Y toward the target yaw ch+0x218 (0x14049F010..0x14049F202, before the sweep) and,
//  after the sweep, copies them to the render instance and the animation (0x1404A1010 ->
//  0x14049FF30 / this+0x68) [P]. So rows written in the same pre-sweep window reach the screen as
//  written: yaw, bank AND pitch (full rows, not just a yaw) [P path, U looks]; ch+0x218 = our yaw
//  so nothing turns it back. On every hand-back the rows are rebuilt upright ([U]: nothing else
//  would level a tilted character - the game's own turning keeps any tilt).
//  Inputs: JUMP from the rider's own game pad (ch+0x6A8 -> +0x18, held bits +0x0C, the JUMP bit
//  [0x141174CB4]) - keyboard and controller alike, each player their own; the stick from that
//  player's XInput port (pad.h, [coop] pad_p1 / pad_p2), else for player 1 W/A/S/D, else the game
//  pad's stick floats +0x3C / +0x40 [I: x right +, y forward +, read off 0x14019B42F / 0x14019B661].
//  Engine reads: IsOnGround 0x14066BD20, IsInContext 0x140006D40 (Swimming / Drowning / Death /
//  Doomed ids), the character's water surface 0x14066BA70(ch, &y, 1) (the drowning classifier's own
//  query, 0x140687640) [P].
//  The rider: if they get off in the air, the creature glides down on its own (coast) and the rider
//  falls; with [pteranodon] chute the parachute (chute.h, StartSkydiveMidfall + DeployParachute) opens
//  by itself after 0.5 s of falling, JUMP opens it sooner (heli_parachute's arming).
//  A rider attached to a locator is pulled after it at most 20 u/s by 0x14049F720 (the cap is the
//  float 0x1411583C8, read only there [P]); whether the ride uses that path is [U] - while one flies
//  the cap is raised to max_speed + 10 and put back after, so a dive can never leave the rider behind.
//  Off: `[` / [pteranodon] enable = 0 (stock at once: upright, the game's own fall), unhook (next PM
//  update), a fault (ptero's own switch: off for the session, the bodies put upright once).
//
//  Animations (2026-11-15 evening, first flight: no animations at all, the rider just held his arms up;
//  Notes\46 §4.5) [P static unless marked]. A character plays its "current action" ch+0x6D0
//  (GetCurrentAction 0x14066A5B0; SetCurrentAction 0x14066F570; PlayAction 0x14066EFF0 = the same after
//  0x1406695C0 checks the character has it). An action plays only if the character's CharDef action map
//  holds an entry for its id: AnimCtx 0x140669EC0(ch) -> +8 = the CharDef, +0x2C8 = entry* per action
//  id (capacity *0x141C3DD2C = 0xE45, fixed at boot), built by 0x14045A340 from the .cd's "Animation
//  Sets" list (+ the sets those include), the first set holding an id winning; HasAnim 0x140814C20 =
//  entry && entry+0x1D6 & 2; all under the critical section 0x141C314C0. An anim set
//  (FindAnimSet 0x14047BDD0(name, 0): +0x20 name, +8 live byte, +0x10 first entry, entry +8 next,
//  +0x1D4 action id, +0x1D6 flags).
//    * The rider: no player .cd lists Ability_Ride_Pteranodon (0 of 1,700+ .cd; every player one lists
//      Ability_Ride - the horse, pig, prowler ride anims). The set itself is resident ("Automatically
//      Load" = 1 in legocity_generic.cpj, like Ability_Ride; the 34 sets with 0 are the Lvl_* / VC_* /
//      Talk_* ones level .txt files load with `Animset`) [P data, I resident]. So the game's pick
//      (slot 85 of LegoCityGameObjects 0x14004BCA0: the mount def's rider_jump +0xCE when airborne,
//      rider_land +0xD0, rider_left/right +0xCA/+0xCC when turning > 10 deg, rider_run +0xC8 when
//      moving > 0.1 u/s, else the .txt rider_action) asked for Ride_Pteranodon_Fly and the rider had no
//      such action: the arms-up pose is what a minifig shows with no anim [I]. Here: while a player
//      rides one, the two Ride_Pteranodon entries are copied from the set into the rider's CharDef map
//      where it has none (re-checked every second: a map rebuild drops them; any disguise - each has
//      its own CharDef), after a structure check (entries read back with their own ids), then the
//      rider is asked for Fly / Idle (ptm::PickAnims) when what plays is something else.
//    * The creature: its own set Pteranodon_Default (fly, glide) is in its .cd; the .txt slots now name
//      them for every state (Fall_action / jump_action / land... were unset: in the air it asked for
//      the stock fall action it does not have - static wings [I]). In the air the creature's per-
//      character slot table (ch+0x6D2 + 2 x slot; fall = slot 0x24, live while *0x141C743B4) gets fly
//      while the wings work and glide otherwise, and the creature is asked for the same.
//    Asking never fights the game (ptm::NudgeStep: every 0.25 s at most, given up after three
//    take-backs); [pteranodon] anims = 0 leaves all of it to the game; a fault here switches only
//    the animations off. [U]: whether the game blends _LEFT/_RIGHT inside Ride_Pteranodon_Fly by itself.
// =============================================================================================
#pragma once
#include "ptero_model.h"

namespace pt {

typedef bool     (__fastcall* SweepFn)(void* phys, float dt);
typedef bool     (__fastcall* ChBoolFn)(uint8_t* ch);
typedef bool     (__fastcall* CtxFn)(uint8_t* ch, int id);
typedef bool     (__fastcall* WaterFn)(uint8_t* ch, float* out, bool deep);
typedef uint8_t* (__fastcall* GetFn)(void* self);

static const uintptr_t SITE_SWEEP = 0x49F5B5, FN_SWEEP = 0xAB7A00;
static const uintptr_t FN_ONGROUND = 0x66BD20, FN_INCTX = 0x006D40, FN_WATER = 0x66BA70;
static const uintptr_t CTX_SWIMMING = 0x116ACFC, CTX_DROWNING = 0x116AD50, CTX_DEATH = 0x116A9F8, CTX_DOOMED = 0x116B30C,
                       CTX_FALLING = 0x116AD80, CTX_JUMPING = 0x116AD34;
static const uintptr_t RVA_JUMP_MASK = 0x1174CB4, RVA_ATTACH_CAP = 0x11583C8;
static const uintptr_t CH_POS = 0xA8, CH_PHYS = 0x210, CH_YAW = 0x218, CH_ROWS = 0x1C0, CH_DEF = 0x650, CH_RIDE = 0x340,
                       RIDE_MOUNT = 0x88, CH_CTRL = 0x6A8, CH_SWITCHES = 0x220;
static const uintptr_t PH_POS = 0x90, PH_VEL = 0xF0, PH_FLAGS = 0x100, PH_GROUND = 0x158;
static const uint32_t  PH_STICK = 0x4;          // "keep to the ground" probe; SetVelocity 0x140670030 clears it too

static SweepFn g_origSweep = nullptr;

struct Slot
{
    uint8_t* ch;            // the creature
    void*    def;
    uint8_t* phys;          // its ch+0x210: what the sweep is called with
    wd::SP   watch;
    uint8_t* rider;         // the player while on it
    ptm::State st;
    // the player's hands, from the tick
    float    turn, pitch;
    bool     held, ridden;
    int      presses;
    uint32_t prevHeld;
    // the step in progress
    float    pre[3];
    bool     wrote, tilted;
    float    rowScale;
    // diagnostics
    int      sweeps;
    float    riddenT, logT;
    bool     sweepWarned, switchesLogged;
    // the rider's parachute after getting off in the air
    uint8_t* chuteRider;
    float    chuteT;
    // animations
    ptm::Nudger nC, nR;     // the creature's, the rider's
    uint8_t* riderCtx;      // the rider's anim context last looked at (a disguise change = a new one)
    float    injectT;       // until the rider's map is looked at again
    bool     animLogged, slotSaved;
    uint16_t slotOrig;      // the creature's fall slot as it was
};
static Slot             g_slot[2] = {};
static void* volatile   g_phys[2] = { nullptr, nullptr };   // the sweep hook compares against these
static volatile bool    g_stopReq = false;                   // unhook: let go on the next PM update
static bool             g_released = false;                  // ...done (unhook or after a fault)
static ptm::Params      g_par;
static bool             g_telemetry = false;
static float            g_capOrig = 0.0f, g_capSet = 0.0f;
static bool             g_capRaised = false;

template <class F> static F Fn(uintptr_t rva) { return reinterpret_cast<F>(g_base + rva); }
static inline int Ctx(uintptr_t rva) { return *reinterpret_cast<const int32_t*>(g_base + rva); }
static bool InCtx(uint8_t* ch, uintptr_t rva) { return Fn<CtxFn>(FN_INCTX)(ch, Ctx(rva)); }

// The sweep hook's test: one of ours? (no engine reads - it runs for every character)
static int Match(void* phys)
{
    if (!phys) return -1;
    if (phys == g_phys[0]) return 0;
    if (phys == g_phys[1]) return 1;
    return -1;
}

static void Params(const Config& c, ptm::Params& p)
{
    p = ptm::Params{};
    p.cruise = c.ptero_cruise; p.stall = c.ptero_stall; p.maxSpeed = c.ptero_max_speed;
    p.glideDeg = c.ptero_glide; p.climbDeg = c.ptero_climb; p.diveDeg = c.ptero_dive;
    p.flapLift = c.ptero_flap_lift; p.flapPeriod = c.ptero_flap_rate; p.flapClimbDeg = c.ptero_flap_climb;
    p.takeoffUp = c.ptero_takeoff_up;
    p.bankDeg = c.ptero_bank; p.turnDeg = c.ptero_turn;
    p.ceiling = c.ptero_ceiling; p.seaY = c.ptero_sea_y; p.skim = c.ptero_skim;
    p.tilt = c.ptero_tilt; p.tiltPitchDeg = c.ptero_tilt_pitch;
    p.autoGlide = c.ptero_auto_glide; p.coastTime = c.ptero_coast;
    p.bounds = c.ptero_bounds; p.minX = c.ptero_min_x; p.maxX = c.ptero_max_x; p.minZ = c.ptero_min_z; p.maxZ = c.ptero_max_z;
}

static bool HasName(const char* name, const char* sub)
{
    if (!name || !sub || !sub[0]) return false;
    const size_t n = strlen(sub);
    for (const char* p = name; *p; ++p) if (_strnicmp(p, sub, n) == 0) return true;
    return false;
}

// What player i rides (Character.GetRidden's read, as dino.h Rides) - null in a vehicle / on foot.
static uint8_t* MountOf(uint8_t* p)
{
    if (!PtrOk(p)) return nullptr;
    uint8_t* blk = At<uint8_t*>(p, eng::MC_VEHBLK);
    if (PtrOk(blk) && At<int32_t>(blk, 4) != 0 && At<void*>(p, eng::MC_VEHREF) != nullptr) return nullptr;
    uint8_t* ride = At<uint8_t*>(p, CH_RIDE);
    if (!PtrOk(ride)) return nullptr;
    void* m = At<void*>(ride, RIDE_MOUNT);
    if (!PtrOk(m) || !PtrOk(*reinterpret_cast<void**>(m))) return nullptr;
    uint8_t* c = reinterpret_cast<GetFn>((*reinterpret_cast<void***>(m))[72])(m);
    return PtrOk(c) && c != p ? c : nullptr;
}

static bool Alive(const Slot& s)
{
    return PtrOk(s.ch) && wd::StillThere(s.watch) && (At<uint8_t>(s.ch, 0x320) & 1) && At<void*>(s.ch, CH_DEF) == s.def &&
           At<uint8_t*>(s.ch, CH_PHYS) == s.phys;
}

static uint8_t* GamePad(uint8_t* ch)
{
    uint8_t* c = PtrOk(ch) ? At<uint8_t*>(ch, CH_CTRL) : nullptr;
    return PtrOk(c) ? At<uint8_t*>(c, 0x18) : nullptr;
}

static float HeadingOfRows(uint8_t* ch)
{
    const float z[3] = { At<float>(ch, CH_ROWS + 0x20), At<float>(ch, CH_ROWS + 0x24), At<float>(ch, CH_ROWS + 0x28) };
    const float h = ptm::HeadingOf(z);
    return IsFin(h) ? h : 0.0f;
}

// The body's rows for an attitude (ptm::Rows), each row kept at the length it had (the model's scale).
static void WriteRows(Slot& s, float heading, float pitch, float roll)
{
    float x[3], y[3], z[3];
    ptm::Rows(heading, pitch, roll, x, y, z);
    const float k = s.rowScale > 0.2f && s.rowScale < 5.0f ? s.rowScale : 1.0f;
    float* r = reinterpret_cast<float*>(s.ch + CH_ROWS);
    for (int i = 0; i < 3; ++i) { r[i] = x[i] * k; r[4 + i] = y[i] * k; r[8 + i] = z[i] * k; }
    r[3] = r[7] = r[11] = 0.0f;
    At<float>(s.ch, CH_YAW) = ptm::Wrap(heading);
    s.tilted = pitch != 0.0f || roll != 0.0f;
}

static void RestoreFallSlot(Slot& s);

static void Forget(Slot& s)
{
    RestoreFallSlot(s);
    wd::Unwatch(&s.watch);
    const uint8_t* cr = s.chuteRider; const float ct = s.chuteT;
    s = Slot{};
    s.chuteRider = const_cast<uint8_t*>(cr); s.chuteT = ct;     // a parachute in progress outlives the creature
    ptm::Reset(s.st);
}

// Let go of one creature: upright if we tilted it, our hands off it.
static void ReleaseSlot(Slot& s, int i, const char* why)
{
    if (PtrOk(s.ch))
    {
        const bool alive = Alive(s);
        if (alive && (s.tilted || ptm::Airborne(s.st))) WriteRows(s, ptm::Airborne(s.st) ? s.st.heading : HeadingOfRows(s.ch), 0.0f, 0.0f);
        if (ptm::Airborne(s.st))
            Log(">>> ptero: player %d's Pteranodon let go in the air (%s) - the game has it again%s\n", i + 1, why, alive ? "" : " (it is gone)");
    }
    g_phys[i] = nullptr;
    Forget(s);
}

static void AttachCap(bool raise)
{
    float* cap = reinterpret_cast<float*>(g_base + RVA_ATTACH_CAP);
    if (raise && !g_capRaised)
    {
        if (*cap == 20.0f)                                       // the shipped value, nobody else's
        {
            g_capOrig = *cap; g_capRaised = true;
            g_capSet = std::fmax(20.0f, g_par.maxSpeed + 10.0f);
            *cap = g_capSet;
        }
    }
    else if (!raise && g_capRaised)
    {
        if (*cap == g_capSet) *cap = g_capOrig;
        g_capRaised = false;
    }
}

// Everything back, once (unhook, or after a fault in this code).
static void Release(const char* why)
{
    if (g_released) return;
    g_released = true;
    for (int i = 0; i < 2; ++i) ReleaseSlot(g_slot[i], i, why);
    AttachCap(false);
}

static void LogEvent(int i, const Slot& s, int ev)
{
    const ptm::State& t = s.st;
    const float* p = reinterpret_cast<const float*>(s.ch + CH_POS);
    switch (ev)
    {
    case ptm::kEvTakeoff:
        Log(">>> ptero: player %d takes off from (%.1f, %.2f, %.1f) heading %.0f - A beats the wings (hold to climb), the left stick "
            "banks and pitches\n", i + 1, p[0], p[1], p[2], t.heading / ptm::kDeg);
        break;
    case ptm::kEvGlideStart: Log(">>> ptero: player %d's Pteranodon spreads its wings in the air at y %.1f\n", i + 1, p[1]); break;
    case ptm::kEvStall:      Log(">>> ptero: player %d stalled (%.1f u/s at y %.1f) - the nose drops until it has speed again\n", i + 1, t.V, p[1]); break;
    case ptm::kEvRecover:    Log(">>> ptero: player %d flying again (%.1f u/s)\n", i + 1, t.V); break;
    case ptm::kEvSkim:       Log(">>> ptero: player %d skims the water at y %.2f (%.1f u/s)\n", i + 1, p[1], t.V); break;
    case ptm::kEvSwimEscape: Log(">>> ptero: player %d's Pteranodon was in the water - it flaps straight out\n", i + 1); break;
    case ptm::kEvWall:       Log(">>> ptero: player %d hit something at %.1f u/s - stopped there, no damage\n", i + 1, t.lastWallLoss); break;
    case ptm::kEvCeiling:    Log(">>> ptero: player %d at the ceiling (y %.0f)\n", i + 1, p[1]); break;
    case ptm::kEvBounds:     Log(">>> ptero: player %d at the edge of the world (%.0f, %.0f) - turning back\n", i + 1, p[0], p[2]); break;
    case ptm::kEvCoast:      Log(">>> ptero: player %d got off in the air at y %.1f - the Pteranodon glides down on its own\n", i + 1, p[1]); break;
    case ptm::kEvCoastEnd:   Log(">>> ptero: player %d's riderless Pteranodon glided %.0f s - left to the game\n", i + 1, t.coastT); break;
    case ptm::kEvDead:       Log(">>> ptero: player %d's Pteranodon died in the air - the game has it\n", i + 1); break;
    case ptm::kEvLand:
        Log(">>> ptero: player %d landed at (%.1f, %.2f, %.1f) after %.1f s: top speed %.1f u/s, top height %.1f, %d wingbeats, "
            "%d skims, %d walls, %d stalls\n", i + 1, p[0], p[1], p[2], t.airT, t.topSpeed, t.topY, t.flaps, t.skims, t.walls, t.stalls);
        break;
    default: break;
    }
}

// ---- the sweep hook: just before / after the move of one of ours (game thread) -----------------
static void Pre(int i, void* phys, float dt)
{
    Slot& s = g_slot[i];
    s.wrote = false;
    if (g_stopReq || g_released || !PtrOk(s.ch) || s.phys != phys) return;
    uint8_t* ch = s.ch;
    if (At<uint8_t*>(ch, CH_PHYS) != phys || !(At<uint8_t>(ch, 0x320) & 1) || At<void*>(ch, CH_DEF) != s.def) return;
    ++s.sweeps;
    if (!s.switchesLogged)
    {
        // once per creature: the integrator's switches for it (gravity / root motion / turning) - for the notes
        s.switchesLogged = true;
        const uint8_t* sw = ch + CH_SWITCHES;
        Log(">>> ptero: its physics step runs (dt %.4f); switches +0x220..+0x227 = %u %u %u %u %u %u %u %u, phys flags 0x%X, "
            "ground %p, row scale %.3f\n", dt, sw[0], sw[1], sw[2], sw[3], sw[4], sw[5], sw[6], sw[7], At<uint32_t>(phys, PH_FLAGS),
            At<void*>(phys, PH_GROUND), s.rowScale);
    }
    if (!(dt > 0.0f) || !IsFin(dt)) return;
    ptm::Facts f = {};
    f.dt = dt > 0.1f ? 0.1f : dt;
    for (int k = 0; k < 3; ++k) { f.pos[k] = At<float>(ch, CH_POS + 4 * k); f.vel[k] = At<float>(phys, PH_VEL + 4 * k); }
    for (int k = 0; k < 3; ++k) if (!IsFin(f.pos[k]) || !IsFin(f.vel[k])) return;
    f.heading = HeadingOfRows(ch);
    f.grounded = Fn<ChBoolFn>(FN_ONGROUND)(ch);
    float wy = -3.4e38f;
    f.waterY = Fn<WaterFn>(FN_WATER)(ch, &wy, true) && IsFin(wy) && wy > -1.0e5f && wy < 1.0e5f ? wy : -3.0e38f;
    f.swimming = InCtx(ch, CTX_SWIMMING) || InCtx(ch, CTX_DROWNING);
    f.dead = InCtx(ch, CTX_DEATH) || InCtx(ch, CTX_DOOMED);
    ptm::Input in = {};
    in.turn = s.turn; in.pitch = s.pitch; in.flapHeld = s.held; in.flapPresses = s.presses; in.ridden = s.ridden;
    s.presses = 0;
    const int modeWas = s.st.mode;
    const ptm::Out o = ptm::Step(g_par, s.st, in, f);
    if (o.event && o.event != ptm::kEvFlap) LogEvent(i, s, o.event);
    if (o.write)
    {
        float* v = reinterpret_cast<float*>(phys) + PH_VEL / 4;
        v[0] = o.vel[0]; v[1] = o.vel[1]; v[2] = o.vel[2];
        At<uint32_t>(phys, PH_FLAGS) &= ~PH_STICK;
        WriteRows(s, o.heading, o.pitch, o.roll);
        for (int k = 0; k < 3; ++k) s.pre[k] = At<float>(phys, PH_POS + 4 * k);
        s.wrote = true;
        if (g_telemetry && (s.logT += f.dt) >= 0.5f)
        {
            s.logT = 0.0f;
            Log(">>> ptero: P%d %s V %.1f path %+.0f bank %+.0f hdg %.0f pos (%.1f, %.2f, %.1f) vel (%+.1f, %+.1f, %+.1f) in %+.2f/%+.2f%s "
                "gnd %d water %.2f\n", i + 1, ptm::ModeName(s.st.mode), s.st.V, s.st.gamma / ptm::kDeg, s.st.bank / ptm::kDeg,
                s.st.heading / ptm::kDeg, f.pos[0], f.pos[1], f.pos[2], o.vel[0], o.vel[1], o.vel[2], in.turn, in.pitch,
                in.flapHeld ? " A" : "", f.grounded ? 1 : 0, f.waterY > -1.0e5f ? f.waterY : -1.0f);
        }
    }
    if (o.release)
    {
        WriteRows(s, o.heading, 0.0f, 0.0f);
        s.tilted = false;
        if (modeWas == ptm::kCoast && !s.ridden) Log(">>> ptero: player %d's riderless Pteranodon is down\n", i + 1);
    }
}

static void Post(int i, void* phys, float dt)
{
    Slot& s = g_slot[i];
    if (!s.wrote || s.phys != phys) return;
    s.wrote = false;
    float post[3];
    for (int k = 0; k < 3; ++k) post[k] = At<float>(phys, PH_POS + 4 * k);
    const bool ground = At<void*>(phys, PH_GROUND) != nullptr;
    const int ev = ptm::AfterSweep(g_par, s.st, s.pre, post, ground, dt > 0.1f ? 0.1f : dt);
    if (ev == ptm::kEvWall || ev == ptm::kEvLand) LogEvent(i, s, ev);
}

// ---- animations (see the top: Notes\46 §4.5) --------------------------------------------------
typedef int      (__fastcall* AnimIdFn)(const char* name);
typedef uint8_t* (__fastcall* FindSetFn)(const char* name, void* skeleton);
typedef uint8_t* (__fastcall* AnimCtxFn)(uint8_t* ch);
typedef bool     (__fastcall* HasAnimFn)(uint8_t* actx, int id);
typedef bool     (__fastcall* PlayFn)(uint8_t* ch, int id);
typedef int      (__fastcall* CurActFn)(uint8_t* ch);
typedef bool     (__fastcall* CsEnterFn)(void* cs, int);
typedef void     (__fastcall* CsLeaveFn)(void* cs);

static const uintptr_t FN_ANIMID = 0x814C50, FN_FINDSET = 0x47BDD0, FN_ANIMCTX = 0x669EC0, FN_HASANIM = 0x814C20,
                       FN_PLAY = 0x66EFF0, FN_CURACT = 0x66A5B0, FN_CS_ENTER = 0x2DA0B0, FN_CS_LEAVE = 0x2DA0D0;
static const uintptr_t RVA_ANIM_CS = 0x1C314C0, RVA_ACTION_CAP = 0x1C3DD2C, RVA_SLOTS_LIVE = 0x1C743B4;
static const uintptr_t ACTX_DEF = 0x8, DEF_MAP = 0x2C8, SET_LIVE = 0x8, SET_FIRST = 0x10, ENT_NEXT = 0x8, ENT_ID = 0x1D4,
                       ENT_FLAGS = 0x1D6, CH_SLOTS = 0x6D2;
static const int       SLOT_FALL = 0x24;
static const int       kRideStock = 5;            // the fallback: a stock ride pose every player has (Ability_Ride)
static const char      kRiderSet[] = "Ability_Ride_Pteranodon";

struct AnimIds { int glide, fly, rideFly, rideIdle, stock; bool ok; };
static AnimIds g_ids = {};
static bool    g_animOff = false;                 // a fault in the animation code: animations left to the game

static bool IdOk(int id) { return id > 0 && id < 0x8000; }

static void ResolveIds()
{
    if (g_ids.ok) return;
    const AnimIdFn id = Fn<AnimIdFn>(FN_ANIMID);           // case-insensitive; resident sets registered them at boot
    g_ids.glide = id("glide");          g_ids.fly = id("fly");
    g_ids.rideFly = id("Ride_Pteranodon_Fly"); g_ids.rideIdle = id("Ride_Pteranodon_Idle");
    g_ids.stock = id("Ride_Horse_idle");                    // static id 0x118 (table 0x1411862B0)
    g_ids.ok = true;
}

static int IdOf(int kind)
{
    switch (kind)
    {
    case ptm::kAnimGlide: return g_ids.glide;
    case ptm::kAnimFly:   return g_ids.fly;
    case ptm::kRideIdle:  return g_ids.rideIdle;
    case ptm::kRideFly:   return g_ids.rideFly;
    case kRideStock:      return g_ids.stock;
    default:              return -1;
    }
}

static int KindOf(int id)
{
    if (!IdOk(id)) return ptm::kAnimNone;
    if (id == g_ids.glide) return ptm::kAnimGlide;
    if (id == g_ids.fly) return ptm::kAnimFly;
    if (id == g_ids.rideIdle) return ptm::kRideIdle;
    if (id == g_ids.rideFly) return ptm::kRideFly;
    if (id == g_ids.stock) return kRideStock;
    return ptm::kAnimNone;
}

static bool Has(uint8_t* actx, int id) { return PtrOk(actx) && IdOk(id) && Fn<HasAnimFn>(FN_HASANIM)(actx, id); }

// The two Ride_Pteranodon entries into the rider's CharDef action map where it has none. Returns the
// number added, or -1 with `why` filled.
static int InjectRiderSet(uint8_t* actx, const char*& why)
{
    uint8_t* def = PtrOk(actx) ? At<uint8_t*>(actx, ACTX_DEF) : nullptr;
    uint8_t** map = PtrOk(def) ? At<uint8_t**>(def, DEF_MAP) : nullptr;
    if (!PtrOk(map)) { why = "the rider has no action map"; return -1; }
    const int cap = *reinterpret_cast<const int32_t*>(g_base + RVA_ACTION_CAP);
    if (cap <= 0 || cap > 0x4000 || !IdOk(g_ids.rideFly) || g_ids.rideFly >= cap || !IdOk(g_ids.rideIdle) || g_ids.rideIdle >= cap)
    { why = "the action ids are out of the map's range"; return -1; }
    uint8_t* set = Fn<FindSetFn>(FN_FINDSET)(kRiderSet, nullptr);
    if (!PtrOk(set)) { why = "the game has no anim set Ability_Ride_Pteranodon"; return -1; }
    if (!At<uint8_t>(set, SET_LIVE)) { why = "Ability_Ride_Pteranodon is not loaded"; return -1; }
    void* cs = reinterpret_cast<void*>(g_base + RVA_ANIM_CS);
    Fn<CsEnterFn>(FN_CS_ENTER)(cs, 0);
    int good = 0, added = 0;
    // the structure check: entries in the rider's map carry their own index as their id
    const int scan = cap < 0x400 ? cap : 0x400;
    for (int k = 1; k < scan && good < 3; ++k)
    {
        uint8_t* e = map[k];
        if (PtrOk(e) && At<int16_t>(e, ENT_ID) == k) ++good;
    }
    uint8_t* found[2] = { nullptr, nullptr };
    int seen = 0;
    for (uint8_t* e = At<uint8_t*>(set, SET_FIRST); PtrOk(e) && seen < 64; e = At<uint8_t*>(e, ENT_NEXT), ++seen)
    {
        if (!(At<uint8_t>(e, ENT_FLAGS) & 1)) continue;
        const int id = At<int16_t>(e, ENT_ID);
        if (id == g_ids.rideFly && !found[0]) found[0] = e;
        if (id == g_ids.rideIdle && !found[1]) found[1] = e;
    }
    if (good < 3) why = "the rider's action map does not read as expected - left alone";
    else if (!found[0] && !found[1]) why = "the set holds no Ride_Pteranodon action (structure differs) - left alone";
    else
    {
        if (found[0] && !map[g_ids.rideFly]) { map[g_ids.rideFly] = found[0]; ++added; }
        if (found[1] && !map[g_ids.rideIdle]) { map[g_ids.rideIdle] = found[1]; ++added; }
    }
    Fn<CsLeaveFn>(FN_CS_LEAVE)(cs);
    return good >= 3 && (found[0] || found[1]) ? added : -1;
}

static bool SlotsLive() { return *reinterpret_cast<const uint8_t*>(g_base + RVA_SLOTS_LIVE) != 0; }

static void RestoreFallSlot(Slot& s)
{
    if (s.slotSaved && Alive(s)) At<uint16_t>(s.ch, CH_SLOTS + 2 * SLOT_FALL) = s.slotOrig;
    s.slotSaved = false;
}

static void LogGiveUp(int i, ptm::Nudger& n, const char* who, int want, int cur)
{
    if (!(n.gaveUp & (1u << want)) || (n.logged & (1u << want))) return;
    n.logged |= 1u << want;
    Log(">>> ptero: P%d %s: the game keeps playing action %d instead of %d - not asking any more this ride\n", i + 1, who, cur, IdOf(want));
}

static void AnimTick(int i, Slot& s, uint8_t* rider, float dt)
{
    if (!PtrOk(s.ch) || !Alive(s)) return;
    ResolveIds();
    float gs = 0.0f;
    if (PtrOk(s.phys)) { const float vx = At<float>(s.phys, PH_VEL), vz = At<float>(s.phys, PH_VEL + 8); gs = std::sqrt(vx * vx + vz * vz); }
    const ptm::AnimPick a = ptm::PickAnims(s.st, s.ridden && PtrOk(rider), gs);
    uint8_t* cctx = Fn<AnimCtxFn>(FN_ANIMCTX)(s.ch);

    // the creature: the fall slot its airborne contexts read, and an ask when something else plays
    if (a.creature != ptm::kAnimNone && Has(cctx, IdOf(a.creature)))
    {
        if (SlotsLive())
        {
            uint16_t& slot = At<uint16_t>(s.ch, CH_SLOTS + 2 * SLOT_FALL);
            if (!s.slotSaved) { s.slotOrig = slot; s.slotSaved = true; }
            slot = static_cast<uint16_t>(IdOf(a.creature));
        }
    }
    else RestoreFallSlot(s);
    {
        const int curId = Fn<CurActFn>(FN_CURACT)(s.ch);
        const int want = a.creature != ptm::kAnimNone && Has(cctx, IdOf(a.creature)) ? a.creature : ptm::kAnimNone;
        if (ptm::NudgeStep(s.nC, KindOf(curId), want, dt)) Fn<PlayFn>(FN_PLAY)(s.ch, IdOf(want));
        if (want != ptm::kAnimNone) LogGiveUp(i, s.nC, "Pteranodon", want, curId);
    }

    // the rider: the rider set's entries into its map, then Fly / Idle
    if (a.rider == ptm::kAnimNone) return;
    uint8_t* rctx = Fn<AnimCtxFn>(FN_ANIMCTX)(rider);
    if (!PtrOk(rctx)) return;
    if (rctx != s.riderCtx) { s.riderCtx = rctx; s.injectT = 0.0f; s.animLogged = false; }
    s.injectT -= dt;
    int added = 0; const char* why = nullptr;
    if (s.injectT <= 0.0f)
    {
        s.injectT = 1.0f;
        if (!Has(rctx, g_ids.rideFly) || !Has(rctx, g_ids.rideIdle)) added = InjectRiderSet(rctx, why);
    }
    const bool fly = Has(rctx, g_ids.rideFly), idle = Has(rctx, g_ids.rideIdle), stock = Has(rctx, g_ids.stock);
    if (!s.animLogged)
    {
        s.animLogged = true;
        Log(">>> ptero: P%d animations - Pteranodon: glide %s, fly %s; rider %s: Ride_Pteranodon_Fly %s, _Idle %s (%s)%s\n", i + 1,
            Has(cctx, g_ids.glide) ? "ok" : "MISSING", Has(cctx, g_ids.fly) ? "ok" : "MISSING", CharDefName(rider),
            fly ? "ok" : "MISSING", idle ? "ok" : "MISSING",
            why ? why : added > 0 ? "copied in from Ability_Ride_Pteranodon" : "it had them",
            !fly && !idle ? (stock ? " - the stock ride pose Ride_Horse_idle stands in" : " - no ride pose at all") : "");
    }
    else if (added > 0) Log(">>> ptero: P%d the rider's map was rebuilt - Ride_Pteranodon copied in again (%d)\n", i + 1, added);
    int want = a.rider;
    if (!Has(rctx, IdOf(want))) want = Has(rctx, IdOf(want == ptm::kRideFly ? ptm::kRideIdle : ptm::kRideFly))
                                       ? (want == ptm::kRideFly ? ptm::kRideIdle : ptm::kRideFly)
                                       : stock ? kRideStock : ptm::kAnimNone;
    const int curId = Fn<CurActFn>(FN_CURACT)(rider);
    if (ptm::NudgeStep(s.nR, KindOf(curId), want, dt)) Fn<PlayFn>(FN_PLAY)(rider, IdOf(want));
    if (want != ptm::kAnimNone) LogGiveUp(i, s.nR, "rider", want, curId);
}

static void AnimTickSafe(int i, Slot& s, uint8_t* rider, float dt)
{
    __try { AnimTick(i, s, rider, dt); }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        g_animOff = true;
        Log("!!! EXCEPTION in the Pteranodon animation code - animations left to the game for this session (the flight goes on)\n");
    }
}

// ---- once per world update, after cPopulationManager::Update (PMThunk) ------------------------
static void Hands(int i, Slot& s, uint8_t* rider, float dt)
{
    (void)dt;
    uint8_t* pad = GamePad(rider);
    if (!pad) pad = GamePad(s.ch);                               // [U] which of the two a ride reads: the rider's first
    pad::Poll();                                                 // this player context's XInput port
    const bool havePad = pad::g_user >= 0 && mdl::KeyDownInGame(-1);
    // JUMP: the game's own bit on the rider's pad (keyboard or controller), or the controller's A read directly
    const uint32_t jump = *reinterpret_cast<const uint32_t*>(g_base + RVA_JUMP_MASK);
    uint32_t held = pad ? (At<uint32_t>(pad, 0x0C) & jump) : 0u;
    if (havePad && (pad::g_buttons & XINPUT_GAMEPAD_A)) held |= 0x80000000u;
    const bool jh = held != 0;
    if (jh && !s.prevHeld && s.presses < 3) ++s.presses;
    s.prevHeld = held;
    s.held = jh;
    float lx = 0.0f, ly = 0.0f;
    if (havePad) { lx = pad::g_lx; ly = pad::g_ly; }
    else if (i == 0)
    {
        lx = (mdl::KeyDownInGame('D') ? 1.0f : 0.0f) - (mdl::KeyDownInGame('A') ? 1.0f : 0.0f);
        ly = (mdl::KeyDownInGame('W') ? 1.0f : 0.0f) - (mdl::KeyDownInGame('S') ? 1.0f : 0.0f);
    }
    else if (pad)
    {
        lx = At<float>(pad, 0x3C); ly = At<float>(pad, 0x40);
        if (!IsFin(lx) || !IsFin(ly)) lx = ly = 0.0f;
    }
    auto dead = [](float v) { const float a = std::fabs(v); return a < 0.12f ? 0.0f : (v > 0.0f ? 1.0f : -1.0f) * std::fmin(1.0f, (a - 0.12f) / 0.88f); };
    s.turn = dead(lx);
    s.pitch = -dead(ly);                                          // + = pulled back = nose up (pitch_sign -1 flips it)
}

static void TickPlayer(int i, Slot& s, float dt, const Config& cfg, bool live)
{
    uint8_t* rider = wd::RawPlayer(i);
    uint8_t* mount = MountOf(rider);
    const bool ptero = mount && HasName(CharDefName(mount), cfg.ptero_creature);
    if (PtrOk(s.ch) && !Alive(s))
    {
        if (ptm::Airborne(s.st)) Log(">>> ptero: player %d's Pteranodon is gone\n", i + 1);
        g_phys[i] = nullptr;
        Forget(s);
    }
    if (ptero && live)
    {
        if (mount != s.ch)
        {
            if (PtrOk(s.ch)) ReleaseSlot(s, i, "another creature");
            s.ch = mount; s.def = At<void*>(mount, CH_DEF); s.phys = At<uint8_t*>(mount, CH_PHYS);
            if (!PtrOk(s.phys) || !wd::Watch(mount, &s.watch))
            {
                Log(">>> ptero: player %d rides %s but %s - it walks as before\n", i + 1, CharDefName(mount),
                    PtrOk(s.phys) ? "no watch on it could be made" : "it has no physics object");
                Forget(s);
            }
            else
            {
                ptm::Reset(s.st);
                const float* r = reinterpret_cast<const float*>(mount + CH_ROWS + 0x10);
                const float len = std::sqrt(r[0] * r[0] + r[1] * r[1] + r[2] * r[2]);
                s.rowScale = IsFin(len) && len > 0.2f && len < 5.0f ? len : 1.0f;
                s.prevHeld = 0xFFFFFFFFu;                            // a JUMP already held (the get-on press) is no press
                Log(">>> ptero: player %d is on a Pteranodon (%s, ch=%p, phys=%p) - JUMP takes off\n", i + 1, CharDefName(mount),
                    mount, s.phys);
            }
        }
        if (PtrOk(s.ch))
        {
            s.rider = rider; s.ridden = true; s.chuteRider = nullptr;
            Hands(i, s, rider, dt);
            if (cfg.ptero_pitch_sign < 0) s.pitch = -s.pitch;
            s.riddenT += dt;
            if (!s.sweepWarned && s.riddenT > 2.0f && s.sweeps == 0)
            {
                s.sweepWarned = true;
                Log(">>> ptero: player %d's Pteranodon got no physics step in 2 s ridden - it cannot fly this way (the game moves "
                    "a ridden creature some other way); it walks as before\n", i + 1);
            }
        }
    }
    else if (PtrOk(s.ch))
    {
        if (!live) ReleaseSlot(s, i, cfg.ptero_enable ? "lcudrive off" : "[pteranodon] enable = 0");
        else if (s.ridden)
        {
            s.ridden = false; s.turn = s.pitch = 0.0f; s.held = false; s.presses = 0;
            if (ptm::Airborne(s.st))
            {
                // off in the air: the creature coasts (the model), the rider falls - with a parachute
                uint8_t* r = s.rider;
                if (cfg.ptero_chute && PtrOk(r) && r == rider && !Fn<ChBoolFn>(FN_ONGROUND)(r))
                {
                    if (cfg.heli_parachute) chute::OnBailOut(r, 3.0e38f);        // JUMP opens it at once...
                    s.chuteRider = r; s.chuteT = 0.0f;                            // ...or it opens by itself
                }
            }
            else { g_phys[i] = nullptr; Forget(s); }
        }
        else if (!ptm::Airborne(s.st) && s.st.mode != ptm::kLanded) { g_phys[i] = nullptr; Forget(s); }
    }
    g_phys[i] = PtrOk(s.ch) ? s.phys : nullptr;
    if (live && cfg.ptero_anims && !g_animOff && PtrOk(s.ch)) AnimTickSafe(i, s, s.ridden ? rider : nullptr, dt);
    else if (s.slotSaved) RestoreFallSlot(s);

    // the rider's parachute, opened by itself 0.5 s into the fall (chute.h's own watchdog takes it from there)
    if (s.chuteRider)
    {
        uint8_t* r = s.chuteRider;
        s.chuteT += dt;
        bool done = r != rider || !PtrOk(r) || s.chuteT > 4.0f || Fn<ChBoolFn>(FN_ONGROUND)(r) || chute::g.open;
        if (!done && s.chuteT >= 0.5f && !chute::Riding(r))
        {
            uint8_t* rp = At<uint8_t*>(r, CH_PHYS);
            const float vy = PtrOk(rp) ? At<float>(rp, PH_VEL + 4) : 0.0f;
            if (vy < -1.0f && (InCtx(r, CTX_FALLING) || InCtx(r, CTX_JUMPING)) && chute::OpenChute(r, cfg))
            {
                chute::g = chute::State{};
                chute::g.ch = r; chute::g.open = true; chute::g.oT = 0.0f; chute::g.y0 = At<float>(r, CH_POS + 4); chute::g.hag0 = 3.0e38f;
                if (uint8_t* pd = GamePad(r)) chute::g.prevHeld = At<uint32_t>(pd, 0x0C);
                Log(">>> ptero: player %d's parachute opened by itself\n", i + 1);
                done = true;
            }
        }
        if (done) { s.chuteRider = nullptr; s.chuteT = 0.0f; }
    }
}

static void Tick(float dt, const Config& cfg, bool on, bool telemetry)
{
    if (g_released) return;
    if (g_stopReq) { Release("lcudrive unhooked"); return; }
    Params(cfg, g_par);
    g_telemetry = telemetry;
    const bool live = on && cfg.ptero_enable;
    bool flying = false;
    for (int i = 0; i < 2; ++i)
    {
        AsP2 as(i == 1);                                          // player 2's pad port and parachute state
        TickPlayer(i, g_slot[i], dt, cfg, live);
        flying = flying || (PtrOk(g_slot[i].ch) && ptm::Airborne(g_slot[i].st));
    }
    AttachCap(flying);
}

} // namespace pt
