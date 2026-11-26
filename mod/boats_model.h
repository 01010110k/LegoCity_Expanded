// =============================================================================================
//  boats_model.h - [boats2] the traffic boats: no more processions (2026-11-19, Notes 56 section 8).
//  Pure (no engine calls): the decisions boats.h makes, so test\boats_harness.cpp can run them.
//
//  Why boats line up (Notes 56 section 8, proven from the exe bytes and the boat map):
//    * the population manager places EVERY traffic vehicle with one routine (0x140702420): it takes the
//      lanes within 96 u of the camera (24 u for cars), draws two random points on each lane, keeps the
//      points 20..80 u from the camera that are at least `gap` from every other vehicle, and picks one;
//      gap = base x U(0.75, 1.25) x half the model's length, base = 16 for the boat map, 8 for roads -
//      two writable floats at 0x1411700E0 / 0x1411700E8 (boats) read by nothing else;
//    * the boat map has few, long lanes (68 road lanes, median 119 u, up to 1,022 u; 1 lane each way):
//      round the camera there is usually ONE road, so every boat the manager adds goes on the same line,
//      one `gap` behind the last - an evenly spaced file - and boats of different speeds (every boat model
//      is MinSpeed 2.25 .. MaxSpeed 3.5) close up on a slower one and queue behind it (no overtaking on
//      a one-lane road); the file follows the camera because new boats keep being added to it.
//  What the DLL does about it (boats.h, [boats] in the ini):
//    1. spawn_gap: the two boat-map floats x spawn_gap (default 1.6: 16 -> 25.6, 8 -> 12.8) - a new boat
//       goes in only where no other vehicle is within ~1.6x the stock distance, so a lane near you takes
//       one or two boats, not six. Put back at `[`, enable = 0, unhook and after a fault.
//    2. files: a traffic boat that has been FOLLOWING another one (same heading within `align`, the other
//       ahead of it within `follow` u and within `lat` u of its line) for `hold` s is faded out - the
//       one at the back first, one every `gap_s` - unless a player is within `keep` u of it, or it is in
//       full view near you (simulation LOD 1 and closer than `fade_view`), or it is a player's, a chasing
//       police boat, or one of the marinas' / stands' boats. The manager puts the next boat somewhere
//       else on the water (it may come back to the same lane, but not into the file: (1)).
//    3. cap: at most `cap` traffic boats within `cap_radius` u of the players; the farthest that may go,
//       goes (same rules), one every `gap_s`.
//  [I] until played: that a faded traffic boat frees its traffic slot as a culled one does (the wanted
//  mode's FadeOutCar does it to traffic police cars); that each boat's speed is drawn in MinSpeed..MaxSpeed.
// =============================================================================================
#pragma once
#include <cmath>
#include <cstdint>

namespace btm {

struct Params
{
    int   enable     = 1;       // 0 = the stock boat traffic (nothing below runs, the floats stay stock)
    float spawn_gap  = 1.6f;    // x the boat map's spawn spacing (1 = stock; 1.0 .. 2.5)
    int   files      = 1;       // 1 = break up files of boats following one another
    float follow     = 40.0f;   // u: a boat this close behind another, heading the same way, is following it
    float lat        = 7.0f;    // u: ... and within this of its line
    float align      = 25.0f;   // degrees: "the same way"
    float hold       = 4.0f;    // s following before it goes
    float keep       = 35.0f;   // u: never one this close to a player
    float fade_view  = 60.0f;   // u: one in full view (LOD 1) goes only from this far
    float gap_s      = 1.5f;    // s between two fades
    int   cap        = 8;       // at most this many traffic boats within cap_radius of the players (0 = no cap)
    float cap_radius = 150.0f;  // u
    int   log        = 1;       // 1 = one line per fade + a summary every 30 s while boats are about
};

// The stock floats and where the code reads them ([P] 0x1407024F1 / 0x1407024FA: movss xmm8 / xmm12,
// [rip+disp] -> 0x1411700E0 = 16.0, 0x1411700E8 = 8.0; the road map's 8.0 / 4.0 at 0x1411700DC / E4).
static const uint32_t RVA_GAP_LANE  = 0x11700E0, RVA_GAP_SPAWN = 0x11700E8;
static const uint32_t RVA_READ_LANE = 0x7024F1,  RVA_READ_SPAWN = 0x7024FA;
static const float    kStockLane = 16.0f, kStockSpawn = 8.0f;

static inline float Clamp(float v, float lo, float hi) { return !(v >= lo) ? lo : v > hi ? hi : v; }

// The ini's values made safe (an unusable value -> the default).
static inline void Sanitize(Params& p)
{

    p.enable = p.enable ? 1 : 0; p.files = p.files ? 1 : 0; p.log = p.log ? 1 : 0;
    p.spawn_gap  = Clamp(p.spawn_gap, 1.0f, 2.5f);
    p.follow     = Clamp(p.follow, 8.0f, 120.0f);
    p.lat        = Clamp(p.lat, 1.0f, 20.0f);
    p.align      = Clamp(p.align, 5.0f, 60.0f);
    p.hold       = Clamp(p.hold, 0.5f, 60.0f);
    p.keep       = Clamp(p.keep, 8.0f, 200.0f);
    p.fade_view  = Clamp(p.fade_view, p.keep, 400.0f);
    p.gap_s      = Clamp(p.gap_s, 0.25f, 30.0f);
    p.cap        = p.cap < 0 ? 0 : p.cap > 24 ? 24 : p.cap;
    p.cap_radius = Clamp(p.cap_radius, 40.0f, 600.0f);

}

// The two floats the DLL writes for a spawn_gap.
static inline float LaneGap(float k)  { return kStockLane * k; }
static inline float SpawnGap(float k) { return kStockSpawn * k; }

// x86-64 "movss xmmN, dword [rip + disp32]" (F3 [REX] 0F 10 modrm disp32, modrm mod 00 rm 101) at `at`:
// the address it reads, or 0 if the bytes are not that instruction. `at` = the instruction's address.
static inline uint64_t MovssRipTarget(const uint8_t* code, uint64_t at)
{
    int i = 0;
    if (code[i++] != 0xF3) return 0;
    if ((code[i] & 0xF0) == 0x40) ++i;
    if (code[i] != 0x0F || code[i + 1] != 0x10) return 0;
    const uint8_t modrm = code[i + 2];
    if ((modrm & 0xC7) != 0x05) return 0;
    int32_t disp = 0;
    for (int k = 0; k < 4; ++k) disp |= static_cast<int32_t>(code[i + 3 + k]) << (8 * k);
    const int len = i + 3 + 4;
    return at + static_cast<uint64_t>(len) + static_cast<int64_t>(disp);
}

// One traffic boat this tick.
struct Boat
{
    const void* id;        // the vehicle (identity across ticks)
    float x, z;            // position
    float fx, fz;          // heading (unit, horizontal)
    float dPlayer;         // u to the nearest player
    bool  lod1;            // full simulation (near a camera / in view)
    bool  guard;           // never ours to fade: a player's, chasing, a marina / stand boat
};

static const int kMax = 48;

// The boat b is following (the nearest one ahead in its file), or -1.
static inline int Leader(const Boat* b, int n, int i, const Params& p, float* gapOut = nullptr)
{
    const float cosA = std::cos(p.align * 3.14159265f / 180.0f);
    int best = -1; float bestAlong = 1e9f;
    for (int j = 0; j < n; ++j)
    {
        if (j == i) continue;
        if (b[i].fx * b[j].fx + b[i].fz * b[j].fz < cosA) continue;        // not the same way
        const float rx = b[j].x - b[i].x, rz = b[j].z - b[i].z;
        const float along = rx * b[i].fx + rz * b[i].fz;
        if (!(along > 0.5f) || along > p.follow) continue;                  // not ahead / too far
        const float lat = std::fabs(rx * b[i].fz - rz * b[i].fx);
        if (lat > p.lat) continue;                                           // not on its line
        if (along < bestAlong) { bestAlong = along; best = j; }
    }
    if (gapOut) *gapOut = best >= 0 ? bestAlong : 0.0f;
    return best;
}

// How many boats are in i's file behind and including i's leader chain (i itself counted): the length of
// the chain of leaders from i forward.
static inline int ChainAhead(const Boat* b, int n, int i, const Params& p)
{
    int len = 1, cur = i;
    for (int guard = 0; guard < n; ++guard)
    {
        const int j = Leader(b, n, cur, p);
        if (j < 0) break;
        ++len; cur = j;
    }
    return len;
}

static inline bool MayFade(const Boat& b, const Params& p)
{
    if (b.guard || b.dPlayer < p.keep) return false;
    return !b.lod1 || b.dPlayer >= p.fade_view;
}

// Per-boat memory: how long it has been following (by vehicle identity).
struct Mem
{
    const void* id[kMax] = {};
    float       t[kMax]  = {};
    int         n = 0;
    float       sinceFade = 1e9f;
};

enum Why : uint8_t { kNone, kFile, kCap };
struct Out { int idx = -1; Why why = kNone; float followS = 0.0f, gapU = 0.0f; int fileLen = 0, inRadius = 0, following = 0, longest = 0; };

// One decision step over this tick's boats: updates the timers, returns at most one boat to fade.
static inline Out Step(Mem& m, const Boat* b, int n, float dt, const Params& p)
{
    Out o;
    if (n > kMax) n = kMax;
    // carry the timers over by identity
    float t[kMax] = {};
    for (int i = 0; i < n; ++i)
        for (int k = 0; k < m.n; ++k) if (m.id[k] == b[i].id) { t[i] = m.t[k]; break; }
    int lead[kMax];
    for (int i = 0; i < n; ++i)
    {
        lead[i] = Leader(b, n, i, p);
        t[i] = lead[i] >= 0 ? t[i] + dt : 0.0f;
        if (lead[i] >= 0) ++o.following;
        if (b[i].dPlayer <= p.cap_radius) ++o.inRadius;
    }
    for (int i = 0; i < n; ++i) { const int L = ChainAhead(b, n, i, p); if (L > o.longest) o.longest = L; }
    m.n = n;
    for (int i = 0; i < n; ++i) { m.id[i] = b[i].id; m.t[i] = t[i]; }
    m.sinceFade += dt;
    if (m.sinceFade < p.gap_s) return o;
    // 1. a file: of the boats that have followed long enough and may go, the one farthest from the players
    if (p.files)
    {
        int pick = -1;
        for (int i = 0; i < n; ++i)
            if (lead[i] >= 0 && t[i] >= p.hold && MayFade(b[i], p) && (pick < 0 || b[i].dPlayer > b[pick].dPlayer)) pick = i;
        if (pick >= 0)
        {
            o.idx = pick; o.why = kFile; o.followS = t[pick];
            Leader(b, n, pick, p, &o.gapU);
            o.fileLen = ChainAhead(b, n, pick, p);
            m.sinceFade = 0.0f;
            return o;
        }
    }
    // 2. the cap: too many near the players -> the farthest one that may go
    if (p.cap > 0 && o.inRadius > p.cap)
    {
        int pick = -1;
        for (int i = 0; i < n; ++i)
            if (b[i].dPlayer <= p.cap_radius && MayFade(b[i], p) && (pick < 0 || b[i].dPlayer > b[pick].dPlayer)) pick = i;
        if (pick >= 0) { o.idx = pick; o.why = kCap; m.sinceFade = 0.0f; }
    }
    return o;
}

} // namespace btm
