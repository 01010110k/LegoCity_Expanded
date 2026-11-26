// =============================================================================================
//  npc_glue.h - the NPC overhaul in game (World\npc\DESIGN.md, Notes\62; merged in round 3, 2026-11-19).
//  The decisions are npc_model.h (behaviour) and npc_voice_model.h (lines, budgets, mixing), both pure
//  (World\npc\dll\npc_harness.cpp -> Drive\test\npc_harness.cpp). Included in lcudrive.cpp after life.h,
//  voice.h, spots.h, ptero.h and weather.h (it uses their helpers); npcg::Tick runs from RunNpc after
//  RunLife / RunVoice / RunSpots, with its own fault switch.
//
//  STAGE 1 - the city's people speak (only voices on what already happens; nothing visible is invented:
//  the crowd's own panic / chat context / glances that life.h already drives are the visible cause):
//    crash / wreck (wd::g_incidentMs), your punches (wd::g_punchMs), a police chase tearing past (wanted >= 2,
//    driving > 6 u/s), a bump on foot (a passer-by within 0.9 u while you move), a near miss (driving > 5 u/s,
//    a passer-by within 3 u of where you will be in 0.3 s), a big jump (the height over its last-second floor),
//    greetings / fans (on foot, walking, someone within 4 u), weather / time / phone remarks (someone 6-18 u
//    away, ~every 60 s), gossip about what happened lately (npc::Memory heat), a costume change (your
//    character's definition name), an odd vehicle (police / heli / plane / boat / bike / space / a dinosaur /
//    a Pteranodon), grudges (someone you bumped glares back with a line if you pass again), and OVERHEARD
//    SMALL TALK: life.h's crowd chat (KChat) asks OnChat first - two passers-by who meet act out one of our
//    72 exchanges, turn by turn (npc::Talk), each line at its own speaker; the stock one-liner is skipped.
//  STAGE 2 - [npc] life: life.h's scene weights x npc::ActWeight by hour / weather / place, the scene rate
//    x npc::RhythmOf(...).sceneEvery (Tune, through lf::g_tune).
//  STAGE 4 - [npc] spots: the people at the life spots (spots.h) say a line of their point's act now and
//    then: bench (sit / eat), vendor / workers (work), play, newplace (look / photo / scope at the new places).
//  (Stage 3, the new activities built from life.h's verbs, is not merged: Notes 62 §8.)
//
//  PLAYBACK, [npc] route = game | xaudio | auto:
//    game    the game's dialogue system, positional: 0x1400C6380(conversation, pos4, 0) - what the stock ambient
//            smalltalk calls (0x1400C6680, [P exe bytes 2026-11-19: entry after int3 padding; called there]);
//            our VO paks / flow files come from PATCH.DAT (World\npc\tools\patch_npc.py). The engine does the 3D
//            sound, the music duck and the volume.
//    xaudio  lcudrive's own XAudio2 engine (a second one, as the radio's): the same paks read from
//            <dll dir>\npc_vo\ (a loose copy), the WAV played with npc_voice_model.h Mix2 - gain by distance to
//            the NEARER of the two players, pan by the camera's view, narrowed when the two hear it on opposite
//            sides - updated every tick while it plays.
//    auto    game; a bank the dialogue system does not take within 10 s twice, or the first one never -> xaudio
//            for the session (logged: "npc: route auto - ... switching to lcudrive's own audio").
//  [U] until played: that 0x1400C6380 plays a conversation of a manifest the ambient system did not load; that
//  new PAKs from PATCH.DAT load; conversation order in the manifest; loudness against the stock VO.
// =============================================================================================
#pragma once
#include "npc_model.h"
#include "npc_voice_model.h"

namespace npcg {

using wd::Fn;

static volatile bool g_fault = false;
static volatile bool g_stopReq = false;

// ---- the dialogue system ------------------------------------------------------------------------------------
static const uintptr_t FN_POSPLAY = 0x0C6380;     // (conversation, pos4, 0): play at a position [P]
typedef void (__fastcall* PosPlayFn)(uint8_t* conv, const float* p4, uint8_t flag);

enum Route : int { kGame = 0, kXa = 1 };
static int      g_route = kGame;          // the route in force
static int      g_wantRoute = -1;         // [npc] route as last applied
static int      g_gameFails = 0, g_gameLoads = 0;
static npcv::Library*  g_lib = nullptr;
static npcv::Memory*   g_mem = nullptr;   // (~10 KB: heap)
static npcv::Budget    g_bud;
static npcv::Mixer     g_mix;
static npcv::MixParams g_mp;
static npcv::AudioParams g_ap;
static npcv::VoiceBook g_vb;
static uint8_t  g_loaded[npcv::kMaxMan];
static uint8_t  g_asked[npcv::kMaxMan];
static uint8_t* g_manPtr[npcv::kMaxMan];
static float    g_loadedAt[npcv::kMaxMan];
static float    g_askedAt[npcv::kMaxMan];
static float    g_now = 0.0f;
static bool     g_tableTried = false;
static char     g_tableName[64] = {};
static const Config* g_cfg = nullptr;     // this tick's settings
static npc::Rng g_r;

// ---- the xaudio route --------------------------------------------------------------------------------------
namespace xa {
typedef HRESULT(WINAPI* CreateFn)(IXAudio2** out, UINT32 flags, XAUDIO2_PROCESSOR proc);
static IXAudio2*               g_xa = nullptr;
static IXAudio2MasteringVoice* g_master = nullptr;
static UINT32                  g_outCh = 2;
static bool                    g_tried = false, g_ok = false;
struct Pak { int man; uint8_t* data; DWORD size; };
static Pak g_pak[16] = {};
struct Voice { IXAudio2SourceVoice* v; int man; float pos[3]; float gain; bool on; };
static Voice g_voice[4] = {};

static bool Start()
{
    if (g_tried) return g_ok;
    g_tried = true;
    HMODULE m = LoadLibraryExW(L"xaudio2_9.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    CreateFn create = m ? reinterpret_cast<CreateFn>(GetProcAddress(m, "XAudio2Create")) : nullptr;
    if (!create) { Log("npc: xaudio route - XAudio2 2.9 not available\n"); return false; }
    HRESULT hr = create(&g_xa, 0, XAUDIO2_DEFAULT_PROCESSOR);
    if (SUCCEEDED(hr)) hr = g_xa->CreateMasteringVoice(&g_master);
    if (FAILED(hr) || !g_master) { Log("npc: xaudio route - no audio device (0x%08lX)\n", hr); return false; }
    XAUDIO2_VOICE_DETAILS d = {};
    g_master->GetVoiceDetails(&d);
    g_outCh = d.InputChannels ? d.InputChannels : 2;
    g_ok = true;
    Log("npc: xaudio route - audio up (%u Hz, %u channels)\n", d.InputSampleRate, d.InputChannels);
    return true;
}
static void StopVoice(Voice& v) { if (v.v) { v.v->Stop(0); v.v->FlushSourceBuffers(); v.v->DestroyVoice(); } v = Voice{}; }
static void StopAll() { for (Voice& v : g_voice) StopVoice(v); }
static bool Playing(int man) { for (const Voice& v : g_voice) if (v.on && v.man == man) return true; return false; }
static Pak* Find(int man) { for (Pak& p : g_pak) if (p.data && p.man == man) return &p; return nullptr; }
static void Free(int man)
{
    Pak* p = Find(man);
    if (!p || Playing(man)) return;
    HeapFree(GetProcessHeap(), 0, p->data); *p = Pak{};
}
static void FreeAll() { StopAll(); for (Pak& p : g_pak) if (p.data) { HeapFree(GetProcessHeap(), 0, p.data); p = Pak{}; } }
static void Shutdown()
{
    FreeAll();
    if (g_master) { g_master->DestroyVoice(); g_master = nullptr; }
    if (g_xa) { g_xa->Release(); g_xa = nullptr; }
    g_ok = false; g_tried = false;
}
// <dll dir>\<vo_dir>\<manifest lower>_pc.pak, whole, checked: version 0x1234567A, the table inside the file.
static bool Load(int man, const char* dllDir, const char* voDir, const char* manName)
{
    if (Find(man)) return true;
    Pak* slot = nullptr;
    for (Pak& p : g_pak) if (!p.data) { slot = &p; break; }
    if (!slot) return false;
    char low[64]; int i = 0;
    for (; manName[i] && i < 63; ++i) low[i] = static_cast<char>(manName[i] >= 'A' && manName[i] <= 'Z' ? manName[i] + 32 : manName[i]);
    low[i] = 0;
    char path[MAX_PATH * 2];
    _snprintf_s(path, sizeof path, _TRUNCATE, "%s%s\\%s_pc.pak", dllDir, voDir, low);
    HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    const DWORD size = GetFileSize(h, nullptr);
    uint8_t* buf = (size >= 24 && size < (16u << 20)) ? static_cast<uint8_t*>(HeapAlloc(GetProcessHeap(), 0, size)) : nullptr;
    DWORD got = 0;
    const bool ok = buf && ReadFile(h, buf, size, &got, nullptr) && got == size;
    CloseHandle(h);
    const uint32_t* hd = reinterpret_cast<const uint32_t*>(buf);
    if (!ok || hd[0] != 0x1234567Au || hd[1] == 0 || hd[1] > 4096 || 24ull + 28ull * hd[1] > size)
    {
        if (buf) HeapFree(GetProcessHeap(), 0, buf);
        return false;
    }
    slot->man = man; slot->data = buf; slot->size = size;
    return true;
}
// The WAV of entry `name` in a pak: its format and its PCM.
static bool Wav(const Pak& p, const char* name, WAVEFORMATEX* wf, const uint8_t** pcm, UINT32* bytes)
{
    const uint32_t n = reinterpret_cast<const uint32_t*>(p.data)[1];
    for (uint32_t k = 0; k < n; ++k)
    {
        const uint32_t* e = reinterpret_cast<const uint32_t*>(p.data + 24 + 28 * k);
        const uint32_t no = e[0], doff = e[1], sz = e[2];
        if (no >= p.size || doff >= p.size || sz > p.size - doff) continue;
        const char* en = reinterpret_cast<const char*>(p.data + no);
        size_t len = 0; while (no + len < p.size && en[len]) ++len;
        const size_t want = strlen(name);
        if (len != want + 4 || _strnicmp(en, name, want) != 0 || _stricmp(en + want, ".wav") != 0) continue;
        const uint8_t* w = p.data + doff;
        if (sz < 44 || memcmp(w, "RIFF", 4) != 0 || memcmp(w + 8, "WAVE", 4) != 0) return false;
        bool fmt = false;
        for (uint32_t o = 12; o + 8 <= sz; )
        {
            const uint32_t cl = *reinterpret_cast<const uint32_t*>(w + o + 4);
            if (memcmp(w + o, "fmt ", 4) == 0 && cl >= 16 && o + 8 + 16 <= sz)
            {
                memset(wf, 0, sizeof *wf);
                memcpy(wf, w + o + 8, 16);
                wf->cbSize = 0;
                fmt = wf->wFormatTag == WAVE_FORMAT_PCM && wf->nChannels == 1 && wf->wBitsPerSample == 16;
            }
            else if (memcmp(w + o, "data", 4) == 0)
            {
                if (!fmt || o + 8 + cl > sz) return false;
                *pcm = w + o + 8; *bytes = cl;
                return cl > 0;
            }
            o += 8 + cl + (cl & 1);
        }
        return false;
    }
    return false;
}
static void Matrix(IXAudio2SourceVoice* v, float left, float right)
{
    float m[8] = {};
    if (g_outCh >= 2) { m[0] = left; m[1] = right; }
    else m[0] = (left + right) * 0.7071f;
    v->SetOutputMatrix(nullptr, 1, g_outCh > 8 ? 8 : g_outCh, m);
}
static bool Play(int man, const char* sample, const float pos[3], const npcv::Out& o, float vol)
{
    Pak* p = Find(man);
    if (!p || !g_ok) return false;
    WAVEFORMATEX wf; const uint8_t* pcm = nullptr; UINT32 bytes = 0;
    if (!Wav(*p, sample, &wf, &pcm, &bytes)) return false;
    Voice* slot = nullptr;
    for (Voice& v : g_voice) if (!v.on) { slot = &v; break; }
    if (!slot) return false;
    IXAudio2SourceVoice* sv = nullptr;
    if (FAILED(g_xa->CreateSourceVoice(&sv, &wf)) || !sv) return false;
    XAUDIO2_BUFFER xb = {};
    xb.AudioBytes = bytes; xb.pAudioData = pcm; xb.Flags = XAUDIO2_END_OF_STREAM;
    if (FAILED(sv->SubmitSourceBuffer(&xb))) { sv->DestroyVoice(); return false; }
    Matrix(sv, o.left * vol, o.right * vol);
    sv->Start(0);
    slot->v = sv; slot->man = man; slot->on = true; slot->gain = vol;
    for (int k = 0; k < 3; ++k) slot->pos[k] = pos[k];
    return true;
}
// Every tick: finished voices go; the others follow the listeners (and the gates).
static void Update(const npcv::AudioParams& ap, const npcv::Listener* ls, float gate)
{
    for (Voice& v : g_voice)
    {
        if (!v.on) continue;
        XAUDIO2_VOICE_STATE st = {};
        v.v->GetState(&st, XAUDIO2_VOICE_NOSAMPLESPLAYED);
        if (st.BuffersQueued == 0) { StopVoice(v); continue; }
        const npcv::Out o = npcv::Mix2(ap, ls, 2, v.pos);
        Matrix(v.v, o.left * v.gain * gate, o.right * v.gain * gate);
    }
}
} // namespace xa

// ---- the table ------------------------------------------------------------------------------------------------
static void ManName(int i, char* out, size_t cap)
{
    const char* m = g_lib->man[i];
    if (g_cfg && g_cfg->npc_subtitles && g_route == kGame && _strnicmp(m, "NPC_", 4) == 0) _snprintf_s(out, cap, _TRUNCATE, "NPCS_%s", m + 4);
    else _snprintf_s(out, cap, _TRUNCATE, "%s", m);
}

static bool LoadTable(const char* dllDir, const char* table)
{
    char path[MAX_PATH * 2];
    _snprintf_s(path, sizeof path, _TRUNCATE, "%s%s", dllDir, table);
    HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (h == INVALID_HANDLE_VALUE) { Log("npc: no line table %s - the people stay quiet\n", path); return false; }
    const DWORD size = GetFileSize(h, nullptr);
    char* buf = (size && size < (8u << 20)) ? static_cast<char*>(HeapAlloc(GetProcessHeap(), 0, size)) : nullptr;
    DWORD got = 0;
    const bool ok = buf && ReadFile(h, buf, size, &got, nullptr) && got == size;
    CloseHandle(h);
    if (!ok) { if (buf) HeapFree(GetProcessHeap(), 0, buf); return false; }
    if (!g_lib) g_lib = static_cast<npcv::Library*>(HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(npcv::Library)));
    if (!g_mem) { void* m = HeapAlloc(GetProcessHeap(), 0, sizeof(npcv::Memory)); if (m) g_mem = new (m) npcv::Memory(); }
    const bool parsed = g_lib && g_mem && npcv::ParseTsv(*g_lib, buf, size);
    HeapFree(GetProcessHeap(), 0, buf);
    if (!parsed) { Log("npc: line table %s empty or unreadable\n", path); return false; }
    npcv::SetDefaultCaps(*g_lib, g_bud);
    Log("npc: %d lines, %d manifests, %d voices, %d exchanges from %s%s%s\n", g_lib->nLines, g_lib->nMan, g_lib->nVoices,
        g_lib->nEx, table, g_lib->refused ? " - refused: " : "", g_lib->refused ? g_lib->firstRefusal : "");
    return true;
}

// The conversation record: the manifest's list (+0xC8, next +8) at the line's place in its flow file.
static uint8_t* ConvOf(int line)
{
    const npcv::Line& ln = g_lib->line[line];
    uint8_t* m = g_manPtr[ln.man];
    if (!PtrOk(m)) return nullptr;
    const char* us = strrchr(ln.conv, '_');
    const int idx = us ? atoi(us + 1) - 1 : -1;
    if (idx < 0 || idx >= At<int32_t>(m, 0xD8)) return nullptr;
    uint8_t* c = At<uint8_t*>(m, 0xC8);
    for (int k = 0; k < idx && PtrOk(c); ++k) c = At<uint8_t*>(c, 8);
    return PtrOk(c) ? c : nullptr;
}

// ---- the listeners ---------------------------------------------------------------------------------------------
static npcv::Listener g_ls[2];
// The camera that looks at p (the nearest live view's camera within 12 u, backrooms.h's way) -> the listener's yaw.
static bool CamYaw(const float p[3], float* yaw)
{
    const uint32_t now = GetTickCount();
    float best = 144.0f; bool ok = false;
    for (const wx::View& v : wx::g_view)
    {
        if (!v.used || !PtrOk(v.key) || now - v.seen > 500) continue;
        uint8_t* view = static_cast<uint8_t*>(v.key);
        uint8_t* cam = At<uint8_t>(view, wx::VIEW_CAM_A_ON) ? view + wx::VIEW_CAM_A : At<uint8_t>(view, wx::VIEW_CAM_B_ON) ? view + wx::VIEW_CAM_B : nullptr;
        if (!cam) continue;
        const float cx = At<float>(cam, wx::CAM_POS), cz = At<float>(cam, wx::CAM_POS + 8);
        const float d2 = (cx - p[0]) * (cx - p[0]) + (cz - p[2]) * (cz - p[2]);
        if (!IsFin(d2) || d2 >= best || d2 < 0.04f) continue;
        best = d2; *yaw = std::atan2(p[0] - cx, p[2] - cz); ok = true;
    }
    return ok;
}
static void Listeners()
{
    for (int i = 0; i < 2; ++i)
    {
        float q[3];
        g_ls[i].on = ap::PlayerPos(i, q);
        if (!g_ls[i].on) continue;
        for (int a = 0; a < 3; ++a) g_ls[i].pos[a] = q[a];
        float y = 0.0f;
        g_ls[i].yaw = CamYaw(q, &y) ? y : g_ls[i].yaw;
    }
}

// ---- saying a line ---------------------------------------------------------------------------------------------
static bool SayAt(int line, const float pos[3], uint8_t prio, uint64_t who, const npcv::Out& o)
{
    const npcv::Line& ln = g_lib->line[line];
    if (g_route == kGame)
    {
        void* dlg = vc::Dlg();
        uint8_t* c = dlg ? ConvOf(line) : nullptr;
        if (!c || At<int32_t>(c, 0x40) == 2) return false;
        alignas(16) float p4[4] = { pos[0], pos[1] + 0.5f, pos[2], 1.0f };   // about mouth height
        Fn<PosPlayFn>(FN_POSPLAY)(c, p4, 0);
    }
    else
    {
        char sample[64];
        _snprintf_s(sample, sizeof sample, _TRUNCATE, "NPC_%s", ln.id);
        for (char* s = sample; *s; ++s) if (*s >= 'a' && *s <= 'z') *s = static_cast<char>(*s - 32);
        if (!xa::Play(ln.man, sample, pos, o, g_cfg ? g_cfg->npc_volume : 1.0f)) return false;
    }
    npcv::Spent(*g_mem, *g_lib, line, g_now);
    npcv::Start(g_mix, line, g_now, ln.ms / 1000.0f + 0.2f, o.gain, prio, pos, static_cast<uint32_t>(who));
    if (who) npcv::Bind(g_vb, who, ln.voice, g_now);
    static uint32_t s_logged = 0;
    if (s_logged < 40) { ++s_logged; Log("npc: \"%s\" (%s, %s) at (%.1f, %.1f, %.1f), %.0f%%\n", ln.id, g_lib->cat[ln.cat], g_route == kGame ? "game" : "xaudio", pos[0], pos[1], pos[2], o.gain * 100.0f); }
    return true;
}

// ---- loads -----------------------------------------------------------------------------------------------------
static void UnloadAll()
{
    void* dlg = vc::Dlg(); char name[64];
    if (g_lib)
        for (int i = 0; i < g_lib->nMan; ++i)
            if (g_loaded[i] || g_asked[i])
            {
                if (g_route == kGame) { ManName(i, name, sizeof name); if (dlg) Fn<vc::UnloadFn>(vc::FN_UNLOAD)(dlg, name, true); }
                g_loaded[i] = g_asked[i] = 0; g_manPtr[i] = nullptr;
            }
    xa::FreeAll();
    g_mix = npcv::Mixer();
}
// A new world: the dialogue system's lists are gone with it - forget, do not unload.
static void ForgetAll()
{
    if (g_route == kGame) { memset(g_loaded, 0, sizeof g_loaded); memset(g_asked, 0, sizeof g_asked); memset(g_manPtr, 0, sizeof g_manPtr); g_mix = npcv::Mixer(); }
}

static void SwitchRoute(int to, const char* why)
{
    if (to == g_route) return;
    UnloadAll();
    g_route = to;
    Log("npc: route %s - %s\n", to == kXa ? "xaudio" : "game", why);
}

static void PumpLoads(const float* demand, const char* dllDir)
{
    char name[64];
    if (g_route == kGame)
    {
        void* dlg = vc::Dlg();
        if (!dlg) return;
        for (int i = 0; i < g_lib->nMan; ++i)
            if (g_asked[i])
            {
                ManName(i, name, sizeof name);
                if (Fn<vc::NameOkFn>(vc::FN_MANOK)(dlg, name) && Fn<vc::NameOkFn>(vc::FN_PAKOK)(dlg, name))
                { g_asked[i] = 0; g_loaded[i] = 1; g_loadedAt[i] = g_now; ++g_gameLoads; }
                else if (g_now - g_askedAt[i] > 10.0f)
                {
                    g_asked[i] = 0; Fn<vc::UnloadFn>(vc::FN_UNLOAD)(dlg, name, true); g_manPtr[i] = nullptr;
                    ++g_gameFails;
                    Log("npc: %s did not load in 10 s - given up\n", name);
                    if (g_cfg->npc_route == 2 && (g_gameFails >= 2 || g_gameLoads == 0))
                    { SwitchRoute(kXa, "route auto: the game's dialogue system did not take our voice banks - switching to lcudrive's own audio"); return; }
                }
            }
    }
    uint8_t busy[npcv::kMaxMan] = {}, any[npcv::kMaxMan];
    for (const npcv::Playing& p : g_mix.slot) if (p.on && p.line >= 0) busy[g_lib->line[p.line].man] = 1;
    for (int i = 0; i < g_lib->nMan; ++i) { any[i] = g_loaded[i] | g_asked[i]; if (g_asked[i]) busy[i] = 1; if (g_route == kXa && xa::Playing(i)) busy[i] = 1; }
    const npcv::LoadPlan lp = npcv::PlanLoads(*g_lib, *g_mem, g_bud, demand, any, busy, g_cfg->npc_max_loaded, g_now, g_loadedAt);
    if (lp.unload >= 0 && g_loaded[lp.unload])
    {
        if (g_route == kGame) { ManName(lp.unload, name, sizeof name); Fn<vc::UnloadFn>(vc::FN_UNLOAD)(vc::Dlg(), name, true); }
        else xa::Free(lp.unload);
        g_loaded[lp.unload] = 0; g_manPtr[lp.unload] = nullptr;
    }
    if (lp.load >= 0)
    {
        ManName(lp.load, name, sizeof name);
        if (g_route == kGame)
        {
            g_manPtr[lp.load] = static_cast<uint8_t*>(Fn<vc::LoadFn>(vc::FN_LOAD)(vc::Dlg(), nullptr, name, true));
            g_asked[lp.load] = 1; g_askedAt[lp.load] = g_now;
        }
        else if (xa::Load(lp.load, dllDir, g_cfg->npc_vo_dir, name)) { g_loaded[lp.load] = 1; g_loadedAt[lp.load] = g_now; }
        else
        {
            static int s_missing = 0;
            if (++s_missing <= 3) Log("npc: xaudio route - no %s%s\\%s_pc.pak (lower case) - is the npc_vo folder installed?\n", dllDir, g_cfg->npc_vo_dir, name);
        }
    }
}

// ---- the moment ------------------------------------------------------------------------------------------------
static uint32_t HaveTags(bool rain, float hour, int district, bool onFoot, bool coop, const float yp[3])
{
    uint32_t t = rain ? npcv::T_RAIN : npcv::T_DRY;
    const uint8_t part = npc::PartOf(hour);
    t |= part <= npc::kMorning ? npcv::T_MORNING : part <= npc::kAfternoon ? npcv::T_MIDDAY : part == npc::kEvening ? npcv::T_EVENING : npcv::T_NIGHT;
    uint8_t pl = npc::PlaceOfDistrict(district);
    if (yp[0] > 350.0f && yp[2] < 40.0f) pl = npc::kNewTown;                                   // Port Atlas (Notes 42/45)
    else if (yp[0] < -60.0f && yp[0] > -840.0f && yp[2] > 120.0f) pl = npc::kWoodsP;           // Woods West + the Wilds
    t |= pl == npc::kBeachP ? npcv::T_BEACH : pl == npc::kParkP ? npcv::T_PARK : pl == npc::kDowntownP ? npcv::T_DOWNTOWN :
         pl == npc::kCountry ? npcv::T_COUNTRY : pl == npc::kNewTown ? (npcv::T_PORTATLAS | npcv::T_HARBOUR) :
         pl == npc::kHarbourP ? npcv::T_HARBOUR : pl == npc::kWoodsP ? (npcv::T_WOODS | npcv::T_COUNTRY) : npcv::T_CITY;
    if (pl == npc::kPlaza || district == 14 || district == 7) t |= npcv::T_LANDMARK;
    t |= onFoot ? npcv::T_ONFOOT : npcv::T_DRIVING;
    if (coop) t |= npcv::T_COOP;
    if (wd::g_shown > 0) t |= npcv::T_WANTED;
    return t;
}
static uint8_t PlaceNow(int district, const float yp[3])
{
    if (yp[0] > 350.0f && yp[2] < 40.0f) return npc::kNewTown;
    if (yp[0] < -60.0f && yp[0] > -840.0f && yp[2] > 120.0f) return npc::kWoodsP;
    return npc::PlaceOfDistrict(district);
}

static int Cat(const char* n) { return g_lib && n ? npcv::FindName(g_lib->cat, g_lib->nCats, n) : -1; }

// A bark of category `cat` from someone at pos (gender, and a key for "this person keeps their voice").
static bool BarkG(const char* cat, const char* topic, uint8_t gender, uint64_t key, const float pos[3], uint32_t have, uint8_t prio,
                  bool voiceOnly = false)
{
    const int c = Cat(cat);
    if (c < 0) return false;
    npcv::Query q; q.cat = c; q.topic = topic ? Cat(topic) : -1; q.have = have; q.loaded = g_loaded;
    if (topic && q.topic < 0) return false;
    q.gender = gender;
    q.voice = npcv::VoiceOf(g_vb, key); q.voiceOnly = voiceOnly && q.voice >= 0;
    g_ap.farU = g_cfg->npc_hear;
    const npcv::Out o = npcv::Mix2(g_ap, g_ls, 2, pos);
    if (!o.audible) return false;
    npcv::Gates g; g.storyTalk = vc::AnyTalking(vc::Dlg(), nullptr) && npcv::Active(g_mix) == 0;
    g.cutscene = rd::g_hold != 0; g.radio = rd::g_audible != 0;
    const float nearU = o.nearest >= 0 ? std::sqrt((pos[0] - g_ls[o.nearest].pos[0]) * (pos[0] - g_ls[o.nearest].pos[0]) + (pos[2] - g_ls[o.nearest].pos[2]) * (pos[2] - g_ls[o.nearest].pos[2])) : 99.0f;
    if (npcv::MayStart(g_mix, g_mp, g, prio, o.gain, pos, nearU, false) != npcv::V_PLAY) return false;
    const int line = npcv::PickLine(*g_lib, *g_mem, g_bud, q, g_now, g_r.U());
    return line >= 0 && SayAt(line, pos, prio, key, o);
}
static uint8_t KGender(uint8_t* k)
{
    uint8_t* ty = lf::KType(k);
    return PtrOk(ty) ? (((ty[0x158] >> 1) & 1) ? npcv::kFemale : npcv::kMale) : npcv::kAnyGender;
}
static bool Bark(const char* cat, const char* topic, uint8_t* k, const float pos[3], uint32_t have, uint8_t prio, bool voiceOnly = false)
{
    return BarkG(cat, topic, KGender(k), reinterpret_cast<uint64_t>(k), pos, have, prio, voiceOnly);
}

// ---- the crowd near a point (life.h ScanCrowd's walk, into our own list: life's candidates stay as they are) ------
struct K { uint8_t* k; float pos[3]; float d; };
static K   g_k[64];
static int g_nk = 0;
static void Scan(void* level, const float at[3], float rMin, float rMax)
{
    g_nk = 0;
    uint8_t* mgr = Fn<wd::KMgrFn>(wd::FN_KMGR)(level);
    if (!PtrOk(mgr)) return;
    int zones = 0;
    for (uint8_t* z = At<uint8_t*>(mgr, 0xE0); PtrOk(z) && zones < 64; z = At<uint8_t*>(z, 8), ++zones)
    {
        uint8_t* base = At<uint8_t*>(z, 0x38);
        const int stride = At<int32_t>(z, 0x40), n = At<int32_t>(z, 0x44);
        if (!PtrOk(base) || stride < 0x100 || stride > 0x2000 || n <= 0 || n > 4096) continue;
        for (int i = 0; i < n && g_nk < 64; ++i)
        {
            uint8_t* k = base + static_cast<size_t>(i) * static_cast<size_t>(stride);
            if (At<uint16_t>(k, 0xD2) & 0xD || !PtrOk(At<void*>(k, 0xF8))) continue;
            float d2 = 0.0f, p[3];
            for (int c = 0; c < 3; ++c) { p[c] = At<float>(k, 0xB0 + 4 * c); const float dc = p[c] - at[c]; d2 += dc * dc; }
            const float d = std::sqrt(d2);
            if (!IsFin(d) || d < rMin || d > rMax) continue;
            const char* name = wd::KrawlieDefName(k);
            if (!lf::Castable(name) || wd::ContainsI(name, "cop") || wd::ContainsI(name, "officer")) continue;
            K& o = g_k[g_nk++]; o.k = k; o.d = d; for (int c = 0; c < 3; ++c) o.pos[c] = p[c];
        }
    }
}
static int Nearest() { int b = -1; for (int i = 0; i < g_nk; ++i) if (b < 0 || g_k[i].d < g_k[b].d) b = i; return b; }

// An event -> who reacts (npc::PlanReaction over the scanned crowd) -> the first barker says a line.
static npc::ReactState g_rs;
static void React(void* level, const float at[3], uint8_t kind, float severity, const char* cat, const char* topic, uint32_t have,
                  uint64_t target = 0)
{
    Scan(level, at, 0.0f, 26.0f);
    npc::Person ppl[64]; int n = 0;
    for (int i = 0; i < g_nk; ++i) { ppl[n].id = reinterpret_cast<uint64_t>(g_k[i].k); ppl[n].pos = npc::Mk(g_k[i].pos[0], g_k[i].pos[1], g_k[i].pos[2]); ppl[n].flags = 0; ++n; }
    npc::Event e; e.kind = kind; e.pos = npc::Mk(at[0], at[1], at[2]); e.severity = severity; e.target = target;
    npc::Reaction r[8];
    const int k = npc::PlanReaction(g_rs, e, ppl, n, g_now, g_r, r, 8);
    for (int j = 0; j < k; ++j)
        if (r[j].barks && Bark(cat, topic, g_k[r[j].who].k, g_k[r[j].who].pos, have, npcv::P_REACT)) break;
}

// ---- overheard small talk (life.h KChat -> OnChat) --------------------------------------------------------------
struct Convo { bool on; uint8_t* k[2]; float pos[2][3]; int ex; bool swapped; npc::Talk talk; float wait; };
static Convo    g_convo = {};
static uint32_t g_have = 0;
static float    g_lastChat = -1e9f;
static bool OnChat(uint8_t* a, uint8_t* b)
{
    __try
    {
        if (g_fault || !g_lib || !g_cfg || !g_cfg->npc_enable || !g_cfg->npc_voices || !g_cfg->npc_smalltalk || g_convo.on) return false;
        if (g_now - g_lastChat < g_cfg->npc_chat_every * g_r.R(0.6f, 1.4f)) return false;
        float pa[3], pb[3];
        for (int c = 0; c < 3; ++c) { pa[c] = At<float>(a, 0xB0 + 4 * c); pb[c] = At<float>(b, 0xB0 + 4 * c); }
        g_ap.farU = g_cfg->npc_hear;
        if (!npcv::Mix2(g_ap, g_ls, 2, pa).audible) return false;
        const int topic = -1;
        const npcv::ExPick xp = npcv::PickExchange(*g_lib, *g_mem, g_bud, topic, KGender(a), KGender(b), g_have, g_now, g_r.U(), Cat("smalltalk"), g_loaded);
        if (xp.ex < 0) return false;
        Convo& cv = g_convo;
        cv = Convo{};
        cv.on = true; cv.k[0] = a; cv.k[1] = b; cv.ex = xp.ex; cv.swapped = xp.swapped;
        for (int c = 0; c < 3; ++c) { cv.pos[0][c] = pa[c]; cv.pos[1][c] = pb[c]; }
        const npcv::Exchange& E = g_lib->ex[xp.ex];
        cv.talk = npc::Talk();
        cv.talk.turns = E.turns < 6 ? E.turns : 6;
        for (int t = 0; t < cv.talk.turns; ++t) cv.talk.lineS[t] = g_lib->line[E.first + t].ms / 1000.0f;
        cv.talk.first = xp.swapped ? 1 : 0;
        g_lastChat = g_now;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { g_fault = true; return false; }
}
static void StepConvo(float dt)
{
    Convo& cv = g_convo;
    if (!cv.on) return;
    npc::TalkStep st[3];
    const int n = npc::StepTalk(cv.talk, dt, g_r, st);
    for (int i = 0; i < n; ++i)
    {
        if (st[i].what == npc::TD_SAY && st[i].turn >= 0)
        {
            const npcv::Exchange& E = g_lib->ex[cv.ex];
            const int line = E.first + st[i].turn;
            const int who = st[i].who < 0 ? 0 : st[i].who > 1 ? 1 : st[i].who;
            const float* pos = cv.pos[who];
            g_ap.farU = g_cfg->npc_hear;
            const npcv::Out o = npcv::Mix2(g_ap, g_ls, 2, pos);
            npcv::Gates g; g.cutscene = rd::g_hold != 0; g.radio = rd::g_audible != 0;
            g.storyTalk = vc::AnyTalking(vc::Dlg(), nullptr) && npcv::Active(g_mix) == 0;
            const npcv::Verdict v = o.audible ? npcv::MayStart(g_mix, g_mp, g, npcv::P_CONVO, o.gain, pos, 5.0f, st[i].turn > 0) : npcv::V_DROP;
            if (v != npcv::V_PLAY || !SayAt(line, pos, npcv::P_CONVO, reinterpret_cast<uint64_t>(cv.k[who]), o)) { cv.on = false; return; }
            if (st[i].turn == 0) g_mem->lastEx[cv.ex] = g_now;
        }
        if (st[i].what == npc::TD_DONE) { cv.on = false; return; }
    }
    if (cv.talk.t > 30.0f) cv.on = false;
}

// ---- stage 2: life.h's scene weights (lf::g_tune) --------------------------------------------------------------
static float g_hourNow = 13.0f; static bool g_rainNow = false; static uint8_t g_placeNow = npc::kTown;
static void Tune(life::Params& p)
{
    if (!g_cfg || !g_cfg->npc_enable || !g_cfg->npc_life || g_fault) return;
    npc::Params np; npc::Ctx c; c.hour = g_hourNow; c.weather.rain = g_rainNow; c.place = g_placeNow;
    // life.h scene -> the activity that it is (npc_model.h kActDef route R_LIFE); 1 = no opinion
    static const int kMap[life::kSceneCount] = {
        npc::A_CHAT, npc::A_PHONE, npc::A_PHOTO, -1, npc::A_QUEUE_FOOD, npc::A_JOG, npc::A_WORKOUT, npc::A_DANCE,
        npc::A_BEACH, npc::A_BUSKER, npc::A_COPS_BEAT, -1, -1, -1, -1, -1 };
    for (int s = 0; s < life::kSceneCount; ++s)
    {
        if (kMap[s] < 0) continue;
        const float base = npc::kActDef[kMap[s]].base;
        const float w = npc::ActWeight(np, c, kMap[s], 0.0f);
        p.mult[s] = base > 0.0f ? npc::Clamp(w / base, 0.15f, 3.0f) : 1.0f;
    }
    p.every *= npc::Clamp(npc::RhythmOf(c.hour, c.weather, c.place).sceneEvery, 0.75f, 3.0f);
}

// ---- the tick --------------------------------------------------------------------------------------------------
static float  g_prev[3]; static bool g_prevOk = false; static float g_speed = 0.0f; static float g_vel[3] = {};
static float  g_yHist[32]; static float g_yT[32]; static int g_yN = 0, g_yHead = 0;
static bool   g_air = false; static float g_airT = 0.0f;
static npc::Memory   g_nm; static npc::MemParams g_nmp; static npc::GreetState g_gs;
static char   g_lastDef[64] = {}; static float g_defChangedAt = -1.0f;
static int    g_lastVeh = -2; static float g_vehAt = -1.0f;

static const char* DisguiseTopic(const char* n)
{
    if (!n) return nullptr;
    if (wd::ContainsI(n, "fire")) return "fireman";
    if (wd::ContainsI(n, "construct") || wd::ContainsI(n, "worker")) return "construction";
    if (wd::ContainsI(n, "robber") || wd::ContainsI(n, "crook") || wd::ContainsI(n, "criminal")) return "robber";
    if (wd::ContainsI(n, "astronaut") || wd::ContainsI(n, "space")) return "astronaut";
    if (wd::ContainsI(n, "farmer")) return "farmer";
    if (wd::ContainsI(n, "miner")) return "miner";
    if (wd::ContainsI(n, "cop") || wd::ContainsI(n, "police") || wd::ContainsI(n, "chase")) return "police";
    return nullptr;
}
static const char* VehicleTopic(int fill)
{
    switch (fill)
    {
    case 0: case 16: return "bike";
    case 1: case 9: return "police";
    case 2: return "heli";
    case 17: return "plane";
    case 18: return "boat";
    case 19: case 20: return "odd";
    default: return nullptr;
    }
}

static void Tick(void* level, float dt, const Config& cfg, bool on, const char* dllDir)
{
    g_cfg = &cfg;
    g_now += dt;
    npcv::Expire(g_mix, g_now);
    if (g_stopReq) { UnloadAll(); xa::Shutdown(); lf::g_onChat = nullptr; lf::g_tune = nullptr; g_stopReq = false; return; }
    lf::g_onChat = &OnChat; lf::g_tune = &Tune;
    if (!on || !cfg.npc_enable || !cfg.npc_voices) { if (g_lib) UnloadAll(); g_convo.on = false; return; }
    if (!g_tableTried || strcmp(g_tableName, cfg.npc_table) != 0)
    {
        g_tableTried = true; strcpy_s(g_tableName, cfg.npc_table);
        if (!LoadTable(dllDir, cfg.npc_table)) return;
    }
    if (!g_lib || !g_lib->nLines) return;
    // the route: [npc] route changed -> start over on the new one
    if (cfg.npc_route != g_wantRoute)
    {
        g_wantRoute = cfg.npc_route;
        const int to = cfg.npc_route == 1 ? kXa : kGame;
        g_gameFails = g_gameLoads = 0;
        if (to != g_route) SwitchRoute(to, cfg.npc_route == 1 ? "[npc] route = xaudio" : "[npc] route = game / auto");
        else Log("npc: route %s ([npc] route = %s)\n", g_route == kXa ? "xaudio" : "game", cfg.npc_route == 0 ? "game" : cfg.npc_route == 1 ? "xaudio" : "auto");
    }
    if (g_route == kXa && !xa::Start())
    {
        if (cfg.npc_route == 2) { SwitchRoute(kGame, "route auto: lcudrive's own audio could not start - back to the game's"); g_gameFails = -100; }
        else return;
    }
    g_bud.perHour = cfg.npc_per_hour;
    Listeners();
    if (g_route == kXa)
    {
        const float gate = (rd::g_hold ? 0.0f : 1.0f) * (vc::AnyTalking(vc::Dlg(), nullptr) ? 0.25f : 1.0f) * (rd::g_audible ? g_mp.radioDuck : 1.0f);
        g_ap.farU = cfg.npc_hear;
        xa::Update(g_ap, g_ls, gate);
    }
    if (wd::MissionRunning() || br::Quiet()) { g_convo.on = false; return; }   // story missions: the game talks; the Backrooms: nobody

    uint8_t* p1 = *reinterpret_cast<uint8_t**>(g_base + eng::RVA_PLAYER1);
    if (!PtrOk(p1) || !g_ls[0].on) return;
    const bool coop = g_ls[1].on;
    const float* yp = g_ls[0].pos;
    // speed / velocity / height history (player 1: the lines are about "you" = whoever drives the event; co-op uses
    // the nearer listener for loudness)
    if (g_prevOk && dt > 0.0f)
    {
        for (int a = 0; a < 3; ++a) g_vel[a] = g_vel[a] * 0.7f + 0.3f * (yp[a] - g_prev[a]) / dt;
        const float s = std::sqrt(g_vel[0] * g_vel[0] + g_vel[2] * g_vel[2]);
        g_speed = IsFin(s) && s < 120.0f ? s : 0.0f;
    }
    const bool jumped = g_prevOk && ((yp[0] - g_prev[0]) * (yp[0] - g_prev[0]) + (yp[2] - g_prev[2]) * (yp[2] - g_prev[2])) > 100.0f;
    for (int a = 0; a < 3; ++a) g_prev[a] = yp[a];
    g_prevOk = true;
    if (jumped) { g_speed = 0.0f; for (float& v : g_vel) v = 0.0f; g_yN = 0; g_air = false; }
    const bool onFoot = !PlayerVehicle();
    g_hourNow = npc::HourOf(lights::g_sunPhase); g_rainNow = rd::g_rain != 0;
    g_placeNow = PlaceNow(static_cast<int>(rd::g_district), yp);
    g_have = HaveTags(g_rainNow, g_hourNow, static_cast<int>(rd::g_district), onFoot, coop, yp);
    npc::Cool(g_nm, g_nmp, dt);

    // what to keep loaded
    static float s_loadT = 0.0f;
    if ((s_loadT -= dt) <= 0.0f)
    {
        s_loadT = 3.0f;
        float demand[npcv::kMaxCats] = {};
        auto want = [&](const char* n, float w) { const int c = Cat(n); if (c >= 0) demand[c] = w; };
        if (cfg.npc_reactions)
        {
            want("react_crash", 1.0f); want("react_near_miss", onFoot ? 0.2f : 0.9f); want("react_stunt", onFoot ? 0.2f : 0.8f);
            want("react_bumped", onFoot ? 0.8f : 0.0f); want("glare_again", onFoot ? 0.4f : 0.0f); want("react_punch", onFoot ? 0.5f : 0.0f);
            want("react_chase", wd::g_shown >= 2 ? 1.2f : 0.0f); want("react_vehicle", onFoot ? 0.1f : 0.5f); want("react_disguise", onFoot ? 0.4f : 0.0f);
        }
        if (cfg.npc_greetings && onFoot) { want("greet_player", 0.8f); want("fan_player", 0.5f); }
        if (cfg.npc_smalltalk)
        {
            want("smalltalk", 0.7f); want("phone", 0.3f); want("time_remark", 0.3f); want("gossip", 0.3f);
            if (rd::g_rain) { want("weather_rain", 0.8f); want("weather_talk", 0.5f); } else want("weather_sun", 0.3f);
        }
        if (cfg.npc_spots && cfg.spots_enable) { want("bench", 0.3f); want("vendor", 0.3f); want("newplace", 0.5f); want("play", 0.2f); want("workers", 0.2f); }
        PumpLoads(demand, dllDir);
    }
    StepConvo(dt);

    const uint32_t nowMs = GetTickCount();
    // crash / wreck just now
    static uint32_t s_inc = 0, s_punch = 0;
    const uint32_t inc = wd::g_incidentMs;
    if (cfg.npc_reactions && inc && inc != s_inc && nowMs - inc < 1500)
    {
        s_inc = inc;
        const bool wreck = wd::g_incidentKind == 3;
        npc::Heard(g_nm, npc::EV_CRASH, wreck ? 1.6f : 0.8f);
        React(level, yp, wreck ? npc::EV_WRECK : npc::EV_CRASH, wreck ? 1.6f : 0.8f, "react_crash", wreck ? "wreck" : nullptr, g_have);
        if (g_convo.on) npc::Interrupt(g_convo.talk, 2.5f);
    }
    // your fists
    const uint32_t pu = wd::g_punchMs;
    if (cfg.npc_reactions && pu && pu != s_punch && nowMs - pu < 1500)
    {
        s_punch = pu;
        React(level, yp, npc::EV_PUNCH, 1.0f, "react_punch", nullptr, g_have);
    }
    // a police chase tearing past
    static float s_chaseT = -1e9f;
    if (cfg.npc_reactions && wd::g_shown >= 2 && !onFoot && g_speed > 6.0f && g_now - s_chaseT > 10.0f)
    {
        s_chaseT = g_now;
        npc::Heard(g_nm, npc::EV_CHASE, 0.6f);
        Scan(level, yp, 6.0f, 20.0f);
        if (g_nk > 0) { const K& k = g_k[g_r.I(g_nk)]; Bark("react_chase", nullptr, k.k, k.pos, g_have, npcv::P_REACT); }
    }
    // the next three need the crowd right round you: one scan at most every 0.25 s
    static float s_scanT = 0.0f;
    if ((s_scanT -= dt) <= 0.0f && !jumped)
    {
        s_scanT = 0.25f;
        static float s_bumpT = -1e9f, s_missT = -1e9f;
        if (cfg.npc_reactions && onFoot && g_speed > 2.0f && g_now - s_bumpT > 3.0f)
        {
            Scan(level, yp, 0.0f, 1.2f);
            const int b = Nearest();
            if (b >= 0 && g_k[b].d < 0.9f)
            {
                s_bumpT = g_now;
                const uint64_t id = reinterpret_cast<uint64_t>(g_k[b].k);
                const npc::Grudge& gr = npc::Bumped(g_nm, g_nmp, id, 0, g_now);
                Bark(gr.level > 1 ? "glare_again" : "react_bumped", nullptr, g_k[b].k, g_k[b].pos, g_have, npcv::P_DIRECT, gr.level > 1);
            }
        }
        if (cfg.npc_reactions && !onFoot && g_speed > 5.0f && g_now - s_missT > 6.0f)
        {
            const float ahead[3] = { yp[0] + g_vel[0] * 0.3f, yp[1], yp[2] + g_vel[2] * 0.3f };
            Scan(level, ahead, 0.0f, 3.0f);
            const int b = Nearest();
            if (b >= 0)
            {
                s_missT = g_now;
                npc::Heard(g_nm, npc::EV_NEAR_MISS, 0.4f);
                Bark("react_near_miss", nullptr, g_k[b].k, g_k[b].pos, g_have, npcv::P_REACT);
            }
        }
        // grudges: someone you bumped, passed again
        if (cfg.npc_reactions && onFoot && npc::Grudges(g_nm, g_now) > 0)
        {
            Scan(level, yp, 0.0f, g_nmp.glareNearU);
            for (int i = 0; i < g_nk; ++i)
                if (npc::GrudgeStep(g_nm, g_nmp, reinterpret_cast<uint64_t>(g_k[i].k), g_k[i].d, g_now) == 2)
                { Bark("glare_again", nullptr, g_k[i].k, g_k[i].pos, g_have, npcv::P_DIRECT, true); break; }
        }
    }
    // a big jump: the height over its last-second floor (driving)
    if (!onFoot && !jumped)
    {
        g_yHist[g_yHead] = yp[1]; g_yT[g_yHead] = g_now; g_yHead = (g_yHead + 1) & 31; if (g_yN < 32) ++g_yN;
        float floorY = 1e9f;
        for (int i = 0; i < g_yN; ++i) if (g_now - g_yT[i] <= 1.0f && g_yHist[i] < floorY) floorY = g_yHist[i];
        const bool up = yp[1] - floorY > 1.2f;
        if (up && !g_air) { g_air = true; g_airT = 0.0f; }
        if (g_air)
        {
            g_airT += dt;
            if (std::fabs(g_vel[1]) < 0.6f && g_airT > 0.4f && !up)
            {
                g_air = false;
                if (cfg.npc_reactions && g_airT > 0.8f)
                {
                    npc::Heard(g_nm, npc::EV_STUNT, g_airT);
                    React(level, yp, npc::EV_STUNT, npc::Clamp(g_airT / 1.5f, 0.5f, 2.5f), "react_stunt", g_airT > 1.5f ? "big" : nullptr, g_have);
                }
            }
            if (g_airT > 8.0f) g_air = false;
        }
    }
    else { g_air = false; g_yN = 0; }
    // greetings / fans: walking, someone within 4 u
    npc::Ctx nc; nc.hour = g_hourNow; nc.weather.rain = g_rainNow; nc.place = g_placeNow;
    nc.pl[0].on = true; nc.pl[0].speed = g_speed; nc.pl[0].vehicle = onFoot ? npc::VH_NONE : npc::VH_CAR; nc.pl[0].wanted = wd::g_shown;
    nc.cutscene = rd::g_hold != 0;
    static float s_greetScan = 0.0f;
    if (cfg.npc_greetings && onFoot && (s_greetScan -= dt) <= 0.0f && npc::MayGreet(g_gs, nc, 0, g_now))
    {
        s_greetScan = 0.5f;
        Scan(level, yp, 1.0f, 4.0f);
        const int b = Nearest();
        if (b >= 0)
        {
            const bool fan = g_r.F() < 0.2f;
            if (Bark(fan ? "fan_player" : "greet_player", fan && coop ? "coop" : nullptr, g_k[b].k, g_k[b].pos, g_have, npcv::P_DIRECT))
                g_gs.last[0] = g_now;
        }
    }
    // remarks in passing: weather, the hour, a phone call, gossip (~every 60 s)
    static float s_remarkT = 30.0f;
    if (cfg.npc_smalltalk && (s_remarkT -= dt) <= 0.0f)
    {
        s_remarkT = g_r.R(45.0f, 80.0f);
        Scan(level, yp, 6.0f, 18.0f);
        if (g_nk > 0)
        {
            const K& k = g_k[g_r.I(g_nk)];
            float heat = 0.0f;
            const int gt = npc::GossipTopic(g_nm, &heat);
            const char* gossipTopic = gt == npc::EV_CHASE ? "chase" : gt == npc::EV_CRASH || gt == npc::EV_WRECK ? "crash" : gt == npc::EV_STUNT ? "stunt" : nullptr;
            const float r = g_r.F();
            bool said = false;
            if (gossipTopic && heat > 0.8f && r < 0.4f) said = Bark("gossip", gossipTopic, k.k, k.pos, g_have, npcv::P_AMBIENT);
            if (!said && rd::g_rain && r < 0.7f) said = Bark(r < 0.45f ? "weather_rain" : "weather_talk", nullptr, k.k, k.pos, g_have, npcv::P_AMBIENT);
            if (!said && r < 0.5f) said = Bark("time_remark", nullptr, k.k, k.pos, g_have, npcv::P_AMBIENT);
            if (!said && r < 0.8f) said = Bark("phone", nullptr, k.k, k.pos, g_have, npcv::P_AMBIENT);
            if (!said && !rd::g_rain) Bark("weather_sun", nullptr, k.k, k.pos, g_have, npcv::P_AMBIENT);
        }
    }
    // a costume change: someone near notices (Chase in police blue is not news)
    {
        const char* dn = CharDefName(p1);
        if (dn && *dn && _stricmp(dn, g_lastDef) != 0)
        {
            const bool first = g_lastDef[0] == 0;
            strncpy_s(g_lastDef, dn, _TRUNCATE);
            if (!first) g_defChangedAt = g_now;
        }
        if (g_defChangedAt >= 0.0f && g_now - g_defChangedAt > 1.5f)
        {
            g_defChangedAt = -1.0f;
            const char* tp = DisguiseTopic(g_lastDef);
            if (cfg.npc_reactions && onFoot && tp && _stricmp(tp, "police") != 0)
            {
                Scan(level, yp, 3.0f, 12.0f);
                if (g_nk > 0) { const K& k = g_k[g_r.I(g_nk)]; Bark("react_disguise", tp, k.k, k.pos, g_have | npcv::T_DISGUISE, npcv::P_REACT); }
            }
        }
    }
    // an odd vehicle (or a dinosaur / a Pteranodon under you)
    {
        int veh = onFoot ? -1 : static_cast<int>(rd::g_vehClass);
        const char* tp = onFoot ? nullptr : VehicleTopic(veh);
        if (onFoot)
        {
            uint8_t* m = pt::MountOf(p1);
            const char* mn = m ? CharDefName(m) : nullptr;
            if (mn && wd::ContainsI(mn, "ptera")) { veh = 100; tp = "pteranodon"; }
            else if (mn && (wd::ContainsI(mn, "behemoth") || wd::ContainsI(mn, "rex") || wd::ContainsI(mn, "dino"))) { veh = 101; tp = "dino"; }
        }
        if (veh != g_lastVeh) { g_lastVeh = veh; g_vehAt = tp ? g_now : -1.0f; }
        static float s_vehT = -1e9f;
        if (g_vehAt >= 0.0f && g_now - g_vehAt > 2.0f && g_now - s_vehT > 30.0f && tp && cfg.npc_reactions)
        {
            g_vehAt = -1.0f;
            Scan(level, yp, 4.0f, 14.0f);
            if (g_nk > 0) { s_vehT = g_now; const K& k = g_k[g_r.I(g_nk)]; Bark("react_vehicle", tp, k.k, k.pos, g_have, npcv::P_REACT); }
        }
    }
    // stage 4: the people at the life spots say a line of what they are doing now and then
    static float s_spotT = 20.0f;
    if (cfg.npc_spots && cfg.spots_enable && (s_spotT -= dt) <= 0.0f)
    {
        s_spotT = g_r.R(25.0f, 45.0f);
        const spm::Data& d = cfg.spots_data;
        int best = -1; float bd = 14.0f;
        for (int i = 0; i < spm::kMaxPeople; ++i)
        {
            const spm::Person& m = sp::g_person[i];
            if (m.state != spm::pActing || m.pt < 0 || m.pt >= d.nPts) continue;
            uint8_t* ch = sp::Char(sp::g_slot[i]);
            float q[3];
            if (!ch || !sp::ChPos(ch, q)) continue;
            const float dd = std::sqrt((q[0] - yp[0]) * (q[0] - yp[0]) + (q[2] - yp[2]) * (q[2] - yp[2]));
            if (dd < bd && dd > 2.0f) { bd = dd; best = i; }
        }
        if (best >= 0)
        {
            uint8_t* ch = sp::Char(sp::g_slot[best]);
            float q[3]; sp::ChPos(ch, q);
            const spm::Pt& pt = d.pts[sp::g_person[best].pt];
            const int cast = d.spots[sp::g_person[best].spot].cast;
            const char* dn = CharDefName(ch);
            const uint8_t gender = dn && (wd::ContainsI(dn, "female") || wd::ContainsI(dn, "girl") || wd::ContainsI(dn, "woman") || wd::ContainsI(dn, "lady")) ? npcv::kFemale : npcv::kMale;
            const char* cat = pt.act == spm::aSit || pt.act == spm::aEat ? "bench" : pt.act == spm::aPlay ? "play" :
                              pt.act == spm::aWork ? (cast == spm::cWorkers || cast == spm::cRangers ? "workers" : "vendor") : "newplace";
            const char* topic = nullptr;
            if (_stricmp(cat, "newplace") == 0)
                topic = g_placeNow == npc::kNewTown ? "portatlas" : g_placeNow == npc::kWoodsP ? "woods" : cast == spm::cPassengers ? "trains" : nullptr;
            BarkG(cat, topic, gender, reinterpret_cast<uint64_t>(ch), q, g_have, npcv::P_AMBIENT);
        }
    }
}

}  // namespace npcg
