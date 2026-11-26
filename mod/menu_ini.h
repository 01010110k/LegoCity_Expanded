// =============================================================================================
//  menu_ini.h - edits to lcudrive.ini that keep the file as the player knows it (Mod Options).
//
//  WritePrivateProfileString would rewrite "power = 0.75  ; engine power (was 1.0 ...)" as
//  "power=0.8": the inline comment and the alignment are lost, and it is not atomic. This edits
//  only the value token of the lines the reader (GetPrivateProfileString) actually sees, and
//  replaces the file atomically (temp file in the same folder + ReplaceFileA).
//
//  Runs on the HOTKEY thread only (file I/O; see menu_bridge.h for the threading rules).
//  v1: a file that cannot be written (read-only, locked, a protected folder) is a refused save -
//  nothing is changed and the caller says so. There is no %LOCALAPPDATA% copy in v1: lcudrive does
//  not read one at startup, so a change written there would be lost at the next launch.
// =============================================================================================
#pragma once
#include <string>
#include <vector>
#include <cmath>
#include <cstdlib>

namespace mini {

struct Edit { char sec[24]; char key[40]; char val[96]; };

static bool ValueOk(const char* v)
{
    // lcudrive's parser cuts a value at ; or # (config.h IniI / IniF / IniS); a line break would
    // corrupt the file. Leading / trailing blanks would be trimmed by the reader anyway.
    for (const char* p = v; *p; ++p) if (*p == ';' || *p == '#' || *p == '\r' || *p == '\n') return false;
    return true;
}

static void SetEdit(Edit& e, const char* sec, const char* key, const char* val)
{
    strncpy_s(e.sec, sec, _TRUNCATE); strncpy_s(e.key, key, _TRUNCATE); strncpy_s(e.val, val, _TRUNCATE);
}

namespace detail {
static bool Blank(char c) { return c == ' ' || c == '\t'; }
static std::string Trim(const std::string& s)
{
    size_t a = 0, b = s.size();
    while (a < b && Blank(s[a])) ++a;
    while (b > a && Blank(s[b - 1])) --b;
    return s.substr(a, b - a);
}
// "[name]" -> name (trimmed), else ""; GetPrivateProfileString ignores leading blanks before '['
static bool SectionName(const std::string& line, std::string& out)
{
    const std::string t = Trim(line);
    if (t.size() < 2 || t[0] != '[') return false;
    const size_t close = t.find(']');
    if (close == std::string::npos) return false;
    out = Trim(t.substr(1, close - 1));
    return true;
}
// a key line: not a comment, has '='; out = the trimmed key
static bool KeyName(const std::string& line, std::string& out, size_t& eq)
{
    const std::string t = Trim(line);
    if (t.empty() || t[0] == ';' || t[0] == '[') return false;
    eq = line.find('=');
    if (eq == std::string::npos) return false;
    out = Trim(line.substr(0, eq));
    return !out.empty();
}
// The same value, spelled differently? ("1.0" vs "1", "0x20" vs "32", "DRIGHT" vs "dright")
static bool SameValue(const std::string& have, const char* val)
{
    if (_stricmp(have.c_str(), val) == 0) return true;
    char* e1 = nullptr; char* e2 = nullptr;
    const bool hex1 = have.size() > 2 && have[0] == '0' && (have[1] == 'x' || have[1] == 'X');
    const bool hex2 = val[0] == '0' && (val[1] == 'x' || val[1] == 'X');
    const double a = hex1 ? static_cast<double>(strtol(have.c_str(), &e1, 0)) : strtod(have.c_str(), &e1);
    const double b = hex2 ? static_cast<double>(strtol(val, &e2, 0)) : strtod(val, &e2);
    return e1 && e2 && e1 != have.c_str() && e2 != val && !*e1 && !*e2 && std::fabs(a - b) < 1e-6;
}

// Replace the value of a key line, keeping the key, the spacing after '=', the inline comment and
// (when it fits) the comment's column. A value the line already holds (however spelled) is left as it is.
static std::string ReplaceValue(const std::string& line, size_t eq, const char* val)
{
    size_t vs = eq + 1;
    while (vs < line.size() && Blank(line[vs])) ++vs;
    size_t cut = line.size();
    for (size_t i = vs; i < line.size(); ++i) if (line[i] == ';' || line[i] == '#') { cut = i; break; }
    size_t ve = cut;
    while (ve > vs && Blank(line[ve - 1])) --ve;
    if (SameValue(line.substr(vs, ve - vs), val)) return line;
    std::string out = line.substr(0, vs);
    if (vs == eq + 1 && val[0]) out += ' ';                    // "key =" with no blank: keep it readable
    out += val;
    if (cut < line.size())                                     // an inline comment follows
    {
        const size_t col = cut;                                // where it was
        if (out.size() + 1 <= col) out.append(col - out.size(), ' ');
        else out += ' ';
        out += line.substr(cut);
    }
    return out;
}
} // namespace detail

// Apply the edits to an ini text. Matching follows GetPrivateProfileString: section and key names are
// case-insensitive and trimmed, and the FIRST section of a name / first key line in it is the one the
// reader returns, so that is the line edited. A key the section lacks is inserted after the section's
// last key line; a section the file lacks is appended. Every line keeps its own line ending (a file mixing
// CRLF and LF stays mixed; inserted lines take CRLF if the file has any, else LF); lines that are not
// edited stay byte-identical (unknown keys, comments, blank lines). Returns false for a value the parser
// could not read back (the text is then not touched).
static bool ApplyText(const std::string& in, const Edit* e, int n, std::string& out)
{
    using namespace detail;
    for (int i = 0; i < n; ++i) if (!ValueOk(e[i].val)) return false;
    const bool crlf = in.find("\r\n") != std::string::npos;
    const char* nl = crlf ? "\r\n" : "\n";

    std::vector<std::string> lines;                            // without their line endings
    std::vector<std::string> ends;                             // each line's own: "\r\n", "\n" or "" (the last)
    {
        size_t p = 0;
        while (p < in.size())
        {
            size_t q = in.find('\n', p);
            if (q == std::string::npos) { lines.push_back(in.substr(p)); ends.push_back(""); p = in.size(); break; }
            const bool cr = q > p && in[q - 1] == '\r';
            const size_t end = cr ? q - 1 : q;
            lines.push_back(in.substr(p, end - p));
            ends.push_back(cr ? "\r\n" : "\n");
            p = q + 1;
        }
    }
    const bool endsWithNl = !in.empty() && in.back() == '\n';

    std::vector<bool> done(n, false);
    // pass 1: edit existing keys in the first section of each name
    {
        std::string cur; bool inSec = false;
        std::vector<std::string> seenSecs;
        bool firstOfName = false;
        for (std::string& line : lines)
        {
            std::string s;
            if (SectionName(line, s))
            {
                cur = s; inSec = true;
                firstOfName = true;
                for (const std::string& x : seenSecs) if (_stricmp(x.c_str(), s.c_str()) == 0) { firstOfName = false; break; }
                seenSecs.push_back(s);
                continue;
            }
            if (!inSec || !firstOfName) continue;
            std::string k; size_t eq = 0;
            if (!KeyName(line, k, eq)) continue;
            for (int i = 0; i < n; ++i)
            {
                if (done[i] || _stricmp(e[i].sec, cur.c_str()) != 0 || _stricmp(e[i].key, k.c_str()) != 0) continue;
                line = ReplaceValue(line, eq, e[i].val);
                done[i] = true;                                // the reader sees only the first: leave any later duplicate
                // (later edits of the same key in this batch: the last one wins)
                for (int j = i + 1; j < n; ++j)
                    if (!done[j] && _stricmp(e[j].sec, e[i].sec) == 0 && _stricmp(e[j].key, e[i].key) == 0)
                    {
                        line = ReplaceValue(line, eq, e[j].val);   // re-find '=' is unchanged: key text is kept
                        done[j] = true;
                    }
                break;
            }
        }
    }
    // pass 2: keys the file lacks
    for (int i = 0; i < n; ++i)
    {
        if (done[i]) continue;
        // later duplicates of this key in the batch win
        const char* val = e[i].val;
        for (int j = i + 1; j < n; ++j)
            if (!done[j] && _stricmp(e[j].sec, e[i].sec) == 0 && _stricmp(e[j].key, e[i].key) == 0) { val = e[j].val; done[j] = true; }
        done[i] = true;
        const std::string add = std::string(e[i].key) + " = " + val;
        int secAt = -1, lastKey = -1;
        for (int li = 0; li < static_cast<int>(lines.size()); ++li)
        {
            std::string s;
            if (SectionName(lines[li], s))
            {
                if (secAt >= 0) break;                         // the end of the first matching section
                if (_stricmp(s.c_str(), e[i].sec) == 0) { secAt = li; lastKey = li; }
                continue;
            }
            std::string k; size_t eq = 0;
            if (secAt >= 0 && KeyName(lines[li], k, eq)) lastKey = li;
        }
        if (secAt >= 0)
        {
            lines.insert(lines.begin() + lastKey + 1, add);
            ends.insert(ends.begin() + lastKey + 1, nl);
        }
        else
        {
            if (!lines.empty() && !Trim(lines.back()).empty()) { lines.push_back(""); ends.push_back(nl); }
            lines.push_back(std::string("[") + e[i].sec + "]"); ends.push_back(nl);
            lines.push_back(add); ends.push_back(nl);
        }
    }
    // only the file's last line may lack an ending, and it does exactly when the input's did
    for (size_t i = 0; i + 1 < lines.size(); ++i) if (ends[i].empty()) ends[i] = nl;
    if (!lines.empty())
    {
        if (!endsWithNl) ends.back().clear();
        else if (ends.back().empty()) ends.back() = nl;
    }
    out.clear();
    for (size_t i = 0; i < lines.size(); ++i) { out += lines[i]; out += ends[i]; }
    return true;
}

// lcudrive.ini is read with GetPrivateProfileString, which also reads a UTF-16 file (Notepad's "Unicode");
// ApplyText works on bytes and would not find a single [section] in it. Such a file is refused, not damaged.
static bool LooksUtf16(const std::string& s)
{
    if (s.size() >= 2 && ((s[0] == '\xFF' && s[1] == '\xFE') || (s[0] == '\xFE' && s[1] == '\xFF'))) return true;
    return s.find('\0') != std::string::npos;
}

// Read <path>, apply, write <path>.menu.tmp next to it, replace <path> atomically. A missing file is
// created. A read-only file is refused (the player locked it on purpose). Sharing violations (an
// editor holding the file) are retried briefly. err gets a reason. On any failure <path> is unchanged.
static bool ApplyFile(const char* path, const Edit* e, int n, char* err, size_t errCap)
{
    err[0] = 0;
    std::string in;
    const DWORD attr = GetFileAttributesA(path);
    const bool exists = attr != INVALID_FILE_ATTRIBUTES;
    if (exists && (attr & FILE_ATTRIBUTE_READONLY)) { _snprintf_s(err, errCap, _TRUNCATE, "the file is read-only"); return false; }
    if (exists)
    {
        HANDLE h = INVALID_HANDLE_VALUE;
        for (int attempt = 0; attempt < 5 && h == INVALID_HANDLE_VALUE; ++attempt)
        {
            h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (h == INVALID_HANDLE_VALUE && GetLastError() == ERROR_SHARING_VIOLATION) Sleep(40);
            else break;
        }
        if (h == INVALID_HANDLE_VALUE) { _snprintf_s(err, errCap, _TRUNCATE, "open failed (%lu)", GetLastError()); return false; }
        LARGE_INTEGER sz{};
        if (!GetFileSizeEx(h, &sz) || sz.QuadPart >= (1 << 22))
        {
            CloseHandle(h);
            _snprintf_s(err, errCap, _TRUNCATE, "the file is too big or unreadable");
            return false;
        }
        in.resize(static_cast<size_t>(sz.QuadPart));
        DWORD got = 0;
        if (!in.empty() && (!ReadFile(h, &in[0], static_cast<DWORD>(in.size()), &got, nullptr) || got != in.size()))
        {
            const DWORD le = GetLastError();
            CloseHandle(h);
            _snprintf_s(err, errCap, _TRUNCATE, "read failed (%lu)", le);
            return false;
        }
        CloseHandle(h);
    }
    if (LooksUtf16(in)) { _snprintf_s(err, errCap, _TRUNCATE, "the file is saved as UTF-16 - save it as ANSI or UTF-8"); return false; }
    std::string out;
    if (!ApplyText(in, e, n, out)) { _snprintf_s(err, errCap, _TRUNCATE, "a value has ; # or a line break"); return false; }
    if (exists && out == in) return true;                      // nothing to do: no write (the reload is harmless)

    char tmp[MAX_PATH + 16];
    _snprintf_s(tmp, sizeof(tmp), _TRUNCATE, "%s.menu.tmp", path);
    HANDLE h = CreateFileA(tmp, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) { _snprintf_s(err, errCap, _TRUNCATE, "cannot create %s (%lu)", tmp, GetLastError()); return false; }
    DWORD wrote = 0;
    const bool ok = (out.empty() || WriteFile(h, out.data(), static_cast<DWORD>(out.size()), &wrote, nullptr)) &&
                    wrote == out.size() && FlushFileBuffers(h);
    CloseHandle(h);
    if (!ok) { DeleteFileA(tmp); _snprintf_s(err, errCap, _TRUNCATE, "write failed (%lu)", GetLastError()); return false; }

    for (int attempt = 0; attempt < 5; ++attempt)
    {
        // ReplaceFile keeps the original's attributes / ACL and is a single rename for readers
        if (exists ? ReplaceFileA(path, tmp, nullptr, REPLACEFILE_IGNORE_MERGE_ERRORS | REPLACEFILE_IGNORE_ACL_ERRORS, nullptr, nullptr) != 0
                   : MoveFileExA(tmp, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0)
            return true;
        const DWORD le = GetLastError();
        // without a backup name, this error can leave <path> gone and the new text under tmp: finish the move
        if (le == ERROR_UNABLE_TO_MOVE_REPLACEMENT && GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES &&
            MoveFileExA(tmp, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            return true;
        if (le != ERROR_SHARING_VIOLATION && le != ERROR_ACCESS_DENIED && le != ERROR_UNABLE_TO_REMOVE_REPLACED &&
            le != ERROR_LOCK_VIOLATION)
        {
            _snprintf_s(err, errCap, _TRUNCATE, "replace failed (%lu)", le);
            break;
        }
        _snprintf_s(err, errCap, _TRUNCATE, "replace failed (%lu) - the file is in use or the folder is protected", le);
        Sleep(40);
    }
    DeleteFileA(tmp);
    return false;
}

} // namespace mini
