// backrooms_model.h - the Backrooms: pure logic (no engine, no Windows).  Tested by test\backrooms_harness.cpp.
//
//   data     the portal table generated with the patch (out\portals.txt = backrooms_table.h): the zone, modules,
//            connectors ("stubs"), walk rectangles, exits, the arrival shaft, statue points, colossi, bikes, exit
//            destinations, generator parameters.
//   maze     every module is a SEALED pocket; a module's connectors are the one standard corridor piece, gender P
//            (room at its west end) or M (room at its east end).  A link joins one P and one M; walking east through a
//            P's mid plane puts you, by a PURE TRANSLATION, at the mid plane of its M, still walking east.  The set of
//            links is a perfect matching that this model re-rolls: a link is rolled when a player walks into a
//            connector, from a per-visit random seed, under the scarcity rules (param lines) - so no two visits and no
//            two walks through the same door give the same maze.  A link in use (a player in either connector, or
//            used within link_hold seconds) is never changed.
//   players  Outside -> Falling (fix1: behind black, held just above the arrival floor, let go, landed at the table's
//            floor height; through the floor = caught and held again) -> Inside -> Surfacing (held over the exit
//            destination until the player stands there) -> Outside.
//   statues  stand still while any view could see them (a conservative cone test, no occlusion credit); move rarely,
//            only unobserved, never within statue_min_dist of a player, only between the module's generated points.
//
// Everything here is deterministic for a given seed and input sequence.
#pragma once
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

namespace brm {

constexpr int kMaxMods = 768, kMaxStubs = 2400, kMaxWalk = 40000, kMaxExits = 64, kMaxPts = 1800, kMaxCol = 16, kMaxBikes = 128,
              kMaxDest = 8, kPlayers = 2, kMaxStatues = 6, kMaxActions = 32;   // [br4] five biomes: 527 modules, 1,7xx connectors
constexpr int kMaxWork = 64, kMaxWorkAlive = 3, kMaxVeh = 8;   // [br3w] worker points, at most 3 out at once; stood vehicles
constexpr int kMaxBiomes = 8, kMaxGold = 96;                    // [br4]

enum Fam : int { F_L0, F_ODD, F_POOL, F_MASSIVE, F_RAIL, F_EXIT, F_ARRIVAL, F_THEME0 };   // themed families: F_THEME0 + index
constexpr int kMaxThemes = 24;
enum : int { MF_MASSIVE = 1, MF_EXIT = 2, MF_ARRIVAL = 4, MF_POOL = 8, MF_RAIL = 16, MF_VAULT = 32, MF_VAST = 64 };   // [br4] 32, 64
enum : int { MK_NONE = 0, MK_EXIT = 1, MK_GOLD = 2, MK_FAKE_H = 3, MK_FAKE_V = 4 };   // [br4] a connector's lit sign; [br5] 3 / 4 = a FAKE
                                                                // exit sign (mirrored / upside down): it promises nothing

struct V3 { float x, y, z; };
inline float Dist(const V3& a, const V3& b) { float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z; return std::sqrt(dx * dx + dy * dy + dz * dz); }
inline float DistXZ(const V3& a, const V3& b) { float dx = a.x - b.x, dz = a.z - b.z; return std::sqrt(dx * dx + dz * dz); }

struct Mod  { char name[20]; int fam; float x0, z0, x1, z1, y, ytop; int flags; int nP, nM; int biome; int nGold; };   // [br4] biome, gold bricks
struct Stub { int mod; char g; float xm, y, z0, z1, x0, x1; int ct; int skin; int mark; };   // [br3] ct = connector type; [br4] skin (a biome's
                                                                            // corridor: links join one type AND one skin), mark (MK_*)
struct Gold { int mod; V3 p; };                                             // [br4] a gold brick to find
struct CType { char name[12]; float len, rise; bool used; };                // [br3] T length (u), height change west -> east (u)
constexpr int kMaxCTypes = 16;
struct Rect { int mod; float x0, z0, x1, z1; };
struct ExitR { int mod; float x0, z0, x1, z1, y; };
struct Pt   { int mod; V3 p; float yaw, scale; };
struct Dest { char name[32]; V3 p; float heading; };
struct VehP { int mod; char model[32]; V3 p; float heading; };             // [br3w] a stock vehicle stood in a module

struct Params {
    float w_l0 = 100, w_odd = 9, special_base = 0, special_gain = 0.012f, special_max = 0.22f, cooldown = 14, first_special = 8,
          pocket_stay = 0.45f, pocket_max = 3, massive_share = 0.22f, pool_share = 0.16f, rail_share = 0.07f, link_hold = 20,
          relink_back = 0.35f, mercy_min = 12, exit_weight = 2, exit_after = 10, statue_cap = 3, statue_min_dist = 4,
          statue_step = 6, statue_period = 25, bike_chance = 0.004f, terlow = -125, mercy_weight = 30, mercy_doors = 4;
    // [br3] so that the same special module does not come back several times: a module seen this visit weighs seen_weight (rising with
    // its age in crossings to 1 at seen_age) against an unseen one; a special family with unseen modules weighs
    // fresh_family times its share; yellow modules the same, milder (l0_seen_*); a special rolled where no connector of
    // the right type leads to it stays owed to the next door.
    float seen_weight = 0.04f, seen_age = 80, fresh_family = 3.0f, l0_seen_weight = 0.35f, l0_seen_age = 12;
    // [fix1 2026-11-19] the arrival: held still just above the arrival floor (the table's height, not the engine's
    // probe), then let go; landed = the engine's "on ground" at that height, or the height alone held for 0.5 s;
    // fell through (floor not streamed in yet) = caught fall_catch below it and held again; all of it behind black.
    float fall_hover = 0.5f, fall_hold = 1.2f, fall_retry_hold = 1.0f, fall_catch = 2.0f, fall_timeout = 45, land_dark = 0.5f;
    // [br4 2026-11-24] the goal (gold bricks), where a visit starts, vast rooms, the reminders, the easy way out
    float goal_count = 5, goal_reach = 0.95f, goal_near = 9.0f;
    float goal_every = 3, goal_boost = 3.0f;
    // [br5 2026-11-25] getting out is harder: EXIT signs are a TRAIL - trail_min..trail_max real EXIT doors in a row (rolled
    // per visit), the last one opens on the exit room; any other door, or a fake sign, starts over.  After mercy_min
    // minutes the trail is mercy_trail doors.  The brick row shows goal_show seconds after a pickup, a text line msg_show;
    // no "how to leave" before remind_first / stuck_from seconds.
    float trail_min = 4, trail_max = 5, mercy_trail = 2, goal_show = 7, msg_show = 6, stuck_from = 300;
    float sign_boost = 3.0f;                         // a body room with a real EXIT sign weighs this many times more behind a plain door     // doors without a new brick after which the next plain door leads to one; weight x of a room with one
    float biome_w0 = 30, biome_w1 = 22, biome_w2 = 16, biome_w3 = 16, biome_w4 = 16, biome_w5 = 0, biome_w6 = 0, biome_w7 = 0;
    float vast_share = 0.24f, vast_every = 9;
    float remind_first = 120, remind_every = 150, remind_show = 12, stuck_time = 70, easy_out_hold = 3.0f;
};

struct Data {
    bool ok = false;
    float zx0 = 0, zy0 = 0, zz0 = 0, zx1 = 0, zy1 = 0, zz1 = 0;
    Params par;
    int nMods = 0, nStubs = 0, nWalk = 0, nExits = 0, nStat = 0, nCol = 0, nBikes = 0, nDest = 0, nThemes = 0;
    Mod mods[kMaxMods]; Stub stubs[kMaxStubs]; Rect walk[kMaxWalk]; ExitR exits[kMaxExits];
    Pt stat[kMaxPts]; Pt col[kMaxCol]; Pt bikes[kMaxBikes]; Dest dest[kMaxDest];
    char themes[kMaxThemes][16];
    CType ct[kMaxCTypes]; int nCt = 0;                                          // [br3] connector types
    Pt work[kMaxWork]; int nWork = 0; VehP veh[kMaxVeh]; int nVeh = 0;         // [br3w] workers, stood vehicles
    bool hasArrive = false; int arriveMod = -1; V3 arrive = {}; float shaftTop = 0;   // the first arrival (biome 0 in a v3 table)
    // [br4] biomes: an arrival room each; the gold bricks; the reward room
    int nBiomes = 1; char biomes[kMaxBiomes][16] = {"level0"}; int arrMod[kMaxBiomes]; V3 arrPt[kMaxBiomes]; bool v3 = false;
    Gold gold[kMaxGold]; int nGold = 0; int vaultMod = -1;
    bool hasEntry = false; float ex0 = 0, ez0 = 0, ex1 = 0, ez1 = 0, eyTop = 0, eyBot = 0;   // the village door's shaft: a box
    int walkFirst[kMaxMods + 1] = {};     // walk rectangles are grouped by module
    int stubFirst[kMaxMods + 1] = {};
    unsigned stamp = 0;
};

// ---------------------------------------------------------------------------------------------- parsing
inline const char* SkipWs(const char* s) { while (*s == ' ' || *s == '\t') ++s; return s; }
inline bool Field(const char*& s, char* out, int cap) {           // next comma-separated field, trimmed
    s = SkipWs(s);
    if (!*s) return false;
    int n = 0;
    while (*s && *s != ',' && *s != '\r' && *s != '\n') { if (n < cap - 1) out[n++] = *s; ++s; }
    while (n > 0 && (out[n - 1] == ' ' || out[n - 1] == '\t')) --n;
    out[n] = 0;
    if (*s == ',') ++s;
    return true;
}
inline bool Num(const char*& s, float& v) { char b[40]; if (!Field(s, b, 40) || !b[0]) return false; char* e = nullptr; v = std::strtof(b, &e); return e != b && std::isfinite(v); }
inline bool Int(const char*& s, int& v) { float f; if (!Num(s, f)) return false; v = (int)std::lround(f); return true; }

inline int FamOf(Data& d, const char* f) {
    if (!std::strcmp(f, "l0")) return F_L0;
    if (!std::strcmp(f, "odd")) return F_ODD;
    if (!std::strcmp(f, "pool")) return F_POOL;
    if (!std::strcmp(f, "massive")) return F_MASSIVE;
    if (!std::strcmp(f, "rail")) return F_RAIL;
    if (!std::strcmp(f, "exit")) return F_EXIT;
    if (!std::strcmp(f, "arrival")) return F_ARRIVAL;
    for (int i = 0; i < d.nThemes; ++i) if (!std::strcmp(d.themes[i], f)) return F_THEME0 + i;
    if (d.nThemes >= kMaxThemes) return -1;
    std::snprintf(d.themes[d.nThemes], 16, "%s", f);
    return F_THEME0 + d.nThemes++;
}
inline bool IsYellow(int fam) { return fam == F_L0 || fam == F_ODD || fam == F_EXIT || fam == F_ARRIVAL; }   // "a body room" (of any biome)

inline bool SetParam(Params& p, const char* k, float v) {
#define BRM_P(n) if (!std::strcmp(k, #n)) { p.n = v; return true; }
    BRM_P(w_l0) BRM_P(w_odd) BRM_P(special_base) BRM_P(special_gain) BRM_P(special_max) BRM_P(cooldown) BRM_P(first_special)
    BRM_P(pocket_stay) BRM_P(pocket_max) BRM_P(massive_share) BRM_P(pool_share) BRM_P(rail_share) BRM_P(link_hold) BRM_P(relink_back)
    BRM_P(mercy_min) BRM_P(exit_weight) BRM_P(exit_after) BRM_P(statue_cap) BRM_P(statue_min_dist) BRM_P(statue_step)
    BRM_P(statue_period) BRM_P(bike_chance) BRM_P(terlow) BRM_P(mercy_weight) BRM_P(mercy_doors)
    BRM_P(fall_hover) BRM_P(fall_hold) BRM_P(fall_retry_hold) BRM_P(fall_catch) BRM_P(fall_timeout) BRM_P(land_dark)
    BRM_P(seen_weight) BRM_P(seen_age) BRM_P(fresh_family) BRM_P(l0_seen_weight) BRM_P(l0_seen_age)
    BRM_P(goal_count) BRM_P(goal_reach) BRM_P(goal_near) BRM_P(goal_every) BRM_P(goal_boost)
    BRM_P(trail_min) BRM_P(trail_max) BRM_P(mercy_trail) BRM_P(goal_show) BRM_P(msg_show) BRM_P(stuck_from) BRM_P(sign_boost)     // [br5] BRM_P(biome_w0) BRM_P(biome_w1) BRM_P(biome_w2) BRM_P(biome_w3) BRM_P(biome_w4)
    BRM_P(biome_w5) BRM_P(biome_w6) BRM_P(biome_w7) BRM_P(vast_share) BRM_P(vast_every) BRM_P(remind_first) BRM_P(remind_every)
    BRM_P(remind_show) BRM_P(stuck_time) BRM_P(easy_out_hold)     // [br4]
#undef BRM_P
    return false;
}

// one line of the table; returns false (and a reason) on a line that is neither a record nor a comment
inline bool ParseLine(Data& d, const char* line, char* why, int cap) {
    const char* s = SkipWs(line);
    if (!*s || *s == '#' || *s == ';' || *s == '\r' || *s == '\n') return true;
    char k[16]; if (!Field(s, k, 16)) return true;
    auto bad = [&](const char* m) { std::snprintf(why, cap, "%s: %.60s", m, line); return false; };
    unsigned h = 2166136261u; for (const char* q = line; *q; ++q) h = (h ^ (unsigned char)*q) * 16777619u; d.stamp = d.stamp * 31u + h;
    if (!std::strcmp(k, "zone")) {
        if (!(Num(s, d.zx0) && Num(s, d.zy0) && Num(s, d.zz0) && Num(s, d.zx1) && Num(s, d.zy1) && Num(s, d.zz1))) return bad("zone");
        return true;
    }
    if (!std::strcmp(k, "param")) { char n[24]; float v; if (!Field(s, n, 24) || !Num(s, v)) return bad("param"); SetParam(d.par, n, v); return true; }
    if (!std::strcmp(k, "mod")) {
        if (d.nMods >= kMaxMods) return bad("too many modules");
        Mod& m = d.mods[d.nMods]; int id; char f[16];
        if (!Int(s, id) || id != d.nMods || !Field(s, m.name, 20) || !Field(s, f, 16)) return bad("mod");
        m.fam = FamOf(d, f);
        if (m.fam < 0 || !(Num(s, m.x0) && Num(s, m.z0) && Num(s, m.x1) && Num(s, m.z1) && Num(s, m.y) && Num(s, m.ytop) && Int(s, m.flags))) return bad("mod");
        m.biome = 0; m.nGold = 0;                                    // [br4] a v2 table = one biome
        if (*SkipWs(s) && !Int(s, m.biome)) return bad("mod biome");
        if (m.biome < 0 || m.biome >= kMaxBiomes) return bad("mod biome");
        if (m.flags & MF_VAULT) d.vaultMod = d.nMods;
        m.nP = m.nM = 0; d.stubFirst[d.nMods] = d.nStubs; d.walkFirst[d.nMods] = d.nWalk;
        ++d.nMods; d.stubFirst[d.nMods] = d.nStubs; d.walkFirst[d.nMods] = d.nWalk;
        return true;
    }
    if (!std::strcmp(k, "stub")) {
        if (d.nStubs >= kMaxStubs) return bad("too many stubs");
        Stub& t = d.stubs[d.nStubs]; int id; char g[4];
        if (!Int(s, id) || id != d.nStubs || !Int(s, t.mod) || t.mod != d.nMods - 1 || !Field(s, g, 4) || (g[0] != 'P' && g[0] != 'M')) return bad("stub");
        t.g = g[0];
        if (!(Num(s, t.xm) && Num(s, t.y) && Num(s, t.z0) && Num(s, t.z1) && Num(s, t.x0) && Num(s, t.x1))) return bad("stub");
        t.ct = 0;                                                    // [br3] a v1 table (no type column) = every connector type 0
        if (*SkipWs(s) && !Int(s, t.ct)) return bad("stub");
        if (t.ct < 0 || t.ct >= kMaxCTypes) return bad("stub type");
        t.skin = d.mods[t.mod].biome; t.mark = MK_NONE;              // [br4] v3: skin, mark
        if (*SkipWs(s) && !(Int(s, t.skin) && Int(s, t.mark))) return bad("stub skin / mark");
        if (t.skin < 0 || t.skin >= kMaxBiomes || t.mark < 0 || t.mark > MK_FAKE_V) return bad("stub skin / mark");
        (t.g == 'P' ? d.mods[t.mod].nP : d.mods[t.mod].nM)++;
        ++d.nStubs; d.stubFirst[d.nMods] = d.nStubs;
        return true;
    }
    if (!std::strcmp(k, "ctype")) {                                  // [br3]
        int id; CType c = {};
        if (!Int(s, id) || id != d.nCt || id >= kMaxCTypes || !Field(s, c.name, 12) || !Num(s, c.len) || !Num(s, c.rise)) return bad("ctype");
        d.ct[d.nCt++] = c; return true;
    }
    if (!std::strcmp(k, "walk")) {
        if (d.nWalk >= kMaxWalk) return bad("too many walk rectangles");
        Rect& r = d.walk[d.nWalk];
        if (!Int(s, r.mod) || r.mod != d.nMods - 1 || !(Num(s, r.x0) && Num(s, r.z0) && Num(s, r.x1) && Num(s, r.z1))) return bad("walk");
        ++d.nWalk; d.walkFirst[d.nMods] = d.nWalk;
        return true;
    }
    if (!std::strcmp(k, "exit")) {
        if (d.nExits >= kMaxExits) return bad("too many exits");
        ExitR& e = d.exits[d.nExits];
        if (!Int(s, e.mod) || e.mod < 0 || e.mod >= d.nMods || !(Num(s, e.x0) && Num(s, e.z0) && Num(s, e.x1) && Num(s, e.z1) && Num(s, e.y))) return bad("exit");
        ++d.nExits; return true;
    }
    if (!std::strcmp(k, "arrive")) {                                 // [br4] one per biome (the biome of its module)
        int am; V3 ap; float top;
        if (!Int(s, am) || am < 0 || am >= d.nMods || !(Num(s, ap.x) && Num(s, ap.y) && Num(s, ap.z) && Num(s, top))) return bad("arrive");
        const int b = d.mods[am].biome;
        d.arrMod[b] = am; d.arrPt[b] = ap;
        if (!d.hasArrive || b == 0) { d.arriveMod = am; d.arrive = ap; d.shaftTop = top; }
        d.hasArrive = true; return true;
    }
    if (!std::strcmp(k, "biome")) {                                  // [br4]
        int id; char nm[16];
        if (!Int(s, id) || id < 0 || id >= kMaxBiomes || !Field(s, nm, 16)) return bad("biome");
        std::snprintf(d.biomes[id], 16, "%s", nm); if (id + 1 > d.nBiomes) d.nBiomes = id + 1; d.v3 = true; return true;
    }
    if (!std::strcmp(k, "gold")) {                                   // [br4]
        Gold g = {};
        if (!Int(s, g.mod) || g.mod < 0 || g.mod >= d.nMods || !(Num(s, g.p.x) && Num(s, g.p.y) && Num(s, g.p.z))) return bad("gold");
        if (d.nGold >= kMaxGold) return bad("too many gold bricks");
        d.gold[d.nGold++] = g; d.mods[g.mod].nGold++; return true;
    }
    if (!std::strcmp(k, "statue") || !std::strcmp(k, "colossus") || !std::strcmp(k, "bike")) {
        Pt p = {}; p.scale = 1;
        if (!Int(s, p.mod) || p.mod < 0 || p.mod >= d.nMods || !(Num(s, p.p.x) && Num(s, p.p.y) && Num(s, p.p.z))) return bad(k);
        if (k[0] == 'c' && !(Num(s, p.yaw) && Num(s, p.scale))) return bad(k);
        if (k[0] == 's') { if (d.nStat < kMaxPts) d.stat[d.nStat++] = p; }
        else if (k[0] == 'c') { if (d.nCol < kMaxCol) d.col[d.nCol++] = p; }
        else { if (d.nBikes < kMaxBikes) d.bikes[d.nBikes++] = p; }
        return true;
    }
    if (!std::strcmp(k, "worker")) {                                 // [br3w]
        Pt p = {}; p.scale = 1;
        if (!Int(s, p.mod) || p.mod < 0 || p.mod >= d.nMods || !(Num(s, p.p.x) && Num(s, p.p.y) && Num(s, p.p.z) && Num(s, p.yaw))) return bad("worker");
        if (d.nWork < kMaxWork) d.work[d.nWork++] = p;
        return true;
    }
    if (!std::strcmp(k, "vehicle")) {                                // [br3w]
        VehP v = {};
        if (!Int(s, v.mod) || v.mod < 0 || v.mod >= d.nMods || !Field(s, v.model, 32) || !v.model[0] ||
            !(Num(s, v.p.x) && Num(s, v.p.y) && Num(s, v.p.z) && Num(s, v.heading))) return bad("vehicle");
        if (d.nVeh < kMaxVeh) d.veh[d.nVeh++] = v;
        return true;
    }
    if (!std::strcmp(k, "dest")) {
        if (d.nDest >= kMaxDest) return bad("too many destinations");
        Dest& t = d.dest[d.nDest];
        if (!Field(s, t.name, 32) || !(Num(s, t.p.x) && Num(s, t.p.y) && Num(s, t.p.z) && Num(s, t.heading))) return bad("dest");
        ++d.nDest; return true;
    }
    if (!std::strcmp(k, "entry")) {
        if (!(Num(s, d.ex0) && Num(s, d.ez0) && Num(s, d.ex1) && Num(s, d.ez1) && Num(s, d.eyTop) && Num(s, d.eyBot)) || !(d.ex1 > d.ex0 && d.ez1 > d.ez0 && d.eyTop > d.eyBot)) return bad("entry");
        d.hasEntry = true; return true;
    }
    return bad("unknown record");
}

inline bool InRect(const Rect& r, float x, float z, float m = 0) { return x >= r.x0 + m && x <= r.x1 - m && z >= r.z0 + m && z <= r.z1 - m; }
inline bool Walkable(const Data& d, int mod, float x, float z, float margin = 0) {
    for (int i = d.walkFirst[mod]; i < d.walkFirst[mod + 1]; ++i) if (InRect(d.walk[i], x, z, margin)) return true;
    return false;
}
// a disc of radius r is on walkable ground if its centre and 8 rim points are
inline bool WalkableDisc(const Data& d, int mod, float x, float z, float r) {
    if (!Walkable(d, mod, x, z)) return false;
    for (int k = 0; k < 8; ++k) { float a = 0.785398f * k; if (!Walkable(d, mod, x + r * std::cos(a), z + r * std::sin(a))) return false; }
    return true;
}

constexpr float kStubLen = 3.048f, kStubWid = 0.6096f;      // 20 x 4 lattice cells of 0.1524 (type 0; [br3] each type its own length)
inline float StubLen(const Data& d, const Stub& s) { return d.nCt > s.ct ? d.ct[s.ct].len : kStubLen; }
inline float StubRise(const Data& d, const Stub& s) { return d.nCt > s.ct ? d.ct[s.ct].rise : 0.0f; }

inline bool Validate(Data& d, char* why, int cap) {
    d.ok = false;
    auto bad = [&](const char* m, int i) { std::snprintf(why, cap, "%s (%d)", m, i); return false; };
    if (d.nMods < 4 || d.nStubs < 4) return bad("too few modules / connectors", d.nMods);
    if (!(d.zx1 > d.zx0 && d.zy1 > d.zy0 && d.zz1 > d.zz0)) return bad("no zone", 0);
    if (!d.hasArrive || d.arriveMod < 0 || d.arriveMod >= d.nMods) return bad("no arrival", 0);
    if (d.nExits < 1 || d.nDest < 1) return bad("no exit or no destination", d.nExits);
    int nP = 0, nM = 0;
    int perP[kMaxCTypes * kMaxBiomes] = {}, perM[kMaxCTypes * kMaxBiomes] = {};          // [br4] per type AND skin
    for (int i = 0; i < d.nCt; ++i) d.ct[i].used = false;
    for (int i = 0; i < d.nStubs; ++i) {
        const Stub& s = d.stubs[i]; const Mod& m = d.mods[s.mod];
        (s.g == 'P' ? nP : nM)++;
        if (d.nCt > 0 && s.ct >= d.nCt) return bad("a connector of an unknown type", i);
        if (d.nCt == 0 && s.ct != 0) return bad("a typed connector in a table without types", i);
        (s.g == 'P' ? perP : perM)[s.ct * kMaxBiomes + s.skin]++;
        if (s.skin >= d.nBiomes) return bad("a connector in an unknown skin", i);
        if (d.nCt > 0) d.ct[s.ct].used = true;
        if (std::fabs((s.x1 - s.x0) - StubLen(d, s)) > 0.01f || std::fabs((s.z1 - s.z0) - kStubWid) > 0.01f) return bad("a connector is not its type's size", i);
        if (std::fabs(s.xm - (s.x0 + s.x1) / 2) > 0.005f) return bad("a connector's plane is not at its middle", i);
        // [br3] a connector's west end lies at its module's floor, or below / above it by its type's climb
        if (std::fabs(s.y - m.y) > std::fabs(StubRise(d, s)) + 0.005f) return bad("a connector is not at its module's base height", i);
        if (s.x0 < m.x0 || s.x1 > m.x1 || s.z0 < m.z0 || s.z1 > m.z1) return bad("a connector outside its module", i);
        // the whole T corridor is walkable (sampled) - a translated player always lands on floor
        for (float fx = 0.05f; fx < 1.0f; fx += 0.1f)
            for (float fz = 0.1f; fz < 1.0f; fz += 0.2f)
                if (!Walkable(d, s.mod, s.x0 + fx * (s.x1 - s.x0), s.z0 + fz * (s.z1 - s.z0))) return bad("a connector corridor is not walkable", i);
    }
    if (nP != nM) return bad("P and M connectors differ in number", nP - nM);
    for (int t = 0; t < kMaxCTypes * kMaxBiomes; ++t) if (perP[t] != perM[t]) return bad("P and M connectors of a type and skin differ in number", t);
    for (int i = 0; i < d.nMods; ++i) {
        const Mod& m = d.mods[i];
        if (m.nP + m.nM < 1) return bad("a module without a connector", i);
        if (m.x0 < d.zx0 || m.x1 > d.zx1 || m.z0 < d.zz0 || m.z1 > d.zz1 || m.y < d.zy0 || m.ytop > d.zy1) return bad("a module outside the zone", i);
    }
    for (int i = 0; i < d.nExits; ++i) if (!(d.mods[d.exits[i].mod].flags & (MF_EXIT | MF_VAULT))) return bad("an exit in a non-exit module", i);
    // [br4] every biome has an arrival room, an exit room reachable by a plain connector of either gender, and the
    // marked connectors' promises can be kept: an EXIT / gold sign needs a partner of its type, skin and class
    for (int b = 0; b < d.nBiomes; ++b) {
        if (d.v3 && (d.arrMod[b] < 0 || d.mods[d.arrMod[b]].biome != b)) return bad("a biome without an arrival room", b);
        if (!d.v3) { d.arrMod[0] = d.arriveMod; d.arrPt[0] = d.arrive; }
    }
    for (int i = 0; i < d.nStubs; ++i) {
        const Stub& s = d.stubs[i];
        if (s.mark == MK_NONE || s.mark >= MK_FAKE_H) continue;       // [br5] a fake sign promises nothing
        int n = 0, nTrail = 0;
        for (int j = 0; j < d.nStubs; ++j) {
            const Stub& t = d.stubs[j]; const Mod& mt = d.mods[t.mod];
            if (t.g == s.g || t.ct != s.ct || t.skin != s.skin || t.mark != MK_NONE || t.mod == s.mod) continue;
            if (s.mark == MK_EXIT ? (mt.flags & MF_EXIT) != 0 : (mt.nGold > 0 && !(mt.flags & MF_VAULT))) ++n;
            if (s.mark == MK_EXIT && !(mt.flags & (MF_EXIT | MF_VAULT | MF_VAST)))            // [br5] the trail's next room: one with a real EXIT of its own
                for (int q = d.stubFirst[t.mod]; q < d.stubFirst[t.mod + 1]; ++q) if (d.stubs[q].mark == MK_EXIT) { ++nTrail; break; }
        }
        if (n < 2) return bad("a signed connector with fewer than two rooms it may lead to", i);
        if (s.mark == MK_EXIT && nTrail < 3) return bad("a real EXIT connector with fewer than three signed rooms to lead on to (the trail)", i);
    }
    if (d.par.goal_count > 0 && d.nGold < (int)d.par.goal_count) return bad("fewer gold bricks than the goal asks for", d.nGold);
    for (int i = 0; i < d.nGold; ++i) {
        const Gold& g = d.gold[i]; bool reachable = false;   // (not "near": a windows.h macro)
        for (float a = 0; a < 6.28f && !reachable; a += 0.3927f) for (float r = 0.3f; r <= 0.76f && !reachable; r += 0.15f)
            if (Walkable(d, g.mod, g.p.x + r * std::cos(a), g.p.z + r * std::sin(a))) reachable = true;
        if (!reachable) return bad("a gold brick nobody can walk up to", i);
    }
    bool exP = false, exM = false;                                 // an exit room must be reachable walking either way
    for (int i = 0; i < d.nMods; ++i) if (d.mods[i].flags & MF_EXIT) { exP |= d.mods[i].nP > 0; exM |= d.mods[i].nM > 0; }
    if (!exP || !exM) return bad("exit rooms need both a P and an M connector among them", 0);
    for (int i = 0; i < d.nStat; ++i) {
        const Pt& p = d.stat[i];
        if (!Walkable(d, p.mod, p.p.x, p.p.z)) return bad("a statue point off the floor", i);
        for (int k = d.stubFirst[p.mod]; k < d.stubFirst[p.mod + 1]; ++k) {
            const Stub& s = d.stubs[k];
            if (p.p.x > s.x0 - 0.6f && p.p.x < s.x1 + 0.6f && p.p.z > s.z0 - 1.25f && p.p.z < s.z1 + 1.25f) return bad("a statue point at a connector", i);
        }
    }
    d.ok = true;
    return true;
}

// [br4] Data is ~1.3 MB now: it is reset IN PLACE (a temporary Data() on the stack would overflow a 1 MB thread stack)
inline void ResetData(Data& d) { d.~Data(); new (&d) Data(); for (int b = 0; b < kMaxBiomes; ++b) { d.arrMod[b] = -1; d.arrPt[b] = V3{}; } }
inline bool ParseTable(Data& d, const char* const* lines, char* why, int cap) {
    ResetData(d);
    for (int i = 0; lines[i]; ++i) if (!ParseLine(d, lines[i], why, cap)) return false;
    return Validate(d, why, cap);
}
inline bool ParseText(Data& d, const char* text, char* why, int cap) {
    ResetData(d);
    char line[512];
    while (*text) {
        int n = 0;
        while (*text && *text != '\n') { if (n < 511) line[n++] = *text; ++text; }
        if (*text == '\n') ++text;
        line[n] = 0;
        if (!ParseLine(d, line, why, cap)) return false;
    }
    return Validate(d, why, cap);
}

// ---------------------------------------------------------------------------------------------- random
struct Rng {
    uint64_t s = 0x9E3779B97F4A7C15ull;
    uint64_t next() { uint64_t z = (s += 0x9E3779B97F4A7C15ull); z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull; z = (z ^ (z >> 27)) * 0x94D049BB133111EBull; return z ^ (z >> 31); }
    float u() { return (float)((next() >> 40) * (1.0 / 16777216.0)); }       // [0, 1)
    int below(int n) { return n <= 0 ? 0 : (int)(next() % (uint64_t)n); }
};

// ---------------------------------------------------------------------------------------------- state
enum PState : int { PS_OUT, PS_FALL, PS_IN, PS_SURFACE };
enum AType : int { A_TELEPORT, A_TERLOW, A_RESPAWN, A_BLINK, A_STATUE_SPAWN, A_STATUE_MOVE, A_STATUE_REMOVE,
                   A_COLOSSUS_SPAWN, A_COLOSSUS_REMOVE, A_BIKE_SPAWN, A_LOG, A_TRACE,
                   A_WORKER_SPAWN, A_WORKER_REMOVE, A_VEH_SPAWN, A_VEH_REMOVE,           // [br3w]
                   A_REWARD };                                                           // [br4] the visit's bricks are found and the vault is reached
// [br4] A_LOG codes 12 gold brick found (a = found, b = goal, i = brick), 13 all found, 14 the vault is reached, 15 the easy
// way out was held, 16 walked into another biome (a = from, b = to), 17 a reminder is shown (a = 1 stuck), 18 a signed /
// gateway connector could not keep its promise (i = connector) - must never happen
struct Action { int type; int who; V3 p; float a, b; int i; bool keepVel; bool zeroVel; };
// A_LOG codes (backrooms.h prints them): 1 landed (on ground), 2 fall timed out -> surface, 3 lost -> arrival room,
// 4 leaves, 5 held over the arrival floor (a = attempt), 6 let go (a = seconds held), 7 fell through / no landing ->
// held again (a = y, b = attempt), 8 landed (by height: the engine's on-ground never said so), 9 left during the
// fall (menu / switched off), 10 surfaced (b = seconds), 11 surfacing timed out (released anyway)
// [vcrfix] 19 the game moved him far out of the block (p = where): he counts as outside, nothing is teleported
// A_TRACE (every 0.25 s while falling): p = position, a = phase (0 held, 1 let go), b = seconds in that phase, i = attempt

struct PlayerIn {
    bool present = false, onFoot = true;
    V3 pos = {};
    bool viewOk = false; V3 cam = {}, fwd = {};      // the player's camera: position, unit forward
    bool groundBelow = false; float groundY = 0;     // the engine's ground probe [fix1: logged only - it is a broadphase
                                                     //  of type-1 OBJECTS (0x1404D0BF0), not a ray on the floor: never -56]
    bool grounded = false;                           // standing on something (the engine's 0x14066BD20)
};
struct Input {
    float dt = 0;
    PlayerIn pl[kPlayers];
    bool enable = true, statues = true, mission = false;
    bool visit[kPlayers] = {}, leave[kPlayers] = {};  // menu requests (one-shot)
    bool easy[kPlayers] = {};                         // [br4] the easy-way-out buttons are down this tick
};
struct PlayerSt {
    int st = PS_OUT;
    int mod = -1, cameBy = -1, inStub = -1, rolledFor = -1;
    V3 last = {}; bool hadLast = false;
    float timeIn = 0, lost = 0, surfT = 0, fallT = 0, glitch = 0, sinceTp = 99;
    int loops = 0, walked = 0, sinceSpecial = 0, specials = 0, pocketLen = 0, dest = -1, mercyDoors = 0;
    int owed = -1;                                   // [br3] a special family rolled where no connector of the right type led to it
    V3 hold = {};
    // [fix1] the arrival: phase 0 = held above the floor, 1 = let go; attempts; the black that covers it
    int fallPhase = 0, tries = 0; float phaseT = 0, holdNeed = 0, stillT = 0, darkT = 0, traceT = 0;
    // [br4] crossings since a vast room; seconds in this room; the reminder; the easy way out; the on-screen arrow
    bool wantOut = false;                            // [br5] Leave / easy way out asked for, not yet carried out
    int trail = 0;                                   // [br5] real EXIT doors taken in a row
    int cameFromMark = 0;                            // [br5] the mark of the connector he is crossing (set at the plane)
    int sinceVast = 0, sinceGold = 0; float stuckT = 0, nextRemind = 0, showT = 0, easyT = 0; bool stuckSaid = false;
    float awayT = 0;                                 // [vcrfix] seconds well clear of the block while still 'inside'
    int arrowKind = 0, arrowDir = 0;                 // kind 0 none, 1 a gold brick, 2 an exit; dir 0..7 (0 ahead, 2 right, 4 behind, 6 left), 8 = here
};
struct Statue { bool alive = false; int pt = -1, mod = -1; float unobs = 0, timer = 0; V3 p = {}; float yaw = 0; };
struct State {
    Rng rng; uint64_t seed = 0; unsigned visit = 0; bool seeded = false;
    int link[kMaxStubs]; float hold[kMaxStubs];
    PlayerSt pl[kPlayers];
    Statue stat[kMaxStatues]; float statueAway = 0;
    bool colOut[kMaxCol] = {}; float colAway[kMaxCol] = {};
    bool bikeDone = false; bool terlowHeld = false;
    float zone[kPlayers] = {};                       // 0..1 fade of the effects, per player's view
    int nAct = 0; Action act[kMaxActions];
    unsigned long rolls = 0, crossings = 0, relinks = 0;
    unsigned long seenAt[kMaxMods] = {};             // [br3] per visit: the crossing count when each module was last entered (0 = not yet)
    bool workOut[kMaxWork] = {}; float workAway[kMaxWork] = {}; bool vehOut[kMaxVeh] = {}; float vehAway[kMaxVeh] = {};   // [br3w]
    // [br4] the visit: where it started, the gold bricks found (shared by both players), the reward
    int startBiome = 0; bool found[kMaxGold] = {}; int nFound = 0; bool goalDone = false, rewarded = false; float flashT = 0;
    unsigned long lies = 0, follows = 0, forced = 0, biomeCross = 0;
    // [br5] this visit's trail length; the brick row's pop-up; ONE line of text at a time (1 FOUND! 2 ALL FOUND 3 the vault)
    int trailNeed = 4; float rowT = 0; int msg = 0; float msgT = 0;
    State() { for (int i = 0; i < kMaxStubs; ++i) { link[i] = -1; hold[i] = 0; } }
};

inline void Push(State& s, int type, int who, V3 p = {}, float a = 0, float b = 0, int i = 0, bool keepVel = false, bool zeroVel = false) {
    if (s.nAct < kMaxActions) s.act[s.nAct++] = Action{type, who, p, a, b, i, keepVel, zeroVel};
}
// [fix1] the arrival's hold point: on the arrival axis, fall_hover above the floor (the table's height)
// [br4] the arrival room of the biome this visit started in
inline int ArrMod(const Data& d, const State& s) { const int b = s.startBiome; return (b >= 0 && b < kMaxBiomes && d.arrMod[b] >= 0) ? d.arrMod[b] : d.arriveMod; }
inline V3 ArrPt(const Data& d, const State& s) { const int b = s.startBiome; return (b >= 0 && b < kMaxBiomes && d.arrMod[b] >= 0) ? d.arrPt[b] : d.arrive; }
inline V3 HoverPoint(const Data& d, const State& s) { const V3 a = ArrPt(d, s); return V3{a.x, a.y + d.par.fall_hover, a.z}; }
inline void HoldAgain(const Data& d, State& s, int k, float need) {
    PlayerSt& pl = s.pl[k];
    pl.fallPhase = 0; pl.phaseT = 0; pl.holdNeed = need; pl.stillT = 0; pl.sinceTp = 0;
    Push(s, A_TELEPORT, k, HoverPoint(d, s), 0, 0, 0, false, true);
}

// a fresh perfect matching P <-> M
inline void Shuffle(const Data& d, State& s) {
    // [br3] a perfect matching within every connector type; [br4] and skin (Validate: as many P as M of each)
    static int P[kMaxStubs], M[kMaxStubs];
    for (int t = 0; t < kMaxCTypes; ++t) for (int sk = 0; sk < d.nBiomes; ++sk) {
        int nP = 0, nM = 0;
        for (int i = 0; i < d.nStubs; ++i) if (d.stubs[i].ct == t && d.stubs[i].skin == sk) (d.stubs[i].g == 'P' ? P[nP++] : M[nM++]) = i;
        if (!nP && !nM) continue;
        for (int i = nM - 1; i > 0; --i) { int j = s.rng.below(i + 1); int q = M[i]; M[i] = M[j]; M[j] = q; }
        for (int i = 0; i < nP && i < nM; ++i) { s.link[P[i]] = M[i]; s.link[M[i]] = P[i]; }
    }
    for (int i = 0; i < d.nStubs; ++i) s.hold[i] = 0;
}
inline void Reseed(const Data& d, State& s, uint64_t seed) {
    s.seed = seed; s.rng.s = seed * 0x2545F4914F6CDD1Dull + 0x1234567ull; s.seeded = true; ++s.visit;
    for (int i = 0; i < kMaxMods; ++i) s.seenAt[i] = 0;
    for (int k = 0; k < kPlayers; ++k) s.pl[k].owed = -1;
    Shuffle(d, s);
    s.bikeDone = false;
    // [br4] a new visit: nothing found yet; where it starts (weights biome_w<n>, only biomes with an arrival room)
    for (int i = 0; i < kMaxGold; ++i) s.found[i] = false;
    s.nFound = 0; s.goalDone = false; s.rewarded = false; s.flashT = 0;
    s.rowT = 0; s.msg = 0; s.msgT = 0;                   // [br5]
    { int lo = (int)d.par.trail_min, hi = (int)d.par.trail_max; if (lo < 1) lo = 1; if (hi < lo) hi = lo; s.trailNeed = lo + s.rng.below(hi - lo + 1); }
    const float bw[kMaxBiomes] = { d.par.biome_w0, d.par.biome_w1, d.par.biome_w2, d.par.biome_w3, d.par.biome_w4, d.par.biome_w5, d.par.biome_w6, d.par.biome_w7 };
    float tot = 0; for (int b = 0; b < d.nBiomes; ++b) if (d.arrMod[b] >= 0 && bw[b] > 0) tot += bw[b];
    s.startBiome = 0;
    if (tot > 0) { float r = s.rng.u() * tot; for (int b = 0; b < d.nBiomes; ++b) { if (d.arrMod[b] < 0 || bw[b] <= 0) continue; s.startBiome = b; r -= bw[b]; if (r <= 0) break; } }
    else for (int b = 0; b < d.nBiomes; ++b) if (d.arrMod[b] >= 0) { s.startBiome = b; break; }
}
inline bool MatchingOk(const Data& d, const State& s) {
    for (int i = 0; i < d.nStubs; ++i) {
        int j = s.link[i];
        if (j < 0 || j >= d.nStubs || s.link[j] != i || d.stubs[i].g == d.stubs[j].g || d.stubs[i].ct != d.stubs[j].ct || d.stubs[i].skin != d.stubs[j].skin) return false;
    }
    return true;
}

inline int ModAt(const Data& d, const V3& p, int hint) {
    auto in = [&](int i) { const Mod& m = d.mods[i]; return p.x >= m.x0 && p.x <= m.x1 && p.z >= m.z0 && p.z <= m.z1 && p.y >= m.y - 4.0f && p.y <= m.ytop + 1.0f; };
    if (hint >= 0 && hint < d.nMods && in(hint)) return hint;
    for (int i = 0; i < d.nMods; ++i) if (in(i)) return i;
    return -1;
}
inline int StubAt(const Data& d, int mod, const V3& p, float pad = 0) {
    if (mod < 0) return -1;
    for (int i = d.stubFirst[mod]; i < d.stubFirst[mod + 1]; ++i) {
        const Stub& t = d.stubs[i];
        const float r = StubRise(d, t);                  // [br3] a climbing T: anywhere between its two floors
        if (p.x >= t.x0 - pad && p.x <= t.x1 + pad && p.z >= t.z0 - pad && p.z <= t.z1 + pad && p.y > t.y + std::fmin(0.0f, r) - 1.2f &&
            p.y < t.y + std::fmax(0.0f, r) + 1.2f) return i;
    }
    return -1;
}
// [vcrfix] well clear of the block: more than 15 u outside it across, or 10 u above its roof (the surface). Falling
// through a floor stays NEAR (the glitch rescue keeps that case).
inline bool FarFromZone(const Data& d, const V3& p) {
    return p.x < d.zx0 - 15.0f || p.x > d.zx1 + 15.0f || p.z < d.zz0 - 15.0f || p.z > d.zz1 + 15.0f || p.y > d.zy1 + 10.0f;
}
inline bool InZone(const Data& d, const V3& p) { return p.x >= d.zx0 && p.x <= d.zx1 && p.y >= d.zy0 && p.y <= d.zy1 && p.z >= d.zz0 && p.z <= d.zz1; }

// is a connector in use: a player in it or next to it (its legs), or it was used a moment ago
inline bool Busy(const Data& d, const State& s, const Input& in, int stub, int except) {
    if (s.hold[stub] > 0) return true;
    const Stub& t = d.stubs[stub];
    for (int k = 0; k < kPlayers; ++k) {
        if (k == except || !in.pl[k].present || s.pl[k].st != PS_IN) continue;
        const V3& p = in.pl[k].pos;
        const float r = StubRise(d, t);
        if (p.x >= t.x0 - 1.0f && p.x <= t.x1 + 1.0f && p.z >= t.z0 - 2.0f && p.z <= t.z1 + 2.0f && p.y > t.y + std::fmin(0.0f, r) - 2.0f &&
            p.y < t.y + std::fmax(0.0f, r) + 2.0f) return true;
    }
    return false;
}

inline bool IsSpecial(const Mod& m) { return !IsYellow(m.fam); }

// [br4] THE LINK RULES.  May connector s0 lead to connector t now?  One type, one skin, and the promises:
//   a lit EXIT sign      -> an exit room                      a gold brick over the door -> a room with a gold brick
//   a gateway (a connector in another biome's skin, or a way out of a vast room / the vault) -> that biome
//   a plain connector    -> its own biome, or a vast room (the hubs carry a mouth in every skin)
//   nobody ARRIVES through a signed connector or a gateway mouth (the way back would break its promise)
//   the vault only once the visit's bricks are found
inline bool HasUnfound(const Data& d, const State& s, int mod) {
    for (int i = 0; i < d.nGold; ++i) if (d.gold[i].mod == mod && !s.found[i]) return true;
    return false;
}
inline bool HasRealExit(const Data& d, int mod) {      // [br5]
    for (int i = d.stubFirst[mod]; i < d.stubFirst[mod + 1]; ++i) if (d.stubs[i].mark == MK_EXIT) return true;
    return false;
}
// [br5] how many real EXIT doors in a row this player needs now (the visit's roll; shorter after the mercy time)
inline int TrailNeed(const Data& d, const State& s, const PlayerSt& pl) {
    int n = s.trailNeed;
    if (pl.timeIn > d.par.mercy_min * 60.0f && (int)d.par.mercy_trail < n) n = (int)d.par.mercy_trail;
    return n < 1 ? 1 : n;
}
inline bool LinkValid(const Data& d, const State& s, int s0, int t) {
    if (s0 < 0 || t < 0 || s0 == t) return false;
    const Stub& a = d.stubs[s0]; const Stub& b = d.stubs[t];
    if (a.g == b.g || a.ct != b.ct || a.skin != b.skin) return false;
    const Mod& ma = d.mods[a.mod]; const Mod& mb = d.mods[b.mod];
    if (mb.flags & MF_VAULT) return s.goalDone && a.mod != b.mod;
    if (b.mark != MK_NONE) return false;
    // [br5] a real EXIT leads to the exit room OR to another body room that has a real EXIT of its own (the trail can
    // always be followed); nothing else ever leads to an exit room; a fake sign is a plain door
    if (a.mark == MK_EXIT) return (mb.flags & MF_EXIT) != 0 || (HasRealExit(d, b.mod) && a.mod != b.mod && b.skin == mb.biome && !(mb.flags & MF_VAST));
    if (mb.flags & MF_EXIT) return false;
    if (a.mark == MK_GOLD) return mb.nGold > 0 && a.mod != b.mod && (b.skin == mb.biome || (mb.flags & MF_VAST));
    if (mb.flags & MF_VAST) return true;
    if (b.skin != mb.biome) return false;
    if (ma.flags & (MF_VAST | MF_VAULT)) return true;        // out of a hub: into the biome of the skin
    return true;                                             // (a.skin == b.skin == mb.biome: the skin's biome)
}

// choose the module the connector `s0` of player k should lead to now; -1 = leave the link as it is
inline int PickTarget(const Data& d, State& s, const Input& in, int k, int s0, bool must = false) {
    const Params& P = d.par; PlayerSt& pl = s.pl[k];
    const Stub& a = d.stubs[s0]; const Mod& cur = d.mods[a.mod];
    auto hasFree = [&](int mi) {                         // a free connector module mi may be entered by, from s0
        for (int i = d.stubFirst[mi]; i < d.stubFirst[mi + 1]; ++i) {
            if (i == s.link[s0] || !LinkValid(d, s, s0, i)) continue;
            if (s.hold[i] > 0 || Busy(d, s, in, i, -1)) continue;
            if (!must && Busy(d, s, in, s.link[i], -1)) continue;
            return true;
        }
        return false;
    };
    static float w[kMaxMods]; float tot = 0;
    // the reward room: once the bricks are found every door that can lead there does
    if (s.goalDone && !s.rewarded && d.vaultMod >= 0 && a.mod != d.vaultMod && hasFree(d.vaultMod)) return d.vaultMod;
    if (a.mark == MK_EXIT || a.mark == MK_GOLD) {        // a signed door: its class; a brick not yet found first
        // [br5] a real EXIT: this is door (trail + 1) of the trail - the last one opens on the exit room, the others on
        // a room with another real EXIT (one not seen lately first)
        const bool last = a.mark == MK_EXIT && pl.trail + 1 >= TrailNeed(d, s, pl);
        for (int pass = 0; pass < 2 && tot <= 0; ++pass) {
            tot = 0;
            for (int i = 0; i < d.nMods; ++i) {
                float wi = hasFree(i) ? 1.0f : 0.0f;
                if (wi > 0 && a.mark == MK_GOLD && !HasUnfound(d, s, i)) wi = 0.03f;
                if (wi > 0 && (d.mods[i].flags & MF_VAULT)) wi = 0;
                if (wi > 0 && a.mark == MK_EXIT && pass == 0 && (((d.mods[i].flags & MF_EXIT) != 0) != last)) wi = 0;   // (pass 1: whichever is free)
                if (wi > 0 && a.mark == MK_EXIT && s.seenAt[i]) wi = 0.2f;
                w[i] = wi; tot += wi;
            }
        }
        if (tot <= 0) return -1;
        float r = s.rng.u() * tot;
        for (int i = 0; i < d.nMods; ++i) { if (w[i] <= 0) continue; r -= w[i]; if (r <= 0) return i; }
        for (int i = d.nMods - 1; i >= 0; --i) if (w[i] > 0) return i;
        return -1;
    }
    const int tb = a.skin;                               // the biome this door leads into
    bool mercy = pl.timeIn > P.mercy_min * 60.0f;
    bool inPocket = IsSpecial(cur) && !(cur.flags & (MF_VAST | MF_VAULT));
    int mode = 0;                                        // 0 a body room, 1 same pocket family, 2 a special, 3 an exit room
    int specialKind = -1;                                // family wanted for mode 2
    int famVast = -1; for (int i = 0; i < d.nMods; ++i) if (d.mods[i].flags & MF_VAST) { famVast = d.mods[i].fam; break; }
    if (false) mode = 3;                                 // [br5] (br4: after the mercy time every 2nd door was an exit room - now only the trail leads out)
    else if (P.goal_count > 0 && P.goal_every > 0 && !s.goalDone && !IsSpecial(cur) && pl.sinceGold >= (int)P.goal_every) mode = 4;   // [br4] a brick is due
    else if (famVast >= 0 && !IsSpecial(cur) && pl.sinceVast >= (int)P.vast_every && pl.sinceSpecial >= (int)P.cooldown) { mode = 2; specialKind = famVast; }   // [br4] a vast room is due
    else if (pl.owed >= 0 && !inPocket) { mode = 2; specialKind = pl.owed; }   // [br3] a special still owed from an earlier door
    else if (inPocket) {
        if (!(cur.flags & MF_MASSIVE) && pl.pocketLen < (int)P.pocket_max && s.rng.u() < P.pocket_stay) mode = 1;
    } else {
        int need = pl.specials == 0 ? (int)P.first_special : (int)P.cooldown;
        if (pl.sinceSpecial >= need) {
            float c = P.special_base + P.special_gain * (float)(pl.sinceSpecial - need + 1);
            if (c > P.special_max) c = P.special_max;
            if (s.rng.u() < c) {
                mode = 2;
                // [br3] which kind: its share, x fresh_family while the family still has a module not seen this visit;
                // [br4] only the families that live in the biome behind this door (vast rooms: any)
                float kw[F_THEME0 + kMaxThemes] = {}; float kt = 0;
                const int nk = F_THEME0 + d.nThemes;
                for (int f = F_POOL; f < nk; ++f) {
                    if (f == F_EXIT || f == F_ARRIVAL) continue;
                    const float rest = 1.0f - P.massive_share - P.pool_share - P.rail_share - P.vast_share;
                    float sh = f == F_MASSIVE ? P.massive_share : f == F_POOL ? P.pool_share : f == F_RAIL ? P.rail_share : f == famVast ? P.vast_share :
                               (d.nThemes > 0 && f >= F_THEME0 ? rest / d.nThemes : 0.0f);
                    bool fresh = false, any = false;
                    for (int i = 0; i < d.nMods; ++i) {
                        const Mod& m = d.mods[i];
                        if (m.fam != f || (m.flags & MF_VAULT) || !((m.flags & MF_VAST) || m.biome == tb)) continue;
                        any = true; if (!s.seenAt[i]) fresh = true;
                    }
                    kw[f] = any ? sh * (fresh ? P.fresh_family : 1.0f) : 0.0f; kt += kw[f];
                }
                if (kt <= 0) mode = 0;
                else {
                    float r = s.rng.u() * kt; specialKind = F_POOL;
                    for (int f = F_POOL; f < nk; ++f) { if (kw[f] <= 0) continue; specialKind = f; r -= kw[f]; if (r <= 0) break; }
                }
            }
        }
    }
    for (int pass = 0; pass < 2; ++pass) {
        tot = 0;
        for (int i = 0; i < d.nMods; ++i) {
            const Mod& m = d.mods[i]; float wi = 0;
            if (m.flags & MF_VAULT) wi = 0;
            else if (mode == 3) wi = (m.flags & MF_EXIT) ? 1.0f : 0.0f;
            else if (mode == 4) wi = (m.nGold > 0 && HasUnfound(d, s, i)) ? 1.0f : 0.0f;
            else if (mode == 1) wi = (m.fam == cur.fam && i != a.mod) ? 1.0f : 0.0f;
            else if (mode == 2) wi = (m.fam == specialKind) ? 1.0f : 0.0f;
            else {
                if (m.fam == F_L0) wi = P.w_l0 * (HasRealExit(d, i) ? P.sign_boost : 1.0f);   // [br5] signs are found a little sooner
                else if (m.fam == F_ODD) wi = P.w_odd;
                else if (m.fam == F_ARRIVAL) wi = P.w_l0 * 0.5f;
                else if (m.fam == F_EXIT) wi = mercy ? P.mercy_weight : (pl.walked >= (int)P.exit_after ? P.exit_weight : 0.0f);
                if (i == a.mod) wi *= 0.03f;             // the same room again: rare, but it happens
            }
            if (wi > 0 && mode != 4 && mode != 3 && P.goal_count > 0 && !s.goalDone && m.nGold > 0 && HasUnfound(d, s, i)) wi *= P.goal_boost;   // [br4]
            if (wi > 0 && !hasFree(i)) wi = 0;
            // [br3] recently seen modules wait their turn: specials until most others were seen, body rooms a little
            if (wi > 0 && s.seenAt[i]) {
                const float age = (float)(s.crossings - s.seenAt[i]);
                const bool sp = IsSpecial(m);
                const float base = sp ? P.seen_weight : P.l0_seen_weight, span = sp ? P.seen_age : P.l0_seen_age;
                const float f = base + (1.0f - base) * (age / (span > 1 ? span : 1));
                wi *= f < 1.0f ? f : 1.0f;
            }
            w[i] = wi; tot += wi;
        }
        if (tot > 0) { if (mode == 2 && specialKind != famVast) pl.owed = -1; break; }
        if (mode == 0) {                                  // nothing by the weights: with a promise to keep, anything valid
            if (!must) return -1;
            for (int i = 0; i < d.nMods; ++i) if (!(d.mods[i].flags & MF_VAULT) && hasFree(i)) return i;
            return -1;
        }
        if (mode == 2 && pl.owed < 0 && specialKind != famVast) pl.owed = specialKind;   // [br3] owed to a later door
        mode = 0;                                        // nothing of the wanted kind is free: an ordinary body room
    }
    float r = s.rng.u() * tot;
    for (int i = 0; i < d.nMods; ++i) { if (w[i] <= 0) continue; r -= w[i]; if (r <= 0) return i; }
    for (int i = d.nMods - 1; i >= 0; --i) if (w[i] > 0) return i;
    return -1;
}

// re-link connector s0 (player k is walking into it).  Returns true if the link changed.
// [br4] `must`: the link as it stands would break a promise (LinkValid) - it is re-linked even if the far side is in use
inline bool Roll(const Data& d, State& s, const Input& in, int k, int s0, bool must = false) {
    ++s.rolls;
    int old = s.link[s0];
    if (s.hold[s0] > 0 || s.hold[old] > 0) return false;
    if (!must && (Busy(d, s, in, s0, k) || Busy(d, s, in, old, -1))) return false;       // the partner (or anyone) is using this link
    int tm = PickTarget(d, s, in, k, s0, must);
    if (tm < 0) return false;
    int cand[48], n = 0, calm[48], nc = 0;                   // calm: its present partner is not in use by anyone
    for (int i = d.stubFirst[tm]; i < d.stubFirst[tm + 1] && n < 48; ++i) {
        if (i == old || !LinkValid(d, s, s0, i)) continue;
        if (s.hold[i] > 0 || Busy(d, s, in, i, -1)) continue;
        const bool pb = Busy(d, s, in, s.link[i], -1);
        if (!must && pb) continue;
        cand[n++] = i; if (!pb) calm[nc++] = i;
    }
    if (!n) return false;
    int t = nc ? calm[s.rng.below(nc)] : cand[s.rng.below(n)], tOld = s.link[t];
    // swap: s0 <-> t, and the two that lose their partners get each other (one is a P, one an M; same type and skin)
    s.link[s0] = t; s.link[t] = s0; s.link[old] = tOld; s.link[tOld] = old;
    ++s.relinks; if (must) ++s.forced;
    return true;
}

inline void Arrived(const Data& d, State& s, int k, int mod) {      // counters when player k walks into a new module
    PlayerSt& pl = s.pl[k];
    const Mod& m = d.mods[mod];
    ++pl.walked;
    s.seenAt[mod] = s.crossings > 0 ? s.crossings : 1;   // [br3] this visit's memory
    if (m.flags & MF_EXIT) pl.mercyDoors = 0; else if (pl.timeIn > d.par.mercy_min * 60.0f) ++pl.mercyDoors;
    if (IsSpecial(m)) {
        if (pl.mod >= 0 && d.mods[pl.mod].fam == m.fam) ++pl.pocketLen; else { pl.pocketLen = 1; ++pl.specials; }
        pl.sinceSpecial = 0;
        pl.owed = -1;                                    // [br4] a special met by another way (a gold door, a vast room) settles the one owed
    } else { ++pl.sinceSpecial; pl.pocketLen = 0; }
    if (!s.bikeDone && m.fam == F_L0 && s.rng.u() < d.par.bike_chance) {
        for (int i = 0; i < d.nBikes; ++i) if (d.bikes[i].mod == mod) { s.bikeDone = true; Push(s, A_BIKE_SPAWN, k, d.bikes[i].p, 0, 0, i); break; }
    }
    // [br4]
    if (m.flags & MF_VAST) pl.sinceVast = 0; else ++pl.sinceVast;
    ++pl.sinceGold;
    // [br5] the trail: a real EXIT door taken = one more; any other door (a fake sign too) = start over
    if (pl.cameFromMark == MK_EXIT) ++pl.trail; else pl.trail = 0;
    pl.stuckT = 0; pl.stuckSaid = false;
    if (pl.mod >= 0 && d.mods[pl.mod].biome != m.biome && !(m.flags & (MF_VAST | MF_VAULT)) && !(d.mods[pl.mod].flags & (MF_VAST | MF_VAULT))) {
        ++s.biomeCross; Push(s, A_LOG, k, {}, (float)d.mods[pl.mod].biome, (float)m.biome, 16);
    }
    if ((m.flags & MF_VAULT) && s.goalDone && !s.rewarded) { s.rewarded = true; s.flashT = 6.0f; s.msg = 3; s.msgT = d.par.msg_show + 2.0f; s.rowT = d.par.msg_show + 2.0f; Push(s, A_REWARD, k); Push(s, A_LOG, k, {}, 0, 0, 14); }
}

// [br4] the direction of q on a player's screen: 0 ahead, 2 right, 4 behind, 6 left (x east, z north: a left-handed
// frame - right of a viewer facing f is (f.z, 0, -f.x)); 8 = on top of it / no camera
inline int ScreenDir(const PlayerIn& v, const V3& q) {
    float dx = q.x - v.pos.x, dz = q.z - v.pos.z;
    if (!v.viewOk || dx * dx + dz * dz < 0.36f) return 8;
    float fx = v.fwd.x, fz = v.fwd.z; float fl = std::sqrt(fx * fx + fz * fz);
    if (fl < 0.05f) return 8;
    fx /= fl; fz /= fl;
    const float ahead = dx * fx + dz * fz, right = dx * fz - dz * fx;
    float a = std::atan2(right, ahead) * 57.29578f; if (a < 0) a += 360.0f;
    return ((int)std::floor((a + 22.5f) / 45.0f)) % 8;
}

// conservative "could this view see point q": inside a wide cone, or very near; no credit for walls
inline bool Sees(const PlayerIn& v, const V3& q, float cosHalf = 0.17f /* ~80 deg half angle */) {
    if (!v.viewOk) return true;
    V3 dq = {q.x - v.cam.x, q.y - v.cam.y, q.z - v.cam.z};
    float L = std::sqrt(dq.x * dq.x + dq.y * dq.y + dq.z * dq.z);
    if (L < 2.5f) return true;
    float c = (dq.x * v.fwd.x + dq.y * v.fwd.y + dq.z * v.fwd.z) / L;
    return c > cosHalf;
}
inline bool Observed(const Data& d, const State& s, const Input& in, const V3& q, int mod) {
    (void)d;
    for (int k = 0; k < kPlayers; ++k) {
        if (!in.pl[k].present) continue;
        if (s.pl[k].st != PS_IN) continue;
        if (s.pl[k].mod != mod) continue;            // sealed pockets: another module cannot be seen at all
        V3 qq = q; qq.y += 0.3f;
        if (Sees(in.pl[k], qq)) return true;
    }
    return false;
}
inline float NearestPlayer(const State& s, const Input& in, const V3& q, int* who = nullptr) {
    float best = 1e9f;
    for (int k = 0; k < kPlayers; ++k) {
        if (!in.pl[k].present || s.pl[k].st == PS_OUT) continue;
        float dd = Dist(in.pl[k].pos, q);
        if (dd < best) { best = dd; if (who) *who = k; }
    }
    return best;
}
inline float YawTo(const V3& from, const V3& to) { return std::atan2(to.x - from.x, to.z - from.z) * 57.29578f; }

inline void StatueStep(const Data& d, State& s, const Input& in) {
    const Params& P = d.par;
    bool anyIn = false, settled = true;
    for (int k = 0; k < kPlayers; ++k) if (in.pl[k].present && s.pl[k].st == PS_IN) { anyIn = true; if (s.pl[k].sinceTp < 4.0f) settled = false; }
    bool on = in.enable && in.statues && !in.mission && anyIn;
    int alive = 0;
    for (int i = 0; i < kMaxStatues; ++i) {
        Statue& t = s.stat[i];
        if (!t.alive) continue;
        bool modHasPlayer = false;
        for (int k = 0; k < kPlayers; ++k) if (in.pl[k].present && s.pl[k].st == PS_IN && s.pl[k].mod == t.mod) modHasPlayer = true;
        bool obs = Observed(d, s, in, t.p, t.mod);
        t.unobs = obs ? 0 : t.unobs + in.dt;
        if (!on || (!modHasPlayer && (t.timer -= in.dt) < -20.0f)) {          // nobody there any more (or switched off): gone, unseen
            if (!obs && settled) { t.alive = false; Push(s, A_STATUE_REMOVE, i); continue; }
        }
        ++alive;
        if (!modHasPlayer || !settled) continue;
        t.timer -= in.dt;
        if (t.timer > 0 || t.unobs < 1.5f) continue;
        t.timer = P.statue_period * (0.5f + s.rng.u());
        float r = s.rng.u();
        if (r < 0.10f) { t.alive = false; --alive; Push(s, A_STATUE_REMOVE, i); continue; }      // sometimes it is simply gone
        // a modest step toward the nearest player, between the generated points
        int who = -1; float cur = NearestPlayer(s, in, t.p, &who);
        int best = -1; float bestD = cur;
        for (int q = 0; q < d.nStat; ++q) {
            const Pt& c = d.stat[q];
            if (c.mod != t.mod || q == t.pt) continue;
            bool taken = false;
            for (int j = 0; j < kMaxStatues; ++j) if (j != i && s.stat[j].alive && s.stat[j].pt == q) taken = true;
            if (taken || Dist(c.p, t.p) > P.statue_step) continue;
            float dn = NearestPlayer(s, in, c.p);
            if (dn < P.statue_min_dist || Observed(d, s, in, c.p, c.mod)) continue;
            if (dn < bestD) { bestD = dn; best = q; }
        }
        if (best >= 0) {
            t.pt = best; t.p = d.stat[best].p; t.unobs = 0;
            if (who >= 0) t.yaw = YawTo(t.p, in.pl[who].pos);
            Push(s, A_STATUE_MOVE, i, t.p, t.yaw);
        } else if (who >= 0 && cur >= P.statue_min_dist) {                       // it only turns to face you
            t.yaw = YawTo(t.p, in.pl[who].pos); Push(s, A_STATUE_MOVE, i, t.p, t.yaw);
        }
    }
    if (!on || !settled) return;
    // a new one, now and then, in a module a player is in: at a point nobody can see and nobody is near
    s.statueAway -= in.dt;
    if (s.statueAway > 0 || alive >= (int)P.statue_cap || alive >= kMaxStatues) return;
    s.statueAway = 20.0f + 40.0f * s.rng.u();
    int k0 = s.rng.below(kPlayers);
    for (int kk = 0; kk < kPlayers; ++kk) {
        int k = (k0 + kk) % kPlayers;
        if (!in.pl[k].present || s.pl[k].st != PS_IN || s.pl[k].mod < 0) continue;
        int cand[16], n = 0;
        for (int q = 0; q < d.nStat && n < 16; ++q) {
            const Pt& c = d.stat[q];
            if (c.mod != s.pl[k].mod) continue;
            bool taken = false;
            for (int j = 0; j < kMaxStatues; ++j) if (s.stat[j].alive && s.stat[j].pt == q) taken = true;
            if (taken || NearestPlayer(s, in, c.p) < P.statue_min_dist + 1.0f || Observed(d, s, in, c.p, c.mod)) continue;
            cand[n++] = q;
        }
        if (!n) continue;
        for (int i = 0; i < kMaxStatues; ++i) if (!s.stat[i].alive) {
            Statue& t = s.stat[i]; int q = cand[s.rng.below(n)];
            t.alive = true; t.pt = q; t.mod = d.stat[q].mod; t.p = d.stat[q].p; t.unobs = 0; t.timer = P.statue_period * (0.5f + s.rng.u());
            t.yaw = YawTo(t.p, in.pl[k].pos);
            Push(s, A_STATUE_SPAWN, i, t.p, t.yaw);
            return;
        }
    }
}

inline void BeginVisit(const Data& d, State& s, int k, uint64_t entropy) {
    bool othersIn = false;
    for (int j = 0; j < kPlayers; ++j) if (j != k && s.pl[j].st != PS_OUT) othersIn = true;
    if (!othersIn) Reseed(d, s, entropy);                // a new maze for every visit (the partner joins the same one)
    PlayerSt& pl = s.pl[k];
    pl = PlayerSt(); pl.st = PS_FALL;
}

// one tick.  `entropy` = any changing number (tick count, performance counter): only used when a visit begins.
inline void Step(const Data& d, State& s, const Input& in, uint64_t entropy) {
    s.nAct = 0;
    if (!d.ok) return;
    const Params& P = d.par;
    float dt = in.dt; if (!(dt > 0)) dt = 0; if (dt > 0.25f) dt = 0.25f;
    for (int i = 0; i < d.nStubs; ++i) if (s.hold[i] > 0) { s.hold[i] -= dt; if (s.hold[i] < 0) s.hold[i] = 0; }
    if (s.flashT > 0) { s.flashT -= dt; if (s.flashT < 0) s.flashT = 0; }
    if (s.rowT > 0) { s.rowT -= dt; if (s.rowT < 0) s.rowT = 0; }                          // [br5]
    if (s.msgT > 0) { s.msgT -= dt; if (s.msgT <= 0) { s.msgT = 0; s.msg = 0; } }
    // [br4] the easy way out: a player inside holds the buttons for easy_out_hold seconds -> BOTH players leave (the
    // menu's "Leave the Backrooms", without the menu)
    bool easyAll = false;
    for (int k = 0; k < kPlayers; ++k) {
        PlayerSt& pl = s.pl[k];
        const bool held = in.pl[k].present && in.easy[k] && (pl.st == PS_IN || pl.st == PS_FALL) && P.easy_out_hold > 0;
        pl.easyT = held ? pl.easyT + dt : 0.0f;
        if (held && pl.easyT >= P.easy_out_hold) { easyAll = true; pl.easyT = 0; Push(s, A_LOG, k, {}, 0, 0, 15); }
    }
    for (int k = 0; k < kPlayers; ++k) {
        const PlayerIn& pi = in.pl[k]; PlayerSt& pl = s.pl[k];
        if (!pi.present) { if (pl.st != PS_OUT) pl = PlayerSt(); continue; }
        pl.sinceTp += dt; if (pl.glitch > 0) { pl.glitch -= dt; if (pl.glitch < 0) pl.glitch = 0; }
        if (pl.darkT > 0) { pl.darkT -= dt; if (pl.darkT < 0) pl.darkT = 0; }
        const V3 p = pi.pos;
        // [vcrfix 11-24] the GAME took this player away (the pause menu's Return to Police Station, a respawn, a new
        // world) while the model still had him inside or falling: well clear of the block for 1 s -> he is out. No
        // teleport back (the old 'lost' rescue did that), the camcorder fades, the kill height comes back below.
        if ((pl.st == PS_IN || pl.st == PS_FALL) && pl.sinceTp > 1.0f && FarFromZone(d, p)) {
            pl.awayT += dt;
            if (pl.awayT >= 1.0f) { Push(s, A_LOG, k, p, 0, 0, 19); pl = PlayerSt(); pl.last = p; pl.hadLast = true; continue; }
        } else pl.awayT = 0;
        switch (pl.st) {
        case PS_OUT: {
            bool go = in.enable && !in.mission && in.visit[k];
            if (in.enable && !in.mission && d.hasEntry && pi.onFoot) {
                // the village door's shaft: on the axis, falling, below the threshold
                if (p.x >= d.ex0 && p.x <= d.ex1 && p.z >= d.ez0 && p.z <= d.ez1 && p.y < d.eyTop && p.y > d.eyBot) go = true;
            }
            if (go && pi.onFoot) {
                // [fix1] straight to the hold point over the arrival floor, behind black (the old fall loop up the
                // 36 u shaft waited for the engine's ground probe to report -56: it never can - Notes 54 fix1)
                BeginVisit(d, s, k, entropy + (uint64_t)k * 7919u);
                Push(s, A_TERLOW, k, {}, P.terlow);
                HoldAgain(d, s, k, P.fall_hold);
                Push(s, A_LOG, k, HoverPoint(d, s), 0, 0, 5);
                pl.hold = HoverPoint(d, s); pl.glitch = 1.0f;
            }
            break; }
        case PS_FALL: {
            pl.fallT += dt; pl.phaseT += dt;
            const V3 arr = ArrPt(d, s);
            const float floorY = arr.y;
            const V3 hov = HoverPoint(d, s);
            if (in.leave[k] || easyAll || !in.enable) {                                        // the menu's Leave / switched off: out, now
                pl.st = PS_SURFACE; pl.dest = 0; pl.surfT = 0; pl.hold = {d.dest[0].p.x, d.dest[0].p.y + 0.8f, d.dest[0].p.z};
                Push(s, A_TELEPORT, k, pl.hold, d.dest[0].heading, 1, 0, false, true); Push(s, A_RESPAWN, k, d.dest[0].p); pl.sinceTp = 0;
                Push(s, A_LOG, k, p, 0, 0, 9);
                break;
            }
            if (pl.fallT > P.fall_timeout) {                                        // never landed: give up, back to a surface point
                pl.st = PS_SURFACE; pl.dest = 0; pl.surfT = 0; pl.hold = {d.dest[0].p.x, d.dest[0].p.y + 0.8f, d.dest[0].p.z};
                Push(s, A_TELEPORT, k, pl.hold, d.dest[0].heading, 1, 0, false, true); Push(s, A_RESPAWN, k, d.dest[0].p); pl.sinceTp = 0;
                Push(s, A_LOG, k, p, pl.fallT, (float)pl.tries, 2);
                break;
            }
            if ((pl.traceT -= dt) <= 0) { pl.traceT = 0.25f; Push(s, A_TRACE, k, p, (float)pl.fallPhase, pl.phaseT, pl.tries); }
            if (pl.fallPhase == 0) {
                // held still just above the floor while its cell streams in (the screen is black)
                if (pl.phaseT >= pl.holdNeed) {
                    pl.fallPhase = 1; Push(s, A_LOG, k, p, pl.phaseT, 0, 6); pl.phaseT = 0; pl.stillT = 0;
                } else if ((DistXZ(p, hov) > 0.05f || std::fabs(p.y - hov.y) > 0.05f) && pl.sinceTp > 0.05f) {
                    Push(s, A_TELEPORT, k, hov, 0, 0, 0, false, true); pl.sinceTp = 0;
                }
                break;
            }
            // let go: the last half unit onto the floor
            const bool atFloor = p.y > floorY - 0.3f && p.y < floorY + 0.75f && DistXZ(p, hov) < 1.5f;
            pl.stillT = (pl.hadLast && std::fabs(p.y - pl.last.y) < 0.003f) ? pl.stillT + dt : 0.0f;
            if (atFloor && (pi.grounded || pl.stillT > 0.5f)) {                     // landed on the yellow carpet
                pl.st = PS_IN; pl.mod = ArrMod(d, s); pl.timeIn = 0; pl.walked = 0; pl.sinceSpecial = 0; pl.cameBy = -1;
                pl.darkT = P.land_dark; pl.sinceTp = 0;
                pl.nextRemind = P.remind_first; pl.showT = 0; pl.stuckT = 0; pl.stuckSaid = false; pl.sinceVast = 0;   // [br4]
                s.seenAt[pl.mod] = 1;
                Push(s, A_RESPAWN, k, {arr.x, arr.y + 0.1f, arr.z});
                Push(s, A_LOG, k, p, pl.fallT, (float)pl.tries, pi.grounded ? 1 : 8);
                break;
            }
            if (p.y < floorY - P.fall_catch || pl.phaseT > 4.0f) {                  // through the floor (not loaded yet) / never settled
                ++pl.tries;
                Push(s, A_LOG, k, p, p.y, (float)pl.tries, 7);
                HoldAgain(d, s, k, P.fall_retry_hold);
            }
            break; }
        case PS_IN: {
            pl.timeIn += dt;
            // [br5] Leave / the easy way out are LATCHED: asked within half a second after a connector (the move's settle
            // time below) the one-tick request used to be lost - found by the br5 fuzz (the menu's Leave right after a door)
            if (in.leave[k] || easyAll) pl.wantOut = true;
            bool leave = pl.wantOut || !in.enable;
            int mod = ModAt(d, p, pl.mod);
            pl.stuckT += dt; if (pl.showT > 0) { pl.showT -= dt; if (pl.showT < 0) pl.showT = 0; }
            if (mod < 0 || !InZone(d, p)) {                                         // out of every module: fell through / glitched
                pl.lost += dt;
                if (pl.lost > 2.5f || p.y < d.zy0 - 2.0f) {
                    const V3 arr = ArrPt(d, s);
                    V3 to = {arr.x, arr.y + 0.6f, arr.z};
                    Push(s, A_TELEPORT, k, to, 0, 0, 0, false); Push(s, A_BLINK, k, {}, 0.2f); Push(s, A_LOG, k, {}, 0, 0, 3);
                    pl.mod = ArrMod(d, s); pl.lost = 0; pl.sinceTp = 0; pl.inStub = -1; pl.glitch = 1;
                }
                break;
            }
            pl.lost = 0;
            if (mod != pl.mod) { pl.mod = mod; }                                    // (only by an unexpected jump)
            // exit doors
            int ex = -1;
            for (int i = 0; i < d.nExits; ++i) {
                const ExitR& e = d.exits[i];
                if (e.mod == mod && p.x >= e.x0 && p.x <= e.x1 && p.z >= e.z0 && p.z <= e.z1 && std::fabs(p.y - e.y) < 1.5f) ex = i;
            }
            if ((ex >= 0 || leave) && pi.onFoot && pl.sinceTp > 0.5f) {
                // which surface point this door leads to: fixed for the visit, shuffled by the seed
                int di = (int)((s.seed + 0x9E37u * (uint64_t)(ex + 1)) % (uint64_t)d.nDest);
                if (leave) di = 0;
                pl.st = PS_SURFACE; pl.dest = di; pl.surfT = 0;
                pl.hold = {d.dest[di].p.x, d.dest[di].p.y + 0.8f, d.dest[di].p.z};
                Push(s, A_BLINK, k, {}, 0.35f);
                Push(s, A_TELEPORT, k, pl.hold, d.dest[di].heading, 1, 0, false);
                Push(s, A_RESPAWN, k, d.dest[di].p);
                Push(s, A_LOG, k, {}, 0, 0, 4);
                pl.sinceTp = 0; pl.inStub = -1; pl.glitch = 1;
                break;
            }
            // [br4] the gold bricks of this room: found by standing next to one (either player, one shared count)
            if (P.goal_count > 0 && d.mods[mod].nGold > 0) {
                for (int i = 0; i < d.nGold; ++i) {
                    const Gold& g = d.gold[i];
                    if (g.mod != mod || s.found[i] || DistXZ(p, g.p) > P.goal_reach || std::fabs(p.y - g.p.y) > 1.6f) continue;
                    s.found[i] = true; ++s.nFound; s.flashT = 4.0f;
                    s.rowT = P.goal_show; s.msg = 1; s.msgT = P.msg_show < P.goal_show ? P.msg_show : P.goal_show;      // [br5]
                    for (int q = 0; q < kPlayers; ++q) s.pl[q].sinceGold = 0;
                    Push(s, A_LOG, k, g.p, (float)s.nFound, P.goal_count, 12);
                    if (!s.goalDone && s.nFound >= (int)P.goal_count) { s.goalDone = true; s.flashT = 8.0f; s.msg = 2; s.msgT = P.msg_show + 4.0f; s.rowT = P.goal_show + 4.0f; Push(s, A_LOG, k, g.p, (float)s.nFound, P.goal_count, 13); }
                }
            }
            // [br4] the reminder how to leave: after remind_first seconds, every remind_every, and at once when a player
            // has stayed stuck_time in one room
            if (pl.timeIn >= pl.nextRemind) { pl.nextRemind = pl.timeIn + P.remind_every; pl.showT = P.remind_show; Push(s, A_LOG, k, {}, 0, 0, 17); }
            else if (!pl.stuckSaid && pl.stuckT >= P.stuck_time && pl.timeIn >= P.stuck_from) { /* [br5] not in the first minutes */ pl.stuckSaid = true; pl.showT = P.remind_show; pl.nextRemind = pl.timeIn + P.remind_every; Push(s, A_LOG, k, {}, 1, 0, 17); }
            // [br4] the on-screen arrow: a gold brick of this room not yet found (near, or anywhere in a vast / massive room);
            // else, while a reminder shows or after the mercy time, this room's way out (its EXIT door or its lit EXIT connector)
            pl.arrowKind = 0; pl.arrowDir = 0;
            if (false) {                                 // [br5] no arrow: nothing on screen points at a brick or at a way out
                float best = 1e9f; V3 at = {};
                for (int i = 0; i < d.nGold; ++i) {
                    const Gold& g = d.gold[i];
                    if (g.mod != mod || s.found[i] || P.goal_count <= 0 || s.goalDone) continue;
                    const float dd = DistXZ(p, g.p);
                    if (dd < best && (dd <= P.goal_near || (d.mods[mod].flags & (MF_MASSIVE | MF_VAST)))) { best = dd; at = g.p; }
                }
                if (best < 1e8f) { pl.arrowKind = 1; pl.arrowDir = ScreenDir(pi, at); }
                else if (pl.showT > 0 || pl.timeIn > P.mercy_min * 60.0f || (d.mods[mod].flags & (MF_EXIT | MF_VAULT))) {
                    for (int i = 0; i < d.nExits; ++i) if (d.exits[i].mod == mod) {
                        const ExitR& e = d.exits[i]; V3 c = {(e.x0 + e.x1) / 2, e.y, (e.z0 + e.z1) / 2};
                        const float dd = DistXZ(p, c); if (dd < best) { best = dd; at = c; }
                    }
                    if (best > 1e8f) for (int i = d.stubFirst[mod]; i < d.stubFirst[mod + 1]; ++i) if (d.stubs[i].mark == MK_EXIT) {
                        const Stub& t = d.stubs[i]; V3 c = {t.g == 'P' ? t.x0 : t.x1, t.y, (t.z0 + t.z1) / 2};
                        const float dd = DistXZ(p, c); if (dd < best) { best = dd; at = c; }
                    }
                    if (best < 1e8f) { pl.arrowKind = 2; pl.arrowDir = ScreenDir(pi, at); }
                }
            }
            // connectors
            int st = StubAt(d, mod, p);
            if (st != pl.inStub) {
                if (st >= 0 && pl.rolledFor != st) {
                    // walking into a connector: roll where it leads - unless it is the one just used (then only sometimes)
                    bool back = (st == pl.cameBy);
                    if (!back || s.rng.u() < P.relink_back || pl.timeIn > P.mercy_min * 60.0f) Roll(d, s, in, k, st);   // [br4] under mercy the way back is re-rolled too
                    if (!LinkValid(d, s, st, s.link[st]) && s.hold[st] <= 0 && !Busy(d, s, in, s.link[st], k)) Roll(d, s, in, k, st, true);   // [br4] a promise to keep
                    // (a far side somebody stands in is left alone: it is his connector's own, valid link seen from behind)
                    pl.rolledFor = st;
                }
                if (st < 0) pl.rolledFor = -1;
                pl.inStub = st;
            }
            if (st >= 0 && pi.onFoot && pl.sinceTp > 0.2f) {
                const Stub& a = d.stubs[st];
                bool beyond = (a.g == 'P') ? (p.x >= a.xm) : (p.x <= a.xm);
                if (beyond) {
                    // [br4] at the plane: the link must keep its connector's promise (a link in its hold = the partner
                    // following / the way back: kept as it is, counted)
                    if (!LinkValid(d, s, st, s.link[st])) {
                        if (s.hold[st] > 0 || (LinkValid(d, s, s.link[st], st) && Busy(d, s, in, s.link[st], k))) ++s.follows;
                        else {
                            Roll(d, s, in, k, st, true);
                            if (!LinkValid(d, s, st, s.link[st])) { ++s.lies; Push(s, A_LOG, k, {}, (float)st, 0, 18); }
                        }
                    }
                    int t = s.link[st];
                    const Stub& b = d.stubs[t];
                    V3 to = {p.x + (b.xm - a.xm), p.y + (b.y - a.y), p.z + (b.z0 - a.z0)};
                    Push(s, A_TELEPORT, k, to, 0, 0, st, true);
                    Push(s, A_BLINK, k, {}, 0.0f);
                    s.hold[st] = s.hold[t] = P.link_hold;
                    ++s.crossings;
                    pl.cameFromMark = a.mark;                                        // [br5]
                    Arrived(d, s, k, b.mod);
                    pl.mod = b.mod; pl.cameBy = t; pl.inStub = t; pl.rolledFor = t; pl.sinceTp = 0; pl.glitch = 1.0f;
                }
            }
            break; }
        case PS_SURFACE: {
            pl.surfT += dt;
            // [fix1] standing at the destination's height (the engine's on-ground there), not the ground probe
            bool there = pi.grounded && p.y > pl.hold.y - 2.5f && DistXZ(p, pl.hold) < 3.0f && pl.sinceTp > 0.2f;
            if (there || pl.surfT > 15.0f) { Push(s, A_LOG, k, p, 0, pl.surfT, there ? 10 : 11); pl = PlayerSt(); break; }
            // the surface cell is still loading: hold him over the point instead of letting him fall through the world
            if ((p.y < pl.hold.y - 1.2f || DistXZ(p, pl.hold) > 1.5f) && pl.sinceTp > 0.15f) {
                Push(s, A_TELEPORT, k, pl.hold, 0, 0, 0, false, true); pl.sinceTp = 0;
            }
            break; }
        }
        pl.last = p; pl.hadLast = true;
    }
    // kill height: lowered while anyone is in the shaft / maze / surfacing, restored afterwards
    bool any = false;
    for (int k = 0; k < kPlayers; ++k) if (s.pl[k].st != PS_OUT) any = true;
    if (any != s.terlowHeld) { s.terlowHeld = any; if (!any) Push(s, A_TERLOW, -1, {}, 0, 1); }
    // effects fade, per player (about a second in and out)
    for (int k = 0; k < kPlayers; ++k) {
        bool i2 = in.pl[k].present && (s.pl[k].st == PS_IN || s.pl[k].st == PS_FALL);
        float stepz = dt / 1.0f;
        s.zone[k] += i2 ? stepz : -stepz; if (s.zone[k] < 0) s.zone[k] = 0; if (s.zone[k] > 1) s.zone[k] = 1;
    }
    StatueStep(d, s, in);
    // colossi: there while a player is in their hall
    for (int i = 0; i < d.nCol; ++i) {
        bool here = false;
        for (int q = 0; q < kPlayers; ++q) if (in.pl[q].present && s.pl[q].st == PS_IN && s.pl[q].mod == d.col[i].mod && s.pl[q].sinceTp > 4.0f) here = true;
        if (here && !s.colOut[i] && in.enable) { s.colOut[i] = true; s.colAway[i] = 0; Push(s, A_COLOSSUS_SPAWN, i, d.col[i].p, d.col[i].yaw, d.col[i].scale); }
        else if (s.colOut[i]) {
            bool anyThere = false;
            for (int q = 0; q < kPlayers; ++q) if (in.pl[q].present && s.pl[q].st == PS_IN && s.pl[q].mod == d.col[i].mod) anyThere = true;
            s.colAway[i] = anyThere ? 0 : s.colAway[i] + dt;
            if (s.colAway[i] > 15.0f || !in.enable) { s.colOut[i] = false; Push(s, A_COLOSSUS_REMOVE, i); }
        }
    }
    // [br3w] the workers: out the moment a player walks into their module (from the connector the room cannot be seen),
    // at most kMaxWorkAlive at once, gone 15 s after the last player left it; the stood vehicles the same, no cap
    auto there = [&](int mod) { for (int q = 0; q < kPlayers; ++q) if (in.pl[q].present && s.pl[q].st == PS_IN && s.pl[q].mod == mod) return true; return false; };
    int alive = 0; for (int i = 0; i < d.nWork; ++i) alive += s.workOut[i] ? 1 : 0;
    for (int i = 0; i < d.nWork; ++i) {
        const bool here = there(d.work[i].mod);
        if (here && !s.workOut[i] && in.enable && alive < kMaxWorkAlive) { s.workOut[i] = true; s.workAway[i] = 0; ++alive; Push(s, A_WORKER_SPAWN, i, d.work[i].p, d.work[i].yaw); }
        else if (s.workOut[i]) {
            s.workAway[i] = here ? 0 : s.workAway[i] + dt;
            if (s.workAway[i] > 15.0f || !in.enable) { s.workOut[i] = false; Push(s, A_WORKER_REMOVE, i); }
        }
    }
    for (int i = 0; i < d.nVeh; ++i) {
        const bool here = there(d.veh[i].mod);
        if (here && !s.vehOut[i] && in.enable) { s.vehOut[i] = true; s.vehAway[i] = 0; Push(s, A_VEH_SPAWN, i, d.veh[i].p, d.veh[i].heading); }
        else if (s.vehOut[i]) {
            s.vehAway[i] = here ? 0 : s.vehAway[i] + dt;
            if (s.vehAway[i] > 15.0f || !in.enable) { s.vehOut[i] = false; Push(s, A_VEH_REMOVE, i); }
        }
    }
}

// what the rest of the DLL asks
inline bool Quiet(const State& s) { for (int k = 0; k < kPlayers; ++k) if (s.pl[k].st != PS_OUT) return true; return false; }
inline bool PlayerInside(const State& s, int k) { return s.pl[k].st == PS_IN || s.pl[k].st == PS_FALL; }
// [fix1] that player's view is black: the whole arrival (held + let go) and land_dark seconds after landing
inline bool Dark(const State& s, int k) { return s.pl[k].st == PS_FALL || s.pl[k].darkT > 0; }

// the shader floats (IniParams 8.xyzw + 9.x, see INTEGRATION.md):
//   zone[0], zone[1]  each player's fade 0..1 (0 everywhere = every effect is bit-identical off)
//   flags             glitch (6 bits) | handicam << 6 | pillarbox << 7 | mercy << 8 | blink P1 << 9 | blink P2 << 10 | intensity (2 bits) << 11
//   secs              seconds in the maze (the longest of the players inside)
//   players           how many players are present (1 = one full view, 2 = split screen)
struct ShaderOut { float zone[kPlayers]; float flags, secs, players; float goal, hud; };
// [br4] goal (IniParams 9.y): found (4 bits) | goal count (4 bits) << 4 | a brick was just found << 8 | all found << 9 |
//        the vault is reached << 10 | player 1's reminder shows << 11 | player 2's << 12
//       hud  (IniParams 9.z): per player 9 bits (player 2 << 9): arrow kind (2 bits: 1 gold brick, 2 exit) | arrow
//        direction (4 bits: 0 ahead, 2 right, 4 behind, 6 left, 8 here) << 2 | easy-way-out progress 0..7 << 6
// [br5] REPLACES both layouts (the shader of br5 reads these):
//       goal (9.y): found (4 bits) | goal count (4 bits) << 4 | the brick row is showing << 8 (only for goal_show
//        seconds after a pickup - never before the first brick) | the ONE text line (2 bits: 1 FOUND! 2 ALL FOUND!
//        ANY DOOR 3 THE VAULT!) << 9
//       hud  (9.z): per player 4 bits (player 2 << 4): easy-way-out progress 0..7 | his "how to leave" reminder << 3.
//        No arrows.  The reminder is never shown while a text line or the LEAVING bar is up (one thing at a time).
inline ShaderOut ShaderFloats(const Data& d, const State& s, const Input& in, bool handicam, bool pillar, int intensity, const bool blink[kPlayers]) {
    ShaderOut o = {};
    float g = 0, t = 0; bool mercy = false; int n = 0; bool any = false;
    for (int k = 0; k < kPlayers; ++k) {
        if (in.pl[k].present) ++n;
        o.zone[k] = s.zone[k]; if (s.zone[k] > 0) any = true;
        if (s.pl[k].glitch > g) g = s.pl[k].glitch;
        if (s.pl[k].st == PS_IN && s.pl[k].timeIn > t) t = s.pl[k].timeIn;
        if (s.pl[k].st == PS_IN && s.pl[k].timeIn > d.par.mercy_min * 60.0f) mercy = true;
        if (s.pl[k].st == PS_IN && s.pl[k].mod >= 0 && IsSpecial(d.mods[s.pl[k].mod]) && g < 0.25f) g = 0.25f;
    }
    for (int i = 0; i < kMaxStatues; ++i) if (s.stat[i].alive && g < 0.4f) g = 0.4f;
    const bool bk[kPlayers] = { blink[0] || Dark(s, 0), blink[1] || Dark(s, 1) };          // [fix1] black through the arrival
    if (bk[0] || bk[1]) any = true;
    if (!any) return o;                                  // all zeros outside
    if (intensity < 0) intensity = 0; if (intensity > 3) intensity = 3;
    int gi = (int)std::lround(g * 63.0f);
    int a = gi | ((handicam ? 1 : 0) << 6) | ((pillar ? 1 : 0) << 7) | ((mercy ? 1 : 0) << 8) | ((bk[0] ? 1 : 0) << 9) | ((bk[1] ? 1 : 0) << 10) | (intensity << 11);
    o.flags = (float)a; o.secs = t; o.players = (float)n;
    int gc = (int)d.par.goal_count; if (gc < 0) gc = 0; if (gc > 15) gc = 15;
    int nf = s.nFound; if (nf > 15) nf = 15;
    const int msg = s.msgT > 0 ? (s.msg & 3) : 0;
    int ga = nf | (gc << 4) | ((s.rowT > 0 && nf > 0 ? 1 : 0) << 8) | (msg << 9);
    int hb = 0;
    for (int k = 0; k < kPlayers; ++k) {
        const PlayerSt& pl = s.pl[k];
        if (pl.st != PS_IN) continue;
        int ez = d.par.easy_out_hold > 0 ? (int)(7.999f * pl.easyT / d.par.easy_out_hold) : 0; if (ez > 7) ez = 7; if (pl.easyT > 0 && ez < 1) ez = 1;
        const int rem = (pl.showT > 0 && msg == 0 && ez == 0) ? 1 : 0;
        hb |= (ez | (rem << 3)) << (4 * k);
    }
    o.goal = (float)ga; o.hud = (float)hb;
    return o;
}

} // namespace brm
