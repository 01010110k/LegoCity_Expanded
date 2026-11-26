// ============================================================================
//  radiologo.h - the radio station logo overlay, the game side (radiologo1, 2026-11-19, Notes 35).
//
//  The radio thread (radio.h) bumps rd::g_logoSeq with the station's atlas tile when the station changes or the
//  radio comes on as you get in. Once per world update (PMThunk, after gfx2::Publish) this turns that into
//  IniParams 16.xyzw / 17.xy (radiologo_model.h rlm::Step) for ShaderFixes\radiologo_fx.hlsl, which runs the
//  slide / hold / fade on the GPU from the start time on the 0.x clock. Written only:
//    - when the block changes (or once a second, so an F10 reload of the ini is repaired), and
//    - after 17.w reads back the sentinel radiologo_fx.ini sets: without that file the array is not grown to 18
//      float4s and nothing is written (no heap overrun).
//  Hidden for a cutscene and in the Backrooms (rd::g_hold covers both: the show is dropped) and while the game
//  is paused (only hidden; the timeline runs on). No game memory is written; the game's shaders are untouched.
//
//  The tiles: <game>\Mods\RadioLogo\logos.txt (index<TAB>station id), read once at startup before rd::Start().
//  Missing: every station gets the generic tile 1 - the overlay still works if the atlas is there.
// ============================================================================
#ifndef LCU_RADIOLOGO_H
#define LCU_RADIOLOGO_H

#include "radiologo_model.h"

namespace rlg {

static uint32_t g_lastWrite = 0;
static rlm::Block g_cur, g_written;
static bool g_haveWritten = false, g_noSentinelLogged = false, g_loggedOk = false;
static LONG g_seenSeq = 0;
static volatile LONG g_fault = 0;

// Load Mods\RadioLogo\logos.txt (beside the game's exe) into rd::g_logoTiles. Call before rd::Start().
static void Load()
{
    char path[MAX_PATH] = {};
    const DWORD n = GetModuleFileNameA(nullptr, path, MAX_PATH);
    if (!n || n >= MAX_PATH) return;
    char* slash = strrchr(path, '\\');
    if (!slash) return;
    slash[1] = 0;
    if (strcat_s(path, "Mods\\RadioLogo\\logos.txt") != 0) return;
    FILE* f = nullptr;
    if (fopen_s(&f, path, "rb") != 0 || !f) { Log("    radiologo: no %s - every station shows the generic logo (if the overlay is installed)\n", path); return; }
    std::string text;
    char buf[4096];
    size_t got;
    while ((got = fread(buf, 1, sizeof(buf), f)) > 0 && text.size() < 65536) text.append(buf, got);
    fclose(f);
    const int tiles = rlm::ParseTiles(text.data(), text.size(), rd::g_logoTiles);
    Log(">>> radiologo: %d tiles from %s\n", tiles, path);
}

// 17.w is the sentinel -> write 16.xyzw / 17.xy (f[64..69]); returns 1 written, 0 no sentinel, -1 fault.
// Also reads the 0.x clock (f[0]) into *clock.
static int ReadClock(uintptr_t base, float* clock, bool* sentinel)
{
    __try
    {
        volatile float* f = reinterpret_cast<volatile float*>(base);
        *sentinel = f[71] == rlm::kSentinel17w;
        *clock = f[0];
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
static int WriteSlots(uintptr_t base, const float v[6])
{
    __try
    {
        volatile float* f = reinterpret_cast<volatile float*>(base);
        if (f[71] != rlm::kSentinel17w) return 0;   // 17.w: the array was not grown to 18 float4s
        f[64] = v[0]; f[65] = v[1]; f[66] = v[2]; f[67] = v[3];   // 16.xyzw
        f[68] = v[4]; f[69] = v[5];                                // 17.xy (17.z is the ini's, 17.w the sentinel)
        return 1;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}

static void Publish(const Config& cfg)
{
    if (g_fault) return;
    if (!cfg.bridge || !vsim::BridgeLockedQ(g_bridge) || g_bridge.base == 0) return;
    float clock = 0.0f;
    bool sentinel = false;
    if (ReadClock(g_bridge.base, &clock, &sentinel) != 1) return;
    if (!sentinel)
    {
        if (!g_noSentinelLogged) { g_noSentinelLogged = true; Log(">>> radiologo: IniParams 17.w is not the sentinel - ShaderFixes\\radiologo_fx.ini is not included by d3dx.ini; no station logos\n"); }
        g_seenSeq = rd::g_logoSeq;                  // nothing queued up for when it appears
        return;
    }
    if (!g_loggedOk) { g_loggedOk = true; Log(">>> radiologo: the overlay is in place (IniParams 16 / 17)\n"); }

    rlm::Settings s;
    s.on = cfg.radio_logo && cfg.radio_enable;
    s.seconds = cfg.radio_logo_seconds; s.corner = cfg.radio_logo_corner; s.scale = cfg.radio_logo_scale;
    s.onEnter = cfg.radio_logo_on_enter;
    s = rlm::Sanitize(s);
    rlm::Event ev;
    const LONG seq = rd::g_logoSeq;
    if (seq != g_seenSeq)
    {
        g_seenSeq = seq;
        ev.fresh = true; ev.kind = static_cast<int>(rd::g_logoKind); ev.tile = static_cast<int>(rd::g_logoTile);
    }
    const bool hideHard = rd::g_hold != 0;                                   // a cutscene / FMV, or the Backrooms
    const bool hideSoft = rd::g_pauseByte && *rd::g_pauseByte != 0;          // the pause menu
    g_cur = rlm::Step(g_cur, ev, s, clock, hideHard, hideSoft);
    const uint32_t now = GetTickCount();
    if (g_haveWritten && rlm::Same(g_cur, g_written) && now - g_lastWrite < 1000) return;
    const float v[6] = { g_cur.start, g_cur.tile, g_cur.seconds, g_cur.corner, g_cur.scale, g_cur.hide };
    const int r = WriteSlots(g_bridge.base, v);
    if (r == 1)
    {
        if (ev.fresh && g_cur.tile >= 0.0f)
            Log("    radiologo: tile %d for %.1f s (%s)\n", static_cast<int>(g_cur.tile), g_cur.seconds, ev.kind == rlm::EV_STEP ? "station" : "got in");
        g_written = g_cur; g_haveWritten = true; g_lastWrite = now;
    }
    else if (r < 0) { InterlockedExchange(&g_fault, 1); Log("!!! radiologo: fault writing IniParams 16 / 17 - the logo overlay is off for this session\n"); }
}

} // namespace rlg

#endif // LCU_RADIOLOGO_H
