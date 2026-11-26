// =============================================================================================
//  gfx.h - the graphics overhaul: the engine side (2026-11-19, Notes\57). gfx_model.h holds every
//  decision (pure, test\gfx_harness.cpp); this file reads / writes the engine and logs.
//
//  WHERE IT WORKS [P disasm, exe TimeDateStamp 0x591B08C3]
//  1. The per-view PostEffect settings (the weather hook, weather.h): Hook_PostEval calls
//     gfx::Pre(view, s) BEFORE the weather writes (to see the level's own fog) and gfx::ViewFrame(view, s)
//     AFTER them, both before the evaluator 0x14037B8C0. Field offsets = the reflection table at
//     0x140B765E0 (component offset - 0xF0) and the evaluator's own tests:
//       Colour (Process Colour)  +0xD0 enable; +0xDC brightness, +0xE0 contrast, +0xE4 saturation,
//                                +0xE8 gamma; +0xEC / +0xFC / +0x10C balance switches (shadows / mids /
//                                highlights) each followed by Cyan-Red, Magenta-Green, Yellow-Blue;
//                                +0x11C film grain, +0x120 grain exposure boost, +0x124 grain size,
//                                +0x128 (neutral 0), +0x12C gain. The evaluator drops the stage when all
//                                are neutral (0x14037C213..0x14037C47D) - so Stock costs nothing.
//                                The reset (0x14037C860) zeroes the block: a level without a colour volume
//                                reads all 0 with the switch off; we then start from the neutral values.
//       SSAO                     +0x68C enable, +0x694 contrast, +0x698 / +0x69C / +0x6A0 radii, +0x6A4
//                                separation cut-off. The evaluator fills 2.4 / 0.13 / 0.2 / 0.3 / 1.8 when
//                                the switch is 0 (0x14037B928) and drops the stage when radius 1 <= 0.
//       Engine Params            +0x08 enable (filled by 0x140378A80(s + 8) when 0, 0x14037BA81), +0x14
//                                Character Rim Strength, +0x30 / +0x34 / +0x38 Object Clip Distance
//                                [Low] / [Medium] / [High] (defaults 40 / 80 / 120 u).
//       God Rays                 +0x794 enable, +0x7B0 God Rays High Quality.
//       Exponential Height Fog   +0x7E0 enable, +0x7F4 Near Distance, +0x7F8 Near Density (weather.h's).
//  2. The render camera: RenderView::Begin 0x14032D450 copies the camera it is given into the view
//     (0x14032D4D7: call 0x140325A80(camera, view + 0x150), a plain field copy) and then builds the view's
//     matrices from that COPY (0x14032D52A: 0x140326A10(view + 0x150, 0)). We patch that one call: the
//     copy is made, then the sway is added to the copy's matrix rows (+0x00..+0x2C) and position (+0x30)
//     - the game's own camera object is never written, so its logic, aiming and collision are untouched.
//  3. The lens block in the 13 main-filter scene passes (Tools\Patch-GfxPost.ps1, ShaderFixes): five
//     floats in IniParams 10.xyzw / 11.x, written only while d3dx.ini's sentinel 11.w is in place.
// =============================================================================================
#pragma once
#include "gfx_model.h"
#include "gfx2_model.h"                   // [gfx2] the Auto governor's levels (ApplyAuto)

namespace gfx {

static const uintptr_t SITE_CAMCOPY = 0x32D4D7;     // the E8 byte: call 0x140325A80 inside RenderView::Begin
static const uintptr_t FN_CAMCOPY   = 0x325A80;     // (src camera, dst = view + 0x150)
static const uintptr_t FN_EP_DEFAULTS = 0x378A80;   // Engine Params defaults (rcx = settings + 8)
static const uint8_t   kEpSig[10] = { 0xC6, 0x01, 0x00, 0xC7, 0x41, 0x08, 0x01, 0x01, 0x01, 0x00 };

static const uintptr_t S_COL_ON = 0xD0, S_COL_BRI = 0xDC, S_COL_CON = 0xE0, S_COL_SAT = 0xE4, S_COL_GAM = 0xE8,
                       S_COL_BAL0 = 0xEC, S_COL_GRAIN = 0x11C, S_COL_GBOOST = 0x120, S_COL_GSIZE = 0x124, S_COL_X128 = 0x128,
                       S_COL_GAIN = 0x12C;
static const uintptr_t S_AO_ON = 0x68C, S_AO_CON = 0x694, S_AO_R1 = 0x698, S_AO_R2 = 0x69C, S_AO_R3 = 0x6A0, S_AO_CUT = 0x6A4;
static const uintptr_t S_EP_ON = 0x08, S_EP_RIM = 0x14, S_EP_CLIP = 0x30;
static const uintptr_t S_GR_ON = 0x794, S_GR_HQ = 0x7B0;
static const uintptr_t S_FOG_ON = 0x7E0, S_FOG_NEARDIST = 0x7F4, S_FOG_NEARDENS = 0x7F8;
static const uintptr_t CAM_ROWS = 0x00, CAM_POS = 0x30;

typedef void(__fastcall* CamCopyFn)(void* src, void* dst);
static CamCopyFn g_origCamCopy = nullptr;
typedef void(__fastcall* EpDefaultsFn)(void* block);
static EpDefaultsFn g_epDefaults = nullptr;          // set by Init only if its first bytes are what we read

static volatile bool g_fault = false;                // a fault = no graphics writes for the session
static volatile bool g_camFault = false;             // the camera layer has its own
static volatile LONG g_autoLevel = 0;                // [gfx2] 0 .. 3, set by gfx2.h's Auto governor (0 = the preset as chosen)
static float g_handicam = 0.0f;                      // 0..1, set by the Backrooms when its handicam runs (MERGE.md)

struct View
{
    void* key; uint32_t seen; bool used;
    // the level's own values, captured before the weather writes
    bool  fogOnLevel; float fogNearDist, fogNearDens; uint8_t fogBefore[0x20];
    bool  logged;
};
enum : int { kViews = 12 };
static View g_view[kViews];
static gfxm::Knobs g_knobs = gfxm::Neutral();        // the moving knobs (render thread)
static uint32_t g_knobsAt = 0;
static int  g_presetLogged = -1, g_gradeLogged = -1;
static int  g_badColour = 0, g_badAo = 0, g_badEp = 0;
static bool g_refuseColour = false, g_refuseAo = false, g_refuseEp = false;
static bool g_logOnce = false;

struct CamView { void* key; uint32_t seen; double at; bool used; gfxm::CamState st; };
static double Qpc()
{
    static LARGE_INTEGER f = {};
    if (!f.QuadPart) QueryPerformanceFrequency(&f);
    LARGE_INTEGER c; QueryPerformanceCounter(&c);
    return f.QuadPart ? static_cast<double>(c.QuadPart) / static_cast<double>(f.QuadPart) : 0.0;
}
static CamView g_cam[kViews];
static bool g_camLogged = false, g_camBadLogged = false;

static View* ViewOf(void* key, uint32_t now)
{
    View* free = nullptr; View* oldest = &g_view[0];
    for (View& v : g_view)
    {
        if (v.used && v.key == key) { v.seen = now; return &v; }
        if (v.used && now - v.seen > 3000) v = View{};
        if (!v.used && !free) free = &v;
        if (v.seen < oldest->seen) oldest = &v;
    }
    View* v = free ? free : oldest;
    *v = View{}; v->used = true; v->key = key; v->seen = now;
    return v;
}

static void Init()
{
    __try
    {
        const uint8_t* p = reinterpret_cast<const uint8_t*>(g_base + FN_EP_DEFAULTS);
        if (std::memcmp(p, kEpSig, sizeof(kEpSig)) == 0) g_epDefaults = reinterpret_cast<EpDefaultsFn>(g_base + FN_EP_DEFAULTS);
        else Log("gfx: the Engine Params defaults function does not look as expected - rim / draw distance are left alone\n");
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { g_epDefaults = nullptr; }
}

static int Fault(EXCEPTION_POINTERS* ep, volatile bool& flag, const char* what)
{
    if (!flag)
    {
        flag = true;
        Log("!!! EXCEPTION 0x%08lX in the graphics code (%s) at %p - that part is off for this session ([gfx] preset = 0 keeps it off).\n",
            ep->ExceptionRecord->ExceptionCode, what, ep->ExceptionRecord->ExceptionAddress);
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

static gfxm::Scales ScalesOf(const Config& c)
{
    gfxm::Scales s = { c.gfx_grade, c.gfx_ao, c.gfx_detail, c.gfx_rim, c.gfx_haze, c.gfx_ca, c.gfx_sharpen, c.gfx_vignette,
                       c.gfx_grain, c.gfx_heat };
    return s;
}
static gfxm::Knobs TargetKnobs(const Config& c, bool on)
{
    if (!on || c.gfx_preset == gfxm::kStock) return gfxm::Neutral();
    gfxm::Knobs k = gfxm::Scaled(gfxm::PresetKnobs(c.gfx_preset), ScalesOf(c));
    k = gfx2m::ApplyAuto(k, static_cast<int>(g_autoLevel));          // [gfx2] Auto: effects stepped down under the frame-rate target
    if (c.gfx_reshade_grade && c.gfx_reshade_aware) k.grade = 0.0f;   // ReShade's grade is on: no second grade on top
    return k;
}
static gfxm::Tod TodNow()
{
    if (!vsim::BridgeLockedQ(g_bridge)) { gfxm::Tod t = {}; t.elev = 1.0f; t.day = 1.0f; return t; }   // no clock: noon
    return gfxm::TodOf(vsim::BridgeClock(g_bridge));
}

// ---- 1. before the weather: remember the level's own fog -----------------------------------------------
static void PreImpl(void* view, uint8_t* s)
{
    if (!PtrOk(s)) return;
    View* v = ViewOf(view, GetTickCount());
    v->fogOnLevel = At<uint8_t>(s, S_FOG_ON) != 0;
    v->fogNearDist = At<float>(s, S_FOG_NEARDIST);
    v->fogNearDens = At<float>(s, S_FOG_NEARDENS);
    std::memcpy(v->fogBefore, s + S_FOG_ON, sizeof(v->fogBefore));
}

static gfxm::Colour ReadColour(uint8_t* s)
{
    gfxm::Colour c = {};
    c.on = At<uint8_t>(s, S_COL_ON) != 0;
    c.bri = At<float>(s, S_COL_BRI); c.con = At<float>(s, S_COL_CON); c.sat = At<float>(s, S_COL_SAT); c.gam = At<float>(s, S_COL_GAM);
    for (int g = 0; g < 3; ++g)
    {
        c.balOn[g] = At<uint8_t>(s, S_COL_BAL0 + 0x10 * g) != 0;
        for (int i = 0; i < 3; ++i) c.bal[g][i] = At<float>(s, S_COL_BAL0 + 0x10 * g + 4 + 4 * i);
    }
    c.grain = At<float>(s, S_COL_GRAIN); c.grainBoost = At<float>(s, S_COL_GBOOST); c.grainSize = At<float>(s, S_COL_GSIZE);
    c.x128 = At<float>(s, S_COL_X128); c.gain = At<float>(s, S_COL_GAIN);
    return c;
}
static void WriteColour(uint8_t* s, const gfxm::Colour& c)
{
    At<float>(s, S_COL_BRI) = c.bri; At<float>(s, S_COL_CON) = c.con; At<float>(s, S_COL_SAT) = c.sat; At<float>(s, S_COL_GAM) = c.gam;
    for (int g = 0; g < 3; ++g)
    {
        At<uint8_t>(s, S_COL_BAL0 + 0x10 * g) = c.balOn[g] ? 1 : 0;
        for (int i = 0; i < 3; ++i) At<float>(s, S_COL_BAL0 + 0x10 * g + 4 + 4 * i) = c.bal[g][i];
    }
    At<float>(s, S_COL_GRAIN) = c.grain; At<float>(s, S_COL_GBOOST) = c.grainBoost; At<float>(s, S_COL_GSIZE) = c.grainSize;
    At<float>(s, S_COL_X128) = c.x128; At<float>(s, S_COL_GAIN) = c.gain;
    At<uint8_t>(s, S_COL_ON) = 1;
}

// ---- 2. after the weather: the look ---------------------------------------------------------------------
static void ViewFrameImpl(void* view, uint8_t* s, const Config& cfg, bool on)
{
    if (!PtrOk(s)) return;
    const uint32_t now = GetTickCount();
    View* v = ViewOf(view, now);
    // the knobs move toward the preset (once a frame, not once a view)
    if (now != g_knobsAt)
    {
        const float dt = g_knobsAt ? (now - g_knobsAt) / 1000.0f : 0.0f;
        g_knobsAt = now;
        g_knobs = gfxm::StepKnobs(g_knobs, TargetKnobs(cfg, on), dt, 0.5f);
    }
    const int preset = on ? cfg.gfx_preset : 0;
    if (preset != g_presetLogged)
    {
        g_presetLogged = preset;
        Log(">>> gfx: preset %s%s\n", gfxm::PresetName(preset),
            preset && cfg.gfx_reshade_grade && cfg.gfx_reshade_aware ? " (ReShade's LCU_DayNightCycle is on: the engine grade stays off)" : "");
    }
    const gfxm::Knobs& k = g_knobs;
    if (gfxm::IsNeutral(k)) return;                                  // Stock: nothing written, nothing read further
    const gfxm::Tod tod = TodNow();
    const float night = gfxm::Night01(tod.elev);

    if (!v->logged || !g_logOnce)
    {
        v->logged = true; g_logOnce = true;
        const gfxm::Colour c = ReadColour(s);
        Log(">>> gfx: view %p, the level's own: colour %s (bri %.3f con %.3f sat %.3f gam %.3f gain %.3f), SSAO %s (%.2f, r %.3f), "
            "engine params %s (rim %.3f, clip %.0f / %.0f / %.0f), god rays %s hq %u, height fog %s (near %.1f u x %.5f)\n", view,
            c.on ? "on" : "off", c.bri, c.con, c.sat, c.gam, c.gain, At<uint8_t>(s, S_AO_ON) ? "on" : "off", At<float>(s, S_AO_CON),
            At<float>(s, S_AO_R1), At<uint8_t>(s, S_EP_ON) ? "on" : "off", At<float>(s, S_EP_RIM), At<float>(s, S_EP_CLIP),
            At<float>(s, S_EP_CLIP + 4), At<float>(s, S_EP_CLIP + 8), At<uint8_t>(s, S_GR_ON) ? "on" : "off", At<uint8_t>(s, S_GR_HQ),
            v->fogOnLevel ? "on" : "off", v->fogNearDist, v->fogNearDens);
    }

    // colour grade
    if (k.grade > 0.0f && !g_refuseColour)
    {
        const gfxm::Colour lv = ReadColour(s);
        if (lv.on && !gfxm::ColourSane(lv))
        {
            if (++g_badColour >= 120) { g_refuseColour = true; Log("!!! gfx: the colour block does not look as expected - the grade is off for this session\n"); }
        }
        else
        {
            if (g_badColour > 0) --g_badColour;
            const gfxm::ColourOut co = gfxm::ApplyGrade(lv, gfxm::GradeAt(tod, k.grade));
            if (co.write) WriteColour(s, co.c);
        }
    }
    // SSAO
    if (k.ao != 1.0f && !g_refuseAo)
    {
        gfxm::Ssao a = { At<uint8_t>(s, S_AO_ON) != 0, At<float>(s, S_AO_CON), At<float>(s, S_AO_R1), At<float>(s, S_AO_R2),
                         At<float>(s, S_AO_R3), At<float>(s, S_AO_CUT) };
        if (a.on && !gfxm::SsaoSane(a))
        {
            if (++g_badAo >= 120) { g_refuseAo = true; Log("!!! gfx: the SSAO block does not look as expected - left alone for this session\n"); }
        }
        else
        {
            if (g_badAo > 0) --g_badAo;
            const gfxm::SsaoOut o = gfxm::MapSsao(a, k.ao);
            if (o.write)
            {
                At<float>(s, S_AO_CON) = o.s.contrast; At<float>(s, S_AO_R1) = o.s.r1; At<float>(s, S_AO_R2) = o.s.r2;
                At<float>(s, S_AO_R3) = o.s.r3; At<float>(s, S_AO_CUT) = o.s.cut; At<uint8_t>(s, S_AO_ON) = 1;
            }
        }
    }
    // rim light at night, draw distance
    if ((k.detail != 1.0f || (k.rim > 0.0f && night > 0.0f)) && !g_refuseEp && g_epDefaults)
    {
        if (At<uint8_t>(s, S_EP_ON) == 0) { g_epDefaults(s + S_EP_ON); At<uint8_t>(s, S_EP_ON) = 1; }   // what the evaluator does next anyway
        gfxm::Engine e = { At<float>(s, S_EP_RIM), { At<float>(s, S_EP_CLIP), At<float>(s, S_EP_CLIP + 4), At<float>(s, S_EP_CLIP + 8) } };
        if (!gfxm::EngineSane(e))
        {
            if (++g_badEp >= 120) { g_refuseEp = true; Log("!!! gfx: the engine-params block does not look as expected - left alone for this session\n"); }
        }
        else
        {
            if (g_badEp > 0) --g_badEp;
            const gfxm::EngineOut o = gfxm::MapEngine(e, k.detail, k.rim, night);
            if (o.write)
            {
                At<float>(s, S_EP_RIM) = o.e.rim;
                for (int i = 0; i < 3; ++i) At<float>(s, S_EP_CLIP + 4 * i) = o.e.clip[i];
            }
        }
    }
    // god rays: high quality where the level has them
    if (k.godrays > 0.0f && At<uint8_t>(s, S_GR_ON) == 1) At<uint8_t>(s, S_GR_HQ) = 1;
    // aerial haze: only on the level's own height fog, only where the weather left the fog alone
    if (k.haze > 0.0f && v->fogOnLevel && std::memcmp(v->fogBefore, s + S_FOG_ON, sizeof(v->fogBefore)) == 0)
    {
        gfxm::Haze h = { true, v->fogNearDist, v->fogNearDens };
        const gfxm::HazeOut o = gfxm::MapHaze(h, k.haze, night, cfg.gfx_haze_dist);
        if (o.write) { At<float>(s, S_FOG_NEARDIST) = o.nearDist; At<float>(s, S_FOG_NEARDENS) = o.nearDens; }
    }
}

static void Pre(void* view, uint8_t* s)
{
    if (g_fault) return;
    __try { PreImpl(view, s); }
    __except (Fault(GetExceptionInformation(), g_fault, "per-view settings")) {}
}
static void ViewFrame(void* view, uint8_t* s, bool on)
{
    if (g_fault) return;
    __try { ViewFrameImpl(view, s, g_cfgBuf[g_cfgIdx], on); }
    __except (Fault(GetExceptionInformation(), g_fault, "per-view settings")) {}
}

// ---- 3. the camera layer: after the engine copies the camera into the view -------------------------------
static void CamImpl(void* dst, const Config& cfg, bool on)
{
    uint8_t* c = static_cast<uint8_t*>(dst);
    if (!PtrOk(c)) return;
    const uint32_t now = GetTickCount();
    CamView* cv = nullptr; CamView* free = nullptr; CamView* oldest = &g_cam[0];
    for (CamView& v : g_cam)
    {
        if (v.used && v.key == dst) { cv = &v; break; }
        if (v.used && now - v.seen > 3000) v = CamView{};
        if (!v.used && !free) free = &v;
        if (v.seen < oldest->seen) oldest = &v;
    }
    if (!cv) { cv = free ? free : oldest; *cv = CamView{}; cv->used = true; cv->key = dst; cv->seen = now; }
    const double tq = Qpc();
    const float dt = cv->at > 0.0 && tq >= cv->at ? static_cast<float>(tq - cv->at) : 0.0f;
    cv->seen = now; cv->at = tq;
    gfxm::CamParams p = { on ? static_cast<float>(cfg.gfx_camera) * cfg.gfx_camera_scale : 0.0f, cfg.gfx_bank_sign >= 0 ? 1.0f : -1.0f, g_handicam };
    if (!(p.intensity > 0.0f)) { cv->st.valid = false; return; }
    float rows[3][3], pos[3];
    for (int r = 0; r < 3; ++r) for (int i = 0; i < 3; ++i) rows[r][i] = At<float>(c, CAM_ROWS + 0x10 * r + 4 * i);
    for (int i = 0; i < 3; ++i) pos[i] = At<float>(c, CAM_POS + 4 * i);
    bool ok = gfxm::Ortho(rows) && IsFin(pos[0]) && IsFin(pos[1]) && IsFin(pos[2]) && std::fabs(pos[0]) < 20000.0f && std::fabs(pos[2]) < 20000.0f;
    if (!ok)
    {
        if (!g_camBadLogged) { g_camBadLogged = true; Log(">>> gfx camera: a view camera whose rows are not a rotation - left alone (logged once)\n"); }
        cv->st.valid = false;
        return;
    }
    // only a player's camera: within 60 u of player 1 or 2 (the split views follow the players; an impostor, a
    // reflection or a menu camera elsewhere is left alone) [I: which callers of Begin are which is not traced]
    {
        bool isNear = false;
        for (int i = 0; i < 2 && !isNear; ++i)
        {
            float pp[3];
            if (!wx::PlayerPos(i, pp)) continue;
            const float dx = pos[0] - pp[0], dy = pos[1] - pp[1], dz = pos[2] - pp[2];
            isNear = dx * dx + dy * dy + dz * dz < 60.0f * 60.0f;
        }
        if (!isNear) { cv->st.valid = false; return; }
    }
    if (!g_camLogged)
    {
        g_camLogged = true;
        Log(">>> gfx camera: first view camera at (%.1f, %.1f, %.1f), rows (%.2f %.2f %.2f) (%.2f %.2f %.2f) (%.2f %.2f %.2f) - the sway is added to this copy only\n",
            pos[0], pos[1], pos[2], rows[0][0], rows[0][1], rows[0][2], rows[1][0], rows[1][1], rows[1][2], rows[2][0], rows[2][1], rows[2][2]);
    }
    const float t = static_cast<float>(std::fmod(tq, 3600.0));      // the handheld noise: a function of time only
    const gfxm::CamOut o = gfxm::StepCamera(cv->st, pos, rows, t, dt, p);
    if (std::fabs(rows[1][1]) < 0.5f) return;                         // looking straight up / down: leave it
    gfxm::ApplyCamera(rows, pos, o);
    for (int r = 0; r < 3; ++r) for (int i = 0; i < 3; ++i) At<float>(c, CAM_ROWS + 0x10 * r + 4 * i) = rows[r][i];
    for (int i = 0; i < 3; ++i) At<float>(c, CAM_POS + 4 * i) = pos[i];
}
// [fix1] the far plane (gfxm::FarFor): on the render copy, a player's camera only, before the matrices are built
static const uintptr_t CAM_NEAR = 0x34C, CAM_FAR = 0x350;             // read by the projection 0x140323E10 [P disasm]
static volatile bool g_farFault = false;
static bool g_farSeen = false, g_farRaised = false;
static void FarImpl(void* dst, const Config& cfg, bool on)
{
    uint8_t* c = static_cast<uint8_t*>(dst);
    if (!PtrOk(c)) return;
    const float x = At<float>(c, CAM_POS), y = At<float>(c, CAM_POS + 4), z = At<float>(c, CAM_POS + 8);
    const float n = At<float>(c, CAM_NEAR), f = At<float>(c, CAM_FAR);
    if (!IsFin(x) || !IsFin(y) || !IsFin(z)) return;
    bool isNear = false;
    for (int i = 0; i < 2 && !isNear; ++i)
    {
        float pp[3];
        if (!wx::PlayerPos(i, pp)) continue;
        const float dx = x - pp[0], dy = y - pp[1], dz = z - pp[2];
        isNear = dx * dx + dy * dy + dz * dz < 60.0f * 60.0f;
    }
    if (!isNear) return;
    if (!g_farSeen) { g_farSeen = true; Log(">>> gfx far plane: first player camera at (%.0f, %.0f, %.0f): near %.3f far %.1f\n", x, y, z, n, f); }
    const gfxm::FarParams p = { on && cfg.gfx_far_fix ? 1 : 0, cfg.gfx_far_from, cfg.gfx_far_dome, cfg.gfx_far_max };
    const float nf = gfxm::FarFor(x, z, n, f, p);
    if (nf != f)
    {
        At<float>(c, CAM_FAR) = nf;
        if (!g_farRaised) { g_farRaised = true; Log(">>> gfx far plane: raised %.1f -> %.1f at (%.0f, %.0f) (the sky dome round the map centre; logged once)\n", f, nf, x, z); }
    }
}
static void Cam(void* dst, bool on)
{
    if (!g_farFault)
    {
        __try { FarImpl(dst, g_cfgBuf[g_cfgIdx], on); }
        __except (Fault(GetExceptionInformation(), g_farFault, "far plane")) {}
    }
    if (g_camFault) return;
    __try { CamImpl(dst, g_cfgBuf[g_cfgIdx], on); }
    __except (Fault(GetExceptionInformation(), g_camFault, "camera")) {}
}

// ---- 4. once per world update: the lens block's floats (IniParams 10.xyzw, 11.x) ------------------------
// Written only while 11.w holds the sentinel d3dx.ini [Constants] sets (w11 = 97531.125): without that line
// the array is shorter than 12 float4s and f[40+] would be past its end.
static const float kSentinel11w = 97531.125f;
static bool g_published = false;
static int WriteSlots(uintptr_t base, const float v[5])
{
    __try
    {
        volatile float* f = reinterpret_cast<volatile float*>(base);
        if (f[47] != kSentinel11w) return 0;
        f[40] = v[0]; f[41] = v[1]; f[42] = v[2]; f[43] = v[3];   // 10.xyzw: aberration, sharpen, vignette, grain
        f[44] = v[4];                                             // 11.x heat haze (11.y / 11.z unused, 11.w sentinel)
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
static void Publish(const Config& cfg, bool on)
{
    if (g_fault) return;
    __try
    {
        // the knobs are stepped by the views; without views (menus, loading) step them here
        const uint32_t now = GetTickCount();
        if (now - g_knobsAt > 500) { g_knobs = gfxm::StepKnobs(g_knobs, TargetKnobs(cfg, on), 0.1f, 0.5f); }
        const gfxm::ShaderOut o = gfxm::MapShader(g_knobs, TodNow());
        const bool nonZero = o.ca > 0.0f || o.sharpen > 0.0f || o.vignette > 0.0f || o.grain > 0.0f || o.heat > 0.0f;
        if (!nonZero && !g_published) return;
        if (!cfg.bridge || !vsim::BridgeLockedQ(g_bridge) || g_bridge.base == 0) return;
        const float v[5] = { o.ca, o.sharpen, o.vignette, o.grain, o.heat };
        const int r = WriteSlots(g_bridge.base, v);
        static bool s_noSentinelLogged = false;
        if (r == 0 && !s_noSentinelLogged)
        {
            s_noSentinelLogged = true;
            Log(">>> gfx: IniParams 11.w is not the sentinel - d3dx.ini lacks the gfx1 [Constants] lines; the lens block stays off\n");
        }
        if (r == 1) g_published = nonZero;
    }
    __except (Fault(GetExceptionInformation(), g_fault, "shader floats")) {}
}
static void Clear()
{
    __try
    {
        if (g_published && vsim::BridgeLockedQ(g_bridge) && g_bridge.base) { const float z[5] = {}; WriteSlots(g_bridge.base, z); }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
    g_published = false;
    for (View& v : g_view) v = View{};
    for (CamView& v : g_cam) v = CamView{};
}

} // namespace gfx
