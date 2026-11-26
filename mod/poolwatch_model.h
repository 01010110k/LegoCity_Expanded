// =============================================================================================
//  poolwatch_model.h - [stationfix] the streaming manager's request-node pool, checked (pure, no game calls).
//
//  The "return to police station" crash (dumps 18312 / 7192 / 20224, 2026-11-19): GoToLevel 0x1407F25B0 ->
//  0x140608C00 (unload the current level trees) -> 0x140607CD0 -> 0x140607FF0 -> 0x140608EE0 ->
//  pool alloc 0x1402E7060(*(void**)0x141C6A770, 0x68) faults at 0x1402E70F0 reading a free-list next of 3.
//  The pool *0x141C6A770 belongs to the streaming manager (*0x141C6A768) and only ever hands out 0x68-byte
//  request nodes (25 code references, all size 0x68 [P]) - so only size class 7 (pool+0x30+7*8 = +0x68) is
//  ever used; class heads 0..6 (+0x30..+0x67) stay 0 in a healthy pool.
//  In all three dumps the node the first alloc of 0x140607FF0 returned was pool+0x10 [P: saved rdi, and
//  r11 / r10 = pool+0x10 / +0x20 left by the node constructor 0x140605CD0]: the class-7 free list pointed
//  INTO THE POOL'S OWN HEADER. A node built there overlays the header: +0x58 (type byte) is the class-7 head's
//  low byte - type 3 = unload request - so the head became 3 and the next alloc faulted.
//
//  Header layout used here [P 0x1402E7060 / 0x1402E6B60 / 0x1402E6C70]:
//    +0x10 parent allocator* (slow path: vt[0] for a new chunk)   +0x18 u32 alignment granule
//    +0x28 chunk list head (chunk: +0 next, +8 u32 size, +0x10 base, +0x18 u32 used, +0x1C u32 count)
//    +0x30 + 8*k  lock-free free-list head of size class k (k = size/16, < 0x1000 bytes)
//    +0x830 byte "lockless", +0x838 lock
//
//  Check() says what is wrong; Plan() says what may be put right. A repair is only planned when the class-7
//  head points into the header (= the header block itself is FREE, so no live node sits in it): drop the
//  class-7 chain (the free nodes leak - a few hundred bytes), restore the parent pointer, the granule and the
//  chunk list from the last healthy snapshot, and zero the unused heads 0..6. The next alloc then bump-allocates
//  a fresh block from a real chunk, as the game does when a class runs dry.
// =============================================================================================
#pragma once
#include <cstdint>

namespace pwm {

static const uint64_t kHdrSize = 0x840;               // the header up to and including the lock
static const uint64_t kParent = 0x10, kGran = 0x18, kChunks = 0x28, kHeads = 0x30;
static const int      kClass = 7;                     // 0x68 rounded up to 16 = 0x70 -> class 7
static const int      kHeadsChecked = 8;              // classes 0..7

struct Hdr
{
    uint64_t parent = 0;
    uint32_t gran = 0;
    uint64_t chunks = 0;
    uint64_t heads[kHeadsChecked] = {};
};

enum Bits : unsigned
{
    kOk = 0,
    kDirtyHeads = 1,      // a class 0..6 head is not 0 (nothing of that size is ever freed here)
    kHeadInHeader = 2,    // the class-7 head points into the header (the crash state)
    kChunksInHeader = 4,  // the chunk list points into the header
    kParentLost = 8,      // the parent allocator pointer changed from the healthy one (or is 0)
    kGranLost = 16,       // the granule changed (or is 0)
    kHeadSmall = 32,      // the class-7 head is a small non-pointer (the fault itself is one pop away)
};

inline bool InHdr(uint64_t v, uint64_t pool) { return v >= pool && v < pool + kHdrSize; }

inline unsigned Check(const Hdr& h, uint64_t pool, const Hdr* healthy)
{
    unsigned r = kOk;
    for (int k = 0; k < kClass; ++k) if (h.heads[k]) { r |= kDirtyHeads; break; }
    const uint64_t h7 = h.heads[kClass];
    if (InHdr(h7, pool)) r |= kHeadInHeader;
    else if (h7 && h7 < 0x10000) r |= kHeadSmall;
    if (InHdr(h.chunks, pool)) r |= kChunksInHeader;
    if (!h.parent || (healthy && healthy->parent && h.parent != healthy->parent)) r |= kParentLost;
    if (!h.gran || (healthy && healthy->gran && h.gran != healthy->gran)) r |= kGranLost;
    return r;
}

struct Repair
{
    bool     apply = false;
    uint64_t expectHead = 0;      // the class-7 head the repair replaces (compare-exchange)
    uint64_t parent = 0;          // 0 = leave
    uint32_t gran = 0;            // 0 = leave
    uint64_t chunks = 0;          // 0 = leave
    bool     zeroHeads = false;   // heads 0..6 -> 0
};

// healthy: the last snapshot Check() passed (haveHealthy false = none yet: never repair blind).
inline Repair Plan(const Hdr& h, uint64_t pool, const Hdr& healthy, bool haveHealthy)
{
    Repair p;
    const unsigned v = Check(h, pool, haveHealthy ? &healthy : nullptr);
    // Only the free-header state: with any other damage a live node may sit in the header (its +0/+8 links ARE
    // the parent / granule fields), and writing them back would break that node. Those are logged, not touched.
    if (!haveHealthy || !(v & kHeadInHeader)) return p;
    if (!healthy.parent || !healthy.gran || InHdr(healthy.chunks, pool)) return p;
    p.apply = true;
    p.expectHead = h.heads[kClass];
    if (v & kParentLost) p.parent = healthy.parent;
    if (v & kGranLost) p.gran = healthy.gran;
    if (v & kChunksInHeader) p.chunks = healthy.chunks;
    p.zeroHeads = (v & kDirtyHeads) != 0;
    return p;
}

inline const char* Describe(unsigned v)
{
    if (v & kHeadSmall) return "the 0x68 free list holds a non-pointer (the station crash state)";
    if (v & kHeadInHeader) return "the 0x68 free list points into the pool's own header (the station crash state)";
    if (v & kChunksInHeader) return "the chunk list points into the pool's own header";
    if (v & (kParentLost | kGranLost)) return "the pool's header fields were overwritten";
    if (v & kDirtyHeads) return "unused size classes have free lists (a node was built inside the header)";
    return "ok";
}

// ---- [streamfix] the root cause and the fix --------------------------------------------------------------------------
// The pool is created at 0x140605A3B: 0x1402E00D0(heap, 0x34000, 8, "LevelStreamingManager::m_RequestMemoryPool") = ONE
// arena chunk of 0x34000 bytes = 1,901 blocks of 0x70 [P]. Its parent allocator (pool+0x10 = exe+0x1A5C198, vtable
// 0x140C990F0, slot 0 = 0x1400410C0 "xor al,al; ret") cannot add a chunk, and the refill 0x1402E6B60 ignores that and
// bump-carves past the arena into whatever follows (the pool header) [P]. The mod's world streams 2,413 levels (stock
// 1,616): a long session runs past 1,901 live request nodes. Fix: one extra chunk, added with the engine's own add-chunk
// 0x1402E67D0(pool, mem, size) (takes the pool lock, links at pool+0x28, adds to pool+0x1C), the memory from the arena's
// own allocator 0x1402DEE00(0x1402E0590(0x1402E05F0()), size, 8, 0, srcpos, 0). The repair above is no longer applied.
static const uint32_t kNode = 0x70;                   // a 0x68 request rounded to the granule
static const uint32_t kStockBytes = 0x34000;          // the stock arena: 1,901 nodes
static const uint32_t kGrowBytes = 0x200000;          // the added chunk: 2 MB = 18,724 nodes

// once per pool instance: a new pool pointer (a level reload rebuilt it) starts a new record
struct GrowSt
{
    uint64_t pool = 0;
    bool     grown = false;       // our chunk is in this pool
    bool     refused = false;     // the signature checks failed for this pool: never touched
    uint32_t nextLog = 1500;      // the next high-water threshold to log (nodes)
    uint32_t peak = 0;
};

inline bool NewPool(GrowSt& g, uint64_t pool)         // true = a different pool than last time (state reset)
{
    if (pool == g.pool) return false;
    g = GrowSt{}; g.pool = pool;
    return true;
}
inline bool WantGrow(const GrowSt& g) { return g.pool && !g.grown && !g.refused; }

// 1500 -> 1800 -> 1901 (the stock arena full) -> then every +2000 (3901, 5901, ...)
inline uint32_t NextThreshold(uint32_t t) { return t < 1800 ? 1800 : t < 1901 ? 1901 : t + 2000; }

// nodes = the carved high-water (sum of the chunks' used bytes / kNode). Returns the threshold crossed now (0 = none);
// several thresholds crossed at once report the highest and move past all of them.
inline uint32_t Crossed(GrowSt& g, uint32_t nodes)
{
    if (nodes > g.peak) g.peak = nodes;
    uint32_t hit = 0;
    while (nodes >= g.nextLog) { hit = g.nextLog; g.nextLog = NextThreshold(g.nextLog); }
    return hit;
}

} // namespace pwm
