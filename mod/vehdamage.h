// =============================================================================================
//  vehdamage.h - crash damage for the vehicles lcudrive flies (helicopters, the Hover planes),
//  dealt the game's own way. Research 2026-11-13 (Notes\28 "Crash damage"):
//
//  * Vehicle health is an int in the user data's MechVehicleHealthInfo: ud = *(veh+0x378) - 0x10,
//    hp = *(ud+0x70), current +0x18, max +0x1C (MAXHEALTH 4 for the police chopper and every
//    AIRCRAFT_* plane, legocityvehiclesettings.txt), +0x24 bit0 = script-invulnerable.
//  * Damage goes through ud vt[25] TakeDamage (base 0x140191CF0): (ud, int amount, float launch,
//    bool fxAlt, bool fxArg). At health 0 a Hover vehicle explodes at once (launch != 0 sets
//    ud+0x382 = 2, the explosion-FX branch of ud vt[13]), fades out, and fires
//    PlayerVehicleDestroyed. The game calls it from inside ApiVehicle::Update, so the vehicle's
//    own Update hook (game thread) is the same context.
//  * The game NEVER calls it for a collision of class 6 (ApiVehicleHover: the helicopters and the
//    Hover planes): OnCollision 0x140194760 branches off at 0x140194830 for class 6 and only plays
//    the impact effect. That is why falling helicopters took no damage.
//  * What lcudrive used to call "wreck" (0x1404BE6C0) only puts a vehicle to sleep for a frame
//    (sim LOD 10); it is not damage.
//  The game's own wall-hit ladder for cars is 0.3 / 0.6 / 0.8 x top speed -> 1 / 2 / 4 damage;
//  CrashDamage() keeps that ladder against a reference speed of our choosing.
// =============================================================================================
#pragma once

namespace vd {

static const uintptr_t UD_HEALTH = 0x70, HP_CUR = 0x18, HP_MAX = 0x1C;
static const uintptr_t RVA_DAMAGE_ON = 0x1010EB0;    // byte, 1 = vehicle damage enabled (never written)
static const int       UD_SLOT_TAKEDAMAGE = 25, HP_SLOT_ISINVULN = 4;

typedef void (__fastcall* TakeDamageFn)(void* ud, int amount, float launch, bool fxAlt, bool fxArg);
typedef bool (__fastcall* IsInvulnFn)(void* hp, int mask, bool* outOpt);

static inline bool InGame(const void* p)
{
    const uintptr_t v = reinterpret_cast<uintptr_t>(p);
    return v >= g_base && v < g_base + eng::kImageSize;
}

static uint8_t* HealthOf(void* veh)
{
    __try
    {
        uint8_t* p = At<uint8_t*>(veh, eng::VEH_USERDATA);
        if (!PtrOk(p)) return nullptr;
        uint8_t* hp = At<uint8_t*>(p - 0x10, UD_HEALTH);
        return PtrOk(hp) ? hp : nullptr;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}

// Health now and its maximum; false when the vehicle has no health record.
static bool Health(void* veh, int& cur, int& max)
{
    uint8_t* hp = HealthOf(veh);
    if (!hp) return false;
    __try { cur = At<int32_t>(hp, HP_CUR); max = At<int32_t>(hp, HP_MAX); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// Deal n damage (n >= the health left destroys it: explosion, fade-out, the destroyed event).
// Game thread, from the vehicle's own Update hook. Returns the health left, -1 when nothing was
// done (damage off, no record, already destroyed, or a script made it invulnerable).
static int Damage(void* veh, int n)
{
    if (n <= 0) return -1;
    __try
    {
        if (!*reinterpret_cast<const uint8_t*>(g_base + RVA_DAMAGE_ON)) return -1;
        uint8_t* p = At<uint8_t*>(veh, eng::VEH_USERDATA);
        if (!PtrOk(p)) return -1;
        uint8_t* ud = p - 0x10;
        uint8_t* hp = At<uint8_t*>(ud, UD_HEALTH);
        if (!PtrOk(hp) || At<int32_t>(hp, HP_CUR) < 1) return -1;
        void** hvt = At<void**>(hp, 0);
        const IsInvulnFn inv = PtrOk(hvt) ? reinterpret_cast<IsInvulnFn>(hvt[HP_SLOT_ISINVULN]) : nullptr;
        if (!InGame(reinterpret_cast<const void*>(inv)) || inv(hp, 0, nullptr)) return -1;   // TakeDamage doesn't check
        void** vt = At<void**>(ud, 0);
        const TakeDamageFn take = PtrOk(vt) ? reinterpret_cast<TakeDamageFn>(vt[UD_SLOT_TAKEDAMAGE]) : nullptr;
        if (!InGame(reinterpret_cast<const void*>(take))) return -1;
        take(ud, n, 1.0f, false, true);        // the game's own wall-hit arguments (0x140195BDC)
        return At<int32_t>(hp, HP_CUR);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}

// The ladder: below 0.3 x vref nothing, then 1 / 2 / 4 (4 = MAXHEALTH: destroyed in one go).
// dvFirst is the impact that does the first point (vref = dvFirst / 0.3).
inline int CrashDamage(float dv, float dvFirst)
{
    if (!(dv > 0.0f) || !(dvFirst > 0.0f)) return 0;
    const float vref = dvFirst / 0.3f;
    if (dv >= 0.8f * vref) return 4;
    if (dv >= 0.6f * vref) return 2;
    if (dv >= 0.3f * vref) return 1;
    return 0;
}

} // namespace vd
