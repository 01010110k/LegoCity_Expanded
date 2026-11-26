// =============================================================================================
//  voice_model.h - the cut voice lines (Notes\46 §4): which recorded line fits the moment, and
//  when. Pure: the moment in, a line (a conversation manifest + a conversation name) out, so
//  test\voice_harness.cpp runs it as is. voice.h asks the game's own dialogue system to play it.
//
//  2026-11-15: the cut lines are played where a moment in the game fits them in context.
//  Every line below is recorded (10 languages), subtitled (text.csv) and has its
//  stuff\conversations\flow\<manifest>.txt, but no script or exe path ever plays it.
//
//    Chase, after the deep-water shark took him:   EatenByShark   "Yowch! I should stay out of deep water..."
//    Chase, at a car call-in point (first use):    OrderNewCar    "Great! I can use this thing to order a new car!"
//    Chase, when a video call rang out unanswered: AnswerThePhone "I should really answer that call." (3)
//    Chase, the first swim of the session:         PlayerHint_GeneralSwim "Time for a dip." (5 of 6)
//    Passers-by (ambient life, life.h):            Thanks_* / NoThanks_* - 8 + 8 town voices, 4 + 4
//      hillbilly (Bluebell, Fort Meadows), 3 + 3 surfer (Paradise Sands): thanks when the pickpocket
//      is caught or a gift cheers them up, disappointment when the thief gets away or you crash.
// =============================================================================================
#pragma once
#include <cstdint>
#include <cstring>

namespace vm {

// ---- what can be said ---------------------------------------------------------------------------
enum Kind : uint8_t
{
    kShark, kOrderCar, kPhone, kSwim,             // Chase (the player says it)
    kThanks, kThanksGift, kGotAway, kWitness,     // a passer-by (life.h's people)
    kKindCount
};
static const char* const kKindName[kKindCount] = {
    "shark", "order car", "phone", "swim", "thanks", "thanks (gift)", "got away", "witness",
};
inline bool IsPed(int k) { return k >= kThanks && k < kKindCount; }

// Chase's own lines: one manifest each, its conversations (one line apiece, speaker slot 0).
struct ChaseLine { const char* manifest; uint8_t n; const char* conv[6]; };
static const ChaseLine kChase[4] = {
    { "EatenByShark",   1, { "EatenByShark" } },
    { "OrderNewCar",    1, { "OrderNewCar" } },
    { "AnswerThePhone", 3, { "AnswerThePhone_001", "AnswerThePhone_002", "AnswerThePhone_003" } },
    // 004 "I'm gonna' need to swim over there." is a puzzle hint: left out
    { "PlayerHint_GeneralSwim", 5, { "PlayerHint_GeneralSwim_001", "PlayerHint_GeneralSwim_002", "PlayerHint_GeneralSwim_003",
                                     "PlayerHint_GeneralSwim_005", "PlayerHint_GeneralSwim_006" } },
};

// The passers-by. region: the voice's accent.
enum Region : uint8_t { kTown, kHill, kSurf };
static const char* const kRegionName[3] = { "town", "hillbilly", "surfer" };

// tags on the disappointed lines: W = fits a crash witness ("Oh no!"), D = said AT someone who let
// them down ("You're useless!") - only for the thief who got away
enum : uint8_t { kTagW = 1, kTagD = 2 };

struct PedLine { const char* manifest; uint8_t female; uint8_t region; uint8_t n; uint8_t tag; const char* conv[2]; const char* text[2]; };

static const PedLine kThanksLines[] = {
    { "Thanks_MaleGenped1", 0, kTown, 2, 0, { "Thanks_MaleGenped1_001", "Thanks_MaleGenped1_002" }, { "Thanks!", "Aw, thanks man!" } },
    { "Thanks_MaleGenped2", 0, kTown, 2, 0, { "Thanks_MaleGenped2_001", "Thanks_MaleGenped2_002" }, { "Excellent!", "You just made my day!" } },
    { "Thanks_MaleGenped3", 0, kTown, 2, 0, { "Thanks_MaleGenped3_001", "Thanks_MaleGenped3_002" }, { "Thanks a lot, buddy!", "Cheers!" } },
    { "Thanks_MaleGenped4", 0, kTown, 2, 0, { "Thanks_MaleGenped4_001", "Thanks_MaleGenped4_002" }, { "Sensational!", "Much appreciated!" } },
    { "Thanks_MaleGenped5", 0, kTown, 2, 0, { "Thanks_MaleGenped5_001", "Thanks_MaleGenped5_002" }, { "Awesome!", "You da' man!" } },
    { "Thanks_MaleGenped6", 0, kTown, 2, 0, { "Thanks_MaleGenped6_001", "Thanks_MaleGenped6_002" }, { "Ta' muchly!", "I'm very grateful." } },
    { "Thanks_MaleGenped7", 0, kTown, 2, 0, { "Thanks_MaleGenped7_001", "Thanks_MaleGenped7_002" }, { "You rock!", "Great!" } },
    { "Thanks_MaleGenped8", 0, kTown, 2, 0, { "Thanks_MaleGenped8_001", "Thanks_MaleGenped8_002" }, { "Phew! Thanks!", "That's a relief!" } },
    { "Thanks_FemaleGenped1", 1, kTown, 2, 0, { "Thanks_FemaleGenped1_001", "Thanks_FemaleGenped1_002" }, { "Nice one!", "Thanks for that!" } },
    { "Thanks_FemaleGenped2", 1, kTown, 2, 0, { "Thanks_FemaleGenped2_001", "Thanks_FemaleGenped2_002" }, { "You're the greatest!", "Thank you so much!" } },
    { "Thanks_FemaleGenped3", 1, kTown, 2, 0, { "Thanks_FemaleGenped3_001", "Thanks_FemaleGenped3_002" }, { "You complete me! Uh, I mean thanks!", "You're my hero!" } },
    { "Thanks_FemaleGenped4", 1, kTown, 2, 0, { "Thanks_FemaleGenped4_001", "Thanks_FemaleGenped4_002" }, { "Amazing!", "That's wonderful!" } },
    { "Thanks_FemaleGenped5", 1, kTown, 2, 0, { "Thanks_FemaleGenped5_001", "Thanks_FemaleGenped5_002" }, { "Thank you!", "That's such a relief!" } },
    { "Thanks_FemaleGenped6", 1, kTown, 2, 0, { "Thanks_FemaleGenped6_001", "Thanks_FemaleGenped6_002" }, { "Great work!", "You're the best!" } },
    { "Thanks_FemaleGenped7", 1, kTown, 2, 0, { "Thanks_FemaleGenped7_001", "Thanks_FemaleGenped7_002" }, { "You're great!", "You're amazing!" } },
    { "Thanks_FemaleGenped8", 1, kTown, 2, 0, { "Thanks_FemaleGenped8_001", "Thanks_FemaleGenped8_002" }, { "Thanks!", "Thanks a lot!" } },
    { "Thanks_MaleHillbilly1", 0, kHill, 1, 0, { "Thanks_MaleHillbilly1_001" }, { "Thank you, sir!" } },
    { "Thanks_MaleHillbilly2", 0, kHill, 1, 0, { "Thanks_MaleHillbilly2_001" }, { "Well ain't you somethin' special!" } },
    { "Thanks_MaleHillbilly3", 0, kHill, 1, 0, { "Thanks_MaleHillbilly3_001" }, { "Way to go!" } },
    { "Thanks_MaleHillbilly4", 0, kHill, 1, 0, { "Thanks_MaleHillbilly4_001" }, { "Woo-hoo!" } },
    { "Thanks_FemaleHillbilly1", 1, kHill, 1, 0, { "Thanks_FemaleHillbilly1_001" }, { "Outstanding!" } },
    { "Thanks_FemaleHillbilly2", 1, kHill, 1, 0, { "Thanks_FemaleHillbilly2_001" }, { "Fantastic!" } },
    { "Thanks_FemaleHillbilly3", 1, kHill, 1, 0, { "Thanks_FemaleHillbilly3_001" }, { "I am truly grateful." } },
    { "Thanks_FemaleHillbilly4", 1, kHill, 1, 0, { "Thanks_FemaleHillbilly4_001" }, { "Thanks, Mr!" } },
    { "Thanks_MaleSurfdude1", 0, kSurf, 1, 0, { "Thanks_MaleSurfdude1_001" }, { "Dude!" } },
    { "Thanks_MaleSurfdude2", 0, kSurf, 1, 0, { "Thanks_MaleSurfdude2_001" }, { "Nice one!" } },
    { "Thanks_MaleSurfdude3", 0, kSurf, 1, 0, { "Thanks_MaleSurfdude3_001" }, { "Gnarly!" } },
    { "Thanks_FemaleSurfdude1", 1, kSurf, 1, 0, { "Thanks_FemaleSurfdude1_001" }, { "Grats." } },
    { "Thanks_FemaleSurfdude2", 1, kSurf, 1, 0, { "Thanks_FemaleSurfdude2_001" }, { "Most grateful." } },
    { "Thanks_FemaleSurfdude3", 1, kSurf, 1, 0, { "Thanks_FemaleSurfdude3_001" }, { "Nice..." } },
};
static const int kNumThanks = static_cast<int>(sizeof(kThanksLines) / sizeof(kThanksLines[0]));

static const PedLine kNoThanksLines[] = {
    { "NoThanks_MaleGenped1", 0, kTown, 1, kTagW, { "NoThanks_MaleGenped1_001" }, { "Aw, no..." } },
    { "NoThanks_MaleGenped2", 0, kTown, 1, kTagW, { "NoThanks_MaleGenped2_001" }, { "Aw, man!" } },
    { "NoThanks_MaleGenped3", 0, kTown, 1, kTagW, { "NoThanks_MaleGenped3_001" }, { "Oh no!" } },
    { "NoThanks_MaleGenped4", 0, kTown, 1, kTagW, { "NoThanks_MaleGenped4_001" }, { "Well, that's not good." } },
    { "NoThanks_MaleGenped5", 0, kTown, 1, kTagD, { "NoThanks_MaleGenped5_001" }, { "Aw, c'mon!" } },
    { "NoThanks_MaleGenped6", 0, kTown, 1, 0,     { "NoThanks_MaleGenped6_001" }, { "Ah well..." } },
    { "NoThanks_MaleGenped7", 0, kTown, 1, kTagW, { "NoThanks_MaleGenped7_001" }, { "This is terrible!" } },
    { "NoThanks_MaleGenped8", 0, kTown, 1, 0,     { "NoThanks_MaleGenped8_001" }, { "Rats!" } },
    { "NoThanks_FemaleGenped1", 1, kTown, 1, kTagW, { "NoThanks_FemaleGenped1_001" }, { "Oh dear." } },
    { "NoThanks_FemaleGenped2", 1, kTown, 1, kTagW, { "NoThanks_FemaleGenped2_001" }, { "That's terrible." } },
    { "NoThanks_FemaleGenped3", 1, kTown, 1, 0,     { "NoThanks_FemaleGenped3_001" }, { "Aw..." } },
    { "NoThanks_FemaleGenped4", 1, kTown, 1, 0,     { "NoThanks_FemaleGenped4_001" }, { "Oh... noodles!" } },
    { "NoThanks_FemaleGenped5", 1, kTown, 1, kTagW, { "NoThanks_FemaleGenped5_001" }, { "Terrible. Just terrible." } },
    { "NoThanks_FemaleGenped6", 1, kTown, 1, kTagD, { "NoThanks_FemaleGenped6_001" }, { "You're useless!" } },
    { "NoThanks_FemaleGenped7", 1, kTown, 1, 0,     { "NoThanks_FemaleGenped7_001" }, { "That's my day ruined." } },
    { "NoThanks_FemaleGenped8", 1, kTown, 1, kTagW, { "NoThanks_FemaleGenped8_001" }, { "This is bad." } },
    { "NoThanks_MaleHillbilly1", 0, kHill, 1, kTagW, { "NoThanks_MaleHillbilly1_001" }, { "Aw shucks!" } },
    { "NoThanks_MaleHillbilly2", 0, kHill, 1, kTagW, { "NoThanks_MaleHillbilly2_001" }, { "Dagnabbit!" } },
    { "NoThanks_MaleHillbilly3", 0, kHill, 1, kTagW, { "NoThanks_MaleHillbilly3_001" }, { "Goldurnit!" } },
    { "NoThanks_MaleHillbilly4", 0, kHill, 1, kTagW, { "NoThanks_MaleHillbilly4_001" }, { "Drat!" } },
    { "NoThanks_FemaleHillbilly1", 1, kHill, 1, kTagW, { "NoThanks_FemaleHillbilly1_001" }, { "Aw, heck!" } },
    { "NoThanks_FemaleHillbilly2", 1, kHill, 1, 0,     { "NoThanks_FemaleHillbilly2_001" }, { "Darn." } },
    { "NoThanks_FemaleHillbilly3", 1, kHill, 1, kTagD, { "NoThanks_FemaleHillbilly3_001" }, { "Typical..." } },
    { "NoThanks_FemaleHillbilly4", 1, kHill, 1, kTagD, { "NoThanks_FemaleHillbilly4_001" }, { "I knew it!" } },
    { "NoThanks_MaleSurfdude1", 0, kSurf, 1, kTagW, { "NoThanks_MaleSurfdude1_001" }, { "Aw, dude..." } },
    { "NoThanks_MaleSurfdude2", 0, kSurf, 1, kTagW, { "NoThanks_MaleSurfdude2_001" }, { "That is not cool." } },
    { "NoThanks_MaleSurfdude3", 0, kSurf, 1, kTagW, { "NoThanks_MaleSurfdude3_001" }, { "Bogus." } },
    { "NoThanks_FemaleSurfdude1", 1, kSurf, 1, kTagW, { "NoThanks_FemaleSurfdude1_001" }, { "Aw, man..." } },
    { "NoThanks_FemaleSurfdude2", 1, kSurf, 1, kTagD, { "NoThanks_FemaleSurfdude2_001" }, { "C'mon!" } },
    { "NoThanks_FemaleSurfdude3", 1, kSurf, 1, kTagD, { "NoThanks_FemaleSurfdude3_001" }, { "You're lame!" } },
};
static const int kNumNoThanks = static_cast<int>(sizeof(kNoThanksLines) / sizeof(kNoThanksLines[0]));

// ---- the voice that fits the speaker ---------------------------------------------------------------
inline bool HasI(const char* s, const char* sub)
{
    if (!s || !sub || !*sub) return false;
    const size_t m = std::strlen(sub);
    for (; *s; ++s)
    {
        size_t i = 0;
        for (; i < m; ++i)
        {
            char a = s[i], b = sub[i];
            if (!a) return false;
            if (a >= 'A' && a <= 'Z') a = static_cast<char>(a - 'A' + 'a');
            if (b >= 'A' && b <= 'Z') b = static_cast<char>(b - 'A' + 'a');
            if (a != b) break;
        }
        if (i == m) return true;
    }
    return false;
}

// The game's district index (radio.h kDistrictFill): 2 fort_meadows, 3 bluebell (the farmland and the
// national park), 11 paradise_sands (the beach). The speaker's own def name wins over the place.
inline uint8_t RegionOf(int district, const char* defName)
{
    static const char* const surf[] = { "surf", "hula", "lifeguard", "beach", "bikini", "swim" };
    static const char* const hill[] = { "farmer", "hillbilly", "cowboy", "cowgirl", "ranger", "lumberjack", "sheriff", "miner" };
    for (const char* k : surf) if (HasI(defName, k)) return kSurf;
    for (const char* k : hill) if (HasI(defName, k)) return kHill;
    if (district == 2 || district == 3) return kHill;
    if (district == 11) return kSurf;
    return kTown;
}

// A stable small number per speaker (his address), so one person keeps one voice.
inline uint32_t Mix(uint64_t v)
{
    v ^= v >> 33; v *= 0xff51afd7ed558ccdull; v ^= v >> 33; v *= 0xc4ceb9fe1a85ec53ull; v ^= v >> 33;
    return static_cast<uint32_t>(v);
}

struct Pick { const char* manifest = nullptr; const char* conv = nullptr; const char* text = nullptr; uint8_t region = kTown; };

// The line for a passer-by: kind (kThanks / kThanksGift / kGotAway / kWitness), gender, region,
// speaker seed (the voice) and a roll (which of the voice's lines). Falls back to the town voices
// when the region has none that fit (the witness lines need tag W).
inline Pick PickPed(int kind, bool female, uint8_t region, uint32_t seed, uint32_t roll)
{
    Pick out;
    const bool thanks = kind == kThanks || kind == kThanksGift;
    const PedLine* tab = thanks ? kThanksLines : kNoThanksLines;
    const int n = thanks ? kNumThanks : kNumNoThanks;
    for (int pass = 0; pass < 2 && !out.manifest; ++pass)
    {
        const uint8_t want = pass == 0 ? region : static_cast<uint8_t>(kTown);
        int idx[32], m = 0;
        for (int i = 0; i < n && m < 32; ++i)
        {
            const PedLine& l = tab[i];
            if ((l.female != 0) != female || l.region != want) continue;
            if (kind == kWitness && !(l.tag & kTagW)) continue;          // an onlooker: a reaction, not a reproach
            idx[m++] = i;
        }
        if (m == 0) continue;
        const PedLine& l = tab[idx[seed % static_cast<uint32_t>(m)]];
        const int k = static_cast<int>(roll % l.n);
        out.manifest = l.manifest; out.conv = l.conv[k]; out.text = l.text[k]; out.region = want;
    }
    return out;
}

// ---- the knobs ([voice] in lcudrive.ini) -----------------------------------------------------------
struct Params
{
    bool  on[kKindCount] = { true, true, true, true, true, true, true, true };
    bool  once = true;            // Chase's shark / order-car / swim lines once per session per player
    float chaseCooldown = 300.0f; // s before the same Chase line again (once = 0)
    float phoneCooldown = 60.0f;  // s between two "answer that call" lines
    float pedGap = 12.0f;         // s between any two passers-by's lines
    float sharkFeetS = 1.0f;      // s back on his feet (respawned) before Chase comments
    float sharkWetS = 4.0f;       // a death this soon after being in the water counts as the shark
    float sharkWaitS = 30.0f;     // ...and gives up if he isn't back on his feet by then
    float swimS = 1.5f;           // s of swimming before "Time for a dip."
};

// ---- one player's moment ---------------------------------------------------------------------------
struct PIn
{
    bool valid = false;           // the player is in the game
    bool swimming = false;        // the Swimming context
    bool drowning = false;        // the Drowning context
    bool dead = false;            // the Death context or the dead test
    bool feet = false;            // the Default context (on foot, standing / walking), not dead
    bool vehicle = false;         // in a vehicle
    bool deadly = false;          // the level's deep water is deadly right now (deadly_deep_water > 0)
    bool dropPoint = false;       // the DropPoint context (at a car call-in point)
};

struct PState
{
    float wetAgo = 1e9f;          // s since last in the water
    bool  armed = false;          // the shark took him: say it once he's back
    float armedT = 0.0f;          // s since
    float feetT = 0.0f;           // s on his feet since
    bool  wasDrown = false, wasDead = false, wasDrop = false;
    float swimT = 0.0f;
    float lastSaid[4] = { -1e9f, -1e9f, -1e9f, -1e9f };   // session time of each Chase line (shark, car, -, swim)
    bool  said[4] = {};
};

// One tick for one player: bit k set = Chase line k is due now (kShark, kOrderCar, kSwim).
inline uint32_t StepPlayer(const Params& p, PState& s, const PIn& in, float dt, float now)
{
    uint32_t due = 0;
    if (!in.valid) { s.wasDrown = s.wasDead = s.wasDrop = false; s.swimT = 0.0f; s.feetT = 0.0f; return 0; }
    const bool wet = in.swimming || in.drowning;
    s.wetAgo = wet ? 0.0f : s.wetAgo + dt;
    auto may = [&](int k) { return p.on[k] && !(p.once && s.said[k]) && now - s.lastSaid[k] >= p.chaseCooldown; };

    // the shark: drowning in deadly water, or dying right after being in it
    const bool drownEdge = in.drowning && !s.wasDrown;
    const bool deathEdge = in.dead && !s.wasDead;
    if (in.deadly && (drownEdge || (deathEdge && s.wetAgo <= p.sharkWetS)) && !s.armed && may(kShark))
    { s.armed = true; s.armedT = 0.0f; s.feetT = 0.0f; }
    if (s.armed)
    {
        s.armedT += dt;
        const bool back = in.feet && !in.dead && !in.drowning && !in.swimming && !in.vehicle;
        s.feetT = back ? s.feetT + dt : 0.0f;
        if (s.feetT >= p.sharkFeetS) { due |= 1u << kShark; s.armed = false; }
        else if (s.armedT > p.sharkWaitS) s.armed = false;
    }

    // a car call-in point: on entering its context
    if (in.dropPoint && !s.wasDrop && may(kOrderCar)) due |= 1u << kOrderCar;

    // the first swim (not while the shark has him)
    s.swimT = in.swimming && !in.drowning && !in.dead ? s.swimT + dt : 0.0f;
    if (s.swimT >= p.swimS && !s.armed && may(kSwim)) { due |= 1u << kSwim; s.swimT = -1e9f; }

    s.wasDrown = in.drowning; s.wasDead = in.dead; s.wasDrop = in.dropPoint;
    return due;
}

// The line was handed to the game (voice.h): the once / cooldown bookkeeping.
inline void Said(PState& s, int k, float now) { if (k >= 0 && k < 4) { s.said[k] = true; s.lastSaid[k] = now; } }

// ---- the video call that rang out ------------------------------------------------------------------
struct PhoneState { bool wasMissed = false; float last = -1e9f; int next = 0; };

// missed: the game's "missed video call" byte (0x14183F768: set once a call rang 3 times unanswered).
// Returns the AnswerThePhone conversation index to play, or -1.
inline int StepPhone(const Params& p, PhoneState& s, bool missed, float now)
{
    int out = -1;
    if (missed && !s.wasMissed && p.on[kPhone] && now - s.last >= p.phoneCooldown)
    {
        out = s.next; s.next = (s.next + 1) % kChase[kPhone].n; s.last = now;
    }
    s.wasMissed = missed;
    return out;
}

// ---- the passers-by's turn -------------------------------------------------------------------------
struct PedState { float last = -1e9f; };
inline bool PedMay(const Params& p, const PedState& s, int kind, float now)
{
    return IsPed(kind) && p.on[kind] && now - s.last >= p.pedGap;
}

// ---- one request through the game's dialogue system ------------------------------------------------
// Load the manifest (async: the .txt parsed, then its _pc.pak), wait while another conversation
// (a story line) is playing - at most quietS, then say it anyway - set the speaker and play, wait
// for the line to finish, linger a moment and unload. Every wait has a timeout.
enum JobState : uint8_t { kJobIdle, kJobLoading, kJobWaitQuiet, kJobPlaying, kJobLinger, kJobDone };
enum JobAct : uint8_t { kActNone, kActLoad, kActPlay, kActUnload, kActDrop };

struct Job
{
    JobState st = kJobIdle;
    float t = 0.0f;               // s in this state
    float age = 0.0f;             // s since queued
    bool  loaded = false;         // our Load was issued (an Unload is owed)
    bool  finished = false;       // the game reported the line finished (not the timeout)
};
struct JobLimits { float loadS = 8.0f, quietS = 10.0f, playMaxS = 12.0f, minPlayS = 0.4f, lingerS = 1.0f; };

struct JobIn
{
    bool ready = false;           // the manifest parsed and its pak loaded
    bool finished = false;        // the line reports finished
    bool speaker = false;         // the speaker is still there
    bool busy = false;            // another conversation is playing (a story line): wait
};

inline JobAct StepJob(Job& j, const JobLimits& L, const JobIn& in, float dt)
{
    j.t += dt; j.age += dt;
    switch (j.st)
    {
    case kJobIdle: j.st = kJobLoading; j.t = 0.0f; j.loaded = true; return kActLoad;
    case kJobLoading:
        if (!in.speaker) { j.st = kJobDone; return j.loaded ? kActUnload : kActDrop; }
        if (in.ready) { j.st = kJobWaitQuiet; j.t = 0.0f; return StepJob(j, L, in, 0.0f); }
        if (j.t > L.loadS) { j.st = kJobDone; return kActUnload; }
        return kActNone;
    case kJobWaitQuiet:
        if (!in.speaker) { j.st = kJobDone; return kActUnload; }
        // a story line in progress: wait for it, but not forever (then say it anyway)
        if (!in.busy || j.t > L.quietS) { j.st = kJobPlaying; j.t = 0.0f; return kActPlay; }
        return kActNone;
    case kJobPlaying:
        if ((in.finished && j.t >= L.minPlayS) || j.t > L.playMaxS) { j.finished = in.finished; j.st = kJobLinger; j.t = 0.0f; }
        return kActNone;
    case kJobLinger:
        if (j.t >= L.lingerS) { j.st = kJobDone; return kActUnload; }
        return kActNone;
    default: return kActNone;
    }
}

} // namespace vm
