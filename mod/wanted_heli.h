// =============================================================================================
//  wanted_heli.h - the police helicopter (2026-11-13: with the police boats, for the
//  higher wanted levels). Part of wanted.h (namespace wd); the flying model is
//  copter_model.h (harness-tested). Notes\33 §3 and §7.3 planned it; Notes\43 has the research of
//  2026-11-13 (the addresses, the recipe, its two corrections) and the review that followed.
//
//  Nothing in the game AI-flies a helicopter [P]: no script follows or circles anything with one,
//  the traffic pursuit only turns its own traffic into pursuers and ChaseTask has no Hover branch,
//  and the engine's servo would fly a Police_Responder at 6 u/s half a unit off the ground. So:
//    * the director (HeliTick, from cPopulationManager::Update - the only place the game's spawn
//      calls are safe, Notes\33 §8) spawns Police_Responder (Enforcer 9,0: a Hover helicopter with
//      a "Searchlight" lamp and siren lamps [P]) with the engine's own SpawnAIVehicle 0x1406FE560 -
//      SAction_CreateAIApiVehicle's call, which streams the model in itself (status 1 until it's
//      loaded) and registers it as a script vehicle, so traffic culling never takes it [P]. One at
//      `heli_level` (4), two from `heli_level2` (5). Seats locked (SetLocked's +0x34 word), pilot
//      state forced to 2 = flying (the only state the vehicle side keeps "occupied", Notes\26),
//      siren on (TurnOnSirene 0x14019B020: ud+0x5C4), forced max detail (veh+0xFC7 |= 0x20);
//    * HoverThunk (lcudrive.cpp) flies it - HeliFly - the way it flies the player's helicopter
//      (Notes\28): velocities taken before ApiVehicleHover::Update, copter_model.h's written after;
//    * the level drops, the chase ends, you're busted, wanted mode switches off: it climbs and
//      flies off, then fades out (ud vt[13] with FadeOut, VehicleAction_Destroy's call) [P]; a story
//      mission, `[`, the unhook or the city's traffic switched off: it fades out at once; a new
//      world: forgotten, untouched. A fade-out that doesn't take in 10 s (an airborne Hover's is
//      [U]) is let go of with everything we did to it undone (HeliLetGo);
//    * while one has you in sight (copter::InSight: `heli_sight`, horizontally) it counts as a
//      pursuer - but alone, once the cars have lost you, only for `heli_hold` seconds
//      (wanted_model.h HeliHold: it flies 36 u/s and no car does 20, and there's no line-of-sight
//      test). It sees your crimes too. It never arrests anyone (no officer comes down from it: the
//      visible-cause rule, Notes\38);
//    * at night its searchlight: a private copy of the lamp definition (lights.h's method) with
//      dynamics on, aimed at you by writing the copy's LocatorOffset, which the light update
//      multiplies into the lamp's world matrix (0x1404D7E10..0x1404D7EF9) [P].
//  Every game call here is behind its own fault switch (CopFault): an exception turns the police
//  helicopters off for the session, the rest of wanted mode carries on. The searchlight has one more
//  (BeamFault) of its own. A fault in the rest of wanted mode (lcudrive.cpp's WantedFault) sends
//  them off and lets go of them at once (RunHeliAbandon): nothing would fly or watch them after.
// =============================================================================================
#pragma once
#include "copter_model.h"

struct WRef { void** cell; volatile LONG* ctl; };     // the game's weak handle: ctl[0] weak count, ctl[1] alive

// SpawnAIVehicle(pm, out, level, cls, type, pos4, facing4, status*, registerAsScriptVehicle, ownerHash):
// SAction_CreateAIApiVehicle's own call (0x140106566) - arguments checked against it [P].
typedef void (__fastcall* SpawnFn)(void* pm, WRef* out, void* lvl, int cls, int type, const float* pos4,
                                   const float* face4, int* status, bool script, uint32_t owner);
typedef void (__fastcall* SirenFn)(uint8_t* ud, bool on);
typedef bool (__fastcall* JointMtxFn)(void* veh, int joint, float* local16, float* world16, bool apply);
// The streaming manager's "this owner no longer needs the model" (the spawn's own call at 0x1406FE72C;
// the owner is compared as a full 64-bit register at 0x1406FAB9D, so it goes in zero-extended).
typedef void (__fastcall* PreloadRelFn)(void* scm, int model, uint64_t owner);

static const uintptr_t FN_SPAWN_AIVEH = 0x6FE560, FN_SIREN = 0x19B020, FN_JOINT_MTX = 0x4A8B20;
static const uintptr_t FN_PRELOAD_REL = 0x6FAAE0, RVA_STREAMING = 0x17EBFA0;
// The game's "paused" byte (lcudrive.cpp gsnd::RVA_PAUSED - the radio's clocks stop on it too):
// vehicle updates stop in the menus, so the "not flown" watchdog doesn't count that time.
static const uintptr_t RVA_PAUSED_BYTE = 0x1A99B95;
// The light update casts a lamp's spot only when its cookie isn't this "no texture" resource and its
// definition's Disable Dynamics is 0 (0x1404D82B0..0x1404D82CB) [P].
static const uintptr_t RVA_NO_COOKIE = 0x1C27CF8;
static const uintptr_t LI_JOINT = 0xA8, LI_NODE = 0x70, AD_LOCATOR = 0x118, AD_ASPECT = 0xED0;
static const int       kMaxHelis = 2;
static const float     kHeliSpawnDist = 85.0f, kHeliSpawnUp = 30.0f;   // out of sight, behind you
static const float     kHeliLostDist = 250.0f;                        // u: it has lost you
static const float     kHeliStaleS = 3.0f;                            // s of game time unflown = replaced

// Why HeliFly didn't fly one this frame (the log): 0 = it did.
enum HeliGate : uint8_t { kFlown, kNoUpdate, kAsleep, kDestroyed, kNotSpawned, kParked, kNoBody, kInvalid };
static const char* const kGateText[] = { "flown", "no hover update reached it", "asleep (sim LOD 10)", "destroyed",
                                         "not spawned yet", "parked / disabled", "no body", "the autopilot's output was invalid" };

struct HeliSlot
{
    WRef     ref;             // our weak count on it (SpawnAIVehicle's out)
    void*    veh;             // *ref.cell, checked against ref every time it's used
    uint8_t* ud;              // HoveringAircraft user data
    int      id;              // a serial number for the log
    float    side;            // -1 / +1: which side it trails on
    float    height;          // u above you (the level's)
    bool     leaving, fading, reset, fadeAgain, flown, seatLocked;
    float    leaveT, fadeT, lostT, age;
    float    sinceFly;        // s of game time (the director's dt, not while paused) since HeliFly last flew it
    uint32_t seatWas;         // the seat's lock word before we locked it (given back when we let go)
    uint8_t  gate;            // HeliGate: why the last hover frame wasn't flown
    // the flight (HoverThunk's side)
    copter::State st;
    int      frames, lod, udFix;
    float    dist, distH, above, hag, speed;
    uint8_t  mode, stuck, respawn;
    // the searchlight
    void*    lamp;
    void*    lampOrig;
    int      lampTries, litFrames, cookieTries;
    bool     lit, calSaid;
    float    aim2[3];         // the row 2 we wrote last frame (the calibration line compares the engine's)
};
static HeliSlot g_heli[kMaxHelis] = {};
static int      g_nHeliLive = 0;          // slots holding a helicopter (HoverThunk's quick test)
static int      g_heliWatch = 0;          // ...with you in sight right now (copter::InSight)
static float    g_heliNear = 1e9f;        // u, horizontally, to the nearest one (the log)
static wanted::HeliHold g_heliHold = {};  // how long they alone have held you (wanted_model.h)
static volatile LONG g_heliNews = 0;      // the radio's helicopter bulletins (lcudrive.cpp -> rd::g_heliNews)
static bool     g_heliAnnounced = false;  // ...this chase's has gone out
static bool     g_heliOff = false;        // the model was refused: no helicopters (until heli_model changes)
static int      g_heliNoFly = 0;          // helicopters replaced without ever having been flown
static bool     g_heliTypeOk = false;
static int      g_heliCls = -1, g_heliType = -1, g_heliSerial = 0, g_heliModel = -1;
static uint32_t g_heliModelHash = 0;      // the heli_model the lookup (and a refusal) was made for
static bool     g_heliPreload = false;    // a spawn said "streaming in" (status 1): the model is held for us
// The next spawn - the director's, not a slot's: one spawn is in progress at a time, and a cool-down
// (a helicopter that lost you, one replaced) keeps the next one waiting whichever slot is free.
static uint32_t g_heliRetryMs = 0, g_heliCoolMs = 0, g_heliWaitMs = 0, g_heliWaitLog = 0;
static float    g_heliOffT = 0.0f;        // s the player's world has had its traffic off (Tick can't run)
static volatile bool g_copFault = false, g_copCleanup = false, g_beamFault = false;

struct alignas(16) Beam { uint8_t data[lights::kAttSize]; };   // the searchlights' private lamp definitions:
static Beam g_beam[kMaxHelis];                                   // static, so a lamp pointed at one can't dangle

static bool RefAlive(const WRef& r) { return PtrOk(r.cell) && PtrOk(const_cast<LONG*>(r.ctl)) && r.ctl[1] != 0; }

// A GetTickCount stamp passed (0 = none set). Signed, so the 49-day wrap doesn't hold it forever.
static bool Due(uint32_t now, uint32_t t) { return t == 0 || static_cast<int32_t>(now - t) >= 0; }

// Our weak count back (the game's own release, as at the end of 0x140106350).
static void RefDrop(WRef& r)
{
    LONG* rc = const_cast<LONG*>(r.ctl);
    if (PtrOk(rc) && InterlockedDecrement(rc) <= 0)
        Fn<RefFreeFn>(FN_REFFREE)(Fn<RefPool1Fn>(FN_REFPOOL1)(Fn<RefPoolFn>(FN_REFPOOL0)()), rc, 0);
    r = WRef{};
}

// HoverThunk asks this of every helicopter, every frame: a pointer compare, nothing read.
static bool HeliOurs(const void* veh)
{
    if (!g_nHeliLive || g_copFault || !veh) return false;
    for (const HeliSlot& h : g_heli) if (h.veh == veh) return true;
    return false;
}

static int HeliIndex(const void* veh)
{
    for (int i = 0; i < kMaxHelis; ++i) if (g_heli[i].veh && g_heli[i].veh == veh) return i;
    return -1;
}

// ...and is it still the one we spawned (not a new vehicle at a freed one's address)? Reads the weak
// handle we hold a count on; lcudrive.cpp calls it under CopFault.
static bool HeliLive(const void* veh)
{
    const int i = HeliIndex(veh);
    return i >= 0 && RefAlive(g_heli[i].ref) && *g_heli[i].ref.cell == veh;
}

static void CountLive()
{
    int n = 0;
    for (const HeliSlot& h : g_heli) if (h.veh) ++n;
    g_nHeliLive = n;
}

// heli.h's boundary-mask table (8 entries, shared with the player's helicopter): an entry for a
// helicopter that's gone is freed without touching the vehicle, so the table never fills up.
static void MaskForget(const void* veh)
{
    if (!veh) return;
    for (hl::MaskRec& r : hl::g_mask) if (r.veh == veh) r.veh = nullptr;
}

// The searchlight's shipped definition back (by day, before a fade-out, on a fault).
static void LampBack(int i)
{
    HeliSlot& h = g_heli[i];
    if (h.lamp && h.lampOrig && At<void*>(h.lamp, lights::LI_DEF) == static_cast<void*>(g_beam[i].data))
        At<void*>(h.lamp, lights::LI_DEF) = h.lampOrig;
    h.lit = false;
}

// Forget slot i: our weak count back, the mask entry freed, the slot empty. For a helicopter that's
// gone; one that's still there goes through HeliLetGo.
static void HeliRelease(int i)
{
    HeliSlot& h = g_heli[i];
    MaskForget(h.veh);
    RefDrop(h.ref);
    h = HeliSlot{};
    CountLive();
}

// Let go of one that's still there (a fade-out that didn't take, a player in it, a fault): what we
// did to it undone first - the lamp, the siren, the seats' lock word as it was, and the boundary bit
// (through heli.h's own record: RunHeliPre only gives a bit back while it has the record, and
// MaskForget alone would drop the record with the bit still set) - then the slot freed. The engine
// has it from here: a script vehicle nothing flies.
static void HeliLetGo(int i)
{
    HeliSlot& h = g_heli[i];
    if (h.veh && RefAlive(h.ref) && *h.ref.cell == h.veh)
    {
        LampBack(i);
        if (h.ud)
        {
            reinterpret_cast<SirenFn>(g_base + FN_SIREN)(h.ud, false);
            uint8_t* seat = At<uint8_t*>(h.ud, eng::UD_SEATREF);
            if (h.seatLocked && PtrOk(seat)) At<uint32_t>(seat, eng::UD_SEATLOCK) = h.seatWas;
        }
        hl::BoundaryMask(h.veh, false);
    }
    HeliRelease(i);
}

// Fade it out (VehicleAction_Destroy's call: ud vt[13] {0, FadeOut}, wanted_officers.h FadeOutCar). The
// slot keeps its weak count until the game has really removed it - flown on its way out meanwhile -
// so the log can say whether the fade-out took (an airborne Hover's fade is [U]).
static void HeliDestroy(int i, const char* why)
{
    HeliSlot& h = g_heli[i];
    if (!h.veh || h.fading) return;
    if (RefAlive(h.ref) && *h.ref.cell == h.veh && h.ud)
    {
        LampBack(i);
        reinterpret_cast<SirenFn>(g_base + FN_SIREN)(h.ud, false);
        FadeOutCar(h.ud);
        h.fading = true; h.leaving = true; h.fadeT = 0.0f;
        Log(">>> wanted: police helicopter #%d fades out (%s), %.0f u from you\n", h.id, why, h.distH < 1e8f ? h.distH : -1.0f);
    }
    else
    {
        Log("    wanted: police helicopter #%d is gone already (%s)\n", h.id, why);
        HeliRelease(i);
    }
}

// A fading one (true; false = not fading): gone - the slot freed; still there 5 s on - asked again;
// 10 s on - let go of. From the director's tick and from the stop path alike, so a fade-out that
// doesn't take is never flown away for good (HoverThunk flies a fading one off while it's ours).
static bool HeliFadeWatch(int i, float dt)
{
    HeliSlot& h = g_heli[i];
    if (!h.veh || !h.fading) return false;
    if (!RefAlive(h.ref) || *h.ref.cell != h.veh)
    {
        Log("    wanted: police helicopter #%d has faded out\n", h.id);
        HeliRelease(i);
        return true;
    }
    h.fadeT += dt;
    if (h.fadeT > 5.0f && !h.fadeAgain)
    {
        h.fadeAgain = true;                                         // the fade-out of an airborne Hover is [U]
        Log("    wanted: police helicopter #%d still there 5 s after its fade-out - asked again\n", h.id);
        FadeOutCar(h.ud);
    }
    else if (h.fadeT > 10.0f)
    {
        Log("    wanted: police helicopter #%d still there 10 s after its fade-out - let go of it (seats, siren, "
            "boundary bit back)\n", h.id);
        HeliLetGo(i);
    }
    return true;
}

static void HeliPreloadRelease(const char* why);

// A new world (a level load, the station trip): the old one's vehicles are gone with it. Forget them
// without touching them - the weak counts are left (a few bytes; touching the old world's blocks
// could fault), the way the officers and the borrowed cookie are forgotten. The model's streaming
// hold is not the world's, though: the streaming manager is the game's (*0x1417EBFA0), so a hold a
// spawn left would outlive the world - it's let go of (0x1406FAAE0 looks our owner up per model and
// does nothing without a match, 0x1406FAB90..0x1406FAC0A [P]).
static void HeliForgetAll(const char* why)
{
    bool any = false;
    for (int i = 0; i < kMaxHelis; ++i)
    {
        if (!g_heli[i].veh) continue;
        any = true;
        MaskForget(g_heli[i].veh);
        g_heli[i] = HeliSlot{};
    }
    CountLive();
    g_heliWatch = 0; g_heliNear = 1e9f; g_heliAnnounced = false;
    wanted::ResetHeliHold(g_heliHold);
    g_heliRetryMs = g_heliCoolMs = g_heliWaitMs = 0;
    g_heliOffT = 0.0f;
    if (any) Log("    wanted: %s - the police helicopters are forgotten\n", why);
    HeliPreloadRelease(why);                   // (last: the slots are empty whatever it does)
}

// Everything down now: wanted mode off, `[`, the unhook, a story mission, the traffic off. The ones
// already fading are watched (dt: the time since the last call; 0 from inside the director's tick,
// which watched them itself).
static void HeliStopAll(const char* why, float dt)
{
    HeliPreloadRelease(why);
    for (int i = 0; i < kMaxHelis; ++i)
    {
        if (!g_heli[i].veh) continue;
        if (HeliFadeWatch(i, dt)) continue;
        HeliDestroy(i, why);
    }
    g_heliWatch = 0; g_heliNear = 1e9f; g_heliAnnounced = false;
    wanted::ResetHeliHold(g_heliHold);
}

// The suspect's velocity (his vehicle's body; on foot 0), and whether he flies (class 4 aircraft /
// 6 Hover: helicopters and the planes, engine.h).
static bool SuspectMotion(float v[3])
{
    v[0] = v[1] = v[2] = 0.0f;
    void* you = PlayerVehicle();
    if (!you) return false;
    if (void* b = BodyOf(you)) for (int k = 0; k < 3; ++k) { const float x = At<float>(b, eng::BODY_LINVEL + 4 * k); v[k] = IsFin(x) ? x : 0.0f; }
    const int32_t cls = At<int32_t>(you, eng::VEH_CLASS);
    return cls == 6 || cls == 4;
}

// The searchlight lamp on this vehicle: the lamp instance whose definition is named "Searchlight".
static void* FindLamp(void* veh, void** orig)
{
    void* inst = At<void*>(veh, lights::VEH_LIGHTS);
    for (int n = 0; PtrOk(inst) && n < lights::kMaxInst; ++n, inst = At<void*>(inst, lights::LI_NEXT))
    {
        void* def = At<void*>(inst, lights::LI_DEF);
        for (int i = 0; i < kMaxHelis; ++i) if (def == static_cast<void*>(g_beam[i].data)) def = nullptr;
        if (!PtrOk(def)) continue;
        if (_strnicmp(reinterpret_cast<const char*>(def) + lights::AD_NAME, "Searchlight", 12) == 0) { *orig = def; return inst; }
    }
    return nullptr;
}

// ---------------------------------------------------------------------------------------------
//  The director: after cPopulationManager::Update, from Tick.
// ---------------------------------------------------------------------------------------------
static const char* const kHeliOwner = "lcudrive_heli_spawn";

// A spawn that answered "streaming in" holds the model for our owner hash until a spawn of it
// succeeds (SpawnAIVehicle lets go of it then, 0x1406FE72C). Not wanted any more before that: let go
// of it ourselves, so a helicopter model nobody uses isn't kept in memory (the game runs close to its
// commit limit - the startup crash of 2026-11 was bad_alloc).
static void HeliPreloadRelease(const char* why)
{
    if (!g_heliPreload) return;
    g_heliPreload = false;
    void* scm = *reinterpret_cast<void**>(g_base + RVA_STREAMING);
    if (!PtrOk(scm) || g_heliModel < 0 || !At<uint64_t>(scm, 0xD8)) return;   // the spawn's own test (0x1406FE710)
    reinterpret_cast<PreloadRelFn>(g_base + FN_PRELOAD_REL)(scm, g_heliModel, static_cast<uint64_t>(H(kHeliOwner)));
    Log("    wanted: the police helicopter's model is let go of again (%s)\n", why);
}

static void HeliSpawn(const World& w, int i, const float yp[3], const Config& cfg, uint32_t now)
{
    if (!g_heliTypeOk)
    {
        const int model = g_modelId(*reinterpret_cast<void**>(g_base + RVA_MODEL_REG), cfg.wanted_heli_model);
        if (model < 0 || !g_classType(H(cfg.wanted_heli_model), &g_heliCls, &g_heliType))
        {
            g_heliOff = true;
            Log(">>> wanted: heli_model %s - not a model this game knows (model %d) - no police helicopters\n",
                cfg.wanted_heli_model, model);
            return;
        }
        g_heliTypeOk = true;
        g_heliModel = model;
        Log("    wanted: the police helicopter is %s (model %d, class %d type %d)\n", cfg.wanted_heli_model, model,
            g_heliCls, g_heliType);
    }
    // where from: behind you (against your way), out of sight; standing still, anywhere. A second one
    // (the other slot flying) comes in 70 degrees round from the first.
    float sv[3];
    SuspectMotion(sv);
    const float vh = std::sqrt(sv[0] * sv[0] + sv[2] * sv[2]);
    float bx, bz;
    if (vh > 3.0f) { bx = -sv[0] / vh; bz = -sv[2] / vh; }
    else { const float a = Rand01() * 6.2831853f; bx = std::cos(a); bz = std::sin(a); }
    if (g_heli[i ^ 1].veh) { const float c = std::cos(1.22f), s = std::sin(1.22f), x = bx * c - bz * s; bz = bx * s + bz * c; bx = x; }
    alignas(16) const float pos[4] = { yp[0] + kHeliSpawnDist * bx, yp[1] + kHeliSpawnUp, yp[2] + kHeliSpawnDist * bz, 1.0f };
    alignas(16) const float face[4] = { -bx, 0.0f, -bz, 0.0f };
    WRef out = {};
    int st = -1;
    reinterpret_cast<SpawnFn>(g_base + FN_SPAWN_AIVEH)(w.pm, &out, w.lvl, g_heliCls, g_heliType, pos, face, &st, true,
                                                       H(kHeliOwner));
    if (st == 1 || st == 2 || st == 8)
    {
        // 1 the model is streaming in, 2 the spawn queue is busy (or traffic was culled for room), 8 the
        // queue says later: the game's own scripts just ask again (status 5 = retry, 0x14010657B)
        if (st == 1) g_heliPreload = true;
        if (!g_heliWaitMs) g_heliWaitMs = now | 1u;
        g_heliRetryMs = (now + 250) | 1u;
        if (now - g_heliWaitLog > 5000)
        {
            g_heliWaitLog = now;
            Log("    wanted: police helicopter %d on its way (spawn status %d: %s)\n", i + 1, st,
                st == 1 ? "the model is streaming in" : st == 2 ? "no spawn room yet" : "the spawn queue is busy");
        }
        if (now - g_heliWaitMs > 20000)
        {
            Log(">>> wanted: police helicopter %d still not spawned after 20 s (status %d) - trying again in 15 s\n", i + 1, st);
            g_heliWaitMs = 0; g_heliCoolMs = (now + 15000) | 1u;
            HeliPreloadRelease("no spawn in 20 s");
        }
        return;
    }
    g_heliWaitMs = 0;
    if (st == 0) g_heliPreload = false;                                  // the spawn let go of it itself
    if (st != 0)
    {
        if (st == 3 || st == 5 || st == 0xE)
        {
            g_heliOff = true;
            Log(">>> wanted: the game refused %s (spawn status %d: %s) - no police helicopters (until heli_model "
                "changes)\n", cfg.wanted_heli_model, st, st == 3 ? "bad class/type" : st == 5 ? "no vehicle definition"
                : "not a valid model");
        }
        else
        {
            g_heliCoolMs = (now + 10000) | 1u;
            Log(">>> wanted: police helicopter %d - spawn status %d (%s) - again in 10 s\n", i + 1, st,
                st == 4 ? "no streaming system" : st == 7 ? "the vehicle manager is full" : st == 9 ? "the create failed" : "?");
        }
        return;
    }
    if (!RefAlive(out) || !PtrOk(*out.cell))
    {
        Log(">>> wanted: police helicopter %d - spawn status 0 but no vehicle - again in 10 s\n", i + 1);
        RefDrop(out);
        g_heliCoolMs = (now + 10000) | 1u;
        return;
    }
    void* veh = *out.cell;
    uint8_t* ud = hl::UserData(veh);
    if (!ud)
    {
        // not a HoveringAircraft: not something we can fly. Fade it out and stop trying.
        uint8_t* raw = VehicleUserData(veh);
        Log(">>> wanted: %s spawned as class %d with user data vtable %p - not a helicopter lcudrive can fly; "
            "no police helicopters (until heli_model changes)\n", cfg.wanted_heli_model, At<int32_t>(veh, eng::VEH_CLASS),
            raw ? reinterpret_cast<void*>(At<uintptr_t>(raw, 0) - g_base) : nullptr);
        if (raw) FadeOutCar(raw);
        RefDrop(out);
        g_heliOff = true;
        return;
    }
    HeliSlot& h = g_heli[i];
    h = HeliSlot{};
    h.ref = out; h.veh = veh; h.ud = ud;
    h.id = ++g_heliSerial;
    h.side = i ? 1.0f : -1.0f;
    h.reset = true;
    h.distH = h.dist = 1e9f;
    h.gate = kNoUpdate;
    At<uint8_t>(veh, eng::VEH_FLAGS7) |= 0x20;                         // forced max detail (MakePursuer's bit)
    uint8_t* seat = At<uint8_t*>(ud, eng::UD_SEATREF);
    h.seatLocked = PtrOk(seat);
    if (h.seatLocked)
    {
        h.seatWas = At<uint32_t>(seat, eng::UD_SEATLOCK);
        At<uint32_t>(seat, eng::UD_SEATLOCK) = 0xFFFFFFFFu;            // SetLocked (0x14011AD41): nobody boards it
    }
    At<int32_t>(ud, eng::UD_STATE) = 2;                                // flying
    if (cfg.wanted_heli_siren) reinterpret_cast<SirenFn>(g_base + FN_SIREN)(ud, true);
    h.lamp = FindLamp(veh, &h.lampOrig);
    CountLive();
    Log(">>> wanted: police helicopter #%d (%s, class %d type %d) takes off %.0f u from you, %.0f u up - seats %s, "
        "siren %s, searchlight lamp %s; veh %p, sim LOD %d, pilot state was set to 2\n", h.id, cfg.wanted_heli_model,
        g_heliCls, g_heliType, kHeliSpawnDist, kHeliSpawnUp, h.seatLocked ? "locked" : "NOT locked (no seat object)",
        cfg.wanted_heli_siren ? "on" : "off (ini)", h.lamp ? "found" : "not found yet", veh, At<int32_t>(veh, eng::VEH_SIMLOD));
}

static void HeliTick(const World& w, float dt, int level, const Config& cfg, bool haveYou, const float yp[3], bool mission)
{
    const uint32_t now = GetTickCount();
    const bool paused = *reinterpret_cast<const uint8_t*>(g_base + RVA_PAUSED_BYTE) != 0;
    // heli_model changed (Ctrl+Alt+R): the lookup - and a refusal - were for the old one
    const uint32_t mh = H(cfg.wanted_heli_model);
    if (mh != g_heliModelHash)
    {
        if (g_heliModelHash)
        {
            HeliPreloadRelease("heli_model changed");
            Log("    wanted: heli_model is %s now - the next police helicopter is one of those\n", cfg.wanted_heli_model);
        }
        g_heliModelHash = mh;
        g_heliTypeOk = false; g_heliModel = -1; g_heliOff = false; g_heliNoFly = 0;
    }
    if (g_heliCoolMs && Due(now, g_heliCoolMs)) g_heliCoolMs = 0;       // (passed stamps cleared: never stale)
    if (g_heliRetryMs && Due(now, g_heliRetryMs)) g_heliRetryMs = 0;
    // the helicopters there are: still alive? taken by a player? done fading? not flown? lost? stuck?
    for (int i = 0; i < kMaxHelis; ++i)
    {
        HeliSlot& h = g_heli[i];
        if (!h.veh) continue;
        h.age += dt;
        if (!paused) h.sinceFly += dt;
        if (!RefAlive(h.ref) || *h.ref.cell != h.veh)
        {
            Log("    wanted: police helicopter #%d %s\n", h.id, h.fading ? "has faded out" : "is gone (the game removed it)");
            HeliRelease(i);
            continue;
        }
        if (h.veh == ::PlayerVehicle() || h.veh == ::Player2Vehicle())
        {
            // its seats are locked, but if a player is in it anyway, it's theirs: no autopilot, no siren
            Log(">>> wanted: a player is in police helicopter #%d - it's theirs now (lcudrive lets go of it)\n", h.id);
            HeliLetGo(i);
            continue;
        }
        if (HeliFadeWatch(i, dt)) continue;
        // Not flown for kHeliStaleS of game time (paused menus don't count: vehicle updates stop there,
        // and the director's dt is clamped). sinceFly only resets on a frame HeliFly really flew, so one
        // stuck behind a gate (asleep, not spawned, no body...) is replaced instead of hanging about.
        if (h.age > 4.0f && h.sinceFly > kHeliStaleS)
        {
            Log(">>> wanted: police helicopter #%d hasn't been flown for %.1f s (sim LOD %d; %s) - replaced\n", h.id,
                h.sinceFly, At<int32_t>(h.veh, eng::VEH_SIMLOD), kGateText[h.gate < 8 ? h.gate : 1]);
            if (!h.flown && ++g_heliNoFly >= 2)
            {
                g_heliOff = true;                                       // the hover hook isn't in, or never gets past a gate
                Log(">>> wanted: two police helicopters were never flown a single frame (%s) - no police helicopters "
                    "(until heli_model changes)\n", kGateText[h.gate < 8 ? h.gate : 1]);
            }
            HeliDestroy(i, "not flown");
            g_heliCoolMs = (now + 3000) | 1u;
            continue;
        }
        if (h.leaving)
        {
            h.leaveT += dt;
            if (h.leaveT > 6.0f || (h.distH > 90.0f && h.distH < 1e8f)) HeliDestroy(i, "it flew off");
            continue;
        }
        if (h.respawn)
        {
            HeliDestroy(i, "stuck for 8 s - a new one comes");
            g_heliCoolMs = (now + 3000) | 1u;
            continue;
        }
        if (h.distH > kHeliLostDist && h.distH < 1e8f) h.lostT += dt; else h.lostT = 0.0f;
        if (h.lostT > 5.0f)
        {
            h.leaving = true; h.leaveT = 0.0f;
            g_heliCoolMs = (now + 25000) | 1u;
            Log(">>> wanted: police helicopter #%d has lost you (%.0f u away) - it turns back; another may come in 25 s\n",
                h.id, h.distH);
        }
    }
    // how many: one at heli_level, two from heli_level2. None in a story mission, after a bust, with
    // wanted mode off (Tick's stop path), or once the model was refused.
    const bool allowed = cfg.wanted_heli && !g_heliOff && !mission && !g_stopReq;
    int want = 0;
    if (allowed && !Me().bust.on && level >= cfg.wanted_heli_level && level > 0)
        want = level >= cfg.wanted_heli_level2 ? 2 : 1;
    if (mission && g_nHeliLive) HeliStopAll("a story mission is on", 0.0f);
    // By count, not by slot (a live slot 1 can be the one at level 4): how many fly for you now. Too
    // many - the highest slot leaves first; too few - one on its way out turns back first (not one
    // that lost you), then a new one comes.
    int active = 0;
    for (const HeliSlot& h : g_heli) if (h.veh && !h.fading && !h.leaving) ++active;
    for (int i = kMaxHelis - 1; i >= 0 && active > want; --i)
    {
        HeliSlot& h = g_heli[i];
        if (!h.veh || h.fading || h.leaving) continue;
        h.leaving = true; h.leaveT = 0.0f; --active;
        Log(">>> wanted: police helicopter #%d leaves (%s)\n", h.id, Me().bust.on ? "you're busted" : !cfg.wanted_heli ? "heli = 0"
            : level <= 0 ? "the chase is over" : "the level dropped");
    }
    for (int i = 0; i < kMaxHelis && active < want; ++i)
    {
        HeliSlot& h = g_heli[i];
        if (!h.veh || h.fading || !h.leaving || h.lostT > 5.0f) continue;
        h.leaving = false; ++active;
        Log(">>> wanted: police helicopter #%d turns back to you\n", h.id);
    }
    for (HeliSlot& h : g_heli) if (h.veh && !h.fading) h.height = level >= 5 ? cfg.wanted_heli_height5 : cfg.wanted_heli_height4;
    // one new one a tick, at most, into the first free slot
    if (active < want && haveYou && Due(now, g_heliCoolMs) && Due(now, g_heliRetryMs))
        for (int i = 0; i < kMaxHelis; ++i)
            if (!g_heli[i].veh) { HeliSpawn(w, i, yp, cfg, now); break; }
    if (!want)
    {
        g_heliWaitMs = 0;
        HeliPreloadRelease("no helicopter wanted any more");
    }
    // in sight (copter::InSight, the one test): the pursuers they add, the crimes they see, and the
    // radio's bulletin when the first one has you
    int watch = 0;
    float nearH = 1e9f;
    for (const HeliSlot& h : g_heli)
    {
        if (!h.veh || h.fading) continue;
        nearH = std::fmin(nearH, h.distH);
        if (copter::InSight(h.distH, h.leaving, h.stuck != 0, cfg.wanted_heli_sight)) ++watch;
    }
    g_heliWatch = watch; g_heliNear = nearH;
    if (level < cfg.wanted_heli_level || level <= 0) g_heliAnnounced = false;
    if (watch > 0 && !g_heliAnnounced)
    {
        g_heliAnnounced = true;
        if (cfg.wanted_heli_news) InterlockedIncrement(&g_heliNews);
        Log(">>> wanted: the police helicopter has you in sight%s\n", cfg.wanted_heli_news ? " (the radio's helicopter bulletin)" : "");
    }
}

// The 1 s status line's part.
static void HeliStatus(char* buf, size_t cap, const Config& cfg)
{
    buf[0] = 0;
    if (!g_nHeliLive) return;
    int k = _snprintf_s(buf, cap, _TRUNCATE, "; helis %d (in sight %d", g_nHeliLive, g_heliWatch);
    if (k > 0 && g_heliHold.t > 0.0f)
        k += _snprintf_s(buf + k, cap - k, _TRUNCATE, cfg.wanted_heli_hold > 0.0f ? ", alone %.0f of %.0f s" : ", alone %.0f s",
                         g_heliHold.t, cfg.wanted_heli_hold);
    if (k > 0) k += _snprintf_s(buf + k, cap - k, _TRUNCATE, ")");
    static const char* const kMode[] = { "orbit", "trail", "leave", "hold" };
    for (int i = 0; i < kMaxHelis && k > 0 && k < static_cast<int>(cap) - 8; ++i)
    {
        HeliSlot& h = g_heli[i];
        if (!h.veh) continue;
        k += _snprintf_s(buf + k, cap - k, _TRUNCATE, " [#%d %s%s %.0f u, %+.0f up, hag %.0f, %.0f u/s, %d upd, LOD %d%s%s%s]",
                         h.id, h.fading ? "fading " : h.leaving ? "leaving " : "", kMode[h.mode < 4 ? h.mode : 3],
                         h.distH < 1e8f ? h.distH : -1.0f, h.above, h.hag < 1e30f ? h.hag : -1.0f, h.speed, h.frames, h.lod,
                         h.stuck ? ", CLIMBING OUT" : "", h.lit ? ", lit" : "",
                         h.gate != kFlown ? (h.gate == kAsleep ? ", NOT FLOWN: asleep" : h.gate == kInvalid ? ", NOT FLOWN: invalid"
                                             : ", NOT FLOWN") : "");
        h.frames = 0;
    }
}

// ---------------------------------------------------------------------------------------------
//  The flight: HoverThunk, after the engine's Update of one of ours.
// ---------------------------------------------------------------------------------------------
static int BeamFault(EXCEPTION_POINTERS* ep)
{
    if (!g_beamFault)
    {
        g_beamFault = true;
        Log("!!! EXCEPTION 0x%08lX in the police helicopter's searchlight at %p - no searchlight for this session; "
            "the helicopters fly on.\n", ep->ExceptionRecord->ExceptionCode, ep->ExceptionRecord->ExceptionAddress);
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

// The searchlight, at night: the copy's LocatorOffset turns the lamp onto you.
//   lamp world = LocatorOffset x joint world (0x140965DD0 is a x b, rows; the joint world is joint local
//   x veh+0x210, 0x1404A8B20) [P], so for a wanted lamp rotation Rd: LocatorOffset = Rd x (joint world)^T.
//   The light node takes the lamp world's row 2 as its axis (0x140067810 <- 0x1404D827C) [P]; which way
//   along it the spot shines is [U]: the cars' LightsFL lamps (identity LocatorOffset, as here) light the
//   road AHEAD, and ahead is -row 2 (engine.h) - so row 2 points away from you (heli_beam_axis -1) [I].
static void HeliBeam(int i, void* veh, const float tp[3], bool haveT, const Config& cfg)
{
    HeliSlot& h = g_heli[i];
    if (!h.lamp)
    {
        if (h.lampTries >= 120) return;
        h.lamp = FindLamp(veh, &h.lampOrig);
        if (++h.lampTries == 120 && !h.lamp) Log("    wanted: police helicopter #%d has no \"Searchlight\" lamp - no searchlight\n", h.id);
        if (!h.lamp) return;
    }
    const bool on = cfg.wanted_heli_searchlight && lights::g_night && haveT && !h.fading;
    if (!on) { if (h.lit) LampBack(i); return; }
    uint8_t* lamp = static_cast<uint8_t*>(h.lamp);
    uint8_t* orig = static_cast<uint8_t*>(h.lampOrig);
    // where the lamp is, and how the joint it hangs on is turned
    alignas(16) float loc[16], jw[16];
    if (!reinterpret_cast<JointMtxFn>(g_base + FN_JOINT_MTX)(veh, At<int32_t>(lamp, LI_JOINT), loc, jw, true)) return;
    float J[3][3];
    for (int r = 0; r < 3; ++r) { for (int c = 0; c < 3; ++c) J[r][c] = jw[r * 4 + c]; if (!copter::Norm3(J[r])) return; }
    const float* L = reinterpret_cast<const float*>(orig + AD_LOCATOR);    // the shipped offset (identity on the Responder)
    float lp[3];
    for (int c = 0; c < 3; ++c) lp[c] = L[12] * J[0][c] + L[13] * J[1][c] + L[14] * J[2][c] + jw[12 + c];
    float aim[3] = { tp[0] - lp[0], tp[1] - lp[1], tp[2] - lp[2] };
    const float slant = std::sqrt(copter::Dot3(aim, aim));
    if (!(slant > 1.0f) || !IsFin(slant)) return;
    for (int k = 0; k < 3; ++k) aim[k] /= slant;
    const float s = cfg.wanted_heli_beam_axis < 0 ? -1.0f : 1.0f;
    float Rd[3][3];
    for (int k = 0; k < 3; ++k) Rd[2][k] = s * aim[k];
    const float wy[3] = { 0.0f, 1.0f, 0.0f };
    copter::Cross3(wy, Rd[2], Rd[0]);                                    // right = up x back (engine.h rows)
    if (!copter::Norm3(Rd[0]))
    {
        const float d = copter::Dot3(J[0], Rd[2]);                      // straight down: the joint's own right
        for (int k = 0; k < 3; ++k) Rd[0][k] = J[0][k] - d * Rd[2][k];
        if (!copter::Norm3(Rd[0])) return;
    }
    copter::Cross3(Rd[2], Rd[0], Rd[1]);
    // the private copy: the shipped lamp, lit always, casting, aimed
    uint8_t* cp = g_beam[i].data;
    std::memcpy(cp, orig, lights::kAttSize);
    At<uint8_t>(cp, lights::AD_NODYN) = 0;
    At<uint8_t>(cp, lights::AD_ALWAYS) = 1;
    At<uint8_t>(cp, lights::AD_HORN) = 0;
    At<uint8_t>(cp, lights::AD_NIGHT) = 0;
    At<uint8_t>(cp, lights::AD_ADV) = 0;
    At<uint8_t>(cp, lights::AD_NOGAME) = 0;
    float* M = reinterpret_cast<float*>(cp + AD_LOCATOR);
    for (int r = 0; r < 3; ++r)
    {
        for (int c = 0; c < 3; ++c) M[r * 4 + c] = copter::Dot3(Rd[r], J[c]);
        M[r * 4 + 3] = 0.0f;
    }
    // (row 3, the lamp's place on the joint, stays the shipped one)
    const float fov = cfg.wanted_heli_beam_fov;
    At<float>(cp, lights::AD_FOV) = fov;
    At<float>(cp, AD_ASPECT) = 1.0f;                                     // a round spot (shipped 0.5)
    const float reach = std::fmax(cfg.wanted_heli_beam_range, slant + 8.0f);
    At<float>(cp, lights::AD_RANGE) = reach - 0.1f;                     // the light's reach (lights.h: the floor)
    At<float>(cp, lights::AD_FALLOFF) = cfg.wanted_heli_beam_range;
    for (int c = 0; c < 3; ++c)
        At<float>(cp, lights::AD_COLOUR + 4 * c) = At<float>(orig, lights::AD_COLOUR + 4 * c) * cfg.wanted_heli_beam_intensity;
    // the visible beam mesh down to you: x = tan(FoV/2) FallOff scale.x, y = scale.y, z = scale.z (Notes\25)
    if (cfg.wanted_heli_beam_mesh)
    {
        const float t = std::tan(Clampf(fov, 1.0f, 170.0f) * 0.00872665f);
        At<float>(cp, lights::AD_BEAM_SX) = slant / std::fmax(cfg.wanted_heli_beam_range, 1.0f);
        At<float>(cp, lights::AD_BEAM_SY) = t * slant;
        At<float>(cp, lights::AD_BEAM_SZ) = slant;
    }
    else At<float>(cp, lights::AD_BEAM_SX) = At<float>(cp, lights::AD_BEAM_SY) = At<float>(cp, lights::AD_BEAM_SZ) = 0.0f;
    At<void*>(lamp, lights::LI_DEF) = cp;
    // a beam shape: the lamp's own beam (HEADLIGHT_SINGLEBEAM4) or bulb texture, else a police car's
    void* ck = At<void*>(lamp, lights::LI_COOKIE);
    if ((!PtrOk(ck) || ck == *reinterpret_cast<void**>(g_base + RVA_NO_COOKIE)) && h.cookieTries < 3)
    {
        ++h.cookieTries;
        if (!lights::OwnCookie(lamp, 2) && !lights::OwnCookie(lamp, 1) && lights::g_cookie)
            lights::g_assign(reinterpret_cast<void**>(lamp + lights::LI_COOKIE), &lights::g_cookie);
    }
    if (!h.lit) Log("    wanted: police helicopter #%d searchlight on (night) - %.0f u to you, cookie %s\n", h.id, slant,
                    PtrOk(At<void*>(lamp, lights::LI_COOKIE)) ? "set" : "NONE (no spot without one)");
    h.lit = true;
    // The calibration, once a helicopter (the open question: which way along row 2 does a spot shine?).
    // The light node is the lamp's light (lamp+0x70; its matrix from 0x140067810: +0x60 right, +0x70 up,
    // +0x80 the axis, +0x90 the place) - after a frame it should hold the row 2 we wrote.
    if (++h.litFrames == 3 && !h.calSaid)
    {
        h.calSaid = true;
        uint8_t* node = At<uint8_t*>(lamp, LI_NODE);
        float ax[3] = { 0, 0, 0 };
        if (PtrOk(node)) for (int k = 0; k < 3; ++k) ax[k] = At<float>(node, 0x80 + 4 * k);
        void* inst = At<void*>(veh, eng::VEH_INSTANCE);
        float bb = 0.0f, bu = 0.0f;
        if (PtrOk(inst))
            for (int k = 0; k < 3; ++k) { bb += J[2][k] * At<float>(inst, eng::INST_ROW2 + 4 * k); bu += J[1][k] * At<float>(inst, eng::INST_ROW1 + 4 * k); }
        const float took = IsFin(ax[0]) ? copter::Dot3(ax, h.aim2) : 0.0f;
        Log(">>> wanted: searchlight calibration (helicopter #%d) - joint LightsFL vs the airframe: back.back %+.2f, up.up %+.2f; "
            "shipped offset row 3 (%.2f %.2f %.2f); toward you (%+.2f %+.2f %+.2f) from %.1f u; we wrote row 2 = %+d x that; "
            "the light node's axis now (%+.2f %+.2f %+.2f), . ours %+.2f (+1 = the engine took the aim). A pool of light on the "
            "ground round you = right; none, with the beam up in the sky = set [wanted] heli_beam_axis = %d\n",
            h.id, bb, bu, L[12], L[13], L[14], aim[0], aim[1], aim[2], slant, static_cast<int>(s), ax[0], ax[1], ax[2], took,
            s < 0.0f ? 1 : -1);
    }
    for (int k = 0; k < 3; ++k) h.aim2[k] = Rd[2][k];
}

static void RunBeam(int i, void* veh, const float tp[3], bool haveT, const Config& cfg)
{
    if (g_beamFault)
    {
        // after a fault: the shipped lamp back, once (a second fault just leaves our static copy in place)
        if (g_heli[i].lit) { __try { LampBack(i); } __except (EXCEPTION_EXECUTE_HANDLER) { g_heli[i].lit = false; } }
        return;
    }
    __try { HeliBeam(i, veh, tp, haveT, cfg); }
    __except (BeamFault(GetExceptionInformation())) {}
}

static copter::Params HeliParams(const Config& cfg, float height)
{
    copter::Params p;
    p.height = height;
    p.orbitR = cfg.wanted_heli_radius;
    p.vMax = cfg.wanted_heli_speed;
    p.leaveSpeed = std::fmin(30.0f, cfg.wanted_heli_speed);
    return p;
}

static void HeliFly(void* veh, const float lv0[3], const float av0[3], const Config& cfg)
{
    const int i = HeliIndex(veh);
    if (i < 0) return;
    HeliSlot& h = g_heli[i];
    if (!RefAlive(h.ref) || *h.ref.cell != veh) return;                 // gone: the director lets go of it
    const int32_t lod = At<int32_t>(veh, eng::VEH_SIMLOD);
    h.lod = lod;
    // Until this frame is flown: no distance (a stale one would keep it "in sight" - a pursuer and a
    // witness - while it isn't flying at all), and the reason for the log. Only a flown frame resets
    // the director's sinceFly, so one held back by a gate below for 3 s is replaced.
    h.distH = h.dist = 1e9f;
    if (lod == 10) { h.gate = kAsleep; return; }                         // asleep
    { int hc = 1, hm = 1; if (vd::Health(veh, hc, hm) && hc < 1) { h.gate = kDestroyed; return; } }   // the game's sequence
    if (!(At<uint8_t>(veh, eng::VEH_FLAGS4) & 1)) { h.gate = kNotSpawned; return; }   // not spawned yet
    if (At<uint8_t>(veh, eng::VEH_FLAGS5) & 0xA0) { h.gate = kParked; return; }       // parked / disabled
    if (h.ud && At<int32_t>(h.ud, eng::UD_STATE) != 2)
    {
        if (h.udFix++ < 3) Log("    wanted: police helicopter #%d pilot state %d - set back to 2 (flying)\n", h.id, At<int32_t>(h.ud, eng::UD_STATE));
        At<int32_t>(h.ud, eng::UD_STATE) = 2;
    }
    void* inst = At<void*>(veh, eng::VEH_INSTANCE);
    void* body = PtrOk(inst) ? At<void*>(inst, eng::INST_BODY) : nullptr;
    if (!PtrOk(body)) { h.gate = kNoBody; return; }
    // reduced sim LOD: the hover update stops before its terrain probe - run it (heli.h Fly)
    if (lod != 1 && hl::g_terrain)
    {
        void* hd = At<void*>(veh, eng::VEH_HDRIVE);
        if (PtrOk(hd)) hl::g_terrain(hd);
    }
    if (h.reset) { copter::Reset(h.st); h.reset = false; }

    copter::Input in{};
    in.dt = At<float>(veh, eng::VEH_DT);
    for (int k = 0; k < 3; ++k)
    {
        in.pos[k]   = At<float>(body, eng::BODY_POS + 4 * k);
        in.right[k] = At<float>(inst, eng::INST_ROW0 + 4 * k);
        in.up[k]    = At<float>(inst, eng::INST_ROW1 + 4 * k);
        in.back[k]  = At<float>(inst, eng::INST_ROW2 + 4 * k);
        in.lv[k] = lv0[k]; in.av[k] = av0[k];
    }
    in.hag = hl::HeightAboveGround(veh);
    // the suspect (either player), and the other player when he flies too (co-op)
    float tp[3];
    const bool haveT = YouPos(tp);
    float tv[3];
    in.tgtFlying = SuspectMotion(tv) ? 1 : 0;
    const float nanv = std::nanf("");
    for (int k = 0; k < 3; ++k) { in.tgt[k] = haveT ? tp[k] : nanv; in.tgtV[k] = tv[k]; in.peer[k] = nanv; }
    {
        void* other = g_sus == 1 ? ::PlayerVehicle() : ::Player2Vehicle();
        void* ob = other ? BodyOf(other) : nullptr;
        const int32_t oc = other ? At<int32_t>(other, eng::VEH_CLASS) : 0;
        if (ob && (oc == 6 || oc == 4)) { for (int k = 0; k < 3; ++k) in.avoid[0][k] = At<float>(ob, eng::BODY_POS + 4 * k); in.nAvoid = 1; }
    }
    // the other police helicopter: kept apart, and on the circle opposite it
    in.orbitAt = nanv;
    const HeliSlot& o = g_heli[i ^ 1];
    if (o.veh && RefAlive(o.ref) && *o.ref.cell == o.veh)
    {
        if (void* ob = BodyOf(o.veh)) for (int k = 0; k < 3; ++k) in.peer[k] = At<float>(ob, eng::BODY_POS + 4 * k);
        if (i == 1 && !o.leaving && !o.fading && o.st.primed && !o.st.chasing) in.orbitAt = o.st.theta + 3.14159265f;
    }
    in.side = h.side;
    in.leaving = (h.leaving || h.fading) ? 1 : 0;
    copter::Output out;
    copter::Step(HeliParams(cfg, h.height > 0.0f ? h.height : cfg.wanted_heli_height4), h.st, in, out);
    if (!out.valid) { h.gate = kInvalid; return; }                       // the engine's frame stands
    h.gate = kFlown; h.sinceFly = 0.0f; h.flown = true; ++h.frames;
    for (int k = 0; k < 3; ++k)
    {
        At<float>(body, eng::BODY_LINVEL + 4 * k) = out.lv[k];
        At<float>(body, eng::BODY_ANGVEL + 4 * k) = out.av[k];
    }
    h.dist = out.dist; h.distH = out.distH; h.mode = out.mode; h.stuck = out.stuck; h.respawn = out.respawn;
    h.above = haveT ? in.pos[1] - tp[1] : 0.0f;
    h.hag = in.hag; h.speed = std::sqrt(copter::Dot3(out.lv, out.lv));
    RunBeam(i, veh, tp, haveT, cfg);
}

// Is one of ours within r of this (the player's) aircraft? heli.h's crash detector would read the
// solver's push from a touch as a crash; HoverThunk skips that frame's check instead.
static bool HeliTouching(void* veh, float r)
{
    if (!g_nHeliLive || g_copFault) return false;
    void* b = BodyOf(veh);
    if (!b) return false;
    float p[3];
    for (int k = 0; k < 3; ++k) p[k] = At<float>(b, eng::BODY_POS + 4 * k);
    for (const HeliSlot& h : g_heli)
    {
        if (!h.veh || !RefAlive(h.ref) || *h.ref.cell != h.veh) continue;
        void* hb = BodyOf(h.veh);
        if (hb && DistTo(hb, p) < r) return true;
    }
    return false;
}

// The helicopters' fault switch (the officers' pattern): off for the session, the rest of wanted mode
// carries on. The handler touches nothing; the next director call cleans up what it can, once.
static int CopFault(EXCEPTION_POINTERS* ep)
{
    if (!g_copFault)
    {
        g_copFault = true;
        g_copCleanup = true;
        Log("!!! EXCEPTION 0x%08lX in the police helicopter code at %p - no police helicopters for this session; "
            "the rest of wanted mode carries on.\n", ep->ExceptionRecord->ExceptionCode, ep->ExceptionRecord->ExceptionAddress);
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

// After a CopFault, once (from whichever director call comes next - the tick or the stop path): the
// ones there are faded out and let go of with what we did to them undone, each step on its own guard;
// whatever's left is forgotten.
static void RunHeliCopCleanup()
{
    if (!g_copCleanup) return;
    g_copCleanup = false;
    __try { HeliStopAll("the helicopter code faulted", 0.0f); }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
    for (int i = 0; i < kMaxHelis; ++i)
    {
        __try { HeliLetGo(i); }
        __except (EXCEPTION_EXECUTE_HANDLER) { g_heli[i] = HeliSlot{}; }
    }
    CountLive();
    g_heliWatch = 0; g_heliNear = 1e9f;
    wanted::ResetHeliHold(g_heliHold);
}

static void RunHeliTick(const World& w, float dt, int level, const Config& cfg, bool haveYou, const float yp[3], bool mission)
{
    if (g_copFault) { RunHeliCopCleanup(); return; }
    __try { HeliTick(w, dt, level, cfg, haveYou, yp, mission); }
    __except (CopFault(GetExceptionInformation())) {}
}

// Wanted mode off, `[`, the unhook: everything down (dt: since the last call, for the fade-outs).
static void RunHeliStop(const char* why, float dt)
{
    wanted::ResetHeliHold(g_heliHold);
    g_heliOffT = 0.0f;
    if (g_copFault) { RunHeliCopCleanup(); g_heliWatch = 0; g_heliNear = 1e9f; g_heliAnnounced = false; return; }
    if (!g_nHeliLive && !g_heliPreload) { g_heliWatch = 0; g_heliNear = 1e9f; g_heliAnnounced = false; return; }
    __try { HeliStopAll(why, dt); }
    __except (CopFault(GetExceptionInformation())) {}
}

// The player's world, but Tick can't run in it: its traffic is off (TM+0x1FC = 0, a mission's
// DisableTraffic). The helicopters fly from the helicopter hook regardless, so they are sent home from
// here - at once when wanted mode is off or lcudrive lets go, after a second otherwise (a frame or two
// with the traffic off isn't worth a new helicopter) - and their fade-outs are watched.
static void RunHeliTrafficOff(float dt, bool stop)
{
    if (!g_nHeliLive && !g_heliPreload) { g_heliOffT = 0.0f; return; }
    g_heliOffT += dt;
    if (stop) RunHeliStop(g_stopReq ? "lcudrive is letting go" : "wanted mode is off", dt);
    else if (g_heliOffT > 1.0f)
    {
        const float t = g_heliOffT;
        RunHeliStop("the city's traffic is off", dt);
        g_heliOffT = t;                                       // (still off: RunHeliStop's reset undone)
    }
}

// A new world (Tick): forget them. A CopFault cleanup still pending has nothing left to clean.
static void RunHeliForget(const char* why)
{
    g_copCleanup = false;
    __try { HeliForgetAll(why); }
    __except (CopFault(GetExceptionInformation())) {}
}

// The rest of wanted mode faulted (lcudrive.cpp's WantedFault): Tick never runs again and IsCopHeli
// refuses them, so nobody would fly them, send them off or watch their fade-outs. Once: faded out and
// let go of at once (lamp, siren, seats and the boundary bit back).
static void RunHeliAbandon(const char* why)
{
    wanted::ResetHeliHold(g_heliHold);
    if (g_copFault) { RunHeliCopCleanup(); return; }
    if (!g_nHeliLive && !g_heliPreload) return;
    __try
    {
        HeliStopAll(why, 0.0f);
        for (int i = 0; i < kMaxHelis; ++i) if (g_heli[i].veh) HeliLetGo(i);
        Log("    wanted: the police helicopters are let go of (%s)\n", why);
    }
    __except (CopFault(GetExceptionInformation())) {}
    RunHeliCopCleanup();
}
