// =============================================================================================
//  weather_model.h - rain, fog and weather zones (2026-11-18, Notes\53): the data and every
//  decision, pure - the same contract as spots_model.h: plain facts in, numbers out, so
//  test\weather_harness.cpp runs it as is. weather.h is the engine side.
//
//  The goal: the east coast settlement leans a little dreary - weird abandoned Newfoundland
//  villages mixed with Silent Hill (without the monsters) - which wants volumetric fog, and
//  rain everywhere.
//
//  THE WEATHER is one small struct, Mix. All zero = the game exactly as it is. It is made from
//    - what the player set for everywhere ([weather] rain / fog; the pause menu's Weather page), and
//    - the weather ZONES: places with their own weather, from DATA (lcudrive_weather.txt beside
//      lcudrive.ini; tools\make_weather.py writes it from a sites.json):
//
//      zone, <name>, <x>, <z>, <radius>, <falloff>, <fog>, <rain>, <overcast> [, <r>, <g>, <b> [, <duty>]]
//      sub,  <name>, <x>, <z>, <radius>, <falloff>, <kind>            (belongs to the zone above)
//
//    One record a line, fields separated by commas, '#' or ';' starts a comment.
//      zone   full weather inside <radius> u of (x, z), fading to NOTHING over the next <falloff> u.
//             Beyond radius + falloff a zone's weight is exactly 0: it cannot touch anything there.
//             fog = off | light | heavy; rain = off | drizzle | rain | storm; overcast 0..1 = how grey
//             and flat the light is; r g b = the fog's colour (0..1 each; default a cold grey);
//             duty = the share of the time it rains there (0..1, default 0.75; 1 = always).
//      sub    a part of the zone above where the fog is thicker: kind = normal (nothing extra) |
//             odd (somewhat) | weird (much; a faint sickly tint). It only counts inside its zone.
//
//  Where the weather is taken: at a CAMERA (each split-screen view asks for its own post-effect
//  settings, weather.h hooks that), so each player's screen has the weather of where it looks from.
//  The mix moves toward its target over a few seconds (Step) - never a pop, whatever the cause
//  (walking in, a teleport, a menu change, the shower starting).
// =============================================================================================
#pragma once
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <cstdint>

namespace wxm {

enum : int { kMaxZones = 24, kMaxSubs = 64 };
enum Rain : int { rOff = 0, rDrizzle, rRain, rStorm, kRains };
// [weather] rain = 4: "Changing" - the weather everywhere comes and goes by itself (ChangingMix). Only for the
// player's own setting; a zone's rain is still one of the four above (kRains is what zone lines may name).
enum : int { kChanging = kRains, kUserRains };
enum Fog  : int { fOff = 0, fLight, fHeavy, kFogs };
enum Kind : int { kNormal = 0, kOdd, kWeird, kKinds };

inline const char* RainName(int r) { static const char* n[] = { "off", "drizzle", "rain", "storm" }; return r >= 0 && r < kRains ? n[r] : "?"; }
inline const char* FogName(int f)  { static const char* n[] = { "off", "light", "heavy" }; return f >= 0 && f < kFogs ? n[f] : "?"; }
inline const char* KindName(int k) { static const char* n[] = { "normal", "odd", "weird" }; return k >= 0 && k < kKinds ? n[k] : "?"; }
inline int FromName(const char* s, const char* (*name)(int), int count)
{
    if (!s || !*s) return -1;
    for (int i = 0; i < count; ++i) if (_stricmp(s, name(i)) == 0) return i;
    char* end = nullptr;
    const long v = std::strtol(s, &end, 10);
    if (end && end != s && !*end && v >= 0 && v < count) return static_cast<int>(v);
    return -1;
}

inline float Clamp01(float v) { return v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v; }
inline float Clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
inline float Lerp(float a, float b, float t) { return a + (b - a) * t; }
inline float Smooth(float t) { t = Clamp01(t); return t * t * (3.0f - 2.0f * t); }
inline bool  Fin(float v) { return v == v && v > -1.0e30f && v < 1.0e30f; }

// ---------------------------------------------------------------------------------------------
//  The weather at one place and time. All zero = nothing of ours.
//  The fog's numbers are PREMULTIPLIED by fogw (visW = fogw * visibility ...), so that blending two
//  mixes - or fading one in from nothing - gives the weighted average of the fogs that are really
//  there, never a half-way value dragged toward a meaningless zero.
// ---------------------------------------------------------------------------------------------
struct Mix
{
    float rain;        // 0..1 how hard it rains (drizzle 0.3, rain 0.65, storm 1)
    float gust;        // 0..1 the storm part: slant and speed
    float overcast;    // 0..1 the grey, flat light
    float mist;        // 0..1 drifting veils on the screen
    float fogw;        // 0..1 how far the engine's fog is taken to ours
    float visW;        // fogw x the distance (u) at which the veil has taken ~90 %
    float lowW;        // fogw x the ground fog's density at height 0
    float thickW;      // fogw x the ground fog's thickness (u)
    float tintW[3];    // fogw x the fog's colour
};
enum : int { kMixFloats = sizeof(Mix) / sizeof(float) };
inline float* F(Mix& m) { return &m.rain; }
inline const float* F(const Mix& m) { return &m.rain; }

inline float Vis(const Mix& m)   { return m.fogw > 1.0e-4f ? m.visW / m.fogw : 0.0f; }
inline float Low(const Mix& m)   { return m.fogw > 1.0e-4f ? m.lowW / m.fogw : 0.0f; }
inline float Thick(const Mix& m) { return m.fogw > 1.0e-4f ? m.thickW / m.fogw : 0.0f; }
inline void  Tint(const Mix& m, float out[3])
{
    for (int k = 0; k < 3; ++k) out[k] = m.fogw > 1.0e-4f ? Clamp01(m.tintW[k] / m.fogw) : 0.0f;
}
// How much fog a mix is: 0 = none. (the veil's optical weight: more with fogw, more the shorter the sight)
inline float FogStrength(const Mix& m) { const float v = Vis(m); return v > 0.0f ? m.fogw * (100.0f / v) : 0.0f; }

inline Mix LerpMix(const Mix& a, const Mix& b, float t)
{
    Mix o;
    for (int i = 0; i < kMixFloats; ++i) F(o)[i] = Lerp(F(a)[i], F(b)[i], t);
    return o;
}
// The stronger of two weathers, part by part (a zone never makes it rain LESS than the player set).
inline Mix MaxMix(const Mix& a, const Mix& b)
{
    Mix o = a;
    if (b.rain > o.rain) o.rain = b.rain;
    if (b.gust > o.gust) o.gust = b.gust;
    if (b.overcast > o.overcast) o.overcast = b.overcast;
    if (b.mist > o.mist) o.mist = b.mist;
    if (FogStrength(b) > FogStrength(a))
    {
        o.fogw = b.fogw; o.visW = b.visW; o.lowW = b.lowW; o.thickW = b.thickW;
        for (int k = 0; k < 3; ++k) o.tintW[k] = b.tintW[k];
    }
    return o;
}
inline bool IsZero(const Mix& m) { for (int i = 0; i < kMixFloats; ++i) if (F(m)[i] != 0.0f) return false; return true; }

// ---------------------------------------------------------------------------------------------
//  Settings ([weather]; config.h fills this)
// ---------------------------------------------------------------------------------------------
struct Params
{
    // the fog presets
    float visLight = 150.0f, visHeavy = 48.0f;       // u: where the veil has taken ~90 % of what is behind it
    float lowLight = 0.04f,  lowHeavy = 0.12f;       // the ground fog's density at height 0 (the engine's "Exp Fog Density At Origin")
    float thickLight = 12.0f, thickHeavy = 7.0f;     // u: the ground fog's thickness ("Exp Fog Vertical Thickness")
    float tint[3] = { 0.62f, 0.66f, 0.70f };         // the fog's colour where a zone names none
    float weird = 0.45f;                             // a weird sub-zone: sight x (1 - weird), ground fog x (1 + 2 weird)
    // feel
    float blend = 4.0f;                              // s: the time the weather takes to change (never a pop)
    float nightFog = 0.6f;                           // the share of the fog kept after dark (night must stay playable)
    float overcast = 1.0f;                           // x the grey light (0 = no grade)
    float mist = 1.0f;                               // x the drifting veils
    float shower = 360.0f;                           // s: one turn of a zone's rain (rains `duty` of it)
    float spell = 300.0f;                            // s: "Changing": how long one spell of weather lasts
    float wetShare = 0.3f;                           // "Changing": the share of spells with rain in them
    // which parts are driven
    bool  screenRain = true;                         // rain drawn on the screen (ShaderFixes main filter)
    bool  engineRain = false;                        // the engine's own rain stage (experimental)
    bool  engineFog  = true;                         // the engine's own exponential height fog
};

// ---------------------------------------------------------------------------------------------
//  The data
// ---------------------------------------------------------------------------------------------
struct Zone
{
    char  name[40];
    float x, z, radius, falloff;
    int   fog, rain;
    float overcast;
    float tint[3]; bool hasTint;
    float duty;
    int   sub0, nSub;
};
struct Sub
{
    char  name[40];
    float x, z, radius, falloff;
    int   kind, zone;
};
struct Table
{
    Zone zones[kMaxZones]; int nZones;
    Sub  subs[kMaxSubs];   int nSubs;
    bool zoneOpen;
    int  bad, firstBadLine; char firstBad[96];
};

namespace detail {
inline const char* Skip(const char* s) { while (*s == ' ' || *s == '\t') ++s; return s; }
inline bool Num(const char* s, float& out)
{
    s = Skip(s);
    if (!*s) return false;
    char* end = nullptr;
    const double v = std::strtod(s, &end);
    if (!end || end == s) return false;
    end = const_cast<char*>(Skip(end));
    if (*end) return false;
    if (!(v == v) || v > 1.0e9 || v < -1.0e9) return false;
    out = static_cast<float>(v);
    return true;
}
inline bool NameOk(const char* s, size_t cap)
{
    const size_t n = strlen(s);
    if (n == 0 || n >= cap) return false;
    for (const char* c = s; *c; ++c) if (static_cast<unsigned char>(*c) < 32 || static_cast<unsigned char>(*c) > 126) return false;
    return true;
}
inline bool PlaceOk(float x, float z, float radius, float falloff)
{
    return std::fabs(x) <= 5000.0f && std::fabs(z) <= 5000.0f && radius >= 1.0f && radius <= 2000.0f &&
           falloff >= 0.0f && falloff <= 2000.0f;
}
} // namespace detail

// One line -> the table. 0 = taken, 1 = blank / comment, 2 = refused (*why says it).
inline int ParseLine(const char* line, Table& d, const char** why)
{
    using namespace detail;
    static const char* dummy; if (!why) why = &dummy;
    char buf[256];
    {
        size_t n = 0;
        for (const char* c = line; *c && *c != '\r' && *c != '\n' && *c != '#' && *c != ';' && n + 1 < sizeof(buf); ++c) buf[n++] = *c;
        buf[n] = 0;
    }
    if (!*Skip(buf)) return 1;
    char f[13][64] = {};
    int n = 0;
    for (const char* s = buf; n < 13; )
    {
        s = Skip(s);
        const char* e = s;
        while (*e && *e != ',') ++e;
        const char* t = e;
        while (t > s && (t[-1] == ' ' || t[-1] == '\t')) --t;
        const size_t len = static_cast<size_t>(t - s);
        if (len >= sizeof(f[0])) { *why = "a field is too long"; return 2; }
        memcpy(f[n], s, len); f[n][len] = 0; ++n;
        if (!*e) break;
        s = e + 1;
    }
    float v[8] = {};
    if (_stricmp(f[0], "zone") == 0)
    {
        d.zoneOpen = false;
        if (n < 9) { *why = "zone needs: name, x, z, radius, falloff, fog, rain, overcast"; return 2; }
        if (d.nZones >= kMaxZones) { *why = "too many zones"; return 2; }
        if (!NameOk(f[1], sizeof(d.zones[0].name))) { *why = "a zone needs a plain name"; return 2; }
        for (int i = 0; i < 4; ++i) if (!Num(f[2 + i], v[i])) { *why = "x, z, radius and falloff must be numbers"; return 2; }
        if (!PlaceOk(v[0], v[1], v[2], v[3])) { *why = "x, z within 5000; radius 1..2000; falloff 0..2000"; return 2; }
        const int fog = FromName(f[6], FogName, kFogs);
        const int rain = FromName(f[7], RainName, kRains);
        if (fog < 0) { *why = "fog must be off, light or heavy"; return 2; }
        if (rain < 0) { *why = "rain must be off, drizzle, rain or storm"; return 2; }
        if (!Num(f[8], v[4]) || v[4] < 0.0f || v[4] > 1.0f) { *why = "overcast must be 0..1"; return 2; }
        Zone z = {};
        strcpy_s(z.name, f[1]);
        z.x = v[0]; z.z = v[1]; z.radius = v[2]; z.falloff = v[3]; z.fog = fog; z.rain = rain; z.overcast = v[4];
        z.duty = 0.75f;
        if (n >= 12)
        {
            for (int i = 0; i < 3; ++i)
                if (!Num(f[9 + i], v[5 + i]) || v[5 + i] < 0.0f || v[5 + i] > 1.0f) { *why = "the tint is three numbers 0..1"; return 2; }
            z.tint[0] = v[5]; z.tint[1] = v[6]; z.tint[2] = v[7]; z.hasTint = true;
        }
        else if (n != 9) { *why = "the tint needs all three of r, g, b"; return 2; }
        if (n >= 13)
        {
            float duty = 0.0f;
            if (!Num(f[12], duty) || duty < 0.0f || duty > 1.0f) { *why = "duty must be 0..1"; return 2; }
            z.duty = duty;
        }
        z.sub0 = d.nSubs; z.nSub = 0;
        d.zones[d.nZones++] = z;
        d.zoneOpen = true;
        return 0;
    }
    if (_stricmp(f[0], "sub") == 0)
    {
        if (n < 7) { *why = "sub needs: name, x, z, radius, falloff, kind"; return 2; }
        if (d.nZones == 0) { *why = "a sub before any zone"; return 2; }
        if (!d.zoneOpen) { *why = "this sub's zone line was refused"; return 2; }
        if (d.nSubs >= kMaxSubs) { *why = "too many subs"; return 2; }
        Zone& z = d.zones[d.nZones - 1];
        if (!NameOk(f[1], sizeof(d.subs[0].name))) { *why = "a sub needs a plain name"; return 2; }
        for (int i = 0; i < 4; ++i) if (!Num(f[2 + i], v[i])) { *why = "x, z, radius and falloff must be numbers"; return 2; }
        if (!PlaceOk(v[0], v[1], v[2], v[3])) { *why = "x, z within 5000; radius 1..2000; falloff 0..2000"; return 2; }
        const int kind = FromName(f[6], KindName, kKinds);
        if (kind < 0) { *why = "kind must be normal, odd or weird"; return 2; }
        Sub s = {};
        strcpy_s(s.name, f[1]);
        s.x = v[0]; s.z = v[1]; s.radius = v[2]; s.falloff = v[3]; s.kind = kind; s.zone = d.nZones - 1;
        d.subs[d.nSubs++] = s;
        ++z.nSub;
        return 0;
    }
    *why = "a line starts with zone or sub";
    return 2;
}

// The whole text -> the table. Returns the records taken; d.bad counts the refused lines.
inline int Parse(const char* text, Table& d)
{
    memset(&d, 0, sizeof(d));
    int taken = 0, lineNo = 0;
    const char* s = text ? text : "";
    while (*s)
    {
        const char* e = s;
        while (*e && *e != '\n') ++e;
        char line[300];
        size_t len = static_cast<size_t>(e - s);
        if (len >= sizeof(line)) len = sizeof(line) - 1;
        memcpy(line, s, len); line[len] = 0;
        ++lineNo;
        const char* why = "";
        const int r = ParseLine(line, d, &why);
        if (r == 0) ++taken;
        else if (r == 2)
        {
            if (!d.bad) { d.firstBadLine = lineNo; strncpy_s(d.firstBad, why, _TRUNCATE); }
            ++d.bad;
        }
        s = *e ? e + 1 : e;
    }
    d.zoneOpen = false;
    return taken;
}

// ---------------------------------------------------------------------------------------------
//  A zone's weight at a place: 1 inside the radius, a smooth fall to EXACTLY 0 at radius + falloff
//  and beyond (the promise that a zone touches nothing outside its reach).
// ---------------------------------------------------------------------------------------------
inline float Weight(float px, float pz, float cx, float cz, float radius, float falloff)
{
    const float dx = px - cx, dz = pz - cz;
    const float d = std::sqrt(dx * dx + dz * dz);
    if (!Fin(d)) return 0.0f;
    if (d <= radius) return 1.0f;
    if (falloff <= 0.0f || d >= radius + falloff) return 0.0f;
    return Smooth(1.0f - (d - radius) / falloff);
}
inline float ZoneWeight(const Zone& z, float px, float pz) { return Weight(px, pz, z.x, z.z, z.radius, z.falloff); }

// How weird it is at a place of a zone, 0..1: its subs' kinds (odd 0.5, weird 1) by their own
// weights, the strongest winning. (0 outside every sub; the caller multiplies the zone's weight in.)
inline float WeirdAt(const Table& t, const Zone& z, float px, float pz)
{
    float w = 0.0f;
    for (int i = 0; i < z.nSub; ++i)
    {
        const Sub& s = t.subs[z.sub0 + i];
        const float k = s.kind == kWeird ? 1.0f : s.kind == kOdd ? 0.5f : 0.0f;
        const float v = k * Weight(px, pz, s.x, s.z, s.radius, s.falloff);
        if (v > w) w = v;
    }
    return w;
}

// ---------------------------------------------------------------------------------------------
//  State mapping: the menu's words -> numbers
// ---------------------------------------------------------------------------------------------
inline float RainAmount(int r)   { static const float v[] = { 0.0f, 0.30f, 0.65f, 1.0f }; return r >= 0 && r < kRains ? v[r] : 0.0f; }
inline float RainGust(int r)     { static const float v[] = { 0.0f, 0.0f, 0.25f, 1.0f }; return r >= 0 && r < kRains ? v[r] : 0.0f; }
inline float RainOvercast(int r) { static const float v[] = { 0.0f, 0.35f, 0.60f, 0.85f }; return r >= 0 && r < kRains ? v[r] : 0.0f; }
inline float RainMist(int r)     { static const float v[] = { 0.0f, 0.10f, 0.20f, 0.35f }; return r >= 0 && r < kRains ? v[r] : 0.0f; }

inline void SetFog(Mix& m, float vis, float low, float thick, const float tint[3])
{
    m.fogw = 1.0f; m.visW = vis; m.lowW = low; m.thickW = thick;
    for (int k = 0; k < 3; ++k) m.tintW[k] = Clamp01(tint[k]);
}

// The weather of a (rain, fog) pair with a rain share `wet` (0..1: a zone's shower; 1 for the player's
// own setting) and a weirdness (0..1).
inline Mix StateMix(int rain, int fog, const Params& p, float wet = 1.0f, float weird = 0.0f, const float* tint = nullptr,
                    float overcast = 0.0f)
{
    Mix m = {};
    rain = rain < 0 ? 0 : rain >= kRains ? kRains - 1 : rain;
    fog  = fog  < 0 ? 0 : fog  >= kFogs  ? kFogs - 1  : fog;
    wet = Clamp01(wet); weird = Clamp01(weird);
    m.rain = RainAmount(rain) * wet;
    m.gust = RainGust(rain) * wet;
    m.overcast = RainOvercast(rain) * (0.5f + 0.5f * wet);          // the clouds stay between showers, thinner
    m.mist = RainMist(rain) * wet;
    if (overcast > m.overcast) m.overcast = Clamp01(overcast);
    const float* tn = tint ? tint : p.tint;
    float tw[3] = { tn[0], tn[1], tn[2] };
    if (weird > 0.0f)                                               // a faint sickly cast
    {
        tw[0] = Lerp(tw[0], tw[0] * 0.98f + 0.02f, weird);
        tw[1] = Lerp(tw[1], tw[1] * 1.00f + 0.01f, weird);
        tw[2] = Lerp(tw[2], tw[2] * 0.90f, weird);
    }
    if (fog == fLight || fog == fHeavy)
    {
        const bool h = fog == fHeavy;
        float vis = h ? p.visHeavy : p.visLight, low = h ? p.lowHeavy : p.lowLight, thick = h ? p.thickHeavy : p.thickLight;
        vis *= 1.0f - Clampf(p.weird, 0.0f, 0.9f) * weird;
        low *= 1.0f + 2.0f * Clampf(p.weird, 0.0f, 0.9f) * weird;
        SetFog(m, vis, low, thick, tw);
        const float oc = h ? 0.5f : 0.25f, ms = h ? 0.6f : 0.3f;
        if (oc > m.overcast) m.overcast = oc;
        if (ms > m.mist) m.mist = ms;
    }
    m.mist = Clamp01(m.mist + 0.4f * weird * (fog != fOff ? 1.0f : 0.0f));
    m.overcast = Clamp01(m.overcast + 0.15f * weird);
    return m;
}

// ---------------------------------------------------------------------------------------------
//  A zone's shower: it rains `duty` of every `period` seconds, each shower with its own strength,
//  coming and going over `edge` seconds. Pure in the time, so it is the same for every view.
//  Returns 0..1.
// ---------------------------------------------------------------------------------------------
inline float Hash01(uint32_t n)
{
    n ^= n >> 16; n *= 0x7FEB352Du; n ^= n >> 15; n *= 0x846CA68Bu; n ^= n >> 16;
    return (n & 0xFFFFFF) / 16777216.0f;
}
inline float Shower(double tSec, float duty, float period, uint32_t seed, float edge = 25.0f)
{
    duty = Clamp01(duty);
    if (duty <= 0.0f) return 0.0f;
    if (duty >= 0.999f || period < 1.0f) return 1.0f;
    const double shifted = tSec / period + Hash01(seed * 2654435761u + 17u);
    const double turn = std::floor(shifted);
    const float ph = static_cast<float>(shifted - turn);               // 0..1 within this turn
    const float e = Clampf(edge / period, 0.001f, duty * 0.5f);
    float env = 0.0f;
    if (ph < duty) env = Smooth(ph / e) * Smooth((duty - ph) / e);
    const float strength = 0.7f + 0.3f * Hash01(static_cast<uint32_t>(static_cast<int64_t>(turn)) * 2246822519u + seed);
    return Clamp01(env * strength);
}
inline uint32_t NameSeed(const char* s)
{
    uint32_t h = 2166136261u;
    for (; s && *s; ++s) { h ^= static_cast<unsigned char>(*s); h *= 16777619u; }
    return h;
}

// ---------------------------------------------------------------------------------------------
//  "Changing" weather (2026-11-25): a weather system that makes it rain
//  occasionally. Time is cut into spells of p.spell seconds; each spell draws its weather from the
//  hash of its number and the session seed:
//      rain   wetShare of the spells: drizzle 40 %, rain 40 %, storm 20 %, each with its own strength
//      cloudy 30 % of the dry spells: grey light only, overcast 0.25..0.55
//      mist   6 % of the dry spells: light fog and clouds (a misty morning, any time)
//      clear  the rest
//  The last `edge` seconds of a spell blend into the next (a shower builds and fades, the clouds
//  come before it), so the result is continuous in time. Pure in (time, seed): every view and the
//  harness see the same sky. The player's own fog setting stays on top (the stronger fog wins).
// ---------------------------------------------------------------------------------------------
struct Spell { int rain; int fog; float strength; float overcast; };
inline Spell SpellRaw(int64_t k, uint32_t seed, float wetShare)
{
    const uint32_t kk = static_cast<uint32_t>(k) * 2654435761u ^ static_cast<uint32_t>(k >> 32) * 40503u;
    const float h  = Hash01(kk + seed);
    const float h2 = Hash01(kk * 3u + seed + 101u);
    const float h3 = Hash01(kk * 7u + seed + 211u);
    Spell s = { rOff, fOff, 1.0f, 0.0f };
    const float w = Clamp01(wetShare);
    if (h < w)
    {
        s.rain = h2 < 0.4f ? rDrizzle : h2 < 0.8f ? rRain : rStorm;
        s.strength = 0.75f + 0.25f * h3;
    }
    else
    {
        const float d = w < 1.0f ? (h - w) / (1.0f - w) : 0.0f;            // 0..1 over the dry spells
        if (d < 0.30f)      s.overcast = 0.25f + 0.30f * h3;              // cloudy
        else if (d > 0.94f) { s.fog = fLight; s.overcast = 0.35f; }       // mist
    }
    return s;
}
// 2026-11-25, so that it does not rain constantly: the draw above alone can open a session
// with a storm and follow it with more rain. Two rules on top of it:
//   - a wet spell never follows a wet spell (the second becomes a cloudy one): rain is one spell at most, then a
//     break of at least one spell;
//   - the spell a session STARTS in and the one after it are dry (k0 = the spell of the session's first frame), so a
//     session never opens in the rain; the second may be cloudy if rain was drawn for it.
static const int64_t kNoStart = INT64_MIN;
inline Spell SpellAt(int64_t k, uint32_t seed, float wetShare, int64_t k0 = kNoStart)
{
    Spell s = SpellRaw(k, seed, wetShare);
    const bool start = k0 != kNoStart && (k == k0 || k == k0 + 1);
    if (s.rain != rOff && (start || SpellRaw(k - 1, seed, wetShare).rain != rOff))
    {
        const bool first = k0 != kNoStart && k == k0;
        s.rain = rOff; s.strength = 1.0f; s.overcast = first ? 0.0f : 0.40f;
    }
    if (k0 != kNoStart && k == k0) { s.fog = fOff; s.overcast = 0.0f; }     // the first minutes: plain clear
    return s;
}
inline const char* SpellName(const Spell& s)
{
    return s.rain == rStorm ? "storm" : s.rain == rRain ? "rain" : s.rain == rDrizzle ? "drizzle"
         : s.fog != fOff ? "mist" : s.overcast > 0.0f ? "cloudy" : "clear";
}
inline int64_t SpellIndex(double tSec, const Params& p, uint32_t seed)
{
    const float spell = p.spell >= 30.0f ? p.spell : 30.0f;
    return static_cast<int64_t>(std::floor(tSec / spell + Hash01(seed ^ 0x5bd1e995u)));
}
inline Mix SpellMix(const Spell& s, const Params& p)
{
    return StateMix(s.rain, s.fog, p, s.rain != rOff ? s.strength : 1.0f, 0.0f, nullptr, s.overcast);
}
inline Mix ChangingMix(double tSec, const Params& p, uint32_t seed, int* rainNow = nullptr, double startSec = -1.0)
{
    const float spell = p.spell >= 30.0f ? p.spell : 30.0f;
    const double u = tSec / spell + Hash01(seed ^ 0x5bd1e995u);
    const double kf = std::floor(u);
    const int64_t k = static_cast<int64_t>(kf);
    const float ph = static_cast<float>(u - kf) * spell;                   // s into this spell
    const float edge = spell * 0.3f < 75.0f ? spell * 0.3f : 75.0f;        // s of blending into the next
    const int64_t k0 = startSec >= 0.0 ? SpellIndex(startSec, p, seed) : kNoStart;
    const Spell a = SpellAt(k, seed, p.wetShare, k0), b = SpellAt(k + 1, seed, p.wetShare, k0);
    const float tb = Smooth((ph - (spell - edge)) / edge);
    if (rainNow) *rainNow = tb < 0.5f ? a.rain : b.rain;
    if (tb <= 0.0f) return SpellMix(a, p);
    return LerpMix(SpellMix(a, p), SpellMix(b, p), tb);
}

// The player's own weather for everywhere: one of the four rains, or "Changing" (with the fog on top).
inline Mix UserMix(int userRain, int userFog, double tSec, const Params& p, uint32_t seed, double startSec = -1.0)
{
    if (userRain == kChanging) return MaxMix(ChangingMix(tSec, p, seed, nullptr, startSec), StateMix(rOff, userFog, p));
    return StateMix(userRain, userFog, p);
}

// ---------------------------------------------------------------------------------------------
//  The target weather at a place: the player's own, and every zone that reaches the place blended in
//  by its weight. With `zones` false, or outside every zone, it is exactly the player's own.
//  inZone (optional): the index of the zone weighing most here and its weight (-1, 0 = none).
// ---------------------------------------------------------------------------------------------
inline Mix TargetMix(const Table& t, float px, float pz, const Mix& user, bool zones, double tSec,
                     const Params& p, int* inZone = nullptr, float* inWeight = nullptr);
inline Mix Target(const Table& t, float px, float pz, int userRain, int userFog, bool zones, double tSec,
                  const Params& p, int* inZone = nullptr, float* inWeight = nullptr)
{
    return TargetMix(t, px, pz, StateMix(userRain, userFog, p), zones, tSec, p, inZone, inWeight);
}
inline Mix TargetMix(const Table& t, float px, float pz, const Mix& user, bool zones, double tSec,
                     const Params& p, int* inZone, float* inWeight)
{
    Mix m = user;
    int best = -1; float bestW = 0.0f;
    if (zones && Fin(px) && Fin(pz))
    {
        for (int i = 0; i < t.nZones; ++i)
        {
            const Zone& z = t.zones[i];
            const float w = ZoneWeight(z, px, pz);
            if (w <= 0.0f) continue;
            const float wet = Shower(tSec, z.duty, p.shower, NameSeed(z.name));
            const float weird = WeirdAt(t, z, px, pz);
            Mix zm = StateMix(z.rain, z.fog, p, wet, weird, z.hasTint ? z.tint : nullptr, z.overcast);
            zm = MaxMix(zm, user);                                  // never less than what the player set for everywhere
            m = LerpMix(m, zm, w);
            if (w > bestW) { bestW = w; best = i; }
        }
    }
    if (inZone) *inZone = best;
    if (inWeight) *inWeight = bestW;
    return m;
}

// ---------------------------------------------------------------------------------------------
//  Smoothing: the mix in force moves a share of the way to its target each frame (time constant
//  tau = [weather] blend / 3, so ~95 % of a change has happened after `blend` seconds). The step of
//  one frame is bounded by dt <= 0.1 s, so a hitch or a teleport cannot make it jump; and a value
//  whose target is 0 lands on exactly 0 once it is below `eps` (off = truly untouched).
// ---------------------------------------------------------------------------------------------
inline void Step(Mix& cur, const Mix& tgt, float dt, float blend, float eps = 5.0e-4f)
{
    if (!(dt > 0.0f)) return;
    if (dt > 0.1f) dt = 0.1f;
    const float tau = (blend > 0.05f ? blend : 0.05f) / 3.0f;
    const float a = 1.0f - std::exp(-dt / tau);
    for (int i = 0; i < kMixFloats; ++i)
    {
        float& c = F(cur)[i];
        const float g = F(tgt)[i];
        if (!Fin(c)) c = 0.0f;
        c += (g - c) * a;
    }
    // (the fog block lands together: its premultiplied numbers only mean something with their weight)
    if (tgt.rain == 0.0f && cur.rain < eps) cur.rain = 0.0f;
    if (tgt.gust == 0.0f && cur.gust < eps) cur.gust = 0.0f;
    if (tgt.overcast == 0.0f && cur.overcast < eps) cur.overcast = 0.0f;
    if (tgt.mist == 0.0f && cur.mist < eps) cur.mist = 0.0f;
    if (tgt.fogw == 0.0f && cur.fogw < eps)
    {
        cur.fogw = cur.visW = cur.lowW = cur.thickW = 0.0f;
        cur.tintW[0] = cur.tintW[1] = cur.tintW[2] = 0.0f;
    }
}

// ---------------------------------------------------------------------------------------------
//  Hysteresis: a switch that goes on above `onAt` and off only below `offAt` (< onAt), so a value
//  hovering at a threshold cannot flick an engine stage on and off every frame.
// ---------------------------------------------------------------------------------------------
struct Latch
{
    bool on = false;
    bool Update(float v, float onAt, float offAt)
    {
        if (!on && v >= onAt) on = true;
        else if (on && v < offAt) on = false;
        return on;
    }
};

// ---------------------------------------------------------------------------------------------
//  The engine's exponential height fog (PostEffect settings, Notes\53 section 2): what the level
//  has, and what we hand back.
//      transmittance(d, h) = 2 ^ -( d * densityAtCamera * (1 - 2^(-k h)) / (k h)  +  nearDensity * min(d, nearDist) )
//  The second term is a height-blind veil that stops growing at nearDist: OUR veil is that term,
//  nearDist = the mix's sight distance and nearDensity x nearDist = kVeilDepth (2^-3.32 = 10 % left).
// ---------------------------------------------------------------------------------------------
static const float kVeilDepth = 3.32f;
static const float kMinDensity = 0.0012f;       // the engine drops the stage below 0.001 (0x14037BD0B)

struct LevelFog { bool on; float dens, thick, nearDist, nearDens; float albedo[3]; };
struct FogOut   { bool write; bool on; float dens, thick, nearDist, nearDens; float albedo[3]; };

// Is this what an exponential-fog block looks like? (weather.h refuses to write into anything else)
inline bool LevelFogSane(const LevelFog& l)
{
    if (!Fin(l.dens) || !Fin(l.thick) || !Fin(l.nearDist) || !Fin(l.nearDens)) return false;
    if (l.dens < 0.0f || l.dens > 1000.0f || l.thick < 0.0f || l.thick > 1.0e6f) return false;
    if (l.nearDist < 0.0f || l.nearDist > 1.0e7f || l.nearDens < 0.0f || l.nearDens > 1000.0f) return false;
    for (int k = 0; k < 3; ++k) if (!Fin(l.albedo[k]) || l.albedo[k] < 0.0f || l.albedo[k] > 64.0f) return false;
    return true;
}

// night01: 0 day .. 1 night. `latch` keeps the on / off decision steady (Latch above).
inline FogOut MapFog(const LevelFog& lv, const Mix& m, float night01, const Params& p, Latch& latch)
{
    FogOut o = {};
    const float keep = 1.0f - Clamp01(night01) * (1.0f - Clamp01(p.nightFog));
    const float f = Clamp01(m.fogw) * keep;
    if (!p.engineFog || !latch.Update(f, 0.02f, 0.004f) || !LevelFogSane(lv)) return o;      // nothing written: the level's own
    const float vis = Clampf(Vis(m), 8.0f, 5000.0f), low = Clampf(Low(m), 0.0f, 10.0f), thick = Clampf(Thick(m), 0.5f, 2000.0f);
    float tint[3]; Tint(m, tint);
    // what the level has (a level without this fog: none of it, and our shape so that only the amount fades in)
    const float d0 = lv.on ? lv.dens : 0.0f, t0 = lv.on && lv.thick >= 0.001f ? lv.thick : thick;
    const float n0 = lv.on ? lv.nearDist : vis, depth0 = lv.on ? lv.nearDens * lv.nearDist : 0.0f;
    o.write = true; o.on = true;
    o.dens = Lerp(d0, d0 > low ? d0 : low, f);                      // never thinner than the level's
    if (o.dens < kMinDensity) o.dens = kMinDensity;
    o.thick = Lerp(t0, thick, f);
    o.nearDist = Lerp(n0, vis, f);
    if (o.nearDist < 1.0f) o.nearDist = 1.0f;
    const float depth = Lerp(depth0, depth0 > kVeilDepth ? depth0 : kVeilDepth, f);
    o.nearDens = depth / o.nearDist;
    for (int k = 0; k < 3; ++k) o.albedo[k] = Lerp(lv.on ? lv.albedo[k] : tint[k], tint[k], 0.85f * f);
    return o;
}

// The engine's own rain stage (experimental): its enable byte and the three amounts the stage is
// gated on (Density, Foreground Alpha, Splash Alpha; 0x14037BD56..0x14037BDA7).
struct RainOut { bool write; bool on; float density, fgAlpha, splashAlpha, speed, dir[3]; };
inline RainOut MapRain(const Mix& m, const Params& p, Latch& latch)
{
    RainOut o = {};
    if (!p.engineRain) { latch.on = false; return o; }
    o.write = true;
    o.on = latch.Update(m.rain, 0.03f, 0.008f);
    const float r = Clamp01(m.rain), g = Clamp01(m.gust);
    o.density = Lerp(0.15f, 1.0f, r);
    o.fgAlpha = Lerp(0.03f, 0.09f, r);
    o.splashAlpha = Lerp(0.15f, 0.5f, r);
    o.speed = Lerp(8.0f, 13.0f, g);
    const float sx = Lerp(0.12f, 0.45f, g);
    const float len = std::sqrt(sx * sx + 1.0f + sx * sx * 0.25f);
    o.dir[0] = sx / len; o.dir[1] = -1.0f / len; o.dir[2] = -0.5f * sx / len;
    return o;
}

// What the shaders get (IniParams 4.y / 4.z / 4.w / 7.y): every one 0 = the patched shaders return
// their input untouched.
struct ShaderOut { float rain, overcast, mist, gust; };
inline ShaderOut MapShader(const Mix& m, const Params& p)
{
    ShaderOut s;
    s.rain = p.screenRain ? Clamp01(m.rain) : 0.0f;
    s.overcast = Clamp01(m.overcast * (p.overcast < 0.0f ? 0.0f : p.overcast));
    s.mist = Clamp01(m.mist * (p.mist < 0.0f ? 0.0f : p.mist));
    s.gust = p.screenRain ? Clamp01(m.gust) : 0.0f;
    return s;
}
inline ShaderOut MaxShader(const ShaderOut& a, const ShaderOut& b)
{
    ShaderOut s;
    s.rain = a.rain > b.rain ? a.rain : b.rain; s.overcast = a.overcast > b.overcast ? a.overcast : b.overcast;
    s.mist = a.mist > b.mist ? a.mist : b.mist; s.gust = a.gust > b.gust ? a.gust : b.gust;
    return s;
}

} // namespace wxm
