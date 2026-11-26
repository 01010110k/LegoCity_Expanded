// =============================================================================================
//  missions.h - [missions1] story missions: which one was just completed (-> the radio's mission
//  news) and whether one is running (-> "Missions: stock game"). Notes\68-missions.md.
//
//  Reads (never writes) the game's job data, the table the save game is made of [P, exe]:
//    JobSystem*  = *(u8**)0x1417ECA08;  its data blocks at +0xE0: { entry* at +0, u32 count at +8 }
//                  (0x1400DDBF0 returns sys+0xE0; 0x1400D95C0 is the lookup)
//    entry       = 0x18 bytes: u32 FNV-1(JOBPATH, upper-cased, basis 0x811C9DC5) at +0,
//                  JobValue** at +8, u16 value count at +0x10 (0x1400D9650 is the lookup)
//    JobValue    = vtable +0, u32 FNV-1(VALUENAME) at +0xC, the value at +0x10
//                  (JobValue_Int vtable 0x140C46B38: int; JobValue_Bool 0x140C46BB8: byte)
//  - the same table JobDataSaveLoad (0x1400DA5E0 / 0x1400DA620) saves and loads, so it holds the
//  story the save game holds. Every read is SEH-guarded and bounds-checked; one bad read = no
//  snapshot this time (no news, the gate keeps its state).
//
//  Game thread: Tick() from cPopulationManager::Update (lcudrive.cpp RunMissions), at most 10 a s.
//  Publishes: g_want (the mask the hotkey thread applies), g_active (the gate's state, for the
//  radio's news hold), completions to rd::PostMission.
// =============================================================================================
#pragma once
#include "missions_mask.h"

namespace mis {

static const uintptr_t RVA_JOBSYS = 0x17ECA08, JOBSYS_BLOCKS = 0xE0;
static const uintptr_t VT_JOBINT = 0xC46B38, VT_JOBBOOL = 0xC46BB8;
static const uintptr_t RVA_GAMEUI = 0x1C7A830, UI_INCITY = 0x335D, UI_POLICESTATION = 0x335E;

static volatile LONG g_want = 0;        // msm::Mask bits the hotkey thread should apply
static volatile LONG g_active = 0;      // the gate's mission state (0 / 1), for the radio's news hold
static volatile LONG g_fired = 0;       // completions seen this session (for the log / tests)
static bool          g_fault = false;   // an exception in here: off for the session (lcudrive.cpp sets it)

static uint32_t Fnv1U(const char* s)
{
    uint32_t h = 0x811C9DC5u;
    for (; *s; ++s) { int c = static_cast<unsigned char>(*s); if (c >= 'a' && c <= 'z') c -= 0x20; h = h * 0x01000193u; h ^= static_cast<uint32_t>(c); }
    return h;
}

struct Key { uint32_t job, val; };
static Key K(const char* job, const char* val) { Key k; k.job = Fnv1U(job); k.val = Fnv1U(val); return k; }

// One value: 0 = not found / bad, 1 = int, 2 = bool. Inside the caller's SEH.
static int ReadValue(uint8_t* entries, uint32_t n, const Key& k, int* out)
{
    for (uint32_t i = 0; i < n; ++i)
    {
        uint8_t* e = entries + 0x18ull * i;
        if (At<uint32_t>(e, 0) != k.job) continue;
        uint8_t** vals = At<uint8_t**>(e, 8);
        const uint16_t nv = At<uint16_t>(e, 0x10);
        if (!PtrOk(vals) || nv == 0 || nv > 1024) return 0;
        for (uint16_t j = 0; j < nv; ++j)
        {
            uint8_t* v = vals[j];
            if (!PtrOk(v) || At<uint32_t>(v, 0xC) != k.val) continue;
            const uintptr_t vt = At<uintptr_t>(v, 0);
            if (vt == g_base + VT_JOBINT) { *out = At<int32_t>(v, 0x10); return 1; }
            if (vt == g_base + VT_JOBBOOL) { *out = At<uint8_t>(v, 0x10) ? 1 : 0; return 2; }
            return 0;
        }
        return 0;
    }
    return 0;
}

static const Key kAct = K("Flow_Setup", "Flow_Act");
static const Key kMis[5] = { {}, K("Flow_Setup.Act_1", "Act1_Mission"), K("Flow_Setup.Act_2", "Act2_Mission"),
                             K("Flow_Setup.Act_3", "Act3_Mission"), K("Flow_Setup.Act_4", "Act4_Mission") };
static const Key kSide = K("City_Setup.Bonus_Missions", "bMissionActive");

static bool ReadSnap(msm::Snap& s)
{
    s = msm::Snap();
    __try
    {
        uint8_t* sys = *reinterpret_cast<uint8_t**>(g_base + RVA_JOBSYS);
        if (!PtrOk(sys)) return false;
        uint8_t* entries = At<uint8_t*>(sys, JOBSYS_BLOCKS);
        const uint32_t n = At<uint32_t>(sys, JOBSYS_BLOCKS + 8);
        if (!PtrOk(entries) || n == 0 || n > 8192) return false;
        s.gen = reinterpret_cast<uintptr_t>(entries) ^ (static_cast<uint64_t>(n) << 48);
        int v = 0;
        if (ReadValue(entries, n, kAct, &v) != 1) return false;
        s.act = v;
        for (int a = 1; a <= 4; ++a) { if (ReadValue(entries, n, kMis[a], &v) != 1) return false; s.mis[a] = v; }
        s.side = ReadValue(entries, n, kSide, &v) == 2 && v != 0;
        s.ok = true;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { s.ok = false; return false; }
}

// A special assignment / story interior: not the city (game + 0x335D, maplabels.h) and not the police
// station (+0x335E: the level is "Police_Station", 0x14003E359) [P code; I: which levels count].
static bool Interior()
{
    __try
    {
        uint8_t* g = *reinterpret_cast<uint8_t**>(g_base + RVA_GAMEUI);
        return PtrOk(g) && At<uint8_t>(g, UI_INCITY) == 0 && At<uint8_t>(g, UI_POLICESTATION) == 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

static float VehSpeed(void* veh)
{
    __try
    {
        uint8_t* inst = PtrOk(veh) ? At<uint8_t*>(veh, eng::VEH_INSTANCE) : nullptr;
        uint8_t* body = PtrOk(inst) ? At<uint8_t*>(inst, eng::INST_BODY) : nullptr;
        if (!PtrOk(body)) return 0.0f;
        const float x = At<float>(body, eng::BODY_LINVEL), y = At<float>(body, eng::BODY_LINVEL + 4), z = At<float>(body, eng::BODY_LINVEL + 8);
        const float v = std::sqrt(x * x + y * y + z * z);
        return IsFin(v) ? v : 1e9f;                        // unreadable = "not safe"
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 1e9f; }
}

static msm::Detector   g_det;
static msm::Gate       g_gate;
static uint32_t        g_lastMs = 0;
static int             g_lastPos[2] = { -1, -1 };
static bool            g_lastSide = false, g_lastRaw = false;

// The player's vehicle kind for the gate: 0 on foot, 1 car / bike, 2 aircraft (radio's g_vehKind: 0 car, 1 bike, 2 heli / plane).
static int KindOf(void* veh, bool p1)
{
    if (!PtrOk(veh)) return 0;
    const LONG k = rd::g_vehKind;
    if (p1 && static_cast<LONG>(GetTickCount() - static_cast<uint32_t>(rd::g_vehSeen)) < 400) return k == 2 ? 2 : 1;
    return 1;                                               // player 2 (or no recent frame): count it as a road vehicle
}

// Once a world update (game thread). `on` = lcudrive running ([general] enable / `]` aside: the gate
// works whenever lcudrive is hooked), cfg = the LIVE config (mission_* keys are never masked).
static void Tick(const Config& cfg, bool hooked)
{
    const uint32_t now = GetTickCount();
    if (g_lastMs && now - g_lastMs < 100) return;
    const double dt = g_lastMs ? (now - g_lastMs) / 1000.0 : 0.1;
    g_lastMs = now;
    const double t = now / 1000.0;

    msm::Snap s;
    const bool ok = ReadSnap(s);
    uint8_t* p1 = *reinterpret_cast<uint8_t**>(g_base + eng::RVA_PLAYER1);
    if (!PtrOk(p1)) p1 = *reinterpret_cast<uint8_t**>(g_base + eng::RVA_PLAYERS);
    const bool present = PtrOk(p1);

    // 1. completions
    msm::DetParams dp;
    dp.stable = cfg.mission_detect_stable; dp.settle = cfg.mission_detect_settle; dp.absent = cfg.mission_detect_absent;
    int fired[1]; int why = 0;
    const int nf = msm::Feed(g_det, s, present, t, dp, fired, 1, &why);
    if (ok && (s.act != g_lastPos[0] || msm::PosMission(s) != g_lastPos[1]))
    {
        Log("    missions: story position act %d, mission %d (counters %d / %d / %d / %d)\n", s.act, msm::PosMission(s),
            s.mis[1], s.mis[2], s.mis[3], s.mis[4]);
        g_lastPos[0] = s.act; g_lastPos[1] = msm::PosMission(s);
    }
    if (why != msm::W_NONE && why != msm::W_FIRE)
        Log(">>> missions: baseline at act %d, mission %d - %s (no news)\n", g_det.base.act, msm::PosMission(g_det.base), msm::kWhyNames[why]);
    for (int i = 0; i < nf; ++i)
    {
        const msm::MissionDef& m = msm::kMissions[fired[i]];
        InterlockedIncrement(&g_fired);
        Log(">>> missions: COMPLETED %s (act %d, chapter %d%s%s) - %s\n", m.id, m.act, m.chapter, m.chapterEnd ? ", the chapter's last" : "",
            m.special ? ", a special assignment" : "", cfg.radio_mission_news ? "news queued for the radio" : "[radio] mission_news = 0: no news");
        if (cfg.radio_mission_news) rd::PostMission(m.id);
    }

    // 2. the gate
    msm::GateIn in;
    in.on = hooked && cfg.mission_stock != 0;
    in.story = wd::MissionRunning();
    in.interior = Interior();
    in.side = ok && s.side;
    in.sideOn = cfg.mission_stock_side != 0;
    in.dt = dt;
    void* v1 = ::PlayerVehicle();
    void* v2 = Player2Char() ? ::Player2Vehicle() : nullptr;
    in.veh[0] = KindOf(v1, true);  in.speed[0] = in.veh[0] ? VehSpeed(v1) : 0.0f;
    in.veh[1] = KindOf(v2, false); in.speed[1] = in.veh[1] ? VehSpeed(v2) : 0.0f;
    in.cutscene = rd::g_hold != 0 && !br::Quiet();
    msm::GateParams gp;
    gp.enter = cfg.mission_stock_enter; gp.exit = cfg.mission_stock_exit; gp.safe = cfg.mission_stock_safe_speed;
    const bool wasActive = g_gate.active;
    uint8_t changed = 0;
    const uint8_t mask = msm::Step(g_gate, in, gp, &changed);
    const bool raw = msm::Raw(in);
    if (raw != g_lastRaw || in.side != g_lastSide)
    {
        Log("    missions: signal %s (story flag %d, interior %d, side mission %d)\n", raw ? "ON" : "off", in.story, in.interior, in.side);
        g_lastRaw = raw; g_lastSide = in.side;
    }
    if (g_gate.active != wasActive)
        Log(">>> missions: %s\n", g_gate.active ? (in.on ? "a mission is running - Missions: stock game suspends the mod's gameplay changes"
                                                         : "a mission is running (Missions: stock game is off - nothing suspended)")
                                                : "the mission is over");
    if (changed)
        Log(">>> missions: suspended now: %s%s\n", msm::MaskText(mask),
            (g_gate.active && in.on && (mask & (msm::M_VEH | msm::M_AIR)) != (msm::M_VEH | msm::M_AIR)) ? " (vehicles / aircraft wait until safe to hand over)" : "");
    InterlockedExchange(&g_want, mask);
    InterlockedExchange(&g_active, g_gate.active ? 1 : 0);
    rd::g_newsHold = g_gate.active ? 1 : 0;
}

} // namespace mis
