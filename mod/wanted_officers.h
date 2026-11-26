// =============================================================================================
//  wanted_officers.h - the police on foot (Notes\38). Part of wanted.h (namespace wd).
//
//  Until 2026-10-25 the police never left their vehicles to pursue the player, so a
//  stun or tie with nobody outside to fire it looked wrong; and cops walking about did not
//  join in on the chase. While you're wanted and on foot:
//    * a chasing police car or motorbike within get-out range is held - the game's own StopTask in its AI's
//      slot 1, the one Chase's whistle uses (0x14016E433) - and once it has stopped, its krawlie
//      driver is pulled out with the game's own car-jack (rd->vt[68] 0x14069CFC0): he becomes a
//      character and jumps out, and we take him over when he lands (the dismount task's state 2);
//    * cop krawlies walking around within join range are turned into characters (0x1406A3F60);
//    * either kind runs after you: cGotoTargetOnFoot, the FollowCharacter recipe (0x140112830);
//    * the one picked to fire faces you and plays GrappleGun_shoot (every NPC cop def has
//      CLASS_Police -> Ability_Police -> Weapon_Grapple_Gun), and the stun / tie lands after the
//      wind-up; at a bust the nearest one plays the cuffing;
//    * when you drive off or the chase ends they're let go - the game's own "Convert To
//      Pedestrian" (Character_Release, 0x140113CF0): they walk off and turn back into crowd cops -
//      and the cars they left fade out once you're away from them.
//  officers_model.h holds each officer's decisions (harness-tested). Every game call in here is
//  behind its own fault switch (RunOfficers in wanted.h): an exception turns the officers off for
//  the session and leaves the rest of wanted mode running.
// =============================================================================================
#pragma once
#include "officers_model.h"

struct SP { void* obj; volatile LONG* rc; };            // the game's shared handle: rc[0] total, rc[1] strong

typedef uint8_t* (__fastcall* NewTaskFn)(void* brain, int id, int slot);
typedef void     (__fastcall* NewTargetFn)(SP* out);
typedef void     (__fastcall* TgtCharFn)(void* t, uint8_t* ch, uint32_t flags);
typedef void     (__fastcall* TgtRangeFn)(void* t, float range);
typedef void     (__fastcall* TaskTgtFn)(uint8_t* task, SP* sp, uint32_t gait);
typedef void     (__fastcall* FaceTgtFn)(uint8_t* task, SP* sp);
typedef void     (__fastcall* SpRelFn)(SP* sp);
typedef void     (__fastcall* ClearQFn)(void* brain, int slot);
typedef void     (__fastcall* EnqueueFn)(void* brain, void* task, int slot, bool front);
typedef void     (__fastcall* ClearFlagFn)(uint8_t* ch, int id, const int* layer);
typedef uint8_t* (__fastcall* Kr2CharFn)(uint8_t* k);
typedef uint8_t* (__fastcall* KMgrFn)(void* level);
typedef void     (__fastcall* APInitFn)(void* p);
typedef void     (__fastcall* APSetFn)(uint8_t* task, uint8_t* ch, void* p);
typedef void     (__fastcall* APFreeFn)(void* p);
typedef void*    (__fastcall* AnimSetFn)(uint8_t* ch);
typedef bool     (__fastcall* HasAnimFn)(void* set, int id);
typedef void*    (__fastcall* TaskAllocFn)(void* heap, size_t size, const void* tag);
typedef void*    (__fastcall* ToPedFn)(void* task, bool walkOff);
typedef void     (__fastcall* JackFn)(uint8_t* rd, void* unused);
typedef uint8_t* (__fastcall* GetFn)(void* self);
typedef void     (__fastcall* DestroyFn)(uint8_t* ud, void* opts);
typedef void     (__fastcall* AttackMgrFn)(SP* out, uint8_t* target);
typedef void     (__fastcall* AttackSetFn)(uint8_t* task, uint8_t* me, uint8_t* target, int32_t mgrIndex);
typedef uint32_t (__fastcall* GetMaskFn)(uint8_t* ch);
typedef void     (__fastcall* SetMaskFn)(uint8_t* ch, uint32_t mask);
typedef void     (__fastcall* AiResetFn)(void* brain);
typedef uint8_t* (__fastcall* ChDataFn)(uint8_t* ch);
typedef void*    (__fastcall* RefPoolFn)();
typedef void*    (__fastcall* RefPool1Fn)(void* x);
typedef void     (__fastcall* RefFreeFn)(void* pool, void* ctl, int zero);

static const uintptr_t FN_NEWTASK = 0x584B70, FN_NEWTARGET = 0x0D2840, FN_TGTCHAR = 0x711670, FN_TGTRANGE = 0x711D20,
                       FN_TASKTGT = 0x713170, FN_FACETGT = 0x71B1E0, FN_SPREL = 0x70B160, FN_CLEARQ = 0x585260,
                       FN_ENQUEUE = 0x584D00, FN_CLEARFLAG = 0x6687B0, FN_KR2CHAR = 0x6A3F60, FN_KMGR = 0x02F1F0,
                       FN_APINIT = 0x709B50, FN_APSET = 0x709D20, FN_APFREE = 0x709BA0, FN_ANIMSET = 0x669EC0,
                       FN_HASANIM = 0x814C20, FN_TASKALLOC = 0x2E7060, FN_TOPED = 0x11BA30,
                       FN_ATTACKMGR = 0x1760E0, FN_ATTACKSET = 0x0FED30, FN_GETMASK = 0x669DF0, FN_SETMASK = 0x66F510,
                       FN_AIRESET = 0x585F10, FN_CHDATA = 0x66A7C0, FN_REFPOOL0 = 0x2E05F0, FN_REFPOOL1 = 0x2E0590,
                       FN_REFFREE = 0x2DAF40;
static const uintptr_t RVA_MASK_GOODIE = 0x1C7E988, RVA_MASK_BADDIE = 0x1C7E98C;   // collision groups by side
static const uintptr_t RVA_VT_DISMOUNT = 0xD43B68;     // Task_KrawlieDismountVehicle: +0x58 state, +0x60 ch, +0x78 ud
static const uintptr_t RVA_VT_DISMOUNT_BOAT = 0xD43CD8; // Task_KrawlieDismountBoat (the jack picks it on water)
static const uintptr_t RVA_TASKHEAP = 0x1C57E70, RVA_TASKTAG = 0xC09828;
static const int ANIM_SHOOT = 0x7B8, ANIM_POINT = 0x6FD, ANIM_CUFFING = 0x998;   // GrappleGun_shoot, Point, cuffing
static const int TASK_GOTO_ON_FOOT = 0x13, TASK_ATTACK = 0x20, TASK_ANIM = 0x24, TASK_FACE = 0x28, TASK_STOP = 0x30;
static const uint32_t SIDE_BITS = 0x10005, SIDE_BADDIE = 1;      // ch+0xD0: bit0 Baddie, bit2 Neutral, bit16 GoodieBaddie
static const int SLOT_TOP = 2;                          // the brain runs only its highest non-empty queue
static const int FLAG_INVULNERABLE = 0x59;              // set on every krawlie turned into a character
// Status flags have three priority layers. 0x1406687B0 takes the layer as a THIRD argument, a pointer to an
// int (it only forwards r8 to 0x1406686F0, which reads it at once). The krawlie turn sets 2, 0x59 and 5 on
// layer 0 (0x1406A3EF1..0x1406A3F2A), and the game's own callers pass &0. The first build passed two
// arguments; the stale r8 faulted at 0x140668701 the moment the first officer landed (2026-10-26 12:01).
static const int kFlagLayer0 = 0;
// The other two flags the krawlie turn sets (0x1406A3EF6, 0x1406A3F2A), named by the legacy script
// functions that set the same ids: 2 = IgnoreAsTarget (0x1408545D0), 5 = ImmuneToBolts (0x140854B30).
// With 2 left on, the player's attacks never pick the officer: he can't be fought (2026-11-11).
static const int FLAG_IGNORE_AS_TARGET = 2, FLAG_IMMUNE_TO_BOLTS = 5;
// The game's own kill (0x14079AE80), with the arguments its terrain-kill and Doomed callers pass
// (0x140894382, 0x14068ADE6): type 3, no killer, 1, 1, 1, 0.0f. A LEGO character falls apart.
typedef void (__fastcall* KillFn)(uint8_t* ch, int type, void* by, int a4, int a5, int a6, float a7);
static const uintptr_t FN_KILL = 0x79AE80;
// A punch he can throw: the first of these his anim set has (ids from the static table 0x140FE3710).
static const int kPunchAnims[] = { 0x907 /*punch_right*/, 0x628 /*Prop_Punch_Right*/, 0x627 /*Prop_Punch_Left*/, 0x8A8 /*shove*/ };

template <class T> static T Fn(uintptr_t rva) { return reinterpret_cast<T>(g_base + rva); }
static uint8_t* VGet(void* self, int slot) { return reinterpret_cast<GetFn>((*reinterpret_cast<void***>(self))[slot])(self); }

// ---- one officer, one car -----------------------------------------------------------------------
struct OfficerSlot
{
    uint8_t*       ch;
    void*          def;          // ch+0x650 when taken: a reused allocation changes it...
    void*          brain;        // ...and ch+0x78 (cops share defs; a new character has its own brain)
    SP             watch;        // a game target on him: its weak ref clears when he's destroyed
    void*          car;          // the car he came out of (null: off the street)
    officers::Unit u;
    float          you[3];       // where you were at his last run order
    bool           shot;         // the aim's anim has started
    bool           flagCleared;  // the converted character's Invulnerable flag is off
    uint8_t        kind;         // the hit he's winding up (wanted::kStun / kTie)
    float          zapS;
    int            id;
    uint32_t       side0;        // his side bits when we took him (ch+0xD0 & 0x10005)
    bool           sideSet;      // ...changed to Baddie for the fight: put back when he's let go
    bool           hitByYou;     // you've hit him once: that's the crime (one per officer)
    float          seen[3];      // where he was at his last report line
    float          seenT;        // s since that line
    int            reissued;     // run orders given since that line
    uint32_t       gen;          // g_susGen at his last run order: a new suspect re-aims him at once
    int            hits;         // your blows on him so far
    uint32_t       lastHitMs;    // ...the last one (one swing can raise several hit events)
    bool           beaten;       // enough of them: he goes down for good at the next tick
    bool           killed;       // ...done
    float          punchT;       // s until his punch lands (0 = none thrown)
    bool           punchSwung;   // its anim has started
};
struct HeldCar
{
    void*    veh;
    uint8_t* ud;
    float    t;                  // s since we held it
    bool     pulled;             // its driver has been pulled out...
    bool     found;              // ...and became our officer (found a frame or two later)
    float    pullT;              // s since the pull
    bool     abandoned;          // nobody's coming back to it: fade it out once you're away
    int      id;
    bool     bike;               // a police motorbike (2016_Police_Dasher), not a car
};
static const int kMaxSlots = 8;
static OfficerSlot      g_os[kMaxSlots];
static HeldCar          g_hc[kMaxSlots];
static int              g_nOs = 0, g_nHc = 0, g_nextId = 1;
static officers::Params g_offP;
static volatile bool    g_offFault = false;
static float            g_holdCool = 0.0f, g_joinCool = 0.0f;
static uint32_t         g_walkerLog = 0;
static bool             g_shotLogged = false;
static float            g_inCarT = 0.0f;         // s you've been in a vehicle: they give up after 3
static float            g_punchCool = 0.0f;      // s until any officer may throw the next punch
static bool             g_wasDown = false;       // you were down last tick (a breather when you get up)
static int              g_hitsToBeat = 2;        // [wanted] officer_hits
static bool             g_punchAnimLogged = false;

// A character's own fields: +0x320 bit 0 = constructed (set by the ctor 0x140665630, cleared by the
// dtor), +0x3E0 = active in the world (0x14067AAC0 / 0x14067AB00), +0x3E1 = a player.
static bool LiveChar(uint8_t* ch, void* def)
{
    return PtrOk(ch) && (At<uint8_t>(ch, 0x320) & 1) && At<uint8_t>(ch, 0x3E0) && !At<uint8_t>(ch, 0x3E1) &&
           At<void*>(ch, 0x650) == def && !reinterpret_cast<DeadFn>(g_base + FN_DEAD)(ch);
}

// NPC characters are heap objects with no pool to check against (Notes\40b §7): a destroyed one's
// memory gets reused. So each one we hold gets a game target (TgtChar), whose weak reference
// clears when he's destroyed - 0x1407112D0 tests it.
typedef bool (__fastcall* TgtOkFn)(void* t);
static const uintptr_t FN_TGTOK = 0x7112D0;

static bool Watch(uint8_t* ch, SP* sp)
{
    *sp = SP{};
    Fn<NewTargetFn>(FN_NEWTARGET)(sp);
    if (!PtrOk(sp->obj) || !PtrOk(const_cast<LONG*>(sp->rc))) { *sp = SP{}; return false; }
    Fn<TgtCharFn>(FN_TGTCHAR)(sp->obj, ch, 0);
    return true;
}

static bool StillThere(const SP& sp) { return PtrOk(sp.obj) && Fn<TgtOkFn>(FN_TGTOK)(sp.obj); }

static void Unwatch(SP* sp)
{
    if (PtrOk(sp->obj) && PtrOk(const_cast<LONG*>(sp->rc))) Fn<SpRelFn>(FN_SPREL)(sp);
    *sp = SP{};
}

static bool Alive(const OfficerSlot& o)
{
    return StillThere(o.watch) && LiveChar(o.ch, o.def) && At<void*>(o.ch, 0x78) == o.brain;
}

static float DistCh(uint8_t* ch, const float p[3])
{
    float d2 = 0.0f;
    for (int k = 0; k < 3; ++k) { const float dk = At<float>(ch, 0xA8 + 4 * k) - p[k]; d2 += dk * dk; }
    const float d = std::sqrt(d2);
    return IsFin(d) ? d : 1e9f;
}

// ---- the game's AI, the way its own script actions drive it -------------------------------------
static const uint32_t GAIT_WALK = 0x2000000u, GAIT_RUN = 0x4000000u, GAIT_SPRINT = 0x8000000u;

// Go to `target` and stop within `range` (FollowCharacter 0x140112830, at the top slot: it replaces
// whatever ran there - a dismount task, an anim). The target is live: it follows them as they move.
static bool OrderGoto(uint8_t* ch, uint8_t* target, float range, uint32_t gait)
{
    void* brain = At<void*>(ch, 0x78);
    if (!PtrOk(brain)) return false;
    SP sp = {};
    Fn<NewTargetFn>(FN_NEWTARGET)(&sp);
    if (!PtrOk(sp.obj) || !PtrOk(const_cast<LONG*>(sp.rc))) return false;
    Fn<TgtCharFn>(FN_TGTCHAR)(sp.obj, target, gait | 0x2857);
    Fn<TgtRangeFn>(FN_TGTRANGE)(sp.obj, range);
    uint8_t* task = Fn<NewTaskFn>(FN_NEWTASK)(brain, TASK_GOTO_ON_FOOT, SLOT_TOP);
    if (task)
    {
        SP c = sp;
        InterlockedIncrement(c.rc + 1);
        InterlockedIncrement(c.rc);
        Fn<TaskTgtFn>(FN_TASKTGT)(task, &c, gait);                     // consumes the copy
    }
    Fn<SpRelFn>(FN_SPREL)(&sp);
    return task != nullptr;
}

// The brain (0x140585CC9..): three slots, each a pending task (+0x168 + 8*slot, what NewTask fills) and a
// queue (+0x180 + 0x58*slot: head +0x28, count +0x38). A pending task CLEARS its slot's queue and becomes its
// only entry at the next update, so every order restarts whatever ran there. The 2026-11-11 test: 25
// officers got out and none closed in over a minute - their run order was being renewed every 0.75 s. Now
// it's renewed only once the last one has ended (the goto ends on arrival; its target follows you live).
static bool SlotBusy(uint8_t* ch, int slot)
{
    uint8_t* brain = At<uint8_t*>(ch, 0x78);
    if (!PtrOk(brain)) return false;
    return At<void*>(brain, 0x168 + 8 * slot) != nullptr || At<int32_t>(brain, 0x180 + 0x58 * slot + 0x38) > 0;
}
static uintptr_t SlotHeadVt(uint8_t* ch, int slot)             // the running task's vtable RVA, 0 = none
{
    uint8_t* brain = At<uint8_t*>(ch, 0x78);
    uint8_t* t = PtrOk(brain) ? At<uint8_t*>(brain, 0x180 + 0x58 * slot + 0x28) : nullptr;
    return PtrOk(t) ? At<uintptr_t>(t, 0) - g_base : 0;
}

// Run at `you`, flat out, to arm's length.
static bool OrderRun(uint8_t* cop, uint8_t* you) { return OrderGoto(cop, you, 1.2f, GAIT_SPRINT); }

// Turn to face `you` (FaceCharacter 0x140111860).
static bool OrderFace(uint8_t* cop, uint8_t* you)
{
    void* brain = At<void*>(cop, 0x78);
    if (!PtrOk(brain)) return false;
    SP sp = {};
    Fn<NewTargetFn>(FN_NEWTARGET)(&sp);
    if (!PtrOk(sp.obj) || !PtrOk(const_cast<LONG*>(sp.rc))) return false;
    Fn<TgtCharFn>(FN_TGTCHAR)(sp.obj, you, 0);
    uint8_t* task = Fn<NewTaskFn>(FN_NEWTASK)(brain, TASK_FACE, SLOT_TOP);
    if (task)
    {
        SP c = sp;
        InterlockedIncrement(c.rc + 1);
        InterlockedIncrement(c.rc);
        Fn<FaceTgtFn>(FN_FACETGT)(task, &c);
    }
    Fn<SpRelFn>(FN_SPREL)(&sp);
    return task != nullptr;
}

static bool HasAnim(uint8_t* ch, int id)
{
    void* set = Fn<AnimSetFn>(FN_ANIMSET)(ch);
    return PtrOk(set) && Fn<HasAnimFn>(FN_HASANIM)(set, id);
}

// Play an anim `loops` times, or until the next order with loops < 0 (PlayContextAnimation
// 0x1401137F0: cAnimationTask and its params block - id +0, 1 at +4, "forever" +5, count +8).
// Not in his set: false, nothing ordered.
static bool OrderAnimN(uint8_t* ch, int id, int loops)
{
    void* brain = At<void*>(ch, 0x78);
    if (!PtrOk(brain) || id <= 0 || !HasAnim(ch, id)) return false;
    uint8_t* task = Fn<NewTaskFn>(FN_NEWTASK)(brain, TASK_ANIM, SLOT_TOP);
    if (!task) return false;
    alignas(16) uint8_t p[0x40] = {};
    Fn<APInitFn>(FN_APINIT)(p);
    *reinterpret_cast<int32_t*>(p) = id;
    p[4] = 1;
    p[5] = loops < 0 ? 1 : 0;
    *reinterpret_cast<int32_t*>(p + 8) = loops;
    Fn<APSetFn>(FN_APSET)(task, ch, p);
    Fn<APFreeFn>(FN_APFREE)(p);
    return true;
}

static bool OrderAnim(uint8_t* cop, int id) { return OrderAnimN(cop, id, 1); }

// His side (ch+0xD0): SetSide's own sequence on one character (0x1408573FA..0x1408574A6) - the
// bits, the collision group that goes with them (goodie / baddie masks at 0x141C7E988 / 98C), and
// when the side really changed, an AI reset.
static void SetSideBits(uint8_t* ch, uint32_t bits)
{
    uint32_t v = At<uint32_t>(ch, 0xD0);
    const uint32_t old = v & SIDE_BITS;
    v = (v & ~SIDE_BITS) | (bits & SIDE_BITS);
    At<uint32_t>(ch, 0xD0) = v;
    const uint32_t mG = *reinterpret_cast<const uint32_t*>(g_base + RVA_MASK_GOODIE);
    const uint32_t mB = *reinterpret_cast<const uint32_t*>(g_base + RVA_MASK_BADDIE);
    uint32_t mask = Fn<GetMaskFn>(FN_GETMASK)(ch) & ~(mG | mB);
    if (v & SIDE_BADDIE) mask |= mB;
    else if (!(v & 4)) mask |= mG;
    Fn<SetMaskFn>(FN_SETMASK)(ch, mask);
    if (!(v & old))
    {
        void* brain = At<void*>(ch, 0x78);
        if (PtrOk(brain)) Fn<AiResetFn>(FN_AIRESET)(brain);
        uint8_t* cd = Fn<ChDataFn>(FN_CHDATA)(ch);
        if (PtrOk(cd)) At<uint32_t>(cd, 0x68) &= ~2u;
        At<uint64_t>(ch, 0x7C8) = 0;
        At<uint32_t>(ch, 0x7D0) = 0;
    }
}

// Fight `you` the way the game's hostile NPCs do (Character_Attack 0x140110490): the target's
// attack manager (made if there's none - the shipped scripts do this on the player), the attack
// task at the top slot, set up with the manager's index (+0x6CFC); then our reference dropped.
static bool OrderFight(uint8_t* cop, uint8_t* you)
{
    void* brain = At<void*>(cop, 0x78);
    if (!PtrOk(brain)) return false;
    SP ref = {};
    Fn<AttackMgrFn>(FN_ATTACKMGR)(&ref, you);
    LONG* rc = const_cast<LONG*>(ref.rc);
    uint8_t* mgr = PtrOk(ref.obj) && PtrOk(rc) && rc[1] != 0 ? static_cast<uint8_t*>(ref.obj) : nullptr;
    bool ok = false;
    if (mgr)
    {
        uint8_t* task = Fn<NewTaskFn>(FN_NEWTASK)(brain, TASK_ATTACK, SLOT_TOP);
        if (task) { Fn<AttackSetFn>(FN_ATTACKSET)(task, cop, you, At<int32_t>(mgr, 0x6CFC)); ok = true; }
    }
    if (PtrOk(ref.obj) && PtrOk(rc) && InterlockedDecrement(rc) <= 0)
        Fn<RefFreeFn>(FN_REFFREE)(Fn<RefPool1Fn>(FN_REFPOOL1)(Fn<RefPoolFn>(FN_REFPOOL0)()), rc, 0);
    return ok;
}

// Let him go: Character_Release's own pair - all his tasks cleared, then "Convert To Pedestrian"
// (he walks off and turns back into a crowd cop) at the top slot.
static void OrderRelease(uint8_t* cop)
{
    void* brain = At<void*>(cop, 0x78);
    if (!PtrOk(brain)) return;
    Fn<ClearQFn>(FN_CLEARQ)(brain, 4);
    void* heap = *reinterpret_cast<void**>(g_base + RVA_TASKHEAP);
    void* t = PtrOk(heap) ? Fn<TaskAllocFn>(FN_TASKALLOC)(heap, 0x80, reinterpret_cast<const void*>(g_base + RVA_TASKTAG)) : nullptr;
    if (t) t = Fn<ToPedFn>(FN_TOPED)(t, true);
    if (t) Fn<EnqueueFn>(FN_ENQUEUE)(brain, t, SLOT_TOP, true);
}

// ---- the cars -----------------------------------------------------------------------------------
// Stop it where it is: StopTask at slot 1, above the chase in slot 0, which stays queued - so the
// population manager keeps counting it and never re-tasks it (Notes\38 §2).
static bool HoldCar(void* veh, uint8_t* ud)
{
    void* ai = At<void*>(veh, 0x68);
    if (!PtrOk(ai) || !PtrOk(ud)) return false;
    uint8_t* t = Fn<NewTaskFn>(FN_NEWTASK)(ai, TASK_STOP, 1);
    if (!t) return false;
    At<uint64_t>(t, 0x148) = At<uint64_t>(ud, 0x1A0);
    At<uint16_t>(t, 0x150) = At<uint16_t>(ud, 0x1A8);
    At<float>(t, 0x158) = 3.0e38f;                                      // until we let it go
    return true;
}

static void FreeCar(void* veh)
{
    void* ai = At<void*>(veh, 0x68);
    if (PtrOk(ai)) Fn<ClearQFn>(FN_CLEARQ)(ai, 1);                       // the chase in slot 0 resumes
}

// The krawlie in the driver's seat: lcudrive.cpp's KrawlieDriverRide (the rider list).
static uint8_t* KrawlieDriver(uint8_t* ud) { return KrawlieDriverRide(ud); }

// What the car-jack (and the krawlie -> character turn inside it) reads without checking.
static bool KrawlieConvertible(uint8_t* k)
{
    uint8_t* z = PtrOk(k) ? At<uint8_t*>(k, 0xF8) : nullptr;
    uint8_t* x = PtrOk(z) ? At<uint8_t*>(z, 0x48) : nullptr;
    if (!PtrOk(x) || !PtrOk(At<void*>(x, 0xE8))) return false;          // its controller (0x1406A43C0)
    uint8_t* s = At<uint8_t*>(x, 0xD0);
    return PtrOk(s) && PtrOk(At<void*>(s, 0x158));                       // the spawner the turn uses
}

static bool PullDriver(uint8_t* rd)
{
    void* ri = At<void*>(rd, 0x70);
    if (!PtrOk(ri) || !PtrOk(At<void*>(rd, 0x88))) return false;
    uint8_t* k = VGet(ri, 75);                                          // as the jack itself gets it
    if (!KrawlieConvertible(k)) return false;
    g_pulling = true;
    reinterpret_cast<JackFn>((*reinterpret_cast<void***>(rd))[68])(rd, nullptr);
    g_pulling = false;
    return true;
}

// A character's dismount task, while it still heads his top queue (brain+0x258).
static uint8_t* DismountTask(uint8_t* ch)
{
    uint8_t* brain = At<uint8_t*>(ch, 0x78);
    uint8_t* t = PtrOk(brain) ? At<uint8_t*>(brain, 0x258) : nullptr;
    return PtrOk(t) && At<uintptr_t>(t, 0) == g_base + RVA_VT_DISMOUNT ? t : nullptr;
}

// The character the jack made out of this car's driver. There's no character pool to search
// (*(int*)0x141C7F518 is only ever written 0 - Notes\40b §7), so it's found from the car: the
// dismount task holds a weak ref to the car's user data (task+0x78, assigned by 0x1402ADCB0), and
// weak refs are linked into their target's own circular list (head ud+8, next node+8). The task's
// character is its weak ref at +0x60, filled on its first update (0x140791130: brain+0x290 - 0x88),
// so this finds him a frame or two after the pull.
static uint8_t* JackedCop(uint8_t* ud)
{
    uint8_t* head = At<uint8_t*>(ud, 8);
    uint8_t* node = head;
    for (int g = 0; PtrOk(node) && g < 64; ++g)
    {
        uint8_t* t = node - 0x78;
        const uintptr_t vt = At<uintptr_t>(t, 0);
        if ((vt == g_base + RVA_VT_DISMOUNT || vt == g_base + RVA_VT_DISMOUNT_BOAT) && At<uint8_t*>(t, 0x78) == ud)
        {
            uint8_t* ch = At<uint8_t*>(t, 0x60);
            return PtrOk(ch) && !At<uint8_t>(ch, 0x3E1) ? ch : nullptr;
        }
        node = At<uint8_t*>(node, 8);
        if (node == head) break;
    }
    return nullptr;
}

static void FadeOutCar(uint8_t* ud)
{
    struct { uint32_t a; uint8_t flags; } o = { 0, 4 };                // VehicleAction_Destroy: bit 2 = FadeOut
    reinterpret_cast<DestroyFn>((*reinterpret_cast<void***>(ud))[13])(ud, &o);
}

static const char* Kind(const HeldCar& c) { return c.bike ? "bike" : "car"; }

static const char* ModelOf(void* veh, char* buf, int cap)
{
    return ReadModelName(At<void*>(veh, eng::VEH_DEF), buf, cap) ? buf : "a police vehicle";
}

// ---- the walking cops ---------------------------------------------------------------------------
// The krawlie manager (0x14002F1F0 on the world) lists its zones at +0xE0 (next +8, name +0x78);
// each zone's pool: base +0x38, stride +0x40, count +0x44. A krawlie: position +0xB0, flags +0xD2
// (bit 0 going, 0xC riding), its character def's name at [[k+0xD8]+0x370]+0x20.
static const char* KrawlieDefName(uint8_t* k)
{
    uint8_t* m = At<uint8_t*>(k, 0xD8);
    uint8_t* d = PtrOk(m) ? At<uint8_t*>(m, 0x370) : nullptr;
    const char* n = PtrOk(d) ? At<const char*>(d, 0x20) : nullptr;
    return PtrOk(n) ? n : nullptr;
}

static uint8_t* NearestCopKrawlie(void* level, const float p[3], float range, float* dOut)
{
    uint8_t* mgr = Fn<KMgrFn>(FN_KMGR)(level);
    if (!PtrOk(mgr)) return nullptr;
    uint8_t* best = nullptr;
    float bestD = range;
    int zones = 0, seen = 0, cops = 0;
    for (uint8_t* z = At<uint8_t*>(mgr, 0xE0); PtrOk(z) && zones < 64; z = At<uint8_t*>(z, 8), ++zones)
    {
        uint8_t* base = At<uint8_t*>(z, 0x38);
        const int stride = At<int32_t>(z, 0x40), n = At<int32_t>(z, 0x44);
        if (!PtrOk(base) || stride < 0x100 || stride > 0x2000 || n <= 0 || n > 4096) continue;
        for (int i = 0; i < n; ++i)
        {
            uint8_t* k = base + static_cast<size_t>(i) * static_cast<size_t>(stride);
            ++seen;
            if (At<uint16_t>(k, 0xD2) & 0xD || !PtrOk(At<void*>(k, 0xF8))) continue;
            const char* name = KrawlieDefName(k);
            if (!ContainsI(name, "cop") || ContainsI(name, "training")) continue;   // trainees: no police anims
            ++cops;
            float d2 = 0.0f;
            for (int c = 0; c < 3; ++c) { const float dc = At<float>(k, 0xB0 + 4 * c) - p[c]; d2 += dc * dc; }
            const float d = std::sqrt(d2);
            if (IsFin(d) && d < bestD) { bestD = d; best = k; }
        }
    }
    const uint32_t now = GetTickCount();
    if (now - g_walkerLog > 20000)
    {
        g_walkerLog = now;
        Log("    wanted: crowd around you: %d zones, %d krawlies, %d cops among them%s\n", zones, seen, cops,
            best ? "" : " (none in reach)");
    }
    if (dOut) *dOut = bestD;
    return best;
}

// ---- the fight ----------------------------------------------------------------------------------
// He can be fought from the moment he's out: targetable, hurt by gadgets, not invulnerable, and on the
// other side (the player's attacks pick Baddies). His side is put back when he's let go.
static void SetSideBits(uint8_t* ch, uint32_t bits);
static void MakeFightable(OfficerSlot& o)
{
    Fn<ClearFlagFn>(FN_CLEARFLAG)(o.ch, FLAG_INVULNERABLE, &kFlagLayer0);
    Fn<ClearFlagFn>(FN_CLEARFLAG)(o.ch, FLAG_IGNORE_AS_TARGET, &kFlagLayer0);
    Fn<ClearFlagFn>(FN_CLEARFLAG)(o.ch, FLAG_IMMUNE_TO_BOLTS, &kFlagLayer0);
    if (!o.sideSet)
    {
        o.side0 = At<uint32_t>(o.ch, 0xD0) & SIDE_BITS;
        SetSideBits(o.ch, SIDE_BADDIE);
        o.sideSet = true;
    }
    o.flagCleared = true;
}

static int PunchAnim(uint8_t* ch)
{
    for (int id : kPunchAnims) if (HasAnim(ch, id)) return id;
    return 0;
}

// ---- the table ----------------------------------------------------------------------------------
static void Forget(int i)
{
    Unwatch(&g_os[i].watch);
    g_os[i] = g_os[--g_nOs];
}

static void ForgetCar(int i)
{
    g_hc[i] = g_hc[--g_nHc];
}

static HeldCar* CarOf(const void* veh)
{
    for (int i = 0; i < g_nHc; ++i) if (g_hc[i].veh == veh) return &g_hc[i];
    return nullptr;
}

static bool Taken(const uint8_t* ch)
{
    for (int i = 0; i < g_nOs; ++i) if (g_os[i].ch == ch) return true;
    return false;
}

// For the crimes (wanted.h OnCharacterHit): which officer that is (0 = none of ours), and whether
// your hit on him is the one that counts (once per officer: a brawl isn't five bricks).
static int OfficerIdOf(const uint8_t* ch)
{
    for (int i = 0; i < g_nOs; ++i) if (g_os[i].ch == ch) return g_os[i].id;
    return 0;
}

// Your blow lands on one of them (wanted.h OnCharacterHit, inside the game's own hit handling: only
// counted here - he goes down at the next tick, never from inside the hit).
static void NoteOfficerHit(const uint8_t* ch)
{
    for (int i = 0; i < g_nOs; ++i)
    {
        OfficerSlot& o = g_os[i];
        if (o.ch != ch || o.beaten) continue;
        const uint32_t now = GetTickCount();
        if (o.lastHitMs && now - o.lastHitMs < 350) return;
        o.lastHitMs = now | 1u;
        ++o.hits;
        if (o.hits >= g_hitsToBeat) o.beaten = true;
        Log(">>> wanted: you hit officer #%d (%d of %d)%s\n", o.id, o.hits, g_hitsToBeat, o.beaten ? " - he's beaten" : "");
        return;
    }
}

static bool FirstHitOn(const uint8_t* ch)
{
    for (int i = 0; i < g_nOs; ++i)
        if (g_os[i].ch == ch) { const bool first = !g_os[i].hitByYou; g_os[i].hitByYou = true; return first; }
    return false;
}

// Let him go, his side put back first (its AI reset clears him for the release).
static void LetGo(OfficerSlot& o)
{
    if (o.sideSet) { SetSideBits(o.ch, o.side0); o.sideSet = false; }
    OrderRelease(o.ch);
}

static OfficerSlot* AddOfficer(uint8_t* ch, void* car, bool fromCar)
{
    if (g_nOs >= kMaxSlots) return nullptr;
    OfficerSlot o = {};
    if (!Watch(ch, &o.watch)) return nullptr;
    o.ch = ch; o.def = At<void*>(ch, 0x650); o.brain = At<void*>(ch, 0x78); o.car = car; o.id = g_nextId++;
    officers::Start(o.u, fromCar);
    g_os[g_nOs] = o;
    return &g_os[g_nOs++];
}

// A level load (the station trip) or wanted mode going off: the pointers mean nothing now.
static void OfficersForgetAll()
{
    g_nOs = 0; g_nHc = 0;
}

// The nearest officer who is out and after you (the model's "officer" input), 1e9 = none.
static float OfficersNearest(const float yp[3])
{
    float best = 1e9f;
    for (int i = 0; i < g_nOs; ++i)
    {
        const OfficerSlot& o = g_os[i];
        if ((o.u.ph == officers::kRun || o.u.ph == officers::kAim || o.u.ph == officers::kFight) && Alive(o))
            best = std::fmin(best, DistCh(o.ch, yp));
    }
    return best;
}

// Busted on foot: the nearest officer within 3 u cuffs you (Ability_Arrest's anim).
static void OfficersCuff(const float yp[3])
{
    int best = -1;
    float bestD = 3.0f;
    for (int i = 0; i < g_nOs; ++i)
    {
        if (!Alive(g_os[i])) continue;
        const float d = DistCh(g_os[i].ch, yp);
        if (d < bestD) { bestD = d; best = i; }
    }
    if (best < 0) return;
    const bool cuff = OrderAnim(g_os[best].ch, ANIM_CUFFING);           // (one order per slot a frame: no face first)
    Log("    wanted: officer #%d %s\n", g_os[best].id, cuff ? "cuffs you" : "reaches you (no cuffing anim in his set)");
}

// A car we hold is still the one we held: in the population manager's traffic slots (PM+0x568,
// alive while ctl+4), with the same user data - a culled car's memory gets reused.
static bool CarAlive(const World& w, const HeldCar& c)
{
    if (!PtrOk(c.veh)) return false;
    uint8_t* slots = reinterpret_cast<uint8_t*>(w.pm) + 0x568;
    for (int i = 0; i < 0x80; ++i)
    {
        uint8_t* cell = At<uint8_t*>(slots, i * 16);
        uint8_t* ctl = At<uint8_t*>(slots, i * 16 + 8);
        if (PtrOk(cell) && PtrOk(ctl) && At<int32_t>(ctl, 4) != 0 && At<void*>(cell, 0) == c.veh)
            return VehicleUserData(c.veh) == c.ud;
    }
    return false;
}

// Everything let go: officers released, held cars with a driver still in freed, empty ones left
// parked to fade out once you're away.
static void OfficersReleaseAll(const World& w, const char* why)
{
    for (int i = 0; i < g_nOs; ++i)
    {
        if (Alive(g_os[i]) && g_os[i].u.ph != officers::kGetOut) LetGo(g_os[i]);
        Unwatch(&g_os[i].watch);
    }
    if (g_nOs) Log("    wanted: %d officer%s on foot let go (%s)\n", g_nOs, g_nOs == 1 ? "" : "s", why);
    g_nOs = 0;
    int k = 0;
    for (int i = 0; i < g_nHc; ++i)
    {
        HeldCar c = g_hc[i];
        if (!CarAlive(w, c)) continue;
        if (KrawlieDriver(c.ud)) { FreeCar(c.veh); continue; }          // its driver is still in: drive on
        c.abandoned = true;
        g_hc[k++] = c;
    }
    g_nHc = k;
}

// Once a tick, after the wanted model (wanted.h Tick, behind RunOfficers' fault switch).
//   chase: you're wanted, not busted, and on foot (or only just got into a vehicle)
//   zap:   the model's hit this tick (wanted::kStun / kTie), fired by the nearest officer in range
static void OfficersTick(const World& w, float dt, int level, bool chase, const float yp[3], uint8_t zap, float zapS,
                         const Config& cfg)
{
    uint8_t* p1 = P1();
    if (!PtrOk(p1)) return;
    void* you = PlayerVehicle();
    g_offP.fireRange = cfg.wanted_officer_fire;
    g_offP.backRange = 0.0f;                           // v1: nobody re-seats; they're let go, their cars fade
    g_offP.fightRange = cfg.wanted_officers_fight ? 2.0f : 0.0f;
    g_offP.breakRange = 3.2f;                          // you stepped back: he runs at you again
    g_offP.refightS = 1.8f;                            // s between his punches
    g_hitsToBeat = cfg.wanted_officer_hits;
    g_punchCool = std::fmax(0.0f, g_punchCool - dt);
    if (g_wasDown && !Me().down.on) g_punchCool = std::fmax(g_punchCool, 2.5f);   // just back up: a breather
    g_wasDown = Me().down.on;
    char name[64];

    // --- the cars we hold
    for (int i = g_nHc - 1; i >= 0; --i)
    {
        HeldCar& c = g_hc[i];
        if (!CarAlive(w, c)) { ForgetCar(i); continue; }
        if (c.veh == you) { FreeCar(c.veh); ForgetCar(i); continue; }   // you took it: it's yours to drive
        c.t += dt;
        void* body = BodyOf(c.veh);
        const float d = body ? DistTo(body, yp) : 1e9f;
        if (c.abandoned)
        {
            if (!KrawlieDriver(c.ud) && !UserDataDriver(c.ud) && (d > 35.0f || c.t > 120.0f))
            {
                Log("    wanted: the empty %s (%s %d) fades out, %.0f u from you\n", ModelOf(c.veh, name, sizeof(name)), Kind(c), c.id, d);
                FadeOutCar(c.ud);
                ForgetCar(i);
            }
            continue;
        }
        if (c.pulled && !c.found)                                      // the jacked driver's first update
        {
            c.pullT += dt;
            uint8_t* cop = JackedCop(c.ud);
            if (cop)
            {
                c.found = true;
                if (OfficerSlot* o = AddOfficer(cop, c.veh, true))
                    Log("    wanted: officer #%d (%s) jumps %s %s %d, %.0f u from you\n", o->id, CharDefName(cop),
                        c.bike ? "off" : "out of", Kind(c), c.id, d);
                else c.abandoned = true;                                // (no room: he goes his own way)
            }
            else if (c.pullT > 2.5f)
            {
                Log("    wanted: %s %d's driver was pulled out but no officer came of it (no dismount task after 2.5 s)\n", Kind(c), c.id);
                c.abandoned = true;
            }
            continue;
        }
        if (c.pulled)
        {
            bool officer = false;
            for (int j = 0; j < g_nOs; ++j) officer |= g_os[j].car == c.veh;
            if (!officer) c.abandoned = true;                            // its officer is gone: leave it parked
            continue;
        }
        if (!chase) { FreeCar(c.veh); ForgetCar(i); continue; }         // over before he got out: drive on
        const float v = body ? SpeedOf(body) : 0.0f;
        if (v > 1.5f && c.t < (c.bike ? 2.0f : 3.0f)) continue;         // still braking
        uint8_t* rd = KrawlieDriver(c.ud);
        if (!rd)
        {
            Log("    wanted: %s %d has no crowd driver to pull out (%s) - it drives on\n", Kind(c), c.id,
                UserDataDriver(c.ud) ? "a character drives it" : "empty");
            FreeCar(c.veh); ForgetCar(i);
            continue;
        }
        if (!PullDriver(rd))
        {
            Log("    wanted: %s %d's driver can't be pulled out (his crowd data is incomplete) - it drives on\n", Kind(c), c.id);
            FreeCar(c.veh); ForgetCar(i);
            continue;
        }
        c.pulled = true;                                               // he's found next tick (JackedCop)
        Log("    wanted: %s %d - its driver is pulled out\n", Kind(c), c.id);
    }

    // --- the officers
    int fireAt = -1;
    if (zap)
    {
        float best = g_offP.fireRange;
        for (int i = 0; i < g_nOs; ++i)
            if (g_os[i].u.ph == officers::kRun && Alive(g_os[i]))
            {
                const float d = DistCh(g_os[i].ch, yp);
                if (d < best) { best = d; fireAt = i; }
            }
        if (fireAt < 0) Me().st.zapCool = std::fmin(Me().st.zapCool, 1.0f);    // nobody in reach after all
        else { g_os[fireAt].kind = zap; g_os[fireAt].zapS = zapS; }
    }
    for (int i = g_nOs - 1; i >= 0; --i)
    {
        OfficerSlot& o = g_os[i];
        if (!Alive(o))
        {
            if (o.killed) Log(">>> wanted: officer #%d is out of the fight\n", o.id);
            else Log("    wanted: officer #%d is gone from the world%s\n", o.id, o.u.ph == officers::kGetOut ? " before he got out" : "");
            Forget(i);
            continue;
        }
        if (o.beaten)
        {
            if (!o.killed)
            {
                o.killed = true;
                Log("    wanted: officer #%d falls apart\n", o.id);
                Fn<KillFn>(FN_KILL)(o.ch, 3, nullptr, 1, 1, 1, 0.0f);
            }
            continue;                                               // gone from the world within a tick or two
        }
        officers::In in{};
        in.dt = dt;
        in.chase = chase;
        in.wanted = level > 0;
        in.youDown = Me().down.on;
        in.dist = DistCh(o.ch, yp);
        if (o.u.ph == officers::kGetOut)
        {
            uint8_t* t = DismountTask(o.ch);
            in.onFoot = t ? At<int32_t>(t, 0x58) >= 2 : !InAVehicle(o.ch);   // landed: take him over
        }
        else in.onFoot = !InAVehicle(o.ch);
        float m2 = 0.0f;
        for (int k = 0; k < 3; ++k) { const float dk = yp[k] - o.you[k]; m2 += dk * dk; }
        in.youMoved = std::sqrt(m2);
        in.fire = i == fireAt;
        const uint8_t before = o.u.ph;
        // his punch: the swing starts 0.25 s after he squares up, and lands 0.35 s after that if you're
        // still within reach - a short knockdown (wanted.h Punched)
        if (o.punchT > 0.0f)
        {
            o.punchT -= dt;
            if (!o.punchSwung && o.punchT <= 0.35f)
            {
                o.punchSwung = true;
                const int anim = PunchAnim(o.ch);
                const bool played = anim && OrderAnim(o.ch, anim);
                if (!g_punchAnimLogged)
                {
                    g_punchAnimLogged = true;
                    Log("    wanted: officer #%d (%s) throws a punch - %s\n", o.id, CharDefName(o.ch),
                        played ? "with a punch anim from his set" : "his set has no punch anim: the blow lands without one");
                }
            }
            if (o.punchT <= 0.0f)
            {
                o.punchT = 0.0f;
                if (in.dist < 2.4f && chase && Punched(o.id, 1.2f)) g_punchCool = 2.0f;
            }
        }
        o.seenT += dt;
        if (o.seenT >= 2.0f && before != officers::kGetOut)              // a report line every 2 s: is he getting anywhere?
        {
            float mv = 0.0f;
            for (int k = 0; k < 3; ++k) { const float dk = At<float>(o.ch, 0xA8 + 4 * k) - o.seen[k]; mv += dk * dk; o.seen[k] = At<float>(o.ch, 0xA8 + 4 * k); }
            static const char* const kPh[] = { "getting out", "running", "aiming", "going back", "gone", "fighting" };
            uint8_t* br = At<uint8_t*>(o.ch, 0x78);
            Log("    wanted: officer #%d %s: %.1f u from you, moved %.1f u in %.1f s, at (%.1f, %.1f, %.1f); top task vt 0x%llX (%d queued), %d new run orders%s\n",
                o.id, kPh[before < 6 ? before : 4], in.dist, std::sqrt(mv), o.seenT, o.seen[0], o.seen[1], o.seen[2],
                static_cast<unsigned long long>(SlotHeadVt(o.ch, SLOT_TOP)),
                PtrOk(br) ? At<int32_t>(br, 0x180 + 0x58 * SLOT_TOP + 0x38) : -1, o.reissued, InAVehicle(o.ch) ? ", IN A VEHICLE" : "");
            o.seenT = 0.0f; o.reissued = 0;
        }
        const officers::Order ord = officers::Step(g_offP, o.u, in);
        switch (ord)
        {
        case officers::kRunOrder:
            if (!o.flagCleared) MakeFightable(o);                    // (its side change resets his AI: the order below follows it)
            if (before == officers::kGetOut) Log("    wanted: officer #%d is out - after you on foot (%.0f u)\n", o.id, in.dist);
            if (before == officers::kRun && o.gen == g_susGen && SlotBusy(o.ch, SLOT_TOP)) break;   // still on his way: don't restart it
            if (!OrderRun(o.ch, p1)) Log("    wanted: officer #%d could not be given a run order\n", o.id);
            o.gen = g_susGen;
            ++o.reissued;
            for (int k = 0; k < 3; ++k) o.you[k] = yp[k];
            break;
        case officers::kAimOrder:
            OrderFace(o.ch, p1);
            o.shot = false;
            Log("    wanted: officer #%d takes aim at %.1f u\n", o.id, in.dist);
            break;
        case officers::kFire:
        case officers::kMissed:
        {
            const bool tried = ord == officers::kFire;
            const bool hit = tried && Hit(o.kind == wanted::kTie, o.zapS);
            if (!hit) Me().st.zapCool = std::fmin(Me().st.zapCool, 1.0f);
            Log("    wanted: officer #%d's %s %s\n", o.id, o.kind == wanted::kTie ? "grapple" : "ray gun",
                hit ? "hits you" : tried ? "is dodged (you can't be hit right now)" : "misses - you got out of reach");
            for (int k = 0; k < 3; ++k) o.you[k] = yp[k];               // the shot plays out; he runs on at the next order
            break;
        }
        case officers::kFightOrder:
            // The game's own attack task (Character_Attack's recipe, OrderFight) took the top slot and then
            // did nothing in two test runs: 18 report lines of officers standing 0.2-2 u from the player,
            // not one blow. So the punch is ours: he turns to you, swings, and it lands if you're in reach.
            if (!o.flagCleared) MakeFightable(o);
            if (before != officers::kFight) Log("    wanted: officer #%d squares up to you (%.1f u)\n", o.id, in.dist);
            if (g_punchCool <= 0.0f && o.punchT <= 0.0f && !Me().down.on && !Me().bust.on)
            {
                OrderFace(o.ch, p1);
                o.punchT = 0.6f; o.punchSwung = false;
                g_punchCool = 1.2f;                                     // one officer at a time
            }
            for (int k = 0; k < 3; ++k) o.you[k] = yp[k];
            break;
        case officers::kBackOrder:
        case officers::kRelease:
            LetGo(o);
            Log("    wanted: officer #%d let go (%s)\n", o.id, !in.wanted ? "the chase is over" : !in.chase ? "you drove off" : "too far behind");
            Forget(i);
            break;
        case officers::kForget:
            if (before == officers::kGetOut) Log("    wanted: officer #%d never got out - forgotten\n", o.id);
            Forget(i);
            break;
        default:
            if (o.u.ph == officers::kAim && !o.shot && o.u.t >= 0.2f)       // the wind-up: the shot you can see
            {
                o.shot = true;
                const bool shoot = OrderAnim(o.ch, ANIM_SHOOT);
                const bool point = !shoot && OrderAnim(o.ch, ANIM_POINT);
                if (!g_shotLogged || !shoot)
                {
                    g_shotLogged = true;
                    Log("    wanted: officer #%d (%s) %s\n", o.id, CharDefName(o.ch),
                        shoot ? "fires the grapple gun (GrappleGun_shoot)" : point ? "points at you (no GrappleGun_shoot in his set)"
                                                                              : "has no shooting anim - the hit comes anyway");
                }
            }
            break;
        }
    }

    // --- more of them, up to the level's number: cars pulling up first, then cops off the street
    if (!chase || level <= 0 || !cfg.wanted_officers) return;
    int busy = g_nOs;
    for (int i = 0; i < g_nHc; ++i) busy += !g_hc[i].found && !g_hc[i].abandoned;   // officers on their way
    const int want = officers::kMaxOnFoot[level > 5 ? 5 : level];
    g_holdCool -= dt; g_joinCool -= dt;
    if (busy < want && g_holdCool <= 0.0f && !you)
    {
        int pick = -1;
        float best = cfg.wanted_officer_get_out;
        for (int i = 0; i < g_nChase; ++i)
            if (g_chase[i].d < best && !CarOf(g_chase[i].veh)) { best = g_chase[i].d; pick = i; }
        if (pick >= 0 && g_nHc < kMaxSlots)
        {
            void* veh = g_chase[pick].veh;
            uint8_t* ud = VehicleUserData(veh);
            if (HoldCar(veh, ud))
            {
                HeldCar& c = g_hc[g_nHc++];
                c = HeldCar{};
                c.veh = veh; c.ud = ud; c.id = g_nextId++; c.bike = g_chase[pick].bike;
                ++busy;
                g_holdCool = 0.5f;
                Log(">>> wanted: %s (%s %d) pulls up %.0f u from you - its officer is getting out\n",
                    ModelOf(veh, name, sizeof(name)), Kind(c), c.id, best);
            }
        }
    }
    if (busy < want && g_joinCool <= 0.0f && !you)
    {
        g_joinCool = 1.0f;
        float d = 0.0f;
        uint8_t* k = NearestCopKrawlie(w.lvl, yp, cfg.wanted_officer_join, &d);
        if (k && KrawlieConvertible(k))
        {
            const char* def = KrawlieDefName(k);
            char defName[64] = "?";
            if (def) strncpy_s(defName, sizeof(defName), def, _TRUNCATE);
            uint8_t* ch = Fn<Kr2CharFn>(FN_KR2CHAR)(k);
            if (PtrOk(ch) && !At<uint8_t>(ch, 0x3E1) && !Taken(ch))
            {
                reinterpret_cast<ChFn>(g_base + FN_TODEF)(ch);
                if (OfficerSlot* o = AddOfficer(ch, nullptr, false))
                {
                    MakeFightable(*o);
                    Log(">>> wanted: %s off the street joins the chase (officer #%d, %.0f u from you)\n", defName, o->id, d);
                }
            }
            else Log("    wanted: %s off the street couldn't be made a character (the game refused)\n", defName);
        }
    }
}
