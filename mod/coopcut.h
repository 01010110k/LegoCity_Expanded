// =============================================================================================
//  coopcut.h - co-op: a cut-scene in one player's view freezes only that player (Notes\41).
//
//  The problem (2026-11-13): while the arrival animation of a helicopter, the train or a called-in
//  car played for one player, the other player froze too - no steering, brake or accelerator
//  until the cut ended.
//
//  Cause [P]: one global dword, 0x141C9DBE8, is the cut-camera gate. The split-screen manager
//  clears it every frame (0x1407BD65D) and 0x140991E00 sets it to 1-4 while ANY camera - the main
//  one or either split view's - is in a minicut (mode 1), a door transition (mode 3) or blending
//  out of one. 82 places read it, and three of them are the partner's controls:
//    * cLCPlayerOnFoot::Update      0x1400F5C7D  (owner in rbx; character = [owner+0x290] - 0x88)
//    * the player's vehicle input   0x1406CA773  (0x1406CA750 writes the source control block
//                                                 [veh+0x360]; the driver character in rbx)
//    * the heli pilot (HoveringAircraft / FireHelicopter user data slot 21) 0x1401A7702 (pilot in rdx)
//  The game already knows which player a split cut belongs to: the scripts call
//  SetMiniCutCanSplit / SetTriggeredByPlayer and the engine keeps the target player at
//  [*0x141CA1D00 + 0x2209] (0xFF = all players), matched against a character's +0x3E3.
//
//  Each site is `cmp dword [gate], 0` (7 bytes) followed by a jne that skips the controls. It's
//  rewritten as `call stub ; test al, al` (5 + 2 bytes) - the jne stays. The stub saves the
//  argument registers and xmm0-3 and calls Frozen(character): while a minicut plays for ONE
//  player, only that player is frozen; a cut for everyone, a door transition, a blend-out, or
//  [coop] split_cuts = 0 / lcudrive unhooked gives the stock answer (gate != 0).
//  The patch is installed once at startup and never taken out; the switch is read every call.
// =============================================================================================
#pragma once

namespace cc {

static const uintptr_t RVA_GATE      = 0x1C9DBE8;   // dword, the cut-camera gate
static const uintptr_t RVA_MC_CAMERA = 0x1CA1CA0;   // the minicut camera (null: no minicut playing)
static const uintptr_t RVA_MC        = 0x1CA1D00;   // the minicut manager; +0x2209 int8 target player
static const uintptr_t MC_TARGET     = 0x2209;
static const uintptr_t CH_PLAYER     = 0x3E3;       // int8, the character's player index

enum Arg { kRbxOwner, kRbxChar, kRdxChar };          // what the site holds, and where

struct Site
{
    const char* name;
    uintptr_t   rva;           // the cmp
    uint8_t     orig[13];      // cmp dword [rip+gate], 0 ; then the first 6 bytes after it
    Arg         arg;
    bool        ok;
    uint32_t    held;          // calls answered "not frozen" while the gate was up (a count, for the log)
};

static Site g_sites[] = {
    { "onfoot",  0xF5C7D,  { 0x83, 0x3D, 0x64, 0x7F, 0xBA, 0x01, 0x00, 0x0F, 0x85, 0xA8, 0x00, 0x00, 0x00 }, kRbxOwner, false, 0 },
    { "drive",   0x6CA773, { 0x83, 0x3D, 0x6E, 0x34, 0x5D, 0x01, 0x00, 0x0F, 0x85, 0xA3, 0x04, 0x00, 0x00 }, kRbxChar,  false, 0 },
    { "heli",    0x1A7702, { 0x83, 0x3D, 0xDF, 0x64, 0xAF, 0x01, 0x00, 0x49, 0x8B, 0xE8, 0x48, 0x8B, 0xF2 }, kRdxChar,  false, 0 },
};

static volatile LONG g_on = 1;                       // [coop] split_cuts, set from the config
static volatile LONG g_lastTarget = -2;              // for the log: the cut we last reported

// The answer for one character: true = keep him frozen (the jne skips his controls).
static bool FrozenChar(uint8_t* ch, int gate)
{
    if (!g_on || g_unhooked) return true;
    if (!PtrOk(*reinterpret_cast<void* const*>(g_base + RVA_MC_CAMERA))) return true;   // a door / blend-out: stock
    uint8_t* mc = *reinterpret_cast<uint8_t* const*>(g_base + RVA_MC);
    if (!PtrOk(mc)) return true;
    const int tgt = *reinterpret_cast<const int8_t*>(mc + MC_TARGET);
    if (tgt != g_lastTarget)
    {
        g_lastTarget = tgt;
        if (tgt >= 0) Log(">>> coop: a cut-scene for player %d (gate %d) - the other player keeps control\n", tgt + 1, gate);
        else          Log(">>> coop: a cut-scene for everyone (gate %d) - both players wait, as in the stock game\n", gate);
    }
    if (tgt < 0 || !PtrOk(ch)) return true;                                             // a cut for everyone
    return *reinterpret_cast<const int8_t*>(ch + CH_PLAYER) == tgt;
}

static uint8_t Frozen(uintptr_t reg, int site)
{
    int gate = 0;
    __try
    {
        gate = *reinterpret_cast<const volatile int*>(g_base + RVA_GATE);
        if (gate == 0) { g_lastTarget = -2; return 0; }
        uint8_t* ch = reinterpret_cast<uint8_t*>(reg);
        if (g_sites[site].arg == kRbxOwner)
        {
            // the same walk cLCPlayerOnFoot::Update does right before (0x1400F5C4F..0x1400F5C6C)
            uint8_t* c = PtrOk(ch) ? *reinterpret_cast<uint8_t**>(ch + 0x290) : nullptr;
            ch = PtrOk(c) && ((*reinterpret_cast<const uint32_t*>(c + 0x4C) >> 12) & 1) ? c - 0x88 : nullptr;
        }
        const bool f = FrozenChar(ch, gate);
        if (!f) ++g_sites[site].held;
        return f ? 1 : 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return gate != 0 ? 1 : 0; }
}

static uint8_t __fastcall Frozen0(uintptr_t r) { return Frozen(r, 0); }
static uint8_t __fastcall Frozen1(uintptr_t r) { return Frozen(r, 1); }
static uint8_t __fastcall Frozen2(uintptr_t r) { return Frozen(r, 2); }

// The stub: everything the site's code may still read is put back (rcx rdx r8 r9, xmm0-3).
//   push rcx/rdx/r8/r9 ; sub rsp,0x68 ; movups [rsp+28..58], xmm0-3 ; mov rcx, <rbx|rdx>
//   mov rax, fn ; call rax ; movups xmm0-3 back ; add rsp,0x68 ; pop r9/r8/rdx/rcx ; ret
// Alignment: rsp is 16-aligned at every site (each makes plain calls); the call pushes 8, the
// four pushes 32, the 0x68 the rest - so rsp is 16-aligned again at `call rax`.
static int BuildStub(uint8_t* s, Arg arg, void* fn)
{
    int n = 0;
    auto put = [&](std::initializer_list<uint8_t> b) { for (uint8_t x : b) s[n++] = x; };
    put({ 0x51, 0x52, 0x41, 0x50, 0x41, 0x51 });
    put({ 0x48, 0x83, 0xEC, 0x68 });
    put({ 0x0F, 0x11, 0x44, 0x24, 0x28 }); put({ 0x0F, 0x11, 0x4C, 0x24, 0x38 });
    put({ 0x0F, 0x11, 0x54, 0x24, 0x48 }); put({ 0x0F, 0x11, 0x5C, 0x24, 0x58 });
    if (arg == kRdxChar) put({ 0x48, 0x89, 0xD1 });  // mov rcx, rdx
    else                 put({ 0x48, 0x89, 0xD9 });  // mov rcx, rbx
    const uint64_t f = reinterpret_cast<uint64_t>(fn);
    put({ 0x48, 0xB8 }); std::memcpy(s + n, &f, 8); n += 8;
    put({ 0xFF, 0xD0 });
    put({ 0x0F, 0x10, 0x44, 0x24, 0x28 }); put({ 0x0F, 0x10, 0x4C, 0x24, 0x38 });
    put({ 0x0F, 0x10, 0x54, 0x24, 0x48 }); put({ 0x0F, 0x10, 0x5C, 0x24, 0x58 });
    put({ 0x48, 0x83, 0xC4, 0x68 });
    put({ 0x41, 0x59, 0x41, 0x58, 0x5A, 0x59, 0xC3 });
    return n;
}

// Rewrites one site without a thread ever seeing half an instruction: first a 2-byte short jump
// over the 7 bytes (to the jne - for the moment whatever the flags say), then the tail, then the
// head as one 2-byte store. Each 2-byte store sits inside one 16-byte block at all three sites.
static bool Install(uintptr_t base, Site& st, void* fn)
{
    uint8_t* site = reinterpret_cast<uint8_t*>(base + st.rva);
    __try
    {
        if (std::memcmp(site, st.orig, sizeof(st.orig)) != 0)
        {
            Log("  patch coop-%-8s REFUSED: the bytes at rva 0x%llX are not the expected ones\n", st.name, (unsigned long long)st.rva);
            return false;
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    if (((st.rva & 15) == 15)) { Log("  patch coop-%-8s REFUSED: head straddles a fetch block\n", st.name); return false; }

    if (!g_stubPage) g_stubPage = AllocNear(base);
    if (!g_stubPage || g_stubUsed + 96 > 0x1000) { Log("  patch coop-%-8s REFUSED: no stub memory\n", st.name); return false; }
    uint8_t* stub = g_stubPage + g_stubUsed;
    g_stubUsed += 96;
    BuildStub(stub, st.arg, fn);
    FlushInstructionCache(GetCurrentProcess(), stub, 96);

    const intptr_t d = reinterpret_cast<intptr_t>(stub) - (reinterpret_cast<intptr_t>(site) + 5);
    if (d < INT32_MIN || d > INT32_MAX) { Log("  patch coop-%-8s REFUSED: stub out of rel32 range\n", st.name); return false; }
    const int32_t rel = static_cast<int32_t>(d);
    uint8_t code[7] = { 0xE8, 0, 0, 0, 0, 0x84, 0xC0 };   // call stub ; test al, al
    std::memcpy(code + 1, &rel, 4);

    DWORD old = 0;
    if (!VirtualProtect(site, 7, PAGE_EXECUTE_READWRITE, &old)) return false;
    *reinterpret_cast<volatile uint16_t*>(site) = 0x05EB;                       // jmp short +5
    FlushInstructionCache(GetCurrentProcess(), site, 7);
    for (int i = 2; i < 7; ++i) reinterpret_cast<volatile uint8_t*>(site)[i] = code[i];
    FlushInstructionCache(GetCurrentProcess(), site, 7);
    uint16_t head; std::memcpy(&head, code, 2);
    *reinterpret_cast<volatile uint16_t*>(site) = head;
    VirtualProtect(site, 7, old, &old);
    FlushInstructionCache(GetCurrentProcess(), site, 7);
    st.ok = true;
    Log("  patch coop-%-8s ok: gate test at rva 0x%llX -> stub %p\n", st.name, (unsigned long long)st.rva, (void*)stub);
    return true;
}

static void Init(uintptr_t base, const Config& cfg)
{
    g_on = cfg.coop_split_cuts ? 1 : 0;
    void* fns[3] = { reinterpret_cast<void*>(&Frozen0), reinterpret_cast<void*>(&Frozen1), reinterpret_cast<void*>(&Frozen2) };
    int ok = 0;
    for (int i = 0; i < 3; ++i) ok += Install(base, g_sites[i], fns[i]) ? 1 : 0;
    Log("coop cut-scenes: %d of 3 control gates per player (%s)\n", ok, g_on ? "split_cuts = 1" : "split_cuts = 0: stock");
}

static void Apply(const Config& cfg) { g_on = cfg.coop_split_cuts ? 1 : 0; }

} // namespace cc
