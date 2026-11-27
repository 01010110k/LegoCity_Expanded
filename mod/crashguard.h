// =============================================================================================
//  crashguard.h - [crashguard 2026-11-19] guards at the three engine sites that crashed the evening of
//  2026-11-19 (after streamfix). All three are stock code that trusts something the mod's busier world
//  (and split-screen's double streaming) breaks. Nothing here changes gameplay; each guard only turns a
//  certain access violation into the engine's own "nothing here" path. Notes: Notes\66-crash-guards.md.
//
//  1. holder  - RigidObjectHelper's message handler 0x1404B9730 looks up the level holder of the message's
//               object, 0x140512DC0(*(void**)0x141C402D8, *(u16*)(obj+4)), and reads holder+0xD8 without a null
//               test (dumps 5008 / 25692: id 0xFFFF -> null -> AV at 0x1404B9768; same at 0x1404B97C0). Both
//               lookups are re-targeted to Holder(): a missing holder becomes a zeroed stand-in, so +0xD8 reads 0
//               and the handler's own `je 0x1404B97E2` (return) is taken.
//  2. levelor - 0x140511D60 resolves a level holder inline (table 0x141C402D8, else r12 = 0) and then does
//               `or [rdi+0xE0], ecx` (dump 29000: rdi = 0 -> AV at 0x140511F54). The 6-byte `or` becomes
//               `call stub ; nop`; the stub skips the `or` when rdi is 0. rdi is not read again before 0x1405120F7
//               resets it [P].
//  3. pool    - the engine pool allocator 0x1402E7060 pops a lock-free free list (head at pool+0x30+8*class) and
//               trusts the head. Dump 32000: the ScriptMemory pool (*0x141C6C688) head was 0x140033BF0, a code
//               address (a ReferenceCounted object destroyed twice left its vtable where the free link belongs);
//               the next alloc built an SCmdAction there -> AV at 0x140660BB2. The station crash (0x1402E70F0,
//               head 3) is the same class. The 12-byte NOP at 0x1402E70E4 (after the null test, before the pop
//               loop) becomes `call stub ; nop7`. The stub checks the head: below 0x10000, not 8-aligned,
//               non-canonical, inside the exe image, or inside the pool header = bad -> the head is swapped to 0
//               (lock cmpxchg; the bad chain leaks) and the stub returns to the slow path 0x1402E710A, which
//               carves a fresh block. A good head returns to the pop as before. The CAS-retry path (0x1402E7101)
//               re-reads the head without the check (contention only).
//
//  Every site's bytes are compared with the stock bytes first; a mismatch = that guard is not installed.
//  Installed once at load, never removed (they are not mod features; '[' / unhook leave them in).
//  The counts are logged from the world update when they change. [general] crash_guards = 0 disables all three.
//  Needs (lcudrive.cpp, before it): Log, g_base, eng::kImageSize, hooks.h (CallPatch, ApplyPatch, AllocNear).
// =============================================================================================
#pragma once
#include <cstdint>
#include <cstring>
#include <initializer_list>

namespace cg {

static const uintptr_t FN_HOLDER      = 0x512DC0;   // (table, u16 id) -> holder or 0 (id > 0x7FF -> 0) [P]
static const uintptr_t SITE_HOLDER_A  = 0x4B9763;   // call FN_HOLDER ; mov rcx, [rax+0xD8]   [P]
static const uintptr_t SITE_HOLDER_B  = 0x4B97BB;   // the same, second message type          [P]
static const uintptr_t SITE_LEVELOR   = 0x511F54;   // or dword ptr [rdi+0xE0], ecx            [P]
static const uintptr_t SITE_POOLNOP   = 0x2E70E4;   // 12-byte NOP between the head test and the pop loop [P]
static const uintptr_t SITE_POOLSLOW  = 0x2E710A;   // the slow path: mov r8, r14 ; mov rdx, rsi ; mov rcx, rbx ; call refill [P]

static const uintptr_t SITE_CELLCVT   = 0x611395;   // pos -> cell 0x140611320: cvttss2si ecx, xmm1 ; cvttss2si edx, xmm2 [P]

static const uint8_t kCellCvt[8]  = { 0xF3, 0x0F, 0x2C, 0xC9, 0xF3, 0x0F, 0x2C, 0xD2 };
static const uint8_t kCellAfter[7] = { 0x4D, 0x85, 0xC0, 0x74, 0x37, 0x8B, 0x05 };   // 0x61139D: test r8, r8 ; je ; mov eax, [..]
static const uint8_t kLevelOr[6]  = { 0x09, 0x8F, 0xE0, 0x00, 0x00, 0x00 };
static const uint8_t kPoolNop[12] = { 0x66, 0x66, 0x66, 0x66, 0x0F, 0x1F, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00 };
// the code around each site, checked too (so a different exe build is refused)
static const uint8_t kPoolCtx[13] = { 0x48, 0x8D, 0x14, 0xC3, 0x48, 0x8B, 0x7A, 0x30, 0x48, 0x85, 0xFF, 0x74, 0x26 };  // 0x2E70D7
static const uint8_t kPoolAfter[6] = { 0x48, 0x8B, 0x0F, 0x48, 0x8B, 0xC7 };                                         // 0x2E70F0
static const uint8_t kPoolSlow[9] = { 0x4D, 0x8B, 0xC6, 0x48, 0x8B, 0xD6, 0x48, 0x8B, 0xCB };                         // 0x2E710A
static const uint8_t kLevelCtx[14] = { 0x8B, 0x8B, 0xC4, 0x00, 0x00, 0x00, 0x8B, 0x42, 0x10, 0xFF, 0xC0, 0x89, 0x42, 0x10 }; // 0x511F46

static volatile LONG g_holder = 0, g_levelOr = 0, g_badHead = 0;   // guards that fired (each = a crash avoided)
static LONG s_said[3] = { 0, 0, 0 };
static bool s_inst[3] = { false, false, false };
static bool s_cellInst = false;
alignas(16) static uint8_t g_zeroHolder[0x400] = {};                // read-only stand-in: every field 0
// [slots2] the level table's size as the loader left it (proxy\levelslots.h): 0x800 slots stock, 0x1000 enlarged
static uint32_t g_idMax = 0x7FF;                                    // the last valid level id
static int      g_slotCap = 0x800;
static void**   g_twin = nullptr;                                   // the twin array {level, object} per slot

// ---- pure code emitters (also used by test\crashguard_test.cpp) ----------------------------------------------------

struct Asm
{
    uint8_t* s; int n = 0;
    explicit Asm(uint8_t* p) : s(p) {}
    void b(std::initializer_list<uint8_t> x) { for (uint8_t v : x) s[n++] = v; }
    void q(uint64_t v) { std::memcpy(s + n, &v, 8); n += 8; }
    int  jcc(uint8_t op) { s[n++] = op; s[n++] = 0; return n - 1; }      // returns the rel8 slot
    void bind(int slot) { s[slot] = static_cast<uint8_t>(n - (slot + 1)); }
};

// The pool stub. Entry (from `call` at the NOP): rdi = the non-null head, rdx = pool + 8*class, rbx = pool.
// Clobbers rax, rcx only (both dead at the site). Bad head -> CAS it to 0, count, return to `slow`.
inline int EmitPoolStub(uint8_t* p, uint64_t imgLo, uint64_t imgHi, uint64_t slow, volatile LONG* counter)
{
    Asm a(p);
    a.b({ 0x48, 0x81, 0xFF, 0x00, 0x00, 0x01, 0x00 });  // cmp rdi, 0x10000
    const int j1 = a.jcc(0x72);                           // jb bad
    a.b({ 0x40, 0xF6, 0xC7, 0x07 });                      // test dil, 7
    const int j2 = a.jcc(0x75);                           // jnz bad
    a.b({ 0x48, 0x89, 0xF8 });                            // mov rax, rdi
    a.b({ 0x48, 0xC1, 0xE8, 0x2F });                      // shr rax, 47
    const int j3 = a.jcc(0x75);                           // jnz bad (non-canonical / kernel)
    a.b({ 0x48, 0xB8 }); a.q(imgLo);                      // mov rax, imgLo
    a.b({ 0x48, 0x39, 0xC7 });                            // cmp rdi, rax
    const int jh = a.jcc(0x72);                           // jb hdr
    a.b({ 0x48, 0xB8 }); a.q(imgHi);                      // mov rax, imgHi
    a.b({ 0x48, 0x39, 0xC7 });                            // cmp rdi, rax
    const int j4 = a.jcc(0x72);                           // jb bad (inside the image)
    a.bind(jh);
    a.b({ 0x48, 0x89, 0xF8 });                            // hdr: mov rax, rdi
    a.b({ 0x48, 0x29, 0xD8 });                            // sub rax, rbx
    a.b({ 0x48, 0x3D, 0x40, 0x08, 0x00, 0x00 });          // cmp rax, 0x840
    const int j5 = a.jcc(0x72);                           // jb bad (inside the pool header)
    a.b({ 0xC3 });                                        // ret -> the pop
    a.bind(j1); a.bind(j2); a.bind(j3); a.bind(j4); a.bind(j5);
    a.b({ 0x48, 0x89, 0xF8 });                            // bad: mov rax, rdi
    a.b({ 0x31, 0xC9 });                                  // xor ecx, ecx
    a.b({ 0xF0, 0x48, 0x0F, 0xB1, 0x4A, 0x30 });          // lock cmpxchg [rdx+0x30], rcx
    a.b({ 0x48, 0xB8 }); a.q(reinterpret_cast<uint64_t>(counter));
    a.b({ 0xF0, 0xFF, 0x00 });                            // lock inc dword ptr [rax]
    a.b({ 0x31, 0xFF });                                  // xor edi, edi
    a.b({ 0x48, 0xB8 }); a.q(slow);                       // mov rax, slow
    a.b({ 0x48, 0x89, 0x04, 0x24 });                      // mov [rsp], rax
    a.b({ 0xC3 });                                        // ret -> the slow path
    return a.n;
}

// The level-or stub: `or [rdi+0xE0], ecx` unless rdi is 0 (then count). Keeps every register.
inline int EmitLevelOrStub(uint8_t* p, volatile LONG* counter)
{
    Asm a(p);
    a.b({ 0x48, 0x85, 0xFF });                            // test rdi, rdi
    const int jz = a.jcc(0x74);                           // jz none
    a.b({ 0x09, 0x8F, 0xE0, 0x00, 0x00, 0x00 });          // or dword ptr [rdi+0xE0], ecx
    a.b({ 0xC3 });
    a.bind(jz);
    a.b({ 0x50 });                                        // push rax
    a.b({ 0x48, 0xB8 }); a.q(reinterpret_cast<uint64_t>(counter));
    a.b({ 0xF0, 0xFF, 0x00 });                            // lock inc dword ptr [rax]
    a.b({ 0x58 });                                        // pop rax
    a.b({ 0xC3 });
    return a.n;
}

// [edgeguard 2026-11-24] The cell stub: the two conversions of pos -> cell, but a NEGATIVE coordinate gives -1
// (cvttss2si truncates toward zero, so -0.99..-0.0 became row / column 0: the strip 30 u outside the grid's
// north and west edge counted as the edge cells). The function's own `js` tests then return "no cell".
// Why it matters: the "is the ground loaded here" helper 0x1404A1910 asks for the cell under each corner of a
// vehicle. Driving north over z 480 (the Wilds), the streaming's own cell leaves the grid and the row-0 cells
// unload while the vehicle's rear corners still map to one of them -> "not loaded" -> the vehicle is held for
// good (the freezes of 2026-11-24 at (-451, 480), bike and truck). Clobbers r9 only (volatile, unused there).
inline int EmitCellStub(uint8_t* p)
{
    Asm a(p);
    a.b({ 0xF3, 0x0F, 0x2C, 0xC9 });                      // cvttss2si ecx, xmm1
    a.b({ 0xF3, 0x0F, 0x2C, 0xD2 });                      // cvttss2si edx, xmm2
    a.b({ 0x66, 0x41, 0x0F, 0x7E, 0xC9 });                // movd r9d, xmm1
    a.b({ 0x45, 0x85, 0xC9 });                            // test r9d, r9d
    const int j1 = a.jcc(0x79);                           // jns
    a.b({ 0x83, 0xC9, 0xFF });                            // or ecx, -1
    a.bind(j1);
    a.b({ 0x66, 0x41, 0x0F, 0x7E, 0xD1 });                // movd r9d, xmm2
    a.b({ 0x45, 0x85, 0xC9 });                            // test r9d, r9d
    const int j2 = a.jcc(0x79);                           // jns
    a.b({ 0x83, 0xCA, 0xFF });                            // or edx, -1
    a.bind(j2);
    a.b({ 0xC3 });
    return a.n;
}

// The holder lookup, as 0x140512DC0 does it, but never null.
static void* __fastcall Holder(uint8_t* table, uint32_t id)
{
    const uint16_t i = static_cast<uint16_t>(id);
    void* h = nullptr;
    if (table && i <= g_idMax) h = *reinterpret_cast<void* const volatile*>(table + 0x18 + 8 * static_cast<size_t>(i));
    if (h) return h;
    InterlockedIncrement(&g_holder);
    return g_zeroHolder;
}

// ---- [slots 2026-11-26] the level table is full -------------------------------------------------------------------
// ROOT CAUSE of the "object whose level is gone" family (dumps 20132 and 34852, co-op): the level-holder table
// (*0x141C402D8, holders at +0x18 + 8*i) and its static twin 0x141C402F0 (16 B per slot: holder, object) have
// 0x800 slots. The level constructor 0x1405108xx takes one with 0x140511650(table, level) (linear search for a
// free slot, `cmp ebx, 0x800`) and keeps the result in level+0x78 - which is -1 when every slot is taken. Both dumps:
// 2,048 of 2,048 used (an earlier single-player dump: 1,106). A level with index -1 gives its objects id 0xFFFF
// (-> the null holder lookups above), and when its load completes, 0x140514736 indexes the twin with -1:
// slot[-1] is the table lock's name pointer, taken for an object and released -> AV at 0x140514761.
//   * SlotStub: at 0x140514736 (`movsx rbx, word [rdi+0x78]`), a negative index skips the store and resumes at
//     0x140514786 (drop the local reference, unlock, carry on). Clobbers rax (dead there).
//   * Hook_LevelSlot: the constructor's call at 0x140510A76, logged when it fails - with what fills the table
//     (names by prefix, names held more than once) the first time, so the cause of the overflow can be fixed.
static const uintptr_t SITE_SLOTIDX  = 0x514736;   // movsx rbx, word ptr [rdi+0x78] ; shl rbx, 4 ; lea rax, [twin]  [P]
static const uintptr_t SITE_SLOTSKIP = 0x514786;   // mov rcx, [rsp+0x58] ; test rcx, rcx ; je ; release ...          [P]
static const uintptr_t FN_SLOTALLOC  = 0x511650, SITE_SLOTALLOC = 0x510A76, RVA_TWIN = 0x1C402F0;
static const uint8_t kSlotIdx[12]  = { 0x48, 0x0F, 0xBF, 0x5F, 0x78, 0x48, 0xC1, 0xE3, 0x04, 0x48, 0x8D, 0x05 };
static const uint8_t kSlotSkip[8]  = { 0x48, 0x8B, 0x4C, 0x24, 0x58, 0x48, 0x85, 0xC9 };
static volatile LONG g_slotSkip = 0, g_noSlot = 0;
static uintptr_t s_base = 0;

inline int EmitSlotStub(uint8_t* p, uint64_t resume, volatile LONG* counter)
{
    Asm a(p);
    a.b({ 0x48, 0x0F, 0xBF, 0x5F, 0x78 });                // movsx rbx, word ptr [rdi+0x78]
    a.b({ 0x48, 0x85, 0xDB });                            // test rbx, rbx
    const int js = a.jcc(0x78);                           // js bad
    a.b({ 0xC3 });                                        // ret -> shl rbx, 4 ...
    a.bind(js);
    a.b({ 0x48, 0xB8 }); a.q(reinterpret_cast<uint64_t>(counter));
    a.b({ 0xF0, 0xFF, 0x00 });                            // lock inc dword ptr [rax]
    a.b({ 0x48, 0xB8 }); a.q(resume);                     // mov rax, resume
    a.b({ 0x48, 0x89, 0x04, 0x24 });                      // mov [rsp], rax
    a.b({ 0xC3 });                                        // ret -> the skip
    return a.n;
}

static const char* LevelName(void* level)
{
    __try
    {
        void** vt = *reinterpret_cast<void***>(level);
        typedef const char* (__fastcall* NameFn)(void*);
        const char* s = reinterpret_cast<NameFn>(vt[0x88 / 8])(level);
        return (s && s[0]) ? s : nullptr;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}

// What fills the table: used slots, names by prefix (up to the last '_'), and how many names are held twice or more.
static void LogSlotCensus(const char* why)
{
    struct E { char k[40]; int n; };
    static E pre[96]; static uint32_t seen[4096]; int np = 0, used = 0, named = 0, dup = 0, ns = 0;
    __try
    {
        void** twin = g_twin;
        for (int i = 0; twin && i < g_slotCap; ++i)
        {
            void* lv = twin[2 * i];
            if (!lv) continue;
            ++used;
            const char* s = LevelName(lv);
            if (!s) continue;
            ++named;
            uint32_t h = 2166136261u; int len = 0, cut = 0;
            for (const char* q = s; *q && len < 200; ++q, ++len) { h = (h ^ static_cast<uint8_t>(*q | 0x20)) * 16777619u; if (*q == '_') cut = len; }
            bool had = false;
            for (int j = 0; j < ns && !had; ++j) had = seen[j] == h;
            if (had) ++dup; else if (ns < 4096) seen[ns++] = h;
            char k[40]; int kl = cut > 0 ? cut : len; if (kl > 39) kl = 39;
            std::memcpy(k, s, kl); k[kl] = 0;
            int j = 0;
            for (; j < np; ++j) if (!_stricmp(pre[j].k, k)) { ++pre[j].n; break; }
            if (j == np && np < 96) { std::memcpy(pre[np].k, k, 40); pre[np].n = 1; ++np; }
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
    Log("!!! level table (%s): %d of %d slots used, %d named, %d distinct names, %d repeats of a name already held\n",
        why, used, g_slotCap, named, ns, dup);
    for (int r = 0; r < 14; ++r)                                         // the 14 biggest prefixes
    {
        int best = -1;
        for (int j = 0; j < np; ++j) if (pre[j].n > 0 && (best < 0 || pre[j].n > pre[best].n)) best = j;
        if (best < 0) break;
        Log("    level table: %4d x %s_*\n", pre[best].n, pre[best].k);
        pre[best].n = -pre[best].n;
    }
}

typedef uint64_t(__fastcall* SlotAllocFn)(void* table, void* level);
static uint64_t __fastcall Hook_LevelSlot(void* table, void* level)
{
    const uint64_t r = reinterpret_cast<SlotAllocFn>(s_base + FN_SLOTALLOC)(table, level);
    static volatile LONG s_calls = 0, s_mark = 0;
    const LONG c = InterlockedIncrement(&s_calls);
    if (static_cast<int16_t>(r) < 0)
    {
        const LONG n = InterlockedIncrement(&g_noSlot);
        if (n <= 12) { const char* s = LevelName(level); Log("!!! level table FULL (%d slots): no slot for %.80s (%ld so far)\n", g_slotCap, s ? s : "?", n); }
        if (n == 1) LogSlotCensus("full");
    }
    else if ((c & 31) == 0)                                              // a census each time 200 more slots are in use
    {
        int used = 0;
        __try { void** twin = g_twin; for (int i = 0; twin && i < g_slotCap; ++i) if (twin[2 * i]) ++used; }
        __except (EXCEPTION_EXECUTE_HANDLER) {}
        const LONG step = used / 200;
        if (step > s_mark && used >= 1200) { InterlockedExchange(&s_mark, step); LogSlotCensus("growing"); }
    }
    return r;
}
static CallPatch g_slotAlloc = { "level slot", SITE_SLOTALLOC, FN_SLOTALLOC, nullptr, 0, false };

// ---- installation ---------------------------------------------------------------------------------------------------

static uint8_t* s_page = nullptr;
static int      s_used = 0;
static uint8_t* Stub(uintptr_t base, int bytes)
{
    if (!s_page) s_page = AllocNear(base);
    if (!s_page || s_used + bytes > 0x1000) return nullptr;
    uint8_t* p = s_page + s_used; s_used += (bytes + 15) & ~15;
    return p;
}

static bool Same(uintptr_t a, const uint8_t* b, size_t n)
{
    __try { return std::memcmp(reinterpret_cast<const void*>(a), b, n) == 0; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// Writes `code` (len >= 2) at site so no thread decodes a torn instruction: a short jump over the region first
// (when `jumpIsNop`, that equals the old bytes' effect), then the tail, then the first two bytes in one store.
static bool Write(uint8_t* site, const uint8_t* code, int len, bool jumpIsNop)
{
    if ((reinterpret_cast<uintptr_t>(site) & 15) == 15) return false;   // the 2-byte head must not straddle a fetch block
    DWORD old = 0;
    if (!VirtualProtect(site, len, PAGE_EXECUTE_READWRITE, &old)) return false;
    if (jumpIsNop)
    {
        *reinterpret_cast<volatile uint16_t*>(site) = static_cast<uint16_t>(0xEB | ((len - 2) << 8));   // jmp short +len-2
        FlushInstructionCache(GetCurrentProcess(), site, len);
    }
    for (int i = 2; i < len; ++i) reinterpret_cast<volatile uint8_t*>(site)[i] = code[i];
    FlushInstructionCache(GetCurrentProcess(), site, len);
    uint16_t head; std::memcpy(&head, code, 2);
    *reinterpret_cast<volatile uint16_t*>(site) = head;
    VirtualProtect(site, len, old, &old);
    FlushInstructionCache(GetCurrentProcess(), site, len);
    return true;
}

static bool CallAt(uintptr_t siteVa, const uint8_t* stub, uint8_t* code)
{
    const intptr_t d = reinterpret_cast<intptr_t>(stub) - static_cast<intptr_t>(siteVa + 5);
    if (d < INT32_MIN || d > INT32_MAX) return false;
    const int32_t rel = static_cast<int32_t>(d);
    code[0] = 0xE8; std::memcpy(code + 1, &rel, 4);
    return true;
}

static CallPatch g_holderA = { "guard-hold1", SITE_HOLDER_A, FN_HOLDER, nullptr, 0, false };
static CallPatch g_holderB = { "guard-hold2", SITE_HOLDER_B, FN_HOLDER, nullptr, 0, false };
static const uintptr_t FN_HOLDER_OBJ = 0x4B6810;   // (holder, out) -> the object at holder+0xF8 [P]
static CallPatch g_holderMore[] = {
    { "guard-hold3", 0x50BA5D, FN_HOLDER, nullptr, 0, false }, { "guard-hold3", 0x50BAD8, FN_HOLDER, nullptr, 0, false },
    { "guard-hold3", 0x4E8693, FN_HOLDER, nullptr, 0, false }, { "guard-hold3", 0x5BDAC2, FN_HOLDER, nullptr, 0, false },
    { "guard-hold3", 0xA2C11C, FN_HOLDER, nullptr, 0, false }, { "guard-hold3", 0x4B68C9, FN_HOLDER, nullptr, 0, false },
};

static void Init(uintptr_t base, bool on)
{
    if (!on) { Log("crash guards: OFF ([general] crash_guards = 0)\n"); return; }

    // 1. holder lookups (call retargets: one atomic rel32 store each)
    const uint8_t movRcx[7] = { 0x48, 0x8B, 0x88, 0xD8, 0x00, 0x00, 0x00 };
    if (Same(base + SITE_HOLDER_A + 5, movRcx, 7) && Same(base + SITE_HOLDER_B + 5, movRcx, 7))
    {
        g_holderA.replacement = reinterpret_cast<void*>(&Holder);
        g_holderB.replacement = reinterpret_cast<void*>(&Holder);
        s_inst[0] = ApplyPatch(base, g_holderA) & ApplyPatch(base, g_holderB);
    }
    else Log("  guard holder REFUSED: the code after the lookups is not the expected one\n");

    // 1b. [holder2 2026-11-26] the same lookup, six more callers that hand the result straight to
    // 0x1404B6810(holder, out) = "the object at holder+0xF8" without a null test (dump 20132, co-op, 35 s after
    // the level came up: 0x14050BA5D -> null -> 0x1404B6810 -> AV at 0x1406341F4 reading 0xF8). With the zeroed
    // stand-in, 0x1406341C0 reads 0 at +0xF8 and returns out = 0; every caller below tests out (or holder+0xD8,
    // also 0) and leaves [P: 0x50BA5D, 0x50BAD8, 0x4E8693, 0x5BDAC2, 0xA2C11C; 0x4B68C9 does not test at once -
    // stock would crash there for certain, so the stand-in cannot be worse].
    {
        int n = 0;
        for (CallPatch& p : g_holderMore)
        {
            bool feeds = false;                                    // a call to 0x1404B6810 within the next 16 bytes
            for (int k = 5; k <= 16 && !feeds; ++k)
            {
                const uint8_t* q = reinterpret_cast<const uint8_t*>(base + p.siteRva + k);
                int32_t rel; std::memcpy(&rel, q + 1, 4);
                feeds = q[0] == 0xE8 && base + p.siteRva + k + 5 + rel == base + FN_HOLDER_OBJ;
            }
            if (!feeds) { Log("  guard holder2 REFUSED at rva 0x%llX: not the expected code\n", (unsigned long long)p.siteRva); continue; }
            p.replacement = reinterpret_cast<void*>(&Holder);
            if (ApplyPatch(base, p)) ++n;
        }
        Log("  guard holder2: %d of %d more lookups guarded\n", n, (int)(sizeof(g_holderMore) / sizeof(g_holderMore[0])));
    }

    // 1c. [slots] the level table is full: skip the store at index -1, and log the failed allocations
    s_base = base;
    g_twin = reinterpret_cast<void**>(base + RVA_TWIN);
    __try                                                              // [slots2] what did the loader do?
    {
        const uint32_t lim = *reinterpret_cast<const uint32_t*>(base + 0x512DC1);        // 0x140512DC0 mov eax, <last id>
        const uint8_t* lea = reinterpret_cast<const uint8_t*>(base + 0x5116C2);            // lea rcx, [twin]
        int32_t rel; std::memcpy(&rel, lea + 3, 4);
        g_twin = reinterpret_cast<void**>(const_cast<uint8_t*>(lea) + 7 + rel);
        if (lim == 0xFFF) { g_idMax = 0xFFF; g_slotCap = 0x1000; }
        char why[16] = "?"; GetEnvironmentVariableA("LCU_LEVEL_SLOTS", why, sizeof why);
        Log("level table: %d slots (%s; loader result %s: 1 = enlarged, -1 = switched off by lcu_stock_level_table.txt, "
            "-2 = not the expected game code or too late, -3 = no memory near the game, -4 = write failed), twin at %p\n",
            g_slotCap, g_slotCap == 0x1000 ? "ENLARGED" : "stock - co-op and busy areas can still fill it", why, (void*)g_twin);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { g_twin = reinterpret_cast<void**>(base + RVA_TWIN); }
    if (Same(base + SITE_SLOTIDX, kSlotIdx, 12) && Same(base + SITE_SLOTSKIP, kSlotSkip, 8))
    {
        uint8_t* st = Stub(base, 48);
        uint8_t code[5] = { 0, 0, 0, 0, 0 };
        if (st && (EmitSlotStub(st, base + SITE_SLOTSKIP, &g_slotSkip), FlushInstructionCache(GetCurrentProcess(), st, 48), true) &&
            CallAt(base + SITE_SLOTIDX, st, code) &&
            Write(reinterpret_cast<uint8_t*>(base + SITE_SLOTIDX), code, 5, false))
            Log("  guard slots   ok: level index -1 skips the table store at rva 0x%llX -> stub %p\n", (unsigned long long)SITE_SLOTIDX, (void*)st);
        else Log("  guard slots   REFUSED: no stub memory / out of range\n");
    }
    else Log("  guard slots   REFUSED: the bytes at rva 0x%llX are not the expected ones\n", (unsigned long long)SITE_SLOTIDX);
    g_slotAlloc.replacement = reinterpret_cast<void*>(&Hook_LevelSlot);
    if (!ApplyPatch(base, g_slotAlloc)) Log("  level slot log NOT installed\n");

    // 2. level-or
    if (Same(base + SITE_LEVELOR, kLevelOr, 6) && Same(base + SITE_LEVELOR - 14, kLevelCtx, 14))
    {
        uint8_t* st = Stub(base, 48);
        uint8_t code[6] = { 0, 0, 0, 0, 0, 0x90 };
        if (st && (EmitLevelOrStub(st, &g_levelOr), FlushInstructionCache(GetCurrentProcess(), st, 48), true) &&
            CallAt(base + SITE_LEVELOR, st, code) &&
            Write(reinterpret_cast<uint8_t*>(base + SITE_LEVELOR), code, 6, true))   // the brief jump skips one `or`
        {
            s_inst[1] = true;
            Log("  guard levelor ok: or at rva 0x%llX -> stub %p\n", (unsigned long long)SITE_LEVELOR, (void*)st);
        }
        else Log("  guard levelor REFUSED: no stub memory / out of range\n");
    }
    else Log("  guard levelor REFUSED: the bytes at rva 0x%llX are not the expected ones\n", (unsigned long long)SITE_LEVELOR);

    // 3. pool free-list head check
    if (Same(base + SITE_POOLNOP, kPoolNop, 12) && Same(base + 0x2E70D7, kPoolCtx, 13) &&
        Same(base + 0x2E70F0, kPoolAfter, 6) && Same(base + SITE_POOLSLOW, kPoolSlow, 9))
    {
        uint8_t* st = Stub(base, 128);
        uint8_t code[12] = { 0, 0, 0, 0, 0, 0x0F, 0x1F, 0x80, 0x00, 0x00, 0x00, 0x00 };   // call stub ; nop7
        if (st && (EmitPoolStub(st, base, base + eng::kImageSize, base + SITE_POOLSLOW, &g_badHead),
                   FlushInstructionCache(GetCurrentProcess(), st, 128), true) &&
            CallAt(base + SITE_POOLNOP, st, code) &&
            Write(reinterpret_cast<uint8_t*>(base + SITE_POOLNOP), code, 12, true))
        {
            s_inst[2] = true;
            Log("  guard pool    ok: free-list head check at rva 0x%llX -> stub %p\n", (unsigned long long)SITE_POOLNOP, (void*)st);
        }
        else Log("  guard pool    REFUSED: no stub memory / out of range\n");
    }
    else Log("  guard pool    REFUSED: the allocator bytes at rva 0x%llX are not the expected ones\n", (unsigned long long)SITE_POOLNOP);

    // 4. [edgeguard] pos -> cell: negative coordinates are outside the grid
    if (Same(base + SITE_CELLCVT, kCellCvt, 8) && Same(base + SITE_CELLCVT + 8, kCellAfter, 7))
    {
        uint8_t* st = Stub(base, 48);
        uint8_t code[8] = { 0, 0, 0, 0, 0, 0x0F, 0x1F, 0x00 };   // call stub ; nop3
        if (st && (EmitCellStub(st), FlushInstructionCache(GetCurrentProcess(), st, 48), true) &&
            CallAt(base + SITE_CELLCVT, st, code) &&
            Write(reinterpret_cast<uint8_t*>(base + SITE_CELLCVT), code, 8, true))
        {
            s_cellInst = true;
            Log("  guard edge    ok: pos -> cell at rva 0x%llX -> stub %p (the strip outside the grid edge is no cell)\n",
                (unsigned long long)SITE_CELLCVT, (void*)st);
        }
        else Log("  guard edge    REFUSED: no stub memory / out of range\n");
    }
    else Log("  guard edge    REFUSED: the bytes at rva 0x%llX are not the expected ones\n", (unsigned long long)SITE_CELLCVT);

    Log("crash guards: level holder %s, level flag %s, pool free lists %s\n", s_inst[0] ? "on" : "OFF",
        s_inst[1] ? "on" : "OFF", s_inst[2] ? "on" : "OFF");
}

// From the world update: say when a guard has fired (each = a crash the stock game would have had).
static void Tick(double t)
{
    static double s_next[3] = { 0, 0, 0 };                             // at most one line per guard per 10 s
    const LONG now[3] = { g_holder, g_levelOr, g_badHead };
    static const char* what[3] = {
        "a message for an object whose level is gone (stock: crash at 0x4B9768)",
        "a level flag for an unloaded level (stock: crash at 0x511F54)",
        "a corrupt pool free list dropped, fresh memory used (stock: crash at 0x660BB2 / 0x2E70F0)" };
    static LONG s_skipSaid = 0;
    if (g_slotSkip != s_skipSaid)
    {
        s_skipSaid = g_slotSkip;
        Log("!!! crashguard at %.1f s: a level without a table slot finished loading (stock: crash at 0x514761) - %ld so far this session\n", t, s_skipSaid);
    }
    for (int i = 0; i < 3; ++i)
        if (now[i] != s_said[i] && t >= s_next[i])
        {
            Log("!!! crashguard at %.1f s: %s - %ld so far this session\n", t, what[i], now[i]);
            s_said[i] = now[i]; s_next[i] = t + 10.0;
        }
}

} // namespace cg
