// =============================================================================================
//  radio_model.h - the radio's content and its station schedulers (pure; no Windows, no audio).
//  Tested by test\radio_harness.cpp; played by radio.h.
//
//  The content comes from Radio\radio_runtime.tsv (written by Radio\_tools\make_runtime.py):
//    S <station> <name> <freq> <adGapMin> <adGapMax> <adsMin> <adsMax> <newsEveryMin> <idEverySongs>
//      <linkChance> <linesMin> <linesMax> <afterDarkFrom> <afterDarkTo>
//    C <owner> <kind> <clipId> <file> <dur> <gainDb> <introS> <ref> <tags> <label>
//
//  Every station is "live" (the GTA trick): its clock runs whether or not anyone listens, and it
//  plans its programme a block at a time - after each song: maybe news, an ad break or a host
//  link (outro for the song that ended, a time / weather / traffic / chat line or a call-in, then
//  an intro for the next song, talked over the song's own intro when it is long enough), a station
//  ID every few songs, then the next song. Tuning in plays whatever is airing, from where it is.
//
//  THE MUSIC SOURCE (2026-11-13, the listener's music on every station): a station's music comes from one
//  of four sources, and only the music changes - the host, the IDs, the calls, the ads and the news
//  stay the station's own:
//    SRC_AI    the generated songs (the default; PlanBlockAi is the original scheduler, untouched)
//    SRC_LIB   the listener's own files (K_TRACK clips, appended at run time; a shuffled deck)
//    SRC_APP   the listener's music app: "ext" slots the radio thread stretches / cuts to the app's
//              real tracks; the station's own clips never air while the app is heard
//    SRC_TALK  no music: host lines, IDs, ads and news
//  On anything but SRC_AI no generated music airs (no song, jingle, or segment with a music bed)
//  and no host line that names a generated song.
// =============================================================================================
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>
#include <unordered_map>

namespace radio {

enum Kind : uint8_t {
    K_SONG, K_JINGLE, K_CALL, K_SEGMENT,
    K_H_STATION_ID, K_H_INTRO, K_H_OUTRO, K_H_BETWEEN, K_H_MORNING, K_H_AFTERNOON, K_H_EVENING, K_H_NIGHT,
    K_H_WEATHER, K_H_TRAFFIC, K_H_CALL, K_H_TO_NEWS, K_H_FROM_ADS,
    K_H_INTRO_ANY, K_H_OUTRO_ANY,          // intros / outros that name no song or genre: for any station
                                           // playing the listener's music (library or app)
    K_AD, K_AD_DISCLAIMER,
    K_N_OPEN, K_N_OPEN_BREAKING, K_N_CLOSE, K_N_STORY,
    K_N_W1, K_N_W2, K_N_W3, K_N_W4, K_N_W5, K_N_HELI, K_N_BUSTED, K_N_ESCAPED, K_N_BREAKFREE, K_N_VEHICLE,
    K_TRACK,                               // internal: one of the listener's files (never read from the TSV)
    K_N_MISSION, K_N_MISSION_RECAP,        // [missions1] a main story mission's newscast / its later recap (ref mission:<JOBPATH>)
    K_N_MISSION_OPEN,                      // [missions1] a special-report opener before a newscast (no mission)
    K_COUNT, K_NONE = 255
};

static const char* const kKindNames[K_COUNT] = {
    "song", "jingle", "call", "segment",
    "host:station_id", "host:intro_song", "host:outro_song", "host:between", "host:time_morning",
    "host:time_afternoon", "host:time_evening", "host:time_night", "host:weather", "host:traffic",
    "host:listener_call", "host:hand_to_news", "host:back_from_ads",
    "host:intro_any", "host:outro_any",
    "ad", "ad_disclaimer",
    "news:open", "news:open_breaking", "news:close", "news:story",
    "news:wanted_level_1", "news:wanted_level_2", "news:wanted_level_3", "news:wanted_level_4",
    "news:wanted_level_5", "news:wanted_heli", "news:wanted_busted", "news:wanted_escaped",
    "news:wanted_breakfree", "news:wanted_vehicle",
    "track",
    "news:mission", "news:mission_recap", "news:mission_open",   // [missions1]
};

// T_NOMUSIC: a segment without a generated music bed (it may air when the AI songs are off).
// [radiologo1] T_MORNING..T_NIGHT: a segment (a programme) meant for that part of the day (stations.json
// time_of_day, = HourSlot); PickSegment prefers one whose part matches the hour it starts in, else any.
// A segment with none of the four (Brick Talk's) fits every hour.
enum Tag : uint8_t { T_CLEAR = 1, T_RAIN = 2, T_WANTED = 4, T_NOMUSIC = 8,
                     T_MORNING = 16, T_AFTERNOON = 32, T_EVENING = 64, T_NIGHT = 128 };
static const uint8_t T_DAYPARTS = T_MORNING | T_AFTERNOON | T_EVENING | T_NIGHT;

// Where a station's music comes from (see the header).
enum Source : uint8_t { SRC_AI, SRC_LIB, SRC_APP, SRC_TALK };
static const char* const kSourceNames[4] = { "ai", "library", "service", "talk" };

static const int kLastExt = -2;            // StationState::lastSong after an app slot

inline float Clampf01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

struct Clip
{
    std::string id, file, ref, label;
    float   dur = 0.0f, gainDb = 0.0f, intro = 0.0f;
    uint8_t kind = K_NONE, tags = 0;
    int     owner = -1;            // station index, -1 = shared (ads, news)
    int     song = -1;             // a song-templated host render: the song's clip index
    int     ad = -1;               // a disclaimer: its ad's clip index
    bool    fill = false;          // a {district} / {vehicle} news render: aired only for that place / car
    uint8_t fillKind = 0;          // 1 district, 2 vehicle
    std::string fillId;            // the fills.json id it names
    std::string mission;           // [missions1] news:mission / _recap: the mission's JOBPATH (ref mission:<id>)
    bool    full = false;          // [missions1] tag `full`: the clip opens and closes itself (no open_breaking / close added)
    // the listener's files (K_TRACK)
    bool    abs = false;           // `file` is an absolute UTF-8 path (not under the content root)
    bool    retired = false;       // left the station's pool (kept: queued events still point here)
    bool    autoGain = false;      // no loudness data: the player measures it as it plays
    bool    durGuess = false;      // the length is a guess until the file is opened
};

struct Station
{
    std::string id, name, freq;
    int   adGap[2] = { 3, 3 }, ads[2] = { 1, 2 }, newsEvery = 30, idEvery = 3, lines[2] = { 1, 2 };
    int   darkFrom = -1, darkTo = -1;
    float linkChance = 0.7f;
    std::vector<int> byKind[K_COUNT];      // clip indices
    // the listener's music
    std::vector<int> mine;                 // the station's pool of K_TRACK clips (library mode)
    uint32_t mineGen = 0;                  // bumps when `mine` changes (the deck reshuffles)
    uint8_t  genre = 0xFF;                 // radio_genre.h's station genre (set by the player; 0xFF = none)
};

struct Library
{
    std::vector<Clip>    clips;
    std::vector<Station> stations;
    std::vector<int>     shared[K_COUNT];  // ads, disclaimers, news
    std::string          district[32];     // `D <game index> <fill id>`: the game's districts as news ids
    bool                 hasDistricts = false;
    int warnings = 0;
    int trackClips = 0;                    // K_TRACK clips appended (capped: kMaxTrackClips)
};

static const int kMaxTrackClips = 60000;

// Generated music: never airs on a station whose music is the listener's (or none).
inline bool IsAiMusic(const Clip& c)
{
    return c.kind == K_SONG || c.kind == K_JINGLE || (c.kind == K_SEGMENT && !(c.tags & T_NOMUSIC));
}

// [radiologo1] The dial: n stations and "off" (index n) in a ring. A step skips the stations that can't
// play (ok[i] == 0: no songs or segments and no music of the listener's), so a half-made station never
// tunes to silence; with every station ok it is the old (i + steps) mod (n + 1). -1 = off.
inline int StepDial(int cur, int steps, const std::vector<uint8_t>& ok)
{
    const int n = static_cast<int>(ok.size());
    int i = (cur < 0 || cur >= n) ? n : cur;
    const int dir = steps > 0 ? 1 : -1;
    for (int left = steps > 0 ? steps : -steps; left > 0; --left)
        for (int k = 0; k <= n; ++k)                       // at most one lap
        {
            i = ((i + dir) % (n + 1) + (n + 1)) % (n + 1);
            if (i == n || ok[static_cast<size_t>(i)]) break;
        }
    return i == n ? -1 : i;
}

// ---- parsing ---------------------------------------------------------------------------------
inline int KindFromName(const char* s)
{
    for (int k = 0; k < K_COUNT; ++k)
    {
        if (k == K_TRACK) continue;                  // internal only: a TSV "track" is unknown
        if (std::strcmp(s, kKindNames[k]) == 0) return k;
    }
    return -1;
}

inline void SplitTabs(char* line, std::vector<char*>& f)
{
    f.clear();
    char* p = line;
    f.push_back(p);
    for (; *p; ++p) if (*p == '\t') { *p = 0; f.push_back(p + 1); }
}

inline std::string Field(const char* s) { return (s[0] == '-' && s[1] == 0) ? std::string() : std::string(s); }

// Parses the whole file text. Returns false only when nothing usable was found. `needSongs` false
// (some station plays the listener's music): a list with no songs at all still loads - its hosts,
// ads and news can talk between the listener's own tracks (a release without the generated music).
inline bool Parse(const char* text, size_t n, Library& L, std::string& err, bool needSongs = true)
{
    L = Library();
    std::string buf(text, n);
    std::vector<char*> f;
    size_t pos = 0;
    int lineNo = 0;
    while (pos < buf.size())
    {
        size_t e = buf.find('\n', pos);
        if (e == std::string::npos) e = buf.size();
        std::string line = buf.substr(pos, e - pos);
        pos = e + 1; ++lineNo;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        std::vector<char> mut(line.begin(), line.end()); mut.push_back(0);
        SplitTabs(mut.data(), f);
        if (f[0][0] == 'S' && f[0][1] == 0 && f.size() >= 15)
        {
            Station s;
            s.id = f[1]; s.name = f[2]; s.freq = f[3];
            s.adGap[0] = std::atoi(f[4]); s.adGap[1] = std::atoi(f[5]);
            s.ads[0] = std::atoi(f[6]);   s.ads[1] = std::atoi(f[7]);
            s.newsEvery = std::atoi(f[8]); s.idEvery = std::atoi(f[9]);
            s.linkChance = static_cast<float>(std::atof(f[10]));
            s.lines[0] = std::atoi(f[11]); s.lines[1] = std::atoi(f[12]);
            s.darkFrom = std::atoi(f[13]); s.darkTo = std::atoi(f[14]);
            if (s.adGap[1] < s.adGap[0]) s.adGap[1] = s.adGap[0];
            if (s.ads[1] < s.ads[0]) s.ads[1] = s.ads[0];
            if (s.lines[1] < s.lines[0]) s.lines[1] = s.lines[0];
            if (s.idEvery < 1) s.idEvery = 1;
            L.stations.push_back(s);
            continue;
        }
        if (f[0][0] == 'D' && f[0][1] == 0 && f.size() >= 3)
        {
            const int gi = std::atoi(f[1]);
            if (gi >= 0 && gi < 32) { L.district[gi] = Field(f[2]); L.hasDistricts = true; }
            continue;
        }
        if (!(f[0][0] == 'C' && f[0][1] == 0) || f.size() < 11) { ++L.warnings; continue; }
        const int k = KindFromName(f[2]);
        if (k < 0) { ++L.warnings; continue; }
        Clip c;
        c.kind = static_cast<uint8_t>(k);
        c.id = f[3]; c.file = f[4];
        c.dur = static_cast<float>(std::atof(f[5]));
        c.gainDb = static_cast<float>(std::atof(f[6]));
        c.intro = static_cast<float>(std::atof(f[7]));
        c.ref = Field(f[8]);
        c.label = f[10];
        const std::string tags = Field(f[9]);
        if (tags.find("clear") != std::string::npos) c.tags |= T_CLEAR;
        if (tags.find("rain") != std::string::npos) c.tags |= T_RAIN;
        if (tags.find("wanted") != std::string::npos) c.tags |= T_WANTED;
        if (tags.find("nomusic") != std::string::npos) c.tags |= T_NOMUSIC;
        // [radiologo1] the programme's part of the day (comma separated words, so "afternoon" is not "noon")
        for (size_t a = 0; a <= tags.size(); )
        {
            size_t b = tags.find(',', a);
            if (b == std::string::npos) b = tags.size();
            const std::string w = tags.substr(a, b - a);
            if (w == "morning") c.tags |= T_MORNING;
            else if (w == "afternoon") c.tags |= T_AFTERNOON;
            else if (w == "evening") c.tags |= T_EVENING;
            else if (w == "night") c.tags |= T_NIGHT;
            else if (w == "full") c.full = true;            // [missions1]
            a = b + 1;
        }
        if (!(c.dur > 0.05f) || c.file.empty()) { ++L.warnings; continue; }
        if (!(c.gainDb > -40.0f && c.gainDb < 24.0f)) c.gainDb = 0.0f;
        if (!(c.intro >= 0.0f && c.intro < c.dur)) c.intro = 0.0f;
        const std::string owner = f[1];
        if (owner == "_shared") c.owner = -1;
        else
        {
            c.owner = -1;
            for (size_t i = 0; i < L.stations.size(); ++i) if (L.stations[i].id == owner) { c.owner = static_cast<int>(i); break; }
            if (c.owner < 0) { ++L.warnings; continue; }      // a clip before (or without) its station
        }
        if (c.ref.compare(0, 9, "district:") == 0)     { c.fill = true; c.fillKind = 1; c.fillId = c.ref.substr(9); }
        else if (c.ref.compare(0, 8, "vehicle:") == 0) { c.fill = true; c.fillKind = 2; c.fillId = c.ref.substr(8); }
        else if (c.ref.compare(0, 8, "mission:") == 0) c.mission = c.ref.substr(8);   // [missions1]
        if ((k == K_N_MISSION || k == K_N_MISSION_RECAP) && c.mission.empty()) { ++L.warnings; continue; }   // [missions1] no mission named
        const int idx = static_cast<int>(L.clips.size());
        L.clips.push_back(c);
        if (c.owner >= 0) L.stations[c.owner].byKind[k].push_back(idx);
        else L.shared[k].push_back(idx);
    }
    // resolve the references: song-templated host renders, disclaimers
    for (Clip& c : L.clips)
    {
        if (c.ref.empty() || c.fill || !c.mission.empty()) continue;   // [missions1] || mission
        if (c.kind == K_AD_DISCLAIMER)
        {
            for (int a : L.shared[K_AD]) if (L.clips[a].id == c.ref) { c.ad = a; break; }
        }
        else if (c.owner >= 0)
        {
            for (int s : L.stations[c.owner].byKind[K_SONG]) if (L.clips[s].id == c.ref) { c.song = s; break; }
        }
    }
    int playable = 0;
    for (const Station& s : L.stations) playable += static_cast<int>(s.byKind[K_SONG].size() + s.byKind[K_SEGMENT].size());
    if (L.stations.empty() || playable == 0)
    {
        if (!needSongs && (!L.stations.empty() || !L.shared[K_AD].empty() || !L.shared[K_N_STORY].empty())) return true;
        err = "no stations with songs"; return false;
    }
    return true;
}

// ---- the listener's tracks -------------------------------------------------------------------
struct Track
{
    std::string path8;             // absolute path, UTF-8
    std::string label;             // "Artist - Title" or the file name
    float       dur = 210.0f;      // seconds
    bool        durGuess = false;  // no length in the tags: 210 s until the file is opened
};

// Appends the listener's files to station `si` as K_TRACK clips and returns their indices. A file
// already in the station's pool keeps its clip (a rescan of an unchanged library adds nothing).
// Clips are never erased: the list is append-only within one parsed Library, so queued events
// always point at a valid clip. Refuses (returns what fitted) past kMaxTrackClips.
inline std::vector<int> AppendTracks(Library& L, int si, const std::vector<Track>& tracks, bool* capped = nullptr)
{
    std::vector<int> out;
    if (capped) *capped = false;
    if (si < 0 || si >= static_cast<int>(L.stations.size())) return out;
    std::unordered_map<std::string, int> have;
    for (int i : L.stations[si].mine) if (i >= 0 && i < static_cast<int>(L.clips.size())) have[L.clips[i].file] = i;
    out.reserve(tracks.size());
    for (const Track& t : tracks)
    {
        auto it = have.find(t.path8);
        if (it != have.end())
        {
            Clip& c = L.clips[it->second];
            if (!c.retired) { c.label = t.label; if (!t.durGuess || c.durGuess) { c.dur = t.dur; c.durGuess = t.durGuess; } out.push_back(it->second); continue; }
        }
        if (L.trackClips >= kMaxTrackClips) { if (capped) *capped = true; break; }
        Clip c;
        c.kind = K_TRACK; c.owner = si; c.abs = true; c.autoGain = true;
        c.file = t.path8; c.label = t.label; c.id = t.label;
        c.dur = t.dur > 1.0f ? t.dur : 1.0f; c.durGuess = t.durGuess;
        out.push_back(static_cast<int>(L.clips.size()));
        L.clips.push_back(c);
        ++L.trackClips;
    }
    return out;
}

// The station's pool becomes `idx` (from AppendTracks). Clips that leave it are retired, never erased.
// The same pool again (a rescan that found nothing new) changes nothing: the deck deals on.
inline void SetPool(Library& L, int si, const std::vector<int>& idx)
{
    if (si < 0 || si >= static_cast<int>(L.stations.size())) return;
    Station& st = L.stations[si];
    if (idx == st.mine) return;
    std::unordered_map<int, char> keep;
    for (int i : idx) keep[i] = 1;
    for (int i : st.mine) if (!keep.count(i) && i >= 0 && i < static_cast<int>(L.clips.size())) L.clips[i].retired = true;
    for (int i : idx) if (i >= 0 && i < static_cast<int>(L.clips.size())) L.clips[i].retired = false;
    st.mine = idx;
    ++st.mineGen;
}

// ---- the scheduler ---------------------------------------------------------------------------
struct Rng
{
    uint64_t s = 0x9E3779B97F4A7C15ull;
    uint32_t Next() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return static_cast<uint32_t>(s >> 11); }
    float    Uniform() { return (Next() & 0xFFFFFF) / 16777216.0f; }
    int      Range(int lo, int hi) { return hi <= lo ? lo : lo + static_cast<int>(Next() % static_cast<uint32_t>(hi - lo + 1)); }
};

struct Event
{
    int    clip = -1;              // what airs
    double start = 0.0;            // station seconds
    float  dur = 0.0f;
    int    over = -1;              // a host intro talked over the start of this song
    float  overAt = 0.0f;          // ... starting this far in
    uint8_t ext = 0;               // 1 = a slot for the listener's music app (clip -1, dur nominal
                                   // until the radio thread stretches or cuts it to the app's track)
};

struct World
{
    float hour = 12.0f;            // game hour 0..24
    float hourRate = 0.0f;         // game hours per station second (the day/night mod: 24 per 480 s),
                                   // so a block planned ahead picks lines for when it will air
    int   wanted = 0;              // heat level 0..5
    bool  rain = false;
    char  district[24] = {};       // where the player is, as a fills.json {district} id ("" = unknown)
    char  vehicle[24] = {};        // what the player drives, as a fills.json {vehicle} id ("" = on foot)
};

struct StationState
{
    double t = 0.0;                // the station's clock
    std::vector<Event> q;          // q[0] airs now; the rest is planned
    int    songsSinceAds = 0, adGap = 3, songsSinceId = 0;
    double lastNews = 0.0;
    int    lastSong = -1;
    int    pendingNews = K_NONE;   // (an empty plan) a wanted bulletin for the first block
    // wanted-mode news: only some stations carry it, the latest event wins, bulletins are spaced
    bool   carriesBreaking = true;
    double breakingGap = 240.0;    // station seconds between two wanted bulletins
    int    breakingPending = K_NONE;
    double lastBreaking = -1e9;
    bool   chaseReported = false;  // a level bulletin aired for the chase now running
    // [missions1] a mission recap for this station's next news break (radio.h sets it on the tuned station only)
    int    recapClip = -1;         // the clip to read after news:open, -1 = none
    float  recapChance = 0.0f;     // ...with this chance per news break
    int    recapPlanned = -1;      // out: the clip a news break took (radio.h counts it and resets this)
    std::vector<int> recent[K_COUNT];
    std::vector<int> recentShared[K_COUNT];
    Rng    rng;
    // the music source (SRC_AI: everything above is the original scheduler's, untouched)
    uint8_t source = SRC_AI;
    bool   genreLines = false;     // the pool is the station's genre: its own genre-flavoured lines may air
    bool   introOver = false;      // library_talkover: an intro_any may go over a track's start
    float  maxOver = 9.0f;         // ... when it is this short at most
    float  extLen[2] = { 150.0f, 270.0f };   // an app slot's nominal length (stations nobody listens to)
    double talkAccum = 0.0;        // talk only: seconds of talk not yet counted as a "song" for the ads
    std::vector<int> deck;         // library: the shuffled pool, dealt in order
    size_t deckPos = 0;
    uint32_t deckGen = 0xFFFFFFFFu;
    int    deckLast = -1;
};

inline int HourSlot(float hour)
{
    const float h = std::fmod(std::fmod(hour, 24.0f) + 24.0f, 24.0f);
    if (h >= 5.0f && h < 11.0f) return K_H_MORNING;
    if (h >= 11.0f && h < 17.0f) return K_H_AFTERNOON;
    if (h >= 17.0f && h < 21.0f) return K_H_EVENING;
    return K_H_NIGHT;
}

inline bool InHours(float hour, int from, int to)
{
    if (from < 0 || to < 0) return false;
    const float h = std::fmod(std::fmod(hour, 24.0f) + 24.0f, 24.0f);
    return from <= to ? (h >= from && h < to) : (h >= from || h < to);
}

inline bool TagsOk(const Clip& c, const World& w)
{
    if ((c.tags & T_WANTED) && w.wanted <= 0) return false;
    if ((c.tags & T_RAIN) && !w.rain) return false;
    if ((c.tags & T_CLEAR) && w.rain) return false;
    return true;
}

// Pick from a list, skipping the recently aired ones (the last ~half, up to `keepCap`) and, with
// `want` >= 0, preferring renders made for that song. -1 when the list has nothing usable.
// (The listener's tracks never come through here: the cand[512] cap would cut a big pool. The deck
// deals them.)
template <class Ok>
inline int PickFrom(const Library& L, const std::vector<int>& from, std::vector<int>& recent, Rng& rng, Ok ok,
                    int wantSong = -1, float preferTemplated = 0.0f, size_t keepCap = 6)
{
    int cand[512]; int n = 0;
    int tmpl[512]; int nt = 0;
    for (int pass = 0; pass < 2 && n == 0; ++pass)
        for (int i : from)
        {
            const Clip& c = L.clips[i];
            if (!ok(c)) continue;
            if (c.song >= 0 && c.song != wantSong) continue;       // a render for another song
            bool seen = false;
            if (pass == 0) for (int r : recent) if (r == i) { seen = true; break; }
            if (seen) continue;
            if (n < 512) cand[n++] = i;
            if (c.song >= 0 && nt < 512) tmpl[nt++] = i;
        }
    if (n == 0) return -1;
    const int pick = (nt > 0 && rng.Uniform() < preferTemplated) ? tmpl[rng.Next() % nt] : cand[rng.Next() % n];
    const size_t keep = from.size() / 2 < keepCap ? from.size() / 2 : keepCap;
    recent.push_back(pick);
    while (recent.size() > keep) recent.erase(recent.begin());
    return pick;
}

// [radiologo1] Does a segment fit the hour it starts in? Untagged: always; tagged: its part of the day.
inline bool SegHourOk(const Clip& c, float hour)
{
    if (!(c.tags & T_DAYPARTS)) return true;
    const int slot = HourSlot(hour);
    const uint8_t bit = slot == K_H_MORNING ? T_MORNING : slot == K_H_AFTERNOON ? T_AFTERNOON : slot == K_H_EVENING ? T_EVENING : T_NIGHT;
    return (c.tags & bit) != 0;
}
// A segment (a programme) for the hour it starts in: one that fits the hour if there is one, else any.
template <class Ok>
inline int PickSegment(const Library& L, const std::vector<int>& from, std::vector<int>& recent, Rng& rng, float hour, Ok ok)
{
    const int c = PickFrom(L, from, recent, rng, [&](const Clip& x) { return ok(x) && SegHourOk(x, hour); });
    return c >= 0 ? c : PickFrom(L, from, recent, rng, ok);
}

// A news line: a render naming where the player is (or what they drive) when there is one - most
// of the time - otherwise a generic one; never a render for somewhere else.
inline int PickNews(StationState& s, const Library& L, int kind, const World& w)
{
    auto match = [&](const Clip& c) {
        return c.fill && ((c.fillKind == 1 && w.district[0] && c.fillId == w.district) ||
                          (c.fillKind == 2 && w.vehicle[0] && c.fillId == w.vehicle));
    };
    auto generic = [](const Clip& c) { return !c.fill; };
    bool any = false;
    for (int i : L.shared[kind]) if (match(L.clips[i])) { any = true; break; }
    if (any && s.rng.Uniform() < 0.6f)
    {
        const int p = PickFrom(L, L.shared[kind], s.recentShared[kind], s.rng, match);
        if (p >= 0) return p;
    }
    return PickFrom(L, L.shared[kind], s.recentShared[kind], s.rng, generic);
}

// A wanted bulletin: open_breaking, the item, (in a vehicle, for a chase) what you drive, close.
inline int Bulletin(StationState& s, const Library& L, int kind, const World& w, int out[4])
{
    const int item = PickNews(s, L, kind, w);
    if (item < 0) return 0;
    int n = 0;
    const int open = PickNews(s, L, K_N_OPEN_BREAKING, w);
    if (open >= 0) out[n++] = open;
    out[n++] = item;
    const bool chase = (kind >= K_N_W1 && kind <= K_N_W5) || kind == K_N_HELI;
    if (chase && w.vehicle[0] && s.rng.Uniform() < 0.7f)
    {
        const int v = PickNews(s, L, K_N_VEHICLE, w);         // only renders exist: none = no line
        if (v >= 0) out[n++] = v;
    }
    const int close = PickNews(s, L, K_N_CLOSE, w);
    if (close >= 0) out[n++] = close;
    return n;
}

inline void Push(StationState& s, const Library& L, double& at, int clip)
{
    if (clip < 0) return;
    Event e; e.clip = clip; e.start = at; e.dur = L.clips[clip].dur;
    s.q.push_back(e);
    at += e.dur;
}

// A slot for the listener's music app: no clip, a nominal length the radio thread fits to the app.
inline void PushExt(StationState& s, double& at, float dur)
{
    Event e; e.clip = -1; e.ext = 1; e.start = at; e.dur = dur;
    s.q.push_back(e);
    at += e.dur;
}

// One block of the generated-music programme: whatever follows the song that just ended, then the
// next song. (This is the scheduler as it was before the music sources: moved here unchanged.)
inline void PlanBlockAi(StationState& s, const Library& L, int si, const World& w)
{
    const Station& st = L.stations[si];
    const size_t before = s.q.size();
    double at = s.q.empty() ? s.t : s.q.back().start + s.q.back().dur;
    auto any = [&](const Clip& c) { return TagsOk(c, w); };
    auto generic = [&](const Clip& c) { return TagsOk(c, w) && !c.fill; };
    auto host = [&](int kind, int wantSong = -1, float pref = 0.0f) {
        return PickFrom(L, st.byKind[kind], s.recent[kind], s.rng, any, wantSong, pref);
    };
    auto shared = [&](int kind) { return PickFrom(L, L.shared[kind], s.recentShared[kind], s.rng, generic); };

    // 1. a wanted bulletin cuts in at the first gap
    if (s.pendingNews != K_NONE)
    {
        int parts[4];
        const int n = Bulletin(s, L, s.pendingNews, w, parts);
        for (int i = 0; i < n; ++i) Push(s, L, at, parts[i]);
        s.pendingNews = K_NONE;
    }
    // 2. the news, every news_every_minutes of station time
    else if (!L.shared[K_N_STORY].empty() && st.newsEvery > 0 && at - s.lastNews >= st.newsEvery * 60.0 &&
             at - s.lastBreaking > 180.0)                     // not straight after a wanted bulletin
    {
        Push(s, L, at, host(K_H_TO_NEWS));
        Push(s, L, at, shared(K_N_OPEN));
        if (s.recapClip >= 0 && s.recapClip < static_cast<int>(L.clips.size()) && s.rng.Uniform() < s.recapChance)   // [missions1]
        {
            Push(s, L, at, s.recapClip); s.recapPlanned = s.recapClip; s.recapClip = -1;
        }
        const int n = s.rng.Range(1, 3);
        for (int i = 0; i < n; ++i) Push(s, L, at, shared(K_N_STORY));
        Push(s, L, at, shared(K_N_CLOSE));
        s.lastNews = at;
    }
    // 3. an ad break
    else if (!L.shared[K_AD].empty() && s.songsSinceAds >= s.adGap)
    {
        const int n = s.rng.Range(st.ads[0], st.ads[1]);
        for (int i = 0; i < n; ++i)
        {
            const int ad = shared(K_AD);
            Push(s, L, at, ad);
            for (int d : L.shared[K_AD_DISCLAIMER]) if (L.clips[d].ad == ad) { Push(s, L, at, d); break; }
        }
        Push(s, L, at, host(K_H_FROM_ADS));
        s.songsSinceAds = 0;
        s.adGap = s.rng.Range(st.adGap[0], st.adGap[1]);
    }

    // the next song (chosen now, so the host can introduce it by name)
    const int next = PickFrom(L, st.byKind[K_SONG], s.recent[K_SONG], s.rng,
                              [&](const Clip& c) { return &c - &L.clips[0] != s.lastSong; });

    // 4. After Dark: the long segments (Brick Talk), instead of the usual talk
    const float hourSeg = w.hour + static_cast<float>((at - s.t) * w.hourRate);   // after any news / ads
    if (InHours(hourSeg, st.darkFrom, st.darkTo) && !st.byKind[K_SEGMENT].empty() && s.rng.Uniform() < 0.75f)
    {
        Push(s, L, at, PickSegment(L, st.byKind[K_SEGMENT], s.recent[K_SEGMENT], s.rng, hourSeg, any));   // [radiologo1] by the hour
    }
    // 5. a host link
    else if (s.rng.Uniform() < st.linkChance)
    {
        const int n = s.rng.Range(st.lines[0], st.lines[1]);
        int used = 0;
        if (s.lastSong >= 0 && n >= 2) { Push(s, L, at, host(K_H_OUTRO, s.lastSong, 0.7f)); ++used; }
        bool chaseLine = false;
        while (used < n - 1)
        {
            const float r = s.rng.Uniform();
            int clip = -1;
            // with heat on, the host talks about the chase now and then (their wanted-tagged lines, any
            // slot) - the stations that don't run the wanted news still react to it
            if (w.wanted > 0 && !chaseLine && s.rng.Uniform() < 0.45f)
            {
                static const int kinds[] = { K_H_BETWEEN, K_H_TRAFFIC, K_H_WEATHER, K_H_CALL, K_H_STATION_ID };
                for (int k : kinds)
                {
                    clip = PickFrom(L, st.byKind[k], s.recent[k], s.rng, [&](const Clip& c) { return (c.tags & T_WANTED) != 0; });
                    if (clip >= 0) break;
                }
                chaseLine = clip >= 0;
            }
            if (clip >= 0) {}
            else if (r < 0.25f) clip = host(HourSlot(w.hour + static_cast<float>((at - s.t) * w.hourRate)));   // when it airs
            else if (r < 0.40f) clip = host(K_H_WEATHER);
            else if (r < 0.55f) clip = host(K_H_TRAFFIC);
            else if (r < 0.75f) clip = st.byKind[K_CALL].empty() || s.rng.Uniform() < 0.3f ? host(K_H_CALL)
                                     : PickFrom(L, st.byKind[K_CALL], s.recent[K_CALL], s.rng, any);
            else                clip = host(K_H_BETWEEN);
            if (clip < 0) clip = host(K_H_BETWEEN);
            Push(s, L, at, clip);
            ++used;
        }
        if (s.songsSinceId + 1 >= st.idEvery)
        {
            const bool jingle = !st.byKind[K_JINGLE].empty() && s.rng.Uniform() < 0.35f;
            Push(s, L, at, jingle ? st.byKind[K_JINGLE][0] : host(K_H_STATION_ID));
            s.songsSinceId = 0;
        }
        else ++s.songsSinceId;
        // the intro: over the song's own intro when it fits, before the song otherwise
        const int intro = (next >= 0) ? host(K_H_INTRO, next, 0.7f) : -1;
        if (intro >= 0 && next >= 0 && L.clips[next].intro >= L.clips[intro].dur + 0.75f)
        {
            Event e; e.clip = next; e.start = at; e.dur = L.clips[next].dur; e.over = intro; e.overAt = 0.4f;
            s.q.push_back(e); at += e.dur;
            s.lastSong = next; ++s.songsSinceAds;
            return;
        }
        Push(s, L, at, intro);
    }
    else if (s.songsSinceId + 1 >= st.idEvery)
    {
        const bool jingle = !st.byKind[K_JINGLE].empty() && s.rng.Uniform() < 0.5f;
        Push(s, L, at, jingle ? st.byKind[K_JINGLE][0] : host(K_H_STATION_ID));
        s.songsSinceId = 0;
    }
    else ++s.songsSinceId;

    if (next >= 0) { Push(s, L, at, next); s.lastSong = next; ++s.songsSinceAds; }
    if (s.q.size() == before)
    {
        // nothing to air (a station with no songs): keep it alive with whatever it has, or a pause
        int c = host(K_H_BETWEEN);
        if (c < 0 && !st.byKind[K_SEGMENT].empty()) c = st.byKind[K_SEGMENT][0];
        if (c < 0) { Event e; e.clip = -1; e.start = at; e.dur = 30.0f; s.q.push_back(e); }
        else Push(s, L, at, c);
    }
}

inline void ShuffleInts(std::vector<int>& v, size_t from, size_t to, Rng& r)
{
    for (size_t i = to; i > from + 1; --i)
    {
        const size_t j = from + r.Next() % static_cast<uint32_t>(i - from);
        const int t = v[i - 1]; v[i - 1] = v[j]; v[j] = t;
    }
}

// The library deck: the pool shuffled (Fisher-Yates), dealt in order - nothing repeats until the
// whole pool has played. A reshuffle of the same pool deals the half played last in its second
// half, so a track never comes back sooner than about half a pool later (never twice in a row).
// -1 when the pool is empty.
inline int DeckNext(StationState& s, const Station& st)
{
    if (st.mine.empty()) { s.deck.clear(); s.deckPos = 0; return -1; }
    if (s.deckGen != st.mineGen || s.deckPos >= s.deck.size() || s.deck.empty())
    {
        const size_t n = st.mine.size();
        if (s.deckGen == st.mineGen && s.deck.size() == n && n > 1)
        {
            // the same pool again: the tracks of the old deck's second half go to the new one's second half
            std::vector<int> head, tail;
            head.reserve(n); tail.reserve(n);
            for (size_t i = 0; i < n; ++i) (i < n / 2 ? head : tail).push_back(s.deck[i]);
            s.deck = head;
            s.deck.insert(s.deck.end(), tail.begin(), tail.end());
            ShuffleInts(s.deck, 0, head.size(), s.rng);
            ShuffleInts(s.deck, head.size(), n, s.rng);
        }
        else
        {
            s.deck = st.mine;
            ShuffleInts(s.deck, 0, s.deck.size(), s.rng);
            if (s.deck.size() > 1 && s.deck[0] == s.deckLast) { const int t = s.deck[0]; s.deck[0] = s.deck.back(); s.deck.back() = t; }
        }
        s.deckPos = 0;
        s.deckGen = st.mineGen;
    }
    const int c = s.deck[s.deckPos++];
    s.deckLast = c;
    return c;
}

// One block of a station playing the listener's music (SRC_LIB / SRC_APP) or no music (SRC_TALK):
// the same pacing as PlanBlockAi - bulletins, news, ads, a host link, the ID - around the
// listener's tracks. Never a generated song, jingle or music-bed segment, never a host line that
// names a generated song; intros and outros are the generic intro_any / outro_any lines (or, on a
// genre-matched library pool, now and then the host's own untemplated genre lines).
inline void PlanBlockMine(StationState& s, const Library& L, int si, const World& w)
{
    const Station& st = L.stations[si];
    const size_t before = s.q.size();
    double at = s.q.empty() ? s.t : s.q.back().start + s.q.back().dur;
    const bool lib = s.source == SRC_LIB && !st.mine.empty();
    const bool app = s.source == SRC_APP;
    const bool talk = !lib && !app;                       // talk only (or a library with nothing in it)
    const size_t keepCap = talk ? 32 : 6;
    auto any = [&](const Clip& c) { return TagsOk(c, w) && c.song < 0; };
    auto untemplated = [&](const Clip& c) { return TagsOk(c, w) && c.song < 0 && c.ref.empty(); };
    auto generic = [&](const Clip& c) { return TagsOk(c, w) && !c.fill; };
    // the repeat window: half of the lines that can air now (not half the list: two clear-weather
    // lines out of four would otherwise come back to back), at most keepCap
    auto capFor = [&](int kind) {
        size_t n = 0;
        for (int i : st.byKind[kind]) if (any(L.clips[i])) ++n;
        n /= 2;
        return n < 1 ? static_cast<size_t>(1) : (n < keepCap ? n : keepCap);
    };
    auto host = [&](int kind) { return PickFrom(L, st.byKind[kind], s.recent[kind], s.rng, any, -1, 0.0f, capFor(kind)); };
    auto shared = [&](int kind) { return PickFrom(L, L.shared[kind], s.recentShared[kind], s.rng, generic); };

    // a. bulletin / news / ads: as PlanBlockAi (a copy, so the generated programme can't change)
    if (s.pendingNews != K_NONE)
    {
        int parts[4];
        const int n = Bulletin(s, L, s.pendingNews, w, parts);
        for (int i = 0; i < n; ++i) Push(s, L, at, parts[i]);
        s.pendingNews = K_NONE;
    }
    else if (!L.shared[K_N_STORY].empty() && st.newsEvery > 0 && at - s.lastNews >= st.newsEvery * 60.0 &&
             at - s.lastBreaking > 180.0)
    {
        Push(s, L, at, host(K_H_TO_NEWS));
        Push(s, L, at, shared(K_N_OPEN));
        if (s.recapClip >= 0 && s.recapClip < static_cast<int>(L.clips.size()) && s.rng.Uniform() < s.recapChance)   // [missions1]
        {
            Push(s, L, at, s.recapClip); s.recapPlanned = s.recapClip; s.recapClip = -1;
        }
        const int n = s.rng.Range(1, 3);
        for (int i = 0; i < n; ++i) Push(s, L, at, shared(K_N_STORY));
        Push(s, L, at, shared(K_N_CLOSE));
        s.lastNews = at;
    }
    else if (!L.shared[K_AD].empty() && s.songsSinceAds >= s.adGap)
    {
        const int n = s.rng.Range(st.ads[0], st.ads[1]);
        for (int i = 0; i < n; ++i)
        {
            const int ad = shared(K_AD);
            Push(s, L, at, ad);
            for (int d : L.shared[K_AD_DISCLAIMER]) if (L.clips[d].ad == ad) { Push(s, L, at, d); break; }
        }
        Push(s, L, at, host(K_H_FROM_ADS));
        s.songsSinceAds = 0;
        s.adGap = s.rng.Range(st.adGap[0], st.adGap[1]);
    }
    const double talkFrom = at;

    // b. the next music: a track off the deck, or a slot for the app
    const int next = lib ? DeckNext(s, st) : -1;

    // c. After Dark: only segments without a generated music bed
    bool darkSeg = false;
    {
        bool hasSeg = false;
        for (int i : st.byKind[K_SEGMENT]) if (!IsAiMusic(L.clips[i])) { hasSeg = true; break; }
        const float hourSeg = w.hour + static_cast<float>((at - s.t) * w.hourRate);
        if (hasSeg && InHours(hourSeg, st.darkFrom, st.darkTo) && s.rng.Uniform() < 0.75f)
        {
            Push(s, L, at, PickSegment(L, st.byKind[K_SEGMENT], s.recent[K_SEGMENT], s.rng, hourSeg,   // [radiologo1] by the hour
                                       [&](const Clip& c) { return TagsOk(c, w) && !IsAiMusic(c); }));
            darkSeg = true;
        }
    }

    // d. a host link
    if (!darkSeg && (talk || s.rng.Uniform() < st.linkChance))
    {
        int n = s.rng.Range(st.lines[0], st.lines[1]);
        if (talk && n < 2) n = 2;
        int used = 0;
        if (!talk && s.lastSong != -1 && n >= 2)
        {
            int o = -1;
            if (s.genreLines && s.rng.Uniform() < 0.3f)
                o = PickFrom(L, st.byKind[K_H_OUTRO], s.recent[K_H_OUTRO], s.rng, untemplated);
            if (o < 0) o = PickFrom(L, st.byKind[K_H_OUTRO_ANY], s.recent[K_H_OUTRO_ANY], s.rng, untemplated);
            if (o >= 0) { Push(s, L, at, o); ++used; }
        }
        bool chaseLine = false;
        const int middle = talk ? n : n - 1;
        while (used < middle)
        {
            const float r = s.rng.Uniform();
            int clip = -1;
            if (w.wanted > 0 && !chaseLine && s.rng.Uniform() < 0.45f)
            {
                static const int kinds[] = { K_H_BETWEEN, K_H_TRAFFIC, K_H_WEATHER, K_H_CALL, K_H_STATION_ID };
                for (int k : kinds)
                {
                    clip = PickFrom(L, st.byKind[k], s.recent[k], s.rng,
                                    [&](const Clip& c) { return (c.tags & T_WANTED) != 0 && c.song < 0; }, -1, 0.0f, keepCap);
                    if (clip >= 0) break;
                }
                chaseLine = clip >= 0;
            }
            if (clip >= 0) {}
            else if (r < 0.25f) clip = host(HourSlot(w.hour + static_cast<float>((at - s.t) * w.hourRate)));
            else if (r < 0.40f) clip = host(K_H_WEATHER);
            else if (r < 0.55f) clip = host(K_H_TRAFFIC);
            else if (r < 0.75f) clip = st.byKind[K_CALL].empty() || s.rng.Uniform() < 0.3f ? host(K_H_CALL)
                                     : PickFrom(L, st.byKind[K_CALL], s.recent[K_CALL], s.rng, any, -1, 0.0f, keepCap);
            else                clip = host(K_H_BETWEEN);
            if (clip < 0) clip = host(K_H_BETWEEN);
            Push(s, L, at, clip);
            ++used;
        }
        if (s.songsSinceId + 1 >= st.idEvery)             // never the jingle: it is generated music
        {
            Push(s, L, at, host(K_H_STATION_ID));
            s.songsSinceId = 0;
        }
        else ++s.songsSinceId;
        if (lib || app)
        {
            int intro = -1;
            if (s.genreLines && s.rng.Uniform() < 0.3f)
                intro = PickFrom(L, st.byKind[K_H_INTRO], s.recent[K_H_INTRO], s.rng, untemplated);
            if (intro < 0) intro = PickFrom(L, st.byKind[K_H_INTRO_ANY], s.recent[K_H_INTRO_ANY], s.rng, untemplated);
            if (intro >= 0 && lib && next >= 0 && s.introOver && L.clips[intro].kind == K_H_INTRO_ANY &&
                L.clips[intro].dur <= s.maxOver && L.clips[intro].dur + 0.75f <= L.clips[next].dur)
            {
                Event e; e.clip = next; e.start = at; e.dur = L.clips[next].dur; e.over = intro; e.overAt = 0.4f;
                s.q.push_back(e); at += e.dur;
                s.lastSong = next; ++s.songsSinceAds;
                return;
            }
            Push(s, L, at, intro);                        // the app: always before (never over its music)
        }
    }
    else if (!darkSeg)
    {
        // e. no link: the ID rule, a host station ID only
        if (s.songsSinceId + 1 >= st.idEvery) { Push(s, L, at, host(K_H_STATION_ID)); s.songsSinceId = 0; }
        else ++s.songsSinceId;
    }

    // f. the music
    if (lib && next >= 0) { Push(s, L, at, next); s.lastSong = next; ++s.songsSinceAds; }
    else if (app)
    {
        const float lo = s.extLen[0], hi = s.extLen[1] > s.extLen[0] ? s.extLen[1] : s.extLen[0];
        PushExt(s, at, lo + (hi - lo) * s.rng.Uniform());
        s.lastSong = kLastExt; ++s.songsSinceAds;
    }

    // g. keep-alive: a between line, a segment without music, or a pause
    if (s.q.size() == before)
    {
        int c = host(K_H_BETWEEN);
        if (c < 0) for (int i : st.byKind[K_SEGMENT]) if (!IsAiMusic(L.clips[i])) { c = i; break; }
        if (c < 0) { Event e; e.clip = -1; e.start = at; e.dur = 30.0f; s.q.push_back(e); at += 30.0; }
        else Push(s, L, at, c);
    }
    if (talk)
    {
        // every 150 s of talk counts as one song for the ad cadence
        s.talkAccum += at - talkFrom;
        while (s.talkAccum >= 150.0) { ++s.songsSinceAds; s.talkAccum -= 150.0; }
    }
}

// One block: the generated-music programme, or the listener's music / talk.
inline void PlanBlock(StationState& s, const Library& L, int si, const World& w)
{
    if (s.source == SRC_AI) { PlanBlockAi(s, L, si, w); return; }
    PlanBlockMine(s, L, si, w);
}

// `src` and the two flags: the station's music source from the start (SRC_AI = as always).
inline void Init(StationState& s, const Library& L, int si, uint64_t seed, float startOffset, const World& w,
                 uint8_t src = SRC_AI, bool genreLines = false, bool introOver = false)
{
    s = StationState();
    s.source = src; s.genreLines = genreLines; s.introOver = introOver;
    s.rng.s = seed * 0x9E3779B97F4A7C15ull + 0xD1B54A32D192ED03ull + static_cast<uint64_t>(si) * 0x632BE59BD9B4E019ull;
    if (!s.rng.s) s.rng.s = 1;
    s.adGap = s.rng.Range(L.stations[si].adGap[0], L.stations[si].adGap[1]);
    s.songsSinceAds = s.rng.Range(0, s.adGap);
    s.lastNews = -s.rng.Uniform() * L.stations[si].newsEvery * 60.0;
    // start the station mid-programme, like it has been on all along
    while (s.q.size() < 2) PlanBlock(s, L, si, w);
    const double target = startOffset;
    while (s.t < target)
    {
        const double step = target - s.t < 5.0 ? target - s.t : 5.0;
        s.t += step;
        while (!s.q.empty() && s.q[0].start + s.q[0].dur <= s.t) s.q.erase(s.q.begin());
        while (s.q.size() < 2) PlanBlock(s, L, si, w);
    }
}

// Station time moves on; finished events drop off; the plan stays at least one event ahead.
// A bulletin (open_breaking, the item, the getaway car, close) goes in right after what is airing;
// the rest of the plan moves back to make room, so nothing it counted (news, ads) is lost.
inline bool InsertBulletin(StationState& s, const Library& L, int kind, const World& w)
{
    if (s.q.empty()) { s.pendingNews = kind; return true; }
    int parts[4];
    const int n = Bulletin(s, L, kind, w, parts);
    if (n == 0) return false;
    const double from = s.q[0].start + s.q[0].dur;
    double at = from;
    std::vector<Event> ins;
    for (int i = 0; i < n; ++i)
    {
        Event e; e.clip = parts[i]; e.start = at; e.dur = L.clips[parts[i]].dur;
        ins.push_back(e); at += e.dur;
    }
    for (size_t i = 1; i < s.q.size(); ++i) s.q[i].start += at - from;
    s.q.insert(s.q.begin() + 1, ins.begin(), ins.end());
    return true;
}

// A wanted-mode event (a level up, or getting away). A station holds at most one bulletin in
// waiting and the latest news wins, so stepping 1 -> 5 makes one level-5 bulletin, not five
// (2026-10-24: five back to back, on every station, was far too many). Getting away is only news
// where the chase was reported; if nothing aired yet, there is nothing to report.
inline void WantedEvent(StationState& s, int kind)
{
    if (!s.carriesBreaking) return;
    if (kind == K_N_ESCAPED || kind == K_N_BUSTED)                   // the end of a chase
    {
        s.breakingPending = s.chaseReported ? kind : K_NONE;
        return;
    }
    s.breakingPending = kind;
}

// [missions1] The clip that reads mission `id` (kind news:mission or news:mission_recap) on station si: the
// station's own read first, then a shared one. -1 = none (no news for it, silently).
inline int MissionClip(const Library& L, int si, int kind, const std::string& id)
{
    if (id.empty() || kind < 0 || kind >= K_COUNT) return -1;
    if (si >= 0 && si < static_cast<int>(L.stations.size()))
        for (int i : L.stations[si].byKind[kind]) if (L.clips[i].mission == id) return i;
    for (int i : L.shared[kind]) if (L.clips[i].mission == id) return i;
    return -1;
}
inline bool HasMissionNews(const Library& L, const std::string& id)
{
    if (MissionClip(L, -1, K_N_MISSION, id) >= 0) return true;
    for (size_t s = 0; s < L.stations.size(); ++s) if (MissionClip(L, static_cast<int>(s), K_N_MISSION, id) >= 0) return true;
    return false;
}
// The station's next item is its news break (the host's hand-over, or the news open).
inline bool NewsBreakNext(const StationState& s, const Library& L)
{
    if (s.q.size() < 2 || s.q[1].clip < 0 || s.q[1].ext) return false;
    const uint8_t k = L.clips[s.q[1].clip].kind;
    return k == K_H_TO_NEWS || k == K_N_OPEN;
}
// The mission newscast goes in right after what airs now (never into it): the station host's hand-over to the
// news (if it has one), a special-report opener (news:mission_open, else news:open_breaking), the newscast,
// a news close. A `full` clip goes in alone. The rest of the plan moves back. Returns the newscast's start
// (station seconds), or -1 when nothing went in.
inline double InsertMission(StationState& s, const Library& L, int si, int clip, const World& w)
{
    if (s.q.empty() || clip < 0 || clip >= static_cast<int>(L.clips.size())) return -1.0;
    int parts[4]; int n = 0, at_i = 0;
    if (!L.clips[clip].full)
    {
        if (si >= 0 && si < static_cast<int>(L.stations.size()))
        {
            const int h = PickFrom(L, L.stations[si].byKind[K_H_TO_NEWS], s.recent[K_H_TO_NEWS], s.rng,
                                   [&](const Clip& c) { return TagsOk(c, w); });
            if (h >= 0) parts[n++] = h;
        }
        int o = PickFrom(L, L.shared[K_N_MISSION_OPEN], s.recentShared[K_N_MISSION_OPEN], s.rng, [](const Clip& c) { return !c.fill; });
        if (o < 0) o = PickNews(s, L, K_N_OPEN_BREAKING, w);
        if (o >= 0) parts[n++] = o;
    }
    at_i = n; parts[n++] = clip;
    if (!L.clips[clip].full) { const int c = PickNews(s, L, K_N_CLOSE, w); if (c >= 0) parts[n++] = c; }
    const double from = s.q[0].start + s.q[0].dur;
    double at = from, readAt = -1.0;
    std::vector<Event> ins;
    for (int i = 0; i < n; ++i)
    {
        Event e; e.clip = parts[i]; e.start = at; e.dur = L.clips[parts[i]].dur;
        if (i == at_i) readAt = at;
        ins.push_back(e); at += e.dur;
    }
    for (size_t i = 1; i < s.q.size(); ++i) s.q[i].start += at - from;
    s.q.insert(s.q.begin() + 1, ins.begin(), ins.end());
    return readAt;
}

// Where a bulletin may cut in: before a song or a host's own chat, not between an ad and its
// disclaimer, a song and its outro, a host's intro and its song, or inside the news.
inline bool CanCutIn(const StationState& s, const Library& L)
{
    if (s.q.size() < 2 || s.q[1].clip < 0 || s.q[1].ext) return true;
    switch (L.clips[s.q[1].clip].kind)
    {
    case K_SONG: case K_JINGLE: case K_SEGMENT: case K_CALL: case K_H_STATION_ID: case K_H_BETWEEN:
    case K_H_MORNING: case K_H_AFTERNOON: case K_H_EVENING: case K_H_NIGHT: case K_H_WEATHER:
    case K_H_TRAFFIC: case K_H_CALL: case K_TRACK:
        return true;
    default:
        return false;
    }
}

// Station time moves on; finished events drop off; the plan stays at least one event ahead. A
// waiting wanted bulletin goes in as the airing item ends, at a clean gap, no sooner than
// breakingGap after the station's last one.
inline void Advance(StationState& s, const Library& L, int si, double dt, const World& w)
{
    if (dt > 0.0) s.t += dt;
    if (s.breakingPending != K_NONE && !s.q.empty() && s.t - s.lastBreaking >= s.breakingGap &&
        s.q[0].start + s.q[0].dur <= s.t + 0.05 && CanCutIn(s, L))
    {
        const int kind = s.breakingPending;
        s.breakingPending = K_NONE;
        if (InsertBulletin(s, L, kind, w)) { s.lastBreaking = s.t; s.chaseReported = kind != K_N_ESCAPED && kind != K_N_BUSTED; }
    }
    while (!s.q.empty() && s.q[0].start + s.q[0].dur <= s.t) s.q.erase(s.q.begin());
    int guard = 0;
    while (s.q.size() < 2 && guard++ < 8) PlanBlock(s, L, si, w);
}

// ---- fitting the plan to the listener's music (the radio thread, on the tuned station) ----------
// q[i].start += delta for every i >= from.
inline void Shift(StationState& s, size_t from, double delta)
{
    for (size_t i = from; i < s.q.size(); ++i) s.q[i].start += delta;
}

// q[0] gets length nd; the rest of the plan follows it exactly (q[1] starts where q[0] now ends).
inline void SetHeadDur(StationState& s, float nd)
{
    if (s.q.empty()) return;
    s.q[0].dur = nd;
    if (s.q.size() > 1) Shift(s, 1, (s.q[0].start + static_cast<double>(nd)) - s.q[1].start);
}

// The app's track plays on: an airing app slot always reaches at least minAhead past the clock.
inline void Stretch(StationState& s, double minAhead = 1.0)
{
    if (s.q.empty() || !s.q[0].ext) return;
    const double want = s.t + minAhead - s.q[0].start;
    if (want <= s.q[0].dur) return;
    SetHeadDur(s, static_cast<float>(want));
}

// What airs ends now (an app track that ended, a file that failed or ran out, a source switch):
// the rest of the plan moves up, so the next item starts at the clock (never after it).
inline void CutAiring(StationState& s)
{
    if (s.q.empty()) return;
    double d = s.t - s.q[0].start;
    if (d < 0.0) d = 0.0;
    if (d >= s.q[0].dur) return;
    float nd = static_cast<float>(d);
    while (nd > 0.0f && s.q[0].start + static_cast<double>(nd) > s.t) nd = std::nextafter(nd, 0.0f);
    SetHeadDur(s, nd);
}

// The airing item turns out to be `d` long (a file's real length): the rest of the plan follows.
inline void SetAiringDur(StationState& s, double d)
{
    if (s.q.empty() || !(d > 0.0)) return;
    SetHeadDur(s, static_cast<float>(d));
}

// The listener pressed play in their app while the station talked: the talk ends now, whatever
// was planned before the next app slot is dropped (planning ahead for one if need be), and that
// slot starts at the clock. False when no slot comes (the station isn't on the app).
inline bool YieldToMusic(StationState& s, const Library& L, int si, const World& w)
{
    if (s.q.empty()) return false;
    if (s.q[0].ext) return true;
    size_t k = 0;
    for (int guard = 0; guard <= 8 && !k; ++guard)
    {
        for (size_t i = 1; i < s.q.size(); ++i) if (s.q[i].ext) { k = i; break; }
        if (!k && guard < 8) PlanBlock(s, L, si, w);
    }
    if (!k) return false;
    s.q.erase(s.q.begin() + 1, s.q.begin() + static_cast<long>(k));
    s.q[0].over = -1;
    double d = s.t - s.q[0].start;
    if (d < 0.0) d = 0.0;
    float nd = static_cast<float>(d);
    while (nd > 0.0f && s.q[0].start + static_cast<double>(nd) > s.t) nd = std::nextafter(nd, 0.0f);
    SetHeadDur(s, nd);
    return true;
}

// Whether the station has something of its own to air after what airs now: the next item is not
// an app slot, or a wanted bulletin is due to cut in.
inline bool TalkFollows(const StationState& s, const Library& L)
{
    if (s.breakingPending != K_NONE && s.t - s.lastBreaking >= s.breakingGap && CanCutIn(s, L)) return true;
    return s.q.size() >= 2 && !s.q[1].ext;
}

// A new music source for a running station. The plan is cut back to what airs now, and that ends
// at once when it belongs to the old source (a generated song or a host line naming one, an app
// slot, a library track): the next Advance plans the new programme from the clock. A library
// track that airs when the station goes to the app plays to its end (the app is paused meanwhile:
// it is not an app slot), and the app comes in from the next block. The ad, news, ID and bulletin
// counters carry on.
inline void SetSource(StationState& s, const Library& L, int si, uint8_t src, bool genreLines, bool introOver)
{
    (void)si;
    if (!s.q.empty())
    {
        s.q.resize(1);
        const Event& e = s.q[0];
        bool cut = false;
        if (e.ext) cut = src != SRC_APP;
        else if (e.clip >= 0)
        {
            const Clip& c = L.clips[e.clip];
            if (src != SRC_AI && (IsAiMusic(c) || c.song >= 0)) cut = true;
            if (c.kind == K_TRACK && src != SRC_LIB && src != SRC_APP) cut = true;
        }
        if (cut) { CutAiring(s); s.q[0].over = -1; }
    }
    s.source = src; s.genreLines = genreLines; s.introOver = introOver;
    s.lastSong = -1;
    s.talkAccum = 0.0;
    s.deck.clear(); s.deckPos = 0; s.deckGen = 0xFFFFFFFFu;
}

} // namespace radio
