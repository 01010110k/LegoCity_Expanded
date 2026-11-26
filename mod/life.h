// =============================================================================================
//  life.h - ambient life: GTA-style street scenes (Notes\40). life_model.h picks the scene; this
//  file casts it from the crowd near you and plays it with the game's own AI.
//
//  2026-10-25: an overhaul of the NPCs' routines and behaviours in general, towards GTA-like
//  street life: beach parties, jogging groups, occasional fights, people having conversations,
//  and many more scenes besides.
//
//  Casting: citizens ("krawlies") 18-45 u from you, near each other, are turned into characters
//  (0x1406A3F60, as the car-jack does - Notes\38), so the swap happens away from your face. A
//  converted citizen loads CLASS_Generic -> Generic_Extended -> AI_Exterior_Male / _Female (the
//  pedestrians' 96 ambient actions: phones, photos, queues, busking, star jumps...), Ability_Dance
//  and the melee. The scene plays with the verbs wanted_officers.h proved: go to someone (0x13),
//  face someone (0x28), play an anim once or on a loop (0x24), fight (0x20). At the end they walk
//  off and turn back into the crowd ("Convert To Pedestrian").
//  Every member is re-checked each tick: a game target holds a weak ref on each (it clears when he
//  is destroyed - NPCs are heap objects, there's no pool), then his def, brain and alive flags. A
//  scene ends when its lead is gone, you're 80 u away, after 2 minutes at most, or when a mission or
//  a wanted level starts. An exception switches the director off for the session (RunLife,
//  lcudrive.cpp).
//  Wave 2 (Notes\40b, each recipe checked against the game's own caller): the game's own two-person
//  conversations (cTalkMaster / cTalkSlave), joggers who really run (cWanderOnFoot on the pedestrian
//  network at a jog), a pickpocket who flees (cFleeOnFoot) from his victim and from any cop the
//  crowd has nearby, the crowd panicking around a police chase or a crash and scattering from your
//  fists (the krawlie hook broadcast, no conversion), and passers-by stopping to chat with each
//  other (the crowd's own "Interacting With Pedestrian", no conversion).
// =============================================================================================
#pragma once
#include "life_model.h"

namespace lf {

using wd::OrderGoto; using wd::OrderFace; using wd::OrderAnimN; using wd::OrderFight; using wd::OrderRelease;
using wd::SetSideBits; using wd::DistCh; using wd::LiveChar; using wd::ContainsI; using wd::Fn;
using wd::GAIT_WALK; using wd::GAIT_RUN; using wd::GAIT_SPRINT;

static volatile bool g_fault = false;
static life::Params  g_p;
static life::State   g_s;
static bool          g_init = false;

// ---- anim ids by name (AnimId 0x140814C50, case-insensitive; each name resolved once) ------------
struct AnimName { const char* name; int id; };
static AnimName g_anims[160];                      // [round3] was 96 (the animals' names too)
static int      g_nAnims = 0;

static int Anim(const char* name)
{
    for (int i = 0; i < g_nAnims; ++i) if (g_anims[i].name == name) return g_anims[i].id;
    const int id = reinterpret_cast<wd::AnimIdFn>(g_base + wd::FN_ANIMID)(name);
    if (g_nAnims < 160) g_anims[g_nAnims++] = { name, id };
    return id;
}

// ---- the people in a scene -----------------------------------------------------------------------
struct Person
{
    uint8_t* ch;
    void*    def;
    void*    brain;
    wd::SP   watch;          // a game target on him (its weak ref clears when he's destroyed)
    uint32_t side0;
    bool     sideSet;
    bool     sped;           // he has a speed override (the joggers): cleared before he's let go
    bool     female;         // his crowd type's female bit (voice.h picks the voice)
};

static bool Alive(const Person& p)
{
    return wd::StillThere(p.watch) && LiveChar(p.ch, p.def) && At<void*>(p.ch, 0x78) == p.brain;
}

typedef void (__fastcall* SpeedFn)(uint8_t* ch, float v, int mode);      // 0x1406703B0: the MoveSpeedAddOn
static const uintptr_t FN_SPEED = 0x6703B0;

static void LetGo(Person& p)
{
    if (Alive(p))
    {
        if (p.sideSet) { SetSideBits(p.ch, p.side0); p.sideSet = false; }
        if (p.sped) { for (int mode : { 0, 1, 3 }) Fn<SpeedFn>(FN_SPEED)(p.ch, 3.4028235e38f, mode); p.sped = false; }
        OrderRelease(p.ch);
    }
    wd::Unwatch(&p.watch);
}

static bool Go(const Person& p, const Person& t, float range, uint32_t gait) { return OrderGoto(p.ch, t.ch, range, gait); }
static bool Face(const Person& p, uint8_t* who) { return OrderFace(p.ch, who); }
static bool Play(const Person& p, const char* name, int loops) { return OrderAnimN(p.ch, Anim(name), loops); }
static float Dist(const Person& a, const Person& b) { float q[3]; for (int k = 0; k < 3; ++k) q[k] = At<float>(b.ch, 0xA8 + 4 * k); return DistCh(a.ch, q); }

static uint32_t g_rng = 0x6A09E667u;
static float Rnd() { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 17; g_rng ^= g_rng << 5; return (g_rng & 0xFFFFFF) / 16777216.0f; }

// The first of `names`, from a random start, that's in his set.
static const char* PlayAny(const Person& p, const char* const* names, int count, int loops)
{
    const int start = static_cast<int>(Rnd() * count);
    for (int i = 0; i < count; ++i)
    {
        const char* n = names[(start + i) % count];
        if (Play(p, n, loops)) return n;
    }
    return nullptr;
}
#define ANY(p, list, loops) PlayAny((p), (list), static_cast<int>(sizeof(list) / sizeof((list)[0])), (loops))

static const char* const kGestures[] = { "Ponder", "point", "Point2", "Shrug", "CheckTime" };
static const char* const kCopGestures[] = { "CopTalk", "CopIdle", "point", "Ponder", "Shrug" };
static const char* const kArgue[] = { "point", "Point2", "point3", "Shrug" };
static const char* const kGoodbye[] = { "Wave", "Walk_wave" };
static const char* const kPhoneLoop[] = { "Phone_Idle", "Idle_PhoneFull" };
static const char* const kPose[] = { "PhotoPose1", "PhotoPose2" };
static const char* const kSnap[] = { "TakePhoto", "TakePhoto2" };
static const char* const kQueue[] = { "Queue1", "Queue2", "Queue3", "QueueShuffle", "CheckTime" };
static const char* const kToilet[] = { "QueueToilet", "Needs_Toilet", "QueueShuffle" };
static const char* const kFront[] = { "Use_Cashmachine", "vendingmachine", "CheckTime" };
static const char* const kWorkout[] = { "Star_Jumps", "Sit_Ups", "excersise_Pressup" };
static const char* const kBreath[] = { "CatchBreath", "CatchBreath2", "panting" };
static const char* const kDance[] = { "Dance_01", "Dance_02", "Dance_03" };
static const char* const kClap[] = { "Cheer_Clap", "Celebration_Music" };
static const char* const kCelebrate[] = { "Celebration", "Cheer_Clap", "Celebration_Low" };
static const char* const kBusk[] = { "PlayGuitar", "Mime1", "Prison_Generic_PlayDrums", "BoomBox_Idle", "KeepyUppy", "Play_Piano" };
static const char* const kBeachDay[] = { "Sunbathing", "Sunbathing2", "Beach_Workout_posing1", "Beach_Workout_posing2",
                                          "PhotoPose1", "Fan", "Seagull_Shoo", "CheckAppearance" };
static const char* const kWatch[] = { "point", "TakePhoto", "Point2", "Cheer_Clap", "TakePhoto2" };
static const char* const kGag[] = { "TreadInPoo", "Trip", "CheckAppearance", "Bubblegum", "AdmireFlower", "HailingCab",
                                     "Lamppost", "Fan", "Sad_Idle", "Ponder" };
static const char* const kCheered[] = { "Celebration_Low", "Wave", "Cheer_Clap" };
static const char* const kGive[] = { "Give_Sandwich", "Wave" };
static const char* const kListen[] = { "Talk_Listen", "Ponder", "CheckTime" };
static const char* const kRobbed[] = { "Sad_Idle", "Shrug", "point" };

// ---- wave 2 verbs (Notes\40b) ---------------------------------------------------------------------
typedef void (__fastcall* LinkFn)(uint8_t* task, uint8_t* ch);                // 0x140102760 / 0x140B93190
typedef void (__fastcall* FleeFn)(uint8_t* task, uint32_t flags, wd::SP* from, int mode);   // 0x1406F71F0
typedef void (__fastcall* QInitFn)(void* q, int mask, int shape, int first, const float* p4, float r1, float r2);
typedef bool (__fastcall* KHitFn)(void* kmgr, void* q, void* hook);           // 0x14069A760
typedef void (__fastcall* KCtxFn)(uint8_t* k, uintptr_t factory);             // 0x1406A3990
typedef int* (__fastcall* KCtxIdFn)(uint8_t* k);                              // a krawlie's vt[9]
typedef void (__fastcall* SayFn)(void* dlg, const float* p4, bool female);   // 0x1400C6410
static const uintptr_t FN_PARTNER = 0x102760, FN_SLAVEOF = 0xB93190, FN_FLEE = 0x6F71F0, FN_QINIT = 0x6991F0,
                       FN_KHIT = 0x69A760, FN_KCTX = 0x6A3990, FN_KCHAT = 0x0B0E00, FN_SAY = 0x0C6410;
static const uintptr_t RVA_KPARTNER = 0x17FDFB0, RVA_KDEFAULT = 0x17FDF84, RVA_DLG = 0x17EC8C0;
static const uintptr_t VT_SCARY = 0xC3C718, VT_ATTACKED = 0xC3C118;
static const int TASK_WANDER = 0x15, TASK_FLEE = 0x18, TASK_TALK = 0x1E;

static void SetSpeed(Person& p, float v, int mode) { Fn<SpeedFn>(FN_SPEED)(p.ch, v, mode); p.sped = true; }

// Wander the pedestrian network (cWanderOnFoot: random 3-5-hop paths, forever; it stops off the net).
static bool Wander(const Person& p, uint32_t gait)
{
    void* brain = At<void*>(p.ch, 0x78);
    if (!PtrOk(brain)) return false;
    uint8_t* t = Fn<wd::NewTaskFn>(wd::FN_NEWTASK)(brain, TASK_WANDER, wd::SLOT_TOP);
    if (!t) return false;
    At<uint32_t>(t, 0x5C) = 0x2857u | gait;                            // its gait (the alt ctor 0x140715100)
    return true;
}

// The game's own conversation (only cDailyRoutine makes one, 0x140B92C80): the master in a's top
// slot, the partner linked (0x140102760, m+0x60), the master's vt[10] (0x140B93370) puts the slave
// in b's slot 1, and the slave learns whom to walk up to (0x140B93190). Three lines each, Talk..Talk5.
// b's slot 2 is cleared first, or the slave never runs. The master never turns: face him first.
static uint8_t* Talk(const Person& a, const Person& b)
{
    void* ba = At<void*>(a.ch, 0x78);
    void* bb = At<void*>(b.ch, 0x78);
    if (!PtrOk(ba) || !PtrOk(bb)) return nullptr;
    Fn<wd::ClearQFn>(wd::FN_CLEARQ)(bb, 2);
    uint8_t* m = Fn<wd::NewTaskFn>(wd::FN_NEWTASK)(ba, TASK_TALK, wd::SLOT_TOP);
    if (!m) return nullptr;
    Fn<LinkFn>(FN_PARTNER)(m, b.ch);
    uint8_t* s = wd::VGet(m, 10);
    if (PtrOk(s)) Fn<LinkFn>(FN_SLAVEOF)(s, a.ch);
    return m;
}

// The conversation is still on: the master pending in a's slot 2 (brain+0x178) or heading its queue.
static bool Talking(const Person& a, uint8_t* m)
{
    uint8_t* brain = At<uint8_t*>(a.ch, 0x78);
    return PtrOk(brain) && PtrOk(m) && (At<uint8_t*>(brain, 0x178) == m || At<uint8_t*>(brain, 0x258) == m);
}

// Run from `from` (Flee 0x140111D80: the target, cFleeOnFoot, its setup 0x1406F71F0, +0xE6 = 1).
static bool Flee(const Person& p, uint8_t* from, int mode)
{
    void* brain = At<void*>(p.ch, 0x78);
    if (!PtrOk(brain) || !PtrOk(from)) return false;
    wd::SP sp = {};
    Fn<wd::NewTargetFn>(wd::FN_NEWTARGET)(&sp);
    if (!PtrOk(sp.obj) || !PtrOk(const_cast<LONG*>(sp.rc))) return false;
    Fn<wd::TgtCharFn>(wd::FN_TGTCHAR)(sp.obj, from, 0x2857u);
    uint8_t* t = Fn<wd::NewTaskFn>(wd::FN_NEWTASK)(brain, TASK_FLEE, wd::SLOT_TOP);
    if (t)
    {
        wd::SP c = sp;
        InterlockedIncrement(c.rc + 1);
        InterlockedIncrement(c.rc);
        Fn<FleeFn>(FN_FLEE)(t, 0x8002857u, &c, mode);
        t[0xE6] = 1;
    }
    Fn<wd::SpRelFn>(wd::FN_SPREL)(&sp);
    return t != nullptr;
}

// The crowd, without converting anyone: a sphere query (0x1406991F0) and a hook sent to every krawlie
// in it (0x14069A760) - TriggerKrawliesMassPanic 0x1400AF840 is exactly this.
struct alignas(16) HookScary { uintptr_t vt; void* obj; float pos[4]; float r; uint8_t scream; uint8_t pad[11]; };
struct HookAttacked { uintptr_t vt; uint8_t* attacker; };

static void Broadcast(void* level, const float p[3], float r, void* hook)
{
    uint8_t* km = Fn<wd::KMgrFn>(wd::FN_KMGR)(level);
    if (!PtrOk(km)) return;
    alignas(16) uint8_t q[0x100] = {};
    alignas(16) float p4[4] = { p[0], p[1], p[2], 1.0f };
    Fn<QInitFn>(FN_QINIT)(q, 4, 2, 0, p4, r, r);
    Fn<KHitFn>(FN_KHIT)(km, q, hook);
}

static void Panic(void* level, const float p[3], float r, bool scream)    // "BigScaryObjectClose": they run
{
    HookScary h = {};
    h.vt = g_base + VT_SCARY;
    h.pos[0] = p[0]; h.pos[1] = p[1]; h.pos[2] = p[2]; h.pos[3] = 1.0f;
    h.r = r; h.scream = scream ? 1 : 0;
    Broadcast(level, p, r, &h);
}

static void Scatter(void* level, uint8_t* attacker, const float p[3], float r)   // "CivilianAttacked"
{
    HookAttacked h = { g_base + VT_ATTACKED, attacker };
    Broadcast(level, p, r, &h);
}

// Two passers-by stop and chat: the crowd's own "Interacting With Pedestrian" (2 % of their bumps;
// 30 s, facing each other, chat anims and a voice line), started the way its collision handler does
// (0x1400B1040, read 2026-10-26). For each of the two: the Default krawlie context, the type's
// no-chat bit clear ([[k+0xD8]+0x370]+0x158 bit 4), its state k+0x30 == 0 (2 = they bump off each
// other instead), k+0x98 bit 1 clear, and not on a crossing (both its links k+0x80 / k+0x88 of
// type 0x800). The handler checks the last one on one side only, and k+0xD2 bit 7 first (a
// collision switch: other handlers swap contexts without it); here both sides, and not riding or going.
static int KCtx(uint8_t* k)
{
    int* c = reinterpret_cast<KCtxIdFn>((*reinterpret_cast<void***>(k))[9])(k);
    return PtrOk(c) ? *c : -1;
}

static uint8_t* KType(uint8_t* k)                    // the type's record: +0x158 bit 1 female, bit 4 no chats
{
    uint8_t* d = At<uint8_t*>(k, 0xD8);
    return PtrOk(d) ? At<uint8_t*>(d, 0x370) : nullptr;
}

static bool KCanChat(uint8_t* k, int def)
{
    uint8_t* x = KType(k);
    if (!PtrOk(x) || (At<uint8_t>(x, 0x158) & 0x10)) return false;
    if (At<uint16_t>(k, 0xD2) & 0xD || At<int32_t>(k, 0x30) != 0 || (At<uint8_t>(k, 0x98) & 2) || KCtx(k) != def) return false;
    uint8_t* l0 = At<uint8_t*>(k, 0x88);
    uint8_t* l1 = At<uint8_t*>(k, 0x80);
    return !(PtrOk(l0) && At<int32_t>(l0, 8) == 0x800 && PtrOk(l1) && At<int32_t>(l1, 8) == 0x800);
}

// [round3] NPC overhaul hooks (npc_glue.h sets them; null = as before): the overheard exchange of a chat that
// just began (true = it is ours, the stock line is not said), and the scene weights (stage 2).
static bool (*g_onChat)(uint8_t* a, uint8_t* b) = nullptr;
static void (*g_tune)(life::Params& p) = nullptr;

static bool KChat(uint8_t* a, uint8_t* b)
{
    const int def = *reinterpret_cast<const int*>(g_base + RVA_KDEFAULT);
    if (!KCanChat(a, def) || !KCanChat(b, def)) return false;
    void** partner = reinterpret_cast<void**>(g_base + RVA_KPARTNER);
    *partner = b; Fn<KCtxFn>(FN_KCTX)(a, g_base + FN_KCHAT);
    *partner = a; Fn<KCtxFn>(FN_KCTX)(b, g_base + FN_KCHAT);
    if (g_onChat && g_onChat(a, b)) return true;                     // [round3] our voices act it out
    uint8_t* who = Rnd() < 0.5f ? a : b;                              // one of them says something
    uint8_t* x = KType(who);
    void* dlg = *reinterpret_cast<void**>(g_base + RVA_DLG);
    if (PtrOk(dlg) && PtrOk(x))
    {
        alignas(16) float p4[4] = { At<float>(who, 0xB0), At<float>(who, 0xB4), At<float>(who, 0xB8), 1.0f };
        Fn<SayFn>(FN_SAY)(dlg, p4, ((At<uint8_t>(x, 0x158) >> 1) & 1) != 0);
    }
    return true;
}

// ---- the crowd near you ----------------------------------------------------------------------------
struct Cand { uint8_t* k; uint8_t* zone; int slot; float d; float pos[3]; bool cop; bool beach; };
static const int kMaxCands = 96;
static Cand g_cand[kMaxCands];
static int  g_nCand = 0, g_nCops = 0, g_nPeople = 0;

// A person the director may cast: not an animal, a crook, a trainee or a guard (by the def's name).
static bool Castable(const char* n)
{
    if (!n) return false;
    static const char* const deny[] = { "pigeon", "seagull", "duck", "crab", "_rat", "bat", "bug", "spider", "frog", "owl",
                                        "rabbit", "horse", "cow", "pig", "goat", "chicken", "bear", "crocodile", "krawly",
                                        "crops", "silhouette", "prisoner", "robber", "gang", "goon", "training", "convict",
                                        "alien", "astronaut", "guard" };
    for (const char* d : deny) if (ContainsI(n, d)) return false;
    return true;
}

static void ScanCrowd(void* level, const float yp[3], float rMin, float rMax)
{
    g_nCand = g_nCops = g_nPeople = 0;
    uint8_t* mgr = Fn<wd::KMgrFn>(wd::FN_KMGR)(level);
    if (!PtrOk(mgr)) return;
    int zones = 0;
    for (uint8_t* z = At<uint8_t*>(mgr, 0xE0); PtrOk(z) && zones < 64; z = At<uint8_t*>(z, 8), ++zones)
    {
        uint8_t* base = At<uint8_t*>(z, 0x38);
        const int stride = At<int32_t>(z, 0x40), n = At<int32_t>(z, 0x44);
        if (!PtrOk(base) || stride < 0x100 || stride > 0x2000 || n <= 0 || n > 4096) continue;
        for (int i = 0; i < n && g_nCand < kMaxCands; ++i)
        {
            uint8_t* k = base + static_cast<size_t>(i) * static_cast<size_t>(stride);
            if (At<uint16_t>(k, 0xD2) & 0xD || !PtrOk(At<void*>(k, 0xF8))) continue;   // going, or riding
            float d2 = 0.0f, pos[3];
            for (int c = 0; c < 3; ++c) { pos[c] = At<float>(k, 0xB0 + 4 * c); const float dc = pos[c] - yp[c]; d2 += dc * dc; }
            const float d = std::sqrt(d2);
            if (!IsFin(d) || d < rMin || d > rMax) continue;
            const char* name = wd::KrawlieDefName(k);
            if (!Castable(name) || !wd::KrawlieConvertible(k)) continue;
            Cand& c = g_cand[g_nCand++];
            c.k = k; c.zone = z; c.slot = i; c.d = d; c.pos[0] = pos[0]; c.pos[1] = pos[1]; c.pos[2] = pos[2];
            c.cop = ContainsI(name, "cop") || ContainsI(name, "officer");
            c.beach = ContainsI(name, "lifeguard") || ContainsI(name, "hula") || ContainsI(name, "surf");
            ++(c.cop ? g_nCops : g_nPeople);
        }
    }
}

// ---- the scenes ----------------------------------------------------------------------------------
static const int kMaxCast = 8, kMaxScenes = 4;
struct Scene
{
    uint8_t type;
    uint8_t phase;
    float   t, pt;          // s since the start / in this phase
    float   next, dur;      // the next beat / the main phase's length
    int     n;
    Person  m[kMaxCast];
    int     id;
    int     step;
    bool    fight, toilet, party;
    float   anchor[3];
    uint8_t* talk;          // the conversation's master task (Talking)
    int     round;          // conversation rounds / jog legs
    float   leg;            // s to the joggers' next breather
    bool    copIn;          // the pickpocket: a cop from the crowd joined the chase
};
static Scene g_sc[kMaxScenes];
static int   g_nSc = 0, g_nextId = 1;

static int PeopleInScenes()
{
    int n = 0;
    for (int i = 0; i < g_nSc; ++i) n += g_sc[i].n;
    return n;
}

// Turn the candidate into a person (false: it changed since the scan, or the game refused). The
// scan is up to a second old and the zone pools stay dense (a deleted krawlie's slot takes the
// last one), so the slot is checked again first.
static bool Convert(const Cand& c, Person& p)
{
    p = Person{};
    if (!PtrOk(c.zone) || c.slot >= At<int32_t>(c.zone, 0x44)) return false;
    if (At<uint16_t>(c.k, 0xD2) & 0xD || !Castable(wd::KrawlieDefName(c.k)) || !wd::KrawlieConvertible(c.k)) return false;
    uint8_t* kt = KType(c.k);
    const bool female = PtrOk(kt) && (At<uint8_t>(kt, 0x158) & 2);
    uint8_t* ch = Fn<wd::Kr2CharFn>(wd::FN_KR2CHAR)(c.k);
    if (!PtrOk(ch) || At<uint8_t>(ch, 0x3E1)) return false;
    reinterpret_cast<wd::ChFn>(g_base + wd::FN_TODEF)(ch);
    Fn<wd::ClearFlagFn>(wd::FN_CLEARFLAG)(ch, wd::FLAG_INVULNERABLE, &wd::kFlagLayer0);   // a converted krawlie is invulnerable
    if (!wd::Watch(ch, &p.watch)) { OrderRelease(ch); return false; }  // no way to tell when he's gone: let him go
    p.ch = ch; p.def = At<void*>(ch, 0x650); p.brain = At<void*>(ch, 0x78); p.female = female;
    return true;
}

// Cast `n` people who stand near each other: a random seed, then the nearest to it within 12 u.
static int Cast(Scene& s, int n, bool cops, bool beach)
{
    int idx[kMaxCast], got = 0;
    int seeds[kMaxCands], ns = 0;
    for (int i = 0; i < g_nCand; ++i)                                // the beach: its own types first
        if (g_cand[i].cop == cops && (!beach || g_cand[i].beach)) seeds[ns++] = i;
    if (ns == 0 && beach)
        for (int i = 0; i < g_nCand; ++i) if (g_cand[i].cop == cops) seeds[ns++] = i;
    if (ns == 0) return 0;
    const int seed = seeds[static_cast<int>(Rnd() * ns) % ns];
    idx[got++] = seed;
    while (got < n)
    {
        int best = -1;
        float bd = 12.0f;
        for (int i = 0; i < g_nCand; ++i)
        {
            if (g_cand[i].cop != cops) continue;
            bool used = false;
            for (int j = 0; j < got; ++j) used |= idx[j] == i;
            if (used) continue;
            float d2 = 0.0f;
            for (int c = 0; c < 3; ++c) { const float dc = g_cand[i].pos[c] - g_cand[seed].pos[c]; d2 += dc * dc; }
            const float d = std::sqrt(d2);
            if (d < bd) { bd = d; best = i; }
        }
        if (best < 0) break;
        idx[got++] = best;
    }
    s.n = 0;
    for (int j = 0; j < got && s.n < kMaxCast; ++j)
        if (Convert(g_cand[idx[j]], s.m[s.n])) ++s.n;
    for (int c = 0; c < 3; ++c) s.anchor[c] = g_cand[seed].pos[c];
    // the converted krawlies are gone from the pool: drop them from the candidates
    for (int j = 0; j < got; ++j) g_cand[idx[j]].d = -1.0f;
    int k = 0;
    for (int i = 0; i < g_nCand; ++i) if (g_cand[i].d >= 0.0f) g_cand[k++] = g_cand[i];
    g_nCand = k;
    return s.n;
}

// Add up to `n` onlookers from the crowd near the scene (an argument turning into a fight).
static void AddOnlookers(Scene& s, int n, int budget)
{
    for (int a = 0; a < n && s.n < kMaxCast && budget > 0; ++a)
    {
        int best = -1;
        float bd = 20.0f;
        for (int i = 0; i < g_nCand; ++i)
        {
            if (g_cand[i].cop) continue;
            float d2 = 0.0f;
            for (int c = 0; c < 3; ++c) { const float dc = g_cand[i].pos[c] - s.anchor[c]; d2 += dc * dc; }
            if (std::sqrt(d2) < bd) { bd = std::sqrt(d2); best = i; }
        }
        if (best < 0) return;
        Person& p = s.m[s.n];
        if (Convert(g_cand[best], p))
        {
            ++s.n; --budget;
            Go(p, s.m[0], 4.0f + a * 0.8f, GAIT_RUN);
        }
        g_cand[best] = g_cand[--g_nCand];
    }
}

static void Next(Scene& s, uint8_t phase) { s.phase = phase; s.pt = 0.0f; s.next = 0.0f; }

static bool Gathered(const Scene& s, float within)
{
    for (int i = 1; i < s.n; ++i) if (Dist(s.m[i], s.m[0]) > within) return false;
    return true;
}

static void FaceRing(Scene& s)                      // everyone faces the lead; the lead faces the first
{
    if (s.n > 1) Face(s.m[0], s.m[1].ch);
    for (int i = 1; i < s.n; ++i) Face(s.m[i], s.m[0].ch);
}

// One tick of a scene: false = it's over (everyone let go by the caller).
static bool Step(Scene& s, float dt, uint8_t* you)
{
    s.t += dt; s.pt += dt;
    Person* m = s.m;
    switch (s.type)
    {
    case life::kChat:
    case life::kCops:
        if (s.phase == 0) { for (int i = 1; i < s.n; ++i) Go(m[i], m[0], 1.5f, GAIT_WALK); Next(s, 1); }
        else if (s.phase == 1)
        {
            if (Gathered(s, 2.6f) || s.pt > 10.0f)
            {
                FaceRing(s);
                if (s.type == life::kChat) Next(s, 6);                 // the game's own conversation
                else { Next(s, 2); s.next = 1.0f; s.dur = 15.0f + Rnd() * 20.0f; }
            }
        }
        else if (s.phase == 6)                                         // a round: one starts, the other answers
        {
            if (s.pt < 0.6f) return true;                              // (they turn to each other first)
            const Person& a = m[s.round & 1 ? 1 : 0];
            const Person& b = m[s.round & 1 ? 0 : 1];
            s.talk = Talk(a, b);
            if (!s.talk) { Next(s, 2); s.next = 0.5f; s.dur = 15.0f + Rnd() * 15.0f; return true; }   // gestures instead
            for (int i = 2; i < s.n; ++i) Face(m[i], a.ch);            // a third one listens in
            Next(s, 7);
        }
        else if (s.phase == 7)
        {
            if (s.step == 0 && s.n > 2 && s.pt > 0.8f) { s.step = 1; for (int i = 2; i < s.n; ++i) ANY(m[i], kListen, -1); }
            const Person& a = m[s.round & 1 ? 1 : 0];
            if (!Talking(a, s.talk) || s.pt > 30.0f)
            {
                if (Talking(a, s.talk)) Fn<wd::ClearQFn>(wd::FN_CLEARQ)(At<void*>(a.ch, 0x78), 2);   // stuck: end it
                s.talk = nullptr; s.step = 0;
                if (++s.round < 2 + (Rnd() < 0.5f ? 1 : 0)) { FaceRing(s); Next(s, 6); }
                else { for (int i = 0; i < s.n; ++i) ANY(m[i], kGoodbye, 1); Next(s, 3); }
            }
        }
        else if (s.phase == 2)
        {
            if (s.pt >= s.next)
            {
                const Person& who = m[static_cast<int>(Rnd() * s.n) % s.n];
                if (s.type == life::kCops) ANY(who, kCopGestures, 1); else ANY(who, kGestures, 1);
                s.next = s.pt + 2.5f + Rnd() * 2.5f;
            }
            if (s.pt > s.dur) { for (int i = 0; i < s.n; ++i) ANY(m[i], kGoodbye, 1); Next(s, 3); }
        }
        else return s.pt < 2.0f;
        return true;

    case life::kPhone:
        if (s.phase == 0)
        {
            s.dur = 12.0f + Rnd() * 18.0f;
            if (Play(m[0], "Phone_Intro", 1)) Next(s, 1);
            else if (ANY(m[0], kPhoneLoop, -1)) Next(s, 2);
            else return false;                                           // no phone in his set
        }
        else if (s.phase == 1) { if (s.pt > 1.6f) { ANY(m[0], kPhoneLoop, -1); Next(s, 2); } }
        else if (s.phase == 2)
        {
            if (s.step == 0 && s.pt > s.dur * 0.5f) { s.step = 1; if (ANY(m[0], kGestures, 1)) s.next = s.pt + 2.0f; }
            if (s.step == 1 && s.next > 0.0f && s.pt > s.next) { s.next = 0.0f; ANY(m[0], kPhoneLoop, -1); }
            if (s.pt > s.dur) { if (!Play(m[0], "Phone_Outro", 1)) return false; Next(s, 3); }
        }
        else return s.pt < 1.8f;
        return true;

    case life::kPhoto:
        if (s.phase == 0) { Go(m[1], m[0], 3.2f, GAIT_WALK); Next(s, 1); }
        else if (s.phase == 1) { if (Dist(m[1], m[0]) < 4.4f || s.pt > 8.0f) { Face(m[0], m[1].ch); Face(m[1], m[0].ch); Next(s, 2); s.next = 0.8f; } }
        else if (s.phase == 2 || s.phase == 3)          // 2: the first poses, the second snaps; 3: swapped
        {
            const Person& model = m[s.phase == 2 ? 0 : 1];
            const Person& snapper = m[s.phase == 2 ? 1 : 0];
            if (s.next > 0.0f && s.pt >= s.next) { s.next = 0.0f; ANY(model, kPose, -1); ANY(snapper, kSnap, 2); }
            if (s.pt > 7.0f)
            {
                if (s.phase == 2) { Next(s, 3); s.next = 0.5f; }
                else { Play(m[0], "PointAtObject", 1) || Play(m[0], "point", 1); Next(s, 4); }
            }
        }
        else return s.pt < 2.0f;
        return true;

    case life::kCheerUp:
        if (s.phase == 0) { if (!Play(m[0], "Sad_Idle", -1)) return false; Next(s, 1); }
        else if (s.phase == 1) { if (s.pt > 4.0f) { Go(m[1], m[0], 1.3f, GAIT_WALK); Next(s, 2); } }
        else if (s.phase == 2) { if (Dist(m[1], m[0]) < 2.2f || s.pt > 10.0f) { Face(m[0], m[1].ch); Face(m[1], m[0].ch); Next(s, 3); } }
        else if (s.phase == 3)
        {
            if (s.step == 0 && s.pt > 0.8f) { s.step = 1; ANY(m[1], kGive, 1); }
            if (s.step == 1 && s.pt > 3.0f) { s.step = 2; ANY(m[0], kCheered, 1); vc::PedSay(vm::kThanksGift, m[0].ch, m[0].female); }
            if (s.pt > 5.5f) { Go(m[0], m[1], 1.4f, GAIT_WALK); Next(s, 4); }
        }
        else return s.pt < 4.0f;
        return true;

    case life::kQueue:
        if (s.phase == 0)
        {
            s.toilet = Rnd() < 0.15f;
            for (int i = 1; i < s.n; ++i) Go(m[i], m[i - 1], 0.9f, GAIT_WALK);
            Next(s, 1);
        }
        else if (s.phase == 1)
        {
            if (s.pt > 7.0f)
            {
                if (!s.toilet) ANY(m[0], kFront, -1);
                for (int i = s.toilet ? 0 : 1; i < s.n; ++i) { if (s.toilet) ANY(m[i], kToilet, -1); else ANY(m[i], kQueue, -1); }
                Next(s, 2); s.next = 9.0f + Rnd() * 4.0f;
            }
        }
        else if (s.phase == 2 && s.pt > s.next)
        {
            LetGo(m[0]);                                               // the front's done: the line moves up
            for (int i = 1; i < s.n; ++i) m[i - 1] = m[i];
            if (--s.n <= 1) return false;
            for (int i = 1; i < s.n; ++i) Go(m[i], m[i - 1], 0.9f, GAIT_WALK);
            Next(s, 1); s.pt = 4.0f;                                   // a few seconds to shuffle up
        }
        return true;

    case life::kWorkout:
        if (s.phase == 0) { for (int i = 1; i < s.n; ++i) Go(m[i], m[0], 2.6f + 0.3f * i, GAIT_WALK); Next(s, 1); }
        else if (s.phase == 1) { if (Gathered(s, 4.5f) || s.pt > 10.0f) { FaceRing(s); Next(s, 2); s.next = 1.2f; } }
        else if (s.phase == 2)
        {
            if (s.pt >= s.next)
            {
                if (s.step >= 6) { for (int i = 0; i < s.n; ++i) ANY(m[i], kBreath, 1); Next(s, 3); return true; }
                const char* move = kWorkout[s.step % 3];
                for (int i = 0; i < s.n; ++i) if (!Play(m[i], move, -1)) ANY(m[i], kBreath, -1);
                ++s.step; s.next = s.pt + 6.0f;
            }
        }
        else return s.pt < 3.0f;
        return true;

    case life::kDance:
    case life::kBeach:
        if (s.type == life::kBeach && !s.party)                         // daytime beach: each to their own
        {
            if (s.phase == 0)
            {
                s.dur = 30.0f + Rnd() * 20.0f;
                int busy = 0;
                for (int i = 0; i < s.n; ++i) busy += ANY(m[i], kBeachDay, -1) != nullptr;
                if (!busy) return false;
                Next(s, 1);
            }
            else if (s.phase == 1 && s.pt > s.dur) { for (int i = 0; i < s.n; ++i) ANY(m[i], kGoodbye, 1); Next(s, 2); }
            else if (s.phase == 2) return s.pt < 2.0f;
            return true;
        }
        if (s.phase == 0) { for (int i = 1; i < s.n; ++i) Go(m[i], m[0], 1.8f + 0.45f * i, GAIT_WALK); Next(s, 1); }
        else if (s.phase == 1) { if (Gathered(s, 5.5f) || s.pt > 10.0f) { FaceRing(s); Next(s, 2); s.next = 0.8f; s.dur = 18.0f + Rnd() * 14.0f; } }
        else if (s.phase == 2)
        {
            if (s.next > 0.0f && s.pt >= s.next)
            {
                s.next = 0.0f;
                if (s.party) Play(m[0], "BoomBox_Idle", -1) || ANY(m[0], kDance, -1);   // the beach party's DJ
                else ANY(m[0], kDance, -1);
                for (int i = 1; i < s.n; ++i)
                    if (Rnd() < 0.62f) { if (!ANY(m[i], kDance, -1)) ANY(m[i], kClap, -1); }
                    else ANY(m[i], kClap, -1);
            }
            if (s.pt > s.dur) { for (int i = 0; i < s.n; ++i) ANY(m[i], kCelebrate, 1); Next(s, 3); }
        }
        else return s.pt < 2.5f;
        return true;

    case life::kBusker:
        if (s.phase == 0)
        {
            if (!ANY(m[0], kBusk, -1)) return false;                    // nothing to perform with
            for (int i = 1; i < s.n; ++i) Go(m[i], m[0], 3.0f + 0.6f * i, GAIT_WALK);
            Next(s, 1); s.dur = 25.0f + Rnd() * 15.0f;
        }
        else if (s.phase == 1)
        {
            if (s.step == 0 && (Gathered(s, 6.0f) || s.pt > 9.0f))
            {
                s.step = 1;
                for (int i = 1; i < s.n; ++i) { Face(m[i], m[0].ch); }
                s.next = s.pt + 1.0f;
            }
            if (s.step == 1 && s.pt > s.next) { s.step = 2; for (int i = 1; i < s.n; ++i) ANY(m[i], kClap, -1); }
            if (s.pt > s.dur) { for (int i = 1; i < s.n; ++i) Play(m[i], "Cheer_Clap", 1); Next(s, 2); }
        }
        else return s.pt < 2.5f;
        return true;

    case life::kFight:
        if (s.phase == 0) { Go(m[1], m[0], 1.3f, GAIT_WALK); Next(s, 1); }
        else if (s.phase == 1)
        {
            if (Dist(m[1], m[0]) < 2.2f || s.pt > 10.0f)
            { Face(m[0], m[1].ch); Face(m[1], m[0].ch); Next(s, 2); s.next = 0.6f; s.dur = 6.0f + Rnd() * 4.0f; s.fight = Rnd() < 0.4f; }
        }
        else if (s.phase == 2)                                         // the argument
        {
            if (s.pt >= s.next) { ANY(m[s.step & 1], kArgue, 1); ++s.step; s.next = s.pt + 1.6f; }
            if (s.pt > s.dur)
            {
                if (!s.fight) { Play(m[0], "Shrug", 1); Play(m[1], "Shrug", 1); Next(s, 5); return true; }
                // a fist fight: the first one is the aggressor (a Baddie - you can arrest him)
                m[0].side0 = At<uint32_t>(m[0].ch, 0xD0) & wd::SIDE_BITS;
                SetSideBits(m[0].ch, wd::SIDE_BADDIE);
                m[0].sideSet = true;
                OrderFight(m[0].ch, m[1].ch);
                OrderFight(m[1].ch, m[0].ch);
                AddOnlookers(s, 3, g_p.maxPeople - PeopleInScenes());
                Log("    life: argument %d turns into a fist fight (%d watching)\n", s.id, s.n - 2);
                Next(s, 3); s.dur = 10.0f + Rnd() * 5.0f;
            }
        }
        else if (s.phase == 3)                                         // the fight; the ring watches
        {
            if (s.step < 100 && s.pt > 2.5f) { s.step = 100; for (int i = 2; i < s.n; ++i) { Face(m[i], m[0].ch); } }
            if (s.step == 100 && s.pt > 3.5f) { s.step = 101; for (int i = 2; i < s.n; ++i) ANY(m[i], kWatch, -1); }
            if (s.step >= 101 && static_cast<int>(s.pt) % 3 == 0 && s.pt - static_cast<int>(s.pt) < dt)
            { OrderFight(m[0].ch, m[1].ch); OrderFight(m[1].ch, m[0].ch); }   // keep them at it
            if (s.pt > s.dur) { if (m[0].sideSet) { SetSideBits(m[0].ch, m[0].side0); m[0].sideSet = false; } Next(s, 4); }
        }
        else if (s.phase == 4) { if (s.pt > 0.3f && s.step < 200) { s.step = 200; for (int i = 0; i < s.n; ++i) Play(m[i], "Shrug", 1); } return s.pt < 2.0f; }
        else return s.pt < 2.0f;
        return true;

    case life::kFans:
        if (!PtrOk(you)) return false;
        if (s.phase == 0) { OrderGoto(m[0].ch, you, 2.2f, GAIT_WALK); Next(s, 1); }
        else if (s.phase == 1)
        {
            float q[3]; for (int k = 0; k < 3; ++k) q[k] = At<float>(you, 0xA8 + 4 * k);
            const float d = DistCh(m[0].ch, q);
            if (d > 25.0f || s.pt > 14.0f) return false;              // you walked away
            if (d < 3.2f) { Face(m[0], you); Next(s, 2); }
        }
        else if (s.phase == 2)
        {
            if (s.step == 0 && s.pt > 0.9f) { s.step = 1; ANY(m[0], kGoodbye, 1); }
            if (s.step == 1 && s.pt > 2.6f) { s.step = 2; if (Rnd() < 0.5f) ANY(m[0], kPose, 2); else ANY(m[0], kSnap, 2); }
            if (s.pt > 6.5f) return false;
        }
        return true;

    case life::kGag:
        if (s.phase == 0) { if (!ANY(m[0], kGag, 1)) return false; Next(s, 1); }
        return s.pt < 5.0f;

    case life::kJog:                                                   // a jogging club: warm up, run, breathe
    {
        auto run = [&]() {
            SetSpeed(m[0], 1.0f, 1);                                   // a jog: the run gait, slowed (Notes\40b §1)
            Wander(m[0], GAIT_RUN);
            for (int i = 1; i < s.n; ++i) { SetSpeed(m[i], 1.08f, 1); Go(m[i], m[0], 1.4f + 0.9f * i, GAIT_RUN); }
        };
        if (s.phase == 0) { for (int i = 1; i < s.n; ++i) Go(m[i], m[0], 1.6f + 0.5f * i, GAIT_RUN); Next(s, 1); }
        else if (s.phase == 1) { if (Gathered(s, 4.0f) || s.pt > 8.0f) { FaceRing(s); Next(s, 2); s.next = 0.8f; } }
        else if (s.phase == 2)                                         // the warm-up: jogging on the spot
        {
            if (s.next > 0.0f && s.pt >= s.next)
            {
                s.next = 0.0f;
                for (int i = 0; i < s.n; ++i) if (!Play(m[i], "Jogging", -1)) ANY(m[i], kBreath, -1);
            }
            if (s.pt > 6.0f) { run(); Next(s, 3); s.next = 2.0f; s.dur = 65.0f + Rnd() * 30.0f; s.leg = 20.0f + Rnd() * 12.0f; }
        }
        else if (s.phase == 3)                                         // the run: the leader picks the way
        {
            if (s.pt >= s.next)                                        // (a goto ends on arrival: keep them on him)
            {
                for (int i = 1; i < s.n; ++i) Go(m[i], m[0], 1.4f + 0.9f * i, GAIT_RUN);
                s.next = s.pt + 2.0f;
            }
            if (s.round == 0 && s.pt > s.leg) { s.round = 1; for (int i = 0; i < s.n; ++i) ANY(m[i], kBreath, 1); Next(s, 4); }
            if (s.t > s.dur) { for (int i = 0; i < s.n; ++i) ANY(m[i], kBreath, 1); Next(s, 5); }
        }
        else if (s.phase == 4)                                         // a breather, then on again
        {
            if (s.step == 0 && s.pt > 2.2f) { s.step = 1; for (int i = 0; i < s.n; ++i) if (!Play(m[i], "Jogging", -1)) ANY(m[i], kBreath, -1); }
            if (s.pt > 6.0f) { s.step = 0; run(); Next(s, 3); s.next = 2.0f; }
        }
        else return s.pt < 2.5f;
        return true;
    }

    case life::kThief:                                                 // m[0] the thief, m[1] the victim, m[2] a cop
        if (s.phase == 0) { Go(m[0], m[1], 0.8f, GAIT_WALK); Next(s, 1); }
        else if (s.phase == 1)
        {
            if (Dist(m[0], m[1]) < 1.4f || s.pt > 12.0f)
            {
                // the snatch: he bolts, a Baddie now - tackle and arrest him
                m[0].side0 = At<uint32_t>(m[0].ch, 0xD0) & wd::SIDE_BITS;
                SetSideBits(m[0].ch, wd::SIDE_BADDIE);
                m[0].sideSet = true;
                Flee(m[0], m[1].ch, 2);
                Play(m[1], "point", 1);
                // a cop in the crowd nearby gives chase
                float tp[3];
                for (int k = 0; k < 3; ++k) tp[k] = At<float>(m[0].ch, 0xA8 + 4 * k);
                int best = -1;
                float bd = 35.0f;
                for (int i = 0; i < g_nCand; ++i)
                {
                    if (!g_cand[i].cop) continue;
                    float d2 = 0.0f;
                    for (int k = 0; k < 3; ++k) { const float dk = g_cand[i].pos[k] - tp[k]; d2 += dk * dk; }
                    if (std::sqrt(d2) < bd) { bd = std::sqrt(d2); best = i; }
                }
                if (best >= 0 && s.n < kMaxCast && PeopleInScenes() < g_p.maxPeople && Convert(g_cand[best], s.m[s.n]))
                {
                    Go(s.m[s.n], m[0], 1.0f, GAIT_SPRINT);
                    ++s.n; s.copIn = true;
                }
                if (best >= 0) g_cand[best] = g_cand[--g_nCand];
                Log(">>> life: pickpocket %d - stop, thief! (%s robs %s%s)\n", s.id, CharDefName(m[0].ch), CharDefName(m[1].ch),
                    s.copIn ? "; a cop gives chase" : "");
                Next(s, 2); s.dur = 40.0f; s.next = 4.0f; s.leg = 1.5f;
            }
        }
        else if (s.phase == 2)
        {
            if (s.step == 0 && s.pt > 1.2f) { s.step = 1; Go(m[1], m[0], 1.2f, GAIT_SPRINT); }   // the victim gives chase...
            if (s.step == 1 && s.pt > 7.0f) { s.step = 2; ANY(m[1], kRobbed, -1); }              // ...and gives up
            if (s.pt >= s.next)                                        // keep him running: from you, the cop, or her
            {
                float q[3];
                for (int k = 0; k < 3; ++k) q[k] = PtrOk(you) ? At<float>(you, 0xA8 + 4 * k) : 1e9f;
                uint8_t* from = PtrOk(you) && DistCh(m[0].ch, q) < 15.0f ? you : s.copIn && s.n > 2 ? m[2].ch : m[1].ch;
                Flee(m[0], from, 2);
                s.next = s.pt + 4.0f;
            }
            if (s.copIn && s.n > 2 && s.pt >= s.leg)                   // the cop closes in and tackles
            {
                if (Dist(m[2], m[0]) < 1.8f) OrderFight(m[2].ch, m[0].ch);
                else Go(m[2], m[0], 1.0f, GAIT_SPRINT);
                s.leg = s.pt + 1.5f;
            }
            if (s.pt > s.dur)
            {
                Log("    life: pickpocket %d - the thief got away\n", s.id);
                if (Alive(m[1])) vc::PedSay(vm::kGotAway, m[1].ch, m[1].female);   // the victim, let down
                return false;
            }
        }
        return true;

    case life::kOnlookers:                                             // your crash: they turn, stare, film it
        if (!PtrOk(you)) return false;
        if (s.phase == 0) { for (int i = 0; i < s.n; ++i) Face(m[i], you); Next(s, 1); }
        else if (s.phase == 1)
        {
            if (s.pt > 1.0f)
            {
                for (int i = 0; i < s.n; ++i) ANY(m[i], kWatch, 2);
                const Person& w = m[static_cast<int>(Rnd() * s.n) % s.n];
                vc::PedSay(vm::kWitness, w.ch, w.female);                 // one of them says what they think
                Next(s, 2);
            }
        }
        else return s.pt < 6.0f;
        return true;

    default:
        return false;
    }
}

// ---- the director -------------------------------------------------------------------------------
static void EndScene(int i, const char* why)
{
    Scene& s = g_sc[i];
    for (int j = 0; j < s.n; ++j) LetGo(s.m[j]);
    Log("    life: %s %d over after %.0f s (%s)\n", life::kSceneName[s.type], s.id, s.t, why);
    g_sc[i] = g_sc[--g_nSc];
}

static void EndAll(const char* why) { while (g_nSc) EndScene(g_nSc - 1, why); }

static void ForgetAll() { g_nSc = 0; }                   // a new world: the pointers mean nothing now

static uint32_t      g_scanT = 0;
static volatile bool g_stopReq = false;   // unhook: let everyone go on the next tick

// Once a tick from cPopulationManager::Update (lcudrive.cpp RunLife, after wanted mode).
//   on:      [life] enable and lcudrive on (`]`)
//   blocked: a story mission or a wanted level - the street empties of scenes
static void Tick(void* level, float dt, const Config& cfg, int district, bool on, bool blocked)
{
    uint8_t* p1 = *reinterpret_cast<uint8_t**>(g_base + eng::RVA_PLAYER1);
    if (!PtrOk(p1)) return;
    if (!g_init) { g_init = true; life::Reset(g_s, GetTickCount() | 1u); g_rng ^= GetTickCount(); }
    g_p.maxScenes = cfg.life_max_scenes < kMaxScenes ? cfg.life_max_scenes : kMaxScenes;
    g_p.maxPeople = cfg.life_max_people;
    g_p.every = cfg.life_every;
    g_p.fights = cfg.life_fights;
    for (float& m : g_p.mult) m = 1.0f;
    if (g_tune) g_tune(g_p);                                          // [round3] NPC overhaul stage 2
    float yp[3];
    void* veh = PlayerVehicle();
    void* body = veh ? wd::BodyOf(veh) : nullptr;
    for (int k = 0; k < 3; ++k) yp[k] = body ? At<float>(body, eng::BODY_POS + 4 * k) : At<float>(p1, 0xA8 + 4 * k);
    const float speed = body ? wd::SpeedOf(body) : 0.0f;              // (wanted.h's copy stops when wanted mode is off)
    const uint32_t now = GetTickCount();

    // the crowd's own reactions, scenes or not (no one is converted): a police chase tearing through
    // makes people run; so does your crash into them; your fists make them scatter
    if (on && !g_stopReq && cfg.life_reactions && !wd::MissionRunning())
    {
        static uint32_t s_chaseT = 0, s_screamT = 0, s_crash = 0, s_punch = 0, s_logT[3] = {};
        auto note = [&](int i, const char* what) { if (now - s_logT[i] > 30000) { s_logT[i] = now; Log("    life: %s\n", what); } };
        if (wd::g_shown >= 2 && veh && speed > 6.0f && now - s_chaseT > 1500)
        {
            s_chaseT = now;
            const bool scream = now - s_screamT > 6000;
            if (scream) s_screamT = now;
            Panic(level, yp, 14.0f, scream);
            note(0, "the crowd screams and runs from the police chase");
        }
        // (a scream when you run someone over or wreck the car; a fender-bender just makes them back off)
        const uint32_t inc = wd::g_incidentMs;
        if (inc && inc != s_crash && now - inc < 1000 && now - s_crash > 4000)
        {
            s_crash = inc;
            Panic(level, yp, 10.0f, wd::g_incidentKind != 2);
            note(1, "people back away from your crash");
        }
        const uint32_t punch = wd::g_punchMs;
        if (punch && punch != s_punch && now - punch < 1000 && now - s_punch > 1000)
        {
            s_punch = punch;
            Scatter(level, p1, yp, 9.0f);
            note(2, "the crowd scatters from your punches or shots");
        }
    }

    if (!on || blocked || g_stopReq)
    {
        if (g_nSc) EndAll(g_stopReq ? "lcudrive unhooked" : blocked ? "a mission or a wanted level" : "life is off");
        return;
    }

    // the scenes in play
    for (int i = g_nSc - 1; i >= 0; --i)
    {
        Scene& s = g_sc[i];
        if (s.n <= 0 || !Alive(s.m[0]))                                // the rest walk off
        {
            if (s.type == life::kThief && s.phase >= 2 && s.n > 1 && Alive(s.m[1]))
                vc::PedSay(vm::kThanks, s.m[1].ch, s.m[1].female);          // the thief was caught: the victim thanks
            EndScene(i, s.type == life::kThief && s.n > 0 ? "the thief was caught" : "its lead is gone");
            continue;
        }
        for (int j = s.n - 1; j >= 1; --j)                             // the others gone are dropped
            if (!Alive(s.m[j]))
            {
                wd::Unwatch(&s.m[j].watch);
                for (int q = j + 1; q < s.n; ++q) s.m[q - 1] = s.m[q];
                --s.n;
            }
        if (s.type == life::kJog || s.type == life::kThief)           // a scene on the move goes where its lead goes
            for (int c = 0; c < 3; ++c) s.anchor[c] = At<float>(s.m[0].ch, 0xA8 + 4 * c);
        float d2 = 0.0f;
        for (int c = 0; c < 3; ++c) { const float dc = s.anchor[c] - yp[c]; d2 += dc * dc; }
        const int need = s.type == life::kPhone || s.type == life::kGag || s.type == life::kFans ? 1 : 2;
        if (s.n < need) { EndScene(i, "its people are gone"); continue; }
        if (std::sqrt(d2) > 80.0f) { EndScene(i, "you left"); continue; }
        if (s.t > 120.0f) { EndScene(i, "the watchdog"); continue; }
        if (!Step(s, dt, p1)) EndScene(i, "done");
    }

    // your crash just now: a few people nearby turn and stare (at most one such crowd every 20 s)
    static uint32_t s_incident = 0, s_lastOnlookers = 0;
    const uint32_t inc = wd::g_incidentMs;
    if (inc && inc != s_incident && now - inc < 1500 && now - s_lastOnlookers > 20000 && g_nSc < kMaxScenes &&
        PeopleInScenes() + 2 <= g_p.maxPeople)
    {
        s_incident = inc;
        ScanCrowd(level, yp, 5.0f, 26.0f);
        g_scanT = 0;                                                   // the regular scan again next tick
        Scene& o = g_sc[g_nSc];
        o = Scene{};
        o.type = life::kOnlookers; o.id = g_nextId++;
        const int want = g_p.maxPeople - PeopleInScenes() >= 3 ? 3 : 2;
        if (Cast(o, want, false, false) >= 2)
        {
            ++g_nSc; s_lastOnlookers = now;
            Log(">>> life: onlookers %d - %d people stop to stare at your crash\n", o.id, o.n);
        }
        else for (int j = 0; j < o.n; ++j) LetGo(o.m[j]);
    }

    // a new one, when the scheduler says so (the crowd scanned at most once a second)
    if (now - g_scanT > 1000)
    {
        g_scanT = now;
        ScanCrowd(level, yp, cfg.life_min_radius, cfg.life_radius);

        // and now and then two passers-by who happen to meet stop for a chat (the crowd's own, no one
        // converted): a pair within 1.6 u of each other, at most one pair every few seconds
        static uint32_t s_kchatT = 0, s_kchatLog = 0;
        static int s_kchats = 0;
        if (cfg.life_crowd_chats && now - s_kchatT > static_cast<uint32_t>(cfg.life_crowd_chat_every * 1000.0f))
        {
            for (int i = 0; i < g_nCand; ++i)
            {
                if (g_cand[i].cop) continue;
                int pair = -1;
                for (int j = i + 1; j < g_nCand && pair < 0; ++j)
                {
                    float d2 = 0.0f;
                    for (int k = 0; k < 3; ++k) { const float dk = g_cand[i].pos[k] - g_cand[j].pos[k]; d2 += dk * dk; }
                    if (!g_cand[j].cop && d2 < 1.6f * 1.6f) pair = j;
                }
                if (pair >= 0 && KChat(g_cand[i].k, g_cand[pair].k))
                {
                    s_kchatT = now; ++s_kchats;
                    if (now - s_kchatLog > 60000)
                    {
                        s_kchatLog = now;
                        Log("    life: passers-by stop to chat (%d pairs so far)\n", s_kchats);
                    }
                    g_cand[pair] = g_cand[--g_nCand];                  // theirs to finish: no scene casts them
                    g_cand[i] = g_cand[--g_nCand];
                    break;
                }
            }
        }
    }
    life::Ctx c;
    c.district = district;
    c.part = life::PartOfDay(lights::g_sunPhase);
    c.onFoot = veh == nullptr;
    c.speed = speed;
    c.people = g_nPeople;
    c.cops = g_nCops;
    int cast = 0;
    const int sc = life::Pick(g_p, g_s, c, dt, g_nSc, PeopleInScenes(), &cast);
    if (sc < 0 || g_nSc >= kMaxScenes) return;
    Scene& s = g_sc[g_nSc];
    s = Scene{};
    s.type = static_cast<uint8_t>(sc);
    s.id = g_nextId++;
    s.party = sc == life::kBeach && (c.part == life::kEvening || c.part == life::kNight || lights::g_night);
    const bool cops = life::kDefs[sc].cops;
    if (Cast(s, cast, cops, sc == life::kBeach) < life::kDefs[sc].minCast)
    {
        for (int j = 0; j < s.n; ++j) LetGo(s.m[j]);
        life::Failed(g_s, sc);
        return;
    }
    ++g_nSc;
    float d2 = 0.0f;
    for (int k = 0; k < 3; ++k) { const float dk = s.anchor[k] - yp[k]; d2 += dk * dk; }
    char who[160] = "";
    int w = 0;
    for (int j = 0; j < s.n && w < 140; ++j)
        w += _snprintf_s(who + w, sizeof(who) - w, _TRUNCATE, "%s%s", j ? ", " : "", CharDefName(s.m[j].ch));
    Log(">>> life: %s%s %d - %d %s (%s), %.0f u from you%s%s\n", life::kSceneName[sc], s.party ? " (party)" : "", s.id, s.n,
        s.n == 1 ? "person" : "people", who, std::sqrt(d2),
        district >= 0 && rd::kDistrictFill[district][0] ? ", " : "",
        district >= 0 && rd::kDistrictFill[district][0] ? rd::kDistrictFill[district] : "");
}

// ---- the krawlie dance party (2026-11-15; Notes\46 §1 item 1) ---------------------------------------
//  The cut script action PumpUpTheKrawlieJam(position, radius) = SAction_KrawliePartyTime (vtable
//  0x140C3C9D8, Execute 0x1400AF420), never called by any script [P]: the level is ctx->vt[1]() - the
//  same object CreateAICharacter hands getPM [P 0x1401067E8] and the one wd::GetWorld gives - and
//  0x14002F1F0 (wd::FN_KMGR) finds its krawlie manager; a sphere query 0x1406991F0(q, 4, 2, 0,
//  &pos4, r, r) (q: +0x20 shape 2, +0x40 centre, +0x50 r, +0x54 r^2 [P 0x14069A6C0]); a one-pointer
//  functor, vtable 0x140C3CA20 = CityKrawlieSimpleHookDance, whose one method (0x1400AF9C0) returns
//  the hook id H("DanceDance") (stored at start-up by 0x140BD8B10 in 0x1417FDEE8); 0x14069A760 hands
//  it to every krawlie in the sphere and returns whether any took it. Exactly Broadcast() above.
//  Who dances [P]: the hook is answered by CityKrawlieStationaryController (vtable 0x140C3C128, slot
//  75 = 0x1400B04D0): its five-move list (+0x68, 0x10 each) is rewritten with random moves - anim id
//  0x22..0x25 (the krawlie anim table at 0x14116D0E0: dance_01, dance_02, dance_03, and chicken_eat)
//  and three random times (0.5-1, 1-2, 3-4 s). So the crowd that STANDS dances: the .led "Stationary
//  Krawlie Controller" groups - the beach party (ny_beach 2210 / 2211 / 2310 / 2311, with its DJ and
//  chicken stand), Times Square's crowd (1613), the Cherry Tree Hills pool and cafes (sf_residential
//  0920 / 0922), the workout groups, the prison guards, the forest miners, the bridges' idlers (23
//  cells). Walking pedestrians fall through to the base handler 0x1406A95F0 [I: no reaction]. The new
//  moves stay until the cell streams out [I]. The anims: generic_extended.as (the city crowd's set,
//  beside AI_Exterior_Male / _Female and LVL_City) lists Ability_Dance, which has its own krawlie pak
//  (ability_dance_krw_nxg.pak, 559 KB) [P data] -> [I] they are loaded city-wide, not only on the
//  beach; [U] until seen. Off the beach the stationary groups are idlers and cafe-goers.
//  Triggers: Ctrl+Alt+B (player 1, the keyboard) and [life] party_pad held 0.8 s on a player's own
//  controller (the hotkey thread, RequestParty), round that player; and now and then by the beach
//  party itself while a player is within 70 u of it (life_model.h PartyAmbient; [life] party_every).
//  The music: none of our own - the beach party's DJ booth (2311) is where it happens [I].
static const uintptr_t VT_DANCE = 0xC3CA20;
static volatile LONG   g_partyReq = 0;          // the hotkey thread: the player who asked, + 1
static volatile LONG   g_partyBeep = 0;         // for the hotkey thread: 1 = they dance, 2 = nobody to dance
static life::PartyMem  g_party = { 1e6f };
// the beach party's dancers: ny_beach_2210 / 2211 krawlies.led, x 235.3..237.6, y 2.21, z 117.3..124.7 [P];
// the DJ and chicken stand in 2310 / 2311 next door (x 240..270, z 90..150)
static const float kBeachParty[3] = { 240.0f, 2.21f, 120.0f };

static void RequestParty(int player) { InterlockedExchange(&g_partyReq, player + 1); }

// Is player `player`'s own pad holding every button of `chord` right now? (Any thread: XInputGetState
// on a connected port is quick; an empty port is never probed - pad::PortFor only names connected ones.)
static bool PadChordDown(int player, uint32_t chord)
{
    if (!chord || !pad::g_getState) return false;
    const int u = pad::PortFor(player);
    if (u < 0) return false;
    XINPUT_STATE st;
    if (pad::g_getState(static_cast<DWORD>(u), &st) != ERROR_SUCCESS) return false;
    if ((chord & pad::kLT) && st.Gamepad.bLeftTrigger <= 128) return false;
    if ((chord & pad::kRT) && st.Gamepad.bRightTrigger <= 128) return false;
    const WORD b = static_cast<WORD>(chord & 0xFFFF);
    return (st.Gamepad.wButtons & b) == b;
}

// The hotkey thread, every pass while the game has focus: each player's own pad holding [life]
// party_pad for 0.8 s asks for a party round that player (once per hold).
static void HotkeyPads(const Config& cfg)
{
    static life::PadHold s_hold[2] = {};
    static DWORD s_t = GetTickCount();
    const DWORD now = GetTickCount();
    float dt = (now - s_t) / 1000.0f;
    s_t = now;
    if (!(dt >= 0.0f) || dt > 0.25f) dt = 0.25f;
    for (int i = 0; i < 2; ++i)
    {
        const bool down = cfg.life_party_hotkey && PadChordDown(i, static_cast<uint32_t>(cfg.life_party_pad));
        if (life::PadHoldTick(s_hold[i], down, dt, 0.8f))
        {
            Log(">>> player %d's controller: dance party\n", i + 1);
            RequestParty(i);
        }
    }
}

static bool Dance(void* level, const float p[3], float r)
{
    uint8_t* km = Fn<wd::KMgrFn>(wd::FN_KMGR)(level);
    if (!PtrOk(km)) return false;
    alignas(16) uint8_t q[0x100] = {};
    alignas(16) float p4[4] = { p[0], p[1], p[2], 1.0f };
    Fn<QInitFn>(FN_QINIT)(q, 4, 2, 0, p4, r, r);
    struct { uintptr_t vt; } hook = { g_base + VT_DANCE };
    return Fn<KHitFn>(FN_KHIT)(km, q, &hook);
}

// Once a tick, after Tick (lcudrive.cpp RunLife). enabled: lcudrive on; blocked: a mission or a wanted level.
static void Party(void* level, float dt, const Config& cfg, bool enabled, bool blocked)
{
    const LONG who = InterlockedExchange(&g_partyReq, 0);
    if (g_stopReq) return;
    float pp[2][3] = {}; bool ok[2] = { false, false };
    for (int i = 0; i < 2; ++i) ok[i] = ap::PlayerPos(i, pp[i]);
    if (who > 0)
    {
        const int i = who == 2 ? 1 : 0;
        if (!enabled || !cfg.life_party_hotkey)
            Log(">>> life: dance party asked for - %s\n", !enabled ? "lcudrive is off ([)" : "[life] party_hotkey = 0");
        else if (!ok[i]) Log(">>> life: dance party asked for by player %d - not in the game\n", i + 1);
        else
        {
            const bool any = Dance(level, pp[i], cfg.life_party_radius);
            life::PartyHappened(g_party);
            InterlockedExchange(&g_partyBeep, any ? 1 : 2);
            Log(">>> life: dance party round player %d at (%.0f, %.0f, %.0f), %.0f u: %s\n", i + 1, pp[i][0], pp[i][1], pp[i][2],
                cfg.life_party_radius, any ? "the people standing about start dancing"
                                           : "nobody to dance (only those standing about take it up: cafes, crowds, the beach party)");
        }
    }
    bool there = false;
    for (int i = 0; i < 2; ++i) if (ok[i] && ap::Dist2(pp[i], kBeachParty) < 70.0f) there = true;
    if (life::PartyAmbient(g_party, dt, there && enabled && cfg.life_enable, blocked, cfg.life_party_every, 180.0f, Rnd()))
    {
        const bool any = Dance(level, kBeachParty, 32.0f);
        Log(">>> life: the beach party gets going - %s\n", any ? "everyone standing about by the DJ dances" : "nobody there took it up");
    }
}

} // namespace lf
