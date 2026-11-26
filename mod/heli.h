// =============================================================================================
//  heli.h - the engine side of the physical helicopter (the model is heli_model.h).
//
//  ApiVehicleHover::Update (slot 91, 0x1404AEA60) flies the helicopter by overwriting the body's
//  velocities every frame: gravity zeroed, the height servo writes vy, a forward-speed servo
//  writes the velocity along the heading, the orientation drive writes the angular velocity.
//  For the player's helicopter lcudrive takes the body's velocities BEFORE that update, lets the
//  update run (rotor, sound, flags and the terrain probe all keep working), puts the velocities
//  back - which undoes every one of those writes at once - and applies heli_model.h instead.
//  Only while the game's own pilot logic says "flying" (user-data state 2, Notes\26); boarding,
//  a helipad landing, getting out, a wrecked helicopter and every AI or scripted one are left
//  entirely to the engine.
//
//  Getting out (Notes\26): the game decides it in ApiVehicleUserData_HoveringAircraft::Update -
//  over a helipad it lands you, over a crash mat you bail out, anywhere else it shows "you can't
//  get out here". lcudrive takes that last call (0x1401A9154): low and slow, it replays the end
//  of the game's own pad landing (state 4) and the heli parks where it is; otherwise you jump out
//  the way the crash-mat bail-out does, and the empty helicopter is left to fall: the rotor
//  winds down, the tail is lost, it spirals in and crashes (heli_model.h Wreck). The game's own
//  habit of lifting an abandoned heli 5 u and pinning it in the air (state 8) is turned into
//  that same fall - for the helicopter you just left, never for a crash-mat or scripted one.
//
//  Free flight (Notes\27): the turn-around walls, the world-Y 35 climb cap and the per-tile
//  minimum-height floors are all in the same pilot code; each is a one-byte patch, switched on
//  while lcudrive flies helicopters and back off with `[` / unhook.
// =============================================================================================
#pragma once
#include "heli_model.h"
#include "vehdamage.h"

namespace hl {

static heli::State g_state;
static void*       g_veh = nullptr;
static double      g_t = 0.0;
static bool        g_headerDone = false;
static volatile bool g_releaseReq = false;   // unhook: give the per-vehicle bits back

// ---- engine calls ----------------------------------------------------------------------------
typedef void(__fastcall* HintFlagFn)(int id);
typedef void(__fastcall* DismountFn)(void* ride, void* params);
typedef void(__fastcall* SetStateFn)(void* ch, int state, int a3, uint64_t a4, int a5);
typedef void(__fastcall* TerrainFn)(void* drive);
static HintFlagFn g_hintFlag = nullptr;
static SetStateFn g_setState = nullptr;
static TerrainFn  g_terrain  = nullptr;

static inline bool InImage(const void* p)
{
    const uintptr_t v = reinterpret_cast<uintptr_t>(p);
    return v >= g_base && v < g_base + eng::kImageSize;
}

// The pilot-side user data of a helicopter (HoveringAircraft or FireHelicopter), or null.
static uint8_t* UserData(void* veh)
{
    __try
    {
        uint8_t* p = At<uint8_t*>(veh, eng::VEH_USERDATA);
        if (!PtrOk(p)) return nullptr;
        uint8_t* ud = p - 0x10;
        const uintptr_t vt = At<uintptr_t>(ud, 0);
        if (vt != g_base + eng::RVA_VT_HELIUD && vt != g_base + eng::RVA_VT_FIREUD) return nullptr;
        return At<void*>(ud, 0x38) == veh ? ud : nullptr;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}
static inline int UdState(uint8_t* ud) { return ud ? At<int32_t>(ud, eng::UD_STATE) : -1; }

static heli::Params ParamsFrom(const Config& c)
{
    heli::Params p;
    p.topSpeed = c.heli_top_speed;   p.maxTiltDeg = c.heli_max_tilt;
    p.climbRate = c.heli_climb;      p.descendRate = c.heli_descend;
    p.maxThrustG = c.heli_thrust_g;  p.yawRateDeg = c.heli_yaw_rate;
    p.turnBank = c.heli_turn_bank;   p.brakeAssist = c.heli_brake;
    p.responseS = c.heli_response;   p.landHeight = c.heli_land_height;
    p.yawSign = c.heli_yaw_sign;     p.fwdSign = c.heli_fwd_sign;  p.vertSign = c.heli_vert_sign;
    return p;
}

// Before the engine's Update: the physical velocities, as last frame's physics step left them.
static bool Capture(void* veh, float lv[3], float av[3])
{
    __try
    {
        void* inst = At<void*>(veh, eng::VEH_INSTANCE);
        if (!PtrOk(inst)) return false;
        void* body = At<void*>(inst, eng::INST_BODY);
        if (!PtrOk(body)) return false;
        for (int k = 0; k < 3; ++k)
        {
            lv[k] = At<float>(body, eng::BODY_LINVEL + 4 * k);
            av[k] = At<float>(body, eng::BODY_ANGVEL + 4 * k);
            if (!IsFin(lv[k]) || !IsFin(av[k])) return false;
        }
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

static float HeightAboveGround(void* veh)
{
    void* hd = At<void*>(veh, eng::VEH_HDRIVE);
    const float h = PtrOk(hd) ? At<float>(hd, eng::HD_HAG) : 3.0e38f;
    return IsFin(h) ? h : 3.0e38f;
}

static void TelemetryHeader()
{
    if (g_headerDone) return;
    g_headerDone = true;
    mdl::TelemetryLine("H,t,mode,occ,landed,spool,cx,cy,cz,thr,rev,brk,px,py,pz,vx,vy,vz,wx,wy,wz,upx,upy,upz,"
                       "fx,fz,hag,thrust,pitchT,rollT,vyT,dt,ud\n");
}

static void LogNewHeli(void* veh)
{
    char name[64];
    void* def = At<void*>(veh, eng::VEH_DEF);
    if (!PtrOk(def) || !ReadModelName(def, name, sizeof(name))) strcpy_s(name, "?");
    void* inst = At<void*>(veh, eng::VEH_INSTANCE);
    void* body = PtrOk(inst) ? At<void*>(inst, eng::INST_BODY) : nullptr;
    int hcur = -1, hmax = -1;
    vd::Health(veh, hcur, hmax);
    Log(">>> helicopter: %s  veh=%p  mass=%.1f  health %d/%d  sim lod=%d  pilot state=%d | def: maxVel=%.1f "
        "heightIdle=%.2f heightActive=%.2f\n", name, veh, PtrOk(body) ? At<float>(body, eng::BODY_MASS) : -1.0f,
        hcur, hmax, At<int32_t>(veh, eng::VEH_SIMLOD), UdState(UserData(veh)),
        PtrOk(def) ? At<float>(def, 0x174) : -1.0f, PtrOk(def) ? At<float>(def, 0x1A48) : -1.0f,
        PtrOk(def) ? At<float>(def, 0x1A4C) : -1.0f);
}

// ---------------------------------------------------------------------------------------------
//  Free flight: the pilot code's walls, height cap and floors (Notes\27), one byte each.
// ---------------------------------------------------------------------------------------------
static BytePatch g_ffPatch[] = {
    // "mov [ud+12E0h], 9" (turn around) -> "mov [ud+12E0h], 2" (keep flying): trigger zones,
    // the hard-coded pillar, the map-edge tiles. The setup they skip only writes ud scratch.
    { "walls/zones",   0x1A8171, 0x09, 0x02, 0x1A8166, { 0xE9, 0x76, 0x03, 0x00, 0x00, 0xC7, 0x87, 0xE0 }, 8, false, false },
    { "walls/pillar",  0x1A82A6, 0x09, 0x02, 0x1A829A, { 0x0F, 0x86, 0x41, 0x02, 0x00, 0x00, 0xC7, 0x87 }, 8, false, false },
    { "walls/edge",    0x1A8418, 0x09, 0x02, 0x1A840C, { 0x0F, 0x84, 0xDC, 0x00, 0x00, 0x00, 0xC7, 0x87 }, 8, false, false },
    // the minimum-height floor: "jbe" -> "jo" (never taken after comiss) keeps the descend key
    // live below it; "jbe" -> "jmp" skips the push back up
    { "floor/descend", 0x1A787B, 0x76, 0x70, 0x1A7873, { 0xF3, 0x0F, 0x10, 0x40, 0x34, 0x0F, 0x2F, 0xC1 }, 8, false, false },
    { "floor/push-up", 0x1A790C, 0x76, 0xEB, 0x1A7900, { 0xF3, 0x0F, 0x10, 0x8B, 0xD4, 0x12, 0x00, 0x00 }, 8, false, false },
};
static bool g_ceilOk = false;
static bool g_ffOn = false;

static void InitFreeFlight()
{
    int ok = 0;
    for (BytePatch& p : g_ffPatch) ok += VerifyBytePatch(g_base, p) ? 1 : 0;
    __try { g_ceilOk = *reinterpret_cast<const float*>(g_base + eng::RVA_HELI_YMAX) == 35.0f; }
    __except (EXCEPTION_EXECUTE_HANDLER) { g_ceilOk = false; }
    Log("  heli free flight: %d/%d patch sites verified, height cap %s\n", ok,
        (int)(sizeof(g_ffPatch) / sizeof(g_ffPatch[0])), g_ceilOk ? "found (35.0)" : "NOT found - left alone");
}

static void SetFreeFlight(bool on)
{
    if (on == g_ffOn) return;
    g_ffOn = on;
    for (BytePatch& p : g_ffPatch) SetBytePatch(g_base, p, on);
    if (g_ceilOk) *reinterpret_cast<volatile float*>(g_base + eng::RVA_HELI_YMAX) = on ? 1.0e9f : 35.0f;
    Log(">>> heli free flight %s\n", on ? "ON: no turn-around walls, no height cap, no floors"
                                        : "off: the game's own flight limits");
}

// The boundary-only blockers (Dyno group 4) - ignored by the helicopter you fly and by a falling
// wreck; given back in that vehicle's own update.
struct MaskRec { void* veh; uint16_t orig; };
static MaskRec g_mask[8] = {};

static void BoundaryMask(void* veh, bool on)
{
    MaskRec* rec = nullptr;
    for (MaskRec& r : g_mask) if (r.veh == veh) { rec = &r; break; }
    if (!on && !rec) return;
    void* inst = At<void*>(veh, eng::VEH_INSTANCE);
    if (!PtrOk(inst)) return;
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
//  The abandoned helicopter
// ---------------------------------------------------------------------------------------------
struct WreckTrack
{
    void*    veh;
    void*    ch;                  // the pilot who bailed (null when he tagged into something)
    heli::WreckState w;
    uint32_t t0, lastSeen;
    bool     active, wrecked, retried;
};
static WreckTrack        g_wr = {};
static heli::WreckParams g_wp;

static void StartWreck(void* veh, void* ch, const Config& cfg)
{
    void* inst = At<void*>(veh, eng::VEH_INSTANCE);
    void* body = PtrOk(inst) ? At<void*>(inst, eng::INST_BODY) : nullptr;
    if (!PtrOk(body)) return;
    // lean the way it was going (or the way the nose points), spin the way it was turning
    float lx = At<float>(body, eng::BODY_LINVEL), lz = At<float>(body, eng::BODY_LINVEL + 8);
    if (lx * lx + lz * lz < 4.0f) { lx = -At<float>(inst, eng::INST_ROW2); lz = -At<float>(inst, eng::INST_ROW2 + 8); }
    heli::WreckReset(g_wr.w, At<float>(body, eng::BODY_ANGVEL + 4) >= 0.0f ? 1.0f : -1.0f, lx, lz);
    g_wp.spinMax = cfg.heli_crash_spin;
    g_wr.veh = veh; g_wr.ch = ch; g_wr.t0 = g_wr.lastSeen = GetTickCount();
    g_wr.active = true; g_wr.wrecked = false; g_wr.retried = false;
}

// Getting out where the game says you can't: called from the hooked HINT_HELI_NOEXIT call with
// the pilot's user data and character. True = handled (no hint).
static bool OffPadExit(uint8_t* ud, uint8_t* ch, const Config& cfg)
{
    if (!cfg.heli_enable || !cfg.heli_exit_anywhere || !PtrOk(ud) || !PtrOk(ch)) return false;
    const uintptr_t vt = At<uintptr_t>(ud, 0);
    if (vt != g_base + eng::RVA_VT_HELIUD && vt != g_base + eng::RVA_VT_FIREUD) return false;
    void* veh = At<void*>(ud, 0x38);
    if (!PtrOk(veh) || UserData(veh) != ud) return false;
    const int32_t cls = At<int32_t>(veh, eng::VEH_CLASS);
    if (cls != 6 && cls != 4) return false;
    // a script that wants you aboard locks the pilot seat; the game's own exits honour it too
    void* seat = At<void*>(ud, eng::UD_SEATREF);
    void* ctl = At<void*>(ud, eng::UD_SEATCTL);
    if (!PtrOk(seat) || !PtrOk(ctl) || At<int32_t>(ctl, 4) == 0) return false;
    if (At<uint8_t>(seat, eng::UD_SEATLOCK) & 1) { Log(">>> heli exit: the seat is locked by the game (mission) - staying in\n"); return false; }
    if (At<int32_t>(veh, eng::VEH_SIMLOD) == 10) return false;          // asleep
    { int hc = 1, hm = 1; if (vd::Health(veh, hc, hm) && hc < 1) return false; }   // destroyed: the game's own sequence
    void* drive = At<void*>(veh, eng::VEH_HDRIVE);
    void* inst = At<void*>(veh, eng::VEH_INSTANCE);
    void* body = PtrOk(inst) ? At<void*>(inst, eng::INST_BODY) : nullptr;
    if (!PtrOk(drive) || !PtrOk(body)) return false;
    const float hag = At<float>(drive, eng::HD_HAG);
    const float vx = At<float>(body, eng::BODY_LINVEL), vy = At<float>(body, eng::BODY_LINVEL + 4),
                vz = At<float>(body, eng::BODY_LINVEL + 8);
    const float vh = std::sqrt(vx * vx + vz * vz);

    if (IsFin(hag) && hag <= cfg.heli_park_height && vh <= 2.0f && std::fabs(vy) <= 1.5f)
    {
        // the end of the game's own helipad landing, without the glide onto a pad: height target
        // 0, no hover override, state 4 - next frame the game takes the pilot out and parks it
        if (cls == 6) At<float>(drive, eng::HD_HEIGHT_ACT) = 0.0f;
        At<float>(drive, eng::HD_OVERRIDE) = 0.0f;
        At<int32_t>(ud, eng::UD_STATE) = 4;
        Log(">>> heli exit: on the ground (%.2f up, %.1f u/s) - it parks here, the game lets you out\n", hag, vh);
        return true;
    }
    if (!cfg.heli_bail) return false;

    void* ride = At<void*>(ch, eng::CH_RIDE);
    if (!PtrOk(ride)) return false;
    void** rvt = At<void**>(ride, 0);
    if (!PtrOk(rvt)) return false;
    const DismountFn dismount = reinterpret_cast<DismountFn>(rvt[eng::SLOT_DISMOUNT]);
    if (!InImage(reinterpret_cast<const void*>(dismount)) || !g_setState) return false;
    // What the game itself does to bail you out over a crash mat (0x1401A8965): a jump-off
    // dismount, then the Falling state. Minus its event and the lift-away of the empty heli: that
    // one is parked (state 0, nobody aboard) and lcudrive lets it fall instead.
    At<int32_t>(ud, eng::UD_STATE) = 0;
    alignas(16) uint8_t prm[0x40] = {};
    *reinterpret_cast<int32_t*>(prm) = 1;                               // type 1 = jump off
    dismount(ride, prm);
    const int32_t falling = *reinterpret_cast<const int32_t*>(g_base + eng::RVA_STATE_FALLING);
    if (falling != -1) g_setState(ch, falling, -1, 0, 0);
    StartWreck(veh, ch, cfg);
    Log(">>> heli exit: BAIL OUT %.1f up at %.1f u/s - the helicopter is on its own\n",
        IsFin(hag) && hag < 1e30f ? hag : -1.0f, std::sqrt(vh * vh + vy * vy));
    if (cfg.heli_parachute) chute::OnBailOut(ch, IsFin(hag) ? hag : 3.0e38f);
    return true;
}

// The helicopter the player flew last, and its last pilot state other than 8 while he was in it.
static void*    g_lastHeli = nullptr;
static int      g_lastHeliState = -1;
static uint32_t g_lastHeliT = 0;

static void NotePlayerHeli(void* veh, uint32_t now)
{
    const int st = UdState(UserData(veh));
    if (veh != g_lastHeli) { g_lastHeli = veh; g_lastHeliState = st; }
    if (st != 8) g_lastHeliState = st;
    g_lastHeliT = now;
}

// Before the engine's update of a helicopter the player is no longer in. If it is the one he
// just left mid-flight (tagging into a car below, say), the game has just set "lift 5 u and pin
// it there" (state 8, End 0x1401A7D44): make it a parked, empty helicopter instead - one that
// falls if it was in the air. Only from flying / getting out (2, 4, 5): the crash-mat bail-out
// (6, 7 -> 8) is left as the game made it.
static void HandOver(void* veh, const Config& cfg, uint32_t now)
{
    if (veh != g_lastHeli) return;
    uint8_t* ud = UserData(veh);
    if (!ud) { g_lastHeli = nullptr; return; }
    if (UdState(ud) == 8 && cfg.heli_exit_anywhere &&
        (g_lastHeliState == 2 || g_lastHeliState == 4 || g_lastHeliState == 5))
    {
        At<int32_t>(ud, eng::UD_STATE) = 0;
        g_lastHeli = nullptr;
        if (g_wr.active && g_wr.veh == veh) return;                     // our own bail-out: already falling
        const float hag = HeightAboveGround(veh);
        if (cfg.heli_bail && hag > cfg.heli_park_height + 0.5f && hag < 1e30f)
        {
            StartWreck(veh, nullptr, cfg);
            Log(">>> heli: you left it %.1f up - no lift-away, it falls\n", hag);
        }
        else Log(">>> heli: you left it on the ground - no lift-away, it stays\n");
        return;
    }
    if (now - g_lastHeliT > 5000) g_lastHeli = nullptr;
}

// Before the engine's update of the wreck. True = the wreck model runs this frame.
static bool WreckPre(void* veh, uint32_t now)
{
    if (!g_wr.active || veh != g_wr.veh) return false;
    // not updated for 10 s (streamed out, gone - longer than any pause menu) or two minutes old:
    // forget it, so a vehicle that later reuses the address is never mistaken for it
    if (now - g_wr.lastSeen > 10000 || now - g_wr.t0 > 120000) { g_wr.active = false; return false; }
    g_wr.lastSeen = now;
    uint8_t* ud = UserData(veh);
    const int st = UdState(ud);
    if (!ud || st == 1 || st == 2)
    {
        // somebody is flying it again (or it is no longer a helicopter we know)
        g_wr.active = false;
        Log(">>> heli: %s\n", ud ? "back in the helicopter - normal flight" : "lost the wreck - released");
        return false;
    }
    if (st == 8) At<int32_t>(ud, eng::UD_STATE) = 0;                    // no lift-away: it falls
    // the jump-off refused? one plain exit after 0.7 s; still aboard at 2.5 s: give it back
    if (g_wr.ch && CtxVehicle() == veh)
    {
        void* ride = At<void*>(g_wr.ch, eng::CH_RIDE);
        const bool riding = PtrOk(ride) && At<int32_t>(ride, eng::RIDE_STATE) == 4;
        if (riding && !g_wr.retried && now - g_wr.t0 > 700)
        {
            g_wr.retried = true;
            void** rvt = At<void**>(ride, 0);
            const DismountFn dismount = PtrOk(rvt) ? reinterpret_cast<DismountFn>(rvt[eng::SLOT_DISMOUNT]) : nullptr;
            if (InImage(reinterpret_cast<const void*>(dismount))) dismount(ride, nullptr);
            Log(">>> heli exit: still aboard after the jump-off - plain exit\n");
        }
        else if (riding && now - g_wr.t0 > 2500)
        {
            At<int32_t>(ud, eng::UD_STATE) = 2;
            g_wr.active = false;
            Log(">>> heli exit: the game kept you in - flying on\n");
            return false;
        }
    }
    return true;
}

static void TelemetryWreck(void* veh, void* inst, const heli::Input& in, const heli::WreckOutput& o)
{
    TelemetryHeader();
    g_t += in.dt;
    mdl::TelemetryLine("H,%.4f,%d,0,%d,0.00,0.00,0.00,0.00,0.00,0.00,0.00,%.2f,%.2f,%.2f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,"
                       "%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.1f,%.3f,%.1f,%.2f,%.4f,%d\n",
        g_t, At<int32_t>(veh, eng::VEH_SIMLOD), 10 + o.phase,
        At<float>(inst, eng::INST_POS), At<float>(inst, eng::INST_POS + 4), At<float>(inst, eng::INST_POS + 8),
        o.lv[0], o.lv[1], o.lv[2], o.av[0], o.av[1], o.av[2],
        in.up[0], in.up[1], in.up[2], -in.back[0], -in.back[2],
        in.hag < 1e30f ? in.hag : -1.0f, o.thrust, o.spin, o.tiltDeg, o.dv, in.dt, UdState(UserData(veh)));
}

// After the engine's update of the wreck: undo its writes, fly the wreck model.
static void WreckPost(void* veh, const float lv0[3], const float av0[3], const Config& cfg, bool telemetry)
{
    void* inst = At<void*>(veh, eng::VEH_INSTANCE);
    if (!PtrOk(inst)) return;
    void* body = At<void*>(inst, eng::INST_BODY);
    if (!PtrOk(body)) return;
    heli::Input in{};
    in.dt = At<float>(veh, eng::VEH_DT);
    for (int k = 0; k < 3; ++k)
    {
        in.right[k] = At<float>(inst, eng::INST_ROW0 + 4 * k);
        in.up[k]    = At<float>(inst, eng::INST_ROW1 + 4 * k);
        in.back[k]  = At<float>(inst, eng::INST_ROW2 + 4 * k);
        in.lv[k] = lv0[k]; in.av[k] = av0[k];
    }
    if (At<int32_t>(veh, eng::VEH_SIMLOD) != 1 && g_terrain)           // reduced sim LOD: see Fly
    {
        void* hd = At<void*>(veh, eng::VEH_HDRIVE);
        if (PtrOk(hd)) g_terrain(hd);
    }
    in.hag = HeightAboveGround(veh);
    heli::WreckOutput o;
    heli::Wreck(g_wp, g_wr.w, in, o);
    if (o.impact) Log(">>> heli wreck: hit something at %.1f u/s after %.1f s - no more control\n",
                      g_wr.w.impactSpeed, g_wr.w.t);
    if (!o.valid)
    {
        if (g_wr.w.phase >= 2) { g_wr.active = false; Log(">>> heli wreck: at rest - left where it lies\n"); }
        return;
    }
    for (int k = 0; k < 3; ++k)
    {
        At<float>(body, eng::BODY_LINVEL + 4 * k) = o.lv[k];
        At<float>(body, eng::BODY_ANGVEL + 4 * k) = o.av[k];
    }
    if (telemetry) TelemetryWreck(veh, inst, in, o);
    // The hard landing destroys it - the game's own explosion through TakeDamage (vehdamage.h). Judged on
    // the speed it hit at (the impact frame's jump, or its speed just before the ground), not the biggest
    // single-frame jump afterwards: the contact solver spreads an impact over frames, and the old test
    // (o.dv > 9) let a 29 u fall survive at 9.6 u/s. A fall of more than 2 s always ends it.
    if (cfg.heli_wreck_on_crash && !g_wr.wrecked && g_wr.w.phase >= 1 &&
        (g_wr.w.impactSpeed >= cfg.heli_crash_speed || o.dv >= cfg.heli_crash_speed || g_wr.w.t - g_wr.w.restT > 2.0f))
    {
        g_wr.wrecked = true;
        const int left = vd::Damage(veh, 1000);
        if (left >= 0) g_wr.active = false;                             // destroyed: the game's sequence owns it
        Log(">>> heli wreck: hit the ground at %.1f u/s after %.1f s - %s\n", std::fmax(g_wr.w.impactSpeed, o.dv),
            g_wr.w.t, left == 0 ? "destroyed (the game's own explosion)" : left > 0 ? "damaged" : "no damage (damage off / invulnerable)");
    }
}

// ---------------------------------------------------------------------------------------------
//  The flying helicopter
// ---------------------------------------------------------------------------------------------
// Crash damage while flown (the game never damages a class-6 vehicle in a collision, vehdamage.h):
// the solver's impulse is the jump between the velocity lcudrive wrote last frame and the one the
// physics step left, before the engine's update. Damage per the game's 0.3 / 0.6 / 0.8 ladder.
struct CrashTrack { void* veh; float last[3]; bool primed; float cool; void* dead; uint32_t deadT; bool ejected; };
static CrashTrack g_ct = {};

// The pilot of a destroyed helicopter: if the game hasn't put him out after 1.2 s, the bail-out's own
// jump-off dismount does (OffPadExit's recipe).
static void EjectFromWreck(void* veh, uint32_t now)
{
    if (g_ct.dead != veh || g_ct.ejected || now - g_ct.deadT < 1200) return;
    g_ct.ejected = true;
    uint8_t* ch = CtxChar();
    void* ride = PtrOk(ch) ? At<void*>(ch, eng::CH_RIDE) : nullptr;
    if (!PtrOk(ride) || At<int32_t>(ride, eng::RIDE_STATE) != 4) return;   // already out
    void** rvt = At<void**>(ride, 0);
    const DismountFn dismount = PtrOk(rvt) ? reinterpret_cast<DismountFn>(rvt[eng::SLOT_DISMOUNT]) : nullptr;
    if (!InImage(reinterpret_cast<const void*>(dismount))) return;
    alignas(16) uint8_t prm[0x40] = {};
    *reinterpret_cast<int32_t*>(prm) = 1;                               // jump off
    dismount(ride, prm);
    Log(">>> heli: the pilot was still aboard the destroyed helicopter - jumped out\n");
}

// After the engine's Update: undo its writes and fly the model. Returns false if it stood down.
static bool Fly(void* veh, const float lv0[3], const float av0[3], const Config& cfg, bool telemetry)
{
    EjectFromWreck(veh, GetTickCount());
    const int32_t stage = At<int32_t>(veh, eng::VEH_SIMLOD);
    if (stage == 10) return false;                                     // asleep (sim LOD 10)
    { int hc = 1, hm = 1; if (vd::Health(veh, hc, hm) && hc < 1) return false; }   // destroyed: the game's sequence
    if (!(At<uint8_t>(veh, eng::VEH_FLAGS4) & 1)) return false;        // not spawned yet
    if (At<uint8_t>(veh, eng::VEH_FLAGS5) & 0xA0) return false;        // parked / disabled
    uint8_t* ud = UserData(veh);
    const int udState = UdState(ud);
    if (ud && udState != 2) return false;                              // boarding, landing, getting out
    void* inst = At<void*>(veh, eng::VEH_INSTANCE);
    if (!PtrOk(inst)) return false;
    void* body = At<void*>(inst, eng::INST_BODY);
    if (!PtrOk(body)) return false;

    if (veh != g_veh) { g_veh = veh; heli::Reset(g_state); LogNewHeli(veh); }
    const bool crashPrimed = g_ct.primed && g_ct.veh == veh;
    g_ct.primed = false;                                                // re-armed below once we write
    // Sim LOD 5 (far from / not seen by a camera - e.g. the other co-op player's view): the engine's hover
    // update stops right after the base update - no servo, so no terrain probe either, and the height
    // above ground would freeze. Run the probe ourselves. (Read as a damage stage until 2026-11-13.)
    if (stage != 1 && g_terrain)
    {
        void* hd = At<void*>(veh, eng::VEH_HDRIVE);
        if (PtrOk(hd)) g_terrain(hd);
    }

    heli::Input in;
    in.dt = At<float>(veh, eng::VEH_DT);
    for (int k = 0; k < 3; ++k)
    {
        in.right[k] = At<float>(inst, eng::INST_ROW0 + 4 * k);
        in.up[k]    = At<float>(inst, eng::INST_ROW1 + 4 * k);
        in.back[k]  = At<float>(inst, eng::INST_ROW2 + 4 * k);
        in.lv[k] = lv0[k]; in.av[k] = av0[k];
    }
    in.hag = HeightAboveGround(veh);
    void* ctrl = At<void*>(veh, eng::VEH_CTRL);
    float thrF = 0, revF = 0, brkF = 0;
    in.cTurn = PtrOk(ctrl) ? At<float>(ctrl, eng::IN_X) : 0.0f;
    in.cFwd  = PtrOk(ctrl) ? At<float>(ctrl, eng::IN_Y) : 0.0f;
    in.cVert = PtrOk(ctrl) ? At<float>(ctrl, eng::IN_Z) : 0.0f;
    if (PtrOk(ctrl))
    {
        thrF = At<float>(ctrl, eng::CTRL_THROTTLE);
        revF = At<float>(ctrl, eng::CTRL_REVERSE);
        brkF = At<float>(ctrl, eng::CTRL_BRAKE);
        // the forward key may arrive as throttle / reverse instead of the stick axis (first flight:
        // +0x04 stayed 0 the whole time while the heli was flown around)
        if (std::fabs(in.cFwd) < 0.05f) in.cFwd = Clampf(thrF - std::fmax(revF, brkF), -1.0f, 1.0f);
    }
    // A controller's left stick, read directly. The game turns it into "steer toward where the stick
    // points, throttle = how far" (camera-relative): pulling back while flying forward arrives as MORE
    // throttle plus a turn (2026-10-24 flight: throttle in 93% of the frames at speed, reverse never;
    // only at a standstill does back read as reverse). Here: up = nose down / forward, back = nose up
    // / back, sideways = turn. Centred, or on the keyboard: the game's fields, as before.
    // The turn takes the stick's X as it is: the game's own turn field has that sign (with it, left was
    // left); negated, in the 2026-10-24 10:2x test flight left and right were inverted.
    if (cfg.heli_stick_direct)
    {
        pad::Poll();
        const float lx = pad::g_lx, ly = pad::g_ly;
        if (pad::g_user >= 0 && mdl::KeyDownInGame(-1) && lx * lx + ly * ly > 0.2f * 0.2f)
        {
            in.cFwd = Clampf(ly, -1.0f, 1.0f);
            in.cTurn = Clampf(lx, -1.0f, 1.0f);
        }
    }
    // We only ever fly the PLAYER's helicopter, so it is occupied by definition. (The FC8 bit is
    // not set for the player - the engine finds the driver through a seat lookup - and reading it
    // spooled the rotor down to nothing on the first flight.)
    in.occupied = 1;

    // crash damage: the impulse the physics step applied since our last write
    if (crashPrimed && cfg.heli_crash_damage && in.dt > 0.0f && in.dt <= 0.1f)
    {
        g_ct.cool -= in.dt;
        float dv2 = 0.0f;
        for (int k = 0; k < 3; ++k) { const float d = lv0[k] - g_ct.last[k]; dv2 += d * d; }
        const float dv = std::sqrt(dv2);
        const int n = g_ct.cool <= 0.0f ? vd::CrashDamage(dv, cfg.heli_damage_speed) : 0;
        if (n > 0)
        {
            g_ct.cool = 0.5f;                                          // resting contact must not count again
            int hc = -1, hm = -1;
            vd::Health(veh, hc, hm);
            const int left = vd::Damage(veh, n);
            Log(">>> heli: crashed at %.1f u/s - %d damage, health %d -> %d of %d%s\n", dv, n, hc, left, hm,
                left == 0 ? " - DESTROYED" : left < 0 ? " (no damage: off or invulnerable)" : "");
            if (left == 0) { g_ct.dead = veh; g_ct.deadT = GetTickCount(); g_ct.ejected = false; return false; }
        }
    }
    heli::Output out;
    heli::Update(ParamsFrom(cfg), g_state, in, out);
    if (!out.valid) return false;                                      // the engine's frame stands
    for (int k = 0; k < 3; ++k)
    {
        At<float>(body, eng::BODY_LINVEL + 4 * k) = out.lv[k];
        At<float>(body, eng::BODY_ANGVEL + 4 * k) = out.av[k];
        g_ct.last[k] = out.lv[k];
    }
    g_ct.veh = veh; g_ct.primed = true;

    if (telemetry)
    {
        TelemetryHeader();
        g_t += in.dt;
        mdl::TelemetryLine("H,%.4f,%d,%d,%d,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,"
                           "%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.1f,%.3f,%.3f,%.2f,%.4f,%d\n",
            g_t, stage, in.occupied, out.landed, g_state.spool, in.cTurn, in.cFwd, in.cVert, thrF, revF, brkF,
            At<float>(inst, eng::INST_POS), At<float>(inst, eng::INST_POS + 4), At<float>(inst, eng::INST_POS + 8),
            out.lv[0], out.lv[1], out.lv[2], out.av[0], out.av[1], out.av[2],
            in.up[0], in.up[1], in.up[2], -in.back[0], -in.back[2],
            in.hag < 1e30f ? in.hag : -1.0f, out.thrust, out.pitchT, out.rollT, out.vyT, in.dt, udState);
    }
    return true;
}

// Player 2 (lcudrive.cpp SwapCtx): the flight state, the helicopter last flown and the wreck being
// tracked are per player. The patches, masks and telemetry clock are shared.
struct Parked { heli::State state; void* veh; void* lastHeli; int lastHeliState; uint32_t lastHeliT; WreckTrack wr; CrashTrack ct; };
static Parked g_p2 = {};
static void SwapP2()
{
    Parked t;
    t.state = g_state; t.veh = g_veh; t.lastHeli = g_lastHeli; t.lastHeliState = g_lastHeliState; t.lastHeliT = g_lastHeliT; t.wr = g_wr; t.ct = g_ct;
    g_state = g_p2.state; g_veh = g_p2.veh; g_lastHeli = g_p2.lastHeli; g_lastHeliState = g_p2.lastHeliState;
    g_lastHeliT = g_p2.lastHeliT; g_wr = g_p2.wr; g_ct = g_p2.ct;
    g_p2 = t;
}
// While player 1's state is live: is this helicopter one player 2 just left, or player 2's falling wreck?
static bool P2Owns(const void* veh) { return veh && (g_p2.lastHeli == veh || (g_p2.wr.active && g_p2.wr.veh == veh)); }

// Once at startup (lcudrive.cpp Main): the engine functions this file calls.
static void Init()
{
    g_hintFlag = reinterpret_cast<HintFlagFn>(g_base + eng::FN_HINT_FLAG);
    g_setState = reinterpret_cast<SetStateFn>(g_base + eng::FN_CHAR_SETSTATE);
    g_terrain  = reinterpret_cast<TerrainFn>(g_base + eng::FN_HELI_TERRAIN);
    InitFreeFlight();
}

} // namespace hl
