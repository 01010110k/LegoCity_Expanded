// =============================================================================================
//  station.h - the pause menu's "Return to Police Station" takes ONLY the player who picked it to
//  the pavement outside the station, with no level change (2026-11-24, "station1"; Notes\69).
//  Included by lcudrive.cpp just before PMThunk (it needs wd::, br::, g_backroomsFault, g_inFlight).
//  The decisions are station_model.h's (pure, test\station_harness.cpp); the move is
//  busted_model.h's sequence (the busted-outside one, wanted_busted.h) run for that player.
//
//  THE HOOK: the confirm menu 0x24's "Yes" ends in `jmp 0x14003ACA0` at 0x140041333 (a tail jump:
//  rcx = the menu, edx = the mode, r8 = 0). Its rel32 is pointed at a stub -> Hook_StationGo, which
//  is then called exactly as 0x14003ACA0 would have been and returns to the menu system as it would
//  have. Before anything is written: the 11 bytes before the jmp, the jmp itself (E9 -> 0x14003ACA0),
//  the callee's prologue and menu 0x24's update slot (0x141176DD8 = 0x1400412F0) must all be the
//  stock ones (station_model.h SiteState) - otherwise nothing is patched and the game's own action
//  stays (logged). Unhook puts the rel32 back.
//
//  IN THE HOOK (the menu, game thread, the world paused): Decide() on what can be read without a
//  call into the engine (the level name, the mission flags, who paused, who is there, the Backrooms,
//  the busted state). Anything but vOutside / vBackrooms / vBusy (and any fault while reading) =
//  the game's own 0x14003ACA0, untouched. Otherwise: a request for that player, then the menu is
//  closed the way the game's own confirm 0x25 does it on "Yes" (0x1408EBAF0(game+0x3058, 0, 1),
//  0x1400413AC [P]), and nothing else - no level change.
//
//  ON THE NEXT WORLD UPDATES (PMThunk -> RunStation): that player only - fade (their half of the
//  screen, the busted fade overlay IniParams 7.z / 7.w), out of any vehicle (Character_ClearVehicle's
//  call), Teleport + LockInPlace("idle") at the spot, hold while it streams in, Teleport again with
//  the heading, fade back, watch 3 s for a fall through. The other player is never called on; only
//  where they stand is read (to stand 0.9 u aside if they are on the spot).
//
//  THE SPOT = wanted's busted-outside spot ([wanted] busted_outside_x/_y/_z/_heading, default the
//  story's own Story_A1_M2_ExitPoliceStation_PlayerWalkTo locator (-178.291, 3.343, -222.379),
//  facing 20 deg, away from the door). Ground there on the stock city collision: y 3.350, flat,
//  nothing overhead [P offline, Notes\69].
//
//  [general] station_return = stock gives the game's own trip back (the hook then always runs the
//  original). `[` (lcudrive stock for the session) does too.
// =============================================================================================
#pragma once
#include "station_model.h"

namespace st {

static const uintptr_t RVA_DIALOG_SLOT = 0x1176DD8, RVA_DIALOG_UPDATE = 0x412F0;   // menu 0x24's update fn
static const uintptr_t RVA_PAUSER = 0x11754F0;      // int: the player index that paused (-1 = either pad) [P Notes 44]
static const uintptr_t RVA_GAME = 0x1C7A830;        // the game object (+0x3058: the frontend; +0x335D/E/F)
static const uintptr_t FN_UNPAUSE = 0x8EBAF0;       // void(game+0x3058, 0, 1): close the pause menu [P 0x1400413AC]
static const uintptr_t RVA_WORLDROOT = 0x1C6A768;   // 0x140606BE0 returns this; [+0x180] -> [+0x68] = the level name [P 0x1407F1D7D]

typedef void(__fastcall* GoFn)(void* menu, int mode, void* level);
typedef void(__fastcall* UnpauseFn)(void* fe, int a, int b);

struct Move
{
    busted::Seq s;
    uint8_t*    ch;
    float       dest[3];
    float       hdg;
    float       from[3];
    uint32_t    lastMs;
    bool        wasRiding;
    bool        locked;      // we put them in LockInPlace (so we take them out)
};

static Move           g_mv[2] = {};
static busted::Params g_p;
static volatile LONG  g_req[2] = { 0, 0 };   // the hook -> the next world update
static volatile bool  g_fault = false;       // latched: the stock action for the session
static volatile bool  g_stopReq = false;     // Unhook: let go on the game thread
static bool           g_applied = false;
static int32_t        g_origRel = 0;
static volatile LONG  g_asked = 0, g_moved = 0;   // (the log)

static uint8_t* Char(int i) { return wd::RawPlayer(i & 1); }
static bool Riding(int i, uint8_t* ch)
{
    if ((i == 0 ? ::PlayerVehicle() : ::Player2Vehicle()) != nullptr) return true;
    void* ride = At<void*>(ch, eng::CH_RIDE);
    return PtrOk(ride) && At<int32_t>(ride, eng::RIDE_STATE) == 4;
}
static bool BustedNow(int i) { return wd::g_pl[i & 1].bust.on || busted::Busy(wd::g_bo[i & 1].s); }
static bool Moving(int i) { return busted::Busy(g_mv[i & 1].s) || g_req[i & 1] != 0; }

// The current level's name, the game's own way (0x1407F1D7D..0x1407F1D95) - reads only.
static const char* LevelName()
{
    uint8_t* root = *reinterpret_cast<uint8_t**>(g_base + RVA_WORLDROOT);
    if (!PtrOk(root)) return nullptr;
    uint8_t* lv = At<uint8_t*>(root, 0x180);
    if (!PtrOk(lv)) return nullptr;
    const char* n = At<const char*>(lv, 0x68);
    return PtrOk(n) ? n : nullptr;
}

static int StationFault(EXCEPTION_POINTERS* ep, const char* where)
{
    if (!g_fault)
    {
        g_fault = true;
        Log("!!! EXCEPTION 0x%08lX in the station-return code (%s) at %p - \"Return to Police Station\" is the game's own for this session.\n",
            ep->ExceptionRecord->ExceptionCode, where, ep->ExceptionRecord->ExceptionAddress);
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

// What the hook knows. Reads only (SEH-guarded by the caller).
static stn::Ask Gather(int mode, const Config& cfg)
{
    stn::Ask a;
    a.mode = mode;
    a.outside = stn::ParseOutside(cfg.station_return);
    a.modOn = g_enabled && !g_unhooked && !g_fault;
    a.mission = wd::MissionRunning();
    // 2026-11-25: picked in the city, it still went inside - the log said "not in LEGO City": the level
    // NAME read did not say lego_city there. The game's own in-the-city flag (game 0x1C7A830 +0x335D, what the map's
    // location line and the weather use) decides; the name only when the flag cannot be read.
    {
        const char* nm = LevelName();
        uint8_t* game = *reinterpret_cast<uint8_t**>(g_base + 0x1C7A830);
        const bool flagOk = PtrOk(game);
        const bool flag = flagOk && At<uint8_t>(game, 0x335D) != 0;
        a.inCity = flagOk ? flag : stn::IsCity(nm);
        static bool s_told = false;
        if (flagOk && flag != stn::IsCity(nm) && !s_told)
        {
            s_told = true;
            Log(">>> station: the level name \"%.40s\" and the in-city flag (%d) disagree - the flag is taken\n", nm ? nm : "(none)", flag ? 1 : 0);
        }
    }
    a.pauser = *reinterpret_cast<const int*>(g_base + RVA_PAUSER);
    for (int i = 0; i < 2; ++i)
    {
        a.present[i] = PtrOk(Char(i));
        a.inBackrooms[i] = br::Inside(i);
        a.brTransit[i] = !g_backroomsFault && br::g_state.pl[i].st != brm::PS_OUT;
        a.busted[i] = BustedNow(i);
        a.moving[i] = Moving(i);
    }
    a.backroomsOk = !g_backroomsFault;
    return a;
}

static bool Intercept(int mode, void* menu)
{
    const Config& cfg = g_cfgBuf[g_cfgIdx];
    stn::Answer r{ stn::vStock, stn::wModOff, -1 };
    stn::Ask a;
    __try { a = Gather(mode, cfg); r = stn::Decide(a); }
    __except (StationFault(GetExceptionInformation(), "reading the state")) { return false; }
    if (r.v == stn::vStock)
    {
        if (r.why != stn::wNotStation && r.why != stn::wSwitchedStock)
            Log(">>> station: \"Return to Police Station\" - the game's own trip (the lobby, both players): %s\n", stn::WhyText(r.why));
        return false;
    }
    const int who = r.who & 1;
    InterlockedIncrement(&g_asked);
    if (r.v == stn::vOutside) InterlockedExchange(&g_req[who], 1);
    else if (r.v == stn::vBackrooms) InterlockedExchange(&br::g_leaveReq[who], 1);
    Log(">>> station: P%d picked \"Return to Police Station\" (paused by %d)%s - %s; the other player is untouched, no level change\n",
        who + 1, a.pauser, a.present[1 - who] ? "" : " (one player)",
        r.v == stn::vOutside ? "off to the pavement outside the station" : r.v == stn::vBackrooms ? "the Backrooms' own way out (Leave) for them"
        : stn::WhyText(r.why));
    // close the pause menu as the game's own confirm 0x25 does on Yes
    __try
    {
        uint8_t* game = *reinterpret_cast<uint8_t**>(g_base + RVA_GAME);
        if (PtrOk(game)) reinterpret_cast<UnpauseFn>(g_base + FN_UNPAUSE)(game + 0x3058, 0, 1);
    }
    __except (StationFault(GetExceptionInformation(), "closing the pause menu")) {}
    (void)menu;
    return true;
}

static void __fastcall Hook_StationGo(void* menu, int mode, void* level)
{
    InterlockedIncrement(&g_inFlight);
    bool handled = false;
    if (!g_unhooked && !g_fault) handled = Intercept(mode, menu);
    if (!handled) reinterpret_cast<GoFn>(g_base + stn::kRvaGo)(menu, mode, level);   // the game's own, as it was
    InterlockedDecrement(&g_inFlight);
}

// ---------------------------------------------------------------------------------------------
//  Install / Uninstall: the jmp's rel32, with hooks.h's stub page (mov rax, imm64; jmp rax).
// ---------------------------------------------------------------------------------------------
static bool Install(uintptr_t base, const Config& cfg)
{
    if (g_applied) return true;
    uint8_t* ctx = reinterpret_cast<uint8_t*>(base + stn::kRvaCtx);
    uint8_t* site = reinterpret_cast<uint8_t*>(base + stn::kRvaSite);
    int state = 2; bool prologue = false, slot = false;
    __try
    {
        uint8_t b[16]; std::memcpy(b, ctx, 16);
        state = stn::SiteState(b);
        uint8_t p[15]; std::memcpy(p, reinterpret_cast<const void*>(base + stn::kRvaGo), 15);
        prologue = stn::GoPrologueOk(p);
        slot = *reinterpret_cast<const uintptr_t*>(base + RVA_DIALOG_SLOT) == base + RVA_DIALOG_UPDATE;
        std::memcpy(&g_origRel, site + 1, 4);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { state = 2; }
    if (state != 0 || !prologue || !slot)
    {
        Log("  patch %-14s REFUSED: %s - \"Return to Police Station\" stays the game's own (the lobby)\n", "station_return",
            state == 1 ? "the jmp at rva 0x41333 goes somewhere else (another mod?)" : state == 2 ? "the bytes at rva 0x41328 are not the expected ones"
            : !prologue ? "0x14003ACA0 is not the expected function" : "menu 0x24's update is not 0x1400412F0");
        return false;
    }
    if (!g_stubPage) g_stubPage = AllocNear(base);
    if (!g_stubPage || g_stubUsed + 16 > 0x1000) { Log("  patch %-14s REFUSED: no stub memory\n", "station_return"); return false; }
    uint8_t* stub = g_stubPage + g_stubUsed;
    g_stubUsed += 16;
    const uint64_t fn = reinterpret_cast<uint64_t>(&Hook_StationGo);
    stub[0] = 0x48; stub[1] = 0xB8; std::memcpy(stub + 2, &fn, 8);   // mov rax, imm64
    stub[10] = 0xFF; stub[11] = 0xE0;                                  // jmp rax
    FlushInstructionCache(GetCurrentProcess(), stub, 16);
    const intptr_t d = reinterpret_cast<intptr_t>(stub) - (reinterpret_cast<intptr_t>(site) + 5);
    if (d < INT32_MIN || d > INT32_MAX) { Log("  patch %-14s REFUSED: stub out of rel32 range\n", "station_return"); return false; }
    DWORD old = 0;
    if (!VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &old)) return false;
    InterlockedExchange(reinterpret_cast<volatile LONG*>(site + 1), static_cast<int32_t>(d));   // one 4-byte store
    VirtualProtect(site, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), site, 5);
    g_applied = true;
    Log("  patch %-14s ok: jmp at rva 0x41333 -> stub %p -> %p; [general] station_return = %s (%s)\n", "station_return", (void*)stub,
        (void*)&Hook_StationGo, cfg.station_return, stn::ParseOutside(cfg.station_return)
        ? "the player who picks it goes to the pavement outside, alone, no loading" : "the game's own trip to the lobby");
    return true;
}

static void Uninstall(uintptr_t base)
{
    if (!g_applied) return;
    uint8_t* site = reinterpret_cast<uint8_t*>(base + stn::kRvaSite);
    DWORD old = 0;
    if (VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &old))
    {
        InterlockedExchange(reinterpret_cast<volatile LONG*>(site + 1), g_origRel);
        VirtualProtect(site, 5, old, &old);
        FlushInstructionCache(GetCurrentProcess(), site, 5);
    }
    g_applied = false;
}

// ---------------------------------------------------------------------------------------------
//  The move, per player (world updates, game thread).
// ---------------------------------------------------------------------------------------------
static void Drop(int i, const char* why, bool unlock)
{
    Move& m = g_mv[i & 1];
    if (!busted::Busy(m.s)) return;
    uint8_t* p = Char(i);
    if (unlock && m.locked && PtrOk(p) && p == m.ch && wd::IsIn(p, wd::CTX_IDLENOGRAV))
        reinterpret_cast<wd::ChFn>(g_base + wd::FN_FORCEDEF)(p);
    Log("    station: P%d - the move outside the station is dropped (%s)\n", (i & 1) + 1, why);
    m = Move{};
    if (!busted::Busy(wd::g_bo[i & 1].s)) wd::BoSetFade(i, 0.0f);
}

static void Start(int i, uint8_t* p, const Config& cfg)
{
    Move& m = g_mv[i];
    m = Move{};
    m.ch = p;
    m.hdg = cfg.wanted_busted_outside_heading;
    const float dest[3] = { cfg.wanted_busted_outside_x, cfg.wanted_busted_outside_y, cfg.wanted_busted_outside_z };
    bool otherThere = false;
    if (uint8_t* q = Char(1 - i))
        if (q != p)
        {
            const float dx = At<float>(q, 0xA8) - dest[0], dz = At<float>(q, 0xB0) - dest[2];
            otherThere = dx * dx + dz * dz < 1.2f * 1.2f;
        }
    busted::Spot(dest, m.hdg, otherThere, m.dest);
    m.from[0] = At<float>(p, 0xA8); m.from[1] = At<float>(p, 0xAC); m.from[2] = At<float>(p, 0xB0);
    float jump = 1.0e9f;
    {
        const float dx = m.from[0] - m.dest[0], dy = m.from[1] - m.dest[1], dz = m.from[2] - m.dest[2];
        const float d = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (IsFin(d)) jump = d;
    }
    g_p = busted::Params{};
    g_p.fadeS = cfg.wanted_busted_fade;
    g_p.holdFarS = cfg.wanted_busted_outside_load;
    m.wasRiding = Riding(i, p);
    busted::Start(m.s, jump, g_p);
    m.lastMs = GetTickCount();
    Log(">>> station: P%d - fade out; off to the pavement outside the police station (%.1f, %.1f, %.1f), %.0f u away%s%s - "
        "only P%d goes, no level change ([general] station_return = stock for the game's own trip)\n",
        i + 1, m.dest[0], m.dest[1], m.dest[2], jump < 1.0e8f ? jump : -1.0f, m.wasRiding ? ", out of the vehicle first" : "",
        otherThere ? ", a step aside (the other player is on the spot)" : "", i + 1);
}

static void Step(int i, float dt)
{
    Move& m = g_mv[i];
    uint8_t* p = Char(i);
    const uint32_t now = GetTickCount();
    busted::Input in{};
    in.dt = dt;
    in.present = PtrOk(p) && p == m.ch;
    if (in.present && now - m.lastMs >= 2000)          // a gap (the pause menu again; a load that reused the address)
    {
        const float* ref = m.s.moved ? m.dest : m.from;
        const float dx = At<float>(p, 0xA8) - ref[0], dy = At<float>(p, 0xAC) - ref[1], dz = At<float>(p, 0xB0) - ref[2];
        const float d = std::sqrt(dx * dx + dy * dy + dz * dz);
        in.present = IsFin(d) && d < (m.s.moved ? 6.0f : 40.0f);
        Log("    station: P%d - %.0f s without a world update in the middle of the move (a pause?) - %s\n", i + 1,
            (now - m.lastMs) / 1000.0f, in.present ? "carrying on" : "another place now: dropped");
    }
    m.lastMs = now;
    if (in.present)
    {
        in.riding = Riding(i, p);
        in.grounded = reinterpret_cast<wd::ChGroundFn>(g_base + wd::FN_CH_ONGROUND)(p);
        const float y = At<float>(p, 0xAC);
        in.fell = m.s.moved && IsFin(y) && y < m.dest[1] - g_p.fallU;
    }
    const busted::Phase before = m.s.ph;
    const uint32_t act = busted::Step(m.s, in, g_p);
    if (in.present)
    {
        if (act & busted::aDismount)
        {
            const bool ok = wd::BoClearVehicle(p);
            Log("    station: P%d - out of the vehicle (the game's ClearVehicle)%s\n", i + 1, ok ? "" : " - nothing to get out of");
        }
        if ((act & busted::aUnlock) && m.locked && wd::IsIn(p, wd::CTX_IDLENOGRAV))
        {
            reinterpret_cast<wd::ChFn>(g_base + wd::FN_FORCEDEF)(p);
            m.locked = false;
        }
        if (act & busted::aTeleport)
        {
            const float pos[4] = { m.dest[0], m.dest[1], m.dest[2], 1.0f };
            const bool ok = reinterpret_cast<wd::ChTeleFn>(g_base + wd::FN_CH_TELEPORT)(p, pos) == 0;
            busted::Moved(m.s, ok);
            if (ok && (act & busted::aLock)) m.locked = wd::LockInPlace(p, "idle");
            if (before == busted::kMove)
                Log(ok ? "    station: P%d is at the station's forecourt - held there (black) while the place loads\n"
                       : "    station: P%d - the game refused the teleport (CanTeleport) - trying again\n", i + 1);
        }
        if (act & busted::aFace)
            reinterpret_cast<wd::ChHeadFn>(g_base + wd::FN_CH_HEADING)(p, busted::HeadingU16(m.hdg), true);
    }
    if (act & busted::aFell)
        Log(">>> station: P%d fell through the ground at the station (it had not loaded) - back to the spot, try %d\n", i + 1, m.s.retries);
    if (act & busted::aRelease)
    {
        static const char* const kWhy[6] = { "", "still in the vehicle", "the game refused the teleport", "fell through the ground too often",
                                             "it took too long", "the player is gone" };
        if (act & busted::aGaveUp)
            Log(">>> station: P%d stays where they are - the move outside the station could not be done (%s)\n", i + 1,
                kWhy[m.s.gaveUp >= 0 && m.s.gaveUp < 6 ? m.s.gaveUp : 0]);
        else
        {
            InterlockedIncrement(&g_moved);
            Log(">>> station: P%d is outside the police station, %.0f u from where they were - fade in\n", i + 1,
                std::sqrt((m.from[0] - m.dest[0]) * (m.from[0] - m.dest[0]) + (m.from[2] - m.dest[2]) * (m.from[2] - m.dest[2])));
        }
    }
    else if ((act & busted::aDone) && (act & busted::aGaveUp) && m.s.gaveUp >= 3)
        Log("    station: P%d - the move outside the station ended early (%s)\n", i + 1,
            m.s.gaveUp == 3 ? "fell through the ground too often - left to the game's own rescue" : m.s.gaveUp == 4 ? "it took too long"
            : "the player is gone");
    if (!busted::Busy(wd::g_bo[i].s)) wd::BoSetFade(i, m.s.fade);   // (the bust's own move owns the black while it runs)
    if (act & busted::aDone) { m = Move{}; if (!busted::Busy(wd::g_bo[i].s)) wd::BoSetFade(i, 0.0f); }
}

static void Tick(void* pm, float dt)
{
    const Config& cfg = g_cfgBuf[g_cfgIdx];
    if (g_stopReq)
    {
        for (int i = 0; i < 2; ++i) { InterlockedExchange(&g_req[i], 0); Drop(i, "lcudrive is letting go", true); }
        return;
    }
    for (int i = 0; i < 2; ++i)
    {
        uint8_t* p = Char(i);
        // only in this player's world, once a frame
        void* lvl = PtrOk(p) ? wd::g_getLevel(p + 0x10) : nullptr;
        const bool here = PtrOk(lvl) && wd::g_getPM(lvl) == pm;
        if (!here)
        {
            if (!PtrOk(p))
            {
                if (InterlockedExchange(&g_req[i], 0)) Log("    station: P%d asked, but is not in the game any more - nothing done\n", i + 1);
                if (busted::Busy(g_mv[i].s)) { busted::Input in{}; in.dt = dt; busted::Step(g_mv[i].s, in, g_p); g_mv[i] = Move{}; wd::BoSetFade(i, 0.0f);
                                               Log("    station: P%d is gone - the move outside the station is dropped\n", i + 1); }
            }
            continue;
        }
        if (busted::Busy(g_mv[i].s) && BustedNow(i)) { Drop(i, "busted on the way - the bust has them now", false); continue; }
        if (InterlockedExchange(&g_req[i], 0) && !busted::Busy(g_mv[i].s))
        {
            if (BustedNow(i)) Log("    station: P%d - busted meanwhile; the bust takes them outside the station\n", i + 1);
            else Start(i, p, cfg);
        }
        if (busted::Busy(g_mv[i].s)) Step(i, dt);
    }
}

// A new world (a level load): the characters are other objects now - nothing is called on them.
static void ForgetAll()
{
    for (int i = 0; i < 2; ++i) { if (busted::Busy(g_mv[i].s)) Log("    station: P%d - a new world; the move is forgotten\n", i + 1); g_mv[i] = Move{}; }
}

} // namespace st

// PMThunk's call: its own SEH filter. A fault = the stock action for the session (st::g_fault), whoever
// was in the middle of the move is let go on the next update if it can be done safely.
static void RunStation(void* pm, void* info)
{
    __try
    {
        float dt = PtrOk(info) ? At<float>(info, 0x18) : 0.0f;
        if (!IsFin(dt) || dt <= 0.0f || dt > 0.25f) dt = 1.0f / 60.0f;
        static void* s_lastPm = nullptr;
        if (pm != s_lastPm)
        {
            // (PMThunk runs for every world; only a change of the PLAYER's world counts - st::Tick checks that per player)
            uint8_t* p1 = wd::RawPlayer(0);
            void* lvl = PtrOk(p1) ? wd::g_getLevel(p1 + 0x10) : nullptr;
            if (PtrOk(lvl) && wd::g_getPM(lvl) == pm) { if (s_lastPm) st::ForgetAll(); s_lastPm = pm; }
        }
        st::Tick(pm, dt);
    }
    __except (st::StationFault(GetExceptionInformation(), "the move")) {}
}
static void RunStationRelease()
{
    __try { for (int i = 0; i < 2; ++i) st::Drop(i, "the station code faulted", true); }
    __except (EXCEPTION_EXECUTE_HANDLER) { st::g_mv[0] = st::Move{}; st::g_mv[1] = st::Move{}; }
}
