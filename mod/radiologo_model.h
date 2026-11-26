// ============================================================================
//  radiologo_model.h - the radio logo overlay, the pure part (radiologo1, 2026-11-19, Notes 35).
//  No Windows, no game memory: test\radiologo_harness.cpp drives every function here.
//
//  The overlay: when the radio changes station (or comes on as you get into a vehicle) the station's logo shows in
//  a corner for `seconds`, slides in for 0.35 s and fades out over the last second. The timing runs on the GPU
//  (ShaderFixes\radiologo_fx.hlsl) from a start time on the 0.x clock, so the DLL writes only when something
//  changes; Alpha() / Slide() below are the shader's formulas, kept here so the harness can check them.
//
//  The tiles: Mods\RadioLogo\logos.txt (LCU-Modding\RadioLogo\make_logos.py) lists "index<TAB>station id";
//  tile 0 = radio off, 1 = the generic logo (a station the atlas does not know yet).
// ============================================================================
#ifndef LCU_RADIOLOGO_MODEL_H
#define LCU_RADIOLOGO_MODEL_H

#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace rlm {

static const int   kTileOff = 0, kTileGeneric = 1;
static const float kIn = 0.35f, kOut = 1.0f, kSlide = 0.10f;   // = RL_IN / RL_OUT / RL_SLIDE in the hlsl
static const float kSentinel17w = 64197.375f;                   // radiologo_fx.ini w17

enum Kind { EV_NONE = 0, EV_STEP = 1, EV_ENTER = 2 };

// ---- the tile table ------------------------------------------------------------------------------------
struct Tiles
{
    std::vector<std::string> id;      // id[index]; "" for a gap
    bool loaded = false;
};

// "index<TAB>id" lines; '#' comments, blank lines and malformed lines are skipped. Returns the tiles read.
inline int ParseTiles(const char* text, size_t n, Tiles& t)
{
    t = Tiles();
    int got = 0;
    size_t pos = 0;
    while (pos < n)
    {
        size_t e = pos;
        while (e < n && text[e] != '\n') ++e;
        std::string line(text + pos, e - pos);
        pos = e + 1;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        const size_t tab = line.find('\t');
        if (tab == std::string::npos || tab == 0) continue;
        char* end = nullptr;
        const long idx = std::strtol(line.c_str(), &end, 10);
        if (end != line.c_str() + tab || idx < 0 || idx > 255) continue;
        std::string id = line.substr(tab + 1);
        while (!id.empty() && (id[0] == ' ' || id[0] == '\t')) id.erase(0, 1);
        if (id.empty()) continue;
        if (static_cast<size_t>(idx) >= t.id.size()) t.id.resize(static_cast<size_t>(idx) + 1);
        t.id[static_cast<size_t>(idx)] = id;
        ++got;
    }
    t.loaded = got > 0;
    return got;
}

// The tile of a station id: its own, else the generic one (a station added after the atlas was built);
// an empty id = the radio is off. Without a table: the generic tile (never a wrong station's logo).
inline int TileFor(const Tiles& t, const std::string& stationId)
{
    if (stationId.empty())
    {
        for (size_t i = 0; i < t.id.size(); ++i) if (t.id[i] == "_off") return static_cast<int>(i);
        return kTileOff;
    }
    for (size_t i = 0; i < t.id.size(); ++i) if (t.id[i] == stationId) return static_cast<int>(i);
    for (size_t i = 0; i < t.id.size(); ++i) if (t.id[i] == "_generic") return static_cast<int>(i);
    return kTileGeneric;
}

// ---- the trigger on the radio thread ------------------------------------------------------------------
// The radio coming on as the player gets in: the radio's own listen state rises after at least minSilent seconds without it
// (a short drop - a loading hitch, a jump out and back in - shows nothing). A station step this frame already
// shows the logo, so the rise adds nothing then.
struct EnterWatch
{
    bool   was = false;
    double offSince = -1e9;          // when it last stopped (seconds)
};
inline bool EnterRise(EnterWatch& w, bool listenNow, double now, double minSilent, bool steppedNow)
{
    bool show = false;
    if (listenNow && !w.was) show = !steppedNow && (now - w.offSince) >= minSilent;
    if (!listenNow && w.was) w.offSince = now;
    w.was = listenNow;
    return show;
}

// ---- the shader's timing (radiologo_fx.hlsl VS) --------------------------------------------------------
inline float Sat(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
inline float ClampDur(float d) { return d < 1.5f ? 1.5f : (d > 30.0f ? 30.0f : d); }
inline float Ease(float t) { const float f = Sat(t / kIn); return 1.0f - (1.0f - f) * (1.0f - f) * (1.0f - f); }
inline bool  Visible(float t, float dur) { return t >= 0.0f && t < ClampDur(dur); }
inline float Alpha(float t, float dur)
{
    if (!Visible(t, dur)) return 0.0f;
    return Ease(t) * Sat((ClampDur(dur) - t) / kOut);
}
inline float Slide(float t) { return (1.0f - Ease(t)) * kSlide; }

// ---- what the DLL writes (IniParams 16.xyzw, 17.xy) ----------------------------------------------------
struct Block
{
    float start = 0.0f, tile = -1.0f, seconds = 7.0f, corner = 0.0f;   // 16.xyzw
    float scale = 1.0f, hide = 0.0f;                                  // 17.xy
};
inline bool Same(const Block& a, const Block& b)
{
    return a.start == b.start && a.tile == b.tile && a.seconds == b.seconds && a.corner == b.corner &&
           a.scale == b.scale && a.hide == b.hide;
}

struct Settings
{
    int   on = 1;           // [radio] logo
    float seconds = 7.0f;   // logo_seconds (3..20)
    int   corner = 0;       // logo_corner 0..4
    float scale = 1.0f;     // logo_scale (0.4..2.5)
    int   onEnter = 1;      // logo_on_enter
};

inline int CornerFromText(const char* s, int def)
{
    if (!s || !*s) return def;
    std::string v;
    for (const char* p = s; *p; ++p) if (*p != ' ' && *p != '-' && *p != '_') v += static_cast<char>(*p >= 'A' && *p <= 'Z' ? *p + 32 : *p);
    if (v == "bottomright" || v == "br") return 0;
    if (v == "topright" || v == "tr") return 1;
    if (v == "bottomcentre" || v == "bottomcenter" || v == "bottom" || v == "bc") return 2;
    if (v == "topcentre" || v == "topcenter" || v == "top" || v == "tc") return 3;
    if (v == "rightmiddle" || v == "right" || v == "middleright" || v == "rm") return 4;
    char* end = nullptr;
    const long n = std::strtol(v.c_str(), &end, 10);
    if (end && *end == 0 && !v.empty() && n >= 0 && n <= 4) return static_cast<int>(n);
    return def;
}
inline Settings Sanitize(Settings s)
{
    s.on = s.on ? 1 : 0;
    s.onEnter = s.onEnter ? 1 : 0;
    if (!(s.seconds >= 3.0f)) s.seconds = s.seconds < 3.0f ? 3.0f : 7.0f;
    if (s.seconds > 20.0f) s.seconds = 20.0f;
    if (s.corner < 0 || s.corner > 4) s.corner = 0;
    if (!(s.scale >= 0.4f)) s.scale = s.scale < 0.4f ? 0.4f : 1.0f;
    if (s.scale > 2.5f) s.scale = 2.5f;
    return s;
}

// One step of the game-thread state. `cur` is what was last written; an event (seq moved) starts a new show
// unless the settings say no; hideHard (a cutscene, the Backrooms, the radio switched off in the ini) ends the
// show; hideSoft (the pause menu) only hides it while it lasts; a show past its time is cleared (so 3Dmigoto's
// [Present] stops running the pass). Returns the block to write.
struct Event { bool fresh = false; int kind = EV_NONE; int tile = -1; };
inline Block Step(Block cur, const Event& ev, const Settings& s, float clock, bool hideHard, bool hideSoft)
{
    cur.seconds = s.seconds; cur.corner = static_cast<float>(s.corner); cur.scale = s.scale;
    if (ev.fresh && s.on && !hideHard && ev.tile >= 0 && (ev.kind == EV_STEP || (ev.kind == EV_ENTER && s.onEnter)))
    {
        cur.start = clock; cur.tile = static_cast<float>(ev.tile);
    }
    if (!s.on || hideHard) cur.tile = -1.0f;
    if (cur.tile >= 0.0f && !Visible(clock - cur.start, cur.seconds) && clock - cur.start > 0.0f) cur.tile = -1.0f;
    if (cur.tile >= 0.0f && clock < cur.start - 1.0f) cur.tile = -1.0f;   // the clock went back (F10 / $tod): drop it
    cur.hide = hideSoft ? 1.0f : 0.0f;
    return cur;
}

} // namespace rlm

#endif // LCU_RADIOLOGO_MODEL_H
