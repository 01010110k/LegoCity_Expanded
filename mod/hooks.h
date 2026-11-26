// =============================================================================================
//  hooks.h - call-site redirection.
//
//  lcudrive replaces three engine routines FOR THE PLAYER'S CAR ONLY by retargeting the single
//  `call rel32` that invokes each one. Every other vehicle still calls the original - the stub
//  forwards to a C++ function that decides per call. Before any byte is written the site must
//  read exactly `E8 <rel32>` and resolve to the expected callee; otherwise nothing is patched.
//
//  A stub is `mov rax, imm64 ; jmp rax` in a page allocated within +-2 GB of the image, so the
//  rel32 can reach it. The original rel32 is kept and restored on unhook.
// =============================================================================================
#pragma once

struct CallPatch
{
    const char* name;
    uintptr_t   siteRva;     // address of the E8 byte
    uintptr_t   calleeRva;   // what the call must currently resolve to
    void*       replacement; // our function (same signature as the callee)
    int32_t     origRel;
    bool        applied;
};

static uint8_t* g_stubPage = nullptr;
static int      g_stubUsed = 0;

static uint8_t* AllocNear(uintptr_t nearAddr)
{
    SYSTEM_INFO si; GetSystemInfo(&si);
    const uintptr_t gran = si.dwAllocationGranularity;
    const uintptr_t base = nearAddr & ~(gran - 1);
    for (uintptr_t off = gran; off < 0x7FF00000ull; off += gran)
    {
        void* p = VirtualAlloc(reinterpret_cast<void*>(base - off), 0x1000,
                               MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
        if (p) return static_cast<uint8_t*>(p);
        p = VirtualAlloc(reinterpret_cast<void*>(base + eng::kImageSize + off), 0x1000,
                         MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
        if (p) return static_cast<uint8_t*>(p);
    }
    return nullptr;
}

static bool ApplyPatch(uintptr_t base, CallPatch& p)
{
    uint8_t* site = reinterpret_cast<uint8_t*>(base + p.siteRva);
    __try
    {
        if (site[0] != 0xE8) { Log("  patch %-12s REFUSED: not a call at rva 0x%llX\n", p.name, (unsigned long long)p.siteRva); return false; }
        int32_t rel; std::memcpy(&rel, site + 1, 4);
        const uintptr_t tgt = reinterpret_cast<uintptr_t>(site) + 5 + rel;
        if (tgt != base + p.calleeRva)
        {
            Log("  patch %-12s REFUSED: call resolves to rva 0x%llX, expected 0x%llX (another mod?)\n",
                p.name, (unsigned long long)(tgt - base), (unsigned long long)p.calleeRva);
            return false;
        }
        p.origRel = rel;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }

    if (!g_stubPage) g_stubPage = AllocNear(base);
    if (!g_stubPage || g_stubUsed + 16 > 0x1000) { Log("  patch %-12s REFUSED: no stub memory\n", p.name); return false; }
    uint8_t* stub = g_stubPage + g_stubUsed;
    g_stubUsed += 16;
    const uint64_t fn = reinterpret_cast<uint64_t>(p.replacement);
    stub[0] = 0x48; stub[1] = 0xB8; std::memcpy(stub + 2, &fn, 8);   // mov rax, imm64
    stub[10] = 0xFF; stub[11] = 0xE0;                                  // jmp rax
    FlushInstructionCache(GetCurrentProcess(), stub, 16);

    const intptr_t d = reinterpret_cast<intptr_t>(stub) - (reinterpret_cast<intptr_t>(site) + 5);
    if (d < INT32_MIN || d > INT32_MAX) { Log("  patch %-12s REFUSED: stub out of rel32 range\n", p.name); return false; }
    const int32_t newRel = static_cast<int32_t>(d);

    DWORD old = 0;
    if (!VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &old)) return false;
    InterlockedExchange(reinterpret_cast<volatile LONG*>(site + 1), newRel);   // one 4-byte store
    VirtualProtect(site, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), site, 5);
    p.applied = true;
    Log("  patch %-12s ok: call at rva 0x%llX -> stub %p -> %p\n", p.name,
        (unsigned long long)p.siteRva, (void*)stub, p.replacement);
    return true;
}

static void RemovePatch(uintptr_t base, CallPatch& p)
{
    if (!p.applied) return;
    uint8_t* site = reinterpret_cast<uint8_t*>(base + p.siteRva);
    DWORD old = 0;
    if (VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &old))
    {
        InterlockedExchange(reinterpret_cast<volatile LONG*>(site + 1), p.origRel);
        VirtualProtect(site, 5, old, &old);
        FlushInstructionCache(GetCurrentProcess(), site, 5);
    }
    p.applied = false;
}

// ---------------------------------------------------------------------------------------------
//  A call patch whose replacement also needs registers the call does not pass. The stub copies
//  rdi / rsi (callee-saved, so they still hold the caller's locals) into rdx / r8 before jumping:
//  the replacement is called as (original rcx, rdi, rsi). Same checks as ApplyPatch.
// ---------------------------------------------------------------------------------------------
static bool ApplyPatchRegs(uintptr_t base, CallPatch& p)
{
    uint8_t* site = reinterpret_cast<uint8_t*>(base + p.siteRva);
    __try
    {
        if (site[0] != 0xE8) { Log("  patch %-12s REFUSED: not a call at rva 0x%llX\n", p.name, (unsigned long long)p.siteRva); return false; }
        int32_t rel; std::memcpy(&rel, site + 1, 4);
        const uintptr_t tgt = reinterpret_cast<uintptr_t>(site) + 5 + rel;
        if (tgt != base + p.calleeRva)
        {
            Log("  patch %-12s REFUSED: call resolves to rva 0x%llX, expected 0x%llX (another mod?)\n",
                p.name, (unsigned long long)(tgt - base), (unsigned long long)p.calleeRva);
            return false;
        }
        p.origRel = rel;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }

    if (!g_stubPage) g_stubPage = AllocNear(base);
    if (!g_stubPage || g_stubUsed + 32 > 0x1000) { Log("  patch %-12s REFUSED: no stub memory\n", p.name); return false; }
    uint8_t* stub = g_stubPage + g_stubUsed;
    g_stubUsed += 32;
    const uint64_t fn = reinterpret_cast<uint64_t>(p.replacement);
    const uint8_t pre[6] = { 0x48, 0x89, 0xFA,      // mov rdx, rdi
                             0x49, 0x89, 0xF0 };    // mov r8, rsi
    std::memcpy(stub, pre, 6);
    stub[6] = 0x48; stub[7] = 0xB8; std::memcpy(stub + 8, &fn, 8);    // mov rax, imm64
    stub[16] = 0xFF; stub[17] = 0xE0;                                  // jmp rax
    FlushInstructionCache(GetCurrentProcess(), stub, 32);

    const intptr_t d = reinterpret_cast<intptr_t>(stub) - (reinterpret_cast<intptr_t>(site) + 5);
    if (d < INT32_MIN || d > INT32_MAX) { Log("  patch %-12s REFUSED: stub out of rel32 range\n", p.name); return false; }
    DWORD old = 0;
    if (!VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &old)) return false;
    InterlockedExchange(reinterpret_cast<volatile LONG*>(site + 1), static_cast<int32_t>(d));
    VirtualProtect(site, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), site, 5);
    p.applied = true;
    Log("  patch %-12s ok: call at rva 0x%llX -> stub %p -> %p (+rdi, rsi)\n", p.name,
        (unsigned long long)p.siteRva, (void*)stub, p.replacement);
    return true;
}

// ---------------------------------------------------------------------------------------------
//  One-byte code / data patches. A single byte store is atomic, so these can be switched on and
//  off while the game runs (a thread can never fetch half an instruction). Each one is checked
//  against its original byte AND a few bytes of context first; nothing is written otherwise.
// ---------------------------------------------------------------------------------------------
struct BytePatch
{
    const char* name;
    uintptr_t   rva;
    uint8_t     orig, repl;
    uintptr_t   ctxRva;          // context bytes that must match (the instruction around the byte)
    uint8_t     ctx[8];
    int         ctxLen;
    bool        ok;              // verified at startup
    bool        on;
};

static bool VerifyBytePatch(uintptr_t base, BytePatch& p)
{
    __try
    {
        const uint8_t* b = reinterpret_cast<const uint8_t*>(base + p.ctxRva);
        p.ok = std::memcmp(b, p.ctx, p.ctxLen) == 0 &&
               *reinterpret_cast<const uint8_t*>(base + p.rva) == p.orig;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { p.ok = false; }
    if (!p.ok) Log("  patch %-14s REFUSED: bytes at rva 0x%llX are not the expected ones\n", p.name, (unsigned long long)p.rva);
    return p.ok;
}

static void SetBytePatch(uintptr_t base, BytePatch& p, bool on)
{
    if (!p.ok || p.on == on) return;
    uint8_t* site = reinterpret_cast<uint8_t*>(base + p.rva);
    DWORD old = 0;
    if (!VirtualProtect(site, 1, PAGE_EXECUTE_READWRITE, &old)) return;
    *reinterpret_cast<volatile uint8_t*>(site) = on ? p.repl : p.orig;
    VirtualProtect(site, 1, old, &old);
    FlushInstructionCache(GetCurrentProcess(), site, 1);
    p.on = on;
}
