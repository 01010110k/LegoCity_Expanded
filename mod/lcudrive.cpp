// =============================================================================================
//  lcudrive.dll - LEGO City Undercover driving overhaul, second generation.
//
//  WHY THIS EXISTS (Notes\24-drive-redesign.md has the full argument)
//  The engine's grip is not a force applied in the vehicle Update: Update only CONFIGURES a Dyno
//  ContactJoint per wheel, and the solver enforces it later in the frame. vehsim pushed its own
//  velocity into the car after Update - and the solver then took it back out, every frame, up to
//  the stock grip limit. Two models were never blended; one silently overruled the other.
//
//  lcudrive works WITH the solver. For the player's car only, it replaces the engine routines
//  that decide what the solver enforces - steering, the per-wheel joint setup, the stock linear
//  drag and the scripted donut yaw - so there is exactly one model in the solve, and it is ours.
//  Traffic runs the stock model. The shared vehicle definitions are never written.
//
//  In game:  ] = lcudrive on   [ = stock   Ctrl+Alt+T = telemetry   Ctrl+Alt+R = reload ini
//            Pause / Ctrl+Alt+U = unhook everything (works without focus).   All of them beep.
// =============================================================================================
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstdio>
#include <cstdarg>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <share.h>

// Every beep the mod makes goes through here (2026-11-15: beeps off by default): [general] beeps = 0
// makes them all silent (the timing of the hotkey thread stays: a silent beep returns at once).
static volatile LONG g_beepsOn = 0;
static inline BOOL LcuBeep(DWORD f, DWORD ms) { return g_beepsOn ? ::Beep(f, ms) : TRUE; }
#define Beep(f, ms) LcuBeep((f), (ms))

#include "engine.h"
#include "sim_bridge.h"

// ---------------------------------------------------------------------------------------------
//  Logging
// ---------------------------------------------------------------------------------------------
static FILE*     g_log = nullptr;
static uint64_t  g_t0 = 0;
static uint32_t  g_frame = 0;            // counts player-car updates
static char      g_dir[MAX_PATH] = {};   // folder this DLL lives in, with trailing backslash

static void Log(const char* fmt, ...)
{
    if (!g_log) return;
    char buf[2048];
    const unsigned long long ms = GetTickCount64() - g_t0;
    int w = _snprintf_s(buf, sizeof(buf), _TRUNCATE, "[%6llu.%03llu f%-7u] ",
                        ms / 1000ull, ms % 1000ull, g_frame);
    if (w < 0) w = 0;
    va_list a; va_start(a, fmt);
    _vsnprintf_s(buf + w, sizeof(buf) - w, _TRUNCATE, fmt, a);
    va_end(a);
    fputs(buf, g_log);
    fflush(g_log);
}

// ---------------------------------------------------------------------------------------------
//  Small helpers
// ---------------------------------------------------------------------------------------------
static uintptr_t g_base = 0;

static inline bool PtrOk(const void* p)
{
    const uintptr_t v = reinterpret_cast<uintptr_t>(p);
    return v >= 0x10000 && v < 0x00007FFFFFFFFFFFull;
}
template <class T> static inline T& At(void* p, uintptr_t off)
{
    return *reinterpret_cast<T*>(reinterpret_cast<uint8_t*>(p) + off);
}
static inline bool IsFin(float v)
{
    uint32_t u; std::memcpy(&u, &v, 4);
    return (u & 0x7F800000u) != 0x7F800000u;
}
static inline float Clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

static void Beep2(bool up)
{
    // Rising pair = on, falling pair = off. Runs on the hotkey thread; Beep blocks.
    if (up) { Beep(660, 70); Beep(990, 90); }
    else    { Beep(990, 70); Beep(660, 90); }
}

// ---------------------------------------------------------------------------------------------
//  Which car is the player's.
//  Player 1's MechCharacter -> alive-checked ref block -> *handle == ApiVehicle*. This chain is
//  the one vehsim settled on after two earlier attempts failed; it resolved in every session
//  logged since 2026-08-18.
//  One exception (2026-10-25): right after a bust on foot the ref twice resolved to a passing
//  traffic car (a Drakonas in another district, its AI flooring it) and lcudrive drove it for 2 s.
//  So a vehicle someone else drives is never the player's: a crowd ("krawlie") driver - how all
//  traffic is driven, the police included (Notes\38 §1.1) - or a character who isn't a player.
//  No driver at all still counts, so an empty heli you bailed out of, or a door you're half
//  through, behave as before.
// ---------------------------------------------------------------------------------------------
static bool ReadModelName(void* def, char* out, int cap);

// A vehicle's user data (veh+0x378 holds ud + 0x10, 0x1404A8970) - the object the game's vehicle
// events carry - and its driver: the user data's occupant ref (ud+0x18 object, ud+0x20 control
// block, alive while ctl+4 != 0), the character at object+0x38 when object+0x28 bit 0 is set. The
// game's own "is a player driving" test before its traffic-hit events, 0x140195061..0x1401950B6.
static uint8_t* VehicleUserData(void* veh)
{
    uint8_t* u = PtrOk(veh) ? At<uint8_t*>(veh, eng::VEH_USERDATA) : nullptr;
    return PtrOk(u) ? u - 0x10 : nullptr;
}

static uint8_t* UserDataDriver(uint8_t* ud)
{
    if (!PtrOk(ud)) return nullptr;
    uint8_t* obj = At<uint8_t*>(ud, 0x18);
    uint8_t* ctl = At<uint8_t*>(ud, 0x20);
    if (!PtrOk(obj) || !PtrOk(ctl) || At<uint32_t>(ctl, 4) == 0 || !(At<uint8_t>(obj, 0x28) & 1)) return nullptr;
    uint8_t* ch = At<uint8_t*>(obj, 0x38);
    return PtrOk(ch) ? ch : nullptr;
}

// The crowd ("krawlie") driver's riding data, if one sits in the driver's seat: the user data's
// rider list (ud+0x138; first ud+0x140, next rd+0x68, ends at ud+0xE8 - the game's own walk,
// 0x1406C2B90), a MechRidingDataVehicleKrawlie (vtable 0x140D221D8) in seat 1 (rd+0xA0), still
// riding (rd+0xF0 == 0; the car-jack sets it). Krawlie drivers never appear in the seat table.
static const uintptr_t RVA_VT_KRIDE = 0xD221D8;

static uint8_t* KrawlieDriverRide(uint8_t* ud)
{
    if (!PtrOk(ud)) return nullptr;
    uint8_t* end = ud + 0xE8;
    uint8_t* rd = At<uint8_t*>(ud, 0x140);
    for (int g = 0; PtrOk(rd) && rd != end && g < 8; ++g, rd = At<uint8_t*>(rd, 0x68))
        if (At<uintptr_t>(rd, 0) == g_base + RVA_VT_KRIDE && At<int32_t>(rd, 0xA0) == 1 && At<int32_t>(rd, 0xF0) == 0)
            return rd;
    return nullptr;
}

static const char* CharDefName(uint8_t* ch)             // ch+0x650 -> def, +0x30 -> "NPC_Cop04"...
{
    uint8_t* d = PtrOk(ch) ? At<uint8_t*>(ch, 0x650) : nullptr;
    const char* n = PtrOk(d) ? At<const char*>(d, 0x30) : nullptr;
    return PtrOk(n) ? n : "?";
}

static void* g_foreignVeh = nullptr;                    // the last one refused (logged once each)

static void* PlayerVehicle()
{
    __try
    {
        void* mc = *reinterpret_cast<void**>(g_base + eng::RVA_PLAYER1);
        if (!PtrOk(mc)) mc = *reinterpret_cast<void**>(g_base + eng::RVA_PLAYERS);
        if (!PtrOk(mc)) return nullptr;
        void* blk = At<void*>(mc, eng::MC_VEHBLK);
        if (!PtrOk(blk) || At<uint32_t>(blk, 4) == 0) return nullptr;
        void* h = At<void*>(mc, eng::MC_VEHREF);
        if (!PtrOk(h)) return nullptr;
        void* veh = *reinterpret_cast<void**>(h);
        if (!PtrOk(veh)) return nullptr;
        uint8_t* ud = VehicleUserData(veh);
        uint8_t* drv = UserDataDriver(ud);
        const bool npc = drv && drv != mc && At<uint8_t>(drv, 0x3E1) == 0;   // ch+0x3E1: is a player
        // A boat keeps a crowd crew record in seat 1 while a player sails it (2026-11-14: police boats
        // arrived only for a swimmer, not for a player sailing a boat - the log refused the player's own
        // NAVAL_HAULER2 as crowd-driven). So the crowd-driver test doesn't apply to boats, nor to a
        // vehicle whose registered driver is this player.
        const bool crowd = drv != mc && At<int32_t>(veh, eng::VEH_CLASS) != 3 && KrawlieDriverRide(ud);
        if (npc || crowd)
        {
            if (veh != g_foreignVeh)
            {
                g_foreignVeh = veh;
                char name[64];
                ReadModelName(At<void*>(veh, eng::VEH_DEF), name, sizeof(name));
                Log(">>> your vehicle ref points at %s %p, which %s is driving - not yours, ignored\n",
                    name[0] ? name : "a vehicle", veh, npc ? CharDefName(drv) : "a crowd driver");
            }
            return nullptr;
        }
        return veh;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}

// The car player 2 is driving (co-op), or null. g_players[1] must be a live, active player character
// other than player 1; the same "somebody else drives it" tests as PlayerVehicle, without its log line.
static void* Player2Vehicle()
{
    __try
    {
        void** players = reinterpret_cast<void**>(g_base + eng::RVA_PLAYERS);
        void* p1 = *reinterpret_cast<void**>(g_base + eng::RVA_PLAYER1);
        if (!PtrOk(p1)) p1 = players[0];
        void* mc = players[1];
        if (!PtrOk(mc) || mc == p1) return nullptr;
        if (!(At<uint8_t>(mc, 0x320) & 1) || !At<uint8_t>(mc, 0x3E0) || !At<uint8_t>(mc, 0x3E1)) return nullptr;
        void* blk = At<void*>(mc, eng::MC_VEHBLK);
        if (!PtrOk(blk) || At<uint32_t>(blk, 4) == 0) return nullptr;
        void* h = At<void*>(mc, eng::MC_VEHREF);
        if (!PtrOk(h)) return nullptr;
        void* veh = *reinterpret_cast<void**>(h);
        if (!PtrOk(veh)) return nullptr;
        uint8_t* ud = VehicleUserData(veh);
        uint8_t* drv = UserDataDriver(ud);
        if (drv && drv != mc && At<uint8_t>(drv, 0x3E1) == 0) return nullptr;
        if (drv != mc && At<int32_t>(veh, eng::VEH_CLASS) != 3 && KrawlieDriverRide(ud)) return nullptr;   // not boats: see PlayerVehicle
        return veh;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
    return nullptr;
}

// Player 2's character: g_players[1] when it is a live, active player other than player 1, else null.
static uint8_t* Player2Char()
{
    __try
    {
        uint8_t** players = reinterpret_cast<uint8_t**>(g_base + eng::RVA_PLAYERS);
        uint8_t* p1 = *reinterpret_cast<uint8_t**>(g_base + eng::RVA_PLAYER1);
        if (!PtrOk(p1)) p1 = players[0];
        uint8_t* mc = players[1];
        if (!PtrOk(mc) || mc == p1) return nullptr;
        if (!(At<uint8_t>(mc, 0x320) & 1) || !At<uint8_t>(mc, 0x3E0) || !At<uint8_t>(mc, 0x3E1)) return nullptr;
        return mc;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
    return nullptr;
}

// ---------------------------------------------------------------------------------------------
//  The player context (2026-11-11: everything is the same for player 2 as for player 1). Every module
//  was written around ONE player: one car model state, one helicopter, one bike, one parachute,
//  one controller. Rather than index all of it, player 2 has a parked copy of each module's
//  per-player state, and SwapCtx() exchanges the live and parked copies. Code that runs for
//  player 2 (their vehicle's Update, a hook called with their character) does so inside AsP2,
//  which swaps in on entry and back out on exit. Outside AsP2 everything is player 1's, exactly
//  as before. Game thread only.
// ---------------------------------------------------------------------------------------------
static int g_ctx = 0;                                   // 0 = player 1's state is live, 1 = player 2's
static uint8_t* CtxChar()    { return g_ctx ? Player2Char() : *reinterpret_cast<uint8_t**>(g_base + eng::RVA_PLAYER1); }
static void*    CtxVehicle() { return g_ctx ? Player2Vehicle() : PlayerVehicle(); }
static void SwapCtx();                                  // after the module headers, below
struct AsP2
{
    bool on;
    explicit AsP2(bool p2) : on(p2 && g_ctx == 0) { if (on) SwapCtx(); }
    ~AsP2() { if (on) SwapCtx(); }
};

static bool ReadModelName(void* def, char* out, int cap)
{
    out[0] = 0;
    __try
    {
        void* a = At<void*>(def, eng::NAME_A);            if (!PtrOk(a)) return false;
        void* b = At<void*>(a, eng::NAME_B);              if (!PtrOk(b)) return false;
        const char* s = At<const char*>(b, eng::NAME_C);  if (!PtrOk(s)) return false;
        int i = 0;
        for (; i < cap - 1 && s[i]; ++i)
        {
            if (s[i] < 0x20 || s[i] > 0x7E) { out[0] = 0; return false; }
            out[i] = s[i];
        }
        out[i] = 0;
        return i > 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { out[0] = 0; return false; }
}

#include "pad.h"
#include "config.h"
#include "model.h"
#include "lights.h"
#include "autotest.h"
#include "hooks.h"
#include "chute.h"
#include "heli.h"
#include "plane.h"
#include "airplane.h"                   // the aeroplanes on their own wheels (car class), after plane.h
#include "bike.h"
#include "jetpack.h"
#include "wanted.h"
#pragma push_macro("Beep")
#undef Beep                             // airport.h has a method of that name
#include "airport.h"                    // after wanted.h (uses wd:: helpers) and plane.h
#pragma pop_macro("Beep")
#include "dino.h"                       // the rideable T-Rex at the airport (after airport.h: its player helpers)
#include "ptero.h"                      // the Pteranodon flies (after dino.h, chute.h; uses AsP2, wd::, pad::)
#include "sites.h"                      // vehicles at stands anywhere + Port Atlas as a place (after airport.h, heli.h)
#include "marina.h"                     // [marina1] berths, anchored boats, boat stations (rides on sites.h; Notes 56)
#include "boats.h"                      // [boats2] the traffic boats: spawn spacing + file breaker (after marina.h; Notes 56 section 8)
#include "radio.h"
#include "voice.h"                      // the cut voice lines (before life.h: its people call PedSay)
#include "life.h"
#include "spots.h"                      // life at the new places: people, driving cars (after life.h, dino.h, sites.h)
#include "streetlamps.h"

// Exchange every module's per-player state with player 2's parked copy (see AsP2 above).
static void SwapCtx()
{
    mdl::SwapP2(); hl::SwapP2(); pl::SwapP2(); fw::SwapP2(); bk::SwapP2(); chute::SwapP2();
    g_ctx ^= 1;
    pad::g_player = g_ctx;
}

// ---------------------------------------------------------------------------------------------
//  State shared between the game thread and the hotkey thread
// ---------------------------------------------------------------------------------------------
typedef void(__fastcall* UpdateFn)(void* self, float dt);

static UpdateFn       g_origCar    = nullptr;
static void**         g_slotCar    = nullptr;
static UpdateFn       g_origHover  = nullptr;
static void**         g_slotHover  = nullptr;
static volatile bool  g_heliFault  = false;
static UpdateFn       g_origBike   = nullptr;
static void**         g_slotBike   = nullptr;
static volatile bool  g_bikeFault  = false;
typedef void(__fastcall* PMUpdateFn)(void* pm, void* info);
static PMUpdateFn     g_origPM     = nullptr;
static void**         g_slotPM     = nullptr;
static volatile bool  g_wantedFault = false;
static volatile LONG  g_inFlight   = 0;
static volatile bool  g_unhooked   = false;
#include "coopcut.h"                    // reads g_unhooked
static volatile bool  g_enabled    = true;    // `]` / `[`
static volatile bool  g_faulted    = false;   // latched on any exception in our code; `]` clears it
static volatile bool  g_restoreReq = false;   // ask the game thread to give the body fields back
static volatile bool  g_restoreReq2 = false;  // ...its twin for player 2's car
static bool           g_modelArmed = false;   // every call patch installed

static Config         g_cfgBuf[2];
static volatile LONG  g_cfgIdx = 0;           // the game thread reads g_cfgBuf[g_cfgIdx]
static Config         g_cfgRaw[2];          // [missions1] the ini as loaded (g_cfgBuf = this with the mission mask); the menu shows these
static uint8_t        g_maskApplied = 0;    // [missions1] the msm::Mask bits in g_cfgBuf[g_cfgIdx] (hotkey thread)

static void* volatile g_activeVeh = nullptr;  // the car whose Update is running under our model
static const Config*  g_activeCfg = nullptr;
static void* volatile g_planeVeh = nullptr;   // the aeroplane whose Update is running under airplane.h
static const Config*  g_planeCfg = nullptr;
static volatile bool  g_planeFault = false;   // latched: aeroplanes are stock cars for the session
#include "crashguard.h"                 // [crashguard] null / free-list guards at the engine sites that crashed 2026-11-19
#include "poolwatch.h"                  // [stationfix] the streaming pool checked each update + on the pause menu (repairs the station-crash state)
#include "pausemenu.h"                  // Mod Options in the pause menu (reads the state above)

static vsim::BridgeParams g_bridgeP;
static vsim::BridgeState  g_bridge;
#include "weather.h"                    // rain, fog, weather zones (reads g_bridge, the players; Notes 53)
#include "shark.h"                      // the rare shark: swim freely, a shark only after a long swim (Notes 61)
#include <intrin.h>                     // [backrooms1] __rdtsc (the maze's entropy)
#include "backrooms.h"                  // [backrooms1] the Backrooms behind the village door (Notes 54)
#include "gfx.h"                        // the graphics overhaul: grade, AO, draw distance, haze, camera feel (Notes 57)
#include "gfx2.h"                       // [gfx2] bloom, reflections, far blur / sun rays / moonlight floats, frame counter, Auto
#include "radiologo.h"                  // [radiologo1] the station logo overlay (IniParams 16 / 17; after radio.h, backrooms.h, g_bridge)
#include <new>                          // [round3] placement new (npc_glue.h)
#include "animals.h"                    // [round3] animals at the Wilds, the farm, beaches, quays (after spots.h; Notes 64)
#include "npc_glue.h"                   // [round3] the city's people speak (after life.h, voice.h, spots.h, ptero.h, weather.h; Notes 62)
#include "maplabels.h"                  // [labels1] map pins + location names for the new places (Notes 59 section 6)
#include "missions.h"                   // [missions1] story missions: completions (radio news) + "Missions: stock game" (Notes 68)

// [missions1] own fault switch: a fault = no mission news and nothing suspended for the session (the mask goes back to 0).
static int MissionsFault(EXCEPTION_POINTERS* ep)
{
    if (!mis::g_fault)
    {
        mis::g_fault = true;
        InterlockedExchange(&mis::g_want, 0); InterlockedExchange(&mis::g_active, 0); rd::g_newsHold = 0;
        Log("!!! EXCEPTION 0x%08lX in the missions code at %p - no mission news, nothing suspended in missions, for this session.\n",
            ep->ExceptionRecord->ExceptionCode, ep->ExceptionRecord->ExceptionAddress);
    }
    return EXCEPTION_EXECUTE_HANDLER;
}
static void RunMissions(const Config& cfg)
{
    __try { mis::Tick(cfg, !g_unhooked); }
    __except (MissionsFault(GetExceptionInformation())) {}
}

// [round3] the animals (animals.h): own fault switch - a fault = no animals for the session, what was out is let go once.
static volatile bool g_animalsFault = false;
static int AnimalsFault(EXCEPTION_POINTERS* ep)
{
    if (!g_animalsFault)
    {
        g_animalsFault = true;
        Log("!!! EXCEPTION 0x%08lX in the animals code at %p - no animals for this session ([animals] enable = 0 keeps them off).\n",
            ep->ExceptionRecord->ExceptionCode, ep->ExceptionRecord->ExceptionAddress);
    }
    return EXCEPTION_EXECUTE_HANDLER;
}
static void RunAnimals(void* pm, void* info, const Config& cfg)
{
    __try
    {
        float dt = PtrOk(info) ? At<float>(info, 0x18) : 0.0f;
        if (!IsFin(dt) || dt <= 0.0f || dt > 0.25f) dt = 1.0f / 60.0f;
        an::Tick(pm, dt, cfg, g_enabled && g_slotCar != nullptr && !br::Quiet());
    }
    __except (AnimalsFault(GetExceptionInformation())) {}
}
static void RunAnimalsRelease()
{
    __try { an::Release(g_cfgBuf[g_cfgIdx], "the animals code faulted"); }
    __except (EXCEPTION_EXECUTE_HANDLER) { an::g_released = true; }
}

// [round3] the NPC voices (npc_glue.h): after life, voice and spots; own fault switch (a fault = the people are quiet).
static int NpcFault(EXCEPTION_POINTERS* ep)
{
    if (!npcg::g_fault)
    {
        npcg::g_fault = true;
        Log("!!! EXCEPTION 0x%08lX in the NPC voice code at %p - the people are quiet for this session.\n",
            ep->ExceptionRecord->ExceptionCode, ep->ExceptionRecord->ExceptionAddress);
    }
    return EXCEPTION_EXECUTE_HANDLER;
}
static void RunNpcOff()
{
    __try { lf::g_onChat = nullptr; lf::g_tune = nullptr; npcg::ForgetAll(); npcg::xa::StopAll(); }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
}
static void RunNpc(void* pm, void* info, const Config& cfg)
{
    __try
    {
        uint8_t* p1 = wd::RawPlayer(0);
        if (!PtrOk(p1)) return;
        void* lvl = wd::g_getLevel(p1 + 0x10);
        if (!PtrOk(lvl) || wd::g_getPM(lvl) != pm) return;
        static void* s_lastPm = nullptr;
        if (pm != s_lastPm) { s_lastPm = pm; npcg::ForgetAll(); }      // a new world: its lists are gone
        float dt = PtrOk(info) ? At<float>(info, 0x18) : 0.0f;
        if (!IsFin(dt) || dt <= 0.0f || dt > 0.25f) dt = 1.0f / 60.0f;
        npcg::Tick(lvl, dt, cfg, g_enabled != 0, g_dir);
    }
    __except (NpcFault(GetExceptionInformation())) { RunNpcOff(); }
}

static CallPatch g_patches[] = {
    { "steering",   eng::SITE_STEER,      eng::FN_STEER,      nullptr, 0, false },
    { "spin/donut", eng::SITE_SPIN,       eng::FN_SPIN,       nullptr, 0, false },
    { "drag",       eng::SITE_DRAG,       eng::FN_DRAG,       nullptr, 0, false },
    { "wheel",      eng::SITE_CONSTRAINT, eng::FN_CONSTRAINT, nullptr, 0, false },
};
// the helicopter's "you can't get out here" (heli.h OffPadExit); optional like the hover hook
static CallPatch g_exitPatch = { "heli exit", eng::SITE_HELI_NOEXIT, eng::FN_HINT_FLAG, nullptr, 0, false };
// the astronaut's jetpack update (jetpack.h); optional too
static CallPatch g_jetPatch = { "jetpack", jp::SITE, jp::FN_UPDATE, nullptr, 0, false };
// every character's swept move in its movement integrator (ptero.h: the Pteranodon flies); optional
static CallPatch g_pteroPatch = { "ptero sweep", pt::SITE_SWEEP, pt::FN_SWEEP, nullptr, 0, false };
// the per-view post-effect settings evaluation (weather.h: the engine's fog and rain); optional
static CallPatch g_weatherPatch = { "weather", wx::SITE_EVAL, wx::FN_EVAL, nullptr, 0, false };
// the render camera copy in RenderView::Begin (gfx.h: the camera feel, added to the copy only); optional
static CallPatch g_gfxCamPatch = { "gfx camera", gfx::SITE_CAMCOPY, gfx::FN_CAMCOPY, nullptr, 0, false };
// wanted mode's crime signals (wanted.h): the player-vehicle event calls (each site loads its
// event's address into rcx, then calls Event::Fire) and the crowd-punch query; optional
static CallPatch g_crimePatches[] = {
    { "crime ped",     0x0ACC7A, wd::FN_EVENT_FIRE,  nullptr, 0, false },
    { "crime traffic", 0x1951A7, wd::FN_EVENT_FIRE,  nullptr, 0, false },
    { "crime traffic", 0x195252, wd::FN_EVENT_FIRE,  nullptr, 0, false },
    { "crime ai car",  0x1951E5, wd::FN_EVENT_FIRE,  nullptr, 0, false },
    { "crime ai car",  0x19528D, wd::FN_EVENT_FIRE,  nullptr, 0, false },
    { "crime wreck",   0x191F17, wd::FN_EVENT_FIRE,  nullptr, 0, false },
    { "crime wreck",   0x19C8F6, wd::FN_EVENT_FIRE,  nullptr, 0, false },
    { "crime wreck",   0x19D2C6, wd::FN_EVENT_FIRE,  nullptr, 0, false },
    { "crime wreck",   0x1A556F, wd::FN_EVENT_FIRE,  nullptr, 0, false },
    { "crime jack",    0x69D1BD, wd::FN_EVENT_FIRE,  nullptr, 0, false },
    { "crime punch",   0x0314CA, wd::FN_KRAWLIE_HIT, nullptr, 0, false },
};
// the ray gun (wanted.h "The ray gun"): its bolt's two krawlie hit queries and its hit on a street prop
static CallPatch g_shotPatches[] = {
    { "crime shot",      wd::SITE_SHOT_KRAWLIE_A, wd::FN_KRAWLIE_HIT,  nullptr, 0, false },
    { "crime shot",      wd::SITE_SHOT_KRAWLIE_B, wd::FN_KRAWLIE_HIT,  nullptr, 0, false },
    { "crime shot prop", wd::SITE_SHOT_PROP,      wd::FN_PROP_BOLTHIT, nullptr, 0, false },
};
// [taxi2 2026-11-25] The .sf script compiler's error sink 0x140660430(const char* message): every compile error
// ("Unknown function/state/action %s", "Unknown state %s", the parser's syntax errors - formatted by 0x14065D850)
// passes through it, at these 16 call sites [P: every E8 to it in .text]. The retail game shows them nowhere; a
// script with one error simply never runs (how a new mod script fails silently). Logged here, then passed on.
static const uintptr_t FN_SCRIPT_ERR = 0x660430;
static CallPatch g_scriptErrPatches[] = {
    { "script err", 0x65C773, FN_SCRIPT_ERR, nullptr, 0, false }, { "script err", 0x65C7E3, FN_SCRIPT_ERR, nullptr, 0, false },
    { "script err", 0x65D892, FN_SCRIPT_ERR, nullptr, 0, false }, { "script err", 0x65DAC2, FN_SCRIPT_ERR, nullptr, 0, false },
    { "script err", 0x65DB50, FN_SCRIPT_ERR, nullptr, 0, false }, { "script err", 0x65DCDE, FN_SCRIPT_ERR, nullptr, 0, false },
    { "script err", 0x65E07A, FN_SCRIPT_ERR, nullptr, 0, false }, { "script err", 0x65E156, FN_SCRIPT_ERR, nullptr, 0, false },
    { "script err", 0x65E1F6, FN_SCRIPT_ERR, nullptr, 0, false }, { "script err", 0x65E2B8, FN_SCRIPT_ERR, nullptr, 0, false },
    { "script err", 0x65E382, FN_SCRIPT_ERR, nullptr, 0, false }, { "script err", 0x65E8CC, FN_SCRIPT_ERR, nullptr, 0, false },
    { "script err", 0x65EC6E, FN_SCRIPT_ERR, nullptr, 0, false }, { "script err", 0x65F05A, FN_SCRIPT_ERR, nullptr, 0, false },
    { "script err", 0x665008, FN_SCRIPT_ERR, nullptr, 0, false }, { "script err", 0x6653A1, FN_SCRIPT_ERR, nullptr, 0, false },
};
typedef uint64_t(__fastcall* ScriptErrFn)(const char* msg, uint64_t b, uint64_t c, uint64_t d);
static uint64_t __fastcall Hook_ScriptErr(const char* msg, uint64_t b, uint64_t c, uint64_t d)
{
    static volatile LONG s_n = 0;
    __try
    {
        const LONG n = InterlockedIncrement(&s_n);
        if (n <= 300 && PtrOk(msg)) Log("!!! script: %.400s\n", msg);
        else if (n == 301) Log("!!! script: (more errors - not logged)\n");
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
    return reinterpret_cast<ScriptErrFn>(g_base + FN_SCRIPT_ERR)(msg, b, c, d);
}
// [taxi3 2026-11-26] Runtime lookups: ScriptHelper::GetNamed* (locator, level, character, gizmo, area, spline ...)
// report a missing name through 0x14010F490(int found, ctx, const char* fmt, const char* name), an empty stub in
// retail [P: 23 E8 sites, fmt in r8, the name in r9, ecx = (result != null)]. A script that names a locator or level
// that does not exist just gets an empty handle - logged here when found == 0.
static const uintptr_t FN_NAMED_ERR = 0x10F490;
static CallPatch g_namedErrPatches[] = {
    { "named err", 0xA60577, FN_NAMED_ERR, nullptr, 0, false }, { "named err", 0xA606B0, FN_NAMED_ERR, nullptr, 0, false },
    { "named err", 0xA608DE, FN_NAMED_ERR, nullptr, 0, false }, { "named err", 0xA60A2C, FN_NAMED_ERR, nullptr, 0, false },
    { "named err", 0xA60B6A, FN_NAMED_ERR, nullptr, 0, false }, { "named err", 0xA60C46, FN_NAMED_ERR, nullptr, 0, false },
    { "named err", 0xA60D89, FN_NAMED_ERR, nullptr, 0, false }, { "named err", 0xA61036, FN_NAMED_ERR, nullptr, 0, false },
    { "named err", 0xA61105, FN_NAMED_ERR, nullptr, 0, false }, { "named err", 0xA611F5, FN_NAMED_ERR, nullptr, 0, false },
    { "named err", 0xA612E0, FN_NAMED_ERR, nullptr, 0, false }, { "named err", 0xA613C2, FN_NAMED_ERR, nullptr, 0, false },
    { "named err", 0xA614DB, FN_NAMED_ERR, nullptr, 0, false }, { "named err", 0xA6164E, FN_NAMED_ERR, nullptr, 0, false },
    { "named err", 0xA6171C, FN_NAMED_ERR, nullptr, 0, false }, { "named err", 0xA61862, FN_NAMED_ERR, nullptr, 0, false },
    { "named err", 0xA61932, FN_NAMED_ERR, nullptr, 0, false }, { "named err", 0xA61A5B, FN_NAMED_ERR, nullptr, 0, false },
    { "named err", 0xA61BD2, FN_NAMED_ERR, nullptr, 0, false }, { "named err", 0xA61FE2, FN_NAMED_ERR, nullptr, 0, false },
    { "named err", 0xA620D5, FN_NAMED_ERR, nullptr, 0, false }, { "named err", 0xA62285, FN_NAMED_ERR, nullptr, 0, false },
    { "named err", 0xA62562, FN_NAMED_ERR, nullptr, 0, false },
};
static void __fastcall Hook_NamedErr(int found, uint64_t ctx, const char* fmt, const char* name)
{
    (void)ctx;
    if (found) return;
    static volatile LONG s_n = 0;
    __try
    {
        const LONG n = InterlockedIncrement(&s_n);
        if (n > 300) { if (n == 301) Log("!!! script lookup: (more - not logged)\n"); return; }
        int pct = 0;
        if (PtrOk(fmt)) for (const char* p = fmt; *p && p - fmt < 300; ++p) if (*p == '%') ++pct;
        char buf[512];
        if (PtrOk(fmt) && pct == 1 && PtrOk(name)) _snprintf_s(buf, sizeof buf, _TRUNCATE, fmt, name);
        else _snprintf_s(buf, sizeof buf, _TRUNCATE, "%.300s [%.120s]", PtrOk(fmt) ? fmt : "?", PtrOk(name) ? name : "?");
        Log("!!! script lookup: %s\n", buf);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
}
// [taxi3] PrintToScreen(text) in a level script: retail draws it only over the script's own character, and a level
// script has none, so it vanishes. The command object's vtable (0x140C13F20, name "PrintToScreen" at slot 1) slot 4
// = execute 0x140109380(this, ctx). The argument is fetched as the handler does (0x1401093D1): arg slot
// [ctx+0x50][byte ctx+0x59 - 1] -> index +0x1C -> 0x140653B50(ctx, index) -> text at +0x28. Logged as "script:".
static const uintptr_t VT_PRINT_SLOT = 0xC13F40, FN_PRINT_EXEC = 0x109380, FN_SCRIPT_ARG = 0x653B50;
typedef uint64_t(__fastcall* PrintExecFn)(void* self, uint8_t* ctx);
typedef uint8_t*(__fastcall* ScriptArgFn)(uint8_t* ctx, uint32_t idx);
static uint64_t __fastcall Hook_PrintExec(void* self, uint8_t* ctx)
{
    static volatile LONG s_n = 0;
    __try
    {
        const uint8_t na = *(ctx + 0x59);
        uint8_t** arr = *reinterpret_cast<uint8_t***>(ctx + 0x50);
        if (na >= 1 && PtrOk(arr) && PtrOk(arr[na - 1]))
        {
            const uint32_t idx = *reinterpret_cast<uint32_t*>(arr[na - 1] + 0x1C);
            uint8_t* v = reinterpret_cast<ScriptArgFn>(g_base + FN_SCRIPT_ARG)(ctx, idx);
            const char* s = PtrOk(v) ? *reinterpret_cast<const char**>(v + 0x28) : nullptr;
            const LONG n = InterlockedIncrement(&s_n);
            if (n <= 2000 && PtrOk(s)) Log(">>> script: %.300s\n", s);
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
    return reinterpret_cast<PrintExecFn>(g_base + FN_PRINT_EXEC)(self, ctx);
}
static bool HookScriptPrint()
{
    void** slot = reinterpret_cast<void**>(g_base + VT_PRINT_SLOT);
    __try { if (reinterpret_cast<uintptr_t>(*slot) != g_base + FN_PRINT_EXEC) return false; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    DWORD old = 0;
    if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &old)) return false;
    *slot = reinterpret_cast<void*>(&Hook_PrintExec);
    VirtualProtect(slot, sizeof(void*), old, &old);
    return true;
}
typedef uint64_t(__fastcall* OnDamageFn)(uint64_t gm, uint64_t att, uint64_t vic, uint64_t bolt, uint64_t amount,
                                         uint64_t flags, uint64_t a5, uint64_t a6);
static OnDamageFn g_origOnDamage = nullptr;
static void**     g_slotOnDamage = nullptr;
// ...and two more vtable slots, hooked like OnCharacterDamage: only when the slot holds the game's own function
typedef uint64_t(__fastcall* BlowupBoltFn)(uint64_t sys, uint64_t level, uint64_t blowup, uint64_t pos, uint64_t player,
                                           uint64_t kind, uint64_t bolt);
static BlowupBoltFn g_origBlowupBolt = nullptr;
static void**       g_slotBlowupBolt = nullptr;
typedef uint64_t(__fastcall* Pass4Fn)(void* a, void* b, void* c, void* d);
static Pass4Fn      g_origBoltVehicle = nullptr;
static void**       g_slotBoltVehicle = nullptr;
static volatile bool g_jetFault = false;

// ---------------------------------------------------------------------------------------------
//  Fault containment. Anything that faults in our code latches lcudrive off for the session; the
//  car is instantly stock again because the engine reconfigures every joint every frame.
// ---------------------------------------------------------------------------------------------
static int FaultFilter(EXCEPTION_POINTERS* ep, const char* phase)
{
    if (!g_faulted)
    {
        g_faulted = true;
        g_restoreReq = g_restoreReq2 = true;
        const uintptr_t at = reinterpret_cast<uintptr_t>(ep->ExceptionRecord->ExceptionAddress);
        HMODULE mod = nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(at), &mod);
        Log("!!! EXCEPTION 0x%08lX in %s at %p (module %p, rva 0x%llX). lcudrive is OFF for this "
            "session; the car is stock. Press ] to try again.\n",
            ep->ExceptionRecord->ExceptionCode, phase, (void*)at, (void*)mod,
            (unsigned long long)(at - reinterpret_cast<uintptr_t>(mod)));
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

// The aeroplanes (airplane.h) have their own fault switch: a fault there makes them stock cars for the
// session (their definition given back in their next Update), and leaves the cars and helicopters alone.
static int PlaneFault(EXCEPTION_POINTERS* ep)
{
    if (!g_planeFault)
    {
        g_planeFault = true;
        Log("!!! EXCEPTION 0x%08lX in the airplane code at %p - airliners are stock cars for this session.\n",
            ep->ExceptionRecord->ExceptionCode, ep->ExceptionRecord->ExceptionAddress);
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

static bool RunPlaneSteer(void* veh)
{
    __try { return fw::Steer(veh); }
    __except (PlaneFault(GetExceptionInformation())) { return false; }
}

static bool RunPlaneWheel(void* wheel)
{
    __try { return fw::Wheel(wheel, *g_planeCfg); }
    __except (PlaneFault(GetExceptionInformation())) { return false; }
}

// ---------------------------------------------------------------------------------------------
//  The redirected engine calls. Each one: our car -> our model (inside SEH, falling back to the
//  engine's own routine if anything faults); any other vehicle -> the engine's routine.
// ---------------------------------------------------------------------------------------------
static bool RunSteer(void* veh)
{
    __try
    {
        const Config& cfg = *g_activeCfg;
        const float deg = mdl::FrameUpdate(veh, cfg);
        if (!mdl::g_snapOk) return false;
        const mdl::CarSnap& s = mdl::g_snap;
        if (!s.srcActive) return true;                            // the engine freezes the angle too
        if (s.directSteer || s.pathFollow) return false;          // scripted drive: stock steering
        At<float>(veh, eng::VEH_STEER_DEG) = deg;
        At<int32_t>(veh, eng::VEH_STEER_BAM) = static_cast<int32_t>(deg * (65536.0f / 360.0f));
        for (int i = 0; i < s.nw; ++i) mdl::g_fn.wheelSetSteer(s.wh[i].w, deg);
        return true;
    }
    __except (FaultFilter(GetExceptionInformation(), "steering/frame")) { return false; }
}

static void __fastcall Hook_Steer(void* veh)
{
    if (veh == g_planeVeh && !g_planeFault && RunPlaneSteer(veh)) return;   // the nose wheel + ground force (airplane.h)
    if (veh == g_activeVeh && !g_faulted && RunSteer(veh)) return;
    mdl::g_fn.steerOrig(veh);
}

static void __fastcall Hook_Spin(void* veh)
{
    if (veh == g_planeVeh && !g_planeFault) return; // an aeroplane: no donut yaw either
    if (veh == g_activeVeh && !g_faulted) return;   // no scripted donut yaw / grip rewrite on our car
    mdl::g_fn.spinOrig(veh);
}

static void __fastcall Hook_Drag(void* veh)
{
    if (veh == g_planeVeh && !g_planeFault) return; // an aeroplane: plane_model.h's drag replaces it
    if (veh == g_activeVeh && !g_faulted) return;   // our aero drag replaces the stock linear drag
    if (veh && veh == bk::g_drive.veh) return;      // the player's motorbike: bike.h Aero, likewise
    if (veh && wd::g_nCop && !g_wantedFault && !g_unhooked && wd::FindCop(veh)) return;   // a chasing cop: wanted.h CopPost
    mdl::g_fn.dragOrig(veh);
}

static bool RunWheel(void* wheel)
{
    __try { mdl::WheelConstraint(wheel, *g_activeCfg); return true; }
    __except (FaultFilter(GetExceptionInformation(), "wheel joint")) { return false; }
}

static int BikeFault(EXCEPTION_POINTERS* ep);     // the motorbike section, below
static int WantedFault(EXCEPTION_POINTERS* ep);   // the wanted-mode section, below

static void RunBikeWheel(void* wheel)
{
    __try { bk::DriveRear(wheel); }
    __except (BikeFault(GetExceptionInformation())) { bk::g_drive.on = false; }
}

static void RunCopWheel(void* wheel, void* veh)
{
    __try { if (const wd::Cop* c = wd::FindCop(veh)) wd::CopWheel(wheel, veh, *c, g_cfgBuf[g_cfgIdx]); }
    __except (WantedFault(GetExceptionInformation())) {}
}

static void __fastcall Hook_Constraint(void* wheel)
{
    void* veh = nullptr;
    __try { veh = At<void*>(wheel, eng::WHL_OWNER); } __except (EXCEPTION_EXECUTE_HANDLER) {}
    if (veh && veh == g_planeVeh && !g_planeFault && RunPlaneWheel(wheel)) return;   // free-rolling + brakes (airplane.h)
    if (veh == g_activeVeh && !g_faulted && mdl::g_snapOk && RunWheel(wheel)) return;
    mdl::g_fn.constraintOrig(wheel);
    // the player's motorbike: the game set the rear tyre up; its drive becomes ours (bike.h)
    if (veh && veh == bk::g_drive.veh && bk::g_drive.on && wheel == bk::g_drive.rear && !g_bikeFault)
        RunBikeWheel(wheel);
    // a police vehicle chasing you: its engine and grip (wanted.h)
    else if (veh && wd::g_nCop && !g_wantedFault && !g_unhooked)
        RunCopWheel(wheel, veh);
}

// ---------------------------------------------------------------------------------------------
//  The vtable hook: ApiVehicleCar::Update (slot 91). Decides, per call, whether this car runs
//  under our model, and publishes that decision to the redirected calls above.
// ---------------------------------------------------------------------------------------------
static void* g_lastPlayerVeh = nullptr;
static void* g_lastPlayer2Veh = nullptr;

static void RunObserve(void* veh, const Config& cfg, bool active)
{
    __try { ModelObserve(veh, cfg, active); }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
}

static uint32_t g_lastPublish = 0;
static bool     g_idlePublished = true;

static void PublishBridge(void* veh, const Config& cfg)
{
    __try
    {
        vsim::BridgeSlots bs;
        if (ModelBridgeSlots(veh, cfg, bs))
        {
            if (cfg.headlights) bs.lampIntensity = 0.0f;   // the engine's own lamps do it now: no shader cones
            vsim::BridgePublish(g_bridgeP, g_bridge, bs);
            g_lastPublish = GetTickCount();
            g_idlePublished = false;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
}

// Once per frame from whichever vehicle updates first. Steps the bridge (that is what re-reads the
// shader clock, so it must run while the player is on foot too), lets the speed blur fall back to
// zero once nobody is driving, and makes the night decision for the headlights.
static uint32_t g_lastStep = 0;

static void FrameStep(const Config& cfg, uint32_t now)
{
    if (now - g_lastStep < 12) return;
    g_lastStep = now;
    pad::g_cfgPort[0] = cfg.coop_pad_p1; pad::g_cfgPort[1] = cfg.coop_pad_p2;
    pad::g_p2Here = Player2Char() != nullptr;
    __try
    {
        vsim::BridgeStep(g_bridgeP, g_bridge, now);
        if (cfg.bridge && !g_idlePublished && now - g_lastPublish > 250)
        {
            vsim::BridgeSlots idle;
            idle.lampIntensity = 0.0f;
            vsim::BridgePublish(g_bridgeP, g_bridge, idle);
            g_idlePublished = true;
        }
        lights::UpdateNight(vsim::BridgeLockedQ(g_bridge), vsim::BridgeClock(g_bridge), cfg, now);
        // the wanted brick meters (slot 5 player 1's, slot 6 player 2's; ShaderFixes\wanted_hud.hlsl draws them)
        if (cfg.bridge)
        {
            const wd::Per& a = wd::g_pl[0];
            const wd::Per& b = wd::g_pl[1];
            vsim::BridgePublishWanted(g_bridgeP, g_bridge, a.hudLevel, a.hudHeat, a.hudFlash, a.hudAlpha);
            vsim::BridgePublishWanted2(g_bridgeP, g_bridge, wd::g_p2Here != 0, b.hudLevel, b.hudHeat, b.hudFlash, b.hudAlpha);
            // [busted1] the busted fade (7.z player 1's view, 7.w player 2's; ShaderFixes/busted_fade.hlsl): only
            // while there is one, and once more after it to leave a clean 0
            static bool s_boWas = false;
            const float bo1 = wd::BoFade(0, now), bo2 = wd::BoFade(1, now);
            if (bo1 > 0.0f || bo2 > 0.0f || s_boWas)
                vsim::BridgePublishBustFade(g_bridgeP, g_bridge, bo1, bo2, busted::kRampS, busted::kOpaqueS);
            s_boWas = bo1 > 0.0f || bo2 > 0.0f;
        }
        // the radio's game hour, from the same clock: the sun rises at clock 0 (06:00), one day per 480 s
        if (vsim::BridgeLockedQ(g_bridge))
        {
            float hr = std::fmod(6.0f + vsim::BridgeClock(g_bridge) / 20.0f, 24.0f);
            if (hr < 0.0f) hr += 24.0f;
            rd::g_hour100 = static_cast<LONG>(hr * 100.0f);
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
}

static bool g_lightsFault = false;

static void RunLights(void* veh, bool mine, const Config& cfg, uint32_t now)
{
    if (g_lightsFault) return;
    __try { lights::Tick(veh, mine, cfg, now); }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        g_lightsFault = true;
        Log("!!! EXCEPTION in the headlight code - headlights stay as they are for this session; "
            "driving is unaffected.\n");
    }
}

static void ChuteFrame(const Config& cfg);   // the helicopter section, below
static void RadioVehicleFrame(void* veh, const Config& cfg);  // the radio section, below

// A police vehicle chasing you (wanted.h): looked up before its Update, finished after it.
static bool CopBefore(void* veh, wd::Cop& out)
{
    if (g_unhooked || g_wantedFault || !wd::g_nCop) return false;
    const wd::Cop* c = wd::FindCop(veh);
    if (!c) return false;
    out = *c;
    return true;
}

static void RunCopPost(void* veh, const wd::Cop& cop, float skidLat)
{
    __try { wd::CopPost(veh, cop, skidLat); }
    __except (WantedFault(GetExceptionInformation())) {}
}

// After our car's Update: the skid byte from our tyres, not the stock spin (model.h WriteSkid,
// Notes\36); and the SpinDonut bit (only the skipped SpinDonut writes it) can never stick.
static void RunWriteSkid(void* veh)
{
    __try
    {
        if (mdl::g_snapOk) mdl::WriteSkid();
        At<uint8_t>(veh, eng::VEH_FLAGS8) &= static_cast<uint8_t>(~0x40);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
}

// ---------------------------------------------------------------------------------------------
//  The aeroplanes on their own wheels (airplane.h): a car-class AIRCRAFT_* in [airplane] models takes
//  this branch for every Update - the player's, the empty one, the parked one, after a fault, with
//  `[` - so the car model above NEVER sees it (no ModelOnNewCar, no g_activeVeh, no observe / bridge /
//  skid writes): DeriveParams would make an 8 u/s car of it [P: model.h DeriveParams]. With [airplane]
//  enable = 0 it is an ordinary car again (its definition given back first).
// ---------------------------------------------------------------------------------------------
static int RunPlaneKind(void* veh, const Config& cfg)
{
    __try { return fw::KindOf(veh, cfg); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

// Its definition and boundary bit back: `[airplane] enable = 0`, a fault, off the [airplane] list.
static void RunPlaneDefOff(void* veh)
{
    __try { fw::Release(veh); }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
}

// A car that is not (or no longer: an ini reload took it off [airplane] models) an aeroplane, while a
// definition is held: if it is that definition's vehicle, give it back now, from its own Update.
static void RunPlaneDefCheck(void* veh)
{
    __try { if (fw::HoldsDefOf(veh)) fw::Release(veh); }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
}

// Before the Update: 1 = the player's plane runs under airplane.h this frame, 2 = the empty plane
// glides, 0 = stock. The definition is only patched while lcudrive can fly it (the call patches are in:
// observe-only stays stock).
static int RunPlanePre(void* veh, bool player, int playerNo, bool on, const Config& cfg, float lv0[3], float av0[3])
{
    __try
    {
        fw::DefTick(veh, cfg, on && g_modelArmed);
        int run = 0;
        if (on)
            run = player ? (fw::Pre(veh, playerNo, cfg, g_modelArmed, lv0, av0) ? 1 : 0)
                         : (fw::UnoccupiedPre(veh, cfg, lv0, av0) ? 2 : 0);
        if (!run) fw::Mask(veh, false);                                 // stock this frame: its boundary bit back
        return run;
    }
    __except (PlaneFault(GetExceptionInformation())) { return 0; }
}

static void RunPlanePost(void* veh, int run, int playerNo, const float lv0[3], const float av0[3], const Config& cfg)
{
    __try
    {
        const bool tele = mdl::g_telemetry || cfg.telemetry;
        if (run == 1) fw::Post(veh, playerNo, lv0, av0, cfg, tele);
        else fw::UnoccupiedPost(veh, lv0, av0, cfg, tele);
    }
    __except (PlaneFault(GetExceptionInformation())) {}
}

// What the wheel joints show (airplane.h WheelVisual: the gear going up, the propellers turning) has
// its own fault switch: a fault there turns only that off - the engine's poses are given back in each
// plane's next Update, and if even that faults the records are dropped (the engine rewrites the poses
// itself at full simulation). The flying is untouched.
static volatile bool g_wheelVisFault = false;

static void RunWheelVisual(void* veh, const Config& cfg, bool release, bool flying, bool occupied)
{
    fw::VisIn in{};
    in.release = release || g_wheelVisFault;
    in.flying = flying; in.occupied = occupied;
    if (!g_wheelVisFault)
    {
        __try { fw::SkidWrite(veh, cfg, flying && !release); }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
    if (in.release && !fw::AnyWheelVis()) return;
    __try { fw::WheelVisual(veh, cfg, in); }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        if (!g_wheelVisFault)
        {
            g_wheelVisFault = true;
            Log("!!! EXCEPTION 0x%08lX in the airplane wheel visuals - off for this session (the gear is drawn down, the "
                "propellers stand at the wheels); the flying is not affected.\n", GetExceptionCode());
        }
        fw::WheelVisForget();
    }
}

static bool PlaneCarThunk(void* veh, float dt, UpdateFn orig)
{
    if (g_unhooked) return false;
    const Config& cfg = g_cfgBuf[g_cfgIdx];
    if (!RunPlaneKind(veh, cfg))
    {
        if (fw::AnyDefHeld()) RunPlaneDefCheck(veh);                        // taken off the list: its definition back
        if (fw::AnyWheelVis()) RunWheelVisual(veh, cfg, true, false, false);   // ... and its wheel poses
        return false;
    }
    if (!cfg.airplane_enable)                                           // off: an ordinary car (the car model's)
    {
        RunPlaneDefOff(veh);
        RunWheelVisual(veh, cfg, true, false, false);
        return false;
    }
    const uint32_t now = GetTickCount();
    const bool mine  = PlayerVehicle() == veh;
    const bool mine2 = !mine && Player2Vehicle() == veh;
    const bool swapped = mine2 || (!mine && fw::P2Owns(veh));          // player 2's plane, or the one they just left
    if (swapped) SwapCtx();
    if (mine || mine2)
    {
        rd::g_vehSeen = static_cast<LONG>(now); rd::g_vehKind = 2;    // the radio: an aircraft
        RadioVehicleFrame(veh, cfg);
        if (mine) ++g_frame;
    }
    ModelRestoreRetired(veh, now);
    FrameStep(cfg, now);
    RunLights(veh, mine || mine2, cfg, now);
    if (!g_heliFault) ChuteFrame(cfg);
    float lv0[3] = {}, av0[3] = {};
    const bool on = g_enabled && !fw::g_releaseReq;
    int run = 0;
    if (g_planeFault) RunPlaneDefOff(veh);                              // a fault: stock, definition given back
    else run = RunPlanePre(veh, mine || mine2, mine2 ? 2 : 1, on, cfg, lv0, av0);
    if (run) { g_planeCfg = &cfg; g_planeVeh = veh; }                   // the four redirected calls act on it

    orig(veh, dt);

    g_planeVeh = nullptr;
    if (run && !g_planeFault) RunPlanePost(veh, run, mine2 ? 2 : 1, lv0, av0, cfg);
    // the wheel joints' poses for the post-physics pass that draws them (the owner's context is still in)
    RunWheelVisual(veh, cfg, g_planeFault || fw::g_releaseReq, run == 1 && !g_planeFault, mine || mine2);
    if (swapped) SwapCtx();
    return true;
}

// ---------------------------------------------------------------------------------------------
//  Life at the new places (spots.h, Notes\51): people and driving cars from the life-spot data. Its
//  world tick runs from the population manager's update (after the stands); a route's car gets its
//  pedals and wheel in its own Update (CarThunk). Its own fault switch: a fault = nobody and nothing
//  new for the session, what was out is let go once.
// ---------------------------------------------------------------------------------------------
static volatile bool g_spotsFault = false;
static int SpotsFault(EXCEPTION_POINTERS* ep)
{
    if (!g_spotsFault)
    {
        g_spotsFault = true;
        Log("!!! EXCEPTION 0x%08lX in the life-spot code at %p - no people or driving cars at the new places for this "
            "session ([spots] enable = 0 keeps them off).\n", ep->ExceptionRecord->ExceptionCode, ep->ExceptionRecord->ExceptionAddress);
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

static void RunSpots(void* pm, void* info, const Config& cfg)
{
    __try
    {
        float dt = PtrOk(info) ? At<float>(info, 0x18) : 0.0f;
        if (!IsFin(dt) || dt <= 0.0f || dt > 0.25f) dt = 1.0f / 60.0f;
        sp::Tick(pm, dt, cfg, g_enabled && g_slotCar != nullptr && !br::Quiet());   // [backrooms1] && !Quiet
    }
    __except (SpotsFault(GetExceptionInformation())) {}
}

// [backrooms1] the Backrooms (backrooms.h, Notes 54): its own fault switch. A fault = the Backrooms off for the session,
// the kill height put back, its statues gone; nothing else changes. br::g_stopReq (Unhook) lets go on the game thread.
static volatile bool g_backroomsFault = false;
static int BackroomsFault(EXCEPTION_POINTERS* ep)
{
    if (!g_backroomsFault)
    {
        g_backroomsFault = true;
        Log("!!! EXCEPTION 0x%08lX in the Backrooms code at %p - the Backrooms are off for this session; everything else is unaffected.\n",
            ep->ExceptionRecord->ExceptionCode, ep->ExceptionRecord->ExceptionAddress);
    }
    return EXCEPTION_EXECUTE_HANDLER;
}
static void RunBackroomsRelease() { __try { br::Release(); } __except (EXCEPTION_EXECUTE_HANDLER) {} }
static void RunBackrooms(void* pm, void* info, const Config& cfg)
{
    __try
    {
        if (br::g_stopReq) { br::Release(); return; }
        float dt = PtrOk(info) ? At<float>(info, 0x18) : 0.0f;
        if (!IsFin(dt) || dt < 0.0f) dt = 0.0f;
        if (dt > 0.25f) dt = 0.25f;
        br::Tick(pm, dt, cfg, g_enabled && g_slotCar != nullptr);
    }
    __except (BackroomsFault(GetExceptionInformation())) { RunBackroomsRelease(); }
}

static void RunSpotsRelease()
{
    __try { sp::Release(g_cfgBuf[g_cfgIdx], "the life-spot code faulted"); }
    __except (EXCEPTION_EXECUTE_HANDLER) { sp::g_released = true; }
}

static void RunSpotsDrive(void* veh, float dt, const Config& cfg)
{
    __try { sp::DriveHook(veh, dt, cfg); }
    __except (SpotsFault(GetExceptionInformation())) {}
}

static void __fastcall CarThunk(void* veh, float dt)
{
    InterlockedIncrement(&g_inFlight);
    const UpdateFn orig = g_origCar;
    if (PlaneCarThunk(veh, dt, orig)) { InterlockedDecrement(&g_inFlight); return; }   // an aeroplane (airplane.h)

    bool mine = false, mine2 = false;
    if (!g_unhooked)
    {
        mine = (PlayerVehicle() == veh);
        if (mine)
        {
            rd::g_vehSeen = static_cast<LONG>(GetTickCount()); rd::g_vehKind = 0;   // the radio: in a car
            RadioVehicleFrame(veh, g_cfgBuf[g_cfgIdx]);
            ++g_frame;
            if (g_restoreReq) { g_restoreReq = false; ModelRestoreAll(); }
            if (veh != g_lastPlayerVeh) { g_lastPlayerVeh = veh; ModelOnNewCar(veh); }
        }
        else if (veh == ModelTunedCar())
        {
            // the player got out of the car we tuned: it is alive (it is updating), give it back
            ModelRestoreAll();
            if (veh == g_lastPlayerVeh) g_lastPlayerVeh = nullptr;
        }
        // Player 2's car: the same model on its own state, swapped in until this Update returns.
        if (!mine)
        {
            mine2 = Player2Vehicle() == veh;
            if (mine2)
            {
                SwapCtx();
                rd::g_vehSeen = static_cast<LONG>(GetTickCount()); rd::g_vehKind = 0;   // the radio: in a car
                RadioVehicleFrame(veh, g_cfgBuf[g_cfgIdx]);
                if (g_restoreReq2) { g_restoreReq2 = false; ModelRestoreAll(); }
                if (veh != g_lastPlayer2Veh) { g_lastPlayer2Veh = veh; Log(">>> player 2's car:\n"); ModelOnNewCar(veh); }
            }
            else if (veh == ModelTunedCar2())
            {
                mdl::SwapP2(); ModelRestoreAll(); mdl::SwapP2();         // player 2 got out: give it back
                if (veh == g_lastPlayer2Veh) g_lastPlayer2Veh = nullptr;
            }
        }
        ModelRestoreRetired(veh, GetTickCount());    // verified give-back, in that car's own Update
    }

    const Config& cfg = g_cfgBuf[g_cfgIdx];
    if (!g_unhooked)
    {
        const uint32_t now = GetTickCount();
        FrameStep(cfg, now);
        RunLights(veh, mine || mine2, cfg, now);
        if (!g_heliFault) ChuteFrame(cfg);
    }
    const bool active = (mine || mine2) && g_modelArmed && g_enabled && !g_faulted && cfg.enable;
    if (active) { g_activeCfg = &cfg; g_activeVeh = veh; }
    wd::Cop cop = {};
    const bool isCop = !mine && !mine2 && CopBefore(veh, cop);
    if (!mine && !mine2 && !isCop && !g_unhooked && !g_spotsFault) RunSpotsDrive(veh, dt, cfg);   // a route's car: this frame's input

    orig(veh, dt);

    if (active)
    {
        g_activeVeh = nullptr;
        RunWriteSkid(veh);
    }
    if (isCop) RunCopPost(veh, cop, 1.0f);      // the game's car skid threshold (Notes\36)
    if (mine)
    {
        RunObserve(veh, cfg, active);
        if (cfg.bridge) PublishBridge(veh, cfg);
    }
    if (mine2) SwapCtx();                       // player 1's state back in the modules' globals
    InterlockedDecrement(&g_inFlight);
}

// ---------------------------------------------------------------------------------------------
//  The helicopter: ApiVehicleHover::Update (slot 91). For the player's helicopter only, the
//  body's velocities are taken before the engine's update and the physical model replaces what
//  the engine wrote (heli.h). Everything else - AI helicopters, cutscenes, parked ones - is the
//  engine's, untouched.
// ---------------------------------------------------------------------------------------------
static int HeliFault(EXCEPTION_POINTERS* ep)
{
    if (!g_heliFault)
    {
        g_heliFault = true;
        Log("!!! EXCEPTION 0x%08lX in the helicopter model at %p - the helicopter is stock for this "
            "session.\n", ep->ExceptionRecord->ExceptionCode, ep->ExceptionRecord->ExceptionAddress);
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

static void RunHeli(void* veh, const float lv0[3], const float av0[3], const Config& cfg)
{
    __try { hl::Fly(veh, lv0, av0, cfg, mdl::g_telemetry || cfg.telemetry); }
    __except (HeliFault(GetExceptionInformation())) {}
}

// The planes (plane.h): true = it's one of the AIRCRAFT_* planes lcudrive flies as an aeroplane, so
// the helicopter model must not touch it this frame (even when the wing stood down).
static bool RunPlane(void* veh, const float lv0[3], const float av0[3], const Config& cfg)
{
    __try
    {
        const int kind = pl::KindOf(veh, cfg);
        if (kind == pl::kNone) return false;
        pl::Fly(veh, kind, lv0, av0, cfg, mdl::g_telemetry || cfg.telemetry);
        return true;
    }
    __except (HeliFault(GetExceptionInformation())) { return true; }
}

// The parachute after a bail-out (chute.h): once per frame from whichever vehicle updates - the
// player is on foot then, and the abandoned heli may already be gone.
static uint32_t g_lastChute = 0, g_lastChute2 = 0;

static void ChuteFrameOne(const Config& cfg, uint32_t& last);
static void ChuteFrame(const Config& cfg)
{
    const bool was2 = g_ctx == 1;
    if (was2) SwapCtx();                                    // from player 2's vehicle: player 1 first
    ChuteFrameOne(cfg, g_lastChute);
    if (chute::g_p2.armed || chute::g_p2.open) { SwapCtx(); ChuteFrameOne(cfg, g_lastChute2); SwapCtx(); }
    if (was2) SwapCtx();
}
static void ChuteFrameOne(const Config& cfg, uint32_t& g_lastChute)
{
    if (!chute::g.armed && !chute::g.open) return;
    const uint32_t now = GetTickCount();
    if (g_lastChute && now - g_lastChute < 12) return;
    const float dt = g_lastChute ? Clampf((now - g_lastChute) / 1000.0f, 0.0f, 0.1f) : 0.016f;
    g_lastChute = now;
    __try { chute::Tick(dt, cfg); }
    __except (HeliFault(GetExceptionInformation())) { chute::g = chute::State{}; }
}

static void RunWreck(void* veh, const float lv0[3], const float av0[3], const Config& cfg)
{
    __try { hl::WreckPost(veh, lv0, av0, cfg, mdl::g_telemetry || cfg.telemetry); }
    __except (HeliFault(GetExceptionInformation())) { hl::g_wr.active = false; }
}

// Before the engine's update of ANY helicopter: the free-flight patches follow ] / [ and the ini,
// the one the player just left gets its hand-over, and a falling wreck gets its frame. A police
// helicopter (cop, wanted_heli.h) ignores the vehicle-only boundary walls too, like the helicopter
// it follows - without this line clearing its mask every frame.
static bool RunHeliPre(void* veh, bool player, bool on, const Config& cfg, bool cop)
{
    __try
    {
        const uint32_t now = GetTickCount();
        const bool ff = on && cfg.heli_free_flight && !hl::g_releaseReq;
        hl::SetFreeFlight(ff);
        if (player) hl::NotePlayerHeli(veh, now);
        else if (on) hl::HandOver(veh, cfg, now);
        const bool wreck = on && hl::WreckPre(veh, now);
        hl::BoundaryMask(veh, (ff && (player || wreck)) || (cop && !hl::g_releaseReq));
        return wreck;
    }
    __except (HeliFault(GetExceptionInformation())) { return false; }
}

static void FreeFlightOff()
{
    __try { hl::SetFreeFlight(false); }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
}

// A police helicopter (wanted.h's, wanted_heli.h): flown by copter_model.h after the engine's
// update, behind the helicopters' own fault switch (wd::CopFault - off for the session, the rest of
// wanted mode carries on). Its flight state is its own: hl::g_state / g_veh / g_ct are never touched.
// After a wanted-mode fault nothing flies them: wd::FaultCleanup (PMThunk) lets go of them.
static bool IsCopHeli(void* veh)
{
    if (g_wantedFault || !wd::HeliOurs(veh)) return false;             // (a pointer compare: most frames end here)
    __try { return wd::HeliLive(veh); }
    __except (wd::CopFault(GetExceptionInformation())) { return false; }
}

static void RunCopHeli(void* veh, const float lv0[3], const float av0[3], const Config& cfg)
{
    __try { wd::HeliFly(veh, lv0, av0, cfg); }
    __except (wd::CopFault(GetExceptionInformation())) {}
}

// The aircraft a player flies, with a police helicopter right against it: heli.h's crash detector
// (and plane.h's) would read the solver's push apart as a crash and damage it. Skip that frame's
// check - the same thing a gap between our writes does (both re-arm on the next write).
static void RunCopTouch(void* veh)
{
    __try { if (wd::HeliTouching(veh, 9.0f)) { hl::g_ct.primed = false; pl::g_state.primed = 0; } }
    __except (wd::CopFault(GetExceptionInformation())) {}
}

static void __fastcall HoverThunk(void* veh, float dt)
{
    InterlockedIncrement(&g_inFlight);
    const UpdateFn orig = g_origHover;
    const Config& cfg = g_cfgBuf[g_cfgIdx];
    bool wreck = false, fly = false, swapped = false, cop = false;
    float lv0[3], av0[3];
    if (!g_unhooked && !g_heliFault)
    {
        const bool on = g_enabled && cfg.heli_enable;
        // player 2's helicopter, the one they just left, or their falling wreck: their state
        const bool p1 = PlayerVehicle() == veh;
        const bool p2 = !p1 && Player2Vehicle() == veh;
        swapped = p2 || (!p1 && hl::P2Owns(veh));
        if (swapped) SwapCtx();
        const bool player = p1 || p2;
        cop = !player && IsCopHeli(veh);                                // a police helicopter of wanted mode's
        if (player) { rd::g_vehSeen = static_cast<LONG>(GetTickCount()); rd::g_vehKind = 2; RadioVehicleFrame(veh, cfg); }
        wreck = RunHeliPre(veh, player, on, cfg, cop);
        fly = !wreck && on && player;
        if ((wreck || fly) && !hl::Capture(veh, lv0, av0)) wreck = fly = false;
        if (fly && !g_wantedFault && wd::g_nHeliLive) RunCopTouch(veh);  // (after the swap: this player's detector)
    }
    else
    {
        if (g_heliFault && hl::g_ffOn) FreeFlightOff();                 // a fault: the game's limits back too
        // the player's helicopter model faulted: wanted mode's helicopters still fly
        cop = !g_unhooked && IsCopHeli(veh) && PlayerVehicle() != veh && Player2Vehicle() != veh;
    }
    const bool copFly = cop && !wreck && hl::Capture(veh, lv0, av0);
    orig(veh, dt);
    if (wreck) RunWreck(veh, lv0, av0, cfg);
    else if (fly && !RunPlane(veh, lv0, av0, cfg)) RunHeli(veh, lv0, av0, cfg);
    else if (copFly) RunCopHeli(veh, lv0, av0, cfg);
    if (swapped) SwapCtx();
    if (!g_unhooked && !g_heliFault) ChuteFrame(cfg);
    InterlockedDecrement(&g_inFlight);
}

// ---------------------------------------------------------------------------------------------
//  The motorbike: ApiVehicleBike::Update (slot 91). For the player's bike only, the body's
//  velocities are taken before the engine's update, and bike.h replaces the pitch (and in the air
//  the yaw) of what the engine wrote: wheelies, stoppies, flips. Everything else is the engine's.
// ---------------------------------------------------------------------------------------------
static int BikeFault(EXCEPTION_POINTERS* ep)
{
    if (!g_bikeFault)
    {
        g_bikeFault = true;
        Log("!!! EXCEPTION 0x%08lX in the motorbike code at %p - motorbikes are stock for this session.\n",
            ep->ExceptionRecord->ExceptionCode, ep->ExceptionRecord->ExceptionAddress);
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

static void RunBike(void* veh, const float lv0[3], const float av0[3], const Config& cfg)
{
    __try
    {
        bk::Ride(veh, lv0, av0, cfg, mdl::g_telemetry || cfg.telemetry);
        if (bk::g_drive.veh == veh) bk::Aero(veh, At<float>(veh, eng::VEH_DT));
    }
    __except (BikeFault(GetExceptionInformation())) {}
}

static void RunPlanDrive(void* veh, const Config& cfg)
{
    __try { bk::BrakeNotSlide(veh, cfg); bk::PlanDrive(veh, cfg); }
    __except (BikeFault(GetExceptionInformation())) { bk::g_drive.veh = nullptr; bk::g_drive.on = false; }
}

static void __fastcall BikeThunk(void* veh, float dt)
{
    InterlockedIncrement(&g_inFlight);
    const UpdateFn orig = g_origBike;
    const Config& cfg = g_cfgBuf[g_cfgIdx];
    const bool bike1 = !g_unhooked && PlayerVehicle() == veh;
    const bool bike2 = !g_unhooked && !bike1 && Player2Vehicle() == veh;
    if (bike2) SwapCtx();                                   // player 2's bike: their state, their pad
    const bool playerBike = bike1 || bike2;
    if (playerBike) { rd::g_vehSeen = static_cast<LONG>(GetTickCount()); rd::g_vehKind = 1; RadioVehicleFrame(veh, cfg); }
    const bool active = playerBike && g_enabled && !g_bikeFault && cfg.bike_enable;
    float lv0[3], av0[3];
    const bool captured = active && hl::Capture(veh, lv0, av0);
    if (captured) RunPlanDrive(veh, cfg);                  // the drag and wheel hooks below act on it
    wd::Cop cop = {};
    const bool isCop = !active && CopBefore(veh, cop);     // a police bike chasing you (wanted.h)
    orig(veh, dt);
    if (captured) RunBike(veh, lv0, av0, cfg);
    if (isCop) RunCopPost(veh, cop, 0.75f);                // the game's bike skid threshold (bike.h)
    bk::g_drive.veh = nullptr; bk::g_drive.on = false;
    if (bike2) SwapCtx();
    if (!g_unhooked && !g_heliFault) ChuteFrame(cfg);
    InterlockedDecrement(&g_inFlight);
}

// ---------------------------------------------------------------------------------------------
//  Wanted mode: cPopulationManager::Update (slot 13) - the director runs after the original, in
//  the system phase that owns the traffic and pursuit lists (wanted.h, Notes\33 §7.1).
// ---------------------------------------------------------------------------------------------
static int WantedFault(EXCEPTION_POINTERS* ep)
{
    if (!g_wantedFault)
    {
        g_wantedFault = true;
        Log("!!! EXCEPTION 0x%08lX in the wanted-mode code at %p - wanted mode is off for this session.\n",
            ep->ExceptionRecord->ExceptionCode, ep->ExceptionRecord->ExceptionAddress);
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

static void RunWanted(void* pm, void* info, const Config& cfg)
{
    __try
    {
        float dt = PtrOk(info) ? At<float>(info, 0x18) : 0.0f;
        if (!IsFin(dt) || dt <= 0.0f || dt > 0.25f) dt = 1.0f / 60.0f;
        wd::Tick(pm, dt, cfg, g_enabled && cfg.wanted_enable != 0 && !br::Quiet());   // [backrooms1] && !Quiet
    }
    __except (WantedFault(GetExceptionInformation())) {}
}

// ---------------------------------------------------------------------------------------------
//  The radio and the game's own sound (Notes\37), once a frame on the game thread. The game's music
//  (TT's NuSound: the TrackManager fader in front of the "Music" bus - score, chase and mission
//  music; free roam has none of its own) fades to `[radio] game_music` while the radio plays and
//  back after; the duck the game puts on its music under dialogue and jingles, and its Volume
//  option, go to the radio; a cutscene holds the radio.
// ---------------------------------------------------------------------------------------------
namespace gsnd {
static const uintptr_t RVA_TRACKMGR = 0x1C4DE10, RVA_GAME = 0x1C7A830, RVA_PAUSED = 0x1A99B95;
static const uintptr_t FN_FADER = 0x569820;                 // void(TrackManager*, float target, float seconds)
static const uintptr_t RVA_CUTSCENES = 0x17EBCF8, FN_CUTSCENE = 0x7B3380;   // bool(LegoCutscenes*, void**)
static const uintptr_t RVA_NUSOUND = 0x1C28240, FN_FINDBUS = 0x4183C0;       // Bus*(NuSound*, const char*)
static const uintptr_t RVA_BUS_DUCKMUSIC = 0x17FE310, RVA_VT_ATTEN = 0xCBB068;
static const uintptr_t TM_TARGET = 0x154, TM_TIME = 0x158;

typedef void(__fastcall* FaderFn)(void* tm, float target, float seconds);
typedef bool(__fastcall* CutsceneFn)(void* cs, void** out);
typedef uint8_t*(__fastcall* FindBusFn)(void* nusound, const char* name);

static bool     g_own = false;               // we have the music fader
static float    g_normal = 1.0f;             // the level to give back (the game's Music option)
static uint8_t* g_duckBus = nullptr;
static volatile bool g_fault = false;

static uint8_t* Options()
{
    uint8_t* g = *reinterpret_cast<uint8_t**>(g_base + RVA_GAME);
    return PtrOk(g) ? At<uint8_t*>(g, 0x3250) : nullptr;
}

// The smallest attenuation on a bus's effect chain (1 = no duck on it).
static float BusDuck(uint8_t* bus)
{
    if (!PtrOk(bus)) return 1.0f;
    const uint32_t n = At<uint32_t>(bus, 0x68);
    if (n == 0 || n > 32) return 1.0f;
    uint8_t* head = At<uint8_t*>(bus, 0x58);
    uint8_t* lastp = At<uint8_t*>(bus, 0x60);
    if (!PtrOk(head) || !PtrOk(lastp)) return 1.0f;
    uint8_t* last = *reinterpret_cast<uint8_t**>(lastp);
    uint8_t* node = At<uint8_t*>(head, 0x08);
    float g = 1.0f;
    for (uint32_t i = 0; PtrOk(node) && i < n; ++i)
    {
        uint8_t* fx = At<uint8_t*>(node, 0x10);
        if (PtrOk(fx) && At<uintptr_t>(fx, 0) == g_base + RVA_VT_ATTEN)
        {
            const float v = At<float>(fx, 0x68);
            if (IsFin(v) && v >= 0.0f && v < g) g = v;
        }
        if (node == last) break;
        node = At<uint8_t*>(node, 0x08);
    }
    return g;
}

static void Tick(const Config& cfg)
{
    const bool paused = *reinterpret_cast<const uint8_t*>(g_base + RVA_PAUSED) != 0;
    // a cutscene or FMV holds the radio (and gives the game its music back)
    void* cs = *reinterpret_cast<void**>(g_base + RVA_CUTSCENES);
    const bool quiet = br::Quiet();                                  // [backrooms1] the Backrooms: no music, no radio
    rd::g_hold = ((PtrOk(cs) && reinterpret_cast<CutsceneFn>(g_base + FN_CUTSCENE)(cs, nullptr)) || quiet) ? 1 : 0;   // [backrooms1] || quiet
    // the game's Volume option, and its duck on music (dialogue lines 0.3, jingles 0.2)
    uint8_t* o = Options();
    if (o) rd::g_vol1000 = static_cast<LONG>((o[6] > 10 ? 10 : o[6]) * 100);
    if (!g_duckBus)
    {
        g_duckBus = *reinterpret_cast<uint8_t**>(g_base + RVA_BUS_DUCKMUSIC);
        if (!PtrOk(g_duckBus)) g_duckBus = reinterpret_cast<FindBusFn>(g_base + FN_FINDBUS)(reinterpret_cast<void*>(g_base + RVA_NUSOUND), "DuckableMusic");
        if (!PtrOk(g_duckBus)) g_duckBus = nullptr;
    }
    rd::g_duck1000 = static_cast<LONG>(BusDuck(g_duckBus) * 1000.0f);

    // the music fader: never while paused (the pause screens save and restore it themselves)
    uint8_t* tm = *reinterpret_cast<uint8_t**>(g_base + RVA_TRACKMGR);
    if (!PtrOk(tm) || paused) return;
    const bool radio = rd::g_audible != 0 && !rd::g_hold && cfg.radio_enable;
    const bool own = radio || quiet;                                  // [backrooms1]
    if (!own && !g_own) return;                                       // never touched unless we took it   [backrooms1] own
    g_normal = (o && o[7] == 0) ? 0.0f : 1.0f;                       // the Music option
    const float want = quiet ? 0.0f : radio ? std::fmin(g_normal, cfg.radio_game_music) : g_normal;   // [backrooms1] quiet = 0
    if (At<float>(tm, TM_TARGET) != want) reinterpret_cast<FaderFn>(g_base + FN_FADER)(tm, want, radio ? 1.5f : 2.0f);
    if (!g_own && radio) Log(">>> radio: the game's music fades to %.2f while the radio plays\n", want);
    g_own = own;                                                      // one frame of `normal`, then we let go   [backrooms1] own
}

// lcudrive unhook: the game's music back at once (any thread; three floats)
static void Release()
{
    if (!g_own) return;
    __try
    {
        uint8_t* tm = *reinterpret_cast<uint8_t**>(g_base + RVA_TRACKMGR);
        if (PtrOk(tm)) { At<float>(tm, TM_TARGET) = g_normal; At<float>(tm, TM_TIME) = 1.0f; }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
    g_own = false;
}

// Where the player is, and in what, for the news ({district} / {vehicle} renders): the game's own
// district lookup (Notes\37 §5: position -> streaming cell -> LegoCityDistrictSystem record), twice a
// second, and the vehicle's model name -> a fills.json class whenever the vehicle changes.
static const uintptr_t RVA_LEVELGRID = 0x1C6A768, FN_GRID = 0x47C0, FN_CELL = 0x611320;
static const uintptr_t RVA_DISTRICTS = 0x17ECA20, FN_DISTRICT = 0xCAD40;
typedef void*(__fastcall* GridFn)(void* b);
typedef void*(__fastcall* CellFn)(void* grid, const float* pos4, float* out);
typedef uint8_t*(__fastcall* DistrictFn)(void* sys, void* cell);
static void*    g_whereVeh = nullptr;
static uint32_t g_whereT = 0;
static LONG     g_lastDistrict = -2;

// The district at a world position (-1 = none / not known; -2 = the lookup isn't up).
static LONG DistrictAt(const float p[3])
{
    {   // Port Atlas ([sites] atlas_*): no district of the game's is out there; the radio gets its own index
        const Config& c = g_cfgBuf[g_cfgIdx];
        if (c.sites_atlas && IsFin(p[0]) && IsFin(p[2]) &&
            sitem::InBox(sitem::MakeBox(c.sites_atlas_x0, c.sites_atlas_x1, c.sites_atlas_z0, c.sites_atlas_z1), p[0], p[2]))
            return sitem::kDistrictPortAtlas;
    }
    uint8_t* l = *reinterpret_cast<uint8_t**>(g_base + RVA_LEVELGRID);
    uint8_t* sys = *reinterpret_cast<uint8_t**>(g_base + RVA_DISTRICTS);
    if (!PtrOk(l) || !PtrOk(sys)) return -2;
    uint8_t* a = At<uint8_t*>(l, 0x178);
    uint8_t* b = PtrOk(a) ? At<uint8_t*>(a, 0x98) : nullptr;
    if (!PtrOk(b)) return -2;
    void* grid = reinterpret_cast<GridFn>(g_base + FN_GRID)(b);
    if (!PtrOk(grid)) return -2;
    alignas(16) float pos[4] = { p[0], p[1], p[2], 1.0f };
    void* cell = reinterpret_cast<CellFn>(g_base + FN_CELL)(grid, pos, nullptr);
    if (!PtrOk(cell)) return -2;
    uint8_t* rec = reinterpret_cast<DistrictFn>(g_base + FN_DISTRICT)(sys, cell);
    const LONG d = PtrOk(rec) ? static_cast<LONG>((rec - (sys + 0xE0)) / 0x28) : -1;
    return (d >= 0 && d < 23) ? d : -1;
}

static void NoteDistrict(LONG d)
{
    rd::g_district = d;
    if (rd::g_district != g_lastDistrict)
    {
        g_lastDistrict = rd::g_district;
        Log("    radio: you're in district %ld (%s)\n", rd::g_district,
            rd::g_district >= 0 && rd::kDistrictFill[rd::g_district][0] ? rd::kDistrictFill[rd::g_district] : "no news name");
    }
}

static void Where(void* veh)
{
    if (veh != g_whereVeh)
    {
        g_whereVeh = veh;
        char name[64];
        void* def = At<void*>(veh, eng::VEH_DEF);
        if (!wd::g_susBoat)                                     // (the wanted suspect at sea: PMThunk's "boat")
            rd::g_vehClass = (PtrOk(def) && ReadModelName(def, name, sizeof(name))) ? rd::VehicleClass(name) : 20;
    }
    const uint32_t now = GetTickCount();
    if (now - g_whereT < 500) return;
    g_whereT = now;
    uint8_t* inst = At<uint8_t*>(veh, eng::VEH_INSTANCE);
    if (!PtrOk(inst)) return;
    const float pos[3] = { At<float>(inst, 0x90), At<float>(inst, 0x94), At<float>(inst, 0x98) };
    const LONG d = DistrictAt(pos);
    if (d == -2) return;
    NoteDistrict(d);
}

// On foot (the ambient life's places, and the news): the same lookup at the character, twice a second.
static void WhereOnFoot(const float p[3])
{
    const uint32_t now = GetTickCount();
    if (now - g_whereT < 500) return;
    g_whereT = now;
    const LONG d = DistrictAt(p);
    if (d != -2) NoteDistrict(d);
}

// While in a vehicle: the radio's D-pad buttons are hidden from the game (D-pad left / right open
// the Criminal Scanner), the way the game blocks a held button: OR'd into the pad's blocked mask and
// taken out of this frame's held / pressed / released (Notes\37 §3). The radio reads XInput itself.
static void BlockPad(const Config& cfg)
{
    if (!cfg.radio_enable || !cfg.radio_pad_block) return;
    uint32_t bits = 0;
    for (uint32_t b : { static_cast<uint32_t>(cfg.radio_pad_next), static_cast<uint32_t>(cfg.radio_pad_prev) })
    {
        if (b == XINPUT_GAMEPAD_DPAD_UP) bits |= 0x1;
        if (b == XINPUT_GAMEPAD_DPAD_RIGHT) bits |= 0x2;
        if (b == XINPUT_GAMEPAD_DPAD_DOWN) bits |= 0x4;
        if (b == XINPUT_GAMEPAD_DPAD_LEFT) bits |= 0x8;
    }
    if (!bits || *reinterpret_cast<const uint8_t*>(g_base + RVA_PAUSED) != 0) return;   // menus keep it
    uint8_t* ch = CtxChar();
    uint8_t* pad = PtrOk(ch) ? chute::Pad(ch) : nullptr;
    if (!PtrOk(pad)) return;
    At<uint32_t>(pad, 0x08) |= bits;
    At<uint32_t>(pad, 0x0C) &= ~bits;
    At<uint32_t>(pad, 0x10) &= ~bits;
    At<uint32_t>(pad, 0x14) &= ~bits;
}
} // namespace gsnd

static void RunGameSound(const Config& cfg)
{
    if (gsnd::g_fault) return;
    __try { gsnd::Tick(cfg); }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        gsnd::g_fault = true;
        rd::g_hold = 0; rd::g_duck1000 = 1000; rd::g_vol1000 = 1000;
        Log("!!! EXCEPTION in the radio's game-sound link - the game's music and the radio go their own "
            "ways for this session; the radio still plays.\n");
    }
}

static void RunBlockPad(const Config& cfg)
{
    if (gsnd::g_fault) return;
    __try { gsnd::BlockPad(cfg); }
    __except (EXCEPTION_EXECUTE_HANDLER) { gsnd::g_fault = true; Log("!!! EXCEPTION in the radio's pad block - off for this session.\n"); }
}

static volatile bool g_whereFault = false;

// The player's vehicle, once a frame: the radio's pad block, and where the news says you are.
static void RadioVehicleFrame(void* veh, const Config& cfg)
{
    RunBlockPad(cfg);
    if (g_whereFault || !cfg.radio_enable) return;
    __try { gsnd::Where(veh); }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        g_whereFault = true;
        rd::g_district = -1;
        Log("!!! EXCEPTION in the radio's district lookup - the news stays generic for this session.\n");
    }
}

// ---- the crimes (wanted.h): game-thread callbacks, SEH-latched like the director -----------------
static void RunCrimeEvent(void* ev, void* payload)
{
    __try { wd::OnVehicleEvent(ev, payload); }
    __except (WantedFault(GetExceptionInformation())) {}
}

// Event::Fire, at the player-vehicle event sites only (g_crimePatches).
static uint64_t __fastcall Hook_EventFire(void* ev, void* key, void* payload, void* r9)
{
    if (!g_unhooked && !g_wantedFault) RunCrimeEvent(ev, payload);
    return reinterpret_cast<Pass4Fn>(g_base + wd::FN_EVENT_FIRE)(ev, key, payload, r9);
}

static void RunCrowdPunch(void* hook)
{
    __try { wd::OnCrowdPunch(static_cast<uint8_t*>(hook)); }
    __except (WantedFault(GetExceptionInformation())) {}
}

// The krawlie hit query of a melee swing: al = something was hit; hook+8 = the attacker.
static uint64_t __fastcall Hook_KrawlieHit(void* mgr, void* query, void* hook, void* r9)
{
    const uint64_t r = reinterpret_cast<Pass4Fn>(g_base + wd::FN_KRAWLIE_HIT)(mgr, query, hook, r9);
    if ((r & 0xFF) && !g_unhooked && !g_wantedFault) RunCrowdPunch(hook);
    return r;
}

static void RunCharacterHit(uint64_t att, uint64_t vic, uint64_t bolt)
{
    __try { wd::OnCharacterHit(reinterpret_cast<uint8_t*>(att), reinterpret_cast<uint8_t*>(vic), reinterpret_cast<uint8_t*>(bolt)); }
    __except (WantedFault(GetExceptionInformation())) {}
}

// GameObjects::OnCharacterDamage (vtable slot 60): every hit on a character (bolt = the shot, if it is one).
static uint64_t __fastcall Hook_OnDamage(uint64_t gm, uint64_t att, uint64_t vic, uint64_t bolt, uint64_t amount,
                                         uint64_t flags, uint64_t a5, uint64_t a6)
{
    if (!g_unhooked && !g_wantedFault) RunCharacterHit(att, vic, bolt);
    return g_origOnDamage(gm, att, vic, bolt, amount, flags, a5, a6);
}

// ---- the ray gun (wanted.h). Who fired is read BEFORE the game's routine runs (the bolt is whole
// then); the crime is added after it, from what the routine did. Reading the game's bolt has a
// latch of its own: a fault there turns the ray-gun crimes off, not wanted mode.
static volatile bool g_rayFault = false;

static int RayFault(EXCEPTION_POINTERS* ep)
{
    if (!g_rayFault)
    {
        g_rayFault = true;
        Log("!!! EXCEPTION 0x%08lX reading a ray-gun shot at %p - the ray gun is no crime for this session "
            "(the rest of wanted mode goes on).\n", ep->ExceptionRecord->ExceptionCode, ep->ExceptionRecord->ExceptionAddress);
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

static bool RayOn() { return !g_unhooked && !g_wantedFault && !g_rayFault; }

static int RunCrowdShotBy(void* hook)
{
    __try { return wd::CrowdShotBy(static_cast<uint8_t*>(hook)); }
    __except (RayFault(GetExceptionInformation())) {}
    return -1;
}

static int RunRayShooter(uint64_t bolt)
{
    __try { return wd::RayShooter(reinterpret_cast<uint8_t*>(bolt)); }
    __except (RayFault(GetExceptionInformation())) {}
    return -1;
}

static void RunCrowdShot(int by)
{
    __try { wd::OnCrowdShot(by); }
    __except (WantedFault(GetExceptionInformation())) {}
}

static void RunPropShot(int by)
{
    __try { wd::OnPropShot(by); }
    __except (WantedFault(GetExceptionInformation())) {}
}

static float RunPropTimer(void* prop)
{
    __try { return wd::PropTimer(static_cast<uint8_t*>(prop)); }
    __except (RayFault(GetExceptionInformation())) {}
    return 1.0f;
}

static void RunVehicleShot(void* bolt, void* veh)
{
    __try { wd::NoteVehicleShot(static_cast<uint8_t*>(bolt), veh); }
    __except (RayFault(GetExceptionInformation())) {}
}

// The krawlie hit query of a bolt in flight (two sites): al = a pedestrian took it; hook+8 = the bolt.
static uint64_t __fastcall Hook_KrawlieShot(void* mgr, void* query, void* hook, void* r9)
{
    const int by = RayOn() ? RunCrowdShotBy(hook) : -1;
    const uint64_t r = reinterpret_cast<Pass4Fn>(g_base + wd::FN_KRAWLIE_HIT)(mgr, query, hook, r9);
    if ((r & 0xFF) && by >= 0 && RayOn()) RunCrowdShot(by);
    return r;
}

// A street prop's bolt hit, called from cBoltCityType's hit only: (prop, level, bolt).
static uint64_t __fastcall Hook_PropBoltHit(void* prop, void* level, void* bolt, void* r9)
{
    const int by = RayOn() ? RunRayShooter(reinterpret_cast<uint64_t>(bolt)) : -1;
    const float before = by >= 0 ? RunPropTimer(prop) : 1.0f;
    const uint64_t r = reinterpret_cast<Pass4Fn>(g_base + wd::FN_PROP_BOLTHIT)(prop, level, bolt, r9);
    if (by >= 0 && RayOn() && wanted::PropSmashed(before, RunPropTimer(prop)) && RayOn()) RunPropShot(by);
    return r;
}

// LegoCityBlowups vtable slot 43: a bolt against a LEGO object; al = it broke or took damage.
static uint64_t __fastcall Hook_BlowupBolt(uint64_t sys, uint64_t level, uint64_t blowup, uint64_t pos, uint64_t player,
                                           uint64_t kind, uint64_t bolt)
{
    const int by = RayOn() ? RunRayShooter(bolt) : -1;
    const uint64_t r = g_origBlowupBolt(sys, level, blowup, pos, player, kind, bolt);
    if ((r & 0xFF) && by >= 0 && RayOn()) RunPropShot(by);
    return r;
}

// LegoCityBolts vtable slot 68: a bolt against a vehicle (bolts, bolt, vehicle). Only remembered.
static uint64_t __fastcall Hook_BoltVehicle(void* bolts, void* bolt, void* veh, void* r9)
{
    if (RayOn()) RunVehicleShot(bolt, veh);
    return g_origBoltVehicle(bolts, bolt, veh, r9);
}

// ---------------------------------------------------------------------------------------------
//  Ambient life (life.h, Notes\40): after wanted mode in the same tick, its own fault switch.
// ---------------------------------------------------------------------------------------------
static int LifeFault(EXCEPTION_POINTERS* ep)
{
    if (!lf::g_fault)
    {
        lf::g_fault = true;
        Log("!!! EXCEPTION 0x%08lX in the ambient life code at %p - no street scenes for this session.\n",
            ep->ExceptionRecord->ExceptionCode, ep->ExceptionRecord->ExceptionAddress);
    }
    lf::ForgetAll();
    return EXCEPTION_EXECUTE_HANDLER;
}

static void RunLife(void* pm, void* info, const Config& cfg)
{
    __try
    {
        wd::World w = wd::GetWorld();
        if (!w.ok || pm != w.pm) return;
        static void* s_lastPm = nullptr;
        if (w.pm != s_lastPm) { s_lastPm = w.pm; lf::ForgetAll(); }       // a new world
        float dt = PtrOk(info) ? At<float>(info, 0x18) : 0.0f;
        if (!IsFin(dt) || dt <= 0.0f || dt > 0.25f) dt = 1.0f / 60.0f;
        uint8_t* p1 = *reinterpret_cast<uint8_t**>(g_base + eng::RVA_PLAYER1);
        if (PtrOk(p1) && !PlayerVehicle())                              // on foot: where are you (in a vehicle
        {                                                               // the radio's own lookup keeps it)
            const float p[3] = { At<float>(p1, 0xA8), At<float>(p1, 0xAC), At<float>(p1, 0xB0) };
            if (cfg.radio_enable) gsnd::WhereOnFoot(p);
            else { const LONG d = gsnd::DistrictAt(p); if (d != -2) rd::g_district = d; }
        }
        const bool blocked = wd::MissionRunning() || wd::g_shown > 0 || br::Quiet();   // [backrooms1] || Quiet (lf::Tick, lf::Party)
        lf::Tick(w.lvl, dt, cfg, static_cast<int>(rd::g_district), g_enabled && cfg.life_enable, blocked);
        lf::Party(w.lvl, dt, cfg, g_enabled, blocked);                  // the krawlie dance party (Notes\46)
    }
    __except (LifeFault(GetExceptionInformation())) {}
}

// The cut voice lines (voice.h, Notes 46): after life (its people queue their lines), own fault switch.
static int VoiceFault(EXCEPTION_POINTERS* ep)
{
    if (!vc::g_fault)
    {
        vc::g_fault = true;
        Log("!!! EXCEPTION 0x%08lX in the voice-line code at %p - no restored voice lines for this session.\n",
            ep->ExceptionRecord->ExceptionCode, ep->ExceptionRecord->ExceptionAddress);
    }
    vc::ForgetAll();
    return EXCEPTION_EXECUTE_HANDLER;
}

static void RunVoice(void* pm, void* info, const Config& cfg)
{
    __try
    {
        wd::World w = wd::GetWorld();
        if (!w.ok || pm != w.pm) return;
        static void* s_lastPm = nullptr;
        if (w.pm != s_lastPm) { if (s_lastPm) vc::StopAll(true); s_lastPm = w.pm; vc::ForgetAll(); }   // a new world
        float dt = PtrOk(info) ? At<float>(info, 0x18) : 0.0f;
        if (!IsFin(dt) || dt <= 0.0f || dt > 0.25f) dt = 1.0f / 60.0f;
        vc::Tick(dt, cfg, g_enabled && cfg.voice_enable && !br::Quiet());   // [backrooms1] && !Quiet
    }
    __except (VoiceFault(GetExceptionInformation())) {}
}

// ---------------------------------------------------------------------------------------------
//  The airport plane (airport.h): spawned / parked / faded from the same system phase as wanted.
// ---------------------------------------------------------------------------------------------
static bool g_airportFault = false;
static int AirportFault(EXCEPTION_POINTERS* ep)
{
    if (!g_airportFault)
    {
        g_airportFault = true;
        Log("!!! EXCEPTION 0x%08lX in the airport-plane code at %p - the airport plane is off for this session.\n",
            ep->ExceptionRecord->ExceptionCode, ep->ExceptionRecord->ExceptionAddress);
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

// On only while the system that flies [plane] airport_model is on: [airplane] (enable + models) for a
// car-class airliner such as the default Aircraft_Elifant, [plane] for a Hover one. Anything else would
// be a parked car or a helicopter - so no airport plane (logged once per change). And never after a
// failed startup (no car hook: nothing could fly it).
static void RunAirport(void* pm, void* info, const Config& cfg)
{
    __try
    {
        float dt = PtrOk(info) ? At<float>(info, 0x18) : 0.0f;
        if (!IsFin(dt) || dt <= 0.0f || dt > 0.25f) dt = 1.0f / 60.0f;
        const char* m = cfg.plane_airport_model;
        const int why = apm::Owner(cfg.airplane_enable != 0, fw::KindFromName(m, cfg) != 0,
                                   cfg.plane_enable != 0, pl::KindFromName(m, cfg) != pl::kNone);
        static int s_why = -1;
        if (why != s_why && cfg.plane_airport)
        {
            s_why = why;
            if (why == 2)
                Log(">>> airport: no airport plane - [plane] airport_model %s is flown by neither [airplane] (enable %d, "
                    "models %s) nor [plane] (enable %d, models %s)\n", m, cfg.airplane_enable,
                    cfg.airplane_models[0] ? cfg.airplane_models : "none", cfg.plane_enable,
                    cfg.plane_models[0] ? cfg.plane_models : "none");
            else Log(">>> airport: %s flies under [%s]\n", m, why == 0 ? "airplane" : "plane");
        }
        ap::Tick(pm, dt, cfg, g_enabled && g_slotCar != nullptr && why != 2 && !br::Quiet());   // [backrooms1] && !Quiet
    }
    __except (AirportFault(GetExceptionInformation())) {}
}

// Once, after a fault in the airport code: what it had out goes back (the pin off; the plane faded
// unless someone is aboard; the weak ref and a pending preload released).
static void RunAirportRelease()
{
    __try { ap::Release(g_cfgBuf[g_cfgIdx], "the airport code faulted"); }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
}

// ---------------------------------------------------------------------------------------------
//  The rideable T-Rex at the airport (dino.h): spawned / removed from the same system phase.
// ---------------------------------------------------------------------------------------------
static bool g_dinoFault = false;
static int DinoFault(EXCEPTION_POINTERS* ep)
{
    if (!g_dinoFault)
    {
        g_dinoFault = true;
        Log("!!! EXCEPTION 0x%08lX in the T-Rex code at %p - the airport T-Rexes are off for this session "
            "([dino] enable = 0 keeps them off).\n", ep->ExceptionRecord->ExceptionCode, ep->ExceptionRecord->ExceptionAddress);
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

static void RunDino(void* pm, void* info, const Config& cfg)
{
    __try
    {
        float dt = PtrOk(info) ? At<float>(info, 0x18) : 0.0f;
        if (!IsFin(dt) || dt <= 0.0f || dt > 0.25f) dt = 1.0f / 60.0f;
        dn::Tick(pm, dt, cfg, g_enabled && g_slotCar != nullptr && !br::Quiet());   // [backrooms1] && !Quiet
    }
    __except (DinoFault(GetExceptionInformation())) {}
}

// Once, after a fault in the T-Rex code: what it had out goes back (a T-Rex nobody is on or beside is
// removed; the watch and a pending preload released).
static void RunDinoRelease()
{
    __try { dn::Release(g_cfgBuf[g_cfgIdx], "the T-Rex code faulted"); }
    __except (EXCEPTION_EXECUTE_HANDLER) { dn::g_released = true; }
}

// ---------------------------------------------------------------------------------------------
//  The Pteranodon flies (ptero.h): its hands and rides once per world update (here), its flight in
//  the character sweep hook (Hook_CharSweep, below). Its own fault switch: a fault = the Pteranodons
//  walk for the session (the bodies put upright once), nothing else changes.
// ---------------------------------------------------------------------------------------------
static volatile bool g_pteroFault = false;
static int PteroFault(EXCEPTION_POINTERS* ep)
{
    if (!g_pteroFault)
    {
        g_pteroFault = true;
        Log("!!! EXCEPTION 0x%08lX in the Pteranodon flight code at %p - Pteranodons walk for this session "
            "([pteranodon] enable = 0 keeps them on the ground).\n", ep->ExceptionRecord->ExceptionCode, ep->ExceptionRecord->ExceptionAddress);
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

static void RunPtero(void* info, const Config& cfg)
{
    __try
    {
        float dt = PtrOk(info) ? At<float>(info, 0x18) : 0.0f;
        if (!IsFin(dt) || dt <= 0.0f || dt > 0.25f) dt = 1.0f / 60.0f;
        pt::Tick(dt, cfg, g_enabled && g_slotCar != nullptr && g_pteroPatch.applied, mdl::g_telemetry || cfg.telemetry);
    }
    __except (PteroFault(GetExceptionInformation())) {}
}

// Once, after a fault in the flight code: the bodies upright, our hands off them.
static void RunPteroRelease()
{
    __try { pt::Release("the Pteranodon code faulted"); }
    __except (EXCEPTION_EXECUTE_HANDLER) { pt::g_released = true; }
}

static void RunPteroPre(int i, void* phys, float dt)
{
    __try { pt::Pre(i, phys, dt); }
    __except (PteroFault(GetExceptionInformation())) { pt::g_phys[0] = pt::g_phys[1] = nullptr; }
}

static void RunPteroPost(int i, void* phys, float dt)
{
    __try { pt::Post(i, phys, dt); }
    __except (PteroFault(GetExceptionInformation())) { pt::g_phys[0] = pt::g_phys[1] = nullptr; }
}

// The retargeted `call 0x140AB7A00` in every character's movement integrator (0x14049F5B5): one of
// ours gets its velocity and attitude right before the swept move, and is looked at right after it.
static bool __fastcall Hook_CharSweep(void* phys, float dt)
{
    InterlockedIncrement(&g_inFlight);
    const int i = !g_unhooked && !g_pteroFault ? pt::Match(phys) : -1;
    if (i >= 0) RunPteroPre(i, phys, dt);
    const bool r = pt::g_origSweep(phys, dt);
    if (i >= 0 && !g_pteroFault) RunPteroPost(i, phys, dt);
    InterlockedDecrement(&g_inFlight);
    return r;
}

// ---------------------------------------------------------------------------------------------
//  The stands of [sites] (sites.h): boats / helicopters waiting at Port Atlas and anywhere else the
//  ini puts one, from the same system phase. Its own fault switch: a fault = no stands for the session.
// ---------------------------------------------------------------------------------------------
static bool g_sitesFault = false;
static int SitesFault(EXCEPTION_POINTERS* ep)
{
    if (!g_sitesFault)
    {
        g_sitesFault = true;
        Log("!!! EXCEPTION 0x%08lX in the stands code at %p - the [sites] vehicles are off for this session "
            "([sites] enable = 0 keeps them off).\n", ep->ExceptionRecord->ExceptionCode, ep->ExceptionRecord->ExceptionAddress);
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

static void RunSites(void* pm, void* info, const Config& cfg)
{
    __try
    {
        float dt = PtrOk(info) ? At<float>(info, 0x18) : 0.0f;
        if (!IsFin(dt) || dt <= 0.0f || dt > 0.25f) dt = 1.0f / 60.0f;
        si::Tick(pm, dt, cfg, g_enabled && g_slotCar != nullptr && !br::Quiet());   // [backrooms1] && !Quiet (marina rides on it)
    }
    __except (SitesFault(GetExceptionInformation())) {}
}

// Once, after a fault in the stands code: what it had out goes back (a vehicle nobody is in or beside
// fades; the pins, the weak refs and a pending preload released).
static void RunSitesRelease()
{
    __try { si::Release(g_cfgBuf[g_cfgIdx], "the stands code faulted"); }
    __except (EXCEPTION_EXECUTE_HANDLER) { si::g_released = true; }
}

// ---------------------------------------------------------------------------------------------
//  [boats2] The traffic boats (boats.h, Notes 56 section 8): the boat map's spawn spacing and the file
//  breaker. Its own fault switch: a fault = the stock spacing back, nothing more from here this session.
// ---------------------------------------------------------------------------------------------
static int BoatsFault(EXCEPTION_POINTERS* ep)
{
    if (!bt::g_fault)
    {
        bt::g_fault = true;
        Log("!!! EXCEPTION 0x%08lX in the boats code at %p - the stock boat traffic for this session "
            "([boats] enable = 0 keeps it off).\n", ep->ExceptionRecord->ExceptionCode, ep->ExceptionRecord->ExceptionAddress);
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

static void RunBoats(void* pm, void* info, const Config& cfg)
{
    __try
    {
        wd::World w = wd::GetWorld();
        if (!w.ok || pm != w.pm) return;
        float dt = PtrOk(info) ? At<float>(info, 0x18) : 0.0f;
        if (!IsFin(dt) || dt <= 0.0f || dt > 0.25f) dt = 1.0f / 60.0f;
        bt::Tick(pm, dt, cfg, g_enabled && g_slotCar != nullptr, g_enabled);
    }
    __except (BoatsFault(GetExceptionInformation())) {}
}

// ---------------------------------------------------------------------------------------------
//  The safe sea (2026-11-14: any fall into the water at Port Atlas ended in an immediate shark
//  death, which made the island close to impossible to leave). The shark is a level
//  setting, deadly_deep_water <depth> (parser 0x1407EC3C9 -> float 0x141C9D708): while it is > 0,
//  LegoFloating's entry test 0x14096A090 refuses a character over a terrain type flagged 0x100000
//  (table 0x14117D2C4, 36-byte entries) unless the character def is immune_to_deadly_water [P].
//  That float's only reader is that test [P, disp scan]. The open sea's floor is that type; Port
//  Atlas has no seabed of its own yet. So while either player is east of safe_sea_x the float is
//  held at 0 (the game's value is put back when both are west of it, and at unhook). That the
//  shark death needs the refusal is [I] until played.
// ---------------------------------------------------------------------------------------------
static const uintptr_t RVA_DEADLY_DEPTH = 0x1C9D708;
static float g_seaStock = 0.0f;       // the game's own depth, as last seen (> 0)
static bool  g_seaHeld = false;

static void SafeSeaRestore()
{
    __try
    {
        float* depth = reinterpret_cast<float*>(g_base + RVA_DEADLY_DEPTH);
        if (g_seaHeld && *depth == 0.0f && g_seaStock > 0.0f) *depth = g_seaStock;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
    g_seaHeld = false;
}

// The rare shark (shark.h, Notes 61): one tick per world update. A fault = the game's own shark for
// the session (the gate passes everything through from that moment).
static int SharkFault(EXCEPTION_POINTERS* ep)
{
    if (!sh::g_fault)
    {
        sh::g_fault = true; sh::g_on = false;
        Log("!!! EXCEPTION 0x%08lX in the shark code at %p - the game's own shark for this session "
            "([shark] enable = 0 keeps it unhooked).\n", ep->ExceptionRecord->ExceptionCode, ep->ExceptionRecord->ExceptionAddress);
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

static void RunShark(void* pm, void* info, const Config& cfg)
{
    __try
    {
        wd::World w = wd::GetWorld();
        if (!w.ok || pm != w.pm) return;
        float dt = PtrOk(info) ? At<float>(info, 0x18) : 0.0f;
        if (!IsFin(dt) || dt <= 0.0f || dt > 0.25f) dt = 1.0f / 60.0f;
        sh::Tick(w.pm, dt, cfg, g_enabled);
    }
    __except (SharkFault(GetExceptionInformation())) {}
}

static void RunSafeSea(const Config& cfg)
{
    __try
    {
        float* depth = reinterpret_cast<float*>(g_base + RVA_DEADLY_DEPTH);
        if (*depth > 0.0f) { g_seaStock = *depth; g_seaHeld = false; }   // a level (re)load wrote its own
        bool east = false;
        if (cfg.world_safe_sea)
        {
            uint8_t* p[2] = { *reinterpret_cast<uint8_t**>(g_base + eng::RVA_PLAYER1), Player2Char() };
            for (int i = 0; i < 2; ++i)
                if (PtrOk(p[i])) { const float x = At<float>(p[i], 0xA8); if (IsFin(x) && x > cfg.world_safe_sea_x) east = true; }
        }
        if (east && !g_seaHeld && *depth > 0.0f)
        {
            *depth = 0.0f; g_seaHeld = true;
            Log(">>> safe sea: a player is east of x %.0f - no deep-water shark (the game's depth %.2f is put back further west)\n",
                cfg.world_safe_sea_x, g_seaStock);
        }
        else if (!east && g_seaHeld)
        {
            SafeSeaRestore();
            Log(">>> safe sea: both players are west of x %.0f again - the deep water is deadly as in the stock game\n", cfg.world_safe_sea_x);
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
}

// ---------------------------------------------------------------------------------------------
//  Weather (weather.h, Notes 53): the engine asks for each view's post-effect settings once a frame;
//  we put the fog / rain of where that view's camera is into them and let the engine carry on. Its
//  own fault switch: a fault = no weather for the session, the game's own settings untouched from the
//  next frame (they are rebuilt from the level every frame, so nothing of ours can stay behind).
// ---------------------------------------------------------------------------------------------
static volatile bool g_weatherFault = false;
static int WeatherFault(EXCEPTION_POINTERS* ep)
{
    if (!g_weatherFault)
    {
        g_weatherFault = true;
        Log("!!! EXCEPTION 0x%08lX in the weather code at %p - no rain / fog from lcudrive for this session "
            "([weather] enable = 0 keeps it off).\n", ep->ExceptionRecord->ExceptionCode, ep->ExceptionRecord->ExceptionAddress);
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

static void RunWeatherView(void* view, uint8_t* settings)
{
    __try
    {
        const Config& cfg = g_cfgBuf[g_cfgIdx];
        wx::ViewFrame(view, settings, cfg, g_enabled && cfg.weather_enable != 0 && !br::Quiet(), GetTickCount());   // [backrooms1] && !Quiet
    }
    __except (WeatherFault(GetExceptionInformation())) {}
}

static void RunWeatherPublish(const Config& cfg)
{
    __try { wx::Publish(cfg, g_enabled && cfg.weather_enable != 0 && !br::Quiet(), GetTickCount(), br::Quiet()); }   // [backrooms1] && !Quiet
    __except (WeatherFault(GetExceptionInformation())) {}
}

static void RunWeatherClear()
{
    __try { wx::Clear(); }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
}

static uintptr_t __fastcall Hook_PostEval(void* mgr, void* view, uint8_t* settings)
{
    InterlockedIncrement(&g_inFlight);
    const wx::EvalFn orig = wx::g_origEval;
    if (!g_unhooked) gfx::Pre(view, settings);                  // gfx: the level's own fog, before the weather writes
    if (!g_unhooked && !g_weatherFault) RunWeatherView(view, settings);
    if (!g_unhooked) gfx::ViewFrame(view, settings, g_enabled);   // gfx: grade, AO, rim, draw distance, haze (Notes 57)
    const uintptr_t r = orig(mgr, view, settings);
    if (!g_unhooked) gfx2::Post(view, settings, g_enabled);      // [gfx2] after the evaluator: bloom, cube-map exposure, the frame counter
    InterlockedDecrement(&g_inFlight);
    return r;
}

static void __fastcall Hook_CamCopy(void* src, void* dst)
{
    InterlockedIncrement(&g_inFlight);
    gfx::g_origCamCopy(src, dst);                                // the engine's copy, as always
    if (!g_unhooked && !g_backroomsFault) br::PortalCam(dst, g_cfgBuf[g_cfgIdx]);   // [fix2] a connector crossing: the view stays with its player
    if (!g_unhooked) gfx::Cam(dst, g_enabled);                   // then the camera feel, on the copy only
    InterlockedDecrement(&g_inFlight);
}

#include "station.h"                    // [station1] "Return to Police Station" = outside, that player only (Notes 69; needs wd::, br::, g_backroomsFault)

static void __fastcall PMThunk(void* pm, void* info)
{
    InterlockedIncrement(&g_inFlight);
    const PMUpdateFn orig = g_origPM;
    orig(pm, info);
    rd::g_gameSeen = static_cast<LONG>(GetTickCount());     // the world is running: radio clocks too
    if (!g_unhooked && !g_wantedFault) RunWanted(pm, info, g_cfgBuf[g_cfgIdx]);
    else if (!g_unhooked) wd::FaultCleanup(pm);            // after a wanted-mode fault: what it had out goes back, once
    rd::g_busted = wd::g_bustCount;                        // (before the level: busted, not "got away")
    rd::g_heliNews = wd::g_heliNews;                       // a police helicopter has you in sight
    rd::g_wanted = wd::g_shown;                            // the higher of the two players' levels
    rd::g_levelNews = wd::g_levelNews;                     // ...and a level-up under it (co-op)
    // The suspect at sea: the wanted news's vehicle line says "boat" (fills.json's {vehicle} "boat",
    // rendered and never picked - no boat Update is hooked, so Where() never sees one). While he is,
    // Where() leaves the class alone (in co-op the other player's car would write its own back every
    // frame); on the way back to land the last vehicle is forgotten once, so the next vehicle frame
    // re-reads its own class.
    {
        static bool s_boat = false;
        const bool boat = wd::g_susBoat;
        if (boat) { static const LONG kBoatFill = rd::VehicleClass("NAVAL_"); rd::g_vehClass = kBoatFill; }
        if (boat != s_boat) { s_boat = boat; gsnd::g_whereVeh = nullptr; }
    }
    if (!g_unhooked && !lf::g_fault) RunLife(pm, info, g_cfgBuf[g_cfgIdx]);
    if (!g_unhooked && !vc::g_fault) RunVoice(pm, info, g_cfgBuf[g_cfgIdx]);
    // one spawner call per world update: the turn goes round the stand spawners that are running (spawn_turn.h)
    spt::g_turn = sitem::NextTurn(spt::g_turn, (!g_airportFault && !ap::g_released ? 1u << sitem::kTurnAirport : 0u) |
                                               (!g_dinoFault && !dn::g_released ? 1u << sitem::kTurnDino : 0u) |
                                               (!g_sitesFault && !si::g_released ? 1u << sitem::kTurnSites : 0u) |
                                               (!g_spotsFault && !sp::g_released ? 1u << sitem::kTurnSpots : 0u) |
                                               (!g_backroomsFault && br::Quiet() ? 1u << sitem::kTurnBackrooms : 0u) |   // [backrooms1]
                                               (!g_animalsFault && !an::g_released ? 1u << sitem::kTurnAnimals : 0u));   // [round3]
    if (!g_unhooked && !g_airportFault) RunAirport(pm, info, g_cfgBuf[g_cfgIdx]);
    else if (!g_unhooked && !ap::g_released) RunAirportRelease();
    if (!g_unhooked && !g_dinoFault) RunDino(pm, info, g_cfgBuf[g_cfgIdx]);
    else if (!g_unhooked && !dn::g_released) RunDinoRelease();
    if (!g_unhooked && !g_pteroFault) RunPtero(info, g_cfgBuf[g_cfgIdx]);      // after the T-Rex stands (theirs go first)
    else if (!g_unhooked && !pt::g_released) RunPteroRelease();
    if (!g_unhooked && !g_heliFault) ChuteFrame(g_cfgBuf[g_cfgIdx]);           // a parachute off a Pteranodon: no vehicle may be updating
    if (!g_unhooked && !g_sitesFault) RunSites(pm, info, g_cfgBuf[g_cfgIdx]);
    else if (!g_unhooked && !si::g_released) RunSitesRelease();
    if (!g_unhooked && !bt::g_fault) RunBoats(pm, info, g_cfgBuf[g_cfgIdx]);           // [boats2] after the stands / marinas (their boats are theirs)
    else if (!g_unhooked && !bt::g_restored) bt::Restore("the boats code faulted");
    if (!g_unhooked && !g_spotsFault) RunSpots(pm, info, g_cfgBuf[g_cfgIdx]);          // after the stands (its parked cars are theirs)
    else if (!g_unhooked && !sp::g_released) RunSpotsRelease();
    if (!g_unhooked && !g_animalsFault) RunAnimals(pm, info, g_cfgBuf[g_cfgIdx]);      // [round3] after the spots (one spawner turn each)
    else if (!g_unhooked && !an::g_released) RunAnimalsRelease();
    if (!g_unhooked && !npcg::g_fault) RunNpc(pm, info, g_cfgBuf[g_cfgIdx]);            // [round3] after life, voice, spots
    if (!g_unhooked && !g_backroomsFault) RunBackrooms(pm, info, g_cfgBuf[g_cfgIdx]);   // [backrooms1] after the spawners (their turn first)
    if (!g_unhooked && !st::g_fault) RunStation(pm, info);                                // [station1] the move outside the station
    else if (!g_unhooked) { static bool s_stRel = false; if (!s_stRel) { s_stRel = true; RunStationRelease(); } }   // [station1] after a fault, once
    gfx::g_handicam = (br::g_inZone && g_cfgBuf[g_cfgIdx].backrooms_handicam && g_cfgBuf[g_cfgIdx].backrooms_effects) ? 1.0f : 0.0f;   // [backrooms1] gfx camera feel steps aside
    if (!g_unhooked) RunGameSound(g_cfgBuf[g_cfgIdx]);
    if (!g_unhooked && !sh::g_fault) RunShark(pm, info, g_cfgBuf[g_cfgIdx]);   // before the safe sea
    if (!g_unhooked) RunSafeSea(g_cfgBuf[g_cfgIdx]);
    if (!g_unhooked && !g_weatherFault) RunWeatherPublish(g_cfgBuf[g_cfgIdx]);   // the shaders' rain / overcast / mist floats
    if (!g_unhooked) gfx::Publish(g_cfgBuf[g_cfgIdx], g_enabled);   // gfx: the lens block's floats (IniParams 10 / 11)
    if (!g_unhooked) gfx2::Publish(g_cfgBuf[g_cfgIdx], g_enabled);  // [gfx2] IniParams 12 .. 15, split-screen, the Auto governor
    if (!g_unhooked) rlg::Publish(g_cfgBuf[g_cfgIdx]);              // [radiologo1] the station logo (IniParams 16 / 17)
    if (!g_unhooked && !mis::g_fault) RunMissions(g_cfgBuf[g_cfgIdx]);   // [missions1] completions + the mission gate
    cg::Tick((GetTickCount64() - g_t0) / 1000.0);                   // [crashguard] say when a guard fired (also when unhooked)
    if (!g_unhooked) pw::Tick("world");                             // [stationfix] the streaming pool, last thing each update
    InterlockedDecrement(&g_inFlight);
}

// ---------------------------------------------------------------------------------------------
//  Street lamps (streetlamps.h): DynamicLightManager's message handler, vtable slot 13. On message
//  0x3a the engine hands its vehicle lamps to every view's light list (0x1405DC9D0); right after it
//  sl hands over the lamps near the players, through the same submit (0x140327EA0). Its own fault
//  switch: a fault = no street lamps for the session, nothing else changes.
// ---------------------------------------------------------------------------------------------
typedef void(__fastcall* LightMgrMsgFn)(void* mgr, void* ctx, int* msg, void* r9);
static LightMgrMsgFn g_origLightMgr = nullptr;
static void**        g_slotLightMgr = nullptr;

static int StreetLampFault(EXCEPTION_POINTERS* ep)
{
    if (!sl::g_fault)
    {
        sl::g_fault = true;
        Log("!!! EXCEPTION 0x%08lX in the street-lamp code at %p - no street lamps for this session; "
            "everything else is unaffected.\n", ep->ExceptionRecord->ExceptionCode, ep->ExceptionRecord->ExceptionAddress);
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

// Where a player is, for the lamps: their vehicle's body, else the character (ch+0xA8) - wanted.h YouPos.
static bool LampPos(void* veh, uint8_t* ch, float out[3])
{
    __try
    {
        uint8_t* inst = PtrOk(veh) ? At<uint8_t*>(veh, eng::VEH_INSTANCE) : nullptr;
        uint8_t* body = PtrOk(inst) ? At<uint8_t*>(inst, eng::INST_BODY) : nullptr;
        if (PtrOk(body)) for (int k = 0; k < 3; ++k) out[k] = At<float>(body, eng::BODY_POS + 4 * k);
        else if (PtrOk(ch)) for (int k = 0; k < 3; ++k) out[k] = At<float>(ch, 0xA8 + 4 * k);
        else return false;
        return IsFin(out[0]) && IsFin(out[1]) && IsFin(out[2]);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// What sl needs from the rest of lcudrive, once per 0x3a (game thread).
static void FillLampInputs(sl::Inputs& in)
{
    in.cityTick  = static_cast<uint32_t>(rd::g_gameSeen);   // PMThunk: cPopulationManager::Update ran - read
    in.now       = GetTickCount();                          // before now, so it is never the newer of the two
    in.clockOk   = vsim::BridgeLockedQ(g_bridge);       // the one day/night clock (lights.h UpdateNight's)
    in.clock     = vsim::BridgeClock(g_bridge);
    in.stock     = !g_enabled;                          // `[`
    in.cityKnown = g_slotPM != nullptr;
    in.paused    = *reinterpret_cast<const uint8_t*>(g_base + gsnd::RVA_PAUSED) != 0;
    uint8_t* p1 = *reinterpret_cast<uint8_t**>(g_base + eng::RVA_PLAYER1);
    if (!PtrOk(p1)) p1 = *reinterpret_cast<uint8_t**>(g_base + eng::RVA_PLAYERS);
    in.p1 = PtrOk(p1) && LampPos(PlayerVehicle(), p1, in.p1pos);
    uint8_t* p2 = Player2Char();
    in.p2 = p2 && LampPos(Player2Vehicle(), p2, in.p2pos);
}

static void RunStreetLampMsg(void* mgr, int* msg, const Config& cfg)
{
    __try { sl::OnMessage(mgr, msg, GetTickCount(), &FillLampInputs, cfg); }
    __except (StreetLampFault(GetExceptionInformation()))
    {
        __try { sl::FaultOff(); } __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
}

static void __fastcall LightMgrThunk(void* mgr, void* ctx, int* msg, void* r9)
{
    InterlockedIncrement(&g_inFlight);
    g_origLightMgr(mgr, ctx, msg, r9);                  // the engine's lights first: the headlights keep
                                                        // their place under the 128-per-view cap
    if (!g_unhooked && !sl::g_fault) RunStreetLampMsg(mgr, msg, g_cfgBuf[g_cfgIdx]);
    InterlockedDecrement(&g_inFlight);
}

// ---------------------------------------------------------------------------------------------
//  The jetpack: the one call to UpdateAstroJetPack (jetpack.h, Notes\30).
// ---------------------------------------------------------------------------------------------
static int JetFault(EXCEPTION_POINTERS* ep)
{
    if (!g_jetFault)
    {
        g_jetFault = true;
        Log("!!! EXCEPTION 0x%08lX in the jetpack code at %p - the jetpack is stock for this session.\n",
            ep->ExceptionRecord->ExceptionCode, ep->ExceptionRecord->ExceptionAddress);
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

static void* RunJetPre(void* ch, const Config& cfg)
{
    __try { return jp::Pre(ch, cfg); }
    __except (JetFault(GetExceptionInformation())) { return reinterpret_cast<void*>(1); }
}

static void RunJetPost(void* ch, const Config& cfg, void* before)
{
    __try { jp::Post(ch, cfg, before); }
    __except (JetFault(GetExceptionInformation())) {}
}

static uint64_t __fastcall Hook_Jetpack(void* ch, uint64_t held)
{
    InterlockedIncrement(&g_inFlight);
    const Config& cfg = g_cfgBuf[g_cfgIdx];
    const bool on = !g_unhooked && g_enabled && !g_jetFault && cfg.jp_enable && PtrOk(ch);
    void* before = on ? RunJetPre(ch, cfg) : reinterpret_cast<void*>(1);
    const uint64_t r = jp::g_orig(ch, held);
    if (on) RunJetPost(ch, cfg, before);
    InterlockedDecrement(&g_inFlight);
    return r;
}

// The hooked HINT_HELI_NOEXIT call (Notes\26): the stub hands us the pilot's user data (rdi)
// and character (rsi) too. Handled = no hint; otherwise the game's own "can't get out here".
static bool RunOffPadExit(uint8_t* ud, uint8_t* ch)
{
    __try
    {
        if (g_unhooked || !g_enabled || g_heliFault) return false;
        return hl::OffPadExit(ud, ch, g_cfgBuf[g_cfgIdx]);
    }
    __except (HeliFault(GetExceptionInformation())) { return false; }
}

static void __fastcall Hook_NoExitHint(int id, uint8_t* ud, uint8_t* ch)
{
    InterlockedIncrement(&g_inFlight);
    bool handled;
    { AsP2 as(PtrOk(ch) && ch == Player2Char()); handled = RunOffPadExit(ud, ch); }
    InterlockedDecrement(&g_inFlight);
    if (!handled) hl::g_hintFlag(id);
}

// ---------------------------------------------------------------------------------------------
//  Install / remove
// ---------------------------------------------------------------------------------------------
static bool ImageMatches()
{
    __try
    {
        const IMAGE_DOS_HEADER* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(g_base);
        const IMAGE_NT_HEADERS* nt  = reinterpret_cast<const IMAGE_NT_HEADERS*>(g_base + dos->e_lfanew);
        Log("image base %p  TimeDateStamp 0x%08X  SizeOfImage 0x%X\n", (void*)g_base,
            nt->FileHeader.TimeDateStamp, nt->OptionalHeader.SizeOfImage);
        return nt->FileHeader.TimeDateStamp == eng::kImageStamp &&
               nt->OptionalHeader.SizeOfImage == eng::kImageSize;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

static bool HookUpdate()
{
    void** vt = reinterpret_cast<void**>(g_base + eng::RVA_VT_CAR);
    void** slot = &vt[eng::SLOT_UPDATE];
    const uintptr_t cur = reinterpret_cast<uintptr_t>(*slot);
    if (cur < g_base || cur >= g_base + eng::kImageSize)
    {
        Log("REFUSING TO HOOK: car vtable slot 91 holds %p, outside the image. Another mod "
            "(vehsim?) already hooked it.\n", (void*)cur);
        return false;
    }
    DWORD old = 0;
    if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &old)) return false;
    g_origCar = reinterpret_cast<UpdateFn>(*slot);
    g_slotCar = slot;
    *slot = reinterpret_cast<void*>(&CarThunk);
    VirtualProtect(slot, sizeof(void*), old, &old);
    Log("hooked ApiVehicleCar::Update  vt=%p orig=%p (rva 0x%llX)\n", (void*)vt, (void*)g_origCar,
        (unsigned long long)(reinterpret_cast<uintptr_t>(g_origCar) - g_base));
    return true;
}

// The helicopter hook is optional: if it cannot be installed, cars are unaffected.
static void HookHover()
{
    void** vt = reinterpret_cast<void**>(g_base + eng::RVA_VT_HOVER);
    void** slot = &vt[eng::SLOT_UPDATE];
    const uintptr_t cur = reinterpret_cast<uintptr_t>(*slot);
    if (cur != g_base + 0x4AEA60)
    {
        Log("helicopter hook NOT installed: hover vtable slot 91 holds %p, not ApiVehicleHover::Update "
            "(another mod, or a different build). Helicopters stay stock.\n", (void*)cur);
        return;
    }
    DWORD old = 0;
    if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &old)) return;
    g_origHover = reinterpret_cast<UpdateFn>(*slot);
    g_slotHover = slot;
    *slot = reinterpret_cast<void*>(&HoverThunk);
    VirtualProtect(slot, sizeof(void*), old, &old);
    Log("hooked ApiVehicleHover::Update vt=%p orig=%p\n", (void*)vt, (void*)g_origHover);
}

// The wanted-mode hook is optional too.
static void HookPM()
{
    void** vt = reinterpret_cast<void**>(g_base + wd::RVA_VT_PM);
    void** slot = &vt[13];
    const uintptr_t cur = reinterpret_cast<uintptr_t>(*slot);
    if (cur != g_base + wd::FN_PM_UPDATE)
    {
        Log("wanted-mode hook NOT installed: population manager slot 13 holds %p (another mod, or a "
            "different build). No wanted mode.\n", (void*)cur);
        return;
    }
    DWORD old = 0;
    if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &old)) return;
    g_origPM = reinterpret_cast<PMUpdateFn>(*slot);
    g_slotPM = slot;
    *slot = reinterpret_cast<void*>(&PMThunk);
    VirtualProtect(slot, sizeof(void*), old, &old);
    Log("hooked cPopulationManager::Update vt=%p orig=%p\n", (void*)vt, (void*)g_origPM);
}

// A vtable slot of the game's: replaced only if it holds exactly the function we expect (another
// mod, another build: left alone and said so). *orig is set BEFORE the slot is written.
static void** HookSlot(uintptr_t slotRva, uintptr_t fnRva, void* hook, void** orig, const char* name)
{
    void** slot = reinterpret_cast<void**>(g_base + slotRva);
    bool ok = false;
    __try
    {
        const uintptr_t cur = reinterpret_cast<uintptr_t>(*slot);
        DWORD old = 0;
        if (cur != g_base + fnRva)
            Log("  slot  %-24s REFUSED: rva 0x%llX holds rva 0x%llX, expected 0x%llX\n", name,
                (unsigned long long)slotRva, (unsigned long long)(cur - g_base), (unsigned long long)fnRva);
        else if (VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &old))
        {
            *orig = *slot;
            InterlockedExchangePointer(slot, hook);
            VirtualProtect(slot, sizeof(void*), old, &old);
            ok = true;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { ok = false; }
    return ok ? slot : nullptr;
}

static void UnhookSlot(void**& slot, void* orig)
{
    DWORD old = 0;
    if (slot && orig && VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &old))
    {
        InterlockedExchangePointer(slot, orig);
        VirtualProtect(slot, sizeof(void*), old, &old);
    }
    slot = nullptr;
}

// Wanted mode's crime signals: the call sites (verified before writing, refused one by one) and
// GameObjects::OnCharacterDamage. All optional: what can't be hooked just isn't a crime.
static void HookCrimes()
{
    int ok = 0;
    for (CallPatch& p : g_crimePatches)
    {
        p.replacement = p.calleeRva == wd::FN_KRAWLIE_HIT ? reinterpret_cast<void*>(&Hook_KrawlieHit)
                                                          : reinterpret_cast<void*>(&Hook_EventFire);
        if (ApplyPatch(g_base, p)) ++ok;
    }
    void** slot = reinterpret_cast<void**>(g_base + wd::RVA_SLOT_ONDAMAGE);
    const uintptr_t cur = reinterpret_cast<uintptr_t>(*slot);
    DWORD old = 0;
    if (cur == g_base + wd::FN_ONDAMAGE && VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &old))
    {
        g_origOnDamage = reinterpret_cast<OnDamageFn>(*slot);
        g_slotOnDamage = slot;
        *slot = reinterpret_cast<void*>(&Hook_OnDamage);
        VirtualProtect(slot, sizeof(void*), old, &old);
    }
    Log("wanted-mode crimes: %d of %d call sites, character hits %s\n", ok,
        (int)(sizeof(g_crimePatches) / sizeof(g_crimePatches[0])), g_slotOnDamage ? "hooked" : "NOT hooked");

    // the ray gun: three more call sites and two more slots, each on its own
    int shots = 0;
    for (CallPatch& p : g_shotPatches)
    {
        p.replacement = p.calleeRva == wd::FN_KRAWLIE_HIT ? reinterpret_cast<void*>(&Hook_KrawlieShot)
                                                          : reinterpret_cast<void*>(&Hook_PropBoltHit);
        if (ApplyPatch(g_base, p)) ++shots;
    }
    g_slotBlowupBolt = HookSlot(wd::RVA_SLOT_BLOWUP_BOLT, wd::FN_BLOWUP_BOLT, reinterpret_cast<void*>(&Hook_BlowupBolt),
                                reinterpret_cast<void**>(&g_origBlowupBolt), "ray gun vs LEGO objects");
    g_slotBoltVehicle = HookSlot(wd::RVA_SLOT_BOLT_VEHICLE, wd::FN_BOLT_VEHICLE, reinterpret_cast<void*>(&Hook_BoltVehicle),
                                 reinterpret_cast<void**>(&g_origBoltVehicle), "ray gun vs vehicles");
    Log("wanted-mode crimes, the ray gun: %d of %d call sites (pedestrians x2, street props), LEGO objects %s, "
        "vehicles %s\n", shots, (int)(sizeof(g_shotPatches) / sizeof(g_shotPatches[0])),
        g_slotBlowupBolt ? "hooked" : "NOT hooked", g_slotBoltVehicle ? "hooked" : "NOT hooked");
}

// The motorbike hook is optional too: if it cannot be installed, bikes are stock.
static void HookBike()
{
    void** vt = reinterpret_cast<void**>(g_base + eng::RVA_VT_BIKE);
    void** slot = &vt[eng::SLOT_UPDATE];
    const uintptr_t cur = reinterpret_cast<uintptr_t>(*slot);
    if (cur != g_base + eng::FN_BIKE_UPDATE)
    {
        Log("motorbike hook NOT installed: bike vtable slot 91 holds %p, not ApiVehicleBike::Update "
            "(another mod, or a different build). Motorbikes stay stock.\n", (void*)cur);
        return;
    }
    DWORD old = 0;
    if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &old)) return;
    g_origBike = reinterpret_cast<UpdateFn>(*slot);
    g_slotBike = slot;
    *slot = reinterpret_cast<void*>(&BikeThunk);
    VirtualProtect(slot, sizeof(void*), old, &old);
    Log("hooked ApiVehicleBike::Update  vt=%p orig=%p\n", (void*)vt, (void*)g_origBike);
}

// The street lamps' hook is optional too: if it cannot be installed, the city has no lamp light.
static void HookLightMgr()
{
    void** vt = reinterpret_cast<void**>(g_base + sl::RVA_VT_LIGHTMGR);
    void** slot = &vt[sl::SLOT_MSG];
    const uintptr_t cur = reinterpret_cast<uintptr_t>(*slot);
    if (cur != g_base + sl::FN_LIGHTMGR_MSG)
    {
        Log(">>> lamps: hook NOT installed: DynamicLightManager slot 13 holds %p, not its message handler "
            "(another mod, or a different build). No street lamps.\n", (void*)cur);
        return;
    }
    DWORD old = 0;
    if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &old)) return;
    g_origLightMgr = reinterpret_cast<LightMgrMsgFn>(*slot);
    g_slotLightMgr = slot;
    *slot = reinterpret_cast<void*>(&LightMgrThunk);
    VirtualProtect(slot, sizeof(void*), old, &old);
    Log(">>> lamps: hooked DynamicLightManager message handler vt=%p orig=%p; %d lamps in the table "
        "(%d fixtures), checksum %08X\n", (void*)vt, (void*)g_origLightMgr, sl::kLampCount, sl::kLampFixtures,
        sl::kLampChecksum);
}

static bool InstallPatches()
{
    g_patches[0].replacement = reinterpret_cast<void*>(&Hook_Steer);
    g_patches[1].replacement = reinterpret_cast<void*>(&Hook_Spin);
    g_patches[2].replacement = reinterpret_cast<void*>(&Hook_Drag);
    g_patches[3].replacement = reinterpret_cast<void*>(&Hook_Constraint);
    bool all = true;
    for (CallPatch& p : g_patches) all = ApplyPatch(g_base, p) && all;
    if (!all)
    {
        for (CallPatch& p : g_patches) RemovePatch(g_base, p);
        Log("call patches NOT all installed - lcudrive stays observe-only (stock handling).\n");
    }
    return all;
}

static void Unhook()
{
    if (g_unhooked || !g_slotCar) return;
    pmn::HideRow();               // the pause menu stops offering Mod Options first
    SafeSeaRestore();             // the sea's sharks are the game's again
    bt::Restore("unhook");        // [boats2] the boat map's spawn spacing is the game's again
    sh::Uninstall();              // ...and the deep-water timer is the game's again
    autotest::Stop("unhook");
    // the cars give their lamps and body fields back in their own Update (game thread), then we go
    lights::g_release = true;
    g_restoreReq = g_restoreReq2 = true;
    hl::g_releaseReq = true;      // helicopters: free-flight patches off, boundary bits back
    fw::g_releaseReq = true;      // aeroplanes: their definition back (in their own Update), then stock
    ap::g_stopReq = true;         // the airport plane: pin off, faded unless someone is aboard (next PM update)
    dn::g_stopReq = true;         // the airport T-Rexes: removed unless a player is on or beside one (next PM update)
    pt::g_stopReq = true;         // the Pteranodons: our hands off, bodies upright (next PM update)
    si::g_stopReq = true;         // the [sites] stands: faded unless a player is in or beside one (next PM update)
    sp::g_stopReq = true;         // life spots: the people go, a route's car fades unless a player is in or beside it
    an::g_stopReq = true;         // [round3] animals: every one goes (next PM update)
    npcg::g_stopReq = true;       // [round3] NPC voices: our banks unloaded, lcudrive's own audio stopped, life's hooks off
    wd::g_stopReq = true;         // wanted mode: the police go back to traffic (next PM update)
    lf::g_stopReq = true;         // ambient life: everyone in a scene walks off
    vc::g_stopReq = true;         // voice lines: the one playing stopped, our manifests unloaded
    sl::g_stopReq = true;         // street lamps: dark, nothing handed to the views from the next 0x3a
    br::g_stopReq = true;         // [backrooms1] the Backrooms: kill height back, statues gone (next PM update)
    st::g_stopReq = true;         // [station1] a move outside the station under way: let go (next PM update)
    Sleep(150);
    g_unhooked = true;
    g_modelArmed = false;
    for (int i = 0; i < 200 && g_inFlight > 0; ++i) Sleep(5);
    pmn::RemoveAll();             // its row handlers back to the stubs; open pages back out
    for (CallPatch& p : g_patches) RemovePatch(g_base, p);
    RemovePatch(g_base, g_exitPatch);
    RemovePatch(g_base, g_jetPatch);
    RemovePatch(g_base, g_pteroPatch);
    RemovePatch(g_base, g_weatherPatch);
    RunWeatherClear();            // the shaders' weather floats back to 0 (the engine's settings are rebuilt every frame)
    RemovePatch(g_base, g_gfxCamPatch);
    gfx::Clear();                 // the lens block's floats back to 0
    gfx2::Clear();                // [gfx2] and 12 .. 15
    RunBackroomsRelease();        // [backrooms1] whatever the game thread did not get to (paused): kill height, statues, floats
    for (CallPatch& p : g_crimePatches) RemovePatch(g_base, p);
    for (CallPatch& p : g_shotPatches) RemovePatch(g_base, p);
    ml::Uninstall(g_base);       // [labels1] the map's location line and pin lookup back to the game's
    st::Uninstall(g_base);       // [station1] "Return to Police Station" back to the game's own jmp
    UnhookSlot(g_slotBlowupBolt, reinterpret_cast<void*>(g_origBlowupBolt));
    UnhookSlot(g_slotBoltVehicle, reinterpret_cast<void*>(g_origBoltVehicle));
    FreeFlightOff();
    DWORD old = 0;
    if (g_slotOnDamage && VirtualProtect(g_slotOnDamage, sizeof(void*), PAGE_READWRITE, &old))
    {
        *g_slotOnDamage = reinterpret_cast<void*>(g_origOnDamage);
        VirtualProtect(g_slotOnDamage, sizeof(void*), old, &old);
    }
    if (VirtualProtect(g_slotCar, sizeof(void*), PAGE_READWRITE, &old))
    {
        *g_slotCar = reinterpret_cast<void*>(g_origCar);
        VirtualProtect(g_slotCar, sizeof(void*), old, &old);
    }
    if (g_slotHover && VirtualProtect(g_slotHover, sizeof(void*), PAGE_READWRITE, &old))
    {
        *g_slotHover = reinterpret_cast<void*>(g_origHover);
        VirtualProtect(g_slotHover, sizeof(void*), old, &old);
    }
    if (g_slotBike && VirtualProtect(g_slotBike, sizeof(void*), PAGE_READWRITE, &old))
    {
        *g_slotBike = reinterpret_cast<void*>(g_origBike);
        VirtualProtect(g_slotBike, sizeof(void*), old, &old);
    }
    if (g_slotPM && VirtualProtect(g_slotPM, sizeof(void*), PAGE_READWRITE, &old))
    {
        *g_slotPM = reinterpret_cast<void*>(g_origPM);
        VirtualProtect(g_slotPM, sizeof(void*), old, &old);
    }
    if (g_slotLightMgr && VirtualProtect(g_slotLightMgr, sizeof(void*), PAGE_READWRITE, &old))
    {
        *g_slotLightMgr = reinterpret_cast<void*>(g_origLightMgr);
        VirtualProtect(g_slotLightMgr, sizeof(void*), old, &old);
    }
    for (int i = 0; i < 200 && g_inFlight > 0; ++i) Sleep(5);
    gsnd::Release();              // the game's music back
    rd::Stop();                   // the radio goes quiet too
    Log(">>> unhooked; the game is stock until the next launch\n");
}

// ---------------------------------------------------------------------------------------------
//  Config hot reload (parsed on this thread, published by index swap)
// ---------------------------------------------------------------------------------------------
static char     g_iniPath[MAX_PATH] = {};
static FILETIME g_iniTime = {};

static bool IniChanged()
{
    WIN32_FILE_ATTRIBUTE_DATA fa;
    if (!GetFileAttributesExA(g_iniPath, GetFileExInfoStandard, &fa)) return false;
    if (CompareFileTime(&fa.ftLastWriteTime, &g_iniTime) == 0) return false;
    g_iniTime = fa.ftLastWriteTime;
    return true;
}

// The radio's settings, with its paths made absolute (relative ones are from this DLL's folder).
static void ApplyRadioSettings(const Config& c)
{
    rd::Settings s;
    s.enable = c.radio_enable; s.volume = c.radio_volume; s.inHeli = c.radio_in_heli;
    s.padNext = static_cast<uint32_t>(c.radio_pad_next); s.padPrev = static_cast<uint32_t>(c.radio_pad_prev);
    s.duckDb = c.radio_duck_db; s.staticDb = c.radio_static_db; s.startStation = c.radio_start_station;
    strcpy_s(s.breaking, c.radio_breaking); s.breakingGap = c.radio_breaking_gap;
    s.missionNews = c.radio_mission_news; s.missionInterrupt = c.radio_mission_interrupt;        // [missions1]
    strcpy_s(s.missionStations, c.radio_mission_stations);
    s.missionKeepH = c.radio_mission_keep_h; s.missionRecapChance = c.radio_mission_recap_chance; s.missionRecapH = c.radio_mission_recap_h;
    s.missionRecapGapMin = c.radio_mission_recap_gap; s.missionRecapMax = c.radio_mission_recap_max;
    s.missionRecapReuse = c.radio_mission_recap_reuse; s.missionOnce = c.radio_mission_once;
    _snprintf_s(s.missionState, sizeof(s.missionState), _TRUNCATE, "%slcudrive_missionnews.txt", g_dir);
    auto absolute = [](const char* p, char* out, size_t cap) {
        out[0] = 0;
        if (!p[0]) return;
        char joined[MAX_PATH * 2];
        const bool abs = (p[0] && p[1] == ':') || (p[0] == '\\' && p[1] == '\\');
        _snprintf_s(joined, sizeof(joined), _TRUNCATE, "%s%s", abs ? "" : g_dir, p);
        if (!GetFullPathNameA(joined, static_cast<DWORD>(cap), out, nullptr)) strcpy_s(out, cap, joined);
    };
    absolute(c.radio_manifest, s.manifest, sizeof(s.manifest));
    // [release1] the public layout keeps the radio beside the DLL (<game>\LCUMod\Radio): when the configured manifest
    // is not there, fall back to Radio\radio_runtime.tsv in this DLL's folder.
    if (s.manifest[0] && GetFileAttributesA(s.manifest) == INVALID_FILE_ATTRIBUTES)
    {
        char alt[MAX_PATH];
        absolute("Radio\\radio_runtime.tsv", alt, sizeof(alt));
        if (GetFileAttributesA(alt) != INVALID_FILE_ATTRIBUTES) strcpy_s(s.manifest, sizeof(s.manifest), alt);
    }
    absolute(c.radio_root, s.root, sizeof(s.root));
    absolute("radio_provisional.tsv", s.fallback, sizeof(s.fallback));
    // the music: UTF-8 paths made absolute and wide (relative ones from this DLL's folder, %VARS% expanded)
    auto wabs = [](const char* p8, wchar_t* out, size_t cap) {
        out[0] = 0;
        if (!p8[0]) return;
        wchar_t raw[MAX_PATH * 2] = {}, exp[MAX_PATH * 2] = {}, dir[MAX_PATH] = {}, joined[MAX_PATH * 3] = {};
        MultiByteToWideChar(CP_UTF8, 0, p8, -1, raw, MAX_PATH * 2);
        if (!ExpandEnvironmentStringsW(raw, exp, MAX_PATH * 2)) wcscpy_s(exp, raw);
        const bool abs = (exp[0] && exp[1] == L':') || (exp[0] == L'\\' && exp[1] == L'\\');
        MultiByteToWideChar(CP_ACP, 0, g_dir, -1, dir, MAX_PATH);
        _snwprintf_s(joined, _countof(joined), _TRUNCATE, L"%s%s", abs ? L"" : dir, exp);
        if (!GetFullPathNameW(joined, static_cast<DWORD>(cap), out, nullptr)) wcsncpy_s(out, cap, joined, _TRUNCATE);
        for (size_t n = wcslen(out); n > 3 && (out[n - 1] == L'\\' || out[n - 1] == L'/'); --n) out[n - 1] = 0;
    };
    s.music = radio::SourceFromName(c.radio_music, radio::SRC_AI);
    if (s.music == 0xFF) { Log("!!! radio: music = %s? (ai, library, service or talk) - using ai\n", c.radio_music); s.music = radio::SRC_AI; }
    for (int i = 0; i < c.radio_station_n && s.nStn < 16; ++i)
    {
        const RadioStationKeys& k = c.radio_station[i];
        rd::StationKeys& o = s.stn[s.nStn++];
        strcpy_s(o.id, k.id);
        o.music = k.music[0] ? radio::SourceFromName(k.music, 0xFF) : static_cast<uint8_t>(0xFF);
        if (k.music[0] && o.music == 0xFF) Log("!!! radio: music_%s = %s? (ai, library, service or talk) - using `music`\n", k.id, k.music);
        strcpy_s(o.playlist, k.playlist);
        o.form = radio::PlaylistForm(k.playlist);
        if (o.form == radio::PL_FOLDER || o.form == radio::PL_M3U) wabs(k.playlist, o.list, MAX_PATH);
        if (o.form == radio::PL_BAD) Log("!!! radio: playlist_%s = %s: not a folder, an .m3u or a spotify: / https:// link - ignored\n", k.id, k.playlist);
        strcpy_s(o.genre, k.genre);
    }
    for (const char* p = c.radio_lib_folders; *p && s.nLibFolders < 8;)
    {
        const char* e = strchr(p, '|');
        if (!e) e = p + strlen(p);
        char one[MAX_PATH * 2] = {};
        size_t n = static_cast<size_t>(e - p) < sizeof(one) - 1 ? static_cast<size_t>(e - p) : sizeof(one) - 1;
        memcpy(one, p, n);
        while (n > 0 && (one[n - 1] == ' ' || one[n - 1] == '\t')) one[--n] = 0;
        const char* q = one;
        while (*q == ' ' || *q == '\t') ++q;
        if (*q) wabs(q, s.libFolders[s.nLibFolders++], MAX_PATH);
        p = *e ? e + 1 : e;
    }
    s.libMin = c.radio_lib_min;
    s.libShortAi = _stricmp(c.radio_lib_short, "ai") == 0 ? 1 : 0;
    strcpy_s(s.libSkip, c.radio_lib_skip);
    s.libHoliday = _stricmp(c.radio_lib_holiday, "always") == 0 ? 1 : (_stricmp(c.radio_lib_holiday, "never") == 0 ? 2 : 0);
    strcpy_s(s.libUnsorted, c.radio_lib_unsorted);
    s.libTalkover = c.radio_lib_talkover;
    wabs(c.radio_lib_cache, s.libCache, MAX_PATH);
    strcpy_s(s.svcApp, c.radio_svc_app);
    s.svcVolume = c.radio_svc_volume; s.svcVolCtl = c.radio_svc_volctl; s.svcLead = c.radio_svc_lead;
    s.svcMaxMin = c.radio_svc_max_min; s.svcOpenTune = c.radio_svc_open_tune;
    rd::Configure(s);
}

static void ReloadConfig()
{
    const LONG next = 1 - g_cfgIdx;
    LoadConfig(g_iniPath, g_cfgRaw[next]);                   // [missions1] the raw ini...
    g_cfgBuf[next] = g_cfgRaw[next];
    msm::MaskConfig(g_cfgBuf[next], g_maskApplied);          // ...and the live one: the mission mask on it
    InterlockedExchange(&g_beepsOn, g_cfgBuf[next].beeps);
    ApplyRadioSettings(g_cfgBuf[next]);
    cc::Apply(g_cfgBuf[next]);
    ++pl::g_kcGen;                               // [plane] models may have changed
    ++fw::g_kcGen;                               // ... and [airplane] models
    InterlockedExchange(&g_cfgIdx, next);
    g_restoreReq = g_restoreReq2 = true;          // re-derive the car with the new numbers on the next frame
    Log(">>> config reloaded%s%s\n", g_maskApplied ? " - a mission runs, suspended: " : "", g_maskApplied ? msm::MaskText(g_maskApplied) : "");
    LogConfig(g_cfgRaw[next]);
}

// [missions1] "Missions: stock game": the live Config becomes the raw one with the mask the game thread asks for
// (mis::g_want). Hotkey thread only (as ReloadConfig, so the two never race). The car is re-derived when the
// vehicle bit changes (mis::Tick only flips it while the car is safe to hand over).
static void ApplyMissionMask(uint8_t want)
{
    if (want == g_maskApplied) return;
    const uint8_t was = g_maskApplied;
    const LONG cur = g_cfgIdx, next = 1 - cur;
    g_cfgRaw[next] = g_cfgRaw[cur];
    g_cfgBuf[next] = g_cfgRaw[next];
    msm::MaskConfig(g_cfgBuf[next], want);
    cc::Apply(g_cfgBuf[next]);                   // split cut-scenes
    ++pl::g_kcGen; ++fw::g_kcGen;
    g_maskApplied = want;
    InterlockedExchange(&g_cfgIdx, next);
    if ((was ^ want) & msm::M_VEH) g_restoreReq = g_restoreReq2 = true;   // the car: stock values back / re-derived
    Log(">>> missions: the mod's %s %s (Missions: stock game)\n", msm::MaskText(static_cast<uint8_t>(was ^ want)),
        (want & ~was) ? "suspended for the mission" : "back");
}

// ---------------------------------------------------------------------------------------------
//  Hotkeys. Focus-gated except the panic keys. Every key is read every poll so a stale
//  "pressed since last call" bit can never fire later.
// ---------------------------------------------------------------------------------------------
static bool GameHasFocus()
{
    HWND fg = GetForegroundWindow();
    DWORD pid = 0;
    if (fg) GetWindowThreadProcessId(fg, &pid);
    return pid == GetCurrentProcessId();
}

static bool Pressed(int vk) { return (GetAsyncKeyState(vk) & 1) != 0; }
static bool Down(int vk)    { return (GetAsyncKeyState(vk) & 0x8000) != 0; }

// The game's window. Once it is gone (the game closing) the radio gives the music app back at once:
// ExitProcess may come before the radio thread's own end (if it does, the ledger restores the app's
// volume on the next start; its play state can't be restored then).
static HWND g_gameWnd = nullptr;

static BOOL CALLBACK FindGameWindow(HWND h, LPARAM lp)
{
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    if (pid != GetCurrentProcessId() || !IsWindowVisible(h)) return TRUE;
    RECT r;
    if (!GetWindowRect(h, &r) || r.right - r.left <= 0 || r.bottom - r.top <= 0) return TRUE;
    *reinterpret_cast<HWND*>(lp) = h;
    return FALSE;
}

// A window counts once it has been there 10 s (a splash or launcher window that closes sooner is
// forgotten, not taken for the game closing). After a release the watcher keeps looking: a game
// window that shows up again (re-created, not closed) has the radio take the app again.
static void WatchGameWindow()
{
    static DWORD s_scan = 0, s_gone = 0, s_seen = 0;
    static bool s_released = false;
    const DWORD nowT = GetTickCount();
    if (!g_gameWnd || s_released)
    {
        if (nowT - s_scan < 2000) return;
        s_scan = nowT;
        HWND h = nullptr;
        HWND fg = GetForegroundWindow();                  // the game's own window when it has focus
        DWORD pid = 0;
        if (fg) GetWindowThreadProcessId(fg, &pid);
        if (fg && pid == GetCurrentProcessId() && IsWindowVisible(fg)) h = fg;
        else EnumWindows(FindGameWindow, reinterpret_cast<LPARAM>(&h));
        if (!h) return;
        g_gameWnd = h; s_seen = nowT; s_gone = 0;
        if (s_released)
        {
            s_released = false;
            Log(">>> a game window is there again - the radio takes the music app again\n");
            rd::RetakeApp();
        }
        return;
    }
    if (IsWindow(g_gameWnd)) { s_gone = 0; return; }
    if (nowT - s_seen < 10000) { g_gameWnd = nullptr; s_gone = 0; return; }   // short-lived: look again
    if (!s_gone) { s_gone = nowT; return; }
    if (nowT - s_gone < 500) return;
    HWND h = nullptr;
    EnumWindows(FindGameWindow, reinterpret_cast<LPARAM>(&h));
    if (h) { g_gameWnd = h; s_seen = nowT; s_gone = 0; return; }       // the game made itself a new window
    s_released = true;
    s_scan = nowT;
    Log(">>> the game window is gone - the radio gives the music app back\n");
    rd::ReleaseApp();
}

// ] / [ (and Mod Effects in the pause menu): lcudrive on, or stock handling, for this session.
static void SetMode(bool on)
{
    if (on)
    {
        g_faulted = false;
        g_enabled = true;
        Log(">>> ] lcudrive ON%s\n", g_modelArmed ? "" : " (call patches missing: observe-only)");
        Beep2(true);
    }
    else
    {
        g_enabled = false;
        g_restoreReq = g_restoreReq2 = true;
        Log(">>> [ stock handling\n");
        Beep2(false);
    }
}

// Mod Options (pausemenu.h, menu_bridge.h), once per pass, before the focus gate: a due save written and
// published (the pass's only ReloadConfig), then Mod Effects, Reload and Switch Off (deferred until the
// game runs again) - see mbr::Pass.
struct MenuPassHooks : mbr::PassHooks
{
    void IniChanged() override { ::IniChanged(); }
    void Reload() override { ReloadConfig(); }
    void SetMode(bool on) override { ::SetMode(on); }
};
static mbr::PassResult MenuBridgePass()
{
    MenuPassHooks h;
    const uint8_t paused = *reinterpret_cast<const volatile uint8_t*>(g_base + gsnd::RVA_PAUSED);
    return mbr::Pass(g_iniPath, paused, GetTickCount(), h, &Log);
}

static DWORD WINAPI HotkeyThread(LPVOID)
{
    uint32_t lastIniPoll = GetTickCount();
    for (;;)
    {
        Sleep(15);
        const bool ctrlAlt = Down(VK_CONTROL) && Down(VK_MENU);
        const bool pPause = Pressed(VK_PAUSE);
        const bool pU     = Pressed('U');
        const bool pOn    = Pressed(VK_OEM_6);   // ]
        const bool pOff   = Pressed(VK_OEM_4);   // [
        const bool pR     = Pressed('R');
        const bool pT     = Pressed('T');
        const bool pP     = Pressed('P');
        const bool pO     = Pressed('O');
        const bool pC     = Pressed('C');
        const bool pL     = Pressed('L');
        const bool pF     = Pressed('F');   // Ctrl+Alt+F: the airport plane back on the runway (airport.h)
        const bool pNext  = Pressed(VK_OEM_PERIOD);  // .  radio: next station
        const bool pPrev  = Pressed(VK_OEM_COMMA);   // ,  radio: previous station
        const bool pSlash = Pressed(VK_OEM_2);       // /  radio: the AI songs <-> your music (this session)
        const bool pSemi  = Pressed(VK_OEM_1);       // ;  radio: the station's playlist in your music app
        const bool pB     = Pressed('B');            // Ctrl+Alt+B: a krawlie dance party round player 1 (life.h)

        if (pPause || (ctrlAlt && pU)) { Unhook(); Beep(330, 120); Beep(220, 200); return 0; }

        ModelHotkeyThreadTick();   // flushes telemetry
        __try { wx::Watch(GetTickCount()); } __except (EXCEPTION_EXECUTE_HANDLER) {}   // no rain over loading screens
        { static DWORD s_scan = 0; const DWORD nowT = GetTickCount(); if (nowT - s_scan > 2000) { s_scan = nowT; pad::Scan(); } }
        const bool focus = GameHasFocus();
        autotest::Tick(GetTickCount(), focus, g_frame);
        if (focus && ctrlAlt && pP)
            autotest::Start(autotest::kStraight, (int)(sizeof(autotest::kStraight) / sizeof(autotest::kStraight[0])), "straight line");
        if (focus && ctrlAlt && pO)
            autotest::Start(autotest::kHandling, (int)(sizeof(autotest::kHandling) / sizeof(autotest::kHandling[0])), "handling");
        if (focus && ctrlAlt && pC)
            autotest::Start(autotest::kCruise, 1, "cruise 5 u/s (steer with A/D; Ctrl+Alt+C again = stop)");

        const mbr::PassResult menu = MenuBridgePass();
        const bool published = menu.published;      // at most one ReloadConfig per pass
        // Switch Off Until Restart: only once the game has run again for a moment (mbr::UnhookGate), so
        // the cars, police and airliner can give their state back in their own updates during Unhook
        if (menu.unhook) { Unhook(); Beep(330, 120); Beep(220, 200); return 0; }

        if (!published && GetTickCount() - lastIniPoll > 1000)
        {
            lastIniPoll = GetTickCount();
            if (IniChanged()) ReloadConfig();
        }
        ApplyMissionMask(static_cast<uint8_t>(mis::g_fault ? 0 : mis::g_want));   // [missions1]
        WatchGameWindow();
        if (!GameHasFocus()) continue;

        if (pOn) SetMode(true);
        else if (pOff) SetMode(false);
        if (ctrlAlt && pR) { if (!published) ReloadConfig(); Beep(880, 60); }
        if (ctrlAlt && pL)
        {
            // wanted level 0 -> 1 -> ... -> 5 -> 0: one short beep per level, a low one for 0.
            // Co-op: BOTH players are set to it (the pause menu's Set Wanted Level sets one).
            const int lv = wd::RequestNext();
            Log(">>> Ctrl+Alt+L: wanted level %d (both players)\n", lv);
            if (lv == 0) Beep(330, 160);
            else for (int i = 0; i < lv; ++i) { Beep(1100, 55); Sleep(45); }
        }
        if (ctrlAlt && pT) ModelToggleTelemetry();
        if (ctrlAlt && pNext) rd::StepStation(+1);
        if (ctrlAlt && pPrev) rd::StepStation(-1);
        if (ctrlAlt && pSlash)
        {
            const int ai = rd::ToggleAi();
            if (ai < 0) { Log(">>> radio: Ctrl+Alt+/: nothing to switch (music = ai)\n"); Beep(330, 120); }
            else if (ai) { Log(">>> radio: Ctrl+Alt+/: the AI songs on every station (this session)\n"); Beep(990, 60); Beep(660, 60); }
            else { Log(">>> radio: Ctrl+Alt+/: your music setup back (lcudrive.ini)\n"); Beep(660, 60); Beep(990, 60); }
        }
        if (ctrlAlt && pSemi) { rd::OpenLink(); Beep(880, 40); }
        if (ctrlAlt && pF) { InterlockedExchange(&ap::g_recall, 1); Log(">>> Ctrl+Alt+F: airport plane\n"); }
        if (ctrlAlt && pB) { lf::RequestParty(0); Log(">>> Ctrl+Alt+B: dance party\n"); }
        lf::HotkeyPads(g_cfgBuf[g_cfgIdx]);           // ...or [life] party_pad held on a player's own controller
        {   // the dance party's answer (set on the game thread): rising pair = they dance, low = nobody to
            const LONG b = InterlockedExchange(&lf::g_partyBeep, 0);
            if (b == 1) { Beep(880, 60); Beep(1175, 80); }
            else if (b == 2) Beep(260, 180);
        }
        {   // the airport plane's answer (set on the game thread): high pair = on the runway, low = refused
            const LONG b = InterlockedExchange(&ap::g_beep, 0);
            if (b == 1) { Beep(1320, 60); Beep(1760, 80); }
            else if (b == 2) Beep(260, 180);
        }
        // the aeroplane's stall horn (airplane.h): a double beep, at most every 1.5 s
        if (InterlockedExchange(&fw::g_horn, 0)) { Beep(1000, 50); Sleep(40); Beep(1000, 50); }
        {   // the landing gear (airplane.h): rising pair = up, falling pair = down, three low = "gear!" (up, low, descending)
            const LONG gb = InterlockedExchange(&fw::g_gearBeep, 0);
            if (gb == 1) { Beep(700, 70); Beep(1050, 90); }
            else if (gb == 2) { Beep(1050, 70); Beep(700, 90); }
            else if (gb == 3) { Beep(420, 60); Sleep(30); Beep(420, 60); Sleep(30); Beep(420, 60); }
        }
    }
}

// ---------------------------------------------------------------------------------------------
//  Startup
// ---------------------------------------------------------------------------------------------
static DWORD WINAPI Main(LPVOID)
{
    g_t0 = GetTickCount64();
    char logPath[MAX_PATH];
    _snprintf_s(logPath, sizeof(logPath), _TRUNCATE, "%slcudrive.log", g_dir);
    g_log = _fsopen(logPath, "a", _SH_DENYWR);

    SYSTEMTIME st; GetLocalTime(&st);
    Log("==================================================================================\n");
    Log("lcudrive session %04d-%02d-%02d %02d:%02d:%02d   (built %s %s)\n", st.wYear, st.wMonth,
        st.wDay, st.wHour, st.wMinute, st.wSecond, __DATE__, __TIME__);

    g_base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    if (!ImageMatches())
    {
        Log("REFUSING TO HOOK: this is not the game build lcudrive was written against.\n");
        Beep(220, 400);
        return 0;
    }

    _snprintf_s(g_iniPath, sizeof(g_iniPath), _TRUNCATE, "%slcudrive.ini", g_dir);
    IniChanged();
    LoadConfig(g_iniPath, g_cfgBuf[0]);
    InterlockedExchange(&g_beepsOn, g_cfgBuf[0].beeps);
    g_cfgBuf[1] = g_cfgBuf[0];
    g_cfgRaw[0] = g_cfgRaw[1] = g_cfgBuf[0];      // [missions1] no mask at startup
    LogConfig(g_cfgBuf[0]);
    g_enabled = g_cfgBuf[0].enable != 0;

    vsim::BridgeReset(g_bridge);
    vsim::BridgeRequestScan(g_bridge);

    ModelInit(g_base, g_dir);
    pad::Init();
    lights::Init(g_base);
    sl::Init(g_base);
    hl::Init();
    pl::Init();
    fw::Init();
    chute::Init();
    jp::Init();
    g_modelArmed = InstallPatches();
    HookHover();
    HookBike();
    wd::Init();
    HookPM();
    if (g_slotPM) HookCrimes();
    if (g_slotHover)
    {
        g_exitPatch.replacement = reinterpret_cast<void*>(&Hook_NoExitHint);
        if (!ApplyPatchRegs(g_base, g_exitPatch))
            Log("helicopter exit-anywhere NOT installed: the game's own exits only.\n");
    }
    cc::Init(g_base, g_cfgBuf[0]);
    cg::Init(g_base, GetPrivateProfileIntA("general", "crash_guards", 1, g_iniPath) != 0);   // [crashguard]
    g_jetPatch.replacement = reinterpret_cast<void*>(&Hook_Jetpack);
    if (!ApplyPatch(g_base, g_jetPatch)) Log("jetpack patch NOT installed: the game's own jetpack.\n");
    pt::g_origSweep = reinterpret_cast<pt::SweepFn>(g_base + pt::FN_SWEEP);
    g_pteroPatch.replacement = reinterpret_cast<void*>(&Hook_CharSweep);
    if (!ApplyPatch(g_base, g_pteroPatch)) Log("Pteranodon sweep patch NOT installed: Pteranodons walk, as the game has them.\n");
    wx::g_origEval = reinterpret_cast<wx::EvalFn>(g_base + wx::FN_EVAL);
    g_weatherPatch.replacement = reinterpret_cast<void*>(&Hook_PostEval);
    if (!ApplyPatch(g_base, g_weatherPatch))
    {
        wx::g_origEval = nullptr;       // (weather.h Publish then takes the weather at the players: screen rain and grade only)
        Log("weather patch NOT installed: no engine fog / engine rain; the screen rain and the grey light still follow [weather].\n");
    }
    gfx::Init();
    gfx::g_origCamCopy = reinterpret_cast<gfx::CamCopyFn>(g_base + gfx::FN_CAMCOPY);
    g_gfxCamPatch.replacement = reinterpret_cast<void*>(&Hook_CamCopy);
    if (!ApplyPatch(g_base, g_gfxCamPatch)) Log("gfx camera patch NOT installed: no camera feel (the rest of [gfx] works).\n");
    {
        int n = 0;
        for (CallPatch& p : g_scriptErrPatches) { p.replacement = reinterpret_cast<void*>(&Hook_ScriptErr); if (ApplyPatch(g_base, p)) ++n; }
        Log("script errors: %d of %d report sites logged (\"!!! script:\" lines)\n", n,
            (int)(sizeof(g_scriptErrPatches) / sizeof(g_scriptErrPatches[0])));
        n = 0;
        for (CallPatch& p : g_namedErrPatches) { p.replacement = reinterpret_cast<void*>(&Hook_NamedErr); if (ApplyPatch(g_base, p)) ++n; }
        Log("script lookups: %d of %d missing-name reports logged (\"!!! script lookup:\"); PrintToScreen -> log: %s\n", n,
            (int)(sizeof(g_namedErrPatches) / sizeof(g_namedErrPatches[0])), HookScriptPrint() ? "on" : "NOT hooked");
    }
    if (!wx::g_origEval) Log("gfx: no per-view settings hook (the weather patch) - no grade / AO / draw distance / haze.\n");
    if (!HookUpdate())
    {
        for (CallPatch& p : g_patches) RemovePatch(g_base, p);
        RemovePatch(g_base, g_exitPatch);
        RemovePatch(g_base, g_jetPatch);
        RemovePatch(g_base, g_pteroPatch);
        RemovePatch(g_base, g_weatherPatch);
        for (CallPatch& p : g_crimePatches) RemovePatch(g_base, p);
        RemovePatch(g_base, g_gfxCamPatch);
        for (CallPatch& p : g_shotPatches) RemovePatch(g_base, p);
        UnhookSlot(g_slotBlowupBolt, reinterpret_cast<void*>(g_origBlowupBolt));
        UnhookSlot(g_slotBoltVehicle, reinterpret_cast<void*>(g_origBoltVehicle));
        if (g_slotOnDamage)
        {
            DWORD old = 0;
            if (VirtualProtect(g_slotOnDamage, sizeof(void*), PAGE_READWRITE, &old))
            {
                *g_slotOnDamage = reinterpret_cast<void*>(g_origOnDamage);
                VirtualProtect(g_slotOnDamage, sizeof(void*), old, &old);
            }
            g_slotOnDamage = nullptr;
        }
        g_modelArmed = false;
        Log("startup failed; nothing is hooked, the game is stock.\n");
        Beep(220, 400);
        return 0;
    }
    ml::Install(g_base, g_cfgBuf[0]);   // [labels1] map pins + location names (refused = stock map)
    st::Install(g_base, g_cfgBuf[0]);   // [station1] "Return to Police Station" -> outside, that player only (refused = the game's own)
    HookLightMgr();                 // after the car hook, so a failed startup leaves no light hook behind
    sh::Install(g_cfgBuf[0]);       // the rare shark: SwimmingContext's deep-water timer, 2 call sites (refused = the game's own shark)
    Log("ready. lcudrive is %s. ] = on, [ = stock, Pause / Ctrl+Alt+U = unhook, Ctrl+Alt+R = "
        "reload ini, Ctrl+Alt+T = telemetry\n",
        !g_modelArmed ? "OBSERVE-ONLY (patches missing)" : (g_enabled ? "ON" : "off"));
    if (g_modelArmed) Beep2(g_enabled); else Beep(220, 400);

    ApplyRadioSettings(g_cfgBuf[0]);
    rd::g_pauseByte = reinterpret_cast<const volatile uint8_t*>(g_base + gsnd::RVA_PAUSED);
    rlg::Load();                    // [radiologo1] Mods\RadioLogo\logos.txt: station id -> logo tile (before the radio thread)
    rd::Start();

    HANDLE t = CreateThread(nullptr, 0, HotkeyThread, nullptr, 0, nullptr);
    if (t)
    {
        CloseHandle(t);
        if (g_cfgBuf[0].menu_enable) pmn::Install(g_base, g_iniPath);   // Mod Options (it saves via that thread)
        else Log("menu: off ([menu] enable = 0)\n");
    }
    return 0;
}

BOOL APIENTRY DllMain(HMODULE h, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(h);
        DWORD n = GetModuleFileNameA(h, g_dir, MAX_PATH);
        if (n > 0 && n < MAX_PATH)
        {
            char* cut = strrchr(g_dir, '\\');
            if (cut) cut[1] = 0;
        }
        HANDLE t = CreateThread(nullptr, 0, Main, nullptr, 0, nullptr);
        if (t) CloseHandle(t);
    }
    return TRUE;
}
