// =============================================================================================
//  marina_model.h - marinas, moored / anchored boats and boat STATIONS (2026-11-19, marina1, Notes\56).
//  Pure: the [marina] lines, the station decisions, the press / reach / placement rules - so
//  test\marina_harness.cpp runs it as is. marina.h is the engine side; both lean on sites_model.h:
//  every boat here is a sitem::Stand driven by sitem::Tick (the same spawn / park / boarded / away /
//  faded / never-removed-beside-a-player state machine as the [sites] stands).
//
//  The goal (2026-11-19): marinas with spawnable boats, and a kind of summon station for the
//  boats. So:
//    berthN   = model, boat | anchored, x, y, z, heading [, label]      (sitem::ParseStand's line;
//               "anchored" = a boat with no map pin). A boat that waits there like a [sites] stand:
//               taken -> yours; left elsewhere -> faded once nobody is near it, and the berth is
//               filled again (sitem::Tick).
//    stationN = post x, y, z, berth x, y, z, heading [, label]
//               a boat-station post: a player ON FOOT within `reach` of the post presses SPECIAL
//               (the game's own special / action button, read from his pad block) ->
//                 nothing of the station's out:   a boat is brought to the station's berth (the next
//                                                 type of `types`), once the berth is clear
//                 its boat waiting at the berth:  swapped for the next type (`cycle`)
//                 its boat out, nobody in it:     that one fades and a fresh one comes (`recall`)
//                 its boat out with someone in it, or one on its way: nothing
//               The station's boat is never refilled by itself: once it has gone (taken and left,
//               wrecked, or nobody near), the station waits for the next press.
//    types    = the boats a station offers, in order (AITypes.txt second column)
//  A berth's y must be the water (-0.5 .. 1.0): a station or berth put on land is refused at load
//  with a reason (a boat summoned onto land must be refused).
// =============================================================================================
#pragma once
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <cstdio>
#include "sites_model.h"

namespace marm {

enum : int { kMaxBerths = 12, kMaxStations = 8, kMaxTypes = 8 };

struct Station
{
    bool  on;
    float post[3];
    float berth[3];
    float heading;
    char  label[32];
};

struct Config
{
    int   enable = 1;          // [marina] enable: berths and stations (also off with `[` / lcudrive off)
    float nearU = 100.0f;       // u: a player this close to a berth gets its boat (farther than [sites] near: pop-in at range)
    float farU = 220.0f;        // u: nobody this close to a waiting boat -> it fades
    float leave = 120.0f;      // u: one left elsewhere fades once nobody is this close (after 10 s)
    float reach = 1.4f;        // u: a station's post is "used" by a player this close to it (on foot, |dy| <= 1.5)
    float clear = 2.0f;        // u: no boat is brought while a player is this close to the berth point
    int   cycle = 1;           // a press while the station's boat waits at its berth: swap it for the next type
    int   recall = 1;          // a press while the station's boat is out and empty: that one fades, a fresh one comes
    char  types[kMaxTypes][48] = {};
    int   nTypes = 0;
    sitem::Stand berth[kMaxBerths] = {};
    Station station[kMaxStations] = {};
};

// ---------------------------------------------------------------------------------------------
//  Parsing
// ---------------------------------------------------------------------------------------------
inline bool Water(float y) { return y >= -0.5f && y <= 1.0f; }

// berthN: 0 = a berth, 1 = empty, 2 = malformed (err says why)
inline int ParseBerth(const char* text, sitem::Stand& out, char* err, size_t errCap)
{
    char buf[256];
    strncpy_s(buf, text ? text : "", _TRUNCATE);
    // "anchored" -> a boat with no map pin (sitem::ParseStand knows boat; quiet is set after)
    bool anchored = false;
    {
        char* c1 = strchr(buf, ',');
        if (c1)
        {
            char* k = c1 + 1;
            while (*k == ' ' || *k == '\t') ++k;
            if (_strnicmp(k, "anchored", 8) == 0 && (k[8] == ',' || k[8] == ' ' || k[8] == '\t'))
            {
                anchored = true;
                memcpy(k, "boat    ", 8);
            }
        }
    }
    const int r = sitem::ParseStand(buf, out, err, errCap);
    if (r != 0) return r;
    if (out.kind != sitem::kBoat)
    {
        if (err && errCap) strncpy_s(err, errCap, "a berth holds a boat (kind boat or anchored)", _TRUNCATE);
        out = sitem::Stand{}; return 2;
    }
    if (!Water(out.pos[1]))
    {
        if (err && errCap) strncpy_s(err, errCap, "y is not the water (-0.5 .. 1.0): a boat is never put on land", _TRUNCATE);
        out = sitem::Stand{}; return 2;
    }
    out.quiet = anchored;
    return 0;
}

// stationN: 0 = a station, 1 = empty, 2 = malformed
inline int ParseStation(const char* text, Station& out, char* err, size_t errCap)
{
    auto fail = [&](const char* why) { if (err && errCap) strncpy_s(err, errCap, why, _TRUNCATE); out = Station{}; return 2; };
    out = Station{};
    if (err && errCap) err[0] = 0;
    const char* s = text ? text : "";
    while (*s == ' ' || *s == '\t') ++s;
    if (!*s) return 1;
    float v[7];
    for (int i = 0; i < 7; ++i)
    {
        while (*s == ' ' || *s == '\t') ++s;
        char* end = nullptr;
        v[i] = strtof(s, &end);
        if (end == s || !(v[i] == v[i]) || std::fabs(v[i]) > 1.0e5f) return fail("needs: post x, y, z, berth x, y, z, heading (numbers)");
        s = end;
        while (*s == ' ' || *s == '\t') ++s;
        if (i < 6) { if (*s != ',') return fail("needs: post x, y, z, berth x, y, z, heading"); ++s; }
    }
    if (*s == ',') ++s; else if (*s) return fail("junk after the heading");
    while (*s == ' ' || *s == '\t') ++s;
    for (int i : { 0, 2, 3, 5 }) if (std::fabs(v[i]) > 5000.0f) return fail("the position is outside the world");
    if (v[1] < -100.0f || v[1] > 500.0f) return fail("the post's y is outside the world");
    if (!Water(v[4])) return fail("the berth's y is not the water (-0.5 .. 1.0): a boat is never brought onto land");
    const float d = std::sqrt((v[0] - v[3]) * (v[0] - v[3]) + (v[2] - v[5]) * (v[2] - v[5]));
    if (d < 2.5f) return fail("the berth is closer than 2.5 u to its post (the boat would come onto the player)");
    if (d > 15.0f) return fail("the berth is more than 15 u from its post");
    for (int i = 0; i < 3; ++i) { out.post[i] = v[i]; out.berth[i] = v[3 + i]; }
    out.heading = v[6];
    strncpy_s(out.label, s, _TRUNCATE);
    out.on = true;
    return 0;
}

// types = A, B, C ...: model names (letters, digits, _), at most kMaxTypes; returns the count
inline int ParseTypes(const char* text, char (*out)[48], int cap)
{
    int n = 0;
    const char* s = text ? text : "";
    while (*s && n < cap)
    {
        while (*s == ' ' || *s == '\t' || *s == ',') ++s;
        const char* e = s;
        while (*e && *e != ',' && *e != ' ' && *e != '\t') ++e;
        const size_t len = static_cast<size_t>(e - s);
        if (len && len < 48)
        {
            bool ok = true;
            for (const char* c = s; c < e; ++c)
                if (!((*c >= 'A' && *c <= 'Z') || (*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9') || *c == '_')) ok = false;
            if (ok) { memcpy(out[n], s, len); out[n][len] = 0; ++n; }
        }
        s = e;
    }
    return n;
}

// The station's boat as a stand: the berth, the heading, the chosen type, a boat with a pin.
inline sitem::Stand StandOf(const Station& st, const char* model)
{
    sitem::Stand s = {};
    s.on = st.on && model && model[0];
    if (!s.on) return s;
    strncpy_s(s.model, model, _TRUNCATE);
    s.kind = sitem::kBoat;
    s.pos[0] = st.berth[0]; s.pos[1] = st.berth[1]; s.pos[2] = st.berth[2];
    s.heading = st.heading;
    strncpy_s(s.label, st.label, _TRUNCATE);
    return s;
}

// ---------------------------------------------------------------------------------------------
//  Using a post
// ---------------------------------------------------------------------------------------------
// A player at a post: on foot, within reach (plan), within 1.5 u of its height.
inline bool AtPost(const float post[3], const float p[3], bool onFoot, float reach)
{
    if (!onFoot) return false;
    const float dx = p[0] - post[0], dz = p[2] - post[2];
    return dx * dx + dz * dz <= reach * reach && std::fabs(p[1] - post[1]) <= 1.5f;
}

// The button's rising edge (held words of this frame and the last), per player.
inline bool Pressed(uint32_t held, uint32_t prevHeld, uint32_t mask) { return mask && (held & mask) && !(prevHeld & mask); }

inline int NextType(int cur, int n) { return n <= 0 ? -1 : (cur < 0 ? 0 : (cur + 1) % n); }

enum Act : int { kNothing = 0, kBring, kCycle, kRecall, kBusyAboard, kBusyComing, kOff };
inline const char* ActName(int a)
{
    static const char* n[] = { "nothing", "bring", "cycle", "recall", "busy (someone is in it)", "busy (one is coming)", "off" };
    return a >= 0 && a <= kOff ? n[a] : "?";
}

// What a press at a station does. state = the station slot's sitem state; armed = the station has a
// boat out or on its way; boatNear = a player within 3 u of the station's boat (it is not ours to fade
// then); onBerth = it is on its berth.
inline int Decide(bool on, bool armed, int state, bool haveVeh, bool aboard, bool boatNear, bool onBerth, const Config& c)
{
    if (!on || c.nTypes <= 0) return kOff;
    if (!armed && !haveVeh) return kBring;
    if (state == sitem::kSpawning || (armed && !haveVeh)) return kBusyComing;
    if (aboard) return kBusyAboard;
    if (state == sitem::kParked && onBerth) return c.cycle ? (boatNear ? kNothing : kCycle) : kNothing;
    // out (away / wreck / left off its berth)
    if (!c.recall) return kNothing;
    return boatNear ? kNothing : kRecall;
}
// (kCycle with a player within 3 u of the waiting boat - e.g. one stepping in - is refused as kNothing:
//  a boat is never swapped from under someone about to board it.)

// After the station slot's tick: does the station stand down (wait for the next press)? It does when
// its slot came back to idle from having had a boat out (faded: left behind, wrecked, nobody near, a
// failed spawn), unless it is a cycle / recall in progress (the next boat is wanted).
inline bool Disarm(int prevState, int newState, bool swapping)
{
    return !swapping && newState == sitem::kIdle && prevState != sitem::kIdle;
}

// The facts a station's boat gets that a [sites] stand's do not: its own clearance at the berth (a
// player at the post is not "on" the berth) - nobody within `clear` of the berth point, no boat within 3 u.
inline bool BerthBlocked(const float berth[3], const float pp[][3], const bool* ppOk, int nPlayers, const float (*vpos)[3],
                         const bool* vOk, int nVeh, float clear)
{
    for (int i = 0; i < nPlayers; ++i)
        if (ppOk[i] && std::hypot(pp[i][0] - berth[0], pp[i][2] - berth[2]) < clear) return true;
    for (int j = 0; j < nVeh; ++j)
        if (vOk[j] && std::hypot(vpos[j][0] - berth[0], vpos[j][2] - berth[2]) < 3.0f) return true;
    return false;
}

// The shipped lines (marina_geo.py's SITES; lcudrive.ini carries the same): used for a key that is
// missing from the ini; an EMPTY value switches that berth / station off.
inline const char* DefaultBerth(int i)
{
    static const char* k[kMaxBerths] = {
        "Naval_Arowana, boat, 403.0, 0.35, -130.5, 270, Atlas Marina finger 1",
        "Naval_Surger, boat, 403.0, 0.35, -135.5, 270, Atlas Marina finger 2",
        "Naval_Skimmer, boat, 403.0, 0.35, -140.5, 270, Atlas Marina finger 3",
        "Naval_Devario, boat, 306.5, 0.35, -46.9, 0, Fresco Marina north 1",
        "Naval_Arowana, boat, 311.5, 0.35, -46.9, 0, Fresco Marina north 2",
        "Naval_Surger, boat, 306.5, 0.35, -53.1, 180, Fresco Marina south 1",
        "Naval_Rubadub, boat, 311.5, 0.35, -53.1, 180, Fresco Marina south 2",
        "Naval_Flotsam, anchored, 592.0, 0.35, -40.0, 200, anchored off Port Atlas east",
        "Naval_Devario, anchored, 452.0, 0.35, 78.0, 120, anchored off Port Atlas north",
        "Naval_Driftwood, anchored, -115.0, 0.35, 854.5, 0, the old drifter in the Outport cove",
        "", "" };
    return i >= 0 && i < kMaxBerths ? k[i] : "";
}
inline const char* DefaultStation(int i)
{
    static const char* k[kMaxStations] = {
        "406.0, 1.25, -145.45, 406.0, 0.35, -151.5, 180, Atlas Marina",
        "315.75, 1.34, -50.0, 322.0, 0.35, -50.0, 90, Fresco Marina",
        "486.55, 1.156, -166.85, 489.3, 0.35, -162.5, 180, Atlas Beach pier",
        "-435.55, 1.05, 188.55, -440.2, 0.35, 188.0, 270, Woods West jetty",
        "-110.0, 1.462, 850.3, -105.5, 0.35, 851.2, 0, Outport wharf",
        "", "", "" };
    return i >= 0 && i < kMaxStations ? k[i] : "";
}
inline const char* DefaultTypes()
{
    return "Naval_Arowana, Naval_Surger, Naval_Skimmer, Naval_Devario, Naval_Catalufa, Naval_Rubadub";
}

// Every berth / station pair: any two boat places closer than minSep (a berth, a station's berth)?
inline bool Crowded(const Config& c, float minSep, int* a, int* b)
{
    float P[kMaxBerths + kMaxStations][2]; bool on[kMaxBerths + kMaxStations];
    for (int i = 0; i < kMaxBerths; ++i) { on[i] = c.berth[i].on; P[i][0] = c.berth[i].pos[0]; P[i][1] = c.berth[i].pos[2]; }
    for (int i = 0; i < kMaxStations; ++i) { on[kMaxBerths + i] = c.station[i].on; P[kMaxBerths + i][0] = c.station[i].berth[0]; P[kMaxBerths + i][1] = c.station[i].berth[2]; }
    for (int i = 0; i < kMaxBerths + kMaxStations; ++i)
        for (int j = i + 1; j < kMaxBerths + kMaxStations; ++j)
            if (on[i] && on[j] && std::hypot(P[i][0] - P[j][0], P[i][1] - P[j][1]) < minSep) { if (a) *a = i; if (b) *b = j; return true; }
    return false;
}

// The ini reader: get(key, default, out, cap) reads [marina] key (config.h passes IniS). Logs through say.
template <class Get, class Say> inline void Load(Config& c, Get get, Say say)
{
    char buf[256];
    const Config d{};
    get("enable", "1", buf, sizeof(buf)); c.enable = atoi(buf) ? 1 : 0;
    auto num = [&](const char* k, float def, float lo, float hi) {
        char dv[32]; sprintf_s(dv, "%g", def);
        get(k, dv, buf, sizeof(buf));
        char* e = nullptr; float v = strtof(buf, &e);
        if (e == buf || !(v == v)) v = def;
        return v < lo ? lo : v > hi ? hi : v;
    };
    c.nearU = num("near", d.nearU, 20.0f, 300.0f);
    c.farU = num("far", d.farU, c.nearU + 20.0f, 800.0f);
    c.leave = num("leave", d.leave, 20.0f, 600.0f);
    c.reach = num("reach", d.reach, 0.5f, 4.0f);
    c.clear = num("clear", d.clear, 1.0f, 5.0f);
    get("cycle", "1", buf, sizeof(buf)); c.cycle = atoi(buf) ? 1 : 0;
    get("recall", "1", buf, sizeof(buf)); c.recall = atoi(buf) ? 1 : 0;
    get("types", DefaultTypes(), buf, sizeof(buf));
    c.nTypes = ParseTypes(buf, c.types, kMaxTypes);
    char err[96], key[16];
    for (int i = 0; i < kMaxBerths; ++i)
    {
        sprintf_s(key, "berth%d", i + 1);
        get(key, DefaultBerth(i), buf, sizeof(buf));
        if (ParseBerth(buf, c.berth[i], err, sizeof(err)) == 2) say("config: [marina] %s refused - %s\n", key, err);
    }
    for (int i = 0; i < kMaxStations; ++i)
    {
        sprintf_s(key, "station%d", i + 1);
        get(key, DefaultStation(i), buf, sizeof(buf));
        if (ParseStation(buf, c.station[i], err, sizeof(err)) == 2) say("config: [marina] %s refused - %s\n", key, err);
    }
    int a = -1, b = -1;
    if (Crowded(c, 4.0f, &a, &b)) say("config: [marina] two boat places are less than 4 u apart (%d, %d) - their boats would meet\n", a + 1, b + 1);
    int nb = 0, ns = 0;
    for (const auto& s : c.berth) nb += s.on;
    for (const auto& s : c.station) ns += s.on;
    say("config: marina enable=%d berths=%d stations=%d types=%d near=%.0f far=%.0f leave=%.0f reach=%.1f clear=%.1f cycle=%d recall=%d\n",
        c.enable, nb, ns, c.nTypes, c.nearU, c.farU, c.leave, c.reach, c.clear, c.cycle, c.recall);
}

} // namespace marm
