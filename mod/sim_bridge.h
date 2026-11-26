// =========================================================================================
//  sim_bridge.h  -  The vehsim -> shader channel: publish gameplay floats into 3Dmigoto's
//                   heap-resident IniParams staging array, which the overlay re-uploads
//                   to GPU t120 every frame. Sentinel-scan discovery, verified writes,
//                   and a no-3Dmigoto no-op path.
//
//  WHAT THIS IS
//  ------------
//  The one module in the overhaul that is NOT engine-blind - it deliberately touches
//  process memory that belongs to a third DLL. It is still caller-owned-state,
//  allocation-free, and single-threaded by contract, and it follows the house rule that
//  matters most here: every pointer it ever dereferences was found by ITS OWN scan,
//  re-verified before every write burst, and read/written only inside SEH. A stale
//  pointer is never written; a torn frame is a one-frame glitch (per-float aligned
//  stores are atomic on x86); a missing 3Dmigoto is a permanently-inert module.
//
//  WHY THIS WORKS (note 12 section 4, receipts included there)
//  -----------------------------------------------------------
//  1. Every patched shader already reads a CPU-writable channel: 3Dmigoto injects
//     Texture1D<float4> IniParams : register(t120) into every override, and the
//     day/night clock is already delivered through it ([Present] x = time + $tod).
//  2. The staging store is plain heap memory: 3Dmigoto 1.3.16 keeps
//     std::vector<XMFLOAT4> iniParams inside its Globals, and the shipped d3d11.dll
//     carries the matching machinery strings ("Resizing IniParams from %Ii to %d").
//  3. The WHOLE array is re-uploaded each frame the dirty flag is set, and our
//     [Present] clock assignment sets it every frame - foreign floats written between
//     frames ride the same upload. No D3D calls from vehsim, no 3Dmigoto rebuild.
//
//  THE SLOT CONTRACT (plan 16 section 0.2 - THE canonical table, do not re-derive)
//  -------------------------------------------------------------------------------
//     index.comp  d3dx name  content                            writer
//     0.x         x          day/night clock time + $tod        d3dx [Present] - NEVER TOUCH
//     0.y         y          player speed 0..1 (motion blur)    THIS MODULE
//     0.z         z          sentinel 123456.78125              d3dx [Constants] - NEVER TOUCH
//     0.w         w          sentinel 987654.3125               d3dx [Constants] - NEVER TOUCH
//     1.xyz/1.w   x1..w1     headlight anchor pos / intensity   THIS MODULE
//     2.xyz/2.w   x2..w2     vehicle forward / lamp half-space  THIS MODULE
//                            (2.w: metres from anchor to each lamp along +/-right;
//                             NEGATIVE = "use the shader's #define default", 0 = a real
//                             measurement - single-track centreline, both cones on the
//                             anchor, ONE lamp - and measured pairs are clamped
//                             [0.2, 3.0] at the write. Claimed 2026-08-18
//                             after verifying NO writer existed: BridgeWriteSlots
//                             skipped f[11], d3dx.ini touches only 0.x/0.z/0.w,
//                             4.x and 7.x, and night_lights.hlsl read only 2.xyz.)
//     3.xyz/3.w   x3..w3     vehicle right / beam half-angle    THIS MODULE
//     4.x         x4         $rain dial                         d3dx [Present] - NEVER TOUCH
//     4.y / 4.z / 4.w        WEATHER: rain 0..1 / overcast 0..1 / mist 0..1     lcudrive (2026-11-18),
//                            BridgePublishWeather only; read by the LCUR_ block of the 13 main-filter
//                            scene passes (Tools\Patch-RainOverlay.ps1 -Bridge). All 0 = those shaders
//                            return their input untouched. (claimed after verifying NO writer existed:
//                            d3dx.ini [Present] writes only x and x4, no ShaderFixes file read 4.y-4.w)
//     5.xyzw      x5..w5     wanted HUD: level / heat / flash / alpha   lcudrive (2026-10-24),
//                            BridgePublishWanted only; read by ShaderFixes\wanted_hud.hlsl
//                            (player 1's meter since 2026-11-14)
//     6.xyzw      x6..w6     PLAYER 2's wanted HUD, the same four       lcudrive (2026-11-14),
//                            (6.z: + 8 while player 2 is in the game -  BridgePublishWanted2 only;
//                            the layout flag)                           wanted_hud.hlsl, wanted_banner.hlsl
//                            (claimed after verifying NO writer existed: d3dx.ini sets only
//                             x7 = 0 up there, no ShaderFixes file read slot 6)
//     7.x                    x7=0 size forcer                   NOT OURS
//     7.y                    WEATHER: gust 0..1 (the storm's slant and speed)   lcudrive (2026-11-18),
//                            BridgePublishWeather only (d3dx.ini sets only x7, at launch / F10)
//     7.z / 7.w              BUSTED FADE: player 1's / player 2's black, as a deadline on the 0.x clock
//                            lcudrive (2026-11-18, busted1), BridgePublishBustFade only; read by
//                            ShaderFixes/busted_fade.hlsl (0 = nothing drawn)
//     8.xyzw / 9.x           [backrooms1] BACKROOMS: fades P1 / P2, flags, seconds, players - lcudrive (2026-11-19),
//                            BridgePublishBackrooms only; read by ShaderFixes/backrooms_fx.hlsl. 9.w = the size
//     9.y / 9.z              [br4] BACKROOMS goal + per-player HUD bits (backrooms_model.h ShaderOut.goal / .hud) -
//                            the same writer, the same sentinel; 0 = nothing drawn
//                            sentinel 246813.75 from ShaderFixes/backrooms_fx.ini (nothing written without it).
//                            (10.xyzw / 11.x = gfx1 lens block, 11.w its sentinel - gfx.h writes those itself.)
//  The write API below has no index parameter on purpose: it writes a fixed field list
//  and structurally cannot reach 0.x, 0.z, 0.w or anything at index 4+. The
//  two-clocks desync taught this project what happens when two writers share a channel
//  by convention instead of by construction - so here it is by construction.
//
//  THE SELF-MATCH TRAP (a real one, found on paper before it bit)
//  --------------------------------------------------------------
//  The scan looks for two exact odd floats sitting at components .z/.w of a 16-aligned
//  float4. If this module stored those sentinel bit patterns anywhere in ITS OWN memory
//  - a params struct on the heap, two locals spilled adjacently on the scanning thread's
//  stack (stacks are committed private RW and get scanned!) - the scan could find
//  itself and lock onto a pointer that never reaches the GPU. Three defences, layered:
//  (a) params carry the sentinels as DOUBLES (different byte pattern than the float);
//  (b) the comparison constants are held XOR-masked and the mask is applied to the
//      CANDIDATE data in a register, so the raw float bit patterns never rest in our
//      memory at all;
//  (c) a candidate only ever LOCKS after a second look >=100 ms later shows the same
//      sentinels AND an ADVANCED clock in [0].x - stack spills and stale copies do not
//      tick like the 3Dmigoto clock does.
//
//  ALIGNMENT ASSUMPTION, STATED
//  ----------------------------
//  The scan steps 16 bytes, assuming vec.data() is 16-aligned. XMFLOAT4 itself is only
//  4-aligned, but x64 operator new returns 16-aligned blocks, so element 0 lands on a
//  16 boundary in practice. If a build ever violates that, the scan parks and the
//  documented fallback applies (note 12: rebuild 3Dmigoto with a shared-memory reader,
//  ~30 lines, we control the binary) - the failure mode is "no bridge", never a crash.
//
//  THE NO-3DMIGOTO NO-OP PATH
//  --------------------------
//  Freshly reset state is IDLE and BridgeStep does nothing there until the caller asks
//  for a scan - a build with bridge_enable = 0 simply never asks, and the module costs
//  one branch per frame. If a scan IS requested and the sentinels do not exist (3Dmigoto
//  absent, Tier-1 d3dx.ini additions not applied yet), the scan fails, retries on a
//  cooldown, and after maxAutoScans consecutive failures PARKS until the next explicit
//  request. Parked and idle bridges publish nothing and scan nothing.
//
//  COMPILES STANDALONE: MSVC, C++17, /W4 /WX clean. <windows.h> + <cstdint> only - and
//  no sim_config.h, because this module reads no Tune at all: its one gate
//  ([system] bridge_enable, default 0) is the CALLER's to read, exactly like
//  enable_assists. SEH functions contain POD only (project constraint 6: no object
//  unwinding in a __try function).
//
// =========================================================================================
//
//  INTEGRATION APPENDIX (for W3-A - terse; see plan 16 section 4 step 14)
//  ----------------------------------------------------------------------
//  CLASS GATE   None. Unlike sim_heli/sim_wing this module is NOT gated on
//               which == 4 || 5: slot 0.y feeds the already-applied motion-blur patch
//               for CARS, and slots 1-3 carry the focus vehicle's lamp frame whatever
//               it is. Publish for the resolved player vehicle, any class.
//  STATE        ONE BridgeState in vehsim.cpp static storage (beside g_cfg). NOT per
//               vehicle, NOT in VehSlot - the 3Dmigoto array is process-global.
//               BridgeReset once at attach.
//  CALLS        BridgeStep(params, state, GetTickCount()) once per frame on the game
//               thread (end of the focus-vehicle hook is fine). BridgeRequestScan from
//               the hotkey thread is flag-only and safe; call it when the player flips
//               bridge_enable on, and from the F10-adjacent reload handling (F10 can
//               reallocate the vector - the freeze detector also catches that, ~2 s
//               later). BridgePublish(params, state, slots) after the player-vehicle
//               resolve, only when [system] bridge_enable == 1.
//  SLOT SOURCES speed01 = |linVel| / max_speed_ms clamped 0..1 (snapshot values);
//               headlight anchor = front-axle midpoint (front wheels' rest offsets
//               through the body basis, lifted half a wheel radius) when >= 2 finite
//               front wheels exist, else the old wheelbase*0.55 nudge from OFF_XFORM
//               0x60 row3; lampHalfSpacing = measured front half-track (0 = single-
//               track centreline / one lamp, -1 = fallback, shader default);
//               fwd = -row2 normalised; right = row0 normalised;
//               intensity/beam from night phase logic (Wave-4 territory -
//               publish 0 intensity until then, which the shaders read as "lamps off").
//  TUNE KEY     [system] bridge_enable, int, default 0 - register in the SysTune table.
//               Fully inert until the player applies the Tier-1 d3dx.ini sentinels AND
//               flips this key (plan 16 morning step 6 proves the pipe via motion blur).
//  HOTKEYS      None claimed. The plan 16 section 0.3 ledger is the authority; a manual
//               rescan chord, if ever wanted, goes through the ledger first (the F-row
//               and numpad belong to the game/3Dmigoto).
//  TWO CLOCKS   The bridge exposes BridgeClock() (last verified [0].x). If vehsim ever
//               needs day/night phase, read THAT - never a second clock. F7 skips stay
//               coherent because there is exactly one time value in the whole system.
// =========================================================================================

#pragma once
#ifndef VEHSIM_SIM_BRIDGE_H
#define VEHSIM_SIM_BRIDGE_H

#include <windows.h>
#include <cstdint>

namespace vsim {

// =========================================================================================
//  SECTION 0 - SMALL HELPERS
// =========================================================================================

// Finite by bit pattern, NOT <cmath> - /fp:fast can optimise isfinite away, and this
// module's floats travel to a GPU where a smuggled NaN becomes a black screen tint.
inline bool BridgeFinBits(uint32_t bits)
{
    return ((bits >> 23) & 0xFFu) != 0xFFu;
}

inline uint32_t BridgeBitsOfFloat(float f)
{
    union { float f; uint32_t u; } c; c.f = f;
    return c.u;
}

inline float BridgeFloatOfBits(uint32_t u)
{
    union { float f; uint32_t u; } c; c.u = u;
    return c.f;
}

inline bool BridgeFinf(float v)
{
    return BridgeFinBits(BridgeBitsOfFloat(v));
}

inline float BridgeClampf(float v, float lo, float hi)
{
    if (!(v == v)) return lo;
    return v < lo ? lo : (v > hi ? hi : v);
}

// Tick arithmetic that survives the 49.7-day GetTickCount wrap: subtract first, compare
// signed. Every time comparison in this module goes through these two.
inline bool BridgeElapsed(uint32_t nowMs, uint32_t thenMs, uint32_t intervalMs)
{
    return (int32_t)(nowMs - thenMs) >= (int32_t)intervalMs;
}

// The XOR mask for defence (b) in the file-header trap note. The value is arbitrary;
// what matters is that (sentinelBits ^ kBridgeMask) is what rests in memory, never
// sentinelBits itself.
enum : uint32_t { kBridgeMask = 0x5AA53CC3u };

// =========================================================================================
//  SECTION 1 - PARAMETERS
//
//  Defaults ARE the deployed contract (plan 16 section 0.2 + note 12 section 4); they are
//  parameters at all only so the harness can shrink the timings and so a future d3dx.ini
//  revision can move the sentinels without recompiling. Sentinels live here as DOUBLES -
//  defence (a) against the self-match trap. Both chosen values are exactly representable
//  in float (123456.78125 = 3950617 * 2^-5, 987654.3125 = 15802469 * 2^-4; both
//  mantissas < 2^24), so the d3dx.ini parse lands bit-exact and bit-exact comparison is
//  legitimate.
// =========================================================================================

struct BridgeParams
{
    double   sentinelZ          = 123456.78125;   // d3dx [Constants] z  (index 0 comp 2)
    double   sentinelW          = 987654.3125;    // d3dx [Constants] w  (index 0 comp 3)

    // [0].x plausibility band: 3Dmigoto's `time` is seconds since launch (+ $tod).
    // 1e7 s is ~115 days of uptime - anything past that is not our clock.
    float    clockMin           = 0.0f;
    float    clockMax           = 1.0e7f;

    // Candidate confirm: second look this much later must show the sentinels intact and
    // [0].x advanced by at least minAdvance. At 60 fps the real clock advances ~0.1 s
    // in 100 ms; 1e-4 tolerates a paused-but-presenting game menu.
    uint32_t confirmDelayMs     = 100;
    float    confirmMinAdvance  = 1.0e-4f;

    // LOCKED-state freeze detector: [0].x not advancing for this long means the vector
    // was reallocated (F10 "Resizing IniParams...") or the overlay died. Rescan.
    uint32_t freezeMs           = 2000;

    // Failed-full-scan retry pacing, and how many consecutive failures park the module.
    uint32_t rescanCooldownMs   = 3000;
    uint32_t maxAutoScans       = 3;

    // Scan work budget per BridgeStep call, to bound the game-thread hitch: bytes of
    // eligible memory examined, and VirtualQuery calls issued. 8 MB of linear scanning
    // is ~1-2 ms on anything that runs this game; a multi-GB commit therefore spreads
    // over a few hundred frames instead of freezing one.
    uint32_t scanBudgetBytes    = 8u * 1024u * 1024u;
    uint32_t maxQueriesPerStep  = 2048;
};

// =========================================================================================
//  SECTION 2 - STATE
// =========================================================================================

enum BridgePhase : int32_t
{
    kBridgeIdle      = 0,   // never asked to scan - the bridge_enable=0 resting state
    kBridgeScanning  = 1,   // budgeted VirtualQuery walk in progress
    kBridgeCandidate = 2,   // found the pattern once; waiting out the confirm delay
    kBridgeLocked    = 3,   // pointer verified; publishing allowed
    kBridgeCooldown  = 4,   // full scan found nothing; waiting to retry
    kBridgeParked    = 5    // gave up until the next explicit BridgeRequestScan
};

struct BridgeState
{
    int32_t  phase;

    // Set from any thread (hotkey), consumed on the Step thread. Flag-only cross-thread
    // traffic, same pattern as ConfigSystem::RequestReload.
    volatile uint32_t requestScan;

    uintptr_t base;              // &vec[0] as a byte address; 0 = not locked
    uintptr_t cursor;            // next address the walk will VirtualQuery

    uintptr_t candAddr;          // candidate float4[0] address
    float     candClock;         // [0].x at find time (must advance to confirm)
    uint32_t  candFoundMs;

    float     lastClockVal;      // freeze detection while locked
    uint32_t  lastClockChangeMs;
    uint32_t  nextRetryMs;       // cooldown gate

    uint32_t  failedScans;       // consecutive full-scan failures

    // Counters for the HUD / log lines - cheap truth for diagnostics.
    uint64_t  scannedBytes;
    uint32_t  locksAcquired;
    uint32_t  verifyFailures;
    uint64_t  publishes;
};

inline void BridgeReset(BridgeState& s)
{
    s.phase             = kBridgeIdle;
    s.requestScan       = 0;
    s.base              = 0;
    s.cursor            = 0;
    s.candAddr          = 0;
    s.candClock         = 0.0f;
    s.candFoundMs       = 0;
    s.lastClockVal      = 0.0f;
    s.lastClockChangeMs = 0;
    s.nextRetryMs       = 0;
    s.failedScans       = 0;
    s.scannedBytes      = 0;
    s.locksAcquired     = 0;
    s.verifyFailures    = 0;
    s.publishes         = 0;
}

// Callable from any thread. Re-arms a parked bridge, restarts a locked one (the player
// pressed the chord because something looked wrong - honour it).
inline void BridgeRequestScan(BridgeState& s)
{
    s.requestScan = 1;
}

inline bool BridgeLockedQ(const BridgeState& s) { return s.phase == kBridgeLocked; }

// The last verified [0].x - THE clock, for any vehsim-side day/night logic. See the
// TWO CLOCKS line in the appendix: this exists so nobody ever grows a second one.
inline float BridgeClock(const BridgeState& s) { return s.lastClockVal; }

// =========================================================================================
//  SECTION 3 - THE PUBLISH PAYLOAD
//
//  Field list = the slot contract, nothing else. No index parameter anywhere.
// =========================================================================================

struct BridgeSlots
{
    float speed01          = 0.0f;                    // -> 0.y   (motion blur)
    float headlightPos[3]  = { 0.0f, 0.0f, 0.0f };    // -> 1.xyz (world)
    float lampIntensity    = 0.0f;                    // -> 1.w   (0 = lamps off)
    float fwd[3]           = { 0.0f, 0.0f, -1.0f };   // -> 2.xyz (unit, world)
    float lampHalfSpacing  = -1.0f;                   // -> 2.w   (<0 = shader default;
                                                      //          0 = centreline/one lamp;
                                                      //          >0 clamped [0.2, 3.0])
    float right[3]         = { 1.0f, 0.0f, 0.0f };    // -> 3.xyz (unit, world)
    float beamHalfAngleRad = 0.5f;                    // -> 3.w
};

// =========================================================================================
//  SECTION 4 - SEH LEAF FUNCTIONS
//
//  Every dereference of scanned memory lives in one of these. POD locals only - project
//  constraint 6, SEH cannot coexist with object unwinding. Each returns a tri-state:
//  1 = yes, 0 = no, -1 = the page faulted (decommitted between query and touch - the
//  scan treats a fault as "not here", the verify treats it as "pointer is dead").
// =========================================================================================

// Linear pattern scan over [from, to). 16-byte steps. On hit, *hitOut = the float4[0]
// address and *clockOut = the [0].x value seen there.
inline int BridgeScanRange(uintptr_t from, uintptr_t to,
                           uint32_t zMasked, uint32_t wMasked,
                           float clockMin, float clockMax,
                           uintptr_t* hitOut, float* clockOut)
{
    __try
    {
        uintptr_t a = (from + 15u) & ~(uintptr_t)15u;
        for (; a + 16u <= to; a += 16u)
        {
            const uint32_t* q = (const uint32_t*)a;
            // Defence (b): the mask is applied to the CANDIDATE dwords in registers;
            // the raw sentinel bit patterns never rest in our memory.
            if (((q[2] ^ kBridgeMask) == zMasked) && ((q[3] ^ kBridgeMask) == wMasked))
            {
                const uint32_t cb = q[0];
                if (BridgeFinBits(cb))
                {
                    const float c = BridgeFloatOfBits(cb);
                    if (c >= clockMin && c <= clockMax)
                    {
                        *hitOut = a;
                        *clockOut = c;
                        return 1;
                    }
                }
            }
        }
        return 0;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return -1;
    }
}

// Re-check a known address: sentinels intact, clock readable. *clockOut valid on 1.
inline int BridgeVerifyAt(uintptr_t addr,
                          uint32_t zMasked, uint32_t wMasked, float* clockOut)
{
    __try
    {
        const uint32_t* q = (const uint32_t*)addr;
        if (((q[2] ^ kBridgeMask) != zMasked) || ((q[3] ^ kBridgeMask) != wMasked))
            return 0;
        const uint32_t cb = q[0];
        if (!BridgeFinBits(cb)) return 0;
        *clockOut = BridgeFloatOfBits(cb);
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return -1;
    }
}

// The one function that writes. Fixed field list; float indices into the array are
// spelled as constants so a reviewer can check them against the contract table by eye:
//   float index = float4Index * 4 + component.
// Writes are volatile 4-byte aligned stores - atomic per float on x86; a torn 16-float
// GROUP across the [Present] upload is the accepted one-frame glitch.
inline int BridgeWriteSlots(uintptr_t base, const float v[13])
{
    __try
    {
        volatile float* f = (volatile float*)base;
        f[ 1] = v[0];              // 0.y  speed01           (NEVER f[0], f[2], f[3])
        f[ 4] = v[1];              // 1.x  headlightPos
        f[ 5] = v[2];              // 1.y
        f[ 6] = v[3];              // 1.z
        f[ 7] = v[4];              // 1.w  lampIntensity
        f[ 8] = v[5];              // 2.x  fwd
        f[ 9] = v[6];              // 2.y
        f[10] = v[7];              // 2.z
        f[11] = v[8];              // 2.w  lampHalfSpacing   (claimed 2026-08-18, see table)
        f[12] = v[9];              // 3.x  right
        f[13] = v[10];             // 3.y
        f[14] = v[11];             // 3.z
        f[15] = v[12];             // 3.w  beamHalfAngleRad  (NEVER f[16]+ - 4.x is $rain)
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return -1;
    }
}

// The wanted-mode brick meter's slot (claimed 2026-10-24 by lcudrive; the array is 8 float4s
// because d3dx [Constants] sets x7): 5.x level 0..5, 5.y heat 0..6, 5.z evading flash 0/1,
// 5.w alpha 0..1 - read by ShaderFixes\wanted_hud.hlsl. Its own writer, like the one above:
// fixed indices, nothing else reachable.
inline int BridgeWriteWanted(uintptr_t base, const float v[4])
{
    __try
    {
        volatile float* f = (volatile float*)base;
        f[20] = v[0];              // 5.x  wanted level
        f[21] = v[1];              // 5.y  heat
        f[22] = v[2];              // 5.z  flash
        f[23] = v[3];              // 5.w  alpha
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return -1;
    }
}

// Player 2's brick meter (slot 6, claimed 2026-11-14 by lcudrive: co-op, a meter per player). The
// same four as slot 5, and 6.z carries the layout flag: + 8 while player 2 is in the game (the
// shaders then put each player's banner on their own side). Fixed indices, nothing else reachable
// (NEVER f[28]+ - 7.x is d3dx's size forcer).
inline int BridgeWriteWanted2(uintptr_t base, const float v[4])
{
    __try
    {
        volatile float* f = (volatile float*)base;
        f[24] = v[0];              // 6.x  player 2's wanted level
        f[25] = v[1];              // 6.y  heat
        f[26] = v[2];              // 6.z  flash (+ 8: player 2 is in the game)
        f[27] = v[3];              // 6.w  alpha
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return -1;
    }
}

// The weather's four floats (claimed 2026-11-18 by lcudrive, weather.h): 4.y rain, 4.z overcast,
// 4.w mist, 7.y gust. Fixed indices, nothing else reachable: NEVER f[16] (4.x is d3dx's $rain dial,
// rewritten by [Present] every frame) and NEVER f[28] (7.x is the size forcer).
inline int BridgeWriteWeather(uintptr_t base, const float v[4])
{
    __try
    {
        volatile float* f = (volatile float*)base;
        f[17] = v[0];              // 4.y  rain 0..1
        f[18] = v[1];              // 4.z  overcast 0..1
        f[19] = v[2];              // 4.w  mist 0..1
        f[29] = v[3];              // 7.y  gust 0..1
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return -1;
    }
}

// =========================================================================================
//  SECTION 5 - THE MAINTENANCE PUMP
//
//  Call once per frame from ONE thread. Owns the whole lifecycle: request handling,
//  budgeted scanning, candidate confirmation, lock verification, freeze detection,
//  cooldown and parking. Never blocks longer than the scan budget allows.
// =========================================================================================
inline void BridgeStep(const BridgeParams& p, BridgeState& s, uint32_t nowMs)
{
    // Masked comparison constants, rebuilt per call from the double-typed params. The
    // raw float bits exist only in registers here (defence (a)+(b) working together).
    const uint32_t zMasked = BridgeBitsOfFloat((float)p.sentinelZ) ^ kBridgeMask;
    const uint32_t wMasked = BridgeBitsOfFloat((float)p.sentinelW) ^ kBridgeMask;

    // ---- explicit rescan request beats everything --------------------------------------
    if (s.requestScan)
    {
        s.requestScan = 0;
        s.phase       = kBridgeScanning;
        s.base        = 0;
        s.cursor      = 0;
        s.candAddr    = 0;
        s.failedScans = 0;
    }

    switch (s.phase)
    {
    case kBridgeIdle:
    case kBridgeParked:
        // The no-op path: costs the switch and nothing else.
        return;

    case kBridgeCooldown:
        if (!BridgeElapsed(nowMs, s.nextRetryMs, 0)) return;
        s.phase  = kBridgeScanning;
        s.cursor = 0;
        return;

    case kBridgeLocked:
    {
        float clock = 0.0f;
        const int ok = BridgeVerifyAt(s.base, zMasked, wMasked, &clock);
        if (ok != 1)
        {
            // Sentinels gone or page dead: the vector moved (F10 resize) or the overlay
            // unloaded. Full rescan - a stale pointer must never be written again.
            s.verifyFailures++;
            s.phase  = kBridgeScanning;
            s.base   = 0;
            s.cursor = 0;
            return;
        }
        if (clock != s.lastClockVal)
        {
            s.lastClockVal      = clock;
            s.lastClockChangeMs = nowMs;
        }
        else if (BridgeElapsed(nowMs, s.lastClockChangeMs, p.freezeMs))
        {
            // Clock frozen ~2 s: 3Dmigoto stopped presenting into this buffer. Most
            // likely a reallocation left this block as a stale copy with intact
            // sentinels - which is exactly the case a sentinel check alone cannot
            // catch, and why the freeze detector exists.
            s.phase  = kBridgeScanning;
            s.base   = 0;
            s.cursor = 0;
        }
        return;
    }

    case kBridgeCandidate:
    {
        if (!BridgeElapsed(nowMs, s.candFoundMs, p.confirmDelayMs)) return;

        float clock = 0.0f;
        const int ok = BridgeVerifyAt(s.candAddr, zMasked, wMasked, &clock);
        if (ok == 1 && BridgeFinf(clock) &&
            (clock - s.candClock) >= p.confirmMinAdvance)
        {
            // Same sentinels, advanced clock: this is the live array. Defence (c) done.
            s.phase             = kBridgeLocked;
            s.base              = s.candAddr;
            s.lastClockVal      = clock;
            s.lastClockChangeMs = nowMs;
            s.failedScans       = 0;   // a lock forgives history: the park counter only
            s.locksAcquired++;         // measures CONSECUTIVE failures
        }
        else
        {
            // A static impostor (spill, stale copy) or a dead page. The cursor is
            // already past it; resume the walk where we left off.
            s.phase = kBridgeScanning;
        }
        s.candAddr = 0;
        return;
    }

    case kBridgeScanning:
    default:
        break;   // fall through to the walk below
    }

    // ---- the budgeted VirtualQuery walk ------------------------------------------------
    // Eligibility per note 12: committed, private, plain PAGE_READWRITE. That covers the
    // CRT heap the std::vector lives in and skips images, mapped files, guard pages and
    // anything executable. Free/reserved space costs one VirtualQuery each, not a read.
    {
        uintptr_t addr   = s.cursor ? s.cursor : 0x10000;   // below 64K is never valid
        uint32_t  budget  = p.scanBudgetBytes;
        uint32_t  queries = 0;
        bool      exhausted = false;

        while (budget > 0 && queries < p.maxQueriesPerStep)
        {
            MEMORY_BASIC_INFORMATION mbi;
            if (VirtualQuery((LPCVOID)addr, &mbi, sizeof(mbi)) != sizeof(mbi))
            {
                exhausted = true;   // walked off the end of user address space
                break;
            }
            ++queries;

            uintptr_t rbase = (uintptr_t)mbi.BaseAddress;
            uintptr_t rend  = rbase + (uintptr_t)mbi.RegionSize;
            if (rend <= addr)
            {
                // A region that does not advance the cursor would loop forever; step a
                // page and let VirtualQuery re-orient. Never observed - pure paranoia,
                // and cheaper than trusting an API at 3 a.m.
                rend = addr + 0x1000;
            }

            const bool eligible =
                (mbi.State == MEM_COMMIT) &&
                (mbi.Type == MEM_PRIVATE) &&
                (mbi.Protect == PAGE_READWRITE);

            if (eligible)
            {
                uintptr_t from = (addr > rbase) ? addr : rbase;
                uintptr_t to   = rend;
                if ((to - from) > (uintptr_t)budget) to = from + budget;

                uintptr_t hit = 0; float clock = 0.0f;
                const int r = BridgeScanRange(from, to, zMasked, wMasked,
                                              p.clockMin, p.clockMax, &hit, &clock);
                s.scannedBytes += (uint64_t)(to - from);

                if (r == 1)
                {
                    s.phase       = kBridgeCandidate;
                    s.candAddr    = hit;
                    s.candClock   = clock;
                    s.candFoundMs = nowMs;
                    s.cursor      = hit + 16u;   // resumes PAST it if confirm fails
                    return;
                }
                // r == -1 (page died mid-read): treat the rest of the region as not
                // containing our vector and move on - the walk must always advance.

                budget -= (uint32_t)(to - from);
                addr = to;
                if (addr < rend)
                {
                    s.cursor = addr;   // budget exhausted mid-region; resume here
                    return;
                }
            }
            else
            {
                addr = rend;
            }

            if (addr >= 0x00007FFFFFFF0000ull) { exhausted = true; break; }
        }

        if (exhausted)
        {
            // A complete pass found nothing: 3Dmigoto absent, or the Tier-1 sentinels
            // are not in d3dx.ini yet (they are staged, not applied, until sign-off).
            s.failedScans++;
            s.cursor = 0;
            if (s.failedScans >= p.maxAutoScans)
            {
                s.phase = kBridgeParked;   // inert until the next explicit request
            }
            else
            {
                s.phase      = kBridgeCooldown;
                s.nextRetryMs = nowMs + p.rescanCooldownMs;
            }
            return;
        }

        s.cursor = addr;   // out of budget/queries this frame; resume next frame
    }
}

// =========================================================================================
//  SECTION 6 - PUBLISH
//
//  Verify, gate, clamp, write - in that order, every burst. Returns true only if the
//  thirteen floats actually landed. Any failure unlocks and schedules a rescan; a bridge
//  that cannot prove its pointer is a bridge that does not write.
// =========================================================================================
inline bool BridgePublish(const BridgeParams& p, BridgeState& s, const BridgeSlots& v)
{
    if (s.phase != kBridgeLocked || s.base == 0) return false;

    // ---- finite gate on EVERY outgoing float, before anything is written ---------------
    // One NaN in t120 tints every patched shader on screen; publish-nothing beats
    // publish-half. (The GPU-side cost of a skipped frame is: the previous frame's
    // values persist. That is always safe - they were finite when they landed.)
    if (!BridgeFinf(v.speed01) ||
        !BridgeFinf(v.headlightPos[0]) || !BridgeFinf(v.headlightPos[1]) ||
        !BridgeFinf(v.headlightPos[2]) ||
        !BridgeFinf(v.lampIntensity) ||
        !BridgeFinf(v.fwd[0])   || !BridgeFinf(v.fwd[1])   || !BridgeFinf(v.fwd[2]) ||
        !BridgeFinf(v.lampHalfSpacing) ||
        !BridgeFinf(v.right[0]) || !BridgeFinf(v.right[1]) || !BridgeFinf(v.right[2]) ||
        !BridgeFinf(v.beamHalfAngleRad))
    {
        return false;
    }

    // ---- scan-verify before the write burst --------------------------------------------
    const uint32_t zMasked = BridgeBitsOfFloat((float)p.sentinelZ) ^ kBridgeMask;
    const uint32_t wMasked = BridgeBitsOfFloat((float)p.sentinelW) ^ kBridgeMask;
    float clock = 0.0f;
    if (BridgeVerifyAt(s.base, zMasked, wMasked, &clock) != 1)
    {
        s.verifyFailures++;
        s.phase  = kBridgeScanning;
        s.base   = 0;
        s.cursor = 0;
        return false;
    }

    // ---- clamp into the contract ranges ------------------------------------------------
    float w[13];
    w[0]  = BridgeClampf(v.speed01, 0.0f, 1.0f);
    w[1]  = BridgeClampf(v.headlightPos[0], -1.0e6f, 1.0e6f);
    w[2]  = BridgeClampf(v.headlightPos[1], -1.0e6f, 1.0e6f);
    w[3]  = BridgeClampf(v.headlightPos[2], -1.0e6f, 1.0e6f);
    w[4]  = BridgeClampf(v.lampIntensity, 0.0f, 100.0f);
    w[5]  = BridgeClampf(v.fwd[0],   -1.0f, 1.0f);
    w[6]  = BridgeClampf(v.fwd[1],   -1.0f, 1.0f);
    w[7]  = BridgeClampf(v.fwd[2],   -1.0f, 1.0f);
    // 2.w is a valve, not a plain range: NEGATIVE means "shader #define keeps authority"
    // and 0 means "single-track centreline - both cones on the anchor, one lamp". A
    // straight BridgeClampf would silently promote a bike's centreline (or every
    // fallback frame) to a 0.2 m two-lamp spacing, which is exactly the splayed-bike
    // bug this three-state encoding exists to prevent. Only a POSITIVE measurement is
    // clamped into the contract band; the two flag values pass through exact.
    w[8]  = (v.lampHalfSpacing > 0.0f)
          ? BridgeClampf(v.lampHalfSpacing, 0.2f, 3.0f)
          : ((v.lampHalfSpacing < 0.0f) ? -1.0f : 0.0f);
    w[9]  = BridgeClampf(v.right[0], -1.0f, 1.0f);
    w[10] = BridgeClampf(v.right[1], -1.0f, 1.0f);
    w[11] = BridgeClampf(v.right[2], -1.0f, 1.0f);
    w[12] = BridgeClampf(v.beamHalfAngleRad, 0.0f, 1.5707963f);

    if (BridgeWriteSlots(s.base, w) != 1)
    {
        // The page died between verify and write - the narrowest possible race, and
        // exactly why the writer is SEH-guarded too.
        s.verifyFailures++;
        s.phase  = kBridgeScanning;
        s.base   = 0;
        s.cursor = 0;
        return false;
    }

    s.publishes++;
    return true;
}

// The brick meter (slot 5), with the same verify-gate-clamp-write discipline.
inline bool BridgePublishWanted(const BridgeParams& p, BridgeState& s, float level, float heat, float flash, float alpha)
{
    if (s.phase != kBridgeLocked || s.base == 0) return false;
    if (!BridgeFinf(level) || !BridgeFinf(heat) || !BridgeFinf(flash) || !BridgeFinf(alpha)) return false;
    const uint32_t zMasked = BridgeBitsOfFloat((float)p.sentinelZ) ^ kBridgeMask;
    const uint32_t wMasked = BridgeBitsOfFloat((float)p.sentinelW) ^ kBridgeMask;
    float clock = 0.0f;
    if (BridgeVerifyAt(s.base, zMasked, wMasked, &clock) != 1) return false;   // BridgePublish rescans
    const float w[4] = { BridgeClampf(level, 0.0f, 5.0f), BridgeClampf(heat, 0.0f, 6.0f),
                         BridgeClampf(flash, 0.0f, 4.0f), BridgeClampf(alpha, 0.0f, 1.0f) };   // flash: 0 none, 1 evading, 2..3 arrest, 4 busted
    return BridgeWriteWanted(s.base, w) == 1;
}

// Player 2's brick meter (slot 6): the same discipline. `here`: player 2 is in the game - it rides in
// 6.z as + 8 (0..4 = no player 2: one meter, the banner centred; 8..12 = two players).
inline bool BridgePublishWanted2(const BridgeParams& p, BridgeState& s, bool here, float level, float heat, float flash, float alpha)
{
    if (s.phase != kBridgeLocked || s.base == 0) return false;
    if (!BridgeFinf(level) || !BridgeFinf(heat) || !BridgeFinf(flash) || !BridgeFinf(alpha)) return false;
    const uint32_t zMasked = BridgeBitsOfFloat((float)p.sentinelZ) ^ kBridgeMask;
    const uint32_t wMasked = BridgeBitsOfFloat((float)p.sentinelW) ^ kBridgeMask;
    float clock = 0.0f;
    if (BridgeVerifyAt(s.base, zMasked, wMasked, &clock) != 1) return false;   // BridgePublish rescans
    const float w[4] = { BridgeClampf(level, 0.0f, 5.0f), BridgeClampf(heat, 0.0f, 6.0f),
                         BridgeClampf(flash, 0.0f, 4.0f) + (here ? 8.0f : 0.0f), BridgeClampf(alpha, 0.0f, 1.0f) };
    return BridgeWriteWanted2(s.base, w) == 1;
}

// The weather (4.y / 4.z / 4.w / 7.y): the same verify-gate-clamp-write discipline.
inline bool BridgePublishWeather(const BridgeParams& p, BridgeState& s, float rain, float overcast, float mist, float gust)
{
    if (s.phase != kBridgeLocked || s.base == 0) return false;
    if (!BridgeFinf(rain) || !BridgeFinf(overcast) || !BridgeFinf(mist) || !BridgeFinf(gust)) return false;
    const uint32_t zMasked = BridgeBitsOfFloat((float)p.sentinelZ) ^ kBridgeMask;
    const uint32_t wMasked = BridgeBitsOfFloat((float)p.sentinelW) ^ kBridgeMask;
    float clock = 0.0f;
    if (BridgeVerifyAt(s.base, zMasked, wMasked, &clock) != 1) return false;   // BridgePublish rescans
    const float w[4] = { BridgeClampf(rain, 0.0f, 1.0f), BridgeClampf(overcast, 0.0f, 1.0f),
                         BridgeClampf(mist, 0.0f, 1.0f), BridgeClampf(gust, 0.0f, 1.0f) };
    return BridgeWriteWeather(s.base, w) == 1;
}

// [busted1 2026-11-18] The busted fade (7.z player 1, 7.w player 2; claimed after verifying NO writer
// existed: d3dx.ini sets only x7, BridgeWriteWeather writes only f[29], no ShaderFixes file read 7.z / 7.w).
// Read by ShaderFixes\busted_fade.hlsl. Fixed indices, nothing else reachable (NEVER f[28], NEVER f[29]).
inline int BridgeWriteBustFade(uintptr_t base, const float v[2])
{
    __try
    {
        volatile float* f = (volatile float*)base;
        f[30] = v[0];              // 7.z  player 1's black: a deadline on the 0.x clock
        f[31] = v[1];              // 7.w  player 2's
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return -1;
    }
}

// fade1 / fade2: 0..1 black for that player's view. What is written is a DEADLINE on the shader's own clock
// (busted_model.h Deadline): opacity = saturate((deadline - clock) / rampS), so a writer that stops leaves no
// black behind. The clock is the one this verify just read. Same verify-gate-clamp-write discipline.
inline bool BridgePublishBustFade(const BridgeParams& p, BridgeState& s, float fade1, float fade2, float rampS, float opaqueS)
{
    if (s.phase != kBridgeLocked || s.base == 0) return false;
    if (!BridgeFinf(fade1) || !BridgeFinf(fade2) || !BridgeFinf(rampS) || !BridgeFinf(opaqueS)) return false;
    const uint32_t zMasked = BridgeBitsOfFloat((float)p.sentinelZ) ^ kBridgeMask;
    const uint32_t wMasked = BridgeBitsOfFloat((float)p.sentinelW) ^ kBridgeMask;
    float clock = 0.0f;
    if (BridgeVerifyAt(s.base, zMasked, wMasked, &clock) != 1) return false;   // BridgePublish rescans
    if (!BridgeFinf(clock)) return false;
    const float f[2] = { BridgeClampf(fade1, 0.0f, 1.0f), BridgeClampf(fade2, 0.0f, 1.0f) };
    float w[2];
    for (int i = 0; i < 2; ++i)
        w[i] = f[i] <= 0.0f ? 0.0f : f[i] >= 1.0f ? clock + rampS + opaqueS : clock + rampS * f[i];
    return BridgeWriteBustFade(s.base, w) == 1;
}

// [backrooms1] The Backrooms overlay (backrooms.h, Notes 54): 8.x / 8.y the players' fades, 8.z flags, 8.w seconds in
// the maze, 9.x players present. Fixed indices f[32..36], written only after 9.w (f[39]) reads back the sentinel that
// ShaderFixes\backrooms_fx.ini sets (with gfx1's d3dx.ini lines the array is 12 float4s, so f[39] is inside it).
inline int BridgeWriteBackrooms(uintptr_t base, const float v[7])
{
    __try
    {
        volatile float* f = (volatile float*)base;
        if (f[39] != 246813.75f) return 0;          // 9.w: the array was not grown to 10 float4s -> writing f[32+] would be a heap overrun
        f[32] = v[0]; f[33] = v[1]; f[34] = v[2]; f[35] = v[3]; f[36] = v[4];
        f[37] = v[5]; f[38] = v[6];                 // [br4] 9.y the goal, 9.z the per-player HUD bits (inside the same grown block: 9.w is f[39])
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
inline bool BridgePublishBackrooms(const BridgeParams& p, BridgeState& s, float z1, float z2, float flags, float secs, float players,
                                   float goal = 0.0f, float hud = 0.0f)
{
    if (s.phase != kBridgeLocked || s.base == 0) return false;
    if (!BridgeFinf(z1) || !BridgeFinf(z2) || !BridgeFinf(flags) || !BridgeFinf(secs) || !BridgeFinf(players) || !BridgeFinf(goal) || !BridgeFinf(hud)) return false;
    const uint32_t zMasked = BridgeBitsOfFloat((float)p.sentinelZ) ^ kBridgeMask, wMasked = BridgeBitsOfFloat((float)p.sentinelW) ^ kBridgeMask;
    float clock = 0.0f;
    if (BridgeVerifyAt(s.base, zMasked, wMasked, &clock) != 1) return false;
    const float v[7] = { BridgeClampf(z1, 0.0f, 1.0f), BridgeClampf(z2, 0.0f, 1.0f), BridgeClampf(flags, 0.0f, 16383.0f),
                         BridgeClampf(secs, 0.0f, 360000.0f), BridgeClampf(players, 0.0f, 2.0f),
                         BridgeClampf(goal, 0.0f, 8191.0f), BridgeClampf(hud, 0.0f, 262143.0f) };
    return BridgeWriteBackrooms(s.base, v) == 1;
}

} // namespace vsim

#endif // VEHSIM_SIM_BRIDGE_H
