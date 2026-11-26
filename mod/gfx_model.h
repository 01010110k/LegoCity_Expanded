// =============================================================================================
//  gfx_model.h - the graphics overhaul's decisions, pure (2026-11-19, Notes\57). The same contract
//  as weather_model.h: plain numbers in, numbers out, so test\gfx_harness.cpp runs it as is.
//  gfx.h is the engine side (the per-view PostEffect settings, the render camera copy, the shader
//  floats).
//
//  The goal: a serious visual fidelity overhaul - graphics, lighting, atmosphere, performance -
//  and a camera rework with a little organic sway, in the manner of GTA, RDR and Watch Dogs 2,
//  with chromatic aberration, bloom and reflections as far as the engine allows.
//
//  THREE PRESETS (the pause menu's Graphics row, [gfx] preset):
//    0 Stock        nothing written anywhere: the game (and the rest of the mod) exactly as before
//    1 Enhanced     the look: a time-of-day colour grade in the engine's own colour stage (one clock),
//                   firmer ambient occlusion, objects drawn further away, a night rim light on the
//                   figures, a light aerial haze, high-quality god rays, and a subtle lens block in
//                   the 13 main-filter scene passes (sharpen, chromatic aberration, vignette, grain)
//    2 Performance  the grade and the sharpen stay (both nearly free); SSAO off, objects culled
//                   nearer, no haze / aberration / grain
//  and the CAMERA layer (its own row, [gfx] camera 0 off / 1 subtle / 2 strong): handheld sway when
//  idle, bank and a little lag into turns, a walk bob, a dip on landings and crashes - added to the
//  RENDER copy of each view's camera, never to the game's camera (gfx.h).
//
//  Every knob is "x the preset": the ini's 1.0 = the preset as designed, 0 = that part off.
// =============================================================================================
#pragma once
#include <cmath>
#include <cstdint>
#include <cstring>

namespace gfxm {

inline float Clamp(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
inline float Clamp01(float v) { return Clamp(v, 0.0f, 1.0f); }
inline float Smooth(float x) { x = Clamp01(x); return x * x * (3.0f - 2.0f * x); }
inline bool  Fin(float v) { return std::isfinite(v) != 0; }
inline float Quant(float v, float step) { return std::floor(v / step + 0.5f) * step; }
inline float Lerp(float a, float b, float t) { return a + (b - a) * t; }

enum : int { kStock = 0, kEnhanced = 1, kPerformance = 2, kPresets = 3 };
inline const char* PresetName(int p) { return p == kEnhanced ? "Enhanced" : p == kPerformance ? "Performance" : "Stock"; }

// ---- the strengths of one preset (all neutral = Stock) -----------------------------------------------
struct Knobs
{
    float grade;      // x the time-of-day colour grade (0 = none)
    float ao;         // x SSAO contrast (1 = the level's; 0 = SSAO switched off)
    float detail;     // x the object clip distances (1 = the level's)
    float rim;        // the night rim light: x (1 + rim * night) on the figures' rim strength
    float haze;       // the aerial haze (0 = none)
    float godrays;    // 1 = god rays in their high-quality mode where the level has them
    float ca, sharpen, vignette, grain, heat;   // the main-filter lens block (shader, slot 10 / 11)
};

inline Knobs Neutral() { Knobs k = {}; k.ao = 1.0f; k.detail = 1.0f; return k; }

inline Knobs PresetKnobs(int preset)
{
    Knobs k = Neutral();
    if (preset == kEnhanced)
    {
        k.grade = 1.0f; k.ao = 1.25f; k.detail = 1.5f; k.rim = 1.5f; k.haze = 1.0f; k.godrays = 1.0f;
        k.ca = 0.35f; k.sharpen = 0.4f; k.vignette = 0.35f; k.grain = 0.25f; k.heat = 0.0f;   // heat: opt-in (ini)
    }
    else if (preset == kPerformance)
    {
        k.grade = 1.0f; k.ao = 0.0f; k.detail = 0.75f; k.rim = 1.5f; k.haze = 0.0f; k.godrays = 0.0f;
        k.ca = 0.0f; k.sharpen = 0.5f; k.vignette = 0.25f; k.grain = 0.0f; k.heat = 0.0f;
    }
    return k;
}

// The player's scales on top of the preset ([gfx] grade, ao, ... : 1 = as designed, 0 = off, up to 2).
struct Scales { float grade, ao, detail, rim, haze, ca, sharpen, vignette, grain, heat; };
inline Scales UnitScales() { Scales s = { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 }; return s; }

inline Knobs Scaled(const Knobs& p, const Scales& s)
{
    Knobs k = p;
    k.grade = p.grade * Clamp(s.grade, 0.0f, 2.0f);
    // ao / detail are factors around 1: the scale scales the CHANGE (ao 0 = SSAO off stays off at any scale > 0)
    if (p.ao <= 0.0f) k.ao = s.ao > 0.0f ? 0.0f : 1.0f;
    else k.ao = 1.0f + (p.ao - 1.0f) * Clamp(s.ao, 0.0f, 2.0f);
    k.detail = Clamp(1.0f + (p.detail - 1.0f) * Clamp(s.detail, 0.0f, 2.0f), 0.5f, 3.0f);
    k.rim = p.rim * Clamp(s.rim, 0.0f, 2.0f);
    k.haze = p.haze * Clamp(s.haze, 0.0f, 2.0f);
    k.ca = p.ca * Clamp(s.ca, 0.0f, 3.0f);
    k.sharpen = p.sharpen * Clamp(s.sharpen, 0.0f, 2.5f);
    k.vignette = p.vignette * Clamp(s.vignette, 0.0f, 2.5f);
    k.grain = p.grain * Clamp(s.grain, 0.0f, 4.0f);
    k.heat = Clamp(s.heat, 0.0f, 2.0f) * (p.grade > 0.0f || p.sharpen > 0.0f ? 1.0f : 0.0f);   // opt-in, any non-stock preset
    k.godrays = p.godrays * (s.detail > 0.0f ? 1.0f : 0.0f);                                  // goes with the detail scale
    return k;
}

// The knobs move between presets over ~1.5 s (a menu change fades, never pops).
inline float Approach(float cur, float tgt, float dt, float tau)
{
    if (!(dt > 0.0f)) return cur;
    if (dt > 0.1f) dt = 0.1f;                      // a hitch moves no further than 0.1 s would
    const float a = 1.0f - std::exp(-dt / (tau > 0.01f ? tau : 0.01f));
    float v = cur + (tgt - cur) * a;
    if (std::fabs(v - tgt) < 1e-4f) v = tgt;       // lands exactly
    return v;
}
inline Knobs StepKnobs(const Knobs& cur, const Knobs& tgt, float dt, float tau)
{
    Knobs k;
    k.grade = Approach(cur.grade, tgt.grade, dt, tau); k.ao = Approach(cur.ao, tgt.ao, dt, tau);
    k.detail = Approach(cur.detail, tgt.detail, dt, tau); k.rim = Approach(cur.rim, tgt.rim, dt, tau);
    k.haze = Approach(cur.haze, tgt.haze, dt, tau); k.godrays = tgt.godrays;
    k.ca = Approach(cur.ca, tgt.ca, dt, tau); k.sharpen = Approach(cur.sharpen, tgt.sharpen, dt, tau);
    k.vignette = Approach(cur.vignette, tgt.vignette, dt, tau); k.grain = Approach(cur.grain, tgt.grain, dt, tau);
    k.heat = Approach(cur.heat, tgt.heat, dt, tau);
    return k;
}
inline bool IsNeutral(const Knobs& k)
{
    return k.grade == 0.0f && k.ao == 1.0f && k.detail == 1.0f && k.rim == 0.0f && k.haze == 0.0f && k.godrays == 0.0f &&
           k.ca == 0.0f && k.sharpen == 0.0f && k.vignette == 0.0f && k.grain == 0.0f && k.heat == 0.0f;
}

// ---- time of day: the ONE clock (IniParams 0.x = time + $tod; the shaders' sun is sin(clock * 0.01309)) ----
struct Tod { float elev; bool rising; float day, golden, blue, night; };

inline Tod TodOf(float clock)
{
    Tod t = {};
    const float ph = clock * 0.01309f;
    t.elev = std::sin(ph);
    t.rising = std::cos(ph) > 0.0f;
    const float e = t.elev;
    t.day    = Smooth((e - 0.15f) / 0.40f);
    t.golden = Smooth((e + 0.03f) / 0.10f) * (1.0f - Smooth((e - 0.10f) / 0.30f));
    t.blue   = Smooth((0.03f - e) / 0.08f) * (1.0f - Smooth((-0.20f - e) / 0.15f));
    t.night  = Smooth((-0.12f - e) / 0.25f);
    return t;
}
// 0 day .. 1 night, the same term weather.h uses
inline float Night01(float elev) { return Smooth((0.10f - elev) / 0.30f); }

// ---- the grade: deltas around the engine's neutral (balances 0.5, the rest x1) -----------------------
// Balance triples are (Cyan-Red, Magenta-Green, Yellow-Blue), the tuning UI's names [P names]; the
// direction (more = toward the second colour) is [I] from the names.
struct Grade { float sat, con; float sh[3], mid[3], hi[3]; };

inline void AddW(Grade& g, const Grade& d, float w)
{
    g.sat += d.sat * w; g.con += d.con * w;
    for (int i = 0; i < 3; ++i) { g.sh[i] += d.sh[i] * w; g.mid[i] += d.mid[i] * w; g.hi[i] += d.hi[i] * w; }
}

inline Grade GradeAt(const Tod& t, float strength)
{
    //                         sat     con     shadows CR MG YB        mids CR MG YB           highlights CR MG YB
    static const Grade kDay    = { 0.08f,  0.05f, { 0.0f,  0.0f,  0.02f }, { 0.0f,  0.0f,  0.0f  }, { 0.01f, 0.0f,  -0.02f } };
    static const Grade kGold   = { 0.12f,  0.03f, {-0.01f, -0.01f, 0.03f }, { 0.03f, 0.0f, -0.04f }, { 0.05f, 0.0f,  -0.07f } };
    static const Grade kBlue   = {-0.05f,  0.02f, {-0.03f,  0.0f,  0.07f }, { 0.0f,  0.0f,  0.04f }, { 0.02f, 0.0f,  -0.03f } };
    static const Grade kNight  = {-0.18f,  0.06f, {-0.04f,  0.0f,  0.07f }, { 0.0f,  0.0f,  0.03f }, { 0.04f, 0.0f,  -0.05f } };
    Grade g = {};
    float wsum = t.day + t.golden + t.blue + t.night;
    const float norm = wsum > 1.0f ? 1.0f / wsum : 1.0f;
    AddW(g, kDay, t.day * norm);
    Grade gold = kGold;
    if (t.rising) { gold.hi[1] -= 0.02f; gold.mid[1] -= 0.01f; }   // dawn a touch pinker than dusk
    AddW(g, gold, t.golden * norm);
    AddW(g, kBlue, t.blue * norm);
    AddW(g, kNight, t.night * norm);
    Grade out = {};
    AddW(out, g, Clamp(strength, 0.0f, 2.0f));
    return out;
}

// ---- the engine's colour block (Process Colour), as the settings hold it [P offsets: gfx.h] ----------
struct Colour
{
    bool  on;                          // +0xD0 (the evaluator drops the stage when all neutral)
    float bri, con, sat, gam, gain;    // neutral 1
    bool  balOn[3];                    // shadows / mids / highlights balance switches
    float bal[3][3];                   // [group][CR, MG, YB], neutral 0.5
    float grain, grainBoost, grainSize, x128;   // neutral 0, 0, 1.5, 0
};
inline Colour IdentityColour()
{
    Colour c = {};
    c.bri = c.con = c.sat = c.gam = c.gain = 1.0f;
    for (int g = 0; g < 3; ++g) for (int i = 0; i < 3; ++i) c.bal[g][i] = 0.5f;
    c.grainSize = 1.5f;
    return c;
}
inline bool ColourSane(const Colour& c)
{
    const float v[] = { c.bri, c.con, c.sat, c.gam, c.gain, c.grain, c.grainBoost, c.grainSize, c.x128 };
    for (float x : v) if (!Fin(x) || x < -1.0f || x > 16.0f) return false;
    for (int g = 0; g < 3; ++g) for (int i = 0; i < 3; ++i) if (!Fin(c.bal[g][i]) || c.bal[g][i] < -0.5f || c.bal[g][i] > 1.5f) return false;
    return true;
}
// the evaluator's own neutrality test (0x14037C213..0x14037C46A, tolerance 1e-4) [P]
inline bool ColourNeutral(const Colour& c)
{
    const float e = 1e-4f;
    auto n = [&](float v, float z) { return std::fabs(v - z) <= e; };
    if (!n(c.gain, 1) || !n(c.bri, 1) || !n(c.con, 1) || !n(c.sat, 1) || !n(c.gam, 1)) return false;
    for (int g = 0; g < 3; ++g)
        if (c.balOn[g] && (!n(c.bal[g][0], 0.5f) || !n(c.bal[g][1], 0.5f) || !n(c.bal[g][2], 0.5f))) return false;
    return n(c.grain, 0) && n(c.grainBoost, 0) && n(c.grainSize, 1.5f) && n(c.x128, 0);
}

// The grade composed onto the level's own: contrast / saturation multiply theirs, a balance adds to
// theirs (or switches on around 0.5). Brightness and gamma are never touched (night darkness belongs to
// the sun patch). Outputs are quantised so the engine sees a change only when it is a real one.
struct ColourOut { bool write; Colour c; };
inline ColourOut ApplyGrade(const Colour& level, const Grade& g)
{
    ColourOut o = {};
    Colour c = level.on ? level : IdentityColour();
    c.on = true;
    c.con = Clamp(Quant(c.con * (1.0f + g.con), 1.0f / 512.0f), 0.5f, 2.0f);
    c.sat = Clamp(Quant(c.sat * (1.0f + g.sat), 1.0f / 512.0f), 0.0f, 2.0f);
    const float* d[3] = { g.sh, g.mid, g.hi };
    for (int grp = 0; grp < 3; ++grp)
    {
        const bool any = std::fabs(d[grp][0]) > 1e-4f || std::fabs(d[grp][1]) > 1e-4f || std::fabs(d[grp][2]) > 1e-4f;
        if (!any) continue;
        if (!c.balOn[grp]) { c.balOn[grp] = true; c.bal[grp][0] = c.bal[grp][1] = c.bal[grp][2] = 0.5f; }
        for (int i = 0; i < 3; ++i) c.bal[grp][i] = Clamp(Quant(c.bal[grp][i] + d[grp][i], 1.0f / 1024.0f), 0.05f, 0.95f);
    }
    o.write = !ColourNeutral(c) || level.on;
    if (!level.on && ColourNeutral(c)) o.write = false;   // nothing to grade: the stage stays off (free)
    o.c = c;
    return o;
}

// ---- SSAO (+0x68C block) [P offsets and the evaluator's forced defaults] ------------------------------
struct Ssao { bool on; float contrast, r1, r2, r3, cut; };
inline Ssao SsaoDefaults() { Ssao s = { true, 2.4f, 0.13f, 0.2f, 0.3f, 1.8f }; return s; }
inline bool SsaoSane(const Ssao& s)
{
    return Fin(s.contrast) && Fin(s.r1) && Fin(s.r2) && Fin(s.r3) && Fin(s.cut) && s.contrast >= 0.0f && s.contrast < 50.0f &&
           s.r1 > -1.0f && s.r1 < 50.0f && s.r2 > -1.0f && s.r2 < 50.0f && s.r3 > -1.0f && s.r3 < 50.0f;
}
struct SsaoOut { bool write; Ssao s; };
inline SsaoOut MapSsao(const Ssao& level, float ao)
{
    SsaoOut o = {};
    if (ao == 1.0f) return o;                       // the level's own, untouched
    o.s = level.on ? level : SsaoDefaults();        // what the evaluator would have made of it
    o.s.on = true;
    if (ao <= 0.0f) o.s.r1 = 0.0f;                  // radius 1 <= 0: the evaluator drops the SSAO stage (0x14037BCF5) [P]
    else o.s.contrast = Clamp(o.s.contrast * ao, 0.0f, 8.0f);
    o.write = true;
    return o;
}

// ---- Engine Params (+0x08 block): rim light, object clip distances ------------------------------------
struct Engine { float rim; float clip[3]; };
inline bool EngineSane(const Engine& e)
{
    if (!Fin(e.rim) || e.rim < 0.0f || e.rim > 10.0f) return false;
    for (float c : e.clip) if (!Fin(c) || c < 0.0f || c > 1.0e6f) return false;
    return true;
}
struct EngineOut { bool write; Engine e; };
inline EngineOut MapEngine(const Engine& level, float detail, float rim, float night)
{
    EngineOut o = {}; o.e = level;
    if (detail != 1.0f)
        for (float& c : o.e.clip) if (c < 50000.0f) c = Clamp(c * detail, 2.0f, 3000.0f);   // 100000 = "never clip": left alone
    if (rim > 0.0f && night > 0.0f) o.e.rim = Clamp(level.rim * (1.0f + rim * night), 0.0f, 0.6f);
    o.write = o.e.rim != level.rim || o.e.clip[0] != level.clip[0] || o.e.clip[1] != level.clip[1] || o.e.clip[2] != level.clip[2];
    return o;
}

// ---- the aerial haze: the height fog's distance veil, only where the level already has height fog ------
// transmittance = 2^-(... + nearDensity * min(d, nearDistance)) (Notes 53 section 2): the veil reaches
// its full depth V at nearDistance and stays there. Ours: V = 0.32 x haze (20 % at 400 u+), half at night.
struct Haze { bool on; float nearDist, nearDens; };
struct HazeOut { bool write; float nearDist, nearDens; };
inline HazeOut MapHaze(const Haze& level, float haze, float night, float dist)
{
    HazeOut o = {};
    if (!level.on || haze <= 0.0f || !Fin(level.nearDist) || !Fin(level.nearDens) || level.nearDist < 0.0f || level.nearDens < 0.0f) return o;
    const float V = 0.32f * Clamp(haze, 0.0f, 2.0f) * (1.0f - 0.5f * Clamp01(night));
    if (level.nearDens * level.nearDist >= V) return o;            // the level's veil is already as deep: untouched
    o.nearDist = level.nearDist > dist ? level.nearDist : Clamp(dist, 50.0f, 5000.0f);
    o.nearDens = V / o.nearDist;
    o.write = true;
    return o;
}

// ---- the shader floats (IniParams 10.xyzw, 11.x; 11.w = the sentinel d3dx.ini sets) --------------------
struct ShaderOut { float ca, sharpen, vignette, grain, heat; };
inline ShaderOut MapShader(const Knobs& k, const Tod& t)
{
    ShaderOut s = {};
    s.ca = Clamp(k.ca, 0.0f, 3.0f);
    s.sharpen = Clamp(k.sharpen, 0.0f, 2.5f);
    s.vignette = Clamp(k.vignette * (1.0f + 0.3f * t.night), 0.0f, 2.5f);   // a little deeper at night
    s.grain = Clamp(k.grain * (1.0f + 0.6f * t.night), 0.0f, 4.0f);        // grain shows more in the dark, as film
    s.heat = Clamp(k.heat * Smooth((t.elev - 0.55f) / 0.30f), 0.0f, 2.0f);   // only round noon
    return s;
}

// ---- ReShade's LCU_DayNightCycle: is the second clock's grade switched on? (ReShadePreset.ini text) ----
inline bool ReShadeGradeOn(const char* text)
{
    if (!text) return false;
    const char* p = text;
    while (*p)
    {
        const char* line = p;
        while (*p && *p != '\n') ++p;
        const char* end = p;
        if (*p) ++p;
        while (line < end && (*line == ' ' || *line == '\t')) ++line;
        if (end - line >= 11 && std::strncmp(line, "Techniques=", 11) == 0)
        {
            for (const char* q = line + 11; q + 16 <= end; ++q)
                if (std::strncmp(q, "LCU_DayNightCycle", 17) == 0) return true;
            return false;
        }
    }
    return false;
}

// =============================================================================================
//  THE CAMERA LAYER. Everything comes from the camera's own path (no player, vehicle or animation
//  read): its speed, how fast it turns, how it moves up and down. Out: a small rotation in the camera's
//  own frame and a small offset along its right / up rows. A camera that jumps (a cut, a respawn, a
//  teleport) resets the motion terms; the handheld noise is a function of time only, so nothing pops.
// =============================================================================================
struct CamParams
{
    float intensity;     // 0 off, 1 subtle, 2 strong
    float bankSign;      // +1 / -1: which way "into the turn" is (the engine's axis handedness is [U])
    float handicam;      // 0..1: the Backrooms handicam's share (its own sway takes over)
};
struct CamState
{
    bool  valid;
    float pos[3], fwd[3];
    float vel[3];        // smoothed camera velocity (u/s)
    float speed;         // smoothed horizontal speed
    float yawRate, yawRateSlow;
    float bank;
    float dip, dipV;     // the landing / impact spring (u)
    float bobPhase, bobAmp;
    float vyFast, vySlow;  // vertical speed, quick and slow (an impact = the quick one stops first)
    float refractory;      // s: no second dip right after one
    float steady;          // s of unbroken frames: the layer fades in over 0.5 s (a view drawn now and then - an
                           // impostor or reflection render - never qualifies; a cut fades it back in)
};
struct CamOut { float yaw, pitch, roll; float ox, oy; };   // radians; u along the camera's right / up rows

inline float Noise1(float t, float seed)
{
    // a sum of incommensurate sines: smooth, never repeating in practice, zero mean, |.| <= 1
    return 0.5f * std::sin(t * 1.13f + seed) + 0.3f * std::sin(t * 2.71f + seed * 1.7f) + 0.2f * std::sin(t * 4.37f + seed * 2.9f);
}

inline bool Ortho(const float r[3][3])
{
    for (int i = 0; i < 3; ++i)
    {
        const float l = r[i][0] * r[i][0] + r[i][1] * r[i][1] + r[i][2] * r[i][2];
        if (!Fin(l) || std::fabs(l - 1.0f) > 0.03f) return false;
        for (int j = i + 1; j < 3; ++j)
            if (std::fabs(r[i][0] * r[j][0] + r[i][1] * r[j][1] + r[i][2] * r[j][2]) > 0.03f) return false;
    }
    return true;
}

// One frame. `rows` = the camera's three axis rows (0 right, 1 up, 2 forward or backward), pos = row 3.
// t = seconds (a steady clock), dt = this frame.
inline CamOut StepCamera(CamState& s, const float pos[3], const float rows[3][3], float t, float dt, const CamParams& p)
{
    CamOut o = {};
    if (!(p.intensity > 0.0f) || !Fin(t)) { s.valid = false; return o; }
    const float k0 = Clamp(p.intensity, 0.0f, 2.0f) * (1.0f - Clamp01(p.handicam));
    const float* f = rows[2];
    bool jump = !s.valid || !(dt > 0.0f) || dt > 0.25f;
    if (!jump)
    {
        const float dx = pos[0] - s.pos[0], dy = pos[1] - s.pos[1], dz = pos[2] - s.pos[2];
        const float d = std::sqrt(dx * dx + dy * dy + dz * dz);
        const float dotf = f[0] * s.fwd[0] + f[1] * s.fwd[1] + f[2] * s.fwd[2];
        if (d > 3.0f + 80.0f * dt || dotf < 0.9f) jump = true;   // > ~25 degrees in one frame: a cut
    }
    if (jump)
    {
        const float dip = s.dip, ph = s.bobPhase;
        s = CamState{};
        s.valid = true; s.dip = dip * 0.5f; s.bobPhase = ph; s.steady = 0.0f;
        for (int i = 0; i < 3; ++i) { s.pos[i] = pos[i]; s.fwd[i] = f[i]; }
    }
    else
    {
        // velocity, speed (smoothed over ~0.15 s), turn rate around world up (smoothed 0.12 / 0.6 s)
        const float a = 1.0f - std::exp(-dt / 0.15f);
        float v[3];
        for (int i = 0; i < 3; ++i) { v[i] = (pos[i] - s.pos[i]) / dt; s.vel[i] += (v[i] - s.vel[i]) * a; }
        const float spPrev = s.speed;
        s.speed = std::sqrt(s.vel[0] * s.vel[0] + s.vel[2] * s.vel[2]);
        const float h0 = std::atan2(s.fwd[0], s.fwd[2]), h1 = std::atan2(f[0], f[2]);
        float dh = h1 - h0;
        while (dh > 3.14159265f) dh -= 6.2831853f;
        while (dh < -3.14159265f) dh += 6.2831853f;
        const float w = Clamp(dh / dt, -6.0f, 6.0f);
        s.yawRate += (w - s.yawRate) * (1.0f - std::exp(-dt / 0.12f));
        s.yawRateSlow += (w - s.yawRateSlow) * (1.0f - std::exp(-dt / 0.6f));
        // landing / crash: the quick vertical speed has stopped while the slow one still says falling; or the
        // speed falls off a cliff (a crash). One kick, then 0.4 s of rest.
        const float vyRaw = v[1];
        s.vyFast += (vyRaw - s.vyFast) * (1.0f - std::exp(-dt / 0.04f));
        s.vySlow += (vyRaw - s.vySlow) * (1.0f - std::exp(-dt / 0.25f));
        s.refractory = s.refractory > dt ? s.refractory - dt : 0.0f;
        float kick = 0.0f;
        if (s.refractory <= 0.0f && s.vySlow < -3.0f && s.vyFast - s.vySlow > 2.5f) kick += Clamp((s.vyFast - s.vySlow) * 0.006f, 0.0f, 0.06f);
        const float decel = (spPrev - s.speed) / dt;
        if (s.refractory <= 0.0f && decel > 30.0f) kick += Clamp((decel - 30.0f) * 0.0008f, 0.0f, 0.05f);
        if (kick > 0.0f) s.refractory = 0.4f;
        s.dipV -= kick * 6.2831853f * 2.2f;          // an impulse of about `kick` u: the spring then swings back
        for (int i = 0; i < 3; ++i) { s.pos[i] = pos[i]; s.fwd[i] = f[i]; }
        s.steady = s.steady + dt > 10.0f ? 10.0f : s.steady + dt;
    }
    const float fadeIn = Smooth(s.steady / 0.5f);
    // the dip spring (2.2 Hz, damping 0.6), integrated in small steps
    {
        float rem = Clamp(dt, 0.0f, 0.1f);
        const float wn = 6.2831853f * 2.2f, z = 0.6f;
        while (rem > 0.0f)
        {
            const float h = rem > 0.005f ? 0.005f : rem; rem -= h;
            const float acc = -wn * wn * s.dip - 2.0f * z * wn * s.dipV;
            s.dipV += acc * h; s.dip += s.dipV * h;
        }
        s.dip = Clamp(s.dip, -0.12f, 0.12f);
        if (std::fabs(s.dip) < 1e-6f && std::fabs(s.dipV) < 1e-5f) { s.dip = 0.0f; s.dipV = 0.0f; }
    }
    const float k = k0 * fadeIn;
    const float deg = 0.0174533f;
    // handheld: strongest standing still, less at speed (a vehicle's own motion takes over)
    const float hand = k * Lerp(1.0f, 0.55f, Smooth(s.speed / 20.0f));
    o.pitch = hand * 0.16f * deg * Noise1(t * 0.55f, 1.3f);
    o.yaw   = hand * 0.20f * deg * Noise1(t * 0.47f, 4.1f);
    o.roll  = hand * 0.24f * deg * Noise1(t * 0.39f, 7.9f);
    // walk / run bob: a figure's pace (peaks round 4-8 u/s, gone above ~13 u/s where vehicles are)
    const float pace = Smooth((s.speed - 0.6f) / 1.5f) * (1.0f - Smooth((s.speed - 9.0f) / 4.0f));
    s.bobAmp += (pace - s.bobAmp) * (1.0f - std::exp(-Clamp(dt, 0.0f, 0.1f) / 0.25f));
    const float freq = Lerp(1.7f, 2.6f, Smooth((s.speed - 2.0f) / 6.0f));
    s.bobPhase = std::fmod(s.bobPhase + 6.2831853f * freq * Clamp(dt, 0.0f, 0.1f), 6.2831853f * 64.0f);
    o.oy   = k * s.bobAmp * 0.010f * std::sin(s.bobPhase * 2.0f);       // two steps a stride: up-down twice
    o.ox   = k * s.bobAmp * 0.006f * std::sin(s.bobPhase);
    o.roll += k * s.bobAmp * 0.10f * deg * std::sin(s.bobPhase);
    // bank into turns, and a little lag at the start / overshoot at the end of one
    s.bank += (Clamp(-s.yawRate * 0.022f, -0.026f, 0.026f) - s.bank) * (1.0f - std::exp(-Clamp(dt, 0.0f, 0.1f) / 0.2f));
    o.roll += k * s.bank * (p.bankSign < 0.0f ? -1.0f : 1.0f);
    o.yaw  += k * Clamp((s.yawRateSlow - s.yawRate) * 0.012f, -0.9f * deg, 0.9f * deg);
    // the dip: down, with the nose a touch down too
    o.oy += k * s.dip;
    o.pitch += k * s.dip * 0.06f;
    // hard limits whatever happens
    o.yaw = Clamp(o.yaw, -1.5f * deg, 1.5f * deg); o.pitch = Clamp(o.pitch, -1.5f * deg, 1.5f * deg);
    o.roll = Clamp(o.roll, -2.0f * deg, 2.0f * deg);
    o.ox = Clamp(o.ox, -0.05f, 0.05f); o.oy = Clamp(o.oy, -0.15f, 0.15f);
    return o;
}

// Rotate the three rows by (yaw about row 1, pitch about row 0, roll about row 2) in the camera's own
// frame, and offset the position along rows 0 / 1. Small-angle exact (Rodrigues per axis, then
// re-orthonormalised), so the rows stay a rotation.
inline void RotRows(float r[3][3], int axis, float ang)
{
    if (ang == 0.0f) return;
    const int a = (axis + 1) % 3, b = (axis + 2) % 3;
    const float c = std::cos(ang), s = std::sin(ang);
    float na[3], nb[3];
    for (int i = 0; i < 3; ++i) { na[i] = c * r[a][i] + s * r[b][i]; nb[i] = -s * r[a][i] + c * r[b][i]; }
    for (int i = 0; i < 3; ++i) { r[a][i] = na[i]; r[b][i] = nb[i]; }
}
inline void ApplyCamera(float rows[3][3], float pos[3], const CamOut& o)
{
    RotRows(rows, 1, o.yaw);
    RotRows(rows, 0, o.pitch);
    RotRows(rows, 2, o.roll);
    for (int i = 0; i < 3; ++i) pos[i] += rows[0][i] * o.ox + rows[1][i] * o.oy;
}

// [fix1] the far plane of a render camera at (x, z). The sky dome (lego_city_noon_dx11.gsc mesh 2) is a world-fixed mesh
// round the map centre (local radius ~1134 u, x ~1.35 by its node [I]); far out - the Outport at z ~880, the Wilds - its
// far side lies beyond the camera's far plane (camera +0x350, read by the projection 0x140323E10 [P]) and is cut off.
// Only out there (r > from) the far plane is raised to cover the dome: (r + dome) x 1.05, capped; never lowered.
struct FarParams { int on; float from, dome, maxFar; };
inline float FarFor(float x, float z, float nearP, float farP, const FarParams& p)
{
    if (!p.on || !Fin(x) || !Fin(z) || !Fin(nearP) || !Fin(farP)) return farP;
    if (!(nearP > 0.0f && farP > nearP && farP < 1.0e6f)) return farP;          // not a projection we understand: leave it
    const float r = std::sqrt(x * x + z * z);
    if (!(r > p.from)) return farP;
    float need = (r + p.dome) * 1.05f;
    if (need > p.maxFar) need = p.maxFar;
    return need > farP ? need : farP;
}

} // namespace gfxm
