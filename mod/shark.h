// =============================================================================================
//  shark.h - the rare shark: engine glue (Notes\61). shark_model.h decides; this file watches the
//  two players and gates the game's own deep-water rule per player.
//
//  How the stock shark works (exe sha256 624fed07..., TimeDateStamp 0x591B08C3) [P unless marked]:
//    stuff/game.txt: surface 18 "Sharks!" = turn_on "deep" turn_on "unsafe" -> terrain type 18 has
//    flag 0x100000 in the 36-byte table 0x14117D2C4 (the only type that has it, read live 11-25).
//    SwimmingContext (vtable slots 7 and 14: 0x140688190 and 0x140688370) calls, at 0x1406882EE and
//    0x1406886A3, the deep-water timer 0x140688B00 (rcx = the context, rdx = the MechCharacter,
//    xmm2 = dt): if his ApiCharacterLak (mc+0x258) has a floor (+0x200) and the terrain type under
//    him (+0x211, else +0x161; <= 33) is deep, and his def (mc+0x650 -> +0x50 -> +0xF8) has no
//    immune_to_deadly_water bit 0x2000: [ctx+0x78] += dt; otherwise [ctx+0x78] = 0.
//    CanDrown 0x14068A7A0 (one caller, 0x1407956F2, in the swimming update) puts him in the
//    Drowning context (id at 0x14116AD50) once [ctx+0x78] > 2.0 (0x14116AD48): the shark, ~2 s
//    after he swims out over deep water. The game then respawns him.
//  NOT the decision (the 11-19 mistake, found 11-25): LegoCityFloating slot 42 / 0x14096A090 and
//    the level float deadly_deep_water 0x141C9D708. That function takes the old game-object body,
//    never a MechCharacter (mc+0x1F90 is 0 on both live players); in 38 hooked sessions it was
//    never once asked about a player, while the timer above drowned them every time.
//  The hook: both call sites of the timer -> DeepTimer below. For anyone who is not one of the two
//  players, or over shallow water, or while the model allows it: the game's own timer. For a player
//  over deep water whom the model does not allow: [ctx+0x78] = 0, exactly what the game writes over
//  any other water - he swims on.
//  The safe sea ([world] safe_sea, east of safe_sea_x): never deep for the clock, never allowed -
//  no shark there at all (the depth float lcudrive.cpp RunSafeSea holds at 0 has no reader that
//  matters; this is what makes the safe sea real).
//  Off switches: [shark] enable = 0 (not hooked, stock), classic = 1 (hooked, stock rule), `[`
//  (stock for the session), a fault (stock for the session), unhook (call sites restored).
// =============================================================================================
#pragma once
#include "shark_model.h"

namespace sh {

static const uintptr_t FN_DEEP_TIMER   = 0x688B00;    // SwimmingContext's deep-water timer (ctx, mc, dt)
static const uintptr_t SITE_TIMER_A    = 0x6882EE;    // its call in SwimmingContext slot 7
static const uintptr_t SITE_TIMER_B    = 0x6886A3;    // its call in SwimmingContext slot 14 (the update)
static const uintptr_t RVA_DEPTH       = 0x1C9D708;   // deadly_deep_water (float): "armed" for the model only
static const uintptr_t RVA_TERRAIN     = 0x117D2C4;   // terrain type flags, 36-byte entries, 34 types
static const uint32_t  kTerrainStride = 36, kTerrainMax = 0x21, kFlagDeep = 0x100000, kImmune = 0x2000;
static const uintptr_t MC_LAK = 0x258, LAK_FLOOR = 0x200, LAK_TYPE_A = 0x211, LAK_TYPE_B = 0x161;
static const uintptr_t MC_CDATA = 0x650, CDATA_PROPS = 0x50, PROPS_FLAGS = 0xF8;
static const uintptr_t CTX_DEEP_T      = 0x78;        // SwimmingContext: s over deep water (drowns past 2.0)
static const uintptr_t CTX_DROWNING    = 0x116AD50;

typedef void(__fastcall* DeepTimerFn)(uint8_t* ctx, uint8_t* mc, float dt);

static CallPatch       g_patch[2] = {
    { "shark timer", SITE_TIMER_A, FN_DEEP_TIMER, nullptr, 0, false },
    { "shark timer", SITE_TIMER_B, FN_DEEP_TIMER, nullptr, 0, false },
};
static DeepTimerFn     g_orig = nullptr;
static bool            g_hooked = false;
static volatile bool   g_on = false;          // the gate is live (hooked, enabled, `]`, no fault)
static volatile bool   g_fault = false;       // latched: stock for the session
static uint8_t* volatile g_ch[2] = { nullptr, nullptr };   // the players' MechCharacters, as of the last tick
static volatile bool   g_allow[2] = { true, true };       // the model lets the game's rule act on him
static volatile LONG   g_frame = 0;
static volatile LONG   g_obsFrame[2] = { -100, -100 };    // when the timer was last asked about him
static volatile LONG   g_obsCount[2] = { 0, 0 };          // ...how often
static volatile LONG   g_heldCount[2] = { 0, 0 };         // ...and how often the gate held it at 0
static shm::Params     g_p;
static shm::State      g_s[2];
static uint32_t        g_rng = 0x9E3779B9u;
static float           g_deepNoObs[2] = { 0.0f, 0.0f };   // s swimming over deep water without one timer call
static bool            g_saidNoObs = false;
static bool            g_saidGate[2] = { false, false };
static void* volatile  g_world = nullptr;

// optional warning cue (no stock cue is callable yet - Notes 61 §4); set by lcudrive.cpp if one is wired
static void (*g_onWarn)(int player) = nullptr;

static float Rnd01()
{
    g_rng ^= g_rng << 13; g_rng ^= g_rng >> 17; g_rng ^= g_rng << 5;
    return static_cast<float>(g_rng >> 8) * (1.0f / 16777216.0f);
}

static inline bool DeepType(uint32_t t)
{
    return t <= kTerrainMax &&
           (*reinterpret_cast<const uint32_t*>(g_base + RVA_TERRAIN + t * kTerrainStride) & kFlagDeep) != 0;
}

// The water under him is deep ("deadly"), exactly as the game's timer 0x140688B00 decides it.
static bool DeepUnder(uint8_t* mc)
{
    uint8_t* lak = At<uint8_t*>(mc, MC_LAK);
    if (!PtrOk(lak) || At<void*>(lak, LAK_FLOOR) == nullptr) return false;
    if (!DeepType(At<uint8_t>(lak, LAK_TYPE_A)) && !DeepType(At<uint8_t>(lak, LAK_TYPE_B))) return false;
    uint8_t* cd = At<uint8_t*>(mc, MC_CDATA);
    if (!PtrOk(cd)) return false;
    uint8_t* props = At<uint8_t*>(cd, CDATA_PROPS);
    if (!PtrOk(props)) return false;
    return (At<uint32_t>(props, PROPS_FLAGS) & kImmune) == 0;
}

// Both call sites of the game's deep-water timer. Game thread, every swimmer, every frame: keep it lean.
static void __fastcall DeepTimer(uint8_t* ctx, uint8_t* mc, float dt)
{
    const DeepTimerFn orig = g_orig;
    if (!g_on || !mc || !ctx) { orig(ctx, mc, dt); return; }
    const int who = mc == g_ch[0] ? 0 : (mc == g_ch[1] ? 1 : -1);
    if (who < 0) { orig(ctx, mc, dt); return; }                       // everyone else: the game's rule
    bool deep = false;
    __try { deep = DeepUnder(mc); }
    __except (EXCEPTION_EXECUTE_HANDLER) { orig(ctx, mc, dt); return; }
    g_obsFrame[who] = g_frame; InterlockedIncrement(&g_obsCount[who]);
    if (!deep || g_allow[who]) { orig(ctx, mc, dt); return; }         // shallow, classic, or the strike
    InterlockedIncrement(&g_heldCount[who]);
    At<float>(ctx, CTX_DEEP_T) = 0.0f;                                // as over any other water: he swims on
}

static void Params(const Config& c)
{
    g_p.classic       = c.shark_classic != 0;
    g_p.graceS        = c.shark_grace_s;
    g_p.chancePerMin  = c.shark_chance_per_min;
    g_p.ramp          = c.shark_ramp;
    g_p.maxPerMin     = c.shark_max_per_min;
    g_p.warnS         = c.shark_warn_s;
    g_p.guardS        = c.shark_guard_s;
}

// At startup, after the car hook (lcudrive.cpp Main). Refused if the code is not the shipped one.
static void Install(const Config& c)
{
    if (!c.shark_enable) { Log(">>> shark: [shark] enable = 0 - the game's own shark (not hooked)\n"); return; }
    bool bytesOk = false;
    __try
    {
        const uint8_t* fn = reinterpret_cast<const uint8_t*>(g_base + FN_DEEP_TIMER);
        // 4c 8b 82 58 02 00 00 = mov r8,[rdx+0x258] (the Lak); f3 41 0f 58 51 78 = addss xmm2,[r9+0x78];
        // 41 c7 41 78 00 00 00 00 = mov dword [r9+0x78],0
        static const uint8_t kHead[7] = { 0x4C, 0x8B, 0x82, 0x58, 0x02, 0x00, 0x00 };
        static const uint8_t kAdd[6]  = { 0xF3, 0x41, 0x0F, 0x58, 0x51, 0x78 };
        static const uint8_t kZero[8] = { 0x41, 0xC7, 0x41, 0x78, 0x00, 0x00, 0x00, 0x00 };
        bytesOk = std::memcmp(fn, kHead, 7) == 0 && std::memcmp(fn + 0x6F, kAdd, 6) == 0 &&
                  std::memcmp(fn + 0x7C, kZero, 8) == 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { bytesOk = false; }
    if (!bytesOk)
    {
        Log(">>> shark: hook REFUSED - the deep-water timer at rva 0x%llX is not the shipped code; the game's own shark\n",
            (unsigned long long)FN_DEEP_TIMER);
        return;
    }
    g_orig = reinterpret_cast<DeepTimerFn>(g_base + FN_DEEP_TIMER);
    for (CallPatch& p : g_patch) p.replacement = reinterpret_cast<void*>(&DeepTimer);
    if (ApplyPatch(g_base, g_patch[0]) && ApplyPatch(g_base, g_patch[1])) g_hooked = true;
    else { RemovePatch(g_base, g_patch[0]); RemovePatch(g_base, g_patch[1]); }
    g_rng ^= GetTickCount() | 1u;
    if (g_hooked)
        Log(">>> shark: hooked the deep-water timer (rva 0x%llX, both call sites) - %s\n", (unsigned long long)FN_DEEP_TIMER,
            c.shark_classic ? "classic: the game's own shark" : "swim freely; a rare shark only after a long swim");
    else
        Log(">>> shark: hook REFUSED - a call site of the deep-water timer is not the shipped one; the game's own shark\n");
}

static void Uninstall()
{
    g_on = false;
    RemovePatch(g_base, g_patch[0]);
    RemovePatch(g_base, g_patch[1]);
    g_hooked = false;
}

// Is the gate live?
static inline bool Active() { return g_hooked && g_on; }

static bool InCtx(uint8_t* c, uintptr_t ctxRva)
{
    const int id = *reinterpret_cast<const int*>(g_base + ctxRva);
    return id != -1 && wd::IsIn(c, ctxRva);
}

static uint8_t* PlayerCh(int i)
{
    if (i == 1) return Player2Char();
    uint8_t* p = *reinterpret_cast<uint8_t**>(g_base + eng::RVA_PLAYER1);
    if (!PtrOk(p)) p = *reinterpret_cast<uint8_t**>(g_base + eng::RVA_PLAYERS);
    return PtrOk(p) ? p : nullptr;
}

static void Mmss(float s, char* out, size_t n) { const int t = static_cast<int>(s); snprintf(out, n, "%d:%02d", t / 60, t % 60); }

// Once a tick from cPopulationManager::Update (lcudrive.cpp RunShark). on = enabled + `]`.
static void Tick(void* world, float dt, const Config& cfg, bool on)
{
    InterlockedIncrement(&g_frame);
    Params(cfg);
    const bool live = on && g_hooked && !g_fault && cfg.shark_enable;
    if (world != g_world) { g_world = world; for (int i = 0; i < 2; ++i) shm::Forget(g_s[i]); }
    if (!live)
    {
        g_on = false;
        g_allow[0] = g_allow[1] = true;
        for (int i = 0; i < 2; ++i) shm::Forget(g_s[i]);
        return;
    }
    const float depth = *reinterpret_cast<const float*>(g_base + RVA_DEPTH);
    for (int i = 0; i < 2; ++i)
    {
        uint8_t* p = PlayerCh(i);
        g_ch[i] = p;
        shm::In in;
        bool safe = false;
        in.valid = PtrOk(p) && (At<uint8_t>(p, 0x320) & 1);
        if (in.valid)
        {
            in.swimming = InCtx(p, wd::CTX_SWIMMING);
            in.drowning = InCtx(p, CTX_DROWNING);
            in.dead = InCtx(p, wd::CTX_DEATH) || reinterpret_cast<wd::DeadFn>(g_base + wd::FN_DEAD)(p);
            in.feet = !in.dead && InCtx(p, wd::CTX_DEFAULT);
            in.vehicle = (i ? Player2Vehicle() : PlayerVehicle()) != nullptr;
            // armed: the level float RunSafeSea may be holding at 0 is not the game's switch (nothing that
            // matters reads it) - the deep terrain flag is, so the model is always armed over deep water
            in.armed = true; (void)depth;
            for (int k = 0; k < 3; ++k) in.pos[k] = At<float>(p, 0xA8 + 4 * k);
            safe = cfg.world_safe_sea != 0 && IsFin(in.pos[0]) && in.pos[0] > cfg.world_safe_sea_x;
            // deep: the same test the game's timer makes, read here every tick (never deep in the safe sea)
            const bool deepRaw = in.swimming && DeepUnder(p);
            in.deep = deepRaw && !safe;
            // a diagnostic: swimming over deep water and the game's timer never asks about him
            if (deepRaw && g_frame - g_obsFrame[i] > 2) g_deepNoObs[i] += dt; else g_deepNoObs[i] = 0.0f;
            if (!g_saidNoObs && g_deepNoObs[i] > 5.0f)
            {
                g_saidNoObs = true;
                Log(">>> shark: player %d has swum 5 s over deep water and the deep-water timer never asked about him - "
                    "the gate is not reached (the game's own shark rules). Calls so far: %ld / %ld\n", i + 1,
                    g_obsCount[0], g_obsCount[1]);
            }
            if (!g_saidGate[i] && g_heldCount[i] > 0)
            {
                g_saidGate[i] = true;
                Log(">>> shark: player %d is over deep water and the gate holds the game's shark off (first time this "
                    "session; grace %.0f s, then %.0f%%/min)\n", i + 1, g_p.graceS, g_p.chancePerMin * 100.0f);
            }
        }

        const int before = g_s[i].phase;
        const uint32_t ev = shm::Step(g_p, g_s[i], in, dt, Rnd01());
        g_allow[i] = shm::Allow(g_p, g_s[i]) && !safe;
        if (!ev) continue;
        char t[16]; Mmss(g_s[i].deepT, t, sizeof(t));
        if (ev & shm::kEvGrace)
            Log(">>> shark: player %d has swum %s in deep water - from now on a small chance (%.0f%%/min, rising)\n",
                i + 1, t, g_p.chancePerMin * 100.0f);
        if (ev & shm::kEvWarn)
        {
            Log(">>> shark: player %d - the warning (%.0f s to reach the shallows, a boat or ground)\n", i + 1, g_p.warnS);
            if (g_onWarn) g_onWarn(i);
        }
        if (ev & shm::kEvStrike) Log(">>> shark: player %d - the shark comes, after %s in deep water\n", i + 1, t);
        if (ev & shm::kEvTaken) Log(">>> shark: player %d was taken by the shark (the game's own; respawn guard %.0f s)\n", i + 1, g_p.guardS);
        if (ev & shm::kEvStockTook)
            Log(">>> shark: player %d drowned without our strike (%s)\n", i + 1, g_p.classic ? "classic" : "a path the gate does not cover");
        if (ev & shm::kEvCancel) Log("    shark: player %d - called off (%s)\n", i + 1, shm::kPhaseName[g_s[i].phase]);
        if ((ev & shm::kEvTeleport) && before != shm::kDry) Log("    shark: player %d jumped (teleport / respawn) - the swim clock starts over\n", i + 1);
    }
    g_on = true;
}

} // namespace sh
