// =============================================================================================
//  streetlamps.h - real light under the street lamps: actual lights in the street lights
//  (2026-11-13). At night every street lamp near a player casts a pool of light on the road, as an
//  engine point light the deferred renderer draws like any other.
//
//  WHY THIS ROUTE (the street-lamp plan, route B)
//  The city places no light at its lamps [P: the city's dawn/dusk/noon light files hold only the
//  sun, sky fill, god rays and a lens flare; only two tiles carry any placed light]. The lamp glass
//  glows (the emissive patch) but lights nothing. The renderer, though, already draws unshadowed
//  point lights (PS f143725c857c77bf is in ShaderUsage) from a list of up to 128 dynamic lights per
//  view [P: cmp eax,0x80 at 0x1403280AD]. So lcudrive owns K = 96 engine light objects of its own,
//  built with the engine's own constructors, parks them on the lamps nearest each player, and
//  hands them to every gameplay view exactly the way the engine hands it the vehicle lamps (the
//  working headlights): from DynamicLightManager's message 0x3a, through 0x140327EA0.
//    * Route A, the engine's dormant 32-light pool, is dead [I, strong]: 0x1402F0720 submits a pool
//      slot only while the view's stage word +0x3144 is 0, and before the lock it still holds 9.
//    * No shader file is involved, so the shader patch order (sun, sky, emissive, fog, wet,
//      speedFX) cannot be disturbed [P: no engine point/spot light shader has a ShaderFixes file].
//  Lamp positions are compiled in (streetlamps_table.h, from NightLights\StreetLamps\lamps.csv: every
//  CityPropInstance lamp in the city cells' _gz2.led; x,y,z = the head).
//
//  HAND-OFFS (review 2026-11-13): a lamp must never leave while it is still bright. In co-op each
//  player's 32 run out before lamp_range at 42% of street positions [P: the 33rd-nearest lamp from
//  every street lamp in the table], so each player's light fades out towards that player's own cut
//  (the nearest lamp left out), not towards lamp_range, and a lamp already lit ranks 10% nearer than
//  it is, so two lamps at nearly the same distance don't swap at every selection. max_lights caps what
//  is handed over: lamps still fading out only fill the room the chosen ones leave.
//
//  Every engine call here runs inside lcudrive.cpp's RunStreetLampMsg SEH guard: a fault latches
//  g_fault and the street lamps are off for the session (lights already in this frame's lists are
//  dropped by the engine at the end of the frame; nothing is submitted again).
// =============================================================================================
#pragma once
#include "streetlamps_table.h"

namespace sl {

// ---- the engine (LEGOLCUR_DX11.exe, RVAs from the preferred base 0x140000000) ----------------
static const uintptr_t RVA_VT_LIGHTMGR = 0xD05878;  // .?AVDynamicLightManager@@ [P RTTI]
static const int       SLOT_MSG        = 13;        // 0xD058E0 = its message handler, the only vtable entry
static const uintptr_t FN_LIGHTMGR_MSG = 0x5DC9D0;  // that holds it [P]; called (mgr, ctx, int* msg, 0) as
                                                    // vt+0x68 by the dispatcher 0x14060490C, return unused
static const int       kMsgSubmit      = 0x3a;      // 0x1405DC9E8 cmp dword [r8],0x3a: the light hand-over
static const uintptr_t FN_NUDL_CTOR    = 0x349280;  // NuDynamicLight::ctor (identity rows, +0x952 = 1, no alloc)
static const uintptr_t FN_DESC_CTOR    = 0x3493C0;  // NuDynamicLightDesc::ctor (vt 0x140CA0D78)
static const uintptr_t FN_DESC_DEFAULT = 0x34AFA0;  // its defaults (range 1, shading 0, falloff type 1, ...)
static const uintptr_t FN_DESC_VARIANT = 0x3F08B0;  // (desc, cutscene): re-points +0x300/+0x320 at the game or
                                                    // cutscene sub-block; the handler runs it before each submit
static const uintptr_t FN_SUBMIT       = 0x327EA0;  // bool (viewList, NuDynamicLight*) - appends to the list
static const uintptr_t FN_MODE_OBJ     = 0x6046B0;  // returns [0x141C6A370]
static const uintptr_t FN_MODE         = 0x604700;  // (obj, 1, layer) -> byte [obj + (layer + 4)*2 + 0xAE9]
static const uintptr_t RVA_LISTS       = 0x1AA4930; // 17 view light lists (0x141AA4930..0x141AA49B8)
static const uintptr_t RVA_SPECIAL     = 0x1168064; // int: the one view the mode-2 filter keeps (static -1)
static const uintptr_t RVA_OVERFLOW    = 0x1AA52C0; // byte the submit sets when a list is full (sticky)
static const int       kViews = 17, kListCap = 128;
static const int       kReserve = 16;               // list room we never take: other lights after us keep it [I]
// NuDynamicLight (0x18D0 bytes; vt 0x140CA0DA0)
static const uintptr_t NL_ROWS = 0x10, NL_POS = 0x40, NL_VIS = 0x952, NL_DESC = 0x18C0, NL_SIZE = 0x18D0;
// NuDynamicLightDesc (0xCF0 bytes): field offsets from the serializer 0x1403EF000 [P]
static const uintptr_t LD_REF = 0x08, LD_ENABLED = 0x20, LD_LAYER = 0x24, LD_SHAPE = 0x28, LD_SHADING = 0x30;
static const uintptr_t LD_COLOUR = 0x60, LD_LINCOL = 0x70, LD_RANGE = 0xA8, LD_FALLOFF = 0xB4;
static const uintptr_t LD_POOLHIDE = 0x422, LD_SIZE = 0xCF0;
// a view light list [P: 0x140327EA0 / the clear 0x14032D3C0]
static const uintptr_t LIST_COUNT = 0x2CE4, LIST_LOCKED = 0x3610;
static const uintptr_t MGR_WORLD = 0x20, WORLD_LAYER = 0xB64;   // the handler's view filter input

typedef void*   (__fastcall* FnCtor)(void* obj);
typedef void    (__fastcall* FnDefaults)(void* desc);
typedef void    (__fastcall* FnVariant)(void* desc, uint8_t cutscene);
typedef uint8_t (__fastcall* FnSubmit)(void* list, void* light);
typedef void*   (__fastcall* FnModeObj)();
typedef uint8_t (__fastcall* FnMode)(void* obj, int one, int layer);

static FnCtor     g_nudlCtor = nullptr;
static FnCtor     g_descCtor = nullptr;
static FnDefaults g_descDefaults = nullptr;
static FnVariant  g_variant = nullptr;
static FnSubmit   g_submit = nullptr;
static FnModeObj  g_modeObj = nullptr;
static FnMode     g_mode = nullptr;
static void**     g_lists = nullptr;
static const int* g_special = nullptr;
static const volatile uint8_t* g_overflow = nullptr;

static void Init(uintptr_t base)
{
    g_nudlCtor     = reinterpret_cast<FnCtor>(base + FN_NUDL_CTOR);
    g_descCtor     = reinterpret_cast<FnCtor>(base + FN_DESC_CTOR);
    g_descDefaults = reinterpret_cast<FnDefaults>(base + FN_DESC_DEFAULT);
    g_variant      = reinterpret_cast<FnVariant>(base + FN_DESC_VARIANT);
    g_submit       = reinterpret_cast<FnSubmit>(base + FN_SUBMIT);
    g_modeObj      = reinterpret_cast<FnModeObj>(base + FN_MODE_OBJ);
    g_mode         = reinterpret_cast<FnMode>(base + FN_MODE);
    g_lists        = reinterpret_cast<void**>(base + RVA_LISTS);
    g_special      = reinterpret_cast<const int*>(base + RVA_SPECIAL);
    g_overflow     = reinterpret_cast<const volatile uint8_t*>(base + RVA_OVERFLOW);
}

// ---- tuning that is per lamp kind, relative to the ini's lamp_radius / lamp_intensity [I: V5] ----
// kinds: 0 single-arm, 1 double (2 heads, one light at their centroid), 2 four-arm floodlight,
// 3 short park lamp (head 0.65 up), 4 lantern post, 5 wall, 6 garden bollard, 7 small lantern
static const float kKindRadius[8] = { 1.0f, 1.15f, 1.3f, 0.75f, 0.9f, 0.6f, 0.6f, 0.6f };
static const float kKindGain[8]   = { 1.0f, 1.3f,  1.6f, 0.6f,  0.8f, 0.45f, 0.4f, 0.5f };
static const char* const kKindName[8] = { "single", "double", "quad", "park", "lantern", "wall", "garden", "deco" };

static const int   K          = 96;         // engine light objects we own
static const int   kMaxCand   = 192;        // nearest lamps kept per player per selection
static const float kFadeS     = 0.25f;      // s, a slot fading in or out (hides the hand-offs)
static const float kMinW      = 1e-3f;      // below this a light is not worth a list entry
static const uint32_t kSelectMs = 100;      // re-select at least this often...
static const float kSelectMove  = 2.0f;     // ...or when a player moved this far (u)
static const float kKeepBias2 = 0.81f;      // a lamp already chosen ranks by d^2 x 0.81 (10% nearer): near-ties at
                                            // a player's cut keep the lamp that is lit, instead of swapping
static const float kCutS      = 0.15f;      // s, how fast a player's cut radius follows the selection's
static const float kMinCut    = 1.0f;       // u, the smallest cut (a share of 0: nothing lights near that player)
static const uint32_t kPauseHoldMs = 60000; // a pause holds the lamps at most this long [I: a pause byte left set
                                            // through a quit or a level load must not light the next scene]
static const int   kCityQuietMsgs = 30;     // the city counts as stopped only after this many 0x3a in a row with no
                                            // new tick (and over 1 s): one long frame can't switch the lamps off

// ---- what lcudrive.cpp hands in, once per 0x3a ------------------------------------------------
struct Inputs
{
    uint32_t now;                 // GetTickCount
    bool     clockOk;             // the bridge is locked: clock is IniParams[0].x (time + $tod)
    float    clock;
    bool     p1, p2;              // player present (p2 = co-op player 2 in the game)
    float    p1pos[3], p2pos[3];  // vehicle body, or the character on foot
    bool     stock;               // `[`: lcudrive is off
    bool     cityKnown;           // the population manager hook is in...
    uint32_t cityTick;            // ...and ran at this GetTickCount (the world is ticking)
    bool     paused;              // the game's pause byte (the pause menu, the map)
    const void* world;            // set by OnMessage, not the fill: [mgr+0x20], the handler's view-filter object;
                                  // a pause hold ends if it changes [I: a level change replaces it]
};
typedef void (*FillFn)(Inputs& in);

// ---- state (game thread only) ------------------------------------------------------------------
struct Slot { int16_t lamp; uint8_t want, on; float fade, d, w; };
static Slot      g_slot[K];
static int16_t   g_slotOf[kLampCount];       // lamp -> slot, -1 = none
static uint32_t  g_mark[kLampCount];         // lamp chosen in selection g_gen
static uint32_t  g_gen = 0;
static int       g_order[K];                 // lit slots, nearest first: the submit order
static int       g_nOrder = 0;
static uint8_t*  g_mem = nullptr;            // K NuDynamicLights, then K NuDynamicLightDescs; never freed
static bool      g_ready = false;
static volatile bool g_fault = false;        // latched: no street lamps for the session
static volatile bool g_stopReq = false;      // unhook: submit nothing from now on
static bool      g_inited = false;
static uint32_t  g_last = 0;                 // last 0x3a
static uint32_t  g_lastSel = 0;
static float     g_selPos[2][3];
static bool      g_selHad[2] = {};
static int       g_selMax = -1, g_selFix = -1;
static float     g_selRange = -1.0f;
static int       g_lastMode = -1;
static bool      g_haveClock = false;
static uint32_t  g_clockSeen = 0;
static float     g_clockLast = 0.0f;
static int       g_gate = -1;
static int       g_gateLogs = 0;
static int       g_n1 = 0, g_n2 = 0;         // lamps chosen for player 1 / player 2 last selection
static bool      g_ovfLogged = false;
static bool      g_badListLogged = false;
static float     g_cutT[2] = {};             // each player's cut at the last selection: the nearest lamp left out
static float     g_cut[2] = {};              // ...and the one the weights use, following it over kCutS
static bool      g_cutInit = false;
static uint32_t  g_cityPrev = 0;             // the last city tick stamp seen...
static int       g_cityQuiet = 0;            // ...and how many 0x3a in a row have seen no newer one
static bool      g_pausing = false;          // the pause byte was set at the last 0x3a
static bool      g_holdOk = false;           // this pause holds the lamps (they were on when it began)
static uint32_t  g_holdT = 0;                // when this pause began
static const void* g_holdWorld = nullptr;    // [mgr+0x20] when it began
static uint32_t  g_pausedAt = 0;             // the last 0x3a under a pause

struct Stats
{
    uint32_t msgs, msg3a, submits, added, already, refused, held, locked, views, noSlot;
    int      litMax;
    float    night;
};
static Stats    g_st = {};
static uint32_t g_statT = 0;
static Inputs   g_lastIn = {};

static uint8_t* Nudl(int i) { return g_mem + static_cast<size_t>(i) * NL_SIZE; }
static uint8_t* Desc(int i) { return g_mem + static_cast<size_t>(K) * NL_SIZE + static_cast<size_t>(i) * LD_SIZE; }

// ---- the night: the lamp glass's own ramp ------------------------------------------------------
// ShaderFixes 57edfe1fd211a45e (the glass emissive) lights the glass with
// saturate(-elev * 1.4 + 0.12), elev = sin(clock * 0.01309) [P]. The pools use the very same term,
// so a pool can never sit under a dark lamp head (on below elev 0.0857, full at -0.629).
static float NightFactor(float clock)
{
    const float elev = std::sin(clock * 0.01309f);
    return Clampf(-elev * 1.4f + 0.12f, 0.0f, 1.0f);
}

static float DistFade(float d, float range)          // smoothstep(R, 0.8R, d): 1 inside 0.8R, 0 at R
{
    if (!(range > 1e-3f)) return 0.0f;                // (a cut never set yet: no NaN into a colour)
    const float s = Clampf((range - d) / (0.2f * range), 0.0f, 1.0f);
    return s * s * (3.0f - 2.0f * s);
}

static float Dist2(const float a[3], const Lamp& l)   // horizontal: a lamp lights the street around you, and
{                                                     // from a helicopter the street below you
    const float dx = l.x - a[0], dz = l.z - a[2];
    return dx * dx + dz * dz;
}

static float NearestDist(const Lamp& l, const Inputs& in)
{
    float d2 = 3.0e30f;
    if (in.p1) d2 = Dist2(in.p1pos, l);
    if (in.p2) { const float e = Dist2(in.p2pos, l); if (e < d2) d2 = e; }
    return std::sqrt(d2);
}

// ---- gates: every "submit nothing" condition, in order -----------------------------------------
enum Gate { kOn = 0, kOffConfig, kOffStop, kOffStock, kOffCity, kOffPlayer, kOffClock, kOffDay, kOffBudget,
            kOffPaused, kGates };
static const char* const kGateText[kGates] = {
    "on", "off - streetlamps = 0", "off - unhooking", "off - [ stock", "off - the world is not ticking (no city)",
    "off - no player", "off - no day/night clock for 5 s", "off - day", "off - max_lights = 0",
    "off - paused, nothing held (the pause began dark, something switched them off, or the hold ended)" };

// cPopulationManager::Update is the city's own tick (traffic and crowd); loading screens, menus and [I] other
// levels don't run it, and their coordinates could fall near a city lamp. Stopped = BOTH the last tick (or the
// last paused 0x3a: the city doesn't tick under a pause [I]) is over 1 s old AND kCityQuietMsgs messages in a row
// saw no new tick - so neither one long frame (a streaming stall) nor a 0x3a that runs before the frame's PM
// update [U: V1] right after a pause can blink every lamp off. Signed, as radio.h's running test: the PM stamp
// may be taken a moment after in.now [U: another thread].
static bool CityStopped(const Inputs& in)
{
    if (!in.cityTick) return true;                           // it has never ticked
    uint32_t ref = in.cityTick;
    if (g_pausedAt && static_cast<int32_t>(g_pausedAt - ref) > 0) ref = g_pausedAt;
    return static_cast<int32_t>(in.now - ref) > 1000 && g_cityQuiet >= kCityQuietMsgs;
}

static int Evaluate(const Inputs& in, const Config& cfg, float& night)
{
    // the clock first, whatever else holds: a brief bridge rescan keeps the last value (lights.h does the same)
    if (in.clockOk) { g_haveClock = true; g_clockSeen = in.now; g_clockLast = in.clock; }
    // the city tick likewise: a new stamp restarts the count of messages that saw none
    if (in.cityTick != g_cityPrev) { g_cityPrev = in.cityTick; g_cityQuiet = 0; }
    else if (g_cityQuiet < 1000000) ++g_cityQuiet;
    night = 0.0f;
    if (cfg.sl_mode == 0) return kOffConfig;
    if (g_stopReq) return kOffStop;
    if (in.stock) return kOffStock;
    if (in.cityKnown && CityStopped(in)) return kOffCity;
    if (cfg.sl_mode == 2) { night = 1.0f; return in.p1 ? kOn : kOffPlayer; }
    if (!in.p1 && !in.p2) return kOffPlayer;
    if (!g_haveClock || (!in.clockOk && in.now - g_clockSeen > 5000)) return kOffClock;
    night = NightFactor(g_clockLast);
    if (night <= 0.0f) return kOffDay;
    if (cfg.sl_max_lights <= 0) return kOffBudget;
    return kOn;
}

// ---- slots -------------------------------------------------------------------------------------
static void Dark(int i)
{
    if (g_ready && g_slot[i].on) At<uint8_t>(Desc(i), LD_ENABLED) = 0;
    g_slot[i].on = 0;
}

static void Free(int i)
{
    Slot& s = g_slot[i];
    Dark(i);
    if (s.lamp >= 0 && s.lamp < kLampCount) g_slotOf[s.lamp] = -1;
    s.lamp = -1; s.want = 0; s.fade = 0.0f; s.w = 0.0f;
}

static void AllOff()
{
    for (int i = 0; i < K; ++i) Free(i);
    g_nOrder = 0;
    g_lastSel = 0;                                   // select afresh the moment it is on again
    g_cutInit = false;                               // ...with the cuts where that selection puts them
}

static void ResetState()
{
    for (int i = 0; i < K; ++i) { g_slot[i] = Slot{ -1, 0, 0, 0.0f, 0.0f, 0.0f }; }
    for (int i = 0; i < kLampCount; ++i) { g_slotOf[i] = -1; g_mark[i] = 0; }
    g_nOrder = 0;
    g_inited = true;
}

// ---- one-time setup, on the game thread, inside the first 0x3a that needs a light ---------------
static bool Setup()
{
    const size_t bytes = static_cast<size_t>(K) * (NL_SIZE + LD_SIZE);
    // VirtualAlloc: page aligned (the constructors use movaps; both strides are multiples of 16) and
    // never freed - a view list may still point at one of these at the moment anything would free it
    g_mem = static_cast<uint8_t*>(VirtualAlloc(nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    if (!g_mem)
    {
        Log("!!! lamps: no memory for the light objects (%u KB) - no street lamps this session\n",
            static_cast<unsigned>(bytes / 1024));
        return false;
    }
    for (int i = 0; i < K; ++i)
    {
        uint8_t* n = Nudl(i);
        uint8_t* d = Desc(i);
        g_nudlCtor(n);                               // 0x140349280
        g_descCtor(d);                               // 0x1403493C0
        g_descDefaults(d);                           // 0x14034AFA0 (a DynamicLightDesc gets these too)
        if (!lights::ClassIs(n, ".?AVNuDynamicLight@@") || !lights::ClassIs(d, ".?AVNuDynamicLightDesc@@"))
        {
            Log("!!! lamps: light object %d is not a NuDynamicLight / NuDynamicLightDesc after construction "
                "(a different build?) - no street lamps this session\n", i);
            return false;
        }
        // Two references: the light's own, and one nobody ever drops, so no Release (the NuDynamicLight dtor
        // 0x140349470 at exit, say) can delete a description that is not heap memory.
        InterlockedExchangeAdd(reinterpret_cast<volatile LONG*>(d + LD_REF), 2);
        At<uint8_t*>(n, NL_DESC) = d;
        At<uint8_t>(d, LD_ENABLED) = 0;
        g_variant(d, 0);
    }
    g_ready = true;
    Log(">>> lamps: %d engine point lights built (%u KB at %p); %d lamps in the table (%d fixtures), "
        "checksum %08X\n", K, static_cast<unsigned>(bytes / 1024), static_cast<void*>(g_mem), kLampCount,
        kLampFixtures, kLampChecksum);
    return true;
}

// ---- one light's fields ------------------------------------------------------------------------
// Brightness goes with the colour SQUARED (every point/spot shader squares g_Light.color [P]), so the
// weight w (night x fade x distance x intensity x kind) scales the colour by sqrt(w): light ~ w.
static void Write(int i, const float pos[3], float radius, const float rgb[3], float w, const Config& cfg)
{
    uint8_t* n = Nudl(i);
    uint8_t* d = Desc(i);
    static const float kRows[12] = { 1, 0, 0, 0,  0, 1, 0, 0,  0, 0, 1, 0 };
    float* r = reinterpret_cast<float*>(n + NL_ROWS);
    for (int k = 0; k < 12; ++k) r[k] = kRows[k];
    float* p = reinterpret_cast<float*>(n + NL_POS);
    p[0] = pos[0]; p[1] = pos[1]; p[2] = pos[2]; p[3] = 1.0f;    // the ctor leaves w = 0
    At<uint8_t>(n, NL_VIS) = 1;
    At<int32_t>(d, LD_SHAPE)   = 1;                  // point
    At<int32_t>(d, LD_SHADING) = 3;                  // no shadow: the headlights' proven setting, cheap
    At<int32_t>(d, LD_LAYER)   = -1;                 // < 0 skips the layer-table check (0x140327EC9)
    At<uint8_t>(d, LD_POOLHIDE) = 0;
    const float s = std::sqrt(w > 0.0f ? w : 0.0f);
    float* c = reinterpret_cast<float*>(d + LD_COLOUR);
    float* l = reinterpret_cast<float*>(d + LD_LINCOL);
    for (int k = 0; k < 3; ++k) { c[k] = rgb[k] * s; l[k] = c[k] * c[k]; }
    c[3] = l[3] = 1.0f;
    At<float>(d, LD_RANGE)   = radius;               // a point light needs +0xA8 (or +0x12C) non-zero
    At<float>(d, LD_FALLOFF) = cfg.sl_falloff;
    At<uint8_t>(d, LD_ENABLED) = 1;
    g_variant(d, 0);                                 // the game (not cutscene) sub-blocks, as the handler does
    g_slot[i].on = 1;
}

// ---- which lamps: the nearest to each player, the budget split between them --------------------
struct Cand { float key, d2; int lamp; };
static Cand g_c1[kMaxCand], g_c2[kMaxCand];
static int  g_want[K];
static int  g_nWant = 0;
static int  g_lit[K], g_out[K];              // Place: the slots handed over this frame / those still fading out

// The lamps within `range` of p, nearest first (at most `cap`). A lamp already chosen ranks by d^2 x kKeepBias2:
// street lamps stand in opposite pairs at nearly equal distances, and without it the pair that straddles a
// player's cut swaps at every 100 ms selection.
static int Nearest(const float p[3], float range, bool fixtures, Cand* out, int cap)
{
    const float r2 = range * range;
    int n = 0;
    for (int i = 0; i < kLampCount; ++i)
    {
        const Lamp& l = kLamps[i];
        if (!fixtures && l.kind >= 5) continue;
        const float d2 = Dist2(p, l);
        if (d2 > r2) continue;
        const int s = g_slotOf[i];
        const float key = (s >= 0 && s < K && g_slot[s].want) ? d2 * kKeepBias2 : d2;
        if (n == cap && key >= out[n - 1].key) continue;
        int j = n < cap ? n++ : cap - 1;
        while (j > 0 && out[j - 1].key > key) { out[j] = out[j - 1]; --j; }
        out[j].key = key; out[j].d2 = d2; out[j].lamp = i;
    }
    return n;
}

// max_lights split evenly between the players present; a share one player can't use goes to the other.
// A lamp both are near counts once. Fills g_want[0..g_nWant) and marks them with g_gen.
static void Choose(const Inputs& in, const Config& cfg)
{
    const float range = cfg.sl_range;
    const bool  fix = cfg.sl_fixtures != 0;
    const int   n1 = in.p1 ? Nearest(in.p1pos, range, fix, g_c1, kMaxCand) : 0;
    const int   n2 = in.p2 ? Nearest(in.p2pos, range, fix, g_c2, kMaxCand) : 0;
    int maxL = cfg.sl_max_lights;
    if (maxL > K) maxL = K;
    if (maxL < 0) maxL = 0;
    int share1 = maxL, share2 = 0;
    if (in.p1 && in.p2) { share1 = (maxL + 1) / 2; share2 = maxL - share1; }
    else if (in.p2)     { share1 = 0; share2 = maxL; }

    ++g_gen;
    if (g_gen == 0) { for (int i = 0; i < kLampCount; ++i) g_mark[i] = 0; g_gen = 1; }
    g_nWant = 0; g_n1 = g_n2 = 0;
    int i1 = 0, i2 = 0;
    // first each player's own share...
    while (g_n1 < share1 && i1 < n1 && g_nWant < maxL)
    {
        const int L = g_c1[i1++].lamp;
        if (g_mark[L] == g_gen) continue;
        g_mark[L] = g_gen; g_want[g_nWant++] = L; ++g_n1;
    }
    while (g_n2 < share2 && i2 < n2 && g_nWant < maxL)
    {
        const int L = g_c2[i2++].lamp;
        if (g_mark[L] == g_gen) continue;
        g_mark[L] = g_gen; g_want[g_nWant++] = L; ++g_n2;
    }
    // ...then whatever is left, taking turns
    while (g_nWant < maxL && (i1 < n1 || i2 < n2))
    {
        if (i1 < n1)
        {
            const int L = g_c1[i1++].lamp;
            if (g_mark[L] != g_gen) { g_mark[L] = g_gen; g_want[g_nWant++] = L; ++g_n1; }
        }
        if (g_nWant < maxL && i2 < n2)
        {
            const int L = g_c2[i2++].lamp;
            if (g_mark[L] != g_gen) { g_mark[L] = g_gen; g_want[g_nWant++] = L; ++g_n2; }
        }
    }
    // Each player's cut: the nearest of their lamps that was left out (lamp_range if none was). Their light fades
    // out from 0.8x it to it (Place), so a lamp a hand-off drops at the cut is already dark when it goes, and the
    // one that replaces it comes in from dark.
    const int   nc[2] = { n1, n2 };
    const Cand* cl[2] = { g_c1, g_c2 };
    for (int p = 0; p < 2; ++p)
    {
        float m2 = range * range;
        for (int k = 0; k < nc[p]; ++k)
            if (g_mark[cl[p][k].lamp] != g_gen && cl[p][k].d2 < m2) m2 = cl[p][k].d2;
        const float cut = std::sqrt(m2);
        g_cutT[p] = cut > kMinCut ? cut : kMinCut;
    }
}

// Stable slots: a lamp keeps its slot as long as it is wanted; a new one takes a free slot, else one already
// fading out - a dark one (past the max_lights cap, or faded) first, then the faintest lit one. Place lights
// at most max_lights <= K, so the dark ones always cover what is needed [the cap's room for fading slots is
// max_lights - chosen <= K - chosen].
static void Assign()
{
    for (int i = 0; i < K; ++i)
    {
        Slot& s = g_slot[i];
        if (s.lamp >= 0) s.want = static_cast<uint8_t>(g_mark[s.lamp] == g_gen ? 1 : 0);
    }
    int nextFree = 0;
    for (int w = 0; w < g_nWant; ++w)
    {
        const int L = g_want[w];
        if (g_slotOf[L] >= 0) continue;
        int pick = -1;
        while (nextFree < K && g_slot[nextFree].lamp >= 0) ++nextFree;
        if (nextFree < K) pick = nextFree;
        else
        {
            float best = 3.0e30f;
            for (int i = 0; i < K; ++i)
            {
                const Slot& s = g_slot[i];
                if (s.want) continue;
                const float key = s.on ? 2.0f + s.w : s.fade;          // dark: 0..1, lit: above 2
                if (key < best) { best = key; pick = i; }
            }
            if (pick >= 0) Free(pick);
        }
        if (pick < 0) { ++g_st.noSlot; continue; }
        Slot& s = g_slot[pick];
        s.lamp = static_cast<int16_t>(L); s.want = 1; s.fade = 0.0f; s.w = 0.0f; s.on = 0;
        g_slotOf[L] = static_cast<int16_t>(pick);
    }
}

static bool SelectDue(const Inputs& in, const Config& cfg)
{
    if (!g_lastSel || in.now - g_lastSel >= kSelectMs) return true;
    if (cfg.sl_max_lights != g_selMax || cfg.sl_range != g_selRange || cfg.sl_fixtures != g_selFix) return true;
    const bool here[2] = { in.p1, in.p2 };
    const float* pos[2] = { in.p1pos, in.p2pos };
    for (int p = 0; p < 2; ++p)
    {
        if (here[p] != g_selHad[p]) return true;
        if (!here[p]) continue;
        const float dx = pos[p][0] - g_selPos[p][0], dz = pos[p][2] - g_selPos[p][2];
        if (dx * dx + dz * dz > kSelectMove * kSelectMove) return true;
    }
    return false;
}

static void Select(const Inputs& in, const Config& cfg)
{
    Choose(in, cfg);
    Assign();
    const bool here[2] = { in.p1, in.p2 };
    for (int p = 0; p < 2; ++p)                      // a player new to the selection (or the first after a switch-
        if (!g_cutInit || !g_selHad[p] || !here[p])  // off) starts at the target; otherwise Place follows it
            g_cut[p] = g_cutT[p];
    g_cutInit = true;
    g_lastSel = in.now ? in.now : 1;
    g_selMax = cfg.sl_max_lights; g_selRange = cfg.sl_range; g_selFix = cfg.sl_fixtures;
    g_selHad[0] = in.p1; g_selHad[1] = in.p2;
    for (int k = 0; k < 3; ++k) { g_selPos[0][k] = in.p1pos[k]; g_selPos[1][k] = in.p2pos[k]; }
}

// ---- every 0x3a while on: fades, weights, fields, the submit order -----------------------------
// How far the players' light reaches a lamp: the best of the players present, each fading out from 0.8x their
// own cut to it (smoothstep). A player whose lamps all fit has the cut at lamp_range.
static float Reach(const Lamp& l, const Inputs& in)
{
    float r = 0.0f;
    if (in.p1) r = DistFade(std::sqrt(Dist2(in.p1pos, l)), g_cut[0]);
    if (in.p2) { const float e = DistFade(std::sqrt(Dist2(in.p2pos, l)), g_cut[1]); if (e > r) r = e; }
    return r;
}

static void Place(const Inputs& in, const Config& cfg, float night, float dt)
{
    if (SelectDue(in, cfg)) Select(in, cfg);
    const float follow = Clampf(dt / kCutS, 0.0f, 1.0f);       // the cuts move smoothly between selections
    for (int p = 0; p < 2; ++p) g_cut[p] += (g_cutT[p] - g_cut[p]) * follow;
    const int maxL = cfg.sl_max_lights < K ? (cfg.sl_max_lights > 0 ? cfg.sl_max_lights : 0) : K;
    const float step = dt / kFadeS;
    int nLit = 0, nOut = 0;
    for (int i = 0; i < K; ++i)
    {
        Slot& s = g_slot[i];
        if (s.lamp < 0) continue;
        s.fade = Clampf(s.fade + (s.want ? step : -step), 0.0f, 1.0f);
        if (!s.want && s.fade <= 0.0f) { Free(i); continue; }
        const Lamp& l = kLamps[s.lamp];
        s.d = NearestDist(l, in);
        s.w = night * s.fade * Reach(l, in) * cfg.sl_intensity * kKindGain[l.kind & 7];
        if (s.w <= kMinW) { Dark(i); continue; }
        if (s.want)
        {
            if (nLit < maxL) g_lit[nLit++] = i; else Dark(i);      // (never: a selection chooses <= max_lights)
        }
        else
        {
            int j = nOut++;                                         // still fading out: brightest first
            while (j > 0 && g_slot[g_out[j - 1]].w < s.w) { g_out[j] = g_out[j - 1]; --j; }
            g_out[j] = i;
        }
    }
    // max_lights caps what is handed over (the cost knob): the chosen lamps, then the brightest of those still
    // fading out in whatever room they leave. The rest go dark now - at the cut they are dark already (Reach).
    for (int k = 0; k < nOut; ++k)
    {
        if (nLit < maxL) g_lit[nLit++] = g_out[k];
        else Dark(g_out[k]);
    }
    g_nOrder = 0;
    for (int k = 0; k < nLit; ++k)
    {
        const int i = g_lit[k];
        const Slot& s = g_slot[i];
        const Lamp& l = kLamps[s.lamp];
        const float pos[3] = { l.x, l.y - cfg.sl_head_drop, l.z };
        Write(i, pos, cfg.sl_radius * kKindRadius[l.kind & 7], cfg.sl_col[l.cls < 3 ? l.cls : 0], s.w, cfg);
        int j = g_nOrder++;                                         // nearest first
        while (j > 0 && g_slot[g_order[j - 1]].d > s.d) { g_order[j] = g_order[j - 1]; --j; }
        g_order[j] = i;
    }
}

// streetlamps = 2: one plain light at player 1's feet, any time of day - the route proof (V2).
static void DebugLight(const Inputs& in, const Config& cfg)
{
    for (int i = 1; i < K; ++i) if (g_slot[i].lamp >= 0 || g_slot[i].on) Free(i);
    const float pos[3] = { in.p1pos[0], in.p1pos[1] + 1.5f, in.p1pos[2] };
    const float radius = cfg.sl_radius > 8.0f ? cfg.sl_radius : 8.0f;
    g_slot[0].lamp = -1; g_slot[0].fade = 1.0f; g_slot[0].d = 0.0f; g_slot[0].w = cfg.sl_intensity;
    Write(0, pos, radius, cfg.sl_col[1], cfg.sl_intensity, cfg);
    g_order[0] = 0;
    g_nOrder = 1;
}

// ---- the log ---------------------------------------------------------------------------------
static void NoteGate(int gate, float night, const Config& cfg)
{
    if (g_gateLogs >= 400) return;                    // a pathological flap can't flood the log
    if (++g_gateLogs == 400) { Log(">>> lamps: (no more on/off lines this session)\n"); return; }
    if (gate == kOn && cfg.sl_mode == 2)
        Log(">>> lamps: DEBUG - one light at player 1's feet, any time of day (streetlamps = 2)\n");
    else if (gate == kOn)
        Log(">>> lamps: on - night %.2f, up to %d lights within %.0f u of each player\n", night,
            cfg.sl_max_lights > K ? K : cfg.sl_max_lights, cfg.sl_range);
    else
        Log(">>> lamps: %s\n", kGateText[gate]);
}

// Once a second with [streetlamps] log = 1 (always with streetlamps = 2): what the hook saw and did.
static void StatTick(uint32_t now, const Config& cfg)
{
    if (!g_statT) { g_statT = now; return; }
    if (now - g_statT < 1000) return;
    if (cfg.sl_log || cfg.sl_mode == 2)
    {
        int lit = 0;
        for (int i = 0; i < K; ++i) lit += g_slot[i].on;
        const Inputs& in = g_lastIn;
        // the nearest table lamp to player 1 for the coordinate check (V3: lamps.csv data row), measured across
        // as the selection measures, with the head's height over you apart: at a lamp's foot the head is ~0.33 u
        // across and ~1.64 u up (Lampost_01_Tall), so a 3D distance could never read "right here"
        int best = -1; float bd2 = 3.0e30f;
        if (in.p1)
            for (int i = 0; i < kLampCount; ++i)
            {
                const float d2 = Dist2(in.p1pos, kLamps[i]);
                if (d2 < bd2) { bd2 = d2; best = i; }
            }
        char nearest[224] = "no player 1";
        if (best >= 0)
            _snprintf_s(nearest, sizeof(nearest), _TRUNCATE,
                        "p1 (%.2f, %.2f, %.2f), nearest lamp row %u %s head (%.2f, %.2f, %.2f) %.2f u across, "
                        "head %.2f u above you",
                        in.p1pos[0], in.p1pos[1], in.p1pos[2], kLamps[best].row, kKindName[kLamps[best].kind & 7],
                        kLamps[best].x, kLamps[best].y, kLamps[best].z, std::sqrt(bd2), kLamps[best].y - in.p1pos[1]);
        Log("    lamps: %u msgs (%u x 0x3a) in %.1f s, %s, night %.2f, lit %d (p1 %d / p2 %d, peak %d), "
            "cut %.0f / %.0f u, views %u, submitted %u: added %u, already %u, refused %u, held %u, locked %u, "
            "no slot %u, overflow byte %u | %s\n",
            g_st.msgs, g_st.msg3a, (now - g_statT) / 1000.0f, g_gate >= 0 && g_gate < kGates ? kGateText[g_gate] : "?",
            g_st.night, lit, g_n1, g_n2, g_st.litMax, g_cut[0], g_cut[1], g_st.views, g_st.submits, g_st.added,
            g_st.already, g_st.refused, g_st.held, g_st.locked, g_st.noSlot, g_overflow ? *g_overflow : 0u, nearest);
    }
    g_st = Stats{};
    g_statT = now;
}

// ---- the frame: once per 0x3a --------------------------------------------------------------------
static void Update(const Inputs& in, const Config& cfg)
{
    if (!g_inited) ResetState();
    ++g_st.msg3a;
    g_lastIn = in;
    const float dt = g_last ? Clampf((in.now - g_last) / 1000.0f, 0.0f, 0.1f) : 0.0f;
    g_last = in.now;
    if (cfg.sl_mode != g_lastMode) { AllOff(); g_lastMode = cfg.sl_mode; g_gate = -1; }
    float night = 0.0f;
    int gate = Evaluate(in, cfg, night);
    g_st.night = night;
    // Paused (pause menu, map): the world stands still behind it, and so do its lamps - the same lights are
    // handed over, nothing fades or moves (the map may move the player [I]). The hold is only what was on
    // when the pause began, while the light manager's world [mgr+0x20] is the same, for at most kPauseHoldMs:
    // a pause byte left set through a quit or a level load [I] must not hand city lamps to the next scene.
    // Every gate still acts (the city one can't fire: the pause itself counts as the city's last sign of
    // life), and once anything has turned them off they stay off until the pause ends.
    if (in.paused)
    {
        if (!g_pausing) { g_pausing = true; g_holdOk = g_gate == kOn; g_holdT = in.now; g_holdWorld = in.world; }
        g_pausedAt = in.now ? in.now : 1;
        if (gate == kOn && g_holdOk)
        {
            const bool same = in.world == g_holdWorld, young = in.now - g_holdT <= kPauseHoldMs;
            if (same && young) return;                                      // held
            Log(">>> lamps: the pause hold ended - %s\n", !same ? "the light manager's world changed under the "
                "pause (mgr+0x20)" : "paused for over 60 s");
        }
        if (gate == kOn) gate = kOffPaused;
        g_holdOk = false;
    }
    else g_pausing = false;
    if (gate != g_gate) { NoteGate(gate, night, cfg); g_gate = gate; }
    if (gate != kOn) { AllOff(); return; }
    if (!g_ready && !Setup()) { g_fault = true; AllOff(); return; }
    if (cfg.sl_mode == 2) DebugLight(in, cfg);
    else Place(in, cfg, night, dt);
    if (g_nOrder > g_st.litMax) g_st.litMax = g_nOrder;
}

// ---- the hand-over: the engine's own view filter, nearest lamps first --------------------------
// The handler 0x1405DC9D0 walks the 17 views; mode = 0x140604700(0x1406046B0(), 1, (int8)[[mgr+0x20]
// +0xB64]); mode 2 submits only to view [0x141168064], any other mode to every view but that one
// (0x1405DCA20..0x1405DCA5A) [P]. The engine's lights are already in (the original ran first), so the
// headlights keep their place under the 128 cap; we stop kReserve short of it.
static void Submit(void* mgr)
{
    if (!g_ready || g_nOrder <= 0 || !PtrOk(mgr)) return;
    uint8_t* world = At<uint8_t*>(mgr, MGR_WORLD);
    if (!PtrOk(world)) return;
    const uint8_t mode = g_mode(g_modeObj(), 1, static_cast<int>(At<int8_t>(world, WORLD_LAYER)));
    const int special = *g_special;
    const uint8_t ovf0 = *g_overflow;
    for (int v = 0; v < kViews; ++v)
    {
        void* list = g_lists[v];
        if (!PtrOk(list)) continue;
        if (mode == 2 ? v != special : v == special) continue;
        ++g_st.views;
        if (At<uint8_t>(list, LIST_LOCKED)) { ++g_st.locked; continue; }   // the submit would refuse anyway
        int cnt = At<int32_t>(list, LIST_COUNT);
        if (cnt < 0 || cnt > kListCap)
        {
            if (!g_badListLogged) { g_badListLogged = true; Log("!!! lamps: view %d's light list holds count %d - skipped\n", v, cnt); }
            continue;
        }
        for (int k = 0; k < g_nOrder; ++k)
        {
            if (cnt >= kListCap - kReserve) { g_st.held += static_cast<uint32_t>(g_nOrder - k); break; }
            const uint8_t ok = g_submit(list, Nudl(g_order[k]));
            ++g_st.submits;
            const int now = At<int32_t>(list, LIST_COUNT);
            if (!ok) ++g_st.refused;
            else if (now > cnt) ++g_st.added;
            else ++g_st.already;                     // already listed for this view (or the list overflowed)
            cnt = now;
        }
    }
    if (*g_overflow && !ovf0 && !g_ovfLogged)
    {
        g_ovfLogged = true;
        Log(">>> lamps: a view's light list overflowed (byte 0x141AA52C0 set) - lower [streetlamps] max_lights\n");
    }
}

// The hook's whole job, every message to the light manager (inside the caller's SEH guard).
static void OnMessage(void* mgr, const int* msg, uint32_t now, FillFn fill, const Config& cfg)
{
    ++g_st.msgs;
    if (msg && *msg == kMsgSubmit)
    {
        Inputs in = {};
        fill(in);
        in.world = PtrOk(mgr) ? At<void*>(mgr, MGR_WORLD) : nullptr;
        Update(in, cfg);
        Submit(mgr);
    }
    StatTick(now, cfg);
}

// After a fault: our objects dark (our own memory - safe even when the fault came from the engine).
static void FaultOff()
{
    if (!g_ready) return;
    for (int i = 0; i < K; ++i) At<uint8_t>(Desc(i), LD_ENABLED) = 0;
    g_nOrder = 0;
}

} // namespace sl
