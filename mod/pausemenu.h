// =============================================================================================
//  pausemenu.h - "Mod Options" in the game's own pause menu (v1, 2026-11-13).
//
//  The pause menu is TT's legacy immediate-mode MENU system, built in C++ (menu id 2: draw 0x14080B090,
//  update 0x14080AB60). It has its own extension point, unused in this game: three LegoCityGame
//  virtuals that exist only to add ONE extra row right after Options -
//      vt+0x358 HasRow(game, Menu*)  = 0x1400410C0 `xor al,al; ret`
//      vt+0x350 AddRow(game, Menu*)  = 0x140211520 `ret`
//      vt+0x360 OnRow (game, Menu*)  = 0x140211520 `ret`
//  called only from the pause draw (0x14080B2BE / 0x14080B2D5) and the pause update (0x14080ADC2 /
//  0x14080ADE2, whose row counter already counts the extra row) [P]. lcudrive fills those three slots and
//  registers two menus of its own in the engine's writable menu table (0x14115D7C0, 47 of 128 used):
//      0x4C00  the Mod Options page             0x4C01  a section page (g_page says which)
//  so the game draws them with its own MenuItem (font, highlight, fade, panel) and drives them with its
//  own input - whoever paused navigates (co-op: the pausing player's pad, menu_logic.h).
//
//  This file is only the game-facing layer: addresses, the slot install / removal, the registration,
//  the 3 row callbacks + the page callbacks, SEH, sounds and text lookup. Everything that decides is
//  in menu_logic.h (pure, tested by test\menu_logic_test.cpp); saving is menu_bridge.h (hotkey thread).
//
//  Threads: every callback runs on the game thread [I: the game itself shares its menu globals between
//  draw and update with no lock]. No file I/O, no Sleep, no Beep, no per-frame log here.
//  Faults: any exception in this code (or in a game function it calls) latches g_menuFault: HasRow
//  answers false (no row), open pages show one Back row and pop one level per update back to the pause
//  menu. Cars, radio and the rest of lcudrive keep running - they have their own fault switches.
//  Switch Off Until Restart uses the same inert path at once (g_switchedOff); the unhook itself waits on the
//  hotkey thread until the game runs again (mbr::UnhookGate), so the world can take its state back.
//
//  Needs (lcudrive.cpp, included after them): Log, g_base, g_inFlight, g_unhooked, g_enabled, g_cfgBuf,
//  g_cfgIdx; pad::g_connected; ap::g_recall; wd::g_request.
// =============================================================================================
#pragma once
#include "menu_logic.h"

#ifndef LCUD_MENU_FAULT_TEST
#define LCUD_MENU_FAULT_TEST 0       // 1 = the T14 drill build: [menu] fault_test = 1 throws once in a section draw
#endif

namespace pmn {

// ---- the game (RVAs; LEGOLCUR_DX11 TimeDateStamp 0x591B08C3) ------------------------------------------
static const uintptr_t RVA_MENU_DEFS = 0x115D7C0, RVA_MENU_DEF_N = 0x115EFE0;            // [P]
static const uintptr_t FN_MENU_OPEN = 0x550140;        // Menu*(int id, int startRow, int -1); startRow in range wins [P]
static const uintptr_t FN_MENU_POP = 0x54EBA0;         // void(): onExit(top), pop, re-enter the revealed menu [P]
static const uintptr_t FN_CUR_MENU_ID = 0x54F6B0;      // int(): the top menu's id, -1 none [P]
static const uintptr_t FN_MENU_ITEM_AUTO = 0x8AF760;   // void(Menu*, const char*): one row, the menu's fade [P]
static const uintptr_t FN_TEXT_LOOKUP = 0x2E28F0;      // const char*(const char* key, const char** outKey) [P]
static const uintptr_t RVA_TEXT_MISSING = 0x10267E8;   // *(char**): what TextLookup returns for no key [P]
static const uintptr_t FN_LEGOTEXT = 0x8E76D0, RVA_LEGOTEXT = 0x1190CD8;   // const char*(LegoapiText*, int) [P]
static const uintptr_t RVA_LEGOTEXT_MISSING = 0x1190CA8;                    // its out-of-range answer [P]
static const int LT_ON = 0x7A, LT_OFF = 0x7B, LT_BACK = 0x1E;              // [P 0x14080A5xx uses]
static const uintptr_t FN_SFX_ID = 0x7A4370, RVA_LEGOSFX = 0x17EBC08;      // int16(LegoSFX*, const void* ref) [P]
static const uintptr_t SFX_SELECT = 0x11963D0, SFX_BACK = 0x11963D4, SFX_NOENTRY = 0x11963D8;   // [P use, I names]
static const uintptr_t RVA_MENU_SOUND = 0x115EFC0;     // int: the sound MenuUpdate plays after update() [P]
// Menu struct (0xF0) fields [P MenuUpdate 0x1405509B9-0x140550A5F, the pause draw's end 0x14080B4C2]
static const uintptr_t M_ROWMIN = 0x0E, M_ROWMAX = 0x12, M_CURSOR = 0x40, M_LEFT = 0x68, M_RIGHT = 0x6C,
                       M_ACCEPT = 0x70, M_BACK = 0x74, M_COUNTER = 0x98;

typedef void* (*MenuOpenFn)(int id, int startRow, int unused);
typedef void (*MenuPopFn)();
typedef int (*CurMenuIdFn)();
typedef void (*MenuItemFn)(void* m, const char* text);
typedef const char* (*TextLookupFn)(const char* key, const char** outKey);
typedef const char* (*LegoTextFn)(void* legoText, int id);
typedef short (*SfxIdFn)(void* legoSfx, const void* ref);

template <class T> static inline T& F(void* m, uintptr_t off) { return *reinterpret_cast<T*>(reinterpret_cast<uint8_t*>(m) + off); }

// ---- state (game thread unless marked) --------------------------------------------------------------
static uintptr_t      g_b = 0;
static volatile bool  g_ready = false;        // the three slots hold ours (hotkey / startup thread writes)
static volatile bool  g_menuFault = false;    // latched for the session
static volatile bool  g_removed = false;      // RemoveAll ran (unhook)
static volatile bool  g_switchedOff = false;  // Switch Off Until Restart confirmed: the menu goes at once, the
                                              // unhook itself waits for the game to run (mbr::UnhookGate)
static volatile bool  s_textFault = false;    // the game's text lookup faulted: compiled English from then on
static void**         s_slot[3] = {};
static uint64_t       s_orig[3] = {}, s_ours[3] = {};
static int            g_page = 0;             // the section the 0x4C01 page shows (set before every MenuOpen)
static mlg::State     s_st;
static mlg::Draft     s_drafts[mt::kNumItems] = {};
static LONG           s_gen = 0;
static LONG           s_seenFailSeq = 0;
static int            s_modeWant = -1;
static DWORD          s_modeT = 0;
static bool           s_everRegistered = false, s_refusedLogged = false, s_stringsLogged = false;
#if LCUD_MENU_FAULT_TEST
static bool           s_faultTest = false;
#endif

static inline bool Inert() { return !g_ready || g_menuFault || g_removed || g_switchedOff || g_unhooked; }

static int MenuFault(EXCEPTION_POINTERS* ep, const char* phase)
{
    if (!g_menuFault)
    {
        g_menuFault = true;
        const uintptr_t at = reinterpret_cast<uintptr_t>(ep->ExceptionRecord->ExceptionAddress);
        HMODULE mod = nullptr;
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(at), &mod);
        Log("!!! EXCEPTION 0x%08lX in the pause-menu code (%s) at %p (rva 0x%llX) - Mod Options is off for this "
            "session; the game's pause menu and the rest of lcudrive keep running.\n",
            ep->ExceptionRecord->ExceptionCode, phase, (void*)at,
            (unsigned long long)(at - reinterpret_cast<uintptr_t>(mod)));
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

static int TextFault(EXCEPTION_POINTERS* ep)
{
    if (!s_textFault)
    {
        s_textFault = true;
        Log("!!! EXCEPTION 0x%08lX in the game's text lookup at %p - Mod Options uses its own English for this "
            "session.\n", ep->ExceptionRecord->ExceptionCode, ep->ExceptionRecord->ExceptionAddress);
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

// ---- strings: the game's text table first, the compiled English otherwise (every frame, no caching: a
//      language reload could free the table) ------------------------------------------------------------
static const char* Lookup(const char* key)            // nullptr = not in the table
{
    if (!key || s_textFault || g_menuFault) return nullptr;
    __try
    {
        const char* s = reinterpret_cast<TextLookupFn>(g_b + FN_TEXT_LOOKUP)(key, nullptr);
        return mlg::TableText(s, *reinterpret_cast<const char* const*>(g_b + RVA_TEXT_MISSING));
    }
    __except (TextFault(GetExceptionInformation())) { return nullptr; }
}
static const char* Str(const char* key, const char* en) { const char* s = Lookup(key); return s ? s : en; }

static const char* Lego(int id, const char* en)
{
    if (s_textFault || g_menuFault) return en;
    __try
    {
        const char* s = reinterpret_cast<LegoTextFn>(g_b + FN_LEGOTEXT)(reinterpret_cast<void*>(g_b + RVA_LEGOTEXT), id);
        s = mlg::TableText(s, *reinterpret_cast<const char* const*>(g_b + RVA_LEGOTEXT_MISSING));
        return s ? s : en;
    }
    __except (TextFault(GetExceptionInformation())) { return en; }
}

struct GameStrings : mlg::Strings
{
    const char* Text(const char* key, const char* en) const override { return Str(key, en); }
    const char* On() const override { return Lego(LT_ON, "On"); }
    const char* Off() const override { return Lego(LT_OFF, "Off"); }
    const char* Sure() const override { return Str("MENU_GEN_AREYOUSURE", "Are you sure?"); }
    const char* Back() const override { return Lego(LT_BACK, "Back"); }
};
static const GameStrings s_strings{};

// ---- small game calls (inside the caller's SEH) --------------------------------------------------------
static void QueueSound(uintptr_t ref)              // exactly what Options does [P 0x14080A14B-0x14080A161]
{
    void* sfx = *reinterpret_cast<void**>(g_b + RVA_LEGOSFX);
    if (!sfx) return;
    const short id = reinterpret_cast<SfxIdFn>(g_b + FN_SFX_ID)(sfx, reinterpret_cast<const void*>(g_b + ref));
    *reinterpret_cast<volatile int*>(g_b + RVA_MENU_SOUND) = static_cast<int>(id);
}
static void Sound(mlg::Sound s)
{
    if (s == mlg::sSelect) QueueSound(SFX_SELECT);
    else if (s == mlg::sBack) QueueSound(SFX_BACK);
    else if (s == mlg::sNoEntry) QueueSound(SFX_NOENTRY);
}
static void MenuItem(void* m, const char* text) { reinterpret_cast<MenuItemFn>(g_b + FN_MENU_ITEM_AUTO)(m, text); }
static void MenuPop() { reinterpret_cast<MenuPopFn>(g_b + FN_MENU_POP)(); }
static int  CurMenuId() { return reinterpret_cast<CurMenuIdFn>(g_b + FN_CUR_MENU_ID)(); }

// The end of every page draw, exactly as the pause draw does it (16-bit) [P 0x14080B4C2-0x14080B4E4].
static void FixRowMax(void* m)
{
    F<uint16_t>(m, M_ROWMAX) = static_cast<uint16_t>(F<uint16_t>(m, M_COUNTER) - 1 + F<uint16_t>(m, M_ROWMIN));
}

// ---- registration (game thread, lazily from HasRow: after MenuSystem_Init 0x14054F700 has filled the
//      table - an earlier write would be wiped by its count = 47) ------------------------------------------
static void MenuEnter(void* m);
static void RootDraw(void* m);
static void RootUpdate(void* m);
static void PageDraw(void* m);
static void PageUpdate(void* m);

struct LiveDefWriter : mlg::DefWriter
{
    void Record(mlg::MenuDef* table, int slot, const mlg::MenuDef& rec) override
    {
        memcpy(&table[slot], &rec, sizeof(rec));      // .data, writable [P: the game's own init writes it]
        MemoryBarrier();
    }
    void Count(volatile LONG* count, LONG v) override { InterlockedExchange(count, v); }   // after the record
};

static bool EnsureRegistered()                      // inside the caller's SEH
{
    mlg::Ours o;
    o.enter = reinterpret_cast<uint64_t>(&MenuEnter);
    o.rootDraw = reinterpret_cast<uint64_t>(&RootDraw);
    o.rootUpdate = reinterpret_cast<uint64_t>(&RootUpdate);
    o.pageDraw = reinterpret_cast<uint64_t>(&PageDraw);
    o.pageUpdate = reinterpret_cast<uint64_t>(&PageUpdate);
    LiveDefWriter w;
    mlg::RegInfo ri;
    const mlg::Reg r = mlg::EnsureRegistered(reinterpret_cast<mlg::MenuDef*>(g_b + RVA_MENU_DEFS),
                                             reinterpret_cast<volatile LONG*>(g_b + RVA_MENU_DEF_N), o, g_b, w, &ri);
    if (r == mlg::regOk) return true;
    if (r == mlg::regAdded)
    {
        if (!s_everRegistered)
            Log("menu: registered Mod Options (ids 0x4C00, 0x4C01) at slots %d-%d, %d/128 used; tail copied from menu 4\n",
                ri.slotRoot, ri.slotPage, ri.count);
        else
            Log("menu: the menu table was reset (count %d) - registered again\n", ri.count - ri.added);
        s_everRegistered = true;
        return true;
    }
    if (!s_refusedLogged)
    {
        s_refusedLogged = true;
        char why[128];
        switch (r)
        {
        case mlg::regFull:  _snprintf_s(why, sizeof(why), _TRUNCATE, "table full %d/128", ri.count); break;
        case mlg::regTaken: _snprintf_s(why, sizeof(why), _TRUNCATE, "id 0x%X taken by rva 0x%llX", ri.takenId,
                                        (unsigned long long)(ri.takenBy - g_b)); break;
        case mlg::regNo2:   _snprintf_s(why, sizeof(why), _TRUNCATE, "menu 2 not found"); break;
        case mlg::regNo4:   _snprintf_s(why, sizeof(why), _TRUNCATE, "menu 4 not found"); break;
        case mlg::regBad2:  _snprintf_s(why, sizeof(why), _TRUNCATE, "menu 2 layout: draw rva 0x%llX", (unsigned long long)(ri.badDraw - g_b)); break;
        case mlg::regBad4:  _snprintf_s(why, sizeof(why), _TRUNCATE, "menu 4 layout: draw rva 0x%llX", (unsigned long long)(ri.badDraw - g_b)); break;
        default:            _snprintf_s(why, sizeof(why), _TRUNCATE, "menu count %d makes no sense", ri.count); break;
        }
        Log("menu: REFUSED - %s; no Mod Options row this session\n", why);
    }
    return false;
}

// Every MenuOpen goes through here: MenuOpen with an id it cannot find closes the menu on top and leaves
// the game paused with no menu [P 0x140550245], so an unregistered id is never passed.
static bool SafeOpen(int32_t id, int startRow)      // inside the caller's SEH
{
    if (!EnsureRegistered())
    {
        QueueSound(SFX_NOENTRY);
        InterlockedExchange(&mbr::g_noteOpenFail, 1);
        return false;
    }
    reinterpret_cast<MenuOpenFn>(g_b + FN_MENU_OPEN)(id, startRow, -1);
    return true;
}

// ---- what the rows show -------------------------------------------------------------------------------
static void FillView(mlg::View& v)
{
    // the gens BEFORE the Config index: the hotkey thread stores doneGen after its swap, so a new doneGen
    // means the new index is already there (volatile reads are acquires with MSVC's /volatile:ms on x64)
    v.doneGen = mbr::g_doneGen;
    v.failGen = mbr::g_failGen;
    v.live = &g_cfgRaw[g_cfgIdx];                    // [missions1] the ini's values, not the mission-masked live ones
    v.drafts = s_drafts;
    v.modeShown = mlg::ModeShown(g_enabled, s_modeWant, GetTickCount() - s_modeT);
    v.wantedPick = s_st.wantedPick;
    const LONG c = pad::g_connected;
    int n = 0;
    for (int i = 0; i < 4; ++i) n += (c >> i) & 1;
    v.padsFound = n;
    v.armedPage = s_st.armedPage;
    v.armedRow = s_st.armedRow;
}

static void DrawRows(void* m, int page)
{
    mlg::View v;
    FillView(v);
    mlg::Row rows[mlg::kMaxRows];
    const int n = mlg::Rows(page, rows, mlg::kMaxRows);
    char buf[128];
    for (int r = 0; r < n; ++r)
    {
        mlg::RowText(page, r, rows[r], v, s_strings, buf, sizeof(buf));
        MenuItem(m, buf);                           // consumed during the call [P: Options reuses one buffer]
    }
}

static bool DrawBody(void* m, int page)
{
    __try
    {
#if LCUD_MENU_FAULT_TEST
        if (s_faultTest && page != mlg::kRoot) { s_faultTest = false; RaiseException(0xE04C4D01u, 0, 0, nullptr); }
#endif
        DrawRows(m, page);
        FixRowMax(m);
        return true;
    }
    __except (MenuFault(GetExceptionInformation(), page == mlg::kRoot ? "Mod Options draw" : "section draw")) { return false; }
}

// faulted / unhooked: one row (Back), so the menu is never empty while it backs out
static void DrawInert(void* m)
{
    __try
    {
        MenuItem(m, Lego(LT_BACK, "Back"));
        FixRowMax(m);
    }
    __except (MenuFault(GetExceptionInformation(), "inert draw")) {}
}

static void InertUpdate()                           // one MenuPop per call, until the top is not ours
{
    __try
    {
        const int id = CurMenuId();
        if (id == mlg::kRootId || id == mlg::kPageId) MenuPop();
    }
    __except (MenuFault(GetExceptionInformation(), "back out")) {}
}

// ---- one frame of a page ------------------------------------------------------------------------------
static void UpdateFrame(void* m, int page)
{
    mlg::Input in;
    in.cursor = F<int>(m, M_CURSOR);
    in.left = F<int>(m, M_LEFT) != 0;
    in.right = F<int>(m, M_RIGHT) != 0;
    in.accept = F<int>(m, M_ACCEPT) != 0;
    in.back = F<int>(m, M_BACK) != 0;
    mlg::View v;
    FillView(v);
    const LONG fs = mbr::g_failSeq;                 // a refused save: the no-entry sound, once (the draft is
    const bool refused = fs != s_seenFailSeq;       // already hidden by the display rule)
    s_seenFailSeq = fs;

    const mlg::Out o = mlg::Decide(page, in, v, s_st);
    if (o.item >= 0)
    {
        const LONG gen = ++s_gen;
        s_drafts[o.item].val = o.val;
        s_drafts[o.item].gen = gen;
        mbr::Post(o.edits, o.nEdits, gen, GetTickCount());
    }
    switch (o.req)
    {
    case mlg::qMode:     s_modeWant = o.reqArg > 0 ? 1 : 0; s_modeT = GetTickCount(); InterlockedExchange(&mbr::g_modeReq, o.reqArg); break;
    case mlg::qDefaults: InterlockedExchange(&mbr::g_defaultsReq, 1); break;
    case mlg::qAirliner: InterlockedExchange(&ap::g_recall, 1); InterlockedExchange(&mbr::g_noteAirliner, 1); break;
    case mlg::qWanted:
    {
        // for the player who paused: the game's own "whose pad drives the menu" int (menu_logic.h
        // GamePadRouting: 0 / 1, -1 = either pad -> both players). wanted.h hands a level for a player
        // who isn't in the game to player 1.
        const int pauser = *reinterpret_cast<const int*>(g_base + 0x11754F0);
        wd::RequestFor(pauser, o.reqArg);
        InterlockedExchange(&mbr::g_noteWanted, (o.reqArg + 1) | ((pauser == 0 ? 1 : pauser == 1 ? 2 : 0) << 8));
        break;
    }
    case mlg::qReload:   InterlockedExchange(&mbr::g_reloadReq, 1); break;
    case mlg::qUnhook:   g_switchedOff = true; InterlockedExchange(&mbr::g_unhookReq, 1); break;   // pages back out from the next update
    default: break;
    }
    if (o.saveReq) InterlockedExchange(&mbr::g_saveReq, 1);
    if (o.openPage >= 0)
    {
        g_page = o.openPage;                        // before MenuOpen: its counting pass draws this page
        if (!SafeOpen(mlg::kPageId, o.startRow)) return;
    }
    if (o.pop) MenuPop();                           // the same order as Options: pop, then the sound [P 0x14080A146]
    if (o.sound != mlg::sNone) Sound(o.sound);
    else if (refused) QueueSound(SFX_NOENTRY);
}

static bool UpdateBody(void* m, int page)
{
    __try { UpdateFrame(m, page); return true; }
    __except (MenuFault(GetExceptionInformation(), page == mlg::kRoot ? "Mod Options update" : "section update")) { return false; }
}

// ---- the callbacks the game calls -----------------------------------------------------------------------
static void DrawPage(void* m, int page)
{
    InterlockedIncrement(&g_inFlight);
    if (Inert() || !DrawBody(m, page)) DrawInert(m);
    InterlockedDecrement(&g_inFlight);
}
static void UpdatePage(void* m, int page)
{
    InterlockedIncrement(&g_inFlight);
    if (Inert() || !UpdateBody(m, page)) InertUpdate();
    InterlockedDecrement(&g_inFlight);
}
static void RootDraw(void* m)   { DrawPage(m, mlg::kRoot); }
static void RootUpdate(void* m) { UpdatePage(m, mlg::kRoot); }
static void PageDraw(void* m)   { DrawPage(m, g_page); }
static void PageUpdate(void* m) { UpdatePage(m, g_page); }

// onEnter of both pages (MenuOpen, and MenuPop revealing one): a confirm never survives a page change
static void MenuEnter(void*)
{
    InterlockedIncrement(&g_inFlight);
    mlg::Disarm(s_st);
    s_seenFailSeq = mbr::g_failSeq;
    InterlockedDecrement(&g_inFlight);
}

static bool HasRowBody()
{
    __try
    {
        if (!EnsureRegistered()) return false;
        if (!s_stringsLogged)
        {
            s_stringsLogged = true;
            Log(Lookup("LCUD_MENU") ? "menu: strings from the game's text table (patch.csv rows found)\n"
                                    : "menu: strings are compiled-in English (LCUD_MENU not in the text table)\n");
        }
        return true;
    }
    __except (MenuFault(GetExceptionInformation(), "has row")) { return false; }
}

// vt+0x358: the pause draw and update both ask; the answer must not change between them in a frame
static bool HasRow(void*, void*)
{
    InterlockedIncrement(&g_inFlight);
    if (!g_unhooked) pw::Tick("pause");      // [stationfix] also in the frame that accepts "Return to police station"
    const bool r = !Inert() && HasRowBody();
    InterlockedDecrement(&g_inFlight);
    return r;
}

// vt+0x350: EXACTLY one row whenever it is called (the game called HasRow first and counts one row)
static void AddRow(void*, void* m)
{
    InterlockedIncrement(&g_inFlight);
    const char* text = Inert() ? "Mod Options" : Str("LCUD_MENU", "Mod Options");   // Str has its own guard
    __try { MenuItem(m, text); }
    __except (MenuFault(GetExceptionInformation(), "pause row")) {}
    InterlockedDecrement(&g_inFlight);
}

static void OnRowBody()
{
    __try { SafeOpen(mlg::kRootId, s_st.rootRow); }   // the pause handler already queued SELECT [P 0x14080ABD4]
    __except (MenuFault(GetExceptionInformation(), "open")) {}
}

// vt+0x360: Accept with the cursor on our row [P 0x14080AB94, 0x14080ADD0]
static void OnRow(void*, void*)
{
    InterlockedIncrement(&g_inFlight);
    if (Inert())
    {
        __try { QueueSound(SFX_NOENTRY); }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
    }
    else OnRowBody();
    InterlockedDecrement(&g_inFlight);
}

// ---- install / remove (startup thread / hotkey thread) -----------------------------------------------------
struct VtWriter : mlg::SlotWriter
{
    bool Write(int slot, uint64_t expect, uint64_t value) override
    {
        DWORD old = 0;
        if (!value || !expect || !s_slot[slot]) return false;            // never a null slot
        if (!VirtualProtect(s_slot[slot], sizeof(void*), PAGE_READWRITE, &old)) return false;
        // one aligned 8-byte compare-exchange: only over what we expect to be there
        const void* was = InterlockedCompareExchangePointer(s_slot[slot], reinterpret_cast<void*>(value),
                                                            reinterpret_cast<void*>(expect));
        VirtualProtect(s_slot[slot], sizeof(void*), old, &old);
        return reinterpret_cast<uint64_t>(was) == expect;
    }
};

static bool ReadSlots(uint64_t cur[3])
{
    __try
    {
        for (int i = 0; i < 3; ++i) cur[i] = reinterpret_cast<uint64_t>(*s_slot[i]);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

static void Install(uintptr_t base, const char* iniPath)
{
    g_b = base;
    if (g_unhooked) return;                         // the Pause key beat us to it
#if LCUD_MENU_FAULT_TEST
    s_faultTest = GetPrivateProfileIntA("menu", "fault_test", 0, iniPath) != 0;
    if (s_faultTest) Log("menu: FAULT DRILL build - the first section draw throws once\n");
#else
    (void)iniPath;
#endif
    for (int i = 0; i < 3; ++i) s_slot[i] = reinterpret_cast<void**>(base + mlg::RVA_VT_GAME + mlg::kSlotOff[i]);
    s_ours[0] = reinterpret_cast<uint64_t>(&AddRow);
    s_ours[1] = reinterpret_cast<uint64_t>(&HasRow);
    s_ours[2] = reinterpret_cast<uint64_t>(&OnRow);
    uint64_t cur[3] = {};
    if (!ReadSlots(cur)) { Log("  patch pause row     REFUSED: the LegoCityGame vtable cannot be read\n"); return; }
    int bad = 0;
    const mlg::Slots c = mlg::CheckSlots(cur, base, s_ours, &bad);
    if (c == mlg::slotsOurs)                       // installed before in this process: s_orig is already the
    {                                              // stubs; never leave it empty (a restore would write 0)
        const uint64_t stock[3] = { base + mlg::RVA_STUB_RET, base + mlg::RVA_STUB_FALSE, base + mlg::RVA_STUB_RET };
        for (int i = 0; i < 3; ++i) if (!s_orig[i]) s_orig[i] = stock[i];
        g_ready = true;
        return;
    }
    if (c == mlg::slotsForeign)
    {
        const uint64_t want = base + (bad == 1 ? mlg::RVA_STUB_FALSE : mlg::RVA_STUB_RET);
        Log("  patch pause row     REFUSED: vt +0x%X holds rva 0x%llX, expected 0x%llX (another mod?)\n",
            mlg::kSlotOff[bad], (unsigned long long)(cur[bad] - base), (unsigned long long)(want - base));
        return;
    }
    memcpy(s_orig, cur, sizeof(s_orig));
    VtWriter w;
    if (!mlg::InstallSlots(w, s_orig, s_ours))
    {
        Log("  patch pause row     REFUSED: a LegoCityGame vtable slot could not be written or changed meanwhile (%lu)\n", GetLastError());
        return;
    }
    g_ready = true;
    Log("  patch pause row     ok: LegoCityGame vt +0x350/+0x358/+0x360 -> lcudrive\n");
}

// Unhook, first thing: the stock HasRow back, so the pause menu stops offering the row.
static void HideRow()
{
    if (!g_ready) return;
    VtWriter w;
    mlg::HideSlots(w, s_orig, s_ours);
}

// Unhook, after its drain: AddRow / OnRow back too, the pages inert. The two menu records stay in the
// table (the DLL is never unloaded); their callbacks only back out from now on.
static void RemoveAll()
{
    if (!g_ready) return;
    g_removed = true;
    VtWriter w;
    mlg::RestoreSlots(w, s_orig, s_ours);
    g_ready = false;
    Log("menu: pause row removed (unhook)\n");
}

} // namespace pmn
