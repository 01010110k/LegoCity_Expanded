// =============================================================================================
//  dino.h - the rideable T-Rex, waiting at the airport (2026-11-15). Its decisions are in
//  dino_model.h so the harness can run them; this is the engine side.
//
//  The goal: the rideable T-Rex of the Museum mission - ridden through the city there, never
//  available afterwards - stands at the airport in free roam; two of them, side by side, one for
//  each co-op player.
//
//  What the T-Rex is [P data]: a CHARACTER, not a vehicle. stuff\ai\AITypes.txt has the class
//  Character.RideableCreature with the type "Behemoth" -> the definition Novelty_Behemoth
//  (chars\vehicles\novelty\novelty_behemoth: can_take_over, ride_locator 0, AddOn TRexAddOn). The
//  story mission (jobs\scripts\act_3\trex_chase\trex_chase.sf) makes it with
//  CreateAICharacter("Behemoth", "RideableCreature", pos, dir), SetInvulnerable(true), and then
//  FORCES the players on with ManageTakeover(..., #Instant, #LockedIn). The museum room before it
//  (levels\standalones\museum\museum_room02\ai\level.sf) makes the same character and just waits
//  for GetCharacterInfo(#HasRider): there the player walks up and gets on with the game's own
//  ride prompt, as on the farm's horse (wi_farmlands_0505_-_horses.sf, free roam) [P scripts]. So a
//  spawned one needs nothing more to be ridden, and without #LockedIn (bit 8 of ManageTakeOver's
//  flag byte, 0x140A9B1E7 [P]; who reads it [U]) the rider gets off the way he leaves any ride.
//
//  What this does, from cPopulationManager::Update after the original (the system phase, as
//  airport.h), for each of [dino] count stands (dino_model.h StandPos):
//    * a player within `near` of the stand and no T-Rex of ours for it -> the game's character
//      spawner, CreateAICharacter's own core 0x1406FE180 [P 0x140106857], script-held (its 8th
//      argument: the population manager never culls it [P 0x14070089B, 0x1406FF48E]), facing
//      [dino] heading; made invulnerable the way the scripts do (status flag 0x59);
//    * ridden -> left alone entirely; left somewhere else -> removed once nobody is within `leave`
//      of it for 10 s, and a fresh one stands on the stand again; nobody within `far` -> removed;
//    * a story mission -> none appear, and the waiting ones go (the mission makes its own);
//    * NEVER removed while a player rides it or stands within 8 u of it (dino_model.h MayRemove).
//  Removal is the script's own Character.Kill for an NPC [P 0x1407613FB..0x140761424]: status
//  flag 0xC cleared, kill type 5 through 0x1407976D0 - what trex_chase.sf does to its own T-Rex
//  when the mission ends (also invulnerable, also out of sight).
//  It stands down while lcudrive is off ([) and goes when [dino] enable = 0. On unhook (and once
//  after a fault here) everything goes back, for every stand: the T-Rex removed unless a player is
//  on it or beside it (then it stays, ours no longer), the watch and any pending preload released.
//
//  Engine facts used here:
//    [P] class tables: characters *(0x141C76B30), 0x40 a class, +0 = name hash; ClassIndex
//        0x1406FB2D0(hash, skipFirst) ; TypeIndex 0x1406FB5F0(class*, hash) walks the class's
//        list (+0x28 head, +0x30 end; node +8 next, +0x10 definition index, +0x14 type-name hash);
//        DefIndex 0x1406FB530(class*, type). Built from AITypes.txt by 0x140703B00 with the name
//        hash 0x1402D9DD0 (wd::H) on the part after "Character." and on each type name.
//    [P] spawner 0x1406FE180(pm, level, classIdx, typeIdx, const float pos[4], const float dir[4],
//        int* status, bool scriptHeld, bool altCtor, uint32_t owner) -> MechCharacter* or null.
//        status: 0 ok; 0xA spawning switched off (0x141C7A85C) or no such class/type; 0xC the
//        definition is not loaded - it asked the streamer for it under `owner` (Preload
//        0x1406F99E0) - call again; 4 no streamer; 0xB none of the manager's 0x30 character slots
//        free - it removed one of its own to make room - call again; 0xF creation failed. A
//        successful call gives the preload back itself (0x1406FE2D3). The script action retries on
//        0xB / 0xC every frame (0x140106867..0x14010687C). The facing is atan2(dir.x, dir.z).
//    [P] pm = wd::g_getPM(level) is the same 0x140701E10 the script action uses on its context's
//        level [0x1401067EE]; [I] that level is the one wd::g_getLevel(player 1 + 0x10) gives (what
//        airport.h spawns its planes with, proven in game).
//    [P] rider: Character.GetRidden 0x140112BA0 = not in a vehicle, ride = *(ch+0x340),
//        mount = (*(ride+0x88))->vt[72](); the same read here, compared with our T-Rex.
//    [P] SetInvulnerable 0x1407991E0 = status flag 0x59 through 0x14066F6C0(ch, id, on, &layer0).
//    [P] script hold: 0x140668020 (ch+0x424 += 1, list 0x141C74920); its release 0x14066E190; the
//        manager's own removal of a character 0x14066E420 (flag 0xC set, kill type 3).
//    [U] driving any of it from the DLL instead of a script job (Notes\32 3.1): the spawner takes
//        no script context, only the level, so nothing in it needs one [P] - but it was never run.
//
//  A second group of stands (2026-11-15, Notes\46 §2 item 4, the cut Pteranodon): [dino] model2 /
//  count2 / x2 / y2 / z2 / step_x2 / step_z2 / heading2 put another RideableCreature type on its own
//  stands, with the same rules (near / far / leave / invulnerable are shared). Default: two
//  Pteranodons on the north rim of Port Atlas's lookout terrace, beside the leaning tower, facing the
//  sea - the highest open flat ground on the island (y 19.6, the sea 19 u below to the north).
//  What ships for it [P data]: chars\creatures\pteranodon\ (pteranodon_skeleton .cd / .cpd / .dno /
//  ghg, a head attachment, one anim set whose only action is "glide"), chars\ability_ride_pteranodon
//  .as + pak (rider actions Ride_Pteranodon_Fly with _LEFT / _RIGHT blends, Ride_Pteranodon_Idle);
//  named in chardata.txt (validate "Creatures\Pteranodon"), legocity_creatures.cpj (PTERANODON_SKELETON
//  + its default anim set), legocity_generic.cpj (Ability_Ride_Pteranodon), hgopathnames.txt.
//  What does NOT ship: an AITypes.txt entry (no type: Resolve fails, logged, 10 min cool-down - the
//  stands simply stay empty), and the character .txt every rideable creature has (can_take_over,
//  ride_locator, rider / move actions; novelty_behemoth.txt, horse.txt) [P]. No string in the exe
//  names it [P strs.py]. Both come from a PATCH.DAT contributor (the lines are in Notes\46 /
//  the cut-content report). Flight: nothing shipped makes it fly - its own set is one glide loop,
//  the rider's is "Fly"/"Idle", and no code or data gives it lift [P]; restored, it is ridden on the
//  ground like the T-Rex, gliding on its feet [I] - ptero.h makes it fly ([pteranodon]). Whether its
//  ride locator 0 seats the rider [U].
// =============================================================================================
#pragma once
#include "dino_model.h"
#include "spawn_turn.h"                 // one spawner call per world update, shared with airport.h and sites.h

namespace dn {

typedef int      (__fastcall* ClassIdxFn)(uint32_t hash, bool skipFirst);
typedef int      (__fastcall* TypeIdxFn)(void* cls, uint32_t hash);
typedef int      (__fastcall* DefIdxFn)(void* cls, int type);
typedef uint8_t* (__fastcall* SpawnFn)(void* pm, void* lvl, int cls, int type, const float* pos, const float* dir,
                                       int* status, bool scriptHeld, bool altCtor, uint32_t owner);
typedef void     (__fastcall* SetFlagFn)(uint8_t* ch, int id, bool on, const int* layer);
typedef void     (__fastcall* ClearFlagFn)(uint8_t* ch, int id, const int* layer);
typedef void     (__fastcall* KillFn)(void* list, uint8_t* ch, int type, void* by, bool flag);
typedef void     (__fastcall* ChFn)(uint8_t* ch);
typedef void     (__fastcall* PreloadFreeFn)(void* scm, int def, uint32_t owner);
typedef uint8_t* (__fastcall* GetFn)(void* self);

static const uintptr_t FN_CLASSIDX = 0x6FB2D0, FN_TYPEIDX = 0x6FB5F0, FN_DEFIDX = 0x6FB530, FN_SPAWN = 0x6FE180,
                       FN_SETFLAG = 0x66F6C0, FN_CLEARFLAG = 0x6687B0, FN_KILL = 0x7976D0, RVA_KILL_LIST = 0x1C77AFC,
                       FN_SCRIPT_RELEASE = 0x66E190, FN_PM_REMOVE = 0x66E420, FN_PRELOAD_FREE = 0x6FAAE0,
                       RVA_SCM = 0x17EBFA0, RVA_CHAR_CLASSES = 0x1C76B30, RVA_CHAR_CLASS_COUNT = 0x1C76B40;
static const uintptr_t CH_RIDE = 0x340, RIDE_MOUNT = 0x88, CH_SCRIPT_HOLD = 0x424;
static const int FLAG_INVULNERABLE = 0x59, FLAG_REMOVING = 0xC, KILL_SCRIPT = 5;
static const int kLayer0 = 0;
static const char kClass[] = "RideableCreature";
template <class F> static F Fn(uintptr_t rva) { return reinterpret_cast<F>(g_base + rva); }

struct Slot
{
    uint8_t* ch;
    void*    def;                // ch+0x650 when it was made (a reused allocation changes it)
    wd::SP   watch;              // a game target on it: its weak ref clears when it is destroyed
    dnm::Mem mem;
    bool     preloaded;          // a spawn attempt asked the streamer for the definition under g_owner
    void*    spawnPm;            // the population manager the spawn attempt began with
    float    pos[3];             // this stand
    bool     riddenLogged;
    int      group;              // dino_model.h SlotGroup: 0 = [dino] model (the T-Rex), 1 = [dino] model2
};
static Slot     g_slot[dnm::kSlots] = {};
static uint32_t g_owner = 0;
struct Kind { int cls, type, def; char resolved[48]; };   // one group's creature, resolved
static Kind     g_kind[dnm::kGroups] = { { -1, -1, -1, "" }, { -1, -1, -1, "" } };
static bool     g_standsChecked = false;

// One group's stands, from [dino]: group 0 = model / count / x / y / z / step_x / step_z / heading,
// group 1 = the same keys with a 2 (model2 / count2 / x2 ...).
struct Grp { int count; const char* model; float base[3]; float stepX, stepZ, heading; };
static Grp GroupOf(const Config& cfg, int g)
{
    Grp r;
    if (g == 0)
    {
        r.count = cfg.dino_enable ? dnm::Count(cfg.dino_count) : 0;
        r.model = cfg.dino_model;
        r.base[0] = cfg.dino_x; r.base[1] = cfg.dino_y; r.base[2] = cfg.dino_z;
        r.stepX = cfg.dino_step_x; r.stepZ = cfg.dino_step_z; r.heading = cfg.dino_heading;
    }
    else
    {
        r.count = cfg.dino_enable && cfg.dino2_model[0] ? dnm::Count(cfg.dino2_count) : 0;
        r.model = cfg.dino2_model;
        r.base[0] = cfg.dino2_x; r.base[1] = cfg.dino2_y; r.base[2] = cfg.dino2_z;
        r.stepX = cfg.dino2_step_x; r.stepZ = cfg.dino2_step_z; r.heading = cfg.dino2_heading;
    }
    return r;
}
static volatile bool g_stopReq = false;  // unhook: give everything back on the next PM update
static bool     g_released = false;      // ... done (unhook or after a fault): nothing more from here

// Ours and still the character we made: the watch's weak ref is alive, the object is constructed
// (+0x320 bit 0) and still has the definition it was made with. Not wd::LiveChar: that one refuses a
// player-controlled character, and what a ridden creature's +0x3E1 reads is [U].
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
    void* scm = *reinterpret_cast<void**>(g_base + RVA_SCM);
    const int def = g_kind[s.group == 1 ? 1 : 0].def;
    if (PtrOk(scm) && def >= 0 && g_owner) Fn<PreloadFreeFn>(FN_PRELOAD_FREE)(scm, def, g_owner);
}

static bool ChPos(uint8_t* ch, float out[3])
{
    for (int k = 0; k < 3; ++k) out[k] = At<float>(ch, 0xA8 + 4 * k);
    return IsFin(out[0]) && IsFin(out[1]) && IsFin(out[2]);
}

// Does player i ride this character? Character.GetRidden's own read (0x140112BA0) [P]: no live
// vehicle ref (ch+0x428 handle, ch+0x430 control block with a count at +4), then the ride's mount.
static bool Rides(int i, uint8_t* dino)
{
    uint8_t* p = wd::RawPlayer(i);
    if (!PtrOk(p)) return false;
    uint8_t* blk = At<uint8_t*>(p, eng::MC_VEHBLK);
    if (PtrOk(blk) && At<int32_t>(blk, 4) != 0 && At<void*>(p, eng::MC_VEHREF) != nullptr) return false;   // in a vehicle
    uint8_t* ride = At<uint8_t*>(p, CH_RIDE);
    if (!PtrOk(ride)) return false;
    void* m = At<void*>(ride, RIDE_MOUNT);
    if (!PtrOk(m) || !PtrOk(*reinterpret_cast<void**>(m))) return false;
    return reinterpret_cast<GetFn>((*reinterpret_cast<void***>(m))[72])(m) == dino;
}

// The script's own Kill for an NPC (0x1407613FB): flag 0xC off, kill type 5, nobody's doing.
static void Kill(uint8_t* ch)
{
    Fn<ClearFlagFn>(FN_CLEARFLAG)(ch, FLAG_REMOVING, &kLayer0);
    Fn<KillFn>(FN_KILL)(reinterpret_cast<void*>(g_base + RVA_KILL_LIST), ch, KILL_SCRIPT, nullptr, false);
}

// The engine side of dino_model.h's actions, for one stand.
struct World
{
    void* pm; void* lvl; const Config* cfg; Slot* s; int idx; bool* spawned; const Grp* grp;
    bool Resolve()
    {
        const char* name = grp->model;
        Kind& k = g_kind[s->group == 1 ? 1 : 0];
        g_owner = wd::H("lcudrive_dino");
        if (k.cls >= 0 && k.type >= 0 && _stricmp(k.resolved, name) == 0) return true;
        k.cls = k.type = k.def = -1;
        uint8_t* table = *reinterpret_cast<uint8_t**>(g_base + RVA_CHAR_CLASSES);
        const int n = *reinterpret_cast<int*>(g_base + RVA_CHAR_CLASS_COUNT);
        const int cls = PtrOk(table) ? Fn<ClassIdxFn>(FN_CLASSIDX)(wd::H(kClass), true) : -1;
        if (cls < 0 || cls >= n)
        {
            Log(">>> dino: the game has no character class %s (class %d of %d) - no %s\n", kClass, cls, n, dnm::CreatureName(name));
            return false;
        }
        void* entry = table + static_cast<size_t>(cls) * 0x40;
        const int type = Fn<TypeIdxFn>(FN_TYPEIDX)(entry, wd::H(name));
        const int def = type >= 0 ? Fn<DefIdxFn>(FN_DEFIDX)(entry, type) : -1;
        if (type < 0 || def < 0)
        {
            Log(">>> dino: %s is not a %s the game knows (type %d, definition %d) - check [dino] %s (Behemoth is the T-Rex;"
                " Pteranodon needs its line in stuff\\ai\\aitypes.txt from a PATCH.DAT)\n",
                name, kClass, type, def, s->group ? "model2" : "model");
            return false;
        }
        k.cls = cls; k.type = type; k.def = def;
        strcpy_s(k.resolved, name);
        Log(">>> dino: %s is %s type %d of class %d, definition %d\n", name, kClass, type, cls, def);
        return true;
    }
    void BeginSpawn() { s->spawnPm = pm; }
    int Spawn(float t)
    {
        if (!spt::Mine(sitem::kTurnDino)) return dnm::kStreaming;        // not this update's spawner: again next tick
        float d[2];
        dnm::HeadingDir(grp->heading, d);
        alignas(16) float p[4] = { s->pos[0], s->pos[1] + 0.1f, s->pos[2], 1.0f };
        alignas(16) float f[4] = { d[0], 0.0f, d[1], 0.0f };
        int st = -1;
        *spawned = true;                                                 // this tick's one spawner call
        const Kind& k = g_kind[s->group == 1 ? 1 : 0];
        uint8_t* ch = Fn<SpawnFn>(FN_SPAWN)(pm, lvl, k.cls, k.type, p, f, &st, true, false, g_owner);
        if (st == dnm::kOk && PtrOk(ch))
        {
            s->preloaded = false;                                        // the spawn gave it back itself [P]
            if (!wd::Watch(ch, &s->watch))
            {
                // no way to know when it goes: it cannot be ours. Back it goes, at once.
                Log(">>> dino[%d]: spawned %p but no watch on it could be made - removed again\n", idx, ch);
                Kill(ch);
                return -3;
            }
            s->ch = ch; s->def = At<void*>(ch, 0x650); s->riddenLogged = false;
            if (cfg->dino_invulnerable) Fn<SetFlagFn>(FN_SETFLAG)(ch, FLAG_INVULNERABLE, true, &kLayer0);
            Log(">>> dino[%d]: %s spawned at (%.1f, %.2f, %.1f) heading %.0f after %.1f s, ch=%p (%s), script hold %d%s\n",
                idx, grp->model, s->pos[0], s->pos[1], s->pos[2], grp->heading, t, ch, CharDefName(ch),
                At<int32_t>(ch, CH_SCRIPT_HOLD), cfg->dino_invulnerable ? ", invulnerable" : "");
            return dnm::kOk;
        }
        if (st == dnm::kOk) st = -2;                                     // "spawned" but no character: a failure
        if (st == dnm::kStreaming) s->preloaded = true;                  // the streamer was asked under g_owner
        return st;
    }
    void AbandonSpawn() { FreePreload(*s); s->spawnPm = nullptr; }
    void Remove(const char* why)
    {
        uint8_t* ch = Char(*s);
        if (!ch) return;
        Log(">>> dino[%d]: the %s goes - %s\n", idx, dnm::CreatureName(grp->model), why);
        Kill(ch);
    }
    void Purge()
    {
        uint8_t* ch = Char(*s);
        if (!ch) return;
        if (At<int32_t>(ch, CH_SCRIPT_HOLD) > 0) Fn<ChFn>(FN_SCRIPT_RELEASE)(ch);
        Fn<ChFn>(FN_PM_REMOVE)(ch);
    }
    void Forget(const char* why)
    {
        if (PtrOk(s->ch)) Log(">>> dino[%d]: %s\n", idx, why);
        wd::Unwatch(&s->watch);
        s->ch = nullptr; s->def = nullptr;
    }
    void Dropped() { wd::Unwatch(&s->watch); s->ch = nullptr; s->def = nullptr; }
    void Say(const char* fmt, ...)
    {
        // the model's lines begin ">>> dino: " - the stand's number goes in
        char buf[512];
        va_list a; va_start(a, fmt);
        _vsnprintf_s(buf, sizeof(buf), _TRUNCATE, fmt, a);
        va_end(a);
        static const char kPre[] = ">>> dino: ";
        if (strncmp(buf, kPre, sizeof(kPre) - 1) == 0) Log(">>> dino[%d]: %s", idx, buf + sizeof(kPre) - 1);
        else Log("%s", buf);
    }
};

// What the model needs to know about one stand's T-Rex right now.
static void Look(Slot& s, uint8_t* ch, const float pp[2][3], const bool ppOk[2], const dnm::Params& p, dnm::Facts& f)
{
    f.best = 1e9f; f.nearDino = 1e9f;
    float dp[3] = {};
    const bool dOk = ch && ChPos(ch, dp);
    for (int k = 0; k < 2; ++k) if (ppOk[k])
    {
        f.best = std::fmin(f.best, ap::Dist2(pp[k], s.pos));
        if (dOk) f.nearDino = std::fmin(f.nearDino, ap::Dist2(pp[k], dp));
    }
    f.have = ch != nullptr;
    f.onStand = dOk && ap::Dist2(dp, s.pos) < p.onStandU;
    f.ridden = ch && (Rides(0, ch) || Rides(1, ch));
    if (ch && !dOk) f.nearDino = 0.0f;                                   // where it is can't be read: treat it as beside a player
}

static void ReleaseSlot(Slot& s, int idx, const Config& cfg, void* pm, void* lvl, const char* why)
{
    bool spawned = false;
    const Grp g = GroupOf(cfg, s.group);
    World w = { pm, lvl, &cfg, &s, idx, &spawned, &g };
    dnm::Facts f = {};
    float pp[2][3] = {}; bool ppOk[2] = { false, false };
    for (int i = 0; i < 2; ++i) ppOk[i] = ap::PlayerPos(i, pp[i]);
    dnm::Params p;
    Look(s, Char(s), pp, ppOk, p, f);
    dnm::Release(w, s.mem, p, f, why);
    FreePreload(s);
    s.spawnPm = nullptr;
}

// Everything back, once (unhook, or after a fault in this code), for every stand.
static void Release(const Config& cfg, const char* why)
{
    if (g_released) return;
    g_released = true;
    for (int i = 0; i < dnm::kSlots; ++i)
        if (g_slot[i].mem.state != dnm::kIdle || PtrOk(g_slot[i].ch) || g_slot[i].preloaded)
            ReleaseSlot(g_slot[i], i, cfg, nullptr, nullptr, why);
}

// once per world update, after cPopulationManager::Update
static void Tick(void* pm, float dt, const Config& cfg, bool on)
{
    if (g_released) return;
    if (g_stopReq) { Release(cfg, "lcudrive unhooked"); return; }
    Grp grp[dnm::kGroups];
    for (int g = 0; g < dnm::kGroups; ++g) grp[g] = GroupOf(cfg, g);
    if (!g_standsChecked)
    {
        g_standsChecked = true;
        for (int g = 0; g < dnm::kGroups; ++g)
            if (!dnm::StandsOk(grp[g].count, grp[g].base, grp[g].stepX, grp[g].stepZ, 3.0f))
                Log(">>> dino: [dino] step_x%s / step_z%s put the %ss less than 3 u apart - they will stand in each other\n",
                    g ? "2" : "", g ? "2" : "", dnm::CreatureName(grp[g].model));
        if (!dnm::GroupsApart(grp[0].count, grp[0].base, grp[0].stepX, grp[0].stepZ,
                              grp[1].count, grp[1].base, grp[1].stepX, grp[1].stepZ, 3.0f))
            Log(">>> dino: [dino] x2 / z2 put a %s less than 3 u from a %s - they will stand in each other\n",
                dnm::CreatureName(grp[1].model), dnm::CreatureName(grp[0].model));
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
    }
    uint8_t* chs[dnm::kSlots]; float cpos[dnm::kSlots][3]; bool cposOk[dnm::kSlots];
    for (int i = 0; i < dnm::kSlots; ++i)
    {
        const Grp& gi = grp[dnm::SlotGroup(i)];
        g_slot[i].group = dnm::SlotGroup(i);
        dnm::StandPos(dnm::SlotStand(i), gi.base, gi.stepX, gi.stepZ, g_slot[i].pos);
        chs[i] = Char(g_slot[i]);
        cposOk[i] = chs[i] && ChPos(chs[i], cpos[i]);
    }
    dnm::Params p;
    p.nearU = cfg.dino_near; p.farU = cfg.dino_far; p.leaveU = cfg.dino_leave;
    bool spawned = false;                    // a spawner call was made this tick
    for (int i = 0; i < dnm::kSlots; ++i)
    {
        Slot& s = g_slot[i];
        const Grp& gi = grp[s.group];
        if (dnm::SlotStand(i) >= gi.count)
        {
            // a stand beyond [dino] count / count2, or [dino] enable = 0: its creature goes unless a player is on / by it
            if (s.mem.state != dnm::kIdle || PtrOk(s.ch) || s.preloaded)
                ReleaseSlot(s, i, cfg, pm, lvl, "this stand is switched off ([dino] enable / count / count2)");
            continue;
        }
        dnm::Facts f = {};
        Look(s, chs[i], pp, ppOk, p, f);
        f.dt = dt;
        f.on = on;
        f.world = world;
        f.samePm = pm == s.spawnPm;
        f.mission = mission;
        for (int k = 0; k < 2; ++k) if (ppOk[k] && ap::Dist2(pp[k], s.pos) < p.clearU) f.someoneClose = true;
        for (int j = 0; j < dnm::kSlots; ++j)
            if (j != i && cposOk[j] && ap::Dist2(cpos[j], s.pos) < p.clearU) f.someoneClose = true;
        f.hold = spawned;
        for (int j = 0; j < dnm::kSlots; ++j)
            if (j != i && dnm::SlotStand(j) < grp[dnm::SlotGroup(j)].count && g_slot[j].mem.state == dnm::kSpawning) f.hold = true;
        if (f.ridden && !s.riddenLogged)
        {
            // once per T-Rex: what a ridden creature's own player fields read (settles dino.h's [U])
            s.riddenLogged = true;
            Log(">>> dino[%d]: ridden - its +0x3E1 %u, +0x3E3 %u; riders' ride states %d / %d\n", i,
                At<uint8_t>(chs[i], 0x3E1), At<uint8_t>(chs[i], 0x3E3),
                PtrOk(p1) && PtrOk(At<void*>(p1, CH_RIDE)) ? At<int32_t>(At<void*>(p1, CH_RIDE), 0xD0) : -1,
                PtrOk(wd::RawPlayer(1)) && PtrOk(At<void*>(wd::RawPlayer(1), CH_RIDE))
                    ? At<int32_t>(At<void*>(wd::RawPlayer(1), CH_RIDE), 0xD0) : -1);
        }
        World w = { pm, lvl, &cfg, &s, i, &spawned, &gi };
        dnm::Tick(w, s.mem, p, f);
        chs[i] = Char(s);                                                // spawned / dropped this tick
        cposOk[i] = chs[i] && ChPos(chs[i], cpos[i]);
    }
}

} // namespace dn
