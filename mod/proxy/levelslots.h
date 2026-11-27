// =============================================================================================
//  levelslots.h - the engine's level table: 0x800 -> 0x1000 slots (Notes 74).
//
//  Every level (each levels.txt entry at start, and up to four more per map cell that streams in) takes one
//  slot of a fixed table for as long as it exists. The stock game registers 1,493 levels and has ~555 slots
//  to spare; with the mod's new areas (1,904 entries) only ~144 are left, and with the table full a new level
//  gets no id at all - the game then crashes in whichever code first looks its objects up (two players, who
//  stream two neighbourhoods, reach that within a minute or two).
//
//  The table object and its code are rewritten for 0x1000 slots: the object grows from 0x4100 to 0x8100 bytes
//  (the fields behind the slot array move up by 0x4000), every id bound becomes 0xFFF / 0x1000, and the static
//  "twin" array (16 bytes per slot, in the exe's .data with live globals right behind it) moves to a new
//  zero-filled block. Ids are compared as 16-bit values, signed in places: 0x1000 is far below 0x8000.
//
//  This MUST run before any game code: the table is built during the game's start-up, long before
//  lcudrive.dll is loaded. dinput8.dll is a static import of the exe, so this proxy's DllMain runs before the
//  exe's entry point, with only the loader's thread alive - that is where Apply() is called.
//
//  All or nothing: the image is checked (time stamp, size, and the stock value of every field) before the
//  first write; any mismatch leaves the game untouched. A file named `lcu_stock_level_table.txt` beside the
//  proxy switches the whole thing off. lcudrive.dll reads the result from the patched code itself.
//  Only kernel32 calls (loader lock).
// =============================================================================================
#pragma once
#include "levelslots_table.h"

namespace lslots {

static int g_state = 0;   // 0 not tried, 1 applied, -1 switched off, -2 not this exe, -3 no memory near, -4 write failed

// A zero-filled block within rel32 reach of every site: `bytes` usable, with one zeroed page in front (a level
// without an id indexes the twin at [-1]: the stock game hits the table lock there; here it reads zeros).
static unsigned char* AllocTwin(unsigned char* base, SIZE_T bytes)
{
    SYSTEM_INFO si; GetSystemInfo(&si);
    const ULONG_PTR gran = si.dwAllocationGranularity;
    const ULONG_PTR b = reinterpret_cast<ULONG_PTR>(base) & ~(gran - 1);
    const SIZE_T total = bytes + 0x1000;
    for (ULONG_PTR off = gran; off < 0x70000000ull; off += gran)
    {
        void* p = VirtualAlloc(reinterpret_cast<void*>(b + kImageSize + off), total, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        if (!p && b > off) p = VirtualAlloc(reinterpret_cast<void*>(b - off), total, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        if (p) return static_cast<unsigned char*>(p) + 0x1000;
    }
    return nullptr;
}

static bool SwitchedOff(HINSTANCE self)
{
    wchar_t p[MAX_PATH];
    DWORD n = GetModuleFileNameW(self, p, MAX_PATH);
    if (n == 0 || n >= MAX_PATH - 32) return false;
    int cut = -1;
    for (DWORD i = 0; i < n; ++i) if (p[i] == L'\\' || p[i] == L'/') cut = static_cast<int>(i);
    if (cut < 0) return false;
    p[cut + 1] = 0;
    lstrcatW(p, L"lcu_stock_level_table.txt");
    return GetFileAttributesW(p) != INVALID_FILE_ATTRIBUTES;
}

static unsigned char* g_twinNew = nullptr;   // the new twin (for the test harness and the curious)

static int ApplyTo(unsigned char* base)
{
    if (!base) return g_state = -2;
    const IMAGE_DOS_HEADER* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return g_state = -2;
    const IMAGE_NT_HEADERS* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.TimeDateStamp != kStamp ||
        nt->OptionalHeader.SizeOfImage != kImageSize) return g_state = -2;

    const int nf = static_cast<int>(sizeof(kFields) / sizeof(kFields[0]));
    const int nt2 = static_cast<int>(sizeof(kTwinRefs) / sizeof(kTwinRefs[0]));
    // 1. every field holds its stock value, every twin reference is the stock lea, and the table is not built yet
    for (int i = 0; i < nf; ++i)
        if (*reinterpret_cast<const unsigned*>(base + kFields[i].rva) != kFields[i].old) return g_state = -2;
    for (int i = 0; i < nt2; ++i)
    {
        const unsigned char* s = base + kTwinRefs[i].site;
        if (s[1] != 0x8D || (s[2] & 0xC7) != 0x05 || *reinterpret_cast<const unsigned*>(s + 3) != kTwinRefs[i].rel) return g_state = -2;
    }
    if (*reinterpret_cast<void* const*>(base + kRvaTable) != nullptr) return g_state = -2;   // too late: leave it stock

    // 2. the new twin, and its distances
    unsigned char* twin = AllocTwin(base, 0x1000 * 16);
    if (!twin) return g_state = -3;
    for (int i = 0; i < nt2; ++i)
    {
        const __int64 d = (twin + kTwinRefs[i].add) - (base + kTwinRefs[i].site + 7);
        if (d < -0x7FFFFFF0ll || d > 0x7FFFFFF0ll) { VirtualFree(twin - 0x1000, 0, MEM_RELEASE); return g_state = -3; }
    }

    // 3. write (single-threaded here; each field is one aligned-or-not 4-byte store)
    bool ok = true;
    for (int i = 0; i < nf && ok; ++i)
    {
        unsigned char* f = base + kFields[i].rva; DWORD old = 0;
        if (!VirtualProtect(f, 4, PAGE_EXECUTE_READWRITE, &old)) { ok = false; break; }
        *reinterpret_cast<unsigned*>(f) = kFields[i].neu;
        VirtualProtect(f, 4, old, &old);
    }
    for (int i = 0; i < nt2 && ok; ++i)
    {
        unsigned char* f = base + kTwinRefs[i].site + 3; DWORD old = 0;
        if (!VirtualProtect(f, 4, PAGE_EXECUTE_READWRITE, &old)) { ok = false; break; }
        *reinterpret_cast<int*>(f) = static_cast<int>((twin + kTwinRefs[i].add) - (base + kTwinRefs[i].site + 7));
        VirtualProtect(f, 4, old, &old);
    }
    if (!ok)
    {
        // put every field back (a half-applied table is the one state that must not run)
        for (int i = 0; i < nf; ++i)
        {
            unsigned char* f = base + kFields[i].rva; DWORD old = 0;
            if (VirtualProtect(f, 4, PAGE_EXECUTE_READWRITE, &old)) { *reinterpret_cast<unsigned*>(f) = kFields[i].old; VirtualProtect(f, 4, old, &old); }
        }
        for (int i = 0; i < nt2; ++i)
        {
            unsigned char* f = base + kTwinRefs[i].site + 3; DWORD old = 0;
            if (VirtualProtect(f, 4, PAGE_EXECUTE_READWRITE, &old)) { *reinterpret_cast<unsigned*>(f) = kTwinRefs[i].rel; VirtualProtect(f, 4, old, &old); }
        }
        return g_state = -4;
    }
    FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
    g_twinNew = twin;
    return g_state = 1;
}

static int Apply(HINSTANCE self)
{
    if (g_state) return g_state;
    if (SwitchedOff(self)) return g_state = -1;
    return ApplyTo(reinterpret_cast<unsigned char*>(GetModuleHandleW(nullptr)));
}

} // namespace lslots
