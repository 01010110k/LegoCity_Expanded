// =============================================================================================
//  wanted_flee.h - traffic that flees a chase (2026-11-15). Included by wanted.h (namespace wd);
//  the decisions are wanted_model.h's Flee* (harness-checked), this is the engine side.
//
//  The cut-content pass (Notes\46 §1 item 8): Vehicle.DriverlessFlee, the never-scripted sibling of
//  the used DriverlessPursue. Its Execute 0x140118A10 [P, read 2026-11-15]:
//    * the target (its first argument): a character (script value type 8, the object's +0xC4 bit
//      0x1000) -> ch->vt[60]() hands a handle to 0x14073DF20 (target type 2, the weak link at +0x28,
//      list head +0x190 on the other side); or a vehicle (type 9) -> its user data (0x1404A8970 - 0x10)
//      +0x38 to 0x14073DFC0 (target type 1, the weak link at +0x10, list head +0x100). The target is
//      0x50 bytes: +0 position (0, 0, 0, 1), +0x10 / +0x28 the links, +0x40 0, +0x44 a float (0), +0x48
//      an int (0), +0x4C the type;
//    * the task: NewTask 0x140584B70(veh+0x68 = the vehicle's AI brain, id 0x32, slot 0); its target
//      +0x150 = the one above (assign 0x14073E280, the temporary freed by 0x140104CF0); +0x1B0 =
//      ud+0x1A0 (qword), +0x1B8 = ud+0x1A8 (word) - the same pair the StopTask takes at +0x148 / +0x150
//      (wanted_officers.h HoldCar); +0x1A8 = its second argument, a float - the speed [I: every
//      DriverlessPursue / DriveTo call in the scripts passes the speed there, 6.5 to 20].
//  Here: the task at slot 1, above the car's own traffic task in slot 0 (HoldCar's slot, proven in
//  game), so letting go is ClearQ(ai, 1) and the car drives on as traffic (FreeCar's, proven) - not
//  slot 0, which would throw its traffic task away. Fleeing from the chased player's vehicle (type 1).
//  [U] the flee task's own behaviour (where it drives, whether it honours lights, how it ends by
//  itself) - never run by the game; [U] that a crowd driver stays seated through it (a Driverless
//  task drives the vehicle, not the driver).
//  Off: [wanted] flee = 0. It stands down (every car let go) with wanted mode, on unhook, after a
//  fault, during story missions and script pursuits, and in a new world (forgotten).
// =============================================================================================
#pragma once

typedef void (__fastcall* FleeTgtVehFn)(void* tgt, void* obj, int i, float f);     // 0x14073DFC0
typedef void (__fastcall* FleeTgtSetFn)(void* dst, const void* src);               // 0x14073E280
typedef void (__fastcall* FleeTgtFreeFn)(void* tgt);                               // 0x140104CF0
static const uintptr_t FN_FLEE_TGTVEH = 0x73DFC0, FN_FLEE_TGTSET = 0x73E280, FN_FLEE_TGTFREE = 0x104CF0;
static const uintptr_t RVA_VT_CAR = 0xCE3368;     // ApiVehicleCar (re2 PRIMER)
static const int TASK_DRIVERLESS_FLEE = 0x32, FLEE_SLOT = 1;
static const uintptr_t FT_TARGET = 0x150, FT_SPEED = 0x1A8, FT_LANE = 0x1B0, FT_LANE2 = 0x1B8;

struct FleeCar
{
    void*    veh;
    uint8_t* cell;               // its PM traffic slot (alive while *cell == veh and ctl[1] != 0)
    uint8_t* ctl;
    float    t;
    int      from;               // the player it flees
};
static const int kMaxFleeCars = 6;
static FleeCar  g_flee[kMaxFleeCars] = {};
static int      g_nFlee = 0;
struct FleeRest { void* veh; uint32_t ms; };
static FleeRest g_fleeRest[16] = {};
static int      g_fleeRestAt = 0;
static float    g_fleeSince = 1e9f;
static int      g_fleeSent = 0;

static bool FleeAlive(const FleeCar& f)
{
    return PtrOk(f.cell) && PtrOk(f.ctl) && At<int32_t>(f.ctl, 4) != 0 && At<void*>(f.cell, 0) == f.veh;
}

static bool FleeRested(void* veh, uint32_t now, float restS)
{
    for (const FleeRest& r : g_fleeRest) if (r.veh == veh && now - r.ms < static_cast<uint32_t>(restS * 1000.0f)) return true;
    return false;
}

// Let one go (it drives on as traffic). `alive`: the car is still there to be told.
static void FleeLetGo(int i, bool alive, const char* why)
{
    FleeCar& f = g_flee[i];
    if (alive)
    {
        void* ai = At<void*>(f.veh, 0x68);
        if (PtrOk(ai)) Fn<ClearQFn>(FN_CLEARQ)(ai, FLEE_SLOT);
    }
    g_fleeRest[g_fleeRestAt] = { f.veh, GetTickCount() };
    g_fleeRestAt = (g_fleeRestAt + 1) % 16;
    Log("    wanted: a fleeing car back to traffic after %.1f s - %s\n", f.t, why);
    g_flee[i] = g_flee[--g_nFlee];
}

static void FleeReleaseAll(const char* why)
{
    while (g_nFlee) FleeLetGo(g_nFlee - 1, FleeAlive(g_flee[g_nFlee - 1]), why);
}

static void FleeForgetAll() { g_nFlee = 0; }       // a new world: the pointers mean nothing now

// Send `veh` fleeing from the player vehicle `from` at `speed` u/s. False: nothing was changed.
static bool FleeSend(void* veh, void* from, float speed)
{
    void* ai = At<void*>(veh, 0x68);
    uint8_t* ud = VehicleUserData(veh);
    uint8_t* fud = VehicleUserData(from);
    void* fobj = PtrOk(fud) ? At<void*>(fud, 0x38) : nullptr;
    if (!PtrOk(ai) || !PtrOk(ud) || !PtrOk(fobj)) return false;
    uint8_t* t = Fn<NewTaskFn>(FN_NEWTASK)(ai, TASK_DRIVERLESS_FLEE, FLEE_SLOT);
    if (!t) return false;
    alignas(16) uint8_t tgt[0x60] = {};
    Fn<FleeTgtVehFn>(FN_FLEE_TGTVEH)(tgt, fobj, 0, 0.0f);
    Fn<FleeTgtSetFn>(FN_FLEE_TGTSET)(t + FT_TARGET, tgt);
    Fn<FleeTgtFreeFn>(FN_FLEE_TGTFREE)(tgt);
    At<uint64_t>(t, FT_LANE) = At<uint64_t>(ud, 0x1A0);
    At<uint16_t>(t, FT_LANE2) = At<uint16_t>(ud, 0x1A8);
    At<float>(t, FT_SPEED) = speed;
    return true;
}

// Once a tick from Tick, after the players' levels are known. lv: each player's level (0 = not here).
static void RunFlee(const World& w, float dt, const Config& cfg, bool blocked, const int lv[2])
{
    wanted::FleeParams fp;
    fp.minLevel = cfg.wanted_flee_level;
    fp.radius = cfg.wanted_flee_radius;
    const uint32_t now = GetTickCount();
    void* pv[2] = { ::PlayerVehicle(), RawPlayer(1) ? ::Player2Vehicle() : nullptr };
    float ppos[2][3] = {}; bool pOk[2] = { false, false }, chase[2] = { false, false };
    for (int i = 0; i < 2; ++i)
    {
        uint8_t* ch = RawPlayer(i);
        void* body = pv[i] ? BodyOf(pv[i]) : nullptr;
        if (body) for (int k = 0; k < 3; ++k) ppos[i][k] = At<float>(body, eng::BODY_POS + 4 * k);
        else if (PtrOk(ch)) for (int k = 0; k < 3; ++k) ppos[i][k] = At<float>(ch, 0xA8 + 4 * k);
        pOk[i] = (body || PtrOk(ch)) && IsFin(ppos[i][0]) && IsFin(ppos[i][1]) && IsFin(ppos[i][2]);
        chase[i] = cfg.wanted_flee && !blocked && pOk[i] && wanted::FleeChase(fp, lv[i], body != nullptr, g_recentSpeed[i]);
    }
    // the ones out there
    for (int i = g_nFlee - 1; i >= 0; --i)
    {
        FleeCar& f = g_flee[i];
        f.t += dt;
        const bool alive = FleeAlive(f);
        float d = 1e9f;
        if (alive && pOk[f.from]) if (void* b = BodyOf(f.veh)) d = DistTo(b, ppos[f.from]);
        const bool still = cfg.wanted_flee && !blocked && lv[f.from] >= fp.minLevel;
        const int e = wanted::FleeCheck(fp, alive, f.t, d, still);
        if (e != wanted::kFleeOn) FleeLetGo(i, alive, wanted::FleeEndName(e));
    }
    // a new one: the nearest eligible car to a chased player
    g_fleeSince += dt;
    if (!chase[0] && !chase[1]) return;
    uint8_t* slots = reinterpret_cast<uint8_t*>(w.pm) + 0x568;
    void* best = nullptr; uint8_t* bestCell = nullptr; uint8_t* bestCtl = nullptr; int bestFrom = 0; float bestD = 1e9f;
    for (int s = 0; s < 0x80; ++s)
    {
        uint8_t* cell = At<uint8_t*>(slots, s * 16);
        uint8_t* ctl = At<uint8_t*>(slots, s * 16 + 8);
        if (!PtrOk(cell) || !PtrOk(ctl) || At<int32_t>(ctl, 4) == 0) continue;
        void* veh = At<void*>(cell, 0);
        if (!PtrOk(veh) || veh == pv[0] || veh == pv[1]) continue;
        void* body = BodyOf(veh);
        if (!body) continue;
        for (int pl = 0; pl < 2; ++pl)
        {
            if (!chase[pl]) continue;
            wanted::FleeCand c = {};
            c.dist = DistTo(body, ppos[pl]);
            if (c.dist > fp.radius || c.dist >= bestD) continue;
            uint8_t* ud = VehicleUserData(veh);
            c.manned = KrawlieDriverRide(ud) != nullptr || UserDataDriver(ud) != nullptr;
            c.police = IsPoliceModel(veh);
            c.car = At<uintptr_t>(veh, 0) == g_base + RVA_VT_CAR;
            for (int q = 0; q < 2; ++q) if (pOk[q] && !pv[q] && DistTo(body, ppos[q]) < 6.0f) c.nearFoot = true;
            for (int k = 0; k < g_nFlee; ++k) if (g_flee[k].veh == veh) c.fleeing = true;
            c.rested = FleeRested(veh, now, fp.restS);
            if (!wanted::FleePick(fp, c, g_nFlee, g_fleeSince)) continue;
            best = veh; bestCell = cell; bestCtl = ctl; bestFrom = pl; bestD = c.dist;
        }
    }
    if (!best || g_nFlee >= kMaxFleeCars) return;
    g_fleeSince = 0.0f;
    char name[64] = "?";
    void* def = At<void*>(best, eng::VEH_DEF);
    if (PtrOk(def)) ReadModelName(def, name, sizeof(name));
    if (!FleeSend(best, pv[bestFrom], cfg.wanted_flee_speed))
    {
        Log("    wanted: a car near %s would not take the flee task (%s) - left alone\n", Pn(bestFrom), name);
        return;
    }
    g_flee[g_nFlee++] = { best, bestCell, bestCtl, 0.0f, bestFrom };
    ++g_fleeSent;
    Log(">>> wanted: %s %.0f u from %s (level %d) panics and flees the chase at %.0f u/s (DriverlessFlee; %d fleeing, %d so far)\n",
        name, bestD, Pn(bestFrom), lv[bestFrom], cfg.wanted_flee_speed, g_nFlee, g_fleeSent);
}
