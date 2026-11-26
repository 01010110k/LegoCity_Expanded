// =============================================================================================
//  backrooms.h - the Backrooms (2026-11-19): the engine side of backrooms_model.h.
//  NOT COMPILED INTO lcudrive YET - written against the names in Drive\ as of 2026-11-19 03:00
//  (see INTEGRATION.md for the lines that include and call it, and for every address' tag).
//
//  What it does each world update (PMThunk -> RunBackrooms -> br::Tick):
//    * reads both players (position, on foot?, standing?, the engine's ground probe, their camera)
//    * steps the pure model (the per-visit maze, the arrival fall loop, exits, statues, colossi)
//    * carries out its actions:
//        A_TELEPORT   the game's own Character_Teleport core, per player - the recipe wanted_busted.h
//                     plays for one player at a time: 0x1406709F0(ch, &{x,y,z,1}) [+ 0x14066F720 heading].
//                     For a portal / the fall loop the physics velocity (phys+0xF0) is read before and
//                     written back after, so the walk / the fall carries on [I: Teleport does not touch
//                     it per the disassembly; U in game].
//        A_TERLOW     the level's kill height (s16 at [[lvl+0x188]+0xBA8]+0x68, stock -6) is held at
//                     `terlow` while anyone is in the shaft / maze, and put back (RunSafeSea's pattern).
//        A_RESPAWN    SetRespawnPoint 0x14066FB00 so a death down there does not respawn in the sea.
//        A_BLINK      a black moment in that player's view (shader flag) to hide the camera's swing.
//        statues      real characters (class Animatronics: the grey museum figures) spawned through
//                     spots.h's spawner sequence, frozen (brain off, not pushable, invulnerable,
//                     IdleNoGravity), moved with Teleport + SetHeading only when the model says so.
//        colossi      the same, with SetScale 0x1404A0450(ch+0x10, scale).
//    * publishes: br::g_inZone (music / radio off, weather off, mod spawners quiet), the shader
//      floats for backrooms_fx.hlsl (IniParams 8.xyzw / 9.x).
//  A fault anywhere here switches the feature off for the session (RunBackrooms' SEH filter) and
//  restores the kill height; players inside are NOT moved by the fault handler (the model's
//  "switched off while inside" path cannot run after a fault) - the pause menu's stock "return to
//  the police station" still works.
// =============================================================================================
#pragma once
#include "backrooms_model.h"
#include "backrooms_table.h"
#include "animfreeze_model.h"            // [freeze] when a figure's anim speed goes to 0

namespace br {

static const uintptr_t FN_TELEPORT = 0x6709F0, FN_HEADING = 0x66F720, FN_ONGROUND = 0x66BD20, FN_RESPAWN = 0x66FB00,
                       FN_CHLEVEL = 0x7EF080, FN_SETSCALE = 0x4A0450;
// [freeze] the character's anim speed: MechCharacter::Update 0x14066C967 advances the anim tree by dt * [ch+0x8B0];
// SetAnimSpeed 0x14066F1E0 = movss [rcx+0x8B0], xmm1 [P]. ch+0x221 = ApiCharacter base update's second (plain dt) advance
// switch (0x14049FD37; 0 expected on a MechCharacter) - logged.
static const uintptr_t FN_SETANIMSPEED = 0x66F1E0, CH_ANIMSPEED = 0x8B0, CH_BASEADVANCE = 0x221;
static const uintptr_t CH_POS = 0xA8, CH_PHYS = 0x210, PHYS_VEL = 0xF0, CH_RIDE = 0x340, CH_PROBE = 0x258,
                       PROBE_GROUND_OBJ = 0x200, PROBE_GROUND_Y = 0x224;
static const uintptr_t LVL_A = 0x188, LVL_B = 0xBA8, LVL_TERLOW = 0x68;
static const int FLAG_BRAIN_OFF = 0x5C, FLAG_NOT_PUSHABLE = 8;

typedef int  (__fastcall* TeleFn)(uint8_t* ch, const float* pos4);
typedef void (__fastcall* HeadFn)(uint8_t* ch, uint16_t bam, int snap);
typedef bool (__fastcall* GroundFn)(uint8_t* ch);
typedef void (__fastcall* RespawnFn)(uint8_t* ch, const float* xyz);
typedef uint8_t* (__fastcall* ChLevelFn)(uint8_t* ch);
typedef void (__fastcall* ScaleFn)(uint8_t* ent, float s);
typedef void (__fastcall* AnimSpeedFn)(uint8_t* ch, float s);   // [freeze]

static brm::Data  g_data;                       // (static storage: ~0.5 MB)
static brm::State g_state;
static bool   g_init = false, g_tableOk = false;
static volatile LONG g_inZone = 0;              // 1 while a player is in the shaft / maze: read by gsnd::Tick, weather, the spawners
static volatile bool g_stopReq = false;         // [round2] lcudrive Unhook: let go on the game thread (RunBackrooms -> Release)
static bool   g_inside[2] = {false, false};
static volatile LONG g_leaveReq[2] = {0, 0};      // [station1] the pause menu's "Return to Police Station" from inside: that player Leaves (station.h)
static bool   g_terlowHeld = false; static int16_t g_terlowStock = -6; static int16_t* g_terlowPtr = nullptr;
static uint32_t g_blinkUntil[2] = {0, 0};
static int    g_lastVisit = 0, g_lastLeave = 0;
static uint32_t g_said = 0;

struct Actor { uint8_t* ch; void* def; wd::SP watch; bool want; float pos[3]; float yaw, scale; uint32_t born;
               afz::St fz; int pose; bool saidReset; };                   // [freeze] anim-speed state, the worker's pose index
static Actor g_statue[brm::kMaxStatues] = {};
static Actor g_colossus[brm::kMaxCol] = {};
static Actor g_worker[brm::kMaxWork] = {};                                  // [br3w] people in white at the consoles
// [br3w] a stood stock vehicle (the pump cart): SpawnAIVehicle as sites.h / airport.h do, parked, faded when not wanted
struct VehStand { ap::WeakRef ref; bool want, bad, said, preloaded; float pos[3]; float heading; char model[32]; int mdl, cls, type, tries; uint32_t at; };
static VehStand g_veh[brm::kMaxVeh] = {};
static sp::Kind* g_kind = nullptr; static int g_kindTry = 0;

inline bool Quiet() { return g_inZone != 0; }                      // for the other features' `on` flags
inline bool Inside(int k) { return k >= 0 && k < 2 && g_inside[k]; }

static uint8_t* Player(int k) { return k == 0 ? *reinterpret_cast<uint8_t**>(g_base + eng::RVA_PLAYER1) : Player2Char(); }

static void LoadTable(const Config& cfg)
{
    char why[200] = "";
    g_tableOk = false;
    // an edited table next to the DLL wins (tuning the param lines needs no patch rebuild); else the built-in one
    char path[MAX_PATH]; std::snprintf(path, MAX_PATH, "%s%s", g_dir, cfg.backrooms_file[0] ? cfg.backrooms_file : "lcudrive_backrooms.txt");
    FILE* f = nullptr; fopen_s(&f, path, "rb");
    if (f)
    {
        static char buf[4000000];                 // [br4] the five-biome table is ~1.6 MB
        const size_t n = fread(buf, 1, sizeof buf - 1, f); fclose(f); buf[n] = 0;
        g_tableOk = brm::ParseText(g_data, buf, why, sizeof why);
        Log(g_tableOk ? ">>> backrooms: table %s - %d modules, %d connectors, %d exits\n" : "!!! backrooms: %s refused (%s) - the built-in table is used\n",
            path, g_tableOk ? g_data.nMods : 0, g_tableOk ? g_data.nStubs : 0, g_tableOk ? g_data.nExits : 0);
        if (!g_tableOk) Log("    reason: %s\n", why);
    }
    if (!g_tableOk)
    {
        g_tableOk = brm::ParseTable(g_data, brm::kTable, why, sizeof why);
        Log(g_tableOk ? ">>> backrooms: the built-in table - %d modules, %d connectors, %d exits, door %s\n" : "!!! backrooms: the built-in table is unusable (%s) - feature off\n",
            g_tableOk ? g_data.nMods : 0, g_data.nStubs, g_data.nExits, g_data.hasEntry ? "set" : "NOT set (menu entry only)");
        if (!g_tableOk) Log("    reason: %s\n", why);
    }
    g_state = brm::State();
}

// ---- the kill height -----------------------------------------------------------------------------
static int16_t* TerlowPtr(uint8_t* ch)
{
    uint8_t* lvl = reinterpret_cast<ChLevelFn>(g_base + FN_CHLEVEL)(ch);
    if (!PtrOk(lvl)) return nullptr;
    uint8_t* a = At<uint8_t*>(lvl, LVL_A); if (!PtrOk(a)) return nullptr;
    uint8_t* b = At<uint8_t*>(a, LVL_B);   if (!PtrOk(b)) return nullptr;
    return reinterpret_cast<int16_t*>(b + LVL_TERLOW);
}
static void TerlowRestore()
{
    if (g_terlowHeld && PtrOk(reinterpret_cast<uint8_t*>(g_terlowPtr))) *g_terlowPtr = g_terlowStock;
    g_terlowHeld = false; g_terlowPtr = nullptr;
}
static void TerlowHold(uint8_t* ch, float value)
{
    int16_t* p = TerlowPtr(ch);
    if (!p) { if (!(g_said & 1)) { g_said |= 1; Log("!!! backrooms: the level's kill height was not found - the fall would be fatal; not entering\n"); } return; }
    if (!g_terlowHeld) { g_terlowStock = *p; g_terlowPtr = p; g_terlowHeld = true;
        Log(">>> backrooms: kill height %d -> %.0f while a player is below (put back afterwards)\n", (int)g_terlowStock, value); }
    *p = static_cast<int16_t>(value);
}

// ---- moving a player ------------------------------------------------------------------------------
static bool Teleport(uint8_t* ch, const brm::Action& a)
{
    alignas(16) float pos[4] = { a.p.x, a.p.y, a.p.z, 1.0f };
    float vel[3] = {0, 0, 0}; uint8_t* phys = At<uint8_t*>(ch, CH_PHYS); const bool keep = a.keepVel && PtrOk(phys);
    if (keep) std::memcpy(vel, phys + PHYS_VEL, sizeof vel);
    const int refused = reinterpret_cast<TeleFn>(g_base + FN_TELEPORT)(ch, pos);
    if (refused) return false;
    if (a.b != 0.0f) reinterpret_cast<HeadFn>(g_base + FN_HEADING)(ch, static_cast<uint16_t>(static_cast<int>(a.a * (65536.0f / 360.0f)) & 0xFFFF), 1);
    if (keep) { phys = At<uint8_t*>(ch, CH_PHYS); if (PtrOk(phys) && IsFin(vel[0]) && IsFin(vel[1]) && IsFin(vel[2])) std::memcpy(phys + PHYS_VEL, vel, sizeof vel); }
    else if (a.zeroVel) { phys = At<uint8_t*>(ch, CH_PHYS); if (PtrOk(phys)) { const float z3[3] = {0, 0, 0}; std::memcpy(phys + PHYS_VEL, z3, sizeof z3); } }   // [fix1] held still
    return true;
}

// ---- [fix2] the camera through a connector ---------------------------------------------------------
//  Without this the camera, after a connector's move, stays between the two points a little longer than the character,
//  so the outside of the rooms shows for a few frames.  Two layers, both per player / per view:
//  1. GAME CAMERA - the engine's own cut, the recipe its respawn uses after moving a character out of view
//     (0x14066E9A8..0x14066E9BA [P disasm]): CameraReset 0x1407D5880(level = 0x1407EF080(ch), cam = [ch+0x7E0]).
//     The reset zeroes the camera's blend / lag state, sets mode +0x244 = 0xFFFF and the snap bit +0x2F2 |= 0x10
//     (also through 0x1406DB200); the camera update 0x140992830 hands that bit to its position solver 0x140991500
//     as argument 8, which then SKIPS the blend from +0x140 and the seek lag (0x14099153B jne 0x14099173F) and
//     writes the wanted position +0xF0, look-at +0x100 and +0x150 directly; the bit is cleared at the end of the
//     update (0x140993290).  [P disasm; that the next frame is the cut: I strong]
//     Mode 1 (ini portal_snap = 1) sets only the snap bit on both of the character's cameras ([ch+0x7E0] and
//     0x140669A20(ch)), as 0x1406DB290 does.  [P]
//  2. RENDER COPY - a safety net in RenderView::Begin's camera copy (Hook_CamCopy, before gfx's sway): every view
//     whose camera was following the player when he was moved keeps its offset to him (new player position + the
//     offset of its last pre-move frame) until the game's own camera is within portal_catch of that place, at most
//     0.6 s; the frames it corrected and the time it took are logged.  The black (blink bits) runs from the move
//     until the camera has caught up, at most blink_ms.
static const uintptr_t FN_CAMRESET = 0x7D5880, FN_GETCAM = 0x66B0F0, FN_GETCAM2 = 0x669A20;
static const uintptr_t CAMOBJ_FLAGS = 0x2F2, RCAM_POS = 0x30;   // RCAM_POS: the render copy's position (gfx.h CAM_POS) [P Notes 57]
static const uint8_t CAMOBJ_SNAP = 0x10;
static const uint8_t kSigReset[16] = { 0x80, 0x8B, 0xF2, 0x02, 0x00, 0x00, 0x10, 0x66, 0xC7, 0x83, 0x44, 0x02, 0x00, 0x00, 0xFF, 0xFF };   // at +0x47
static const uint8_t kSigGetCam[8] = { 0x48, 0x8B, 0x81, 0xE0, 0x07, 0x00, 0x00, 0xC3 };
static const uint8_t kSigGetCam2[12] = { 0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xD9, 0x48, 0x8D, 0x0D };
typedef void (__fastcall* CamResetFn)(void* level, void* cam);
typedef uint8_t* (__fastcall* GetCamFn)(uint8_t* ch);
static int  g_camSig = -1;                      // -1 not checked, 0 bytes differ (snap off), 1 ok
static volatile bool g_camFault = false;
static int CamSigOk()
{
    if (g_camSig < 0)
    {
        __try
        {
            g_camSig = (std::memcmp(reinterpret_cast<const void*>(g_base + FN_CAMRESET + 0x47), kSigReset, sizeof kSigReset) == 0 &&
                        std::memcmp(reinterpret_cast<const void*>(g_base + FN_GETCAM), kSigGetCam, sizeof kSigGetCam) == 0 &&
                        std::memcmp(reinterpret_cast<const void*>(g_base + FN_GETCAM2), kSigGetCam2, sizeof kSigGetCam2) == 0) ? 1 : 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER) { g_camSig = 0; }
        Log(g_camSig ? ">>> backrooms: the engine's camera cut is where expected (0x1407D5880 / [ch+0x7E0] / 0x140669A20) - used at every connector\n"
                     : "!!! backrooms: the camera cut functions do not look as expected - connectors rely on the render-copy hold and the black\n");
    }
    return g_camSig;
}
// returns 0 nothing done, 1 snap bit, 2 engine reset (+ bit)
static int CamSnapImpl(uint8_t* ch, int mode)
{
    if (mode <= 0 || CamSigOk() != 1) return 0;
    uint8_t* cam = reinterpret_cast<GetCamFn>(g_base + FN_GETCAM)(ch);
    int r = 0;
    if (PtrOk(cam) && mode >= 2)
    {
        uint8_t* lvl = reinterpret_cast<ChLevelFn>(g_base + FN_CHLEVEL)(ch);
        if (PtrOk(lvl)) { reinterpret_cast<CamResetFn>(g_base + FN_CAMRESET)(lvl, cam); r = 2; }
    }
    if (PtrOk(cam)) { cam[CAMOBJ_FLAGS] |= CAMOBJ_SNAP; if (!r) r = 1; }
    uint8_t* cam2 = reinterpret_cast<GetCamFn>(g_base + FN_GETCAM2)(ch);
    if (PtrOk(cam2) && cam2 != cam) cam2[CAMOBJ_FLAGS] |= CAMOBJ_SNAP;
    return r;
}
static int CamSnap(uint8_t* ch, int mode)
{
    if (g_camFault) return 0;
    __try { return CamSnapImpl(ch, mode); }
    __except (EXCEPTION_EXECUTE_HANDLER) { g_camFault = true; Log("!!! EXCEPTION in the Backrooms camera cut - off for the session (the render-copy hold and the black stay)\n"); return 0; }
}

// the move, published by the game thread for the render-side hold; the result, published back for the log
struct CamJump { float oldP[3], newP[3], d[3]; double t; int from, to, snap; };
static CamJump g_jump[2] = {};
static volatile LONG g_jumpSeq[2] = {0, 0};
struct CamRes { LONG seq; int frames, held; float ms, miss; bool caught, followed; };
static CamRes g_res[2] = {};
static volatile LONG g_resSeq[2] = {0, 0};
static LONG g_resLogged[2] = {0, 0};
static double QpcNow()
{
    static LARGE_INTEGER f = {};
    if (!f.QuadPart) QueryPerformanceFrequency(&f);
    LARGE_INTEGER c; QueryPerformanceCounter(&c);
    return f.QuadPart ? static_cast<double>(c.QuadPart) / static_cast<double>(f.QuadPart) : 0.0;
}
struct PView { void* key; float pos[3]; double at; bool used; LONG seen[2]; bool active[2]; float off[2][3]; int frames[2], held[2]; };
static PView g_pview[8] = {};
static volatile bool g_pviewFault = false;
static bool PlayerAt(int k, float out[3])
{
    uint8_t* ch = Player(k);
    if (!PtrOk(ch)) return false;
    for (int i = 0; i < 3; ++i) out[i] = At<float>(ch, CH_POS + 4 * i);
    return IsFin(out[0]) && IsFin(out[1]) && IsFin(out[2]);
}
static void PortalCamImpl(void* dst, const Config& cfg)
{
    uint8_t* c = static_cast<uint8_t*>(dst);
    if (!PtrOk(c)) return;
    const double now = QpcNow();
    PView* v = nullptr; PView* freeV = nullptr; PView* oldest = &g_pview[0];
    for (PView& q : g_pview)
    {
        if (q.used && q.key == dst) { v = &q; break; }
        if (q.used && now - q.at > 3.0) q = PView{};
        if (!q.used && !freeV) freeV = &q;
        if (q.at < oldest->at) oldest = &q;
    }
    if (!v)
    {
        v = freeV ? freeV : oldest; *v = PView{}; v->used = true; v->key = dst;
        v->seen[0] = g_jumpSeq[0]; v->seen[1] = g_jumpSeq[1];
        for (int i = 0; i < 3; ++i) v->pos[i] = At<float>(c, RCAM_POS + 4 * i);
        v->at = now; return;
    }
    float cp[3]; for (int i = 0; i < 3; ++i) cp[i] = At<float>(c, RCAM_POS + 4 * i);
    if (!IsFin(cp[0]) || !IsFin(cp[1]) || !IsFin(cp[2])) return;
    const float capS = 0.6f;                                              // the hold's own cap (the black's is blink_ms)
    for (int k = 0; k < 2; ++k)
    {
        const LONG seq = g_jumpSeq[k];
        if (seq != v->seen[k])
        {
            v->seen[k] = seq; v->active[k] = false;
            const CamJump& J = g_jump[k];
            float other[3]; const bool haveOther = PlayerAt(1 - k, other);
            const float dOld = std::sqrt((v->pos[0] - J.oldP[0]) * (v->pos[0] - J.oldP[0]) + (v->pos[1] - J.oldP[1]) * (v->pos[1] - J.oldP[1]) + (v->pos[2] - J.oldP[2]) * (v->pos[2] - J.oldP[2]));
            const float dOth = haveOther ? std::sqrt((v->pos[0] - other[0]) * (v->pos[0] - other[0]) + (v->pos[1] - other[1]) * (v->pos[1] - other[1]) + (v->pos[2] - other[2]) * (v->pos[2] - other[2])) : 1e9f;
            // this view followed player k: its last pre-move frame was near his old place (and nearer him than the partner)
            if (now - J.t < 1.0 && now - v->at < 0.5 && dOld < 12.0f && dOld <= dOth)
            {
                v->active[k] = true; v->frames[k] = 0; v->held[k] = 0;
                for (int i = 0; i < 3; ++i) v->off[k][i] = v->pos[i] - J.oldP[i];
            }
        }
        if (!v->active[k]) continue;
        const CamJump& J = g_jump[k];
        float pk[3]; if (!PlayerAt(k, pk)) { for (int i = 0; i < 3; ++i) pk[i] = J.newP[i]; }
        float want[3]; for (int i = 0; i < 3; ++i) want[i] = pk[i] + v->off[k][i];
        const float miss = std::sqrt((cp[0] - want[0]) * (cp[0] - want[0]) + (cp[1] - want[1]) * (cp[1] - want[1]) + (cp[2] - want[2]) * (cp[2] - want[2]));
        ++v->frames[k];
        const float el = static_cast<float>(now - J.t);
        const bool caught = miss < cfg.backrooms_portal_catch;
        if (caught || el > capS)
        {
            v->active[k] = false;
            CamRes r; r.seq = seq; r.frames = v->frames[k] - (caught ? 1 : 0); r.held = v->held[k]; r.ms = el * 1000.0f; r.miss = miss; r.caught = caught; r.followed = true;
            g_res[k] = r; InterlockedExchange(&g_resSeq[k], seq);
            continue;
        }
        if (cfg.backrooms_portal_hold)
        {   // the picture from where the camera should be: the game's rows, the corrected position
            for (int i = 0; i < 3; ++i) At<float>(c, RCAM_POS + 4 * i) = want[i];
            ++v->held[k];
        }
    }
    for (int i = 0; i < 3; ++i) v->pos[i] = cp[i];                       // the game's own camera, before any correction
    v->at = now;
}
// Hook_CamCopy calls this first (before gfx::Cam), on every render-view camera copy
static void PortalCam(void* dst, const Config& cfg)
{
    if (g_pviewFault || !g_tableOk) return;
    if (g_jumpSeq[0] == 0 && g_jumpSeq[1] == 0) return;               // nobody has crossed a connector yet: nothing to do
    __try { PortalCamImpl(dst, cfg); }
    __except (EXCEPTION_EXECUTE_HANDLER) { g_pviewFault = true; Log("!!! EXCEPTION in the Backrooms render-camera hold - off for the session\n"); }
}

// [fix1] what the engine says about player k right now (for the arrival's log lines)
struct Probe { bool ok; float y, vy; bool grounded, below; float gy; void* obj; void* level; };
static Probe ProbeOf(int k)
{
    Probe r = {};
    uint8_t* ch = Player(k);
    if (!PtrOk(ch)) return r;
    r.ok = true; r.y = At<float>(ch, CH_POS + 4);
    uint8_t* phys = At<uint8_t*>(ch, CH_PHYS); r.vy = PtrOk(phys) ? At<float>(phys, PHYS_VEL + 4) : 0.0f;
    r.grounded = reinterpret_cast<GroundFn>(g_base + FN_ONGROUND)(ch);
    uint8_t* probe = At<uint8_t*>(ch, CH_PROBE);
    if (PtrOk(probe)) { r.obj = At<void*>(probe, PROBE_GROUND_OBJ); r.gy = At<float>(probe, PROBE_GROUND_Y); r.below = PtrOk(static_cast<uint8_t*>(r.obj)); }
    r.level = wd::g_getLevel(ch + 0x10);                                       // the level the character belongs to [I]: changes when its cell owns him
    return r;
}
static void LogFall(const char* what, int who, const brm::Action& A)
{
    const Probe q = ProbeOf(who);
    Log("%s: player %d %s - at (%.2f, %.2f, %.2f) vy %.2f, on ground %d, engine probe %s y %.2f, level %p | floor %.2f, t %.1f s, a %.2f b %.2f\n",
        A.type == brm::A_TRACE ? "    backrooms" : ">>> backrooms", who + 1, what, A.p.x, A.p.y, A.p.z, q.vy, q.grounded ? 1 : 0,
        q.below ? "obj" : "none", q.gy, q.level, brm::ArrPt(g_data, g_state).y, g_state.pl[who].fallT, A.a, A.b);   // [br4] this visit's arrival room
}

// ---- statues and colossi --------------------------------------------------------------------------
static uint8_t* ActorChar(const Actor& s)
{
    if (!PtrOk(s.ch) || !wd::StillThere(s.watch)) return nullptr;
    if (!(At<uint8_t>(s.ch, 0x320) & 1) || At<void*>(s.ch, 0x650) != s.def) return nullptr;
    return s.ch;
}
static void ActorFreeze(uint8_t* ch)
{
    static const int one = 1;
    reinterpret_cast<dn::SetFlagFn>(g_base + dn::FN_SETFLAG)(ch, FLAG_BRAIN_OFF, true, &one);            // Character_SetAiOverride's flag: the brain does not run
    reinterpret_cast<dn::SetFlagFn>(g_base + dn::FN_SETFLAG)(ch, FLAG_NOT_PUSHABLE, true, &dn::kLayer0);
    reinterpret_cast<dn::SetFlagFn>(g_base + dn::FN_SETFLAG)(ch, dn::FLAG_INVULNERABLE, true, &dn::kLayer0);
    wd::LockInPlace(ch, "idle");                                                                         // IdleNoGravity: no gravity, no velocity
}
static void ActorGone(Actor& s) { if (uint8_t* ch = ActorChar(s)) dn::Kill(ch); wd::Unwatch(&s.watch); s = Actor{}; }
// [br3w] the workers' people (AITypes Character.Collectable / Character.Astronaut) and their quiet business; every anim
// is HasAnim-checked on the spawned character; none = the frozen idle stands
static const spm::Member kWorkerMember[] = { {"Collectable", "HazmatGuy"}, {"Astronaut", "Scientist01"}, {"Astronaut", "AstronautScientist"},
                                             {"Astronaut", "MissionController1"} };
static const char* const kWorkerAnim[] = { "Ponder", "CheckTime", "LookAtObject", "point" };
static int WorkerPose(uint8_t* n, int idx)          // [freeze] returns the pose index played (-1 = none: the idle stands)
{
    for (int k = 0; k < 4; ++k)
    {
        const int j = (idx + k) % 4;
        const char* a = kWorkerAnim[j];
        const int id = lf::Anim(a);
        if (id > 0 && id <= 0xFFFF && wd::HasAnim(n, id)) { wd::LockInPlace(n, a); return j; }
    }
    return -1;
}
// [freeze] the figures' anim speed (animfreeze_model.h): statues and colossi settle a moment then hold still for good;
// workers settle into their console pose, then hold it - with an optional new pose now and then while nobody looks.
static afz::Params g_fzP;
static void FreezeStep(Actor& s, uint8_t* ch, const char* what, float dt, bool observed, int idx)
{
    const afz::Kind k = what[0] == 'w' ? afz::kWorker : what[0] == 'c' ? afz::kColossus : afz::kStatue;
    const float cur = At<float>(ch, CH_ANIMSPEED);
    const afz::Out o = afz::Step(s.fz, k, g_fzP, dt, observed, IsFin(cur) ? cur : 1.0f);
    if (o.newPose)
    {
        const int next = WorkerPose(ch, s.pose < 0 ? idx : s.pose + 1);
        Log("    backrooms: worker %d changes pose unseen (%s, %.1f s of motion, then still again; pose change %d)\n",
            idx, next >= 0 ? kWorkerAnim[next] : "idle", g_fzP.repose_run, s.fz.reposes);
        s.pose = next;
    }
    if (cur != o.speed) reinterpret_cast<AnimSpeedFn>(g_base + FN_SETANIMSPEED)(ch, o.speed);
    const float back = At<float>(ch, CH_ANIMSPEED);
    if (o.justFrozen)
        Log(">>> backrooms: %s %d animation frozen (anim speed ch+0x8B0 %.2f -> %.2f after %.2f s%s; base advance byte ch+0x221 = %u%s)\n",
            what, idx, cur, back, s.fz.age, observed ? ", stopped as it came into view" : "",
            At<uint8_t>(ch, CH_BASEADVANCE), At<uint8_t>(ch, CH_BASEADVANCE) ? " - the base update ALSO advances it: tell me" : "");
    if (o.reset && !s.saidReset)
    {
        s.saidReset = true;
        Log("    backrooms: %s %d - the engine set its anim speed back to %.2f (a context change); held at 0 again (logged once per figure)\n", what, idx, cur);
    }
}
// one spawner call per world update, on this feature's turn only
static void ActorService(void* pm, void* lvl, Actor& s, const char* what, bool* spawned, const brm::Input& in, float dt, bool freeze)
{
    uint8_t* ch = ActorChar(s);
    if (!s.want) { if (PtrOk(s.ch)) ActorGone(s); return; }
    if (!ch)
    {
        if (*spawned || !spt::Mine(sitem::kTurnBackrooms)) return;
        static const spm::Member kMember[] = { {"Animatronics", "CrazyScientist"}, {"Animatronics", "SpaceMan"}, {"Animatronics", "CaveMan"},
                                               {"Animatronics", "ForestMan"}, {"Animatronics", "TribalChief"}, {"Animatronics", "TribalHunter"} };
        const bool worker = what[0] == 'w';                                                   // [br3w]
        const int wi = worker ? static_cast<int>(&s - g_worker) : 0;
        sp::Kind* k = worker ? sp::Resolve(kWorkerMember[(wi + (g_kindTry++ / 8)) % 4]) : sp::Resolve(kMember[(g_kindTry++) % 6]);
        if (!k || k->di < 0) return;
        alignas(16) float pos[4] = { s.pos[0], s.pos[1] + 0.05f, s.pos[2], 1.0f };
        const float r = s.yaw * 0.0174533f;
        alignas(16) float dir[4] = { std::sin(r), 0.0f, std::cos(r), 0.0f };
        int st = -1; *spawned = true;
        uint8_t* n = reinterpret_cast<dn::SpawnFn>(g_base + dn::FN_SPAWN)(pm, lvl, k->ci, k->ti, pos, dir, &st, true, false, wd::H("lcudrive_backrooms"));
        if (st != dnm::kOk || !PtrOk(n)) { if (!(g_said & 2)) { g_said |= 2; Log(">>> backrooms: a %s could not be spawned yet (status %d) - trying again\n", what, st); } return; }
        if (!wd::Watch(n, &s.watch)) { dn::Kill(n); return; }
        s.ch = n; s.def = At<void*>(n, 0x650); s.born = GetTickCount();
        s.fz = afz::St{}; s.pose = -1; s.saidReset = false;                                   // [freeze]
        ActorFreeze(n);
        if (worker) s.pose = WorkerPose(n, wi);                                               // [br3w] at the console, not looking up
        if (s.scale > 1.01f) reinterpret_cast<ScaleFn>(g_base + FN_SETSCALE)(n + 0x10, s.scale);
        if (what[0] == 'c') reinterpret_cast<AnimSpeedFn>(g_base + FN_SETANIMSPEED)(n, 0.0f);   // [br4] a colossus: the animation stopped from its first frame
        Log(">>> backrooms: a %s stands at (%.1f, %.1f, %.1f)%s\n", what, s.pos[0], s.pos[1], s.pos[2], s.scale > 1.01f ? " (scaled)" : "");
        return;
    }
    // keep it frozen where the model has it
    const float dx = At<float>(ch, CH_POS) - s.pos[0], dz = At<float>(ch, CH_POS + 8) - s.pos[2];
    // [br4] a colossus stands completely still: it is NEVER written after its spawn - no position,
    // heading, scale or pose (if the engine itself moved one it is logged once and left; the brick-built ones cannot move)
    if (what[0] == 'c')
    {
        if (dx * dx + dz * dz > 0.04f && !s.saidReset) { s.saidReset = true; Log("!!! backrooms: a scaled-actor colossus is %.2f u from where it was spawned - not corrected (colossi = 0 leaves only the brick-built ones)\n", std::sqrt(dx * dx + dz * dz)); }
    }
    else if (dx * dx + dz * dz > 0.04f)
    {
        brm::Action a = {}; a.p = brm::V3{ s.pos[0], s.pos[1] + 0.05f, s.pos[2] }; a.a = s.yaw < 0 ? s.yaw + 360.0f : s.yaw; a.b = 1.0f;
        Teleport(ch, a); ActorFreeze(ch);
    }
    // [freeze] hold the animation still (the speed is re-read and re-applied every update)
    if (freeze)
    {
        const brm::V3 q = { s.pos[0], s.pos[1], s.pos[2] };
        const int mod = brm::ModAt(g_data, q, -1);
        bool seen = false;
        if (mod >= 0) seen = brm::Observed(g_data, g_state, in, q, mod);
        else for (int k = 0; k < brm::kPlayers; ++k) if (in.pl[k].present && brm::Sees(in.pl[k], q)) seen = true;
        const int idx = what[0] == 'w' ? static_cast<int>(&s - g_worker) : what[0] == 'c' ? static_cast<int>(&s - g_colossus) : static_cast<int>(&s - g_statue);
        if (!s.fz.init) afz::Init(s.fz, what[0] == 'w' ? afz::kWorker : what[0] == 'c' ? afz::kColossus : afz::kStatue, g_fzP,
                                  static_cast<uint32_t>(reinterpret_cast<uintptr_t>(ch) >> 4) ^ s.born);
        FreezeStep(s, ch, what, dt, seen, idx);
    }
    else if (s.fz.init)
    {   // switched off (ini reload): the engine's own idle again
        reinterpret_cast<AnimSpeedFn>(g_base + FN_SETANIMSPEED)(ch, 1.0f);
        s.fz = afz::St{};
    }
}

// ---- [br3w] a stood vehicle: one spawner call per world update on this feature's turn; parked; a watchdog fades it
// if the engine moved it (a rail vehicle away from any track: never seen [U]) - then not tried again this session
static void* VehOf(const VehStand& v)
{
    if (!PtrOk(v.ref.ctl) || !PtrOk(v.ref.cell) || v.ref.ctl[1] <= 0) return nullptr;
    void* p = *v.ref.cell; return PtrOk(p) ? p : nullptr;
}
static void VehFade(VehStand& v, int i, const char* why)
{
    if (void* veh = VehOf(v))
    {
        if (veh != ::PlayerVehicle() && veh != ::Player2Vehicle())
            if (uint8_t* ud = VehicleUserData(veh))
            {
                struct { uint32_t a; uint8_t flags; } o = { 0, 4 };                              // VehicleAction_Destroy #FadeOut
                reinterpret_cast<ap::DestroyFn>((*reinterpret_cast<void***>(ud))[13])(ud, &o);
            }
        Log(">>> backrooms: the stood %s (%d) goes - %s\n", v.model, i, why);
    }
    ap::DropRef(v.ref);
}
static void VehService(void* pm, void* lvl, VehStand& v, int i, bool* spawned)
{
    void* veh = VehOf(v);
    if (!v.want) { if (veh || PtrOk(v.ref.ctl)) VehFade(v, i, "nobody in its room"); return; }
    if (veh)
    {
        float p[3];
        if (ap::VehPos(veh, p) && (std::fabs(p[0] - v.pos[0]) > 2.0f || std::fabs(p[2] - v.pos[2]) > 2.0f || p[1] < v.pos[1] - 2.0f) &&
            veh != ::PlayerVehicle() && veh != ::Player2Vehicle())
        {
            Log("!!! backrooms: the stood %s moved to (%.1f, %.1f, %.1f) by itself - taken away, not tried again this session\n", v.model, p[0], p[1], p[2]);
            VehFade(v, i, "it would not stay"); v.bad = true;
        }
        return;
    }
    if (v.bad || *spawned || !spt::Mine(sitem::kTurnBackrooms)) return;
    if (v.mdl < 0 || v.cls < 0)
    {
        v.mdl = wd::g_modelId(*reinterpret_cast<void**>(g_base + wd::RVA_MODEL_REG), v.model);
        if (v.mdl < 0 || !wd::g_classType(wd::H(v.model), &v.cls, &v.type))
        {
            Log(">>> backrooms: the game has no vehicle %s - nothing stands in its room\n", v.model); v.bad = true; return;
        }
    }
    alignas(16) float pos[4] = { v.pos[0], v.pos[1] + 0.3f, v.pos[2], 1.0f };
    const float r = v.heading * 0.0174533f;
    alignas(16) float f[4] = { std::sin(r), 0.0f, std::cos(r), 0.0f };
    int st = -1; ap::WeakRef out = {};
    *spawned = true;
    ap::Fn<ap::SpawnFn>(ap::FN_SPAWN)(pm, &out, lvl, v.cls, v.type, pos, f, &st, true, wd::H("lcudrive_backrooms"));
    if (st == 0 && PtrOk(out.ctl) && out.ctl[1] > 0)
    {
        v.ref = out; v.preloaded = false;
        if (void* nv = VehOf(v)) ap::Park(nv, true);
        Log(">>> backrooms: the stood %s (class %d type %d) is at (%.1f, %.1f, %.1f) heading %.0f, parked\n", v.model, v.cls, v.type, v.pos[0], v.pos[1], v.pos[2], v.heading);
        return;
    }
    if (PtrOk(out.ctl)) ap::DropRef(out);
    if (st == 1) { v.preloaded = true; return; }                                             // streaming: again next turn
    if (++v.tries >= 20) { Log(">>> backrooms: the stood %s could not be spawned (status %d) - given up for this session\n", v.model, st); v.bad = true; }
}

// ---- [br4] the easy way out: are player k's buttons down?  The controller of that player (pad.h's port choice), read
// directly (br::Tick runs once per world update, outside the per-player pad poll); the keyboard key counts for the
// player who has no controller of his own (player 1 when both have one).
static uint32_t g_easyMask[4] = {0, 0, 0, 0}; static int g_easyN = -1; static char g_easyStr[32] = "";
static bool EasyHeld(int k, const Config& cfg)
{
    if (g_easyN < 0 || std::strcmp(g_easyStr, cfg.backrooms_easy_buttons) != 0)
    {
        strncpy_s(g_easyStr, cfg.backrooms_easy_buttons, _TRUNCATE); g_easyN = 0;
        char tmp[32]; strncpy_s(tmp, g_easyStr, _TRUNCATE);
        char* ctx = nullptr;
        for (char* t = strtok_s(tmp, "+ ", &ctx); t && g_easyN < 4; t = strtok_s(nullptr, "+ ", &ctx)) { const uint32_t c = pad::Parse(t); if (c) g_easyMask[g_easyN++] = c; }
    }
    const int port = pad::PortFor(k), other = pad::PortFor(1 - k);
    bool down = false;
    if (g_easyN > 0 && port >= 0 && pad::g_getState)
    {
        XINPUT_STATE st;
        if (pad::g_getState(static_cast<DWORD>(port), &st) == ERROR_SUCCESS)
        {
            down = true;
            for (int i = 0; i < g_easyN; ++i)
            {
                const uint32_t c = g_easyMask[i];
                const bool d1 = (c & pad::kLT) ? st.Gamepad.bLeftTrigger > 128 : (c & pad::kRT) ? st.Gamepad.bRightTrigger > 128 : (st.Gamepad.wButtons & static_cast<WORD>(c)) != 0;
                if (!d1) down = false;
            }
        }
    }
    const bool keyMine = port < 0 || (other >= 0 && k == 0);
    if (!down && keyMine && cfg.backrooms_easy_key && (GetAsyncKeyState(cfg.backrooms_easy_key) & 0x8000))
    {   // only while the game has the keyboard
        DWORD pid = 0; GetWindowThreadProcessId(GetForegroundWindow(), &pid);
        if (pid == GetCurrentProcessId()) down = true;
    }
    return down;
}
// ---- [br4] the reward: studs into the bank - the pointer and the plain write wanted.h's TakeStuds uses (in game since
// 2026-10-25, logged as a bank before -> after line); here the same cell gets MORE.  Nothing else of the save is touched.
static void GiveStuds(int who, int n)
{
    if (n <= 0) { Log(">>> backrooms: the vault is reached (vault_studs = 0: no studs)\n"); return; }
    // [br5] a very large stud reward: the bank is a u64 (wanted.h; it can hold tens of billions,
    // far past 32 bits).  The deposit never takes it past kBankCap = 999,999,999,999 (twelve digits): it is clamped.
    static const uint64_t kBankCap = 999999999999ull;
    uint64_t* bank = *reinterpret_cast<uint64_t**>(g_base + 0x1C7E640);
    if (!PtrOk(reinterpret_cast<uint8_t*>(bank))) { Log(">>> backrooms: the vault is reached, but the stud bank was not found - no studs given\n"); return; }
    const uint64_t b0 = *bank;
    if (b0 >= kBankCap) { Log(">>> backrooms: the stud bank reads %llu (at or over %llu) - not touched\n", static_cast<unsigned long long>(b0), static_cast<unsigned long long>(kBankCap)); return; }
    const uint64_t add = (b0 + static_cast<uint64_t>(n) > kBankCap) ? kBankCap - b0 : static_cast<uint64_t>(n);
    *bank = b0 + add;
    Log(">>> backrooms: player %d reached the vault with every brick found - %d studs for the bank (%llu -> %llu)\n", who + 1, n,
        static_cast<unsigned long long>(b0), static_cast<unsigned long long>(*bank));
}

// ---- one world update -------------------------------------------------------------------------------
static void Tick(void* pm, float dt, const Config& cfg, bool on)
{
    if (!g_init) { g_init = true; LoadTable(cfg); }
    if (!g_tableOk) return;
    uint8_t* p1 = Player(0);
    void* lvl = PtrOk(p1) ? wd::g_getLevel(p1 + 0x10) : nullptr;
    const bool world = PtrOk(lvl) && wd::g_getPM(lvl) == pm;
    if (!world)
    {
        if (brm::Quiet(g_state) && !(g_said & 8)) { g_said |= 8; Log("!!! backrooms: the world check failed while a player was in the Backrooms (level %p) - the state is reset (logged once)\n", lvl); }
        if (g_terlowHeld) { g_terlowHeld = false; g_terlowPtr = nullptr; }
        g_state = brm::State(); InterlockedExchange(&g_inZone, 0); g_inside[0] = g_inside[1] = false; return;
    }
    brm::Input in;
    in.dt = dt; in.enable = on && cfg.backrooms_enable != 0; in.statues = cfg.backrooms_statues != 0; in.mission = wd::MissionRunning();
    const uint32_t now = GetTickCount();
    for (int k = 0; k < 2; ++k)
    {
        uint8_t* ch = Player(k);
        brm::PlayerIn& pi = in.pl[k];
        if (!PtrOk(ch)) continue;
        const float x = At<float>(ch, CH_POS), y = At<float>(ch, CH_POS + 4), z = At<float>(ch, CH_POS + 8);
        if (!IsFin(x) || !IsFin(y) || !IsFin(z)) continue;
        pi.present = true; pi.pos = brm::V3{ x, y, z };
        pi.onFoot = !PtrOk(At<void*>(ch, CH_RIDE));
        pi.grounded = reinterpret_cast<GroundFn>(g_base + FN_ONGROUND)(ch);
        uint8_t* probe = At<uint8_t*>(ch, CH_PROBE);
        if (PtrOk(probe) && PtrOk(At<void*>(probe, PROBE_GROUND_OBJ))) { const float gy = At<float>(probe, PROBE_GROUND_Y); if (IsFin(gy)) { pi.groundBelow = true; pi.groundY = gy; } }
        // the player's camera: the nearest live view's camera; it looks at the player (third-person follow) [I]
        float best = 64.0f;                                  // within 8 u
        for (const wx::View& v : wx::g_view)
        {
            if (!v.used || !PtrOk(v.key) || now - v.seen > 500) continue;
            uint8_t* view = static_cast<uint8_t*>(v.key);
            uint8_t* cam = At<uint8_t>(view, wx::VIEW_CAM_A_ON) ? view + wx::VIEW_CAM_A : At<uint8_t>(view, wx::VIEW_CAM_B_ON) ? view + wx::VIEW_CAM_B : nullptr;
            if (!cam) continue;
            const float cx = At<float>(cam, wx::CAM_POS), cy = At<float>(cam, wx::CAM_POS + 4), cz = At<float>(cam, wx::CAM_POS + 8);
            const float d2 = (cx - x) * (cx - x) + (cy - y) * (cy - y) + (cz - z) * (cz - z);
            if (!IsFin(d2) || d2 >= best || d2 < 0.01f) continue;
            best = d2; const float l = std::sqrt(d2);
            pi.viewOk = true; pi.cam = brm::V3{ cx, cy, cz }; pi.fwd = brm::V3{ (x - cx) / l, (y + 0.3f - cy) / l, (z - cz) / l };
        }
    }
    for (int k = 0; k < 2; ++k) in.easy[k] = in.pl[k].present && g_state.pl[k].st != brm::PS_OUT && EasyHeld(k, cfg);   // [br4]
    if (cfg.backrooms_visit != g_lastVisit) { g_lastVisit = cfg.backrooms_visit; if (cfg.backrooms_visit) in.visit[0] = in.visit[1] = true; }
    if (cfg.backrooms_leave != g_lastLeave) { g_lastLeave = cfg.backrooms_leave; if (cfg.backrooms_leave) in.leave[0] = in.leave[1] = true; }
    for (int k = 0; k < 2; ++k) if (InterlockedExchange(&g_leaveReq[k], 0)) in.leave[k] = true;   // [station1] only that player
    brm::Step(g_data, g_state, in, (static_cast<uint64_t>(now) << 20) ^ __rdtsc());
    for (int a = 0; a < g_state.nAct; ++a)
    {
        const brm::Action& A = g_state.act[a];
        uint8_t* ch = (A.who >= 0 && A.who < 2) ? Player(A.who) : nullptr;
        switch (A.type)
        {
        case brm::A_TERLOW:
            if (A.b != 0.0f) { TerlowRestore(); Log(">>> backrooms: nobody is below any more - the kill height is back at %d\n", (int)g_terlowStock); }
            else if (PtrOk(ch)) TerlowHold(ch, A.a);
            break;
        case brm::A_TELEPORT:
        {
            const bool portal = A.keepVel && A.who >= 0 && A.who < 2;      // a connector crossing (the only keepVel move)
            float before[3] = {0, 0, 0}; const bool hadBefore = portal && PtrOk(ch) && PlayerAt(A.who, before);
            if (PtrOk(ch) && !Teleport(ch, A)) { if (!(g_said & 4)) { g_said |= 4; Log(">>> backrooms: the game refused a teleport (CanTeleport) - the model tries again\n"); } break; }
            if (portal && hadBefore)
            {   // [fix2] the camera goes with him: the engine's cut now, the render-copy hold from the next drawn frame
                const int snap = CamSnap(ch, cfg.backrooms_portal_snap);
                CamJump& J = g_jump[A.who];
                float after[3]; if (!PlayerAt(A.who, after)) { after[0] = A.p.x; after[1] = A.p.y; after[2] = A.p.z; }
                for (int i = 0; i < 3; ++i) { J.oldP[i] = before[i]; J.newP[i] = after[i]; J.d[i] = after[i] - before[i]; }
                J.t = QpcNow(); J.from = A.i; J.to = g_state.pl[A.who].cameBy; J.snap = snap;
                InterlockedIncrement(&g_jumpSeq[A.who]);
                if (cfg.backrooms_portal_black && cfg.backrooms_blink_ms > 0) g_blinkUntil[A.who] = now + static_cast<uint32_t>(cfg.backrooms_blink_ms);
            }
            break;
        }
        case brm::A_RESPAWN:
            if (PtrOk(ch)) { const float q[3] = { A.p.x, A.p.y, A.p.z }; reinterpret_cast<RespawnFn>(g_base + FN_RESPAWN)(ch, q); }
            break;
        case brm::A_BLINK:
            if (A.a <= 0.0f) break;                                        // [fix2] a crossing's black is set with its move (until the camera caught up)
            if (A.who >= 0 && A.who < 2) g_blinkUntil[A.who] = now + static_cast<uint32_t>(A.a * 1000.0f);
            break;
        case brm::A_STATUE_SPAWN: case brm::A_STATUE_MOVE:
            if (A.who >= 0 && A.who < brm::kMaxStatues) { Actor& s = g_statue[A.who]; s.want = true; s.pos[0] = A.p.x; s.pos[1] = A.p.y; s.pos[2] = A.p.z; s.yaw = A.a; s.scale = 1.0f; }
            break;
        case brm::A_STATUE_REMOVE:
            if (A.who >= 0 && A.who < brm::kMaxStatues) g_statue[A.who].want = false;
            break;
        case brm::A_COLOSSUS_SPAWN:
            if (A.who >= 0 && A.who < brm::kMaxCol && cfg.backrooms_colossi) { Actor& s = g_colossus[A.who]; s.want = true; s.pos[0] = A.p.x; s.pos[1] = A.p.y; s.pos[2] = A.p.z; s.yaw = A.a; s.scale = A.b; }
            break;
        case brm::A_COLOSSUS_REMOVE:
            if (A.who >= 0 && A.who < brm::kMaxCol) g_colossus[A.who].want = false;
            break;
        case brm::A_WORKER_SPAWN:                                                             // [br3w]
            if (A.who >= 0 && A.who < brm::kMaxWork && cfg.backrooms_workers) { Actor& s = g_worker[A.who]; s.want = true; s.pos[0] = A.p.x; s.pos[1] = A.p.y; s.pos[2] = A.p.z; s.yaw = A.a; s.scale = 1.0f; }
            break;
        case brm::A_WORKER_REMOVE:
            if (A.who >= 0 && A.who < brm::kMaxWork) g_worker[A.who].want = false;
            break;
        case brm::A_VEH_SPAWN:
            if (A.who >= 0 && A.who < brm::kMaxVeh && A.who < g_data.nVeh && cfg.backrooms_vehicles)
            {
                VehStand& v = g_veh[A.who];
                if (!v.model[0]) { strncpy_s(v.model, g_data.veh[A.who].model, _TRUNCATE); v.mdl = v.cls = v.type = -1; }
                v.want = true; v.pos[0] = A.p.x; v.pos[1] = A.p.y; v.pos[2] = A.p.z; v.heading = A.a;
            }
            break;
        case brm::A_VEH_REMOVE:
            if (A.who >= 0 && A.who < brm::kMaxVeh) g_veh[A.who].want = false;
            break;
        case brm::A_REWARD:                                                                   // [br4]
            GiveStuds(A.who, cfg.backrooms_vault_studs);                                      // [br5] vault_studs (goal_studs is no longer read)
            break;
        case brm::A_BIKE_SPAWN:
            Log(">>> backrooms: a bicycle would be waiting at (%.1f, %.1f, %.1f) - the vehicle spawn is INTEGRATION.md step B4 (not wired)\n", A.p.x, A.p.y, A.p.z);
            break;
        case brm::A_LOG:
            switch (A.i)
            {
            case 1:  Log(">>> backrooms: player %d has landed in the Backrooms (visit %u, on ground, after %.1f s, %d retries)\n", A.who + 1, g_state.visit, A.a, (int)A.b); break;
            case 8:  Log(">>> backrooms: player %d has landed in the Backrooms (visit %u, by height: the engine's on-ground never said so, after %.1f s, %d retries)\n", A.who + 1, g_state.visit, A.a, (int)A.b); break;
            case 2:  Log("!!! backrooms: player %d never landed in %.0f s (%d retries) - the arrival floor never held him; back to the surface\n", A.who + 1, A.a, (int)A.b); LogFall("gave up", A.who, A); break;
            case 3:  Log(">>> backrooms: player %d was outside every room - put back in the arrival room\n", A.who + 1); break;
            case 5:  LogFall("is held over the arrival floor (screen black)", A.who, A); break;
            case 6:  LogFall("is let go", A.who, A); break;
            case 7:  LogFall("went through the floor / did not settle (cell not loaded?) - held again", A.who, A); break;
            case 9:  Log(">>> backrooms: player %d left during the arrival (menu Leave / switched off)\n", A.who + 1); break;
            case 10: Log(">>> backrooms: player %d stands at the surface point after %.1f s - outside again\n", A.who + 1, A.b); break;
            case 11: Log(">>> backrooms: player %d - the surface point did not report ground in %.0f s; released anyway at y %.2f\n", A.who + 1, A.b, A.p.y); break;
            // [br4]
            case 12: Log(">>> backrooms: player %d found a gold brick - %d of %d (at %.1f, %.1f, %.1f)\n", A.who + 1, (int)A.a, (int)A.b, A.p.x, A.p.y, A.p.z); break;
            case 13: Log(">>> backrooms: every gold brick of this visit is found (%d) - the next plain door leads to the vault (this visit's EXIT trail: %d doors)\n", (int)A.a, g_state.trailNeed); break;
            case 14: Log(">>> backrooms: player %d walked into the vault\n", A.who + 1); break;
            case 15: Log(">>> backrooms: player %d held the easy way out - both players leave\n", A.who + 1); break;
            case 16: Log(">>> backrooms: player %d walked from %s into %s\n", A.who + 1, g_data.biomes[(int)A.a & 7], g_data.biomes[(int)A.b & 7]); break;
            case 17: Log("    backrooms: player %d is shown how to leave (%s)\n", A.who + 1, A.a > 0.5f ? "a long time in one room" : "the reminder"); break;
            case 18: Log("!!! backrooms: connector %d could not keep its sign's promise (no free room of its kind) - tell me\n", (int)A.a); break;
            case 19: Log(">>> backrooms: player %d was taken out by the game (at %.1f, %.1f, %.1f: the police station, a respawn or a new world) - outside now, camcorder off\n", A.who + 1, A.p.x, A.p.y, A.p.z); break;
            default: Log(">>> backrooms: player %d leaves the Backrooms\n", A.who + 1); break;
            }
            break;
        case brm::A_TRACE:
            if (cfg.backrooms_trace) LogFall(A.a > 0.5f ? "falling" : "held", A.who, A);
            break;
        }
    }
    bool spawned = false;
    g_fzP.worker_repose = cfg.backrooms_worker_repose != 0;                                  // [freeze]
    const bool fz = cfg.backrooms_freeze != 0;
    for (int i = 0; i < brm::kMaxStatues; ++i) ActorService(pm, lvl, g_statue[i], "statue", &spawned, in, dt, fz);
    for (int i = 0; i < brm::kMaxCol; ++i) ActorService(pm, lvl, g_colossus[i], "colossus", &spawned, in, dt, fz);
    for (int i = 0; i < brm::kMaxWork; ++i) ActorService(pm, lvl, g_worker[i], "worker", &spawned, in, dt, fz);     // [br3w]
    for (int i = 0; i < brm::kMaxVeh; ++i) VehService(pm, lvl, g_veh[i], i, &spawned);
    for (int k = 0; k < 2; ++k) g_inside[k] = brm::PlayerInside(g_state, k);
    InterlockedExchange(&g_inZone, brm::Quiet(g_state) ? 1 : 0);
    // [fix2] a crossing's result from the render side: end the black, one log line per crossing
    for (int k = 0; k < 2; ++k)
    {
        const LONG rs = g_resSeq[k];
        if (rs != 0 && rs != g_resLogged[k])
        {
            g_resLogged[k] = rs;
            const CamRes r = g_res[k]; const CamJump& J = g_jump[k];
            if (rs == g_jumpSeq[k] && r.caught) g_blinkUntil[k] = 0;
            Log(">>> backrooms: player %d through connector %d -> %d (moved %.1f, %.1f, %.1f): camera %s; the view %s after %d frame(s) / %.0f ms, %d frame(s) held by the render copy\n",
                k + 1, J.from, J.to, J.d[0], J.d[1], J.d[2], J.snap == 2 ? "cut (engine reset)" : J.snap == 1 ? "snap bit" : "not cut",
                r.caught ? "caught up" : "had NOT caught up (cap)", r.frames, r.ms, r.held);
            if (!r.caught) Log("    backrooms: the camera was still %.2f u from where it belongs when the hold ended - tell me\n", r.miss);
        }
        else if (g_jumpSeq[k] != 0 && g_jumpSeq[k] != g_resLogged[k] && g_jumpSeq[k] != g_resSeq[k] && QpcNow() - g_jump[k].t > 1.0)
        {
            g_resLogged[k] = g_jumpSeq[k];
            Log(">>> backrooms: player %d through connector %d -> %d: no player view was following him (camera %s; nothing held)\n",
                k + 1, g_jump[k].from, g_jump[k].to, g_jump[k].snap == 2 ? "cut (engine reset)" : g_jump[k].snap == 1 ? "snap bit" : "not cut");
        }
    }
    // the shader floats
    const bool blink[2] = { now < g_blinkUntil[0], now < g_blinkUntil[1] };
    brm::ShaderOut o = brm::ShaderFloats(g_data, g_state, in, cfg.backrooms_handicam != 0, cfg.backrooms_pillarbox != 0, cfg.backrooms_intensity, blink);
    if (!cfg.backrooms_effects)
    {   // [fix1] the effects off: still black through the arrival (blink bits only; the shader's plain path draws black)
        const int bits = static_cast<int>(o.flags) & (3 << 9);
        const float players = o.players;
        o = brm::ShaderOut{};
        if (bits) { o.flags = static_cast<float>(bits); o.players = players; }
    }
    if (!cfg.backrooms_hud) { o.goal = 0; o.hud = 0; }                                         // [br4]
    if (cfg.bridge) vsim::BridgePublishBackrooms(g_bridgeP, g_bridge, o.zone[0], o.zone[1], o.flags, o.secs, o.players, o.goal, o.hud);
}

// lcudrive unhooking / a fault: let go of everything that is ours
static void Release()
{
    TerlowRestore();
    for (Actor& s : g_statue) if (PtrOk(s.ch)) ActorGone(s);
    for (Actor& s : g_colossus) if (PtrOk(s.ch)) ActorGone(s);
    for (Actor& s : g_worker) if (PtrOk(s.ch)) ActorGone(s);                                // [br3w]
    for (int i = 0; i < brm::kMaxVeh; ++i) { g_veh[i].want = false; VehFade(g_veh[i], i, "lcudrive lets go"); }
    InterlockedExchange(&g_inZone, 0); g_inside[0] = g_inside[1] = false;
    if (g_bridge.phase == vsim::kBridgeLocked) vsim::BridgePublishBackrooms(g_bridgeP, g_bridge, 0, 0, 0, 0, 0, 0, 0);
}

} // namespace br
