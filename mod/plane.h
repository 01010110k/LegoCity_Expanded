// =============================================================================================
//  plane.h - the engine side of the fixed-wing planes (the model is plane_model.h).
//
//  The 2026-11-13 goal: flyable aeroplanes - the game has no aeroplane mechanics or physics, only
//  helicopters. The game's planes - AIRCRAFT_CLOUD, _HERA, _JUPITER, _UFO (the Extras cheats: Cloud
//  MBRZHV, Hera NGQCTT, Jupiter YJMSJN, UFO NZZJNN; called in at a helipad) - are Hover-class
//  vehicles with the helicopter's pilot user data (HoveringAircraft), so the game flies them as
//  helicopters. lcudrive now flies them as aeroplanes: the same hook and the same recipe as heli.h
//  Fly() - velocities captured before ApiVehicleHover::Update, the engine's writes undone, the
//  model's written instead - only the model is plane_model.h's wing, not heli_model.h's rotor.
//  Everything around the flight stays the helicopter's: boarding, the pilot states, the exits (on a
//  pad, the crash-mat bail-out, lcudrive's exit-anywhere and parachute), free flight (no walls, no
//  Y 35 cap), the wreck fall of an abandoned one.
//
//  Airframes (which preset flies which model, [plane] models picks them). Corrected 2026-11-13 from
//  the game's own icons and mesh renders: none of the four is an aeroplane, so [plane] models is
//  empty by default and they fly as helicopters (heli.h). The real aeroplane - AIRCRAFT_ELIFANT, the
//  airliner on its own wheels - is airplane.h.
//    CLOUD    a jetpack                   -> plane::Jet()
//    HERA     a helicopter (rotor, skids) -> plane::LightPlane()
//    JUPITER  the "Invader" saucer        -> plane::Ufo() (hovers, flat turns)
//    UFO      a saucer pod                -> plane::Ufo()
//  Controls: a controller flies it directly - left stick = bank / pitch (pushed forward = nose
//  down), RT = more throttle, LT = less throttle, and on the ground the brakes. The throttle is a
//  lever: it stays where you leave it. Keyboard: the game's turn / forward fields, W / S as RT / LT.
//
//  Damage (the game never damages a class-6 vehicle in a collision - vehdamage.h): the solver's
//  impulse (the model's dv) on the game's 0.3 / 0.6 / 0.8 ladder - by default the first point at
//  7.5 u/s, two at 15, destroyed at 20 - and a bad touchdown: a hard sink costs 2 (4 when twice the
//  safe rate), a wingtip 1, a nose-first one at 10 u/s or more destroys it.
// =============================================================================================
#pragma once
#include "plane_model.h"

namespace pl {

enum Kind : int { kNone = 0, kJet = 1, kLight = 2, kUfo = 3 };
static const char* KindName(int k) { return k == kJet ? "jet" : k == kLight ? "light plane" : k == kUfo ? "hover craft" : "-"; }

static plane::State g_state;
static void*        g_veh = nullptr;
static int          g_kind = kNone;
static float        g_cool = 0.0f;           // s before another crash can do damage
static uint32_t     g_lastWriteT = 0;        // when we last wrote velocities (a gap un-primes the dv)
static float        g_lastPos[3] = {};
static double       g_t = 0.0;
static bool         g_headerDone = false;

// ---- which plane is this ------------------------------------------------------------------------
static int KindFromName(const char* name, const Config& cfg)
{
    if (_strnicmp(name, "AIRCRAFT_", 9) != 0) return kNone;
    const char* m = name + 9;
    int kind = kNone;
    if      (!_stricmp(m, "CLOUD"))   kind = kJet;
    else if (!_stricmp(m, "HERA"))    kind = kLight;
    else if (!_stricmp(m, "JUPITER")) kind = kUfo;
    else if (!_stricmp(m, "UFO"))     kind = kUfo;
    if (kind == kNone) return kNone;
    // [plane] models: a comma-separated list of the names after AIRCRAFT_ (or "all")
    const char* s = cfg.plane_models;
    if (!_stricmp(s, "all")) return kind;
    const size_t n = strlen(m);
    while (*s)
    {
        while (*s == ',' || *s == ' ') ++s;
        const char* e = s;
        while (*e && *e != ',' && *e != ' ') ++e;
        if (static_cast<size_t>(e - s) == n && !_strnicmp(s, m, n)) return kind;
        s = e;
    }
    return kNone;
}

struct KindCache { void* veh; void* def; int kind; uint32_t gen; };
static KindCache g_kc[6] = {};
static uint32_t  g_kcNext = 0, g_kcGen = 1;  // gen bumps on an ini reload (the list may have changed)

static int KindOf(void* veh, const Config& cfg)
{
    if (!cfg.plane_enable) return kNone;
    void* def = At<void*>(veh, eng::VEH_DEF);
    for (KindCache& c : g_kc) if (c.veh == veh && c.def == def && c.gen == g_kcGen) return c.kind;
    char name[64];
    const int kind = PtrOk(def) && ReadModelName(def, name, sizeof(name)) ? KindFromName(name, cfg) : kNone;
    g_kc[g_kcNext++ % 6] = KindCache{ veh, def, kind, g_kcGen };
    return kind;
}

static plane::Params ParamsFor(int kind, const Config& c)
{
    plane::Params p = kind == kJet ? plane::Jet() : kind == kUfo ? plane::Ufo() : plane::LightPlane();
    p.turnSign = c.plane_turn_sign;
    p.fwdSign  = c.plane_fwd_sign;
    if (c.plane_gear_height >= 0.0f) p.gearHeight = c.plane_gear_height;
    if (c.plane_auto_level >= 0.0f)  p.autoLevel = c.plane_auto_level;
    if (c.plane_level_path_rate >= 0.0f) p.levelPathRate = c.plane_level_path_rate;
    return p;
}

static void LogNewPlane(void* veh, int kind)
{
    char name[64];
    void* def = At<void*>(veh, eng::VEH_DEF);
    if (!PtrOk(def) || !ReadModelName(def, name, sizeof(name))) strcpy_s(name, "?");
    void* inst = At<void*>(veh, eng::VEH_INSTANCE);
    void* body = PtrOk(inst) ? At<void*>(inst, eng::INST_BODY) : nullptr;
    int hcur = -1, hmax = -1;
    vd::Health(veh, hcur, hmax);
    Log(">>> plane: %s flies as a %s  veh=%p  mass=%.1f  health %d/%d  gravity %.2f  hag %.2f\n", name, KindName(kind), veh,
        PtrOk(body) ? At<float>(body, eng::BODY_MASS) : -1.0f, hcur, hmax, At<float>(veh, eng::VEH_GRAVITY),
        std::fmin(hl::HeightAboveGround(veh), 999.0f));
}

static void TelemetryHeader()
{
    if (g_headerDone) return;
    g_headerDone = true;
    mdl::TelemetryLine("P,t,kind,lod,ground,cx,cy,thr,brk,px,py,pz,vx,vy,vz,airspeed,alpha,beta,bank,pitch,gamma,"
                       "liftG,lever,spool,stall,dv,hag,crash,dt\n");
}

// The damage a crash the model reported is worth (the dv ladder is dealt separately).
static int TouchdownDamage(uint8_t why, float speed, float sink, const plane::Params& p)
{
    switch (why)
    {
    case plane::CRASH_SINK:  return sink >= 2.0f * p.safeSink ? 4 : 2;
    case plane::CRASH_BANK:  return 1;
    case plane::CRASH_PITCH: return speed >= 10.0f ? 4 : 2;
    default:                 return 0;
    }
}

// After the engine's Update of the player's plane: undo its writes, fly the wing. False = it stood
// down this frame (the engine's frame stands).
static bool Fly(void* veh, int kind, const float lv0[3], const float av0[3], const Config& cfg, bool telemetry)
{
    const uint32_t now = GetTickCount();
    hl::EjectFromWreck(veh, now);
    const int32_t stage = At<int32_t>(veh, eng::VEH_SIMLOD);
    if (stage == 10) return false;                                     // asleep
    { int hc = 1, hm = 1; if (vd::Health(veh, hc, hm) && hc < 1) return false; }   // destroyed: the game's sequence
    if (!(At<uint8_t>(veh, eng::VEH_FLAGS4) & 1)) return false;        // not spawned yet
    if (At<uint8_t>(veh, eng::VEH_FLAGS5) & 0xA0) return false;        // parked / disabled
    uint8_t* ud = hl::UserData(veh);
    const int udState = hl::UdState(ud);
    if (ud && udState != 2) return false;                              // boarding, landing, getting out
    void* inst = At<void*>(veh, eng::VEH_INSTANCE);
    if (!PtrOk(inst)) return false;
    void* body = At<void*>(inst, eng::INST_BODY);
    if (!PtrOk(body)) return false;

    float pos[3];
    for (int k = 0; k < 3; ++k) pos[k] = At<float>(inst, eng::INST_POS + 4 * k);
    if (veh != g_veh || kind != g_kind)
    {
        g_veh = veh; g_kind = kind; plane::Reset(g_state); g_cool = 0.0f;
        LogNewPlane(veh, kind);
    }
    else
    {
        float d2 = 0.0f;
        for (int k = 0; k < 3; ++k) { const float d = pos[k] - g_lastPos[k]; d2 += d * d; }
        if (d2 > 30.0f * 30.0f) { plane::Reset(g_state); Log(">>> plane: moved %.0f u in a frame - flight state reset\n", std::sqrt(d2)); }
    }
    for (int k = 0; k < 3; ++k) g_lastPos[k] = pos[k];
    if (now - g_lastWriteT > 100) g_state.primed = 0;                   // a gap: last write isn't last frame's
    if (stage != 1 && hl::g_terrain)                                    // reduced sim LOD: probe the ground ourselves
    {
        void* hd = At<void*>(veh, eng::VEH_HDRIVE);
        if (PtrOk(hd)) hl::g_terrain(hd);
    }

    plane::Input in{};
    in.dt = At<float>(veh, eng::VEH_DT);
    for (int k = 0; k < 3; ++k)
    {
        in.right[k] = At<float>(inst, eng::INST_ROW0 + 4 * k);
        in.up[k]    = At<float>(inst, eng::INST_ROW1 + 4 * k);
        in.back[k]  = At<float>(inst, eng::INST_ROW2 + 4 * k);
        in.lv[k] = lv0[k]; in.av[k] = av0[k];
    }
    in.hag = hl::HeightAboveGround(veh);
    in.gravity = At<float>(veh, eng::VEH_GRAVITY);
    in.occupied = 1;                                                    // only ever the player's plane

    // controls: the controller directly when there is one (the game's own turn / forward fields are
    // camera-relative steering), else the game's fields
    void* ctrl = At<void*>(veh, eng::VEH_CTRL);
    bool padIn = false;
    if (cfg.plane_stick_direct)
    {
        pad::Poll();
        if (pad::g_user >= 0 && mdl::KeyDownInGame(-1))
        {
            padIn = true;
            in.cTurn = Clampf(pad::g_lx, -1.0f, 1.0f);
            in.cFwd  = Clampf(pad::g_ly, -1.0f, 1.0f);
            in.throttle = pad::g_rt / 255.0f;
            in.brake    = pad::g_lt / 255.0f;
        }
    }
    if (PtrOk(ctrl))
    {
        if (!padIn)
        {
            in.cTurn = At<float>(ctrl, eng::IN_X);
            in.cFwd  = At<float>(ctrl, eng::IN_Y);
            in.throttle = At<float>(ctrl, eng::CTRL_THROTTLE);
            in.brake    = std::fmax(At<float>(ctrl, eng::CTRL_REVERSE), At<float>(ctrl, eng::CTRL_BRAKE));
        }
        in.cVert = At<float>(ctrl, eng::IN_Z);                         // ascend / descend (the hover craft)
    }

    const plane::Params P = ParamsFor(kind, cfg);
    plane::Output out;
    plane::Update(P, g_state, in, out);
    if (!out.valid) return false;                                      // the engine's frame stands

    // crash damage: the solver's impulse on the ladder, and a bad touchdown
    if (cfg.plane_crash_damage && in.dt > 0.0f && in.dt <= 0.1f)
    {
        g_cool -= in.dt;
        int n = g_cool <= 0.0f ? vd::CrashDamage(out.dv, cfg.plane_damage_speed) : 0;
        const uint8_t why = g_state.crash;
        if (why != plane::CRASH_NONE)
        {
            const int td = TouchdownDamage(why, g_state.impactSpeed, out.sink, P);
            if (td > n) n = td;
            g_state.crash = plane::CRASH_NONE;                          // re-armed: the next one counts too
        }
        if (n > 0 && g_cool <= 0.0f)
        {
            g_cool = 0.5f;                                              // resting contact must not count again
            int hc = -1, hm = -1;
            vd::Health(veh, hc, hm);
            const int left = vd::Damage(veh, n);
            static const char* kWhy[] = { "hit something", "hit something", "landed too hard", "a wingtip hit", "nose / tail first" };
            Log(">>> plane: %s at %.1f u/s (dv %.1f) - %d damage, health %d -> %d of %d%s\n", kWhy[why < 5 ? why : 0],
                why == plane::CRASH_SINK ? out.sink : std::sqrt(lv0[0] * lv0[0] + lv0[1] * lv0[1] + lv0[2] * lv0[2]),
                out.dv, n, hc, left, hm, left == 0 ? " - DESTROYED" : left < 0 ? " (no damage: off or invulnerable)" : "");
            if (left == 0) { hl::g_ct.dead = veh; hl::g_ct.deadT = now; hl::g_ct.ejected = false; return false; }
        }
    }

    for (int k = 0; k < 3; ++k)
    {
        At<float>(body, eng::BODY_LINVEL + 4 * k) = out.lv[k];
        At<float>(body, eng::BODY_ANGVEL + 4 * k) = out.av[k];
    }
    g_lastWriteT = now;

    if (telemetry)
    {
        TelemetryHeader();
        g_t += in.dt;
        mdl::TelemetryLine("P,%.4f,%d,%d,%d,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%.3f,%.3f,%.3f,%.2f,%.2f,%.2f,%.1f,%.1f,%.1f,"
                           "%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%d,%.4f\n",
            g_t, kind, stage, out.ground, in.cTurn, in.cFwd, in.throttle, in.brake, pos[0], pos[1], pos[2],
            out.lv[0], out.lv[1], out.lv[2], out.airspeed, out.alphaDeg, out.betaDeg, out.bankDeg, out.pitchDeg, out.gammaDeg,
            out.liftG, out.lever, out.spool, out.stall, out.dv, in.hag < 1e30f ? in.hag : -1.0f, (int)out.crash, in.dt);
    }
    return true;
}

// Player 2 (lcudrive.cpp SwapCtx): the flight state is per player.
struct Parked { plane::State state; void* veh; int kind; float cool; uint32_t lastWriteT; float lastPos[3]; };
static Parked g_p2 = {};
static void SwapP2()
{
    Parked t;
    t.state = g_state; t.veh = g_veh; t.kind = g_kind; t.cool = g_cool; t.lastWriteT = g_lastWriteT;
    for (int k = 0; k < 3; ++k) t.lastPos[k] = g_lastPos[k];
    g_state = g_p2.state; g_veh = g_p2.veh; g_kind = g_p2.kind; g_cool = g_p2.cool; g_lastWriteT = g_p2.lastWriteT;
    for (int k = 0; k < 3; ++k) g_lastPos[k] = g_p2.lastPos[k];
    g_p2 = t;
}

static void Init() { plane::Reset(g_state); plane::Reset(g_p2.state); }

} // namespace pl
