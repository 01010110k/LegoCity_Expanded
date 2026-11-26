// =============================================================================================
//  chute.h - a parachute on demand after bailing out of a helicopter (Notes\29).
//
//  The game's skydive is one character add-on, SkyDiveAddon, that exists only while skydiving;
//  its chute (add-on state 9) is opened only by the script action DeployParachute, which the
//  missions fire from a trigger box. There is no chute button. So after an lcudrive bail-out
//  (heli.h OffPadExit) this arms itself, and a JUMP press while Chase is still falling runs what
//  the missions run - StartSkydiveMidfall then DeployParachute, in the same frame (the free-fall
//  state never gets an update, so its ground "splat" can never fire) - with the "Default"
//  settings (the only steerable chute). Landing or water ends it by itself; a watchdog ends it
//  on death (the add-on would otherwise restart the dive at the respawn point: a death loop), on
//  boarding a vehicle, or on a landing the surface refuses. Never press: he just falls.
//  Every call runs on the game thread (from a vehicle update, which is where the game's own
//  heli skydive runs too).
// =============================================================================================
#pragma once

namespace chute {

typedef int  (__fastcall* ExecFn)(void* self, void* subject, void* ctx);
typedef void (__fastcall* StrFn)(const char* name);
typedef void (__fastcall* ChFn)(uint8_t* ch);
typedef bool (__fastcall* CtxFn)(uint8_t* ch, int ctx);
typedef bool (__fastcall* BoolFn)(uint8_t* ch);

static ExecFn g_start = nullptr, g_deploy = nullptr, g_end = nullptr;
static StrFn  g_select = nullptr;
static ChFn   g_constOff = nullptr;
static CtxFn  g_inCtx = nullptr;
static BoolFn g_onGround = nullptr;

static const uintptr_t RVA_SKY_KEY   = 0x1C7B488;   // u32 hash("SkyDive") = 0x9D0FA2EA
static const uintptr_t RVA_SKY_VT    = 0xD49868;    // SkyDiveAddon main vtable
static const uintptr_t RVA_JUMP_MASK = 0x1174CB4;   // u32: the JUMP button bit in the pad words
static const uintptr_t RVA_SETTINGS  = 0x1C7B468;   // void*: active settings block (name at -0x40)
static const uintptr_t CTX_JUMPING = 0x116AD34, CTX_FALLING = 0x116AD80, CTX_DEATH = 0x116A9F8,
                       CTX_DOOMED = 0x116B30C;

struct State
{
    uint8_t* ch;
    float    t, hag0, y0, gT, lT, oT;
    uint32_t prevHeld;
    uint16_t slot24;
    bool     armed, open, cut;
};
static State g = {};
static State g_p2 = {};                 // player 2's (lcudrive.cpp SwapCtx)
static void SwapP2() { const State t = g; g = g_p2; g_p2 = t; }

// The chute's sink rate lives in the active settings record (state 9 block, max_fall_speed:
// block + 9 * 0x28 + 0x08, Notes\29 §3) and is read every frame; set for our chute only, put back after.
static float* g_fallField = nullptr;
static float  g_fallOrig = 0.0f;

static void RestoreFall()
{
    if (g_fallField) { *g_fallField = g_fallOrig; g_fallField = nullptr; }
}

static void Init()
{
    g_start    = reinterpret_cast<ExecFn>(g_base + 0x114C00);   // Character_StartSkydiveMidfall
    g_deploy   = reinterpret_cast<ExecFn>(g_base + 0x110900);   // Character_DeployParachute
    g_end      = reinterpret_cast<ExecFn>(g_base + 0x110FF0);   // Character_EndSkydive
    g_select   = reinterpret_cast<StrFn>(g_base + 0x7DD880);    // select skydive settings by name
    g_constOff = reinterpret_cast<ChFn>(g_base + 0x7DA7D0);     // DisableSkydiveConstantMove(ch)
    g_inCtx    = reinterpret_cast<CtxFn>(g_base + 0x006D40);    // IsInContext(ch, id)
    g_onGround = reinterpret_cast<BoolFn>(g_base + 0x66BD20);   // IsOnGround(ch)
}

static inline int Ctx(uintptr_t rva) { return *reinterpret_cast<const int32_t*>(g_base + rva); }

static uint8_t* Pad(uint8_t* ch)
{
    uint8_t* c = At<uint8_t*>(ch, 0x6A8);
    return PtrOk(c) ? At<uint8_t*>(c, 0x18) : nullptr;
}

static uint8_t* FindSkyDive(uint8_t* ch)
{
    uint8_t* list = At<uint8_t*>(ch, 0x680);
    if (!PtrOk(list)) return nullptr;
    const uint32_t key = *reinterpret_cast<const uint32_t*>(g_base + RVA_SKY_KEY);
    int guard = 0;
    for (uint8_t* a = At<uint8_t*>(list, 8); PtrOk(a) && guard < 64; a = At<uint8_t*>(a, 0x48), ++guard)
        if (At<uint32_t>(a, 0x50) == key && !(At<uint8_t>(a, 0x38) & 1))
            return At<uintptr_t>(a, 0) == g_base + RVA_SKY_VT ? a : nullptr;
    return nullptr;
}

static bool Riding(uint8_t* ch)
{
    uint8_t* r = At<uint8_t*>(ch, 0x340);
    if (!PtrOk(r)) return false;
    const int s = At<int32_t>(r, 0xD0);
    return s == 4 || s == 3;
}

// Right after the bail-out's jump-off + Falling (heli.h). hag: the heli's height above ground.
static void OnBailOut(uint8_t* ch, float hag)
{
    g = State{};
    g.ch = ch; g.armed = true;
    g.hag0 = hag < 1e30f ? hag : 3.0e38f;
    g.y0 = At<float>(ch, 0xAC);
    if (uint8_t* p = Pad(ch)) g.prevHeld = At<uint32_t>(p, 0x0C);    // a JUMP already held is no press
    Log(">>> parachute armed: press JUMP while falling to open it\n");
}

static bool OpenChute(uint8_t* ch, const Config& cfg)
{
    if (!(At<uint32_t>(ch, 0xD4) & 0x1000)) return false;            // the actions' own subject check
    if (FindSkyDive(ch)) return false;
    g_select("Default");                                             // the steerable chute, 2 u/s
    g_constOff(ch);                                                  // a mission may have left it on
    if (cfg.heli_chute_fall > 0.0f && !g_fallField)
    {
        uint8_t* set = *reinterpret_cast<uint8_t**>(g_base + RVA_SETTINGS);
        if (PtrOk(set))
        {
            g_fallField = reinterpret_cast<float*>(set + 9 * 0x28 + 0x08);
            g_fallOrig = *g_fallField;
            if (IsFin(g_fallOrig) && g_fallOrig > 0.0f && g_fallOrig < 50.0f) *g_fallField = cfg.heli_chute_fall;
            else g_fallField = nullptr;                              // not what Notes\29 describes: leave it
        }
    }
    g.slot24 = At<uint16_t>(ch, 0x71A);                              // anim slot 0x24 (never restored by the game)
    float rp[3]; std::memcpy(rp, reinterpret_cast<uint8_t*>(ch) + 0x7F4, 12);
    g_start(nullptr, ch + 0x10, nullptr);                            // Jumping + add-on + state 3 + pose
    uint8_t* a = FindSkyDive(ch);
    if (!a) { Log(">>> parachute: the game refused the skydive\n"); return false; }
    // the add-on rewrites the respawn point from these every frame; nothing initialises them
    std::memcpy(a + 0xF8, rp, 12); std::memcpy(a + 0x104, rp, 12);
    At<float>(a, 0x108) = -3.4e38f; At<uint8_t>(a, 0x1C8) = 0;
    if (At<int32_t>(a, 0xBC) == 3 && g_inCtx(ch, Ctx(CTX_JUMPING)))
    {
        g_deploy(nullptr, ch + 0x10, nullptr);                       // state 9, chute-open anim, canopy
        const uint8_t* set = *reinterpret_cast<uint8_t* const*>(g_base + RVA_SETTINGS);
        Log(">>> parachute OPEN (state %d, canopy %p, settings '%s')\n", At<int32_t>(a, 0xBC),
            At<void*>(a, 0x1D8), PtrOk(set) ? reinterpret_cast<const char*>(set - 0x40) : "?");
    }
    else
    {
        g_end(nullptr, ch + 0x10, nullptr);                          // vetoed: give up cleanly
        Log(">>> parachute: the game would not open it here (state %d) - plain fall\n", At<int32_t>(a, 0xBC));
    }
    return true;
}

static uint32_t PressedJump(uint8_t* ch)
{
    uint8_t* pad = Pad(ch);
    const uint32_t held = pad ? At<uint32_t>(pad, 0x0C) : 0u, press = held & ~g.prevHeld;
    g.prevHeld = held;
    return press & *reinterpret_cast<const uint32_t*>(g_base + RVA_JUMP_MASK);
}

// Once per frame, from whichever vehicle updates.
static void Tick(float dt, const Config& cfg)
{
    if (!g.armed && !g.open) return;
    uint8_t* ch = CtxChar();                                         // the current player context's character
    if (ch != g.ch) { g = State{}; return; }
    uint8_t* a = FindSkyDive(ch);
    if (g.open)
    {
        const uint32_t jump = PressedJump(ch);
        if (!a)
        {
            At<uint16_t>(ch, 0x71A) = g.slot24;
            RestoreFall();
            if (g.cut && cfg.heli_parachute)
            {
                // cut away: falling again, and the chute can be opened once more lower down
                const State keep = g;
                g = State{};
                g.ch = keep.ch; g.hag0 = keep.hag0; g.y0 = keep.y0; g.prevHeld = keep.prevHeld; g.armed = true;
                Log(">>> parachute: cut away - JUMP opens it again\n");
            }
            else { Log(">>> parachute: landed, put away\n"); g = State{}; }
            return;
        }
        const int s = At<int32_t>(a, 0xBC);
        const char* why = nullptr;
        if (g_inCtx(ch, Ctx(CTX_DEATH)) || g_inCtx(ch, Ctx(CTX_DOOMED))) why = "died";
        else if (Riding(ch)) why = "got into a vehicle";
        if (s == 9 && g_onGround(ch)) { if ((g.gT += dt) > 1.0f) why = "landing refused by the surface"; } else g.gT = 0.0f;
        if (s == 4 && (g.lT += dt) > 3.0f) why = "landing took too long";
        if ((g.oT += dt) > 120.0f) why = "two minutes";
        if (s != 9 && s != 4) why = "left the chute state";
        // cut the cord: JUMP again under the canopy (not in the first 0.8 s - a double tap opening it)
        if (!why && jump && s == 9 && g.oT > 0.8f && cfg.heli_chute_cut && !g_onGround(ch)) { why = "cut away"; g.cut = true; }
        if (why && !At<uint8_t>(a, 0x134)) { g_end(nullptr, ch + 0x10, nullptr); Log(">>> parachute ended: %s\n", why); }
        return;
    }
    g.t += dt;
    const uint32_t jump = PressedJump(ch);
    if (!cfg.heli_parachute || g.t > 30.0f) { g.armed = false; return; }
    if (Riding(ch)) return;                                          // the jump-off is still finishing
    if (g.t > 0.25f && g_onGround(ch)) { g.armed = false; return; }  // landed without it: a plain fall
    if (!jump) return;
    uint8_t* phys = At<uint8_t*>(ch, 0x210);
    const float vy = PtrOk(phys) ? At<float>(phys, 0xF4) : 0.0f;
    const float estHag = g.hag0 - (g.y0 - At<float>(ch, 0xAC));
    if (g.t < 0.25f || vy > -0.5f || estHag < 2.5f) return;
    if (!g_inCtx(ch, Ctx(CTX_FALLING)) && !g_inCtx(ch, Ctx(CTX_JUMPING))) return;
    uint8_t* dis = At<uint8_t*>(ch, 0x650);                          // the astronaut's jetpack takes
    if (PtrOk(dis) && (At<uint8_t>(dis, 0xB4) & 7)) return;          // the same press: let it win
    if (OpenChute(ch, cfg)) { g.armed = false; g.open = true; g.cut = false; g.oT = 0.0f; }
}

} // namespace chute
