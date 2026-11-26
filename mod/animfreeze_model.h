// =============================================================================================
//  animfreeze_model.h - [freeze] when a Backrooms figure's animation is stopped (pure, no game calls).
//
//  The engine fact this rests on [P, LEGOLCUR_DX11.exe]:
//    MechCharacter::Update (vtable 0x140D1C7E8 slot 65 = 0x14066C6A0, this = ch+0x10):
//      0x14066C961  mov rcx, [ch+0x70]                 ; the model instance (ApiCharacter +0x60)
//      0x14066C964  movaps xmm1, xmm7                  ; dt
//      0x14066C967  mulss xmm1, [rbx+0x8A0]            ; x the character's ANIM SPEED  (= ch+0x8B0)
//      0x14066C96F  call 0x14046BE00                   ; advance the whole anim tree by dt * speed
//    0x14066F1E0 = SetAnimSpeed(ch, float) (one movss to ch+0x8B0, 47 engine callers), 0x1406697C0 = its getter,
//    the constructor 0x140665A93 sets 1.0, the context-handler base enter / exit (0x140692F80 / 0x140692FD0)
//    set it back to 1.0. Speed 0 = the anim tree does not advance: the pose holds (blend, loop, breathing).
//  So: a figure is frozen by speed 0, re-applied whenever the engine put it back (a context change).
//
//  Per figure kind:
//    statue    the pose settles for statue_settle s (only while nobody sees it - they spawn unseen), then 0 for good;
//              teleports (moves while unseen) keep the frozen pose.
//    colossus  the same, colossus_settle; never moves again (no idle at all).
//    worker    the chosen console pose (Ponder / CheckTime / ...) needs time to reach its posture: worker_settle_min..max
//              s (random per figure, also when seen - he was just "doing something"), then 0. Optional re-pose: every
//              repose_min..repose_max s of being UNSEEN he plays a new pose for repose_run s, then freezes again; if a
//              player looks while it runs, it freezes on the spot.
//  Step() returns the speed to hold this tick and what to log; the glue writes it, reads it back, logs.
// =============================================================================================
#pragma once
#include <cstdint>

namespace afz {

enum Kind { kStatue = 0, kColossus = 1, kWorker = 2 };

struct Params
{
    float statue_settle     = 0.35f;
    float colossus_settle   = 0.35f;
    float worker_settle_min = 1.2f, worker_settle_max = 2.6f;
    bool  worker_repose     = true;
    float repose_min = 30.0f, repose_max = 90.0f, repose_run = 1.6f;
};

struct St
{
    bool     init = false;
    bool     frozen = false;      // settled and held at speed 0
    bool     reposing = false;    // a worker's short unseen pose change is running
    float    age = 0;             // since spawn
    float    settle = 0;          // this figure's settle time
    float    untilRepose = 0;     // unseen seconds left before the next re-pose (workers)
    float    runLeft = 0;         // seconds left of the running re-pose
    uint32_t rng = 1;
    int      resets = 0;          // the engine put the speed back while frozen (re-applied)
    int      reposes = 0;
};

struct Out
{
    float speed = 1.0f;           // the anim speed to hold this tick
    bool  justFrozen = false;     // log "animation frozen" (once per settle)
    bool  newPose = false;        // the glue plays the next pose anim now
    bool  reset = false;          // the engine had changed the speed - log the first one
};

inline float Rnd(St& s)            // xorshift32 -> [0, 1)
{
    uint32_t x = s.rng ? s.rng : 0x9E3779B9u;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5; s.rng = x;
    return static_cast<float>(x >> 8) * (1.0f / 16777216.0f);
}

inline void Init(St& s, Kind k, const Params& P, uint32_t seed)
{
    s = St{};
    s.init = true;
    s.rng = seed * 2654435761u + 0x6D2B79F5u; if (!s.rng) s.rng = 1;
    Rnd(s);
    if (k == kWorker) s.settle = P.worker_settle_min + (P.worker_settle_max - P.worker_settle_min) * Rnd(s);
    else s.settle = k == kColossus ? P.colossus_settle : P.statue_settle;
    s.untilRepose = P.repose_min + (P.repose_max - P.repose_min) * Rnd(s);
}

// curSpeed: what the character holds now (read back from ch+0x8B0). observed: any player view could see the figure.
inline Out Step(St& s, Kind k, const Params& P, float dt, bool observed, float curSpeed)
{
    Out o;
    if (!s.init) Init(s, k, P, 1);
    if (!(dt >= 0.0f) || dt > 1.0f) dt = 0.0f;              // a hitch: no time passes for the timers
    s.age += dt;
    if (!s.frozen)
    {
        // statues / colossi stop the moment anyone could see them; workers settle into their pose regardless
        const bool stopNow = s.age >= s.settle || (observed && k != kWorker);
        if (!stopNow) { o.speed = 1.0f; return o; }
        s.frozen = true; o.justFrozen = true; o.speed = 0.0f;
        return o;
    }
    if (s.reposing)
    {
        s.runLeft -= dt;
        if (observed || s.runLeft <= 0.0f)
        {
            s.reposing = false; s.runLeft = 0;
            s.untilRepose = P.repose_min + (P.repose_max - P.repose_min) * Rnd(s);
            o.speed = 0.0f; return o;
        }
        o.speed = 1.0f; return o;
    }
    if (curSpeed != 0.0f) { ++s.resets; o.reset = true; }   // the engine put it back (a context change): hold 0 again
    if (k == kWorker && P.worker_repose && !observed)
    {
        s.untilRepose -= dt;
        if (s.untilRepose <= 0.0f)
        {
            s.reposing = true; s.runLeft = P.repose_run; ++s.reposes;
            o.newPose = true; o.speed = 1.0f; return o;
        }
    }
    o.speed = 0.0f;
    return o;
}

} // namespace afz
