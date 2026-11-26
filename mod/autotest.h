// =============================================================================================
//  autotest.h - a scripted proving-ground pass on the car you are sitting in.
//
//    Ctrl+Alt+P  straight line: launch (towards 17 u/s), full brake to a stop
//    Ctrl+Alt+O  handling: up to 10 u/s, full-lock step right, recover, slalom, handbrake turn, stop
//    (either hotkey again = stop)
//
//  It drives through the game's OWN keyboard controls (J accelerate, K brake/reverse, A/D steer,
//  Space = lcudrive's handbrake), so stock `[` and lcudrive `]` get identical inputs and the A/B is
//  fair. Keys are injected only while the game has focus; losing focus, getting out, pausing, or a
//  collision (a sudden loss of speed nobody asked for) ends the pass and lets go of every key.
//  Each phase is logged, and its id goes into the telemetry `phase` column (straight 1-4,
//  handling 11-19) for tools\telemetry_report.py.
// =============================================================================================
#pragma once

namespace autotest {

enum { K_THR = 1, K_BRK = 2, K_LEFT = 4, K_RIGHT = 8, K_HB = 16 };
static const WORD kScan[5] = { 0x24 /*J*/, 0x25 /*K*/, 0x1E /*A*/, 0x20 /*D*/, 0x39 /*Space*/ };

struct Phase
{
    int   id;          // telemetry `phase` value (stable names for the analyzer, whatever the order)
    const char* name;
    int   keys;        // held for the whole phase
    float secs;        // time limit
    int   until;       // 0 = time only, 1 = until vfwd >= v, 2 = until vfwd <= v
    float v;
    float hold;        // > 0: throttle on/off to hold this speed
    float weave;       // > 0: alternate right/left, this many seconds per side
};

static const Phase kStraight[] = {
    { 1,  "settle",  0,     0.8f, 0, 0.0f,  0.0f,  0.0f },
    { 2,  "launch",  K_THR, 9.0f, 1, 17.0f, 0.0f,  0.0f },
    { 3,  "brake",   K_BRK, 7.0f, 2, 0.4f,  0.0f,  0.0f },
    { 4,  "pause",   0,     0.8f, 0, 0.0f,  0.0f,  0.0f },
};
// road-friendly first (the slalom stays within a lane or two); the full-lock step needs room
static const Phase kHandling[] = {
    { 11, "settle",  0,              0.8f, 0, 0.0f,  0.0f,  0.0f  },
    { 12, "to10",    K_THR,          7.0f, 1, 10.0f, 0.0f,  0.0f  },
    { 15, "slalom",  0,              4.2f, 0, 0.0f,  10.0f, 0.83f },
    { 16, "line",    0,              1.0f, 0, 0.0f,  10.0f, 0.0f  },
    { 13, "step",    K_RIGHT,        2.0f, 0, 0.0f,  10.0f, 0.0f  },
    { 14, "recover", 0,              1.0f, 0, 0.0f,  10.0f, 0.0f  },
    { 17, "hbturn",  K_LEFT | K_HB,  0.8f, 0, 0.0f,  0.0f,  0.0f  },
    { 18, "coast",   0,              1.5f, 0, 0.0f,  0.0f,  0.0f  },
    { 19, "stop",    K_BRK,          6.0f, 2, 0.4f,  0.0f,  0.0f  },
};

// Ctrl+Alt+C: cruise - lcudrive keeps the throttle at 5 u/s while you steer (A/D) to a test spot.
// (Not on J: the cruise itself presses J, which would read as the hotkey again.)
static const Phase kCruise[] = {
    { 31, "cruise",  0,           600.0f, 0, 0.0f,  5.0f,  0.0f  },
};

static const Phase* g_seq = nullptr;
static int      g_n = 0, g_i = -1;
static uint32_t g_t0 = 0;            // phase start
static int      g_down = 0;          // keys we are holding right now
static float    g_lastV = 0.0f;
static uint32_t g_lastVt = 0, g_lastFrame = 0, g_lastFrameT = 0;

static void Send(int k, bool down)
{
    INPUT in = {};
    in.type = INPUT_KEYBOARD;
    in.ki.wScan = kScan[k];
    in.ki.dwFlags = KEYEVENTF_SCANCODE | (down ? 0 : KEYEVENTF_KEYUP);
    SendInput(1, &in, sizeof(in));
}

static void SetKeys(int want)
{
    for (int k = 0; k < 5; ++k)
    {
        const int bit = 1 << k;
        if ((want & bit) && !(g_down & bit)) Send(k, true);
        if (!(want & bit) && (g_down & bit)) Send(k, false);
    }
    g_down = want;
}

static void Stop(const char* why)
{
    SetKeys(0);
    if (g_i >= 0) Log(">>> autotest stopped: %s\n", why);
    g_i = -1;
    g_seq = nullptr;
    InterlockedExchange(&mdl::g_testPhase, 0);
}

static void Start(const Phase* seq, int n, const char* name)
{
    if (g_i >= 0) { Stop("hotkey"); Beep(440, 120); return; }
    if (!mdl::g_snapOk || GetTickCount() - g_lastFrameT > 500)       // Tick tracks the player-car frames
    {
        Log(">>> autotest: get in a car first\n");
        Beep(220, 200);
        return;
    }
    g_seq = seq; g_n = n; g_i = 0;
    g_t0 = GetTickCount(); g_lastVt = 0;
    InterlockedExchange(&mdl::g_testPhase, seq[0].id);
    Log(">>> autotest %s on %s (%s)\n", name, mdl::g_modelName, mdl::g_snapOk ? "car found" : "?");
    Beep(1320, 60);
}

// Hotkey thread, every ~15 ms.
static void Tick(uint32_t now, bool focus, uint32_t frame)
{
    if (frame != g_lastFrame) { g_lastFrame = frame; g_lastFrameT = now; }
    if (g_i < 0) return;
    if (!focus) { Stop("the game lost focus"); return; }
    if (now - g_lastFrameT > 500) { Stop("no player car update (paused, or out of the car)"); return; }
    if (!mdl::g_snapOk) { Stop("lost the car"); return; }

    const float v = mdl::g_snap.vFwd;
    const Phase& p = g_seq[g_i];
    const float el = (now - g_t0) / 1000.0f;

    // a sudden loss of speed nobody asked for = we hit something: let go of everything
    if (!(p.keys & K_BRK) && g_lastVt && now - g_lastVt < 60 && g_lastV - v > 2.0f)
    {
        Stop("collision");
        Beep(220, 300);
        return;
    }
    g_lastV = v; g_lastVt = now;

    bool next = el >= p.secs;
    if (p.until == 1 && v >= p.v) next = true;
    if (p.until == 2 && v <= p.v) next = true;
    if (next)
    {
        Log("    autotest %-8s %5.2fs  v=%5.2f\n", p.name, el, v);
        if (++g_i >= g_n)
        {
            SetKeys(0);
            g_i = -1;
            InterlockedExchange(&mdl::g_testPhase, 0);
            Log(">>> autotest finished\n");
            Beep(990, 80); Beep(1320, 80);
            return;
        }
        g_t0 = now;
        InterlockedExchange(&mdl::g_testPhase, g_seq[g_i].id);
        return;
    }

    int keys = p.keys;
    if (p.hold > 0.0f)
    {
        const bool on = (g_down & K_THR) != 0;
        if (v < p.hold - 0.25f || (on && v < p.hold + 0.25f)) keys |= K_THR;
    }
    if (p.weave > 0.0f) keys |= (static_cast<int>(el / p.weave) & 1) ? K_LEFT : K_RIGHT;
    SetKeys(keys);
}

} // namespace autotest
