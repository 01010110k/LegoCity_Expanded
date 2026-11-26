// =============================================================================================
//  lights.h - real headlights, using the engine's own vehicle light system.
//
//  What the engine already has (Notes\25-headlights.md): every car carries lamp attachments
//  (VehicleDefAttachment: "Front Left Head", "Rear Right Brake", ...). The headlights are flagged
//  Nighttime Only, and each lamp instance owns a DynamicLight spot object from spawn. The per-frame
//  light update 0x1404D7A90 turns a Nighttime Only lamp on only when the vehicle owner's night byte
//  [[veh+0x330]+0x179] is set - the daytime hub never sets it - and lets the DYNAMIC light cast
//  only when the lamp's "Disable Dynamics" is 0 (it is 1 on every shipped car) and the instance
//  holds a cookie texture (the beam shape; only police cars resolve one: HEADLIGHT_SINGLEBEAM).
//
//  So, at night (decided from the same clock the day/night shaders use):
//    * every vehicle owner's night byte is set -> all traffic shows its headlight glow, the way
//      TT's own night levels look;
//    * the player's car gets real light on the road: each of its headlight instances is pointed
//      at a PRIVATE copy of its lamp definition (dynamics on, range/FOV/brightness from the ini)
//      and given a reference to a cookie texture borrowed from the first car seen that has one.
//      The engine's own light update then drives the spot every frame.
//  The shared definitions are never written. Everything is re-derived every frame from what the
//  live lamp lists hold, so nothing depends on bookkeeping surviving a despawn or a level change:
//  any lamp still pointing at one of our copies when it should not is simply pointed back.
//
//  WHICH WAY A LAMP SHINES (2026-11-14: the Sphinx car's headlights pointed backwards)
//  The light update builds each lamp's world matrix as LocatorOffset (def+0x118) x the model's lamp
//  locator, turned half round its up axis (0x1404A8B20 with its last argument 1: 0x1404A8BF9..C7F)
//  x the vehicle's matrix veh+0x210, and hands row 2 to the light as its axis (0x140067810 <-
//  0x1404D827C). The spot looks from its place ALONG +row 2 (0x1403503B0: a look-at from
//  pos - row2*d to pos + row2, 0x1402D3BF0, with a +z projection, 0x1402D63D0) [P]. A model's lamp
//  locators sit square in the car (the four models read: identity), so an untouched LocatorOffset
//  shines ahead. But the artists also used the LocatorOffset to turn the lamp's BULB mesh onto the
//  model's brick, and with dynamics off on every car nobody saw where that left the light: of the
//  226 headlight lamps in the vehicle records, MUSCLECARS_SPHINX's and UBER_BRUTE's two are turned
//  half round (they shine backwards) and SaloonCars_Arbalest's two a quarter up (into the sky).
//  [lights] headlight_autofix measures each lit lamp against the car (the engine's own joint
//  function, the car's own rows) and turns the copy's LocatorOffset so the axis is ahead and up is
//  up; the Bulb Offset and scales are turned back by the same amount, so the bulb stays where the
//  artist put it, and the beam mesh goes with the light.
// =============================================================================================
#pragma once

namespace lights {

typedef void*(__fastcall* FnAssign)(void** dst, void** src);   // intrusive smart-pointer assign
static FnAssign g_assign = nullptr;                            // (Release = vtbl+0x18, AddRef +0x10)

static const size_t    kAttSize   = 0x12D0;     // sizeof(VehicleDefAttachment), PC build
static const uintptr_t AD_NAME    = 0x18;       // AttachName, char[0x80]
static const uintptr_t AD_COLOUR  = 0xEC0;      // Colour (3 floats)
static const uintptr_t AD_FOV     = 0xECC;      // FoV, degrees
static const uintptr_t AD_RANGE   = 0xED4;      // range floor (unregistered field after Aspect)
static const uintptr_t AD_FALLOFF = 0xED8;      // FallOff: the reach proper - AND the beam mesh width
static const uintptr_t AD_BEAM_SX = 0x10C0;     // Beam Static Scale.x (the visible beam mesh's width)
static const uintptr_t AD_BEAM_SY = 0x10C4, AD_BEAM_SZ = 0x10C8;   // ...y, and z = its LENGTH (0x1404D85AE..)
static const uintptr_t AD_ALWAYS  = 0x1234, AD_BRAKE = 0x1235, AD_NODYN = 0x1236;
static const uintptr_t AD_NIGHT   = 0x1237, AD_ADV   = 0x1238, AD_HORN  = 0x1239;
static const uintptr_t AD_NOGAME  = 0x123A;     // Exclude from Game
static const uintptr_t VEH_OWNER  = 0x330, OWN_NIGHT = 0x179;
static const uintptr_t VEH_LIGHTS = 0x598, LI_NEXT = 0x08, LI_DEF = 0x68, LI_COOKIE = 0xB0;
static const uintptr_t LI_JOINT   = 0xA8;       // the lamp's locator index in the model (0x1404D7DD9)
static const uintptr_t AD_LOCOFF  = 0x118;      // LocatorOffset, Mtx: rows right / up / the light's axis / place
static const uintptr_t AD_BULB_OFF = 0x1154;    // Bulb Offset, Mtx
static const uintptr_t AD_BULB_SX = 0x11A0;     // Bulb Static Scale x, y, z (0x1404D878F..)
static const uintptr_t AD_BULB_DYN = 0x11B0;    // Bulb Dynamic ScaleX / Y / Z, one byte each
static const int       kMaxInst   = 32;

static volatile bool g_release = false;      // Unhook: give everything back on the game thread
static bool     g_night = false;             // current decision (hysteresis on the sun elevation)
static uint32_t g_clockSeen = 0;
static uint32_t g_lastTick = 0;              // last time any vehicle updated
static volatile float g_sunPhase = -1.0f;    // 0 sunrise, pi/2 noon, pi sunset, 3pi/2 midnight (-1 = no clock)

// Resource-handle resolve (out, handle, index): out = the handle's container entry, AddRef'd; a
// handle with no entry gives the global default resource [0x141C27C20] instead. Every lamp's three
// texture handles (cookie +0x98, beam +0x78, bulb +0x88) point into the same container, so they
// resolve to the same class, whose texture smart pointer sits at +0x130 (the cookie path uses it).
typedef void*(__fastcall* FnResolve)(void** out, void* handle, int index);
static FnResolve g_resolve = nullptr;
static void**    g_defaultRes = nullptr;
static uintptr_t g_imgLo = 0, g_imgHi = 0;

// A lamp locator's matrices (veh, locator index, out local, out world = local x veh+0x210, turn): the
// light update's own call (0x1404D7DEF), with turn = 1 as there - the locator turned half round its up
// axis, which is what makes an untouched lamp's row 2 point ahead.
typedef bool (__fastcall* FnJoint)(void* veh, int joint, float* local16, float* world16, bool turn);
static FnJoint g_joint = nullptr;

static void Init(uintptr_t base)
{
    g_assign     = reinterpret_cast<FnAssign>(base + 0x371450);
    g_resolve    = reinterpret_cast<FnResolve>(base + 0x3438C0);
    g_defaultRes = reinterpret_cast<void**>(base + 0x1C27C20);
    g_joint      = reinterpret_cast<FnJoint>(base + 0x4A8B20);
    g_imgLo = base; g_imgHi = base + 0x223E000;
}

static bool InImage(const void* p)
{
    const uintptr_t a = reinterpret_cast<uintptr_t>(p);
    return a >= g_imgLo && a < g_imgHi;
}

// A ref-counted engine object: its vtable and its AddRef / Release live in the game image.
static bool LooksRefCounted(void* obj)
{
    if (!PtrOk(obj)) return false;
    void** vt = At<void**>(obj, 0);
    return InImage(vt) && InImage(vt[2]) && InImage(vt[3]);
}

// The object's class name from MSVC RTTI (the binary ships it): vtable[-1] -> CompleteObjectLocator
// (x64: signature 1, TypeDescriptor as an image-relative offset at +12) -> TypeDescriptor.name (+16).
static const char* ClassName(void* obj)
{
    if (!PtrOk(obj)) return nullptr;
    void** vt = At<void**>(obj, 0);
    if (!InImage(vt)) return nullptr;
    const uint8_t* col = static_cast<const uint8_t*>(vt[-1]);
    if (!InImage(col) || *reinterpret_cast<const uint32_t*>(col) != 1) return nullptr;
    const char* name = reinterpret_cast<const char*>(g_imgLo + *reinterpret_cast<const uint32_t*>(col + 12) + 16);
    return InImage(name) && name[0] == '.' && name[1] == '?' ? name : nullptr;
}

static bool ClassIs(void* obj, const char* exact) { const char* n = ClassName(obj); return n && !std::strcmp(n, exact); }
static bool ClassHas(void* obj, const char* part) { const char* n = ClassName(obj); return n && std::strstr(n, part); }

// ---- private lamp definitions: a static pool, so a pointer to one can never dangle ---------------
struct alignas(16) CopyRec { void* orig; uint8_t pad[8]; uint8_t data[kAttSize]; };
static const int kMaxCopies = 160;
static CopyRec   g_copies[kMaxCopies];
static int       g_nCopies = 0;
static bool      g_poolFullLogged = false;
// ...and, per copy, the turn that puts its light ahead (headlight_autofix; re-measured every second)
struct AimRec { uint8_t known, turn, dir; int8_t q[3][3]; uint32_t t; };
static AimRec    g_aim[kMaxCopies];

static int CopyIndex(const void* p)
{
    const uintptr_t a  = reinterpret_cast<uintptr_t>(p);
    const uintptr_t lo = reinterpret_cast<uintptr_t>(&g_copies[0].data);
    if (a < lo || a >= lo + sizeof(CopyRec) * kMaxCopies || (a - lo) % sizeof(CopyRec)) return -1;
    const int i = static_cast<int>((a - lo) / sizeof(CopyRec));
    return i < g_nCopies ? i : -1;
}

// The shipped definition behind `def` (itself when it is not one of ours).
static void* OrigOf(void* def)
{
    const int i = CopyIndex(def);
    return i >= 0 ? g_copies[i].orig : def;
}

static uint8_t* CopyOf(void* orig)
{
    for (int i = 0; i < g_nCopies; ++i) if (g_copies[i].orig == orig) return g_copies[i].data;
    if (g_nCopies >= kMaxCopies)
    {
        if (!g_poolFullLogged) { g_poolFullLogged = true; Log("    lights: lamp pool full - new car models keep stock lamps\n"); }
        return nullptr;
    }
    CopyRec& r = g_copies[g_nCopies];
    r.orig = orig;
    g_aim[g_nCopies] = AimRec{};
    ++g_nCopies;
    return r.data;
}

// ---- the night decision: sun elevation from the shader clock -------------------------------------
// IniParams[0].x = time + $tod; the day/night shaders put the sun at sin(clock * 0.01309).
static void UpdateNight(bool clockOk, float clock, const Config& cfg, uint32_t now)
{
    bool n = g_night;
    if (cfg.headlights == 2) n = true;
    else if (cfg.headlights == 0) n = false;
    else if (!clockOk) { if (now - g_clockSeen > 5000) n = false; }   // a brief bridge rescan keeps the answer
    else
    {
        g_clockSeen = now;
        const float elev = std::sin(clock * 0.01309f);
        const float ph = std::fmod(clock * 0.01309f, 6.2831853f);
        g_sunPhase = ph < 0.0f ? ph + 6.2831853f : ph;                 // the ambient life's daily rhythm
        if (!n && elev < cfg.night_below) n = true;
        else if (n && elev > cfg.night_below + 0.04f) n = false;
    }
    if (n != g_night)
    {
        g_night = n;
        Log(">>> lights: %s\n", n ? "night - headlights on" : "day - headlights off");
    }
}

// ---- every vehicle: the owner's night byte -----------------------------------------------------
// `orig` is the stock value. Should the byte ever hold something we did not write, the game set it
// (a night level, or a new owner at a reused address) and that becomes the stock value.
struct OwnerRec { void* p; uint8_t orig, wrote; uint32_t seen; };
static OwnerRec g_owners[16];

static void OwnerTick(void* veh, const Config& cfg, uint32_t now)
{
    void* own = At<void*>(veh, VEH_OWNER);
    if (!PtrOk(own)) return;
    uint8_t& b = At<uint8_t>(own, OWN_NIGHT);
    OwnerRec* rec = nullptr;
    for (auto& o : g_owners) if (o.p == own) { rec = &o; break; }
    if (!rec)
    {
        rec = &g_owners[0];
        for (auto& o : g_owners) if (!o.p || o.seen < rec->seen) rec = &o;    // free slot, else LRU
        rec->p = own; rec->orig = rec->wrote = b;
        Log("    lights: vehicle owner %p, stock night byte %u\n", own, b);
    }
    rec->seen = now;
    if (b != rec->wrote) rec->orig = b;
    const uint8_t want = (g_night && cfg.traffic_lights && !g_release) ? 1 : rec->orig;
    if (b != want) b = want;
    rec->wrote = want;
}

// ---- a cookie texture to lend: from the first car seen that resolved one -------------------------
static void* g_cookie = nullptr;             // our reference

static void HarvestCookie(void* veh)
{
    void* inst = At<void*>(veh, VEH_LIGHTS);
    for (int n = 0; PtrOk(inst) && n < kMaxInst; ++n, inst = At<void*>(inst, LI_NEXT))
    {
        if (CopyIndex(At<void*>(inst, LI_DEF)) >= 0) continue;    // one we lent it to
        if (!PtrOk(At<void*>(inst, LI_COOKIE))) continue;
        g_assign(&g_cookie, reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(inst) + LI_COOKIE));
        Log(">>> lights: beam texture borrowed (%p) - your headlights can light the road now\n", g_cookie);
        return;
    }
}

// Diagnostic, once per lamp definition: does any car name a beam texture, and does it resolve?
// (If no car ever resolves one, the texture is missing from the PC data, like the rain textures.)
static void* g_seenTex[48];
static int   g_nSeenTex = 0;

static void CensusCookies(void* veh)
{
    if (g_nSeenTex >= 48) return;
    void* inst = At<void*>(veh, VEH_LIGHTS);
    for (int n = 0; PtrOk(inst) && n < kMaxInst; ++n, inst = At<void*>(inst, LI_NEXT))
    {
        void* def = OrigOf(At<void*>(inst, LI_DEF));
        if (!PtrOk(def)) continue;
        const char* tex = reinterpret_cast<const char*>(def) + 0xFF4;      // "Light TexSpecial"
        if (!tex[0] || (tex[0] == 'N' && tex[1] == 'O' && tex[2] == 'N' && !tex[3])) continue;
        bool seen = false;
        for (int i = 0; i < g_nSeenTex; ++i) if (g_seenTex[i] == def) { seen = true; break; }
        if (seen) continue;
        if (g_nSeenTex >= 48) return;
        g_seenTex[g_nSeenTex++] = def;
        char name[64], texs[33];
        strncpy_s(name, reinterpret_cast<const char*>(def) + AD_NAME, _TRUNCATE);
        strncpy_s(texs, tex, _TRUNCATE);
        Log("    lights: lamp '%s' names beam texture '%s': %s (state 0x%02X, nodyn %u)\n", name, texs,
            PtrOk(At<void*>(inst, LI_COOKIE)) ? "RESOLVED" : "not resolved",
            At<uint8_t>(inst, 0xD0), At<uint8_t>(def, AD_NODYN));
    }
}

// No car's beam texture (HEADLIGHT_SINGLEBEAM) exists in the PC data (the census logs every lamp
// that names one: none resolves). So give the lamp its OWN glow texture as the beam shape - the
// bulb (+0x88, state bit 2) or flare (+0x78, bit 1) it resolved at spawn - exactly the way the
// engine would have assigned a cookie: resolve, take +0x130, release the resolve reference.
static int g_ownCookieLogs = 0;

// Diagnostic: which fields of an object point at RTTI-named engine objects ("off:Class ...").
// Every field is probed under its own guard: a stale pointer only skips that field.
static const char* ProbeClassAt(void* obj, uintptr_t off)
{
    __try
    {
        void* p = At<void*>(obj, off);
        if (!PtrOk(p) || InImage(p)) return nullptr;
        return ClassName(p);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}

static void LogFieldMap(const char* what, void* obj)
{
    char buf[1400];
    int w = _snprintf_s(buf, sizeof(buf), _TRUNCATE, "    lights: %s fields:", what);
    for (uintptr_t off = 0; off < 0x400 && w > 0 && w < (int)sizeof(buf) - 90; off += 8)
    {
        const char* n = ProbeClassAt(obj, off);
        if (n) w += _snprintf_s(buf + w, sizeof(buf) - w, _TRUNCATE, " %llX:%s", (unsigned long long)off, n + 4);
    }
    Log("%s\n", buf);
}

static bool OwnCookie(void* inst, int which)
{
    const uintptr_t handle = which == 2 ? 0x78 : 0x88;
    const uint8_t   bit    = which == 2 ? 0x01 : 0x02;
    const uint8_t   state  = At<uint8_t>(inst, 0xD0);
    if (!(state & bit) || !g_resolve)
    {
        if (g_ownCookieLogs < 6) { ++g_ownCookieLogs; Log("    lights: no %s texture on this lamp (state 0x%02X)\n", which == 2 ? "flare" : "bulb", state); }
        return false;
    }
    void* res = nullptr;
    g_resolve(&res, reinterpret_cast<uint8_t*>(inst) + handle, 0);
    if (!PtrOk(res)) return false;
    bool ok = false;
    void* tex = At<void*>(res, 0x130);
    // The resolver hands back a material (NuMtl); the spawn code takes its +0x130 slot as the cookie.
    // Use that same slot, and only when RTTI says it really holds a texture.
    if (LooksRefCounted(res) && ClassIs(res, ".?AVNuMtl@@") && LooksRefCounted(tex) && ClassHas(tex, "Texture"))
    {
        g_assign(reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(inst) + LI_COOKIE),
                 reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(res) + 0x130));
        ok = true;
    }
    if (g_ownCookieLogs < 4)
    {
        ++g_ownCookieLogs;
        const char* rn = ClassName(res);
        const char* tn = PtrOk(tex) ? ClassName(tex) : nullptr;
        Log("    lights: %s material: %s, +0x130 %s -> %s\n", which == 2 ? "flare" : "bulb",
            rn ? rn : "(no RTTI)", tn ? tn : "(none)", ok ? "used as the beam shape" : "REFUSED");
        LogFieldMap(which == 2 ? "flare material" : "bulb material", res);
        // the other glow too, for comparison
        const uintptr_t other = which == 2 ? 0x88 : 0x78;
        void* res2 = nullptr;
        if (At<uint8_t>(inst, 0xD0) & (which == 2 ? 0x02 : 0x01))
        {
            g_resolve(&res2, reinterpret_cast<uint8_t*>(inst) + other, 0);
            if (PtrOk(res2))
            {
                LogFieldMap(which == 2 ? "bulb material" : "flare material", res2);
                if (LooksRefCounted(res2)) reinterpret_cast<void(__fastcall*)(void*)>(At<void**>(res2, 0)[3])(res2);
            }
        }
    }
    if (LooksRefCounted(res))
        reinterpret_cast<void(__fastcall*)(void*)>(At<void**>(res, 0)[3])(res);   // Release
    return ok;
}

static bool NameHas(const void* def, const char* word)
{
    const char* s = reinterpret_cast<const char*>(def) + AD_NAME;
    for (int i = 0; i < 0x80 && s[i]; ++i)
    {
        int k = 0;
        while (word[k] && i + k < 0x80 && (s[i + k] | 0x20) == word[k]) ++k;
        if (!word[k]) return true;
    }
    return false;
}

static bool IsHeadlight(void* def)
{
    if (!PtrOk(def)) return false;
    if (!At<uint8_t>(def, AD_NIGHT) || At<uint8_t>(def, AD_BRAKE) || At<uint8_t>(def, AD_HORN)) return false;
    if (At<uint8_t>(def, AD_NOGAME)) return false;
    return !NameHas(def, "rear") && !NameHas(def, "tail") && !NameHas(def, "back");
}

// ---- a headlight that shines the wrong way (headlight_autofix) -------------------------------------
static bool AimNorm(float v[3])
{
    const float n = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (!IsFin(n) || !(n > 1e-6f)) return false;
    v[0] /= n; v[1] /= n; v[2] /= n;
    return true;
}

enum { kAimAhead = 0, kAimBack, kAimUp, kAimDown, kAimSide };

// Pure. lamp = the lamp's world rows as the model has them (right, up, axis: the spot shines along
// +row 2; any length), ahead / up = the car's. false = the lamp already shines ahead (or nothing can be
// said): leave it. true = q is the turn - a rotation made of 0 and +-1 only, to go in front of the
// LocatorOffset (rows) - after which the axis is ahead; *dir says which way it faced.
//   A lamp that sits square in the car (every shipped one does, a quarter or half turn at most) is
//   set fully straight: axis ahead, up up. Any other that faces backwards (axis . ahead < 0) is
//   turned half round its own up axis.
static bool AimTurn(const float lamp[3][3], const float ahead[3], const float up[3], int8_t q[3][3], int* dir)
{
    float w[3][3], t[3][3];
    for (int i = 0; i < 3; ++i) { for (int c = 0; c < 3; ++c) w[i][c] = lamp[i][c]; if (!AimNorm(w[i])) return false; }
    // the frame a straight lamp has: row 2 ahead, row 1 up (squared to it), row 0 = row 1 x row 2
    for (int c = 0; c < 3; ++c) t[2][c] = ahead[c];
    if (!AimNorm(t[2])) return false;
    const float d = up[0] * t[2][0] + up[1] * t[2][1] + up[2] * t[2][2];
    for (int c = 0; c < 3; ++c) t[1][c] = up[c] - d * t[2][c];
    if (!AimNorm(t[1])) return false;
    t[0][0] = t[1][1] * t[2][2] - t[1][2] * t[2][1];
    t[0][1] = t[1][2] * t[2][0] - t[1][0] * t[2][2];
    t[0][2] = t[1][0] * t[2][1] - t[1][1] * t[2][0];
    float D[3][3];                            // the lamp's rows in that frame: identity = straight
    for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) D[i][j] = w[i][0] * t[j][0] + w[i][1] * t[j][1] + w[i][2] * t[j][2];
    {
        const float ax = std::fabs(D[2][0]), ay = std::fabs(D[2][1]), az = std::fabs(D[2][2]);
        const int k = (az >= ax && az >= ay) ? (D[2][2] < 0.0f ? kAimBack : kAimAhead)
                    : (ay >= ax ? (D[2][1] > 0.0f ? kAimUp : kAimDown) : kAimSide);
        if (dir) *dir = k;
    }
    // square in the car? every row along one axis of the frame, each axis used once, a rotation
    int8_t P[3][3] = {};
    bool square = true;
    int used = 0;
    for (int i = 0; i < 3 && square; ++i)
    {
        int j = 0;
        if (std::fabs(D[i][1]) > std::fabs(D[i][j])) j = 1;
        if (std::fabs(D[i][2]) > std::fabs(D[i][j])) j = 2;
        if (std::fabs(D[i][j]) < 0.96f || (used & (1 << j))) square = false;   // within 16 degrees
        else { used |= 1 << j; P[i][j] = D[i][j] > 0.0f ? 1 : -1; }
    }
    if (square)
    {
        const int det = P[0][0] * (P[1][1] * P[2][2] - P[1][2] * P[2][1])
                      - P[0][1] * (P[1][0] * P[2][2] - P[1][2] * P[2][0])
                      + P[0][2] * (P[1][0] * P[2][1] - P[1][1] * P[2][0]);
        if (det != 1) square = false;         // a mirrored offset: only the half turn below
    }
    if (square)
    {
        if (P[2][2] == 1) return false;       // the axis is ahead (a roll about it is left alone)
        for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) q[i][j] = P[j][i];   // P^-1: straight again
        return true;
    }
    if (!(D[2][2] < 0.0f)) return false;      // not square, but not backwards either
    for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) q[i][j] = 0;
    q[0][0] = -1; q[1][1] = 1; q[2][2] = -1;  // half round the lamp's own up axis
    return true;
}

// Pure. The turn into a private copy (cp; orig = the shipped lamp it mirrors):
//   LocatorOffset' = q x LocatorOffset - the rows turn, row 3 (the lamp's place on its locator) stays,
//   so the light and the beam mesh (drawn from the lamp's matrix) turn round the lamp's own centre;
//   the bulb mesh is drawn as Bulb Offset x S x lamp, S = diag(-sx, sy, -sz) (0x1404D8788..0x1404D891C):
//   Bulb Offset' = Bulb Offset x F q^T F (F = diag(-1, 1, -1)) and the scales (and their Dynamic Scale
//   flags) re-ordered the way q re-orders the axes give Bulb Offset' x S' x q = Bulb Offset x S -
//   the bulb exactly where the artist put it.
static void ApplyTurn(uint8_t* cp, const uint8_t* orig, const int8_t q[3][3])
{
    const float* L = reinterpret_cast<const float*>(orig + AD_LOCOFF);
    float*       M = reinterpret_cast<float*>(cp + AD_LOCOFF);
    for (int i = 0; i < 3; ++i) for (int c = 0; c < 4; ++c)
        M[i * 4 + c] = q[i][0] * L[c] + q[i][1] * L[4 + c] + q[i][2] * L[8 + c];
    static const float f[3] = { -1.0f, 1.0f, -1.0f };
    const float* B  = reinterpret_cast<const float*>(orig + AD_BULB_OFF);
    float*       B2 = reinterpret_cast<float*>(cp + AD_BULB_OFF);
    for (int r = 0; r < 4; ++r) for (int c = 0; c < 3; ++c)
    {
        float v = 0.0f;
        for (int k = 0; k < 3; ++k) v += B[r * 4 + k] * f[k] * static_cast<float>(q[c][k]) * f[c];
        B2[r * 4 + c] = v;
    }
    for (int i = 0; i < 3; ++i)
    {
        const int k = q[i][0] ? 0 : (q[i][1] ? 1 : 2);
        At<float>(cp, AD_BULB_SX + 4 * i) = *reinterpret_cast<const float*>(orig + AD_BULB_SX + 4 * k);
        At<uint8_t>(cp, AD_BULB_DYN + i)  = orig[AD_BULB_DYN + k];
    }
}

// The engine's joint function, under its own guard: a fault costs the autofix, not the headlights.
static bool g_aimFault = false;

static bool JointWorld(void* veh, int joint, float* world16)
{
    alignas(16) float loc[16];
    __try { return g_joint(veh, joint, loc, world16, true); }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        g_aimFault = true;
        Log("!!! EXCEPTION reading a lamp locator - headlight_autofix is off for this session; the "
            "headlights stay on as the cars have them.\n");
        return false;
    }
}

// One lit lamp of the player's car: measure (now, and again every second - a copy can come to stand
// for another model after a level change) which way the SHIPPED lamp shines, and turn the copy.
// Stateless but for that: the shipped offset and the live locator are the only inputs.
static bool AimLamp(void* veh, void* inst, const uint8_t* orig, uint8_t* cp, uint32_t now)
{
    const int ci = CopyIndex(cp);
    if (ci < 0 || !g_joint || g_aimFault) return false;
    AimRec& a = g_aim[ci];
    if (!a.known || now - a.t >= 1000)
    {
        uint8_t* vi = At<uint8_t*>(veh, eng::VEH_INSTANCE);
        alignas(16) float jw[16];
        if (PtrOk(vi) && JointWorld(veh, At<int32_t>(inst, LI_JOINT), jw))
        {
            const float* L = reinterpret_cast<const float*>(orig + AD_LOCOFF);
            float lamp[3][3], ahead[3], up[3];
            for (int i = 0; i < 3; ++i) for (int c = 0; c < 3; ++c)
                lamp[i][c] = L[i * 4] * jw[c] + L[i * 4 + 1] * jw[4 + c] + L[i * 4 + 2] * jw[8 + c];
            for (int c = 0; c < 3; ++c)
            {
                ahead[c] = -At<float>(vi, eng::INST_ROW2 + 4 * c);     // engine.h: forward = -row 2
                up[c]    =  At<float>(vi, eng::INST_ROW1 + 4 * c);
            }
            int dir = kAimAhead;
            int8_t q[3][3] = {};
            a.turn = AimTurn(lamp, ahead, up, q, &dir) ? 1 : 0;
            a.dir = static_cast<uint8_t>(dir);
            std::memcpy(a.q, q, sizeof(a.q));
            a.known = 1; a.t = now;
        }
    }
    if (g_aimFault || !a.known || !a.turn) return false;
    ApplyTurn(cp, orig, a.q);
    return true;
}

// ---- one car's lamps -----------------------------------------------------------------------------
static void* g_litCar = nullptr;             // for the log only
static void* g_litCar2 = nullptr;            // ...the other player's car
static void* g_aimCar = nullptr;             // the cars whose turned lamps the log has named
static void* g_aimCar2 = nullptr;

static void CarLamps(void* veh, bool lit, const Config& cfg)
{
    if (!lit && !g_nCopies) return;          // nothing of ours can be on this car
    int n = 0;
    void* inst = At<void*>(veh, VEH_LIGHTS);
    for (int k = 0; PtrOk(inst) && k < kMaxInst; ++k, inst = At<void*>(inst, LI_NEXT))
    {
        void* def = At<void*>(inst, LI_DEF);
        if (!lit)
        {
            const int i = CopyIndex(def);
            if (i >= 0) At<void*>(inst, LI_DEF) = g_copies[i].orig;     // ours: point it back
            continue;
        }
        if (IsHeadlight(OrigOf(def))) ++n;
    }
    if (!lit || !n) return;

    const float share = n > 2 ? 2.0f / static_cast<float>(n) : 1.0f;   // 4-lamp cars: same total light
    int on = 0, beams = 0, own = 0, turned = 0, turnDir = kAimAhead;
    inst = At<void*>(veh, VEH_LIGHTS);
    for (int k = 0; PtrOk(inst) && k < kMaxInst; ++k, inst = At<void*>(inst, LI_NEXT))
    {
        void* def  = At<void*>(inst, LI_DEF);
        void* orig = OrigOf(def);
        if (!IsHeadlight(orig)) continue;
        uint8_t* cp = CopyOf(orig);
        if (!cp) continue;
        std::memcpy(cp, orig, kAttSize);     // the shipped lamp stays the source of truth
        At<uint8_t>(cp, AD_NODYN)  = 0;
        At<uint8_t>(cp, AD_ALWAYS) = 1;
        At<uint8_t>(cp, AD_ADV)    = 0;
        // Reach comes from the range FLOOR: the light update takes max(floor + 0.1, FallOff..).
        // FallOff itself stays as shipped, because the visible beam mesh is sized by it
        // (0x1404D8490: width = tan(FoV/2) * FallOff * Beam Static Scale.x) - raising it to the
        // reach made every beam ~10x too wide (seen 2026-10-23). A FoV change is cancelled
        // out of the mesh through its x scale, so the mesh keeps its shipped size.
        At<float>(cp, AD_RANGE)    = cfg.headlight_range - 0.1f;
        {
            const float fovO = At<float>(orig, AD_FOV);
            const float tO = std::tan(Clampf(fovO, 1.0f, 170.0f) * 0.00872665f);
            const float tN = std::tan(Clampf(cfg.headlight_fov, 1.0f, 170.0f) * 0.00872665f);
            At<float>(cp, AD_FOV) = cfg.headlight_fov;
            if (tO > 1e-3f && tN > 1e-3f) At<float>(cp, AD_BEAM_SX) = At<float>(orig, AD_BEAM_SX) * tO / tN;
        }
        // The beam mesh is a fixed-size textured wedge: x = tan(FoV/2) * FallOff * scale.x, y = scale.y,
        // z (its length) = scale.z, about one car long on the cars checked, whatever the light's reach.
        // From the chase camera its far end is a hard flat line across the road (seen
        // 2026-11-11). Scaled to nothing it draws nothing; the light itself is untouched.
        if (!cfg.headlight_beam)
            At<float>(cp, AD_BEAM_SX) = At<float>(cp, AD_BEAM_SY) = At<float>(cp, AD_BEAM_SZ) = 0.0f;
        for (int c = 0; c < 3; ++c)
            At<float>(cp, AD_COLOUR + 4 * c) = At<float>(orig, AD_COLOUR + 4 * c) * cfg.headlight_intensity * share;
        // a lamp the car's data has shining backwards (or up): turned so it shines ahead
        if (cfg.headlight_autofix && AimLamp(veh, inst, static_cast<const uint8_t*>(orig), cp, g_lastTick))
        {
            ++turned;
            turnDir = g_aim[CopyIndex(cp)].dir;
        }
        if (def != cp) At<void*>(inst, LI_DEF) = cp;
        if (!PtrOk(At<void*>(inst, LI_COOKIE)))
        {
            if (cfg.headlight_cookie && OwnCookie(inst, cfg.headlight_cookie)) ++own;
            else if (g_cookie)
                g_assign(reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(inst) + LI_COOKIE), &g_cookie);
        }
        if (PtrOk(At<void*>(inst, LI_COOKIE))) ++beams;
        ++on;
    }
    if (veh != g_litCar && veh != g_litCar2)
    {
        g_litCar2 = g_litCar; g_litCar = veh;
        char name[64];
        void* vdef = At<void*>(veh, eng::VEH_DEF);
        if (!PtrOk(vdef) || !ReadModelName(vdef, name, sizeof(name))) strcpy_s(name, "?");
        Log("    lights: %s - %d of %d headlight lamp%s engine-lit, %d with a beam shape (%d from the "
            "lamp's own %s texture this frame%s)\n", name, on, n, n == 1 ? "" : "s", beams, own,
            cfg.headlight_cookie == 2 ? "flare" : "bulb", beams ? "" : "; none: glow only");
    }
    if (turned && veh != g_aimCar && veh != g_aimCar2)
    {
        g_aimCar2 = g_aimCar; g_aimCar = veh;
        static const char* const kWay[] = { "ahead", "backwards", "up", "down", "sideways" };
        char name[64];
        void* vdef = At<void*>(veh, eng::VEH_DEF);
        if (!PtrOk(vdef) || !ReadModelName(vdef, name, sizeof(name))) strcpy_s(name, "?");
        Log("    lights: %s - %d headlight lamp%s faced %s (the lamp definition's LocatorOffset) - turned "
            "to shine ahead\n", name, turned, turned == 1 ? "" : "s", kWay[turnDir < 5 ? turnDir : 4]);
    }
}

// ---- once per vehicle Update, before the engine runs it -------------------------------------------
static void Tick(void* veh, bool isPlayerCar, const Config& cfg, uint32_t now)
{
    // A stretch with no vehicle updating at all = loading screen, menu, cutscene or interior. A
    // level change frees the car the borrowed cookie came from, so forget our reference - on
    // purpose WITHOUT releasing it: a leaked reference to a live texture costs nothing, touching a
    // freed one would. The next car that has one lends it again.
    if (g_lastTick && now - g_lastTick > 1500) g_cookie = nullptr;
    g_lastTick = now;

    OwnerTick(veh, cfg, now);
    if (g_release) { CarLamps(veh, false, cfg); return; }
    CensusCookies(veh);
    if (!g_cookie && cfg.headlights) HarvestCookie(veh);
    const bool lit = isPlayerCar && g_night && cfg.headlights;
    CarLamps(veh, lit, cfg);
    if (!lit && veh == g_litCar) { g_litCar = g_litCar2; g_litCar2 = nullptr; }
    else if (!lit && veh == g_litCar2) g_litCar2 = nullptr;
    if (!lit && veh == g_aimCar) { g_aimCar = g_aimCar2; g_aimCar2 = nullptr; }
    else if (!lit && veh == g_aimCar2) g_aimCar2 = nullptr;
}

} // namespace lights
