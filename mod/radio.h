// =============================================================================================
//  radio.h - the in-game radio (Notes\35): plays radio_model.h's stations while you drive.
//
//  Its own thread, its own audio: Media Foundation decodes the MP3s, XAudio2 plays them (a
//  second XAudio2 engine next to the game's). The game thread only publishes facts - "the
//  player's vehicle just updated", the game hour, the wanted level - and the hotkey thread the
//  station steps. Nothing here touches game memory.
//
//    in a vehicle (and the game running)  -> the current station, from wherever it is right now
//    on foot, paused, loading            -> fades out; every station's clock keeps running while
//                                           the world does (the GTA trick)
//    a station change                    -> a burst of tuning static, then the new station
//    a host intro over a song's intro    -> the song ducks under the voice
//
//  The listener's music on every station (2026-11-13, spec v1): with `music` (or music_<id>) other than ai,
//  a station keeps its host, IDs, calls, ads and news, and its music comes from the listener:
//    library   their files, sorted onto the stations by genre (radio_genre.h, radio_library.h), or
//              a playlist_<id> folder / .m3u
//    service   their music app (radio_app.h, radio_app_model.h): it plays in the station's music
//              slots and is paused for everything else - the DJ never talks over it
//    talk      no music
//  The AI songs stay the default and one switch away (Ctrl+Alt+/ for the session).
// =============================================================================================
#pragma once
#include <objbase.h>
#include <propidl.h>
#include <xaudio2.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "ole32.lib")
#include <algorithm>
#include "radio_model.h"
#include "missions_model.h"              // [missions1] the mission-news queue (pure)
#include "radio_genre.h"
#include "radio_app_model.h"
#include "radio_library.h"
#include "radio_app.h"
#include "radiologo_model.h"   // [radiologo1]

namespace rd {

// ---- published by the other threads -----------------------------------------------------------
static volatile LONG g_vehSeen  = 0;      // GetTickCount: the player's vehicle last updated
static volatile LONG g_vehKind  = 0;      // 0 car, 1 motorbike, 2 helicopter / plane
static volatile LONG g_gameSeen = 0;      // GetTickCount: the world last ticked (population manager)
static volatile LONG g_hour100  = -1;     // game hour x 100, -1 = not known
static volatile LONG g_wanted   = 0;      // wanted level 0..5 (co-op: the higher of the two players')
static volatile LONG g_rain     = 0;      // 1 = it is raining where a player's camera is (weather.h Publish)
static volatile LONG g_levelNews = 0;     // (count << 8) | level: a player's level went up under the other's (wanted.h)
static volatile LONG g_busted   = 0;      // counts arrests (set before the level drops to 0)
static volatile LONG g_heliNews = 0;      // counts "a police helicopter has you in sight" (wanted_heli.h)
static volatile LONG g_step     = 0;      // station steps asked for (+ next, - previous)
static volatile LONG g_stop     = 0;
static volatile LONG g_cfgVer   = 0;      // bumped when the settings below change
static volatile LONG g_hold     = 0;      // a cutscene is playing (the game thread asks the game)
static volatile LONG g_newsHold = 0;      // [missions1] a mission runs (missions.h's gate): no mission flash now
static volatile LONG g_duck1000 = 1000;   // the game's own music duck x 1000: dialogue, jingles (Notes\37 §4)
static volatile LONG g_vol1000  = 1000;   // the game's Volume option x 1000
static volatile LONG g_audible  = 0;      // out: the radio is (or is fading) on - the game thread lowers
                                          // the game's music for it
static const volatile uint8_t* g_pauseByte = nullptr;   // the game's "paused" byte (static, safe to read here)
static volatile LONG g_district = -1;     // the game's district index (Notes\37 §5), -1 = not known
static volatile LONG g_vehClass = -1;     // kVehicleFill index of what the player drives, -1 = on foot
static volatile LONG g_aiOn     = 0;      // Ctrl+Alt+/: the AI songs on every station (this session)
static volatile LONG g_hasMine  = 0;      // the ini puts some station on something other than ai
static volatile LONG g_openLink = 0;      // Ctrl+Alt+;: open the tuned station's playlist
static volatile LONG g_releaseApp = 0;    // the game window is gone: give the music app back now
// [radiologo1] the station logo overlay (radiologo.h reads these on the game thread):
static volatile LONG g_logoSeq  = 0;      // out: bumps on every "show the logo" (a station step, getting in)
static volatile LONG g_logoTile = -1;     // out: the atlas tile of that station (rlm::TileFor)
static volatile LONG g_logoKind = 0;      // out: rlm::EV_STEP / EV_ENTER
static rlm::Tiles    g_logoTiles;         // Mods\RadioLogo\logos.txt, loaded by radiologo.h before Start()

// The game's 23 districts (LegoCityDistrictSystem order) as Radio\_shared\fills.json {district} ids;
// the bridges, the tunnel and the canal have no renders of their own (generic lines there).
// Index 23 is not the game's: Port Atlas, by the box of [sites] atlas_* (sitem::kDistrictPortAtlas).
static const char* const kDistrictFill[24] = {
    "cherry_tree_hills", "auburn", "fort_meadows", "bluebell", "", "", "", "liberty_island",
    "albatross_island", "apollo_island", "airport", "paradise_sands", "", "crescent_park", "pagoda",
    "downtown", "kings_court", "festival_square", "fresco", "bright_lights_plaza", "", "uptown", "",
    "port_atlas",
};

// fills.json {vehicle} ids, and the vehicle record names they cover (upper case; a trailing '_'
// is a prefix). The first match wins, so the specific names come before the families.
static const char* const kVehicleFill[] = {
    "police_bike", "armoured_truck", "helicopter", "taxi", "garbage_truck", "ambulance", "sports_car",
    "off_roader", "truck", "police_car", "muscle_car", "small_car", "family_car", "van", "farm_vehicle",
    "fire_engine", "motorbike", "airplane", "boat", "space_vehicle", "unusual",
};
struct VehRule { const char* name; int fill; };
static const VehRule kVehicleRules[] = {
    { "2016_POLICE_DASHER", 0 }, { "POLICE_BASTION", 1 }, { "POLICE_CHOPPER", 2 }, { "HOSPITAL_SWOOPER", 2 },
    { "CIVIC_TAXICAB", 3 }, { "CIVIC_TRASHER", 4 }, { "HOSPITAL_PANACEA", 5 }, { "CHANS_DRAKONAS", 6 },
    { "CHANS_IRONBACK", 7 }, { "2016_CIVIC_SLOGGER", 8 },
    { "POLICE_", 9 }, { "2016_POLICE_", 9 }, { "SPORTSCARS_", 6 }, { "2016_SPORTSCARS_", 6 },
    { "MUSCLECARS_", 10 }, { "SMALLCARS_", 11 }, { "SALOONCARS_", 12 }, { "SUV_", 7 }, { "2016_SUV_", 7 },
    { "VAN_", 13 }, { "2016_VAN_", 13 }, { "INDUSTRIAL_", 8 }, { "CIVIC_", 8 }, { "FARM_", 14 }, { "FIRE_", 15 },
    { "MOTORCYCLES_", 16 }, { "2016_MOTORCYCLES_", 16 }, { "AIRCRAFT_", 17 }, { "NAVAL_", 18 }, { "SPACE_", 19 },
};

// The model name -> a kVehicleFill index ("unusual" for anything else).
static int VehicleClass(const char* model)
{
    char up[64]; int i = 0;
    for (; model[i] && i < 63; ++i) up[i] = static_cast<char>(model[i] >= 'a' && model[i] <= 'z' ? model[i] - 32 : model[i]);
    up[i] = 0;
    for (const VehRule& r : kVehicleRules)
    {
        const size_t n = strlen(r.name);
        if (r.name[n - 1] == '_' ? strncmp(up, r.name, n) == 0 : strncmp(up, r.name, n) == 0 && (up[n] == 0 || up[n] == '_'))
            return r.fill;
    }
    return 20;
}

// One station's keys from [radio] (music_<id>, playlist_<id>, genre_<id>).
struct StationKeys
{
    char     id[48] = {};
    uint8_t  music = 0xFF;                // radio::Source; 0xFF = the global `music`
    char     playlist[780] = {};          // as written (UTF-8)
    uint8_t  form = radio::PL_NONE;       // radio::PlaylistForm of it
    wchar_t  list[MAX_PATH] = {};         // a folder / .m3u: absolute
    char     genre[256] = {};             // extra genre words
};

struct Settings
{
    int      enable = 1;
    float    volume = 0.8f;               // 0..1
    int      inHeli = 1;                  // helicopters and planes have a radio too
    uint32_t padNext = 0, padPrev = 0;    // XInput buttons (0 = none)
    float    duckDb = -9.0f;              // a song under a talk-over
    float    staticDb = -16.0f;           // the tuning static
    int      startStation = 0;            // 1-based; 0 = a random one
    char     manifest[MAX_PATH] = {};
    char     root[MAX_PATH] = {};         // the content folder; empty = the manifest's own folder
    char     fallback[MAX_PATH] = {};     // the provisional list, used until the manifest exists
    char     breaking[256] = "brick-talk,bright-lights";   // stations with wanted-mode news ("all" / "none")
    float    breakingGap = 240.0f;        // seconds between two wanted bulletins on one station
    // [missions1] the mission newscasts
    int      missionNews = 1;
    int      missionInterrupt = 1;         // 1 = at the next song boundary, 0 = at the next news break
    char     missionStations[256] = "all";
    float    missionKeepH = 6.0f, missionRecapChance = 0.35f, missionRecapH = 3.0f, missionRecapGapMin = 20.0f;
    int      missionRecapMax = 2, missionRecapReuse = 1, missionOnce = 0;
    char     missionState[MAX_PATH] = {}; // lcudrive_missionnews.txt beside the DLL (absolute)
    // the music (spec v1 section 8)
    uint8_t  music = radio::SRC_AI;       // the global source
    StationKeys stn[16];                  // per-station keys
    int      nStn = 0;
    wchar_t  libFolders[8][MAX_PATH] = {};   // library_folders (empty: the Windows Music library)
    int      nLibFolders = 0;
    int      libMin = 8;                  // library_min_tracks
    int      libShortAi = 0;              // library_short = ai
    char     libSkip[512] = "horrorcore, gangsta, drill";
    int      libHoliday = 0;              // 0 season, 1 always, 2 never
    char     libUnsorted[48] = "skip";    // skip | a station id
    int      libTalkover = 0;
    wchar_t  libCache[MAX_PATH] = {};     // absolute ("" = no cache)
    char     svcApp[128] = "auto";        // auto | any | part of an app id
    float    svcVolume = 0.8f;            // the app's level while the radio plays it (x its own; 0..1)
    int      svcVolCtl = 1;               // 0: never touch the app's volume
    float    svcLead = 0.6f;              // seconds before a track ends that the app pauses for the DJ
    int      svcMaxMin = 15;              // a track longer than this gets the DJ in anyway (0 = never)
    int      svcOpenTune = 0;             // tuning to a service station opens its playlist
};

static Settings g_setIn;                  // written by Configure() (another thread), under g_setLock
static SRWLOCK  g_setLock = SRWLOCK_INIT;
static Settings g_set;                    // the radio thread's copy, taken when g_cfgVer changes

// Which stations carry wanted-mode news, and how far apart (re-applied on every settings change).
static void ApplyBreaking(std::vector<radio::StationState>& st, const radio::Library& L)
{
    for (size_t i = 0; i < st.size() && i < L.stations.size(); ++i)
    {
        const std::string& id = L.stations[i].id;
        bool on = _stricmp(g_set.breaking, "all") == 0;
        if (!on && _stricmp(g_set.breaking, "none") != 0)
        {
            // a comma / space separated list of station ids
            const char* p = g_set.breaking;
            while (*p)
            {
                while (*p == ',' || *p == ' ' || *p == '\t') ++p;
                const char* e = p;
                while (*e && *e != ',' && *e != ' ' && *e != '\t') ++e;
                if (e > p && static_cast<size_t>(e - p) == id.size() && _strnicmp(p, id.c_str(), id.size()) == 0) on = true;
                p = e;
            }
        }
        st[i].carriesBreaking = on;
        st[i].breakingGap = g_set.breakingGap;
    }
}

static HANDLE g_thread = nullptr;
static bool   g_testMute = false;         // the playback test: everything runs, nothing is heard
static volatile LONG g_testOpened = 0;    // ... and counts what it opened and fed
static volatile LONG g_testBuffers = 0;
static volatile LONG g_testDurFixed = 0;  // ... and how often a file's real length replaced a guess
static volatile LONG g_testCuts = 0;      // ... and how often a file ran out early
static bool   g_testNoDurFix = false;     // the test: keep a file's guessed length (so it runs out early)

static bool AnyMine(const Settings& s)
{
    if (s.music != radio::SRC_AI) return true;
    for (int i = 0; i < s.nStn && i < 16; ++i) if (s.stn[i].music != 0xFF && s.stn[i].music != radio::SRC_AI) return true;
    return false;
}

static void Configure(const Settings& s)
{
    AcquireSRWLockExclusive(&g_setLock);
    g_setIn = s;
    ReleaseSRWLockExclusive(&g_setLock);
    InterlockedExchange(&g_hasMine, AnyMine(s) ? 1 : 0);
    InterlockedIncrement(&g_cfgVer);
}

// ---- the engine --------------------------------------------------------------------------------
typedef HRESULT(WINAPI* XACreateFn)(IXAudio2** out, UINT32 flags, XAUDIO2_PROCESSOR proc);

struct Stream
{
    IMFSourceReader*     reader = nullptr;
    IXAudio2SourceVoice* voice = nullptr;
    int    clip = -1;
    double eventStart = -1.0;            // which airing of the clip (the same clip can air twice)
    bool   eof = false;
    float  gain = 1.0f;                  // the clip's loudness gain, linear
    float  vol = 0.0f, target = 1.0f;    // fade, linear
    float  rate = 0.06f;                 // seconds for a full fade
    std::vector<uint8_t> bufs[4];
    int    next = 0;
    UINT32 blockAlign = 4, bytesPerSec = 176400;
    // the listener's files
    bool   autoGain = false;             // no loudness data: measured as it plays
    UINT32 bits = 16;
    double sumSq = 0.0, nSq = 0.0;       // ... over its first 20 s
    float  gainScale = 1.0f;
};

static IXAudio2*               g_xa = nullptr;
static IXAudio2MasteringVoice* g_master = nullptr;
static IXAudio2SubmixVoice*    g_bus = nullptr;
static IXAudio2SourceVoice*    g_fx = nullptr;          // the tuning static
static std::vector<int16_t>    g_static;
static std::wstring            g_root;                  // the folder the manifest is in

static std::wstring Widen(const std::string& s)
{
    if (s.empty()) return std::wstring();
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), &w[0], n);
    for (wchar_t& c : w) if (c == L'/') c = L'\\';
    return w;
}

// Guarded (/EHa: catch (...) also catches a fault): a reader that faults on its way out (a decoder
// that broke on a listener's file) is let go without being touched again.
static void Close(Stream& s)
{
    if (s.voice) { try { s.voice->DestroyVoice(); } catch (...) {} s.voice = nullptr; }
    if (s.reader) { try { s.reader->Release(); } catch (...) {} s.reader = nullptr; }
    s.clip = -1; s.eventStart = -1.0; s.eof = false; s.vol = 0.0f; s.next = 0;
    s.autoGain = false; s.sumSq = s.nSq = 0.0; s.gainScale = 1.0f;
}

// After a fault inside the decoder: the voice goes, the reader is left as it is (leaked, never
// touched again).
static void Drop(Stream& s)
{
    if (s.voice) { try { s.voice->DestroyVoice(); } catch (...) {} s.voice = nullptr; }
    s.reader = nullptr;
    s.clip = -1; s.eventStart = -1.0; s.eof = false; s.vol = 0.0f; s.next = 0;
    s.autoGain = false; s.sumSq = s.nSq = 0.0; s.gainScale = 1.0f;
}

// `name`: for the log. A station clip goes through Open(); a listener's file comes here directly.
// `quiet`: no log line on a failure (the caller writes its own).
static bool OpenPath(Stream& s, const std::wstring& path, const char* name, float gainDb, int clipIdx, double eventStart, double offset,
                     HRESULT* why = nullptr)
{
    Close(s);
    IMFSourceReader* r = nullptr;
    HRESULT hr = MFCreateSourceReaderFromURL(path.c_str(), nullptr, &r);
    if (why) *why = hr;
    if (FAILED(hr)) { if (!why) Log("!!! radio: can't open %s (0x%08lX)\n", name, hr); return false; }
    r->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_ALL_STREAMS), FALSE);
    r->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM), TRUE);
    IMFMediaType* want = nullptr;
    MFCreateMediaType(&want);
    want->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    want->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
    want->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
    hr = r->SetCurrentMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM), nullptr, want);
    want->Release();
    IMFMediaType* got = nullptr;
    if (SUCCEEDED(hr)) hr = r->GetCurrentMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM), &got);
    WAVEFORMATEX* wfx = nullptr; UINT32 wfxSize = 0;
    if (SUCCEEDED(hr)) hr = MFCreateWaveFormatExFromMFMediaType(got, &wfx, &wfxSize);
    if (got) got->Release();
    if (FAILED(hr) || !wfx) { if (why) *why = FAILED(hr) ? hr : E_FAIL; Log("!!! radio: %s: no PCM decode (0x%08lX)\n", name, hr); r->Release(); return false; }
    if (offset > 0.05)
    {
        PROPVARIANT pv; PropVariantInit(&pv);
        pv.vt = VT_I8; pv.hVal.QuadPart = static_cast<LONGLONG>(offset * 1e7);
        r->SetCurrentPosition(GUID_NULL, pv);
        PropVariantClear(&pv);
    }
    XAUDIO2_SEND_DESCRIPTOR sd = { 0, g_bus };
    XAUDIO2_VOICE_SENDS sends = { 1, &sd };
    IXAudio2SourceVoice* v = nullptr;
    hr = g_xa->CreateSourceVoice(&v, wfx, 0, XAUDIO2_DEFAULT_FREQ_RATIO, nullptr, &sends, nullptr);
    s.blockAlign = wfx->nBlockAlign ? wfx->nBlockAlign : 4;
    s.bytesPerSec = wfx->nAvgBytesPerSec ? wfx->nAvgBytesPerSec : 176400;
    s.bits = wfx->wBitsPerSample;
    CoTaskMemFree(wfx);
    if (FAILED(hr) || !v) { if (why) *why = FAILED(hr) ? hr : E_FAIL; Log("!!! radio: no voice for %s (0x%08lX)\n", name, hr); r->Release(); return false; }
    v->SetVolume(0.0f);
    v->Start(0);
    InterlockedIncrement(&g_testOpened);
    s.reader = r; s.voice = v; s.clip = clipIdx; s.eventStart = eventStart;
    s.eof = false; s.next = 0; s.vol = 0.0f; s.target = 1.0f; s.rate = 0.06f;
    s.gain = std::pow(10.0f, gainDb / 20.0f);
    return true;
}

static bool Open(Stream& s, const radio::Clip& c, int clipIdx, double eventStart, double offset)
{
    return OpenPath(s, g_root + Widen(c.file), c.file.c_str(), c.gainDb, clipIdx, eventStart, offset);
}

// Keep three buffers of ~0.2 s queued (four slots: the one refilled is never in use).
static void Pump(Stream& s)
{
    if (!s.voice || !s.reader) return;
    XAUDIO2_VOICE_STATE st;
    s.voice->GetState(&st, XAUDIO2_VOICE_NOSAMPLESPLAYED);
    UINT32 queued = st.BuffersQueued;
    while (queued < 3 && !s.eof)
    {
        std::vector<uint8_t>& b = s.bufs[s.next];
        b.clear();
        const size_t want = s.bytesPerSec / 5;
        while (b.size() < want)
        {
            DWORD flags = 0; LONGLONG ts = 0; IMFSample* smp = nullptr;
            const HRESULT hr = s.reader->ReadSample(static_cast<DWORD>(MF_SOURCE_READER_FIRST_AUDIO_STREAM), 0, nullptr, &flags, &ts, &smp);
            if (FAILED(hr) || (flags & MF_SOURCE_READERF_ENDOFSTREAM) || (flags & MF_SOURCE_READERF_ERROR)) s.eof = true;
            if (smp)
            {
                IMFMediaBuffer* mb = nullptr;
                if (SUCCEEDED(smp->ConvertToContiguousBuffer(&mb)) && mb)
                {
                    BYTE* p = nullptr; DWORD len = 0;
                    if (SUCCEEDED(mb->Lock(&p, nullptr, &len)) && p) { b.insert(b.end(), p, p + len); mb->Unlock(); }
                    mb->Release();
                }
                smp->Release();
            }
            if (s.eof) break;
        }
        b.resize(b.size() - b.size() % s.blockAlign);
        if (s.autoGain && s.bits == 16 && s.nSq < s.bytesPerSec * 10.0)        // the first 20 s of a listener's file
        {
            const int16_t* p = reinterpret_cast<const int16_t*>(b.data());
            const size_t n = b.size() / 2;
            double acc = 0.0;
            for (size_t i = 0; i < n; ++i) acc += static_cast<double>(p[i]) * p[i];
            s.sumSq += acc; s.nSq += static_cast<double>(n);
        }
        if (b.empty()) { if (s.eof) { s.voice->Discontinuity(); } break; }
        XAUDIO2_BUFFER xb = {};
        xb.AudioBytes = static_cast<UINT32>(b.size());
        xb.pAudioData = b.data();
        if (s.eof) xb.Flags = XAUDIO2_END_OF_STREAM;
        if (FAILED(s.voice->SubmitSourceBuffer(&xb))) { s.eof = true; break; }
        InterlockedIncrement(&g_testBuffers);
        s.next = (s.next + 1) & 3;
        ++queued;
    }
}

static bool Finished(Stream& s)
{
    if (!s.voice) return true;
    if (!s.eof) return false;
    XAUDIO2_VOICE_STATE st;
    s.voice->GetState(&st, XAUDIO2_VOICE_NOSAMPLESPLAYED);
    return st.BuffersQueued == 0;
}

static void Fade(Stream& s, float dt)
{
    if (!s.voice) return;
    const float step = dt / (s.rate > 0.005f ? s.rate : 0.005f);
    if (s.vol < s.target) s.vol = s.vol + step > s.target ? s.target : s.vol + step;
    else if (s.vol > s.target) s.vol = s.vol - step < s.target ? s.target : s.vol - step;
    if (s.autoGain && s.nSq > s.bytesPerSec * 0.5)
    {
        // A listener's file has no loudness data: it starts at -6 dB and glides (1.5 dB/s at most)
        // toward about -19 dBFS RMS, the stations' -16 LUFS, from what has played so far (-14..+3 dB).
        const double rms = std::sqrt(s.sumSq / s.nSq) / 32768.0;
        float wantDb = -19.0f - 20.0f * std::log10(static_cast<float>(rms > 1e-4 ? rms : 1e-4));
        wantDb = wantDb < -14.0f ? -14.0f : (wantDb > 3.0f ? 3.0f : wantDb);
        const float want = std::pow(10.0f, wantDb / 20.0f), glide = std::pow(10.0f, 1.5f * dt / 20.0f);
        s.gain = s.gain < want ? std::fmin(want, s.gain * glide) : std::fmax(want, s.gain / glide);
    }
    s.voice->SetVolume(s.vol * s.gain * s.gainScale);
}

// ---- the tuning static: 0.35 s of band-limited noise with a quick swell and tail ------------------
static inline float Clamp16(float v) { return v < -32767.0f ? -32767.0f : (v > 32767.0f ? 32767.0f : v); }

static void MakeStatic()
{
    const int rate = 44100, n = rate * 35 / 100;
    g_static.resize(static_cast<size_t>(n));
    uint32_t x = 0x1234567u;
    float lp = 0.0f, hp = 0.0f, prev = 0.0f;
    for (int i = 0; i < n; ++i)
    {
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;
        const float white = (static_cast<int>(x & 0xFFFF) - 32768) / 32768.0f;
        lp += 0.35f * (white - lp);                       // soften the hiss
        hp = 0.97f * (hp + lp - prev); prev = lp;          // drop the rumble
        const float t = static_cast<float>(i) / n;
        float env = t < 0.08f ? t / 0.08f : (t > 0.7f ? (1.0f - t) / 0.3f : 1.0f);
        const float warble = 0.75f + 0.25f * std::sin(t * 60.0f);
        g_static[static_cast<size_t>(i)] = static_cast<int16_t>(Clamp16(hp * env * warble * 26000.0f));
    }
}

static void PlayStatic()
{
    if (!g_fx || g_static.empty()) return;
    g_fx->Stop(0);
    g_fx->FlushSourceBuffers();
    XAUDIO2_BUFFER xb = {};
    xb.AudioBytes = static_cast<UINT32>(g_static.size() * sizeof(int16_t));
    xb.pAudioData = reinterpret_cast<const BYTE*>(g_static.data());
    xb.Flags = XAUDIO2_END_OF_STREAM;
    g_fx->SubmitSourceBuffer(&xb);
    g_fx->SetVolume(std::pow(10.0f, g_set.staticDb / 20.0f));
    g_fx->Start(0);
}

static bool StartEngine()
{
    HMODULE m = LoadLibraryExW(L"xaudio2_9.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    XACreateFn create = m ? reinterpret_cast<XACreateFn>(GetProcAddress(m, "XAudio2Create")) : nullptr;
    if (!create) { Log("!!! radio: XAudio2 2.9 not available - no radio\n"); return false; }
    HRESULT hr = create(&g_xa, 0, XAUDIO2_DEFAULT_PROCESSOR);
    if (SUCCEEDED(hr)) hr = g_xa->CreateMasteringVoice(&g_master);
    if (FAILED(hr) || !g_master) { Log("!!! radio: no audio device (0x%08lX) - no radio\n", hr); return false; }
    if (g_testMute) g_master->SetVolume(0.0f);
    XAUDIO2_VOICE_DETAILS d = {};
    g_master->GetVoiceDetails(&d);
    hr = g_xa->CreateSubmixVoice(&g_bus, d.InputChannels >= 2 ? 2 : d.InputChannels, d.InputSampleRate);
    if (FAILED(hr) || !g_bus) { Log("!!! radio: no mix bus (0x%08lX) - no radio\n", hr); return false; }
    g_bus->SetVolume(0.0f);
    MakeStatic();
    WAVEFORMATEX f = {};
    f.wFormatTag = WAVE_FORMAT_PCM; f.nChannels = 1; f.nSamplesPerSec = 44100; f.wBitsPerSample = 16;
    f.nBlockAlign = 2; f.nAvgBytesPerSec = 88200;
    g_xa->CreateSourceVoice(&g_fx, &f);              // straight to the master: not ducked by the fade
    Log(">>> radio: audio up (%u Hz, %u channels)\n", d.InputSampleRate, d.InputChannels);
    return true;
}

static void StopEngine()
{
    if (g_fx) { g_fx->DestroyVoice(); g_fx = nullptr; }
    if (g_bus) { g_bus->DestroyVoice(); g_bus = nullptr; }
    if (g_master) { g_master->DestroyVoice(); g_master = nullptr; }
    if (g_xa) { g_xa->Release(); g_xa = nullptr; }
}

// ---- the library -------------------------------------------------------------------------------
static radio::Library            g_lib;              // the content + the listener's tracks (appended)
static radio::Library            g_libBase;          // the content as parsed
static std::vector<radio::StationState> g_st;
static int                       g_station = 0;      // -1 = off
static uint64_t                  g_seed = 0;

static bool FileExists(const char* path)
{
    const DWORD a = GetFileAttributesA(path);
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

// `rootOverride`: the content folder when the list lives elsewhere (the provisional list).
static bool LoadLibraryFile(const char* path, const char* rootOverride = nullptr)
{
    FILE* f = nullptr;
    if (fopen_s(&f, path, "rb") != 0 || !f) { Log("    radio: no station list at %s\n", path); return false; }
    std::string text;
    char chunk[65536];
    size_t n;
    while ((n = fread(chunk, 1, sizeof(chunk), f)) > 0) text.append(chunk, n);
    fclose(f);
    std::string err;
    radio::Library lib;
    if (!radio::Parse(text.data(), text.size(), lib, err, !AnyMine(g_set))) { Log("!!! radio: %s: %s - no radio\n", path, err.c_str()); return false; }
    radio::ApplyStationGenres(lib);
    g_libBase = lib;
    g_lib = lib;
    const char* root = (rootOverride && rootOverride[0]) ? rootOverride : g_set.root;
    std::string dir(root[0] ? root : path);
    if (root[0]) { if (dir.back() != '\\' && dir.back() != '/') dir += '\\'; }
    else
    {
        const size_t slash = dir.find_last_of("\\/");
        dir = slash == std::string::npos ? std::string() : dir.substr(0, slash + 1);
    }
    g_root = Widen(dir);
    int clips = static_cast<int>(g_lib.clips.size());
    Log(">>> radio: %d stations, %d clips (%d ads, %d news), %d lines skipped, from %s\n",
        static_cast<int>(g_lib.stations.size()), clips, static_cast<int>(g_lib.shared[radio::K_AD].size()),
        static_cast<int>(g_lib.shared[radio::K_N_STORY].size() + g_lib.shared[radio::K_N_OPEN].size()),
        g_lib.warnings, path);
    return true;
}

static radio::World WorldNow()
{
    radio::World w;
    const LONG h = g_hour100;
    if (h >= 0) { w.hour = h / 100.0f; w.hourRate = 24.0f / 480.0f; }       // the day/night mod's clock
    else { SYSTEMTIME t; GetLocalTime(&t); w.hour = t.wHour + t.wMinute / 60.0f; w.hourRate = 1.0f / 3600.0f; }
    w.wanted = static_cast<int>(g_wanted);
    w.rain = g_rain != 0;                                                    // the "rain" / "clear" tagged lines (weather.h)
    const LONG d = g_district;                // the list's own `D` map when it has one, else the built-in
    if (d >= 0 && d < 32 && g_lib.hasDistricts) strncpy_s(w.district, g_lib.district[d].c_str(), _TRUNCATE);
    else if (d >= 0 && d < 24) strcpy_s(w.district, kDistrictFill[d]);
    const LONG v = g_vehClass;
    if (v >= 0 && v < static_cast<LONG>(sizeof(kVehicleFill) / sizeof(kVehicleFill[0]))) strcpy_s(w.vehicle, kVehicleFill[v]);
    return w;
}

static const char* StationName(int i)
{
    return (i >= 0 && i < static_cast<int>(g_lib.stations.size())) ? g_lib.stations[i].name.c_str() : "off";
}

// [radiologo1] Ask the game thread for the logo of station i (-1 = the "radio off" tile).
static void ShowLogo(int i, LONG kind)
{
    const bool st = i >= 0 && i < static_cast<int>(g_lib.stations.size());
    g_logoTile = rlm::TileFor(g_logoTiles, st ? g_lib.stations[i].id : std::string());
    g_logoKind = kind;
    InterlockedIncrement(&g_logoSeq);
}

// ---- the music sources ----------------------------------------------------------------------------
struct StationMusic
{
    radio::SourceOut res;                 // what the station plays now (ResolveSource)
    bool     resolved = false;
    std::vector<radio::Track> genre, over;   // the last scan's tracks for it
    bool     overRead = false;
    uint32_t poolStamp = 0xFFFFFFFFu;     // which list is in its pool (scan gen x 2 + override)
    std::string lastNote;                 // the last log line about it
};

static std::vector<StationMusic> g_mus;
static bool          g_haveScan = false;   // a scan result for this station list arrived
static uint32_t      g_scanGen = 0, g_scanTaken = 0;
static int           g_trackFails = 0;     // library files that would not open, in a row
static app::Smtc*        g_smtc = nullptr;
static app::AppVolume*   g_vol = nullptr;
static radio::AppCtl     g_ctl;
static bool          g_appBound = false;   // an app session bound (debounced 5 s)
static uint32_t      g_boundSeen = 0;      // tick: last seen bound
static bool          g_appFinal = false;   // the music app has been given back for good

static const StationKeys* KeysFor(const std::string& id)
{
    for (int i = 0; i < g_set.nStn && i < 16; ++i) if (_stricmp(g_set.stn[i].id, id.c_str()) == 0) return &g_set.stn[i];
    return nullptr;
}

// [radiologo1] Which stations the dial stops at: those with songs or segments, or with the listener's music
// (library / app). A station in the list with neither (a half-made one) is skipped, never tuned to silence.
static std::vector<uint8_t> DialOk()
{
    std::vector<uint8_t> ok(g_lib.stations.size(), 0);
    for (size_t i = 0; i < ok.size(); ++i)
    {
        const radio::Station& s = g_lib.stations[i];
        bool o = !s.byKind[radio::K_SONG].empty() || !s.byKind[radio::K_SEGMENT].empty();
        if (!o && i < g_mus.size() && g_mus[i].resolved)
            o = g_mus[i].res.src == radio::SRC_LIB || g_mus[i].res.src == radio::SRC_APP;
        ok[i] = o ? 1 : 0;
    }
    return ok;
}

static uint8_t WantFor(int si)
{
    const StationKeys* k = KeysFor(g_lib.stations[si].id);
    return (k && k->music != 0xFF) ? k->music : g_set.music;
}

// The library matters when a station wants it, or wants the app (its fallback), or has a file playlist.
static bool NeedLibrary()
{
    if (!AnyMine(g_set) || g_aiOn) return false;
    for (size_t i = 0; i < g_lib.stations.size(); ++i)
    {
        const uint8_t want = WantFor(static_cast<int>(i));
        if (want == radio::SRC_AI) continue;
        const StationKeys* k = KeysFor(g_lib.stations[i].id);
        if (want == radio::SRC_LIB || want == radio::SRC_APP || (k && (k->form == radio::PL_FOLDER || k->form == radio::PL_M3U))) return true;
    }
    return false;
}

static bool NeedService()
{
    if (g_aiOn || app::g_appOff || g_appFinal) return false;
    for (size_t i = 0; i < g_lib.stations.size(); ++i)
    {
        const uint8_t want = WantFor(static_cast<int>(i));
        if (want == radio::SRC_AI) continue;
        const StationKeys* k = KeysFor(g_lib.stations[i].id);
        if (want == radio::SRC_APP || (k && (k->form == radio::PL_SPOTIFY || k->form == radio::PL_WEB))) return true;
    }
    return false;
}

// The station is meant to play the music app (whether or not the app is there right now): what
// Ctrl+Alt+; and service_open_tune go by.
static bool ServiceWanted(int si)
{
    if (g_aiOn || si < 0 || si >= static_cast<int>(g_lib.stations.size())) return false;
    const uint8_t want = WantFor(si);
    if (want == radio::SRC_AI) return false;
    const StationKeys* k = KeysFor(g_lib.stations[si].id);
    if (k && (k->form == radio::PL_FOLDER || k->form == radio::PL_M3U)) return false;
    if (k && (k->form == radio::PL_SPOTIFY || k->form == radio::PL_WEB)) return true;
    return want == radio::SRC_APP && g_lib.stations[si].genre != radio::G_TALK;
}

// A playlist link for the log: Spotify's canonical form, a web link without its query or fragment
// (share links carry a ?si= id tied to the account that shared them).
static std::string LinkForLog(const std::string& value)
{
    std::string canon;
    const uint8_t f = radio::PlaylistForm(value, &canon);
    if (f == radio::PL_SPOTIFY) return canon;
    std::string v = f == radio::PL_WEB ? canon : value;
    const size_t cut = v.find_first_of("?#");
    if (f == radio::PL_WEB && cut != std::string::npos) v.resize(cut);
    return v;
}

static int GenreFromId(const char* id)
{
    if (!id || !id[0] || _stricmp(id, "skip") == 0) return -1;
    const uint8_t g = radio::StationGenre(id);
    return g < 8 ? g : -1;
}

static radio::SourceIn SourceInFor(int si)
{
    const radio::Station& st = g_lib.stations[si];
    const StationKeys* k = KeysFor(st.id);
    radio::SourceIn in;
    in.want = WantFor(si);
    in.aiToggle = g_aiOn != 0;
    in.hasAi = !g_libBase.stations[si].byKind[radio::K_SONG].empty() || !g_libBase.stations[si].byKind[radio::K_SEGMENT].empty();
    in.form = k ? k->form : radio::PL_NONE;
    in.genre = st.genre;
    in.genrePool = si < static_cast<int>(g_mus.size()) ? static_cast<int>(g_mus[si].genre.size()) : 0;
    in.overridePool = si < static_cast<int>(g_mus.size()) ? static_cast<int>(g_mus[si].over.size()) : 0;
    in.libMin = g_set.libMin;
    in.shortAi = g_set.libShortAi != 0;
    in.libOff = lib::g_libOff != 0;
    in.scanPending = !g_haveScan && !lib::g_libOff;
    in.appOff = app::g_appOff != 0 || g_appFinal;        // (given back for good: the game window closed)
    in.appBound = g_appBound;
    in.talkover = g_set.libTalkover != 0;
    return in;
}

// The library pool behind a station's source (appended to g_lib once per scan and list).
static void ApplyPool(int si, const radio::SourceOut& o)
{
    if (o.src != radio::SRC_LIB || si >= static_cast<int>(g_mus.size())) return;
    StationMusic& m = g_mus[si];
    const uint32_t stamp = g_scanTaken * 2u + (o.overridePool ? 1u : 0u);
    if (stamp == m.poolStamp) return;
    bool capped = false;
    const std::vector<int> idx = radio::AppendTracks(g_lib, si, o.overridePool ? m.over : m.genre, &capped);
    radio::SetPool(g_lib, si, idx);
    m.poolStamp = stamp;
    if (capped) Log("!!! radio: %s: the radio holds %d of your tracks already - the rest wait for a restart of the game\n", StationName(si), g_lib.trackClips);
}

static std::string SourceNote(int si, const radio::SourceOut& o, const radio::SourceIn& in)
{
    const radio::Station& st = g_lib.stations[si];
    char b[512];
    switch (o.src)
    {
    case radio::SRC_AI:
        if (o.why == radio::SW_LIB_SHORT_AI || o.why == radio::SW_LIB_OFF)
            _snprintf_s(b, sizeof(b), _TRUNCATE, "%s: %d of your tracks fit (need %d) - the AI songs", st.name.c_str(), in.genrePool, in.libMin);
        else _snprintf_s(b, sizeof(b), _TRUNCATE, "%s plays the AI songs%s", st.name.c_str(), o.why == radio::SW_AI_TOGGLE ? " (Ctrl+Alt+/)" : "");
        break;
    case radio::SRC_LIB:
        if (o.overridePool) _snprintf_s(b, sizeof(b), _TRUNCATE, "%s plays your music (playlist_%s: %d tracks)", st.name.c_str(), st.id.c_str(), in.overridePool);
        else _snprintf_s(b, sizeof(b), _TRUNCATE, "%s plays your music (%d %s tracks)%s", st.name.c_str(), in.genrePool, radio::GenreName(st.genre),
                         o.why == radio::SW_APP_OFF_LIB ? " - your music app isn't there" : "");
        break;
    case radio::SRC_APP:
        _snprintf_s(b, sizeof(b), _TRUNCATE, "%s plays your music app", st.name.c_str());
        break;
    default:
        if (o.why == radio::SW_LIB_SHORT_TALK)
            _snprintf_s(b, sizeof(b), _TRUNCATE, "%s: %d of your tracks fit (need %d) - talk only. Add words (genre_%s = ...) or a playlist (playlist_%s = ...)",
                        st.name.c_str(), in.genrePool, in.libMin, st.id.c_str(), st.id.c_str());
        else if (o.why == radio::SW_SCAN) _snprintf_s(b, sizeof(b), _TRUNCATE, "%s: talk only until the library scan is done", st.name.c_str());
        else if (o.why == radio::SW_APP_OFF_TALK) _snprintf_s(b, sizeof(b), _TRUNCATE, "%s: talk only until your music app shows up", st.name.c_str());
        else if (o.why == radio::SW_LIB_OFF) _snprintf_s(b, sizeof(b), _TRUNCATE, "%s: talk only (the library is off this session)", st.name.c_str());
        else if (o.why == radio::SW_TALK_STATION) _snprintf_s(b, sizeof(b), _TRUNCATE, "%s: talk only (a talk station; give it a playlist_%s for music)", st.name.c_str(), st.id.c_str());
        else if (o.why == radio::SW_AI_NONE) _snprintf_s(b, sizeof(b), _TRUNCATE, "%s: talk only (no AI songs in this build)", st.name.c_str());
        else _snprintf_s(b, sizeof(b), _TRUNCATE, "%s: talk only", st.name.c_str());
        break;
    }
    return b;
}

// Every station's music source, from the settings, the library, the app and the toggle. SetSource
// only where it changes; `init`: before InitStations (no plan yet).
static void ResolveSources(bool init)
{
    const int n = static_cast<int>(g_lib.stations.size());
    if (static_cast<int>(g_mus.size()) != n) g_mus.resize(static_cast<size_t>(n));
    const radio::World w = WorldNow();
    for (int i = 0; i < n; ++i)
    {
        const radio::SourceIn in = SourceInFor(i);
        const radio::SourceOut o = radio::ResolveSource(in);
        StationMusic& m = g_mus[i];
        const bool changed = !m.resolved || o.src != m.res.src || o.genreLines != m.res.genreLines || o.introOver != m.res.introOver ||
                             o.overridePool != m.res.overridePool;
        ApplyPool(i, o);
        if (!init && i < static_cast<int>(g_st.size()))
        {
            if (changed) radio::SetSource(g_st[i], g_lib, i, o.src, o.genreLines, o.introOver);
        }
        m.res = o; m.resolved = true;
        const std::string note = SourceNote(i, o, in);
        if (note != m.lastNote)
        {
            m.lastNote = note;
            if (AnyMine(g_set) || g_aiOn || o.src != radio::SRC_AI) Log(">>> radio: %s\n", note.c_str());
        }
    }
}

static void InitStations()
{
    g_st.assign(g_lib.stations.size(), radio::StationState());
    ResolveSources(true);
    const radio::World w = WorldNow();
    for (size_t i = 0; i < g_st.size(); ++i)
    {
        radio::Rng r; r.s = g_seed + i * 7919u + 1;
        const radio::SourceOut& o = g_mus[i].res;
        radio::Init(g_st[i], g_lib, static_cast<int>(i), g_seed, 30.0f + r.Uniform() * 900.0f, w, o.src, o.genreLines, o.introOver);
    }
}

static void PostScan()
{
    lib::ScanJob* job = new (std::nothrow) lib::ScanJob();
    if (!job) return;
    job->gen = ++g_scanGen;
    for (int i = 0; i < g_set.nLibFolders && i < 8; ++i) if (g_set.libFolders[i][0]) job->roots.push_back(g_set.libFolders[i]);
    for (const radio::Station& st : g_lib.stations)
    {
        lib::StationJob sj;
        sj.id = st.id; sj.genre = st.genre;
        if (const StationKeys* k = KeysFor(st.id))
        {
            sj.extra = k->genre;
            if (k->form == radio::PL_FOLDER || k->form == radio::PL_M3U) { sj.form = k->form; sj.list = k->list; }
        }
        job->stations.push_back(sj);
    }
    job->skip = g_set.libSkip;
    job->holiday = g_set.libHoliday;
    SYSTEMTIME t; GetLocalTime(&t); job->month = t.wMonth;
    job->unsorted = GenreFromId(g_set.libUnsorted);
    job->cache = g_set.libCache;
    lib::PostScanJob(job);
}

static void TakeScan(lib::ScanResult* r)
{
    if (!r) return;
    if (r->gen != g_scanGen || r->st.size() != g_lib.stations.size()) { delete r; return; }   // superseded
    if (r->fault)
    {
        Log("!!! EXCEPTION in the radio's library scan - your library is off for this session; library stations %s\n",
            g_set.libShortAi ? "play the AI songs" : "talk only");
        delete r;
        return;
    }
    g_scanTaken = r->gen;
    g_haveScan = true;
    for (size_t i = 0; i < r->st.size() && i < g_mus.size(); ++i)
    {
        g_mus[i].genre.swap(r->st[i].genre);
        g_mus[i].over.swap(r->st[i].over);
        g_mus[i].overRead = r->st[i].overRead;
    }
    Log(">>> radio: library scan: %d files in %s (%d from the cache, %d read) in %.1f s\n", r->files, r->rootsNote.c_str(), r->cached, r->read, r->seconds);
    char b[600];
    _snprintf_s(b, sizeof(b), _TRUNCATE, "%s %d, %s %d, %s %d, %s %d, %s %d, %s %d, %s %d, %s %d | unsorted %d, never aired %d, skip words %d, "
                "holiday out of season %d, unplayable %d%s%s%s, DRM %d, cloud-only %d",
                radio::GenreName(0), r->perGenre[0], radio::GenreName(1), r->perGenre[1], radio::GenreName(2), r->perGenre[2],
                radio::GenreName(3), r->perGenre[3], radio::GenreName(4), r->perGenre[4], radio::GenreName(5), r->perGenre[5],
                radio::GenreName(6), r->perGenre[6], radio::GenreName(7), r->perGenre[7], r->unsorted, r->never, r->skipWords,
                r->holidayOut, r->unplayable, r->unplayableExt.empty() ? "" : " (", r->unplayableExt.c_str(), r->unplayableExt.empty() ? "" : ")",
                r->drm, r->cloud);
    Log("    radio: library: %s\n", b);
    for (const std::string& s : r->notes) Log("    %s\n", s.c_str());
    for (const std::string& s : r->warnings) Log("!!! %s\n", s.c_str());
    delete r;
}

// Ctrl+Alt+; (or service_open_tune): the tuned station's playlist link, else - on a station meant to
// play the app, whether the app is there yet or not - its built-in Spotify one, when the app is (or
// will be) Spotify: service_app names Spotify, or auto / any with nothing bound yet or Spotify bound.
// Another app bound under auto / any gets no Spotify link opened on top of it.
static void OpenTunedLink(bool fromKey)
{
    if (g_station < 0 || g_station >= static_cast<int>(g_lib.stations.size())) { if (fromKey) Log("    radio: Ctrl+Alt+;: the radio is off\n"); return; }
    const radio::Station& st = g_lib.stations[g_station];
    const StationKeys* k = KeysFor(st.id);
    std::string link;
    if (k && (k->form == radio::PL_SPOTIFY || k->form == radio::PL_WEB)) link = k->playlist;
    else if (k && k->form == radio::PL_BAD) { Log("!!! radio: playlist_%s: not a link the radio opens (only spotify: and https://)\n", st.id.c_str()); return; }
    else if (ServiceWanted(g_station))
    {
        const bool bound = g_smtc && !g_smtc->aumid.empty();
        const bool boundSpotify = bound && app::IContains(g_smtc->aumid, L"spotify");
        const bool autoOrAny = !g_set.svcApp[0] || _stricmp(g_set.svcApp, "auto") == 0 || _stricmp(g_set.svcApp, "any") == 0;
        const bool spotify = radio::ContainsNoCase(g_set.svcApp, "spotify") || (autoOrAny && (!bound || boundSpotify));
        const radio::DefaultLink* d = radio::DefaultLinkFor(st.id);
        if (spotify && d) link = d->uri;
        else if (d && fromKey)
        {
            Log("    radio: Ctrl+Alt+;: %s's built-in playlist is a Spotify one and your music app is %s - give it a playlist_%s link for that app\n",
                st.name.c_str(), bound ? app::Narrow(g_smtc->aumid).c_str() : g_set.svcApp, st.id.c_str());
            return;
        }
    }
    if (link.empty()) { if (fromKey) Log("    radio: Ctrl+Alt+;: %s has no playlist link to open\n", st.name.c_str()); return; }
    if (app::LaunchLink(link)) Log(">>> radio: Ctrl+Alt+;: opening %s's playlist in your music app (%s) - press play there\n", st.name.c_str(), LinkForLog(link).c_str());
    else Log("!!! radio: playlist_%s: not a link the radio opens (only spotify: and https://)\n", st.id.c_str());
}

// R8: the music app as the radio found it, its own volume back, the ledger cleared. Once.
static void FinalRelease(const char* why)
{
    if (g_appFinal) return;
    g_appFinal = true;
    try
    {
        if (g_smtc && g_ctl.phase != radio::AP_IDLE)
        {
            radio::AppIn in; in.final = true; in.playing = g_smtc->playing;
            const radio::AppOut o = radio::AppStep(g_ctl, in);
            if (o.cmd != radio::APP_NONE) app::SmtcCommandWait(*g_smtc, o.cmd, 600);
        }
        if (g_vol) app::ReleaseAll(*g_vol, why);
    }
    catch (...) { Log("!!! EXCEPTION giving the music app back (%s)\n", why); }
}

// ---- the radio thread ----------------------------------------------------------------------------
static bool FocusIsGame()
{
    HWND fg = GetForegroundWindow();
    DWORD pid = 0;
    if (fg) GetWindowThreadProcessId(fg, &pid);
    return pid == GetCurrentProcessId();
}

static bool IsTrackClip(int clip)
{
    return clip >= 0 && clip < static_cast<int>(g_lib.clips.size()) && g_lib.clips[clip].kind == radio::K_TRACK;
}

// A listener's file made Windows' decoder throw or fault, inside the game (spec I5): the stream is
// dropped without touching its reader again, the file leaves every pool, what airs moves on, and the
// library is off for the session (its stations talk, or play the AI songs). The game goes on.
static void TrackFault(Stream& s, const char* what)
{
    const int clip = s.clip;
    const double at = s.eventStart;
    Drop(s);
    if (!IsTrackClip(clip)) { Log("!!! EXCEPTION in the radio's decoder (%s) - that clip is skipped\n", what); return; }
    for (int si = 0; si < static_cast<int>(g_lib.stations.size()); ++si)
    {
        std::vector<int> pool = g_lib.stations[si].mine;
        const size_t n = pool.size();
        pool.erase(std::remove(pool.begin(), pool.end(), clip), pool.end());
        if (pool.size() != n) radio::SetPool(g_lib, si, pool);
    }
    if (g_station >= 0 && g_station < static_cast<int>(g_st.size()))
    {
        radio::StationState& S = g_st[g_station];
        if (!S.q.empty() && S.q[0].clip == clip && S.q[0].start == at) { radio::CutAiring(S); radio::Advance(S, g_lib, g_station, 0.0, WorldNow()); }
    }
    if (!lib::g_libOff)
    {
        InterlockedExchange(&lib::g_libOff, 1);
        Log("!!! EXCEPTION in the radio's decoder (%s) on %s - your library is off for this session; library stations %s\n",
            what, g_lib.clips[clip].file.c_str(), g_set.libShortAi ? "play the AI songs" : "talk only");
    }
}

// Pump with the decoder guarded (a station clip or a listener's file).
static void PumpSafe(Stream& s)
{
    if (!s.voice) return;
    try { Pump(s); }
    catch (...) { TrackFault(s, "decoding"); }
}

// A station clip, opened with the decoder guarded.
static bool OpenSafe(Stream& s, const radio::Clip& c, int clipIdx, double eventStart, double offset)
{
    try { return Open(s, c, clipIdx, eventStart, offset); }
    catch (...) { s.clip = clipIdx; s.eventStart = eventStart; TrackFault(s, "opening"); return false; }
}

// ---- [missions1] the mission newscasts (Notes\68 section 6) -----------------------------------------
// The game thread posts a completed mission's id (missions.h); this thread keeps the queue (msm::NewsQueue,
// saved to lcudrive_missionnews.txt), plays each flash once on the station the player listens to - after
// the item airing now ends, never into it; at the next song boundary (mission_news_interrupt = 1) or the
// station's next news break (0) - and later lets the tuned station's news breaks recap it now and then.
// Held while a cut-scene plays, while a mission runs (g_newsHold), in the BlockRooms (g_hold) and when nobody
// hears the radio. A mission with no clip: nothing, silently.
static SRWLOCK                  g_misLock = SRWLOCK_INIT;
static std::vector<std::string> g_misPosted;
static volatile LONG            g_misHeard = 0;            // flashes heard this session (log / tests)

static void PostMission(const char* id)                    // game thread
{
    if (!id || !id[0]) return;
    AcquireSRWLockExclusive(&g_misLock);
    if (g_misPosted.size() < 64) g_misPosted.push_back(id);
    ReleaseSRWLockExclusive(&g_misLock);
}

static bool StationListed(const char* list, const std::string& id)
{
    if (_stricmp(list, "all") == 0) return true;
    if (_stricmp(list, "none") == 0) return false;
    for (const char* p = list; *p; )
    {
        while (*p == ',' || *p == ' ' || *p == '\t') ++p;
        const char* e = p;
        while (*e && *e != ',' && *e != ' ' && *e != '\t') ++e;
        if (e > p && static_cast<size_t>(e - p) == id.size() && _strnicmp(p, id.c_str(), id.size()) == 0) return true;
        p = e;
    }
    return false;
}

struct MisFlight { std::string id; int station = -1, clip = -1; double start = -1.0; };

static void MissionNewsTick(bool listen, int tuned, const radio::World& w)
{
    static msm::NewsQueue q;
    static bool loaded = false;
    static char loadedFrom[MAX_PATH] = {};
    static MisFlight fl;
    static int recapEntry = -1, recapStation = -1;
    static uint32_t lastSave = 0;
    const int64_t nowS = static_cast<int64_t>(_time64(nullptr));
    msm::NewsParams np;
    np.keep = g_set.missionKeepH * 3600.0; np.recapWindow = g_set.missionRecapH * 3600.0; np.recapGap = g_set.missionRecapGapMin * 60.0;
    np.recapMax = g_set.missionRecapMax; np.once = g_set.missionOnce != 0;

    if (!loaded || strcmp(loadedFrom, g_set.missionState) != 0)
    {
        loaded = true; strcpy_s(loadedFrom, g_set.missionState);
        q = msm::NewsQueue();
        if (g_set.missionState[0])
        {
            FILE* f = nullptr;
            if (fopen_s(&f, g_set.missionState, "rb") == 0 && f)
            {
                std::string txt; char buf[4096]; size_t n;
                while ((n = fread(buf, 1, sizeof(buf), f)) > 0 && txt.size() < 1000000) txt.append(buf, n);
                fclose(f);
                const int k = msm::Load(q, txt.data(), txt.size());
                int waiting = 0; for (const msm::NewsEntry& x : q.e) waiting += x.state == 0;
                Log("    radio: mission news: %d missions in %s (%d waiting for their flash)\n", k, g_set.missionState, waiting);
            }
        }
    }
    // the completions the game thread saw
    std::vector<std::string> posted;
    AcquireSRWLockExclusive(&g_misLock);
    posted.swap(g_misPosted);
    ReleaseSRWLockExclusive(&g_misLock);
    for (const std::string& id : posted)
    {
        if (!g_set.missionNews) continue;
        if (!radio::HasMissionNews(g_lib, id)) { Log("    radio: mission news: no clip for %s - no flash\n", id.c_str()); continue; }
        if (msm::Add(q, id, nowS, np)) Log(">>> radio: mission news: %s waits for its flash\n", id.c_str());
        else Log("    radio: mission news: %s already %s\n", id.c_str(), g_set.missionOnce ? "heard (mission_news_once = 1)" : "waiting");
    }

    const int nSt = static_cast<int>(g_st.size());
    // the recap offer: only on the tuned station, only while it is heard
    for (int i = 0; i < nSt; ++i)
    {
        radio::StationState& S = g_st[i];
        if (S.recapPlanned >= 0)
        {
            if (i == recapStation && recapEntry >= 0 && recapEntry < static_cast<int>(q.e.size()))
            {
                msm::Recapped(q, recapEntry, nowS);
                Log("    radio: mission news: a recap of %s in %s's news\n", q.e[recapEntry].id.c_str(), StationName(i));
            }
            S.recapPlanned = -1;
        }
        S.recapClip = -1;
    }
    recapEntry = -1; recapStation = -1;
    const bool ok = g_set.missionNews && !g_newsHold && !g_hold && tuned >= 0 && tuned < nSt &&
                    tuned < static_cast<int>(g_lib.stations.size()) && StationListed(g_set.missionStations, g_lib.stations[tuned].id);
    if (ok && listen)
    {
        const int r = msm::RecapCandidate(q, nowS, np);
        if (r >= 0)
        {
            int c = radio::MissionClip(g_lib, tuned, radio::K_N_MISSION_RECAP, q.e[r].id);
            if (c < 0 && g_set.missionRecapReuse) c = radio::MissionClip(g_lib, tuned, radio::K_N_MISSION, q.e[r].id);
            if (c >= 0) { g_st[tuned].recapClip = c; g_st[tuned].recapChance = g_set.missionRecapChance; recapEntry = r; recapStation = tuned; }
        }
    }

    // a flash on the air: heard once it starts while the radio is heard
    if (fl.clip >= 0)
    {
        bool found = false;
        if (fl.station >= 0 && fl.station < nSt && fl.clip < static_cast<int>(g_lib.clips.size()) && g_lib.clips[fl.clip].mission == fl.id)
        {
            radio::StationState& S = g_st[fl.station];
            for (size_t i = 0; i < S.q.size(); ++i) if (S.q[i].clip == fl.clip && std::fabs(S.q[i].start - fl.start) < 0.001) { found = true; break; }
            if (found && S.q[0].clip == fl.clip && std::fabs(S.q[0].start - fl.start) < 0.001 && S.t >= fl.start)
            {
                const int e = msm::FindEntry(q, fl.id);
                if (listen && fl.station == tuned && e >= 0 && q.e[e].state == 0)
                {
                    msm::Heard(q, e, nowS);
                    InterlockedIncrement(&g_misHeard);
                    Log(">>> radio: mission news ON AIR: %s on %s (%s)\n", fl.id.c_str(), StationName(fl.station), g_lib.clips[fl.clip].file.c_str());
                }
                else Log("    radio: mission news: %s aired on %s with nobody listening - it waits for the next chance\n", fl.id.c_str(), StationName(fl.station));
                fl = MisFlight();
            }
        }
        if (!found) fl = MisFlight();
    }
    // the next flash
    if (fl.clip < 0 && ok && listen)
    {
        const int e = msm::Due(q, nowS, np);
        if (e >= 0)
        {
            radio::StationState& S = g_st[tuned];
            const int c = radio::MissionClip(g_lib, tuned, radio::K_N_MISSION, q.e[e].id);
            const bool gap = !S.q.empty() && S.q[0].start + S.q[0].dur <= S.t + 0.30;
            const bool where = g_set.missionInterrupt ? radio::CanCutIn(S, g_lib) : radio::NewsBreakNext(S, g_lib);
            if (c < 0) { Log("    radio: mission news: %s has no clip on this station list - dropped\n", q.e[e].id.c_str()); msm::Heard(q, e, nowS); }
            else if (gap && where)
            {
                const double at = radio::InsertMission(S, g_lib, g_set.missionInterrupt ? tuned : -1, c, w);   // (at a news break the station has its own hand-over)
                if (at >= 0.0)
                {
                    fl.id = q.e[e].id; fl.station = tuned; fl.clip = c; fl.start = at;
                    Log("    radio: mission news: %s goes on after this on %s\n", fl.id.c_str(), StationName(tuned));
                }
            }
        }
    }
    if (q.dirty && g_set.missionState[0] && GetTickCount() - lastSave > 2000)
    {
        lastSave = GetTickCount();
        q.dirty = false;
        const std::string txt = msm::Save(q);
        char tmp[MAX_PATH + 8];
        _snprintf_s(tmp, sizeof(tmp), _TRUNCATE, "%s.tmp", g_set.missionState);
        FILE* f = nullptr;
        if (fopen_s(&f, tmp, "wb") == 0 && f)
        {
            const bool wrote = fwrite(txt.data(), 1, txt.size(), f) == txt.size();
            fclose(f);
            if (!wrote || !MoveFileExA(tmp, g_set.missionState, MOVEFILE_REPLACE_EXISTING)) { DeleteFileA(tmp); Log("!!! radio: mission news: couldn't save %s\n", g_set.missionState); }
        }
    }
}

static DWORD WINAPI Thread(LPVOID)
{
    const HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(MFStartup(MF_VERSION, MFSTARTUP_NOSOCKET))) { Log("!!! radio: Media Foundation unavailable - no radio\n"); return 0; }
    LONG ver = -1;
    char manifest[MAX_PATH] = {};
    bool engine = false, loaded = false, provisional = false;
    uint32_t lastProbe = 0;
    Stream cur, over, dying[2];
    float bus = 0.0f;
    bool retune = true;
    double playedEvent = -1.0, overPlayed = -1.0;   // which airing each voice last started (a clip
                                                    // that ends a hair early must not start again)
    LONG lastWanted = 0, lastBusted = 0, lastHeli = 0;
    WORD padPrevButtons = 0, padPrevButtons2 = 0;
    LARGE_INTEGER qf, q0; QueryPerformanceFrequency(&qf); QueryPerformanceCounter(&q0);
    double last = 0.0;
    uint32_t lastLog = 0;
    {
        LARGE_INTEGER c; QueryPerformanceCounter(&c);
        g_seed = static_cast<uint64_t>(c.QuadPart) ^ (static_cast<uint64_t>(GetTickCount()) << 32);
    }
    // the music: the app's restore ledger first (a crash last time left an app at the radio's level)
    g_vol = new (std::nothrow) app::AppVolume();
    if (g_vol) { try { app::ReadLedger(*g_vol); } catch (...) {} }
    LONG aiSeen = 0, appOffSeen = 0, libOffSeen = 0;
    bool noSessionLogged = false, scanOwed = false, resolveFaultLogged = false;
    int tunedSeen = -2;
    std::string trackSeen;

    while (!g_stop)
    {
        Sleep(10);
        LARGE_INTEGER qc; QueryPerformanceCounter(&qc);
        const double now = static_cast<double>(qc.QuadPart - q0.QuadPart) / static_cast<double>(qf.QuadPart);
        const float frameDt = static_cast<float>(now - last > 0.1 ? 0.1 : now - last);
        const uint32_t tick = GetTickCount();

        // settings (and the content, the first time or when the manifest moves)
        // The station list: the real one, or until it exists the provisional one (no ads or news,
        // shipped next to the DLL, content from the real one's folder). Looked for again every 15 s,
        // so the finished list takes over without a restart.
        bool reload = false, cfgChanged = false;
        if (ver != g_cfgVer)
        {
            ver = g_cfgVer;
            AcquireSRWLockShared(&g_setLock);
            g_set = g_setIn;
            ReleaseSRWLockShared(&g_setLock);
            cfgChanged = true;
            if (strcmp(manifest, g_set.manifest) != 0) { strcpy_s(manifest, g_set.manifest); reload = true; }
            else if (loaded) ApplyBreaking(g_st, g_lib);
        }
        if (provisional && tick - lastProbe > 15000)
        {
            lastProbe = tick;
            if (FileExists(manifest)) { reload = true; Log(">>> radio: the finished station list is here - switching to it\n"); }
        }
        if (reload)
        {
            for (Stream* s : { &cur, &over, &dying[0], &dying[1] }) Close(*s);
            const int keep = loaded ? g_station : -2;
            std::vector<std::string> oldIds;
            for (const radio::Station& s : g_lib.stations) oldIds.push_back(s.id);
            loaded = LoadLibraryFile(manifest);
            provisional = false;
            if (!loaded && g_set.fallback[0])
            {
                std::string dir(manifest);
                const size_t slash = dir.find_last_of("\\/");
                dir = slash == std::string::npos ? std::string() : dir.substr(0, slash + 1);
                loaded = LoadLibraryFile(g_set.fallback, g_set.root[0] ? g_set.root : dir.c_str());
                provisional = loaded;
                lastProbe = tick;
                if (loaded) Log("    radio: playing the provisional list until %s exists\n", manifest);
            }
            if (!loaded) Log("!!! radio: no station list - no radio (looked for %s)\n", manifest);
            if (loaded)
            {
                // the same stations: the last scan's tracks are appended again; others: a new scan
                bool same = oldIds.size() == g_lib.stations.size();
                for (size_t i = 0; same && i < oldIds.size(); ++i) same = oldIds[i] == g_lib.stations[i].id;
                if (!same) { g_mus.clear(); g_haveScan = false; }
                for (StationMusic& m : g_mus) { m.poolStamp = 0xFFFFFFFFu; m.resolved = false; }
                for (const StationKeys* k = g_set.stn; k < g_set.stn + g_set.nStn && k < g_set.stn + 16; ++k)
                {
                    bool found = false;
                    for (const radio::Station& s : g_lib.stations) if (_stricmp(s.id.c_str(), k->id) == 0) found = true;
                    if (!found) Log("!!! radio: music_ / playlist_ / genre_%s: no station '%s' (station ids are in lcudrive.ini)\n", k->id, k->id);
                }
                InitStations();
                ApplyBreaking(g_st, g_lib);
                const int n = static_cast<int>(g_lib.stations.size());
                radio::Rng r; r.s = g_seed | 1;
                if (keep >= -1 && keep < n) g_station = keep;
                else g_station = (g_set.startStation >= 1 && g_set.startStation <= n) ? g_set.startStation - 1 : r.Range(0, n - 1);
                {   // [radiologo1] never start on a station that can't play (the next one that can)
                    const std::vector<uint8_t> ok = DialOk();
                    if (g_station >= 0 && g_station < n && !ok[static_cast<size_t>(g_station)])
                    {
                        const int nx = radio::StepDial(g_station, 1, ok);
                        g_station = nx >= 0 ? nx : radio::StepDial(-1, 1, ok);
                    }
                }
                retune = true;
                cfgChanged = true;                                   // a scan for the new list
            }
        }
        if (!loaded) { last = now; continue; }
        if (!engine)
        {
            engine = StartEngine();
            if (!engine) break;
        }

        // ---- the music sources: settings, the library scan, the app, the hotkeys ---------------------
        // Guarded (spec I5): a fault here turns off the part it came from - the library or the app -
        // for the session, never the radio or the game.
        {
            bool resolve = false;
            int stage = 0;                                       // 0 the library / sources, 1 the app
            try
            {
                if (cfgChanged)
                {
                    if (NeedLibrary() && !lib::g_libOff) { PostScan(); scanOwed = false; }
                    else if (g_aiOn) scanOwed = true;            // changed under Ctrl+Alt+/: scanned on the way back
                    resolve = true;
                }
                if (lib::ScanResult* r = lib::TakeScanResult()) { TakeScan(r); resolve = true; }
                if (lib::g_libOff != libOffSeen) { libOffSeen = lib::g_libOff; resolve = true; }
                stage = 1;
                if ((NeedService() || (g_smtc && g_ctl.phase != radio::AP_IDLE)) && !g_appFinal)
                {
                    if (!g_smtc) g_smtc = new (std::nothrow) app::Smtc();
                    if (g_smtc)
                    {
                        wchar_t want[128] = {};
                        MultiByteToWideChar(CP_UTF8, 0, g_set.svcApp, -1, want, 128);
                        g_smtc->want = want;
                        // the app the radio has stays bound while it is there (no rebinding mid-take)
                        app::SmtcPoll(*g_smtc, tick, g_ctl.phase != radio::AP_IDLE);
                        if (g_smtc->rebound)
                        {
                            // straight from one app to another while the radio had the old one: the
                            // old one is paused (if it played in the radio's hands), the controller
                            // starts over and takes the new one; Retarget below gives the old one's
                            // volume back once its pause has taken (2 s)
                            g_smtc->rebound = false;
                            if (g_ctl.phase != radio::AP_IDLE)
                            {
                                const bool pauseOld = radio::AppRebind(g_ctl, FocusIsGame(), g_smtc->prevPlaying);
                                if (pauseOld) app::SmtcPausePrev(*g_smtc);
                                Log("    radio: app: %s instead of %s - the radio starts over with it%s\n", app::Narrow(g_smtc->aumid).c_str(),
                                    app::Narrow(g_smtc->prevAumid).c_str(), pauseOld ? " (the old one paused)" : "");
                            }
                            g_smtc->prevSess = nullptr;
                        }
                        if (!g_smtc->aumid.empty()) g_boundSeen = tick;
                        const bool bound = !g_smtc->aumid.empty() || (g_boundSeen && tick - g_boundSeen < 5000);
                        if (bound != g_appBound) { g_appBound = bound; resolve = true; if (bound) noSessionLogged = false; }
                        if (!bound && !noSessionLogged && g_smtc->loggedUp && g_smtc->sessAt && !app::g_appOff)
                        {
                            noSessionLogged = true;
                            Log("!!! radio: your music app: no %s session - its stations play your library or talk until it shows up\n",
                                _stricmp(g_set.svcApp, "auto") == 0 ? "Spotify (or media keys)" : g_set.svcApp);
                        }
                        if (g_vol && !g_smtc->aumid.empty() && g_vol->target.aumid != g_smtc->aumid)
                        {
                            try { app::Retarget(*g_vol, g_smtc->aumid, tick); } catch (...) {}
                        }
                        // the app's tracks go in the log only while the radio plays the app; with the
                        // titles only for Spotify (to check the ad detection), other apps: just "a new track"
                        if (g_ctl.phase != radio::AP_IDLE && !g_smtc->key.empty() && g_smtc->key != trackSeen)
                        {
                            trackSeen = g_smtc->key;
                            const int len = g_smtc->timeline ? static_cast<int>(g_smtc->lenS + 0.5) : 0;
                            if (app::IContains(g_smtc->aumid, L"spotify"))
                                Log("    radio: app track: \"%s\" by \"%s\" (%d:%02d)%s\n", g_smtc->title.c_str(), g_smtc->artist.c_str(), len / 60, len % 60,
                                    radio::LooksLikeAd(app::Narrow(g_smtc->aumid), g_smtc->title, g_smtc->artist, g_smtc->lenS) ? " - looks like the service's own ad - left alone" : "");
                            else Log("    radio: app: a new track (%d:%02d)\n", len / 60, len % 60);
                        }
                    }
                }
                if (g_vol) { try { app::Tick(*g_vol, tick); } catch (...) {} }
                if (app::g_appOff != appOffSeen) { appOffSeen = app::g_appOff; resolve = true; }
                stage = 0;
                const LONG ai = g_aiOn;
                if (ai != aiSeen)
                {
                    aiSeen = ai; resolve = true;
                    if (!ai && NeedLibrary() && !lib::g_libOff && (scanOwed || !g_haveScan)) { PostScan(); scanOwed = false; }
                }
                stage = 1;
                if (InterlockedExchange(&g_openLink, 0)) OpenTunedLink(true);
                if (g_releaseApp && !g_appFinal)
                {
                    // the game window is gone: the app back now; its stations play the library or talk
                    // (app slots would air as silence) until a game window is there again
                    Log("    radio: the game window closed - giving the music app back\n");
                    FinalRelease("the game window closed");
                    resolve = true;
                }
                else if (!g_releaseApp && g_appFinal)
                {
                    g_appFinal = false; g_ctl = radio::AppCtl(); resolve = true;
                    Log("    radio: the game window is back - your music app's stations play it again\n");
                }
                stage = 0;
                if (resolve) ResolveSources(false);
            }
            catch (...)
            {
                if (stage == 0)
                {
                    InterlockedExchange(&lib::g_libOff, 1);
                    Log("!!! EXCEPTION in the radio's library - your library is off for this session; library stations %s\n",
                        g_set.libShortAi ? "play the AI songs" : "talk only");
                }
                else
                {
                    InterlockedExchange(&app::g_appOff, 1);
                    Log("!!! EXCEPTION in the radio's music-app link - off for this session; its stations play your library or talk\n");
                    try { if (g_vol) app::ReleaseAll(*g_vol, "the link faulted"); } catch (...) {}
                }
                try { ResolveSources(false); }
                catch (...) { if (!resolveFaultLogged) { resolveFaultLogged = true; Log("!!! EXCEPTION choosing the stations' music - they keep what they had\n"); } }
            }
        }

        // the stations' clocks run while the world does (not paused, not on a menu or the map)
        const bool paused = g_pauseByte && *g_pauseByte != 0;
        const bool running = static_cast<LONG>(tick - static_cast<uint32_t>(g_gameSeen)) < 300 && !paused;
        const double dt = running ? (now - last > 0.25 ? 0.25 : now - last) : 0.0;
        last = now;
        const radio::World w = WorldNow();

        // wanted mode on the news: a level up, a police helicopter on you (news:wanted_heli - rendered
        // since the first batch, never picked until wanted mode had helicopters), or getting away
        // Co-op: g_wanted is the higher of the two levels, so a bust with the other player still wanted
        // isn't followed by "got away", and the other player's level-ups come through g_levelNews.
        const LONG wl = g_wanted, bc = g_busted, hn = g_heliNews, ln = g_levelNews;
        static LONG lastLevelNews = 0;
        if (wl != lastWanted || bc != lastBusted || hn != lastHeli || ln != lastLevelNews)
        {
            int kind = radio::K_NONE;
            const LONG lnLevel = ln & 0xFF;
            if (bc != lastBusted) kind = radio::K_N_BUSTED;           // caught: not "got away"
            else if (hn != lastHeli && wl > 0) kind = radio::K_N_HELI;
            else if (wl > lastWanted && wl >= 1 && wl <= 5) kind = radio::K_N_W1 + static_cast<int>(wl) - 1;
            else if (wl == 0 && lastWanted > 0) kind = radio::K_N_ESCAPED;
            else if (ln != lastLevelNews && wl > 0 && lnLevel >= 1 && lnLevel <= 5) kind = radio::K_N_W1 + static_cast<int>(lnLevel) - 1;
            lastBusted = bc;
            lastHeli = hn;
            lastLevelNews = ln;
            if (kind != radio::K_NONE && !g_lib.shared[kind].empty())
                for (size_t i = 0; i < g_st.size(); ++i) radio::WantedEvent(g_st[i], kind);
            lastWanted = wl;
        }
        for (size_t i = 0; i < g_st.size(); ++i) radio::Advance(g_st[i], g_lib, static_cast<int>(i), dt, w);

        // where the player is
        const bool inVeh = static_cast<LONG>(tick - static_cast<uint32_t>(g_vehSeen)) < 200 &&
                           (g_vehKind != 2 || g_set.inHeli);

        // station steps: the hotkeys, and the pad's buttons while in a vehicle
        LONG steps = InterlockedExchange(&g_step, 0);
        for (int pl = 0; pl < 2 && pad::g_getState && (g_set.padNext || g_set.padPrev); ++pl)   // either player's pad
        {
            const int port = pad::PortFor(pl);
            XINPUT_STATE ps;
            if (port >= 0 && pad::g_getState(static_cast<DWORD>(port), &ps) == ERROR_SUCCESS)
            {
                WORD& prevB = pl ? padPrevButtons2 : padPrevButtons;
                const WORD b = ps.Gamepad.wButtons, edge = static_cast<WORD>(b & ~prevB);
                prevB = b;
                if (inVeh && FocusIsGame() && !paused)      // paused: the D-pad belongs to the menus (Mod Options)
                {
                    if (g_set.padNext && (edge & g_set.padNext)) ++steps;
                    if (g_set.padPrev && (edge & g_set.padPrev)) --steps;
                }
            }
        }
        const bool stepped = steps && inVeh;                             // [radiologo1]
        if (steps && inVeh)
        {
            // [radiologo1] the dial skips a station that can't play (radio::StepDial; index n = off)
            g_station = radio::StepDial(g_station, static_cast<int>(steps), DialOk());
            Log(">>> radio: %s%s%s\n", StationName(g_station), g_station >= 0 ? " " : "",
                g_station >= 0 ? g_lib.stations[g_station].freq.c_str() : "");
            if (bus > 0.05f) PlayStatic();
            retune = true;
            ShowLogo(g_station, rlm::EV_STEP);                           // [radiologo1] the logo overlay
        }
        // [radiologo1] getting into a vehicle with the radio on shows the tuned station's logo too (the game thread
        // drops it when [radio] logo_on_enter = 0). Only while the world runs: a pause or a load changes nothing.
        {
            static rlm::EnterWatch s_enter;
            if (running && rlm::EnterRise(s_enter, g_set.enable && inVeh && g_station >= 0, now, 3.0, stepped))
                ShowLogo(g_station, rlm::EV_ENTER);
        }

        // the station playing the app: the radio's own audio only while the game has focus
        const int nSt = static_cast<int>(g_st.size());
        const bool tunedOk = g_station >= 0 && g_station < nSt && g_station < static_cast<int>(g_mus.size());
        const uint8_t tunedSrc = tunedOk ? g_mus[g_station].res.src : static_cast<uint8_t>(radio::SRC_AI);
        const bool focus = FocusIsGame();
        const bool listenBase = g_set.enable && inVeh && running && !g_hold && g_station >= 0;
        const bool listen = radio::ServiceListen(listenBase, tunedSrc, focus);
        g_audible = listen ? 1 : 0;
        const float busTarget = listen ? 1.0f : 0.0f;
        const float busStep = frameDt / (listen ? 0.35f : 0.45f);
        bus = bus < busTarget ? (bus + busStep > busTarget ? busTarget : bus + busStep)
                              : (bus - busStep < busTarget ? busTarget : bus - busStep);
        // the player's volume x the game's own Volume option x the game's duck under dialogue / jingles
        const float user = g_set.volume < 0.0f ? 0.0f : (g_set.volume > 1.5f ? 1.5f : g_set.volume);
        const float game = radio::Clampf01(g_vol1000 / 1000.0f) * radio::Clampf01(g_duck1000 / 1000.0f);
        g_bus->SetVolume(bus * user * game);
        // [missions1] the mission newscasts (own fault switch: an exception = no mission news this session)
        {
            static bool s_misOff = false;
            if (!s_misOff && loaded)
            {
                try { MissionNewsTick(listen, tunedOk ? g_station : -1, w); }
                catch (...) { s_misOff = true; Log("!!! EXCEPTION in the radio's mission news - off for this session\n"); }
            }
        }

        // a station whose programme the listener's pause turned into talk goes back to its own source
        // (ResolveSources') once it isn't the tuned app station the controller runs - left, or moved
        // off the app meanwhile (no AI identity change: with every station on ai nothing differs)
        const bool ctlRuns = g_smtc && !g_appFinal;
        for (int i = 0; i < nSt && i < static_cast<int>(g_mus.size()); ++i)
        {
            if (i == g_station && tunedSrc == radio::SRC_APP && ctlRuns) continue;
            const radio::SourceOut& r = g_mus[i].res;
            if (g_mus[i].resolved && g_st[i].source != r.src) radio::SetSource(g_st[i], g_lib, i, r.src, r.genreLines, r.introOver);
        }

        // ---- the music app: it plays in the tuned station's music slots, paused for everything else ----
        if (ctlRuns)
        {
            try
            {
                radio::StationState* S = tunedOk ? &g_st[g_station] : nullptr;
                radio::AppIn in;
                in.dt = frameDt; in.now = now; in.focus = focus;
                in.want = listenBase && tunedSrc == radio::SRC_APP && !g_smtc->aumid.empty() && !app::g_appOff;
                in.tuned = g_station;
                in.slot = S && !S->q.empty() && S->q[0].ext;
                in.talkFollows = S ? radio::TalkFollows(*S, g_lib) : false;
                in.playing = g_smtc->playing;
                // the service's own ad: only once its track details were read since it was bound, and
                // only while it plays (blank details never read are not an ad)
                in.isAd = in.playing && g_smtc->propsKnown &&
                          radio::LooksLikeAd(app::Narrow(g_smtc->aumid), g_smtc->title, g_smtc->artist, g_smtc->lenS);
                in.seekable = g_smtc->seekable;
                in.timeline = g_smtc->timeline;
                in.remaining = g_smtc->lenS - g_smtc->posS;
                in.key = g_smtc->key;
                const float svc = radio::Clampf01(g_set.svcVolume);
                in.level = bus * svc * game;
                // the volume is only the radio's to hold when the app's audio session was found
                in.volctl = g_set.svcVolCtl != 0 && g_vol && (g_vol->canHold || !g_vol->held.empty());
                in.lead = g_set.svcLead;
                in.maxTrackS = static_cast<float>(g_set.svcMaxMin) * 60.0f;
                const radio::AppOut o = radio::AppStep(g_ctl, in);
                if (S)
                {
                    const int si = g_station;
                    bool fitted = false;
                    if (tunedSrc == radio::SRC_APP)        // the listener's pause: talk only, until they press play
                    {
                        if (o.talkMode && S->source != radio::SRC_TALK) { radio::SetSource(*S, g_lib, si, radio::SRC_TALK, false, false); fitted = true; }
                        if (!o.talkMode && S->source == radio::SRC_TALK) { radio::SetSource(*S, g_lib, si, radio::SRC_APP, false, false); fitted = true; }
                    }
                    if (o.yield) { radio::YieldToMusic(*S, g_lib, si, w); fitted = true; }
                    if (o.cut) { radio::CutAiring(*S); fitted = true; }
                    if (o.hold) radio::Stretch(*S, 1.0);
                    if (fitted) radio::Advance(*S, g_lib, si, 0.0, w);
                }
                if (o.seek0) app::SmtcSeek0(*g_smtc);
                if (o.cmd != radio::APP_NONE) app::SmtcCommand(*g_smtc, o.cmd);
                if (g_vol)
                {
                    if (o.held) { if (!g_vol->holding) app::Hold(*g_vol, o.volume, tick); else app::Apply(*g_vol, o.volume); }
                    else if (o.release || g_vol->holding) app::Release(*g_vol, o.note ? o.note : "the radio lets go");
                }
                if (o.note) Log("    radio: app: %s\n", o.note);
                // tuning to a service station can open its playlist too (service_open_tune) - also when
                // the app isn't there yet (opening the link starts it)
                if (g_station != tunedSeen)
                {
                    if (g_set.svcOpenTune && inVeh && ServiceWanted(g_station) && tunedSeen != -2) OpenTunedLink(false);
                    tunedSeen = g_station;
                }
            }
            catch (...)
            {
                InterlockedExchange(&app::g_appOff, 1);
                Log("!!! EXCEPTION in the radio's music-app link - off for this session; its stations play your library or talk\n");
                try { if (g_vol) app::ReleaseAll(*g_vol, "the link faulted"); } catch (...) {}
            }
        }

        if (!listen && bus <= 0.0f)
        {
            for (Stream* s : { &cur, &over, &dying[0], &dying[1] }) Close(*s);
            retune = true; playedEvent = overPlayed = -1.0;
            continue;
        }

        // what is airing on the station, and from where
        if (g_station >= 0 && g_station < static_cast<int>(g_st.size()))
        {
            radio::StationState& S = g_st[g_station];
            const radio::Event e = S.q.empty() ? radio::Event() : S.q[0];
            const double off = S.t - e.start;
            if (retune || e.start != playedEvent)
            {
                // the old one fades out quickly (no click), the new one comes in from its position
                Stream& d = dying[dying[0].voice ? 1 : 0];
                Close(d);
                if (cur.voice) { std::swap(d, cur); d.target = 0.0f; d.rate = retune ? 0.03f : 0.05f; }
                if (over.voice) Close(over);
                playedEvent = e.start; overPlayed = -1.0;
                if (e.clip >= 0 && off < e.dur - 0.05)
                {
                    const radio::Clip& c = g_lib.clips[e.clip];
                    bool opened = false;
                    if (c.kind == radio::K_TRACK)
                    {
                        // one of the listener's files: no loudness data (measured as it plays), its real
                        // length. Opened with the decoder guarded (spec I5): a fault drops the file and
                        // turns the library off for the session.
                        HRESULT why = S_OK;
                        bool faulted = false;
                        double real = 0.0;
                        const int clipIdx = e.clip;
                        const std::string file = c.file;
                        try
                        {
                            opened = OpenPath(cur, Widen(file), file.c_str(), -6.0f, clipIdx, e.start, off > 0.0 ? off : 0.0, &why);
                            if (opened)
                            {
                                PROPVARIANT pv; PropVariantInit(&pv);
                                if (SUCCEEDED(cur.reader->GetPresentationAttribute(static_cast<DWORD>(MF_SOURCE_READER_MEDIASOURCE), MF_PD_DURATION, &pv)) && pv.vt == VT_UI8)
                                    real = static_cast<double>(pv.uhVal.QuadPart) / 1e7;
                                PropVariantClear(&pv);
                            }
                        }
                        catch (...) { faulted = true; }
                        if (faulted)
                        {
                            cur.clip = clipIdx; cur.eventStart = e.start;
                            TrackFault(cur, "opening");
                            opened = false;
                        }
                        else if (opened)
                        {
                            g_trackFails = 0;
                            cur.autoGain = true;
                            if (!g_testNoDurFix && real > 1.0 && std::fabs(real - e.dur) > 0.5)
                            {
                                radio::SetAiringDur(S, real);
                                g_lib.clips[clipIdx].dur = static_cast<float>(real);
                                g_lib.clips[clipIdx].durGuess = false;
                                InterlockedIncrement(&g_testDurFixed);
                            }
                        }
                        else
                        {
                            // left out of the pool; the plan moves up (the next item starts now)
                            Log("!!! radio: library: %s: can't open (0x%08lX) - left out\n", c.file.c_str(), static_cast<unsigned long>(why));
                            std::vector<int> pool = g_lib.stations[g_station].mine;
                            pool.erase(std::remove(pool.begin(), pool.end(), e.clip), pool.end());
                            radio::SetPool(g_lib, g_station, pool);
                            radio::CutAiring(S);
                            radio::Advance(S, g_lib, g_station, 0.0, w);
                            if (++g_trackFails >= 20 && !lib::g_libOff)
                            {
                                InterlockedExchange(&lib::g_libOff, 1);
                                Log("!!! radio: library: 20 files in a row would not open (a drive gone?) - your library is off for this session\n");
                            }
                        }
                    }
                    else opened = OpenSafe(cur, c, e.clip, e.start, off > 0.0 ? off : 0.0);
                    if (opened)
                    {
                        cur.rate = off > 0.3 ? 0.12f : 0.02f;
                        if (tick - lastLog > 250 || retune)
                        {
                            lastLog = tick;
                            Log("    radio: %s - %s [%s] from %.0f s\n", StationName(g_station), c.label.c_str(),
                                radio::kKindNames[c.kind], off);
                        }
                    }
                }
                retune = false;
            }
            // a host intro talked over the song's intro
            float duck = 1.0f;
            if (e.over >= 0 && e.clip >= 0)
            {
                const double o0 = e.start + e.overAt, o1 = o0 + g_lib.clips[e.over].dur;
                if (S.t >= o0 && S.t < o1 - 0.05 && overPlayed != e.start)
                {
                    overPlayed = e.start;
                    if (OpenSafe(over, g_lib.clips[e.over], e.over, e.start, S.t - o0)) over.rate = 0.02f;
                }
                // ducked from the song's first moment (the voice comes in 0.4 s later) to the voice's end
                const bool talking = over.voice ? !Finished(over) : (S.t < o0 + 0.1);
                if (S.t < o1 && talking) duck = std::pow(10.0f, g_set.duckDb / 20.0f);
            }
            if (over.voice && Finished(over)) Close(over);
            cur.target = duck;
            if (cur.vol > 0.0f && duck < 1.0f) cur.rate = 0.25f;
        }
        else
        {
            if (cur.voice) { cur.target = 0.0f; cur.rate = 0.05f; }
            if (over.voice) Close(over);
        }

        PumpSafe(cur); PumpSafe(over);
        Fade(cur, frameDt); Fade(over, frameDt);
        for (Stream& d : dying)
        {
            if (!d.voice) continue;
            PumpSafe(d); Fade(d, frameDt);
            if (d.vol <= 0.0f || Finished(d)) Close(d);
        }
        if (cur.voice && Finished(cur))
        {
            // a listener's file that ran out before its slot ended: the next item starts now (no dead air)
            if (g_station >= 0 && g_station < static_cast<int>(g_st.size()) && cur.clip >= 0 && cur.clip < static_cast<int>(g_lib.clips.size()) &&
                g_lib.clips[cur.clip].kind == radio::K_TRACK)
            {
                radio::StationState& S = g_st[g_station];
                if (!S.q.empty() && S.q[0].start == cur.eventStart && S.t < S.q[0].start + S.q[0].dur - 0.5)
                {
                    radio::CutAiring(S);
                    radio::Advance(S, g_lib, g_station, 0.0, w);
                    InterlockedIncrement(&g_testCuts);
                }
            }
            Close(cur);
        }
    }

    FinalRelease("the radio stops");
    lib::StopScanThread(300);
    g_audible = 0;
    for (Stream* s : { &cur, &over, &dying[0], &dying[1] }) Close(*s);
    StopEngine();
    delete g_smtc; g_smtc = nullptr;               // WinRT references released on this thread
    delete g_vol; g_vol = nullptr;
    MFShutdown();
    if (SUCCEEDED(co)) CoUninitialize();
    return 0;
}

static void Start()
{
    if (g_thread) return;
    g_stop = 0;
    g_thread = CreateThread(nullptr, 0, Thread, nullptr, 0, nullptr);
}

static void Stop()
{
    if (!g_thread) return;
    InterlockedExchange(&g_stop, 1);
    WaitForSingleObject(g_thread, 2000);
    CloseHandle(g_thread);
    g_thread = nullptr;
}

// From the hotkey thread.
static void StepStation(int d) { InterlockedExchangeAdd(&g_step, d); }

// Ctrl+Alt+/: the AI songs on every station for this session, or back to the ini's setup.
// 1 = the AI songs now, 0 = your setup again, -1 = nothing to switch (music = ai everywhere).
static int ToggleAi()
{
    if (!g_hasMine) return -1;
    const LONG was = InterlockedXor(&g_aiOn, 1);
    return was ? 0 : 1;
}

// Ctrl+Alt+;: the tuned station's playlist in the music app.
static void OpenLink() { InterlockedExchange(&g_openLink, 1); }

// The game window is gone: the music app back now (the process may end any moment). Its stations
// play the library or talk meanwhile.
static void ReleaseApp() { InterlockedExchange(&g_releaseApp, 1); }

// A game window is there again (it was re-created, not closed): the app's stations take it again.
static void RetakeApp() { InterlockedExchange(&g_releaseApp, 0); }

} // namespace rd
