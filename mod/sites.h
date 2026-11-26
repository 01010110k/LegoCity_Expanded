// =============================================================================================
//  sites.h - vehicles waiting at stands anywhere ([sites] in lcudrive.ini), and Port Atlas as a
//  place the mod knows (2026-11-15). The decisions are in sites_model.h so the harness can run
//  them; this is the engine side. airport.h is the same thing for the airport's planes - this is
//  its general form: any model, any kind, any place, the places read from the ini.
//
//  The problem: Port Atlas had no boats or helicopters, so it was close to impossible to leave
//  the island. So boats wait off the quay and a helicopter on the apron - and a
//  boat on Fresco's east waterfront, to get there in the first place.
//
//  What this does, from cPopulationManager::Update after the original (the system phase, as
//  airport.h and dino.h), for each stand of [sites] stand1..stand12:
//    * a player within `near` of it and no vehicle of ours for it -> SpawnAIVehicle (airport.h's
//      call, script-registered: never culled as traffic), facing the stand's heading; the tick
//      after, parked (cars / planes; boats too with [sites] park = 2) or settled (helicopters), pin on;
//    * a player gets in -> unparked, pin off, left alone entirely while he is in it;
//    * left on its stand -> waits again; left somewhere else -> faded once nobody is within
//      `leave` of it for 10 s, and a fresh one stands on the (clear) stand again;
//    * nobody within `far` -> faded; a story mission -> the waiting ones fade, none appear;
//    * NEVER removed while a player is in it or within 8 u of it (sites_model.h MayRemove).
//  Slots 13..24 (2026-11-18, Notes)) are not ini lines: config.h fills them from the life-spot data's
//  "car" lines (spots_model.h) - parked scenery cars at the new places, kind "scenery": a car, no map pin.
//  It stands down while lcudrive is off ([) and when [sites] enable = 0. On unhook (and once after
//  a fault here) everything goes back, for every stand: the pin off, the vehicle faded unless a
//  player is in it or beside it (then it stays, unparked, ours no longer), the weak ref and any
//  pending preload released.
//
//  Engine facts used here (the calls are airport.h's, proven there: SpawnAIVehicle 0x1406FE560,
//  SetParked 0x140194170, the weak-ref release, ud vt[13] Destroy #FadeOut, the map pin calls):
//    [P] a name -> its model id (0x140834EA0 on the registry *0x1417EBD38) and its class / type
//        (0x1406FB490 on the name's hash): wd::g_modelId / wd::g_classType, as airport.h. The
//        name is AITypes.txt's SECOND column (Naval_Arowana, Aircraft_Hera, Police_Chopper).
//    [P] boats: the free-roam scripts make the boats the player is meant to take with the same
//        call and nothing else - CreateAIVehicle("Arowana", "Naval", pos, dir) in
//        parkedvehicles\ny_little_italy_2217_-_canal.sf, then only GetDriver() / Release(). So a
//        script-spawned boat floats where it is put and is boarded like any boat. [U] from the DLL.
//    [P] SetParked on a boat: 0x140194170 first sets the user data's stop block (ud+0x390, by
//        0x14018B1F0: parked byte +8, the speed along its heading kept at +0xC), then, for class 3
//        (veh+0x340), 0x1404AB750 = veh+0xFC8 bit 3; ApiVehicle 0x1404AA910 copies that bit to
//        veh+0xFC6 bit 3 every frame. The script's blocker boat is SetLocked + SetParked
//        (br_twin_bridge_1103_-_boats.sf) [P]; what a parked boat does that an unparked one does
//        not is [U], so boats are NOT parked unless [sites] park = 2 (cars and planes are, as
//        airport.h parks its planes).
//    [P] helicopters: sf_residential_0822_helicoper.sf makes its SkyBringer with CreateAIVehicle
//        and waits for GetDriver(): a script-spawned helicopter is boarded with no more than that.
//        Its pilot data (HoveringAircraft, Notes\26) is born in state 2, "flying" [P 0x1401A6623]:
//        the occupied bit stays set and it hovers at its HeightTargetActive, rotor turning. State 0
//        is "parked": the vehicle side clears the occupied bit, the servo uses HeightTargetIdle,
//        gravity is back, the body rests - where a helicopter is after any pad landing, and Begin
//        (boarding) leaves it for 1 -> 2 [P Notes\26 2.2]. So a waiting helicopter of ours is put in
//        state 0 once ([sites] heli_settle; the write heli.h's WreckPre already makes) [I: that the
//        born-in-2 one takes it like a landed one]. Flying it afterwards is heli.h's business as for
//        any helicopter a player flies (it looks at the player's vehicle, not at who made it); its
//        exit-anywhere / free-flight patches are what let it land and leave at Port Atlas, which has
//        no helipad gizmo and no minimum-height tiles of its own [I].
//    [U] the boat map (PopulationManagers +0xF0, loaded after Act 2 mission 12): SpawnAIVehicle
//        copies lane handles into the vehicle's user data; whether a boat made before the map is
//        loaded misbehaves was never seen. The log line of the spawn is the first evidence.
// =============================================================================================
#pragma once
#include "sites_model.h"
#include "spawn_turn.h"

namespace si {

struct Slot
{
    ap::WeakRef  ref;
    sitem::Mem   mem;
    sitem::Stand stand;          // the stand this slot's state is for (a changed ini line lets go first)
    int      model, cls, type;
    bool     pinned;
    float    send[2];            // the facing sent (x, z): the stand's, or the watchdog's turn
    bool     sendSet;
    bool     preloaded;          // a streaming attempt took a preload under g_owner (status 1)
    void*    spawnPm;            // the population manager the spawn attempt began with
    char     name[48];           // the model resolved for this stand (after the fall-back)
    bool     bad;                // the stand's own model failed for good here: the fall-back stands in
    bool     fellBack;           // ... that happened this tick (the stand-in comes after 2 s, not 60)
    bool     said;               // the resolve line was logged for this name
};
static Slot     g_slot[sitem::kMaxSlots] = {};
static uint32_t g_owner = 0;
static bool     g_checked = false;
static volatile bool g_stopReq = false;  // unhook: give everything back on the next PM update
static bool     g_released = false;      // ... done (unhook or after a fault): nothing more from here
static bool     g_there[2] = { false, false };   // player i is at Port Atlas
static float    g_prevPos[2][3] = {};            // each player's position on the last tick
static bool     g_prevOk[2] = { false, false };
static float    g_settle = 0.0f;                 // > 0: a player jumped a moment ago (sites_model.h Facts.settling)
// [marina1] marina.h rides on this update (Notes 56): its berths / station boats tick right after the stands, with the
// same spawn turn, the same one-spawn-per-update flag and the same teleport hold; each side holds while the other spawns.
typedef void (*AfterFn)(void* pm, void* lvl, float dt, const Config& cfg, bool on, bool world, bool mission,
                        const float pp[2][3], const bool ppOk[2], bool& spawned, bool settling);
static AfterFn g_after = nullptr;                                          // [marina1]
static void (*g_afterRelease)(const Config& cfg, const char* why) = nullptr;   // [marina1]
static bool (*g_extraBusy)() = nullptr;                                   // [marina1] one of its boats is being spawned
static bool (*g_extraNear)(const float p[3], float r) = nullptr;         // [marina1] one of its boats is within r of p

static void* Veh(const Slot& s)
{
    if (!PtrOk(s.ref.ctl) || !PtrOk(s.ref.cell) || s.ref.ctl[1] <= 0) return nullptr;
    void* v = *s.ref.cell;
    return PtrOk(v) ? v : nullptr;
}

// (airport.h FreePreload: the engine's own release, only what g_owner took)
static void FreePreload(Slot& s)
{
    if (!s.preloaded) return;
    s.preloaded = false;
    void* scm = *reinterpret_cast<void**>(g_base + ap::RVA_SCM);
    if (PtrOk(scm) && s.model >= 0 && g_owner) ap::Fn<ap::PreloadFreeFn>(ap::FN_PRELOAD_FREE)(scm, s.model, g_owner);
}

// The map pin (airport.h Pin: UI_Map_SetVehicleActive's own sequence), named by [sites] pin.
static void Pin(Slot& s, void* veh, bool on, const Config& cfg)
{
    if (on == s.pinned) return;
    if (on && (!cfg.sites_pin[0] || s.stand.quiet)) return;             // no pin wanted / scenery (taking one off always runs)
    void* mgr = reinterpret_cast<void*>(g_base + ap::RVA_MAPMGR);
    for (int i = 0; i < 2; ++i)
    {
        void* map = ap::Fn<ap::MapOfFn>(ap::FN_MAPOF)(mgr, i);
        if (!PtrOk(map)) continue;
        void* list = ap::Fn<ap::PinListFn>(ap::FN_PINLIST)(reinterpret_cast<uint8_t*>(map) + ap::MAP_PINS);
        if (!PtrOk(list)) continue;
        if (on)
        {
            uint32_t name = 0;
            ap::Fn<ap::NameHashFn>(ap::FN_NAMEHASH)(&name, cfg.sites_pin);
            ap::Fn<ap::PinAddFn>(ap::FN_PINADD)(list, veh, false, &name);
        }
        else if (void* pin = ap::Fn<ap::PinFindFn>(ap::FN_PINFIND)(list, veh))
            ap::Fn<ap::PinDelFn>(ap::FN_PINDEL)(list, pin, 0);
    }
    s.pinned = on;
}

static void FadeOut(Slot& s, int idx, void* veh, const Config& cfg, const char* why)
{
    Pin(s, veh, false, cfg);
    if (uint8_t* ud = VehicleUserData(veh))
    {
        struct { uint32_t a; uint8_t flags; } o = { 0, 4 };              // VehicleAction_Destroy #FadeOut
        reinterpret_cast<ap::DestroyFn>((*reinterpret_cast<void***>(ud))[13])(ud, &o);
    }
    Log(">>> sites[%d]: the %s fades out - %s\n", idx + 1, sitem::KindName(s.stand.kind), why);
    ap::DropRef(s.ref);
}

// The engine side of sites_model.h's actions, for one stand.
struct World
{
    void* pm; void* lvl; const Config* cfg; Slot* s; int idx; bool* spawned;
    bool Known(const char* name, int* model, int* cls, int* type)
    {
        *model = wd::g_modelId(*reinterpret_cast<void**>(g_base + wd::RVA_MODEL_REG), name);
        return *model >= 0 && wd::g_classType(wd::H(name), cls, type);
    }
    // The stand's model; when the game does not know it, or it failed to spawn for good here, the
    // kind's fall-back (sitem::FallbackModel). Logged once per name.
    bool Resolve()
    {
        const sitem::Stand& st = s->stand;
        const char* fb = sitem::FallbackModel(st.kind);
        int model = -1, cls = -1, type = -1;
        const char* use = st.model;
        const char* why = nullptr;
        if (s->bad) why = "it could not be spawned";
        else if (!Known(use, &model, &cls, &type)) why = "the game has no such model";
        if (why)
        {
            if (!fb[0] || _stricmp(fb, st.model) == 0 || !Known(fb, &model, &cls, &type))
            {
                Log(">>> sites[%d]: %s - %s, and nothing stands in for a %s: check [sites] stand%d\n", idx + 1, st.model, why,
                    sitem::KindName(st.kind), idx + 1);
                return false;
            }
            use = fb;
        }
        if (!s->said || _stricmp(s->name, use) != 0)
        {
            if (why) Log(">>> sites[%d]: %s wanted, but %s - %s stands there instead\n", idx + 1, st.model, why, use);
            else Log(">>> sites[%d]: %s (%s, class %d type %d) for (%.1f, %.2f, %.1f) heading %.0f%s%s\n", idx + 1, use,
                     sitem::KindName(st.kind), cls, type, st.pos[0], st.pos[1], st.pos[2], st.heading, st.label[0] ? " - " : "",
                     st.label);
            strcpy_s(s->name, use); s->said = true;
        }
        s->model = model; s->cls = cls; s->type = type;
        g_owner = wd::H("lcudrive_sites");
        return true;
    }
    void BeginSpawn() { s->spawnPm = pm; }
    int Spawn(float t)
    {
        if (!spt::Mine(sitem::kTurnSites)) return 1;                     // not this update's spawner: again next tick
        const sitem::Stand& st0 = s->stand;
        if (!s->sendSet) { sitem::HeadingDir(st0.heading, s->send); s->sendSet = true; }
        alignas(16) float p[4] = { st0.pos[0], st0.pos[1] + sitem::SpawnLift(st0.kind), st0.pos[2], 1.0f };
        alignas(16) float f[4] = { s->send[0], 0.0f, s->send[1], 0.0f };
        int st = -1;
        ap::WeakRef out = {};
        *spawned = true;                                                 // this tick's one SpawnAIVehicle call
        ap::Fn<ap::SpawnFn>(ap::FN_SPAWN)(pm, &out, lvl, s->cls, s->type, p, f, &st, true, g_owner);
        if (st == 0 && PtrOk(out.ctl) && out.ctl[1] > 0)
        {
            s->ref = out;
            s->preloaded = false;                                        // the spawn released it itself [P]
            void* v = Veh(*s);
            Log(">>> sites[%d]: %s (class %d type %d) spawned at (%.1f, %.2f, %.1f) heading %.0f after %.1f s, veh=%p, "
                "vehicle class %d\n", idx + 1, s->name, s->cls, s->type, st0.pos[0], st0.pos[1], st0.pos[2], st0.heading, t, v,
                v ? At<int32_t>(v, eng::VEH_CLASS) : -1);
            return 0;
        }
        if (PtrOk(out.ctl)) ap::DropRef(out);                            // a ref handed back without a vehicle: not leaked
        if (st == 0) st = -2;                                            // "spawned" but no live ref: a failure
        if (st == 1) s->preloaded = true;                                // the streaming took a preload under g_owner
        // The stand's own model ending in a failure of its own (never streams in, bad class / type, no
        // definition, create failed) is not asked for again here: the fall-back stands in (Resolve).
        const bool retry = sitem::RetryStatus(st) && t < 20.0f;
        if (!retry && st != 2 && st != 8 && st != 7 && !s->bad && _stricmp(s->name, s->stand.model) == 0 &&
            sitem::FallbackModel(s->stand.kind)[0] && _stricmp(sitem::FallbackModel(s->stand.kind), s->stand.model) != 0)
        {
            Log(">>> sites[%d]: %s could not be spawned (status %d after %.1f s) - %s will stand there instead\n", idx + 1,
                s->name, st, t, sitem::FallbackModel(s->stand.kind));
            s->bad = true; s->fellBack = true;
        }
        return st;
    }
    void AbandonSpawn() { FreePreload(*s); s->spawnPm = nullptr; }
    void Turn(int attempt)
    {
        // the facing watchdog: this vehicle's nose against what was sent -> the next facing to send
        void* v = Veh(*s);
        float got[2], next[2], want[2];
        sitem::HeadingDir(s->stand.heading, want);
        if (v && ap::Nose(v, got) && apm::NextFacing(attempt, s->send, got, want, next))
        {
            Log(">>> sites[%d]: it faces (%.2f, %.2f), sent (%.2f, %.2f), the stand is (%.2f, %.2f) - next time sent "
                "(%.2f, %.2f) (%s)\n", idx + 1, got[0], got[1], s->send[0], s->send[1], want[0], want[1], next[0], next[1],
                attempt == 0 ? "as a turn" : "as a mirror");
            s->send[0] = next[0]; s->send[1] = next[1];
        }
    }
    void Park(bool on)
    {
        void* v = Veh(*s);
        if (!v || s->stand.kind == sitem::kHeli) return;                 // a helicopter is never parked (sites_model.h ParkKind)
        if (on && !sitem::ParkKind(s->stand.kind, cfg->sites_park)) return;   // (taking the parking off always runs)
        ap::Park(v, on);
    }
    void Pin(bool on) { if (void* v = Veh(*s)) si::Pin(*s, v, on, *cfg); else if (!on) s->pinned = false; }
    // A waiting helicopter: its pilot data from "flying" (2, how it is born) to "parked" (0). Only a
    // HoveringAircraft / FireHelicopter user data (heli.h's own test), only state 2, only with nobody in it.
    void Settle()
    {
        void* v = Veh(*s);
        if (!v) return;
        const int vc = At<int32_t>(v, eng::VEH_CLASS);
        const int k = s->stand.kind;
        if ((k == sitem::kBoat) != (vc == 3) || (k == sitem::kHeli && vc != 6))
            Log(">>> sites[%d]: %s is vehicle class %d (3 = boat, 6 = helicopter / hover, 1 car, 2 bike, 4 aircraft) but stand%d "
                "says %s - fix the kind in [sites]\n", idx + 1, s->name, vc, idx + 1, sitem::KindName(k));
        if (k != sitem::kHeli) return;
        uint8_t* ud = hl::UserData(v);
        const int st = hl::UdState(ud);
        const bool settle = ud && st == 2 && cfg->sites_heli_settle && v != ::PlayerVehicle() && v != ::Player2Vehicle();
        Log(">>> sites[%d]: helicopter pilot data %s, state %d%s\n", idx + 1,
            ud ? "HoveringAircraft (heli.h flies it once a player does)" : "is NOT a HoveringAircraft - heli.h will not fly it", st,
            settle ? " -> 0 (parked: it settles on its stand)" : "");
        if (settle) At<int32_t>(ud, eng::UD_STATE) = 0;
    }
    void Fade(const char* why)   { if (void* v = Veh(*s)) FadeOut(*s, idx, v, *cfg, why); else ap::DropRef(s->ref); s->pinned = false; }
    void Disown(const char* why)
    {
        Log(">>> sites[%d]: %s - the %s stays with its player, no longer ours\n", idx + 1, why, sitem::KindName(s->stand.kind));
        ap::DropRef(s->ref); s->pinned = false;
    }
    void Dropped()               { ap::DropRef(s->ref); s->pinned = false; }
    void Say(const char* fmt, ...)
    {
        // the model's lines begin ">>> sites: " - the stand's number goes in
        char buf[512];
        va_list a; va_start(a, fmt);
        _vsnprintf_s(buf, sizeof(buf), _TRUNCATE, fmt, a);
        va_end(a);
        static const char kPre[] = ">>> sites: ";
        if (strncmp(buf, kPre, sizeof(kPre) - 1) == 0) Log(">>> sites[%d]: %s", idx + 1, buf + sizeof(kPre) - 1);
        else Log("%s", buf);
    }
};

static sitem::Params ParamsFor(const Slot& s, const Config& cfg)
{
    sitem::Params p;
    p.nearU = cfg.sites_near; p.farU = cfg.sites_far; p.leaveU = cfg.sites_leave;
    p.park = sitem::ParkKind(s.stand.kind, cfg.sites_park);
    return p;
}

// What the model needs to know about one stand's vehicle right now (the parts Release needs too).
static void Look(const Slot& s, void* veh, const float pp[2][3], const bool ppOk[2], const sitem::Params& p, sitem::Facts& f)
{
    f.best = 1e9f; f.nearVeh = 1e9f;
    float vp[3] = {};
    const bool vOk = veh && ap::VehPos(veh, vp);
    for (int k = 0; k < 2; ++k) if (ppOk[k])
    {
        f.best = std::fmin(f.best, ap::Dist2(pp[k], s.stand.pos));
        if (vOk) f.nearVeh = std::fmin(f.nearVeh, ap::Dist2(pp[k], vp));
    }
    f.haveVeh = veh != nullptr;
    f.aboard = veh && (veh == ::PlayerVehicle() || veh == ::Player2Vehicle());
    f.onStand = vOk && ap::Dist2(vp, s.stand.pos) < p.onStandU;
    f.sunk = vOk && IsFin(vp[1]) && vp[1] < s.stand.pos[1] - p.sunkU;
    if (veh && !vOk) f.nearVeh = 0.0f;                                   // where it is can't be read: treat it as beside a player
}

static void ReleaseSlot(Slot& s, int idx, const Config& cfg, const char* why)
{
    bool spawned = false;
    World w = { nullptr, nullptr, &cfg, &s, idx, &spawned };
    sitem::Facts f = {};
    float pp[2][3] = {}; bool ppOk[2] = { false, false };
    for (int i = 0; i < 2; ++i) ppOk[i] = ap::PlayerPos(i, pp[i]);
    const sitem::Params p = ParamsFor(s, cfg);
    Look(s, Veh(s), pp, ppOk, p, f);
    sitem::Release(w, s.mem, p, f, why);
    FreePreload(s);
    s.spawnPm = nullptr; s.pinned = false;
    s.sendSet = false; s.bad = false; s.said = false; s.name[0] = 0;
}

static bool Busy(const Slot& s) { return s.mem.state != sitem::kIdle || PtrOk(s.ref.ctl) || s.preloaded; }

// Everything back, once (unhook, or after a fault in this code), for every stand.
static void Release(const Config& cfg, const char* why)
{
    if (g_released) return;
    g_released = true;
    for (int i = 0; i < sitem::kMaxSlots; ++i) if (Busy(g_slot[i])) ReleaseSlot(g_slot[i], i, cfg, why);
    if (g_afterRelease) g_afterRelease(cfg, why);                         // [marina1]
}

// Port Atlas as a place: a log line when a player arrives or leaves (the radio's district is
// lcudrive.cpp's DistrictAt, from the same box).
static void Place(const Config& cfg, const float pp[2][3], const bool ppOk[2])
{
    if (!cfg.sites_atlas) { g_there[0] = g_there[1] = false; return; }
    const sitem::Box box = sitem::MakeBox(cfg.sites_atlas_x0, cfg.sites_atlas_x1, cfg.sites_atlas_z0, cfg.sites_atlas_z1);
    for (int i = 0; i < 2; ++i)
    {
        if (!PtrOk(wd::RawPlayer(i))) { g_there[i] = false; continue; }  // no such player: nobody to announce
        const int ev = sitem::Presence(box, 10.0f, ppOk[i], pp[i][0], pp[i][2], g_there[i]);
        if (ev > 0) Log(">>> sites: player %d arrived at Port Atlas (%.0f, %.1f, %.0f)\n", i + 1, pp[i][0], pp[i][1], pp[i][2]);
        else if (ev < 0) Log(">>> sites: player %d left Port Atlas (%.0f, %.1f, %.0f)\n", i + 1, pp[i][0], pp[i][1], pp[i][2]);
    }
}

// once per world update, after cPopulationManager::Update
static void Tick(void* pm, float dt, const Config& cfg, bool on)
{
    if (g_released) return;
    if (g_stopReq) { Release(cfg, "lcudrive unhooked"); return; }
    if (!g_checked)
    {
        g_checked = true;
        int j = -1;
        const int i = sitem::FirstCrowded(cfg.sites_stand, sitem::kMaxSlots, 4.0f, &j);
        if (i >= 0) Log(">>> sites: stand%d and stand%d are less than 4 u apart - their vehicles will be put into each other\n", i + 1, j + 1);
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
        Place(cfg, pp, ppOk);
    }
    // A teleport = a player more than 12 u from where he was on the last tick (no vehicle of this game
    // covers that in one frame): hold every stand for 4 s.
    if (g_settle > 0.0f) g_settle -= dt;
    for (int i = 0; i < 2; ++i)
    {
        if (ppOk[i] && g_prevOk[i] && ap::Dist2(pp[i], g_prevPos[i]) > 12.0f)
        {
            if (g_settle <= 0.0f) Log(">>> sites: player %d jumped %.0f u (a teleport) - stands hold for 4 s\n", i + 1, ap::Dist2(pp[i], g_prevPos[i]));
            g_settle = 4.0f;
        }
        g_prevOk[i] = ppOk[i];
        if (ppOk[i]) { g_prevPos[i][0] = pp[i][0]; g_prevPos[i][1] = pp[i][1]; g_prevPos[i][2] = pp[i][2]; }
    }
    const bool live = on && cfg.sites_enable;
    void* vehs[sitem::kMaxSlots]; float vpos[sitem::kMaxSlots][3]; bool vposOk[sitem::kMaxSlots];
    for (int i = 0; i < sitem::kMaxSlots; ++i)
    {
        vehs[i] = Veh(g_slot[i]);
        vposOk[i] = vehs[i] && ap::VehPos(vehs[i], vpos[i]);
    }
    bool spawned = false;                    // a SpawnAIVehicle call was made this tick
    for (int i = 0; i < sitem::kMaxSlots; ++i)
    {
        Slot& s = g_slot[i];
        static const sitem::Stand kNoStand = {};
        // slots 13..24 are the life-spot data's parked cars: there only with [spots] enable and cars
        // [diycars1] the DIY garage's slots (cfg.diy_first ..) are not the life-spot cars: [spots] does not gate them
        const bool spotCar = i >= sitem::kMaxStands && i < cfg.diy_first;
        const sitem::Stand& want = spotCar && !(cfg.spots_enable && cfg.spots_cars) ? kNoStand : cfg.sites_stand[i];
        if (!sitem::SameStand(s.stand, want))
        {
            // the ini's line for this stand changed (or went): what stood for the old line goes first
            if (Busy(s)) ReleaseSlot(s, i, cfg, "this stand was changed or switched off in [sites]");
            if (Busy(s)) continue;                                       // (cannot happen: Release resets it)
            s.stand = want; s.sendSet = false; s.bad = false; s.said = false; s.name[0] = 0;
            sitem::Reset(s.mem);
        }
        if (!s.stand.on) continue;
        void* veh = vehs[i];
        const sitem::Params p = ParamsFor(s, cfg);
        sitem::Facts f = {};
        Look(s, veh, pp, ppOk, p, f);
        f.dt = dt;
        f.on = live;
        f.world = world;
        f.samePm = pm == s.spawnPm;
        f.mission = mission;
        f.settling = g_settle > 0.0f;
        for (int k = 0; k < 2; ++k) if (ppOk[k] && ap::Dist2(pp[k], s.stand.pos) < p.clearU) f.someoneClose = true;
        for (int j = 0; j < sitem::kMaxSlots; ++j)                      // another stand's vehicle left on this one
            if (j != i && vposOk[j] && ap::Dist2(vpos[j], s.stand.pos) < p.clearU) f.someoneClose = true;
        if (g_extraNear && g_extraNear(s.stand.pos, p.clearU)) f.someoneClose = true;   // [marina1]
        f.speed = veh ? ap::Speed(veh) : 0.0f;
        int hc = 1, hm = 1;
        f.wrecked = veh && vd::Health(veh, hc, hm) && hc < 1;
        float nose[2], dir[2];
        sitem::HeadingDir(s.stand.heading, dir);
        if (veh && ap::Nose(veh, nose)) { f.facingKnown = true; f.facingDot = nose[0] * dir[0] + nose[1] * dir[1]; }
        f.hold = spawned;
        for (int j = 0; j < sitem::kMaxSlots; ++j) if (j != i && g_slot[j].mem.state == sitem::kSpawning) f.hold = true;
        if (g_extraBusy && g_extraBusy()) f.hold = true;                     // [marina1]
        World w = { pm, lvl, &cfg, &s, i, &spawned };
        s.fellBack = false;
        sitem::Tick(w, s.mem, p, f);
        if (s.fellBack && s.mem.state == sitem::kIdle && s.mem.cool > 2.0f) s.mem.cool = 2.0f;
        vehs[i] = Veh(s);                                                // faded / spawned this tick
        vposOk[i] = vehs[i] && ap::VehPos(vehs[i], vpos[i]);
    }
    if (g_after) g_after(pm, lvl, dt, cfg, on, world, mission, pp, ppOk, spawned, g_settle > 0.0f);   // [marina1]
}

} // namespace si
