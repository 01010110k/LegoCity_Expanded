// =============================================================================================
//  poolwatch.h - [stationfix] watch the streaming manager's request-node pool; [streamfix] GROW it.
//  The model, the crash analysis and the header layout: poolwatch_model.h. Tests: test\poolwatch_test.cpp.
//
//  pw::Tick(where) runs on the game thread: at the end of every world update ("world") and from the pause
//  menu's HasRow ("pause" - the pause update asks it every frame, so it also runs in the frame that accepts
//  "Return to police station", before the level change). A few reads per call; no allocation.
//    [streamfix] the root cause (poolwatch_model.h): the pool has ONE 0x34000-byte arena (1,901 nodes) and a parent
//                that cannot grow it; past 1,901 live requests the engine carves into the pool header. On the first
//                world update that sees a (new) pool, once its identity is proven (the name string, the parent
//                allocator, the granule, the code bytes of the engine calls), a 2 MB chunk is added with the engine's
//                own add-chunk. Once per pool instance (a level reload that rebuilds the pool gets it again).
//                The carved high-water and the manager's queue length are logged at 1500 / 1800 / 1901 / +2000...
//    healthy  -> the snapshot is kept (the parent allocator, granule, chunk list)
//    damaged  -> logged ONCE per kind with the session time. [streamfix] no longer repaired: the header repair
//                could not undo a chunk whose used counter had already run past its end.
//  Any exception latches pw::g_fault: the watch stops for the session, the game is not touched again.
//  Needs (lcudrive.cpp, before it): Log, g_base, PtrOk.
// =============================================================================================
#pragma once
#include "poolwatch_model.h"

namespace pw {

static const uintptr_t RVA_STREAM_POOL = 0x1C6A770;   // *(Pool**): the streaming manager's node pool [P]
static const uintptr_t RVA_STREAM_MGR  = 0x1C6A768;   // *(Mgr**): the manager itself (+0x140 = queued requests)
// [streamfix] the pool's identity and the engine calls used to grow it [P, the exe bytes 2026-11-19]
static const uintptr_t RVA_POOL_NAME   = 0xD0AAD8;    // "LevelStreamingManager::m_RequestMemoryPool" (pool+8)
static const uintptr_t RVA_POOL_PARENT = 0x1A5C198;   // the fixed parent allocator (pool+0x10; vt 0x140C990F0, slot 0 = fail)
static const uintptr_t RVA_SRCPOS      = 0xC99220;    // "...NuMemory.cpp:505" - the engine's own tag for an arena chunk
static const uintptr_t FN_HEAPCTX  = 0x2E05F0;        // () -> the heap context (pool creation passes it, 0x140605A2C)
static const uintptr_t FN_ALLOCOF  = 0x2E0590;        // (ctx) -> this thread's allocator, else ctx+0x18
static const uintptr_t FN_ALLOC    = 0x2DEE00;        // (alloc, size, align, flags, srcpos, short) -> mem
static const uintptr_t FN_ADDCHUNK = 0x2E67D0;        // (pool, mem, size): lock pool+0x838, link at pool+0x28, pool+0x1C += size

typedef void* (__fastcall* HeapCtxFn)();
typedef void* (__fastcall* AllocOfFn)(void* ctx);
typedef void* (__fastcall* AllocFn)(void* alloc, uint64_t size, uint32_t align, uint32_t flags, const char* srcpos, uint16_t z);
typedef void  (__fastcall* AddChunkFn)(void* pool, void* mem, uint64_t size);

static volatile bool g_fault = false;
static pwm::Hdr  s_healthy;
static bool      s_have = false;
static uint64_t  s_pool = 0;
static unsigned  s_logged = 0;      // verdict bits already logged
static pwm::GrowSt s_g;             // [streamfix] per pool instance
static void*     s_mine = nullptr;  // [streamfix] the base of the chunk we added (this pool)
static bool      s_saidPast = false;

static void Read(const uint8_t* p, pwm::Hdr& h)
{
    h.parent = *reinterpret_cast<const volatile uint64_t*>(p + pwm::kParent);
    h.gran   = *reinterpret_cast<const volatile uint32_t*>(p + pwm::kGran);
    h.chunks = *reinterpret_cast<const volatile uint64_t*>(p + pwm::kChunks);
    for (int k = 0; k < pwm::kHeadsChecked; ++k)
        h.heads[k] = *reinterpret_cast<const volatile uint64_t*>(p + pwm::kHeads + 8 * k);
}

// [streamfix] the engine code we call is the code we read (bytes past each prologue)
static bool CodeOk()
{
    static const uint8_t kAlloc[] = { 0x0F, 0xB7, 0x44, 0x24, 0x68, 0x48, 0x8B, 0x6C, 0x24, 0x60, 0x48, 0x8B, 0xFA, 0x66, 0x89, 0x44 };   // +0x14
    static const uint8_t kAdd[]   = { 0x48, 0x8D, 0x8B, 0x38, 0x08, 0x00, 0x00, 0x33, 0xD2, 0x89, 0x78, 0x08, 0x48, 0x89, 0x70, 0x10 };   // +0x56
    static const uint8_t kCtx[]   = { 0x48, 0x8B, 0x05, 0x7C, 0xDE, 0x76, 0x01, 0x48, 0x85, 0xC0, 0x75, 0x35 };                         // +0x0D
    static const uint8_t kOf[]    = { 0x83, 0x79, 0x44, 0xFF, 0x48, 0x8B, 0xD9, 0x75, 0x14, 0x48, 0x8B, 0x0D };                         // +0x06
    return std::memcmp(reinterpret_cast<const void*>(g_base + FN_ALLOC + 0x14), kAlloc, sizeof kAlloc) == 0 &&
           std::memcmp(reinterpret_cast<const void*>(g_base + FN_ADDCHUNK + 0x56), kAdd, sizeof kAdd) == 0 &&
           std::memcmp(reinterpret_cast<const void*>(g_base + FN_HEAPCTX + 0x0D), kCtx, sizeof kCtx) == 0 &&
           std::memcmp(reinterpret_cast<const void*>(g_base + FN_ALLOCOF + 0x06), kOf, sizeof kOf) == 0;
}

struct Walk { uint32_t used = 0, size = 0, stockUsed = 0, stockSize = 0; int chunks = 0; };
static Walk WalkChunks(const uint8_t* pool)
{
    Walk w;
    const uint8_t* c = *reinterpret_cast<uint8_t* const volatile*>(pool + pwm::kChunks);
    for (int i = 0; i < 32 && PtrOk(c); ++i)
    {
        const uint32_t sz = *reinterpret_cast<const volatile uint32_t*>(c + 8);
        const uint32_t used = *reinterpret_cast<const volatile uint32_t*>(c + 0x18);
        const void* base = *reinterpret_cast<void* const volatile*>(c + 0x10);
        w.used += used; w.size += sz; ++w.chunks;
        if (base != s_mine) { w.stockUsed += used; w.stockSize += sz; }
        c = *reinterpret_cast<uint8_t* const volatile*>(c);
    }
    return w;
}

static int QueueLen()
{
    uint8_t* mgr = *reinterpret_cast<uint8_t**>(g_base + RVA_STREAM_MGR);
    return PtrOk(mgr) ? *reinterpret_cast<const volatile int*>(mgr + 0x140) : -1;
}

static void Grow(uint8_t* pool)
{
    if (!pwm::WantGrow(s_g)) return;
    const uintptr_t name = *reinterpret_cast<const uintptr_t*>(pool + 8);
    const uintptr_t parent = *reinterpret_cast<const uintptr_t*>(pool + pwm::kParent);
    const uint32_t gran = *reinterpret_cast<const uint32_t*>(pool + pwm::kGran);
    const bool code = CodeOk();
    if (name != g_base + RVA_POOL_NAME || parent != g_base + RVA_POOL_PARENT || gran != 8 || !code)
    {
        s_g.refused = true;
        Log("!!! streamfix: the streaming request pool %p does not look like the stock one (name %p, parent %p, granule %u, "
            "engine code %s) - NOT grown; the station crash can still happen in a long session\n", pool, (void*)name,
            (void*)parent, gran, code ? "ok" : "DIFFERENT");
        return;
    }
    void* ctx = reinterpret_cast<HeapCtxFn>(g_base + FN_HEAPCTX)();
    void* al = PtrOk(ctx) ? reinterpret_cast<AllocOfFn>(g_base + FN_ALLOCOF)(ctx) : nullptr;
    void* mem = PtrOk(al) ? reinterpret_cast<AllocFn>(g_base + FN_ALLOC)(al, pwm::kGrowBytes, 8, 0,
                                reinterpret_cast<const char*>(g_base + RVA_SRCPOS), 0) : nullptr;
    if (!PtrOk(mem))
    {
        s_g.refused = true;
        Log("!!! streamfix: the engine allocator gave no %u KB for the streaming request pool %p (context %p, allocator %p) - NOT grown\n",
            pwm::kGrowBytes / 1024, pool, ctx, al);
        return;
    }
    const Walk before = WalkChunks(pool);
    reinterpret_cast<AddChunkFn>(g_base + FN_ADDCHUNK)(pool, mem, pwm::kGrowBytes);
    s_mine = mem; s_g.grown = true;
    Log(">>> streamfix: the streaming request pool %p (LevelStreamingManager::m_RequestMemoryPool, %d chunk(s) %u KB, %u nodes carved) "
        "grown by %u KB = +%u request nodes on top of the stock %u (chunk memory %p); queue %d requests\n",
        pool, before.chunks, before.size / 1024, before.used / pwm::kNode, pwm::kGrowBytes / 1024, pwm::kGrowBytes / pwm::kNode,
        pwm::kStockBytes / pwm::kNode, mem, QueueLen());
}

static void TickBody(const char* where)
{
    uint8_t* pool = *reinterpret_cast<uint8_t**>(g_base + RVA_STREAM_POOL);
    if (!PtrOk(pool)) return;
    const uint64_t pa = reinterpret_cast<uint64_t>(pool);
    if (pa != s_pool) { s_pool = pa; s_have = false; s_logged = 0; }   // a new pool: a new baseline
    if (pwm::NewPool(s_g, pa)) { s_mine = nullptr; s_saidPast = false; }
    pwm::Hdr h;
    Read(pool, h);
    const unsigned v = pwm::Check(h, pa, s_have ? &s_healthy : nullptr);
    if (v == pwm::kOk) { s_healthy = h; s_have = true; }
    else
    {
        if (v & ~s_logged)
        {
            s_logged |= v;
            Log("!!! poolwatch (%s): the streaming pool %p is damaged - %s [bits 0x%X] | class-7 head %p, parent %p "
                "(healthy %p), granule %u (healthy %u), chunks %p, manager %p. A level change (Return to police station, "
                "quit) would crash here.\n", where, pool, pwm::Describe(v), v, (void*)h.heads[pwm::kClass],
                (void*)h.parent, (void*)s_healthy.parent, h.gran, s_healthy.gran, (void*)h.chunks,
                *reinterpret_cast<void**>(g_base + RVA_STREAM_MGR));
        }
        return;                                                       // a damaged pool is neither grown nor walked
    }
    if (where[0] == 'w') Grow(pool);                                  // [streamfix] on a world update only
    const Walk w = WalkChunks(pool);
    const uint32_t nodes = w.used / pwm::kNode;
    if (const uint32_t t = pwm::Crossed(s_g, nodes))
        Log("    streamfix: request-pool high-water %u nodes (passed %u) - %u of %u KB carved in %d chunk(s), stock arena %u / %u KB; "
            "queue %d requests (%s)\n", nodes, t, w.used / 1024, w.size / 1024, w.chunks, w.stockUsed / 1024, w.stockSize / 1024,
            QueueLen(), s_g.grown ? "grown" : "NOT grown");
    if (!s_saidPast && w.stockSize && w.stockUsed > w.stockSize)
    {
        s_saidPast = true;
        Log("!!! streamfix: the stock request arena was carved PAST its end (%u of %u bytes) - the pool header may be hit\n",
            w.stockUsed, w.stockSize);
    }
}

static void Tick(const char* where)
{
    if (g_fault || !g_base) return;
    __try { TickBody(where); }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        g_fault = true;
        Log("!!! poolwatch: exception reading the streaming pool - the watch is off for this session\n");
    }
}

} // namespace pw
