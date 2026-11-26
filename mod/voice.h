// =============================================================================================
//  voice.h - the cut voice lines, played through the game's own dialogue system (Notes\46 §4).
//  voice_model.h decides which line and when; this file watches the players and plays the line.
//
//  How the game plays a conversation (read 2026-11-15, LC_Conversations at [0x1417EC8C0]) [P]:
//    LoadConversationManifest  0x140108420 -> 0x1400C56A0(dlg, owner, name, 1): queues the async job
//      "LC_Conversations::DoLoadConversationManifest" 0x1400C3150, which ALWAYS reads
//      stuff\Conversations\flow\<name>.txt (no per-level list: any manifest, any level) and loads
//      Audio\Samples\VO\<lang>\dialogue\<PAC_FILE_NAME>_pc.pak. owner null = the current world's
//      root level (0x1407F1D00). A refcount (+0xA0) per load.
//    ConversationManifestLoaded 0x1400C51A0(dlg, name) / ...PAKIsLoaded 0x1400C2AF0(dlg, name).
//    AddCharacterToManifest    0x1400C2180(dlg, name, slot, ch + 0x10): the speaker of the lines
//      with that slot (a weak ref - the character's +0x1A0 ring). A script Character holds ch + 0x10
//      (the actions test [p + 0xC4] & 0x1000, then use p - 0x10). AddDRCCharacterToManifest
//      0x1400C2330 is the same with the Wii U pad-speaker flag (the PC calls use it).
//    PlayConversation          0x1400C6780(dlg, conv, 0): marks it state 2; the dialogue update
//      0x1400C8500 starts each line (3D at the speaker, falloff 5..6 u unless set) and lip-syncs it
//      (LIP_ANIM_SET_NAME), subtitle key = the sample name in text.csv.
//    SetBKConversationFallOff  0x1400C7B60(dlg, conv, line, min, max): line +0xC8 / +0xCC.
//    ConversationLineFinished  0x1400C53D0(dlg, conv, line); StopConversation 0x1400C8010(dlg, conv);
//    UnloadConversationManifest 0x1400C8170(dlg, name, 1): refcount - 1, freed at 0.
//  The moments:
//    the shark   - the Drowning context (id at 0x14116AD50, registered 0x140680C4A) or a death within
//                  4 s of being in the water, while deadly_deep_water (0x141C9D708) > 0 [I: which of
//                  the two the shark is]; the line once he's back on his feet.
//    a call-in   - the DropPoint context (id at 0x140FEECAC, registered 0x14002B1B1; the vehicle drop
//                  menu's own context, Notes\41 §2) [P register, I that entering it = using one].
//    a call      - 0x14183F768, "missed video call" (PlayerMissedVideoCall 0x140256100 reads it; set by
//                  0x140249D20 6 HUD frames after 0x14183F760, which 0x140248770 sets when the ring
//                  counters reach 3) [P code, I meaning].
//    a swim      - the Swimming context (0x14116ACFC, wanted.h).
//    passers-by  - life.h calls PedSay at the pickpocket's end, a gift, a crash's onlookers.
//  An exception switches it off for the session (RunVoice, lcudrive.cpp).
// =============================================================================================
#pragma once
#include "voice_model.h"

namespace vc {

using wd::Fn;

static volatile bool g_fault = false;
static volatile bool g_stopReq = false;   // unhook: stop and unload on the next tick

static const uintptr_t RVA_DLG = 0x17EC8C0;
static const uintptr_t FN_LOAD = 0x0C56A0, FN_ADDCHAR = 0x0C2180, FN_PAKOK = 0x0C2AF0, FN_MANOK = 0x0C51A0,
                       FN_PLAY = 0x0C6780, FN_STOP = 0x0C8010, FN_UNLOAD = 0x0C8170, FN_LINEDONE = 0x0C53D0,
                       FN_FALLOFF = 0x0C7B60;
static const uintptr_t CTX_DROWNING = 0x116AD50, CTX_DROPPOINT = 0xFEECAC;
static const uintptr_t RVA_DEADLY = 0x1C9D708, RVA_CALL_MISSED = 0x183F768;

typedef void* (__fastcall* LoadFn)(void* dlg, void* owner, const char* name, bool keep);
typedef void  (__fastcall* AddCharFn)(void* dlg, const char* name, int slot, void* who);
typedef bool  (__fastcall* NameOkFn)(void* dlg, const char* name);
typedef void  (__fastcall* PlayFn)(void* dlg, const char* conv, bool flag);
typedef void  (__fastcall* NameFn)(void* dlg, const char* name);
typedef void  (__fastcall* UnloadFn)(void* dlg, const char* name, bool keep);
typedef bool  (__fastcall* LineDoneFn)(void* dlg, const char* conv, int line);
typedef void  (__fastcall* FallOffFn)(void* dlg, const char* conv, int line, float mn, float mx);

static void* Dlg() { void* d = *reinterpret_cast<void**>(g_base + RVA_DLG); return PtrOk(d) ? d : nullptr; }

static bool InCtx(uint8_t* c, uintptr_t ctxRva)
{
    if (!PtrOk(c)) return false;
    const int id = *reinterpret_cast<const int*>(g_base + ctxRva);
    return id != -1 && wd::IsIn(c, ctxRva);
}

// A character the dialogue system can take: constructed, and its +0x10 base carries the character flag.
static bool CanSpeak(uint8_t* ch)
{
    return PtrOk(ch) && (At<uint8_t>(ch, 0x320) & 1) && (At<uint32_t>(ch + 0x10, 0xC4) & 0x1000);
}

static uint8_t* PlayerCh(int i)
{
    if (i == 1) return Player2Char();
    uint8_t* p = *reinterpret_cast<uint8_t**>(g_base + eng::RVA_PLAYER1);
    if (!PtrOk(p)) p = *reinterpret_cast<uint8_t**>(g_base + eng::RVA_PLAYERS);
    return PtrOk(p) ? p : nullptr;
}

// Is any conversation playing (states 2-7 of the line player 0x1400C8500)? The manifests hang off
// dlg+0x210 (next +8), their conversations off +0xC8 (next +8), the state at +0x40 [P, Play 0x1400C6780].
static bool AnyTalking(void* dlg, const char* except)
{
    int guard = 0;
    for (uint8_t* m = At<uint8_t*>(dlg, 0x210); PtrOk(m) && guard < 4096; m = At<uint8_t*>(m, 8), ++guard)
    {
        bool mine = false;
        if (except)
        {
            uint8_t* nm = At<uint8_t*>(m, 0xB8);                       // the name record: +0x10 -> the text
            const char* s = PtrOk(nm) ? At<const char*>(nm, 0x10) : nullptr;
            mine = PtrOk(s) && _stricmp(s, except) == 0;
        }
        if (mine) continue;
        for (uint8_t* c = At<uint8_t*>(m, 0xC8); PtrOk(c) && guard < 4096; c = At<uint8_t*>(c, 8), ++guard)
        {
            const int st = At<int32_t>(c, 0x40);
            if (st >= 2 && st <= 7) return true;
        }
    }
    return false;
}

// ---- the requests ---------------------------------------------------------------------------------
struct Req
{
    uint8_t     kind;
    int         player;        // 0 / 1 = that player says it; -1 = a passer-by (ch, watched)
    uint8_t*    ch;
    void*       def;
    wd::SP      watch;
    const char* manifest;
    const char* conv;
    const char* text;
    uint8_t     region;
    vm::Job     job;
};
static const int kMaxQ = 6;
static Req       g_q[kMaxQ];
static int       g_nq = 0;

static vm::Params    g_p;
static vm::PState    g_ps[2];
static vm::PhoneState g_phone;
static vm::PedState  g_ped;
static vm::JobLimits g_lim;
static bool          g_on = false;
static bool          g_phoneInit = false; // the missed-call byte read once before its edges count
static float         g_now = 0.0f;       // s of game updates this session
static float         g_hear = 30.0f, g_falloff = 40.0f;
static uint32_t      g_rng = 0x9E3779B9u;
static uint32_t Rnd() { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 17; g_rng ^= g_rng << 5; return g_rng; }

static void Pop(bool release)
{
    if (g_nq <= 0) return;
    if (release && g_q[0].player < 0) wd::Unwatch(&g_q[0].watch);
    for (int i = 1; i < g_nq; ++i) g_q[i - 1] = g_q[i];
    --g_nq;
}

static bool Push(const Req& r)
{
    if (g_nq >= kMaxQ) return false;
    g_q[g_nq++] = r;
    return true;
}

static void SayChase(int kind, int player, int idx)
{
    const vm::ChaseLine& l = vm::kChase[kind];
    Req r = {};
    r.kind = static_cast<uint8_t>(kind); r.player = player; r.manifest = l.manifest;
    r.conv = l.conv[idx % l.n]; r.text = r.conv;
    if (Push(r)) Log(">>> voice: player %d - %s line queued (%s)\n", player + 1, vm::kKindName[kind], r.conv);
}

// ---- life.h's people (called from lf::Tick, game thread) --------------------------------------------
static void PedSay(int kind, uint8_t* ch, bool female)
{
    if (!g_on || g_fault || g_stopReq || !vm::PedMay(g_p, g_ped, kind, g_now) || !PtrOk(ch) || !CanSpeak(ch)) return;
    for (int i = 0; i < g_nq; ++i) if (g_q[i].player < 0 && g_q[i].ch == ch) return;     // one line each at a time
    // someone must be near enough to hear it
    float best = 1e9f;
    for (int i = 0; i < 2; ++i)
        if (uint8_t* p = PlayerCh(i)) { float q[3]; for (int k = 0; k < 3; ++k) q[k] = At<float>(p, 0xA8 + 4 * k); const float d = wd::DistCh(ch, q); if (d < best) best = d; }
    if (best > g_hear) return;
    const char* def = CharDefName(ch);
    const vm::Pick pk = vm::PickPed(kind, female, vm::RegionOf(static_cast<int>(rd::g_district), def),
                                    vm::Mix(reinterpret_cast<uintptr_t>(ch)), Rnd());
    if (!pk.manifest) return;
    Req r = {};
    r.kind = static_cast<uint8_t>(kind); r.player = -1; r.ch = ch; r.def = At<void*>(ch, 0x650);
    r.manifest = pk.manifest; r.conv = pk.conv; r.text = pk.text; r.region = pk.region;
    if (!wd::Watch(ch, &r.watch)) return;
    if (!Push(r)) { wd::Unwatch(&r.watch); return; }
    g_ped.last = g_now;
    Log(">>> voice: %s - %s (%s, %s %s) says \"%s\" (%.0f u from a player)\n", vm::kKindName[kind], def,
        pk.manifest, female ? "female" : "male", vm::kRegionName[pk.region], pk.text, best);
}

// ---- the dialogue system ----------------------------------------------------------------------------
static uint8_t* Speaker(const Req& r)
{
    if (r.player >= 0) { uint8_t* p = PlayerCh(r.player); return CanSpeak(p) ? p : nullptr; }
    if (!wd::StillThere(r.watch) || !PtrOk(r.ch) || !(At<uint8_t>(r.ch, 0x320) & 1) || At<void*>(r.ch, 0x650) != r.def) return nullptr;
    return CanSpeak(r.ch) ? r.ch : nullptr;
}

static void Process(void* dlg, float dt)
{
    // the waiting ones go stale: a passer-by's moment is gone after a few seconds
    for (int i = g_nq - 1; i >= 1; --i)
    {
        g_q[i].job.age += dt;
        if (g_q[i].job.age > (g_q[i].player < 0 ? 6.0f : 60.0f))
        {
            Log("    voice: %s line dropped (waited %.0f s)\n", vm::kKindName[g_q[i].kind], g_q[i].job.age);
            if (g_q[i].player < 0) wd::Unwatch(&g_q[i].watch);
            for (int k = i + 1; k < g_nq; ++k) g_q[k - 1] = g_q[k];
            --g_nq;
        }
    }
    if (g_nq <= 0) return;
    Req& r = g_q[0];
    uint8_t* who = Speaker(r);
    vm::JobIn in;
    in.speaker = who != nullptr;
    if (r.job.st == vm::kJobLoading || r.job.st == vm::kJobWaitQuiet)
    {
        in.ready = Fn<NameOkFn>(FN_MANOK)(dlg, r.manifest) && Fn<NameOkFn>(FN_PAKOK)(dlg, r.manifest);
        in.busy = in.ready && r.player >= 0 && AnyTalking(dlg, r.manifest);   // Chase waits for a story line
    }
    if (r.job.st == vm::kJobPlaying) in.finished = Fn<LineDoneFn>(FN_LINEDONE)(dlg, r.conv, 0);
    const vm::JobState before = r.job.st;
    const vm::JobAct a = vm::StepJob(r.job, g_lim, in, dt);
    switch (a)
    {
    case vm::kActLoad:
        Fn<LoadFn>(FN_LOAD)(dlg, nullptr, r.manifest, true);
        break;
    case vm::kActPlay:
        Fn<AddCharFn>(FN_ADDCHAR)(dlg, r.manifest, 0, who + 0x10);
        Fn<FallOffFn>(FN_FALLOFF)(dlg, r.conv, 0, g_falloff * 0.25f, g_falloff);
        Fn<PlayFn>(FN_PLAY)(dlg, r.conv, false);
        Log("    voice: playing %s (%s) from %s%s\n", r.conv, vm::kKindName[r.kind],
            r.player >= 0 ? (r.player ? "player 2" : "player 1") : CharDefName(who),
            r.job.age > g_lim.quietS ? " (after waiting for another conversation)" : "");
        break;
    case vm::kActUnload:
        if (before == vm::kJobLoading || before == vm::kJobWaitQuiet)
            Log("    voice: %s line not played (%s)\n", vm::kKindName[r.kind], in.speaker ? "the line didn't load in time" : "the speaker is gone");
        else Log("    voice: %s done after %.1f s%s\n", r.conv, r.job.age, r.job.finished ? "" : " (no finish reported - timed out)");
        Fn<UnloadFn>(FN_UNLOAD)(dlg, r.manifest, true);
        Pop(true);
        break;
    case vm::kActDrop:
        Pop(true);
        break;
    default: break;
    }
}

// Everything back: the line playing stopped, our loads released. world = true: a new world, the
// old pointers mean nothing (nothing is released, as life.h's ForgetAll).
static void StopAll(bool world)
{
    void* dlg = Dlg();
    if (world || !dlg) { g_nq = 0; return; }
    while (g_nq > 0)
    {
        Req& r = g_q[0];
        if (r.job.st == vm::kJobPlaying) Fn<NameFn>(FN_STOP)(dlg, r.conv);
        if (r.job.loaded && r.job.st != vm::kJobDone) Fn<UnloadFn>(FN_UNLOAD)(dlg, r.manifest, true);
        Pop(true);
    }
}

// (a shark already noted stays noted: the respawn may well be a new world)
static void ForgetAll() { g_nq = 0; g_phoneInit = false; for (vm::PState& s : g_ps) s.swimT = 0.0f; }

// Once a tick from cPopulationManager::Update (lcudrive.cpp RunVoice, after life).
static void Tick(float dt, const Config& cfg, bool on)
{
    g_now += dt;
    g_p.on[vm::kShark] = cfg.voice_shark != 0;
    g_p.on[vm::kOrderCar] = cfg.voice_order_car != 0;
    g_p.on[vm::kPhone] = cfg.voice_phone != 0;
    g_p.on[vm::kSwim] = cfg.voice_swim != 0;
    g_p.on[vm::kThanks] = g_p.on[vm::kThanksGift] = cfg.voice_ped_thanks != 0;
    g_p.on[vm::kGotAway] = cfg.voice_ped_nothanks != 0;
    g_p.on[vm::kWitness] = cfg.voice_ped_witness != 0;
    g_p.once = cfg.voice_once != 0;
    g_p.chaseCooldown = cfg.voice_chase_cooldown;
    g_p.phoneCooldown = cfg.voice_phone_cooldown;
    g_p.pedGap = cfg.voice_ped_gap;
    g_hear = cfg.voice_ped_hear;
    g_falloff = cfg.voice_falloff;
    void* dlg = Dlg();
    g_on = on && dlg != nullptr && !g_stopReq;
    if (!g_on)
    {
        g_phoneInit = false;
        if (g_nq) { StopAll(false); Log("    voice: %s - lines stopped\n", g_stopReq ? "lcudrive unhooked" : "voice is off"); }
        return;
    }

    // the players
    const float deadly = *reinterpret_cast<const float*>(g_base + RVA_DEADLY);
    for (int i = 0; i < 2; ++i)
    {
        uint8_t* p = PlayerCh(i);
        vm::PIn in;
        in.valid = PtrOk(p) && (At<uint8_t>(p, 0x320) & 1);
        if (in.valid)
        {
            in.swimming = InCtx(p, wd::CTX_SWIMMING);
            in.drowning = InCtx(p, CTX_DROWNING);
            in.dead = InCtx(p, wd::CTX_DEATH) || reinterpret_cast<wd::DeadFn>(g_base + wd::FN_DEAD)(p);
            in.feet = !in.dead && InCtx(p, wd::CTX_DEFAULT);
            in.vehicle = (i ? Player2Vehicle() : PlayerVehicle()) != nullptr;
            in.deadly = IsFin(deadly) && deadly > 0.0f;
            in.dropPoint = InCtx(p, CTX_DROPPOINT);
        }
        const vm::PState before = g_ps[i];
        const uint32_t due = vm::StepPlayer(g_p, g_ps[i], in, dt, g_now);
        if (g_ps[i].armed && !before.armed)
            Log(">>> voice: player %d %s in deadly water - the shark (Chase comments once back on his feet)\n", i + 1,
                in.drowning ? "is drowning" : "died");
        for (int k : { vm::kShark, vm::kOrderCar, vm::kSwim })
            if (due & (1u << k)) { SayChase(k, i, static_cast<int>(Rnd() % vm::kChase[k].n)); vm::Said(g_ps[i], k, g_now); }
    }

    // the video call that rang out (the calls go to player 1, as the game's own DRC scripts do)
    const bool missed = *reinterpret_cast<const uint8_t*>(g_base + RVA_CALL_MISSED) != 0;
    if (!g_phoneInit) { g_phoneInit = true; g_phone.wasMissed = missed; }    // a call missed before we looked: not ours
    const int ph = vm::StepPhone(g_p, g_phone, missed, g_now);
    if (ph >= 0)
    {
        const int who = PlayerCh(0) ? 0 : 1;
        Log(">>> voice: a video call rang out unanswered\n");
        SayChase(vm::kPhone, who, ph);
    }

    Process(dlg, dt);
}

} // namespace vc
