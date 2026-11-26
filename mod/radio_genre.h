// =============================================================================================
//  radio_genre.h - which station the listener's music belongs on (pure; no Windows). A port of
//  tools\genre\classify.py (the reference; test\radio_harness.cpp checks parity case by case), plus
//  the per-station playlist settings, .m3u parsing, and the station's music source resolution.
//
//    a genre value  ->  normalised ("Hip-Hop/Rap" -> "hip hop rap") -> the longest known phrase
//                       (radio_genre_data.h, generated from genre_phrases.tsv / id3v1.tsv)
//    a track        ->  its values voted (the first counts most), then, when the tags settle
//                       nothing: its album, its album artist, a genre word in its folder names,
//                       a stable per-artist coin for "Pop/Rock", soundtracks and Christmas to pop
//    a station      ->  ai | library | service | talk, with the fallbacks of spec v1 section 2
// =============================================================================================
#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <unordered_map>
#include "radio_model.h"
#include "radio_genre_data.h"

namespace radio {

// The music genres a station can have (the codes of radio_genre_data.h) and the talk station.
enum Genre : uint8_t { G_BL, G_CR, G_BC, G_OR, G_BB, G_TB, G_PS, G_KC, G_TALK, G_NONE = 0xFF };
enum : uint8_t { C_X = 0x10, C_Q = 0x11, C_HOL = 0x12, C_OST = 0x13, C_AMB = 0x40 };

inline bool IsStationCode(uint8_t c) { return c < 8; }
inline bool IsAmb(uint8_t c) { return (c & 0xC0) == C_AMB; }
inline uint8_t AmbA(uint8_t c) { return (c >> 3) & 7; }
inline uint8_t AmbB(uint8_t c) { return c & 7; }

inline const char* GenreName(uint8_t g)
{
    static const char* const k[9] = { "pop", "rock", "country", "lo-fi/chill", "funk/disco", "hip hop", "reggae", "classical", "talk" };
    return g < 9 ? k[g] : "none";
}
inline const char* GenreCodeName(uint8_t c)
{
    static const char* const k[8] = { "BL", "CR", "BC", "OR", "BB", "TB", "PS", "KC" };
    static char amb[12];
    if (c < 8) return k[c];
    if (c == C_X) return "X";
    if (c == C_Q) return "?";
    if (c == C_HOL) return "HOL";
    if (c == C_OST) return "OST";
    if (IsAmb(c)) { std::snprintf(amb, sizeof(amb), "AMB:%s+%s", k[AmbA(c)], k[AmbB(c)]); return amb; }
    return "??";
}

// The built-in map: station id -> its genre (G_NONE for an id the radio doesn't know).
inline uint8_t StationGenre(const std::string& id)
{
    for (const StationGenreEntry& e : kStationGenres) if (id == e.id) return e.genre;
    return G_NONE;
}

inline void ApplyStationGenres(Library& L)
{
    for (Station& s : L.stations)
    {
        s.genre = StationGenre(s.id);
        // [radiologo1] a station the built-in map doesn't know that airs talk segments (programmes) is a talk
        // station (Baseplate 93.5; its few 'songs' are theme beds): never given genre music from the library
        if (s.genre == G_NONE && !s.byKind[K_SEGMENT].empty()) s.genre = G_TALK;
    }
}

inline const DefaultLink* DefaultLinkFor(const std::string& id)
{
    for (const DefaultLink& d : kDefaultLinks) if (id == d.station) return &d;
    return nullptr;
}

// ---- normalising ------------------------------------------------------------------------------------
inline bool GenreWordChar(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'; }
inline bool GenreSpace(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v'; }

// UTF-8 -> what classify.py's NFKD + combining-strip + lower makes of it (non-ASCII left over -> ' ').
inline std::string GenreFold(const std::string& in)
{
    std::string out;
    out.reserve(in.size());
    size_t i = 0;
    while (i < in.size())
    {
        const unsigned char b = static_cast<unsigned char>(in[i]);
        uint32_t cp = 0; int len = 1;
        if (b < 0x80) cp = b;
        else if ((b & 0xE0) == 0xC0 && i + 1 < in.size()) { cp = ((b & 0x1Fu) << 6) | (in[i + 1] & 0x3F); len = 2; }
        else if ((b & 0xF0) == 0xE0 && i + 2 < in.size()) { cp = ((b & 0x0Fu) << 12) | ((in[i + 1] & 0x3Fu) << 6) | (in[i + 2] & 0x3F); len = 3; }
        else if ((b & 0xF8) == 0xF0 && i + 3 < in.size()) { cp = ((b & 0x07u) << 18) | ((in[i + 1] & 0x3Fu) << 12) | ((in[i + 2] & 0x3Fu) << 6) | (in[i + 3] & 0x3F); len = 4; }
        else cp = 0xFFFD;
        i += static_cast<size_t>(len);
        if (cp < 0x80) { char c = static_cast<char>(cp); if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + 32); out += c; continue; }
        // combining marks (an already-decomposed tag): dropped, as NFKD + combining-strip does
        if ((cp >= 0x0300 && cp <= 0x036F) || (cp >= 0x1AB0 && cp <= 0x1AFF) || (cp >= 0x1DC0 && cp <= 0x1DFF) ||
            (cp >= 0x20D0 && cp <= 0x20FF) || (cp >= 0xFE20 && cp <= 0xFE2F)) continue;
        int lo = 0, hi = kFoldCount - 1;
        const char* f = nullptr;
        while (lo <= hi)
        {
            const int mid = (lo + hi) / 2;
            if (kFold[mid].cp == cp) { f = kFold[mid].s; break; }
            if (kFold[mid].cp < cp) lo = mid + 1; else hi = mid - 1;
        }
        if (f) out += f; else out += ' ';
    }
    return out;
}

// One of classify.py's REWRITES patterns: literal alternatives with \s* or [\s-]* between them,
// anchored at word boundaries at both ends.
struct GenreRwTok { const char* alts[7]; uint8_t gap; };   // gap after this token: 0 none, 1 \s*, 2 [\s-]*

inline size_t GenreRwMatch(const std::string& s, size_t pos, const GenreRwTok* t, int nt, int k, int* which)
{
    if (k == nt) return pos;
    for (int a = 0; a < 7 && t[k].alts[a]; ++a)
    {
        const char* lit = t[k].alts[a];
        const size_t n = std::strlen(lit);
        if (s.compare(pos, n, lit) != 0) continue;
        size_t p = pos + n;
        if (t[k].gap == 1) while (p < s.size() && GenreSpace(s[p])) ++p;
        else if (t[k].gap == 2) while (p < s.size() && (GenreSpace(s[p]) || s[p] == '-')) ++p;
        if (which) which[k] = a;
        const size_t e = GenreRwMatch(s, p, t, nt, k + 1, which);
        if (e != std::string::npos) return e;
    }
    return std::string::npos;
}

// re.sub(pattern, repl, s) for one pattern. `cap` > 0: the replacement is "<alt of token 0> <alt of
// token 1>" (the k-/j-/c-pop rule).
inline std::string GenreRw(const std::string& s, const GenreRwTok* t, int nt, const char* repl, bool cap = false)
{
    std::string out;
    size_t i = 0;
    while (i < s.size())
    {
        if (GenreWordChar(s[i]) && (i == 0 || !GenreWordChar(s[i - 1])))
        {
            int which[8] = {};
            const size_t e = GenreRwMatch(s, i, t, nt, 0, which);
            if (e != std::string::npos && (e == s.size() || !GenreWordChar(s[e])))
            {
                if (cap) { out += t[0].alts[which[0]]; out += ' '; out += t[nt - 1].alts[which[nt - 1]]; }
                else out += repl;
                i = e;
                continue;
            }
        }
        out += s[i++];
    }
    return out;
}

// classify.py normalize(): fold, lower, the token rewrites in order, punctuation -> space, collapse.
inline std::string NormalizeGenre(const std::string& utf8)
{
    std::string s = GenreFold(utf8);
    static const GenreRwTok rnb[] = { { { "r" }, 1 }, { { "&", "and", "'n'", "n", "'" }, 1 }, { { "b" }, 0 } };
    static const GenreRwTok rhy[] = { { { "rhythm" }, 1 }, { { "&", "and", "'n'", "n" }, 1 }, { { "blues" }, 0 } };
    static const GenreRwTok hip[] = { { { "hip" }, 2 }, { { "hop" }, 0 } };
    static const GenreRwTok lof[] = { { { "lo" }, 2 }, { { "fi" }, 0 } };
    static const GenreRwTok kpo[] = { { { "k", "j", "c" }, 2 }, { { "pop", "rock" }, 0 } };
    static const GenreRwTok dnb1[] = { { { "drum" }, 1 }, { { "&", "and", "'n'", "n", "'" }, 1 }, { { "bass" }, 0 } };
    static const GenreRwTok dnb2[] = { { { "d" }, 1 }, { { "&", "'n'", "n" }, 1 }, { { "b" }, 0 } };
    static const GenreRwTok rnr[] = { { { "rock" }, 1 }, { { "&", "and", "'n'", "'n", "n'", "n" }, 1 }, { { "roll" }, 0 } };
    static const GenreRwTok nud[] = { { { "nu" }, 2 }, { { "disco" }, 0 } };
    static const GenreRwTok doo[] = { { { "doo" }, 2 }, { { "wop" }, 0 } };
    static const GenreRwTok hin[] = { { { "hi" }, 2 }, { { "nrg" }, 0 } };
    s = GenreRw(s, rnb, 3, "rnb");
    s = GenreRw(s, rhy, 3, "rhythm and blues");
    s = GenreRw(s, hip, 2, "hip hop");
    s = GenreRw(s, lof, 2, "lo fi");
    s = GenreRw(s, kpo, 2, nullptr, true);
    {
        // drum'n'bass | d&b: one pattern (an alternation) in classify.py - tried in that order at each place
        std::string out;
        size_t i = 0;
        while (i < s.size())
        {
            if (GenreWordChar(s[i]) && (i == 0 || !GenreWordChar(s[i - 1])))
            {
                size_t e = GenreRwMatch(s, i, dnb1, 3, 0, nullptr);
                if (e != std::string::npos && !(e == s.size() || !GenreWordChar(s[e]))) e = std::string::npos;
                if (e == std::string::npos)
                {
                    e = GenreRwMatch(s, i, dnb2, 3, 0, nullptr);
                    if (e != std::string::npos && !(e == s.size() || !GenreWordChar(s[e]))) e = std::string::npos;
                }
                if (e != std::string::npos) { out += "drum and bass"; i = e; continue; }
            }
            out += s[i++];
        }
        s = out;
    }
    s = GenreRw(s, rnr, 3, "rock and roll");
    s = GenreRw(s, nud, 2, "nu disco");
    s = GenreRw(s, doo, 2, "doo wop");
    s = GenreRw(s, hin, 2, "hi nrg");
    std::string t;
    t.reserve(s.size() + 8);
    for (char c : s)
    {
        if (c == '&' || c == '+') t += " and ";
        else t += c;
    }
    std::string out;
    bool sp = true;
    for (char c : t)
    {
        const bool keep = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9');
        if (keep) { out += c; sp = false; }
        else if (!sp) { out += ' '; sp = true; }
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

// ---- the classifier ---------------------------------------------------------------------------------
struct GenreClassifier
{
    std::unordered_map<std::string, uint8_t> extra;      // genre_<id> words (normalised): ahead of the table
    std::vector<std::vector<std::string>> skip;           // library_skip phrases (normalised, as words)
    int maxWords = kGenreMaxWords;

    // genre_<id> = "shoegaze, post-rock": these words mean that station
    void AddWords(const std::string& list, uint8_t station)
    {
        size_t p = 0;
        while (p <= list.size())
        {
            size_t e = list.find_first_of(",;|", p);
            if (e == std::string::npos) e = list.size();
            const std::string w = NormalizeGenre(list.substr(p, e - p));
            if (!w.empty() && !extra.count(w))
            {
                extra[w] = station;
                int n = 1; for (char c : w) if (c == ' ') ++n;
                if (n > maxWords) maxWords = n;
            }
            p = e + 1;
        }
    }
    // library_skip = "horrorcore, gangsta, drill": a track with one of these words in any of its genre
    // values never airs ("Gangsta Rap", "UK Drill" included)
    void AddSkip(const std::string& list)
    {
        size_t p = 0;
        while (p <= list.size())
        {
            size_t e = list.find_first_of(",;|", p);
            if (e == std::string::npos) e = list.size();
            const std::string w = NormalizeGenre(list.substr(p, e - p));
            if (!w.empty())
            {
                std::vector<std::string> toks;
                size_t a = 0;
                while (a < w.size()) { size_t b = w.find(' ', a); if (b == std::string::npos) b = w.size(); toks.push_back(w.substr(a, b - a)); a = b + 1; }
                skip.push_back(toks);
            }
            p = e + 1;
        }
    }
};

inline int GenreTableFind(const std::string& p)
{
    int lo = 0, hi = kGenrePhraseCount - 1;
    while (lo <= hi)
    {
        const int mid = (lo + hi) / 2;
        const int c = std::strcmp(kGenrePhrases[mid].p, p.c_str());
        if (c == 0) return mid;
        if (c < 0) lo = mid + 1; else hi = mid - 1;
    }
    return -1;
}

inline bool GenreLookup(const GenreClassifier& g, const std::string& p, uint8_t& code)
{
    auto it = g.extra.find(p);
    if (it != g.extra.end()) { code = it->second; return true; }
    const int i = GenreTableFind(p);
    if (i < 0) return false;
    code = kGenrePhrases[i].code;
    return true;
}

inline void GenreTokens(const std::string& n, std::vector<std::string>& toks)
{
    toks.clear();
    size_t a = 0;
    while (a < n.size()) { size_t b = n.find(' ', a); if (b == std::string::npos) b = n.size(); if (b > a) toks.push_back(n.substr(a, b - a)); a = b + 1; }
}

// classify.py classify_text(): the whole value, else the longest phrase inside it (ties: the
// rightmost, the head noun; '?' phrases skipped inside).
inline uint8_t ClassifyText(const GenreClassifier& g, const std::string& value)
{
    const std::string n = NormalizeGenre(value);
    if (n.empty()) return C_Q;
    uint8_t code = C_Q;
    if (GenreLookup(g, n, code)) return code;
    std::vector<std::string> toks;
    GenreTokens(n, toks);
    int bestL = 0, bestI = -1; uint8_t best = C_Q;
    const int nt = static_cast<int>(toks.size());
    for (int i = 0; i < nt; ++i)
    {
        const int maxL = g.maxWords < nt - i ? g.maxWords : nt - i;
        for (int L = maxL; L >= 1; --L)
        {
            std::string p = toks[static_cast<size_t>(i)];
            for (int k = 1; k < L; ++k) { p += ' '; p += toks[static_cast<size_t>(i + k)]; }
            uint8_t c;
            if (GenreLookup(g, p, c) && c != C_Q)
            {
                if (bestI < 0 || L > bestL || (L == bestL && i > bestI)) { bestL = L; bestI = i; best = c; }
                break;
            }
        }
    }
    return bestI >= 0 ? best : C_Q;
}

inline bool GenreSkipHit(const GenreClassifier& g, const std::string& value)
{
    if (g.skip.empty()) return false;
    std::vector<std::string> toks;
    GenreTokens(NormalizeGenre(value), toks);
    for (const auto& sk : g.skip)
        for (size_t i = 0; i + sk.size() <= toks.size(); ++i)
        {
            size_t k = 0;
            while (k < sk.size() && toks[i + k] == sk[k]) ++k;
            if (k == sk.size()) return true;
        }
    return false;
}

struct GenreValue { bool id3 = false; int num = 0; std::string text; };

inline std::string GenreStrip(const std::string& s)
{
    size_t a = 0, b = s.size();
    while (a < b && GenreSpace(s[a])) ++a;
    while (b > a && GenreSpace(s[b - 1])) --b;
    return s.substr(a, b - a);
}

inline bool GenreAllDigits(const std::string& s, size_t maxLen)
{
    if (s.empty() || s.size() > maxLen) return false;
    for (char c : s) if (c < '0' || c > '9') return false;
    return true;
}

// classify.py split_values(): ; | , and a doubled backslash separate values; "(17)", a bare 1-3 digit
// value and "Rock (80)" give ID3v1 numbers in order; (RX) remix / (CR) cover are dropped.
inline void SplitGenreValues(const std::vector<std::string>& values, std::vector<GenreValue>& out)
{
    out.clear();
    for (const std::string& v : values)
    {
        std::vector<std::string> parts;
        std::string cur;
        for (size_t i = 0; i < v.size(); ++i)
        {
            const char c = v[i];
            if (c == ';' || c == '|' || c == ',') { parts.push_back(cur); cur.clear(); continue; }
            if (c == '\\' && i + 1 < v.size() && v[i + 1] == '\\') { parts.push_back(cur); cur.clear(); ++i; continue; }
            cur += c;
        }
        parts.push_back(cur);
        for (const std::string& raw : parts)
        {
            const std::string part = GenreStrip(raw);
            if (part.empty()) continue;
            size_t j = 0;
            while (j < part.size())
            {
                if (part[j] == '(')
                {
                    const size_t close = part.find(')', j + 1);
                    if (close != std::string::npos)
                    {
                        const std::string in = part.substr(j + 1, close - j - 1);
                        if (GenreAllDigits(in, 3)) { GenreValue g; g.id3 = true; g.num = std::atoi(in.c_str()); out.push_back(g); j = close + 1; continue; }
                        if (in == "RX" || in == "CR") { j = close + 1; continue; }
                    }
                    ++j; continue;                                 // a parenthesis with no match: skipped
                }
                if (part[j] == ')') { ++j; continue; }
                size_t e = j;
                while (e < part.size() && part[e] != '(' && part[e] != ')') ++e;
                const std::string rest = GenreStrip(part.substr(j, e - j));
                j = e;
                if (rest.empty()) continue;
                GenreValue g;
                if (GenreAllDigits(rest, 3)) { g.id3 = true; g.num = std::atoi(rest.c_str()); }
                else g.text = rest;
                out.push_back(g);
            }
        }
    }
}

struct GenreResult { uint8_t code = C_Q; bool holiday = false; };

// classify.py classify_values(): one track's genre values -> a station, AMB, OST, X or ?.
inline GenreResult ClassifyValues(const GenreClassifier& g, const std::vector<std::string>& values)
{
    GenreResult r;
    for (const std::string& v : values) if (GenreSkipHit(g, v)) { r.code = C_X; return r; }
    std::vector<GenreValue> items;
    SplitGenreValues(values, items);
    std::vector<uint8_t> codes;
    for (const GenreValue& it : items)
        codes.push_back(it.id3 ? (it.num >= 0 && it.num < 192 ? kId3v1Codes[it.num] : C_Q) : ClassifyText(g, it.text));
    if (!codes.empty() && codes[0] == C_X) { r.code = C_X; return r; }
    double votes[8] = {}; int order[8]; bool has[8] = {};
    for (int& o : order) o = -1;
    uint8_t amb = 0; bool ost = false;
    for (size_t i = 0; i < codes.size(); ++i)
    {
        const uint8_t c = codes[i];
        const double w = i == 0 ? 1.0 : 0.75;
        if (IsStationCode(c)) { votes[c] += w; has[c] = true; if (order[c] < 0) order[c] = static_cast<int>(i); }
        else if (IsAmb(c))
        {
            for (uint8_t s : { AmbA(c), AmbB(c) }) { votes[s] += w / 2; has[s] = true; if (order[s] < 0) order[s] = static_cast<int>(i); }
            if (!amb) amb = c;
        }
        else if (c == C_HOL) r.holiday = true;
        else if (c == C_OST) ost = true;
    }
    double top = -1.0;
    for (int s = 0; s < 8; ++s) if (has[s] && votes[s] > top) top = votes[s];
    if (top >= 0.0)
    {
        int nTied = 0, pick = -1;
        bool tiedA = false, tiedB = false, other = false;
        for (int s = 0; s < 8; ++s)
        {
            if (!has[s] || votes[s] != top) continue;
            ++nTied;
            if (pick < 0 || order[s] < order[pick]) pick = s;
            if (amb && s == AmbA(amb)) tiedA = true;
            else if (amb && s == AmbB(amb)) tiedB = true;
            else other = true;
        }
        if (nTied > 1 && amb && tiedA && tiedB && !other) { r.code = amb; return r; }
        r.code = static_cast<uint8_t>(pick);
        return r;
    }
    r.code = ost ? C_OST : C_Q;
    return r;
}

// ---- a whole library ------------------------------------------------------------------------------------
enum LibWhy : uint8_t { WHY_TAG, WHY_SCORE, WHY_ALBUM, WHY_ARTIST, WHY_FOLDER, WHY_AMB, WHY_OST_HOL, WHY_UNSORTED_TO,
                        WHY_UNSORTED, WHY_NEVER, WHY_HOLIDAY_OUT, WHY_HOLIDAY_NEVER };
inline const char* LibWhyName(uint8_t w)
{
    static const char* const k[] = { "tag", "soundtrack album says score", "album", "album artist", "folder name",
                                     "ambiguous, per-artist coin", "soundtrack/holiday default", "unsorted -> library_unsorted",
                                     "unsorted", "never aired", "holiday, out of season", "holiday, library_holiday = never" };
    return w < sizeof(k) / sizeof(k[0]) ? k[w] : "?";
}

struct LibTrack
{
    // in
    std::string path;              // UTF-8, '\' or '/'
    std::string root;              // the library folder it was found under
    std::vector<std::string> genres;
    std::string artist, albumArtist, album;
    // out
    uint8_t code = C_Q;
    bool    holiday = false;
    int     station = -1;          // a genre 0-7 (the station that plays it), -1 = none
    uint8_t why = WHY_UNSORTED;
};

struct LibOptions
{
    int holiday = 0;               // 0 season (December only), 1 always, 2 never
    int month = 1;                 // the local month, 1-12
    int unsorted = -1;             // -1 skip, else a genre 0-7
};

inline std::string GenreLower(const std::string& s)
{
    std::string o(s);
    for (char& c : o) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + 32);
    return o;
}

inline std::string GenreDir(const std::string& path)
{
    const size_t s = path.find_last_of("\\/");
    return s == std::string::npos ? std::string() : path.substr(0, s);
}

inline uint32_t Fnv1a32(const std::string& s)
{
    uint32_t h = 2166136261u;
    for (unsigned char c : s) { h ^= c; h *= 16777619u; }
    return h;
}

inline bool GenreHasWord(const std::string& lowerS, const char* w)
{
    const size_t n = std::strlen(w);
    for (size_t i = lowerS.find(w); i != std::string::npos; i = lowerS.find(w, i + 1))
    {
        const bool a = i == 0 || !GenreWordChar(lowerS[i - 1]);
        const bool b = i + n >= lowerS.size() || !GenreWordChar(lowerS[i + n]);
        if (a && b) return true;
    }
    return false;
}

// classify.py resolve_library(), plus the season / unsorted options.
inline void ResolveLibrary(const GenreClassifier& g, std::vector<LibTrack>& tracks, const LibOptions& opt)
{
    for (LibTrack& t : tracks)
    {
        const GenreResult r = ClassifyValues(g, t.genres);
        t.code = r.code; t.holiday = r.holiday;
    }
    auto majority = [&](const std::vector<int>& group) -> int {
        int tally[8] = {}, total = 0;
        for (int i : group) if (IsStationCode(tracks[static_cast<size_t>(i)].code)) { ++tally[tracks[static_cast<size_t>(i)].code]; ++total; }
        if (!total) return -1;
        int best = -1;
        for (int s = 0; s < 8; ++s) if (tally[s] && (best < 0 || tally[s] > tally[best])) best = s;
        return tally[best] * 10 >= total * 6 ? best : -1;
    };
    std::unordered_map<std::string, std::vector<int>> albums, artists;
    for (size_t i = 0; i < tracks.size(); ++i)
    {
        const LibTrack& t = tracks[i];
        albums[GenreLower(GenreDir(t.path)) + '\x1f' + GenreLower(t.album)].push_back(static_cast<int>(i));
        const std::string a = GenreLower(t.albumArtist.empty() ? t.artist : t.albumArtist);
        if (!a.empty() && a != "various artists" && a != "various") artists[a].push_back(static_cast<int>(i));
    }
    for (LibTrack& t : tracks)
    {
        t.station = -1;
        if (t.code == C_X) { t.why = WHY_NEVER; continue; }
        if (IsStationCode(t.code)) { t.station = t.code; t.why = WHY_TAG; }
        else if (t.code == C_OST && GenreHasWord(GenreLower(t.album), "score")) { t.station = G_KC; t.why = WHY_SCORE; }
        else
        {
            int s = majority(albums[GenreLower(GenreDir(t.path)) + '\x1f' + GenreLower(t.album)]);
            if (s >= 0) { t.station = s; t.why = WHY_ALBUM; }
            const std::string a = GenreLower(t.albumArtist.empty() ? t.artist : t.albumArtist);
            if (t.station < 0)
            {
                auto it = artists.find(a);
                s = it == artists.end() ? -1 : majority(it->second);
                if (s >= 0) { t.station = s; t.why = WHY_ARTIST; }
            }
            if (t.station < 0 && !t.root.empty())
            {
                // a genre word in a folder name under the library root, deepest first
                std::string dir = GenreDir(t.path), root = t.root;
                for (char& c : dir) if (c == '/') c = '\\';
                for (char& c : root) if (c == '/') c = '\\';
                while (!root.empty() && root.back() == '\\') root.pop_back();
                if (dir.size() > root.size() && GenreLower(dir.substr(0, root.size())) == GenreLower(root) && dir[root.size()] == '\\')
                {
                    std::string rel = dir.substr(root.size() + 1);
                    std::vector<std::string> comps;
                    size_t p = 0;
                    while (p <= rel.size()) { size_t e = rel.find('\\', p); if (e == std::string::npos) e = rel.size(); if (e > p) comps.push_back(rel.substr(p, e - p)); p = e + 1; }
                    for (size_t k = comps.size(); k-- > 0;)
                    {
                        if (comps[k] == ".") continue;
                        const uint8_t c = ClassifyText(g, comps[k]);
                        if (IsStationCode(c)) { t.station = c; t.why = WHY_FOLDER; break; }
                    }
                }
            }
            if (t.station < 0 && IsAmb(t.code))
            {
                std::string key = a.empty() ? GenreLower(t.path) : a;
                t.station = (Fnv1a32(key) & 1) ? AmbB(t.code) : AmbA(t.code);
                t.why = WHY_AMB;
            }
            if (t.station < 0 && (t.code == C_OST || t.holiday)) { t.station = G_BL; t.why = WHY_OST_HOL; }
            if (t.station < 0)
            {
                if (opt.unsorted >= 0 && opt.unsorted < 8) { t.station = opt.unsorted; t.why = WHY_UNSORTED_TO; }
                else t.why = WHY_UNSORTED;
            }
        }
        if (t.station >= 0 && t.holiday)
        {
            if (opt.holiday == 2) { t.station = -1; t.why = WHY_HOLIDAY_NEVER; }
            else if (opt.holiday == 0 && opt.month != 12) { t.station = -1; t.why = WHY_HOLIDAY_OUT; }
        }
    }
}

// ---- playlist settings ------------------------------------------------------------------------------
enum PlForm : uint8_t { PL_NONE, PL_FOLDER, PL_M3U, PL_SPOTIFY, PL_WEB, PL_BAD };
static const char* const kPlFormNames[6] = { "none", "folder", "m3u", "spotify", "web", "bad" };

inline bool PlBase62(const std::string& s, size_t pos, size_t n)
{
    if (s.size() < pos + n) return false;
    for (size_t i = pos; i < pos + n; ++i)
    {
        const char c = s[i];
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))) return false;
    }
    return true;
}

inline bool PlEndsWith(const std::string& lower, const char* suf)
{
    const size_t n = std::strlen(suf);
    return lower.size() >= n && lower.compare(lower.size() - n, n, suf) == 0;
}

// What a playlist_<id> value is. `canon` gets spotify:<type>:<id> for a Spotify link.
inline uint8_t PlaylistForm(const std::string& raw, std::string* canon = nullptr)
{
    const std::string v = GenreStrip(raw);
    if (v.empty()) return PL_NONE;
    const std::string low = GenreLower(v);
    static const char* const kTypes[] = { "playlist", "album", "artist", "track", "show" };
    if (low.compare(0, 8, "spotify:") == 0)
    {
        for (const char* t : kTypes)
        {
            const std::string pre = std::string("spotify:") + t + ":";
            if (low.compare(0, pre.size(), pre) != 0) continue;
            if (!PlBase62(v, pre.size(), 22)) return PL_BAD;
            const std::string tail = low.substr(pre.size() + 22);
            if (!tail.empty() && tail != ":play") return PL_BAD;
            if (canon) *canon = std::string("spotify:") + t + ":" + v.substr(pre.size(), 22);
            return PL_SPOTIFY;
        }
        return PL_BAD;
    }
    if (low.compare(0, 8, "https://") == 0)
    {
        const std::string host = "https://open.spotify.com/";
        if (low.compare(0, host.size(), host) == 0)
        {
            size_t p = host.size();
            if (low.compare(p, 5, "intl-") == 0) { const size_t sl = low.find('/', p); if (sl != std::string::npos) p = sl + 1; }
            for (const char* t : kTypes)
            {
                const std::string pre = std::string(t) + "/";
                if (low.compare(p, pre.size(), pre) != 0) continue;
                const size_t idp = p + pre.size();
                if (!PlBase62(v, idp, 22)) break;
                const size_t after = idp + 22;
                if (after < v.size() && v[after] != '?' && v[after] != '/' && v[after] != '#') break;
                if (canon) *canon = std::string("spotify:") + t + ":" + v.substr(idp, 22);
                return PL_SPOTIFY;
            }
        }
        if (canon) *canon = v;
        return PL_WEB;
    }
    if (low.find("://") != std::string::npos || low.compare(0, 5, "file:") == 0 || low.find("file:") != std::string::npos) return PL_BAD;
    if (PlEndsWith(low, ".m3u") || PlEndsWith(low, ".m3u8")) return PL_M3U;
    static const char* const kBadExt[] = { ".exe", ".bat", ".cmd", ".com", ".lnk", ".ps1", ".vbs", ".js", ".msi" };
    for (const char* e : kBadExt) if (PlEndsWith(low, e)) return PL_BAD;
    return PL_FOLDER;
}

inline int PlHex(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

// "\\host\share\" (lower case) of a network path, "" for a local one. \\?\UNC\host\share counts as
// \\host\share; \\?\C:\x is local.
inline std::string ShareOf(const std::string& path)
{
    std::string p = path;
    for (char& c : p) if (c == '/') c = '\\';
    if (p.compare(0, 8, "\\\\?\\UNC\\") == 0 || p.compare(0, 8, "\\\\?\\unc\\") == 0) p = "\\\\" + p.substr(8);
    else if (p.compare(0, 4, "\\\\?\\") == 0 || p.compare(0, 4, "\\\\.\\") == 0) return std::string();
    if (p.size() < 3 || p[0] != '\\' || p[1] != '\\') return std::string();
    size_t a = p.find('\\', 2);
    if (a == std::string::npos) return GenreLower(p) + "\\";
    size_t b = p.find('\\', a + 1);
    return GenreLower(b == std::string::npos ? p + "\\" : p.substr(0, b + 1));
}

// The file entries of an .m3u / .m3u8 (already UTF-8): comments and #EXT lines skipped, web entries
// skipped, file:/// URLs percent-decoded, relative entries joined to the playlist's folder.
// Paths come back with backslashes. Network entries (\\host\share, file://host/share,
// \\?\UNC\...) are skipped - a downloaded playlist must not make the game reach out to other
// machines - unless they are on the playlist's own share or on one in `allowShares` (the listener's
// own music folders); device paths (\\.\) always. `netSkipped` counts what was left out.
inline void ParseM3u(const std::string& text, const std::string& baseDir, std::vector<std::string>& out, int* netSkipped = nullptr,
                     const std::vector<std::string>* allowShares = nullptr)
{
    out.clear();
    if (netSkipped) *netSkipped = 0;
    const std::string ownShare = ShareOf(baseDir);
    size_t p = 0;
    if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF && static_cast<unsigned char>(text[1]) == 0xBB &&
        static_cast<unsigned char>(text[2]) == 0xBF) p = 3;
    std::string base = baseDir;
    for (char& c : base) if (c == '/') c = '\\';
    if (!base.empty() && base.back() != '\\') base += '\\';
    while (p < text.size())
    {
        size_t e = text.find('\n', p);
        if (e == std::string::npos) e = text.size();
        std::string line = GenreStrip(text.substr(p, e - p));
        p = e + 1;
        if (line.empty() || line[0] == '#') continue;
        const std::string low = GenreLower(line);
        std::string path;
        if (low.compare(0, 7, "file://") == 0)
        {
            std::string rest = line.substr(7);
            std::string dec;
            for (size_t i = 0; i < rest.size(); ++i)
            {
                if (rest[i] == '%' && i + 2 < rest.size() && PlHex(rest[i + 1]) >= 0 && PlHex(rest[i + 2]) >= 0)
                { dec += static_cast<char>(PlHex(rest[i + 1]) * 16 + PlHex(rest[i + 2])); i += 2; }
                else dec += rest[i];
            }
            if (!dec.empty() && dec[0] == '/' && dec.size() > 2 && dec[2] == ':') path = dec.substr(1);   // file:///C:/x
            else if (!dec.empty() && dec[0] != '/') path = "//" + dec;                                    // file://server/share
            else path = dec;
        }
        else if (low.find("://") != std::string::npos) continue;                                          // a web entry
        else path = line;
        for (char& c : path) if (c == '/') c = '\\';
        if (path.compare(0, 4, "\\\\.\\") == 0) { if (netSkipped) ++*netSkipped; continue; }               // a device
        if (path.compare(0, 4, "\\\\?\\") == 0 && ShareOf(path).empty()) path = path.substr(4);            // \\?\C:\x
        const std::string share = ShareOf(path);
        if (!share.empty() && share != ownShare)
        {
            bool ok = false;
            if (allowShares) for (const std::string& s : *allowShares) if (GenreLower(s) == share) ok = true;
            if (!ok) { if (netSkipped) ++*netSkipped; continue; }
        }
        const bool abs = (path.size() > 2 && path[1] == ':' && path[2] == '\\') || (path.size() > 1 && path[0] == '\\' && path[1] == '\\');
        if (!abs)
        {
            if (!path.empty() && path[0] == '\\') { if (base.size() >= 2 && base[1] == ':') path = base.substr(0, 2) + path; }
            else path = base + path;
        }
        out.push_back(path);
    }
}

// ---- the station's music source (spec v1 section 2) --------------------------------------------------
enum SrcWhy : uint8_t {
    SW_AI, SW_AI_NONE, SW_AI_TOGGLE, SW_LIB_GENRE, SW_LIB_PLAYLIST, SW_LIB_SHORT_TALK, SW_LIB_SHORT_AI, SW_LIB_OFF,
    SW_SCAN, SW_APP, SW_APP_OFF_LIB, SW_APP_OFF_TALK, SW_TALK, SW_TALK_STATION
};

struct SourceIn
{
    uint8_t want = SRC_AI;         // music_<id> if set, else the global music
    bool    aiToggle = false;      // Ctrl+Alt+/ this session: the AI songs everywhere
    bool    hasAi = true;          // the station has songs or segments
    uint8_t form = PL_NONE;        // its playlist_<id>
    uint8_t genre = G_NONE;        // its genre (G_TALK: brick-talk)
    int     genrePool = 0, overridePool = 0;
    int     libMin = 8;
    bool    shortAi = false;       // library_short = ai
    bool    libOff = false;        // the library faulted this session
    bool    scanPending = false;   // the first scan runs and there is no cache yet
    bool    appOff = false;        // the media session link is unavailable / faulted
    bool    appBound = true;       // an app session is bound (debounced by the thread)
    bool    talkover = false;      // library_talkover (never on Kings Court: its host doesn't talk over music)
};

struct SourceOut
{
    uint8_t src = SRC_AI;
    bool    genreLines = false, introOver = false;
    bool    overridePool = false;  // LIB from the playlist (folder / .m3u), not the genre
    uint8_t why = SW_AI;
};

inline SourceOut ResolveSource(const SourceIn& in)
{
    SourceOut o;
    uint8_t want = in.want;
    if (in.aiToggle) want = SRC_AI;
    if (want == SRC_AI)
    {
        o.src = in.hasAi ? SRC_AI : SRC_TALK;
        o.why = in.aiToggle ? SW_AI_TOGGLE : (in.hasAi ? SW_AI : SW_AI_NONE);
        return o;
    }
    bool over = false;
    if (in.form == PL_FOLDER || in.form == PL_M3U) { want = SRC_LIB; over = true; }
    else if (in.form == PL_SPOTIFY || in.form == PL_WEB) want = SRC_APP;
    const bool hasList = in.form == PL_FOLDER || in.form == PL_M3U || in.form == PL_SPOTIFY || in.form == PL_WEB;
    if (in.genre == G_TALK && (want == SRC_LIB || want == SRC_APP) && !hasList) { o.src = SRC_TALK; o.why = SW_TALK_STATION; return o; }
    const bool musicGenre = in.genre < 8;
    auto shortFall = [&](uint8_t whyTalk) {
        if (in.shortAi && in.hasAi) { o.src = SRC_AI; o.why = SW_LIB_SHORT_AI; }
        else { o.src = SRC_TALK; o.why = whyTalk; }
    };
    if (want == SRC_LIB)
    {
        if (in.libOff) { shortFall(SW_LIB_OFF); if (o.src == SRC_AI) o.why = SW_LIB_OFF; return o; }
        if (in.scanPending) { o.src = SRC_TALK; o.why = SW_SCAN; return o; }
        const int pool = over ? in.overridePool : in.genrePool;
        const int need = over ? 1 : (in.libMin > 0 ? in.libMin : 1);
        if ((over || musicGenre) && pool >= need)
        {
            o.src = SRC_LIB; o.overridePool = over; o.genreLines = !over && musicGenre; o.introOver = in.talkover && in.genre != G_KC;
            o.why = over ? SW_LIB_PLAYLIST : SW_LIB_GENRE;
            return o;
        }
        shortFall(SW_LIB_SHORT_TALK);
        return o;
    }
    if (want == SRC_APP)
    {
        if (in.appOff || !in.appBound)
        {
            if (!in.libOff && !in.scanPending && musicGenre && in.genrePool >= (in.libMin > 0 ? in.libMin : 1))
            {
                o.src = SRC_LIB; o.genreLines = true; o.introOver = in.talkover && in.genre != G_KC; o.why = SW_APP_OFF_LIB;
            }
            else { o.src = SRC_TALK; o.why = SW_APP_OFF_TALK; }
            return o;
        }
        o.src = SRC_APP; o.why = SW_APP;
        return o;
    }
    o.src = SRC_TALK; o.why = SW_TALK;
    return o;
}

inline uint8_t SourceFromName(const char* s, uint8_t def)
{
    if (!s || !s[0]) return def;
    std::string v = GenreLower(GenreStrip(s));
    if (v == "ai") return SRC_AI;
    if (v == "library" || v == "files" || v == "lib") return SRC_LIB;
    if (v == "service" || v == "app" || v == "spotify") return SRC_APP;
    if (v == "talk" || v == "none") return SRC_TALK;
    return 0xFF;
}

} // namespace radio
