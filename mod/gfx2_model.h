// =============================================================================================
//  gfx2_model.h - the second graphics pass, pure decisions (2026-11-19, Notes\57 section 9+).
//  Same contract as gfx_model.h: plain numbers in, numbers out; test\gfx2_harness.cpp runs it
//  as is. gfx2.h is the engine side.
//
//  What gfx2 adds on top of gfx1 (each with its own switch in [gfx]):
//    bloom       the engine's own Bloom stage: Threshold (+0xC4) and Intensity (+0xC8) [P names:
//                the PostEffect reflection getter 0x140B76120, ids 0x8000000B / 0x8000000A ->
//                component +0x1B4 / +0x1B8 = settings +0xC4 / +0xC8]. Restrained by day, stronger
//                after dark (a lower threshold + more intensity = halos round lamps, windows, neon).
//    reflections the live cube-map reflections' "LRZ HDR Exposure" (+0x8B8) up a little while it
//                rains (the wet roads mirror the city more).
//    far blur    a very light distance-only blur, self-written in the main-filter pass from the
//                scene depth (IniParams 12.xyz); off in split-screen.
//    sun rays    depth-masked radial shafts + a small lens flare from the mod's own sun (12.w, 13.x).
//    moonlight   a cool ambient floor at night in the deferred light pass (13.y).
//    frame counter (14.x..15.x) and the Auto governor (steps effects down under a frame-rate target).
// =============================================================================================
#pragma once
#include "gfx_model.h"

namespace gfx2m {

using gfxm::Clamp;
using gfxm::Clamp01;
using gfxm::Smooth;
using gfxm::Fin;
using gfxm::Quant;
using gfxm::Lerp;

// ---- the player's switches ([gfx], all default as designed) ---------------------------------------------
struct Opts
{
    float bloom;        // 0 off .. 2 (x the bloom change)
    float dof;          // 0 off .. 2 (x the far blur)
    float dofStart;     // u: where the far blur begins
    float dofFull;      // u: where it reaches its full (small) radius
    float shafts;       // 0 off .. 2 (x the sun rays)
    float flare;        // 0 off .. 2 (x the sun's lens flare)
    float moon;         // 0 off .. 2 (x the moonlight floor)
    float reflect;      // 0 off .. 2 (x the wet-weather reflection boost)
    int   splitFx;      // 1 = view-dependent effects (far blur, sun rays, flare) in split-screen too
};
inline Opts DefaultOpts() { Opts o = { 1.0f, 1.0f, 140.0f, 420.0f, 1.0f, 1.0f, 1.0f, 1.0f, 0 }; return o; }

// What the preset allows (x the switches). Stock = nothing; Performance keeps what is free.
struct Level { float bloom, dof, shafts, flare, moon, reflect; };
inline Level LevelOf(int preset)
{
    Level l = {};
    if (preset == gfxm::kEnhanced)         { l.bloom = 1.0f; l.dof = 1.0f; l.shafts = 1.0f; l.flare = 1.0f; l.moon = 1.0f; l.reflect = 1.0f; }
    else if (preset == gfxm::kPerformance) { l.bloom = 1.0f; l.dof = 0.0f; l.shafts = 0.0f; l.flare = 0.0f; l.moon = 1.0f; l.reflect = 0.0f; }
    return l;
}

// ---- the Auto governor: steps effects down while the frame rate is under the target -------------------
// level 0 = the preset as chosen; 1 = no sun rays / flare / far blur / aberration / grain; 2 = also
// the preset's extra draw distance and haze; 3 = also no ambient occlusion and nearer culling
// (what Performance does). Down: 3 s under 92 % of the target. Up: `wait` s at >= 97 % (a capped
// frame rate cannot show headroom, so the way up is a trial); a trial that fails within 10 s doubles
// the wait (20 s .. 320 s), a level held for 60 s halves it again.
enum : int { kAutoMax = 3 };
struct Auto
{
    int   level;
    float under, over;     // s spent under / over
    float wait;            // s at target before trying one level up
    float sinceUp;         // s since the last step up (a failed trial is one that falls back soon)
    bool  trial;
};
inline Auto AutoInit() { Auto a = {}; a.wait = 20.0f; a.sinceUp = 1.0e6f; return a; }
// One step. fps = the frame rate averaged over about half a second; dt = seconds since the last step.
// Returns the level. on = false or a senseless reading: the level relaxes to 0 at once / holds.
inline int StepAuto(Auto& a, bool on, float fps, float target, float dt)
{
    if (!on) { a = AutoInit(); return 0; }
    if (!Fin(fps) || !Fin(dt) || !(dt > 0.0f) || !(fps > 1.0f) || !(target > 1.0f)) return a.level;
    if (dt > 0.5f) dt = 0.5f;                              // a hitch (a load, alt-tab) is not evidence
    a.sinceUp += dt;
    if (a.trial && a.sinceUp > 60.0f) { a.trial = false; a.wait = Clamp(a.wait * 0.5f, 20.0f, 320.0f); }
    if (fps < 0.92f * target) { a.under += dt; a.over = 0.0f; }
    else if (fps >= 0.97f * target) { a.over += dt; a.under = 0.0f; }
    else { a.under = 0.0f; }                               // in the band: neither clock runs on
    if (a.under >= 3.0f && a.level < kAutoMax)
    {
        ++a.level; a.under = 0.0f; a.over = 0.0f;
        if (a.trial && a.sinceUp < 10.0f) a.wait = Clamp(a.wait * 2.0f, 20.0f, 320.0f);   // the trial failed
        a.trial = false;
    }
    else if (a.over >= a.wait && a.level > 0)
    {
        --a.level; a.over = 0.0f; a.under = 0.0f; a.sinceUp = 0.0f; a.trial = true;
    }
    return a.level;
}
// The preset's knobs at an Auto level (gfx.h TargetKnobs; the knobs fade, so a step never pops).
inline gfxm::Knobs ApplyAuto(const gfxm::Knobs& k, int level)
{
    gfxm::Knobs o = k;
    if (level >= 1) { o.ca = 0.0f; o.grain = 0.0f; o.heat = 0.0f; }
    if (level >= 2) { if (o.detail > 1.0f) o.detail = 1.0f; o.haze = 0.0f; o.godrays = 0.0f; }
    if (level >= 3) { o.ao = 0.0f; if (o.detail > 0.75f) o.detail = 0.75f; }
    return o;
}
inline Level ApplyAuto(const Level& l, int level)
{
    Level o = l;
    if (level >= 1) { o.dof = 0.0f; o.shafts = 0.0f; o.flare = 0.0f; }
    if (level >= 2) { o.reflect = 0.0f; }
    return o;
}

// ---- bloom (the engine's block: +0xBC enable, +0xC4 Threshold, +0xC8 Intensity) -----------------------
struct Bloom { bool on; float thr, inten; };
inline bool BloomSane(const Bloom& b) { return Fin(b.thr) && Fin(b.inten) && b.thr >= 0.0f && b.thr <= 16.0f && b.inten >= 0.0f && b.inten <= 16.0f; }
struct BloomOut { bool write; float thr, inten; };
// k = 0 .. 2. By day intensity x1.08 (restrained); golden hour x1.2; blue hour x1.35 / threshold x0.9;
// night x1.6 / threshold x0.78. Rain softens the light a little more (+10 % at full rain).
// Never below 0.2 threshold, never above 2.5 intensity (or the level's own, if that is higher).
inline BloomOut MapBloom(const Bloom& lv, float k, const gfxm::Tod& t, float rain)
{
    BloomOut o = {}; o.thr = lv.thr; o.inten = lv.inten;
    k = Clamp(k, 0.0f, 2.0f);
    if (!lv.on || k <= 0.0f || !BloomSane(lv)) return o;
    const float wsum = t.day + t.golden + t.blue + t.night;
    const float n = wsum > 1.0f ? 1.0f / wsum : 1.0f;
    const float up = (0.08f * t.day + 0.20f * t.golden + 0.35f * t.blue + 0.60f * t.night) * n + 0.10f * Clamp01(rain);
    const float dn = (0.10f * t.blue + 0.22f * t.night) * n;
    float inten = lv.inten * (1.0f + k * up);
    float thr = lv.thr * (1.0f - Clamp(k * dn, 0.0f, 0.6f));
    const float capI = lv.inten > 2.5f ? lv.inten : 2.5f;
    const float floorT = lv.thr < 0.2f ? lv.thr : 0.2f;
    o.inten = Clamp(Quant(inten, 1.0f / 256.0f), lv.inten, capI);     // only ever more glow than the level's own
    o.thr = Clamp(Quant(thr, 1.0f / 256.0f), floorT, lv.thr);
    o.write = o.inten != lv.inten || o.thr != lv.thr;
    return o;
}

// ---- reflections: the live cube map's exposure while it rains ------------------------------------------
struct LrzOut { bool write; float exposure; };
inline LrzOut MapLrz(bool on, float exposure, float k, float rain)
{
    LrzOut o = {}; o.exposure = exposure;
    k = Clamp(k, 0.0f, 2.0f); rain = Clamp01(rain);
    if (!on || k <= 0.0f || rain <= 0.0f || !Fin(exposure) || exposure <= 0.0f || exposure > 64.0f) return o;
    o.exposure = Clamp(Quant(exposure * (1.0f + 0.35f * k * rain), 1.0f / 256.0f), exposure, exposure * 1.7f);
    o.write = o.exposure != exposure;
    return o;
}

// ---- split-screen: view-dependent screen effects need ONE gameplay view ---------------------------------
// playerViews = distinct views drawn in the last half second whose camera is with a player;
// minFrac = the smallest viewport fraction (width or height) among them (1 = the whole screen).
inline bool IsSplit(int playerViews, float minFrac) { return playerViews >= 2 || (Fin(minFrac) && minFrac > 0.0f && minFrac < 0.95f); }

// ---- the shader floats (IniParams 12.xyzw, 13.xyzw; 15.w = the sentinel gfx2_fx.ini sets) --------------
struct Fx { float dof, dofStart, dofFull, shafts, flare, moon, split; };
inline Fx MapFx(const Level& l, const Opts& o, const gfxm::Tod& t, bool split)
{
    Fx f = {};
    const bool viewFx = !split || o.splitFx != 0;
    f.split = split ? 1.0f : 0.0f;
    f.dofStart = Clamp(o.dofStart, 60.0f, 2000.0f);              // never nearer than 60 u: gameplay stays sharp
    f.dofFull = Clamp(o.dofFull, f.dofStart + 40.0f, 6000.0f);
    f.dof = viewFx ? Clamp(l.dof * o.dof, 0.0f, 2.0f) : 0.0f;
    // the sun: nothing below the horizon; rays strongest at golden hour, modest at noon
    const float up = Smooth((t.elev + 0.02f) / 0.10f);
    const float sunK = up * (0.45f + 0.55f * Clamp01(t.golden) + 0.0f * t.day);
    f.shafts = viewFx ? Clamp(l.shafts * o.shafts * sunK, 0.0f, 2.0f) : 0.0f;
    f.flare = viewFx ? Clamp(l.flare * o.flare * up * (0.6f + 0.4f * Clamp01(t.golden)), 0.0f, 2.0f) : 0.0f;
    f.moon = Clamp(l.moon * o.moon * gfxm::Night01(t.elev), 0.0f, 2.0f);
    return f;
}
inline bool FxZero(const Fx& f) { return f.dof == 0.0f && f.shafts == 0.0f && f.flare == 0.0f && f.moon == 0.0f; }
// the floats fade like the knobs (a menu change or a view joining never pops)
inline Fx StepFx(const Fx& c, const Fx& g, float dt, float tau)
{
    Fx f = g;                                   // distances and the split flag are taken as they are
    f.dof = gfxm::Approach(c.dof, g.dof, dt, tau); f.shafts = gfxm::Approach(c.shafts, g.shafts, dt, tau);
    f.flare = gfxm::Approach(c.flare, g.flare, dt, tau); f.moon = gfxm::Approach(c.moon, g.moon, dt, tau);
    return f;
}

// ---- the frame counter: frame times of ONE view (every view is drawn once a frame) ---------------------
struct Stats
{
    enum : int { kN = 256 };
    float ms[kN]; int head, count;
    double last;
    float fps, avgMs, worstMs;       // over the last ~0.5 s / 0.5 s / 1 s
};
inline Stats StatsInit() { Stats s = {}; return s; }
// now = seconds (a steady clock). A gap over 1 s (a load, the pause menu) restarts the window.
inline void Tick(Stats& s, double now)
{
    if (!(s.last > 0.0) || now < s.last || now - s.last > 1.0) { s.head = 0; s.count = 0; s.last = now; return; }
    const float ms = static_cast<float>((now - s.last) * 1000.0);
    s.last = now;
    s.ms[s.head] = ms; s.head = (s.head + 1) % Stats::kN; if (s.count < Stats::kN) ++s.count;
    float acc = 0.0f, worst = 0.0f, accHalf = 0.0f; int nHalf = 0;
    for (int i = 0; i < s.count; ++i)
    {
        const float v = s.ms[(s.head - 1 - i + 2 * Stats::kN) % Stats::kN];
        if (acc + v > 1000.0f && i > 0) break;
        acc += v; if (v > worst) worst = v;
        if (accHalf < 500.0f) { accHalf += v; ++nHalf; }
    }
    s.avgMs = nHalf > 0 ? accHalf / static_cast<float>(nHalf) : 0.0f;
    s.fps = s.avgMs > 0.0f ? 1000.0f / s.avgMs : 0.0f;
    s.worstMs = worst;
}

// ---- the overlay's floats (IniParams 14.xyzw, 15.xyz) ---------------------------------------------------
struct Overlay { float on, fps, avgMs, worstMs, views, level, split; };
inline Overlay MapOverlay(bool show, const Stats& s, int views, int autoLevel, bool split)
{
    Overlay o = {};
    if (!show) return o;
    o.on = 1.0f;
    o.fps = Clamp(s.fps, 0.0f, 999.0f); o.avgMs = Clamp(s.avgMs, 0.0f, 999.9f); o.worstMs = Clamp(s.worstMs, 0.0f, 999.9f);
    o.views = static_cast<float>(views < 0 ? 0 : views > 9 ? 9 : views);
    o.level = static_cast<float>(autoLevel < 0 ? 0 : autoLevel > 9 ? 9 : autoLevel);
    o.split = split ? 1.0f : 0.0f;
    return o;
}

// ---- projecting a world point with a row-major 4x4 (row-vector convention: clip = p * M) ---------------
// Used only to check, once, which matrix in the view's camera copy is the view-projection (gfx2.h logs
// it; nothing depends on it yet - the sun's place on screen is worked out in the shader from the
// engine's own constants).
inline bool Project(const float m[16], const float p[3], float out[4])
{
    for (int c = 0; c < 4; ++c) out[c] = p[0] * m[c] + p[1] * m[4 + c] + p[2] * m[8 + c] + m[12 + c];
    return Fin(out[0]) && Fin(out[1]) && Fin(out[2]) && Fin(out[3]);
}
// A point 10 u ahead of the camera must land near the middle of the screen with w about 10.
inline bool LooksLikeViewProj(const float m[16], const float pos[3], const float fwd[3])
{
    const float p[3] = { pos[0] + 10.0f * fwd[0], pos[1] + 10.0f * fwd[1], pos[2] + 10.0f * fwd[2] };
    float c[4];
    if (!Project(m, p, c) || !(c[3] > 1.0f) || c[3] > 100.0f) return false;
    return std::fabs(c[0] / c[3]) < 0.05f && std::fabs(c[1] / c[3]) < 0.05f && std::fabs(c[3] - 10.0f) < 0.5f;
}

} // namespace gfx2m
