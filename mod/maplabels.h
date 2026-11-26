// =============================================================================================
//  maplabels.h - [labels1] the new places on the city map (Notes 59 section 6).
//
//  1. PINS. PATCH.DAT appends PINDATA records (pinName LCUM_*, startEnabled TRUE) to mappins.txt.
//     The game builds a start-enabled pin through the Generic LocatorPinInterface [P: 0x140233EF0
//     loop -> 0x140232420 -> handler[0] = Generic (0x140232120) -> slot 2 0x140232C60], which asks
//     FindLocator 0x14058A6F0(level ai, pinName) for a Locator and copies its position (+0x88 x,
//     +0x8C y, +0x90 z) and its name (vtable slot 4 = [this+0x30]) into a pooled pin - the locator
//     itself is not kept [P 0x140232220]. We retarget ONLY that call (rva 0x232C98, the pin
//     system's own lookup): the game's answer first; if it has none and the name is one of ours
//     (in an enabled group), a static fake Locator with our position. lego_city_ai.led is untouched.
//  2. LOCATION LINE. The map / communicator location line is 0x140275A80(hud, pos4), called from
//     0x140275764 (map cursor) and 0x140275D20 (the player) [P]. It names a cell's district on a
//     cell change via 0x1400CAD40 -> NULL for every new cell, and returns early with no cell
//     (side-list areas). We call it, then: if mlm::Classify says a new place and it is not the
//     one this hud last showed, set its two text widgets (0x1401E9580, +0xE8 / +0xF0) to our
//     name, its district index (+0xC4) to 22 (UNASSIGNED: the cursor's district stats are skipped,
//     0x140275DF9) and its last-cell word (+0xC0) to 0xFFFF so the game re-evaluates on the next
//     real cell. lcudrive's own DistrictAt (0x1400CAD40 direct) is not affected.
//  Text: the game's text table first (stuff\text\patch.csv rows LCUM_DISTRICT_*), else English.
//  Off: [maplabels] enable = 0 (startup) / label = 0 / pins_* = 0; unhook restores both sites.
// =============================================================================================
#pragma once
#include "maplabels_model.h"

namespace ml {

static const uintptr_t SITE_FINDLOC  = 0x232C98, FN_FINDLOC = 0x58A6F0;   // in LocatorPinInterface slot 2
static const uintptr_t SITE_LOC_CUR  = 0x275764, SITE_LOC_PLY = 0x275D20, FN_LOC = 0x275A80;
static const uintptr_t FN_SETTEXT    = 0x1E9580;                          // (widget, const char*), copies
static const uintptr_t FN_TEXTLOOKUP = 0x2E28F0, RVA_TEXTMISSING = 0x10267E8;
static const uintptr_t RVA_GAME      = 0x1C7A830, GAME_INCITY = 0x335D;   // byte: the city (not an interior)
static const uintptr_t RVA_LEVELGRID = 0x1C6A768, FN_GRID = 0x47C0, FN_CELL = 0x611320;
static const uintptr_t CELL_NAME     = 0x68;                              // char* (NuString at +0x60, 0x14060CFA0 +0x10)
static const uintptr_t HUD_TEXT_A = 0xE8, HUD_TEXT_B = 0xF0, HUD_CELL = 0xC0, HUD_DISTRICT = 0xC4, HUD_MODE = 0xCC;

typedef void* (*FindLocFn)(void* ai, const char* name);
typedef void  (*LocFn)(void* hud, const float* pos4);
typedef void  (*SetTextFn)(void* widget, const char* s);
typedef const char* (*TextLookupFn)(const char* key, const char** outKey);
typedef void* (*GridFn2)(void*);
typedef void* (*CellFn2)(void*, const float*, float*);

static CallPatch g_findPatch = { "map pin loc", SITE_FINDLOC, FN_FINDLOC, nullptr, 0, false };
static CallPatch g_locPatchC = { "map loc cur", SITE_LOC_CUR, FN_LOC, nullptr, 0, false };
static CallPatch g_locPatchP = { "map loc ply", SITE_LOC_PLY, FN_LOC, nullptr, 0, false };
static uintptr_t g_b = 0;
static volatile LONG g_labelFault = 0, g_pinFault = 0;

// ---- the fake Locators (static; what the pin code reads: vtable slot 4, +0x30, +0x88..+0x90) ----
struct alignas(16) FakeLoc { uint8_t b[0xC0]; };
static FakeLoc g_fake[mlt::kPinCount];
static void* g_fakeVt[16];
static const char* FakeName(void* self) { return *reinterpret_cast<const char**>(reinterpret_cast<uint8_t*>(self) + 0x30); }
static void* FakeNull(void*) { return nullptr; }

static void BuildFakes()
{
    for (void*& s : g_fakeVt) s = reinterpret_cast<void*>(&FakeNull);
    g_fakeVt[4] = reinterpret_cast<void*>(&FakeName);
    for (int i = 0; i < mlt::kPinCount; ++i)
    {
        FakeLoc& f = g_fake[i];
        std::memset(&f, 0, sizeof(f));
        void** vt = g_fakeVt;
        std::memcpy(f.b + 0x00, &vt, 8);
        const char* n = mlt::kPins[i].name;
        std::memcpy(f.b + 0x30, &n, 8);
        const float p[4] = { mlt::kPins[i].x, mlt::kPins[i].y, mlt::kPins[i].z, 1.0f };
        std::memcpy(f.b + 0x80, p, 16);                       // +0x80 x again (the matrix-row habit), harmless
        std::memcpy(f.b + 0x88, p, 12);                       // +0x88 x, +0x8C y, +0x90 z  [P 0x14022B730 / 0x1402322B9]
    }
}

static void* Hook_FindLocator(void* ai, const char* name)
{
    void* r = reinterpret_cast<FindLocFn>(g_b + FN_FINDLOC)(ai, name);
    if (r || g_pinFault) return r;
    __try
    {
        const Config& c = g_cfgBuf[g_cfgIdx];
        if (!c.maplabels_enable) return r;
        const int i = mlm::FindPin(name, mlm::GroupMask(c.maplabels_pins_districts, c.maplabels_pins_places,
                                                        c.maplabels_pins_boats));
        if (i < 0) return r;
        Log("    maplabels: pin %s at (%.1f, %.1f, %.1f) (our locator)\n", mlt::kPins[i].name, mlt::kPins[i].x,
            mlt::kPins[i].y, mlt::kPins[i].z);
        return &g_fake[i];
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        InterlockedExchange(&g_pinFault, 1);
        Log("!!! maplabels: exception in the pin lookup - no more pins from lcudrive this session\n");
        return r;
    }
}

// ---- the location line ----------------------------------------------------------------------------------
struct HudState { void* hud; int last; };
static HudState g_hud[4] = {};

static HudState& StateFor(void* hud)
{
    for (HudState& s : g_hud) if (s.hud == hud) return s;
    for (HudState& s : g_hud) if (!s.hud) { s.hud = hud; s.last = -1; return s; }
    g_hud[3].hud = hud; g_hud[3].last = -1;              // more than 4 huds: the last slot is reused
    return g_hud[3];
}

static const char* Text(int d)
{
    const char* en = mlt::kDistricts[d].en;
    __try
    {
        const char* s = reinterpret_cast<TextLookupFn>(g_b + FN_TEXTLOOKUP)(mlt::kDistricts[d].key, nullptr);
        s = mlg::TableText(s, *reinterpret_cast<const char* const*>(g_b + RVA_TEXTMISSING));
        return s ? s : en;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return en; }
}

static const char* CellNameAt(const float* pos4)
{
    uint8_t* l = *reinterpret_cast<uint8_t**>(g_b + RVA_LEVELGRID);
    if (!PtrOk(l)) return nullptr;
    uint8_t* a = At<uint8_t*>(l, 0x178);
    uint8_t* bb = PtrOk(a) ? At<uint8_t*>(a, 0x98) : nullptr;
    if (!PtrOk(bb)) return nullptr;
    void* grid = reinterpret_cast<GridFn2>(g_b + FN_GRID)(bb);
    if (!PtrOk(grid)) return nullptr;
    alignas(16) float p[4] = { pos4[0], pos4[1], pos4[2], 1.0f };
    uint8_t* cell = static_cast<uint8_t*>(reinterpret_cast<CellFn2>(g_b + FN_CELL)(grid, p, nullptr));
    if (!PtrOk(cell)) return nullptr;
    const char* n = At<const char*>(cell, CELL_NAME);
    return PtrOk(n) ? n : nullptr;
}

static void LabelPost(void* hud, const float* pos4)
{
    if (!PtrOk(hud) || !PtrOk(pos4)) return;
    void* wa = At<void*>(hud, HUD_TEXT_A);
    if (!PtrOk(wa)) return;                                                   // as the game: no widget, nothing
    uint8_t* game = *reinterpret_cast<uint8_t**>(g_b + RVA_GAME);
    if (!PtrOk(game) || At<uint8_t>(game, GAME_INCITY) == 0) return;          // an interior shows its level name
    if (At<int>(hud, HUD_MODE) == 2) return;                                  // mode 2: no location line
    if (!IsFin(pos4[0]) || !IsFin(pos4[1]) || !IsFin(pos4[2])) return;
    const int d = mlm::Classify(CellNameAt(pos4), pos4[0], pos4[1], pos4[2]);
    HudState& st = StateFor(hud);
    if (d < 0) { st.last = -1; return; }
    if (st.last == d) return;
    const char* s = Text(d);
    reinterpret_cast<SetTextFn>(g_b + FN_SETTEXT)(wa, s);
    void* wb = At<void*>(hud, HUD_TEXT_B);
    if (PtrOk(wb)) reinterpret_cast<SetTextFn>(g_b + FN_SETTEXT)(wb, s);
    At<int>(hud, HUD_DISTRICT) = 0x16;
    At<uint16_t>(hud, HUD_CELL) = 0xFFFF;
    st.last = d;
    Log("    maplabels: location line \"%s\" (hud %p, %s at %.0f, %.0f)\n", s, hud,
        At<int>(hud, HUD_MODE) == 1 ? "map cursor" : "player", pos4[0], pos4[2]);
}

static void Hook_MapLoc(void* hud, const float* pos4)
{
    InterlockedIncrement(&g_inFlight);
    reinterpret_cast<LocFn>(g_b + FN_LOC)(hud, pos4);
    if (!g_unhooked && !g_labelFault)
    {
        const Config& c = g_cfgBuf[g_cfgIdx];
        if (c.maplabels_enable && c.maplabels_label)
        {
            __try { LabelPost(hud, pos4); }
            __except (EXCEPTION_EXECUTE_HANDLER)
            {
                InterlockedExchange(&g_labelFault, 1);
                Log("!!! maplabels: exception in the location line - the game's own from now on\n");
            }
        }
    }
    InterlockedDecrement(&g_inFlight);
}

static void Install(uintptr_t base, const Config& c)
{
    g_b = base;
    if (!c.maplabels_enable) { Log("maplabels: off ([maplabels] enable = 0)\n"); return; }
    BuildFakes();
    g_findPatch.replacement = reinterpret_cast<void*>(&Hook_FindLocator);
    g_locPatchC.replacement = reinterpret_cast<void*>(&Hook_MapLoc);
    g_locPatchP.replacement = reinterpret_cast<void*>(&Hook_MapLoc);
    const bool pins = ApplyPatch(base, g_findPatch);
    bool loc = ApplyPatch(base, g_locPatchC);
    loc = ApplyPatch(base, g_locPatchP) && loc;
    if (!loc) { RemovePatch(base, g_locPatchC); RemovePatch(base, g_locPatchP); }
    Log("maplabels: %d pins (%s), location line %s; %d names, %d side areas\n", mlt::kPinCount,
        pins ? "lookup hooked" : "NOT hooked: no new pins", loc ? "hooked" : "NOT hooked: the game's own",
        mlt::kDistrictCount, mlt::kSideBoxCount);
}

static void Uninstall(uintptr_t base)
{
    RemovePatch(base, g_findPatch);
    RemovePatch(base, g_locPatchC);
    RemovePatch(base, g_locPatchP);
}

}  // namespace ml
