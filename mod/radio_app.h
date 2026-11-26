// =============================================================================================
//  radio_app.h - a station playing the listener's music app, the OS side (radio_app_model.h is the
//  pure controller; radio.h drives both). Everything runs on the radio thread (COM multithreaded)
//  except the link launcher, a short-lived thread of its own. Nothing here touches game memory and
//  nothing here talks to any web service: no Spotify Web API, no client id, no token.
//
//    the app        Windows' System Media Transport Controls (the media overlay's own link to
//                   Spotify, Apple Music, a browser tab, Media Player...): which app, what it plays,
//                   its timeline, play / pause / back to the start of the track. Never Next /
//                   Previous / shuffle. Every async call is started and looked at on later frames.
//    its loudness   that app's own Windows audio sessions (the volume mixer's slider for it): the
//                   radio's fades reach the app through them; its own volume is saved first and
//                   given back when the radio lets go - and, if the game ever closes while the radio
//                   holds it, on the next start (the restore ledger, lcudrive_app_volumes.txt)
//    a link         Ctrl+Alt+; opens the station's playlist (spotify: or https:// only) in the app
// =============================================================================================
#pragma once
#include <objbase.h>
#include <mmdeviceapi.h>
#include <audiopolicy.h>
#include <appmodel.h>
#include <shlobj.h>
#include <shellapi.h>
#include <propsys.h>
#include <string>
#include <vector>
#include <chrono>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Media.Control.h>
#include "radio_app_model.h"
#include "radio_genre.h"
#pragma comment(lib, "runtimeobject.lib")   // C++/WinRT: RoGetActivationFactory and its error helpers
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "advapi32.lib")

namespace rd {
namespace app {

static volatile LONG g_appOff = 0;            // the media session link faulted / is missing: off for the session

static std::string Narrow(const std::wstring& w)
{
    if (w.empty()) return std::string();
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(n > 0 ? n : 0), '\0');
    if (n > 0) WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), &s[0], n, nullptr, nullptr);
    return s;
}

static bool IEquals(const std::wstring& a, const std::wstring& b) { return _wcsicmp(a.c_str(), b.c_str()) == 0; }

static bool IContains(const std::wstring& hay, const std::wstring& needle)
{
    if (needle.empty() || hay.size() < needle.size()) return false;
    for (size_t i = 0; i + needle.size() <= hay.size(); ++i)
        if (_wcsnicmp(hay.c_str() + i, needle.c_str(), needle.size()) == 0) return true;
    return false;
}

// This module's folder (the DLL's, or a test exe's), with a trailing backslash.
static std::wstring ModuleDir()
{
    HMODULE m = nullptr;
    wchar_t p[MAX_PATH] = {};
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(&ModuleDir), &m))
        GetModuleFileNameW(m, p, MAX_PATH);
    std::wstring d(p);
    const size_t s = d.find_last_of(L"\\/");
    return s == std::wstring::npos ? std::wstring() : d.substr(0, s + 1);
}

// ---- which process is an app id ---------------------------------------------------------------------
// The media session names its app by an AppUserModelID: "Spotify.exe", a packaged app's
// "Family_hash!App", a browser's "Chrome" / "MSEdge" / Firefox's install hash. The audio sessions
// name a process. This joins the two.
struct AppTarget
{
    std::wstring aumid;                     // the media session's app id ("" = none)
    std::vector<std::wstring> exes;         // process image names whose audio is that app's
    std::wstring family;                    // a packaged app: its package family (matched instead)
};

static bool ProcessIdentity(DWORD pid, std::wstring& exe, std::wstring& family)
{
    exe.clear(); family.clear();
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) return false;
    wchar_t buf[MAX_PATH * 2]; DWORD n = static_cast<DWORD>(_countof(buf));
    if (QueryFullProcessImageNameW(h, 0, buf, &n))
    {
        const wchar_t* b = wcsrchr(buf, L'\\');
        exe = b ? b + 1 : buf;
    }
    wchar_t fam[256]; UINT32 len = static_cast<UINT32>(_countof(fam));
    if (GetPackageFamilyName(h, &len, fam) == ERROR_SUCCESS) family = fam;
    CloseHandle(h);
    return !exe.empty();
}

// The windows that carry an app id (browsers set it per window) say which process it is.
static const PROPERTYKEY kAumidKey = { { 0x9F4C2855, 0x9F79, 0x4B39, { 0xA8, 0xD0, 0xE1, 0xD4, 0x2D, 0xE1, 0xD5, 0xF3 } }, 5 };

struct WindowSearch { const std::wstring* aumid; std::vector<std::wstring>* exes; };

static BOOL CALLBACK WindowAumid(HWND hwnd, LPARAM lp)
{
    WindowSearch* ws = reinterpret_cast<WindowSearch*>(lp);
    if (!IsWindowVisible(hwnd)) return TRUE;
    IPropertyStore* ps = nullptr;
    if (FAILED(SHGetPropertyStoreForWindow(hwnd, IID_PPV_ARGS(&ps))) || !ps) return TRUE;
    PROPVARIANT pv; PropVariantInit(&pv);
    if (SUCCEEDED(ps->GetValue(kAumidKey, &pv)) && pv.vt == VT_LPWSTR && pv.pwszVal)
    {
        const size_t n = ws->aumid->size();
        if (_wcsnicmp(pv.pwszVal, ws->aumid->c_str(), n) == 0 && (pv.pwszVal[n] == 0 || pv.pwszVal[n] == L'.'))
        {
            DWORD pid = 0; GetWindowThreadProcessId(hwnd, &pid);
            std::wstring exe, fam;
            if (pid && pid != GetCurrentProcessId() && ProcessIdentity(pid, exe, fam))
            {
                bool have = false;
                for (const std::wstring& e : *ws->exes) if (IEquals(e, exe)) have = true;
                if (!have) ws->exes->push_back(exe);
            }
        }
    }
    PropVariantClear(&pv);
    ps->Release();
    return TRUE;
}

static void ResolveTarget(const std::wstring& aumid, AppTarget& t)
{
    t = AppTarget();
    t.aumid = aumid;
    if (aumid.empty()) return;
    const size_t bang = aumid.find(L'!');
    if (bang != std::wstring::npos) { t.family = aumid.substr(0, bang); return; }         // a packaged app
    if (aumid.size() > 4 && _wcsicmp(aumid.c_str() + aumid.size() - 4, L".exe") == 0)     // "Spotify.exe", or a path
    {
        const size_t s = aumid.find_last_of(L"\\/");
        t.exes.push_back(s == std::wstring::npos ? aumid : aumid.substr(s + 1));
        return;
    }
    static const struct { const wchar_t* id; const wchar_t* exe; } kAlias[] = {
        { L"Chrome", L"chrome.exe" }, { L"MSEdge", L"msedge.exe" }, { L"Brave", L"brave.exe" },
        { L"Opera", L"opera.exe" }, { L"Vivaldi", L"vivaldi.exe" }, { L"Firefox", L"firefox.exe" },
        { L"308046B0AF4A39CB", L"firefox.exe" }, { L"6F193CCC56814779", L"firefox.exe" },
    };
    for (const auto& a : kAlias)
    {
        const size_t n = wcslen(a.id);
        if (_wcsnicmp(aumid.c_str(), a.id, n) == 0 && (aumid.size() == n || aumid[n] == L'.')) { t.exes.push_back(a.exe); return; }
    }
    WindowSearch ws = { &aumid, &t.exes };
    EnumWindows(WindowAumid, reinterpret_cast<LPARAM>(&ws));
    if (t.exes.empty()) t.exes.push_back(aumid + L".exe");                                // the last guess
}

static bool TargetMatches(const AppTarget& t, const std::wstring& exe, const std::wstring& family)
{
    if (!t.family.empty()) return !family.empty() && IEquals(t.family, family);
    for (const std::wstring& e : t.exes) if (IEquals(e, exe)) return true;
    return false;
}

// Every audio session on every active output: fn(control, pid). Never the game's own, never the
// system sounds.
template <class Fn>
static void ForEachAudioSession(Fn fn)
{
    IMMDeviceEnumerator* en = nullptr;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&en))) || !en) return;
    IMMDeviceCollection* devs = nullptr;
    if (SUCCEEDED(en->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &devs)) && devs)
    {
        UINT nd = 0; devs->GetCount(&nd);
        for (UINT i = 0; i < nd && i < 32; ++i)
        {
            IMMDevice* dev = nullptr;
            if (FAILED(devs->Item(i, &dev)) || !dev) continue;
            IAudioSessionManager2* mgr = nullptr;
            if (SUCCEEDED(dev->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(&mgr))) && mgr)
            {
                IAudioSessionEnumerator* se = nullptr;
                if (SUCCEEDED(mgr->GetSessionEnumerator(&se)) && se)
                {
                    int ns = 0; se->GetCount(&ns);
                    for (int k = 0; k < ns && k < 256; ++k)
                    {
                        IAudioSessionControl* c = nullptr;
                        if (FAILED(se->GetSession(k, &c)) || !c) continue;
                        IAudioSessionControl2* c2 = nullptr;
                        if (SUCCEEDED(c->QueryInterface(IID_PPV_ARGS(&c2))) && c2)
                        {
                            DWORD pid = 0;
                            if (c2->IsSystemSoundsSession() != S_OK && c2->GetProcessId(&pid) == S_OK && pid && pid != GetCurrentProcessId())
                                fn(c2, pid);
                            c2->Release();
                        }
                        c->Release();
                    }
                    se->Release();
                }
                mgr->Release();
            }
            dev->Release();
        }
        devs->Release();
    }
    en->Release();
}

// ---- the app's loudness ----------------------------------------------------------------------------
struct HeldSession
{
    ISimpleAudioVolume* vol = nullptr;
    std::wstring id;                        // the session instance (a re-scan finds it again by this)
    std::wstring name;                      // its exe (or package family): the ledger's key
    float saved = 1.0f;                     // the app's own volume, given back
    float lastSet = -1.0f;
    uint32_t until = 0;                     // retiring: given back at this tick
};

struct AppVolume
{
    AppTarget target;
    std::vector<HeldSession> held;          // the app's sessions while the radio holds it
    std::vector<HeldSession> retiring;      // a previous app's, silenced, given back in a moment
    bool     holding = false;
    float    factor = 1.0f;
    uint32_t scanAt = 0;
    std::vector<std::pair<std::wstring, float>> pending;   // from the ledger: volumes still to give back
    uint32_t pendingAt = 0;
    int      faults = 0;
    bool     canHold = false;               // the target app has an audio session the radio can set
    uint32_t probeAt = 0;                   // ... last looked for (while not holding)
};

static std::wstring LedgerPath() { return ModuleDir() + L"lcudrive_app_volumes.txt"; }

static void WriteLedger(const AppVolume& a)
{
    const std::wstring path = LedgerPath();
    radio::VolumeLedger led;
    for (const HeldSession& h : a.held) led.Add(Narrow(h.name), h.saved);
    for (const HeldSession& h : a.retiring) led.Add(Narrow(h.name), h.saved);
    for (const auto& p : a.pending) led.Add(Narrow(p.first), p.second);
    if (led.Empty()) { DeleteFileW(path.c_str()); return; }
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"wb") != 0 || !f) return;
    const std::string t = led.Text();
    fwrite(t.data(), 1, t.size(), f);
    fclose(f);
}

static void ReadLedger(AppVolume& a)
{
    FILE* f = nullptr;
    if (_wfopen_s(&f, LedgerPath().c_str(), L"rb") != 0 || !f) return;
    std::string text;
    char buf[4096]; size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) text.append(buf, n);
    fclose(f);
    radio::VolumeLedger led;
    led.Parse(text);
    for (const auto& e : led.e)
    {
        wchar_t w[260] = {};
        MultiByteToWideChar(CP_UTF8, 0, e.first.c_str(), -1, w, 260);
        a.pending.push_back(std::make_pair(std::wstring(w), e.second));
    }
    if (!a.pending.empty()) Log(">>> radio: your music app: %d app volume(s) left held last time - giving them back\n", static_cast<int>(a.pending.size()));
}

// The ledger's leftovers, for whatever is running now (looked for again every 10 s until done).
static void ApplyPending(AppVolume& a, uint32_t tick)
{
    if (a.pending.empty() || (a.pendingAt && tick - a.pendingAt < 10000)) return;
    a.pendingAt = tick;
    bool changed = false;
    ForEachAudioSession([&](IAudioSessionControl2* c, DWORD pid) {
        std::wstring exe, fam;
        if (!ProcessIdentity(pid, exe, fam)) return;
        for (size_t i = 0; i < a.pending.size(); ++i)
        {
            if (!IEquals(a.pending[i].first, exe) && !(fam.size() && IEquals(a.pending[i].first, fam))) continue;
            ISimpleAudioVolume* v = nullptr;
            if (SUCCEEDED(c->QueryInterface(IID_PPV_ARGS(&v))) && v)
            {
                v->SetMasterVolume(a.pending[i].second, nullptr);
                v->Release();
                Log("    radio: gave %s its volume back (%.2f) - the radio held it when the game last closed\n", Narrow(exe).c_str(), a.pending[i].second);
            }
            a.pending.erase(a.pending.begin() + static_cast<long>(i));
            changed = true;
            break;
        }
    });
    if (changed) WriteLedger(a);
}

static void SetVolume(HeldSession& h, float v)
{
    if (!h.vol) return;
    if (h.lastSet >= 0.0f && std::fabs(v - h.lastSet) < 0.002f) return;
    h.vol->SetMasterVolume(v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v), nullptr);
    h.lastSet = v;
}

// Finds the target app's sessions; new ones (a browser opens another, Spotify changes output device)
// are saved and taken. Every 3 s while holding.
static void ScanSessions(AppVolume& a, uint32_t tick)
{
    a.scanAt = tick;
    if (a.target.aumid.empty()) return;
    bool added = false;
    ForEachAudioSession([&](IAudioSessionControl2* c, DWORD pid) {
        std::wstring exe, fam;
        if (!ProcessIdentity(pid, exe, fam) || !TargetMatches(a.target, exe, fam)) return;
        LPWSTR sid = nullptr;
        std::wstring id;
        if (SUCCEEDED(c->GetSessionInstanceIdentifier(&sid)) && sid) { id = sid; CoTaskMemFree(sid); }
        for (const HeldSession& h : a.held) if (h.id == id) return;
        HeldSession h;
        if (FAILED(c->QueryInterface(IID_PPV_ARGS(&h.vol))) || !h.vol) return;
        h.id = id;
        h.name = a.target.family.empty() ? exe : a.target.family;
        h.saved = 1.0f;
        h.vol->GetMasterVolume(&h.saved);
        for (size_t i = 0; i < a.pending.size(); ++i)            // still at last time's level: the real one
            if (IEquals(a.pending[i].first, h.name)) { h.saved = a.pending[i].second; a.pending.erase(a.pending.begin() + static_cast<long>(i)); break; }
        SetVolume(h, h.saved * a.factor);
        Log("    radio: your music app holds %s's volume (its own: %.2f)\n", Narrow(exe).c_str(), h.saved);
        a.held.push_back(h);
        added = true;
    });
    a.canHold = !a.held.empty();
    if (added) WriteLedger(a);
}

// Whether the target app has an audio session the radio could set (looked for every 3 s while the
// radio doesn't hold it; holding, ScanSessions says). Without one the controller runs play / pause
// only: an app that won't pause can't be silenced, so the music wins over the DJ (radio_app_model.h).
static void Probe(AppVolume& a, uint32_t tick)
{
    if (a.holding) return;
    if (a.probeAt && tick - a.probeAt < 3000) return;
    a.probeAt = tick ? tick : 1;
    bool found = false;
    if (!a.target.aumid.empty())
        ForEachAudioSession([&](IAudioSessionControl2* c, DWORD pid) {
            std::wstring exe, fam;
            if (found || !ProcessIdentity(pid, exe, fam) || !TargetMatches(a.target, exe, fam)) return;
            ISimpleAudioVolume* v = nullptr;
            if (SUCCEEDED(c->QueryInterface(IID_PPV_ARGS(&v))) && v) { found = true; v->Release(); }
        });
    a.canHold = found;
}

static void Hold(AppVolume& a, float factor, uint32_t tick)
{
    a.factor = factor;
    if (a.holding) return;
    a.holding = true;
    ScanSessions(a, tick);
}

// The radio's level for the app (its own volume x factor). A slider the listener moved meanwhile in
// the volume mixer becomes their new own volume.
static void Apply(AppVolume& a, float factor)
{
    for (HeldSession& h : a.held)
    {
        const float v = h.saved * factor;
        if (h.lastSet >= 0.0f && std::fabs(v - h.lastSet) < 0.002f) continue;
        float now = h.lastSet;
        if (h.vol && h.lastSet >= 0.0f && SUCCEEDED(h.vol->GetMasterVolume(&now)) && std::fabs(now - h.lastSet) > 0.02f && a.factor > 0.25f)
            h.saved = std::fmin(1.0f, now / a.factor);
        SetVolume(h, h.saved * factor);
    }
    a.factor = factor;
}

static void Release(AppVolume& a, const char* why)
{
    if (!a.holding && a.held.empty()) return;
    for (HeldSession& h : a.held)
        if (h.vol) { h.vol->SetMasterVolume(h.saved, nullptr); h.vol->Release(); h.vol = nullptr; }
    if (!a.held.empty()) Log("    radio: your music app gets %s's volume back (%.2f)%s%s\n", Narrow(a.held[0].name).c_str(),
                             a.held[0].saved, why ? " - " : "", why ? why : "");
    a.held.clear();
    a.holding = false;
    WriteLedger(a);
}

// Another app is bound now: the old one's sessions are silenced now and given back in a moment
// (2 s: after the pause radio.h sent it has taken), the new one is looked up.
static void Retarget(AppVolume& a, const std::wstring& aumid, uint32_t tick)
{
    if (a.target.aumid == aumid) return;
    for (HeldSession& h : a.held) { SetVolume(h, 0.0f); h.until = tick + 2000; a.retiring.push_back(h); }
    a.held.clear();
    ResolveTarget(aumid, a.target);
    a.canHold = false; a.probeAt = 0;
    if (a.holding) ScanSessions(a, tick);
    else Probe(a, tick);
    WriteLedger(a);
}

static void Tick(AppVolume& a, uint32_t tick)
{
    bool changed = false;
    for (size_t i = 0; i < a.retiring.size();)
    {
        HeldSession& h = a.retiring[i];
        if (static_cast<LONG>(tick - h.until) < 0) { ++i; continue; }
        if (h.vol) { h.vol->SetMasterVolume(h.saved, nullptr); h.vol->Release(); }
        a.retiring.erase(a.retiring.begin() + static_cast<long>(i));
        changed = true;
    }
    if (changed) WriteLedger(a);
    if (a.holding && tick - a.scanAt > 3000) ScanSessions(a, tick);
    else if (!a.holding) Probe(a, tick);
    ApplyPending(a, tick);
}

static void ReleaseAll(AppVolume& a, const char* why)
{
    for (HeldSession& h : a.retiring) if (h.vol) { h.vol->SetMasterVolume(h.saved, nullptr); h.vol->Release(); }
    a.retiring.clear();
    Release(a, why);
    WriteLedger(a);
}

// ---- the app: Windows' media session link (C++/WinRT) ------------------------------------------------
namespace wmc = winrt::Windows::Media::Control;
namespace wfn = winrt::Windows::Foundation;
using SmtcManager = wmc::GlobalSystemMediaTransportControlsSessionManager;
using SmtcSession = wmc::GlobalSystemMediaTransportControlsSession;
using SmtcProps = wmc::GlobalSystemMediaTransportControlsSessionMediaProperties;

// Lives on the heap and is deleted on the radio thread: a global holding WinRT references would be
// released at process exit, after COM is gone.
struct Smtc
{
    SmtcManager mgr{ nullptr };
    wfn::IAsyncOperation<SmtcManager> mgrOp{ nullptr };
    uint32_t    mgrAt = 0;
    SmtcSession sess{ nullptr };
    std::wstring aumid;                     // the bound app ("" = none)
    std::wstring want = L"auto";            // service_app
    LONG        serial = 0;                 // bumps when the bound app changes
    wfn::IAsyncOperation<SmtcProps> propsOp{ nullptr };
    uint32_t    propsAt = 0, sessAt = 0, infoAt = 0, tlAt = 0;
    std::string key, title, artist;         // what it plays (UTF-8; key = title \x1f artist, "" = not known)
    bool        propsKnown = false;         // its track details were read since it was bound
    bool        rebound = false;            // the bound app changed from one app straight to another:
    SmtcSession prevSess{ nullptr };        // ... the old one's session (radio.h may pause it once)
    bool        prevPlaying = false;        // ... it played at the change
    std::wstring prevAumid;
    std::wstring lastApp;                   // the last app named in the log
    bool        playing = false;
    bool        seekable = false;
    bool        timeline = false;           // its timeline is usable
    double      lenS = 0.0, posS = 0.0;     // ... the track's length and the (extrapolated) position
    double      lastPos = -1.0;
    int         faults = 0;                 // in a row
    bool        unavailable = false;        // this Windows has no media session API
    bool        dead = false;               // faulted too often: off for this session
    bool        loggedUp = false;
    uint32_t    pollMs = 100;               // backs off to 250 ms if the calls are slow
    double      slowestMs = 0.0;
    uint32_t    slowAt = 0;
    std::string lastLogged;                 // the last track logged
};

// A media session call threw. Faults count in a row across re-picks (only a whole poll of a live
// session resets the count): after 3 the app counts as gone (unbound: its stations fall back after
// radio.h's 5 s debounce, a take is released), every 5th the manager is asked for again (a dead
// proxy after the media broker restarted), at 30 the link is off for the session.
static void SmtcFault(Smtc& m, HRESULT hr, const char* what)
{
    m.sess = nullptr; m.propsOp = nullptr; m.playing = false; m.timeline = false;
    // looked for again in 0.25 s (not the usual 2 s): a one-off fault never reads as the listener's
    // own pause (1.5 s); a link that keeps failing reaches 30 in about 8 s
    if (m.sessAt) m.sessAt = GetTickCount() - 1750;
    if (!m.mgr && (hr == REGDB_E_CLASSNOTREG || hr == CLASS_E_CLASSNOTAVAILABLE || hr == E_NOTIMPL || hr == static_cast<HRESULT>(0x80040111)))
    {
        m.unavailable = true;
        InterlockedExchange(&g_appOff, 1);
        Log("!!! radio: your music app: this Windows has no media session link (%s, 0x%08lX) - service stations play your library or talk\n",
            what, static_cast<unsigned long>(hr));
        return;
    }
    ++m.faults;
    if (m.faults >= 3 && !m.aumid.empty())
    {
        if (m.faults == 3)
            Log("    radio: your music app: the media session link failed (%s, 0x%08lX) - looking for it again\n", what, static_cast<unsigned long>(hr));
        m.aumid.clear(); ++m.serial;
        m.key.clear(); m.title.clear(); m.artist.clear(); m.propsKnown = false; m.lastPos = -1.0;
        m.rebound = false; m.prevSess = nullptr;
    }
    if (m.faults % 5 == 0) { m.mgr = nullptr; m.mgrOp = nullptr; m.mgrAt = 0; }
    if (m.faults >= 30)
    {
        m.dead = true;
        InterlockedExchange(&g_appOff, 1);
        Log("!!! radio: your music app: the media session link keeps failing (%s, 0x%08lX) - off for this session\n",
            what, static_cast<unsigned long>(hr));
    }
}

static double Secs(wfn::TimeSpan t) { return std::chrono::duration<double>(t).count(); }

// Which session (radio_app_model.h PickAumid): service_app = any -> the media keys' one; auto ->
// Spotify's when it has one, else the media keys' one; otherwise the first app id containing the
// text. `keep`: the radio has the bound app now - it stays bound while its session is there.
static SmtcSession PickSession(Smtc& m, bool keep)
{
    std::vector<SmtcSession> all;
    std::vector<std::string> ids;
    for (auto const& s : m.mgr.GetSessions())
    {
        if (!s) continue;
        all.push_back(s);
        ids.push_back(Narrow(std::wstring(s.SourceAppUserModelId())));
    }
    const SmtcSession cur = m.mgr.GetCurrentSession();
    const std::string curId = cur ? Narrow(std::wstring(cur.SourceAppUserModelId())) : std::string();
    const std::string pick = radio::PickAumid(ids, curId, Narrow(m.want), Narrow(m.aumid), keep);
    if (pick.empty()) return SmtcSession{ nullptr };
    if (keep && m.sess && pick == Narrow(m.aumid))
        for (const SmtcSession& s : all) if (s == m.sess) return s;          // the very session the radio has
    if (cur && pick == curId) return cur;
    for (size_t i = 0; i < all.size(); ++i) if (ids[i] == pick) return all[i];
    return SmtcSession{ nullptr };
}

static void SmtcPoll(Smtc& m, uint32_t tick, bool keep)
{
    if (m.dead || m.unavailable) return;
    const char* what = "the manager";
    LARGE_INTEGER qf, q0, q1; QueryPerformanceFrequency(&qf); QueryPerformanceCounter(&q0);
    try
    {
        if (!m.mgr)
        {
            if (!m.mgrOp)
            {
                if (m.mgrAt && tick - m.mgrAt < 5000) return;
                m.mgrAt = tick;
                m.mgrOp = SmtcManager::RequestAsync();
            }
            const wfn::AsyncStatus st = m.mgrOp.Status();
            if (st == wfn::AsyncStatus::Started)
            {
                if (tick - m.mgrAt > 5000) { m.mgrOp.Cancel(); m.mgrOp = nullptr; m.mgrAt = tick; Log("    radio: your music app: the media session link is slow to answer - asking again\n"); }
                return;
            }
            if (st == wfn::AsyncStatus::Completed) m.mgr = m.mgrOp.GetResults();
            m.mgrOp = nullptr; m.mgrAt = tick;
            if (!m.mgr) return;
            if (!m.loggedUp) { m.loggedUp = true; Log(">>> radio: your music app: linked to Windows' media sessions\n"); }
        }
        what = "the session";
        if (!m.sessAt || tick - m.sessAt >= 2000)
        {
            m.sessAt = tick ? tick : 1;
            SmtcSession s = PickSession(m, keep);
            const std::wstring id = s ? std::wstring(s.SourceAppUserModelId()) : std::wstring();
            if (id != m.aumid)
            {
                // straight from one app to another: radio.h starts the controller over (and may
                // pause the old one); from or to none it simply takes / lets go
                m.rebound = !m.aumid.empty() && !id.empty();
                m.prevSess = m.rebound ? m.sess : SmtcSession{ nullptr };
                m.prevPlaying = m.rebound && m.playing;
                m.prevAumid = m.aumid;
                m.aumid = id; ++m.serial;
                m.key.clear(); m.title.clear(); m.artist.clear(); m.propsKnown = false;
                m.propsOp = nullptr; m.propsAt = 0; m.infoAt = 0; m.tlAt = 0; m.playing = false; m.timeline = false; m.lastPos = -1.0;
                if (!id.empty() && id != m.lastApp) Log(">>> radio: your music app: %s\n", Narrow(id).c_str());
                if (!id.empty()) m.lastApp = id;
            }
            m.sess = s;
        }
        if (!m.sess) { m.playing = false; m.timeline = false; return; }     // (not a success: faults keep counting)
        what = "the playback state";
        if (tick - m.infoAt >= m.pollMs)
        {
            m.infoAt = tick;
            const auto info = m.sess.GetPlaybackInfo();
            m.playing = info && info.PlaybackStatus() == wmc::GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing;
            m.seekable = info && info.Controls() && info.Controls().IsPlaybackPositionEnabled();
        }
        what = "the timeline";
        bool jumped = false;
        if (tick - m.tlAt >= m.pollMs)
        {
            m.tlAt = tick;
            const auto tl = m.sess.GetTimelineProperties();
            if (tl)
            {
                const double start = Secs(tl.StartTime()), end = Secs(tl.EndTime());
                double pos = Secs(tl.Position());
                if (m.playing)
                {
                    const double since = std::chrono::duration<double>(winrt::clock::now() - tl.LastUpdatedTime()).count();
                    if (since > 0.0 && since < 3600.0) pos += since;
                }
                m.timeline = end - start > 0.5 && end - start < 6 * 3600.0;
                m.lenS = end - start;
                m.posS = pos - start;
                if (m.posS > m.lenS) m.posS = m.lenS;
                if (m.lastPos >= 0.0 && m.posS + 2.0 < m.lastPos) jumped = true;     // back to a track's start
                m.lastPos = m.posS;
            }
            else m.timeline = false;
        }
        what = "the track";
        if (m.propsOp)
        {
            const wfn::AsyncStatus st = m.propsOp.Status();
            if (st == wfn::AsyncStatus::Completed)
            {
                const SmtcProps p = m.propsOp.GetResults();
                m.propsOp = nullptr;
                if (p)
                {
                    m.title = Narrow(std::wstring(p.Title())); m.artist = Narrow(std::wstring(p.Artist()));
                    m.key = m.title.empty() && m.artist.empty() ? std::string() : m.title + "\x1f" + m.artist;
                    m.propsKnown = true;
                }
            }
            else if (st != wfn::AsyncStatus::Started) m.propsOp = nullptr;
            else if (tick - m.propsAt > 3000) { m.propsOp.Cancel(); m.propsOp = nullptr; }
        }
        else if (jumped || tick - m.propsAt >= 1000)
        {
            m.propsAt = tick;
            m.propsOp = m.sess.TryGetMediaPropertiesAsync();
        }
        m.faults = 0;
    }
    catch (winrt::hresult_error const& e) { SmtcFault(m, static_cast<HRESULT>(e.code()), what); }
    catch (...) { SmtcFault(m, E_FAIL, what); }
    // the slowest synchronous poll, once a minute; slower than 2 ms: poll every 250 ms instead
    QueryPerformanceCounter(&q1);
    const double ms = static_cast<double>(q1.QuadPart - q0.QuadPart) * 1000.0 / static_cast<double>(qf.QuadPart);
    if (ms > m.slowestMs) m.slowestMs = ms;
    if (!m.slowAt) m.slowAt = tick;
    if (tick - m.slowAt >= 60000)
    {
        if (m.slowestMs > 2.0 && m.pollMs < 250) { m.pollMs = 250; Log("    radio: your music app: the media session answers slowly (%.1f ms) - asking every 250 ms\n", m.slowestMs); }
        m.slowestMs = 0.0; m.slowAt = tick;
    }
}

// Play / pause, fire and forget: the next playback poll says whether it took.
static void SmtcCommand(Smtc& m, uint8_t cmd)
{
    if (!m.sess || cmd == radio::APP_NONE) return;
    try
    {
        if (cmd == radio::APP_PLAY) m.sess.TryPlayAsync();
        else m.sess.TryPauseAsync();
        m.infoAt = 0;
    }
    catch (winrt::hresult_error const& e) { SmtcFault(m, static_cast<HRESULT>(e.code()), cmd == radio::APP_PLAY ? "play" : "pause"); }
    catch (...) { SmtcFault(m, E_FAIL, cmd == radio::APP_PLAY ? "play" : "pause"); }
}

// The app bound before a rebind, paused once (it played in the radio's hands). Its session may be
// gone already: nothing then.
static void SmtcPausePrev(Smtc& m)
{
    if (!m.prevSess) return;
    try { m.prevSess.TryPauseAsync(); }
    catch (...) {}
    m.prevSess = nullptr;
}

// Back to the start of the track (a break paused it just after it began). Only where the app takes a position.
static void SmtcSeek0(Smtc& m)
{
    if (!m.sess || !m.seekable) return;
    try { m.sess.TryChangePlaybackPositionAsync(0); m.tlAt = 0; }
    catch (...) {}
}

// At the end (the radio stops for good): the command, waiting up to `ms` for it to take - on the
// radio thread, never under the loader lock.
static void SmtcCommandWait(Smtc& m, uint8_t cmd, uint32_t ms)
{
    if (!m.sess || cmd == radio::APP_NONE) return;
    try
    {
        auto op = cmd == radio::APP_PLAY ? m.sess.TryPlayAsync() : m.sess.TryPauseAsync();
        op.wait_for(std::chrono::milliseconds(ms));
    }
    catch (...) {}
}

// ---- links --------------------------------------------------------------------------------------------
static DWORD WINAPI LinkThread(LPVOID p)
{
    std::wstring* urls = static_cast<std::wstring*>(p);          // "first\0fallback"
    const HRESULT co = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    const size_t z = urls->find(L'\0');
    const std::wstring first = urls->substr(0, z), second = z == std::wstring::npos ? std::wstring() : urls->substr(z + 1);
    for (const std::wstring* u : { &first, &second })
    {
        if (u->empty()) continue;
        SHELLEXECUTEINFOW sei = {};
        sei.cbSize = sizeof(sei);
        sei.fMask = SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
        sei.lpVerb = L"open";
        sei.lpFile = u->c_str();
        sei.nShow = SW_SHOWNOACTIVATE;
        if (ShellExecuteExW(&sei)) break;
        Log("!!! radio: couldn't open %s (error %lu)%s\n", Narrow(*u).c_str(), GetLastError(), (u == &first && !second.empty()) ? " - trying the web link" : "");
    }
    if (SUCCEEDED(co)) CoUninitialize();
    delete urls;
    return 0;
}

// Opens a playlist link in the listener's app: spotify: (the desktop app; its web link if that
// fails) or https:// only - nothing else is ever launched.
static bool LaunchLink(const std::string& value)
{
    std::string canon;
    const uint8_t form = radio::PlaylistForm(value, &canon);
    if (form != radio::PL_SPOTIFY && form != radio::PL_WEB) return false;
    std::wstring both;
    auto widen = [](const std::string& s) {
        std::wstring w(static_cast<size_t>(MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0)), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], static_cast<int>(w.size()));
        if (!w.empty() && w.back() == L'\0') w.pop_back();
        return w;
    };
    if (form == radio::PL_SPOTIFY)
    {
        // spotify:<type>:<id> -> https://open.spotify.com/<type>/<id> as the fallback
        const size_t a = canon.find(':'), b = canon.find(':', a + 1);
        const std::string web = "https://open.spotify.com/" + canon.substr(a + 1, b - a - 1) + "/" + canon.substr(b + 1);
        both = widen(canon); both.push_back(L'\0'); both += widen(web);
    }
    else both = widen(canon);
    std::wstring* p = new (std::nothrow) std::wstring(both);
    if (!p) return false;
    HANDLE t = CreateThread(nullptr, 0, LinkThread, p, 0, nullptr);
    if (!t) { delete p; return false; }
    CloseHandle(t);
    return true;
}

} // namespace app
} // namespace rd
