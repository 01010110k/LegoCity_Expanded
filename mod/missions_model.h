// =============================================================================================
//  missions_model.h - [missions1] the story-mission detector, the "Missions: stock game" gate and
//  the mission-news queue (pure: no Windows, no game memory). Notes\68-missions.md.
//  Tested by test\missions_harness.cpp; driven by missions.h (game thread) and radio.h (radio thread).
//
//  1. Detector. The game keeps the story's place in two job values (lcu.jobdatamap):
//       Flow_Setup / Flow_Act              the act, 1..4
//       Flow_Setup.Act_N / ActN_Mission    the mission counter inside act N
//     Each main mission owns one counter value (missions_table.h); the scripts move the counter on
//     when it is done (SetValue("ActN_Mission", 0) then (.., k) in one script step). A mission counts
//     as complete once the story position (act, counter) is past its own. The detector only ever
//     compares two SETTLED positions (held unchanged `stable` s), so the script's 0-then-k write and
//     an act change half-way are never seen. It fires only when
//       - the job table is the one it took its baseline from (a reload / new game = new baseline),
//       - the position moved FORWARD and exactly one mission became complete (a save loaded from the
//         front end jumps many at once, or goes back: both = new baseline, no news),
//       - its baseline is at least `settle` s old, and the player was never gone for `absent` s since.
//  2. Gate ("Missions: stock game", [general] mission_stock). raw = a story mission's own UI flag
//     (wanted.h MissionRunning) || a special-assignment interior || a side mission (races, time
//     trials...). Hysteresis: on after raw held `enter` s, off after raw gone `exit` s. Three masks
//     follow it: GEN at once, VEH (car / bike handling) and AIR (helicopter, planes, Pteranodon) only
//     when that player's vehicle is safe to hand over (not in one, slower than `safe`, or a cut-scene).
//  3. NewsQueue: which missions wait for their news flash, which were heard, the recaps. Times are
//     wall-clock seconds (they outlive a restart); the state file is a few text lines.
// =============================================================================================
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace msm {

struct MissionDef
{
    const char* id;                 // the JOBPATH (lcu.jobdatamap) - the key everywhere
    uint8_t act, value;             // Flow_Act and the ActN_Mission value the mission owns
    uint8_t chapter, chapterEnd;    // 1..15, 1 = the chapter's last mission
    uint8_t special;                // special assignment number 1..15, 0 = none
};
#include "missions_table.h"

// ---- 1. the detector ------------------------------------------------------------------------
struct Snap
{
    bool     ok = false;            // every value read
    uint64_t gen = 0;               // which job table (pointer ^ count)
    int      act = 0;               // Flow_Act
    int      mis[5] = {};           // [1..4] ActN_Mission
    bool     side = false;          // City_Setup.Bonus_Missions / bMissionActive (a side mission runs)
};

inline int PosMission(const Snap& s) { return (s.act >= 1 && s.act <= 4) ? s.mis[s.act] : -1; }
inline bool PosValid(const Snap& s) { return s.ok && s.act >= 1 && s.act <= 4 && PosMission(s) >= 0 && PosMission(s) < 100; }
// (a, v) < (b, w)
inline int PosCmp(int a, int v, int b, int w) { return a != b ? (a < b ? -1 : 1) : (v != w ? (v < w ? -1 : 1) : 0); }
inline bool Done(const MissionDef& m, const Snap& s) { return PosCmp(s.act, PosMission(s), m.act, m.value) > 0; }

struct DetParams
{
    double stable = 1.0;            // s a new position must hold before it counts
    double settle = 8.0;            // s after a new baseline before anything may fire
    double absent = 6.0;            // s without a player = the front end / a load: new baseline
};

enum Why { W_NONE, W_BASELINE, W_NEWTABLE, W_BACKWARD, W_BULK, W_UNSETTLED, W_ABSENT, W_FIRE };
static const char* const kWhyNames[] = { "", "first baseline", "a new job table (a load / new game)",
    "the story went back (a save loaded)", "several missions at once (a save loaded)",
    "too soon after a baseline", "back after the player was gone", "fired" };

struct Detector
{
    bool   have = false;            // a baseline
    Snap   base;
    double baseAt = 0.0;
    Snap   cand;
    double candSince = 0.0;
    bool   candOk = false;
    double absentSince = -1.0;
    bool   rebase = false;          // the player was gone long enough: the next settled read is a baseline
    int    lastWhy = W_NONE;        // for the log: what the last change of base was
};

inline bool SamePos(const Snap& a, const Snap& b) { return a.gen == b.gen && a.act == b.act && PosMission(a) == PosMission(b); }

// One read. Returns the number of mission indices written to out (0 or 1); `why` says what
// happened to the baseline this call (W_NONE = nothing).
inline int Feed(Detector& d, const Snap& s, bool present, double now, const DetParams& p, int* out, int cap, int* why)
{
    *why = W_NONE;
    if (!present)
    {
        if (d.absentSince < 0.0) d.absentSince = now;
        if (now - d.absentSince >= p.absent) d.rebase = true;
    }
    else d.absentSince = -1.0;
    if (!PosValid(s)) { d.candOk = false; return 0; }
    if (!d.candOk || !SamePos(s, d.cand)) { d.cand = s; d.candSince = now; d.candOk = true; return 0; }
    if (now - d.candSince < p.stable) return 0;
    const Snap& c = d.cand;
    if (!d.have || d.rebase || c.gen != d.base.gen)
    {
        *why = !d.have ? W_BASELINE : (d.rebase ? W_ABSENT : W_NEWTABLE);
        d.base = c; d.have = true; d.rebase = false; d.baseAt = now; d.lastWhy = *why;
        return 0;
    }
    if (SamePos(c, d.base)) return 0;
    auto rebase = [&](int w) { *why = w; d.base = c; d.baseAt = now; d.lastWhy = w; return 0; };
    if (PosCmp(c.act, PosMission(c), d.base.act, PosMission(d.base)) < 0) return rebase(W_BACKWARD);
    int n = 0, first = -1;
    for (int i = 0; i < kMissionCount; ++i)
        if (!Done(kMissions[i], d.base) && Done(kMissions[i], c)) { if (first < 0) first = i; ++n; }
    if (n > 1) return rebase(W_BULK);
    if (now - d.baseAt < p.settle) return rebase(W_UNSETTLED);
    d.base = c; d.baseAt = now;          // (a move inside the post-game values fires nothing: n == 0)
    if (n == 1 && cap > 0) { out[0] = first; *why = W_FIRE; return 1; }
    return 0;
}

inline int FindMission(const char* id)
{
    for (int i = 0; i < kMissionCount; ++i) if (std::strcmp(kMissions[i].id, id) == 0) return i;
    return -1;
}

// ---- 2. the gate ----------------------------------------------------------------------------
enum Mask : uint8_t { M_GEN = 1, M_VEH = 2, M_AIR = 4 };

struct GateIn
{
    bool  on = true;                // [general] mission_stock (and lcudrive running)
    bool  story = false;            // wanted.h MissionRunning (game + 0x335F / 0x335E)
    bool  interior = false;         // a special assignment / story interior (not the city, not the police station)
    bool  side = false;             // a side mission runs (bMissionActive)
    bool  sideOn = true;            // [general] mission_stock_side
    double dt = 0.0;                // s since the last step
    int   veh[2] = { 0, 0 };        // per player: 0 on foot, 1 car / bike, 2 helicopter / plane / Pteranodon
    float speed[2] = { 0, 0 };      // u/s of that vehicle
    bool  cutscene = false;
};
struct GateParams { float enter = 2.0f, exit = 6.0f, safe = 3.0f; };

struct Gate
{
    bool   active = false;          // the mission state after the hysteresis (on or off of `on`)
    double onT = 0.0, offT = 0.0;
    uint8_t mask = 0;               // what is suspended now
};

inline bool Raw(const GateIn& in) { return in.story || in.interior || (in.sideOn && in.side); }

// One step; returns the mask. `changed` (optional) gets the bits that flipped.
inline uint8_t Step(Gate& g, const GateIn& in, const GateParams& p, uint8_t* changed = nullptr)
{
    const bool raw = Raw(in);
    if (raw) { g.onT += in.dt; g.offT = 0.0; } else { g.offT += in.dt; g.onT = 0.0; }
    if (!g.active && raw && g.onT >= p.enter) g.active = true;
    else if (g.active && !raw && g.offT >= p.exit) g.active = false;
    const bool want = g.active && in.on;
    const uint8_t before = g.mask;
    uint8_t m = static_cast<uint8_t>(g.mask & ~M_GEN);
    if (want) m |= M_GEN;
    auto safeFor = [&](int kind) {
        if (in.cutscene) return true;
        for (int k = 0; k < 2; ++k)
            if (in.veh[k] == kind && !(in.speed[k] < p.safe)) return false;
        return true;
    };
    if (((g.mask & M_VEH) != 0) != want && safeFor(1)) m = static_cast<uint8_t>(want ? (m | M_VEH) : (m & ~M_VEH));
    if (((g.mask & M_AIR) != 0) != want && safeFor(2)) m = static_cast<uint8_t>(want ? (m | M_AIR) : (m & ~M_AIR));
    g.mask = m;
    if (changed) *changed = static_cast<uint8_t>(before ^ m);
    return m;
}

// ---- 3. the news queue ----------------------------------------------------------------------
struct NewsEntry
{
    std::string id;
    int     state = 0;              // 0 waiting for its flash, 1 heard, 2 never heard in time (recaps may air it)
    int64_t detected = 0, heard = 0, lastRecap = 0;
    int     recaps = 0;
};
struct NewsParams
{
    double keep = 6 * 3600.0;       // s a flash waits to be heard; then it only comes back as a recap
    double recapWindow = 3 * 3600.0;// s after the flash in which recaps may air
    double recapGap = 20 * 60.0;    // s between two recaps of one mission
    int    recapMax = 2;            // recaps per mission
    bool   once = false;            // 1 = a mission heard once never gets news again (any save)
};

struct NewsQueue
{
    std::vector<NewsEntry> e;
    bool dirty = false;
};

inline int FindEntry(const NewsQueue& q, const std::string& id)
{
    for (size_t i = 0; i < q.e.size(); ++i) if (q.e[i].id == id) return static_cast<int>(i);
    return -1;
}

// A completion seen in game. False = nothing new (already waiting, or `once` and heard).
inline bool Add(NewsQueue& q, const std::string& id, int64_t now, const NewsParams& p)
{
    int i = FindEntry(q, id);
    if (i >= 0)
    {
        NewsEntry& x = q.e[static_cast<size_t>(i)];
        if (x.state == 0) return false;
        if (p.once && x.state == 1) return false;
        x = NewsEntry(); x.id = id; x.detected = now;      // a new playthrough of it (another save): news again
        q.dirty = true;
        return true;
    }
    NewsEntry x; x.id = id; x.detected = now;
    q.e.push_back(x);
    q.dirty = true;
    return true;
}

// The flash due now: the oldest waiting one (older ones past `keep` turn into state 2). -1 = none.
inline int Due(NewsQueue& q, int64_t now, const NewsParams& p)
{
    int best = -1;
    for (size_t i = 0; i < q.e.size(); ++i)
    {
        NewsEntry& x = q.e[i];
        if (x.state != 0) continue;
        if (static_cast<double>(now - x.detected) > p.keep) { x.state = 2; x.heard = now; q.dirty = true; continue; }
        if (best < 0 || x.detected < q.e[static_cast<size_t>(best)].detected) best = static_cast<int>(i);
    }
    return best;
}

inline void Heard(NewsQueue& q, int i, int64_t now)
{
    if (i < 0 || i >= static_cast<int>(q.e.size())) return;
    q.e[static_cast<size_t>(i)].state = 1; q.e[static_cast<size_t>(i)].heard = now; q.dirty = true;
}

// A mission a news break may recap now: heard (or never heard in time), inside the window, under the
// cap, not recapped for `recapGap`. The least recapped, then the most recent. -1 = none.
inline int RecapCandidate(const NewsQueue& q, int64_t now, const NewsParams& p)
{
    int best = -1;
    for (size_t i = 0; i < q.e.size(); ++i)
    {
        const NewsEntry& x = q.e[i];
        if (x.state == 0 || x.recaps >= p.recapMax) continue;
        if (static_cast<double>(now - x.heard) > p.recapWindow) continue;
        if (x.lastRecap && static_cast<double>(now - x.lastRecap) < p.recapGap) continue;
        if (x.state == 1 && static_cast<double>(now - x.heard) < p.recapGap) continue;   // not straight after the flash
        if (best < 0) { best = static_cast<int>(i); continue; }
        const NewsEntry& b = q.e[static_cast<size_t>(best)];
        if (x.recaps < b.recaps || (x.recaps == b.recaps && x.heard > b.heard)) best = static_cast<int>(i);
    }
    return best;
}

inline void Recapped(NewsQueue& q, int i, int64_t now)
{
    if (i < 0 || i >= static_cast<int>(q.e.size())) return;
    ++q.e[static_cast<size_t>(i)].recaps; q.e[static_cast<size_t>(i)].lastRecap = now; q.dirty = true;
}

// The state file: "# ..." comments, then one line per mission: id state detected heard recaps lastRecap.
inline std::string Save(const NewsQueue& q)
{
    std::string s = "# lcudrive mission news (missions1): id\tstate(0 waiting,1 heard,2 expired)\tdetected\theard\trecaps\tlast_recap (unix s)\n";
    char buf[512];
    for (const NewsEntry& x : q.e)
    {
        std::snprintf(buf, sizeof(buf), "%s\t%d\t%lld\t%lld\t%d\t%lld\n", x.id.c_str(), x.state, static_cast<long long>(x.detected),
                      static_cast<long long>(x.heard), x.recaps, static_cast<long long>(x.lastRecap));
        s += buf;
    }
    return s;
}

// Lines that don't parse, unknown ids (missions_table.h) and duplicates are skipped. Returns the count kept.
inline int Load(NewsQueue& q, const char* text, size_t n)
{
    q = NewsQueue();
    std::string all(text, n);
    size_t pos = 0;
    while (pos < all.size())
    {
        size_t e = all.find('\n', pos);
        if (e == std::string::npos) e = all.size();
        std::string line = all.substr(pos, e - pos);
        pos = e + 1;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        char id[160] = {}; int st = 0, rc = 0; long long d = 0, h = 0, lr = 0;
#ifdef _MSC_VER
        if (sscanf_s(line.c_str(), "%159[^\t]\t%d\t%lld\t%lld\t%d\t%lld", id, static_cast<unsigned>(sizeof(id)), &st, &d, &h, &rc, &lr) != 6) continue;
#else
        if (std::sscanf(line.c_str(), "%159[^\t]\t%d\t%lld\t%lld\t%d\t%lld", id, &st, &d, &h, &rc, &lr) != 6) continue;
#endif
        if (FindMission(id) < 0 || FindEntry(q, id) >= 0 || st < 0 || st > 2 || rc < 0 || rc > 1000) continue;
        NewsEntry x; x.id = id; x.state = st; x.detected = d; x.heard = h; x.recaps = rc; x.lastRecap = lr;
        q.e.push_back(x);
    }
    return static_cast<int>(q.e.size());
}

} // namespace msm
