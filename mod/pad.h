// =============================================================================================
//  pad.h - read the player's XInput controller directly, for the one control the game does not
//  give us: a separate handbrake. (Steering, throttle and brake/reverse already arrive through the
//  game's own input block, from keyboard and controller alike.)
//
//  The game itself polls XINPUT1_3.dll (ordinal 2 = XInputGetState), so the DLL is already loaded;
//  we borrow the same entry point. XInputGetState on an EMPTY port is slow (it enumerates), so a
//  disconnected pad is only re-probed every two seconds.
// =============================================================================================
#pragma once
#include <Xinput.h>

namespace pad {

typedef DWORD(WINAPI* FnGetState)(DWORD user, XINPUT_STATE* state);

static FnGetState g_getState = nullptr;
static int        g_user = -1;          // the XInput port of the current player context, -1 = none
static DWORD      g_nextProbe = 0;
static WORD       g_buttons = 0;
static BYTE       g_lt = 0, g_rt = 0;
static float      g_ly = 0.0f;          // left stick Y, -1 (pulled back) .. +1 (pushed forward)
static float      g_lx = 0.0f;          // left stick X, -1 (left) .. +1 (right)

static float StickAxis(SHORT v) { return v < 0 ? static_cast<float>(v) / 32768.0f : static_cast<float>(v) / 32767.0f; }

static void Init()
{
    HMODULE m = GetModuleHandleA("XINPUT1_3.dll");
    if (!m) m = LoadLibraryA("XINPUT1_3.dll");
    if (!m) m = LoadLibraryA("XINPUT1_4.dll");
    if (m)
    {
        g_getState = reinterpret_cast<FnGetState>(GetProcAddress(m, "XInputGetState"));
        if (!g_getState) g_getState = reinterpret_cast<FnGetState>(GetProcAddress(m, reinterpret_cast<LPCSTR>(2)));
    }
}

// Two players (2026-11-11). Each player context polls its own XInput port:
//   [coop] pad_p1 / pad_p2 = a port 0..3, "none", or auto. Auto: with two or more controllers, player 1 has
//   the lowest port and player 2 the next; with exactly ONE controller it is player 2's while player 2 is in
//   the game (player 1 on the keyboard), otherwise player 1's.
// Which ports are connected is kept by the hotkey thread (Scan, every 2 s), so the slow empty-port probe
// never runs on the game thread.
static volatile LONG g_connected = 0;       // bit i: XInput port i has a controller
static int           g_cfgPort[2] = { -1, -1 };   // -1 auto, 0..3 a port, 4 none
static int           g_player = 0;          // the player context being polled (lcudrive.cpp SwapCtx)
static volatile bool g_p2Here = false;      // player 2 is in the game (lcudrive.cpp)
static int           g_portLogged[2] = { -2, -2 };

static void Scan()
{
    if (!g_getState) return;
    LONG m = 0;
    XINPUT_STATE st;
    for (DWORD u = 0; u < 4; ++u) if (g_getState(u, &st) == ERROR_SUCCESS) m |= 1 << u;
    InterlockedExchange(&g_connected, m);
}

static int PortFor(int player)
{
    if (g_cfgPort[player] >= 0) return g_cfgPort[player] < 4 ? g_cfgPort[player] : -1;
    const LONG m = g_connected;
    int ports[4], n = 0;
    for (int u = 0; u < 4; ++u) if (m & (1 << u)) ports[n++] = u;
    if (n == 0) return -1;
    if (n == 1) return (g_p2Here ? player == 1 : player == 0) ? ports[0] : -1;
    return ports[player];
}

// Once per player frame, for the current player context.
static void Poll()
{
    g_buttons = 0; g_lt = g_rt = 0; g_ly = g_lx = 0.0f;
    g_user = PortFor(g_player);
    if (g_user != g_portLogged[g_player])
    {
        g_portLogged[g_player] = g_user;
        if (g_user >= 0) Log(">>> player %d's controller: XInput port %d\n", g_player + 1, g_user);
        else Log(">>> player %d: no controller of their own (keyboard)\n", g_player + 1);
    }
    if (!g_getState || g_user < 0) return;
    XINPUT_STATE st;
    if (g_getState(static_cast<DWORD>(g_user), &st) != ERROR_SUCCESS)
    {
        InterlockedAnd(&g_connected, ~(1 << g_user));     // unplugged: Scan finds it again
        g_user = -1;
        return;
    }
    g_buttons = st.Gamepad.wButtons; g_lt = st.Gamepad.bLeftTrigger; g_rt = st.Gamepad.bRightTrigger;
    g_ly = StickAxis(st.Gamepad.sThumbLY); g_lx = StickAxis(st.Gamepad.sThumbLX);
}

// Button codes: the XINPUT_GAMEPAD_* bits, plus two pseudo-bits for the analog triggers.
static const uint32_t kLT = 0x10000, kRT = 0x20000;

static bool Down(uint32_t code)
{
    if (!code) return false;
    if (code & kLT) return g_lt > 128;
    if (code & kRT) return g_rt > 128;
    return (g_buttons & static_cast<WORD>(code)) != 0;
}

static uint32_t Parse(const char* s)
{
    struct { const char* n; uint32_t c; } t[] = {
        { "A", XINPUT_GAMEPAD_A }, { "B", XINPUT_GAMEPAD_B }, { "X", XINPUT_GAMEPAD_X }, { "Y", XINPUT_GAMEPAD_Y },
        { "LB", XINPUT_GAMEPAD_LEFT_SHOULDER }, { "RB", XINPUT_GAMEPAD_RIGHT_SHOULDER },
        { "LS", XINPUT_GAMEPAD_LEFT_THUMB }, { "RS", XINPUT_GAMEPAD_RIGHT_THUMB },
        { "BACK", XINPUT_GAMEPAD_BACK }, { "START", XINPUT_GAMEPAD_START },
        { "DUP", XINPUT_GAMEPAD_DPAD_UP }, { "DDOWN", XINPUT_GAMEPAD_DPAD_DOWN },
        { "DLEFT", XINPUT_GAMEPAD_DPAD_LEFT }, { "DRIGHT", XINPUT_GAMEPAD_DPAD_RIGHT },
        { "LT", kLT }, { "RT", kRT }, { "NONE", 0 },
    };
    for (auto& e : t) if (_stricmp(s, e.n) == 0) return e.c;
    return 0;
}

} // namespace pad
