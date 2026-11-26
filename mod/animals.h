// =============================================================================================
//  animals.h - animals at the Wilds, the farm, the beaches and the quays (round 3, 2026-11-19,
//  Notes\64-animals.md). The data and every decision are in animals_model.h (harness:
//  test\animals_harness.cpp); this is the engine side, built on exactly what spots.h does for people:
//
//  SPAWN    CreateAICharacter's own core 0x1406FE180 (dino.h's call, [P in game] on the T-Rex, a
//           Character.RideableCreature) with Character.Creature / RideableCreature types from AITypes.txt
//           [P data; that Creature spawns from the DLL like RideableCreature does is I]. Script-held, its own
//           owner hash ("lcudrive_animals"), one spawner call per world update shared with the other spawners
//           (spawn_turn.h, sitem::kTurnAnimals), invulnerable (flag 0x59, [animals] invulnerable).
//  WATCH    a game Target per animal (wd::Watch), re-validated every tick before anything touches it.
//  ORDERS   (top brain slot, so the creature's own default brain never runs while ours does)
//           rest / sleep  cAnimationTask on a loop (wd::OrderAnimN, -1), names HasAnim-checked: the first one
//                         its set has of the behaviour's list (idle / graze / peck / sleep ...); none = it stands
//           walk / run    cGotoTargetOnFoot to a POSITION in a straight line (spots.h's MoveTo #STRAIGHTLINE
//                         sequence), walk or run gait
//           fly off       a fly anim if its set has one, a run away, and the removal 2.5 s later (the stock
//                         seagull flies off and fades; we remove it, the herd fills again after [animals] respawn)
//  REMOVE   dino.h's (the script's own Character.Kill), with its fall-back (script hold released + the
//           population manager's removal), exactly as spots.h.
//  Nothing is ever ordered AT a player: no attack, no follow - the bear only ever walks away.
//  Everything stands down for 4 s after a teleport, in story missions, while lcudrive is off ([), in the
//  Backrooms (br::Quiet), with [animals] enable = 0; on unhook / after a fault everything is let go once.
//  Own fault switch (RunAnimals, lcudrive.cpp).
// =============================================================================================
#pragma once
#include "animals_model.h"
#include "spawn_turn.h"

namespace an {

using wd::Fn;

struct Slot { uint8_t* ch; void* def; wd::SP watch; bool preloaded; int preDef; void* spawnPm; };
static Slot          g_slot[anm::kMaxAnimals] = {};
static anm::Animal   g_an[anm::kMaxAnimals] = {};
static anm::HerdMem  g_hm = {};
static spm::Settle   g_settle = {};
static uint32_t      g_owner = 0;
static uint32_t      g_stamp = 0;
static bool          g_init = false;
static volatile bool g_stopReq = false;
static bool          g_released = false;
static uint32_t      g_said = 0;

// ---- type -> the spawner's indices, resolved once each ------------------------------------------------
struct Kind { int ci, ti, di; bool tried; };
static Kind g_kind[32] = {};

static Kind* Resolve(int type)
{
    if (type < 0 || type >= anm::TypeCount() || type >= 32) return nullptr;
    Kind& k = g_kind[type];
    if (k.tried) return &k;
    k = Kind{ -1, -1, -1, true };
    const anm::Type& t = anm::TypeAt(type);
    const char* cls = anm::ClsName(t.cls);
    uint8_t* table = *reinterpret_cast<uint8_t**>(g_base + dn::RVA_CHAR_CLASSES);
    const int n = *reinterpret_cast<int*>(g_base + dn::RVA_CHAR_CLASS_COUNT);
    const int ci = PtrOk(table) ? Fn<dn::ClassIdxFn>(dn::FN_CLASSIDX)(wd::H(cls), true) : -1;
    if (ci < 0 || ci >= n) { Log(">>> animals: the game has no character class %s (class %d of %d)\n", cls, ci, n); return &k; }
    void* entry = table + static_cast<size_t>(ci) * 0x40;
    const int ti = Fn<dn::TypeIdxFn>(dn::FN_TYPEIDX)(entry, wd::H(t.name));
    const int di = ti >= 0 ? Fn<dn::DefIdxFn>(dn::FN_DEFIDX)(entry, ti) : -1;
    if (ti < 0 || di < 0) { Log(">>> animals: %s.%s is not a character the game knows (type %d, definition %d)\n", cls, t.name, ti, di); return &k; }
    k.ci = ci; k.ti = ti; k.di = di;
    Log(">>> animals: %s.%s is type %d of class %d, definition %d\n", cls, t.name, ti, ci, di);
    return &k;
}

static uint8_t* Char(const Slot& s)
{
    if (!PtrOk(s.ch) || !wd::StillThere(s.watch)) return nullptr;
    if (!(At<uint8_t>(s.ch, 0x320) & 1) || At<void*>(s.ch, 0x650) != s.def) return nullptr;
    return s.ch;
}

static void FreePreload(Slot& s)
{
    if (!s.preloaded) return;
    s.preloaded = false;
    void* scm = *reinterpret_cast<void**>(g_base + dn::RVA_SCM);
    if (PtrOk(scm) && s.preDef >= 0 && g_owner) Fn<dn::PreloadFreeFn>(dn::FN_PRELOAD_FREE)(scm, s.preDef, g_owner);
}

// Walk / run to p in a straight line (spots.h GotoPos with a gait).
static bool GotoPos(uint8_t* ch, const float p[3], bool fast)
{
    void* brain = sp::Brain(ch);
    if (!brain) return false;
    void* lvl = wd::g_getLevel(ch + 0x10);
    if (!PtrOk(lvl) || !PtrOk(wd::g_getPM(lvl))) return false;
    void* nav = Fn<sp::PtrFn>(sp::FN_NAV)(lvl);
    if (!PtrOk(nav)) return false;
    const uint32_t flags = (fast ? wd::GAIT_RUN : wd::GAIT_WALK) | sp::F_TARGET | sp::F_STRAIGHT;
    alignas(16) float p4[4] = { p[0], p[1], p[2], 1.0f };
    wd::SP t = {};
    Fn<wd::NewTargetFn>(wd::FN_NEWTARGET)(&t);
    if (!PtrOk(t.obj) || !PtrOk(const_cast<LONG*>(t.rc))) return false;
    Fn<sp::TgtPosFn>(sp::FN_TGTPOS)(t.obj, p4, flags, nav);
    Fn<wd::TgtRangeFn>(wd::FN_TGTRANGE)(t.obj, 0.4f);
    uint8_t* task = Fn<wd::NewTaskFn>(wd::FN_NEWTASK)(brain, wd::TASK_GOTO_ON_FOOT, wd::SLOT_TOP);
    if (task)
    {
        wd::SP c = t;
        InterlockedIncrement(c.rc + 1);
        InterlockedIncrement(c.rc);
        Fn<wd::TaskTgtFn>(wd::FN_TASKTGT)(task, &c, flags);
    }
    Fn<wd::SpRelFn>(wd::FN_SPREL)(&t);
    return task != nullptr;
}

// The anims a behaviour tries, in order (HasAnim-checked by OrderAnimN; names as the animal sets / scripts use [I]).
static int RestAnims(int beh, bool asleep, const char* const** out)
{
    static const char* const sleep[] = { "Sleep", "Sleeping", "sleep", "Idle", "idle" };
    static const char* const graze[] = { "Eat", "Graze", "Idle", "idle", "Idle2" };
    static const char* const peck[]  = { "Peck", "peck", "Eat", "Idle", "idle" };
    static const char* const swim[]  = { "Swim", "SwimIdle", "Idle", "idle" };
    static const char* const perch[] = { "Idle", "idle", "Peck", "Perch" };
    static const char* const idle[]  = { "Idle", "idle", "Idle2", "Eat" };
    #define AN_LIST(a) *out = a; return static_cast<int>(sizeof(a) / sizeof(a[0]))
    if (asleep) { AN_LIST(sleep); }
    switch (beh)
    {
    case anm::bGraze: case anm::bGrazeShy: case anm::bIdlePaddock: AN_LIST(graze);
    case anm::bPeck: AN_LIST(peck);
    case anm::bSwimLake: AN_LIST(swim);
    case anm::bPerchFlyoff: case anm::bPerchNight: AN_LIST(perch);
    default: AN_LIST(idle);
    }
    #undef AN_LIST
}
static int GestureAnims(int beh, const char* const** out)
{
    static const char* const graze[] = { "Eat", "Graze", "LookAround", "Idle2" };
    static const char* const peck[]  = { "Peck", "peck", "Eat", "Flap" };
    static const char* const bird[]  = { "Peck", "Flap", "LookAround", "Preen" };
    static const char* const other[] = { "LookAround", "Idle2", "Sniff", "Eat" };
    #define AN_LIST(a) *out = a; return static_cast<int>(sizeof(a) / sizeof(a[0]))
    switch (beh)
    {
    case anm::bGraze: case anm::bGrazeShy: case anm::bIdlePaddock: AN_LIST(graze);
    case anm::bPeck: AN_LIST(peck);
    case anm::bPerchFlyoff: case anm::bPerchNight: AN_LIST(bird);
    default: AN_LIST(other);
    }
    #undef AN_LIST
}
static bool PlayFirst(uint8_t* ch, const char* const* names, int n, int loops)
{
    for (int i = 0; i < n; ++i) if (wd::OrderAnimN(ch, lf::Anim(names[i]), loops)) return true;
    return false;
}

static bool ChPos(uint8_t* ch, float out[3]) { return sp::ChPos(ch, out); }

struct World
{
    void* pm; void* lvl; const Config* cfg; int idx; bool* spawned;
    Slot& S() { return g_slot[idx]; }
    const anm::Herd& H(int herd) { return cfg->animals_data.herds[herd]; }
    int Spawn(int herd, const float at[3], float t)
    {
        Slot& s = S();
        s.spawnPm = pm;
        if (!spt::Mine(sitem::kTurnAnimals) || *spawned) return 1;
        const anm::Herd& h = H(herd);
        Kind* k = Resolve(h.type);
        if (!k || k->di < 0) return -4;
        g_owner = wd::H("lcudrive_animals");
        {
            float q[3];
            for (int j = 0; j < 2; ++j) if (ap::PlayerPos(j, q) && anm::Dist2(q, at) < 6.0f) return 1;   // not beside anyone
        }
        float dx = h.pos[0] - at[0], dz = h.pos[2] - at[2];
        const float l = std::sqrt(dx * dx + dz * dz);
        if (l > 0.01f) { dx /= l; dz /= l; } else { dx = 0.0f; dz = 1.0f; }
        alignas(16) float pos[4] = { at[0], at[1] + 0.3f, at[2], 1.0f };
        alignas(16) float dir[4] = { -dz, 0.0f, dx, 0.0f };                    // side-on to the centre
        int st = -1;
        *spawned = true;
        uint8_t* ch = Fn<dn::SpawnFn>(dn::FN_SPAWN)(pm, lvl, k->ci, k->ti, pos, dir, &st, true, false, g_owner);
        if (st == dnm::kOk && PtrOk(ch))
        {
            s.preloaded = false;
            if (!wd::Watch(ch, &s.watch)) { Log(">>> animals[%d]: spawned %p but no watch could be made - removed again\n", idx, ch); dn::Kill(ch); return -3; }
            s.ch = ch; s.def = At<void*>(ch, 0x650);
            if (cfg->animals_invulnerable) Fn<dn::SetFlagFn>(dn::FN_SETFLAG)(ch, dn::FLAG_INVULNERABLE, true, &dn::kLayer0);
            Log(">>> animals[%d]: %s (%s) at (%.1f, %.2f, %.1f), %s, after %.1f s, ch=%p (%s)\n", idx, h.name, anm::TypeAt(h.type).name,
                at[0], at[1], at[2], anm::Def(h.beh).name, t, ch, CharDefName(ch));
            return 0;
        }
        if (st == dnm::kOk) return -2;
        if (st == dnm::kStreaming) { s.preloaded = true; s.preDef = k->di; }
        return dnm::Retry(st) ? 1 : (st == 1 ? -1 : st);
    }
    void AbandonSpawn() { FreePreload(S()); S().spawnPm = nullptr; }
    bool Rest(int beh, bool asleep)
    {
        uint8_t* ch = Char(S()); if (!ch) return false;
        const char* const* n = nullptr; const int k = RestAnims(beh, asleep, &n);
        return PlayFirst(ch, n, k, -1);
    }
    bool Gesture(int beh)
    {
        uint8_t* ch = Char(S()); if (!ch) return false;
        const char* const* n = nullptr; const int k = GestureAnims(beh, &n);
        return PlayFirst(ch, n, k, 1);
    }
    bool Goto(const float p[3], bool fast) { uint8_t* ch = Char(S()); return ch && GotoPos(ch, p, fast); }
    bool FlyOff(const float p[3])
    {
        uint8_t* ch = Char(S()); if (!ch) return false;
        static const char* const fly[] = { "FlyOff", "Fly", "TakeOff", "Flap" };
        if (PlayFirst(ch, fly, 4, 1)) return true;
        return GotoPos(ch, p, true);
    }
    void Remove(const char* why)
    {
        uint8_t* ch = Char(S()); if (!ch) return;
        const bool quiet = _stricmp(why, "nobody near") == 0 || _stricmp(why, "flew off") == 0;
        if (!quiet || !(g_said & 1)) Log(">>> animals[%d]: it goes - %s\n", idx, why);
        if (quiet) g_said |= 1;
        dn::Kill(ch);
    }
    void Purge()
    {
        uint8_t* ch = Char(S()); if (!ch) return;
        if (At<int32_t>(ch, dn::CH_SCRIPT_HOLD) > 0) Fn<dn::ChFn>(dn::FN_SCRIPT_RELEASE)(ch);
        Fn<dn::ChFn>(dn::FN_PM_REMOVE)(ch);
    }
    void Forget(const char* why)
    {
        if (PtrOk(S().ch) && why && why[0] == 'i') Log(">>> animals[%d]: %s\n", idx, why);
        wd::Unwatch(&S().watch); S().ch = nullptr; S().def = nullptr;
    }
    void Dropped() { wd::Unwatch(&S().watch); S().ch = nullptr; S().def = nullptr; }
    void Say(const char* fmt, ...)
    {
        char buf[384];
        va_list a; va_start(a, fmt);
        _vsnprintf_s(buf, sizeof(buf), _TRUNCATE, fmt, a);
        va_end(a);
        Log("%s", buf);
    }
};

static anm::Params ParamsOf(const Config& cfg)
{
    anm::Params p;
    p.maxAnimals = cfg.animals_max; p.perHerd = cfg.animals_per_herd;
    p.nearU = cfg.animals_near; p.farU = cfg.animals_far; p.minSpawnU = cfg.animals_min_spawn;
    p.shy = cfg.animals_shy; p.respawnS = cfg.animals_respawn;
    return p;
}

static void ReleaseAll(const Config& cfg, const char* why)
{
    bool spawned = true;
    for (int i = 0; i < anm::kMaxAnimals; ++i)
    {
        if (g_an[i].state == anm::aIdle && !PtrOk(g_slot[i].ch) && !g_slot[i].preloaded) continue;
        World w = { nullptr, nullptr, &cfg, i, &spawned };
        anm::Release(w, g_an[i], Char(g_slot[i]) != nullptr, why);
        FreePreload(g_slot[i]);
    }
}

static void Release(const Config& cfg, const char* why)
{
    if (g_released) return;
    g_released = true;
    ReleaseAll(cfg, why);
}

// once per world update, after the life spots
static void Tick(void* pm, float dt, const Config& cfg, bool on)
{
    if (g_released) return;
    if (g_stopReq) { Release(cfg, "lcudrive unhooked"); return; }
    if (!g_init)
    {
        g_init = true;
        for (int i = 0; i < anm::kMaxAnimals; ++i) { g_an[i].rng = 0x85EBCA6Bu * static_cast<uint32_t>(i + 7) ^ GetTickCount(); if (!g_an[i].rng) g_an[i].rng = 1; anm::Reset(g_an[i]); }
        g_stamp = cfg.animals_stamp;
    }
    const anm::Data& d = cfg.animals_data;
    if (cfg.animals_stamp != g_stamp)
    {
        Log(">>> animals: the herd data changed - every animal goes and comes back from the new table\n");
        ReleaseAll(cfg, "the data changed");
        g_hm = anm::HerdMem{};
        g_stamp = cfg.animals_stamp;
    }
    bool world = false;
    void* lvl = nullptr;
    uint8_t* p1 = wd::RawPlayer(0);
    if (PtrOk(p1))
    {
        lvl = wd::g_getLevel(p1 + 0x10);
        world = PtrOk(lvl) && wd::g_getPM(lvl) == pm && At<uintptr_t>(pm, 0) == g_base + wd::RVA_VT_PM;
    }
    float pp[2][3] = {}; bool ppOk[2] = { false, false };
    bool mission = false;
    if (world)
    {
        for (int i = 0; i < 2; ++i) ppOk[i] = ap::PlayerPos(i, pp[i]);
        mission = wd::MissionRunning();
        if (spm::SettleStep(g_settle, dt, pp, ppOk) && !(g_said & 4))
        {
            g_said |= 4;
            Log(">>> animals: a player jumped (a teleport / a new player) - nothing is put down, removed or ordered for 4 s\n");
        }
    }
    const bool settling = spm::Settling(g_settle);
    const bool live = on && cfg.animals_enable && cfg.animals_max > 0;
    const bool night = lights::g_sunPhase > 3.3f;
    const anm::Params p = ParamsOf(cfg);
    anm::CoolHerds(g_hm, dt);
    bool spawned = false;

    uint8_t* chs[anm::kMaxAnimals];
    bool anySpawning = false;
    for (int i = 0; i < anm::kMaxAnimals; ++i) { chs[i] = Char(g_slot[i]); if (g_an[i].state == anm::aSpawning) anySpawning = true; }
    int wantHerd = -1, wantSlot = -1;
    if (world && live && !mission && !settling && !anySpawning && d.nHerds > 0)
    {
        for (int i = 0; i < anm::kMaxAnimals && i < p.maxAnimals; ++i)
            if (g_an[i].state == anm::aIdle && g_an[i].cool <= 0.0f) { wantSlot = i; break; }
        if (wantSlot >= 0) wantHerd = anm::PickSpawn(d, g_an, anm::kMaxAnimals, g_hm, pp, ppOk, p, night);
        if (wantHerd < 0) wantSlot = -1;
    }
    for (int i = 0; i < anm::kMaxAnimals; ++i)
    {
        anm::Animal& m = g_an[i];
        if (m.state == anm::aIdle && i != wantSlot) { if (m.cool > 0.0f) m.cool -= dt; continue; }
        anm::Facts f = {};
        f.dt = dt; f.on = live; f.world = world; f.mission = mission; f.settling = settling; f.night = night;
        f.samePm = pm == g_slot[i].spawnPm;
        f.have = chs[i] != nullptr;
        f.posKnown = chs[i] && ChPos(chs[i], f.pos);
        f.busy = chs[i] && wd::SlotBusy(chs[i], wd::SLOT_TOP);
        f.nearPlayer = 1e9f; f.nearHerd = 1e9f;
        const int herd = m.state == anm::aIdle ? wantHerd : m.herd;
        for (int k = 0; k < 2; ++k) if (ppOk[k])
        {
            if (f.posKnown) { const float dd = anm::Dist2(pp[k], f.pos); if (dd < f.nearPlayer) { f.nearPlayer = dd; for (int a = 0; a < 3; ++a) f.playerPos[a] = pp[k][a]; } }
            if (herd >= 0 && herd < d.nHerds) f.nearHerd = std::fmin(f.nearHerd, anm::Dist2(pp[k], d.herds[herd].pos));
        }
        if (chs[i] && !f.posKnown) f.nearPlayer = 0.0f;                  // not known: treated as watched (never removed in view)
        f.hold = anySpawning && m.state != anm::aSpawning;
        f.wantHerd = i == wantSlot ? wantHerd : -1;
        World w = { pm, lvl, &cfg, i, &spawned };
        anm::Tick(w, d, g_an, anm::kMaxAnimals, i, g_hm, p, f);
        chs[i] = Char(g_slot[i]);
    }
}

} // namespace an
