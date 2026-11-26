// =============================================================================================
//  missions_mask.h - [missions1] "Missions: stock game": what a story mission switches off.
//  Needs config.h (Config). Pure: the hotkey thread builds the live Config as the raw (as-loaded)
//  one with these switches at 0, exactly as if the ini said so - every feature then stands down
//  through its own, already-tested "switched off" path (the one the Mod Options menu uses). Taking
//  the mask away = the raw Config again. Notes\68-missions.md section 4 (the classification).
// =============================================================================================
#pragma once
#include "missions_model.h"

namespace msm {

// M_GEN: features that act on the world around a mission (police, people, spawners, extra creatures).
// M_VEH: the handling of the car / bike a player drives (only handed over while the vehicle is safe).
// M_AIR: the flight models and the air extras (only while no player flies, or flies slowly).
inline void MaskConfig(Config& c, uint8_t m)
{
    if (m & M_GEN)
    {
        c.wanted_enable    = 0;     // the wanted system and everything under it (police boats, helicopters, officers, busted)
        c.life_enable      = 0;     // street life scenes, crowd reactions, the dance party
        c.voice_enable     = 0;     // Chase's cut voice lines and the passers-by's
        c.npc_enable       = 0;     // the NPC voices / greetings / small talk
        c.spots_enable     = 0;     // people and cars at the new places
        c.animals_enable   = 0;     // the animals
        c.sites_enable     = 0;     // vehicles waiting at stands anywhere (also carries the marinas)
        c.marina.enable    = 0;     // berths, anchored boats, boat stations
        c.boats.enable     = 0;     // the traffic-boat spacing / file breaker (stock boat traffic)
        c.dino_enable      = 0;     // the rideable T-Rexes
        c.plane_airport    = 0;     // the airliner waiting at the airport
        c.jp_enable        = 0;     // the jetpack upgrade (the game's own 1.5 s jetpack)
        c.shark_classic    = 1;     // the game's own shark rule (deep water = the shark at once)
        c.backrooms_enable = 0;     // the BlockRooms door is only a door
        c.coop_split_cuts  = 0;     // a cut-scene freezes both players, as the stock game
    }
    if (m & M_VEH)
    {
        c.enable           = 0;     // the car handling overhaul (engine, gears, tyres, assists)
        c.engine_sound     = 0;     // ...and its engine sound
        c.bike_enable      = 0;     // bike stunts
        c.bike_drive       = 0;     // the bike engine
    }
    if (m & M_AIR)
    {
        c.heli_enable          = 0; // helicopter physics
        c.heli_free_flight     = 0; // the invisible walls back
        c.heli_exit_anywhere   = 0; // the game's own exits
        c.heli_bail            = 0;
        c.heli_crash_damage    = 0;
        c.heli_wreck_on_crash  = 0;
        c.heli_parachute       = 0;
        c.plane_enable         = 0; // the Hover-class planes fly as the game has them
        c.plane_crash_damage   = 0;
        c.airplane_enable      = 0; // the Elifant aeroplane
        c.airplane_crash_damage = 0;
        c.airplane_abandon_glide = 0;
        c.airplane_bail_parachute = 0;
        c.ptero_enable         = 0; // the flying Pteranodon
    }
}

inline const char* MaskText(uint8_t m)
{
    static const char* const t[8] = { "nothing", "world", "vehicles", "world + vehicles", "aircraft", "world + aircraft",
                                      "vehicles + aircraft", "world + vehicles + aircraft" };
    return t[m & 7];
}

} // namespace msm
