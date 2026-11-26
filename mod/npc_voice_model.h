// =============================================================================================
//  npc_voice_model.h - the NPC voice lines (World\npc\DESIGN.md §4-§6, Notes\62). PURE: no game
//  memory, no audio API. The moment in, a decision out, so npc_harness.cpp runs it as is.
//
//    Library    the runtime table (World\npc\data\npc_runtime.tsv, one line a clip) parsed into
//               fixed arrays: id, manifest + conversation (the game's dialogue system plays it),
//               voice, gender, category, topic, context tags, exchange / turn, length, gesture.
//    Picker     which line fits: category + topic + context tags + the speaker's gender (and the
//               voice the speaker already has); never a line heard in the last hour if any other
//               fits; per-category and global hourly budgets; least-recently-used first.
//    Exchanges  two- or three-turn overheard snippets: an exchange whose voices' genders fit the
//               pair that will act it out (either order).
//    Audio      distance gain and constant-power pan per listener, the two co-op listeners merged
//               into one output, the audible gate (a line is never spent on nobody).
//    Mixer      at most N lines at once, never two loud ones on top of each other, priorities,
//               held under cutscenes / story dialogue / pause, ducked under the radio.
//
//  Two playback routes use the same decisions (DESIGN.md §5):
//    A  the game's own dialogue system (voice.h's calls): new VO PAKs in PATCH.DAT; the engine does
//       3D, lip-sync on converted speakers, subtitles from text.csv. Gain / pan below are then only
//       used for the audible gate and the overlap rule.
//    B  lcudrive's own XAudio2 voices (radio.h's engine): Gain / Pan below drive SetOutputMatrix.
// =============================================================================================
#pragma once
#include <cstdint>
#include <cstring>
#include <cmath>
#include <cstdlib>
#include <cstdio>

namespace npcv {

// ---- the library ------------------------------------------------------------------------------
enum Gender : uint8_t { kMale = 0, kFemale = 1, kAnyGender = 2 };

// Context tags a line may require (all of its tags must hold). Bit positions are the table's
// tag names (kTagName), so the TSV stays readable.
enum Tag : uint32_t
{
    T_MORNING = 1u << 0, T_MIDDAY = 1u << 1, T_EVENING = 1u << 2, T_NIGHT = 1u << 3,
    T_RAIN = 1u << 4, T_DRY = 1u << 5,
    T_CITY = 1u << 6, T_COUNTRY = 1u << 7, T_BEACH = 1u << 8, T_PARK = 1u << 9, T_DOWNTOWN = 1u << 10,
    T_PORTATLAS = 1u << 11, T_WOODS = 1u << 12, T_CASTLE = 1u << 13, T_STATION = 1u << 14, T_SKYRAIL = 1u << 15,
    T_DINO = 1u << 16, T_HARBOUR = 1u << 17, T_LANDMARK = 1u << 18,
    T_ONFOOT = 1u << 19, T_DRIVING = 1u << 20, T_WANTED = 1u << 21, T_DISGUISE = 1u << 22,
    T_COOP = 1u << 23, T_AFTERCHASE = 1u << 24, T_COLD = 1u << 25,
};
static const int kTagCount = 26;
static const char* const kTagName[kTagCount] = {
    "morning", "midday", "evening", "night", "rain", "dry",
    "city", "country", "beach", "park", "downtown", "portatlas", "woods", "castle", "station", "skyrail",
    "dino", "harbour", "landmark", "onfoot", "driving", "wanted", "disguise", "coop", "afterchase", "cold",
};

static const int kMaxLines = 2048, kMaxStr = 48, kMaxCats = 128, kMaxVoices = 32, kMaxExchanges = 512, kMaxMan = 256;

struct Line
{
    char     id[kMaxStr];
    char     manifest[kMaxStr];
    char     conv[kMaxStr];
    uint16_t man;            // index into Library::man (the manifest = one VO pak the game loads)
    uint8_t  voice;          // index into Library::voice
    uint8_t  gender;         // Gender
    uint8_t  cat;            // index into Library::cat
    uint8_t  topic;          // index into Library::cat as well (topics share the name table)
    uint32_t tags;           // all must hold
    int16_t  exchange;       // -1 = a single bark; else index into Library::ex
    uint8_t  turn;           // 0.. within its exchange
    uint16_t ms;             // length (estimated until the audio exists)
    char     gesture[24];    // anim hint for the speaker (life.h names: Talk, Talk_Angry, point, Wave ...)
    float    weight;
};

struct Exchange { int16_t first; uint8_t turns; uint8_t gA, gB; uint8_t voiceA, voiceB; uint8_t topic; uint32_t tags; };

struct Library
{
    Line     line[kMaxLines];
    int      nLines = 0;
    char     cat[kMaxCats][kMaxStr];
    int      nCats = 0;
    char     voice[kMaxVoices][kMaxStr];
    uint8_t  voiceGender[kMaxVoices];
    int      nVoices = 0;
    char     man[kMaxMan][kMaxStr];
    uint8_t  manCat[kMaxMan];
    int      manLines[kMaxMan];
    int      nMan = 0;
    Exchange ex[kMaxExchanges];
    int      nEx = 0;
    int      refused = 0;              // lines the parser refused
    char     firstRefusal[96] = {};
};

inline bool SameI(const char* a, const char* b)
{
    for (; *a && *b; ++a, ++b) { char x = *a, y = *b; if (x >= 'A' && x <= 'Z') x += 32; if (y >= 'A' && y <= 'Z') y += 32; if (x != y) return false; }
    return *a == *b;
}
inline void Copy(char* d, int cap, const char* s, int n)
{
    int k = 0; for (; k < n && k < cap - 1 && s[k]; ++k) d[k] = s[k]; d[k] = 0;
}
inline int Intern(char (*names)[kMaxStr], int& n, int cap, const char* s, int len)
{
    char tmp[kMaxStr]; Copy(tmp, kMaxStr, s, len);
    for (int i = 0; i < n; ++i) if (SameI(names[i], tmp)) return i;
    if (n >= cap) return -1;
    Copy(names[n], kMaxStr, tmp, kMaxStr); return n++;
}
inline int FindName(const char (*names)[kMaxStr], int n, const char* s)
{
    for (int i = 0; i < n; ++i) if (SameI(names[i], s)) return i;
    return -1;
}
inline uint32_t TagBit(const char* s, int len)
{
    char t[24]; Copy(t, sizeof t, s, len);
    for (int i = 0; i < kTagCount; ++i) if (SameI(kTagName[i], t)) return 1u << i;
    return 0;
}

// The runtime TSV (npc_runtime.tsv; header line first, '#' comments):
//   id  manifest  conversation  voice  gender(m|f)  category  topic  tags(a+b+c|-)  exchange(-|name)  turn  ms  gesture  weight  text
// The text (the subtitle) is not kept here: the game shows its own text.csv row.
inline bool ParseTsv(Library& L, const char* buf, size_t size)
{
    L.nLines = L.nCats = L.nVoices = L.nEx = L.nMan = 0; L.refused = 0; L.firstRefusal[0] = 0;
    char exName[kMaxExchanges][kMaxStr]; int nExName = 0;
    size_t i = 0; bool header = true;
    while (i < size)
    {
        size_t e = i; while (e < size && buf[e] != '\n') ++e;
        size_t len = e - i; if (len && buf[i + len - 1] == '\r') --len;
        const char* s = buf + i; i = e + 1;
        if (!len || s[0] == '#') continue;
        if (header) { header = false; if (len >= 2 && (s[0] == 'i' || s[0] == 'I') && (s[1] == 'd' || s[1] == 'D')) continue; }
        const char* f[14]; int fl[14]; int nf = 0; size_t a = 0;
        for (size_t k = 0; k <= len && nf < 14; ++k)
            if (k == len || s[k] == '\t') { f[nf] = s + a; fl[nf] = static_cast<int>(k - a); ++nf; a = k + 1; }
        auto refuse = [&](const char* why) {
            if (!L.refused) { char idb[32]; Copy(idb, sizeof idb, f[0], fl[0]); snprintf(L.firstRefusal, sizeof L.firstRefusal, "%s: %s", idb, why); }
            ++L.refused; };
        if (nf < 13) { refuse("too few columns"); continue; }
        if (L.nLines >= kMaxLines) { refuse("table full"); continue; }
        Line& ln = L.line[L.nLines];
        memset(&ln, 0, sizeof ln);
        Copy(ln.id, kMaxStr, f[0], fl[0]); Copy(ln.manifest, kMaxStr, f[1], fl[1]); Copy(ln.conv, kMaxStr, f[2], fl[2]);
        if (!ln.id[0] || !ln.manifest[0] || !ln.conv[0]) { refuse("empty id / manifest / conversation"); continue; }
        const int v = Intern(L.voice, L.nVoices, kMaxVoices, f[3], fl[3]);
        if (v < 0) { refuse("too many voices"); continue; }
        const int mi = Intern(L.man, L.nMan, kMaxMan, f[1], fl[1]);
        if (mi < 0) { refuse("too many manifests"); continue; }
        ln.man = static_cast<uint16_t>(mi);
        const char g = fl[4] ? f[4][0] : '?';
        if (g != 'm' && g != 'f' && g != 'M' && g != 'F') { refuse("gender must be m or f"); continue; }
        ln.voice = static_cast<uint8_t>(v); ln.gender = (g == 'f' || g == 'F') ? kFemale : kMale;
        L.voiceGender[v] = ln.gender;
        const int c = Intern(L.cat, L.nCats, kMaxCats, f[5], fl[5]);
        const int t = (fl[6] && !(fl[6] == 1 && f[6][0] == '-')) ? Intern(L.cat, L.nCats, kMaxCats, f[6], fl[6]) : c;
        if (c < 0 || t < 0) { refuse("too many categories"); continue; }
        ln.cat = static_cast<uint8_t>(c); ln.topic = static_cast<uint8_t>(t);
        bool badTag = false; ln.tags = 0;
        if (!(fl[7] == 1 && f[7][0] == '-'))
            for (int k = 0, st = 0; k <= fl[7]; ++k)
                if (k == fl[7] || f[7][k] == '+') { if (k > st) { uint32_t b = TagBit(f[7] + st, k - st); if (!b) badTag = true; ln.tags |= b; } st = k + 1; }
        if (badTag) { refuse("unknown tag"); continue; }
        ln.exchange = -1;
        if (fl[8] && !(fl[8] == 1 && f[8][0] == '-'))
        {
            const int x = Intern(exName, nExName, kMaxExchanges, f[8], fl[8]);
            if (x < 0) { refuse("too many exchanges"); continue; }
            ln.exchange = static_cast<int16_t>(x);
        }
        ln.turn = static_cast<uint8_t>(atoi(f[9]));
        const int ms = atoi(f[10]); ln.ms = static_cast<uint16_t>(ms < 200 ? 200 : ms > 20000 ? 20000 : ms);
        Copy(ln.gesture, sizeof ln.gesture, f[11], fl[11]);
        ln.weight = static_cast<float>(atof(f[12])); if (!(ln.weight > 0.0f)) ln.weight = 1.0f;
        L.manCat[mi] = ln.cat;
        ++L.nLines;
    }
    // exchanges: their turns must be contiguous, 0..n-1, two voices alternating (a third turn may be A again)
    for (int x = 0; x < nExName; ++x)
    {
        int first = -1, turns = 0; bool ok = true;
        for (int k = 0; k < L.nLines; ++k)
            if (L.line[k].exchange == x)
            {
                if (first < 0) first = k;
                if (k != first + turns || L.line[k].turn != turns) ok = false;
                ++turns;
            }
        if (first < 0) continue;
        uint8_t vA = L.line[first].voice, vB = turns > 1 ? L.line[first + 1].voice : vA;
        for (int k = 0; k < turns && ok; ++k) if (L.line[first + k].voice != ((k & 1) ? vB : vA)) ok = false;
        if (turns < 2 || vA == vB) ok = false;
        if (!ok || L.nEx >= kMaxExchanges)
        {   // unusable: the lines stay as single barks? no - they would make no sense alone; drop them
            for (int k = 0; k < L.nLines; ++k) if (L.line[k].exchange == x) L.line[k].weight = 0.0f;
            if (!L.refused) snprintf(L.firstRefusal, sizeof L.firstRefusal, "exchange %s: turns / voices do not alternate", exName[x]);
            ++L.refused; continue;
        }
        Exchange& E = L.ex[L.nEx];
        E.first = static_cast<int16_t>(first); E.turns = static_cast<uint8_t>(turns);
        E.voiceA = vA; E.voiceB = vB; E.gA = L.voiceGender[vA]; E.gB = L.voiceGender[vB];
        E.topic = L.line[first].topic; E.tags = L.line[first].tags;
        for (int k = 0; k < turns; ++k) L.line[first + k].exchange = static_cast<int16_t>(L.nEx);
        ++L.nEx;
    }
    for (int k = 0; k < L.nLines; ++k)        // lines of exchanges that were dropped
        if (L.line[k].exchange >= L.nEx || (L.line[k].exchange >= 0 && L.line[k].weight == 0.0f)) L.line[k].exchange = -2;
    for (int q = 0; q < L.nMan; ++q) L.manLines[q] = 0;
    for (int k = 0; k < L.nLines; ++k) if (L.line[k].exchange != -2) ++L.manLines[L.line[k].man];
    return L.nLines > 0;
}

// ---- repetition memory and budgets ------------------------------------------------------------------
struct Budget
{
    float lineGapS      = 3600.0f;  // a line is not heard again within the hour while others fit
    float lineMinGapS   = 900.0f;   // ...and never within 15 min (then: silence instead)
    float exchangeGapS  = 3600.0f;  // the same for exchanges
    int   perHour       = 150;      // all NPC lines, both players together
    int   perMinute     = 8;
    float catGapS       = 6.0f;     // two lines of one category this close only if they are an exchange
    int   catPerHour[kMaxCats];     // 0 = no cap; set from the table's defaults (SetCatCap)
    Budget() { for (int& c : catPerHour) c = 0; }
};

struct Memory
{
    float    lastLine[kMaxLines];
    float    lastEx[kMaxExchanges];
    float    lastCat[kMaxCats];
    float    stamps[256];            // ring of play times (the hourly budget)
    uint8_t  stampCat[256];
    int      head = 0, count = 0;
    void Reset() { for (float& t : lastLine) t = -1e9f; for (float& t : lastEx) t = -1e9f; for (float& t : lastCat) t = -1e9f; head = count = 0; }
    Memory() { Reset(); }
};

inline int PlayedSince(const Memory& m, float now, float window, int cat = -1)
{
    int n = 0;
    for (int k = 0; k < m.count; ++k)
    {
        const int i = (m.head - 1 - k + 256) & 255;
        if (now - m.stamps[i] > window) break;
        if (cat < 0 || m.stampCat[i] == cat) ++n;
    }
    return n;
}

inline bool BudgetAllows(const Budget& b, const Memory& m, int cat, float now, bool partOfExchange)
{
    if (PlayedSince(m, now, 3600.0f) >= b.perHour) return false;
    if (PlayedSince(m, now, 60.0f) >= b.perMinute) return false;
    if (cat >= 0 && cat < kMaxCats)
    {
        if (b.catPerHour[cat] > 0 && PlayedSince(m, now, 3600.0f, cat) >= b.catPerHour[cat]) return false;
        if (!partOfExchange && now - m.lastCat[cat] < b.catGapS) return false;
    }
    return true;
}

// Each category's hourly cap defaults to its number of single lines (an hour of one kind of bark can
// never wrap round); categories with exchanges get their exchange count. 0 lines = no cap.
inline void SetDefaultCaps(const Library& L, Budget& b)
{
    int n[kMaxCats] = {};
    for (int k = 0; k < L.nLines; ++k) if (L.line[k].exchange == -1) ++n[L.line[k].cat];
    for (int x = 0; x < L.nEx; ++x) ++n[L.line[L.ex[x].first].cat];
    for (int c = 0; c < kMaxCats; ++c) b.catPerHour[c] = n[c] ? (n[c] < 4 ? 4 : n[c]) : 0;
}

inline void Spent(Memory& m, const Library& L, int line, float now)
{
    if (line < 0 || line >= L.nLines) return;
    m.lastLine[line] = now;
    m.lastCat[L.line[line].cat] = now;
    if (L.line[line].exchange >= 0) m.lastEx[L.line[line].exchange] = now;
    m.stamps[m.head] = now; m.stampCat[m.head] = L.line[line].cat;
    m.head = (m.head + 1) & 255; if (m.count < 256) ++m.count;
}

// ---- picking ----------------------------------------------------------------------------------------
struct Query
{
    int      cat = -1;          // required
    int      topic = -1;        // -1 = any topic of the category
    uint8_t  gender = kAnyGender;
    int      voice = -1;        // the voice this speaker already has (-1 = none yet): preferred, and
    bool     voiceOnly = false; // ...required when true (a glaring NPC keeps their voice)
    uint32_t have = 0;          // the context tags that hold now
    const uint8_t* loaded = nullptr;   // per manifest: 1 = its pak is loaded (nullptr = all are)
};

inline uint32_t Hash(uint32_t x) { x ^= x >> 16; x *= 0x7FEB352Du; x ^= x >> 15; x *= 0x846CA68Bu; x ^= x >> 16; return x; }

// A single bark. roll: a random number (the harness passes its own). -1 = nothing fits / budget spent.
// Lines not heard for lineGapS are "fresh"; only when none is fresh may a line heard lineMinGapS..lineGapS
// ago come back (likelier the longer ago). Never sooner.
inline float LineWeight(const Library& L, const Memory& m, const Budget& b, const Query& q, int k, float now, int& bucket)
{
    bucket = -1;
    const Line& ln = L.line[k];
    if (ln.exchange != -1 || ln.cat != q.cat || ln.weight <= 0.0f) return 0.0f;
    if (q.loaded && !q.loaded[ln.man]) return 0.0f;
    if (q.topic >= 0 && ln.topic != q.topic) return 0.0f;
    if (q.gender != kAnyGender && ln.gender != q.gender) return 0.0f;
    if (q.voiceOnly && q.voice >= 0 && ln.voice != q.voice) return 0.0f;
    if ((ln.tags & q.have) != ln.tags) return 0.0f;
    const float age = now - m.lastLine[k];
    bucket = age >= b.lineGapS ? 0 : age >= b.lineMinGapS ? 1 : -1;
    if (bucket < 0) return 0.0f;
    float w = ln.weight * (q.voice >= 0 && ln.voice == q.voice ? 4.0f : 1.0f);
    if (bucket == 1) w *= age / b.lineGapS;
    return w;
}

inline int PickLine(const Library& L, const Memory& m, const Budget& b, const Query& q, float now, uint32_t roll)
{
    if (q.cat < 0 || !BudgetAllows(b, m, q.cat, now, false)) return -1;
    float total[2] = { 0.0f, 0.0f };
    for (int k = 0; k < L.nLines; ++k) { int bk; const float w = LineWeight(L, m, b, q, k, now, bk); if (bk >= 0) total[bk] += w; }
    const int want = total[0] > 0.0f ? 0 : total[1] > 0.0f ? 1 : -1;
    if (want < 0) return -1;
    float r = (Hash(roll) & 0xFFFFFF) / 16777216.0f * total[want];
    int last = -1;
    for (int k = 0; k < L.nLines; ++k)
    {
        int bk; const float w = LineWeight(L, m, b, q, k, now, bk);
        if (bk != want || w <= 0.0f) continue;
        last = k;
        if (r < w) return k;
        r -= w;
    }
    return last;
}

// An overheard exchange for a pair (gender of the one who starts, of the other). swapped = the pair
// acts it the other way round (B's lines by the first person). -1 = none.
struct ExPick { int ex = -1; bool swapped = false; };
inline ExPick PickExchange(const Library& L, const Memory& m, const Budget& b, int topic, uint8_t g1, uint8_t g2,
                           uint32_t have, float now, uint32_t roll, int topicCat = -1, const uint8_t* loaded = nullptr)
{
    ExPick out;
    if (!BudgetAllows(b, m, topicCat, now, true)) return out;
    float total = 0.0f, totalStale = 0.0f;
    for (int pass = 0; pass < 2; ++pass)
    {
        float r = pass ? (Hash(roll) & 0xFFFFFF) / 16777216.0f * (total > 0.0f ? total : totalStale) : 0.0f;
        const bool useFresh = total > 0.0f;
        for (int x = 0; x < L.nEx; ++x)
        {
            const Exchange& E = L.ex[x];
            if (topic >= 0 && E.topic != topic) continue;
            if ((E.tags & have) != E.tags) continue;
            if (loaded && !loaded[L.line[E.first].man]) continue;
            const bool fwd = (g1 == kAnyGender || E.gA == g1) && (g2 == kAnyGender || E.gB == g2);
            const bool rev = (g1 == kAnyGender || E.gB == g1) && (g2 == kAnyGender || E.gA == g2);
            if (!fwd && !rev) continue;
            const float age = now - m.lastEx[x];
            if (age < b.lineMinGapS) continue;
            const bool fresh = age >= b.exchangeGapS;
            const float w = L.line[E.first].weight * (fresh ? 1.0f : age / b.exchangeGapS);
            if (!pass) { if (fresh) total += w; else totalStale += w; continue; }
            if (fresh != useFresh) continue;
            out.ex = x; out.swapped = !fwd;
            if (r < w) return out;
            r -= w;
        }
        if (!pass && total <= 0.0f && totalStale <= 0.0f) return out;
    }
    return out;
}

// ---- which manifests to keep loaded ------------------------------------------------------------------
// The game loads a manifest's pak asynchronously (~1-8 s, voice.h waits up to 8). lcudrive keeps a few
// loaded - the ones the moment wants (demand per category, from the context: rain -> weather_rain, a
// plaza at noon -> tourist / busker / smalltalk, any time -> the reactions) whose lines are freshest - and
// swaps one at a time, never one with a line playing. As the game itself does with smalltalk_NNN.
struct LoadPlan { int load = -1; int unload = -1; };
inline float ManScore(const Library& L, const Memory& m, const Budget& b, const float* demand, int mi, float now)
{
    const float d = demand[L.manCat[mi]];
    if (d <= 0.0f || L.manLines[mi] <= 0) return 0.0f;
    int fresh = 0;
    for (int k = 0; k < L.nLines; ++k) if (L.line[k].man == mi && L.line[k].exchange != -2 && now - m.lastLine[k] >= b.lineGapS) ++fresh;
    return d * (0.1f + static_cast<float>(fresh) / L.manLines[mi]);
}
// Coverage first: a wanted category with nothing loaded counts triple, and each chunk a category already
// has loaded divides the next one's score - so the reactions always have a chunk in, and one busy
// category (say smalltalk) cannot crowd out the rest.
inline LoadPlan PlanLoads(const Library& L, const Memory& m, const Budget& b, const float* demand, const uint8_t* loaded,
                          const uint8_t* busy, int maxLoaded, float now, const float* loadedAt = nullptr, float minStayS = 90.0f)
{
    LoadPlan p; int nLoaded = 0;
    int have[kMaxCats] = {};
    for (int i = 0; i < L.nMan; ++i) if (loaded[i]) ++have[L.manCat[i]];
    float best = 0.0f, worst = 1e9f; int bestI = -1, worstI = -1;
    for (int i = 0; i < L.nMan; ++i)
    {
        const int c = L.manCat[i];
        float sc = ManScore(L, m, b, demand, i, now);
        if (loaded[i])
        {
            ++nLoaded;
            sc /= static_cast<float>(have[c]);                     // its share of the category's slots
            const bool settled = !loadedAt || now - loadedAt[i] >= minStayS || sc <= 0.0f;   // no thrashing
            if (!(busy && busy[i]) && settled && sc < worst) { worst = sc; worstI = i; }
        }
        else
        {
            sc *= have[c] ? 1.0f / (1.0f + have[c]) : 3.0f;
            if (sc > best) { best = sc; bestI = i; }
        }
    }
    if (worstI >= 0 && worst <= 0.0f) { p.unload = worstI; return p; }          // not wanted now: let it go
    if (bestI < 0) return p;
    if (nLoaded < maxLoaded) { p.load = bestI; return p; }
    if (worstI >= 0 && best > worst * 2.0f + 0.1f) { p.unload = worstI; p.load = bestI; }    // swap, with hysteresis
    return p;
}

// ---- positional audio --------------------------------------------------------------------------------
struct Listener { bool on = false; float pos[3] = { 0, 0, 0 }; float yaw = 0.0f; };   // yaw: the camera's, radians, atan2(dx, dz) convention
struct AudioParams
{
    float nearU   = 2.5f;     // full level within this
    float farU    = 24.0f;    // silent beyond this
    float rolloff = 1.0f;     // inverse-distance exponent between near and far
    float gateDb  = -24.0f;   // a line is only spent if some listener hears it at least this loud
    float panWidth = 0.8f;    // 1 = hard left/right possible
};

inline float DistGain(const AudioParams& a, float d)
{
    if (!(d >= 0.0f)) return 0.0f;
    if (d <= a.nearU) return 1.0f;
    if (d >= a.farU) return 0.0f;
    float g = std::pow(a.nearU / d, a.rolloff);
    const float x = (d - a.nearU) / (a.farU - a.nearU);           // a soft window over the last third
    if (x > 0.66f) { const float t = (x - 0.66f) / 0.34f; g *= 1.0f - t * t * (3.0f - 2.0f * t); }
    return g;
}

// -1 left .. +1 right, for a source at p heard by l (facing yaw; +x is to the right at yaw 0, facing +z).
inline float PanOf(const AudioParams& a, const Listener& l, const float p[3])
{
    const float dx = p[0] - l.pos[0], dz = p[2] - l.pos[2];
    const float h = std::sqrt(dx * dx + dz * dz);
    if (h < 0.05f) return 0.0f;
    const float ang = std::atan2(dx, dz) - l.yaw;                 // 0 = straight ahead
    float pan = std::sin(ang) * a.panWidth;
    const float nearFade = h < a.nearU ? h / a.nearU : 1.0f;      // right on top of you: centred
    pan *= nearFade;
    return pan < -1.0f ? -1.0f : pan > 1.0f ? 1.0f : pan;
}

struct Out { float gain = 0.0f; float pan = 0.0f; float left = 0.0f, right = 0.0f; int nearest = -1; bool audible = false; };

// Two listeners (co-op on one screen / one pair of speakers): the line is as loud as the nearer
// listener hears it (never doubled), its pan is the loudness-weighted mean, narrowed when the two
// hear it on opposite sides (a split screen must not throw a voice hard to one ear).
inline Out Mix2(const AudioParams& a, const Listener* ls, int n, const float p[3])
{
    Out o; float g[2] = { 0, 0 }, pn[2] = { 0, 0 }; float sw = 0.0f;
    for (int i = 0; i < n && i < 2; ++i)
    {
        if (!ls[i].on) continue;
        const float dx = p[0] - ls[i].pos[0], dy = p[1] - ls[i].pos[1], dz = p[2] - ls[i].pos[2];
        g[i] = DistGain(a, std::sqrt(dx * dx + dy * dy + dz * dz));
        pn[i] = PanOf(a, ls[i], p);
        if (g[i] > o.gain) { o.gain = g[i]; o.nearest = i; }
        sw += g[i] * g[i];
    }
    if (sw > 0.0f)
    {
        o.pan = (g[0] * g[0] * pn[0] + g[1] * g[1] * pn[1]) / sw;
        if (g[0] > 0.0f && g[1] > 0.0f && pn[0] * pn[1] < 0.0f)
        {
            const float lesser = (g[0] < g[1] ? g[0] * g[0] : g[1] * g[1]) / sw;   // 0..0.5
            o.pan *= 1.0f - 2.0f * lesser;                                      // equal and opposite -> centre
        }
    }
    const float th = (o.pan + 1.0f) * 0.78539816f;                // constant power
    o.left = o.gain * std::cos(th); o.right = o.gain * std::sin(th);
    o.audible = o.gain > 0.0f && 20.0f * std::log10(o.gain) >= a.gateDb;
    return o;
}

// ---- the mixer: what may start now ----------------------------------------------------------------------
enum Prio : uint8_t { P_AMBIENT = 0, P_CONVO = 1, P_REACT = 2, P_DIRECT = 3 };   // DIRECT: said to a player right by them
struct Playing { bool on = false; int line = -1; float until = 0.0f; float gain = 0.0f; uint8_t prio = 0; float pos[3] = { 0, 0, 0 }; uint32_t owner = 0; };

struct MixParams
{
    int   maxAtOnce   = 2;
    float overlapDb   = 6.0f;    // a second line must be this much quieter than the one playing...
    float minApartU   = 6.0f;    // ...and this far from it (else it waits)
    float radioDuck   = 0.5f;    // x gain while the radio plays (B route) and only lines this near:
    float radioNearU  = 10.0f;
};
struct Gates { bool paused = false, cutscene = false, storyTalk = false, radio = false, loading = false; };

struct Mixer { Playing slot[4]; };

enum Verdict : uint8_t { V_PLAY, V_WAIT, V_DROP };
inline const char* VerdictName(int v) { return v == V_PLAY ? "play" : v == V_WAIT ? "wait" : "drop"; }

inline void Expire(Mixer& m, float now) { for (Playing& p : m.slot) if (p.on && now >= p.until) p.on = false; }
inline int Active(const Mixer& m) { int n = 0; for (const Playing& p : m.slot) n += p.on; return n; }

// May a line of this priority, at this gain and place, start now? (gain: Mix2's, before the radio duck)
inline Verdict MayStart(const Mixer& m, const MixParams& mp, const Gates& g, uint8_t prio, float gain, const float pos[3],
                        float nearestU, bool continuesExchange)
{
    if (g.paused || g.cutscene || g.loading) return prio >= P_CONVO ? V_WAIT : V_DROP;
    if (g.storyTalk) return V_DROP;                                  // the game's own dialogue wins, always
    if (g.radio && nearestU > mp.radioNearU && prio < P_DIRECT) return V_DROP;
    int n = 0; float loudest = 0.0f; bool tooNear = false; uint8_t topPrio = 0;
    for (const Playing& p : m.slot)
    {
        if (!p.on) continue;
        ++n; if (p.gain > loudest) loudest = p.gain; if (p.prio > topPrio) topPrio = p.prio;
        const float dx = p.pos[0] - pos[0], dz = p.pos[2] - pos[2];
        if (dx * dx + dz * dz < mp.minApartU * mp.minApartU) tooNear = true;
    }
    if (n == 0) return V_PLAY;
    if (continuesExchange) return n < mp.maxAtOnce ? V_PLAY : V_WAIT; // the next turn of a talk already playing
    if (n >= mp.maxAtOnce) return prio > topPrio ? V_WAIT : (prio >= P_CONVO ? V_WAIT : V_DROP);
    if (tooNear) return prio >= P_CONVO ? V_WAIT : V_DROP;
    const float quieterBy = gain > 0.0f && loudest > 0.0f ? 20.0f * std::log10(loudest / gain) : 99.0f;
    if (quieterBy >= mp.overlapDb) return V_PLAY;                   // a murmur far behind a near voice is fine
    return prio >= P_CONVO ? V_WAIT : V_DROP;
}

inline bool Start(Mixer& m, int line, float now, float seconds, float gain, uint8_t prio, const float pos[3], uint32_t owner)
{
    for (Playing& p : m.slot)
        if (!p.on)
        {
            p.on = true; p.line = line; p.until = now + seconds; p.gain = gain; p.prio = prio; p.owner = owner;
            for (int k = 0; k < 3; ++k) p.pos[k] = pos[k];
            return true;
        }
    return false;
}

// Gain a playing line gets this frame (route B), with fades handled by the caller.
inline float LiveGain(const MixParams& mp, const Gates& g, float gain)
{
    if (g.paused || g.cutscene || g.loading) return 0.0f;
    if (g.storyTalk) return gain * 0.25f;
    if (g.radio) return gain * mp.radioDuck;
    return gain;
}

// ---- voices stick to people ------------------------------------------------------------------------------
// A person keeps the voice they first spoke with for as long as we know them (key = a stable id the
// caller gives: the krawlie / character pointer, hashed). 64 entries, oldest replaced.
struct VoiceBook { uint64_t key[64]; int8_t voice[64]; float seen[64]; VoiceBook() { for (int i = 0; i < 64; ++i) { key[i] = 0; voice[i] = -1; seen[i] = -1e9f; } } };
inline int VoiceOf(const VoiceBook& vb, uint64_t key) { for (int i = 0; i < 64; ++i) if (vb.key[i] == key && key) return vb.voice[i]; return -1; }
inline void Bind(VoiceBook& vb, uint64_t key, int voice, float now)
{
    int slot = 0;
    for (int i = 0; i < 64; ++i) { if (vb.key[i] == key) { slot = i; break; } if (vb.seen[i] < vb.seen[slot]) slot = i; }
    vb.key[slot] = key; vb.voice[slot] = static_cast<int8_t>(voice); vb.seen[slot] = now;
}

}  // namespace npcv
