// =============================================================================================
//  boats.h - [boats2] the traffic boats: no more processions (2026-11-19, Notes 56 section 8).
//  The decisions are in boats_model.h (pure; test\boats_harness.cpp). This is the engine side, run
//  from cPopulationManager::Update after the original (the system phase, as sites.h):
//    * spawn_gap: the boat map's two spawn-spacing floats (0x1411700E0 = 16, 0x1411700E8 = 8, read only
//      by the placement routine 0x140702420 at 0x1407024F1 / 0x1407024FA) x [boats] spawn_gap. The two
//      instructions are checked once (they must read exactly those addresses) and the floats must hold
//      the stock values or ours; anything else = not touched, logged. Stock again at `[`, enable = 0,
//      unhook, and after a fault here.
//    * files / cap: every 0.25 s the traffic boats (PM+0x568 traffic slots whose vehicle class is 3)
//      go through btm::Step; the one it names fades (ud vt[13] {0, FadeOut}, VehicleAction_Destroy's
//      call - the wanted mode's FadeOutCar, sites.h FadeOut). Never in a story mission, never a player's
//      boat, a chasing police boat (veh+0xFC7 & 0x20), or one of the stands' / marinas' boats.
//  Its own fault switch: a fault = the stock floats back and nothing more from here for the session.
// =============================================================================================
#pragma once
#include "boats_model.h"

namespace bt {

static bool          g_fault = false;
static bool          g_restored = false;     // after a fault / unhook: the floats are stock again
static int           g_sig = 0;              // 0 = not checked, 1 = the code reads our floats, -1 = it does not
static float*        g_fLane = nullptr;
static float*        g_fSpawn = nullptr;
static float         g_k = 1.0f;             // the factor in the floats now (1 = stock)
static btm::Mem      g_mem;
static float         g_acc = 0.0f, g_sumT = 0.0f;
static int           g_fadedFile = 0, g_fadedCap = 0;
static bool          g_wasOn = false;

static bool CheckSig()
{
    if (g_sig) return g_sig > 0;
    const uint8_t* rl = reinterpret_cast<const uint8_t*>(g_base + btm::RVA_READ_LANE);
    const uint8_t* rs = reinterpret_cast<const uint8_t*>(g_base + btm::RVA_READ_SPAWN);
    const uint64_t tl = btm::MovssRipTarget(rl, g_base + btm::RVA_READ_LANE);
    const uint64_t ts = btm::MovssRipTarget(rs, g_base + btm::RVA_READ_SPAWN);
    const bool where = tl == g_base + btm::RVA_GAP_LANE && ts == g_base + btm::RVA_GAP_SPAWN;
    float* fl = reinterpret_cast<float*>(g_base + btm::RVA_GAP_LANE);
    float* fs = reinterpret_cast<float*>(g_base + btm::RVA_GAP_SPAWN);
    const bool stock = where && *fl == btm::kStockLane && *fs == btm::kStockSpawn;
    g_sig = where && stock ? 1 : -1;
    if (g_sig > 0)
    {
        g_fLane = fl; g_fSpawn = fs;
        Log(">>> boats: the boat map's spawn spacing is where expected (0x%llX reads %.1f, 0x%llX reads %.1f) - [boats] spawn_gap applies\n",
            static_cast<unsigned long long>(0x140000000ull + btm::RVA_GAP_LANE), *fl,
            static_cast<unsigned long long>(0x140000000ull + btm::RVA_GAP_SPAWN), *fs);
    }
    else
        Log(">>> boats: the spawn-spacing code is NOT as expected (reads %p / %p, values %.2f / %.2f) - spawn_gap is not applied "
            "(the file breaker still runs)\n", reinterpret_cast<void*>(tl), reinterpret_cast<void*>(ts), where ? *fl : 0.0f, where ? *fs : 0.0f);
    return g_sig > 0;
}

// The floats for factor k (1 = stock). Only ever written when CheckSig passed.
static void SetGap(float k, const char* why)
{
    if (!CheckSig() || k == g_k) return;
    *g_fLane = btm::LaneGap(k);
    *g_fSpawn = btm::SpawnGap(k);
    g_k = k;
    Log(">>> boats: spawn spacing x%.2f (lane gap %.1f, spawn-point gap %.1f) - %s\n", k, *g_fLane, *g_fSpawn, why);
}

// Back to the stock floats (unhook, a fault, `[`): safe from any thread - two aligned float writes.
static void Restore(const char* why)
{
    __try
    {
        if (g_sig > 0 && g_k != 1.0f)
        {
            *g_fLane = btm::kStockLane; *g_fSpawn = btm::kStockSpawn; g_k = 1.0f;
            Log(">>> boats: spawn spacing back to stock (16 / 8) - %s\n", why);
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
    g_restored = true;
}

// One of the stands' / marinas' vehicles (theirs to look after).
static bool Owned(const void* v)
{
    for (const si::Slot& s : si::g_slot) if (si::Veh(s) == v) return true;
    for (const si::Slot& s : mar::g_berth) if (si::Veh(s) == v) return true;
    for (const si::Slot& s : mar::g_st) if (si::Veh(s) == v) return true;
    return false;
}

static bool PlayerPos(int i, void** veh, float out[3])
{
    uint8_t* ch = wd::RawPlayer(i);
    *veh = nullptr;
    if (!PtrOk(ch)) return false;
    void* v = i == 0 ? ::PlayerVehicle() : ::Player2Vehicle();
    void* body = PtrOk(v) ? wd::BodyOf(v) : nullptr;
    *veh = PtrOk(v) ? v : nullptr;
    for (int k = 0; k < 3; ++k) out[k] = body ? At<float>(body, eng::BODY_POS + 4 * k) : At<float>(ch, 0xA8 + 4 * k);
    return IsFin(out[0]) && IsFin(out[1]) && IsFin(out[2]);
}

static void Tick(void* pm, float dt, const Config& cfg, bool on, bool lcuOn)
{
    const btm::Params& p = cfg.boats;
    on = on && p.enable;
    if (!on)
    {
        if (g_wasOn) { SetGap(1.0f, lcuOn ? "[boats] enable = 0" : "lcudrive off ([)"); g_mem = btm::Mem(); g_wasOn = false; }
        return;
    }
    if (!g_wasOn) { g_wasOn = true; g_acc = 0.0f; }
    SetGap(p.spawn_gap, "[boats] spawn_gap");
    g_acc += dt; g_sumT += dt;
    if (g_acc < 0.25f) return;
    const float step = g_acc; g_acc = 0.0f;

    float pp[2][3] = {}; bool ppOk[2] = { false, false }; void* pv[2] = { nullptr, nullptr };
    for (int i = 0; i < 2; ++i) ppOk[i] = PlayerPos(i, &pv[i], pp[i]);
    if (!ppOk[0] && !ppOk[1]) return;

    btm::Boat b[btm::kMax];
    void* veh[btm::kMax];
    int n = 0;
    uint8_t* slots = reinterpret_cast<uint8_t*>(pm) + 0x568;
    for (int i = 0; i < 0x80 && n < btm::kMax; ++i)
    {
        uint8_t* cell = At<uint8_t*>(slots, i * 16);
        uint8_t* ctl = At<uint8_t*>(slots, i * 16 + 8);
        if (!PtrOk(cell) || !PtrOk(ctl) || At<int32_t>(ctl, 4) == 0) continue;
        void* v = At<void*>(cell, 0);
        if (!PtrOk(v) || At<int32_t>(v, eng::VEH_CLASS) != 3) continue;          // boats only
        void* inst = At<void*>(v, eng::VEH_INSTANCE);
        void* body = wd::BodyOf(v);
        if (!PtrOk(inst) || !body) continue;
        btm::Boat& o = b[n];
        o.id = v;
        o.x = At<float>(body, eng::BODY_POS + 0); o.z = At<float>(body, eng::BODY_POS + 8);
        float fx = -At<float>(inst, eng::INST_ROW2 + 0), fz = -At<float>(inst, eng::INST_ROW2 + 8);
        const float fl = std::sqrt(fx * fx + fz * fz);
        if (!IsFin(o.x) || !IsFin(o.z) || !(fl > 0.1f)) continue;
        o.fx = fx / fl; o.fz = fz / fl;
        o.dPlayer = 1e9f;
        const float y = At<float>(body, eng::BODY_POS + 4);
        for (int k = 0; k < 2; ++k)
            if (ppOk[k])
            {
                const float dx = o.x - pp[k][0], dy = y - pp[k][1], dz = o.z - pp[k][2];
                o.dPlayer = std::fmin(o.dPlayer, std::sqrt(dx * dx + dy * dy + dz * dz));
            }
        o.lod1 = At<int32_t>(v, eng::VEH_SIMLOD) == 1;
        o.guard = v == pv[0] || v == pv[1] || (At<uint8_t>(v, eng::VEH_FLAGS7) & 0x20) != 0 || Owned(v);
        veh[n++] = v;
    }
    const bool mission = wd::MissionRunning();
    btm::Params q = p;
    if (mission) { q.files = 0; q.cap = 0; }                                  // a story mission: nothing faded
    const btm::Out o = btm::Step(g_mem, b, n, step, q);
    if (o.idx >= 0)
    {
        void* v = veh[o.idx];
        char name[64];
        const char* nm = ReadModelName(At<void*>(v, eng::VEH_DEF), name, sizeof(name)) ? name : "a boat";
        if (uint8_t* ud = VehicleUserData(v))
        {
            struct { uint32_t a; uint8_t flags; } opt = { 0, 4 };              // VehicleAction_Destroy #FadeOut
            reinterpret_cast<ap::DestroyFn>((*reinterpret_cast<void***>(ud))[13])(ud, &opt);
            if (o.why == btm::kFile) ++g_fadedFile; else ++g_fadedCap;
            if (p.log)
            {
                if (o.why == btm::kFile)
                    Log(">>> boats: a traffic %s %.0f u from the nearest player fades - it had followed another boat %.0f u ahead for %.1f s "
                        "(a file of %d)%s\n", nm, b[o.idx].dPlayer, o.gapU, o.followS, o.fileLen, b[o.idx].lod1 ? "" : ", out of view");
                else
                    Log(">>> boats: a traffic %s %.0f u from the nearest player fades - %d traffic boats within %.0f u (cap %d)\n",
                        nm, b[o.idx].dPlayer, o.inRadius, p.cap_radius, p.cap);
            }
        }
    }
    if (p.log && g_sumT >= 30.0f)
    {
        g_sumT = 0.0f;
        if (n > 0)
            Log(">>> boats: %d traffic boats about, %d within %.0f u, %d following another, longest file %d; faded so far: %d from files, "
                "%d over the cap; spawn spacing x%.2f%s\n", n, o.inRadius, p.cap_radius, o.following, o.longest, g_fadedFile, g_fadedCap,
                g_k, mission ? " (story mission: none faded)" : "");
    }
}

} // namespace bt
