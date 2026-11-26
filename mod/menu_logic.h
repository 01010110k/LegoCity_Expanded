// =============================================================================================
//  menu_logic.h - the Mod Options pages as pure logic (no game memory, no file I/O, no Windows calls).
//
//  pausemenu.h reads the game's Menu fields into an Input, calls Decide() once a frame and executes
//  the Out; it draws each row with RowText(). Both draw and update take their rows from Rows(page),
//  so the row count of a page is a constant and the row-to-item mapping can never differ between the
//  draw, the update and the game's own counting pass (MenuOpen draws once just to count, 0x140550290).
//
//  Also here, so the harness can drive them on fakes: the menu-table registration (EnsureRegistered)
//  and the LegoCityGame vtable slot check / write order (CheckSlots, InstallSlots, HideSlots, RestoreSlots).
// =============================================================================================
#pragma once
#include "menu_bridge.h"
#include <cstdint>

namespace mlg {

// ---- pages -------------------------------------------------------------------------------------------
static const int kRoot = -1;          // the Mod Options page; 0..7 = the section pages (mt::kSections)
static const int kMaxRows = 12;

enum RowKind : unsigned char { rItem, rSection };
struct Row { RowKind kind; short item; short section; };

inline int ItemIndex(const char* id)
{
    for (int i = 0; i < mt::kNumItems; ++i) if (!strcmp(mt::kItems[i].id, id)) return i;
    return -1;
}

// The rows of a page, in order. Root: its plain items (Mod Effects), the 8 sections, then its confirm
// actions (Reset to Defaults). A section page: its items in table order, v1-hidden ones left out.
inline int Rows(int page, Row* out, int cap)
{
    int n = 0;
    auto add = [&](RowKind k, int item, int sec) { if (n < cap) { out[n].kind = k; out[n].item = static_cast<short>(item); out[n].section = static_cast<short>(sec); } ++n; };
    if (page == kRoot)
    {
        for (int i = 0; i < mt::kNumItems; ++i)
            if (mt::kItems[i].section == -1 && !mt::kItems[i].v1Hidden && !mt::kItems[i].confirm) add(rItem, i, -1);
        for (int s = 0; s < mt::kNumSections; ++s) add(rSection, -1, s);
        for (int i = 0; i < mt::kNumItems; ++i)
            if (mt::kItems[i].section == -1 && !mt::kItems[i].v1Hidden && mt::kItems[i].confirm) add(rItem, i, -1);
    }
    else if (page >= 0 && page < mt::kNumSections)
    {
        for (int i = 0; i < mt::kNumItems; ++i)
            if (mt::kItems[i].section == page && !mt::kItems[i].v1Hidden) add(rItem, i, page);
    }
    return n < cap ? n : cap;
}
inline int RowCount(int page) { Row r[kMaxRows]; return Rows(page, r, kMaxRows); }

// ---- values ------------------------------------------------------------------------------------------
struct Val { int c; float v; };       // toggles: c 0/1; choices: c the index (-1 = Custom); sliders: v
struct Draft { Val val; LONG gen; };  // gen 0 = no draft

inline Val Live(const mt::Item& it, const Config& cfg)
{
    Val r{ -1, 0.0f };
    if (it.kind == mt::kSlider) r.v = mbr::CurrentSlider(it, cfg);
    else if (it.kind == mt::kToggle || it.kind == mt::kChoice) r.c = mbr::CurrentChoice(it, cfg);
    return r;
}

inline bool SameVal(const mt::Item& it, Val a, Val b)
{
    if (it.kind == mt::kSlider) return std::fabs(a.v - b.v) < it.step * 1e-3f;
    return a.c == b.c;
}

// What the page shows for an item: the draft while it is newer than every save that finished, else the
// live Config (callers read the gens BEFORE the Config index - see pausemenu.h).
inline Val Shown(const mt::Item& it, const Draft& d, LONG doneGen, LONG failGen, const Config& live)
{
    if (d.gen > 0 && mbr::ShowDraft(d.gen, doneGen, failGen)) return d.val;
    return Live(it, live);
}

// One step. Sliders: to the next grid point in that direction (a value between grid points goes to the
// neighbouring one), clamped at the ends, no wrap; a value beyond an end comes back to it. Toggles flip.
// Choices: the next one with wrap, hidden choices skipped (a hidden live one is shown, never offered);
// from Custom, Right goes to the first and Left to the last.
inline Val Step(const mt::Item& it, Val cur, int dir)
{
    if (dir == 0) return cur;
    if (it.kind == mt::kToggle) { Val r = cur; r.c = cur.c ? 0 : 1; return r; }
    if (it.kind == mt::kChoice)
    {
        const int n = it.nChoices;
        int i = cur.c;
        for (int k = 0; k < n; ++k)
        {
            if (i < 0 || i >= n) i = dir > 0 ? 0 : n - 1;
            else i = ((i + (dir > 0 ? 1 : -1)) % n + n) % n;
            if (!it.choices[i].hidden) { Val r = cur; r.c = i; return r; }
        }
        return cur;
    }
    if (it.kind != mt::kSlider) return cur;
    float v = cur.v;
    if (!(v == v)) v = it.lo;
    const float eps = it.step * 1e-3f;
    const float k = (v - it.lo) / it.step;
    const float kr = std::round(k);
    const bool onGrid = std::fabs(k - kr) < 1e-3f;
    float nv;
    if (dir > 0)
    {
        if (v >= it.hi - eps) return cur;                      // at (or past) the top: stays
        nv = it.lo + (onGrid ? kr + 1.0f : std::floor(k) + 1.0f) * it.step;
    }
    else
    {
        if (v <= it.lo + eps) return cur;                      // at (or below) the bottom: stays
        nv = it.lo + (onGrid ? kr - 1.0f : std::ceil(k) - 1.0f) * it.step;
    }
    Val r = cur;
    r.v = mbr::Snap(it, nv);
    return r;
}

// ---- strings -----------------------------------------------------------------------------------------
// The game's text, with the compiled English as the fallback (pausemenu.h: TextLookup / LegoText).
struct Strings
{
    virtual const char* Text(const char* key, const char* en) const = 0;
    virtual const char* On() const = 0;
    virtual const char* Off() const = 0;
    virtual const char* Sure() const = 0;       // MENU_GEN_AREYOUSURE
    virtual const char* Back() const = 0;
};

// What the game's lookup gave, or nullptr when it is no real text: null, the lookup's own "missing" answer
// (TextLookup: *(char**)0x1410267E8 = " "), an empty string or a lone space. The caller then uses English.
inline const char* TableText(const char* got, const char* missing)
{
    if (!got || got == missing || !got[0] || (got[0] == ' ' && !got[1])) return nullptr;
    return got;
}

inline const char* ChoiceText(const mt::Choice& c, const Strings& s)
{
    if (!c.textKey) return c.en;
    if (!strcmp(c.textKey, "ON")) return s.On();
    if (!strcmp(c.textKey, "OFF")) return s.Off();
    return s.Text(c.textKey, c.en);
}

// Cut a string at cap-1 bytes without splitting a UTF-8 sequence (the game's own strings may be UTF-8).
inline void Utf8Fit(char* s)
{
    const size_t n = strlen(s);
    size_t lead = n;
    int back = 0;
    while (lead > 0 && back < 4 && (static_cast<unsigned char>(s[lead - 1]) & 0xC0) == 0x80) { --lead; ++back; }
    if (lead == 0) return;
    const unsigned char b = static_cast<unsigned char>(s[lead - 1]);
    const size_t want = b < 0x80 ? 1 : (b >> 5) == 6 ? 2 : (b >> 4) == 14 ? 3 : (b >> 3) == 30 ? 4 : 1;
    if (n - (lead - 1) < want) s[lead - 1] = 0;
}

// The game-thread state the rows show besides the Config.
struct View
{
    const Config* live;
    const Draft*  drafts;          // [mt::kNumItems]
    LONG          doneGen, failGen;
    int           modeShown;       // Mod Effects: 1 on, 0 stock
    int           wantedPick;      // Set Wanted Level: 0..5
    int           padsFound;       // Controllers Found
    int           armedPage, armedRow;
};

// One row's text: "<Label>: <value>" (Options' own format, 0x140C87744), a section / plain action as its
// label, an armed confirm as "<Label>: Are you sure?".
inline void RowText(int page, int r, const Row& row, const View& v, const Strings& s, char* out, size_t cap)
{
    out[0] = 0;
    if (row.kind == rSection)
    {
        const mt::Section& sec = mt::kSections[row.section];
        _snprintf_s(out, cap, _TRUNCATE, "%s", s.Text(sec.textKey, sec.en));
        Utf8Fit(out);
        return;
    }
    const mt::Item& it = mt::kItems[row.item];
    const char* label = s.Text(it.labelKey, it.label);
    char val[48] = {};
    const char* value = nullptr;
    switch (it.kind)
    {
    case mt::kToggle:
    case mt::kChoice:
    {
        const Val x = Shown(it, v.drafts[row.item], v.doneGen, v.failGen, *v.live);
        if (it.kind == mt::kToggle) value = x.c ? s.On() : s.Off();
        else value = (x.c >= 0 && x.c < it.nChoices) ? ChoiceText(it.choices[x.c], s) : "Custom";
        break;
    }
    case mt::kSlider:
        mbr::SliderText(it, Shown(it, v.drafts[row.item], v.doneGen, v.failGen, *v.live).v, val, sizeof(val));
        value = val;
        break;
    case mt::kAction:
        if (it.confirm && v.armedPage == page && v.armedRow == r) value = s.Sure();
        else if (!strcmp(it.id, "mode") && it.nChoices >= 2) value = ChoiceText(it.choices[v.modeShown ? 0 : 1], s);
        else if (!strcmp(it.id, "set_wanted") && it.nChoices > 0)
            value = ChoiceText(it.choices[v.wantedPick >= 0 && v.wantedPick < it.nChoices ? v.wantedPick : 0], s);
        break;
    case mt::kInfo:
        _snprintf_s(val, sizeof(val), _TRUNCATE, "%d", v.padsFound);
        value = val;
        break;
    }
    if (value) _snprintf_s(out, cap, _TRUNCATE, "%s: %s", label, value);
    else _snprintf_s(out, cap, _TRUNCATE, "%s", label);
    Utf8Fit(out);
}

// ---- input -> outputs --------------------------------------------------------------------------------
// The Menu struct's flags this frame [P MenuUpdate 0x1405509B9-0x140550A5F]: +0x40 cursor, +0x68 Left,
// +0x6C Right (both auto-repeat), +0x70 Accept, +0x74 Back. Up/Down and Start belong to the game.
struct Input { int cursor; bool left, right, accept, back; };
enum Sound : unsigned char { sNone, sSelect, sBack, sNoEntry };
enum Req : unsigned char { qNone, qMode, qDefaults, qAirliner, qWanted, qReload, qUnhook };

struct State                          // the game thread's menu state (pausemenu.h owns one)
{
    int armedPage = -2, armedRow = -1;    // an armed two-press confirm
    int wantedPick = 0;                   // Set Wanted Level
    int rootRow = 0;                      // cursor memory: the root page and each section page
    int pageRow[mt::kNumSections] = {};
};

struct Out
{
    int item = -1; Val val{ -1, 0.0f };   // item >= 0: a value change (kept as a draft, edits posted)
    int nEdits = 0; mini::Edit edits[3];
    Req req = qNone; int reqArg = 0;
    int openPage = -1; int startRow = 0;  // openPage >= 0: open the section page at startRow
    bool pop = false, saveReq = false;
    Sound sound = sNone;
};

inline void Disarm(State& st) { st.armedPage = -2; st.armedRow = -1; }

inline Req ActionReq(const mt::Item& it)
{
    if (!strcmp(it.id, "defaults")) return qDefaults;
    if (!strcmp(it.id, "call_airliner")) return qAirliner;
    if (!strcmp(it.id, "reload")) return qReload;
    if (!strcmp(it.id, "unhook")) return qUnhook;
    return qNone;
}

// One frame of one page. Back first (a confirm armed: it only disarms); Left/Right (a value step, the wanted
// pick, Mod Effects); Accept (a toggle / choice steps +1, a section opens, an action runs or arms its
// confirm). A cursor move or another page disarms a confirm.
inline Out Decide(int page, const Input& in, const View& v, State& st)
{
    Out o;
    Row rows[kMaxRows];
    const int n = Rows(page, rows, kMaxRows);
    if (st.armedRow >= 0 && (st.armedPage != page || st.armedRow != in.cursor)) Disarm(st);
    if (in.back)
    {
        if (st.armedRow >= 0) { Disarm(st); return o; }
        if (page == kRoot) st.rootRow = in.cursor;
        else if (page >= 0 && page < mt::kNumSections) st.pageRow[page] = in.cursor;
        o.saveReq = true; o.pop = true; o.sound = sBack;
        return o;
    }
    if (in.cursor < 0 || in.cursor >= n) return o;
    const Row& row = rows[in.cursor];
    int dir = (in.right ? 1 : 0) - (in.left ? 1 : 0);
    if (row.kind == rSection)
    {
        if (!in.accept) return o;
        const int k = row.section, rc = RowCount(k);
        int sr = st.pageRow[k];
        if (sr < 0) sr = 0;
        if (sr >= rc) sr = rc - 1;
        o.openPage = k; o.startRow = sr; o.sound = sSelect;
        return o;
    }
    const mt::Item& it = mt::kItems[row.item];
    switch (it.kind)
    {
    case mt::kToggle:
    case mt::kChoice:
    case mt::kSlider:
    {
        if (dir == 0 && in.accept && it.kind != mt::kSlider) dir = 1;
        if (dir == 0) return o;
        const Val cur = Shown(it, v.drafts[row.item], v.doneGen, v.failGen, *v.live);
        const Val nv = Step(it, cur, dir);
        if (SameVal(it, cur, nv)) return o;                    // at an end: nothing
        o.nEdits = mbr::MakeEdits(it, nv.c, nv.v, o.edits);
        if (o.nEdits <= 0) return o;
        o.item = row.item; o.val = nv; o.sound = sSelect;
        return o;
    }
    case mt::kAction:
        if (!strcmp(it.id, "mode"))
        {
            if (dir == 0 && !in.accept) return o;
            o.req = qMode; o.reqArg = v.modeShown ? -1 : 1; o.sound = sSelect;
            return o;
        }
        if (!strcmp(it.id, "set_wanted"))
        {
            const int nc = it.nChoices > 0 ? it.nChoices : 6;
            if (dir != 0) { st.wantedPick = ((st.wantedPick + dir) % nc + nc) % nc; o.sound = sSelect; }
            else if (in.accept) { o.req = qWanted; o.reqArg = st.wantedPick < 0 ? 0 : (st.wantedPick > 5 ? 5 : st.wantedPick); o.sound = sSelect; }
            return o;
        }
        if (!in.accept) return o;
        if (it.confirm)
        {
            if (st.armedPage == page && st.armedRow == in.cursor) { Disarm(st); o.req = ActionReq(it); }
            else { st.armedPage = page; st.armedRow = in.cursor; }
            o.sound = sSelect;
            return o;
        }
        o.req = ActionReq(it);
        if (o.req != qNone) o.sound = sSelect;
        return o;
    case mt::kInfo:
        return o;
    }
    return o;
}

// Mod Effects as shown: the request until lcudrive's state matches it, or for 1 s.
inline int ModeShown(bool enabled, int want, DWORD msSinceReq)
{
    const int e = enabled ? 1 : 0;
    return (want >= 0 && want != e && msSinceReq < 1000) ? want : e;
}

// ---- the game's menu table (0x14115D7C0, MenuDef[128], count at 0x14115EFE0) --------------------------
struct MenuDef { int32_t id; int32_t pad; uint64_t onEnter, draw, update, onExit; uint8_t tail[8]; };
static_assert(sizeof(MenuDef) == 0x30, "MenuDef is 0x30 bytes [P MenuOpen 0x140550177..0x140550194]");

static const int32_t kRootId = 0x4C00, kPageId = 0x4C01;   // unused: the game has 0..0x29 and 1000..1004
static const int kTableCap = 128;
static const uintptr_t RVA_PAUSE_DRAW = 0x80B090, RVA_PAUSE_UPDATE = 0x80AB60;        // id 2
static const uintptr_t RVA_OPT_ENTER = 0x80A090, RVA_OPT_DRAW = 0x80A530,              // id 4
                       RVA_OPT_UPDATE = 0x80A0F0, RVA_OPT_EXIT = 0x80A9B0;

struct Ours { uint64_t enter, rootDraw, rootUpdate, pageDraw, pageUpdate; };

// Writes go through this so the harness can log their order (record before count).
struct DefWriter
{
    virtual void Record(MenuDef* table, int slot, const MenuDef& rec) = 0;
    virtual void Count(volatile LONG* count, LONG v) = 0;
};

enum Reg { regOk, regAdded, regFull, regTaken, regNo2, regBad2, regNo4, regBad4, regCorrupt };
struct RegInfo { int slotRoot = -1, slotPage = -1; int count = 0; int added = 0; int32_t takenId = 0; uint64_t takenBy = 0; uint64_t badDraw = 0; };

inline bool IsOurs(const MenuDef& d, const Ours& o)
{
    if (d.id == kRootId) return d.onEnter == o.enter && d.draw == o.rootDraw && d.update == o.rootUpdate && d.onExit == 0;
    if (d.id == kPageId) return d.onEnter == o.enter && d.draw == o.pageDraw && d.update == o.pageUpdate && d.onExit == 0;
    return false;
}

// Find or add the two records. Refuses (with ZERO writes) when an id is taken by someone else's callbacks,
// the table would overflow, or menus 2 / 4 do not look as this build's do. Each record is written before
// the count that makes it visible (a release store), so a reader never sees a half-written record.
inline Reg EnsureRegistered(MenuDef* table, volatile LONG* count, const Ours& o, uintptr_t base, DefWriter& w, RegInfo* info)
{
    RegInfo tmp;
    RegInfo& ri = info ? *info : tmp;
    LONG n = *count;
    ri.count = n;
    if (n < 0 || n > kTableCap) return regCorrupt;
    int at2 = -1, at4 = -1;
    for (int i = 0; i < n; ++i)
    {
        const MenuDef& d = table[i];
        if (d.id == kRootId || d.id == kPageId)
        {
            if (!IsOurs(d, o)) { ri.takenId = d.id; ri.takenBy = d.draw; return regTaken; }
            (d.id == kRootId ? ri.slotRoot : ri.slotPage) = i;
        }
        else if (d.id == 2 && at2 < 0) at2 = i;
        else if (d.id == 4 && at4 < 0) at4 = i;
    }
    if (ri.slotRoot >= 0 && ri.slotPage >= 0) return regOk;
    if (at2 < 0) return regNo2;
    if (table[at2].draw != base + RVA_PAUSE_DRAW || table[at2].update != base + RVA_PAUSE_UPDATE)
    {
        ri.badDraw = table[at2].draw;
        return regBad2;
    }
    if (at4 < 0) return regNo4;
    const MenuDef& d4 = table[at4];
    if (d4.onEnter != base + RVA_OPT_ENTER || d4.draw != base + RVA_OPT_DRAW || d4.update != base + RVA_OPT_UPDATE ||
        d4.onExit != base + RVA_OPT_EXIT)
    {
        ri.badDraw = d4.draw;
        return regBad4;
    }
    const int need = (ri.slotRoot < 0 ? 1 : 0) + (ri.slotPage < 0 ? 1 : 0);
    if (n + need > kTableCap) return regFull;
    for (int which = 0; which < 2; ++which)
    {
        int& slot = which == 0 ? ri.slotRoot : ri.slotPage;
        if (slot >= 0) continue;
        MenuDef rec{};
        rec.id = which == 0 ? kRootId : kPageId;
        rec.onEnter = o.enter;
        rec.draw = which == 0 ? o.rootDraw : o.pageDraw;
        rec.update = which == 0 ? o.rootUpdate : o.pageUpdate;
        rec.onExit = 0;
        memcpy(rec.tail, d4.tail, sizeof(rec.tail));       // remember 0/0, wrap 0, panel 1,1 - as Options [P]
        w.Record(table, n, rec);
        slot = n;
        ++n;
        w.Count(count, n);
        ++ri.added;
    }
    ri.count = n;
    return regAdded;
}

// ---- the LegoCityGame vtable slots (0x140C1F308 + 0x350 / 0x358 / 0x360) --------------------------------
static const uintptr_t RVA_VT_GAME = 0xC1F308;
static const int kSlotOff[3] = { 0x350, 0x358, 0x360 };    // [0] AddRow, [1] HasRow, [2] OnRow
static const uintptr_t RVA_STUB_RET = 0x211520, RVA_STUB_FALSE = 0x0410C0;
static const int kInstallOrder[3] = { 0, 2, 1 };          // Add, On, then Has: no row before its handlers
static const int kRestoreOrder[3] = { 1, 0, 2 };          // Has first: the row goes before its handlers

enum Slots { slotsStock, slotsOurs, slotsForeign };
inline Slots CheckSlots(const uint64_t cur[3], uintptr_t base, const uint64_t ours[3], int* bad)
{
    const uint64_t stock[3] = { base + RVA_STUB_RET, base + RVA_STUB_FALSE, base + RVA_STUB_RET };
    if (cur[0] == ours[0] && cur[1] == ours[1] && cur[2] == ours[2]) return slotsOurs;
    for (int i = 0; i < 3; ++i) if (cur[i] != stock[i]) { if (bad) *bad = i; return slotsForeign; }
    return slotsStock;
}

// One slot write: `value` is stored only while the slot still holds `expect` (a compare-exchange), and a
// zero is never stored (a null HasRow would crash the pause draw's call [rax+0x358], 0x14080B2BE). Returns
// whether the store happened.
struct SlotWriter { virtual bool Write(int slot, uint64_t expect, uint64_t value) = 0; };

// Writes ours in kInstallOrder over the stubs; a failed write puts back what was written (the row never shows).
inline bool InstallSlots(SlotWriter& w, const uint64_t orig[3], const uint64_t ours[3])
{
    for (int k = 0; k < 3; ++k)
    {
        const int s = kInstallOrder[k];
        if (!w.Write(s, orig[s], ours[s]))
        {
            for (int j = k - 1; j >= 0; --j) w.Write(kInstallOrder[j], ours[kInstallOrder[j]], orig[kInstallOrder[j]]);
            return false;
        }
    }
    return true;
}
// Removal puts a slot back only while it still holds OUR pointer: a mod that chained onto it after us keeps
// its hook (it then calls our inert handlers: no row, no open).
inline void HideSlots(SlotWriter& w, const uint64_t orig[3], const uint64_t ours[3]) { w.Write(1, ours[1], orig[1]); }
inline void RestoreSlots(SlotWriter& w, const uint64_t orig[3], const uint64_t ours[3])
{
    for (int k = 0; k < 3; ++k) w.Write(kRestoreOrder[k], ours[kRestoreOrder[k]], orig[kRestoreOrder[k]]);
}

// ---- co-op: whose pad drives the menu (the GAME's rule, modelled for the harness) -----------------------
// While paused the menu flags come only from the pausing player's pad, p = *(int*)0x1411754F0 (PauseGame
// 0x1407EAACB); p = -1 ORs pads 0 and 1 [P 0x14080C3FA-0x14080C417, 0x14080C5BA-0x14080C5D9]. Mod Options
// adds no pad code of its own: Decide() sees only the Menu struct, so whoever paused navigates, and no
// setting (pad_p1 / pad_p2 only map lcudrive's own extra buttons) can change who that is.
inline uint32_t GamePadRouting(int pauser, uint32_t pad0, uint32_t pad1)
{
    return pauser == 0 ? pad0 : pauser == 1 ? pad1 : (pad0 | pad1);
}

} // namespace mlg
