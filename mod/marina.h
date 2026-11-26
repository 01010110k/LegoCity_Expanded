// =============================================================================================
//  marina.h - marinas, moored / anchored boats and boat STATIONS (2026-11-19, marina1, Notes\56).
//  The decisions are in marina_model.h (pure; test\marina_harness.cpp); the vehicle state machine
//  is sites_model.h's and the engine calls are sites.h's (si::World: SpawnAIVehicle, park, pin,
//  fade, weak refs, the facing watchdog). This file only adds the slots and the post presses.
//
//  The goal (2026-11-19): marinas with spawnable boats, and a kind of summon station for the
//  boats. The marina geometry (jetties, fingers, MARINA signs, the striped posts with a
//  boat sign, lamps) is World\marina's PATCH layer; this is where the boats come from:
//    berth1..12   [marina] - a boat that waits there like a [sites] stand (moored at a marina
//                 finger, or "anchored" out in open water with no map pin). Taken: yours; left
//                 elsewhere: faded once nobody is near it, and the berth fills again.
//    station1..8  [marina] - a boat-station post. A player ON FOOT within `reach` of it presses
//                 SPECIAL (the game's special / action button) -> a boat of the next type in
//                 `types` is brought to the station's berth (marina_model.h Decide: bring / swap
//                 the waiting one for the next type / recall an empty one left elsewhere).
//  How it rides on sites.h (one include in lcudrive.cpp, the hook lines in sites.h marked [marina1]):
//    si::Tick calls Tick() at its end, in the same population-manager update, with the same
//    `spawned` flag (one SpawnAIVehicle call per update between them), the same spawner turn
//    (spt kTurnSites - si::World::Spawn checks it), the same teleport hold (4 s after a jump > 12 u:
//    Notes 48 §4.2's crash), the same story-mission / lcudrive-off rules; a stand and a marina boat
//    never spawn at the same time (g_extraBusy / the stands' spawning states) or onto each other
//    (g_extraNear / the stands' positions). si::Release (unhook, a fault in the stands code) calls
//    Release() here: every boat faded unless a player is in it or within 8 u (then it stays, ours no
//    longer), every pin off, every pending preload given back.
//  Engine facts used here [P unless marked]: the character's pad block = [[ch+0x6A8]+0x18], its held
//    word +0x0C (chute.h Pad / PressedJump, in game); the SPECIAL bit mask u32 at exe+0x1174CBC
//    (wanted.h RVA_MASK_SPECIAL, used to block it while down [P in game]). That SPECIAL at a post
//    does nothing else in the stock game on open ground is [I]; a post stands on a quay / jetty end.
//  Log lines say "sites[101..112]" for berths and "sites[201..208]" for stations (si::World's).
// =============================================================================================
#pragma once
#include "marina_model.h"
#include "sites.h"

namespace mar {

static const uintptr_t RVA_MASK_SPECIAL = 0x1174CBC;      // u32: the SPECIAL bit in the pad words (wanted.h)
enum : int { kBerthLog = 100, kStationLog = 200 };

struct StMem
{
    bool  armed;           // the station has a boat out or on its way
    int   type;            // the last type it brought (index into [marina] types; -1 = none yet)
    bool  swapping;        // a cycle / recall: the old boat faded, the next one wanted
    float idleT;           // armed with no boat for this long (a request that never came true)
    marm::Station def;     // the station line this memory is for
};

static si::Slot g_berth[marm::kMaxBerths] = {};
static si::Slot g_st[marm::kMaxStations] = {};
static StMem    g_sm[marm::kMaxStations] = {};
static uint32_t g_prevHeld[2] = {};
static bool     g_checked = false;

static uint8_t* PadOf(uint8_t* ch)
{
    if (!PtrOk(ch)) return nullptr;
    uint8_t* c = At<uint8_t*>(ch, 0x6A8);
    return PtrOk(c) ? At<uint8_t*>(c, 0x18) : nullptr;
}

static sitem::Params ParamsOf(const Config& cfg, bool station)
{
    sitem::Params p;
    p.nearU = cfg.marina.nearU; p.farU = cfg.marina.farU; p.leaveU = cfg.marina.leave;
    p.park = sitem::ParkKind(sitem::kBoat, cfg.sites_park);            // boats wait unparked unless [sites] park = 2
    if (station) p.clearU = cfg.marina.clear;
    return p;
}

// every vehicle the stands and the marina hold, for the clearance rules
struct Seen { float pos[64][3]; bool ok[64]; void* veh[64]; int n; };
static void Collect(Seen& s)
{
    s.n = 0;
    auto add = [&](const si::Slot& sl) {
        if (s.n >= 64) return;
        void* v = si::Veh(sl);
        s.veh[s.n] = v; s.ok[s.n] = v && ap::VehPos(v, s.pos[s.n]); ++s.n;
    };
    for (const auto& sl : si::g_slot) add(sl);
    for (const auto& sl : g_berth) add(sl);
    for (const auto& sl : g_st) add(sl);
}

static bool Spawning()
{
    for (const auto& sl : g_berth) if (sl.mem.state == sitem::kSpawning) return true;
    for (const auto& sl : g_st) if (sl.mem.state == sitem::kSpawning) return true;
    return false;
}

static bool Near(const float p[3], float r)
{
    float v[3];
    for (const auto& sl : g_berth) if (void* x = si::Veh(sl)) if (ap::VehPos(x, v) && ap::Dist2(v, p) < r) return true;
    for (const auto& sl : g_st) if (void* x = si::Veh(sl)) if (ap::VehPos(x, v) && ap::Dist2(v, p) < r) return true;
    return false;
}

// The facts sites_model.h wants, for one of our slots (as si::Tick builds them for a stand).
static void Facts(const si::Slot& s, void* veh, const float pp[2][3], const bool ppOk[2], const sitem::Params& p, const Seen& seen,
                  float dt, bool live, bool world, bool mission, bool settling, void* pm, bool station, const Config& cfg,
                  bool spawned, sitem::Facts& f)
{
    f = sitem::Facts{};
    si::Look(s, veh, pp, ppOk, p, f);
    f.dt = dt; f.on = live; f.world = world; f.samePm = pm == s.spawnPm; f.mission = mission; f.settling = settling;
    if (station)                                                         // a player at the post is not on the berth
        f.someoneClose = marm::BerthBlocked(s.stand.pos, pp, ppOk, 2, seen.pos, seen.ok, 0, cfg.marina.clear);
    else
    {
        for (int k = 0; k < 2; ++k) if (ppOk[k] && ap::Dist2(pp[k], s.stand.pos) < p.clearU) f.someoneClose = true;
    }
    for (int j = 0; j < seen.n; ++j)                                     // another boat on this berth (not this slot's own)
        if (seen.ok[j] && seen.veh[j] != veh && ap::Dist2(seen.pos[j], s.stand.pos) < (station ? 3.0f : p.clearU)) f.someoneClose = true;
    f.speed = veh ? ap::Speed(veh) : 0.0f;
    int hc = 1, hm = 1;
    f.wrecked = veh && vd::Health(veh, hc, hm) && hc < 1;
    float nose[2], dir[2];
    sitem::HeadingDir(s.stand.heading, dir);
    if (veh && ap::Nose(veh, nose)) { f.facingKnown = true; f.facingDot = nose[0] * dir[0] + nose[1] * dir[1]; }
    f.hold = spawned || Spawning();
    for (const auto& sl : si::g_slot) if (sl.mem.state == sitem::kSpawning) f.hold = true;
}

static bool SameStation(const marm::Station& a, const marm::Station& b)
{
    if (a.on != b.on) return false;
    if (!a.on) return true;
    for (int i = 0; i < 3; ++i) if (a.post[i] != b.post[i] || a.berth[i] != b.berth[i]) return false;
    return a.heading == b.heading;
}

// One press at station i (marm::Decide said what).
static void Act(int i, int act, int player, si::World& w, const Config& cfg, sitem::Facts& f)
{
    si::Slot& s = g_st[i];
    StMem& m = g_sm[i];
    const marm::Station& st = cfg.marina.station[i];
    Log(">>> marina: player %d at the %s post - %s\n", player + 1, st.label[0] ? st.label : "boat station", marm::ActName(act));
    if (act == marm::kBring || act == marm::kCycle || act == marm::kRecall)
    {
        if (act != marm::kBring)
        {
            // the station's own boat, nobody in it and nobody within 3 u of it (Decide): it goes on the player's word
            w.Fade(act == marm::kCycle ? "swapped for the next type at its post" : "called back to its post");
            m.swapping = true;
        }
        m.type = marm::NextType(m.type, cfg.marina.nTypes);
        s.stand = marm::StandOf(st, cfg.marina.types[m.type]);
        s.sendSet = false; s.bad = false; s.said = false; s.name[0] = 0;
        sitem::Reset(s.mem);
        s.mem.cool = act == marm::kBring ? 0.0f : 2.5f;                  // the faded one is gone before the next appears
        m.armed = true; m.idleT = 0.0f;
        Log(">>> marina: %s - %s comes to (%.1f, %.2f, %.1f) heading %.0f\n", st.label, s.stand.model, st.berth[0], st.berth[1],
            st.berth[2], st.heading);
        (void)f;
    }
}

static void ReleaseAll(const Config& cfg, const char* why)
{
    for (int i = 0; i < marm::kMaxBerths; ++i) if (si::Busy(g_berth[i])) si::ReleaseSlot(g_berth[i], kBerthLog + i, cfg, why);
    for (int i = 0; i < marm::kMaxStations; ++i)
    {
        if (si::Busy(g_st[i])) si::ReleaseSlot(g_st[i], kStationLog + i, cfg, why);
        g_sm[i].armed = false; g_sm[i].swapping = false; g_st[i].stand.on = false;
    }
}

// once per world update, at the end of si::Tick (see the head of this file)
static void Tick(void* pm, void* lvl, float dt, const Config& cfg, bool on, bool world, bool mission,
                 const float pp[2][3], const bool ppOk[2], bool& spawned, bool settling)
{
    const bool live = on && cfg.marina.enable && cfg.sites_enable;
    if (!g_checked)
    {
        g_checked = true;
        Log(">>> marina: %d types for the stations; SPECIAL mask 0x%X\n", cfg.marina.nTypes,
            *reinterpret_cast<const uint32_t*>(g_base + RVA_MASK_SPECIAL));
    }
    Seen seen; Collect(seen);
    // ---- the berths: stands of their own
    for (int i = 0; i < marm::kMaxBerths; ++i)
    {
        si::Slot& s = g_berth[i];
        const sitem::Stand& want = cfg.marina.berth[i];
        if (!sitem::SameStand(s.stand, want))
        {
            if (si::Busy(s)) si::ReleaseSlot(s, kBerthLog + i, cfg, "this berth was changed or switched off in [marina]");
            if (si::Busy(s)) continue;
            s.stand = want; s.sendSet = false; s.bad = false; s.said = false; s.name[0] = 0;
            sitem::Reset(s.mem);
        }
        if (!s.stand.on) continue;
        void* veh = si::Veh(s);
        const sitem::Params p = ParamsOf(cfg, false);
        sitem::Facts f;
        Facts(s, veh, pp, ppOk, p, seen, dt, live, world, mission, settling, pm, false, cfg, spawned, f);
        si::World w = { pm, lvl, &cfg, &s, kBerthLog + i, &spawned };
        s.fellBack = false;
        sitem::Tick(w, s.mem, p, f);
        if (s.fellBack && s.mem.state == sitem::kIdle && s.mem.cool > 2.0f) s.mem.cool = 2.0f;
    }
    // ---- the stations: presses first (rising edge of SPECIAL, on foot, at the post)
    int press[2] = { -1, -1 };
    const uint32_t mask = *reinterpret_cast<const uint32_t*>(g_base + RVA_MASK_SPECIAL);
    for (int k = 0; k < 2; ++k)
    {
        uint8_t* ch = wd::RawPlayer(k);
        uint8_t* pad = PadOf(ch);
        const uint32_t held = pad ? At<uint32_t>(pad, 0x0C) : 0u;
        const bool edge = marm::Pressed(held, g_prevHeld[k], mask);
        g_prevHeld[k] = held;
        if (!edge || !live || !world || mission || settling || !ppOk[k]) continue;
        const bool onFoot = (k == 0 ? ::PlayerVehicle() : ::Player2Vehicle()) == nullptr;
        for (int i = 0; i < marm::kMaxStations; ++i)
            if (cfg.marina.station[i].on && marm::AtPost(cfg.marina.station[i].post, pp[k], onFoot, cfg.marina.reach)) { press[k] = i; break; }
    }
    for (int i = 0; i < marm::kMaxStations; ++i)
    {
        si::Slot& s = g_st[i];
        StMem& m = g_sm[i];
        const marm::Station& def = cfg.marina.station[i];
        if (!SameStation(m.def, def))
        {
            if (si::Busy(s)) si::ReleaseSlot(s, kStationLog + i, cfg, "this station was changed or switched off in [marina]");
            if (si::Busy(s)) continue;
            m = StMem{}; m.type = -1; m.def = def;
            s.stand = sitem::Stand{}; sitem::Reset(s.mem);
        }
        void* veh = si::Veh(s);
        const sitem::Params p = ParamsOf(cfg, true);
        sitem::Facts f;
        Facts(s, veh, pp, ppOk, p, seen, dt, live, world, mission, settling, pm, true, cfg, spawned, f);
        si::World w = { pm, lvl, &cfg, &s, kStationLog + i, &spawned };
        for (int k = 0; k < 2; ++k)
        {
            if (press[k] != i) continue;
            bool boatNear = false;
            float vp[3];
            if (veh && ap::VehPos(veh, vp))
                for (int q = 0; q < 2; ++q) if (ppOk[q] && ap::Dist2(pp[q], vp) < 3.0f) boatNear = true;
            const int act = marm::Decide(def.on, m.armed, s.mem.state, veh != nullptr, f.aboard, boatNear, f.onStand, cfg.marina);
            Act(i, act, k, w, cfg, f);
            veh = si::Veh(s);
            Facts(s, veh, pp, ppOk, p, seen, dt, live, world, mission, settling, pm, true, cfg, spawned, f);
            break;                                                       // one press per station per update
        }
        if (!m.armed && !si::Busy(s)) continue;
        if (!s.stand.on && !si::Busy(s)) { m.armed = false; continue; }
        const int before = s.mem.state;
        s.fellBack = false;
        sitem::Tick(w, s.mem, p, f);
        if (s.fellBack && s.mem.state == sitem::kIdle && s.mem.cool > 2.0f) s.mem.cool = 2.0f;
        const int after = s.mem.state;
        if (after == sitem::kSpawning || after == sitem::kParked) m.swapping = false;
        if (marm::Disarm(before, after, m.swapping))
        {
            Log(">>> marina: %s - its boat has gone; the post waits for the next press\n", def.label);
            m.armed = false; s.stand.on = false;
        }
        m.idleT = (m.armed && after == sitem::kIdle && !si::Veh(s)) ? m.idleT + dt : 0.0f;
        if (m.armed && m.idleT > 30.0f)
        {
            Log(">>> marina: %s - no boat came in 30 s (nobody near the berth any more, or the berth kept busy); request dropped\n", def.label);
            m.armed = false; m.swapping = false; s.stand.on = false; m.idleT = 0.0f;
        }
    }
}

static void Release(const Config& cfg, const char* why) { ReleaseAll(cfg, why); }

static struct Hook
{
    Hook() { si::g_after = &Tick; si::g_afterRelease = &Release; si::g_extraBusy = &Spawning; si::g_extraNear = &Near; }
} g_hook;

} // namespace mar
