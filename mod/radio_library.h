// =============================================================================================
//  radio_library.h - the listener's music files for the radio's library mode (spec v1 section 7.1).
//  One background thread (created on the first job, background CPU / disk priority, COM
//  multithreaded) walks the music folders, reads each file's tags with Windows' own property
//  handlers (or the 128-byte ID3v1 tag), keeps what it learned in a cache beside the DLL, sorts
//  every file onto a station with radio_genre.h, and loads the per-station playlists (a folder or
//  an .m3u). The radio thread posts jobs and takes results; nothing here touches game memory.
//
//    PostScanJob(job)   the latest job wins (an unstarted older one is dropped)
//    TakeScanResult()   the radio thread takes a finished result (or nullptr)
//    g_libOff           set when the scan faults: library stations talk (or play the AI songs)
// =============================================================================================
#pragma once
#include <objbase.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <shlwapi.h>
#include <propsys.h>
#include <propvarutil.h>
#include <string>
#include <vector>
#include <unordered_map>
#include "radio_model.h"
#include "radio_genre.h"
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "propsys.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "advapi32.lib")

namespace rd {
namespace lib {

// ---- jobs and results --------------------------------------------------------------------------------
struct StationJob
{
    std::string  id;
    uint8_t      genre = radio::G_NONE;
    std::string  extra;            // genre_<id> words
    uint8_t      form = radio::PL_NONE;
    std::wstring list;             // the folder or .m3u of playlist_<id> (absolute)
};

struct ScanJob
{
    uint32_t gen = 0;
    std::vector<std::wstring> roots;           // empty: the Windows Music library
    std::vector<StationJob> stations;
    std::string skip;                          // library_skip
    int holiday = 0, month = 1, unsorted = -1; // radio::LibOptions
    std::wstring cache;                        // the cache file ("" = none)
};

struct StationResult
{
    std::vector<radio::Track> genre;           // the station's genre pool
    std::vector<radio::Track> over;            // its playlist (folder / .m3u)
    int  overEntries = 0, overMissing = 0;
    bool overRead = false;                     // the playlist was looked at
};

struct ScanResult
{
    uint32_t gen = 0;
    std::vector<StationResult> st;             // as ScanJob::stations
    int files = 0, cached = 0, read = 0, roots = 0;
    int perGenre[8] = {};
    int unsorted = 0, never = 0, skipWords = 0, holidayOut = 0, unplayable = 0, drm = 0, cloud = 0, untagged = 0;
    std::string unplayableExt;                 // ".ogg 29, .opus 2"
    std::string rootsNote;                     // "2 folders" / "the Windows Music library (3 folders)"
    double seconds = 0.0;
    std::vector<std::string> notes;            // log lines the radio thread writes
    std::vector<std::string> warnings;         // ... with '!!! '
    bool fault = false;
};

static volatile LONG g_libOff = 0;             // the scan faulted: the library is off for this session
static volatile LONG g_scanStop = 0;
static HANDLE        g_scanThread = nullptr;
static HANDLE        g_jobEvent = nullptr;
static ScanJob* volatile    g_job = nullptr;
static ScanResult* volatile g_result = nullptr;

// ---- strings -----------------------------------------------------------------------------------------
static std::string ToUtf8(const std::wstring& w)
{
    if (w.empty()) return std::string();
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(n > 0 ? n : 0), '\0');
    if (n > 0) WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), &s[0], n, nullptr, nullptr);
    return s;
}

static std::wstring FromUtf8(const std::string& s)
{
    if (s.empty()) return std::wstring();
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(n > 0 ? n : 0), L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), &w[0], n);
    return w;
}

static bool ValidUtf8(const std::string& s)
{
    return s.empty() || MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.c_str(), static_cast<int>(s.size()), nullptr, 0) > 0;
}

static std::wstring LowerW(std::wstring s) { for (wchar_t& c : s) c = static_cast<wchar_t>(towlower(c)); return s; }

static std::string Clean(std::string s)          // no tabs / newlines in a cache field
{
    for (char& c : s) if (c == '\t' || c == '\r' || c == '\n') c = ' ';
    return s;
}

// ---- which files Windows can play ---------------------------------------------------------------------
static const wchar_t* const kCandidates[] = { L".mp3", L".m4a", L".aac", L".wma", L".wav", L".flac", L".webm", L".mka",
                                              L".ogg", L".opus", L".oga" };

struct Playable
{
    bool ok[sizeof(kCandidates) / sizeof(kCandidates[0])] = {};
    int  Index(const wchar_t* name) const
    {
        const wchar_t* dot = wcsrchr(name, L'.');
        if (!dot) return -1;
        for (int i = 0; i < static_cast<int>(sizeof(kCandidates) / sizeof(kCandidates[0])); ++i) if (_wcsicmp(dot, kCandidates[i]) == 0) return i;
        return -1;
    }
};

// ---- Windows' own handlers only -------------------------------------------------------------------------
// A COM class's DLL must be Windows' own (under System32): a third-party codec pack's tag reader or
// file reader is never loaded into the game. Decided once per CLSID.
struct HandlerCheck { std::unordered_map<std::wstring, int> ok; std::wstring sys; std::vector<std::string>* warn = nullptr; };

static bool ClsidInSystem(HandlerCheck& hc, const CLSID& clsid, std::wstring& dll, bool& fresh)
{
    wchar_t g[64] = {};
    StringFromGUID2(clsid, g, 64);
    fresh = false;
    auto it = hc.ok.find(g);
    if (it != hc.ok.end()) return it->second != 0;
    fresh = true;
    if (hc.sys.empty())
    {
        wchar_t s[MAX_PATH] = {};
        GetSystemDirectoryW(s, MAX_PATH);
        hc.sys = LowerW(s);
        if (!hc.sys.empty() && hc.sys.back() != L'\\') hc.sys += L'\\';
    }
    std::wstring key = std::wstring(L"CLSID\\") + g + L"\\InprocServer32";
    wchar_t raw[MAX_PATH * 2] = {}, exp[MAX_PATH * 2] = {};
    DWORD cb = sizeof(raw), type = 0;
    bool ok = false;
    if (RegGetValueW(HKEY_CLASSES_ROOT, key.c_str(), nullptr, RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ | RRF_NOEXPAND, &type, raw, &cb) == ERROR_SUCCESS)
    {
        if (!ExpandEnvironmentStringsW(raw, exp, MAX_PATH * 2)) wcscpy_s(exp, raw);
        std::wstring p = LowerW(exp);
        if (!p.empty() && p[0] == L'"') { p.erase(0, 1); const size_t q = p.find(L'"'); if (q != std::wstring::npos) p.resize(q); }
        ok = p.compare(0, hc.sys.size(), hc.sys) == 0 || p.find(L'\\') == std::wstring::npos;   // bare "x.dll" = System32 search
        dll = exp;
    }
    hc.ok[g] = ok ? 1 : 0;
    return ok;
}

// A Media Foundation byte-stream handler for the extension: what the radio's decoder can open - only
// when every handler registered for it is Windows' own (Media Foundation may try any of them on the
// listener's files, inside the game).
static Playable FindPlayable(HandlerCheck* hcIn = nullptr)
{
    HandlerCheck local;
    HandlerCheck& hc = hcIn ? *hcIn : local;
    Playable p;
    for (int i = 0; i < static_cast<int>(sizeof(kCandidates) / sizeof(kCandidates[0])); ++i)
    {
        std::wstring k = L"SOFTWARE\\Microsoft\\Windows Media Foundation\\ByteStreamHandlers\\";
        k += kCandidates[i];
        HKEY h = nullptr;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, k.c_str(), 0, KEY_READ, &h) != ERROR_SUCCESS) continue;
        int handlers = 0, foreign = 0;
        for (DWORD v = 0; v < 64; ++v)
        {
            wchar_t name[128] = {}; DWORD nn = 128;
            if (RegEnumValueW(h, v, name, &nn, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS) break;
            CLSID clsid;
            if (name[0] != L'{' || FAILED(CLSIDFromString(name, &clsid))) continue;
            ++handlers;
            std::wstring dll; bool fresh = false;
            if (!ClsidInSystem(hc, clsid, dll, fresh))
            {
                ++foreign;
                if (fresh && hc.warn)
                    hc.warn->push_back(std::string("radio: library: ") + ToUtf8(kCandidates[i]) + " files are left out: a file reader for them is not Windows' own (" +
                                       ToUtf8(name) + " " + ToUtf8(dll) + ")");
            }
        }
        RegCloseKey(h);
        p.ok[i] = handlers > 0 && foreign == 0;
    }
    return p;
}

// ---- tags ----------------------------------------------------------------------------------------------
static const PROPERTYKEY kPkGenre       = { { 0x56A3372E, 0xCE9C, 0x11D2, { 0x9F, 0x0E, 0x00, 0x60, 0x97, 0xC6, 0x86, 0xF6 } }, 11 };
static const PROPERTYKEY kPkArtist      = { { 0x56A3372E, 0xCE9C, 0x11D2, { 0x9F, 0x0E, 0x00, 0x60, 0x97, 0xC6, 0x86, 0xF6 } }, 2 };
static const PROPERTYKEY kPkAlbumArtist = { { 0x56A3372E, 0xCE9C, 0x11D2, { 0x9F, 0x0E, 0x00, 0x60, 0x97, 0xC6, 0x86, 0xF6 } }, 13 };
static const PROPERTYKEY kPkAlbum       = { { 0x56A3372E, 0xCE9C, 0x11D2, { 0x9F, 0x0E, 0x00, 0x60, 0x97, 0xC6, 0x86, 0xF6 } }, 4 };
static const PROPERTYKEY kPkTitle       = { { 0xF29F85E0, 0x4FF9, 0x1068, { 0xAB, 0x91, 0x08, 0x00, 0x2B, 0x27, 0xB3, 0xD9 } }, 2 };
static const PROPERTYKEY kPkDuration    = { { 0x64440490, 0x4C8B, 0x11D1, { 0x8B, 0x70, 0x08, 0x00, 0x36, 0xB1, 0x1A, 0x03 } }, 3 };
static const PROPERTYKEY kPkDrm         = { { 0xAEAC19E4, 0x89AE, 0x4508, { 0xB9, 0xB7, 0xBB, 0x86, 0x7A, 0xBE, 0xE2, 0xED } }, 2 };

enum : uint32_t { TF_DRM = 1, TF_ID3V1 = 2, TF_NOHANDLER = 4 };

struct Tags
{
    uint64_t size = 0, mtime = 0;
    float    dur = 0.0f;                       // 0 = not known
    std::string genre;                         // values joined with ';'
    std::string artist, albumArtist, album, title;
    uint32_t flags = 0;
};

static std::string PvText(const PROPVARIANT& pv, const char* join)
{
    std::string out;
    if (pv.vt == VT_LPWSTR && pv.pwszVal) out = ToUtf8(pv.pwszVal);
    else if (pv.vt == (VT_VECTOR | VT_LPWSTR))
        for (ULONG i = 0; i < pv.calpwstr.cElems; ++i)
        {
            if (!pv.calpwstr.pElems[i]) continue;
            if (!out.empty()) out += join;
            out += ToUtf8(pv.calpwstr.pElems[i]);
        }
    return Clean(out);
}

// The file's tag reader must be Windows' own (else the file plays untagged).
static bool HandlerAllowed(HandlerCheck& hc, const CLSID& clsid, const wchar_t* ext)
{
    std::wstring dll; bool fresh = false;
    const bool ok = ClsidInSystem(hc, clsid, dll, fresh);
    if (!ok && fresh && hc.warn)
    {
        wchar_t g[64] = {};
        StringFromGUID2(clsid, g, 64);
        hc.warn->push_back(std::string("radio: library: the tag reader for ") + ToUtf8(ext) + " is not Windows' own (" + ToUtf8(g) + " " +
                           ToUtf8(dll) + ") - those files play untagged");
    }
    return ok;
}

// ID3v1: the last 128 bytes. Latin-1 text; genre byte 255 = none.
static bool ReadId3v1(const std::wstring& path, Tags& t)
{
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER sz = {};
    bool ok = false;
    unsigned char b[128];
    if (GetFileSizeEx(h, &sz) && sz.QuadPart >= 128)
    {
        LARGE_INTEGER off; off.QuadPart = sz.QuadPart - 128;
        DWORD got = 0;
        if (SetFilePointerEx(h, off, nullptr, FILE_BEGIN) && ReadFile(h, b, 128, &got, nullptr) && got == 128 && b[0] == 'T' && b[1] == 'A' && b[2] == 'G')
            ok = true;
    }
    CloseHandle(h);
    if (!ok) return false;
    auto field = [&](int at, int n) {
        int len = 0;
        while (len < n && b[at + len]) ++len;
        while (len > 0 && (b[at + len - 1] == ' ')) --len;
        wchar_t w[64] = {};
        MultiByteToWideChar(28591, 0, reinterpret_cast<const char*>(b + at), len, w, 63);   // Latin-1
        return Clean(ToUtf8(w));
    };
    if (t.title.empty()) t.title = field(3, 30);
    if (t.artist.empty()) t.artist = field(33, 30);
    if (t.album.empty()) t.album = field(63, 30);
    if (t.genre.empty() && b[127] != 255) { char g[16]; _snprintf_s(g, sizeof(g), _TRUNCATE, "(%u)", b[127]); t.genre = g; }
    t.flags |= TF_ID3V1;
    return true;
}

// The file's own property handler, created directly (no shell namespace, no thumbnail work).
static void ReadTags(HandlerCheck& hc, const std::wstring& path, Tags& t)
{
    CLSID clsid;
    const wchar_t* dot = wcsrchr(path.c_str(), L'.');
    if (SUCCEEDED(PSLookupPropertyHandlerCLSID(path.c_str(), &clsid)) && HandlerAllowed(hc, clsid, dot ? dot : L"?"))
    {
        IPropertyStore* ps = nullptr;
        if (SUCCEEDED(CoCreateInstance(clsid, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&ps))) && ps)
        {
            bool inited = false;
            IInitializeWithStream* iws = nullptr;
            if (SUCCEEDED(ps->QueryInterface(IID_PPV_ARGS(&iws))) && iws)
            {
                IStream* st = nullptr;
                if (SUCCEEDED(SHCreateStreamOnFileEx(path.c_str(), STGM_READ | STGM_SHARE_DENY_NONE, FILE_ATTRIBUTE_NORMAL, FALSE, nullptr, &st)) && st)
                {
                    inited = SUCCEEDED(iws->Initialize(st, STGM_READ));
                    st->Release();
                }
                iws->Release();
            }
            if (inited)
            {
                PROPVARIANT pv;
                PropVariantInit(&pv); if (SUCCEEDED(ps->GetValue(kPkGenre, &pv))) t.genre = PvText(pv, ";"); PropVariantClear(&pv);
                PropVariantInit(&pv); if (SUCCEEDED(ps->GetValue(kPkArtist, &pv))) t.artist = PvText(pv, "; "); PropVariantClear(&pv);
                PropVariantInit(&pv); if (SUCCEEDED(ps->GetValue(kPkAlbumArtist, &pv))) t.albumArtist = PvText(pv, "; "); PropVariantClear(&pv);
                PropVariantInit(&pv); if (SUCCEEDED(ps->GetValue(kPkAlbum, &pv))) t.album = PvText(pv, "; "); PropVariantClear(&pv);
                PropVariantInit(&pv); if (SUCCEEDED(ps->GetValue(kPkTitle, &pv))) t.title = PvText(pv, "; "); PropVariantClear(&pv);
                PropVariantInit(&pv);
                if (SUCCEEDED(ps->GetValue(kPkDuration, &pv)) && pv.vt == VT_UI8) t.dur = static_cast<float>(pv.uhVal.QuadPart / 1e7);
                PropVariantClear(&pv);
                PropVariantInit(&pv);
                if (SUCCEEDED(ps->GetValue(kPkDrm, &pv)) && pv.vt == VT_BOOL && pv.boolVal) t.flags |= TF_DRM;
                PropVariantClear(&pv);
            }
            ps->Release();
        }
    }
    else t.flags |= TF_NOHANDLER;
    if (dot && _wcsicmp(dot, L".mp3") == 0 && t.title.empty() && t.genre.empty()) ReadId3v1(path, t);
}

// ---- the cache ------------------------------------------------------------------------------------------
using Cache = std::unordered_map<std::wstring, Tags>;           // key: the lower-cased path

static bool LoadCache(const std::wstring& path, Cache& c)
{
    c.clear();
    if (path.empty()) return false;
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"rb") != 0 || !f) return false;
    std::string text;
    char buf[65536]; size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) text.append(buf, n);
    fclose(f);
    if (text.compare(0, 24, "# lcudrive music cache 1") != 0) return false;
    size_t p = 0;
    while (p < text.size())
    {
        size_t e = text.find('\n', p);
        if (e == std::string::npos) e = text.size();
        std::string line = text.substr(p, e - p);
        p = e + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        std::vector<std::string> fl;
        size_t a = 0;
        while (true) { const size_t b = line.find('\t', a); fl.push_back(line.substr(a, b == std::string::npos ? std::string::npos : b - a)); if (b == std::string::npos) break; a = b + 1; }
        if (fl.size() < 10) continue;
        Tags t;
        t.size = _strtoui64(fl[1].c_str(), nullptr, 10);
        t.mtime = _strtoui64(fl[2].c_str(), nullptr, 10);
        t.dur = static_cast<float>(atof(fl[3].c_str()));
        t.genre = fl[4]; t.artist = fl[5]; t.albumArtist = fl[6]; t.album = fl[7]; t.title = fl[8];
        t.flags = static_cast<uint32_t>(strtoul(fl[9].c_str(), nullptr, 10));
        c[LowerW(FromUtf8(fl[0]))] = t;
    }
    return true;
}

// Written to .tmp, then moved over the old one in one step.
static bool SaveCache(const std::wstring& path, const Cache& c, const std::unordered_map<std::wstring, std::wstring>& names)
{
    if (path.empty()) return false;
    const std::wstring tmp = path + L".tmp";
    FILE* f = nullptr;
    if (_wfopen_s(&f, tmp.c_str(), L"wb") != 0 || !f) return false;
    fputs("# lcudrive music cache 1\n# path\tsize\tmtime\tdur_s\tgenre\tartist\talbumartist\talbum\ttitle\tflags (1 drm, 2 id3v1, 4 no tag reader)\n", f);
    for (const auto& kv : c)
    {
        auto it = names.find(kv.first);
        const Tags& t = kv.second;
        fprintf(f, "%s\t%llu\t%llu\t%.3f\t%s\t%s\t%s\t%s\t%s\t%u\n", ToUtf8(it != names.end() ? it->second : kv.first).c_str(),
                static_cast<unsigned long long>(t.size), static_cast<unsigned long long>(t.mtime), t.dur, t.genre.c_str(), t.artist.c_str(),
                t.albumArtist.c_str(), t.album.c_str(), t.title.c_str(), t.flags);
    }
    const bool ok = fflush(f) == 0;
    fclose(f);
    if (!ok) { DeleteFileW(tmp.c_str()); return false; }
    return MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
}

// ---- the walk --------------------------------------------------------------------------------------------
struct FoundFile { std::wstring path; uint64_t size = 0, mtime = 0; size_t root = 0; };

struct WalkStats { int unplayable = 0, cloud = 0; std::unordered_map<std::wstring, int> badExt; };

static void Walk(const std::wstring& dir, size_t root, const Playable& pl, std::vector<FoundFile>& out, WalkStats& ws, int depth)
{
    if (depth > 12 || out.size() >= 50000 || g_scanStop) return;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileExW((dir + L"\\*").c_str(), FindExInfoBasic, &fd, FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
    if (h == INVALID_HANDLE_VALUE) return;
    do
    {
        const wchar_t* n = fd.cFileName;
        if (n[0] == L'.' && (n[1] == 0 || (n[1] == L'.' && n[2] == 0))) continue;
        if (fd.dwFileAttributes & (FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM)) continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) Walk(dir + L"\\" + n, root, pl, out, ws, depth + 1);   // no junction loops
            continue;
        }
        const int k = pl.Index(n);
        if (k < 0) continue;
        if (!pl.ok[k]) { ++ws.unplayable; ++ws.badExt[kCandidates[k]]; continue; }
        // online-only cloud files (OneDrive...) would download on open: left out
        if (fd.dwFileAttributes & (FILE_ATTRIBUTE_OFFLINE | 0x00040000 /*RECALL_ON_OPEN*/ | 0x00400000 /*RECALL_ON_DATA_ACCESS*/)) { ++ws.cloud; continue; }
        FoundFile f;
        f.path = dir + L"\\" + n;
        f.size = (static_cast<uint64_t>(fd.nFileSizeHigh) << 32) | fd.nFileSizeLow;
        f.mtime = (static_cast<uint64_t>(fd.ftLastWriteTime.dwHighDateTime) << 32) | fd.ftLastWriteTime.dwLowDateTime;
        f.root = root;
        out.push_back(f);
    } while (out.size() < 50000 && !g_scanStop && FindNextFileW(h, &fd));
    FindClose(h);
}

// The music folders: the ini's, else the Windows Music library's (its folders that exist on disk),
// else the Music folder.
static std::vector<std::wstring> MusicRoots(const std::vector<std::wstring>& given, std::string& note)
{
    std::vector<std::wstring> roots;
    for (const std::wstring& r : given) if (!r.empty()) roots.push_back(r);
    if (!roots.empty()) { note = std::to_string(roots.size()) + (roots.size() == 1 ? " folder" : " folders"); return roots; }
    IShellLibrary* sl = nullptr;
    if (SUCCEEDED(SHLoadLibraryFromKnownFolder(FOLDERID_MusicLibrary, STGM_READ, IID_PPV_ARGS(&sl))) && sl)
    {
        IShellItemArray* arr = nullptr;
        if (SUCCEEDED(sl->GetFolders(LFF_FORCEFILESYSTEM, IID_PPV_ARGS(&arr))) && arr)
        {
            DWORD n = 0; arr->GetCount(&n);
            for (DWORD i = 0; i < n && i < 32; ++i)
            {
                IShellItem* it = nullptr;
                if (FAILED(arr->GetItemAt(i, &it)) || !it) continue;
                PWSTR p = nullptr;
                if (SUCCEEDED(it->GetDisplayName(SIGDN_FILESYSPATH, &p)) && p) { roots.push_back(p); CoTaskMemFree(p); }
                it->Release();
            }
            arr->Release();
        }
        sl->Release();
    }
    if (!roots.empty()) { note = "the Windows Music library (" + std::to_string(roots.size()) + (roots.size() == 1 ? " folder)" : " folders)"); return roots; }
    PWSTR m = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Music, 0, nullptr, &m)) && m) roots.push_back(m);
    if (m) CoTaskMemFree(m);
    note = "the Music folder";
    return roots;
}

static std::string FileLabel(const std::wstring& path, const Tags& t)
{
    if (!t.title.empty()) return t.artist.empty() ? t.title : t.artist + " - " + t.title;
    const size_t s = path.find_last_of(L"\\/");
    std::wstring n = s == std::wstring::npos ? path : path.substr(s + 1);
    const size_t d = n.find_last_of(L'.');
    if (d != std::wstring::npos) n.resize(d);
    return Clean(ToUtf8(n));
}

static radio::Track ToTrack(const std::wstring& path, const Tags& t)
{
    radio::Track r;
    r.path8 = ToUtf8(path);
    r.label = FileLabel(path, t);
    if (t.dur > 1.0f) r.dur = t.dur; else { r.dur = 210.0f; r.durGuess = true; }
    return r;
}

// The tags of one file: the cache's when its size and time match, else read (and cached).
struct TagSource
{
    Cache* cache = nullptr;
    std::unordered_map<std::wstring, std::wstring>* names = nullptr;
    std::unordered_map<std::wstring, char>* seen = nullptr;
    HandlerCheck* hc = nullptr;
    int cached = 0, read = 0;
    bool dirty = false;
    const Tags& Get(const std::wstring& path, uint64_t size, uint64_t mtime)
    {
        const std::wstring key = LowerW(path);
        (*seen)[key] = 1;
        auto it = cache->find(key);
        if (it != cache->end() && it->second.size == size && it->second.mtime == mtime) { ++cached; return it->second; }
        Tags t; t.size = size; t.mtime = mtime;
        ReadTags(*hc, path, t);
        ++read; dirty = true;
        (*cache)[key] = t;
        (*names)[key] = path;
        return (*cache)[key];
    }
};

static bool FileInfo(const std::wstring& path, uint64_t& size, uint64_t& mtime, DWORD& attr)
{
    WIN32_FILE_ATTRIBUTE_DATA fa;
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fa) || (fa.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) return false;
    size = (static_cast<uint64_t>(fa.nFileSizeHigh) << 32) | fa.nFileSizeLow;
    mtime = (static_cast<uint64_t>(fa.ftLastWriteTime.dwHighDateTime) << 32) | fa.ftLastWriteTime.dwLowDateTime;
    attr = fa.dwFileAttributes;
    return true;
}

// An .m3u / .m3u8: UTF-8 when it has a BOM or is valid UTF-8 (or is .m3u8), else the ANSI code page.
static bool ReadM3u(const std::wstring& path, std::vector<std::string>& entries, int& netSkipped, const std::vector<std::string>& allowShares)
{
    netSkipped = 0;
    FILE* f = nullptr;
    if (_wfopen_s(&f, path.c_str(), L"rb") != 0 || !f) return false;
    std::string text;
    char buf[65536]; size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0 && text.size() < 16u * 1024 * 1024) text.append(buf, n);
    fclose(f);
    const bool bom = text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF;
    if (!bom && !ValidUtf8(text))
    {
        const int wn = MultiByteToWideChar(CP_ACP, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0);
        std::wstring w(static_cast<size_t>(wn > 0 ? wn : 0), L'\0');
        if (wn > 0) MultiByteToWideChar(CP_ACP, 0, text.c_str(), static_cast<int>(text.size()), &w[0], wn);
        text = ToUtf8(w);
    }
    std::wstring dir = path;
    const size_t s = dir.find_last_of(L"\\/");
    dir = s == std::wstring::npos ? std::wstring() : dir.substr(0, s + 1);
    radio::ParseM3u(text, ToUtf8(dir), entries, &netSkipped, &allowShares);
    return true;
}

// ---- one job --------------------------------------------------------------------------------------------
static ScanResult* RunJob(const ScanJob& job)
{
    LARGE_INTEGER qf, q0, q1; QueryPerformanceFrequency(&qf); QueryPerformanceCounter(&q0);
    ScanResult* r = new ScanResult();
    r->gen = job.gen;
    r->st.resize(job.stations.size());
    HandlerCheck hc; hc.warn = &r->warnings;
    const Playable pl = FindPlayable(&hc);
    Cache cache;
    LoadCache(job.cache, cache);
    std::unordered_map<std::wstring, std::wstring> names;          // lower -> as found
    for (const auto& kv : cache) names[kv.first] = kv.first;
    std::unordered_map<std::wstring, char> seen;
    TagSource ts; ts.cache = &cache; ts.names = &names; ts.seen = &seen; ts.hc = &hc;

    // 1. the genre pools: every file under the music folders
    std::vector<std::wstring> roots = MusicRoots(job.roots, r->rootsNote);
    r->roots = static_cast<int>(roots.size());
    std::vector<FoundFile> files;
    WalkStats ws;
    for (size_t i = 0; i < roots.size() && !g_scanStop; ++i)
    {
        std::wstring root = roots[i];
        while (root.size() > 3 && (root.back() == L'\\' || root.back() == L'/')) root.pop_back();
        roots[i] = root;
        const DWORD a = GetFileAttributesW(root.c_str());
        if (a == INVALID_FILE_ATTRIBUTES || !(a & FILE_ATTRIBUTE_DIRECTORY)) { r->warnings.push_back("radio: library: " + ToUtf8(root) + " - no such folder"); continue; }
        Walk(root, i, pl, files, ws, 0);
    }
    r->files = static_cast<int>(files.size());
    r->unplayable = ws.unplayable; r->cloud = ws.cloud;
    for (const auto& kv : ws.badExt) { if (!r->unplayableExt.empty()) r->unplayableExt += ", "; r->unplayableExt += ToUtf8(kv.first) + " " + std::to_string(kv.second); }

    radio::GenreClassifier gc;
    for (const StationJob& sj : job.stations) if (sj.genre < 8 && !sj.extra.empty()) gc.AddWords(sj.extra, sj.genre);
    gc.AddSkip(job.skip);
    std::vector<radio::LibTrack> lt;
    std::vector<size_t> ltFile;
    lt.reserve(files.size());
    for (size_t i = 0; i < files.size() && !g_scanStop; ++i)
    {
        const FoundFile& f = files[i];
        const Tags& t = ts.Get(f.path, f.size, f.mtime);
        if (t.flags & TF_DRM) { ++r->drm; continue; }
        if (t.genre.empty() && t.title.empty()) ++r->untagged;
        radio::LibTrack x;
        x.path = ToUtf8(f.path); x.root = ToUtf8(roots[f.root]);
        size_t a = 0;
        while (a <= t.genre.size()) { size_t b = t.genre.find(';', a); if (b == std::string::npos) b = t.genre.size(); if (b > a) x.genres.push_back(t.genre.substr(a, b - a)); a = b + 1; }
        x.artist = t.artist; x.albumArtist = t.albumArtist; x.album = t.album;
        lt.push_back(x);
        ltFile.push_back(i);
    }
    radio::LibOptions opt; opt.holiday = job.holiday; opt.month = job.month; opt.unsorted = job.unsorted;
    radio::ResolveLibrary(gc, lt, opt);
    std::vector<radio::Track> byGenre[8];
    for (size_t k = 0; k < lt.size(); ++k)
    {
        const radio::LibTrack& x = lt[k];
        if (x.station >= 0 && x.station < 8)
        {
            const FoundFile& f = files[ltFile[k]];
            byGenre[x.station].push_back(ToTrack(f.path, cache[LowerW(f.path)]));
            ++r->perGenre[x.station];
            continue;
        }
        switch (x.why)
        {
        case radio::WHY_NEVER:
        {
            bool sk = false;
            for (const std::string& g : x.genres) if (radio::GenreSkipHit(gc, g)) sk = true;
            if (sk) ++r->skipWords; else ++r->never;
            break;
        }
        case radio::WHY_HOLIDAY_OUT: case radio::WHY_HOLIDAY_NEVER: ++r->holidayOut; break;
        default: ++r->unsorted; break;
        }
    }
    for (size_t s = 0; s < job.stations.size(); ++s)
    {
        const uint8_t g = job.stations[s].genre;
        if (g < 8) r->st[s].genre = byGenre[g];
    }

    // 2. the playlists: a folder (every playable file, no genre filter) or an .m3u
    for (size_t s = 0; s < job.stations.size() && !g_scanStop; ++s)
    {
        const StationJob& sj = job.stations[s];
        StationResult& sr = r->st[s];
        if (sj.form == radio::PL_FOLDER && !sj.list.empty())
        {
            sr.overRead = true;
            std::vector<FoundFile> ff; WalkStats w2;
            std::wstring root = sj.list;
            while (root.size() > 3 && (root.back() == L'\\' || root.back() == L'/')) root.pop_back();
            const DWORD a = GetFileAttributesW(root.c_str());
            if (a == INVALID_FILE_ATTRIBUTES || !(a & FILE_ATTRIBUTE_DIRECTORY))
                r->warnings.push_back("radio: library: playlist_" + sj.id + ": " + ToUtf8(root) + " - no such folder");
            else Walk(root, 0, pl, ff, w2, 0);
            for (const FoundFile& f : ff)
            {
                const Tags& t = ts.Get(f.path, f.size, f.mtime);
                if (t.flags & TF_DRM) continue;
                sr.over.push_back(ToTrack(f.path, t));
            }
            sr.overEntries = static_cast<int>(ff.size());
            r->notes.push_back("radio: library: playlist_" + sj.id + ": " + ToUtf8(root) + " - " + std::to_string(sr.over.size()) + " files");
        }
        else if (sj.form == radio::PL_M3U && !sj.list.empty())
        {
            sr.overRead = true;
            std::vector<std::string> entries;
            std::vector<std::string> shares;                       // network music folders the listener named themselves
            for (const std::wstring& rt : roots) { const std::string sh = radio::ShareOf(ToUtf8(rt)); if (!sh.empty()) shares.push_back(sh); }
            int net = 0;
            if (!ReadM3u(sj.list, entries, net, shares)) { r->warnings.push_back("radio: library: playlist_" + sj.id + ": " + ToUtf8(sj.list) + " - can't read it"); continue; }
            if (net)
                r->warnings.push_back("radio: library: playlist_" + sj.id + ": " + ToUtf8(sj.list) + " - " + std::to_string(net) +
                                      " network entries left out (only files on this PC, on the playlist's own share or in library_folders)");
            sr.overEntries = static_cast<int>(entries.size()) + net;
            for (const std::string& e : entries)
            {
                const std::wstring p = FromUtf8(e);
                uint64_t size = 0, mtime = 0; DWORD attr = 0;
                const int k = pl.Index(p.c_str());
                if (k < 0 || !pl.ok[k] || !FileInfo(p, size, mtime, attr)) { ++sr.overMissing; continue; }
                if (attr & (FILE_ATTRIBUTE_OFFLINE | 0x00040000 | 0x00400000)) { ++sr.overMissing; continue; }
                const Tags& t = ts.Get(p, size, mtime);
                if (t.flags & TF_DRM) { ++sr.overMissing; continue; }
                sr.over.push_back(ToTrack(p, t));
            }
            if (sr.overMissing)
                r->warnings.push_back("radio: library: playlist_" + sj.id + ": " + ToUtf8(sj.list) + " - " + std::to_string(sr.overMissing) + " of " +
                                      std::to_string(sr.overEntries) + " entries missing or unplayable");
            else r->notes.push_back("radio: library: playlist_" + sj.id + ": " + ToUtf8(sj.list) + " - " + std::to_string(sr.over.size()) + " files");
        }
    }

    // 3. the cache: files no longer seen leave it; written only when something changed
    for (auto it = cache.begin(); it != cache.end();)
    {
        if (!seen.count(it->first)) { it = cache.erase(it); ts.dirty = true; }
        else ++it;
    }
    if (ts.dirty && !g_scanStop && !job.cache.empty() && !SaveCache(job.cache, cache, names))
        r->warnings.push_back("radio: library: can't write the cache " + ToUtf8(job.cache) + " (the next start reads every file again)");
    r->cached = ts.cached; r->read = ts.read;
    QueryPerformanceCounter(&q1);
    r->seconds = static_cast<double>(q1.QuadPart - q0.QuadPart) / static_cast<double>(qf.QuadPart);
    return r;
}

// ---- the thread -----------------------------------------------------------------------------------------
static DWORD WINAPI ScanThread(LPVOID)
{
    const HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    SetThreadPriority(GetCurrentThread(), THREAD_MODE_BACKGROUND_BEGIN);   // low CPU and disk priority
    while (!g_scanStop)
    {
        WaitForSingleObject(g_jobEvent, 1000);
        if (g_scanStop) break;
        ScanJob* job = static_cast<ScanJob*>(InterlockedExchangePointer(reinterpret_cast<PVOID volatile*>(&g_job), nullptr));
        if (!job) continue;
        ScanResult* res = nullptr;
        try { res = RunJob(*job); }
        catch (...) { delete res; res = nullptr; }
        if (!res)
        {
            InterlockedExchange(&g_libOff, 1);
            res = new ScanResult(); res->gen = job->gen; res->fault = true;
        }
        delete job;
        if (g_scanStop) { delete res; break; }
        ScanResult* old = static_cast<ScanResult*>(InterlockedExchangePointer(reinterpret_cast<PVOID volatile*>(&g_result), res));
        delete old;                                  // never taken: superseded
    }
    SetThreadPriority(GetCurrentThread(), THREAD_MODE_BACKGROUND_END);
    if (SUCCEEDED(co)) CoUninitialize();
    return 0;
}

// The latest job wins: an older one not yet started is dropped.
static void PostScanJob(ScanJob* job)
{
    if (!job) return;
    if (!g_jobEvent) g_jobEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    ScanJob* old = static_cast<ScanJob*>(InterlockedExchangePointer(reinterpret_cast<PVOID volatile*>(&g_job), job));
    delete old;
    if (!g_scanThread)
    {
        g_scanStop = 0;
        g_scanThread = CreateThread(nullptr, 0, ScanThread, nullptr, 0, nullptr);
        if (!g_scanThread) { InterlockedExchange(&g_libOff, 1); return; }
    }
    if (g_jobEvent) SetEvent(g_jobEvent);
}

static ScanResult* TakeScanResult()
{
    return static_cast<ScanResult*>(InterlockedExchangePointer(reinterpret_cast<PVOID volatile*>(&g_result), nullptr));
}

// At the radio's end: the scan is asked to stop; a scan still reading a slow disk is left to finish
// on its own (it owns what it holds), never freed under it.
static void StopScanThread(DWORD waitMs)
{
    if (!g_scanThread) return;
    InterlockedExchange(&g_scanStop, 1);
    if (g_jobEvent) SetEvent(g_jobEvent);
    if (WaitForSingleObject(g_scanThread, waitMs) == WAIT_OBJECT_0)
    {
        CloseHandle(g_scanThread);
        g_scanThread = nullptr;
        delete TakeScanResult();
        delete static_cast<ScanJob*>(InterlockedExchangePointer(reinterpret_cast<PVOID volatile*>(&g_job), nullptr));
    }
}

} // namespace lib
} // namespace rd
