// =============================================================================================
//  wanted_busted.h - busted: fade out, and only the busted player wakes up OUTSIDE the police
//  station (2026-11-18, "busted1"). Included by wanted.h (inside namespace wd), before BustTick.
//  The decisions are busted_model.h's (pure, in the wanted harness); this file is the engine side.
//
//  WHY (co-op): a busted player fades out and is moved to outside the police station instead of
//  inside it, because the trip inside forces the other player to end up there too.
//  The old trip is FN_STATION 0x14003ACA0(_, 1, _) = the pause menu's "return to the
//  police station" = GoToLevel(Police_Station_Lobby): a level change is the whole game's, so both
//  players took the loading screen and landed in the lobby (Notes\32 5.3) [P].
//
//  WHAT, all inside the open city, no level change - the game's own far move, as its scripts do
//  it for ONE player at a time (train_control.sf TeleportTrain: FadeScreen, cPlayerN.Teleport +
//  LockInPlace(true,"idle"), wait for the tiles, LockInPlace(false) + Teleport, FadeScreen(false)):
//    * Character_Teleport's own two calls [P exe bytes, SAction 0x140114DD0]:
//        0x1406709F0(ch, &{x,y,z,1})  -> 0x140670C00: CanTeleport(ch,0) 0x140768CA0, entity
//                                        SetPosition 0x1404A07F0, the character's physics re-seat
//                                        0x140672430; returns 0 = done, 1 = refused
//        0x14066F720(ch, u16 heading, 1)   heading in 65536ths of a turn -> ch+0x218 + the rows
//    * Character_ClearVehicle's own call [P exe bytes, CharacterAction 0x1401105D0]:
//        ride = *(ch+0x340); ride->vt[68](ride, &{int 1, ..., qword 0 at +0x20, byte 0 at +0x28})
//      - the same jump-off dismount heli.h has used since 2026-10 (OffPadExit, EjectFromWreck).
//    * LockInPlace / ForceDefault: wanted.h's (the cuffs use them already).
//    * IsOnGround 0x14066BD20 (chute.h, jetpack.h, ptero.h).
//  WHERE: the locator the story itself teleports Chase to when he walks out of the station in
//  Act 1 (exit_lcpd.sf: cPlayer1.Teleport("Story_A1_M2_ExitPoliceStation_PlayerWalkTo")) [P data,
//  lego_city_ai.led]: (-178.291, 3.343, -222.379) on the forecourt, 2.9 u from the door hotspot
//  HUB_Access_PoliceStation (-179.313, 3.351, -225.102; it needs a button press anyway). Facing
//  20 degrees = straight away from the door, toward the street. All four in the ini.
//  THE BLACK: lcudrive's own overlay through the 3Dmigoto bridge (IniParams 7.z player 1, 7.w
//  player 2; ShaderFixes\busted_fade.hlsl) - with two players only the busted player's half of the
//  screen goes black. The engine's FadeScreen is the whole screen's and wants a script context as
//  its owner (Notes\32 5.3 [U]) - not used. What is published is a deadline, not an opacity
//  (busted_model.h Deadline): if nothing publishes any more, the black is gone in 1.5 s by itself.
//  Without the shader files installed the move still happens, just without the fade.
//
//  The other player: no call here takes them, reads their pad or moves their camera; the only
//  thing read is where they stand (to step aside if they're on the spot). Sites, spots and life
//  see a > 12 u jump and hold for 4 s by themselves (sites.h Settle) - that is wanted here too.
//
//  [wanted] busted_outside = 0 gives the old behaviour back exactly (BoStart returns false and
//  BustTick runs its old lines). busted_station = 0 (let go on the spot) still wins over both.
// =============================================================================================
#pragma once
#include "busted_model.h"

static const uintptr_t FN_CH_TELEPORT = 0x6709F0, FN_CH_HEADING = 0x66F720, FN_CH_ONGROUND = 0x66BD20;
typedef int(__fastcall* ChTeleFn)(uint8_t*, const float*);
typedef void(__fastcall* ChHeadFn)(uint8_t*, int, bool);
typedef bool(__fastcall* ChGroundFn)(uint8_t*);
typedef void(__fastcall* RideOffFn)(void* ride, void* params);

struct BustOut
{
    busted::Seq s;
    uint8_t*    ch;          // whose
    float       dest[3];
    float       hdg;         // degrees
    float       from[3];     // where he was busted (the log)
    uint32_t    lastMs;      // the last tick (after a gap he must still be where the sequence left him)
    bool        wasRiding;
};
static BustOut        g_bo[2] = {};
static busted::Params g_boP;
// the black, for lcudrive.cpp's FrameStep: 0..1 per player, and when it was last set
static volatile float g_boFade[2] = { 0.0f, 0.0f };
static volatile LONG  g_boFadeMs[2] = { 0, 0 };

// For the bridge: this player's black now. A value nobody has refreshed for half a second is 0.
static float BoFade(int i, uint32_t now)
{
    const float f = g_boFade[i & 1];
    if (!(f > 0.0f)) return 0.0f;
    return now - static_cast<uint32_t>(g_boFadeMs[i & 1]) > 500 ? 0.0f : (f > 1.0f ? 1.0f : f);
}
static void BoSetFade(int i, float f)
{
    g_boFade[i & 1] = f;
    InterlockedExchange(&g_boFadeMs[i & 1], static_cast<LONG>(GetTickCount()));
}

static bool BoRiding(uint8_t* p)
{
    if (PlayerVehicle()) return true;
    void* ride = At<void*>(p, eng::CH_RIDE);
    return PtrOk(ride) && At<int32_t>(ride, eng::RIDE_STATE) == 4;
}

// Character_ClearVehicle, as the game's script action does it (0x1401105D0).
static bool BoClearVehicle(uint8_t* p)
{
    void* ride = At<void*>(p, eng::CH_RIDE);
    if (!PtrOk(ride)) return false;
    void** rvt = At<void**>(ride, 0);
    const RideOffFn off = PtrOk(rvt) ? reinterpret_cast<RideOffFn>(rvt[eng::SLOT_DISMOUNT]) : nullptr;
    if (!hl::InImage(reinterpret_cast<const void*>(off))) return false;
    alignas(16) uint8_t prm[0x40] = {};
    *reinterpret_cast<int32_t*>(prm) = 1;                              // type 1 (+0x20 qword 0, +0x28 byte 0)
    off(ride, prm);
    return true;
}

static bool BoBusy() { return busted::Busy(g_bo[g_sus & 1].s); }
// Will a bust end outside the station? busted_station = 0 still means "let go on the spot, no station at all".
static bool BoWill(const Config& cfg) { return busted::Will(cfg.wanted_busted_outside, cfg.wanted_busted_station); }

// Let go of whatever the sequence holds - wanted mode off, `[`, the unhook, a fault's clean-up
// (StandDown). `p` = that player's character now (null = gone: nothing is called).
static void BoAbort(int i, uint8_t* p)
{
    BustOut& o = g_bo[i & 1];
    if (!busted::Busy(o.s)) return;
    if (PtrOk(p) && p == o.ch && IsIn(p, CTX_IDLENOGRAV)) reinterpret_cast<ChFn>(g_base + FN_FORCEDEF)(p);
    Log("    wanted: %s - the move to the station's forecourt is dropped where it stood (wanted mode stood down)\n", Pn(i));
    o = BustOut{};
    BoSetFade(i, 0.0f);
}

// A new world (a level load): the characters are other objects now - nothing is called on them.
static void BoForgetAll()
{
    for (int i = 0; i < 2; ++i)
    {
        if (busted::Busy(g_bo[i].s) && busted::Holding(g_bo[i].s)) g_pl[i].bust = {};   // or the bust would start it again here
        g_bo[i] = BustOut{}; g_boFade[i] = 0.0f;
    }
}

// The bust's beat is over (BustTick). true = the sequence has it from here; false = the old way.
// Runs AsPlayer(the busted one).
static bool BoStart(const Config& cfg)
{
    if (!BoWill(cfg)) return false;
    const int who = g_sus & 1;
    uint8_t* p = P1();
    if (!PtrOk(p)) return false;
    BustOut& o = g_bo[who];
    o = BustOut{};
    o.ch = p;
    o.hdg = cfg.wanted_busted_outside_heading;
    const float dest[3] = { cfg.wanted_busted_outside_x, cfg.wanted_busted_outside_y, cfg.wanted_busted_outside_z };
    // the other player on the spot already (busted a moment ago): one step to the side
    bool otherThere = false;
    if (uint8_t* q = RawPlayer(1 - who))
        if (q != p)
        {
            const float dx = At<float>(q, 0xA8) - dest[0], dz = At<float>(q, 0xB0) - dest[2];
            otherThere = dx * dx + dz * dz < 1.2f * 1.2f;
        }
    busted::Spot(dest, o.hdg, otherThere, o.dest);
    float jump = 1.0e9f;
    if (YouPos(o.from))
    {
        const float dx = o.from[0] - o.dest[0], dy = o.from[1] - o.dest[1], dz = o.from[2] - o.dest[2];
        jump = std::sqrt(dx * dx + dy * dy + dz * dz);
    }
    g_boP = busted::Params{};
    g_boP.fadeS = cfg.wanted_busted_fade;
    g_boP.holdFarS = cfg.wanted_busted_outside_load;
    o.wasRiding = BoRiding(p);
    busted::Start(o.s, jump, g_boP);
    o.lastMs = GetTickCount();
    Log(">>> wanted: %s - fade out; off to the pavement outside the police station (%.1f, %.1f, %.1f), %.0f u away%s%s - "
        "only %s goes, no level change ([wanted] busted_outside = 0 for the old trip)\n",
        Pn(), o.dest[0], o.dest[1], o.dest[2], jump < 1.0e8f ? jump : -1.0f,
        o.wasRiding ? ", out of the vehicle first" : "", otherThere ? ", a step aside (the other player is on the spot)" : "", Pn());
    return true;
}

// Every tick while BoBusy(), AsPlayer(the busted one). true = the wanted system's bust is over NOW
// (BustTick clears it); the sequence may run on a little (the fade in, the watch for a fall).
static bool BoTick(float dt, const Config&)
{
    const int who = g_sus & 1;
    BustOut& o = g_bo[who];
    uint8_t* p = P1();
    const uint32_t now = GetTickCount();
    busted::Input in{};
    in.dt = dt;
    // the same character. After a gap in the ticks (the pause menu - or a loading screen that gave a new
    // character the old address): only if he is still where the sequence left him
    in.present = PtrOk(p) && p == o.ch;
    if (in.present && now - o.lastMs >= 2000)
    {
        const float* ref = o.s.moved ? o.dest : o.from;
        const float dx = At<float>(p, 0xA8) - ref[0], dy = At<float>(p, 0xAC) - ref[1], dz = At<float>(p, 0xB0) - ref[2];
        const float d = std::sqrt(dx * dx + dy * dy + dz * dz);
        in.present = IsFin(d) && d < (o.s.moved ? 6.0f : 40.0f);
        Log("    wanted: %s - %.0f s without a tick in the middle of the move outside the station (a pause?) - %s\n", Pn(),
            (now - o.lastMs) / 1000.0f, in.present ? "carrying on" : "another place now: dropped");
    }
    o.lastMs = now;
    if (in.present)
    {
        in.riding = BoRiding(p);
        in.grounded = reinterpret_cast<ChGroundFn>(g_base + FN_CH_ONGROUND)(p);
        const float y = At<float>(p, 0xAC);
        in.fell = o.s.moved && IsFin(y) && y < o.dest[1] - g_boP.fallU;
    }
    const busted::Phase before = o.s.ph;
    const uint32_t act = busted::Step(o.s, in, g_boP);
    if (in.present)
    {
        if (act & busted::aDismount)
        {
            const bool ok = BoClearVehicle(p);
            Log("    wanted: %s - out of the vehicle (the game's ClearVehicle)%s\n", Pn(), ok ? "" : " - nothing to get out of");
        }
        if ((act & busted::aUnlock) && IsIn(p, CTX_IDLENOGRAV)) reinterpret_cast<ChFn>(g_base + FN_FORCEDEF)(p);
        if (act & busted::aTeleport)
        {
            const float pos[4] = { o.dest[0], o.dest[1], o.dest[2], 1.0f };
            const bool ok = reinterpret_cast<ChTeleFn>(g_base + FN_CH_TELEPORT)(p, pos) == 0;
            const bool first = before == busted::kMove;
            busted::Moved(o.s, ok);
            if (ok && (act & busted::aLock)) LockInPlace(p, "idle");
            if (first)
                Log(ok ? "    wanted: %s is moved to the station's forecourt - held there (black) while the place loads\n"
                       : "    wanted: %s - the game refused the teleport (CanTeleport) - trying again\n", Pn());
        }
        if (act & busted::aFace)
            reinterpret_cast<ChHeadFn>(g_base + FN_CH_HEADING)(p, busted::HeadingU16(o.hdg), true);
    }
    if (act & busted::aFell)
        Log(">>> wanted: %s fell through the ground at the station (it had not loaded) - back to the spot, try %d\n", Pn(), o.s.retries);
    if (act & busted::aRelease)
    {
        static const char* const kWhy[6] = { "", "still in the vehicle", "the game refused the teleport", "fell through the ground too often",
                                             "it took too long", "the player is gone" };
        if (act & busted::aGaveUp)
            Log(">>> wanted: %s is let go where they are - the move outside the station could not be done (%s)\n", Pn(),
                kWhy[o.s.gaveUp >= 0 && o.s.gaveUp < 6 ? o.s.gaveUp : 0]);
        else
            Log(">>> wanted: %s is outside the police station, %.0f u from where they were busted - fade in; grace %.0f s left\n",
                Pn(), std::sqrt((o.from[0] - o.dest[0]) * (o.from[0] - o.dest[0]) + (o.from[2] - o.dest[2]) * (o.from[2] - o.dest[2])),
                Me().st.graceT);
    }
    else if ((act & busted::aDone) && (act & busted::aGaveUp) && o.s.gaveUp >= 3)   // (1 and 2 were said at the release)
        Log("    wanted: %s - the move outside the station ended early (%s)\n", Pn(),
            o.s.gaveUp == 3 ? "fell through the ground too often - left to the game's own rescue" : o.s.gaveUp == 4 ? "it took too long"
            : "the player is gone");
    BoSetFade(who, o.s.fade);
    if (act & busted::aDone) { o = BustOut{}; BoSetFade(who, 0.0f); }
    return (act & busted::aRelease) != 0;
}
