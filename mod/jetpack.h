// =============================================================================================
//  jetpack.h - the astronaut's jetpack: higher, longer, and resumable in the air (Notes\30).
//
//  Each flight is a JetPackAddOn the game builds on the second jump press in the air; it copies
//  its climb height (0.55 above the press) and fuel (1.5 s) from the character data at creation.
//  Let go and it is not deleted - it stays attached, "spent" (state 3), until you land, and the
//  game refuses a new one while it exists: that is the land-first rule.
//
//  lcudrive takes the single call to UpdateAstroJetPack (0x140048225, Notes\30 §4.2). Before it:
//  a fresh JUMP press while falling with a spent add-on that still has fuel replays the game's own
//  take-off (OnAdded's body: HoverUp, take-off anim, state 1, new target, sound) - the flight
//  carries on with the fuel that was left. After it: a flight created this frame gets our height
//  and fuel. Nothing shared is written: the character data stays as shipped.
// =============================================================================================
#pragma once

namespace jp {

typedef uint64_t (__fastcall* UpdateFn)(void* ch, uint64_t held);
typedef void*    (__fastcall* FindFn)(void* holder, const uint32_t* typeHash, void* after);
typedef bool     (__fastcall* ChBoolFn)(void* ch);
typedef bool     (__fastcall* ChIntBoolFn)(void* ch, int id);
typedef void*    (__fastcall* SetActionFn)(void* ch, int id, int arg);
typedef void     (__fastcall* ChIntFn)(void* ch, int arg);

static UpdateFn    g_orig = nullptr;
static FindFn      g_find = nullptr;
static ChBoolFn    g_onGround = nullptr;
static ChIntBoolFn g_inState = nullptr;
static SetActionFn g_setAction = nullptr;
static ChIntBoolFn g_playAnim = nullptr;
static ChIntFn     g_sound = nullptr;

static const uintptr_t SITE = 0x048225, FN_UPDATE = 0x044990;
static const uintptr_t RVA_TYPE = 0x1C9E868;                       // u32 FNV("JetPack"), filled at boot
static const uintptr_t RVA_JUMP_BIT = 0x1174CB4;
static const uintptr_t ID_JUMPING = 0x116AD34, ID_FALLING = 0x116AD80, ID_SWIMMING = 0x116ACFC,
                       ID_DEATH = 0x116A9F8, ID_JUMP = 0x117DA10, ID_HOVERUP = 0x117DBDC;
static const uintptr_t JP_STATE = 0x78, JP_FUEL = 0x7C, JP_MAX = 0x88, JP_HEIGHT = 0x8C, JP_TARGET = 0x90;

static void*  g_last = nullptr;                                    // the current flight's add-on
static float  g_ceiling = 0.0f;                                    // its first take-off target

static void Init()
{
    g_find      = reinterpret_cast<FindFn>(g_base + 0x766990);
    g_onGround  = reinterpret_cast<ChBoolFn>(g_base + 0x66BD20);
    g_inState   = reinterpret_cast<ChIntBoolFn>(g_base + 0x006D40);
    g_setAction = reinterpret_cast<SetActionFn>(g_base + 0x66FBC0);
    g_playAnim  = reinterpret_cast<ChIntBoolFn>(g_base + 0x66FC40);
    g_sound     = reinterpret_cast<ChIntFn>(g_base + 0x7A5520);
    g_orig      = reinterpret_cast<UpdateFn>(g_base + FN_UPDATE);
}

static inline int Id(uintptr_t rva) { return *reinterpret_cast<const int32_t*>(g_base + rva); }

static void* Find(void* ch)
{
    return g_find(reinterpret_cast<uint8_t*>(ch) + 0x680, reinterpret_cast<const uint32_t*>(g_base + RVA_TYPE), nullptr);
}

// Before the game's update: a mid-air resume. Returns the add-on that existed before the call.
static void* Pre(void* ch, const Config& c)
{
    void* jp = Find(ch);
    if (!PtrOk(jp) || !c.jp_resume || At<int32_t>(jp, JP_STATE) != 3 || !(At<float>(jp, JP_FUEL) > 0.05f)) return jp;
    void* ctrl = At<void*>(ch, 0x6A8);
    void* pad = PtrOk(ctrl) ? At<void*>(ctrl, 0x18) : nullptr;
    if (!PtrOk(pad) || !(At<uint32_t>(pad, 0x10) & *reinterpret_cast<const uint32_t*>(g_base + RVA_JUMP_BIT))) return jp;
    if (g_onGround(ch) || g_inState(ch, Id(ID_SWIMMING)) || g_inState(ch, Id(ID_DEATH))) return jp;
    if (!g_inState(ch, Id(ID_FALLING)) && !g_inState(ch, Id(ID_JUMPING)) && !g_inState(ch, Id(ID_JUMP))) return jp;
    // OnAdded's own steps (0x1409CE371..0x1409CE3C5), minus the hook registration
    g_setAction(ch, Id(ID_HOVERUP), 0);
    g_playAnim(ch, 0x5D);
    At<int32_t>(jp, JP_STATE) = 1;
    float t = At<float>(ch, 0xAC) + At<float>(jp, JP_HEIGHT);
    if (c.jp_keep_ceiling && !c.jp_unlimited && jp == g_last) t = std::fmin(t, g_ceiling);
    At<float>(jp, JP_TARGET) = t;
    g_sound(ch, 0);
    Log(">>> jetpack: resumed in the air (%.1f s of fuel left)\n", At<float>(jp, JP_FUEL));
    return jp;
}

// [jp-unl] No height limit: while the flight is on (state 1 climb / 2 hover = JUMP still held, fuel left), keep the
// target ahead of the character so it never reaches it: Move 0x1409CE710 gives vy = 1.5 * (target + 1 - y), so
// target = y + speed / 1.5 - 1 climbs at `climb_speed`; the fuel (burned while climbing) decides how high you get.
static void Unlimited(void* ch, const Config& c)
{
    if (!c.jp_unlimited) return;
    void* jp = Find(ch);
    if (!PtrOk(jp)) return;
    const int32_t st = At<int32_t>(jp, JP_STATE);
    if ((st != 1 && st != 2) || !(At<float>(jp, JP_FUEL) > 0.0f)) return;
    const float y = At<float>(ch, 0xAC);
    if (!std::isfinite(y)) return;
    At<int32_t>(jp, JP_STATE) = 1;
    At<float>(jp, JP_TARGET) = y + c.jp_climb_speed / 1.5f - 1.0f;
}

// After it: a flight that started this frame gets our height and fuel.
static void Post(void* ch, const Config& c, void* before)
{
    if (before) { Unlimited(ch, c); return; }
    void* jp = Find(ch);
    if (!PtrOk(jp) || At<int32_t>(jp, JP_STATE) != 1) return;
    At<float>(jp, JP_TARGET) += c.jp_height - At<float>(jp, JP_HEIGHT);
    At<float>(jp, JP_HEIGHT) = c.jp_height;
    At<float>(jp, JP_MAX) = c.jp_time;
    At<float>(jp, JP_FUEL) = c.jp_time;
    g_last = jp;
    g_ceiling = At<float>(jp, JP_TARGET);
    Unlimited(ch, c);
}

} // namespace jp
