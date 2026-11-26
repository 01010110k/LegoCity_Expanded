// =============================================================================================
//  gfx2.h - the second graphics pass: the engine side (2026-11-19, Notes\57 section 9+).
//  gfx2_model.h holds every decision (pure, test\gfx2_harness.cpp).
//
//  WHERE IT WORKS [P disasm, exe TimeDateStamp 0x591B08C3]
//  1. AFTER the evaluator (Hook_PostEval calls gfx2::Post once 0x14037B8C0 has returned). By then the
//     evaluator has filled its defaults (Bloom: Threshold 0.5 / Intensity 0.8 where a level left it
//     off, 0x14037B973) and settled the stage mask at settings+0; the engine copies the block into
//     the frame's render packet later (0x140374B80 from 0x14038587E), so a value written here is
//     the value drawn. The settings are the view's own (RenderView::Begin 0x14032D710:
//     lea r8,[view+0x1230]) and are rebuilt from the level every frame (0x14037C9F0 reset +
//     blend), so nothing written here accumulates.
//       Bloom      mask bit 1; +0xBC enable; +0xC4 "Bloom Threshold (Last-Gen)", +0xC8 "Bloom
//                  Intensity (Last-Gen)": the reflection getter 0x140B76120 switches on the field
//                  id (jump table 0x140B763EC): 0x8000000B -> mov eax,[rdx+0x1B4],
//                  0x8000000A -> mov eax,[rdx+0x1B8]; component - 0xF0 = settings (gfx.h).
//       Live cube  mask bit 26; +0x8A8 enable; +0x8B0 "LRZ FarClip", +0x8B4 "LRZ Focus Factor",
//                  +0x8B8 "LRZ HDR Exposure" (the evaluator drops the stage when +0x8B0 or +0x8B8
//                  is 0, 0x14037BF3D).
//       DOF        (read and logged only) +0x7B4 enable, +0x7BC Blur Method, +0x7C0 FStop,
//                  +0x7C4 (35 in the game's own gameplay block, 65 in its aim blend: a focal
//                  length [I]), +0x7C8 Focal Plane (u), +0x7D0 Blur Region [I], +0x7D4 Technique
//                  (2 = the evaluator switches DOF off), +0x7D8 Max Blur Radius, +0x7DC Override
//                  Game Settings; +0x12 "Do DoF In Split Screen".
//  2. The view: +0x40 / +0x44 = the viewport's share of the screen (Begin multiplies them by the
//     screen size, 0x14032D548); +0x150 the render camera copy (rows, +0x30 position; +0x40 view,
//     +0x80 view x projection [I, checked at run time and logged], +0x100 projection,
//     +0x340 FOV, +0x348 1/aspect, +0x34C near, +0x350 far - 0x140323E10).
//  3. IniParams 12.xyzw / 13.xyzw (the main-filter block Tools\Patch-Gfx2Post.ps1 and the light
//     pass ShaderFixes\night_lights.hlsl) and 14.xyzw / 15.xyz (the frame counter,
//     ShaderFixes\gfx2_overlay.hlsl), written only while 15.w holds the sentinel
//     ShaderFixes\gfx2_fx.ini sets (w15 = 86420.625).
// =============================================================================================
#pragma once
#include "gfx2_model.h"

namespace gfx2 {

static const uintptr_t S_MASK = 0x00, S_BLOOM_ON = 0xBC, S_BLOOM_THR = 0xC4, S_BLOOM_INT = 0xC8;
static const uintptr_t S_LRZ_ON = 0x8A8, S_LRZ_FAR = 0x8B0, S_LRZ_EXPOSURE = 0x8B8;
static const uintptr_t S_DOF_ON = 0x7B4, S_DOF_METHOD = 0x7BC, S_DOF_FSTOP = 0x7C0, S_DOF_FOCAL = 0x7C4, S_DOF_PLANE = 0x7C8,
                       S_DOF_REGION = 0x7D0, S_DOF_TECH = 0x7D4, S_DOF_RADIUS = 0x7D8, S_DOF_SPLIT = 0x12;
static const uintptr_t V_FRAC_W = 0x40, V_FRAC_H = 0x44, V_CAM = 0x150, C_VIEWPROJ = 0x80, C_FOV = 0x340, C_NEAR = 0x34C, C_FAR = 0x350;
static const uint32_t  kBitBloom = 1u << 1, kBitLrz = 1u << 26;
static const float     kSentinel15w = 86420.625f;

struct View
{
    void* key; uint32_t seen; bool used;
    bool  player; float frac;      // a player's camera? the smaller of its viewport fractions
    bool  logged;
};
enum : int { kViews = 12 };
static View g_view[kViews];
static void* volatile g_statKey = nullptr;     // the one view whose frames are counted
static gfx2m::Stats g_stats = gfx2m::StatsInit();
static gfx2m::Auto  g_auto = gfx2m::AutoInit();
static volatile LONG g_split = 0, g_playerViews = 0;
static gfx2m::Fx g_fx = {};
static float g_rt = 0.0f;                    // [gfx3] 13.w as published
static uint32_t g_fxAt = 0, g_autoAt = 0;
static bool g_published = false, g_noSentinelLogged = false;
static int  g_badBloom = 0; static bool g_refuseBloom = false;
static int  g_levelLogged = -1, g_splitLogged = -1;
static volatile bool g_fault = false;

static int Fault(EXCEPTION_POINTERS* ep, const char* what)
{
    if (!g_fault)
    {
        g_fault = true;
        Log("!!! EXCEPTION 0x%08lX in the graphics code (gfx2, %s) at %p - bloom / far blur / sun rays / frame counter are off for this session.\n",
            ep->ExceptionRecord->ExceptionCode, what, ep->ExceptionRecord->ExceptionAddress);
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

static gfx2m::Opts OptsOf(const Config& c)
{
    gfx2m::Opts o = gfx2m::DefaultOpts();
    o.bloom = c.gfx_bloom ? c.gfx_bloom_scale : 0.0f;
    o.dof = c.gfx_far_blur ? c.gfx_far_blur_scale : 0.0f;
    o.dofStart = c.gfx_far_blur_start; o.dofFull = c.gfx_far_blur_full;
    o.shafts = c.gfx_sun_rays ? c.gfx_sun_rays_scale : 0.0f;
    o.flare = c.gfx_sun_rays ? c.gfx_flare_scale : 0.0f;
    o.moon = c.gfx_moonlight ? c.gfx_moonlight_scale : 0.0f;
    o.reflect = c.gfx_reflections ? c.gfx_reflections_scale : 0.0f;
    o.splitFx = c.gfx_split_fx;
    return o;
}
static gfx2m::Level LevelNow(const Config& c, bool on)
{
    if (!on) return gfx2m::Level{};
    return gfx2m::ApplyAuto(gfx2m::LevelOf(c.gfx_preset), static_cast<int>(gfx::g_autoLevel));
}
static float RainNow(uint32_t now)
{
    float r = 0.0f;
    for (const wx::View& v : wx::g_view) if (v.used && now - v.seen < 1000 && v.cur.rain > r) r = v.cur.rain;
    return r;
}

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

// ---- after the evaluator: one view's settings ---------------------------------------------------------
static void PostImpl(void* view, uint8_t* s, const Config& cfg, bool on)
{
    if (!PtrOk(s) || !PtrOk(view)) return;
    const uint32_t now = GetTickCount();
    View* v = ViewOf(view, now);
    // is this a player's camera, and how much of the screen is it?
    {
        float cp[3]; bool isNear = false;
        if (wx::CameraPos(view, cp))
            for (int i = 0; i < 2 && !isNear; ++i)
            {
                float pp[3];
                if (!wx::PlayerPos(i, pp)) continue;
                const float dx = cp[0] - pp[0], dy = cp[1] - pp[1], dz = cp[2] - pp[2];
                isNear = dx * dx + dy * dy + dz * dz < 60.0f * 60.0f;
            }
        v->player = isNear;
        const float fw = At<float>(view, V_FRAC_W), fh = At<float>(view, V_FRAC_H);
        v->frac = (IsFin(fw) && IsFin(fh) && fw > 0.01f && fw <= 1.001f && fh > 0.01f && fh <= 1.001f) ? (fw < fh ? fw : fh) : 1.0f;
    }
    // the frame counter: successive frames of ONE player view
    if (v->player)
    {
        void* k = g_statKey;
        if (k == nullptr) { g_statKey = view; k = view; }
        if (k == view) gfx2m::Tick(g_stats, gfx::Qpc());
    }
    const gfx2m::Level lv = LevelNow(cfg, on);
    const gfx2m::Opts o = OptsOf(cfg);
    const uint32_t mask = At<uint32_t>(s, S_MASK);

    if (!v->logged && v->player)
    {
        v->logged = true;
        uint8_t* cam = static_cast<uint8_t*>(view) + V_CAM;
        float m[16], pos[3], fwd[3];
        for (int i = 0; i < 16; ++i) m[i] = At<float>(cam, C_VIEWPROJ + 4 * i);
        for (int i = 0; i < 3; ++i) { pos[i] = At<float>(cam, 0x30 + 4 * i); fwd[i] = At<float>(cam, 0x20 + 4 * i); }
        Log(">>> gfx2: view %p (%.0f%% of the screen), stage mask %08X, the level's own: bloom %s (threshold %.3f, intensity %.3f), "
            "DOF %s (f-stop %.2f, +0x7C4 %.2f, focal plane %.2f, region %d, technique %d, method %d, max radius %d, in split screen %u), "
            "live cube map %s (exposure %.3f, far %.0f); camera fov %.3f near %.3f far %.0f; camera +0x80 %s a view-projection\n",
            view, v->frac * 100.0f, mask, At<uint8_t>(s, S_BLOOM_ON) ? "on" : "off", At<float>(s, S_BLOOM_THR), At<float>(s, S_BLOOM_INT),
            At<uint8_t>(s, S_DOF_ON) ? "on" : "off", At<float>(s, S_DOF_FSTOP), At<float>(s, S_DOF_FOCAL), At<float>(s, S_DOF_PLANE),
            At<int>(s, S_DOF_REGION), At<int>(s, S_DOF_TECH), At<int>(s, S_DOF_METHOD), At<int>(s, S_DOF_RADIUS), At<uint8_t>(s, S_DOF_SPLIT),
            At<uint8_t>(s, S_LRZ_ON) ? "on" : "off", At<float>(s, S_LRZ_EXPOSURE), At<float>(s, S_LRZ_FAR),
            At<float>(cam, C_FOV), At<float>(cam, C_NEAR), At<float>(cam, C_FAR),
            gfx2m::LooksLikeViewProj(m, pos, fwd) ? "looks like" : "does NOT look like");
    }

    const gfxm::Tod tod = gfx::TodNow();
    // bloom
    if (lv.bloom > 0.0f && o.bloom > 0.0f && !g_refuseBloom && (mask & kBitBloom) && At<uint8_t>(s, S_BLOOM_ON))
    {
        const gfx2m::Bloom b = { true, At<float>(s, S_BLOOM_THR), At<float>(s, S_BLOOM_INT) };
        if (!gfx2m::BloomSane(b))
        {
            if (++g_badBloom >= 120) { g_refuseBloom = true; Log("!!! gfx2: the bloom block does not look as expected - left alone for this session\n"); }
        }
        else
        {
            if (g_badBloom > 0) --g_badBloom;
            const gfx2m::BloomOut bo = gfx2m::MapBloom(b, lv.bloom * o.bloom, tod, RainNow(now));
            if (bo.write) { At<float>(s, S_BLOOM_THR) = bo.thr; At<float>(s, S_BLOOM_INT) = bo.inten; }
        }
    }
    // the live cube map while it rains
    if (lv.reflect > 0.0f && o.reflect > 0.0f && (mask & kBitLrz) && At<uint8_t>(s, S_LRZ_ON))
    {
        const gfx2m::LrzOut lo = gfx2m::MapLrz(true, At<float>(s, S_LRZ_EXPOSURE), lv.reflect * o.reflect, RainNow(now));
        if (lo.write) At<float>(s, S_LRZ_EXPOSURE) = lo.exposure;
    }
}
static void Post(void* view, uint8_t* s, bool on)
{
    if (g_fault) return;
    __try { PostImpl(view, s, g_cfgBuf[g_cfgIdx], on); }
    __except (Fault(GetExceptionInformation(), "per-view settings")) {}
}

// ---- once per world update: split-screen, the Auto governor, the shader floats --------------------------
static int WriteSlots(uintptr_t base, const float v[15])
{
    __try
    {
        volatile float* f = reinterpret_cast<volatile float*>(base);
        if (f[63] != kSentinel15w) return 0;
        for (int i = 0; i < 15; ++i) f[48 + i] = v[i];           // 12.x .. 15.z (15.w = the sentinel, never written)
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
static void PublishImpl(const Config& cfg, bool on)
{
    const uint32_t now = GetTickCount();
    // the views of the last half second
    int players = 0; float minFrac = 1.0f; bool statSeen = false;
    void* stat = g_statKey;
    for (const View& v : g_view)
    {
        if (!v.used || now - v.seen > 500) continue;
        if (v.key == stat && v.player) statSeen = true;
        if (!v.player) continue;
        ++players; if (v.frac < minFrac) minFrac = v.frac;
    }
    if (!statSeen) g_statKey = nullptr;                          // that view is gone: the next player view counts
    const bool split = gfx2m::IsSplit(players, minFrac);
    g_split = split ? 1 : 0; g_playerViews = players;
    if (static_cast<int>(split) != g_splitLogged && players > 0)
    {
        g_splitLogged = split ? 1 : 0;
        Log(">>> gfx2: %d player view(s), smallest %.0f%% of the screen -> %s\n", players, minFrac * 100.0f,
            split ? (cfg.gfx_split_fx ? "split-screen (far blur / sun rays kept: split_fx = 1)" : "split-screen: far blur, sun rays and flare are off")
                  : "one view: far blur, sun rays and flare allowed");
    }
    // the Auto governor (about four times a second)
    if (now - g_autoAt >= 250)
    {
        const float dt = g_autoAt ? (now - g_autoAt) / 1000.0f : 0.0f;
        g_autoAt = now;
        // a reading counts only while frames are arriving (not in a menu, a load or with no player view)
        const bool fresh = players > 0 && g_stats.count >= 20 && g_stats.last > 0.0 && gfx::Qpc() - g_stats.last < 0.5;
        const int lvl = gfx2m::StepAuto(g_auto, on && cfg.gfx_auto != 0 && cfg.gfx_preset != gfxm::kStock,
                                        fresh ? g_stats.fps : 0.0f, cfg.gfx_auto_target, dt);
        gfx::g_autoLevel = lvl;
        if (lvl != g_levelLogged)
        {
            if (g_levelLogged >= 0 || lvl > 0)
                Log(">>> gfx2 auto: level %d (%.0f fps, target %.0f) - %s\n", lvl, g_stats.fps, cfg.gfx_auto_target,
                    lvl == 0 ? "the preset as chosen" : lvl == 1 ? "no sun rays / flare / far blur / fringes / grain"
                    : lvl == 2 ? "also no extra draw distance / haze" : "also no ambient occlusion, nearer culling");
            g_levelLogged = lvl;
        }
    }
    // the shader floats
    const gfx2m::Fx target = gfx2m::MapFx(LevelNow(cfg, on), OptsOf(cfg), gfx::TodNow(), split);
    const float dt = g_fxAt ? (now - g_fxAt) / 1000.0f : 0.0f;
    g_fxAt = now;
    g_fx = gfx2m::StepFx(g_fx, target, dt, 0.5f);
    const gfx2m::Overlay ov = gfx2m::MapOverlay(on && cfg.gfx_fps != 0, g_stats, players, static_cast<int>(gfx::g_autoLevel), split);
    // [gfx3] ray-traced light (ShaderFixes gfx3_trace / gfx3_apply): one gameplay view only, eased in and out
    {
        const float want = (on && cfg.gfx_rt && !split && cfg.gfx_preset != 0) ? cfg.gfx_rt_scale : 0.0f;
        const float stepMax = dt > 0.0f ? (dt < 0.1f ? dt : 0.1f) * 2.0f : 0.0f;
        g_rt += want > g_rt ? (want - g_rt < stepMax ? want - g_rt : stepMax) : -(g_rt - want < stepMax ? g_rt - want : stepMax);
        if (!(g_rt > 0.0f)) g_rt = 0.0f;
        static bool s_told = false;
        if (g_rt > 0.0f && !s_told) { s_told = true; Log(">>> gfx3: ray-traced light on (strength %.2f)\n", want); }
    }
    const bool nonZero = !gfx2m::FxZero(g_fx) || ov.on > 0.0f || g_rt > 0.0f;
    if (!nonZero && !g_published) return;
    if (!cfg.bridge || !vsim::BridgeLockedQ(g_bridge) || g_bridge.base == 0) return;
    const float v[15] = { g_fx.dof, g_fx.dofStart, g_fx.dofFull, g_fx.shafts,          // 12.xyzw
                          g_fx.flare, g_fx.moon, g_fx.split, g_rt,                       // 13.xyzw (13.w = gfx3 ray-traced light)
                          ov.on, ov.fps, ov.avgMs, ov.worstMs,                           // 14.xyzw
                          ov.views, ov.level, ov.split };                                // 15.xyz
    const int r = WriteSlots(g_bridge.base, v);
    if (r == 0 && !g_noSentinelLogged)
    {
        g_noSentinelLogged = true;
        Log(">>> gfx2: IniParams 15.w is not the sentinel - ShaderFixes\\gfx2_fx.ini is not included by d3dx.ini; far blur / sun rays / moonlight / frame counter stay off (bloom and reflections still work)\n");
    }
    if (r == 1) g_published = nonZero;
}
static void Publish(const Config& cfg, bool on)
{
    if (g_fault) return;
    __try { PublishImpl(cfg, on); }
    __except (Fault(GetExceptionInformation(), "shader floats")) {}
}
static void Clear()
{
    __try
    {
        if (g_published && vsim::BridgeLockedQ(g_bridge) && g_bridge.base) { const float z[15] = {}; WriteSlots(g_bridge.base, z); }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {}
    g_published = false; g_fx = gfx2m::Fx{}; g_rt = 0.0f; gfx::g_autoLevel = 0; g_auto = gfx2m::AutoInit();
    for (View& v : g_view) v = View{};
}

} // namespace gfx2
