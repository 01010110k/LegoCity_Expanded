// =============================================================================================
//  airport.h - real planes waiting at the airport (the staged airport file of
//  2026-11-13; its decisions moved to airport_model.h so the harness can run them - same behaviour).
//
//  The goal: the plane spawns at the airport, and takes off and lands like an aeroplane, not a
//  helicopter. The airport (NY_Airport, cells A 19..21 x B 3..6) has NO runway
//  in its data - no runway material, no long strip. The only straight flat run that ends in open
//  air is the north quay apron: y 1.64 the whole way (KdTerrain), from the vehicle-theft garage
//  fence at x 141 to the quay edge at x 209.6 (the last 25 u a pier, the dock basin to the south),
//  and open sea beyond (the hover probe reads the sea surface at y 0.3 out there). So that is the
//  runway: take off toward +x (out to sea), land toward -x (in from the sea). Numbers and obstacles:
//  planes_stage\airport\airport_layout.json.
//
//  What this does, from cPopulationManager::Update after the original (the system phase, as
//  wanted.h; never from a vehicle update):
//    * a player within `near` of the threshold and no airport plane -> SpawnAIVehicle the
//      [plane] airport_model there (registered as a script vehicle: not culled as traffic),
//      facing down the runway, parked (VehicleAction_SetParked's own call), with a map pin;
//    * a player gets in -> unparked (airplane.h stands down on a parked vehicle), pin off;
//    * left on the runway again -> parked again, pin back;
//    * left anywhere else, or wrecked, or nobody within `far` -> faded out (VehicleAction_Destroy
//      #FadeOut), weak ref dropped; the next one appears at the threshold when a player is near;
//    * Ctrl+Alt+F (the hotkey thread only sets a request): bring the plane back to the threshold;
//    * which way a fresh plane faces is checked on the tick it is parked (the facing goes through
//      SpawnAIVehicle's own construction): off the runway -> faded and respawned turned, twice at most.
//  Several planes (2026-11-14: more than one flyable plane at the airport, where there had been only
//  one): [plane] airport_count of them (default 3, 1..6), one per stand of airport_model.h's table -
//  stand 0 is the runway start above, the others are apron stands south of the quay. Every stand is a
//  Slot here: its own weak ref, state, pin, facing watchdog and model name ([plane] airport_models, one
//  per stand; a name the game does not know, or one neither [airplane] nor [plane] flies, falls back to
//  [plane] airport_model - logged once per stand). The stands take turns spawning (one SpawnAIVehicle
//  call per world update, one preload outstanding); a stand is not filled while a player or another
//  airport plane stands on it; Ctrl+Alt+F goes to every stand (the plane a player sits in and the ones
//  on their stands stay; every other stand gets a fresh one), one beep for the lot.
//  Engine limits [P: Notes\33 1.8 / 2.2]: a script-registered vehicle (registerAsScript = true) is on
//  the population manager's script list, outside the traffic pool - no MaxInstances entry is needed
//  (Aircraft_Elifant has none: it is in no pool) and the traffic pass never culls or counts it against
//  MaxActive 24; what remains is the 0x80 vehicles of the level's ApiVehicleManager (status 7 = full:
//  retried after 60 s like any failed spawn). Six parked planes are six bodies asleep (sim LOD 10).
//  It stands down during story missions (wd::MissionRunning) and while lcudrive is off ([). On unhook
//  (and once after a fault here) everything goes back, for every stand: pin off, the plane faded unless
//  someone is in it, the weak ref and any pending preload released.
//  Everything here is the game's own calls, used the way the scripts / the PM pass use them:
//    SpawnAIVehicle 0x1406FE560 (CreateAIVehicle's core), park 0x140194170 (SetParked), the
//    weak-ref release 0x1402DAF40(0x1402E0590(0x1402E05F0()), ctl, 0), ud vt[13] (Destroy),
//    the map pin 0x1402324B0 / 0x1402328F0 / 0x14022E5B0 on 0x14023C820(0x14183F690, player)+0x390
//    (UI_Map_SetVehicleActive's and MakePursuer's own sequence).
// =============================================================================================
#pragma once
#include "airport_model.h"
#include "spawn_turn.h"                 // one spawner call per world update, shared with dino.h and sites.h

namespace ap {

struct WeakRef { void** cell; int32_t* ctl; };             // alive: ctl && ctl[1] > 0; obj = *cell

typedef WeakRef* (*SpawnFn)(void* pm, WeakRef* out, void* lvl, int cls, int type, const float* pos,
                            const float* facing, int* status, bool registerAsScript, uint32_t owner);
typedef void     (*ParkFn)(uint8_t* ud, bool parked, float a, float stopDist, float d);
typedef void     (*DestroyFn)(uint8_t* ud, void* opts);
typedef void*    (*RefPoolFn)();
typedef void*    (*RefPool1Fn)(void*);
typedef void     (*RefFreeFn)(void* pool, int32_t* ctl, int z);
typedef void     (*PreloadFreeFn)(void* scm, int model, uint32_t owner);
typedef void*    (*MapOfFn)(void* mgr, int player);
typedef void*    (*PinListFn)(void* p);
typedef uint32_t*(*NameHashFn)(uint32_t* out, const char* name);
typedef void     (*PinAddFn)(void* list, void* veh, bool flag, uint32_t* pinName);
typedef void*    (*PinFindFn)(void* list, void* veh);
typedef void     (*PinDelFn)(void* list, void* pin, int z);

static const uintptr_t FN_SPAWN = 0x6FE560, FN_PARK = 0x194170, FN_REFPOOL0 = 0x2E05F0, FN_REFPOOL1 = 0x2E0590,
                       FN_REFFREE = 0x2DAF40, FN_PRELOAD_FREE = 0x6FAAE0, RVA_SCM = 0x17EBFA0,
                       FN_MAPOF = 0x23C820, RVA_MAPMGR = 0x183F690, MAP_PINS = 0x390, FN_PINLIST = 0x23C870,
                       FN_NAMEHASH = 0x251FE0, FN_PINADD = 0x2324B0, FN_PINFIND = 0x2328F0, FN_PINDEL = 0x22E5B0;
template <class F> static F Fn(uintptr_t rva) { return reinterpret_cast<F>(g_base + rva); }

// ---- the runway (the north quay apron; airport_layout.json) and the stands (airport_model.h) -----
static const float kEdgeX = 209.6f;                         // the quay edge on the centre line (57 u from the nose)

// One stand's plane. SpawnAIVehicle: row2 = -facing, so the NOSE points along the facing sent.
struct Slot
{
    WeakRef  ref;
    apm::Mem mem;
    int      model, cls, type;
    bool     pinned;
    float    send[2];            // the facing sent (x, z): the stand's, or the watchdog's turn
    bool     sendSet;
    bool     preloaded;          // a streaming attempt took a preload under g_owner (status 1)
    void*    spawnPm;            // the population manager the spawn attempt began with
    char     name[48];           // the model resolved for this stand (after the fall-back)
    char     asked[48];          // ... and what [plane] airport_models asked for when that was logged
    char     bad[48];            // a model of airport_models whose spawn failed for good here: not asked for again
    bool     fellBack;           // ... that happened this tick (the stand-in comes after 2 s, not 60)
    int      beep;               // this tick's answer to Ctrl+Alt+F (0 none, 1 done, 2 refused)
};
static Slot     g_slot[apm::kMaxStands] = {};
static uint32_t g_owner = 0;
static bool     g_standsChecked = false;
static volatile LONG g_recall = 0;       // Ctrl+Alt+F (set on the hotkey thread)
static volatile LONG g_beep = 0;         // 1 = done, 2 = refused (played by the hotkey thread)
static volatile bool g_stopReq = false;  // unhook: give everything back on the next PM update
static bool     g_released = false;      // ... done (unhook or after a fault): nothing more from here

static void* Veh(const Slot& s)
{
    if (!PtrOk(s.ref.ctl) || !PtrOk(s.ref.cell) || s.ref.ctl[1] <= 0) return nullptr;
    void* v = *s.ref.cell;
    return PtrOk(v) ? v : nullptr;
}

// A weak reference's count back (the engine's own release: 0x1402DAF40 on the pool when it hits 0).
static void DropRef(WeakRef& r)
{
    if (PtrOk(r.ctl) && InterlockedDecrement(reinterpret_cast<volatile LONG*>(r.ctl)) <= 0)
        Fn<RefFreeFn>(FN_REFFREE)(Fn<RefPool1Fn>(FN_REFPOOL1)(Fn<RefPoolFn>(FN_REFPOOL0)()), r.ctl, 0);
    r = WeakRef{};
}

// The preload a streaming attempt took (status 1 = Preload 0x1406F99E0 under g_owner [P]): a successful
// spawn releases it itself [P: 0x1406FE71E]; an attempt that ends without a plane gives it back here -
// the engine's own call, only when its SCM is there (it checks the same). One stand spawns at a time
// (apm Facts.hold), so at most one preload is ever out under g_owner.
static void FreePreload(Slot& s)
{
    if (!s.preloaded) return;
    s.preloaded = false;
    void* scm = *reinterpret_cast<void**>(g_base + RVA_SCM);
    if (PtrOk(scm) && s.model >= 0 && g_owner) Fn<PreloadFreeFn>(FN_PRELOAD_FREE)(scm, s.model, g_owner);
}

static void Pin(Slot& s, void* veh, bool on, const Config& cfg)
{
    if (on == s.pinned) return;
    if (on && !cfg.plane_airport_pin[0]) return;                        // no pin wanted (taking one off always runs)
    void* mgr = reinterpret_cast<void*>(g_base + RVA_MAPMGR);
    for (int i = 0; i < 2; ++i)                                         // MakePursuer's loop
    {
        void* map = Fn<MapOfFn>(FN_MAPOF)(mgr, i);
        if (!PtrOk(map)) continue;
        void* list = Fn<PinListFn>(FN_PINLIST)(reinterpret_cast<uint8_t*>(map) + MAP_PINS);
        if (!PtrOk(list)) continue;
        if (on)
        {
            uint32_t name = 0;
            Fn<NameHashFn>(FN_NAMEHASH)(&name, cfg.plane_airport_pin);
            Fn<PinAddFn>(FN_PINADD)(list, veh, false, &name);
        }
        else if (void* pin = Fn<PinFindFn>(FN_PINFIND)(list, veh))
            Fn<PinDelFn>(FN_PINDEL)(list, pin, 0);
    }
    s.pinned = on;
}

static void Park(void* veh, bool on)
{
    uint8_t* ud = VehicleUserData(veh);
    if (ud) Fn<ParkFn>(FN_PARK)(ud, on, 0.0f, 0.0f, -1.0f);             // SetParked at rest: stopDist 0
}

static void FadeOut(Slot& s, int idx, void* veh, const Config& cfg, const char* why)
{
    Pin(s, veh, false, cfg);
    if (uint8_t* ud = VehicleUserData(veh))
    {
        struct { uint32_t a; uint8_t flags; } o = { 0, 4 };              // VehicleAction_Destroy #FadeOut
        reinterpret_cast<DestroyFn>((*reinterpret_cast<void***>(ud))[13])(ud, &o);
    }
    Log(">>> airport[%d]: the plane fades out - %s\n", idx, why);
    DropRef(s.ref);
}

// where player i is: their vehicle's body, or on foot the character (ch+0xA8)
static bool PlayerPos(int i, float out[3])
{
    uint8_t* ch = wd::RawPlayer(i);
    if (!PtrOk(ch)) return false;
    void* v = i == 0 ? ::PlayerVehicle() : ::Player2Vehicle();
    void* inst = PtrOk(v) ? At<void*>(v, eng::VEH_INSTANCE) : nullptr;
    void* body = PtrOk(inst) ? At<void*>(inst, eng::INST_BODY) : nullptr;
    for (int k = 0; k < 3; ++k) out[k] = PtrOk(body) ? At<float>(body, eng::BODY_POS + 4 * k) : At<float>(ch, 0xA8 + 4 * k);
    return IsFin(out[0]) && IsFin(out[1]) && IsFin(out[2]);
}

static float Dist2(const float a[3], const float b[3]) { const float dx = a[0] - b[0], dz = a[2] - b[2]; return std::sqrt(dx * dx + dz * dz); }

static bool VehPos(void* veh, float out[3])
{
    void* inst = At<void*>(veh, eng::VEH_INSTANCE);
    if (!PtrOk(inst)) return false;
    for (int k = 0; k < 3; ++k) out[k] = At<float>(inst, eng::INST_POS + 4 * k);
    return IsFin(out[0]) && IsFin(out[2]);
}

static float Speed(void* veh)
{
    void* inst = At<void*>(veh, eng::VEH_INSTANCE);
    void* body = PtrOk(inst) ? At<void*>(inst, eng::INST_BODY) : nullptr;
    if (!PtrOk(body)) return 0.0f;
    float v2 = 0.0f;
    for (int k = 0; k < 3; ++k) { const float v = At<float>(body, eng::BODY_LINVEL + 4 * k); v2 += v * v; }
    return std::sqrt(v2);
}

// Where its nose points, flat (x, z): the engine's forward is -row2 (engine.h).
static bool Nose(void* veh, float out[2])
{
    void* inst = At<void*>(veh, eng::VEH_INSTANCE);
    if (!PtrOk(inst)) return false;
    out[0] = -At<float>(inst, eng::INST_ROW2 + 0);
    out[1] = -At<float>(inst, eng::INST_ROW2 + 8);
    const float l = std::sqrt(out[0] * out[0] + out[1] * out[1]);
    if (!IsFin(l) || l < 0.3f) return false;
    out[0] /= l; out[1] /= l;
    return true;
}

// Which system flies this model (lcudrive.cpp, after airplane.h and plane.h): 0 [airplane], 1 [plane], 2 neither.
static int FlownBy(const char* model, const Config& cfg)
{
    return apm::Owner(cfg.airplane_enable != 0, fw::KindFromName(model, cfg) != 0,
                      cfg.plane_enable != 0, pl::KindFromName(model, cfg) != pl::kNone);
}

// The engine side of airport_model.h's actions, for one stand.
struct World
{
    void* pm; void* lvl; const Config* cfg; Slot* s; int idx; bool* spawned;
    bool Known(const char* name, int* model, int* cls, int* type)
    {
        *model = wd::g_modelId(*reinterpret_cast<void**>(g_base + wd::RVA_MODEL_REG), name);
        return *model >= 0 && wd::g_classType(wd::H(name), cls, type);
    }
    // The stand's model: its entry of [plane] airport_models when the game knows it AND a flight system
    // flies it; else [plane] airport_model (the one RunAirport's switch is about). Logged when it changes.
    bool Resolve()
    {
        char want[48];
        apm::StandModel(cfg->plane_airport_models, idx, cfg->plane_airport_model, want, sizeof(want));
        int model = -1, cls = -1, type = -1;
        const char* use = want;
        const char* why = nullptr;
        if (_stricmp(want, cfg->plane_airport_model) != 0)
        {
            if (s->bad[0] && _stricmp(want, s->bad) == 0) why = "it could not be spawned";
            else if (!Known(want, &model, &cls, &type)) why = "the game has no such model (its data is not installed)";
            else if (FlownBy(want, *cfg) == 2)     why = "neither [airplane] models nor [plane] models flies it";
            if (why) use = cfg->plane_airport_model;
        }
        if (use != want || model < 0)
        {
            if (!Known(use, &model, &cls, &type))
            {
                Log(">>> airport[%d]: %s is not a model the game can spawn (model %d) - check [plane] airport_model\n",
                    idx, use, model);
                return false;
            }
        }
        if (_stricmp(s->asked, want) != 0 || _stricmp(s->name, use) != 0)
        {
            if (why) Log(">>> airport[%d]: %s wanted for %s, but %s - %s stands there instead\n", idx, want,
                         apm::StandAt(idx).what, why, use);
            else Log(">>> airport[%d]: %s for %s (%.1f, %.1f)\n", idx, use, apm::StandAt(idx).what,
                     apm::StandAt(idx).pos[0], apm::StandAt(idx).pos[2]);
            strcpy_s(s->asked, want); strcpy_s(s->name, use);
        }
        s->model = model; s->cls = cls; s->type = type;
        g_owner = wd::H("lcudrive_airport");
        return true;
    }
    void BeginSpawn() { s->spawnPm = pm; }
    int Spawn(float t)
    {
        if (!spt::Mine(sitem::kTurnAirport)) return 1;                   // not this update's spawner: again next tick
        const apm::Stand& st0 = apm::StandAt(idx);
        if (!s->sendSet) { s->send[0] = st0.facing[0]; s->send[1] = st0.facing[1]; s->sendSet = true; }
        alignas(16) float p[4] = { st0.pos[0], st0.pos[1] + 0.3f, st0.pos[2], 1.0f };
        alignas(16) float f[4] = { s->send[0], 0.0f, s->send[1], 0.0f };
        int st = -1;
        WeakRef out = {};
        *spawned = true;                                                 // this tick's one SpawnAIVehicle call
        Fn<SpawnFn>(FN_SPAWN)(pm, &out, lvl, s->cls, s->type, p, f, &st, true, g_owner);
        if (st == 0 && PtrOk(out.ctl) && out.ctl[1] > 0)
        {
            s->ref = out;
            s->preloaded = false;                                        // the spawn released it itself [P]
            Log(">>> airport[%d]: %s (class %d type %d) spawned on %s (%.1f, %.1f) after %.1f s, veh=%p, facing (%.2f, %.2f)\n",
                idx, s->name, s->cls, s->type, st0.what, st0.pos[0], st0.pos[2], t, Veh(*s), s->send[0], s->send[1]);
            return 0;
        }
        // no plane: a ref it handed back anyway (status 0 with a dead object) is released, not leaked
        if (PtrOk(out.ctl)) DropRef(out);
        if (st == 0) st = -2;                                            // "spawned" but no live ref: a failure
        if (st == 1) s->preloaded = true;                                // the streaming took a preload under g_owner
        // A model of airport_models (not airport_model itself) that ends in a failure of its own - never
        // streams in (20 s), bad class / type, no definition, create failed, invalid - is not asked for
        // again on this stand: airport_model stands in (Resolve). "No room" (2 / 8 / 7) is nobody's fault.
        const bool retry = (st == 1 || st == 2 || st == 8) && t < 20.0f;  // airport_model.h's own rule
        if (!retry && st != 2 && st != 8 && st != 7 && _stricmp(s->name, cfg->plane_airport_model) != 0)
        {
            Log(">>> airport[%d]: %s could not be spawned (status %d after %.1f s) - %s will stand there instead\n", idx, s->name,
                st, t, cfg->plane_airport_model);
            strcpy_s(s->bad, s->name);
            s->fellBack = true;
        }
        return st;
    }
    void AbandonSpawn()         { FreePreload(*s); s->spawnPm = nullptr; }
    void Turn(int attempt)
    {
        // the facing watchdog: this plane's nose against what was sent -> the next facing to send
        void* v = Veh(*s);
        float got[2], next[2];
        const float want[2] = { apm::StandAt(idx).facing[0], apm::StandAt(idx).facing[1] };
        if (v && Nose(v, got) && apm::NextFacing(attempt, s->send, got, want, next))
        {
            Log(">>> airport[%d]: the plane faces (%.2f, %.2f), sent (%.2f, %.2f), the stand is (%.2f, %.2f) - next time sent "
                "(%.2f, %.2f) (%s)\n", idx, got[0], got[1], s->send[0], s->send[1], want[0], want[1], next[0], next[1],
                attempt == 0 ? "as a turn" : "as a mirror");
            s->send[0] = next[0]; s->send[1] = next[1];
        }
    }
    void Park(bool on)          { if (void* v = Veh(*s)) ap::Park(v, on); }
    void Pin(bool on)           { if (void* v = Veh(*s)) ap::Pin(*s, v, on, *cfg); else if (!on) s->pinned = false; }
    void Fade(const char* why)  { if (void* v = Veh(*s)) FadeOut(*s, idx, v, *cfg, why); else DropRef(s->ref); s->pinned = false; }
    void Dropped()              { DropRef(s->ref); s->pinned = false; }
    void Beep(int code)         { s->beep = code; }
    void Say(const char* fmt, ...)
    {
        // the model's lines begin ">>> airport: " - the stand's number goes in
        char buf[512];
        va_list a; va_start(a, fmt);
        _vsnprintf_s(buf, sizeof(buf), _TRUNCATE, fmt, a);
        va_end(a);
        static const char kPre[] = ">>> airport: ";
        if (strncmp(buf, kPre, sizeof(kPre) - 1) == 0) Log(">>> airport[%d]: %s", idx, buf + sizeof(kPre) - 1);
        else Log("%s", buf);
    }
};

static void ReleaseSlot(Slot& s, int idx, const Config& cfg, const char* why)
{
    void* veh = Veh(s);
    if (veh)
    {
        const bool aboard = veh == ::PlayerVehicle() || veh == ::Player2Vehicle();
        if (!aboard) FadeOut(s, idx, veh, cfg, why);
        else { Pin(s, veh, false, cfg); DropRef(s.ref); Log(">>> airport[%d]: %s - the plane stays with its pilot, no longer ours\n", idx, why); }
    }
    else DropRef(s.ref);
    s.pinned = false;
    FreePreload(s);
    s.spawnPm = nullptr;
    apm::Reset(s.mem);
}

// Everything back, once (unhook, or after a fault in this code), for every stand: the pin off; the
// plane faded unless someone is aboard (then it is theirs: only our ref goes); a pending preload given back.
static void Release(const Config& cfg, const char* why)
{
    if (g_released) return;
    g_released = true;
    for (int i = 0; i < apm::kMaxStands; ++i) ReleaseSlot(g_slot[i], i, cfg, why);
}

// once per world update, after cPopulationManager::Update
static void Tick(void* pm, float dt, const Config& cfg, bool on)
{
    if (g_released) return;
    if (g_stopReq) { Release(cfg, "lcudrive unhooked"); return; }
    const int count = apm::StandCount(cfg.plane_airport_count);
    if (!g_standsChecked)
    {
        g_standsChecked = true;
        if (!apm::StandsOk(apm::kMaxStands, 8.6f, 349.6f, 3.3f)) Log(">>> airport: the stand table fails its own check - tell the author\n");
    }
    const bool recall = InterlockedExchange(&g_recall, 0) != 0;
    bool world = false;
    void* lvl = nullptr;
    uint8_t* p1 = wd::RawPlayer(0);
    if (PtrOk(p1))
    {
        lvl = wd::g_getLevel(p1 + 0x10);
        world = PtrOk(lvl) && wd::g_getPM(lvl) == pm && At<uintptr_t>(pm, 0) == g_base + wd::RVA_VT_PM;
    }
    const bool live = on && cfg.plane_airport && world;
    float pp[2][3] = {}; bool ppOk[2] = { false, false };
    void* pv[2] = { nullptr, nullptr };
    bool mission = false;
    if (live)
    {
        for (int i = 0; i < 2; ++i) ppOk[i] = PlayerPos(i, pp[i]);
        pv[0] = ::PlayerVehicle(); pv[1] = ::Player2Vehicle();
        mission = wd::MissionRunning();
    }
    // where every stand's plane is (for "another airport plane stands on this stand")
    void* vehs[apm::kMaxStands]; float vpos[apm::kMaxStands][3]; bool vposOk[apm::kMaxStands];
    for (int i = 0; i < apm::kMaxStands; ++i)
    {
        vehs[i] = Veh(g_slot[i]);
        vposOk[i] = live && vehs[i] && VehPos(vehs[i], vpos[i]);
    }
    bool spawned = false;                    // a SpawnAIVehicle call was made this tick
    int beeps[apm::kMaxStands] = {};
    for (int i = 0; i < apm::kMaxStands; ++i)
    {
        Slot& s = g_slot[i];
        const apm::Stand& st = apm::StandAt(i);
        void* veh = vehs[i];
        if (i >= count)
        {
            // a stand beyond [plane] airport_count (the count was lowered): its plane goes unless flown
            if (s.mem.state != apm::kIdle || veh || PtrOk(s.ref.ctl))
            {
                const bool aboard = veh && (veh == ::PlayerVehicle() || veh == ::Player2Vehicle());
                if (!aboard) ReleaseSlot(s, i, cfg, "this stand is switched off (airport_count)");
            }
            continue;
        }
        apm::Facts f = {};
        f.dt = dt;
        f.recall = recall;
        f.on = on && cfg.plane_airport;
        f.haveVeh = veh != nullptr;
        f.samePm = pm == s.spawnPm;
        f.world = world;
        World w = { pm, lvl, &cfg, &s, i, &spawned };
        s.beep = 0;
        if (live)
        {
            // the players: nearest to this stand, and whether one of them is in its plane
            f.best = 1e9f; f.nearPlane = 1e9f;
            for (int k = 0; k < 2; ++k) if (ppOk[k])
            {
                const float d = Dist2(pp[k], st.pos);
                f.best = std::fmin(f.best, d);
                if (d < 7.0f) f.someoneClose = true;
            }
            for (int j = 0; j < apm::kMaxStands; ++j)                    // another stand's plane left on this one
                if (j != i && vposOk[j] && Dist2(vpos[j], st.pos) < 7.0f) f.someoneClose = true;
            f.aboard = veh && (veh == pv[0] || veh == pv[1]);
            if (vposOk[i])
            {
                f.onStand = Dist2(vpos[i], st.pos) < apm::StandRadius(i, 20.0f);
                for (int k = 0; k < 2; ++k) if (ppOk[k]) f.nearPlane = std::fmin(f.nearPlane, Dist2(pp[k], vpos[i]));
            }
            f.speed = veh ? Speed(veh) : 0.0f;
            int hc = 1, hm = 1;
            f.wrecked = veh && vd::Health(veh, hc, hm) && hc < 1;
            f.mission = mission;
            float nose[2];
            if (veh && Nose(veh, nose)) { f.facingKnown = true; f.facingDot = nose[0] * st.facing[0] + nose[1] * st.facing[1]; }
            f.hold = spawned;
            for (int j = 0; j < count; ++j) if (j != i && g_slot[j].mem.state == apm::kSpawning) f.hold = true;
        }
        apm::Params p;
        p.nearU = cfg.plane_airport_near; p.farU = cfg.plane_airport_far; p.leaveU = cfg.plane_airport_leave;
        p.onStandU = apm::StandRadius(i, 20.0f);
        s.fellBack = false;
        apm::Tick(w, s.mem, p, f);
        if (s.fellBack && s.mem.state == apm::kIdle && s.mem.cool > 2.0f) s.mem.cool = 2.0f;
        beeps[i] = s.beep;
        vehs[i] = Veh(s);                                                // faded / spawned this tick
        if (!vehs[i]) vposOk[i] = false;
    }
    if (recall)
    {
        const int b = apm::RecallBeep(beeps, count);
        if (b) g_beep = b;
    }
}

} // namespace ap
