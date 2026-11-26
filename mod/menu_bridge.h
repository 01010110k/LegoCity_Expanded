// =============================================================================================
//  menu_bridge.h - how a Mod Options change reaches lcudrive.
//
//  THE RULE: the ini stays the one source of truth. The menu never writes a Config itself.
//    game thread (the menu)   reads the LIVE values from g_cfgBuf[g_cfgIdx] (as every hook does),
//                             keeps a draft of the player's changes, and posts them as Edits with a
//                             generation number (Post: a memcpy under an SRW lock - no I/O, no Sleep,
//                             no Beep). It sets the existing session requests directly (atomics consumed
//                             on the game thread): wd::g_request (wanted level), ap::g_recall (airliner).
//    hotkey thread (15 ms)    HotkeyTick(): when a flush is due (FlushDue) it drains the edits, writes
//                             lcudrive.ini (menu_ini.h: comments kept, atomic replace); the caller then
//                             runs IniChanged() (to swallow its own write), ReloadConfig() - the same path
//                             as a hand edit, so ApplyRadioSettings, cc::Apply, the [plane]/[airplane]
//                             list generations and the car re-derive all happen exactly as today - and
//                             only THEN Published(gen): the draft gives way to the reloaded value with no
//                             frame of the old one. It is also the only thread that runs ] / [ (g_modeReq),
//                             Ctrl+Alt+R (g_reloadReq) and Unhook (g_unhookReq: Unhook Sleeps and drains
//                             g_inFlight - never on the game thread - and only once the game runs again,
//                             see UnhookGate).
//    radio thread             untouched: it sees new settings through rd::Configure / g_cfgVer, which
//                             only ReloadConfig (hotkey thread) calls.
//  At most ONE publish (ReloadConfig) per hotkey-loop pass: the game thread may hold a reference to
//  the previous buffer for the length of a hook, and a second swap in the same pass would rewrite it.
//  When a save is due: Back out of a page (saveReq), the game unpaused (Start clears the menu stack
//  WITHOUT calling any onExit, 0x14054FF70), or 1 s with no new change while paused. Reset to Defaults
//  saves at once.
//  The draft display rule (ShowDraft): a draft is on screen iff its gen > max(doneGen, failGen).
// =============================================================================================
#pragma once
#include "menu_ini.h"
#include "menu_table.h"
#include <cmath>

namespace mbr {

// ---- values <-> the ini's spelling ---------------------------------------------------------------
inline int StepDecimals(float step)
{
    for (int d = 0; d < 4; ++d)
    {
        const float s = step * std::pow(10.0f, static_cast<float>(d));
        if (std::fabs(s - std::round(s)) < 1e-4f) return d;
    }
    return 3;
}

inline float Snap(const mt::Item& it, float v)
{
    if (!(v == v)) v = it.lo;
    if (v < it.lo) v = it.lo;
    if (v > it.hi) v = it.hi;
    const float k = std::round((v - it.lo) / it.step);
    float s = it.lo + k * it.step;
    if (s > it.hi) s = it.hi;
    return s;
}

// a slider value as the ini spells it: snapped, the step's decimals with trailing zeros dropped but one kept
// ("0.75", "0.8", "1.0", "6.0", "17") - the shipped file's own style
inline void SliderIni(const mt::Item& it, float v, char* out, size_t cap)
{
    _snprintf_s(out, cap, _TRUNCATE, "%.*f", StepDecimals(it.step), Snap(it, v));
    if (const char* dot = strchr(out, '.'))
    {
        size_t n = strlen(out);
        const size_t keep = static_cast<size_t>(dot - out) + 2;      // "x.y"
        while (n > keep && out[n - 1] == '0') out[--n] = 0;
    }
    if (out[0] == '-' && strspn(out + 1, "0.") == strlen(out + 1)) memmove(out, out + 1, strlen(out));   // no "-0.0"
}

inline const void* Field(const mt::Item& it, const Config& c) { return reinterpret_cast<const char*>(&c) + it.cfgOff; }

inline int PortValue(const char* ini)        // config.h's [coop] pad_p* rule
{
    if (ini[0] >= '0' && ini[0] <= '3' && !ini[1]) return ini[0] - '0';
    if (!_stricmp(ini, "none")) return 4;
    return -1;
}

inline bool SameList(const char* a, const char* b)   // "brick-talk, bright-lights" == "brick-talk,bright-lights"
{
    for (;;)
    {
        while (*a == ' ' || *a == '\t') ++a;
        while (*b == ' ' || *b == '\t') ++b;
        if (!*a || !*b) return !*a && !*b;
        if (tolower(static_cast<unsigned char>(*a)) != tolower(static_cast<unsigned char>(*b))) return false;
        ++a; ++b;
    }
}

// Does a Config member of this item's field type hold this ini value? (toggles: choice 0 = "0", 1 = "1")
inline bool FieldMatches(const mt::Item& it, const void* f, const char* ini)
{
    switch (it.field)
    {
    case mt::fInt:     return *static_cast<const int*>(f) == atoi(ini) || (it.kind == mt::kToggle && (*static_cast<const int*>(f) != 0) == (atoi(ini) != 0));
    case mt::fFloat:   return std::fabs(*static_cast<const float*>(f) - static_cast<float>(atof(ini))) < 1e-4f ||
                              (it.kind == mt::kToggle && (*static_cast<const float*>(f) > 0.5f) == (atof(ini) > 0.5));
    case mt::fStr:     return SameList(static_cast<const char*>(f), ini);
    case mt::fPadBits: return static_cast<uint32_t>(*static_cast<const int*>(f)) == pad::Parse(ini);
    case mt::fPort:    return *static_cast<const int*>(f) == PortValue(ini);
    default:           return false;
    }
}
inline bool ChoiceMatches(const mt::Item& it, const char* ini, const Config& c) { return FieldMatches(it, Field(it, c), ini); }

// A choice that writes two keys (ini + ini2, e.g. Station Buttons pad_next + pad_prev) is the live one only
// when BOTH members hold it; a half-set pair (one key edited by hand) shows as Custom.
inline bool ChoiceLive(const mt::Item& it, const mt::Choice& ch, const Config& c)
{
    if (!ChoiceMatches(it, ch.ini, c)) return false;
    if (!ch.ini2 || it.ini2Off == 0xFFFF) return true;
    return FieldMatches(it, reinterpret_cast<const char*>(&c) + it.ini2Off, ch.ini2);
}

// The live value: toggles 0/1, choices the index (-1 = something the menu does not list: show it as
// "Custom" and leave it alone unless the player picks a choice), sliders the value.
inline int CurrentChoice(const mt::Item& it, const Config& c)
{
    if (it.kind == mt::kToggle) return ChoiceMatches(it, "1", c) ? 1 : 0;
    for (int i = 0; i < it.nChoices; ++i) if (ChoiceLive(it, it.choices[i], c)) return i;
    return -1;
}
inline float CurrentSlider(const mt::Item& it, const Config& c)
{
    const void* f = Field(it, c);
    return it.field == mt::fInt ? static_cast<float>(*static_cast<const int*>(f)) : *static_cast<const float*>(f);
}

// The decimals a slider value is shown with: the step's own ("15 s", "6.0 s", "1.50"); a value off the grid
// (hand-edited) gets as many more as it needs, up to 3, so it shows as it is (police_top_speed 17.5 -> "17.5").
inline int ShownDecimals(const mt::Item& it, float v)
{
    int d = StepDecimals(it.step);
    for (; d < 3; ++d)
    {
        const double p = std::pow(10.0, d), x = static_cast<double>(v) * p;
        if (std::fabs(x - std::round(x)) < 1e-3) break;
    }
    return d;
}

// What the menu prints for a slider value (numbers are language-neutral; no text.csv format strings).
// The value as it is - a hand-edited 0.77 shows as 77%, not as the grid point it would snap to.
inline void SliderText(const mt::Item& it, float v, char* out, size_t cap)
{
    if (!(v == v)) v = it.lo;
    const int d = ShownDecimals(it, v);
    switch (it.fmt)
    {
    case mt::fmtPct:    _snprintf_s(out, cap, _TRUNCATE, "%.*f%%", d > 2 ? d - 2 : 0, static_cast<double>(v) * 100.0); break;
    case mt::fmtSec:    _snprintf_s(out, cap, _TRUNCATE, "%.*f s", d, v); break;
    case mt::fmtSigned: _snprintf_s(out, cap, _TRUNCATE, "%+.*f", d < 1 ? 1 : d, v); break;
    default:            _snprintf_s(out, cap, _TRUNCATE, "%.*f", d, v); break;
    }
    if (!strcmp(out, "-0.0")) strcpy_s(out, cap, "0.0");
    if (!strcmp(out, "-0")) strcpy_s(out, cap, "0");
    if (!strcmp(out, "-0%")) strcpy_s(out, cap, "0%");
}

// The edits one menu change makes: the item's key plus its also-keys (a choice's ini2 goes to also[0]).
// choice: toggles 0/1, choices an index; slider: v. Returns the count (0 = nothing to write).
inline int MakeEdits(const mt::Item& it, int choice, float v, mini::Edit* out)
{
    char val[96] = {}, val2[96] = {};
    if (it.kind == mt::kSlider) SliderIni(it, v, val, sizeof(val));
    else if (it.kind == mt::kToggle) strcpy_s(val, choice ? "1" : "0");
    else if (it.kind == mt::kChoice && choice >= 0 && choice < it.nChoices)
    {
        strcpy_s(val, it.choices[choice].ini);
        if (it.choices[choice].ini2) strcpy_s(val2, it.choices[choice].ini2);
    }
    else return 0;
    if (!it.key.sec) return 0;
    int n = 0;
    mini::SetEdit(out[n++], it.key.sec, it.key.key, val);
    for (int a = 0; a < 2; ++a)
        if (it.also[a].sec) mini::SetEdit(out[n++], it.also[a].sec, it.also[a].key, (a == 0 && val2[0]) ? val2 : val);
    return n;
}

// Every menu item's shipped default, as edits (Reset to Defaults). Actions / info have none. The v1-hidden
// "overhaul" ([general] enable) is not on the page, so Reset leaves it alone too.
inline int DefaultEdits(mini::Edit* out, int cap)
{
    int n = 0;
    for (int i = 0; i < mt::kNumItems && n + 3 <= cap; ++i)
    {
        const mt::Item& it = mt::kItems[i];
        if (!it.def || !it.key.sec || it.v1Hidden) continue;
        if (it.kind == mt::kSlider) mini::SetEdit(out[n++], it.key.sec, it.key.key, it.def);   // the shipped spelling
        else if (it.kind == mt::kToggle) n += MakeEdits(it, atoi(it.def) != 0, 0.0f, out + n);
        else for (int c = 0; c < it.nChoices; ++c) if (!strcmp(it.choices[c].ini, it.def)) { n += MakeEdits(it, c, 0.0f, out + n); break; }
    }
    return n;
}

// ---- the mailbox (game thread -> hotkey thread) ------------------------------------------------------
static const int     kQueue = 128;
static SRWLOCK       g_lock = SRWLOCK_INIT;
static mini::Edit    g_q[kQueue];
static int           g_nq = 0;
static LONG          g_qGen = 0;           // the newest gen in the queue (under g_lock)
static DWORD         g_postTick = 0;       // GetTickCount of the newest Post (under g_lock)
static volatile LONG g_saveReq = 0;        // flush now (Back out of a page)
static volatile LONG g_defaultsReq = 0;    // Reset to Defaults (after the confirm)
static volatile LONG g_reloadReq = 0;      // Reload Settings File
static volatile LONG g_unhookReq = 0;      // Switch Off Until Restart (after the confirm)
static volatile LONG g_modeReq = 0;        // +1 = ] (on), -1 = [ (stock); session only
static volatile LONG g_doneGen = 0;        // the newest gen saved AND reloaded (stored after ReloadConfig)
static volatile LONG g_failGen = 0;        // the newest gen of a refused save
static volatile LONG g_failSeq = 0;        // counts refused saves (the page plays NOENTRY when it moves)
// game-thread events the hotkey thread logs (the game thread logs nothing per press)
static volatile LONG g_noteWanted = 0;     // (n + 1) | who << 8 (who: 0 both players, 1 = P1, 2 = P2)
static volatile LONG g_noteAirliner = 0;
static volatile LONG g_noteOpenFail = 0;   // 1 = the menu records are missing and could not be added again

// game thread: queue edits (newest wins per [sec] key, case-insensitive). Bounded work under the lock, no
// I/O. A key beyond the 128th distinct one is dropped (the menu has ~50; its draft then simply reverts).
inline void Post(const mini::Edit* e, int n, LONG gen, DWORD now)
{
    AcquireSRWLockExclusive(&g_lock);
    for (int i = 0; i < n; ++i)
    {
        int k = 0;
        for (; k < g_nq; ++k) if (!_stricmp(g_q[k].sec, e[i].sec) && !_stricmp(g_q[k].key, e[i].key)) break;
        if (k == g_nq && g_nq < kQueue) ++g_nq;
        if (k < g_nq) g_q[k] = e[i];
    }
    if (n > 0)
    {
        if (gen > g_qGen) g_qGen = gen;
        g_postTick = now;
    }
    ReleaseSRWLockExclusive(&g_lock);
}

inline int Drain(mini::Edit* out, int cap, LONG* maxGen)
{
    AcquireSRWLockExclusive(&g_lock);
    const int n = g_nq < cap ? g_nq : cap;
    for (int i = 0; i < n; ++i) out[i] = g_q[i];
    if (maxGen) *maxGen = g_qGen;
    g_nq = 0;
    g_qGen = 0;
    ReleaseSRWLockExclusive(&g_lock);
    return n;
}

inline bool Pending(DWORD* lastPost)
{
    AcquireSRWLockShared(&g_lock);
    const bool p = g_nq > 0;
    if (lastPost) *lastPost = g_postTick;
    ReleaseSRWLockShared(&g_lock);
    return p;
}

// When the hotkey thread saves (pure): Reset to Defaults at once; otherwise only with edits pending, and
// then on Back (saveReq), as soon as the game runs again (the pause byte reads 0), or after 1 s with no
// new change.
inline bool FlushDue(bool pending, bool saveReq, unsigned pauseByte, DWORD msSincePost, bool defaultsReq)
{
    if (defaultsReq) return true;
    if (!pending) return false;
    return saveReq || pauseByte == 0 || msSincePost >= 1000;
}

// The draft display rule (pure).
inline bool ShowDraft(LONG draftGen, LONG doneGen, LONG failGen)
{
    const LONG seen = doneGen > failGen ? doneGen : failGen;
    return draftGen > seen;
}

inline void StoreMax(volatile LONG* p, LONG v)
{
    for (;;)
    {
        const LONG cur = *p;
        if (v <= cur || InterlockedCompareExchange(p, v, cur) == cur) return;
    }
}

// hotkey thread, AFTER ReloadConfig() has swapped the buffer
inline void Published(LONG gen) { StoreMax(&g_doneGen, gen); }

struct TickResult
{
    int  kind;            // 0 nothing, 1 saved (caller: IniChanged, ReloadConfig, Published(gen)), 2 refused
    LONG gen;             // the newest gen in the batch
    int  n;               // edits written
    bool defaults;
    char what[640];       // "[feel] power = 0.8, [feel] grip = 1.2"
    char err[160];
};

// ms since the last Post, never "wrapped": `now` is read before Pending(), so a Post landing in between
// (with the tick moving on) has last > now; that counts as 0 ms, not as ~49 days.
inline DWORD MsSince(DWORD now, DWORD last) { const LONG d = static_cast<LONG>(now - last); return d > 0 ? static_cast<DWORD>(d) : 0; }

// Hotkey thread, once per pass. Never publishes itself; see the header for the caller's order.
// forceSave: a pending Switch Off or Reload Settings File - the edits on screen are written first.
inline TickResult HotkeyTick(const char* iniPath, unsigned pauseByte, DWORD now, bool forceSave = false)
{
    TickResult r{};
    const bool defaults = InterlockedExchange(&g_defaultsReq, 0) != 0;
    const bool save = InterlockedExchange(&g_saveReq, 0) != 0;
    DWORD last = 0;
    const bool pending = Pending(&last);
    if (!FlushDue(pending, save || forceSave, pauseByte, MsSince(now, last), defaults)) return r;
    mini::Edit batch[160];
    LONG gen = 0;
    int n = Drain(batch, kQueue, &gen);
    if (defaults) n = DefaultEdits(batch, 160);        // the queue is dropped: defaults win
    if (n <= 0) return r;
    r.gen = gen; r.n = n; r.defaults = defaults;
    for (int i = 0; i < n; ++i)
    {
        const size_t used = strlen(r.what);
        if (used + 8 >= sizeof(r.what)) break;
        _snprintf_s(r.what + used, sizeof(r.what) - used, _TRUNCATE, "%s[%s] %s = %s", i ? ", " : "", batch[i].sec,
                    batch[i].key, batch[i].val);
    }
    if (mini::ApplyFile(iniPath, batch, n, r.err, sizeof(r.err))) { r.kind = 1; return r; }
    StoreMax(&g_failGen, gen);
    InterlockedIncrement(&g_failSeq);
    r.kind = 2;
    return r;
}

// ---- Switch Off Until Restart waits for the game to run ---------------------------------------------------
// Unhook() hands the cars' body fields and lamps, the police, the street life and the airliner back through
// their OWN game-thread updates during its Sleep(150), then sets g_unhooked, which gates those updates off
// (lcudrive.cpp Unhook / CarThunk). The world does not update while paused [I: Notes 43 #4 - a pause of
// over 3 s looked like helicopters "not updating"], and this request can only come from the pause menu.
// So the request is latched and the caller unhooks only once the pause byte has read 0 for kUnhookRunMs;
// meanwhile the menu is inert (pausemenu.h: no row, open pages back out). Pausing again restarts the wait.
static const DWORD kUnhookRunMs = 250;
struct UnhookGate { bool latched = false; bool running = false; DWORD since = 0; };
static UnhookGate g_unhookGate;               // hotkey thread only

inline bool UnhookDue(UnhookGate& g, bool request, unsigned pauseByte, DWORD now)    // pure
{
    if (request) g.latched = true;
    if (!g.latched) return false;
    if (pauseByte != 0) { g.running = false; return false; }
    if (!g.running) { g.running = true; g.since = now; }
    return MsSince(now, g.since) >= kUnhookRunMs;
}

// ---- the hotkey thread's menu pass (lcudrive.cpp HotkeyThread, before its focus gate) -----------------
// A save that is due is written and published here - the pass's only ReloadConfig (published = true: the
// caller then skips its 1 s ini poll and Ctrl+Alt+R in this pass). A Switch Off or Reload request makes the
// save due (the values on screen are written first). Then the menu's session requests in order: Mod Effects,
// Reload Settings File, and last Switch Off (unhook = true: the caller unhooks) - not before the game has run
// again for kUnhookRunMs (UnhookGate). The game-thread notes are logged here (no game-thread logs).
struct PassHooks
{
    virtual void IniChanged() = 0;        // lcudrive: swallow our own write (the 1 s poll's timestamp)
    virtual void Reload() = 0;            // lcudrive: ReloadConfig() (LoadConfig into the other buffer, swap)
    virtual void SetMode(bool on) = 0;    // lcudrive: the ] / [ code
};
typedef void (*LogFn)(const char* fmt, ...);
struct PassResult { bool published; bool unhook; };

inline PassResult Pass(const char* iniPath, unsigned pauseByte, DWORD now, PassHooks& h, LogFn log)
{
    PassResult p{ false, false };
    const bool unhookReq = InterlockedExchange(&g_unhookReq, 0) != 0;
    const bool reloadReq = InterlockedExchange(&g_reloadReq, 0) != 0;
    const TickResult r = HotkeyTick(iniPath, pauseByte, now, unhookReq || reloadReq);
    if (r.kind == 1)
    {
        if (r.defaults) log(">>> menu: Reset to Defaults - %d keys written\n", r.n);
        else log(">>> menu: saved %d change(s) to lcudrive.ini: %s\n", r.n, r.what);
        h.IniChanged();
        h.Reload();
        Published(r.gen);                                  // AFTER the swap: the draft gives way to the new value
        p.published = true;
    }
    else if (r.kind == 2) log("!!! menu: couldn't save lcudrive.ini (%s) - nothing was changed\n", r.err);
    if (const LONG w = InterlockedExchange(&g_noteWanted, 0))
        log(">>> menu: wanted level %ld requested for %s\n", (w & 0xFF) - 1,
            (w >> 8) == 1 ? "P1 (who paused)" : (w >> 8) == 2 ? "P2 (who paused)" : "both players");
    if (InterlockedExchange(&g_noteAirliner, 0)) log(">>> menu: airliner requested\n");
    if (InterlockedExchange(&g_noteOpenFail, 0))
        log("menu: Mod Options not opened - its menu records are missing and could not be added again\n");
    if (const LONG m = InterlockedExchange(&g_modeReq, 0))
    {
        log(">>> menu: Mod Effects -> %s (this session)\n", m > 0 ? "On" : "Stock");
        h.SetMode(m > 0);
    }
    if (reloadReq)
    {
        log(">>> menu: reload requested\n");
        if (!p.published) { h.IniChanged(); h.Reload(); p.published = true; }
    }
    if (unhookReq && !g_unhookGate.latched)
        log(">>> menu: Switch Off Until Restart - lcudrive switches off once the game runs again\n");
    if (UnhookDue(g_unhookGate, unhookReq, pauseByte, now))
    {
        log(">>> menu: Switch Off Until Restart - the game is running, switching off now\n");
        p.unhook = true;
    }
    return p;
}

} // namespace mbr
