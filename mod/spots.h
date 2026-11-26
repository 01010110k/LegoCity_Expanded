// =============================================================================================
//  spots.h - life at the new places: people and driving cars put there from data (2026-11-18,
//  Notes\51). The data and every decision are in spots_model.h (harness: test\spots_harness.cpp);
//  this is the engine side. The parked scenery cars of the same data are sites.h's (extra stands).
//
//  The goal: more life at the new places - people and cars there, doing their thing.
//  The new land (Port Atlas, Woods West, the brick sets) has no pedestrian network, no crowd zones
//  and no lane graph [P: no nav / glm / krawlie data is written for it by any World\ pipeline], so the
//  stock crowd (life.h's cast) and the stock traffic never appear there. So:
//
//  PEOPLE are real characters made with CreateAICharacter's own core, 0x1406FE180 - the call dino.h
//  makes for the T-Rex [P in game 2026-11-15], here with the classes / types the scripts use for
//  citizens (Character.Pedestrian "MaleA" ..., Pedestrian_Harbour "HarbourWorker" ...: AITypes.txt
//  [P data]; that these classes spawn from the DLL like RideableCreature does is [I]). Script-held
//  (never culled by the population manager), invulnerable like every scripted bystander
//  ([spots] invulnerable). Each is watched through a game Target (wanted_officers.h Watch: NPCs are
//  heap objects, no pool) and re-validated every tick before anything touches him.
//  What they do, with the game's own AI tasks in the top brain slot (so whatever default task the
//  spawner gave them never runs while ours does):
//    stand   cFaceTargetTask on a POSITION (FaceLocator's own sequence 0x140111A90 [P disasm]:
//            TgtPosQ 0x1407113F0, task 0x28, 0x14071B1E0) - it never ends, so he stands and looks;
//            or, at a pose point (sit / eat / fish / scope / sun), the pose's anim on a loop.
//    gesture cAnimationTask once (wanted_officers.h OrderAnimN [P in game]), an anim of the point's act
//            that his set has (HasAnim-checked, as life.h).
//    walk    cGotoTargetOnFoot to a POSITION in a STRAIGHT LINE (MoveTo's own sequence 0x1401131C0
//            with #STRAIGHTLINE [P disasm 2026-11-18]: TgtPos 0x1407114C0 with the level's nav object
//            0x14070CF00(0x140506D30(ch+0x10)), TgtRange, task 0x13, TaskTgt 0x140713170 with flags
//            gait | 0x2857 | 0x400000; state 0 of the task's update 0x140712B30 goes straight to its
//            straight-line child when bit 22 is set - no path is asked of the nav net [P 0x140712EAA]).
//            [U in game]: never run from the DLL. The points a person may walk between were checked
//            offline (ground continuous, no step) by Drive\tools\make_spots.py.
//  Removal is dino.h's (the script's own Character.Kill for an NPC [P in game on the T-Rex]), with its
//  fall-back (script hold released + the population manager's own removal).
//
//  CARS THAT DRIVE: SpawnAIVehicle (airport.h's call), left unparked, and from ApiVehicleCar::Update
//  (CarThunk, before the original) the car's SOURCE input block gets steer / throttle / brake from
//  spots_model.h DriveStep and its "active" byte set: 0x1404AC720 copies source -> working block only
//  while [src+0x34] != 0 [P disasm 2026-11-18], and the car's own Update does that copy each frame
//  (Notes\22 §1). The saved byte is put back when we let go. [U in game]: that a driverless script
//  vehicle's source block exists and nothing else writes it; that an empty car's engine pulls. A car
//  that does not move is "stuck": faded after 9 s, three times = no more for the session. The car is
//  only driven at simulation LOD 1 (near a camera); it yields to players and to our people.
//
//  Everything stands down for 4 s after a player teleports (spots_model.h Settle), during story
//  missions, while lcudrive is off ([), with [spots] enable = 0; on unhook and after a fault here
//  everyone / everything is let go once. Own fault switch (RunSpots, lcudrive.cpp).
// =============================================================================================
#pragma once
#include "spots_model.h"
#include "spawn_turn.h"

namespace sp {

using wd::Fn;

typedef void* (__fastcall* PtrFn)(void* x);
typedef void  (__fastcall* TgtPosFn)(void* t, const float* p4, uint32_t flags, void* nav);
typedef void  (__fastcall* TgtPosQFn)(void* t, const float* p4, uint32_t flags);
static const uintptr_t FN_TGTPOS = 0x7114C0, FN_TGTPOSQ = 0x7113F0, FN_NAV = 0x70CF00;
static const uint32_t  F_STRAIGHT = 0x400000u, F_TARGET = 0x2857u;

struct Slot
{
    uint8_t* ch;
    void*    def;
    wd::SP   watch;
    bool     preloaded;
    int      preDef;             // the definition the pending preload is for
    void*    spawnPm;
    int      member;             // which of the cast (so a spot's people differ)
};
static Slot        g_slot[spm::kMaxPeople] = {};
static spm::Person g_person[spm::kMaxPeople] = {};
static spm::Settle g_settle = {};
static uint32_t    g_owner = 0;
static uint32_t    g_rng = 0xC2B2AE35u;
static uint32_t    g_stamp = 0;              // the data's stamp the people out there belong to
static bool        g_init = false;
static volatile bool g_stopReq = false;
static bool        g_released = false;
static int         g_spawnSeq = 0;
static uint32_t    g_said = 0;               // log lines said once (bits)

// ---- the cast: class / type -> the spawner's indices, resolved once each ---------------------------
struct Kind { const char* cls; const char* type; int ci, ti, di; bool tried; };
static Kind g_kind[64] = {};                     // [round3] was 40 (two more casts)
static int  g_nKind = 0;

static Kind* Resolve(const spm::Member& m)
{
    for (int i = 0; i < g_nKind; ++i) if (g_kind[i].cls == m.cls && g_kind[i].type == m.type) return &g_kind[i];
    if (g_nKind >= 64) return nullptr;
    Kind& k = g_kind[g_nKind++];
    k = Kind{ m.cls, m.type, -1, -1, -1, true };
    uint8_t* table = *reinterpret_cast<uint8_t**>(g_base + dn::RVA_CHAR_CLASSES);
    const int n = *reinterpret_cast<int*>(g_base + dn::RVA_CHAR_CLASS_COUNT);
    const int cls = PtrOk(table) ? Fn<dn::ClassIdxFn>(dn::FN_CLASSIDX)(wd::H(m.cls), true) : -1;
    if (cls < 0 || cls >= n) { Log(">>> spots: the game has no character class %s (class %d of %d)\n", m.cls, cls, n); return &k; }
    void* entry = table + static_cast<size_t>(cls) * 0x40;
    const int type = Fn<dn::TypeIdxFn>(dn::FN_TYPEIDX)(entry, wd::H(m.type));
    const int def = type >= 0 ? Fn<dn::DefIdxFn>(dn::FN_DEFIDX)(entry, type) : -1;
    if (type < 0 || def < 0) { Log(">>> spots: %s.%s is not a character the game knows (type %d, definition %d)\n", m.cls, m.type, type, def); return &k; }
    k.ci = cls; k.ti = type; k.di = def;
    Log(">>> spots: %s.%s is type %d of class %d, definition %d\n", m.cls, m.type, type, cls, def);
    return &k;
}

// Ours and still the character we made (dino.h Char).
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

// ---- orders on a position -------------------------------------------------------------------------
static void* Brain(uint8_t* ch) { void* b = At<void*>(ch, 0x78); return PtrOk(b) ? b : nullptr; }

// MoveTo's own pace pick for speed v (0x14011336D-0x1401133C2 [P disasm 2026-11-25]): the definition
// [[ch+0x650]+0x50] gives the thresholds; v > +0x2C sprint, v > +0x28 run, else walk - each with the
// MoveSpeedAddOn 0x1406703B0(ch, v, mode 0/1/3) holding him to v. Without the add-on (as before 11-25)
// a walk went at the definition's slowest pace, which the game animates as the sneaking tiptoe.
typedef void (__fastcall* SpeedFn)(uint8_t* ch, float v, int mode);
static const uintptr_t FN_SPEED = 0x6703B0;
static uint32_t Pace(uint8_t* ch, float v)
{
    void* cd = At<void*>(ch, 0x650);
    uint8_t* def = PtrOk(cd) ? At<uint8_t*>(cd, 0x50) : nullptr;
    if (!PtrOk(def) || !(v > 0.0f)) return wd::GAIT_WALK;
    const float sprint = At<float>(def, 0x2C), run = At<float>(def, 0x28);
    if (IsFin(sprint) && v > sprint) { Fn<SpeedFn>(FN_SPEED)(ch, v, 0); return wd::GAIT_SPRINT; }
    if (IsFin(run) && v > run)       { Fn<SpeedFn>(FN_SPEED)(ch, v, 1); return wd::GAIT_RUN; }
    Fn<SpeedFn>(FN_SPEED)(ch, v, 3);
    return wd::GAIT_WALK;
}

// Walk to p in a straight line (MoveTo #STRAIGHTLINE's own sequence), stop within `range`.
static bool GotoPos(uint8_t* ch, const float p[3], float range, float speed)
{
    void* brain = Brain(ch);
    if (!brain) return false;
    void* lvl = wd::g_getLevel(ch + 0x10);
    if (!PtrOk(lvl) || !PtrOk(wd::g_getPM(lvl))) return false;           // 0x14070CF00 reads [PM(level) + 0xE30] unchecked
    void* nav = Fn<PtrFn>(FN_NAV)(lvl);
    if (!PtrOk(nav)) return false;                                      // TgtPos reads nav+0xA8 unchecked
    const uint32_t flags = Pace(ch, speed) | F_TARGET | F_STRAIGHT;
    alignas(16) float p4[4] = { p[0], p[1], p[2], 1.0f };
    wd::SP sp = {};
    Fn<wd::NewTargetFn>(wd::FN_NEWTARGET)(&sp);
    if (!PtrOk(sp.obj) || !PtrOk(const_cast<LONG*>(sp.rc))) return false;
    Fn<TgtPosFn>(FN_TGTPOS)(sp.obj, p4, flags, nav);
    Fn<wd::TgtRangeFn>(wd::FN_TGTRANGE)(sp.obj, range);
    uint8_t* task = Fn<wd::NewTaskFn>(wd::FN_NEWTASK)(brain, wd::TASK_GOTO_ON_FOOT, wd::SLOT_TOP);
    if (task)
    {
        wd::SP c = sp;
        InterlockedIncrement(c.rc + 1);
        InterlockedIncrement(c.rc);
        Fn<wd::TaskTgtFn>(wd::FN_TASKTGT)(task, &c, flags);            // consumes the copy
    }
    Fn<wd::SpRelFn>(wd::FN_SPREL)(&sp);
    return task != nullptr;
}

// Stand and face p (FaceLocator's own sequence); the task never ends by itself.
static bool FacePos(uint8_t* ch, const float p[3])
{
    void* brain = Brain(ch);
    if (!brain) return false;
    alignas(16) float p4[4] = { p[0], p[1], p[2], 1.0f };
    wd::SP sp = {};
    Fn<wd::NewTargetFn>(wd::FN_NEWTARGET)(&sp);
    if (!PtrOk(sp.obj) || !PtrOk(const_cast<LONG*>(sp.rc))) return false;
    Fn<TgtPosQFn>(FN_TGTPOSQ)(sp.obj, p4, 0);
    uint8_t* task = Fn<wd::NewTaskFn>(wd::FN_NEWTASK)(brain, wd::TASK_FACE, wd::SLOT_TOP);
    if (task)
    {
        wd::SP c = sp;
        InterlockedIncrement(c.rc + 1);
        InterlockedIncrement(c.rc);
        Fn<wd::FaceTgtFn>(wd::FN_FACETGT)(task, &c);
    }
    Fn<wd::SpRelFn>(wd::FN_SPREL)(&sp);
    return task != nullptr;
}

// The anims of an act (names resolved by life.h Anim; every play is HasAnim-checked by OrderAnimN).
static int ActAnims(int act, const char* const** out)
{
    static const char* const look[]  = { "LookAtObject", "PointAtObject", "LookAtObject2", "Ponder", "LookAtObject3", "point", "TakePhoto" };
    static const char* const photo[] = { "TakePhoto", "PhotoPose1", "TakePhoto2", "PhotoPose2", "PointAtObject", "Wave" };
    static const char* const wait[]  = { "CheckTime", "Queue1", "Queue2", "Ponder", "Queue3", "QueueShuffle", "Wave" };
    static const char* const work[]  = { "point", "CheckTime", "Point2", "Ponder", "Shrug", "Wave", "point3" };
    static const char* const play[]  = { "Star_Jumps", "Celebration", "Cheer_Clap", "KeepyUppy", "Wave", "Celebration_Low", "Dance_01" };
    static const char* const chat[]  = { "Talk", "Ponder", "Talk2", "Shrug", "Talk3", "point", "Wave", "CheckTime" };
    static const char* const sit[]   = { "Sit_Idle", "Sitting1", "Sit_ReadPaper", "Sit_Sleepy" };
    static const char* const eat[]   = { "Sit_Eating", "Sit_Idle", "Sitting1" };
    static const char* const fish[]  = { "Fishing" };
    static const char* const scope[] = { "Seaside_Scope", "Binoculars_kneeling" };
    static const char* const sun[]   = { "Sunbathing", "Sunbathing2" };
    #define SP_LIST(a) *out = a; return static_cast<int>(sizeof(a) / sizeof(a[0]))
    switch (act)
    {
    case spm::aPhoto: SP_LIST(photo);
    case spm::aWait:  SP_LIST(wait);
    case spm::aWork:  SP_LIST(work);
    case spm::aPlay:  SP_LIST(play);
    case spm::aChat:  SP_LIST(chat);
    case spm::aSit:   SP_LIST(sit);
    case spm::aEat:   SP_LIST(eat);
    case spm::aFish:  SP_LIST(fish);
    case spm::aScope: SP_LIST(scope);
    case spm::aSun:   SP_LIST(sun);
    default:          SP_LIST(look);
    }
    #undef SP_LIST
}

static bool PlayAct(uint8_t* ch, int act, int loops)
{
    const char* const* names = nullptr;
    const int n = ActAnims(act, &names);
    const int start = static_cast<int>(spm::Rnd(g_rng) * n) % n;
    for (int i = 0; i < n; ++i)
        if (wd::OrderAnimN(ch, lf::Anim(names[(start + i) % n]), loops)) return true;
    return false;
}

static bool ChPos(uint8_t* ch, float out[3])
{
    for (int k = 0; k < 3; ++k) out[k] = At<float>(ch, 0xA8 + 4 * k);
    return IsFin(out[0]) && IsFin(out[1]) && IsFin(out[2]);
}

static void* RouteCar(int i);            // (below: a route's car, if it is out)

// The engine side of spots_model.h's actions, for one person.
struct World
{
    void* pm; void* lvl; const Config* cfg; int idx; bool* spawned;
    Slot& S() { return g_slot[idx]; }
    int Spawn(int spot, int pt, float t)
    {
        Slot& s = S();
        s.spawnPm = pm;                                                  // the world this attempt belongs to (set before any early return)
        if (!spt::Mine(sitem::kTurnSpots) || *spawned) return 1;         // not this update's spawner: again next tick
        const spm::Data& d = cfg->spots_data;
        const spm::Member* mem = nullptr;
        const int n = spm::CastMembers(d.spots[spot].cast, &mem);
        if (t <= 0.0f || s.member < 0) s.member = (g_spawnSeq++ + static_cast<int>(spm::Rnd(g_rng) * 3.0f)) % n;
        Kind* k = Resolve(mem[s.member % n]);
        if (!k || k->di < 0)
        {
            // not a character the game has: the cast's first member instead (a citizen always exists)
            k = Resolve(mem[0]);
            if (!k || k->di < 0)
            {
                // [round3] a whole cast the game does not have (rangers / anglers are new casts): a citizen instead
                const spm::Member* cit = nullptr;
                spm::CastMembers(spm::cVisitors, &cit);
                k = Resolve(cit[s.member % 12]);
                if (!k || k->di < 0) return -4;
            }
        }
        g_owner = wd::H("lcudrive_spots");
        const spm::Pt& p = d.pts[pt];
        // a player (or one of our route cars) has come up to the point while this was streaming: not onto him -
        // wait (the model gives the attempt up after 20 s)
        {
            float q[3];
            for (int j = 0; j < 2; ++j) if (ap::PlayerPos(j, q) && spm::Dist2(q, p.pos) < 4.0f) return 1;
            for (int j = 0; j < spm::kMaxRoutes; ++j) if (void* v = RouteCar(j)) if (ap::VehPos(v, q) && spm::Dist2(q, p.pos) < 3.0f) return 1;
        }
        float dx = p.look[0] - p.pos[0], dz = p.look[1] - p.pos[2];
        const float l = std::sqrt(dx * dx + dz * dz);
        if (l > 0.01f) { dx /= l; dz /= l; } else { dx = 0.0f; dz = 1.0f; }
        alignas(16) float pos[4] = { p.pos[0], p.pos[1] + 0.1f, p.pos[2], 1.0f };
        alignas(16) float dir[4] = { dx, 0.0f, dz, 0.0f };
        int st = -1;
        *spawned = true;                                                 // this tick's one spawner call
        uint8_t* ch = Fn<dn::SpawnFn>(dn::FN_SPAWN)(pm, lvl, k->ci, k->ti, pos, dir, &st, true, false, g_owner);
        if (st == dnm::kOk && PtrOk(ch))
        {
            s.preloaded = false;                                         // the spawn gave the preload back itself [P]
            if (!wd::Watch(ch, &s.watch))
            {
                Log(">>> spots[%d]: spawned %p but no watch on it could be made - removed again\n", idx, ch);
                dn::Kill(ch);
                return -3;
            }
            s.ch = ch; s.def = At<void*>(ch, 0x650);
            if (cfg->spots_invulnerable) Fn<dn::SetFlagFn>(dn::FN_SETFLAG)(ch, dn::FLAG_INVULNERABLE, true, &dn::kLayer0);
            Log(">>> spots[%d]: %s.%s at %s (%.1f, %.2f, %.1f) to %s, after %.1f s, ch=%p (%s)\n", idx, k->cls, k->type,
                d.spots[spot].name, p.pos[0], p.pos[1], p.pos[2], spm::ActName(p.act), t, ch, CharDefName(ch));
            return 0;
        }
        if (st == dnm::kOk) return -2;                                   // "spawned" but no character
        if (st == dnm::kStreaming) { s.preloaded = true; s.preDef = k->di; }
        return dnm::Retry(st) ? 1 : (st == 1 ? -1 : st);
    }
    void AbandonSpawn() { FreePreload(S()); S().spawnPm = nullptr; S().member = -1; }
    void Stand(int pt)
    {
        uint8_t* ch = Char(S());
        if (!ch) return;
        const spm::Pt& p = cfg->spots_data.pts[pt];
        if (spm::ActIsPose(p.act) && PlayAct(ch, p.act, -1)) return;     // the pose, held until his next order
        const float look[3] = { p.look[0], p.pos[1], p.look[1] };
        FacePos(ch, look);
    }
    bool Gesture(int pt)
    {
        uint8_t* ch = Char(S());
        return ch && PlayAct(ch, cfg->spots_data.pts[pt].act, 1);
    }
    bool Walk(int pt)
    {
        uint8_t* ch = Char(S());
        return ch && GotoPos(ch, cfg->spots_data.pts[pt].pos, 0.2f, cfg->spots_walk_speed);
    }
    void Remove(const char* why)
    {
        uint8_t* ch = Char(S());
        if (!ch) return;
        if (!(g_said & 1) || _stricmp(why, "nobody near") != 0) Log(">>> spots[%d]: he goes - %s\n", idx, why);
        if (_stricmp(why, "nobody near") == 0) g_said |= 1;
        dn::Kill(ch);
    }
    void Purge()
    {
        uint8_t* ch = Char(S());
        if (!ch) return;
        if (At<int32_t>(ch, dn::CH_SCRIPT_HOLD) > 0) Fn<dn::ChFn>(dn::FN_SCRIPT_RELEASE)(ch);
        Fn<dn::ChFn>(dn::FN_PM_REMOVE)(ch);
    }
    void Forget(const char* why)
    {
        if (PtrOk(S().ch) && why && why[0] == 'h') Log(">>> spots[%d]: %s\n", idx, why);
        wd::Unwatch(&S().watch);
        S().ch = nullptr; S().def = nullptr; S().member = -1;
    }
    void Dropped() { wd::Unwatch(&S().watch); S().ch = nullptr; S().def = nullptr; S().member = -1; }
    void Say(const char* fmt, ...)
    {
        char buf[384];
        va_list a; va_start(a, fmt);
        _vsnprintf_s(buf, sizeof(buf), _TRUNCATE, fmt, a);
        va_end(a);
        Log("%s", buf);
    }
};

static spm::Params ParamsOf(const Config& cfg)
{
    spm::Params p;
    p.maxPeople = cfg.spots_people ? cfg.spots_max_people : 0;
    p.perSpot = cfg.spots_per_spot;
    p.nearU = cfg.spots_near; p.farU = cfg.spots_far; p.minSpawnU = cfg.spots_min_spawn;
    p.walkShare = cfg.spots_walk; p.nightShare = cfg.spots_night;
    return p;
}

static void ReleasePeople(const Config& cfg, const char* why)
{
    bool spawned = true;
    for (int i = 0; i < spm::kMaxPeople; ++i)
    {
        if (g_person[i].state == spm::pIdle && !PtrOk(g_slot[i].ch) && !g_slot[i].preloaded) continue;
        World w = { nullptr, nullptr, &cfg, i, &spawned };
        spm::Release(w, g_person[i], Char(g_slot[i]) != nullptr, why);
        FreePreload(g_slot[i]);
    }
}

// ---------------------------------------------------------------------------------------------
//  Cars that drive a route
// ---------------------------------------------------------------------------------------------
struct CarSlot
{
    ap::WeakRef ref;
    spm::CarMem mem;
    int      model, cls, type;
    bool     resolved, bad;
    bool     preloaded;
    void*    spawnPm;
    void*    src;                // the source input block we set "active" on
    uint8_t  active0;            // ... and what that byte was
    bool     srcSet;
    float    lastHeading; bool headingOk;
    int      status;             // the last DriveStep status
    int      lastStatus;         // ... as last logged
    uint32_t frames;
};
static CarSlot g_car[spm::kMaxRoutes] = {};

static void* CarVeh(const CarSlot& s)
{
    if (!PtrOk(s.ref.ctl) || !PtrOk(s.ref.cell) || s.ref.ctl[1] <= 0) return nullptr;
    void* v = *s.ref.cell;
    return PtrOk(v) ? v : nullptr;
}

static void* RouteCar(int i) { return i >= 0 && i < spm::kMaxRoutes ? CarVeh(g_car[i]) : nullptr; }

// Our hands off its input block: the "active" byte back, the pedals up.
static void CarUnhand(CarSlot& s, void* veh)
{
    if (!s.srcSet) return;
    s.srcSet = false;
    void* src = veh ? At<void*>(veh, eng::VEH_SRCCTRL) : nullptr;
    if (PtrOk(src) && src == s.src)
    {
        At<float>(src, eng::CTRL_STEER) = 0.0f; At<float>(src, eng::CTRL_THROTTLE) = 0.0f; At<float>(src, eng::CTRL_BRAKE) = 0.0f;
        At<uint8_t>(src, eng::CTRL_ACTIVE) = s.active0;
    }
    s.src = nullptr;
}

static void CarFreePreload(CarSlot& s)
{
    if (!s.preloaded) return;
    s.preloaded = false;
    void* scm = *reinterpret_cast<void**>(g_base + ap::RVA_SCM);
    if (PtrOk(scm) && s.model >= 0 && g_owner) ap::Fn<ap::PreloadFreeFn>(ap::FN_PRELOAD_FREE)(scm, s.model, g_owner);
}

struct CarWorld
{
    void* pm; void* lvl; const Config* cfg; int idx; bool* spawned;
    CarSlot& S() { return g_car[idx]; }
    int Spawn(float t)
    {
        CarSlot& s = S();
        s.spawnPm = pm;                                                  // (before any early return: the model compares it next tick)
        if (!spt::Mine(sitem::kTurnSpots) || *spawned) return 1;
        const spm::Data& d = cfg->spots_data;
        const spm::Route& r = d.routes[idx];
        if (!s.resolved)
        {
            s.model = wd::g_modelId(*reinterpret_cast<void**>(g_base + wd::RVA_MODEL_REG), r.model);
            if (s.model < 0 || !wd::g_classType(wd::H(r.model), &s.cls, &s.type))
            {
                Log(">>> spots: route %s - the game has no vehicle %s\n", r.name, r.model);
                s.bad = true;
                return -5;
            }
            s.resolved = true;
        }
        g_owner = wd::H("lcudrive_spots");
        float pos[3], heading = 0.0f, dir[2];
        spm::RouteStart(r, d.wps, pos, &heading);
        sitem::HeadingDir(heading, dir);
        alignas(16) float p[4] = { pos[0], pos[1] + 0.3f, pos[2], 1.0f };
        alignas(16) float f[4] = { dir[0], 0.0f, dir[1], 0.0f };
        int st = -1;
        ap::WeakRef out = {};
        *spawned = true;
        ap::Fn<ap::SpawnFn>(ap::FN_SPAWN)(pm, &out, lvl, s.cls, s.type, p, f, &st, true, g_owner);
        if (st == 0 && PtrOk(out.ctl) && out.ctl[1] > 0)
        {
            s.ref = out; s.preloaded = false;
            spm::DriveReset(s.mem.drive, 1);
            s.headingOk = false; s.status = spm::dDriving; s.lastStatus = -1; s.frames = 0; s.srcSet = false; s.src = nullptr;
            void* v = CarVeh(s);
            Log(">>> spots: route %s - %s (class %d type %d) starts at (%.1f, %.2f, %.1f) heading %.0f after %.1f s, veh=%p, "
                "vehicle class %d, source input block %p\n", r.name, r.model, s.cls, s.type, pos[0], pos[1], pos[2], heading, t, v,
                v ? At<int32_t>(v, eng::VEH_CLASS) : -1, v ? At<void*>(v, eng::VEH_SRCCTRL) : nullptr);
            return 0;
        }
        if (PtrOk(out.ctl)) ap::DropRef(out);
        if (st == 0) st = -2;
        if (st == 1) s.preloaded = true;
        return sitem::RetryStatus(st) ? 1 : (st == 1 ? -1 : st);
    }
    void AbandonSpawn() { CarFreePreload(S()); S().spawnPm = nullptr; }
    void Fade(const char* why)
    {
        CarSlot& s = S();
        void* v = CarVeh(s);
        if (v)
        {
            CarUnhand(s, v);
            if (uint8_t* ud = VehicleUserData(v))
            {
                struct { uint32_t a; uint8_t flags; } o = { 0, 4 };      // VehicleAction_Destroy #FadeOut
                reinterpret_cast<ap::DestroyFn>((*reinterpret_cast<void***>(ud))[13])(ud, &o);
            }
            Log(">>> spots: route %s - the car fades out - %s\n", cfg->spots_data.routes[idx].name, why);
        }
        ap::DropRef(s.ref);
    }
    void Disown(const char* why)
    {
        CarSlot& s = S();
        CarUnhand(s, CarVeh(s));
        Log(">>> spots: route %s - %s: the car is the player's now, ours no longer\n", cfg->spots_data.routes[idx].name, why);
        ap::DropRef(s.ref);
    }
    void Dropped() { S().srcSet = false; S().src = nullptr; ap::DropRef(S().ref); }
    void Say(const char* fmt, ...)
    {
        char buf[384];
        va_list a; va_start(a, fmt);
        _vsnprintf_s(buf, sizeof(buf), _TRUNCATE, fmt, a);
        va_end(a);
        Log("%s", buf);
    }
};

static void ReleaseCars(const Config& cfg, const char* why)
{
    bool spawned = true;
    for (int i = 0; i < spm::kMaxRoutes; ++i)
    {
        CarSlot& s = g_car[i];
        CarWorld w = { nullptr, nullptr, &cfg, i, &spawned };
        void* v = CarVeh(s);
        if (s.mem.state == spm::cSpawning) w.AbandonSpawn();
        if (v)
        {
            float vp[3] = {}, pp[3] = {};
            bool beside = v == ::PlayerVehicle() || v == ::Player2Vehicle() || !ap::VehPos(v, vp);
            for (int k = 0; k < 2 && !beside; ++k) if (ap::PlayerPos(k, pp) && ap::Dist2(pp, vp) < 8.0f) beside = true;
            if (beside) w.Disown(why); else w.Fade(why);
        }
        else if (PtrOk(s.ref.ctl)) w.Dropped();
        CarFreePreload(s);
        const int fails = s.mem.fails;
        s.mem = spm::CarMem{}; s.mem.fails = fails;
        s.resolved = false; s.bad = false;
    }
}

// From ApiVehicleCar::Update, before the original, for every car that is not a player's: if it is one
// of our route cars, this frame's pedals and wheel go into its source input block.
static void DriveHook(void* veh, float dt, const Config& cfg)
{
    if (g_released) return;
    for (int i = 0; i < spm::kMaxRoutes; ++i)
    {
        CarSlot& s = g_car[i];
        if (s.mem.state != spm::cDriving && s.mem.state != spm::cDone) continue;
        if (CarVeh(s) != veh) continue;
        const spm::Data& d = cfg.spots_data;
        if (i >= d.nRoutes) return;
        void* src = At<void*>(veh, eng::VEH_SRCCTRL);
        float pos[3], nose[2];
        if (!PtrOk(src) || !ap::VehPos(veh, pos) || !ap::Nose(veh, nose))
        {
            if (!(g_said & 2)) { g_said |= 2; Log(">>> spots: route %s - the car has no source input block / no body (%p) - it cannot be driven\n", d.routes[i].name, src); }
            s.status = spm::dStuck;
            return;
        }
        if (!IsFin(dt) || dt <= 0.0f || dt > 0.25f) dt = 1.0f / 60.0f;
        spm::DriveIn in = {};
        in.dt = dt;
        in.pos[0] = pos[0]; in.pos[1] = pos[1]; in.pos[2] = pos[2];
        in.fwd[0] = nose[0]; in.fwd[1] = nose[1];
        void* inst = At<void*>(veh, eng::VEH_INSTANCE);
        void* body = PtrOk(inst) ? At<void*>(inst, eng::INST_BODY) : nullptr;
        if (PtrOk(body)) in.speed = At<float>(body, eng::BODY_LINVEL) * nose[0] + At<float>(body, eng::BODY_LINVEL + 8) * nose[1];
        if (!IsFin(in.speed)) in.speed = 0.0f;
        const float heading = spm::HeadingOf(nose[0], nose[1]);
        in.yawRate = s.headingOk ? spm::Wrap180(heading - s.lastHeading) / dt : 0.0f;
        s.lastHeading = heading; s.headingOk = true;
        in.nearest = 1e9f;
        auto see = [&](const float q[3])
        {
            const float dx = q[0] - pos[0], dz = q[2] - pos[2];
            const float dist = std::sqrt(dx * dx + dz * dz);
            if (!IsFin(dist) || std::fabs(q[1] - pos[1]) > 4.0f) return;
            const float along = dx * nose[0] + dz * nose[1], side = std::fabs(dx * nose[1] - dz * nose[0]);
            const bool ahead = along > 0.0f && side < 1.3f;              // its own width and half a person
            if (dist < 3.0f || ahead) { if (dist < in.nearest) { in.nearest = dist; in.nearestAhead = ahead; } }
        };
        float q[3];
        for (int k = 0; k < 2; ++k) if (ap::PlayerPos(k, q)) see(q);
        for (int k = 0; k < spm::kMaxPeople; ++k) if (uint8_t* ch = Char(g_slot[k])) if (ChPos(ch, q)) see(q);
        for (int k = 0; k < spm::kMaxRoutes; ++k) if (k != i) if (void* o = CarVeh(g_car[k])) if (ap::VehPos(o, q)) see(q);
        spm::Route r = d.routes[i];
        r.speed *= cfg.spots_drive_speed;
        spm::DriveParams dp;
        spm::DriveOut out;
        const bool was = s.mem.drive.flipped;
        const float sign0 = s.mem.drive.steerSign;
        const bool lod1 = At<int32_t>(veh, eng::VEH_SIMLOD) == 1;
        if (!lod1) { s.mem.drive.stuckT = 0.0f; }                        // far from every camera: the car's own update idles
        spm::DriveStep(r, d.wps, s.mem.drive, in, dp, out);
        if (!lod1 && out.status == spm::dStuck) out.status = spm::dDriving;
        if (!was && s.mem.drive.flipped && s.mem.drive.steerSign != sign0)
            Log(">>> spots: route %s - the car turns AWAY from the side it is steered to: the steering sign is flipped (now %+.0f)\n",
                d.routes[i].name, s.mem.drive.steerSign);
        s.status = out.status;
        if (out.status != s.lastStatus)
        {
            static const char* const nm[] = { "driving", "stops for someone ahead", "at the end of its route", "stuck", "off its route" };
            Log(">>> spots: route %s - the car %s (waypoint %d of %d, %.1f u/s, at (%.1f, %.2f, %.1f), lap %d)\n", d.routes[i].name,
                nm[out.status < 0 || out.status > 4 ? 0 : out.status], s.mem.drive.wp, r.nWp, in.speed, pos[0], pos[1], pos[2], s.mem.drive.laps);
            s.lastStatus = out.status;
        }
        if (!s.srcSet || s.src != src) { s.src = src; s.active0 = At<uint8_t>(src, eng::CTRL_ACTIVE); s.srcSet = true; }
        At<uint8_t>(src, eng::CTRL_ACTIVE) = 1;
        At<float>(src, eng::CTRL_STEER) = out.steer;
        At<float>(src, eng::CTRL_THROTTLE) = out.throttle;
        At<float>(src, eng::CTRL_BRAKE) = out.brake;
        At<float>(src, eng::CTRL_REVERSE) = 0.0f;
        At<float>(src, eng::CTRL_HANDBRAKE) = out.status == spm::dArrived ? 1.0f : 0.0f;
        if ((++s.frames % 600) == 300 && cfg.spots_log)
            Log("    spots: route %s wp %d err %+.0f deg steer %+.2f thr %.2f brk %.2f v %.1f lod %d\n", d.routes[i].name, s.mem.drive.wp,
                out.errDeg, out.steer, out.throttle, out.brake, in.speed, At<int32_t>(veh, eng::VEH_SIMLOD));
        return;
    }
}

// Everything back, once (unhook, or after a fault in this code).
static void Release(const Config& cfg, const char* why)
{
    if (g_released) return;
    g_released = true;
    ReleasePeople(cfg, why);
    ReleaseCars(cfg, why);
}

// once per world update, after cPopulationManager::Update
static void Tick(void* pm, float dt, const Config& cfg, bool on)
{
    if (g_released) return;
    if (g_stopReq) { Release(cfg, "lcudrive unhooked"); return; }
    if (!g_init)
    {
        g_init = true;
        for (int i = 0; i < spm::kMaxPeople; ++i) { g_person[i].rng = 0x9E3779B9u * static_cast<uint32_t>(i + 1); spm::Reset(g_person[i]); g_slot[i].member = -1; }
        g_rng ^= GetTickCount();
        if (!g_rng) g_rng = 1;
        g_stamp = cfg.spots_stamp;
    }
    const spm::Data& d = cfg.spots_data;
    if (cfg.spots_stamp != g_stamp)
    {
        // the data was reloaded: everyone belongs to the old table - they go, the new ones come
        Log(">>> spots: the life-spot data changed - everyone goes and comes back from the new table\n");
        ReleasePeople(cfg, "the data changed");
        ReleaseCars(cfg, "the data changed");
        g_stamp = cfg.spots_stamp;
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
            Log(">>> spots: a player jumped (a teleport / a new player) - nothing is put down, removed or ordered for 4 s\n");
        }
    }
    const bool settling = spm::Settling(g_settle);
    const bool live = on && cfg.spots_enable;
    const bool night = lights::g_sunPhase > 3.3f;                        // after sunset (pi) with a margin; -1 = no clock = day
    const spm::Params p = ParamsOf(cfg);
    bool spawned = false;

    // ---- people ----
    uint8_t* chs[spm::kMaxPeople];
    bool anySpawning = false;
    for (int i = 0; i < spm::kMaxPeople; ++i) { chs[i] = Char(g_slot[i]); if (g_person[i].state == spm::pSpawning) anySpawning = true; }
    int wantSpot = -1, wantPt = -1, wantSlot = -1;
    if (world && live && !mission && !settling && !anySpawning && p.maxPeople > 0 && d.nSpots > 0)
    {
        for (int i = 0; i < spm::kMaxPeople && i < p.maxPeople; ++i)
            if (g_person[i].state == spm::pIdle && g_person[i].cool <= 0.0f) { wantSlot = i; break; }
        if (wantSlot >= 0 && !spm::PickSpawn(d, g_person, spm::kMaxPeople, pp, ppOk, p, night, g_rng, &wantSpot, &wantPt)) wantSlot = -1;
    }
    for (int i = 0; i < spm::kMaxPeople; ++i)
    {
        spm::Person& m = g_person[i];
        if (m.state == spm::pIdle && i != wantSlot) { if (m.cool > 0.0f) m.cool -= dt; continue; }
        spm::Facts f = {};
        f.dt = dt; f.on = live && p.maxPeople > 0; f.world = world; f.mission = mission; f.settling = settling;
        f.samePm = pm == g_slot[i].spawnPm;
        f.have = chs[i] != nullptr;
        f.posKnown = chs[i] && ChPos(chs[i], f.pos);
        f.busy = chs[i] && wd::SlotBusy(chs[i], wd::SLOT_TOP);
        f.nearPerson = 1e9f; f.nearSpot = 1e9f;
        const int spot = m.state == spm::pIdle ? wantSpot : m.spot;
        for (int k = 0; k < 2; ++k) if (ppOk[k])
        {
            if (f.posKnown) f.nearPerson = std::fmin(f.nearPerson, spm::Dist2(pp[k], f.pos));
            if (spot >= 0 && spot < d.nSpots) f.nearSpot = std::fmin(f.nearSpot, spm::Dist2(pp[k], d.spots[spot].pos));
        }
        if (chs[i] && !f.posKnown) f.nearPerson = 0.0f;
        f.hold = anySpawning && m.state != spm::pSpawning;
        f.wantSpot = i == wantSlot ? wantSpot : -1;
        f.wantPt = i == wantSlot ? wantPt : -1;
        World w = { pm, lvl, &cfg, i, &spawned };
        spm::Tick(w, d, g_person, spm::kMaxPeople, i, p, f);
        chs[i] = Char(g_slot[i]);
    }

    // ---- cars that drive ----
    bool carSpawning = false;
    for (int i = 0; i < spm::kMaxRoutes; ++i) if (g_car[i].mem.state == spm::cSpawning) carSpawning = true;
    for (int i = 0; i < spm::kMaxRoutes; ++i)
    {
        CarSlot& s = g_car[i];
        const bool mine = i < d.nRoutes && i < cfg.spots_drive && !s.bad;
        void* veh = CarVeh(s);
        if (!mine && s.mem.state == spm::cIdle && !veh) continue;
        spm::CarFacts f = {};
        f.dt = dt; f.on = live && mine; f.world = world; f.mission = mission; f.settling = settling;
        f.samePm = pm == s.spawnPm;
        f.have = veh != nullptr;
        f.aboard = veh && (veh == ::PlayerVehicle() || veh == ::Player2Vehicle());
        f.hold = (carSpawning && s.mem.state != spm::cSpawning) || anySpawning;
        f.nearStart = 1e9f; f.nearCar = 1e9f;
        float start[3] = {}, vp[3] = {}, heading = 0.0f;
        if (i < d.nRoutes) spm::RouteStart(d.routes[i], d.wps, start, &heading);
        const bool vOk = veh && ap::VehPos(veh, vp);
        // "near the route" = near any of its waypoints (the car itself always starts at waypoint 0); it is not
        // put down with a player within 25 u of that start - a car popping up beside him - or onto anything of ours
        float nearStartPt = 1e9f;
        for (int k = 0; k < 2; ++k) if (ppOk[k])
        {
            nearStartPt = std::fmin(nearStartPt, spm::Dist2(pp[k], start));
            if (i < d.nRoutes)
                for (int q = 0; q < d.routes[i].nWp; ++q) f.nearStart = std::fmin(f.nearStart, spm::Dist2(pp[k], d.wps[d.routes[i].wp0 + q]));
            if (vOk) f.nearCar = std::fmin(f.nearCar, spm::Dist2(pp[k], vp));
        }
        if (veh && !vOk) f.nearCar = 0.0f;
        spm::CarParams cp;
        f.startBlocked = nearStartPt < 25.0f;
        for (int k = 0; k < spm::kMaxRoutes; ++k)
        {
            float op[3];
            if (k != i) if (void* o = CarVeh(g_car[k])) if (ap::VehPos(o, op) && spm::Dist2(op, start) < cp.clearU) f.startBlocked = true;
        }
        f.status = s.status;
        CarWorld w = { pm, lvl, &cfg, i, &spawned };
        spm::CarTick(w, s.mem, cp, f);
    }
}

} // namespace sp
