// =============================================================================================
//  busted_model.h - "busted: fade out, wake up OUTSIDE the police station" (2026-11-18, busted1).
//  The pure part: no engine, no Windows - Drive\test\wanted_harness.cpp runs it.
//
//  Why: the old station trip is the pause menu's "return to the police station", a LEVEL CHANGE
//  into the lobby interior - it takes the whole game, so the co-op partner went too. This moves
//  ONE character inside the open city instead, the way the game's own scripts do a far move
//  (train_control.sf, played): fade, Teleport + LockInPlace, wait for the place to load,
//  LockInPlace(false) + Teleport again, fade back.
//
//  One Seq per player. The engine side (wanted_busted.h) calls Step every wanted tick and does
//  what the returned Act bits say, in this order: aDismount, aUnlock, aTeleport (and tells the
//  model how it went: Moved), aLock, aFace.
//
//    kFadeOut   the black comes up over fadeS (the player is still where he was busted: cuffed
//               on foot, or sitting in the stopped vehicle)
//    kExit      riding anything (car, boat, helicopter, a mount): the game's own ClearVehicle,
//               then wait until he is out (exitMinS..exitS; still aboard after that = give up)
//    kMove      unlock (the cuffs), Teleport, lock ("idle": no gravity, so nothing falls through
//               ground that hasn't streamed in yet). A refused teleport is retried for teleS.
//    kHold      black, locked: holdNearS when the jump was short (the cell is loaded), holdFarS
//               when it was far (the station's cells stream in around him)
//    kLand      unlock, Teleport again (with the heading): on the ground, or landS at most
//    kFadeIn    the black goes over fadeS; the bust is over for the wanted system (Release)
//    kWatch     watchS more of looking for a fall through the world (the cell wasn't in after
//               all): back to kMove with the far hold, maxRetries times, then left to the game
//
//  The other player is never an input and never an output: nothing here can touch them.
//  Every exit unlocks; the whole thing ends by itself after maxS whatever happens.
// =============================================================================================
#pragma once
#include <cstdint>

namespace busted {

struct Params
{
    float fadeS      = 0.5f;    // s for the fade, each way (0 = a cut)
    float exitMinS   = 0.30f;   // s at least between the dismount and the move
    float exitS      = 1.5f;    // s to be out of the vehicle before giving up
    float teleS      = 1.5f;    // s of retrying a refused teleport before giving up
    float teleEveryS = 0.25f;   // ...one try this often
    float nearU      = 45.0f;   // a jump shorter than this lands in loaded cells (the streaming grid is 30 u)
    float holdNearS  = 0.6f;    // s black and locked after a short jump
    float holdFarS   = 2.5f;    // ...after a far one, and after a fall through the world
    float landS      = 1.0f;    // s at most waiting to stand on the ground before the fade in
    float fallU      = 2.5f;    // this far below the spot = fell through
    float watchS     = 3.0f;    // s of watching for a fall after the fade in
    int   maxRetries = 3;       // falls put right before it is left to the game
    float maxS       = 25.0f;   // the whole sequence, at most
};

enum Phase : uint8_t { kIdle, kFadeOut, kExit, kMove, kHold, kLand, kFadeIn, kWatch };

enum Act : uint32_t
{
    aNone     = 0,
    aDismount = 1,      // the game's ClearVehicle
    aUnlock   = 2,      // out of LockInPlace (the cuffs, or our "idle")
    aTeleport = 4,      // to the spot; answer with Moved()
    aLock     = 8,      // LockInPlace "idle" - only if the teleport was done
    aFace     = 16,     // set the heading
    aRelease  = 32,     // the wanted system's bust is over (once)
    aDone     = 64,     // the sequence is over (once)
    aGaveUp   = 128,    // with aRelease: it could not be done - he is let go where he is
    aFell     = 256,    // (the log) fell through the world: going again
};

struct Seq
{
    Phase   ph;
    float   t;          // in this phase
    float   total;      // since Start
    float   fade;       // 0..1 black, for this player's view
    float   tryT;       // kMove: since the last teleport try
    bool    farJump;    // the long hold
    bool    asked;      // kExit: the dismount was asked for / kMove: a try is out
    bool    moved;      // a teleport was done
    bool    released;   // aRelease was given
    int     retries;
    int     gaveUp;     // 0, or why: 1 still aboard, 2 teleport refused, 3 fell too often, 4 took too long, 5 the player is gone
};

struct Input
{
    float dt;
    bool  present;      // the same character is still there
    bool  riding;       // in / on anything
    bool  grounded;     // standing on something
    bool  fell;         // below the spot by more than fallU (only looked at after a move)
};

// Does a bust end outside the station? [wanted] busted_outside, and busted_station = 0 ("let go on the
// spot, no station at all") still wins. false = wanted.h's old lines run untouched.
inline bool Will(int bustedOutside, int bustedStation) { return bustedOutside != 0 && bustedStation != 0; }

inline void Reset(Seq& s) { s = Seq{}; }
inline bool Busy(const Seq& s) { return s.ph != kIdle; }
// the wanted system's bust still belongs to the sequence (he is not free yet)
inline bool Holding(const Seq& s) { return s.ph != kIdle && !s.released; }

inline void Start(Seq& s, float jumpU, const Params& p)
{
    s = Seq{};
    s.ph = kFadeOut;
    s.farJump = jumpU > p.nearU;
}

inline uint32_t End(Seq& s, int why)
{
    uint32_t a = aUnlock | aDone;
    if (!s.released) a |= aRelease;
    if (why) a |= aGaveUp;
    const int w = why;
    s = Seq{};
    s.gaveUp = w;
    return a;
}

// The answer to aTeleport, the same tick. ok = the engine did it.
inline void Moved(Seq& s, bool ok)
{
    if (s.ph == kMove)
    {
        if (ok) { s.ph = kHold; s.t = 0.0f; s.moved = true; }
        else    { s.asked = false; }                 // tried again in teleEveryS
    }
    // kLand's second teleport: he is there already, a refusal changes nothing
}

inline uint32_t Step(Seq& s, const Input& in, const Params& p)
{
    if (s.ph == kIdle) return aNone;
    const float dt = in.dt > 0.1f ? 0.1f : in.dt < 0.0f ? 0.0f : in.dt;
    if (!in.present)                                  // gone (left the game, a level change): nothing to call
    {
        const bool rel = !s.released;
        s = Seq{}; s.gaveUp = 5;
        return aDone | aGaveUp | (rel ? aRelease : 0u);
    }
    s.t += dt; s.total += dt;
    if (s.total > p.maxS) return End(s, 4);

    switch (s.ph)
    {
    case kFadeOut:
        s.fade = p.fadeS > 0.0f ? (s.t >= p.fadeS ? 1.0f : s.t / p.fadeS) : 1.0f;
        if (s.fade >= 1.0f)
        {
            s.t = 0.0f; s.asked = false; s.tryT = 0.0f;
            s.ph = in.riding ? kExit : kMove;
        }
        return aNone;

    case kExit:
        s.fade = 1.0f;
        if (!s.asked) { s.asked = true; return aDismount; }
        if (!in.riding && s.t >= p.exitMinS) { s.ph = kMove; s.t = 0.0f; s.asked = false; s.tryT = 0.0f; return aNone; }
        if (s.t > p.exitS)
        {
            if (in.riding) { s.ph = kFadeIn; s.t = 0.0f; s.gaveUp = 1; s.released = true; return aRelease | aGaveUp; }
            s.ph = kMove; s.t = 0.0f; s.asked = false; s.tryT = 0.0f;
        }
        return aNone;

    case kMove:
        s.fade = 1.0f;
        if (in.riding && !s.moved) { s.ph = kExit; s.t = 0.0f; s.asked = false; return aNone; }   // got back in: out again
        if (s.asked) return aNone;                    // (Moved() always answers the same tick)
        s.tryT += dt;
        if (s.t <= dt * 1.5f || s.tryT >= p.teleEveryS)
        {
            if (s.t > p.teleS)                        // refused all along
            {
                const bool rel = !s.released;
                s.ph = kFadeIn; s.t = 0.0f; s.gaveUp = 2; s.released = true;
                return aUnlock | aGaveUp | (rel ? aRelease : 0u);
            }
            s.tryT = 0.0f; s.asked = true;
            return aUnlock | aTeleport | aLock;
        }
        return aNone;

    case kHold:
        s.fade = 1.0f;
        if (s.t >= (s.farJump || s.retries > 0 ? p.holdFarS : p.holdNearS))
        {
            s.ph = kLand; s.t = 0.0f;
            return aUnlock | aTeleport | aFace;
        }
        return aNone;

    case kLand:
        s.fade = 1.0f;
        if (in.fell) goto fell;
        if (in.grounded || s.t >= p.landS)
        {
            s.ph = kFadeIn; s.t = 0.0f;
            if (!s.released) { s.released = true; return aRelease; }
        }
        return aNone;

    case kFadeIn:
        if (s.moved && !s.gaveUp && in.fell) goto fell;
        s.fade = p.fadeS > 0.0f ? (s.t >= p.fadeS ? 0.0f : 1.0f - s.t / p.fadeS) : 0.0f;
        if (s.fade <= 0.0f)
        {
            if (s.moved && !s.gaveUp) { s.ph = kWatch; s.t = 0.0f; return aNone; }
            const int w = s.gaveUp;
            s = Seq{}; s.gaveUp = w;
            return aDone | (w ? aGaveUp : 0u);
        }
        return aNone;

    case kWatch:
        s.fade = 0.0f;
        if (in.fell) goto fell;
        if (s.t >= p.watchS) { s = Seq{}; return aDone; }
        return aNone;

    default:
        return aNone;
    }

fell:
    if (++s.retries > p.maxRetries) return End(s, 3);
    s.ph = kMove; s.t = 0.0f; s.asked = false; s.tryT = 0.0f; s.fade = 1.0f; s.farJump = true;
    return aFell;
}

// ---------------------------------------------------------------------------------------------
//  Where: the spot, moved aside when the other player stands on it (both busted one after the
//  other). `right` = the unit vector to the right of the heading.
// ---------------------------------------------------------------------------------------------
inline void Spot(const float dest[3], float headingDeg, bool otherThere, float out[3])
{
    out[0] = dest[0]; out[1] = dest[1]; out[2] = dest[2];
    if (!otherThere) return;
    const float h = headingDeg * 0.01745329252f;
    // heading: 0 = +z, 90 = +x (yaw = atan2(dx, dz)); right of it = (cos h, 0, -sin h)
    float c = 1.0f, sn = 0.0f;
    {   // small sin / cos without <cmath> (the harness and the DLL both have it, but keep this header bare)
        float x = h; while (x > 3.14159265f) x -= 6.2831853f; while (x < -3.14159265f) x += 6.2831853f;
        const float x2 = x * x;
        sn = x * (1.0f - x2 / 6.0f * (1.0f - x2 / 20.0f * (1.0f - x2 / 42.0f * (1.0f - x2 / 72.0f))));
        c = 1.0f - x2 / 2.0f * (1.0f - x2 / 12.0f * (1.0f - x2 / 30.0f * (1.0f - x2 / 56.0f * (1.0f - x2 / 90.0f))));
    }
    out[0] += 0.9f * c;
    out[2] -= 0.9f * sn;
}

// The engine's heading: 65536 to the turn (0x14066F720 multiplies by 2 pi / 65536).
inline uint16_t HeadingU16(float deg)
{
    float d = deg; while (d < 0.0f) d += 360.0f; while (d >= 360.0f) d -= 360.0f;
    return static_cast<uint16_t>(static_cast<uint32_t>(d * (65536.0f / 360.0f) + 0.5f) & 0xFFFFu);
}

// ---------------------------------------------------------------------------------------------
//  The black, for the shader (ShaderFixes\busted_fade.hlsl). What is published is not an opacity
//  but a DEADLINE on the shader's own clock (IniParams 0.x = seconds, d3dx.ini [Present]):
//      opacity = saturate((deadline - clock) / kRampS),   0 when deadline - clock > kStaleS
//  so if lcudrive stops publishing for any reason (a level change, a fault, the unhook, a
//  loading screen) the black is gone by itself within kOpaqueS + kRampS seconds - a view can
//  never be left black.
// ---------------------------------------------------------------------------------------------
static const float kRampS = 0.5f, kOpaqueS = 1.0f, kStaleS = 4.0f;
inline float Deadline(float clock, float fade)
{
    if (!(fade > 0.0f)) return 0.0f;                              // nothing: far in the past
    if (fade >= 1.0f) return clock + kRampS + kOpaqueS;
    return clock + kRampS * fade;
}
// the shader's side of it, for the tests
inline float OpacityAt(float deadline, float clock)
{
    const float d = deadline - clock;
    if (d <= 0.0f || d > kStaleS) return 0.0f;
    return d >= kRampS ? 1.0f : d / kRampS;
}

} // namespace busted
