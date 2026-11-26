// =============================================================================================
//  wanted.h - wanted mode, step 1: police cars and bikes chase you on a debug level (Notes\34).
//
//  The game ships a complete traffic-pursuit system (Notes\33 §1): the free-roam Vehicle Thefts
//  side missions use it. A "pursuit record" (a police model + a target) in the world's
//  population manager makes every traffic vehicle of that model that passes within the record's
//  radius of you switch to the chase AI (task 0x34) and ram you; it is released back to traffic
//  past radius + 5. The model's weight in the city's "Pursuers" traffic zone makes more of it
//  spawn around you. This file drives exactly those natives, the way the scripts do:
//    start = (pool entry with our MaxInstances) + AddPursuit + zone weight,
//    stop  = DelPursuit(release) + zone remove + the saved pool entry back.
//  It runs from cPopulationManager::Update (lcudrive.cpp), after the original - the system phase
//  where the population manager edits these same lists, never from inside a vehicle update.
//  Step 1: the level comes from the ini (debug_level) or Ctrl+Alt+L; crimes, HUD and arrests later.
//  The police that chase get a real engine (below): stock, they top out at 7-11 u/s.
//  Police boats (2026-11-13): the same natives with a boat model. The engine
//  already files Enforcer types 4 and 9 under boat traffic - the BoatTraffic gridlock map and the
//  LittleShips lanes (PopulationManagers vt[43]/vt[44], 0x140180050 / 0x1401800B0) [P] - and the
//  chase AI has a boat branch (ChaseTask 0x1407460B0, def+0x1D4 == 3) [P]; no shipped script ever
//  ran a boat pursuit, so how they chase is [U] until the first test. They run only while the
//  suspect is on the water (the latch in Tick), so a boat weighted in never idles while you drive.
//  Police helicopters (2026-11-13, at the higher wanted levels): no
//  traffic record can drive a Hover vehicle, so wanted_heli.h spawns Police_Responder itself at
//  levels 4-5 and lcudrive flies it (copter_model.h) from the helicopter hook. Notes\43 has the
//  research behind both, and the review of 2026-11-13.
//  Co-op (2026-11-14): each player has their own wanted level, so one can be wanted and not the
//  other, and each has a meter. Everything about ONE player - heat, level, countdown, grace, arrest,
//  bust, stun, water, meter - is in Per (g_pl[2]); the police are shared out between the wanted
//  players unit by unit (wanted_model.h SplitUnits), each pursuit record aimed at its own player.
//  The officers on foot and the helicopters serve one player, the PRIMARY (g_sus): a v1 limit.
// =============================================================================================
#pragma once
#include "wanted_model.h"
#include "copter_model.h"
#include "busted_model.h"                // [busted1] busted: outside the station (wanted_busted.h)

namespace wd {

typedef void*    (__fastcall* PFn)(void*);
typedef int      (__fastcall* ModelIdFn)(void* reg, const char* name);
typedef bool     (__fastcall* ClassTypeFn)(uint32_t h, int* cls, int* type);
typedef void*    (__fastcall* TgtCtorFn)(void* t);
typedef void     (__fastcall* TgtSetCharFn)(void* t, void* subject, int a, float speed);
typedef void     (__fastcall* TgtDtorFn)(void* t);
typedef void     (__fastcall* AddPursuitFn)(void* pm, void* t, int cls, int type, bool matchSpeed, bool showOnMap);
typedef void     (__fastcall* DelPursuitFn)(void* pm, int cls, int type, bool release, bool destroy);
typedef void     (__fastcall* ZoneWeightFn)(void* tm, const uint32_t* zone, int model, float w);
typedef void     (__fastcall* ZoneRemoveFn)(void* tm, const uint32_t* zone, int model);
typedef bool     (__fastcall* AddModelFn)(void* tm, void* rec, bool common, bool optional, bool overwrite);
typedef void*    (__fastcall* RecCtorFn)(void* rec);

static PFn          g_getLevel = nullptr, g_getPM = nullptr, g_getTM = nullptr;
static ModelIdFn    g_modelId = nullptr;
static ClassTypeFn  g_classType = nullptr;
static TgtCtorFn    g_tgtCtor = nullptr;
static TgtSetCharFn g_tgtSetChar = nullptr;
static TgtDtorFn    g_tgtDtor = nullptr;
static AddPursuitFn g_addPursuit = nullptr;
static DelPursuitFn g_delPursuit = nullptr;
static ZoneWeightFn g_zoneWeight = nullptr;
static ZoneRemoveFn g_zoneRemove = nullptr;
static AddModelFn   g_addModel = nullptr;
static RecCtorFn    g_recCtor = nullptr;

static const uintptr_t RVA_VT_PM = 0xD348A8, FN_PM_UPDATE = 0x704140;   // cPopulationManager, slot 13
static const uintptr_t RVA_MODEL_REG = 0x17EBD38;

// the game's name hash (0x1402D9DD0): FNV prime, multiply then xor, a-z upper-cased
static uint32_t H(const char* s)
{
    uint32_t h = 0x811C9DC5u;
    for (; *s; ++s) { int c = static_cast<signed char>(*s); if (c >= 'a' && c <= 'z') c -= 32; h = h * 0x01000193u ^ static_cast<uint32_t>(c); }
    return h;
}

struct alignas(16) Target { uint8_t b[0x50]; };          // +0x40 acquire radius, +0x44 chase speed
struct TrafficModelRec                                   // 0x38, the head of a 0x58 pool entry
{
    int32_t  model, trailer, driver; uint32_t nameHash, driverHash;
    float    minSpeed, maxSpeed; int32_t colour; uint32_t category;
    uint16_t flags;          // 0x8000 default | 0x4000 siren | 0x2000 ignore road rules | 0x1000 can spawn parked
    uint16_t maxInstances; uint32_t gridlockMap, unk2C; int16_t cls, type, tCls, tType;
};
static_assert(sizeof(TrafficModelRec) == 0x38, "TrafficModelRec");

// The police per level - which models, how many, and which run when (boats, the second boat taking
// the motorbikes' record) - is wanted_model.h's kUnits / WantUnit (pure: the harness checks no level
// ever wants more than the PM's four pursuit records). Every unit lives there, boats included:
// Ours() / Foreign() only know g_p, and a pursuit record that isn't in g_p reads as a mission's own
// chase and stands wanted mode down.
using wanted::kUnits;
using wanted::kNumUnits;
using wanted::kBoat2;
using wanted::IsBoatUnit;

struct Pursuit
{
    int  model, cls, type;
    TrafficModelRec saved;      // the pool entry as it was before us (hadEntry)
    TrafficModelRec added;      // what we put in its place (Stop puts a new one back at MaxInstances 0)
    bool hadEntry, on;
    wanted::UnitPlan plan;      // while on: whom it chases (owner 0 / 1), at which level, how many
};
static Pursuit g_p[kNumUnits] = {};
static wanted::UnitPlan g_plan[kNumUnits] = {};   // the plan the records were last made from (Tick)

// ---- the water (police boats) -----------------------------------------------------------------
// ApiVehicleBoat's constructor writes class 3 to veh+0x340 (0x1404EC219; car 1, bike 2, aircraft 4,
// static 5, hover 6) [P]; the game's own "water below?" test checks the same int first (0x140049EFC).
static const int32_t   kClassBoat = 3;
// The boat gridlock map: PopulationManagers (*0x1417EC7F8) +0xF0/+0xF8, written by the script action
// SetGridlockBoatMap (0x140700370) from loadroadmap.sf - which only runs it after Act 2 mission 12.
// Until then it holds the "no map" handle {*(u64*)0x141170870, *(u16*)0x141170878} and the traffic
// manager can't create a single boat (0x14075EED1) [P].
static const uintptr_t RVA_POPMGRS = 0x17EC7F8, RVA_NULL_MAP = 0x1170870;
static const uintptr_t FN_TM_DRIVER = 0x75BB10;   // (tm, name) -> character model id; AddTrafficVehicleModel's DriverName
static const float     kWaterStartS = 1.0f;       // s on the water before the boats come out
typedef int (__fastcall* TmDriverFn)(void* tm, const char* name);

static bool          g_boat2Bad = false;      // the second boat failed to start in this world: the bikes keep their record
static volatile bool g_susBoat = false;       // the primary is in a boat right now (the radio's news: "...in a boat")
// (each player's water latch, "boats may run" and wet state are in Per, below)

static bool BoatMapLoaded()
{
    uint8_t* s = *reinterpret_cast<uint8_t**>(g_base + RVA_POPMGRS);
    if (!PtrOk(s)) return false;
    return !(At<uint64_t>(s, 0xF0) == *reinterpret_cast<const uint64_t*>(g_base + RVA_NULL_MAP) &&
             At<uint16_t>(s, 0xF8) == *reinterpret_cast<const uint16_t*>(g_base + RVA_NULL_MAP + 8));
}

static wanted::Params g_params;
static volatile LONG  g_request = -1;     // BOTH players: set by the hotkey thread / the ini at boot
static volatile LONG  g_requestP[2] = { -1, -1 };   // one player (the pause menu's Set Wanted Level: whoever paused)
static volatile LONG  g_shown = 0;        // the higher of the two levels, for the hotkey thread, the radio, life.h
static volatile LONG  g_levelNews = 0;    // (count << 8) | level: a player's level went up UNDER the other's (the radio)
static volatile bool  g_stopReq = false;  // unhook / [ : release everything
static uint32_t       g_lastLog = 0;
static bool           g_bootDone = false;

static void ResetPlayers();                // (below, with Per)
static void Init()
{
    g_getLevel   = reinterpret_cast<PFn>(g_base + 0x506D30);
    g_getPM      = reinterpret_cast<PFn>(g_base + 0x701E10);
    g_getTM      = reinterpret_cast<PFn>(g_base + 0x10E3F0);
    g_modelId    = reinterpret_cast<ModelIdFn>(g_base + 0x834EA0);
    g_classType  = reinterpret_cast<ClassTypeFn>(g_base + 0x6FB490);
    g_tgtCtor    = reinterpret_cast<TgtCtorFn>(g_base + 0x73E0B0);
    g_tgtSetChar = reinterpret_cast<TgtSetCharFn>(g_base + 0x73EC70);
    g_tgtDtor    = reinterpret_cast<TgtDtorFn>(g_base + 0x104CF0);
    g_addPursuit = reinterpret_cast<AddPursuitFn>(g_base + 0x6FCB70);
    g_delPursuit = reinterpret_cast<DelPursuitFn>(g_base + 0x705D40);
    g_zoneWeight = reinterpret_cast<ZoneWeightFn>(g_base + 0x759790);
    g_zoneRemove = reinterpret_cast<ZoneRemoveFn>(g_base + 0x75F2C0);
    g_addModel   = reinterpret_cast<AddModelFn>(g_base + 0x7594D0);
    g_recCtor    = reinterpret_cast<RecCtorFn>(g_base + 0x758200);
    ResetPlayers();
}

// ---------------------------------------------------------------------------------------------
//  The players (co-op). 2026-11-11: player 2 is chased by the police too - wanted mode
//  followed ONE suspect, whoever committed the latest crime. 2026-11-14: each player is wanted on
//  their own. `g_sus` is the player the code is talking about right now:
//    * at rest (outside AsPlayer) it is the PRIMARY: the wanted player with the higher level
//      (wanted::PickPrimary). The officers on foot and the helicopters chase the primary only, and
//      the helicopter hook (wanted_heli.h HeliFly) reads it between ticks;
//    * inside an AsPlayer scope - the crime hooks, and Tick's per-player part - it is that player.
//  Inside namespace wd, PlayerVehicle(), P1(), YouPos() and Me() mean g_sus's.
// ---------------------------------------------------------------------------------------------
static int      g_sus = 0;          // 0 = player 1, 1 = player 2
static uint32_t g_susGen = 0;       // bumped whenever the primary changes: the officers re-aim
static uint32_t g_priMs = 0;        // when it last changed (no flipping back and forth)
static float    g_recentSpeed[2] = { 0.0f, 0.0f };   // each player's vehicle speed, held briefly past an impact

struct Down
{
    bool  on, tie, knocked, overrideSet, held, punch;
    float t, len;
};
struct Bust
{
    bool  on, onFoot, inWater;  // inWater: no cuffed pose in the water (IdleNoGrav would hang you in it)
    float t;
};
static const int kNumCrimes = 10;    // = cCount (static_assert below)

// Everything wanted mode knows about ONE player.
struct Per
{
    wanted::State      st;
    wanted::WaterLatch water;
    Down     down;
    Bust     bust;
    float    crimeHeat;             // since the last director tick
    uint32_t crimeLast[kNumCrimes];
    bool     counts;                // crimes count for this player (not being busted, no grace) - Tick decides
    bool     inGrace;               // the after-bust grace is running (logged when it ends)
    bool     arrestShown;
    bool     boatsUp;               // this tick: the water units may run for them (latch + ini + boat map)
    uint8_t  wetHow;                // 0 dry, 1 in a boat, 2 swimming (the log)
    int      applied;               // the level their police were last made for
    // this tick's police, from RefreshCops: the nearest MANNED vehicle chasing them, their own speed, the boats
    float    copNearest, youSpeed, boatNear;
    int      nBoat, nBoatManned;
    // the brick meter (lcudrive publishes it through the 3Dmigoto bridge: slot 5 player 1, slot 6 player 2)
    volatile float hudLevel, hudHeat, hudFlash, hudAlpha;
};
static Per g_pl[2] = {};
static volatile LONG g_p2Here = 0;  // player 2 is in the game (the meters' layout; set by Tick)
static Per& Me() { return g_pl[g_sus & 1]; }
static const char* Pn(int i) { return i == 1 ? "P2" : "P1"; }
static const char* Pn() { return Pn(g_sus & 1); }

static void ResetPlayer(Per& p)
{
    const float a = p.hudAlpha;
    wanted::Reset(p.st); wanted::ResetWater(p.water);
    p.down = {}; p.bust = {};
    p.crimeHeat = 0.0f; std::memset(p.crimeLast, 0, sizeof(p.crimeLast));
    p.counts = false; p.inGrace = false; p.arrestShown = false; p.boatsUp = false; p.wetHow = 0; p.applied = 0;
    p.copNearest = 1e9f; p.youSpeed = 0.0f; p.boatNear = 1e9f; p.nBoat = p.nBoatManned = 0;
    p.hudLevel = 0.0f; p.hudHeat = 0.0f; p.hudFlash = 0.0f; p.hudAlpha = a;      // (the meter fades out by itself)
}
static void ResetPlayers() { for (Per& p : g_pl) { ResetPlayer(p); p.hudAlpha = 0.0f; } }

static uint8_t* RawPlayer(int i)
{
    uint8_t* p1 = *reinterpret_cast<uint8_t**>(g_base + eng::RVA_PLAYER1);
    if (i == 0) return p1;
    uint8_t* p2 = reinterpret_cast<uint8_t**>(g_base + eng::RVA_PLAYERS)[1];
    if (!PtrOk(p2) || p2 == p1) return nullptr;
    if (!(At<uint8_t>(p2, 0x320) & 1) || !At<uint8_t>(p2, 0x3E0) || !At<uint8_t>(p2, 0x3E1)) return nullptr;
    return p2;
}
static uint8_t* SusChar() { uint8_t* p = g_sus == 1 ? RawPlayer(1) : nullptr; return p ? p : RawPlayer(0); }
static void* PlayerVehicle() { return g_sus == 1 && RawPlayer(1) ? ::Player2Vehicle() : ::PlayerVehicle(); }
// "As this player" for the length of a hook: whose crime an event is gets judged with their position and car.
struct AsPlayer { int old; explicit AsPlayer(int i) : old(g_sus) { g_sus = i; } ~AsPlayer() { g_sus = old; } };

struct World { void* player; void* lvl; void* pm; void* tm; bool ok; };

static World GetWorld()
{
    World w = {};
    w.player = SusChar();
    if (!PtrOk(w.player)) return w;
    w.lvl = g_getLevel(reinterpret_cast<uint8_t*>(w.player) + 0x10);
    if (!PtrOk(w.lvl)) return w;
    w.pm = g_getPM(w.lvl);
    w.tm = g_getTM(w.lvl);
    w.ok = PtrOk(w.pm) && PtrOk(w.tm) && At<uint8_t>(w.tm, 0x1FC) != 0 &&            // new traffic active
           At<uintptr_t>(w.pm, 0) == g_base + RVA_VT_PM;
    return w;
}

// the pursuit record ring at PM+0x2398 (0x60 records)
template <class F> static void ForRecords(const World& w, F f)
{
    uint8_t* r = At<uint8_t*>(w.pm, 0x2398);
    if (!PtrOk(r)) return;
    uint8_t* d = At<uint8_t*>(r, 8);
    const uint16_t n = At<uint16_t>(r, 0x10), h = At<uint16_t>(r, 0x12), cap = At<uint16_t>(r, 0x14);
    if (!PtrOk(d) || !cap || n > cap) return;
    for (uint16_t i = 0; i < n; ++i) f(d + ((h + i) % cap) * 0x60);
}

static int UnitOf(int cls, int type)
{
    for (int u = 0; u < kNumUnits; ++u) if (g_p[u].on && g_p[u].cls == cls && g_p[u].type == type) return u;
    return -1;
}
static bool Ours(int cls, int type) { return UnitOf(cls, type) >= 0; }

// a pursuit we didn't start = a mission or side mission is chasing someone: stand down
static bool Foreign(const World& w)
{
    bool foreign = false;
    ForRecords(w, [&](uint8_t* e) { if (e[0x5A] && !Ours(At<int32_t>(e, 0x50), At<int32_t>(e, 0x54))) foreign = true; });
    return foreign;
}

// The live pursuers of one player: of the records whose unit chases them.
static int Pursuers(const World& w, int pl)
{
    int c = 0;
    ForRecords(w, [&](uint8_t* e) {
        if (!e[0x5A]) return;
        const int u = UnitOf(At<int32_t>(e, 0x50), At<int32_t>(e, 0x54));
        if (u >= 0 && (g_p[u].plan.owner & 1) == pl) c += At<int32_t>(e, 0x5C);
    });
    return c;
}

static TrafficModelRec* FindPoolEntry(const World& w, int model)
{
    uint8_t* r = At<uint8_t*>(w.tm, 0xD0);
    if (!PtrOk(r)) return nullptr;
    uint8_t* d = At<uint8_t*>(r, 8);
    const uint16_t n = At<uint16_t>(r, 0x10), h = At<uint16_t>(r, 0x12), cap = At<uint16_t>(r, 0x14);
    if (!PtrOk(d) || !cap || n > cap) return nullptr;
    for (uint16_t i = 0; i < n; ++i)
    {
        TrafficModelRec* e = reinterpret_cast<TrafficModelRec*>(d + ((h + i) % cap) * 0x58);
        if (e->model == model) return e;
    }
    return nullptr;
}

static const uint32_t& Pursuers_Zone() { static const uint32_t z = H("Pursuers"); return z; }

// The pursuit records of one police model (cls, type) - all, and active (+0x5A) - and how full the
// list is. AddPursuit (0x1406FCB70) [P] first looks for a record of the same model and target and
// re-activates it; only failing that does it append, and only if n < cap. Our Stop removes its
// records (DelPursuit with release: 0x140705A60 takes it out of the ring), so a Start normally
// appends - but a re-activation is a success too.
static void CountRecords(const World& w, int cls, int type, int& all, int& active)
{
    all = active = 0;
    ForRecords(w, [&](uint8_t* e) {
        if (At<int32_t>(e, 0x50) == cls && At<int32_t>(e, 0x54) == type) { ++all; if (e[0x5A]) ++active; }
    });
}

static void RecordsUsed(const World& w, int& n, int& cap)
{
    n = cap = 0;
    uint8_t* r = At<uint8_t*>(w.pm, 0x2398);
    if (!PtrOk(r)) return;
    n = At<uint16_t>(r, 0x10); cap = At<uint16_t>(r, 0x14);
}

static bool Start(const World& w, int u, const wanted::UnitPlan& plan, float radius, float speed, const Config& cfg)
{
    Pursuit& P = g_p[u];
    P = Pursuit{};
    const int maxInst = plan.inst;
    uint8_t* tgt = RawPlayer(plan.owner);                    // whom this record chases
    if (!PtrOk(tgt)) return false;
    const char* name = kUnits[u].name;
    const bool boat = IsBoatUnit(u);
    P.model = g_modelId(*reinterpret_cast<void**>(g_base + RVA_MODEL_REG), name);
    if (P.model < 0 || !g_classType(H(name), &P.cls, &P.type))
    {
        Log(">>> wanted: %s - not a model this game knows (model %d) - skipped\n", name, P.model);
        return false;
    }
    TrafficModelRec r;
    int driver = -1;
    if (TrafficModelRec* e = FindPoolEntry(w, P.model)) { P.saved = *e; P.hadEntry = true; r = *e; }
    else
    {
        std::memset(&r, 0, sizeof(r));
        g_recCtor(&r);
        r.model = P.model; r.nameHash = H(name); r.minSpeed = 3.5f; r.maxSpeed = 4.5f;
        r.cls = static_cast<int16_t>(P.cls); r.type = static_cast<int16_t>(P.type);
        if (boat)
        {
            // loading.sf's own boat traffic speeds. GridlockMap / GridlockRoadGroup (+0x28/+0x2C) stay 0:
            // the engine then files a (9,4)/(9,9) under BoatTraffic / LittleShips by itself (vt[43]/vt[44]) -
            // a road map here would put the boat on the roads.
            r.minSpeed = 2.25f; r.maxSpeed = 3.5f;
            // Who sits at the wheel: AddTrafficVehicleModel's DriverName does exactly this (0x140AA0B9C:
            // +0x08 = 0x14075BB10(tm, name), +0x10 = the name's hash) [P]. A driver is what makes the boat
            // "manned" for the arrest (the visible cause); whether the TM streams him in is [U] - the 1 s
            // log says how many chasing boats are manned.
            if (cfg.wanted_boat_driver[0])
            {
                driver = reinterpret_cast<TmDriverFn>(g_base + FN_TM_DRIVER)(w.tm, cfg.wanted_boat_driver);
                if (driver >= 0) { r.driver = driver; r.driverHash = H(cfg.wanted_boat_driver); }
                else Log("    wanted: boat_driver \"%s\" is no character this game knows - the boat keeps the game's driver\n",
                         cfg.wanted_boat_driver);
            }
        }
    }
    const unsigned before = r.maxInstances;
    r.maxInstances = static_cast<uint16_t>(maxInst);
    r.flags = static_cast<uint16_t>((r.flags | 0x2000 | 0x4000) & ~0x1000);   // no road rules, siren, never parked
    g_addModel(w.tm, &r, true, false, true);
    P.added = r;
    Target t;
    g_tgtCtor(&t);
    g_tgtSetChar(&t, tgt + 0x10, 0, speed);
    *reinterpret_cast<float*>(t.b + 0x40) = radius;
    int had, hadOn, has, hasOn;
    CountRecords(w, P.cls, P.type, had, hadOn);
    g_addPursuit(w.pm, &t, P.cls, P.type, false, true);
    g_tgtDtor(&t);
    CountRecords(w, P.cls, P.type, has, hasOn);
    // A full list takes nothing and says nothing: check, and undo the pool entry, so a model we
    // couldn't hand a record to isn't weighted in to idle about.
    if (has <= had && hasOn <= hadOn)
    {
        int n, cap;
        RecordsUsed(w, n, cap);
        TrafficModelRec back = P.added;
        back.maxInstances = 0;
        g_addModel(w.tm, P.hadEntry ? &P.saved : &back, true, false, true);
        Log(">>> wanted: %s - the pursuit list is full (%d/%d records) - not added\n", name, n, cap);
        return false;
    }
    g_zoneWeight(w.tm, &Pursuers_Zone(), P.model, 0.8f);
    P.on = true;
    P.plan = plan;
    Log(">>> wanted: %s (model %d, class %d type %d) pursues %s - %d at a time (pool had %s%u), radius %.0f\n",
        name, P.model, P.cls, P.type, Pn(plan.owner), maxInst, P.hadEntry ? "" : "no entry, ", before, radius);
    if (boat)
    {
        uint8_t* s = *reinterpret_cast<uint8_t**>(g_base + RVA_POPMGRS);
        uint8_t* e = reinterpret_cast<uint8_t*>(FindPoolEntry(w, P.model));
        Log("    wanted: boat %s - BoatTraffic map %016llX:%04X, pool entry max %u, live %u/%u, driver %d (%s)\n", name,
            PtrOk(s) ? static_cast<unsigned long long>(At<uint64_t>(s, 0xF0)) : 0ull, PtrOk(s) ? At<uint16_t>(s, 0xF8) : 0u,
            e ? At<uint16_t>(e, 0x26) : 0u, e ? At<uint16_t>(e, 0x38) : 0u, e ? At<uint16_t>(e, 0x3A) : 0u,
            P.added.driver, P.hadEntry ? "the pool's" : driver >= 0 ? cfg.wanted_boat_driver : "the game's");
    }
    return true;
}

static void Stop(const World& w, int u)
{
    Pursuit& P = g_p[u];
    if (!P.on) return;
    g_delPursuit(w.pm, P.cls, P.type, true, false);          // release them back to traffic
    g_zoneRemove(w.tm, &Pursuers_Zone(), P.model);
    // The pool entry as it was. One we created (no entry before - the police boats are in no pool)
    // stays at MaxInstances 0, which weighs it out of the spawn pick (0x14075E69D) [P]: no new ones,
    // and the released boats go back to boat traffic (ReleasePursuer 0x14017FA30) and get culled.
    if (P.hadEntry) g_addModel(w.tm, &P.saved, true, false, true);
    else { TrafficModelRec back = P.added; back.maxInstances = 0; g_addModel(w.tm, &back, true, false, true); }
    P.on = false;
}

static void StopAll(const World& w) { for (int u = 0; u < kNumUnits; ++u) Stop(w, u); }

static bool AnyOn() { for (const Pursuit& p : g_p) if (p.on) return true; return false; }

static float RadiusOf(int u, int level, const Config& cfg)
{
    const float r = cfg.wanted_radius + 10.0f * static_cast<float>(level - 1);
    return IsBoatUnit(u) ? std::fmax(r, cfg.wanted_boat_radius) : r;     // boat spawns sit ~4x further out
}

// Which unit chases whom, at which level, how many (wanted_model.h SplitUnits): one player wanted =
// every unit theirs, both = shared out. boatsUp: each player's "on the water, boats on, boat map in".
static void MakePlan(wanted::UnitPlan out[kNumUnits], const Config& cfg)
{
    const int lv[2] = { g_pl[0].st.level, RawPlayer(1) ? g_pl[1].st.level : 0 };
    const bool up[2] = { g_pl[0].boatsUp, g_pl[1].boatsUp };
    wanted::SplitUnits(lv, g_sus, up, cfg.wanted_boat_minnow != 0, g_boat2Bad, out);
}

// Make the pursuit records match the plan. A unit whose owner, level or count changed - or whose
// owner's level was just set (redo: the old Apply, which re-made everything) - is stopped; then
// what should run and doesn't is started, so a record one unit gives up is free before another takes
// it. Units that stay keep their records. If the second boat can't start, the bikes keep theirs.
// One player: a level change re-makes every unit (they all carry the level), the water or a boat key
// changing only moves the boats - exactly what Apply and Reconcile did.
static void ApplyPlan(const World& w, const bool redoIn[2], const Config& cfg)
{
    bool redo[2] = { redoIn[0], redoIn[1] };
    for (int pass = 0; pass < 2; ++pass)
    {
        const bool bad = g_boat2Bad;
        MakePlan(g_plan, cfg);
        for (int u = 0; u < kNumUnits; ++u)
        {
            Pursuit& P = g_p[u];
            if (!P.on) continue;
            const int was = P.plan.owner & 1;
            if (wanted::SamePlan(P.plan, g_plan[u]) && !redo[was]) continue;
            Stop(w, u);
            if (!redo[was])                                      // (a level change says it itself)
                Log(">>> wanted: %s stands down (%s%s)\n", kUnits[u].name, Pn(was),
                    g_plan[u].owner >= 0 && g_plan[u].owner != was ? ": it goes after the other player" : "");
        }
        redo[0] = redo[1] = false;
        for (int u = 0; u < kNumUnits; ++u)
        {
            const wanted::UnitPlan& d = g_plan[u];
            if (g_p[u].on || d.owner < 0) continue;
            if (!Start(w, u, d, RadiusOf(u, d.level, cfg), cfg.wanted_speed, cfg) && kUnits[u].kind == kBoat2)
            {
                g_boat2Bad = true;
                Log("    wanted: no second boat in this world - the motorbikes keep their record\n");
            }
        }
        if (bad == g_boat2Bad) break;                        // nothing changed: done
    }
}

// ---------------------------------------------------------------------------------------------
//  The police engine. Pursuers are ordinary traffic on the stock drivetrain: its motor target is
//  capped at the model's Max Velocity (7-11 u/s for these, Notes\20) and the stock linear drag
//  (Notes\23, #2) holds them there, while lcudrive's cars do 15-20 - in the first chase (2026-10-24)
//  they were very easy to get away from. While a police vehicle is CHASING you (one of our models, with
//  MakePursuer's FC7 bit 0x20 set) it gets what the motorbike gets (bike.h): an exact drive force
//  through each driven tyre's joint, and quadratic aero in place of the stock drag, the two equal
//  at `police_top_speed`. Its tyres hold `police_grip` x the stock cornering and braking force.
//  Far behind you it has up to `police_catchup` more; on your bumper, `police_ram` x the power.
//  The chase AI still steers, brakes and decides the throttle.
// ---------------------------------------------------------------------------------------------
struct Cop { void* veh; float vTop, power; };       // this frame's engine for one chasing vehicle
static const int     kMaxCops = 24;
static Cop           g_cop[kMaxCops];
static volatile LONG g_nCop = 0;
static float         g_copFastest = 0.0f;                    // for the log (nearest / your speed: Per)

// The police cars and motorbikes chasing the PRIMARY this tick, for the officers on foot
// (wanted_officers.h): their crowd drivers can get out ([wanted] officers_bikes = 0 keeps the riders on).
struct Chaser { void* veh; float d; bool bike; };
static const int kMaxChasers = 24;
static Chaser    g_chase[kMaxChasers];
static int       g_nChase = 0;

static const Cop* FindCop(const void* veh)
{
    const int n = g_nCop;
    for (int i = 0; i < n; ++i) if (g_cop[i].veh == veh) return &g_cop[i];
    return nullptr;
}

static void* BodyOf(void* veh)
{
    void* inst = At<void*>(veh, eng::VEH_INSTANCE);
    void* body = PtrOk(inst) ? At<void*>(inst, eng::INST_BODY) : nullptr;
    return PtrOk(body) ? body : nullptr;
}

static float SpeedOf(void* body)
{
    float v2 = 0.0f;
    for (int k = 0; k < 3; ++k) { const float v = At<float>(body, eng::BODY_LINVEL + 4 * k); v2 += v * v; }
    return std::sqrt(v2);
}

// PM+0x568: 0x80 traffic slots {cell*, ctl*}; alive while ctl[1] != 0; *cell = ApiVehicle,
// cell+0x18 / +0x1C = class / type (DelPursuit's walk, 0x140705E10..0x140705E41)
// Where the player is: their vehicle's body, or on foot the character (ch+0xA8, Notes\29).
static bool YouPos(float out[3])
{
    void* you = PlayerVehicle();
    if (void* body = you ? BodyOf(you) : nullptr)
    {
        for (int k = 0; k < 3; ++k) out[k] = At<float>(body, eng::BODY_POS + 4 * k);
        return IsFin(out[0]) && IsFin(out[1]) && IsFin(out[2]);
    }
    uint8_t* ch = SusChar();
    if (!PtrOk(ch)) return false;
    for (int k = 0; k < 3; ++k) out[k] = At<float>(ch, 0xA8 + 4 * k);
    return IsFin(out[0]) && IsFin(out[1]) && IsFin(out[2]);
}

static float DistTo(void* body, const float p[3])
{
    float d2 = 0.0f;
    for (int k = 0; k < 3; ++k) { const float dk = At<float>(body, eng::BODY_POS + 4 * k) - p[k]; d2 += dk * dk; }
    const float d = std::sqrt(d2);
    return IsFin(d) ? d : 1e9f;
}

// The chasing police boats this tick (RefreshCops): per player (Per) how many, how many with an
// officer aboard, and the nearest manned one (the water arrest). They get no CopWheel / CopPost (a
// boat has no wheel list) and no officers on foot (nobody steps out onto the water): RefreshCops'
// car / bike vtable gates already keep them out of g_cop and g_chase.
// Every chasing vehicle belongs to the player its unit's record is aimed at (g_p[u].plan.owner): the
// distances, the catch-up and the ram are measured to THAT player.
static void RefreshCops(const World& w, const Config& cfg)
{
    int n = 0;
    g_nChase = 0;
    float fastest = 0.0f;
    const int pri = g_sus & 1;
    void* you[2] = { nullptr, nullptr };
    void* youBody[2] = { nullptr, nullptr };
    float yp[2][3] = {};
    bool haveYou[2] = { false, false };
    for (int i = 0; i < 2; ++i)
    {
        Per& me = g_pl[i];
        me.copNearest = 1e9f; me.boatNear = 1e9f; me.nBoat = me.nBoatManned = 0; me.youSpeed = 0.0f;
        if (!PtrOk(RawPlayer(i))) continue;
        AsPlayer as(i);
        you[i] = PlayerVehicle();
        youBody[i] = you[i] ? BodyOf(you[i]) : nullptr;
        me.youSpeed = youBody[i] ? SpeedOf(youBody[i]) : 0.0f;
        haveYou[i] = YouPos(yp[i]);
    }
    if (AnyOn())
    {
        uint8_t* slots = reinterpret_cast<uint8_t*>(w.pm) + 0x568;
        for (int i = 0; i < 0x80; ++i)
        {
            uint8_t* cell = At<uint8_t*>(slots, i * 16);
            uint8_t* ctl = At<uint8_t*>(slots, i * 16 + 8);
            if (!PtrOk(cell) || !PtrOk(ctl) || At<int32_t>(ctl, 4) == 0) continue;
            void* veh = At<void*>(cell, 0);
            if (!PtrOk(veh) || veh == you[0] || veh == you[1]) continue;
            const int u = UnitOf(At<int32_t>(cell, 0x18), At<int32_t>(cell, 0x1C));
            if (u < 0) continue;
            if (!(At<uint8_t>(veh, eng::VEH_FLAGS7) & 0x20)) continue;      // not chasing (yet)
            void* body = BodyOf(veh);
            if (!body) continue;
            const int own = g_p[u].plan.owner & 1;                          // whom it chases
            Per& me = g_pl[own];
            const float d = haveYou[own] ? DistTo(body, yp[own]) : 1e9f;
            uint8_t* ud = VehicleUserData(veh);                             // an empty one (its officer out) can't
            const bool manned = KrawlieDriverRide(ud) || UserDataDriver(ud); // arrest you, nor be held again
            fastest = std::fmax(fastest, SpeedOf(body));
            if (At<int32_t>(veh, eng::VEH_CLASS) == kClassBoat)              // a boat arrests only through
            {                                                               // boatNear, which Tick uses only
                ++me.nBoat;                                                 // while you're on the water (a boat
                if (manned) { ++me.nBoatManned; me.boatNear = std::fmin(me.boatNear, d); }   // at a quay is not at
                continue;                                                   // your car's door)
            }
            if (manned && d < me.copNearest) me.copNearest = d;             // for the gadgets and the arrest
            const uintptr_t vt = At<uintptr_t>(veh, 0);                     // the engine: cars and bikes only,
            const bool isCar = vt == g_base + eng::RVA_VT_CAR, isBike = vt == g_base + eng::RVA_VT_BIKE;
            if (own == pri && manned && (isCar || (isBike && cfg.wanted_officers_bikes)) && g_nChase < kMaxChasers)
                g_chase[g_nChase++] = Chaser{ veh, d, isBike };
            if (!cfg.wanted_police_boost || n >= kMaxCops ||                // their thunks finish it
                (vt != g_base + eng::RVA_VT_CAR && vt != g_base + eng::RVA_VT_BIKE)) continue;
            float b = 1.0f;
            if (d < 1e8f)
            {
                b += cfg.wanted_police_catchup * Clampf((d - 25.0f) / 35.0f, 0.0f, 1.0f);
                if (youBody[own]) b += (cfg.wanted_police_ram - 1.0f) * Clampf((15.0f - d) / 7.0f, 0.0f, 1.0f);
            }
            g_cop[n].veh = veh;
            g_cop[n].vTop = cfg.wanted_police_speed * b;
            g_cop[n].power = cfg.wanted_police_power * b;
            ++n;
        }
    }
    g_nCop = n;
    g_copFastest = fastest;
}

// One cop's engine: power P and aero c, with c V^3 = P at its top speed.
static bool CopEngine(void* body, const Cop& cop, float& m, float& P, float& c)
{
    m = At<float>(body, eng::BODY_MASS);
    if (!IsFin(m) || m < 10.0f) return false;
    P = m * 55.0f * cop.power;                                       // the motorbike's: 0 -> 15 u/s in ~3 s
    c = P / (cop.vTop * cop.vTop * cop.vTop);
    return true;
}

// In the wheel-joint hook, right after the game's own setup of a chasing police vehicle's tyre.
static void CopWheel(void* wheel, void* veh, const Cop& cop, const Config& cfg)
{
    if (At<uint8_t>(wheel, eng::WHL_AIRBORNE) != 0) return;
    void* joint = At<void*>(wheel, eng::WHL_JOINT);
    uint8_t* d = PtrOk(joint) ? At<uint8_t*>(joint, eng::JNT_PARAMS) : nullptr;
    void* ctrl = At<void*>(veh, eng::VEH_CTRL);
    void* inst = At<void*>(veh, eng::VEH_INSTANCE);
    void* body = BodyOf(veh);
    if (!PtrOk(d) || !PtrOk(ctrl) || !body) return;

    // grip: the stock cornering cap, and the stock brake cap while braking, x police_grip
    const float g = cfg.wanted_police_grip;
    const float latHi = At<float>(d, eng::JP_LAT_HI) * g, latLo = At<float>(d, eng::JP_LAT_LO) * g;
    At<float>(d, eng::JP_LAT_HI) = latHi;
    At<float>(d, eng::JP_LAT_LO) = latLo;
    const float thr = At<float>(ctrl, eng::CTRL_THROTTLE);
    const float brk = std::fmax(At<float>(ctrl, eng::CTRL_BRAKE), At<float>(ctrl, eng::CTRL_HANDBRAKE));
    if (brk > 0.05f)
    {
        At<float>(d, eng::JP_LONG_HI) *= g;
        At<float>(d, eng::JP_LONG_LO) *= g;
        return;
    }

    // drive: driven tyres, on the throttle, rolling forward (reversing stays the game's)
    void* wdef = At<void*>(wheel, eng::WHL_DEF);
    if (!(thr > 0.05f) || At<float>(ctrl, eng::CTRL_REVERSE) > 0.05f) return;
    if (!PtrOk(wdef) || !At<uint8_t>(wdef, eng::WDEF_DRIVE)) return;
    float fwd[3], right[3], v = 0.0f;
    for (int k = 0; k < 3; ++k)
    {
        fwd[k]   = -At<float>(inst, eng::INST_ROW2 + 4 * k);
        right[k] =  At<float>(inst, eng::INST_ROW0 + 4 * k);
        v += At<float>(body, eng::BODY_LINVEL + 4 * k) * fwd[k];
    }
    if (v < -0.5f) return;
    float m, P, c;
    if (!CopEngine(body, cop, m, P, c)) return;
    int nDriven = 0, guard = 0;
    for (void* w = At<void*>(veh, eng::VEH_WHEELS); PtrOk(w) && guard < 16; w = At<void*>(w, eng::WHL_NEXT), ++guard)
    {
        void* wd2 = At<void*>(w, eng::WHL_DEF);
        if (PtrOk(wd2) && At<uint8_t>(wd2, eng::WDEF_DRIVE)) ++nDriven;
    }
    if (nDriven <= 0) return;
    float F = std::fmin(m * 7.0f * cop.power, P / std::fmax(v, 1.0f)) * Clampf(thr, 0.0f, 1.0f) / static_cast<float>(nDriven);
    const float N = At<float>(wheel, eng::WHL_SPRINGF);                // traction: what the tyre can carry
    if (IsFin(N) && N > 0.0f) F = std::fmin(F, 1.2f * g * N);

    // the axis the game set: forward turned by the wheel's steer, laid on the ground (model.h)
    const float st = At<float>(wheel, eng::WHL_STEERDEG) * 0.0174533f;
    float n[3], axis[4] = {};
    for (int k = 0; k < 3; ++k)
    {
        n[k] = At<float>(wheel, eng::WHL_NORMAL + 4 * k);
        axis[k] = std::cos(st) * fwd[k] - std::sin(st) * right[k];
    }
    const float an = axis[0] * n[0] + axis[1] * n[1] + axis[2] * n[2];
    for (int k = 0; k < 3; ++k) axis[k] -= an * n[k];
    const float len = std::sqrt(axis[0] * axis[0] + axis[1] * axis[1] + axis[2] * axis[2]);
    if (!(len > 1e-3f)) return;
    for (int k = 0; k < 3; ++k) axis[k] /= len;
    mdl::g_fn.jointLimits(joint, axis, latHi, F, latLo, -F);
}

// After a chasing police vehicle's Update: the aero that replaces the stock drag (skipped in the
// drag hook), and its skid bytes (wheel+0x1B0, Notes\36). The game marks a tyre when its spin and
// the ground speed differ, and the spin is still capped at the model's Max Velocity, so at the
// boosted speed every tyre would leave marks and squeal. Ours: the game's decision stands while
// braking, or where the tyre really slides sideways.
static void CopPost(void* veh, const Cop& cop, float skidLat)
{
    void* body = BodyOf(veh);
    if (!body) return;
    float v[3], av[3], cm[3], v2 = 0.0f;
    for (int k = 0; k < 3; ++k)
    {
        v[k]  = At<float>(body, eng::BODY_LINVEL + 4 * k);
        av[k] = At<float>(body, eng::BODY_ANGVEL + 4 * k);
        cm[k] = At<float>(body, eng::BODY_POS + 4 * k);
        v2 += v[k] * v[k];
    }
    const float sp = std::sqrt(v2), dt = At<float>(veh, eng::VEH_DT);
    float m, P, c;
    if (CopEngine(body, cop, m, P, c) && dt > 0.0f && dt <= 0.1f && sp > 0.1f)
    {
        const float dv = std::fmin(c * v2 / m * dt, 0.5f * sp);
        for (int k = 0; k < 3; ++k) At<float>(body, eng::BODY_LINVEL + 4 * k) = v[k] - v[k] / sp * dv;
    }

    void* ctrl = At<void*>(veh, eng::VEH_CTRL);
    if (PtrOk(ctrl) && std::fmax(At<float>(ctrl, eng::CTRL_BRAKE), At<float>(ctrl, eng::CTRL_HANDBRAKE)) > 0.05f) return;
    int guard = 0;
    for (void* w = At<void*>(veh, eng::VEH_WHEELS); PtrOk(w) && guard < 16; w = At<void*>(w, eng::WHL_NEXT), ++guard)
    {
        if (!At<uint8_t>(w, 0x1B0)) continue;                          // no mark: stands
        if (At<uint8_t>(w, eng::WHL_AIRBORNE) != 0) { At<uint8_t>(w, 0x1B0) = 0; continue; }
        float r[3], rgt[3];                                             // the wheel's right row and hub
        for (int k = 0; k < 3; ++k) { rgt[k] = At<float>(w, 0x10 + 4 * k); r[k] = At<float>(w, 0x40 + 4 * k) - cm[k]; }
        const float wx = av[1] * r[2] - av[2] * r[1], wz = av[0] * r[1] - av[1] * r[0];
        const float lat = (v[0] + wx) * rgt[0] + (v[2] + wz) * rgt[2];  // xz only, like the game's test
        if (IsFin(lat) && std::fabs(lat) <= skidLat) At<uint8_t>(w, 0x1B0) = 0;
    }
}

// ---------------------------------------------------------------------------------------------
//  Crimes (Notes\32 §6, Notes\33 §6), fed on the game thread by lcudrive.cpp's hooks:
//    the player-vehicle events fired through 0x1402E4330 (call sites retargeted): a pedestrian
//    run over, a traffic / AI vehicle hit (a police one = a ram), the vehicle wrecked, a car taken
//    (a crime unless you wear the police uniform; a police car counts more);
//    GameObjects::OnCharacterDamage (vtable slot 60): the player hitting a character - a cop, or
//    anyone who isn't a crook (fighting crooks is Chase's job);
//    the krawlie hit query (call 0x1400314CA): the player punching a crowd pedestrian;
//    the ray gun (2026-11-14; "The ray gun" below): its bolt's own krawlie hit query (a pedestrian
//    frozen), the bolt smashing a street prop or a LEGO object, the bolt hitting a vehicle that is
//    then destroyed, and - through OnCharacterDamage, which carries the bolt - a character shot.
// ---------------------------------------------------------------------------------------------
static const uintptr_t FN_EVENT_FIRE = 0x2E4330, FN_KRAWLIE_HIT = 0x69A760;
static const uintptr_t EV_HIT_KRAWLIE = 0x17F8688, EV_HIT_TRAFFIC = 0x17F86B0, EV_HIT_AIVEH = 0x17F85E8,
                       EV_DESTROYED = 0x17F8700, EV_JACKED = 0x1C74EB8;
static const uintptr_t RVA_SLOT_ONDAMAGE = 0xC273D8, FN_ONDAMAGE = 0x48C20;   // GameObjects vtable slot 60
// the ray gun's sites (lcudrive.cpp installs them; each is checked against these before it is touched)
static const uintptr_t SITE_SHOT_KRAWLIE_A = 0x874A15, SITE_SHOT_KRAWLIE_B = 0x874A7D;   // E8 -> FN_KRAWLIE_HIT, in the bolt's move (0x140873F90)
static const uintptr_t SITE_SHOT_PROP = 0x1B1178, FN_PROP_BOLTHIT = 0x0B91A0;            // E8 in cBoltCityType's hit (0x1401B1110)
static const uintptr_t RVA_SLOT_BLOWUP_BOLT = 0xC25C30, FN_BLOWUP_BOLT = 0x21D80;        // LegoCityBlowups vtable slot 43
static const uintptr_t RVA_SLOT_BOLT_VEHICLE = 0xC204C8, FN_BOLT_VEHICLE = 0x871B10;     // LegoCityBolts vtable slot 68
static const uintptr_t RVA_BOLTS = 0x17EC7B0;                                            // LegoCityBolts*

enum Crime { cRunOver, cRamPolice, cWreck, cStealCar, cHitCop, cHitPerson, cPunch, cShootPerson, cShootProp, cStealPolice, cCount };
static const char* const kCrimeName[cCount] = {
    "ran over a pedestrian", "rammed a police car", "wrecked the vehicle", "stole a car",
    "hit a police officer", "hit someone", "punched a pedestrian",
    "shot a pedestrian with the ray gun", "smashed something with the ray gun", "stole a police car",
};
static float    g_crimeW[cCount] = { 0.6f, 0.8f, 0.5f, 0.5f, 1.0f, 0.3f, 0.3f, 0.3f, 0.15f, 0.8f };   // from the ini (Tick)
static_assert(cCount == kNumCrimes, "Per::crimeLast");
static bool     g_crimesOn = false;       // wanted mode on, no story mission (Tick decides); each player: Per::counts
static bool     g_jackLogged = false;

static uint8_t* P1() { return SusChar(); }                // the suspect (player 1 unless player 2 is)

static const char* DefName(uint8_t* c)
{
    uint8_t* d = PtrOk(c) ? At<uint8_t*>(c, 0x650) : nullptr;
    const char* n = PtrOk(d) ? At<const char*>(d, 0x30) : nullptr;
    return PtrOk(n) ? n : nullptr;
}

static bool ContainsI(const char* s, const char* sub)
{
    if (!s) return false;
    const size_t n = strlen(sub);
    for (int i = 0; s[i] && i < 96; ++i) if (_strnicmp(s + i, sub, n) == 0) return true;
    return false;
}

// Taking a car is police work in the police uniform (COP_CLASSIC, Cop, Police_*), theft in anything else - plain
// clothes (Civilian_ChaseMcCain) included. (2026-11-25: before, only the robber disguise counted.)
static bool InPoliceUniform(uint8_t* ch)
{
    const char* dn = DefName(ch);
    return dn && (ContainsI(dn, "cop") || ContainsI(dn, "police"));
}
static const uint32_t kStealCoolMs = 5000;   // one theft, one crime

static void AddCrime(int c, uint32_t coolMs)
{
    Per& me = Me();                                         // (the hooks run AsPlayer(whoever did it):
    if (!g_crimesOn || !me.counts) return;                  //  only THEIR heat rises)
    const uint32_t now = GetTickCount();
    if (now - me.crimeLast[c] < coolMs) return;
    me.crimeLast[c] = now;
    me.crimeHeat += g_crimeW[c];
    Log(">>> wanted: %s crime - %s (+%.2f heat)\n", Pn(), kCrimeName[c], g_crimeW[c]);
}

static bool IsPoliceModel(void* veh)
{
    char name[64];
    void* def = At<void*>(veh, eng::VEH_DEF);
    if (!PtrOk(def) || !ReadModelName(def, name, sizeof(name))) return false;
    return _strnicmp(name, "POLICE_", 7) == 0 || _strnicmp(name, "2016_POLICE_", 12) == 0;
}

// The nearest police vehicle (any, chasing or not) to a point, within `range`; 1e9 = none.
static float NearestPolice(const float p[3], float range)
{
    World w = GetWorld();
    if (!w.ok) return 1e9f;
    void* you = PlayerVehicle();
    uint8_t* slots = reinterpret_cast<uint8_t*>(w.pm) + 0x568;
    float best = 1e9f;
    for (int i = 0; i < 0x80; ++i)
    {
        uint8_t* cell = At<uint8_t*>(slots, i * 16);
        uint8_t* ctl = At<uint8_t*>(slots, i * 16 + 8);
        if (!PtrOk(cell) || !PtrOk(ctl) || At<int32_t>(ctl, 4) == 0) continue;
        void* veh = At<void*>(cell, 0);
        if (!PtrOk(veh) || veh == you) continue;
        void* body = BodyOf(veh);
        if (!body) continue;
        const float d = DistTo(body, p);
        if (d < range && d < best && IsPoliceModel(veh)) best = d;
    }
    return best;
}

// The vehicle events carry the VEHICLE'S USER DATA at payload+0x10 (each site loads it with
// 0x1404A8970, minus 0x10), not a character as Notes\32 §6 read it. The run-over and wreck sites
// fire for any vehicle with a driver - in the 2026-10-25 test the chasing police running people
// over counted as yours, 8 times in a minute while you stood on foot - so each one counts only for
// your vehicle: its user data, or user data whose driver is you. (A character payload still counts.)
static bool YourVehicleEvent(uint8_t* ud)
{
    uint8_t* p1 = P1();
    if (!PtrOk(ud) || !PtrOk(p1)) return false;
    if (ud == p1 || ud == p1 + 0x10) return true;
    void* you = PlayerVehicle();
    if (you && VehicleUserData(you) == ud) return true;
    return UserDataDriver(ud) == p1;
}

// Which player's vehicle event this is: 0, 1, or -1 (nobody's).
static int WhoseVehicleEvent(uint8_t* ud)
{
    for (int i = 0; i < 2; ++i)
    {
        if (!PtrOk(RawPlayer(i))) continue;
        AsPlayer as(i);
        if (YourVehicleEvent(ud)) return i;
    }
    return -1;
}

// Which player stands within `range` of this car (the car-jack: you're not in it yet), or -1.
static int PlayerNearCar(uint8_t* ud, float range)
{
    void* jv = PtrOk(ud) ? At<void*>(ud, 0x38) : nullptr;                   // ud+0x38 = the vehicle
    void* jb = PtrOk(jv) ? BodyOf(jv) : nullptr;
    if (!jb) return -1;
    for (int i = 0; i < 2; ++i)
    {
        if (!PtrOk(RawPlayer(i))) continue;
        AsPlayer as(i);
        float yp[3];
        if (YouPos(yp) && DistTo(jb, yp) < range) return i;
    }
    return -1;
}

static bool g_otherLogged[cCount] = {};
static int  g_jackSeen = 0;
static bool g_pulling = false;          // our own car-jack is running (wanted_officers.h): not your crime

static void NotYours(int c, uint8_t* ud)
{
    if (g_otherLogged[c]) return;
    g_otherLogged[c] = true;
    uint8_t* drv = UserDataDriver(ud);
    Log("    wanted: a \"%s\" event from someone else's vehicle (driver %s) - not your crime (shown once)\n",
        kCrimeName[c], drv ? CharDefName(drv) : "unknown");
}

// Your crashes and punches, for the ambient life (life.h: onlookers, a scattering crowd), whether or
// not crimes count right now. Kind: 1 you ran someone over, 2 a crash, 3 a wreck.
static volatile uint32_t g_incidentMs = 0, g_punchMs = 0;
static volatile LONG     g_incidentKind = 0;

// ---------------------------------------------------------------------------------------------
//  The ray gun (2026-11-14; static analysis, unplayed). A shot is a Bolt: bolt+0x20 = its owner
//  (owner->vt[72]() = the shooter's character, the game's own lookup at every site below),
//  bolt+0x40 = its BoltType, type+8 = the type's row in the bolt system's table
//  (LegoCityBolts +0xE1 count, +0xE8 16-byte rows, row+0 = the name's hash; 0x140870CB0).
//  Where a shot lands:
//    * a crowd pedestrian: the bolt's move (0x140873F90) runs the krawlie hit query with a HitBy
//      hook {+8 the bolt, +0x10 kind 0}; the city krawlie's handler (0x1400AD2D0) freezes him by
//      bolt type and alarms the crowd around him itself. al = somebody took it.
//    * a character: Damage (0x14079B010) -> GameObjects slot 60 with the bolt as its 4th argument.
//      A bolt never reaches a character on the shooter's own side (0x14086DAAE), so in practice
//      that is the officers on foot and the crooks.
//    * a street prop (LegoCityGadgetProps): cBoltCityType's hit (0x1401B1110) calls the prop's
//      bolt hit (0x1400B91A0), which smashes it when the type has DestroyProps (0x1400B9720 starts
//      the respawn timer prop+0x318). Punches and cars smash props through other callers.
//    * a LEGO object (a "blowup"): 0x14087F5E0 asks LegoCityBlowups slot 43 (0x140021D80, the bolt
//      is its 7th argument); al = the bolt broke or damaged it, 0 = it bounced off.
//    * a vehicle: LegoBolts slot 68 (0x140871B10) hands the bolt to the vehicle's user data. What
//      that does to it is not traced; the hit is only remembered, and if THAT vehicle's destroyed
//      event follows within 6 s it is the shooter's wreck.
//  Only the ray gun's own bolt types count (wanted_model.h IsRayGunBolt): the police grapple, the
//  colour gun, the chicken gun and the fire hoses are not crimes.
// ---------------------------------------------------------------------------------------------
static uint32_t BoltTypeHash(uint8_t* bolt)
{
    uint8_t* type = PtrOk(bolt) ? At<uint8_t*>(bolt, 0x40) : nullptr;
    uint8_t* mgr = *reinterpret_cast<uint8_t**>(g_base + RVA_BOLTS);
    if (!PtrOk(type) || !PtrOk(mgr)) return 0;
    const int id = At<uint8_t>(type, 8), n = At<int8_t>(mgr, 0xE1);
    uint8_t* rows = At<uint8_t*>(mgr, 0xE8);
    if (id >= n || !PtrOk(rows)) return 0;
    return At<uint32_t>(rows, static_cast<uintptr_t>(id) * 16);
}

// Which player fired this bolt (0, 1), or -1: nobody we know, or not the ray gun.
static int RayShooter(uint8_t* bolt)
{
    uint8_t* own = PtrOk(bolt) ? At<uint8_t*>(bolt, 0x20) : nullptr;
    if (!PtrOk(own)) return -1;
    uint8_t* ch = nullptr;
    const uintptr_t vt = At<uintptr_t>(own, 0);
    if (vt > g_base && vt < g_base + eng::kImageSize - 0x248)
    {
        const uintptr_t fn = *reinterpret_cast<const uintptr_t*>(vt + 0x240);
        typedef uint8_t* (__fastcall* OwnerCharFn)(void*);
        if (fn > g_base && fn < g_base + eng::kImageSize) ch = reinterpret_cast<OwnerCharFn>(fn)(own);
    }
    int by = -1;
    for (int i = 0; i < 2; ++i)
    {
        uint8_t* p = RawPlayer(i);
        if (PtrOk(p) && (ch == p || own == p || own == p + 0x10)) by = i;
    }
    if (by < 0) return -1;
    const uint32_t h = BoltTypeHash(bolt);
    if (wanted::IsRayGunBolt(h)) return by;
    static uint32_t s_other[6] = {};                       // what else the players fire: said once per type
    const uint32_t key = h ? h : 1u;                       // (0 = the type couldn't be read)
    for (uint32_t& o : s_other)
    {
        if (o == key) break;
        if (o) continue;
        o = key;
        Log("    wanted: a player's bolt of type 0x%08X isn't the ray gun - shots with it are no crime (shown once per type)\n", h);
        break;
    }
    return -1;
}

// The bolt in a krawlie HitBy hook (kind 0 = a bolt; a melee swing carries its attacker there).
static int CrowdShotBy(uint8_t* hook)
{
    if (!PtrOk(hook) || At<int32_t>(hook, 0x10) != 0) return -1;
    return RayShooter(At<uint8_t*>(hook, 8));
}

static void OnCrowdShot(int by)                            // a pedestrian took the shot
{
    AsPlayer as(by & 1);
    g_punchMs = GetTickCount() | 1u;                       // the crowd near you scatters too (life.h)
    if (g_crimesOn) AddCrime(cShootPerson, 800);
}

static float PropTimer(uint8_t* prop) { return PtrOk(prop) ? At<float>(prop, 0x318) : 1.0f; }

static void OnPropShot(int by)                             // a prop or a LEGO object broke under the shot
{
    AsPlayer as(by & 1);
    if (g_crimesOn) AddCrime(cShootProp, 1500);
}

static uint8_t* g_shotVehUd = nullptr;                     // the last vehicle a ray gun hit: its user data,
static uint32_t g_shotVehMs = 0;                           // when (0 = none),
static int      g_shotVehBy = -1;                          // and whose shot

static void NoteVehicleShot(uint8_t* bolt, void* veh)
{
    const int by = RayShooter(bolt);
    uint8_t* ud = by >= 0 ? VehicleUserData(veh) : nullptr;
    if (!ud) return;
    if (ud != g_shotVehUd || !wanted::ShotThenWrecked(g_shotVehMs, GetTickCount(), 6000))
    {
        static int s_said = 0;
        if (s_said < 3) { ++s_said; Log("    wanted: P%d's ray gun hits a vehicle (user data %p) - a crime if it's destroyed within 6 s\n", by + 1, ud); }
    }
    g_shotVehUd = ud; g_shotVehMs = GetTickCount() | 1u; g_shotVehBy = by;
}

// Whose ray gun just hit the vehicle of this destroyed event, or -1.
static int ShotVehicleBy(uint8_t* ud)
{
    if (!PtrOk(ud) || ud != g_shotVehUd || !wanted::ShotThenWrecked(g_shotVehMs, GetTickCount(), 6000)) return -1;
    return g_shotVehBy;
}

// lcudrive.cpp's hooks call these (inside SEH).
static void OnVehicleEvent(void* ev, void* payload)
{
    const uintptr_t e = reinterpret_cast<uintptr_t>(ev) - g_base;
    uint8_t* ud = PtrOk(payload) ? At<uint8_t*>(payload, 0x10) : nullptr;
    // whose event: either player's. The rest of this hook then runs as that player.
    int who = WhoseVehicleEvent(ud);
    if (who < 0 && e == EV_JACKED && !g_pulling) who = PlayerNearCar(ud, 6.0f);
    bool shot = false;                                     // someone else's vehicle, destroyed under a ray gun
    if (who < 0 && e == EV_DESTROYED) { who = ShotVehicleBy(ud); shot = who >= 0; }
    AsPlayer as(who >= 0 ? who : g_sus);
    if ((e == EV_DESTROYED || e == EV_HIT_TRAFFIC || e == EV_HIT_AIVEH || e == EV_HIT_KRAWLIE) && YourVehicleEvent(ud))
    {
        g_incidentKind = e == EV_HIT_KRAWLIE ? 1 : e == EV_DESTROYED ? 3 : 2;
        g_incidentMs = GetTickCount() | 1u;
    }
    if (!g_crimesOn || !Me().counts) return;
    if (e == EV_JACKED)
    {
        // The car-jack itself (a krawlie driver's riding data vt[68], 0x14069CFC0; Notes\38 §1.2):
        // payload+0x10 is the jacked car's user data, and you're not in it yet. Yours = a car right
        // next to you, and not one of the officers' own pull-outs. The first few say what they hold.
        if (g_pulling) return;
        uint8_t* p1 = P1();
        void* jv = PtrOk(ud) ? At<void*>(ud, 0x38) : nullptr;               // ud+0x38 = the vehicle
        void* jb = PtrOk(jv) ? BodyOf(jv) : nullptr;
        float yp[3];
        const bool mine = YourVehicleEvent(ud) || (jb && YouPos(yp) && DistTo(jb, yp) < 6.0f);
        if (g_jackSeen < 3)
        {
            ++g_jackSeen;
            void* you = PlayerVehicle();
            uint8_t* drv = UserDataDriver(ud);
            Log("    wanted: car-taken event %d: payload+0x10 %p (you %p, your vehicle's user data %p; its driver %s) "
                "-> %s; you're \"%s\"\n", g_jackSeen, ud, p1, you ? VehicleUserData(you) : nullptr,
                drv ? CharDefName(drv) : "none", mine ? "yours" : "not yours", CharDefName(p1));
        }
        if (!mine) return;
        const char* dn = DefName(p1);
        if (!g_jackLogged) { g_jackLogged = true; Log("    wanted: you took a car as \"%s\"\n", dn ? dn : "?"); }
        if (InPoliceUniform(p1)) return;
        AddCrime(jv && IsPoliceModel(jv) ? cStealPolice : cStealCar, kStealCoolMs);
        return;
    }
    int c;
    uint32_t cool;
    if (e == EV_HIT_KRAWLIE) { c = cRunOver; cool = 700; }
    else if (e == EV_HIT_TRAFFIC || e == EV_HIT_AIVEH) { c = cRamPolice; cool = 2500; }
    else if (e == EV_DESTROYED) { c = cWreck; cool = 5000; }
    else return;
    if (shot)
    {
        g_shotVehMs = 0;                                   // (one wreck per vehicle)
        Log("    wanted: the vehicle %s's ray gun hit is destroyed\n", Pn());
        AddCrime(cWreck, cool);
        return;
    }
    if (!YourVehicleEvent(ud)) { NotYours(c, ud); return; }
    if (c == cRamPolice)
    {
        float yp[3];
        if (!PlayerVehicle() || !YouPos(yp) || NearestPolice(yp, 6.5f) >= 6.5f) return;
        // a collision event has two sides: the police running into a slow or stopped car is not that
        // car's crime (2026-11-11 co-op: a coasting player 1 became the suspect this way and was busted
        // while player 2 drove off)
        if (g_recentSpeed[g_sus & 1] < 4.0f)
        {
            static bool s_said = false;
            if (!s_said) { s_said = true; Log("    wanted: the police ran into a car doing %.1f u/s - not a ramming (shown once)\n", g_recentSpeed[g_sus & 1]); }
            return;
        }
    }
    AddCrime(c, cool);
}

static int  OfficerIdOf(const uint8_t* ch);               // wanted_officers.h: the officers on foot
static void NoteOfficerHit(const uint8_t* ch);            // ...your blow on one of them
static bool FirstHitOn(const uint8_t* ch);
static uint32_t g_meleeLog = 0;
static int      g_meleeHits = 0;

static void OnCharacterHit(uint8_t* att, uint8_t* vic, uint8_t* bolt)
{
    if (!g_crimesOn) return;
    if (!PtrOk(vic)) return;
    // either player's blow counts; the rest runs as the one who threw it
    int by = -1;
    bool vicIsPlayer = false;
    for (int i = 0; i < 2; ++i)
    {
        uint8_t* p = RawPlayer(i);
        if (!PtrOk(p)) continue;
        if (att == p || att == p + 0x10) by = i;
        if (vic == p) vicIsPlayer = true;
    }
    // a shot: the bolt knows who fired it, and whether it is the ray gun (a crime of its own)
    const int shooter = PtrOk(bolt) && !vicIsPlayer ? RayShooter(bolt) : -1;
    if (by < 0) by = shooter;
    AsPlayer as(by >= 0 ? by : g_sus);
    if (!Me().counts) return;                                       // (being busted, or in the grace after)
    uint8_t* p1 = P1();
    if (!PtrOk(p1)) return;
    const bool byYou = by >= 0;
    if (vicIsPlayer)                                                // an officer's blow lands on you
    {
        const int id = byYou ? 0 : OfficerIdOf(att);
        const uint32_t now = GetTickCount();
        if (id && (++g_meleeHits == 1 || now - g_meleeLog > 5000))
        {
            g_meleeLog = now;
            Log("    wanted: officer #%d's blow lands (%d from the officers so far)\n", id, g_meleeHits);
        }
        return;
    }
    if (!byYou) return;
    if (OfficerIdOf(vic)) { NoteOfficerHit(vic); if (FirstHitOn(vic)) AddCrime(cHitCop, 0); return; }   // the crime: once each
    if (At<uint32_t>(vic, 0xD0) & 0x10001) return;                  // a baddie: fair game
    const bool cop = ContainsI(DefName(vic), "cop");
    AddCrime(cop ? cHitCop : shooter >= 0 ? cShootPerson : cHitPerson, cop ? 1000 : 800);
}

static void OnCrowdPunch(uint8_t* hook)
{
    if (!PtrOk(hook)) return;
    uint8_t* att = At<uint8_t*>(hook, 8);
    int by = -1;
    for (int i = 0; i < 2; ++i)
    {
        uint8_t* p = RawPlayer(i);
        if (PtrOk(p) && (att == p || att == p + 0x10)) by = i;
    }
    if (by < 0) return;
    AsPlayer as(by);
    g_punchMs = GetTickCount() | 1u;                                   // the crowd scatters (life.h)
    if (g_crimesOn) AddCrime(cPunch, 600);
}

// ---------------------------------------------------------------------------------------------
//  The officers' gadgets and the arrest (Notes\32 §4.6, §5.4, §7). NPC cops never fire in this
//  game, so a hit is ours to apply with the game's own states: the ray gun = the city Knockdown
//  context on the player for N s (its duration override cd+0x29C, the "stunned" pose 0xF9), the
//  grapple = LockInPlace in Grapple_TakeHit, then the same knockdown. While down, the get-up
//  buttons are held back. Busted = the cuffed pose, studs taken, the game's own "return to the
//  police station" (0x14003ACA0(_, 1, _), the pause menu's).
// ---------------------------------------------------------------------------------------------
static const uintptr_t CTX_KNOCKDOWN = 0xFF42C0, CTX_IDLENOGRAV = 0x100E204, CTX_DEFAULT = 0x116ABAC,
                       CTX_DEATH = 0x116A9F8;
static const uintptr_t FN_ISIN = 0x6D40, FN_SETSUB = 0x66FBC0, FN_TODEF = 0x668780, FN_FORCEDEF = 0x6709C0,
                       FN_CURH = 0x66A000, FN_KDSETUP = 0x139B40, FN_ANIMID = 0x814C50, FN_SUBPLAY = 0x82A0D0,
                       FN_DEAD = 0x66B700, FN_STATION = 0x3ACA0;
static const uintptr_t RVA_MASK_JUMP = 0x1174CB4, RVA_MASK_ATTACK = 0x1174CB8, RVA_MASK_SPECIAL = 0x1174CBC;
static const uintptr_t RVA_GAME = 0x1C7A830;                     // + 0x335F: "return to station" allowed

typedef bool(__fastcall* IsInFn)(uint8_t*, int);
typedef uint8_t*(__fastcall* SetSubFn)(uint8_t*, int, int);
typedef void(__fastcall* ChFn)(uint8_t*);
typedef uint8_t*(__fastcall* CurHFn)(uint8_t*);
typedef void(__fastcall* KdSetupFn)(uint8_t*, int, int, void*);
typedef int(__fastcall* AnimIdFn)(const char*);
typedef bool(__fastcall* SubPlayFn)(uint8_t*, uint16_t, bool);
typedef bool(__fastcall* DeadFn)(uint8_t*);
typedef void(__fastcall* StationFn)(void*, int, void*);

static int  Ctx(uintptr_t rva) { return *reinterpret_cast<const int*>(g_base + rva); }
static bool IsIn(uint8_t* c, uintptr_t ctx) { return reinterpret_cast<IsInFn>(g_base + FN_ISIN)(c, Ctx(ctx)); }

// Is the suspect on the water? 1 = in a boat (veh+0x340 == 3), 2 = swimming (the "Swimming" character
// context, registered at 0x140680C2A into 0x14116ACFC - jetpack.h's ID_SWIMMING) [P], 0 = neither.
static const uintptr_t CTX_SWIMMING = 0x116ACFC;
static uint8_t OnWaterNow()
{
    if (void* v = PlayerVehicle()) return At<int32_t>(v, eng::VEH_CLASS) == kClassBoat ? 1 : 0;
    uint8_t* p = SusChar();
    return PtrOk(p) && IsIn(p, CTX_SWIMMING) ? 2 : 0;
}

// (Down and Bust, one of each per player: Per, at the top)
static volatile LONG g_bustCount = 0;   // the radio's "busted" news
static uint32_t      g_stationMs = 0;   // when the station trip was asked for (0 = none pending)
static int           g_stationWho = 0;  // ...and whose bust it was
static uint32_t      g_lastTickMs = 0;  // the last director tick in the player's world

// The story-mission signal: missions switch the pause menu's "return to police station" off
// while they run (Notes\32 §5.3, inferred). Wanted mode stands down then.
static bool MissionRunning()
{
    uint8_t* g = *reinterpret_cast<uint8_t**>(g_base + RVA_GAME);
    return PtrOk(g) && At<uint8_t>(g, 0x335F) == 0 && At<uint8_t>(g, 0x335E) == 0;
}

static bool LockInPlace(uint8_t* c, const char* anim)
{
    if (reinterpret_cast<DeadFn>(g_base + FN_DEAD)(c)) return false;
    if (!IsIn(c, CTX_IDLENOGRAV))
    {
        reinterpret_cast<ChFn>(g_base + FN_FORCEDEF)(c);
        reinterpret_cast<SetSubFn>(g_base + FN_SETSUB)(c, Ctx(CTX_IDLENOGRAV), 0);
    }
    if (!IsIn(c, CTX_IDLENOGRAV)) return false;
    const int id = reinterpret_cast<AnimIdFn>(g_base + FN_ANIMID)(anim);
    if (id <= 0 || id > 0xFFFF) return true;                         // locked, just no such anim
    reinterpret_cast<SubPlayFn>(g_base + FN_SUBPLAY)(reinterpret_cast<CurHFn>(g_base + FN_CURH)(c), static_cast<uint16_t>(id), false);
    return true;
}

static bool Knockdown(uint8_t* p, float seconds)
{
    uint8_t* cd = At<uint8_t*>(p, 0x640);
    if (!PtrOk(cd)) return false;
    At<float>(cd, 0x29C) = seconds;                                   // = Character_SetArrestTime
    uint8_t* kd = reinterpret_cast<SetSubFn>(g_base + FN_SETSUB)(p, Ctx(CTX_KNOCKDOWN), 0);
    if (!PtrOk(kd) || reinterpret_cast<uintptr_t>(kd) == 0xDEADDEAD) { At<float>(cd, 0x29C) = -1.0f; return false; }
    reinterpret_cast<KdSetupFn>(g_base + FN_KDSETUP)(kd, 0xF9, 0, nullptr);   // the lying "stunned" pose
    Me().down.overrideSet = true;
    Me().down.knocked = true;
    return true;
}

// `why` = the log's reason (nullptr = quiet: a bust or wanted mode switching off took over).
static void EndDown(uint8_t* p, const char* why)
{
    if (why) Log("    wanted: %s back on their feet after %.1f s (%s)\n", Pn(), Me().down.t, why);
    if (Me().down.overrideSet && PtrOk(p))
        if (uint8_t* cd = At<uint8_t*>(p, 0x640)) if (PtrOk(cd)) At<float>(cd, 0x29C) = -1.0f;
    Me().down = {};
}

// A hit lands: only on foot, standing (not climbing, swimming, riding...), not already down.
// A miss is logged (at most every 10 s) and the officers try again a second later.
static uint32_t g_missLog = 0;

static bool Hit(bool tie, float seconds)
{
    uint8_t* p = P1();
    if (!PtrOk(p) || Me().down.on || Me().bust.on) return false;
    const char* miss = PlayerVehicle() ? "you're in a vehicle"
                     : reinterpret_cast<DeadFn>(g_base + FN_DEAD)(p) ? "you're down already"
                     : !IsIn(p, CTX_DEFAULT) ? "you're not on your feet (climbing, jumping, swimming...)" : nullptr;
    if (miss)
    {
        const uint32_t now = GetTickCount();
        if (now - g_missLog > 10000)
        {
            g_missLog = now;
            Log("    wanted: a police %s missed %s - %s\n", tie ? "grapple" : "ray gun", Pn(), miss);
        }
        return false;
    }
    Me().down = {}; Me().down.on = true; Me().down.tie = tie; Me().down.len = seconds;
    if (tie)
    {
        if (!LockInPlace(p, "Grapple_TakeHit")) { Me().down.tie = false; if (!Knockdown(p, seconds)) { Me().down = {}; return false; } }
    }
    else if (!Knockdown(p, seconds)) { Me().down = {}; return false; }
    Log(">>> wanted: %s %s - down for %.1f s\n", Pn(), tie ? "tied up by a police grapple" : "stunned by a police ray gun", seconds);
    return true;
}

// An officer's punch lands (wanted_officers.h): a short knockdown, with Hit's own gates.
static bool Punched(int officerId, float seconds)
{
    uint8_t* p = P1();
    if (!PtrOk(p) || Me().down.on || Me().bust.on) return false;
    if (PlayerVehicle() || reinterpret_cast<DeadFn>(g_base + FN_DEAD)(p) || !IsIn(p, CTX_DEFAULT)) return false;
    Me().down = {}; Me().down.on = true; Me().down.punch = true; Me().down.len = seconds;
    if (!Knockdown(p, seconds)) { Me().down = {}; return false; }
    Log(">>> wanted: %s knocked down by officer #%d's punch - down for %.1f s\n", Pn(), officerId, seconds);
    return true;
}

static void DownTick(float dt)
{
    if (!Me().down.on) return;
    uint8_t* p = P1();
    if (!PtrOk(p)) { Me().down = {}; return; }
    Me().down.t += dt;
    // the grapple's wrap plays first, then he lies tied up for the rest - or, if the game won't
    // take him from the wrap to a knockdown, he stays held in the wrap until the time is up
    if (Me().down.tie && !Me().down.knocked && !Me().down.held && Me().down.t >= 1.1f &&
        !Knockdown(p, std::fmax(0.5f, Me().down.len - Me().down.t)))
    {
        if (!IsIn(p, CTX_IDLENOGRAV))
        {
            reinterpret_cast<ChFn>(g_base + FN_TODEF)(p);
            EndDown(p, "the tie's knockdown was refused and the hold was gone");
            return;
        }
        Me().down.held = true;
        Log("    wanted: the tie's knockdown was refused - held in the wrap for the rest\n");
    }
    const bool inKd = IsIn(p, CTX_KNOCKDOWN);
    if (Me().down.overrideSet && inKd)
    {
        uint8_t* h = reinterpret_cast<CurHFn>(g_base + FN_CURH)(p);
        if (PtrOk(h) && At<float>(h, 0x68) > 0.0f)                    // the knockdown took our time: drop it
        {
            uint8_t* cd = At<uint8_t*>(p, 0x640);
            if (PtrOk(cd)) At<float>(cd, 0x29C) = -1.0f;
            Me().down.overrideSet = false;
        }
    }
    if (inKd || IsIn(p, CTX_IDLENOGRAV))
    {
        // no getting up early: JUMP (and attack / special) held back while down
        uint8_t* c = At<uint8_t*>(p, 0x6A8);
        uint8_t* pad = PtrOk(c) ? At<uint8_t*>(c, 0x18) : nullptr;
        if (PtrOk(pad))
        {
            const uint32_t m = *reinterpret_cast<const uint32_t*>(g_base + RVA_MASK_JUMP) |
                               *reinterpret_cast<const uint32_t*>(g_base + RVA_MASK_ATTACK) |
                               *reinterpret_cast<const uint32_t*>(g_base + RVA_MASK_SPECIAL);
            At<uint32_t>(pad, 0x0C) &= ~m;
            At<uint32_t>(pad, 0x10) &= ~m;
        }
    }
    const char* why = nullptr;
    if (IsIn(p, CTX_DEATH)) why = "you died";
    else if (Me().down.held && Me().down.t >= Me().down.len) why = "the tie wore off";
    else if (Me().down.held && !IsIn(p, CTX_IDLENOGRAV)) why = "the hold broke";
    else if (Me().down.knocked && !inKd && !IsIn(p, CTX_IDLENOGRAV) && Me().down.t > 0.5f)
        why = Me().down.tie ? "the tie wore off" : Me().down.punch ? "you got up" : "the stun wore off";
    else if (Me().down.t > Me().down.len + 3.0f) why = "the watchdog: still down 3 s past the time";
    if (why)
    {
        if (IsIn(p, CTX_IDLENOGRAV)) reinterpret_cast<ChFn>(g_base + FN_TODEF)(p);
        EndDown(p, why);
    }
}

// The studs. Free roam keeps them in the save's bank (*(u64**)0x141C7E640 = save + 0xFB10, set at
// 0x140811B18), not in the character's wallet ([ch+0xAA8]+0x320, an in-level count): the
// 2026-10-25 test read the wallet and took "0 studs" at all 11 busts. The game's own LoseStuds
// 0x1409AD170 picks between the two (0x1409A5A90, from the level's flags) and pops the stud
// counter; it's called once (type 2 = 1000 base, forced past the invincibility extra), and the
// balance it moved is then set to exactly `pen` less.
static const uintptr_t FN_LOSESTUDS = 0x9AD170, RVA_BANK = 0x1C7E640, RVA_GAMERULES = 0x17EC0A0;
typedef uint64_t(__fastcall* LoseStudsFn)(uint8_t* ch, int type, int force);

static uint64_t TakeStuds(uint8_t* p, uint64_t pen, char* how, size_t howCap)
{
    if (pen == 0) { strcpy_s(how, howCap, "busted_studs = 0"); return 0; }
    uint8_t* wa = At<uint8_t*>(p, 0xAA8);
    uint64_t* wallet = PtrOk(wa) ? At<uint64_t*>(wa, 0x320) : nullptr;
    if (!PtrOk(wallet)) wallet = nullptr;
    uint64_t* bank = *reinterpret_cast<uint64_t**>(g_base + RVA_BANK);
    if (!PtrOk(bank)) bank = nullptr;
    const uint64_t w0 = wallet ? *wallet : 0, b0 = bank ? *bank : 0;
    // LoseStuds gives up without a wallet; its bank path writes the counter's timer through
    // [game+0x3050], and it always notifies the game rules *0x1417EC0A0 (vt[104]), both unchecked
    uint8_t* g = *reinterpret_cast<uint8_t**>(g_base + RVA_GAME);
    const bool call = wallet && PtrOk(g) && PtrOk(At<void*>(g, 0x3050)) &&
                      PtrOk(*reinterpret_cast<void**>(g_base + RVA_GAMERULES));
    const uint64_t r = call ? reinterpret_cast<LoseStudsFn>(g_base + FN_LOSESTUDS)(p, 2, 1) : 0;
    const uint64_t w1 = wallet ? *wallet : 0, b1 = bank ? *bank : 0;
    uint64_t* live = nullptr;
    const char* which = "nothing to take";
    if (bank && b1 < b0)         { live = bank;   which = "the bank"; }
    else if (wallet && w1 < w0)  { live = wallet; which = "the wallet"; }
    else if (bank && b0)         { live = bank;   which = "the bank, directly (LoseStuds took nothing)"; }
    else if (wallet && w0)       { live = wallet; which = "the wallet, directly (LoseStuds took nothing)"; }
    const uint64_t before = live == bank ? b0 : w0;
    if (live) *live = before > pen ? before - pen : 0;
    const uint64_t lost = live ? before - *live : 0;
    _snprintf_s(how, howCap, _TRUNCATE, "from %s; LoseStuds %s %llu; bank %llu -> %llu, wallet %llu -> %llu", which,
                call ? "returned" : "not called,", static_cast<unsigned long long>(r),
                static_cast<unsigned long long>(b0), static_cast<unsigned long long>(bank ? *bank : 0),
                static_cast<unsigned long long>(w0), static_cast<unsigned long long>(wallet ? *wallet : 0));
    return lost;
}

// [busted1 2026-11-18] busted: fade out, and only the busted player wakes up outside the police station.
#include "wanted_busted.h"

// Busted: does the trip to the station follow? It is the pause menu's own "return to the police
// station" - a level change to Police_Station_Lobby (Notes\32 §5.3) [P], so it takes the whole game:
// BOTH players go through the loading screen. In co-op that is only done when asked for
// ([wanted] busted_station_coop = 1); by default the busted player is let go on the spot (studs
// taken, the grace) and the other one plays on.
static bool StationTrip(const Config& cfg)
{
    return cfg.wanted_busted_station && (!RawPlayer(1) || cfg.wanted_busted_station_coop);
}

// kind = the model's arrestKind (1 on foot, 2 in a vehicle, 3 in the water). Runs AsPlayer(the busted one):
// only their crimes stop counting, only their character is cuffed and charged. (Free roam keeps the
// studs in the save's one bank - TakeStuds - so the total both players see drops.)
static void Busted(int level, const Config& cfg, uint8_t kind)
{
    uint8_t* p = P1();
    if (!PtrOk(p)) return;
    Per& me = Me();
    me.counts = false; me.crimeHeat = 0.0f;                    // nothing counts from here (the grace)
    me.inGrace = true;
    me.bust = {}; me.bust.on = true;
    me.bust.inWater = kind == 3;
    me.bust.onFoot = PlayerVehicle() == nullptr && !me.bust.inWater;   // a swimmer isn't cuffed in the water
    if (me.down.on) EndDown(p, nullptr);
    if (me.bust.onFoot) LockInPlace(p, "cuffed_back_idle");
    char how[200] = "";
    const uint64_t pen = static_cast<uint64_t>(cfg.wanted_busted_studs) * static_cast<uint64_t>(level > 0 ? level : 1);
    const uint64_t lost = TakeStuds(p, pen, how, sizeof(how));
    InterlockedIncrement(&g_bustCount);
    Log(">>> wanted: %s BUSTED at level %d %s - %llu studs taken (%s)%s\n", Pn(), level,
        me.bust.inWater ? "in the water (a police boat came alongside)"
        : me.bust.onFoot ? "on foot (down, and an officer cuffed you)" : "in a vehicle (stopped, with the police at your door)",
        static_cast<unsigned long long>(lost), how,
        BoWill(cfg) ? "; next: the pavement outside the police station (only this player)"      // [busted1]
        : StationTrip(cfg) ? "; off to the police station"
        : cfg.wanted_busted_station ? "; let go on the spot (co-op: the station trip would take the other player too)" : "");
}

// After the cuffs (a beat): the pause menu's own "return to the police station", a level change
// with a loading screen. Tick checks that the loading screen really came (g_stationMs).
static const float kBustBeatS = 3.0f;    // the cuffs and the BUSTED banner, before the station trip
static void BustTick(float dt, const Config& cfg)
{
    Per& me = Me();
    if (BoBusy()) { if (BoTick(dt, cfg)) me.bust = {}; return; }   // [busted1] the fade and the move are running
    if (!me.bust.on) return;
    me.bust.t += dt;
    if (me.bust.t < kBustBeatS) return;
    if (BoStart(cfg)) return;                                      // [busted1] busted_outside = 1: no station trip
    uint8_t* p = P1();
    if (PtrOk(p) && me.bust.onFoot) reinterpret_cast<ChFn>(g_base + FN_FORCEDEF)(p);
    me.bust = {};
    if (!cfg.wanted_busted_station) return;
    if (!StationTrip(cfg))
    {
        Log(">>> wanted: %s is let go on the spot - no station trip in co-op (a level change: it would take the other "
            "player along; [wanted] busted_station_coop = 1 does it anyway)\n", Pn());
        return;
    }
    // The level change takes the other player too: their chase ends with the old world.
    const int who = g_sus & 1;
    if (RawPlayer(1))
    {
        AsPlayer as(1 - who);
        Per& o = Me();
        if (o.st.level > 0 || o.st.heat > 0.0f || o.down.on)
        {
            Log(">>> wanted: %s goes to the station too (the trip is a level change for both) - their level %d is cleared\n",
                Pn(), o.st.level);
            if (o.down.on) EndDown(P1(), nullptr);
            wanted::Reset(o.st);
        }
    }
    Log(">>> wanted: %s - the station trip - the pause menu's own \"return to the police station\"\n", Pn(who));
    g_stationMs = GetTickCount() | 1;
    g_stationWho = who;
    reinterpret_cast<StationFn>(g_base + FN_STATION)(nullptr, 1, nullptr);
}

// NPC characters are heap objects; there is no pool to walk (the "character pool" at 0x141C7F510
// is the legacy creature array, its count only ever written 0 - Notes\40b §7). A character is in a
// vehicle while the ref the player's own vehicle comes from (ch+0x428 / +0x430) is alive.
static bool InAVehicle(uint8_t* ch)
{
    uint8_t* blk = At<uint8_t*>(ch, eng::MC_VEHBLK);
    void** h = At<void**>(ch, eng::MC_VEHREF);
    return PtrOk(blk) && At<uint32_t>(blk, 4) != 0 && PtrOk(h) && PtrOk(*h);
}

// The police on foot: officers out of the chasing cars and cops off the street (Notes\38).
#include "wanted_officers.h"

static int OfficerFault(EXCEPTION_POINTERS* ep)
{
    if (!g_offFault)
    {
        g_offFault = true;
        Log("!!! EXCEPTION 0x%08lX in the officers-on-foot code at %p - the police stay in their cars for this "
            "session; the rest of wanted mode carries on.\n", ep->ExceptionRecord->ExceptionCode,
            ep->ExceptionRecord->ExceptionAddress);
    }
    OfficersForgetAll();
    return EXCEPTION_EXECUTE_HANDLER;
}

static void RunOfficers(const World& w, float dt, int level, bool chase, const float yp[3], uint8_t zap, float zapS,
                        const Config& cfg)
{
    if (g_offFault) return;
    __try { OfficersTick(w, dt, level, chase, yp, zap, zapS, cfg); }
    __except (OfficerFault(GetExceptionInformation())) {}
}

static void RunOfficersRelease(const World& w, const char* why)
{
    if (g_offFault) return;
    __try { OfficersReleaseAll(w, why); }
    __except (OfficerFault(GetExceptionInformation())) {}
}

static float RunOfficersNearest(const float yp[3])
{
    if (g_offFault) return 1e9f;
    __try { return OfficersNearest(yp); }
    __except (OfficerFault(GetExceptionInformation())) { return 1e9f; }
}

static void RunOfficerCuff(const float yp[3])
{
    if (g_offFault) return;
    __try { OfficersCuff(yp); }
    __except (OfficerFault(GetExceptionInformation())) {}
}

static const char* WhyText(uint8_t why)
{
    switch (why)
    {
    case wanted::kRequested: return "set";
    case wanted::kEvaded:    return "you lost them";
    case wanted::kNotFound:  return "no police found you";
    case wanted::kBlocked:   return "a mission is running (its own chase, or a story mission) - standing down";
    case wanted::kCrime:     return "a crime";
    case wanted::kBusted:    return "busted";
    case wanted::kWasted:    return "you went down - the police lose interest";
    default:                 return "";
    }
}

// The brick meters (lcudrive publishes them through the 3Dmigoto bridge: slot 5 player 1's, slot 6
// player 2's): level, heat (the next brick's fill), flash (evading), alpha (fades in with heat, out a
// few seconds after clear) - Per::hudLevel / hudHeat / hudFlash / hudAlpha, and g_p2Here for the layout.
static uint32_t       g_rngState = 0x2545F491u;

static float Rand01()
{
    g_rngState ^= g_rngState << 13; g_rngState ^= g_rngState >> 17; g_rngState ^= g_rngState << 5;
    return (g_rngState & 0xFFFFFF) / 16777216.0f;
}

// The police helicopters at levels 4-5 (spawned here, flown from lcudrive.cpp's helicopter hook).
#include "wanted_heli.h"
// Traffic that flees a chase (the cut Vehicle.DriverlessFlee, 2026-11-15).
#include "wanted_flee.h"

// Wanted mode off, `[`, the unhook - and once after a wanted-mode fault (FaultCleanup): everything it
// has out goes back - the officers on foot, the pursuits and pool entries, the helicopters - and
// both players' level, bust, water and meter are cleared.
static void StandDown(const World& w, float dt, const char* why)
{
    g_nCop = 0; g_nChase = 0;
    g_crimesOn = false;
    if (g_nFlee) FleeReleaseAll(why);
    if (g_nOs || g_nHc) RunOfficersRelease(w, why);
    if (AnyOn()) { StopAll(w); Log(">>> wanted: off - the police go back to traffic (%s)\n", why); }
    RunHeliStop(why, dt);
    for (int i = 0; i < 2; ++i)
    {
        AsPlayer as(i);
        Per& me = Me();
        if (me.down.on) EndDown(RawPlayer(i), nullptr);
        BoAbort(i, RawPlayer(i));                                  // [busted1] a move outside the station under way
        ResetPlayer(me);
        me.hudAlpha = std::fmax(0.0f, me.hudAlpha - dt);
    }
    for (wanted::UnitPlan& d : g_plan) { d.owner = -1; d.level = 0; d.inst = 0; }
    g_stationMs = 0;
    g_shown = 0;
    g_susBoat = false;
}

// After a wanted-mode fault (lcudrive.cpp's WantedFault): Tick never runs again, so what wanted mode
// had out would stay out for the session - police cars ramming you, a police helicopter with its
// siren on and its searchlight lit, nothing flying it. From PMThunk, once: the helicopters sent off
// and let go of at once (they need no world), then - in the player's world, when it next ticks with
// its traffic on - the rest of the stand-down. Each part on its own guard; nothing is retried.
static void FaultCleanup(void* pm)
{
    static bool s_heli = false, s_rest = false;
    if (!s_heli) { s_heli = true; RunHeliAbandon("wanted mode faulted"); }
    if (s_rest) return;
    World w = {};
    __try { w = GetWorld(); }
    __except (EXCEPTION_EXECUTE_HANDLER) { s_rest = true; return; }
    if (!w.ok || pm != w.pm) return;
    s_rest = true;
    __try { StandDown(w, 0.0f, "wanted mode faulted"); }
    __except (EXCEPTION_EXECUTE_HANDLER) { Log("    wanted: the clean-up after the fault faulted too - left as it is\n"); }
    g_pl[0].hudAlpha = 0.0f; g_pl[1].hudAlpha = 0.0f;        // (no tick fades the brick meters out after this)
}

// What one player's tick leaves for the shared part of Tick (the police, the 1 s log).
struct PlayerOut
{
    wanted::Output o;
    bool  here, redo, inVehicle;
    int   pursuers;
    float officer;
    uint8_t wet;
};

// One player's part of the director tick, run AsPlayer(pl): their water, their model update, their
// arrest and bust, their stun, their meter. `pri`: they are the primary - the officers on foot and
// the helicopters are theirs. `req`: -1, or a level to set (debug / the pause menu).
static void PlayerTick(const World& w, int pl, bool pri, float dt, const Config& cfg, bool blocked, bool mapIn,
                       int req, PlayerOut& out)
{
    Per& me = Me();
    // nothing counts while you're being busted, nor for the grace after (the model holds it too)
    if (me.inGrace && !me.bust.on && me.st.graceT <= 0.0f)
    {
        me.inGrace = false;
        Log(">>> wanted: %s - the after-bust grace is over - crimes count again\n", Pn());
    }
    me.counts = !me.bust.on && me.st.graceT <= 0.0f;

    // The water: this player in a boat or swimming. The latch (wanted_model.h) decides when the boats
    // come out and go home; the boat units then run for them while it's on, boats are on in the ini and
    // the city has its boat traffic (the boat map). A change shows in the plan (Tick, ApplyPlan).
    // (It runs while you're not wanted too - become wanted at sea and the boats come at once - but
    // only talks while you are.)
    const uint8_t wet = OnWaterNow();
    const bool isWanted = me.applied > 0;
    if (isWanted && wet != me.wetHow && (wet || me.water.on))
        Log("    wanted: %s is %s\n", Pn(), wet == 1 ? "in a boat" : wet == 2 ? "swimming" : "back on land");
    me.wetHow = wet;
    if (pri) g_susBoat = wet == 1;
    if (wanted::WaterStep(me.water, wet != 0, dt, kWaterStartS, cfg.wanted_boat_linger) && isWanted)
        Log(">>> wanted: %s %s\n", Pn(), me.water.on ? "on the water - the harbour police can join in"
                                                   : "back on land a while - the police boats can go home");
    static bool s_mapSaid = false;
    if (isWanted && me.water.on && cfg.wanted_boats && !mapIn && !s_mapSaid)
    {
        s_mapSaid = true;
        Log("    wanted: no boat traffic in this city yet (the game loads the boat map after Act 2 mission 12) - "
            "no police boats (shown once)\n");
    }
    me.boatsUp = me.water.on && cfg.wanted_boats && mapIn;

    wanted::Input in{};
    in.dt = dt;
    in.requested = req;
    // A police helicopter with you in sight is a pursuer too (wanted_heli.h, last tick's count): while it
    // watches, the evade countdown can't start - but alone, once the cars have lost you, only for
    // heli_hold seconds (wanted_model.h HeliHold: no car outruns it, and nothing tests line of sight).
    // in.nearest stays the cars': it drives the arrest, and nobody arrests you from the air.
    // (The helicopters follow the primary only.)
    const int ground = Pursuers(w, pl);
    in.pursuers = ground + (pri ? wanted::HeliPursuers(g_heliHold, ground, g_heliWatch, dt, cfg.wanted_heli_hold) : 0);
    in.blocked = blocked;
    in.crime = me.crimeHeat; me.crimeHeat = 0.0f;
    float yp[3];
    const bool haveYou = YouPos(yp);
    // a crime the police saw: a police vehicle or officer near, or a helicopter with you in sight (the
    // same test as the pursuer's: not one leaving, none with heli_sight = 0)
    if (in.crime > 0.0f) in.policeNear = haveYou && (NearestPolice(yp, 50.0f) < 50.0f || (pri && g_heliWatch > 0));
    in.nearest = me.copNearest;
    void* veh = PlayerVehicle();
    in.inVehicle = veh != nullptr;
    in.onFoot = !veh && PtrOk(P1()) && cfg.wanted_gadgets && !me.bust.on;
    // the officers on foot chase the primary: only they can be tied, cuffed, or arrested by one at the door
    in.officer = pri && haveYou && g_nOs ? RunOfficersNearest(yp) : 1e9f;
    uint8_t* p1 = P1();
    in.down = me.down.on || (!veh && PtrOk(p1) && IsIn(p1, CTX_KNOCKDOWN));   // ours, or knocked down in a fight
    in.dead = PtrOk(p1) && IsIn(p1, CTX_DEATH);
    in.speed = me.youSpeed;
    in.roll = Rand01();
    // the water arrest: only while you're on the water now, so a boat at a quay can't arrest a car
    in.swimming = wet == 2;
    in.boatNear = wet && cfg.wanted_boat_arrest ? me.boatNear : 1e9f;
    const int before = me.st.level;
    wanted::Output& o = out.o;
    wanted::Update(g_params, me.st, in, o);
    if (pri && o.level <= 0) wanted::ResetHeliHold(g_heliHold);    // (a new chase: a new hold)
    // the officers on foot: out of the cars, off the street, and the one in range fires the model's
    // hit. They stand still through the bust's beat (the nearest one cuffs you), and a station trip
    // forgets them with the old world.
    if (pri) g_inCarT = veh ? g_inCarT + dt : 0.0f;
    // the arrest you can see: the officer at your side starts cuffing when the meter starts
    if (o.arrest > 0.0f && !o.busted)
    {
        if (!me.arrestShown)
        {
            me.arrestShown = true;
            Log(">>> wanted: %s ARREST in progress - %s\n", Pn(), o.arrestKind == 1
                ? "you're down and an officer is cuffing you (get up before the meter fills)"
                : o.arrestKind == 3 ? "a police boat is alongside you in the water (swim for the shore before the meter fills)"
                : "you're stopped with the police at your door (drive off before the meter fills)");
            if (pri && o.arrestKind == 1 && haveYou) RunOfficerCuff(yp);
        }
    }
    else if (me.arrestShown && !o.busted) { me.arrestShown = false; Log("    wanted: %s - the arrest is off - you got away from it\n", Pn()); }
    if (o.busted) me.arrestShown = false;
    if (o.busted)
    {
        Busted(before, cfg, o.arrestKind);
        if (pri && me.bust.onFoot && haveYou) RunOfficerCuff(yp);
    }
    else if (pri && !me.bust.on && haveYou && (cfg.wanted_officers || g_nOs || g_nHc))
    {
        const bool footChase = cfg.wanted_officers && o.level > 0 && (!veh || g_inCarT < 3.0f);
        RunOfficers(w, dt, o.level, footChase, yp, o.zap, o.zapS, cfg);
    }
    else if (o.zap) me.st.zapCool = std::fmin(me.st.zapCool, 1.0f);
    DownTick(dt);
    BustTick(dt, cfg);
    out.redo = o.level != me.applied || (in.requested >= 0 && o.level > 0);
    if (out.redo)
    {
        Log(">>> wanted: %s level %d (%s), heat %.2f\n", Pn(), o.level, WhyText(o.why), o.heat);
        me.applied = o.level;
    }
    // the brick meter
    me.hudLevel = static_cast<float>(o.level);
    me.hudHeat = o.heat;
    // .z: 0 nothing, 1 evading (the bricks blink), 2..3 an arrest in progress (2 + how far), 4 BUSTED.
    // The banner ends just before the station trip, so it can't be left standing over the loading screen.
    const bool banner = me.bust.on && me.bust.t < kBustBeatS - 0.3f;
    me.hudFlash = banner ? 4.0f : o.arrest > 0.0f ? 2.0f + o.arrest : o.evadeLeft > 0.0f ? 1.0f : 0.0f;
    const float want = (o.level > 0 || o.heat > 0.05f || banner) && cfg.wanted_hud ? 1.0f : 0.0f;
    const float a = me.hudAlpha;
    me.hudAlpha = want > a ? std::fmin(1.0f, a + dt * 3.0f) : std::fmax(0.0f, a - dt * 0.5f);
    out.here = true;
    out.inVehicle = in.inVehicle;
    out.pursuers = in.pursuers;
    out.officer = in.officer;
    out.wet = wet;
}

// After cPopulationManager::Update (the game thread).
static void Tick(void* pm, float dt, const Config& cfg, bool on)
{
    const bool p2 = RawPlayer(1) != nullptr;
    g_p2Here = p2 ? 1 : 0;
    // Player 2 left the game: after a moment (a respawn or a cut may hide them for a few frames [U])
    // their chase is over. Until then their state waits, their police go to player 1 or home (MakePlan).
    {
        static float s_goneT = 0.0f;
        Per& b = g_pl[1];
        s_goneT = p2 ? 0.0f : s_goneT + dt;
        if (!p2)
        {
            b.hudFlash = 0.0f;
            b.hudAlpha = std::fmax(0.0f, b.hudAlpha - dt * 0.5f);
            if (s_goneT > 5.0f && (b.st.level || b.st.heat > 0.0f || b.st.graceT > 0.0f || b.down.on || b.bust.on || b.applied))
            {
                if (b.st.level) Log(">>> wanted: P2 has left the game - their level %d goes with them\n", b.st.level);
                ResetPlayer(b);
            }
        }
    }
    // The primary - whom the officers on foot and the helicopters chase: the wanted player with the
    // higher level (a tie stays where it is). Player 2 leaving hands it back to player 1. Never in the
    // middle of the current one's stun or bust, and - while the current one is still wanted - not
    // within 5 s of the last change.
    {
        const int cur = g_sus & 1;
        const int want = wanted::PickPrimary(cur, g_pl[0].st.level, p2 ? g_pl[1].st.level : 0, p2);
        const bool lost = cur == 1 && !p2;
        const uint32_t nowS = GetTickCount();
        const Per& c = g_pl[cur];
        const bool free = !c.down.on && !c.bust.on && (c.st.level <= 0 || nowS - g_priMs > 5000);
        if (lost || (want != cur && free))
        {
            g_sus = lost ? 0 : want;
            ++g_susGen;
            g_priMs = nowS;
            wanted::ResetHeliHold(g_heliHold);
            Log(">>> wanted: the officers on foot and the helicopters are after %s now%s\n", Pn(), lost ? " (player 2 is gone)" : "");
        }
    }
    const int pri = g_sus & 1;
    World w = GetWorld();
    if (!w.ok || pm != w.pm)                                 // only the player's world, once a frame
    {
        // The player's world with its traffic off (TM+0x1FC = 0: a mission's DisableTraffic) - the rest
        // of wanted mode waits, but the helicopters fly from the helicopter hook whatever happens here:
        // they're sent home from here (wanted_heli.h).
        if (PtrOk(w.pm) && pm == w.pm) RunHeliTrafficOff(dt, !on || g_stopReq);
        return;
    }
    // a new world (a level load: the station trip): the officers' pointers mean nothing now
    static void* s_lastPm = nullptr;
    if (w.pm != s_lastPm)
    {
        if (s_lastPm && (g_nOs || g_nHc)) Log("    wanted: a new world - the officers on foot are forgotten\n");
        s_lastPm = w.pm;
        OfficersForgetAll();
        FleeForgetAll();
        BoForgetAll();                                             // [busted1]
        for (Per& me : g_pl) { wanted::ResetWater(me.water); me.boatsUp = false; }   // the water starts over too
        g_boat2Bad = false;
        RunHeliForget("a new world");                                         // ...and the helicopters went with the old one
    }
    // the station trip, checked: the world stops for its loading screen, or it never happened
    const uint32_t nowMs = GetTickCount();
    if (g_stationMs)
    {
        if (nowMs - g_lastTickMs > 2500)
        {
            Log(">>> wanted: back in the world %.0f s after the station trip began (the loading screen); %s's grace %.0f s left\n",
                (nowMs - g_stationMs) / 1000.0f, Pn(g_stationWho), g_pl[g_stationWho & 1].st.graceT);
            g_stationMs = 0;
        }
        else if (nowMs - g_stationMs > 10000)
        {
            Log(">>> wanted: no loading screen 10 s after the station call - the station trip did NOT happen\n");
            g_stationMs = 0;
        }
    }
    g_lastTickMs = nowMs;
    if (!on || g_stopReq)
    {
        StandDown(w, dt, g_stopReq ? "lcudrive is letting go" : "wanted mode is off");
        return;
    }
    if (!g_bootDone) { g_bootDone = true; if (cfg.wanted_debug_level > 0) InterlockedExchange(&g_request, cfg.wanted_debug_level); }
    g_params.evadeBase = cfg.wanted_evade_base; g_params.evadePerLevel = cfg.wanted_evade_per_level;
    g_params.levelDamp = cfg.wanted_level_damp; g_params.unseenMul = cfg.wanted_unseen; g_params.unseenCap = cfg.wanted_unseen_cap;
    g_params.stunS = cfg.wanted_stun_s; g_params.tieS = cfg.wanted_tie_s; g_params.graceS = cfg.wanted_busted_grace;
    g_params.zapRange = cfg.wanted_officer_fire; g_params.zapPerS = cfg.wanted_officer_grapple;
    g_params.boatRange = cfg.wanted_boat_arrest_range; g_params.boatArrestS = cfg.wanted_boat_arrest_s;
    const float wts[cCount] = { cfg.wanted_crime_run_over, cfg.wanted_crime_ram_police, cfg.wanted_crime_wreck,
                                cfg.wanted_crime_steal_car, cfg.wanted_crime_hit_cop, cfg.wanted_crime_hit_person,
                                cfg.wanted_crime_punch, cfg.wanted_crime_shoot_person, cfg.wanted_crime_shoot_prop,
                                cfg.wanted_crime_steal_police };
    for (int i = 0; i < cCount; ++i) g_crimeW[i] = wts[i];
    const bool mission = MissionRunning();
    static int s_lastMission = -1;
    if (static_cast<int>(mission) != s_lastMission)
    {
        s_lastMission = mission;
        Log(">>> wanted: %s\n", mission ? "a story mission is on - wanted mode stands down"
                                        : "free roam - crimes count");
    }
    g_crimesOn = cfg.wanted_crimes && !mission;              // (and each player's own Per::counts)
    RefreshCops(w, cfg);
    const bool mapIn = BoatMapLoaded();
    const bool blocked = Foreign(w) || mission;
    for (int pl = 0; pl < 2; ++pl)                          // each player's speed, held 0.3 s past an impact
    {
        void* pv = pl ? ::Player2Vehicle() : ::PlayerVehicle();
        void* pb = pv ? BodyOf(pv) : nullptr;
        const float sp = pb ? SpeedOf(pb) : 0.0f;
        g_recentSpeed[pl] = std::fmax(IsFin(sp) ? sp : 0.0f, g_recentSpeed[pl] - 20.0f * dt);
    }

    // Each player's own wanted level. A level set for both (Ctrl+Alt+L, debug_level) goes to both; one
    // set for a player who isn't in the game (the pause menu, pad 2 alone) goes to player 1.
    const int reqAll = static_cast<int>(InterlockedExchange(&g_request, -1));
    int req[2] = { static_cast<int>(InterlockedExchange(&g_requestP[0], -1)), static_cast<int>(InterlockedExchange(&g_requestP[1], -1)) };
    if (!p2 && req[1] >= 0) { req[0] = req[1]; req[1] = -1; }
    if (reqAll >= 0) req[0] = req[1] = reqAll;
    const int maxBefore = static_cast<int>(g_shown);
    int lvBefore[2] = { g_pl[0].st.level, g_pl[1].st.level };
    PlayerOut po[2] = {};
    for (int pl = 0; pl < 2; ++pl)
    {
        if (pl == 1 && !p2) continue;
        AsPlayer as(pl);
        PlayerTick(w, pl, pl == pri, dt, cfg, blocked, mapIn, req[pl], po[pl]);
    }
    // (Empty / parked cars are never a crime: nobody sees them go. Only the car-jack event - a car somebody was
    //  driving - counts. 2026-11-25: only driven cars.)
    const int lvNow[2] = { g_pl[0].st.level, p2 ? g_pl[1].st.level : 0 };
    const int maxNow = lvNow[0] > lvNow[1] ? lvNow[0] : lvNow[1];
    g_shown = maxNow;
    // the radio's level-up bulletin follows g_shown (the higher level); a player whose level went up
    // UNDER the other's gets one through g_levelNews - never both for the same tick
    if (maxNow <= maxBefore)
        for (int pl = 0; pl < 2; ++pl)
            if (po[pl].here && lvNow[pl] > lvBefore[pl] && lvNow[pl] >= 1)
            {
                g_levelNews = (((g_levelNews >> 8) + 1) << 8) | lvNow[pl];
                break;
            }

    // The police, shared out between the wanted players (wanted_model.h SplitUnits). The records are
    // re-made when a player's level was set or changed (theirs only), or when the plan changed: the
    // other player became wanted or got away, the primary changed, the water, a boat key.
    {
        wanted::UnitPlan now[kNumUnits];
        MakePlan(now, cfg);
        bool changed = false;
        for (int u = 0; u < kNumUnits; ++u) if (!wanted::SamePlan(now[u], g_plan[u])) changed = true;
        const bool redo[2] = { po[0].redo, po[1].redo };
        if (changed || redo[0] || redo[1])
        {
            ApplyPlan(w, redo, cfg);
            if (lvNow[0] > 0 && lvNow[1] > 0)
            {
                char who[200]; int k = 0; who[0] = 0;
                for (int u = 0; u < kNumUnits; ++u)
                    if (g_p[u].on && k < 160)
                        k += _snprintf_s(who + k, sizeof(who) - k, _TRUNCATE, " %s->%s x%d", kUnits[u].name, Pn(g_p[u].plan.owner),
                                         static_cast<int>(g_p[u].plan.inst));
                Log(">>> wanted: both players wanted (P1 level %d, P2 level %d) - the police are shared:%s\n", lvNow[0], lvNow[1], who);
            }
        }
    }
    // the helicopters: spawned, sent off and faded out here (the game thread's spawn phase), flown
    // from the helicopter hook. They follow the primary.
    {
        AsPlayer as(pri);
        float yp[3];
        const bool haveYou = YouPos(yp);
        RunHeliTick(w, dt, po[pri].here ? po[pri].o.level : 0, cfg, haveYou, yp, mission);
    }
    // civilian cars near a fast chase flee it (wanted_flee.h; [wanted] flee)
    RunFlee(w, dt, cfg, blocked, lvNow);
    const uint32_t now = GetTickCount();
    if (maxNow > 0 && now - g_lastLog > 1000)
    {
        g_lastLog = now;
        char recs[256]; int k = 0; recs[0] = 0;
        ForRecords(w, [&](uint8_t* e) {
            const int u = UnitOf(At<int32_t>(e, 0x50), At<int32_t>(e, 0x54));
            if (k < 200) k += _snprintf_s(recs + k, sizeof(recs) - k, _TRUNCATE, " [%d/%d act %d n %d%s]",
                                          At<int32_t>(e, 0x50), At<int32_t>(e, 0x54), e[0x5A], At<int32_t>(e, 0x5C),
                                          u < 0 ? " FOREIGN" : !p2 ? "" : g_p[u].plan.owner == 1 ? " P2" : " P1");
        });
        for (int pl = 0; pl < 2; ++pl)
        {
            if (!po[pl].here || lvNow[pl] <= 0) continue;
            const Per& me = g_pl[pl];
            const wanted::Output& o = po[pl].o;
            const uint8_t wet = po[pl].wet;
            char evade[64] = "", foot[64] = "", water[160] = "", helis[320] = "";
            if (pl == pri) HeliStatus(helis, sizeof(helis), cfg);
            if (o.evadeLeft > 0.0f) _snprintf_s(evade, sizeof(evade), _TRUNCATE, " - out of sight, drops in %.0f s", o.evadeLeft);
            if (pl == pri && (g_nOs || g_nHc))
                _snprintf_s(foot, sizeof(foot), _TRUNCATE, "; officers on foot %d (nearest %.0f u), cars held %d",
                            g_nOs, po[pl].officer < 1e8f ? po[pl].officer : -1.0f, g_nHc);
            // the water: why you count as on it, the latch, and the boats - chasing, with an officer aboard
            // (manned = the water arrest can happen), the nearest manned one
            if (wet || me.water.on || me.nBoat)
                _snprintf_s(water, sizeof(water), _TRUNCATE, "; water: %s, latch %s (%+.1f s)%s, boats chasing %d (manned %d, nearest %.0f u)",
                            wet == 1 ? "in a boat" : wet == 2 ? "swimming" : "on land", me.water.on ? "ON" : "off", me.water.t,
                            me.water.on && !me.boatsUp ? (cfg.wanted_boats ? " - no boat map" : " - boats = 0") : "",
                            me.nBoat, me.nBoatManned, me.boatNear < 1e8f ? me.boatNear : -1.0f);
            Log("    wanted %s %d (heat %.2f): %d chasing you (%d boosted, fastest %.1f u/s, nearest %.0f u; you %s %.1f u/s%s%s%s)%s%s%s | records%s\n",
                Pn(pl), o.level, o.heat, po[pl].pursuers, static_cast<int>(g_nCop), g_copFastest, me.copNearest < 1e8f ? me.copNearest : -1.0f,
                po[pl].inVehicle ? "driving" : "on foot", me.youSpeed, foot, water, helis, me.down.on ? " - DOWN" : "",
                me.st.seen ? "" : " (they haven't found you yet)", evade, recs);
        }
    }
}

// The hotkey thread: Ctrl+Alt+L steps the level 0 -> 1 -> ... -> 5 -> 0, for BOTH players (from the
// higher of the two: a debug key, and the one way to set player 1's and player 2's level at once).
static int RequestNext()
{
    const int next = (static_cast<int>(g_shown) + 1) % 6;
    InterlockedExchange(&g_request, next);
    return next;
}

// The pause menu's Set Wanted Level: for the player who paused (0 / 1), or both (anything else: the
// game doesn't say who).
static void RequestFor(int player, int level)
{
    if (player == 0 || player == 1) InterlockedExchange(&g_requestP[player], level);
    else InterlockedExchange(&g_request, level);
}

} // namespace wd
