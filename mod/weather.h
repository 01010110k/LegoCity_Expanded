// =============================================================================================
//  weather.h - rain, fog and weather zones: the engine side (2026-11-18, Notes\53).
//  weather_model.h holds every decision (pure, test\weather_harness.cpp); this file only reads the
//  camera, hands the model's numbers to the engine and to the shaders, and logs.
//
//  WHERE IT HOOKS [P disasm, Notes\53 section 2]
//    The engine builds one PostEffect settings object per view (camera) per frame:
//      0x140375FE0  PostEffectManager::ComputeSettings(mgr, view, out)
//         resets `out`, blends the level's PostEffect volumes into it by the view's camera position
//         (cam = 0x14032A620(view); pos = cam + 0x30), then
//      0x140376736  call 0x14037B8C0 (&g_postFx, view, out)     <- the ONE call site, patched here
//         the evaluator: turns the enable bytes into the frame's stage mask and applies the
//         per-stage sanity rules (e.g. rain off when Density <= 0.0001).
//    Our replacement edits `out` first and then calls the original. `out` is rebuilt from the level
//    data every frame, so nothing we write outlives the frame: no restore, nothing to undo on unhook.
//
//  WHAT IT WRITES into the settings (offsets = the reflection table at 0x140B765E0, component
//  offset - 0xF0, cross-checked against the defaults the constructor 0x140378D10 writes):
//    Exponential Height Fog   +0x7E0 enable, +0x7E8 Density At Origin, +0x7EC Vertical Thickness,
//                             +0x7F4 Near Distance, +0x7F8 Near Density, +0x7FC Albedo rgb
//    Rain (experimental)      +0x358 enable, +0x360 Direction, +0x374 Density, +0x3AC Foreground Speed,
//                             +0x3B8 Foreground Alpha, +0x4B0 Splash Alpha
//  and to the shaders, through the IniParams bridge (sim_bridge.h): 4.y rain, 4.z overcast, 4.w mist,
//  7.y gust - read by the LCUR_ block in the 13 main-filter scene passes.
//
//  CO-OP: every view has its own camera, so each split-screen half gets the weather of where its
//  camera is (the engine fog, the engine rain). The shader floats are one set for the whole frame:
//  they take the STRONGER of the views' values, so the screen rain / grey light show on both halves
//  while either player is in a zone (limit, Notes\53 section 6).
// =============================================================================================
#pragma once
#include "weather_model.h"

namespace wx {

static const uintptr_t SITE_EVAL = 0x376736;        // the E8 byte: call 0x14037B8C0
static const uintptr_t FN_EVAL   = 0x37B8C0;        // (mgr, view, settings)
static const uintptr_t RVA_DEFAULT_CAM = 0x1A99820; // 0x14032A620's third answer
static const uintptr_t VIEW_CAM_A_ON = 0x144, VIEW_CAM_A = 0x150, VIEW_CAM_B_ON = 0x145, VIEW_CAM_B = 0x4C0;
static const uintptr_t CAM_POS = 0x30;              // float x, y, z (movaps [cam+0x30] at 0x140376060)

static const uintptr_t S_FOG_ON = 0x7E0, S_FOG_DENS = 0x7E8, S_FOG_THICK = 0x7EC, S_FOG_NEARDIST = 0x7F4,
                       S_FOG_NEARDENS = 0x7F8, S_FOG_ALBEDO = 0x7FC;
static const uintptr_t S_RAIN_ON = 0x358, S_RAIN_DIR = 0x360, S_RAIN_DENSITY = 0x374, S_RAIN_SPEED = 0x3AC,
                       S_RAIN_FGALPHA = 0x3B8, S_RAIN_SPLASHALPHA = 0x4B0;

typedef uintptr_t(__fastcall* EvalFn)(void* mgr, void* view, uint8_t* settings);
static EvalFn g_origEval = nullptr;

struct View
{
    void*    key;
    uint32_t seen, logAt;
    bool     used;
    wxm::Mix cur;
    wxm::Latch fog, rain, inZone;
    int      zone;
    wxm::ShaderOut sh;
};
enum : int { kViews = 12, kPlayers = 2 };       // (views: both players' cameras and whatever else asks for settings)
static View g_view[kViews];
static View g_pl[kPlayers];                  // the fallback: the weather at the players (no view seen)
static volatile LONG g_viewSeen = 0;         // GetTickCount of the last view frame
static bool  g_camLogged = false;
static bool  g_camBadLogged = false;
static int   g_fogBad = 0;                   // frames whose fog block did not look like one
static bool  g_fogRefused = false;           // ... too many: the engine fog is left alone for the session
static wxm::LevelFog g_levelLogged = {};
static uint32_t g_levelLogAt = 0;
static volatile bool g_publishedNonZero = false;
static volatile uint32_t g_lastPublishMs = 0;      // Publish's last world update (the loading-screen watch)
static uint32_t g_stamp = 0;

static wxm::Params ParamsOf(const Config& c)
{
    wxm::Params p;
    p.visLight = c.weather_vis_light; p.visHeavy = c.weather_vis_heavy;
    p.lowLight = c.weather_low_light; p.lowHeavy = c.weather_low_heavy;
    p.thickLight = c.weather_thick_light; p.thickHeavy = c.weather_thick_heavy;
    p.weird = c.weather_weird; p.blend = c.weather_blend; p.nightFog = c.weather_night_fog;
    p.overcast = c.weather_overcast; p.mist = c.weather_mist; p.shower = c.weather_shower; p.spell = c.weather_spell; p.wetShare = c.weather_wet_share;
    p.screenRain = c.weather_screen_rain != 0; p.engineRain = c.weather_engine_rain != 0; p.engineFog = c.weather_engine_fog != 0;
    return p;
}

static double Seconds() { return static_cast<double>(GetTickCount64()) / 1000.0; }
// the first weather frame of this game session: "Changing" weather starts dry from here (weather_model.h SpellAt)
static double SessionStart()
{
    static double s = -1.0;
    if (s < 0.0) s = Seconds();
    return s;
}
// "Changing" weather: a new sequence of spells every game session (taken once, on first use).
static uint32_t SessionSeed()
{
    static uint32_t s = 0;
    if (!s) { s = static_cast<uint32_t>(GetTickCount64() * 2654435761ull) ^ GetCurrentProcessId() * 40503u; if (!s) s = 1; }
    return s;
}

// [br3] no weather effects in the Backrooms (fog leaked into a room that lies under the fishing village's
// zone): the zones are 2-D (x / z), so a view deep below a zone was in it.  Below
// this height (the Backrooms lie under y -19.7; nothing above ground is this low) a view has NO weather at all - no
// zone, its state snapped to nothing at once (no fade), its engine fog and rain switched off - per view, so in co-op
// the partner above keeps his.
static const float kNoWeatherBelowY = -15.0f;

// 2026-11-25: no rain inside interiors. The police station and every other interior is its own level, and the
// weather is the city's. Not in the city (the game's own flag, the one the map's location line uses: an interior
// shows its level name) = no weather, snapped to nothing at once like the Backrooms. Unreadable = treated as the city.
static bool Indoors()
{
    __try
    {
        uint8_t* game = *reinterpret_cast<uint8_t**>(g_base + 0x1C7A830);        // maplabels.h RVA_GAME
        return PtrOk(game) && At<uint8_t>(game, 0x335D) == 0;                    // GAME_INCITY
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// 0 day .. 1 night, from the one shader clock (lights.h UpdateNight's): the sun is at sin(clock * 0.01309).
static float Night01()
{
    if (!vsim::BridgeLockedQ(g_bridge)) return 0.0f;
    const float elev = std::sin(vsim::BridgeClock(g_bridge) * 0.01309f);
    return wxm::Smooth((0.10f - elev) / 0.30f);
}

static bool PlayerPos(int i, float out[3])
{
    uint8_t* ch = i == 0 ? *reinterpret_cast<uint8_t**>(g_base + eng::RVA_PLAYER1) : Player2Char();
    if (!PtrOk(ch)) return false;
    for (int k = 0; k < 3; ++k) out[k] = At<float>(ch, 0xA8 + 4 * k);
    return IsFin(out[0]) && IsFin(out[1]) && IsFin(out[2]) && std::fabs(out[0]) < 20000.0f && std::fabs(out[2]) < 20000.0f;
}

// The view's camera position (0x14032A620's three cases, then +0x30).
static bool CameraPos(void* view, float out[3])
{
    if (!PtrOk(view)) return false;
    uint8_t* v = static_cast<uint8_t*>(view);
    uint8_t* cam = v[VIEW_CAM_A_ON] ? v + VIEW_CAM_A : v[VIEW_CAM_B_ON] ? v + VIEW_CAM_B
                                                     : reinterpret_cast<uint8_t*>(g_base + RVA_DEFAULT_CAM);
    if (!PtrOk(cam)) return false;
    for (int k = 0; k < 3; ++k) out[k] = At<float>(cam, CAM_POS + 4 * k);
    return IsFin(out[0]) && IsFin(out[1]) && IsFin(out[2]) && std::fabs(out[0]) < 20000.0f && std::fabs(out[2]) < 20000.0f;
}

static View* ViewOf(void* key, uint32_t now)
{
    View* free = nullptr; View* oldest = &g_view[0];
    for (View& v : g_view)
    {
        if (v.used && v.key == key) return &v;
        if (v.used && now - v.seen > 3000) v = View{};              // a view that stopped drawing
        if (!v.used && !free) free = &v;
        if (v.seen < oldest->seen) oldest = &v;
    }
    View* v = free ? free : oldest;
    *v = View{};
    v->used = true; v->key = key; v->seen = now; v->zone = -1;
    return v;
}

static void ZoneLog(View& v, const Config& cfg, int zone, float weight, const char* who, int idx)
{
    const bool was = v.inZone.on;
    const bool in = v.inZone.Update(zone >= 0 ? weight : 0.0f, 0.5f, 0.25f);
    if (in && !was && zone >= 0)
    {
        v.zone = zone;
        Log(">>> weather: %s %d comes into the zone \"%s\" (fog %s, rain %s)\n", who, idx, cfg.weather_data.zones[zone].name,
            wxm::FogName(cfg.weather_data.zones[zone].fog), wxm::RainName(cfg.weather_data.zones[zone].rain));
    }
    else if (!in && was)
    {
        Log(">>> weather: %s %d has left the zone \"%s\"\n", who, idx,
            v.zone >= 0 && v.zone < cfg.weather_data.nZones ? cfg.weather_data.zones[v.zone].name : "?");
        v.zone = -1;
    }
}

// One step of one weather state (a view's, or a player's in the fallback) at a place.
static void StepAt(View& v, const float pos[3], const Config& cfg, bool on, uint32_t now, const wxm::Params& p,
                   const char* who, int idx)
{
    float dt = v.seen ? (now - v.seen) / 1000.0f : 0.0f;
    if (!(dt >= 0.0f) || dt > 1.0f) dt = 0.0f;                      // first frame / back after a long gap: no step
    v.seen = now;
    wxm::Mix tgt = {};
    int zone = -1; float zw = 0.0f;
    const bool below = pos[1] < kNoWeatherBelowY || Indoors();      // [br3] + interiors (2026-11-25)
    if (on && !below)
    {
        const double now_s = Seconds();
        const wxm::Mix user = wxm::UserMix(cfg.weather_rain, cfg.weather_fog, now_s, p, SessionSeed(), SessionStart());
        tgt = wxm::TargetMix(cfg.weather_data, pos[0], pos[2], user, cfg.weather_zones != 0, now_s, p, &zone, &zw);
    }
    if (below) v.cur = wxm::Mix{};                                  // [br3] at once, no lingering fade
    wxm::Step(v.cur, tgt, dt, p.blend);
    v.sh = wxm::MapShader(v.cur, p);
    ZoneLog(v, cfg, on ? zone : -1, zw, who, idx);
}

// ---- the hook's work: one view, this frame ---------------------------------------------------------
static void ViewFrame(void* view, uint8_t* s, const Config& cfg, bool on, uint32_t now)
{
    if (cfg.weather_stamp != g_stamp)                               // new zone data: every state starts from its place again
    {
        g_stamp = cfg.weather_stamp;
        for (View& v : g_view) { v.inZone.on = false; v.zone = -1; }
        for (View& v : g_pl) { v.inZone.on = false; v.zone = -1; }
    }
    float pos[3] = {};
    bool havePos = CameraPos(view, pos);
    if (havePos && !g_camLogged)
    {
        g_camLogged = true;
        float pp[3] = {};
        if (PlayerPos(0, pp))
        {
            const float dx = pos[0] - pp[0], dz = pos[2] - pp[2];
            Log(">>> weather: view %p looks from (%.1f, %.1f, %.1f); player 1 is at (%.1f, %.1f, %.1f), %.1f u away\n", view,
                pos[0], pos[1], pos[2], pp[0], pp[1], pp[2], std::sqrt(dx * dx + dz * dz));
        }
        else Log(">>> weather: view %p looks from (%.1f, %.1f, %.1f) (no player yet)\n", view, pos[0], pos[1], pos[2]);
    }
    if (!havePos)
    {
        if (!g_camBadLogged) { g_camBadLogged = true; Log(">>> weather: a view without a usable camera position - player 1's place is used for it\n"); }
        havePos = PlayerPos(0, pos);
    }
    View* v = ViewOf(view, now);
    const wxm::Params p = ParamsOf(cfg);
    const int idx = static_cast<int>(v - g_view);
    if (!havePos)
    {
        // 2026-11-25: was `seen = now; return` - the view's LAST rain stayed in v->sh and Publish kept sending it
        // (rain in the Backrooms). Weather off: step it to nothing; on: hold (no place to ask).
        if (!on) { const float nowhere[3] = { 0.0f, -1000.0f, 0.0f }; StepAt(*v, nowhere, cfg, false, now, p, "view", idx); }
        else v->seen = now;
        return;
    }
    StepAt(*v, pos, cfg, on, now, p, "view", idx);
    InterlockedExchange(&g_viewSeen, static_cast<LONG>(now));
    if (!PtrOk(s)) return;
    if (pos[1] < kNoWeatherBelowY)                                  // [br3] down there: no height fog, no rain, whatever was written before
    {
        At<uint8_t>(s, S_FOG_ON) = 0;
        At<uint8_t>(s, S_RAIN_ON) = 0;
        v->fog = wxm::Latch{}; v->rain = wxm::Latch{};
        return;
    }

    // ---- the engine's height fog ----
    bool wroteFog = false;
    wxm::LevelFog lv = {};
    if (p.engineFog && !g_fogRefused)
    {
        const uint8_t fogOn = At<uint8_t>(s, S_FOG_ON);
        lv.on = fogOn != 0;
        lv.dens = At<float>(s, S_FOG_DENS); lv.thick = At<float>(s, S_FOG_THICK);
        lv.nearDist = At<float>(s, S_FOG_NEARDIST); lv.nearDens = At<float>(s, S_FOG_NEARDENS);
        for (int k = 0; k < 3; ++k) lv.albedo[k] = At<float>(s, S_FOG_ALBEDO + 4 * k);
        if (fogOn > 1 || !wxm::LevelFogSane(lv))
        {
            if (++g_fogBad >= 120)
            {
                g_fogRefused = true;
                Log("!!! weather: the height-fog settings do not look as expected (enable %u, density %g, thickness %g, near %g x %g) - "
                    "the engine fog is left alone for this session; rain and the screen effects carry on\n", fogOn, lv.dens, lv.thick,
                    lv.nearDist, lv.nearDens);
            }
        }
        else
        {
            if (g_fogBad > 0) --g_fogBad;
            // the level's own fog, logged when it changes (what the presets are measured against)
            const bool changed = lv.on != g_levelLogged.on || std::fabs(lv.dens - g_levelLogged.dens) > 0.02f * (std::fabs(lv.dens) + 0.01f) ||
                                 std::fabs(lv.thick - g_levelLogged.thick) > 0.02f * (std::fabs(lv.thick) + 0.1f) ||
                                 std::fabs(lv.nearDist - g_levelLogged.nearDist) > 0.02f * (std::fabs(lv.nearDist) + 0.1f);
            if (changed && (g_levelLogAt == 0 || now - g_levelLogAt > 10000))
            {
                g_levelLogged = lv; g_levelLogAt = now ? now : 1;
                Log(">>> weather: the level's height fog: %s, density %.4f, thickness %.2f, near %.1f u x %.4f, albedo (%.2f %.2f %.2f)\n",
                    lv.on ? "on" : "off", lv.dens, lv.thick, lv.nearDist, lv.nearDens, lv.albedo[0], lv.albedo[1], lv.albedo[2]);
            }
            const wxm::FogOut fo = wxm::MapFog(lv, v->cur, Night01(), p, v->fog);
            if (fo.write)
            {
                At<uint8_t>(s, S_FOG_ON) = 1;
                At<float>(s, S_FOG_DENS) = fo.dens; At<float>(s, S_FOG_THICK) = fo.thick;
                At<float>(s, S_FOG_NEARDIST) = fo.nearDist; At<float>(s, S_FOG_NEARDENS) = fo.nearDens;
                for (int k = 0; k < 3; ++k) At<float>(s, S_FOG_ALBEDO + 4 * k) = fo.albedo[k];
                wroteFog = true;
            }
        }
    }
    // ---- the engine's own rain stage (experimental) ----
    const wxm::RainOut ro = wxm::MapRain(v->cur, p, v->rain);
    if (ro.write && ro.on)
    {
        At<uint8_t>(s, S_RAIN_ON) = 1;
        At<float>(s, S_RAIN_DENSITY) = ro.density;
        At<float>(s, S_RAIN_FGALPHA) = ro.fgAlpha;
        At<float>(s, S_RAIN_SPLASHALPHA) = ro.splashAlpha;
        At<float>(s, S_RAIN_SPEED) = ro.speed;
        for (int k = 0; k < 3; ++k) At<float>(s, S_RAIN_DIR + 4 * k) = ro.dir[k];
    }
    if (cfg.weather_log && now - v->logAt > 5000)
    {
        v->logAt = now;
        Log("    weather view %d at (%.0f, %.0f, %.0f): rain %.2f gust %.2f overcast %.2f mist %.2f fog %.2f (sight %.0f u, low %.3f x %.1f u) "
            "| engine fog %s%s | level fog %s %.4f / %.2f / %.1f x %.4f | night %.2f\n", idx, pos[0], pos[1], pos[2], v->cur.rain,
            v->cur.gust, v->cur.overcast, v->cur.mist, v->cur.fogw, wxm::Vis(v->cur), wxm::Low(v->cur), wxm::Thick(v->cur),
            wroteFog ? "written" : "untouched", ro.write && ro.on ? ", engine rain ON" : "", lv.on ? "on" : "off", lv.dens, lv.thick,
            lv.nearDist, lv.nearDens, Night01());
    }
}

// ---- once per world update (PMThunk): the shader floats ----------------------------------------------
// The views' values if the hook is running; otherwise (patch refused) the weather at the players.
static void Publish(const Config& cfg, bool on, uint32_t now, bool quiet = false)
{
    wxm::ShaderOut out = {};
    float raining = 0.0f;                                           // the rain itself (whether or not it is drawn on the screen)
    const bool views = g_origEval != nullptr && now - static_cast<uint32_t>(g_viewSeen) < 1000;
    if (views)
    {
        for (const View& v : g_view)
            if (v.used && now - v.seen < 1000) { out = wxm::MaxShader(out, v.sh); if (v.cur.rain > raining) raining = v.cur.rain; }
    }
    else
    {
        const wxm::Params p = ParamsOf(cfg);
        for (int i = 0; i < kPlayers; ++i)
        {
            float pos[3];
            if (!PlayerPos(i, pos)) { g_pl[i] = View{}; continue; }
            g_pl[i].used = true;
            StepAt(g_pl[i], pos, cfg, on, now, p, "player", i + 1);
            out = wxm::MaxShader(out, g_pl[i].sh);
            if (g_pl[i].cur.rain > raining) raining = g_pl[i].cur.rain;
        }
    }
    // the radio's weather lines: its hosts say "rain" while it rains where a player is, "clear" otherwise
    static wxm::Latch s_radioRain;
    rd::g_rain = s_radioRain.Update(raining, 0.12f, 0.05f) ? 1 : 0;
    // (weather switched off: the states step to nothing by themselves, so the screen effects fade too - no cut)
    const bool nonZero = out.rain > 0.0f || out.overcast > 0.0f || out.mist > 0.0f || out.gust > 0.0f;
    g_lastPublishMs = now ? now : 1;
    // "Changing": one log line per spell, so a session's weather can be read back afterwards
    if (on && cfg.weather_rain == wxm::kChanging)
    {
        static int64_t s_spell = wxm::kNoStart;
        const wxm::Params pp = ParamsOf(cfg);
        const double ts = Seconds();
        const int64_t k = wxm::SpellIndex(ts, pp, SessionSeed()), k0 = wxm::SpellIndex(SessionStart(), pp, SessionSeed());
        if (k != s_spell)
        {
            s_spell = k;
            const wxm::Spell a = wxm::SpellAt(k, SessionSeed(), pp.wetShare, k0), b = wxm::SpellAt(k + 1, SessionSeed(), pp.wetShare, k0);
            Log(">>> weather: changing - this spell (%.0f s each) is %s, the next %s; rain now %.2f\n", pp.spell, wxm::SpellName(a), wxm::SpellName(b), raining);
        }
    }
    // 2026-11-25: the rain must stop on entering the Backrooms. Whatever the views say, nothing of the weather
    // reaches the screen while anyone is down there (the slots are one set for the whole screen).
    if (quiet)
    {
        static bool s_told = false;
        if (nonZero && !s_told) { s_told = true; Log(">>> weather: the Backrooms - the screen's rain / grey / mist forced to 0 (a view still had %.2f rain)\n", out.rain); }
        out = wxm::ShaderOut{};
    }
    const bool nonZero2 = out.rain > 0.0f || out.overcast > 0.0f || out.mist > 0.0f || out.gust > 0.0f;
    if (!nonZero2 && !g_publishedNonZero) return;                   // nothing of ours in the slots: leave them be
    if (cfg.bridge && vsim::BridgePublishWeather(g_bridgeP, g_bridge, out.rain, out.overcast, out.mist, out.gust))
        g_publishedNonZero = nonZero2;
}

// 2026-11-25: no rain over the loading screen. Publish runs once per WORLD update, so when the
// world stops (a loading screen, the pause menu) the last values stayed in the slots and the main filter kept
// drawing rain over the loading screen. The hotkey thread calls this every 15 ms: no world update for 0.4 s
// -> the four slots go to 0 (the states are kept, so the rain is back on the first world frame).
static void Watch(uint32_t now)
{
    const uint32_t last = g_lastPublishMs;
    if (!g_publishedNonZero || last == 0 || now - last < 400) return;
    if (vsim::BridgePublishWeather(g_bridgeP, g_bridge, 0.0f, 0.0f, 0.0f, 0.0f)) g_publishedNonZero = false;
}

// lcudrive going (Unhook) or a fault: the shader slots back to 0, the states forgotten.
static void Clear()
{
    vsim::BridgePublishWeather(g_bridgeP, g_bridge, 0.0f, 0.0f, 0.0f, 0.0f);
    g_publishedNonZero = false;
    rd::g_rain = 0;
    for (View& v : g_view) v = View{};
    for (View& v : g_pl) v = View{};
}

} // namespace wx
