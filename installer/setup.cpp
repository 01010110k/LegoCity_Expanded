// setup.cpp - the player's installer / uninstaller (Notes\70-release.md, README.md "What Setup does").
//
//   Setup.exe                         the window: find the game, choose parts, Install / Update / Uninstall
//   Setup.exe /silent [options]       no window; result in the exit code and the log
//   Uninstall.exe (= a copy of Setup.exe in <game>\LCUMod) or Setup.exe /uninstall
//
// Options: /dir=<game folder>  /components=graphics,radio (core is always installed; "none" = core only)
//          /foreign=backup|cancel (silent default: cancel)  /saves=0|1  /savesdir=<folder>  /log=<file>
//          /payload=<folder with the .lcupak files>  /fullcheck (hash every GAME*.DAT completely)  /noregister
// Exit codes: 0 ok, 1 error, 2 bad arguments, 10 game not found, 11 unsupported exe, 12 game archives not stock,
//             13 other mods' files found and not allowed to move them, 14 not enough disk space, 15 PATCH.DAT build failed,
//             16 payload missing or damaged, 17 game folder not writable, 18 the game is running, 20 not installed
#include "lcurel.h"
#include <commctrl.h>
#include <shlobj.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <tlhelp32.h>
#include <thread>
#include <atomic>
#include <set>
#include <map>
#include <sstream>
#include <ctime>
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shlwapi.lib")

using namespace rel;

// --------------------------------------------------------------------------------------------- state ---
struct Options {
    bool silent = false, uninstall = false, fullcheck = false, noregister = false, autoStart = false;
    std::wstring dir, savesDir, logPath, payloadDir;
    std::set<std::string> components = { "core", "graphics", "radio" };
    bool componentsGiven = false;
    std::string foreign = "";          // "", "backup", "cancel"
    int saves = 1;
};
static Options g_opt;
static HWND g_wnd = nullptr;
static std::atomic<bool> g_busy{ false }, g_cancel{ false };
static FILE* g_log = nullptr;
static std::wstring g_logFile;

static const wchar_t* kRegKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\LCUMod";
static const wchar_t* kModDir = L"LCUMod";
static const wchar_t* kBackupDir = L"LCUMod_Backup";
static const wchar_t* kManifest = L"LCUMod\\install-manifest.tsv";

enum { WM_APP_LOG = WM_APP + 1, WM_APP_PROGRESS, WM_APP_DONE, WM_APP_STATUS };

static std::string Now()
{
    time_t t = time(nullptr); tm lt; localtime_s(&lt, &t);
    char b[32]; strftime(b, sizeof b, "%Y-%m-%d %H:%M:%S", &lt); return b;
}
static std::wstring Stamp()
{
    time_t t = time(nullptr); tm lt; localtime_s(&lt, &t);
    wchar_t b[32]; wcsftime(b, 32, L"%Y-%m-%d_%H%M%S", &lt); return b;
}
static void Log(const std::string& s)
{
    if (g_log) { fprintf(g_log, "%s  %s\n", Now().c_str(), s.c_str()); fflush(g_log); }
    if (g_wnd) PostMessageW(g_wnd, WM_APP_LOG, 0, (LPARAM)new std::wstring(W(s)));
    if (g_opt.silent) { printf("%s\n", s.c_str()); }
}
static void Status(const std::string& s)
{
    Log(s);
    if (g_wnd) PostMessageW(g_wnd, WM_APP_STATUS, 0, (LPARAM)new std::wstring(W(s)));
}
static void Progress(double f) { if (g_wnd) PostMessageW(g_wnd, WM_APP_PROGRESS, (WPARAM)(f * 1000), 0); }

// A yes/no question: in the window a message box, in silent mode the /foreign answer.
static bool Ask(const std::wstring& text, const std::wstring& title)
{
    if (g_opt.silent) return g_opt.foreign == "backup";
    return MessageBoxW(g_wnd, text.c_str(), title.c_str(), MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) == IDYES;
}
static void Tell(const std::wstring& text, bool error)
{
    if (g_opt.silent) { printf("%s\n", U(text).c_str()); return; }
    MessageBoxW(g_wnd, text.c_str(), L"Setup", MB_OK | (error ? MB_ICONERROR : MB_ICONINFORMATION));
}

static std::wstring ExeDir()
{
    wchar_t p[MAX_PATH * 2]; GetModuleFileNameW(nullptr, p, MAX_PATH * 2);
    std::wstring s = p; return s.substr(0, s.find_last_of(L"\\/"));
}
static std::wstring ExePath() { wchar_t p[MAX_PATH * 2]; GetModuleFileNameW(nullptr, p, MAX_PATH * 2); return p; }
static bool Exists(const std::wstring& p) { return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES; }
static bool IsDir(const std::wstring& p) { DWORD a = GetFileAttributesW(p.c_str()); return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY); }
static void MakeDirs(const std::wstring& path)   // every parent of a FILE path
{
    for (size_t i = 3; i < path.size(); ++i)
        if (path[i] == L'\\') CreateDirectoryW(path.substr(0, i).c_str(), nullptr);
}

// --------------------------------------------------------------------------------- release metadata ---
struct DatCheck { std::string name; uint64_t size; std::string indexSha, fullSha; };
struct Component { std::string id, title; uint64_t bytes = 0; bool defaultOn = true; };
struct FromPatch { std::string path; uint64_t off, size; std::string sha; std::string component; };
struct Release {
    std::string name = "MOD_NAME", version = "0", publisher = "";
    std::string exeName; uint64_t exeSize = 0; std::string exeSha;
    std::vector<DatCheck> dats;
    uint64_t patchSize = 0; std::string patchSha;
    std::vector<Component> comps;
    std::vector<FromPatch> fromPatch;
    std::vector<std::string> injectorNames;     // other mods' files we look for (dxgi.dll ...)
    std::vector<std::string> generated;         // files the injectors / DLL create at run time (removed on uninstall)
};
static Release g_rel;
static std::map<std::string, std::unique_ptr<Pak>> g_paks;

static bool ParseRelease(const std::string& text)
{
    std::istringstream in(text); std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        std::vector<std::string> f; size_t a = 0;
        for (;;) { size_t b = line.find('\t', a); f.push_back(line.substr(a, b == std::string::npos ? std::string::npos : b - a)); if (b == std::string::npos) break; a = b + 1; }
        const std::string& k = f[0];
        if (k == "name" && f.size() > 1) g_rel.name = f[1];
        else if (k == "version" && f.size() > 1) g_rel.version = f[1];
        else if (k == "publisher" && f.size() > 1) g_rel.publisher = f[1];
        else if (k == "exe" && f.size() > 3) { g_rel.exeName = f[1]; g_rel.exeSize = _strtoui64(f[2].c_str(), nullptr, 10); g_rel.exeSha = f[3]; }
        else if (k == "dat" && f.size() > 4) g_rel.dats.push_back({ f[1], _strtoui64(f[2].c_str(), nullptr, 10), f[3], f[4] });
        else if (k == "patch" && f.size() > 2) { g_rel.patchSize = _strtoui64(f[1].c_str(), nullptr, 10); g_rel.patchSha = f[2]; }
        else if (k == "component" && f.size() > 4) g_rel.comps.push_back({ f[1], f[2], _strtoui64(f[3].c_str(), nullptr, 10), f[4] == "1" });
        else if (k == "frompatch" && f.size() > 5) g_rel.fromPatch.push_back({ f[1], _strtoui64(f[2].c_str(), nullptr, 10), _strtoui64(f[3].c_str(), nullptr, 10), f[4], f[5] });
        else if (k == "injector" && f.size() > 1) g_rel.injectorNames.push_back(f[1]);
        else if (k == "generated" && f.size() > 1) g_rel.generated.push_back(f[1]);
    }
    return !g_rel.exeSha.empty() && !g_rel.patchSha.empty();
}

static std::wstring PakPath(const std::string& comp)
{
    std::wstring d = g_opt.payloadDir.empty() ? ExeDir() : g_opt.payloadDir;
    return JoinPath(d, W(comp) + L".lcupak");
}
static bool PakAvailable(const std::string& comp)
{
    std::wstring p = PakPath(comp);
    return Exists(p) || Exists(p + L".001");
}
static Pak* OpenPak(const std::string& comp, std::string& err)
{
    auto it = g_paks.find(comp);
    if (it != g_paks.end()) return it->second.get();
    auto p = std::make_unique<Pak>();
    if (!p->Open(PakPath(comp), err)) return nullptr;
    Pak* r = p.get(); g_paks[comp] = std::move(p); return r;
}
static bool LoadRelease(std::string& err)
{
    Pak* core = OpenPak("core", err);
    if (!core) return false;
    const PakFile* f = core->Find("@setup\\release.txt");
    std::vector<uint8_t> d;
    if (!f || !core->ExtractToMemory(*f, d, err)) { if (err.empty()) err = "release.txt missing from the payload"; return false; }
    if (!ParseRelease(std::string(d.begin(), d.end()))) { err = "release.txt is incomplete"; return false; }
    return true;
}

// ------------------------------------------------------------------------------------ find the game ---
static std::wstring RegStr(HKEY root, const wchar_t* key, const wchar_t* val, REGSAM extra = 0)
{
    wchar_t buf[2048]; DWORD cb = sizeof buf, type = 0;
    HKEY h;
    if (RegOpenKeyExW(root, key, 0, KEY_READ | extra, &h) != ERROR_SUCCESS) return L"";
    LONG r = RegQueryValueExW(h, val, nullptr, &type, (BYTE*)buf, &cb);
    RegCloseKey(h);
    if (r != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ)) return L"";
    buf[std::min<DWORD>(cb / 2, 2047)] = 0;
    std::wstring s = buf; for (auto& c : s) if (c == L'/') c = L'\\';
    return s;
}
static std::wstring SteamRoot()
{
    std::wstring s = RegStr(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath");
    if (s.empty()) s = RegStr(HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\Valve\\Steam", L"InstallPath");
    if (s.empty()) s = RegStr(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Valve\\Steam", L"InstallPath");
    return s;
}
// every "path" in steamapps\libraryfolders.vdf, plus the Steam folder itself
static std::vector<std::wstring> SteamLibraries()
{
    std::vector<std::wstring> libs;
    std::wstring root = SteamRoot();
    if (root.empty()) return libs;
    libs.push_back(root);
    std::vector<uint8_t> d;
    if (ReadWhole(JoinPath(root, L"steamapps\\libraryfolders.vdf"), d)) {
        std::string t(d.begin(), d.end());
        size_t p = 0;
        while ((p = t.find("\"path\"", p)) != std::string::npos) {
            size_t a = t.find('"', p + 6); if (a == std::string::npos) break;
            size_t b = a + 1; std::string v;
            while (b < t.size() && t[b] != '"') { if (t[b] == '\\' && b + 1 < t.size()) { v += t[b + 1]; b += 2; } else v += t[b++]; }
            std::wstring w = W(v); for (auto& c : w) if (c == L'/') c = L'\\';
            bool dup = false; for (auto& l : libs) if (_wcsicmp(l.c_str(), w.c_str()) == 0) dup = true;
            if (!dup && !w.empty()) libs.push_back(w);
            p = b + 1;
        }
    }
    return libs;
}
static std::wstring FindGame()
{
    for (auto& lib : SteamLibraries()) {
        std::wstring dir;
        std::vector<uint8_t> d;
        if (ReadWhole(JoinPath(lib, L"steamapps\\appmanifest_578330.acf"), d)) {
            std::string t(d.begin(), d.end());
            size_t p = t.find("\"installdir\"");
            if (p != std::string::npos) {
                size_t a = t.find('"', p + 12), b = a == std::string::npos ? a : t.find('"', a + 1);
                if (b != std::string::npos) dir = JoinPath(lib, L"steamapps\\common\\" + W(t.substr(a + 1, b - a - 1)));
            }
        }
        if (dir.empty()) dir = JoinPath(lib, L"steamapps\\common\\LEGO City Undercover");
        if (Exists(JoinPath(dir, L"LEGOLCUR_DX11.exe"))) return dir;
    }
    std::wstring u = RegStr(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Steam App 578330", L"InstallLocation", KEY_WOW64_64KEY);
    if (!u.empty() && Exists(JoinPath(u, L"LEGOLCUR_DX11.exe"))) return u;
    return L"";
}

// Is the game running FROM THIS FOLDER?  (A copy of the game elsewhere does not lock these files.)
static bool GameRunning(const std::wstring& game)
{
    HANDLE s = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (s == INVALID_HANDLE_VALUE) return false;
    PROCESSENTRY32W pe{ sizeof pe }; bool found = false;
    std::wstring want = Lower(JoinPath(game, L"LEGOLCUR_DX11.exe"));
    for (BOOL ok = Process32FirstW(s, &pe); ok; ok = Process32NextW(s, &pe)) {
        if (_wcsicmp(pe.szExeFile, L"LEGOLCUR_DX11.exe") != 0) continue;
        HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe.th32ProcessID);
        if (!h) { found = true; break; }                 // cannot tell: be careful
        wchar_t path[MAX_PATH * 2]; DWORD n = MAX_PATH * 2;
        bool same = QueryFullProcessImageNameW(h, 0, path, &n) && Lower(path) == want;
        CloseHandle(h);
        if (same) { found = true; break; }
    }
    CloseHandle(s);
    return found;
}

// ----------------------------------------------------------------------------------------- manifest ---
struct Manifest {
    std::string version, components, time;
    struct F { std::string path; uint64_t size; std::string sha; };
    std::vector<F> files;                       // every file Setup put in the game folder
    std::vector<std::string> createdDirs;       // folders Setup created (removed with everything in them)
    std::vector<std::pair<std::string, std::string>> backups;   // original path -> path under LCUMod_Backup
    std::vector<std::string> absent;            // run-time files that did not exist before install
    bool Load(const std::wstring& path)
    {
        std::vector<uint8_t> d; if (!ReadWhole(path, d)) return false;
        std::istringstream in(std::string(d.begin(), d.end())); std::string line;
        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty() || line[0] == '#') continue;
            std::vector<std::string> f; size_t a = 0;
            for (;;) { size_t b = line.find('\t', a); f.push_back(line.substr(a, b == std::string::npos ? std::string::npos : b - a)); if (b == std::string::npos) break; a = b + 1; }
            if (f[0] == "version" && f.size() > 1) version = f[1];
            else if (f[0] == "components" && f.size() > 1) components = f[1];
            else if (f[0] == "time" && f.size() > 1) time = f[1];
            else if (f[0] == "file" && f.size() > 3) files.push_back({ f[1], _strtoui64(f[2].c_str(), nullptr, 10), f[3] });
            else if (f[0] == "dir" && f.size() > 1) createdDirs.push_back(f[1]);
            else if (f[0] == "backup" && f.size() > 2) backups.push_back({ f[1], f[2] });
            else if (f[0] == "absent" && f.size() > 1) absent.push_back(f[1]);
        }
        return true;
    }
    bool Save(const std::wstring& path) const
    {
        std::string s = "# install manifest - written by Setup, read by Uninstall. Every file Setup added or replaced,\n"
                        "# with its SHA-256, and where the files it moved aside went (LCUMod_Backup). Do not edit.\n";
        s += "version\t" + version + "\ncomponents\t" + components + "\ntime\t" + time + "\n";
        for (auto& d : createdDirs) s += "dir\t" + d + "\n";
        for (auto& b : backups) s += "backup\t" + b.first + "\t" + b.second + "\n";
        for (auto& a : absent) s += "absent\t" + a + "\n";
        for (auto& f : files) s += "file\t" + f.path + "\t" + std::to_string(f.size) + "\t" + f.sha + "\n";
        File o; if (!o.OpenWrite(path)) return false;
        return o.WriteAt(0, s.data(), s.size());
    }
    bool HasFile(const std::string& p) const { for (auto& f : files) if (_stricmp(f.path.c_str(), p.c_str()) == 0) return true; return false; }
    bool HasDir(const std::string& p) const { for (auto& f : createdDirs) if (_stricmp(f.c_str(), p.c_str()) == 0) return true; return false; }
};

// ---------------------------------------------------------------------------------------- utilities ---
static bool DeleteTree(const std::wstring& dir)
{
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(JoinPath(dir, L"*").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            std::wstring n = fd.cFileName;
            if (n == L"." || n == L"..") continue;
            std::wstring p = JoinPath(dir, n);
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) DeleteTree(p);
            else { SetFileAttributesW(p.c_str(), FILE_ATTRIBUTE_NORMAL); DeleteFileW(p.c_str()); }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    return RemoveDirectoryW(dir.c_str()) != 0;
}
static bool DirEmpty(const std::wstring& dir) { return PathIsDirectoryEmptyW(dir.c_str()) != FALSE; }
static void CopyTree(const std::wstring& from, const std::wstring& to, int& n)
{
    CreateDirectoryW(to.c_str(), nullptr);
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(JoinPath(from, L"*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        std::wstring nm = fd.cFileName;
        if (nm == L"." || nm == L"..") continue;
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) CopyTree(JoinPath(from, nm), JoinPath(to, nm), n);
        else if (CopyFileW(JoinPath(from, nm).c_str(), JoinPath(to, nm).c_str(), FALSE)) ++n;
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}
static std::string ShaHex(const std::wstring& p, uint64_t* size = nullptr)
{
    uint8_t s[32]; if (!Sha256File(p, s, size)) return ""; return Hex(s, 32);
}
static std::string TopName(const std::string& rel) { size_t k = rel.find('\\'); return k == std::string::npos ? rel : rel.substr(0, k); }
static uint64_t FreeBytes(const std::wstring& dir)
{
    ULARGE_INTEGER fb{}; if (!GetDiskFreeSpaceExW(dir.c_str(), &fb, nullptr, nullptr)) return 0; return fb.QuadPart;
}
static std::string Mb(uint64_t b) { char s[32]; sprintf_s(s, "%.0f MB", b / 1048576.0); return s; }
static bool Writable(const std::wstring& dir)
{
    std::wstring p = JoinPath(dir, L"LCUMod_write_test.tmp");
    HANDLE h = CreateFileW(p.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_FLAG_DELETE_ON_CLOSE, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    CloseHandle(h); return true;
}

// ------------------------------------------------------------------------------------- game checks ---
// 0 ok, else an exit code; msg says why in plain words.
static int CheckGame(const std::wstring& game, std::wstring& msg)
{
    std::wstring exe = JoinPath(game, W(g_rel.exeName));
    if (!Exists(exe)) { msg = L"There is no " + W(g_rel.exeName) + L" in\n" + game + L"\n\nChoose the LEGO City Undercover folder (the one with the game's .exe and GAME.DAT)."; return 10; }
    Status("Checking the game version...");
    uint64_t size = 0; std::string sha = ShaHex(exe, &size);
    Log("exe: " + std::to_string(size) + " bytes, sha256 " + sha);
    if (size != g_rel.exeSize || _stricmp(sha.c_str(), g_rel.exeSha.c_str()) != 0) {
        msg = L"This game version is not supported.\n\n" + W(g_rel.exeName) + L" is not the Steam version this mod was made for "
              L"(another store's copy, a newer or older update, or a modified exe).\n\nSetup has not changed anything.";
        return 11;
    }
    Status("Checking the game's archives (GAME*.DAT)...");
    for (size_t i = 0; i < g_rel.dats.size(); ++i) {
        auto& d = g_rel.dats[i];
        std::wstring p = JoinPath(game, W(d.name));
        WIN32_FILE_ATTRIBUTE_DATA fa;
        if (!GetFileAttributesExW(p.c_str(), GetFileExInfoStandard, &fa)) { msg = W(d.name) + L" is missing. Use Steam's \"Verify integrity of game files\", then run Setup again.\n\nSetup has not changed anything."; return 12; }
        uint64_t sz = ((uint64_t)fa.nFileSizeHigh << 32) | fa.nFileSizeLow;
        Dat dat; std::string err; uint8_t ix[32] = {};
        bool ok = sz == d.size && dat.Open(p, err) && dat.IndexSha(ix) && _stricmp(Hex(ix, 32).c_str(), d.indexSha.c_str()) == 0;
        if (ok && g_opt.fullcheck) {
            dat.file.Close();
            Status("Hashing " + d.name + " completely (/fullcheck)...");
            std::string full = ShaHex(p);
            ok = _stricmp(full.c_str(), d.fullSha.c_str()) == 0;
        }
        Log(d.name + (ok ? ": stock" : ": NOT stock"));
        if (!ok) {
            msg = W(d.name) + L" is not the stock file (another mod changed it, or it is damaged).\n\nIn Steam: right-click the game > Properties > "
                  L"Installed Files > \"Verify integrity of game files\" restores it. Then run Setup again.\n\nSetup has not changed anything.";
            return 12;
        }
        Progress(0.02 + 0.03 * (i + 1) / g_rel.dats.size());
    }
    return 0;
}

// ----------------------------------------------------------------------------------- save backups ---
static void BackupSaves()
{
    std::wstring dest = g_opt.savesDir;
    if (dest.empty()) {
        PWSTR docs = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &docs))) { dest = JoinPath(docs, W(g_rel.name) + L" save backups"); CoTaskMemFree(docs); }
    }
    if (dest.empty()) { Log("save backup: no Documents folder - skipped"); return; }
    dest = JoinPath(dest, Stamp());
    int n = 0;
    std::wstring root = SteamRoot();
    if (!root.empty()) {
        std::wstring ud = JoinPath(root, L"userdata");
        WIN32_FIND_DATAW fd;
        HANDLE h = FindFirstFileW(JoinPath(ud, L"*").c_str(), &fd);
        if (h != INVALID_HANDLE_VALUE) {
            do {
                std::wstring nm = fd.cFileName;
                if (nm == L"." || nm == L".." || !(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
                std::wstring src = JoinPath(ud, nm + L"\\578330\\remote");
                if (!IsDir(src)) continue;
                MakeDirs(JoinPath(dest, L"steam-" + nm + L"\\x"));
                CopyTree(src, JoinPath(dest, L"steam-" + nm + L"\\remote"), n);
            } while (FindNextFileW(h, &fd));
            FindClose(h);
        }
    }
    PWSTR appdata = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &appdata))) {
        std::wstring cfg = JoinPath(appdata, L"Warner Bros. Interactive Entertainment\\LEGO City Undercover");
        CoTaskMemFree(appdata);
        if (IsDir(cfg)) { MakeDirs(JoinPath(dest, L"settings\\x")); CopyTree(cfg, JoinPath(dest, L"settings"), n); }
    }
    if (n) Log("save backup: " + std::to_string(n) + " files copied to " + U(dest));
    else { Log("save backup: no saves found (fine for a new game)"); RemoveDirectoryW(dest.c_str()); }
}

// --------------------------------------------------------------------------------------- uninstall ---
// Removes what the manifest lists and puts the backed-up files back.  keepBackups: an update in progress.
static bool RemoveInstalled(const std::wstring& game, const Manifest& m, bool restoreBackups, const std::set<std::string>& keep, std::string& report)
{
    int removed = 0, changed = 0, failed = 0;
    for (auto& f : m.files) {
        if (keep.count(U(Lower(W(f.path))))) continue;
        std::wstring p = JoinPath(game, W(f.path));
        if (!Exists(p)) continue;
        if (_wcsicmp(p.c_str(), ExePath().c_str()) == 0) { ++removed; continue; }   // this running Uninstall.exe: RemoveSelfLater
        if (_stricmp(ShaHex(p).c_str(), f.sha.c_str()) != 0) ++changed;
        SetFileAttributesW(p.c_str(), FILE_ATTRIBUTE_NORMAL);
        if (DeleteFileW(p.c_str())) ++removed; else { ++failed; Log("could not delete " + f.path + " (" + std::to_string(GetLastError()) + ")"); }
    }
    if (restoreBackups) {
        for (auto& a : m.absent) {                       // run-time files of the injectors, created after the install
            std::wstring p = JoinPath(game, W(a));
            if (Exists(p) && !IsDir(p)) { DeleteFileW(p.c_str()); Log("removed run-time file " + a); }
        }
        // folders Setup created, deepest first, with whatever the game / injectors wrote into them since
        std::vector<std::string> dirs = m.createdDirs;
        std::sort(dirs.begin(), dirs.end(), [](const std::string& a, const std::string& b) { return a.size() > b.size(); });
        for (auto& d : dirs) {
            std::wstring p = JoinPath(game, W(d));
            if (_stricmp(d.c_str(), "LCUMod") == 0) continue;     // last: Uninstall.exe may be running from it
            if (IsDir(p)) DeleteTree(p);
        }
        for (auto& b : m.backups) {
            std::wstring orig = JoinPath(game, W(b.first)), bak = JoinPath(JoinPath(game, kBackupDir), W(b.second));
            if (!Exists(bak)) { Log("backup missing: " + b.second); ++failed; continue; }
            MakeDirs(orig);
            if (Exists(orig)) { if (IsDir(orig)) DeleteTree(orig); else DeleteFileW(orig.c_str()); }
            if (MoveFileExW(bak.c_str(), orig.c_str(), MOVEFILE_COPY_ALLOWED)) Log("restored " + b.first);
            else { ++failed; Log("could not restore " + b.first + " (" + std::to_string(GetLastError()) + ")"); }
        }
        std::wstring bd = JoinPath(game, kBackupDir);
        if (IsDir(bd)) {
            // remove the (now empty) backup folders
            std::function<void(const std::wstring&)> prune = [&](const std::wstring& d) {
                WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileW(JoinPath(d, L"*").c_str(), &fd);
                if (h != INVALID_HANDLE_VALUE) {
                    do { std::wstring n = fd.cFileName; if (n != L"." && n != L".." && (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) prune(JoinPath(d, n)); } while (FindNextFileW(h, &fd));
                    FindClose(h);
                }
                RemoveDirectoryW(d.c_str());
            };
            prune(bd);
        }
    }
    report = std::to_string(removed) + " files removed" + (changed ? " (" + std::to_string(changed) + " had been changed since install, e.g. your settings)" : "") +
             (failed ? ", " + std::to_string(failed) + " problems (see the log)" : "");
    return failed == 0;
}

static void RemoveSelfLater(const std::wstring& modDir)
{
    // Uninstall.exe cannot delete itself while it runs: a hidden cmd waits a moment, then removes it and the folder.
    std::wstring self = ExePath();
    std::wstring cmd = L"cmd.exe /d /c ping -n 3 127.0.0.1 >nul & del /f /q \"" + self + L"\" & rmdir /s /q \"" + modDir + L"\"";
    STARTUPINFOW si{ sizeof si }; PROCESS_INFORMATION pi{};
    std::vector<wchar_t> buf(cmd.begin(), cmd.end()); buf.push_back(0);
    if (CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW | DETACHED_PROCESS, nullptr, nullptr, &si, &pi)) {
        CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    }
}

static int DoUninstall(const std::wstring& game)
{
    Manifest m;
    if (!m.Load(JoinPath(game, kManifest))) { Tell(L"The mod is not installed in\n" + game, true); return 20; }
    if (GameRunning(game)) { Tell(L"Close LEGO City Undercover first, then run Uninstall again.", true); return 18; }
    if (!g_opt.silent && MessageBoxW(g_wnd, (L"Remove " + W(g_rel.name.empty() ? "the mod" : g_rel.name) + L" from\n" + game +
            L"?\n\nThe game's own files and your saves are not touched. Files Setup moved aside are put back.").c_str(),
            L"Uninstall", MB_YESNO | MB_ICONQUESTION) != IDYES) return 1;
    Status("Uninstalling...");
    std::string rep;
    bool ok = RemoveInstalled(game, m, true, {}, rep);
    Log("uninstall: " + rep);
    RegDeleteTreeW(HKEY_CURRENT_USER, kRegKey);
    std::wstring modDir = JoinPath(game, kModDir);
    std::wstring self = ExePath();
    bool runningFromModDir = _wcsnicmp(self.c_str(), (modDir + L"\\").c_str(), modDir.size() + 1) == 0;
    // keep the log of this run outside the folder that is about to go
    if (runningFromModDir) RemoveSelfLater(modDir); else DeleteTree(modDir);
    Progress(1.0);
    Status(ok ? "Uninstalled. The game is back to how it was." : "Uninstalled, with problems - see the log.");
    Tell(ok ? L"Uninstalled. The game folder is back to how it was before the mod." : L"Uninstalled, but some files could not be removed or restored. The log says which:\n" + g_logFile, !ok);
    return ok ? 0 : 1;
}

// ----------------------------------------------------------------------------------------- install ---
struct Conflict { std::string rel; bool dir; };

static int DoInstall(const std::wstring& game)
{
    std::wstring msg;
    std::string err;
    Progress(0.01);
    if (GameRunning(game)) { Tell(L"Close LEGO City Undercover first, then run Setup again.", true); return 18; }
    int rc = CheckGame(game, msg);
    if (rc) { Log(U(msg)); Tell(msg, true); return rc; }
    if (!Writable(game)) {
        Log("the game folder is not writable");
        Tell(L"Setup cannot write to the game folder:\n" + game + L"\n\nRun Setup again and allow it to make changes (it asks for administrator permission when needed).", true);
        return 17;
    }
    // components and their payloads
    std::set<std::string> comps = g_opt.components; comps.insert("core");
    for (auto& c : comps) if (!PakAvailable(c)) { Tell(L"The download is incomplete: " + W(c) + L".lcupak is missing next to Setup.exe.", true); return 16; }
    std::vector<std::pair<std::string, Pak*>> paks;
    for (auto& c : g_rel.comps) if (comps.count(c.id)) { Pak* p = OpenPak(c.id, err); if (!p) { Tell(W(err), true); return 16; } paks.push_back({ c.id, p }); }
    std::string compList; for (auto& p : paks) compList += (compList.empty() ? "" : ",") + p.first;
    Log("components: " + compList);

    // previous install (update)
    Manifest old; bool update = old.Load(JoinPath(game, kManifest));
    if (update) Log("updating the install from " + old.time + " (version " + old.version + ")");

    // what will be written
    std::vector<std::pair<Pak*, const PakFile*>> todo;
    std::set<std::string> newPaths;                       // lower-case
    uint64_t need = g_rel.patchSize + 256ull * 1048576;
    for (auto& pk : paks)
        for (auto& f : pk.second->files) {
            if (f.path.rfind("@setup\\", 0) == 0) continue;
            todo.push_back({ pk.second, &f }); newPaths.insert(U(Lower(W(f.path)))); need += f.size;
        }
    for (auto& fp : g_rel.fromPatch) if (comps.count(fp.component)) { newPaths.insert(U(Lower(W(fp.path)))); need += fp.size; }
    newPaths.insert("patch.dat");
    uint64_t freeB = FreeBytes(game);
    Log("disk: need about " + Mb(need) + ", free " + Mb(freeB));
    if (freeB < need) { Tell(L"Not enough free disk space on the game's drive: Setup needs about " + W(Mb(need)) + L", there is " + W(Mb(freeB)) + L".", true); return 14; }

    // other mods' files in the way
    std::vector<Conflict> conflicts;
    std::set<std::string> seenTop;
    auto ours = [&](const std::string& rel) { return update && (old.HasFile(rel) || old.HasDir(rel)); };
    for (auto& t : todo) {
        const std::string& rel = t.second->path;
        std::string top = TopName(rel);
        if (rel.find('\\') != std::string::npos && (top == "ShaderFixes" || top == "LCUMod")) {
            // our own folders: a foreign ShaderFixes / LCUMod is moved aside as a whole
            if (!seenTop.count(top) && IsDir(JoinPath(game, W(top))) && !ours(top)) conflicts.push_back({ top, true });
            seenTop.insert(top);
            continue;
        }
        if (Exists(JoinPath(game, W(rel))) && !ours(rel)) conflicts.push_back({ rel, false });
    }
    if (Exists(JoinPath(game, L"PATCH.DAT")) && !ours("PATCH.DAT")) conflicts.push_back({ "PATCH.DAT", false });
    for (auto& n : g_rel.injectorNames)
        if (Exists(JoinPath(game, W(n))) && !ours(n) && !newPaths.count(U(Lower(W(n))))) {
            bool dup = false; for (auto& c : conflicts) if (_stricmp(c.rel.c_str(), n.c_str()) == 0) dup = true;
            if (!dup) conflicts.push_back({ n, IsDir(JoinPath(game, W(n))) });
        }
    if (update) {   // backups of the earlier install stay backups (they are the player's originals)
        std::vector<Conflict> keep;
        for (auto& c : conflicts) { bool b = false; for (auto& ob : old.backups) if (_stricmp(ob.first.c_str(), c.rel.c_str()) == 0) b = true; if (!b) keep.push_back(c); }
        conflicts.swap(keep);
    }
    if (!conflicts.empty()) {
        std::wstring list;
        for (auto& c : conflicts) { list += L"   " + W(c.rel) + (c.dir ? L"\\  (folder)" : L"") + L"\n"; Log("not ours: " + c.rel); }
        std::wstring q = L"These files in the game folder belong to another mod or tool:\n\n" + list +
                         L"\nSetup can move them to " + std::wstring(kBackupDir) + L" (inside the game folder) and put them back when you uninstall this mod.\n\n"
                         L"Yes = move them aside and continue.\nNo = cancel (nothing is changed).";
        if (!Ask(q, L"Other mods found")) { Status("Cancelled: other mods' files are in the way."); return 13; }
    }

    Progress(0.06);
    if (g_opt.saves) { Status("Backing up your saves..."); BackupSaves(); }

    Manifest m;
    m.version = g_rel.version; m.components = compList; m.time = Now();
    if (update) { m.backups = old.backups; m.absent = old.absent; m.createdDirs = old.createdDirs; }
    auto rollback = [&](const std::string& why) {
        Log("FAILED: " + why + " - undoing the changes");
        std::string rep; RemoveInstalled(game, m, !update, {}, rep);
        if (!update && m.HasDir("LCUMod")) DeleteTree(JoinPath(game, kModDir));
        Log("rollback: " + rep);
    };

    // 1. PATCH.DAT: rebuilt from the player's own GAME*.DAT (unless an identical one is already there)
    std::wstring patch = JoinPath(game, L"PATCH.DAT"), tmp = patch + L".lcutmp";
    bool havePatch = false;
    if (update && Exists(patch) && old.HasFile("PATCH.DAT")) {
        Status("Checking the existing PATCH.DAT...");
        havePatch = _stricmp(ShaHex(patch).c_str(), g_rel.patchSha.c_str()) == 0;
        if (havePatch) Log("PATCH.DAT is already the right one - not rebuilt");
    }
    if (!havePatch) {
        Status("Building PATCH.DAT from your game files (a few minutes)...");
        Pak* core = paks[0].second;
        const PakFile* rf = core->Find("@setup\\patch.recipe");
        std::vector<uint8_t> rd;
        if (!rf || !core->ExtractToMemory(*rf, rd, err)) { Tell(L"The download is damaged: " + W(err), true); return 16; }
        Recipe recipe;
        if (!recipe.Parse(rd, err)) { Tell(L"The download is damaged: " + W(err), true); return 16; }
        std::vector<uint8_t>().swap(rd);
        RecipeBuilder b;
        b.progress = [](uint64_t d, uint64_t t) { Progress(0.08 + 0.62 * (double)d / (double)t); };
        b.cancelled = []() { return g_cancel.load(); };
        DWORD t0 = GetTickCount();
        if (!b.Build(recipe, game, tmp, err)) {
            Log("PATCH.DAT build failed: " + err + " (the partial file was deleted)");
            Tell(L"Building PATCH.DAT failed, so nothing was installed:\n\n" + W(err) +
                 L"\n\nThe half-built file has been deleted. If the game was updated or another mod changed its files, Steam's \"Verify integrity of game files\" fixes that.", true);
            return 15;
        }
        Log("PATCH.DAT built and verified: sha256 " + Hex(recipe.outSha, 32) + ", " + std::to_string(recipe.outSize) + " bytes, " +
            std::to_string((GetTickCount() - t0) / 1000) + " s");
    }
    Progress(0.70);

    // 2. move other mods' files aside
    for (auto& c : conflicts) {
        std::wstring from = JoinPath(game, W(c.rel)), to = JoinPath(JoinPath(game, kBackupDir), Stamp() + L"\\" + W(c.rel));
        MakeDirs(to);
        if (!MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_COPY_ALLOWED)) {
            DeleteFileW(tmp.c_str());
            rollback("could not move " + c.rel + " aside (" + std::to_string(GetLastError()) + ")");
            Tell(L"Could not move " + W(c.rel) + L" aside (is it in use?). Nothing was installed.", true);
            return 1;
        }
        std::wstring relTo = to.substr(JoinPath(game, kBackupDir).size() + 1);
        m.backups.push_back({ c.rel, U(relTo) });
        Log("moved aside: " + c.rel + " -> " + std::string(U(kBackupDir)) + "\\" + U(relTo));
    }

    // 3. folders we create, run-time files that do not exist yet
    for (auto& t : todo) {
        std::string rel = t.second->path;
        for (size_t k = rel.find('\\'); k != std::string::npos; k = rel.find('\\', k + 1)) {
            std::string d = rel.substr(0, k);
            if (!IsDir(JoinPath(game, W(d))) && !m.HasDir(d)) { m.createdDirs.push_back(d); CreateDirectoryW(JoinPath(game, W(d)).c_str(), nullptr); }
        }
    }
    if (!m.HasDir("LCUMod")) { if (!IsDir(JoinPath(game, kModDir))) m.createdDirs.push_back("LCUMod"); CreateDirectoryW(JoinPath(game, kModDir).c_str(), nullptr); }
    if (!update)
        for (auto& g : g_rel.generated) if (!Exists(JoinPath(game, W(g)))) m.absent.push_back(g);

    // 4. an update keeps the player's settings file aside
    std::wstring ini = JoinPath(game, L"LCUMod\\lcudrive.ini");
    if (update && Exists(ini)) {
        std::wstring keepIni = ini + L".old";
        CopyFileW(ini.c_str(), keepIni.c_str(), FALSE);
        Log("your previous settings were kept as LCUMod\\lcudrive.ini.old");
    }
    // files of the old install that this version no longer has
    if (update) {
        for (auto& f : old.files) {
            std::string l = U(Lower(W(f.path)));
            if (!newPaths.count(l) && l != "lcumod\\uninstall.exe" && l != "lcumod\\lcudrive.ini.old") { DeleteFileW(JoinPath(game, W(f.path)).c_str()); Log("removed (no longer part of the mod): " + f.path); }
        }
    }

    // 5. extract the payload
    Status("Copying the mod's files...");
    uint64_t total = 0, done = 0;
    for (auto& t : todo) total += t.second->size;
    std::sort(todo.begin(), todo.end(), [](const std::pair<Pak*, const PakFile*>& a, const std::pair<Pak*, const PakFile*>& b) {
        return a.first != b.first ? a.first < b.first : a.second->streamOff < b.second->streamOff; });
    for (auto& t : todo) {
        if (g_cancel) { DeleteFileW(tmp.c_str()); rollback("cancelled"); return 1; }
        std::wstring dest = JoinPath(game, W(t.second->path));
        MakeDirs(dest);
        SetFileAttributesW(dest.c_str(), FILE_ATTRIBUTE_NORMAL);
        if (!t.first->ExtractToFile(*t.second, dest, err)) {
            DeleteFileW(tmp.c_str()); rollback(err);
            Tell(L"Copying failed: " + W(err) + L"\nThe changes were undone.", true);
            return 16;
        }
        m.files.push_back({ t.second->path, t.second->size, Hex(t.second->sha, 32) });
        done += t.second->size;
        Progress(0.70 + 0.25 * (double)done / (double)std::max<uint64_t>(total, 1));
    }
    // 6. PATCH.DAT into place, then the files that are copies of PATCH.DAT entries (the NPC voice banks)
    if (!havePatch) {
        if (!MoveFileExW(tmp.c_str(), patch.c_str(), MOVEFILE_REPLACE_EXISTING)) { DeleteFileW(tmp.c_str()); rollback("could not place PATCH.DAT"); Tell(L"Could not place PATCH.DAT. The changes were undone.", true); return 1; }
    }
    m.files.push_back({ "PATCH.DAT", g_rel.patchSize, g_rel.patchSha });
    {
        File pf;
        if (!pf.OpenRead(patch)) { rollback("cannot read PATCH.DAT"); Tell(L"Cannot read PATCH.DAT. The changes were undone.", true); return 1; }
        for (auto& fp : g_rel.fromPatch) {
            if (!comps.count(fp.component)) continue;
            std::vector<uint8_t> d((size_t)fp.size);
            uint8_t sh[32];
            if (!pf.ReadAt(fp.off, d.data(), d.size())) { rollback("read PATCH.DAT"); return 1; }
            Sha256(d.data(), d.size(), sh);
            if (_stricmp(Hex(sh, 32).c_str(), fp.sha.c_str()) != 0) { rollback("PATCH.DAT entry checksum " + fp.path); Tell(L"Internal check failed. The changes were undone.", true); return 1; }
            std::wstring dest = JoinPath(game, W(fp.path));
            MakeDirs(dest);
            File o; if (!o.OpenWrite(dest) || !o.WriteAt(0, d.data(), d.size())) { rollback("cannot write " + fp.path); Tell(L"Cannot write " + W(fp.path) + L". The changes were undone.", true); return 1; }
            m.files.push_back({ fp.path, fp.size, fp.sha });
        }
    }
    // 7. settings for the parts that were left out
    if (Exists(ini) && !comps.count("radio")) {
        WritePrivateProfileStringW(L"radio", L"enable", L"0", ini.c_str());
        Log("radio not installed: [radio] enable = 0 in lcudrive.ini");
    }
    for (auto& f : m.files) if (_stricmp(f.path.c_str(), "LCUMod\\lcudrive.ini") == 0) { uint64_t sz; f.sha = ShaHex(ini, &sz); f.size = sz; }
    if (update && Exists(ini + L".old") && !m.HasFile("LCUMod\\lcudrive.ini.old")) { uint64_t sz; std::string s = ShaHex(ini + L".old", &sz); m.files.push_back({ "LCUMod\\lcudrive.ini.old", sz, s }); }
    // 8. the uninstaller, the manifest, Add/Remove Programs
    std::wstring un = JoinPath(game, L"LCUMod\\Uninstall.exe");
    if (_wcsicmp(ExePath().c_str(), un.c_str()) != 0) CopyFileW(ExePath().c_str(), un.c_str(), FALSE);
    { uint64_t sz; std::string s = ShaHex(un, &sz); bool have = false; for (auto& f : m.files) if (_stricmp(f.path.c_str(), "LCUMod\\Uninstall.exe") == 0) { f.sha = s; f.size = sz; have = true; } if (!have) m.files.push_back({ "LCUMod\\Uninstall.exe", sz, s }); }
    if (!m.Save(JoinPath(game, kManifest))) { rollback("cannot write the manifest"); Tell(L"Cannot write the install manifest. The changes were undone.", true); return 1; }
    if (!g_opt.noregister) {
        HKEY k;
        if (RegCreateKeyExW(HKEY_CURRENT_USER, kRegKey, 0, nullptr, 0, KEY_WRITE, nullptr, &k, nullptr) == ERROR_SUCCESS) {
            auto sz = [&](const wchar_t* n, const std::wstring& v) { RegSetValueExW(k, n, 0, REG_SZ, (const BYTE*)v.c_str(), (DWORD)((v.size() + 1) * 2)); };
            auto dw = [&](const wchar_t* n, DWORD v) { RegSetValueExW(k, n, 0, REG_DWORD, (const BYTE*)&v, 4); };
            sz(L"DisplayName", W(g_rel.name) + L" (LEGO City Undercover mod)");
            sz(L"DisplayVersion", W(g_rel.version));
            if (!g_rel.publisher.empty()) sz(L"Publisher", W(g_rel.publisher));
            sz(L"InstallLocation", game);
            sz(L"UninstallString", L"\"" + un + L"\" /uninstall /dir=\"" + game + L"\"");
            sz(L"QuietUninstallString", L"\"" + un + L"\" /uninstall /silent /dir=\"" + game + L"\"");
            sz(L"DisplayIcon", JoinPath(game, L"LEGOLCUR_DX11.exe"));
            dw(L"NoModify", 1); dw(L"NoRepair", 1);
            uint64_t kb = 0; for (auto& f : m.files) kb += f.size / 1024;
            dw(L"EstimatedSize", (DWORD)std::min<uint64_t>(kb, 0xFFFFFFFF));
            RegCloseKey(k);
        }
    }
    Progress(1.0);
    Log("installed " + std::to_string(m.files.size()) + " files; manifest " + U(JoinPath(game, kManifest)));
    if (g_log && !g_logFile.empty()) { fflush(g_log); CopyFileW(g_logFile.c_str(), JoinPath(game, L"LCUMod\\setup.log").c_str(), FALSE); }
    Status(update ? "Updated. Have fun!" : "Installed. Have fun!");
    Tell(std::wstring(update ? L"Updated" : L"Installed") + L". Start the game from Steam as usual.\n\nThe mod's settings are in the pause menu under Mod Options.\nTo remove the mod: Windows Settings > Apps > " +
         W(g_rel.name) + L", or run LCUMod\\Uninstall.exe in the game folder.", false);
    return 0;
}

// ------------------------------------------------------------------------------------------ window ---
enum { ID_DIR = 101, ID_BROWSE, ID_GRAPHICS, ID_RADIO, ID_SAVES, ID_INSTALL, ID_UNINSTALL, ID_CLOSE, ID_PROGRESS, ID_STATUS, ID_LOG, ID_INFO };
static HWND hDir, hGraphics, hRadio, hSaves, hInstall, hUninstall, hClose, hProg, hStatus, hLogBox, hInfo;
static HFONT g_font;

static void RefreshButtons()
{
    wchar_t d[MAX_PATH * 2]; GetWindowTextW(hDir, d, MAX_PATH * 2);
    bool installed = Exists(JoinPath(d, kManifest));
    SetWindowTextW(hInstall, installed ? L"Update / Repair" : L"Install");
    EnableWindow(hUninstall, installed && !g_busy);
    EnableWindow(hInstall, !g_busy);
}

static void StartWork(bool uninstall)
{
    if (g_busy) return;
    wchar_t d[MAX_PATH * 2]; GetWindowTextW(hDir, d, MAX_PATH * 2);
    std::wstring game = d;
    while (!game.empty() && (game.back() == L'\\' || game.back() == L' ')) game.pop_back();
    g_opt.components = { "core" };
    if (SendMessageW(hGraphics, BM_GETCHECK, 0, 0) == BST_CHECKED) g_opt.components.insert("graphics");
    if (SendMessageW(hRadio, BM_GETCHECK, 0, 0) == BST_CHECKED) g_opt.components.insert("radio");
    g_opt.saves = SendMessageW(hSaves, BM_GETCHECK, 0, 0) == BST_CHECKED;
    if (!uninstall && Exists(JoinPath(game, L"LEGOLCUR_DX11.exe")) && !Writable(game)) {
        // relaunch elevated with the same choices
        std::wstring comps; for (auto& c : g_opt.components) if (c != "core") comps += (comps.empty() ? L"" : L",") + W(c);
        std::wstring args = L"/auto /dir=\"" + game + L"\" /components=" + (comps.empty() ? L"none" : comps) + L" /saves=" + (g_opt.saves ? L"1" : L"0");
        if (!g_opt.payloadDir.empty()) args += L" /payload=\"" + g_opt.payloadDir + L"\"";
        SHELLEXECUTEINFOW se{ sizeof se }; se.lpVerb = L"runas"; std::wstring exe = ExePath(); se.lpFile = exe.c_str(); se.lpParameters = args.c_str(); se.nShow = SW_SHOWNORMAL;
        if (ShellExecuteExW(&se)) { PostMessageW(g_wnd, WM_CLOSE, 0, 0); return; }
        MessageBoxW(g_wnd, L"Setup needs permission to write to the game folder and could not get it.", L"Setup", MB_OK | MB_ICONERROR);
        return;
    }
    g_busy = true; RefreshButtons();
    EnableWindow(hGraphics, FALSE); EnableWindow(hRadio, FALSE); EnableWindow(hSaves, FALSE); EnableWindow(hDir, FALSE);
    std::thread([game, uninstall]() {
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        int rc = uninstall ? DoUninstall(game) : DoInstall(game);
        Log("result: " + std::to_string(rc));
        PostMessageW(g_wnd, WM_APP_DONE, rc, 0);
    }).detach();
}

static HWND Ctl(HWND p, const wchar_t* cls, const wchar_t* text, DWORD style, int x, int y, int w, int h, int id, DWORD ex = 0)
{
    HWND c = CreateWindowExW(ex, cls, text, WS_CHILD | WS_VISIBLE | style, x, y, w, h, p, (HMENU)(INT_PTR)id, GetModuleHandleW(nullptr), nullptr);
    SendMessageW(c, WM_SETFONT, (WPARAM)g_font, TRUE);
    return c;
}

static std::wstring CompLabel(const char* id, const wchar_t* fallback)
{
    for (auto& c : g_rel.comps) if (c.id == id) return W(c.title) + L"  (" + W(Mb(c.bytes)) + L")" + (PakAvailable(id) ? L"" : L"  - not downloaded");
    return fallback;
}

static LRESULT CALLBACK WndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE: {
        UINT dpi = GetDpiForWindow(h); auto S = [dpi](int v) { return MulDiv(v, dpi, 96); };
        NONCLIENTMETRICSW ncm{ sizeof ncm }; SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof ncm, &ncm, 0, dpi);
        g_font = CreateFontIndirectW(&ncm.lfMessageFont);
        std::wstring intro = W(g_rel.name) + L" " + W(g_rel.version) + L" - a free, unofficial mod for LEGO City Undercover (Steam).\r\n"
            L"Setup checks your game, backs up your saves, builds PATCH.DAT from your own game files, and copies the mod into the game folder. "
            L"Nothing is downloaded and nothing is sent anywhere. README.txt explains every step.";
        hInfo = Ctl(h, L"STATIC", intro.c_str(), SS_LEFT, S(14), S(12), S(540), S(64), ID_INFO);
        Ctl(h, L"STATIC", L"Game folder:", SS_LEFT, S(14), S(84), S(100), S(18), 0);
        hDir = Ctl(h, L"EDIT", g_opt.dir.c_str(), ES_AUTOHSCROLL | WS_BORDER | WS_TABSTOP, S(14), S(104), S(440), S(24), ID_DIR);
        Ctl(h, L"BUTTON", L"Browse...", BS_PUSHBUTTON | WS_TABSTOP, S(462), S(103), S(92), S(26), ID_BROWSE);
        HWND core = Ctl(h, L"BUTTON", CompLabel("core", L"Core (required)").c_str(), BS_AUTOCHECKBOX, S(14), S(140), S(540), S(20), 0);
        SendMessageW(core, BM_SETCHECK, BST_CHECKED, 0); EnableWindow(core, FALSE);
        hGraphics = Ctl(h, L"BUTTON", CompLabel("graphics", L"Graphics pack").c_str(), BS_AUTOCHECKBOX | WS_TABSTOP, S(14), S(162), S(540), S(20), ID_GRAPHICS);
        hRadio = Ctl(h, L"BUTTON", CompLabel("radio", L"Radio").c_str(), BS_AUTOCHECKBOX | WS_TABSTOP, S(14), S(184), S(540), S(20), ID_RADIO);
        hSaves = Ctl(h, L"BUTTON", L"Back up my saves first (to Documents) - recommended", BS_AUTOCHECKBOX | WS_TABSTOP, S(14), S(212), S(540), S(20), ID_SAVES);
        SendMessageW(hGraphics, BM_SETCHECK, g_opt.components.count("graphics") && PakAvailable("graphics") ? BST_CHECKED : BST_UNCHECKED, 0);
        SendMessageW(hRadio, BM_SETCHECK, g_opt.components.count("radio") && PakAvailable("radio") ? BST_CHECKED : BST_UNCHECKED, 0);
        if (!PakAvailable("graphics")) EnableWindow(hGraphics, FALSE);
        if (!PakAvailable("radio")) EnableWindow(hRadio, FALSE);
        SendMessageW(hSaves, BM_SETCHECK, g_opt.saves ? BST_CHECKED : BST_UNCHECKED, 0);
        hProg = Ctl(h, PROGRESS_CLASSW, L"", 0, S(14), S(242), S(540), S(18), ID_PROGRESS);
        SendMessageW(hProg, PBM_SETRANGE32, 0, 1000);
        hStatus = Ctl(h, L"STATIC", L"Ready.", SS_LEFT | SS_ENDELLIPSIS, S(14), S(266), S(540), S(18), ID_STATUS);
        hLogBox = Ctl(h, L"EDIT", L"", ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL | WS_BORDER, S(14), S(288), S(540), S(120), ID_LOG);
        hInstall = Ctl(h, L"BUTTON", L"Install", BS_DEFPUSHBUTTON | WS_TABSTOP, S(246), S(418), S(110), S(30), ID_INSTALL);
        hUninstall = Ctl(h, L"BUTTON", L"Uninstall", BS_PUSHBUTTON | WS_TABSTOP, S(364), S(418), S(90), S(30), ID_UNINSTALL);
        hClose = Ctl(h, L"BUTTON", L"Close", BS_PUSHBUTTON | WS_TABSTOP, S(462), S(418), S(92), S(30), ID_CLOSE);
        RefreshButtons();
        if (g_opt.dir.empty()) Status("Could not find the game automatically - use Browse to pick the LEGO City Undercover folder.");
        if (g_opt.autoStart) PostMessageW(h, WM_COMMAND, ID_INSTALL, 0);
        return 0;
    }
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case ID_BROWSE: {
            IFileOpenDialog* d = nullptr;
            if (SUCCEEDED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&d)))) {
                DWORD o; d->GetOptions(&o); d->SetOptions(o | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
                d->SetTitle(L"Choose the LEGO City Undercover folder");
                if (SUCCEEDED(d->Show(h))) {
                    IShellItem* it; if (SUCCEEDED(d->GetResult(&it))) { PWSTR p; if (SUCCEEDED(it->GetDisplayName(SIGDN_FILESYSPATH, &p))) { SetWindowTextW(hDir, p); CoTaskMemFree(p); } it->Release(); }
                }
                d->Release();
            }
            RefreshButtons();
            return 0;
        }
        case ID_DIR: if (HIWORD(wp) == EN_CHANGE) RefreshButtons(); return 0;
        case ID_INSTALL: StartWork(false); return 0;
        case ID_UNINSTALL: StartWork(true); return 0;
        case ID_CLOSE: if (g_busy) { if (MessageBoxW(h, L"Stop Setup? Anything done so far is undone.", L"Setup", MB_YESNO | MB_ICONQUESTION) == IDYES) g_cancel = true; return 0; } DestroyWindow(h); return 0;
        }
        break;
    case WM_APP_LOG: {
        std::wstring* s = (std::wstring*)lp;
        int n = GetWindowTextLengthW(hLogBox);
        SendMessageW(hLogBox, EM_SETSEL, n, n);
        SendMessageW(hLogBox, EM_REPLACESEL, FALSE, (LPARAM)(*s + L"\r\n").c_str());
        delete s; return 0;
    }
    case WM_APP_STATUS: { std::wstring* s = (std::wstring*)lp; SetWindowTextW(hStatus, s->c_str()); delete s; return 0; }
    case WM_APP_PROGRESS: SendMessageW(hProg, PBM_SETPOS, wp, 0); return 0;
    case WM_APP_DONE:
        g_busy = false; g_cancel = false;
        EnableWindow(hGraphics, PakAvailable("graphics")); EnableWindow(hRadio, PakAvailable("radio")); EnableWindow(hSaves, TRUE); EnableWindow(hDir, TRUE);
        RefreshButtons();
        return 0;
    case WM_CLOSE: if (g_busy) { SendMessageW(h, WM_COMMAND, ID_CLOSE, 0); return 0; } DestroyWindow(h); return 0;
    case WM_CTLCOLORSTATIC: SetBkMode((HDC)wp, TRANSPARENT); return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

// ------------------------------------------------------------------------------------------- main ---
static std::wstring ArgValue(const std::wstring& a, const wchar_t* key)
{
    size_t n = wcslen(key);
    if (_wcsnicmp(a.c_str(), key, n) != 0) return L"\x01";
    std::wstring v = a.substr(n);
    if (v.size() >= 2 && v.front() == L'"' && v.back() == L'"') v = v.substr(1, v.size() - 2);
    return v;
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int show)
{
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    int argc = 0; LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    for (int i = 1; i < argc; ++i) {
        std::wstring a = argv[i], v;
        if (_wcsicmp(a.c_str(), L"/silent") == 0) g_opt.silent = true;
        else if (_wcsicmp(a.c_str(), L"/uninstall") == 0) g_opt.uninstall = true;
        else if (_wcsicmp(a.c_str(), L"/fullcheck") == 0) g_opt.fullcheck = true;
        else if (_wcsicmp(a.c_str(), L"/noregister") == 0) g_opt.noregister = true;
        else if (_wcsicmp(a.c_str(), L"/auto") == 0) g_opt.autoStart = true;
        else if ((v = ArgValue(a, L"/dir=")) != L"\x01") g_opt.dir = v;
        else if ((v = ArgValue(a, L"/savesdir=")) != L"\x01") g_opt.savesDir = v;
        else if ((v = ArgValue(a, L"/log=")) != L"\x01") g_opt.logPath = v;
        else if ((v = ArgValue(a, L"/payload=")) != L"\x01") g_opt.payloadDir = v;
        else if ((v = ArgValue(a, L"/foreign=")) != L"\x01") g_opt.foreign = U(v);
        else if ((v = ArgValue(a, L"/saves=")) != L"\x01") g_opt.saves = _wtoi(v.c_str());
        else if ((v = ArgValue(a, L"/components=")) != L"\x01") {
            g_opt.components = { "core" }; g_opt.componentsGiven = true;
            std::string s = U(v); size_t p = 0;
            while (p <= s.size()) { size_t q = s.find(',', p); std::string c = s.substr(p, q == std::string::npos ? std::string::npos : q - p); if (!c.empty() && c != "none") g_opt.components.insert(c); if (q == std::string::npos) break; p = q + 1; }
        }
        else { if (g_opt.silent) printf("unknown argument\n"); return 2; }
    }
    LocalFree(argv);
    if (g_opt.silent) { AttachConsole(ATTACH_PARENT_PROCESS); FILE* f; freopen_s(&f, "CONOUT$", "w", stdout); }
    // the log: %TEMP% (or /log=)
    g_logFile = g_opt.logPath;
    if (g_logFile.empty()) { wchar_t t[MAX_PATH]; GetTempPathW(MAX_PATH, t); g_logFile = JoinPath(t, std::wstring(g_opt.uninstall ? L"LCUMod-uninstall.log" : L"LCUMod-setup.log")); }
    _wfopen_s(&g_log, g_logFile.c_str(), L"a, ccs=UTF-8");
    if (g_log) { fclose(g_log); g_log = nullptr; _wfopen_s(&g_log, g_logFile.c_str(), L"ab"); }
    Log("---- Setup started (" + std::string(g_opt.uninstall ? "uninstall" : "install") + (g_opt.silent ? ", silent" : "") + ")");

    // an uninstaller run from inside the game folder: the game folder is its parent's parent
    if (g_opt.dir.empty()) {
        std::wstring ed = ExeDir();
        if (_wcsicmp(PathFindFileNameW(ed.c_str()), kModDir) == 0 && Exists(JoinPath(ed, L"install-manifest.tsv"))) g_opt.dir = ed.substr(0, ed.find_last_of(L"\\"));
    }
    std::string err;
    bool haveRelease = LoadRelease(err);
    if (!haveRelease && !g_opt.uninstall) {
        Log("payload: " + err);
        Tell(L"Setup cannot find its data: " + W(err) + L"\n\nKeep Setup.exe in the same folder as the .lcupak files you downloaded.", true);
        return 16;
    }
    if (!haveRelease) {
        // the uninstaller in <game>\LCUMod has no payload: its name and version come from the manifest
        g_rel.name = "MOD_NAME";
    }
    if (g_opt.dir.empty() && !g_opt.uninstall) g_opt.dir = FindGame();
    if (g_opt.dir.empty() && g_opt.uninstall) {
        std::wstring loc = RegStr(HKEY_CURRENT_USER, kRegKey, L"InstallLocation");
        g_opt.dir = loc;
    }
    if (!g_opt.componentsGiven) {
        g_opt.components = { "core" };
        for (auto& c : g_rel.comps) if (c.defaultOn) g_opt.components.insert(c.id);
    }
    Log("game folder: " + (g_opt.dir.empty() ? std::string("(not found)") : U(g_opt.dir)));

    if (g_opt.silent) {
        if (g_opt.dir.empty()) { Log("game not found; use /dir="); return 10; }
        int rc = g_opt.uninstall ? DoUninstall(g_opt.dir) : DoInstall(g_opt.dir);
        Log("result: " + std::to_string(rc));
        if (g_log) fclose(g_log);
        return rc;
    }
    if (g_opt.uninstall) {
        if (g_opt.dir.empty()) { Tell(L"The mod does not seem to be installed.", true); return 20; }
        int rc = DoUninstall(g_opt.dir);
        if (g_log) fclose(g_log);
        return rc;
    }
    INITCOMMONCONTROLSEX icc{ sizeof icc, ICC_PROGRESS_CLASS | ICC_STANDARD_CLASSES }; InitCommonControlsEx(&icc);
    WNDCLASSW wc{}; wc.lpfnWndProc = WndProc; wc.hInstance = inst; wc.lpszClassName = L"LCUModSetup";
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW); wc.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
    RegisterClassW(&wc);
    UINT dpi = GetDpiForSystem();
    RECT r{ 0, 0, MulDiv(568, dpi, 96), MulDiv(462, dpi, 96) };
    AdjustWindowRectExForDpi(&r, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE, 0, dpi);
    std::wstring title = W(g_rel.name) + L" " + W(g_rel.version) + L" Setup";
    g_wnd = CreateWindowExW(0, wc.lpszClassName, title.c_str(), WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                            CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left, r.bottom - r.top, nullptr, nullptr, inst, nullptr);
    ShowWindow(g_wnd, show); UpdateWindow(g_wnd);
    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0)) { if (!IsDialogMessageW(g_wnd, &m)) { TranslateMessage(&m); DispatchMessageW(&m); } }
    if (g_log) fclose(g_log);
    return 0;
}
