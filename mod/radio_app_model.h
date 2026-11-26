// =============================================================================================
//  radio_app_model.h - a station playing the listener's music APP (Spotify, or anything in Windows'
//  media controls), the pure half: when the app plays, when it is paused for the station's own
//  items, its loudness, and what the station's plan does around it. No Windows: tested by
//  test\radio_harness.cpp against a simulated app; driven by radio.h with radio_app.h (the OS side).
//
//  The rules (spec v1 section 6; R0-R9 below):
//    - the station's own items (the DJ, IDs, ads, news) never air while the app is audible: the app
//      is paused first - silenced if it won't pause - and never ducked under talk
//    - the app plays during the station's music slots (radio_model.h "ext" events); a slot is held
//      open while the app's track plays and ends at the app's track boundary
//    - a track the listener skips within 25 s never brings the DJ in (flicking through)
//    - the listener's own pause stands: the station talks; their play brings the music back at once
//    - the service's own ads are left alone: no pause, no seek, no fade, no duck, never muted (the app
//      plays them at its own level), no DJ over them
//    - unfocused (alt-tab): hands off - no commands, its own volume back
//    - the bound app stays the bound app while the radio has it (no rebinding mid-take)
// =============================================================================================
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>
#include <utility>
#include "radio_model.h"

namespace radio {

// ---- the app's tracks ----------------------------------------------------------------------------
// Key = title + '\x1f' + artist. An empty key (an app between tracks) changes nothing; the first key
// is a baseline, not a boundary.
struct TrackWatch
{
    std::string key;
    double      startT = 0.0;
};

inline bool ObserveTrack(TrackWatch& tw, const std::string& key, double t, double& played)
{
    played = 0.0;
    if (key.empty() || key == tw.key) return false;
    const bool first = tw.key.empty();
    played = t - tw.startT;
    tw.key = key; tw.startT = t;
    return !first;
}

inline bool ContainsNoCase(const std::string& hay, const char* needle)
{
    const size_t n = std::strlen(needle);
    if (!n || hay.size() < n) return false;
    for (size_t i = 0; i + n <= hay.size(); ++i)
    {
        size_t k = 0;
        for (; k < n; ++k)
        {
            char a = hay[i + k], b = needle[k];
            if (a >= 'A' && a <= 'Z') a = static_cast<char>(a + 32);
            if (b >= 'A' && b <= 'Z') b = static_cast<char>(b + 32);
            if (a != b) break;
        }
        if (k == n) return true;
    }
    return false;
}

// The service's own ad (Spotify Free), as far as the media controls show it. [U] how Spotify's ads
// look in SMTC: while the radio plays Spotify every track is logged so a listener can confirm.
// radio.h asks only once the app's track details have been read since it was bound (blank details
// that were never read are not an ad), and only while it plays.
inline bool LooksLikeAd(const std::string& aumid, const std::string& title, const std::string& artist, double durS)
{
    if (!ContainsNoCase(aumid, "spotify")) return false;
    return title.empty() || artist.empty() || artist == "Spotify" || title == "Advertisement" ||
           (durS > 0.0 && durS <= 35.0 && artist.empty());
}

inline bool EqualsNoCase(const std::string& a, const char* b)
{
    const size_t n = std::strlen(b);
    return a.size() == n && ContainsNoCase(a, b);
}

// Which media session the radio binds (spec v1 section 7.2), from every session's app id (in the
// media controls' order) and the media keys' current one:
//   service_app = any  -> the media keys' one
//   auto (or empty)    -> the first whose id contains "spotify", else the media keys' one
//   anything else      -> the first whose id contains that text (else none)
// `keep`: the radio has the bound app right now - it stays bound while its session exists, so a
// browser tab or another player starting up never takes over in the middle of a take.
inline std::string PickAumid(const std::vector<std::string>& ids, const std::string& current, const std::string& want,
                             const std::string& bound, bool keep)
{
    if (keep && !bound.empty())
        for (const std::string& id : ids) if (id == bound) return bound;
    if (EqualsNoCase(want, "any")) return current;
    const bool autoPick = want.empty() || EqualsNoCase(want, "auto");
    const std::string needle = autoPick ? std::string("spotify") : want;
    for (const std::string& id : ids) if (ContainsNoCase(id, needle.c_str())) return id;
    return autoPick ? current : std::string();
}

// The radio's own audio on a station: listen as before, and on a station playing the app only while
// the game has focus (the listener in their app hears their app, not the DJ).
inline bool ServiceListen(bool listenBase, uint8_t tunedSource, bool focus)
{
    return listenBase && (tunedSource != SRC_APP || focus);
}

// ---- the restore ledger (pure part): the app volumes the radio holds -----------------------------
// While the radio holds an app's volume its own is listed here; radio_app.h writes Text() to a file
// beside the DLL, so a crash leaves a list to give back on the next start.
struct VolumeLedger
{
    std::vector<std::pair<std::string, float>> e;      // name (exe or package family, UTF-8), its own volume

    void Add(const std::string& name, float v)
    {
        for (auto& x : e) if (x.first == name) { x.second = v; return; }
        e.push_back(std::make_pair(name, v));
    }
    void Remove(const std::string& name)
    {
        for (size_t i = 0; i < e.size(); ++i) if (e[i].first == name) { e.erase(e.begin() + static_cast<long>(i)); return; }
    }
    bool Empty() const { return e.empty(); }
    std::string Text() const
    {
        std::string s = "# lcudrive radio: app volumes to give back if the game closed while the radio held them\r\n";
        char b[64];
        for (const auto& x : e) { std::snprintf(b, sizeof(b), "\t%.4f\r\n", x.second); s += x.first; s += b; }
        return s;
    }
    void Parse(const std::string& text)
    {
        e.clear();
        size_t p = 0;
        while (p < text.size())
        {
            size_t q = text.find('\n', p);
            if (q == std::string::npos) q = text.size();
            std::string line = text.substr(p, q - p);
            p = q + 1;
            while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
            if (line.empty() || line[0] == '#') continue;
            const size_t tab = line.find('\t');
            if (tab == std::string::npos || tab == 0) continue;
            const float v = static_cast<float>(std::atof(line.c_str() + tab + 1));
            if (!(v >= 0.0f && v <= 1.0f)) continue;
            Add(line.substr(0, tab), v);
        }
    }
};

// ---- the controller ------------------------------------------------------------------------------
enum AppCmd : uint8_t { APP_NONE, APP_PLAY, APP_PAUSE };
enum AppPhase : uint8_t { AP_IDLE, AP_PLAYING, AP_PAUSING, AP_BREAK, AP_USERPAUSED, AP_RELEASING };
static const char* const kAppPhaseNames[6] = { "idle", "playing", "pausing", "break", "you paused", "letting go" };

struct AppIn
{
    float  dt = 0.0f;
    double now = 0.0;              // seconds, real time
    bool   focus = true;           // the game has focus
    bool   want = false;           // in a vehicle, unpaused, no cutscene, radio on, the tuned station's
                                   // source is the app, the app is bound, the service isn't off
    int    tuned = -1;             // the tuned station (a change while wanted = APP -> APP)
    bool   slot = false;           // the tuned station airs an app slot (q[0].ext)
    bool   talkFollows = false;    // radio_model.h TalkFollows
    bool   playing = false;        // the app says it plays
    bool   isAd = false;           // LooksLikeAd
    bool   seekable = false;       // it takes a playback position
    bool   timeline = false;       // its timeline is usable
    double remaining = 0.0;        // ... seconds left in its track
    std::string key;               // its track (title \x1f artist; "" = not known)
    float  level = 1.0f;           // bus x service_volume x the game's Volume option x the game's duck
    bool   volctl = true;          // service_volume_control AND the app's audio session was found (the
                                   // radio can really set its volume); false: play / pause only
    float  lead = 0.6f;            // service_pause_lead (0 = no pre-end pause)
    float  maxTrackS = 900.0f;     // service_max_track_min x 60 (0 = never)
    bool   final = false;          // R8: the radio stops for good (unhook, shutdown, the game window gone)
};

struct AppOut
{
    uint8_t cmd = APP_NONE;
    bool  seek0 = false;           // back to the start of the track (before the PLAY)
    bool  hold = false;            // radio_model.h Stretch: the slot stays open
    bool  cut = false;             // CutAiring: the slot ends now
    bool  yield = false;           // YieldToMusic: the listener's play stops the talk
    bool  talkMode = false;        // the listener paused the app: the station runs as talk only
    bool  held = false;            // the app's volume is the radio's (radio_app.h holds its own)
    float volume = 1.0f;           // x its own, while held
    bool  release = false;         // give it its own volume back (once)
    bool  silenced = false;        // ... held at 0: it would not pause
    const char* note = nullptr;    // what happened, for the log
};

struct AppCtl
{
    uint8_t phase = AP_IDLE;
    int     tuned = -1;
    std::string key;               // the app's track
    double  keyT = 0.0;            // when it began (or the radio took it)
    double  adT = 0.0;             // seconds of the service's ads since keyT (not counted as played)
    double  slotT = 0.0;           // the airing slot began
    double  playSentT = -1e9;      // our last PLAY
    int     playSends = 0;
    bool    playConfirmed = false; // the app played since our last PLAY
    double  pauseSentT = -1e9;     // our last PAUSE
    double  pauseT = 0.0;          // seconds since our PAUSE (pausing, or a break we started)
    int     pauses = 0;
    bool    wePaused = false;      // the app is (being) paused by us
    bool    breakPausing = false;  // a break that started with the app still playing: pause pending
    double  breakT = 0.0;          // the break began
    float   notPlayT = 0.0f;       // seconds not playing in a slot
    float   fade = 0.0f;           // our ramp: 0..1
    bool    held = false;
    bool    everTaken = false;
    bool    prePlaying = false;    // the app played when the radio first took it this session
    bool    seekPending = false;   // rewind the new track before the next PLAY
    bool    silenced = false;
    bool    unfocused = false;
    double  relT = 0.0;            // releasing: seconds since the fade ended
    int     relPauses = 0;
    bool    relNoFade = false;
};

// The 25 s rule: seconds of the current track heard (the service's ads not counted).
inline double PlayedS(const AppCtl& c, double now) { return now - c.keyT - c.adT; }

inline void AppRebase(AppCtl& c, double now) { c.keyT = now; c.adT = 0.0; }
inline void AppPause(AppCtl& c, AppOut& o, double now) { o.cmd = APP_PAUSE; c.pauseSentT = now; c.wePaused = true; }
inline void AppPlay(AppCtl& c, AppOut& o, double now)  { o.cmd = APP_PLAY; c.playSentT = now; c.wePaused = false; c.playConfirmed = false; }

// R1: the radio takes the app (tuned in, back in the car, unpaused, a cutscene over, the source
// switched to the app) - or the tuned app station changed (APP -> APP keeps the hold).
inline void AppTake(AppCtl& c, const AppIn& in, AppOut& o)
{
    c.tuned = in.tuned;
    if (!c.everTaken) { c.everTaken = true; c.prePlaying = in.playing; }
    c.held = in.volctl;
    c.silenced = false; c.seekPending = false; c.notPlayT = 0.0f; c.breakPausing = false;
    c.playConfirmed = in.playing;
    AppRebase(c, in.now);
    if (in.slot)
    {
        c.slotT = in.now;
        c.phase = AP_PLAYING;
        if (!in.playing) { AppPlay(c, o, in.now); c.playSends = 1; o.note = "the radio takes your music app: play"; }
        else c.wePaused = false;                       // (a pause of ours still on its way: undone in PLAYING)
        return;
    }
    // the station's own item airs
    if (in.playing)
    {
        if (in.isAd || !in.volctl)
        {
            // an ad can't be muted, and without volume control the app can't be hidden while it
            // pauses: the music stays, the talk goes
            o.yield = true; c.slotT = in.now; c.phase = AP_PLAYING; c.wePaused = false;
            o.note = "your music app plays on - the DJ stops";
            return;
        }
        AppPause(c, o, in.now); c.breakPausing = true; c.pauseT = 0.0; c.pauses = 1;
        c.fade = 0.0f;                                 // silent at once: the DJ is on
        c.breakT = in.now; c.phase = AP_BREAK;
        o.note = "the DJ is on - your music app pauses";
        return;
    }
    c.wePaused = false; c.breakT = in.now;
    c.phase = AP_BREAK;
}

inline AppOut AppStep(AppCtl& c, const AppIn& in)
{
    AppOut o;
    const float dt = in.dt > 0.0f ? in.dt : 0.0f;

    // ---- R8: the radio stops for good: its own volume back --------------------------------------
    // An app that played before the radio first took it is left as it is; one the radio found
    // paused, and plays now, is paused again. Never a PLAY, and never any command while the game is
    // unfocused (the listener is in their app: hands off, I4).
    if (in.final)
    {
        if (c.everTaken && c.phase != AP_IDLE && !c.unfocused && !c.prePlaying && in.playing) o.cmd = APP_PAUSE;
        o.release = c.held;
        c.held = false; c.phase = AP_IDLE; c.wePaused = false; c.silenced = false;
        o.note = "the radio stops - your music app as it was";
        return o;
    }

    // ---- the app's track ------------------------------------------------------------------------------
    bool newKey = false;
    if (!in.key.empty() && in.key != c.key)
    {
        newKey = !c.key.empty();
        c.key = in.key;
    }
    const bool playingPhase = c.phase == AP_PLAYING || c.phase == AP_PAUSING;
    if (in.isAd && playingPhase && in.focus) c.adT += dt;

    // ---- R0: unfocused - hands off --------------------------------------------------------------------
    if (!in.focus)
    {
        if (c.held) { o.release = true; c.held = false; }
        if (newKey && !in.isAd) AppRebase(c, in.now);
        c.unfocused = true;
        o.hold = playingPhase;                         // frozen: the slot waits for us
        o.talkMode = c.phase == AP_USERPAUSED;
        return o;
    }
    if (c.unfocused)
    {
        c.unfocused = false;
        if (c.phase != AP_IDLE && c.phase != AP_RELEASING) c.held = in.volctl;   // re-held; no PLAY
    }

    // ---- R1: take ---------------------------------------------------------------------------------------
    bool took = false;
    if (in.want && (c.phase == AP_IDLE || c.phase == AP_RELEASING || in.tuned != c.tuned))
    {
        AppTake(c, in, o);
        took = true;
    }

    // ---- R7: release ------------------------------------------------------------------------------------
    if (!in.want && c.phase != AP_IDLE && c.phase != AP_RELEASING)
    {
        c.phase = AP_RELEASING; c.relT = 0.0; c.relPauses = 0; c.relNoFade = in.isAd;
        if (in.isAd) c.fade = 0.0f;                    // R6: no fade over an ad - paused at once
    }

    switch (took ? static_cast<uint8_t>(AP_IDLE) : c.phase)
    {
    case AP_PLAYING:
    {
        o.hold = true;
        if (!in.slot)
        {
            // the station's own item airs although the app had the slot (the plan changed under us)
            o.hold = false;
            if (in.playing && !in.isAd && in.volctl)
            {
                AppPause(c, o, in.now); c.breakPausing = true; c.pauseT = 0.0; c.pauses = 1;
                c.fade = 0.0f; c.breakT = in.now; c.phase = AP_BREAK;
            }
            else if (in.playing) { o.yield = true; c.slotT = in.now; }
            else { c.breakT = in.now; c.phase = AP_BREAK; }
            break;
        }
        if (in.isAd) { c.notPlayT = 0.0f; break; }                 // R6: nothing decided during an ad
        if (newKey)
        {
            if (PlayedS(c, in.now) < 25.0) AppRebase(c, in.now);  // flicking through: no talk
            else if (in.talkFollows)
            {
                AppPause(c, o, in.now); c.pauses = 1; c.pauseT = 0.0;
                c.seekPending = in.seekable; c.phase = AP_PAUSING;
                AppRebase(c, in.now);
                o.note = in.seekable ? "paused for the DJ (new track, rewound)" : "paused for the DJ (new track)";
                break;
            }
            else
            {
                o.cut = true; o.hold = false; c.slotT = in.now; AppRebase(c, in.now);
                o.note = "plays on";
            }
        }
        else if (in.timeline && in.lead > 0.0f && in.talkFollows && PlayedS(c, in.now) >= 25.0 &&
                 in.remaining <= in.lead && in.remaining > 0.0)
        {
            AppPause(c, o, in.now); c.pauses = 1; c.pauseT = 0.0;
            c.seekPending = false; c.phase = AP_PAUSING;
            o.note = "paused for the DJ (the track's last moment left)";
            break;
        }
        else if (in.maxTrackS > 0.0f && in.now - c.slotT >= in.maxTrackS && in.talkFollows)
        {
            AppPause(c, o, in.now); c.pauses = 1; c.pauseT = 0.0;
            c.seekPending = false; c.phase = AP_PAUSING;
            o.note = "paused for the DJ (a long one)";
            break;
        }
        if (!in.playing)
        {
            c.notPlayT += dt;
            if (in.now - c.pauseSentT < 2.0 && c.pauseSentT > c.playSentT)
            {
                AppPlay(c, o, in.now); c.playSends = 1;            // a pause of ours arrived late: undone
            }
            else if (!c.playConfirmed && in.now - c.playSentT < 2.0)
            {
                // our PLAY hasn't taken yet: once more after 1 s, then the 2 s grace runs out
                if (c.playSends == 1 && in.now - c.playSentT >= 1.0) { o.cmd = APP_PLAY; c.playSends = 2; }
            }
            else if (c.notPlayT >= 1.5f)
            {
                c.phase = AP_USERPAUSED; o.talkMode = true; o.hold = false;
                o.note = "you paused it - the station talks";
            }
        }
        else { c.notPlayT = 0.0f; c.playConfirmed = true; }
        break;
    }
    case AP_PAUSING:
    {
        o.hold = true;
        if (!in.playing) { o.cut = true; o.hold = false; c.breakT = in.now; c.breakPausing = false; c.phase = AP_BREAK; break; }
        if (in.isAd) { c.pauseT = 0.0; break; }                    // R6
        if (newKey) { c.seekPending = in.seekable; AppRebase(c, in.now); }
        c.pauseT += dt;
        if (c.pauses == 1 && c.pauseT >= 0.4) { AppPause(c, o, in.now); c.pauses = 2; }
        if (c.pauseT >= 0.8 && in.volctl)
        {
            c.silenced = true; o.cut = true; o.hold = false; c.breakT = in.now; c.breakPausing = false; c.phase = AP_BREAK;
            o.note = "would not pause - silenced for the DJ";
        }
        else if (c.pauseT >= 2.0 && !in.volctl)
        {
            // no volume control and it won't pause: the music wins (the DJ never talks over it)
            c.phase = AP_PLAYING; c.wePaused = false; c.slotT = in.now; AppRebase(c, in.now);
            o.note = "would not pause - it plays on (no volume control)";
        }
        break;
    }
    case AP_BREAK:
    {
        if (in.playing && in.isAd)
        {
            // R6: the service's own ad came on while the station talks (a pause of ours still on its
            // way, or one it ignored): it is never muted or talked over - the DJ stops
            o.yield = true; c.wePaused = false; c.silenced = false; c.breakPausing = false; c.seekPending = false;
            c.phase = AP_PLAYING; c.slotT = in.now; c.notPlayT = 0.0f; AppRebase(c, in.now);
            o.note = "your music app plays its own ad - the DJ stops";
            break;
        }
        if (c.breakPausing)
        {
            if (!in.playing) c.breakPausing = false;
            else if (!in.isAd)
            {
                c.pauseT += dt;
                if (c.pauses == 1 && c.pauseT >= 0.4) { AppPause(c, o, in.now); c.pauses = 2; }
                if (c.pauseT >= 0.8) { c.silenced = true; c.breakPausing = false; o.note = "would not pause - silenced for the DJ"; }
            }
        }
        if (in.slot)
        {
            // the break is over: the music comes back
            if (c.seekPending && in.seekable) o.seek0 = true;
            if (!in.playing) { AppPlay(c, o, in.now); c.playSends = 1; }
            c.silenced = false; c.wePaused = false; c.seekPending = false; c.breakPausing = false;
            c.phase = AP_PLAYING; c.slotT = in.now; c.notPlayT = 0.0f; AppRebase(c, in.now);
            break;
        }
        if (in.playing && !c.silenced && !c.breakPausing && in.now - c.breakT >= 0.3 && in.now - c.pauseSentT >= 0.3)
        {
            o.yield = true; c.wePaused = false; c.phase = AP_PLAYING; c.slotT = in.now; c.notPlayT = 0.0f;
            AppRebase(c, in.now);
            o.note = "you pressed play - the DJ stops";
        }
        break;
    }
    case AP_USERPAUSED:
    {
        o.talkMode = true;
        if (in.playing)
        {
            o.talkMode = false; o.yield = true; c.wePaused = false;
            c.phase = AP_PLAYING; c.slotT = in.now; c.notPlayT = 0.0f; AppRebase(c, in.now);
            o.note = "you pressed play - the DJ stops";
        }
        break;
    }
    case AP_RELEASING:
    {
        if (!c.relNoFade) c.fade = std::fmax(0.0f, c.fade - dt / 0.2f);
        if (c.fade <= 0.0f)
        {
            c.relT += dt;
            if (in.playing && c.relPauses == 0) { AppPause(c, o, in.now); c.relPauses = 1; c.relT = 0.0; }
            else if (in.playing && c.relPauses == 1 && c.relT >= 1.0) { AppPause(c, o, in.now); c.relPauses = 2; }
            if (!in.playing || c.relT >= 2.0)
            {
                if (c.held) o.release = true;
                c.held = false; c.phase = AP_IDLE; c.wePaused = false; c.silenced = false;
                o.note = "the radio lets go of your music app";
            }
        }
        break;
    }
    default: break;
    }

    // ---- the volume is the radio's while it has the app and can really set it (the app's audio session
    // found; it can show up or go away mid-take) -------------------------------------------------------
    if (c.phase != AP_IDLE && c.phase != AP_RELEASING) c.held = in.volctl;

    // ---- R9: the app's level: the radio's fade x level (no talk-over factor) --------------------------
    // R6: while the service's own ad plays it is at the app's own level (x 1): never faded, ducked or
    // muted - not on a take, not after a pause; the fade stands at 1 so the music after it doesn't dip.
    const bool adOn = in.isAd && in.playing;
    if (c.phase == AP_PLAYING || c.phase == AP_PAUSING)
    {
        if (adOn) c.fade = 1.0f;
        else c.fade = std::fmin(1.0f, c.fade + dt / 0.3f);
    }
    else if (c.phase == AP_BREAK || c.phase == AP_USERPAUSED) c.fade = 0.0f;
    float vol = Clampf01(in.level) * c.fade;
    if (c.silenced) vol = 0.0f;
    if (adOn && c.phase != AP_IDLE) vol = 1.0f;
    o.held = c.held;
    o.volume = c.held ? vol : 1.0f;
    o.silenced = c.silenced;
    if (c.phase == AP_USERPAUSED) o.talkMode = true;
    return o;
}

// The bound app changed while the radio had one (the old app's session went away, or service_app
// changed): the old app is paused if it still plays in the radio's hands (only with the game in
// focus, I4), and the controller starts over - the next AppStep takes the new app (R1) with a
// prePlaying of its own. radio_app.h gives the old app's volume back once its pause has taken.
// True: pause the old app.
inline bool AppRebind(AppCtl& c, bool focus, bool oldPlaying)
{
    const bool pauseOld = focus && !c.unfocused && oldPlaying && c.phase != AP_IDLE;
    c = AppCtl();
    return pauseOld;
}

} // namespace radio
