// =============================================================================================
//  config.h - lcudrive.ini. Flat and small; every key is documented in the shipped ini.
//  Parsed on the hotkey thread with GetPrivateProfileString, published by index swap.
// =============================================================================================
#pragma once
#include "radio_genre.h"                  // the [radio] playlist forms (pure; no Windows)
#include "radiologo_model.h"              // [radiologo1] logo_corner names (pure)
#include "sites_model.h"                  // the [sites] stand lines (pure)
#include "spots_model.h"                  // the life-spot data: people, parked cars, routes (pure)
#include "spots_table.h"                  // ... its built-in table (tools\make_spots.py)
#include "animals_model.h"                // [round3] the animals' herd data (pure)
#include "weather_model.h"                // rain, fog and the weather zones (pure; Notes\53)
#include "gfx_model.h"                    // the graphics overhaul: presets, grade, camera layer (pure; Notes\57)
#include "marina_model.h"                 // [marina1] the [marina] berths / anchored boats / boat stations (pure)
#include "diycars_model.h"               // [diycars1] the DIY-car garage -> [sites] stands (pure; Notes9)
#include "boats_model.h"                  // [boats2] the traffic boats: no processions (pure; Notes 56 section 8)
#include "station_model.h"                // [station1] [general] station_return's spelling (pure; Notes 69)

// [radio] music_<id> / playlist_<id> / genre_<id>: one station's keys (read from the whole section)
struct RadioStationKeys
{
    char id[48];
    char music[16];                       // "" = the global `music`
    char playlist[780];                   // UTF-8
    char genre[256];
};

struct Config
{
    // [general]
    int   enable           = 1;      // boot state of `]`
    int   bridge           = 1;      // publish speed + headlight frame to 3Dmigoto (speed blur)
    int   telemetry        = 0;      // write lcudrive_telemetry.csv every frame (Ctrl+Alt+T too)
    float bridge_speed     = 30.0f;  // units/s that read as "flat out" for the blur
    int   handbrake_key    = 0x20;   // virtual-key code read directly (0x20 = Space, 0 = none)
    int   brake_key_brakes = 1;      // the game turns its brake button into the HANDBRAKE while
                                     // moving forward; 1 = treat it as a real brake instead
    int   engine_sound     = 1;      // drive the engine sound's RPM from our engine
    int   pad_handbrake    = 0;      // controller handbrake button (XInput bits, parsed from a name;
                                     // none by default: RB is the game's car jump)
    int   pedal_handbrake  = 1;      // accelerator + brake together while rolling forward = handbrake
    float brake_drift      = 0.7f;   // the brake with the steering past this (0..1) at speed = a drift
    // [missions1] "Missions: stock game" (missions.h, Notes\68): while a story mission runs the mod's gameplay
    // changes stand down (msm::MaskConfig), graphics / radio / safety stay. Never masked themselves.
    int   mission_stock    = 1;      // 1 = suspend them during missions
    int   mission_stock_side = 1;    // ...during side missions too (races, time trials, gang fights, garages, thefts)
    float mission_stock_enter = 2.0f;// s the mission signal must hold before anything is suspended
    float mission_stock_exit  = 6.0f;// s it must be gone before everything comes back
    float mission_stock_safe_speed = 3.0f;   // u/s: a car / aircraft is handed over only below this (or on foot, or in a cut-scene)
    float mission_detect_stable = 1.0f;      // s a new story position must hold (mission news detector)
    float mission_detect_settle = 8.0f;      // s after a baseline (a load) before a completion counts
    float mission_detect_absent = 6.0f;      // s without a player (the front end) = a new baseline
                                     // entry, like the game's own power slide (0 = it always brakes)
    float stick_deadzone   = 0.10f;  // analog steering: stick travel ignored around centre
    float steer_curve      = 1.5f;   // analog steering response (1 = linear; keyboard unaffected)

    // [feel] - 1.0 (or 0 for balance) = as tuned
    float power            = 1.0f;   // engine power
    float top_speed        = 1.0f;   // top speed (via gearing + aero drag)
    float grip             = 1.0f;   // tyre grip everywhere
    float balance          = 0.0f;   // -1 loose tail ... 0 neutral ... +1 planted tail
    float brakes           = 1.0f;
    float steer_speed      = 1.0f;   // how fast the wheels turn
    float steer_lock       = 1.0f;   // how far they turn
    float weight           = 1.0f;   // weight-transfer strength (0 = even loads, like stock)
    float handbrake        = 1.0f;   // handbrake strength
    float breakaway        = 1.0f;   // how much grip drops once a tyre slides (0 = none)
    float traction_control = 0.5f;   // 0 = burnouts, 1 = no wheelspin
    float yaw_damping      = 0.30f;  // solver angular damping on our car (stock 1.0)
    float yaw_inertia      = 0.85f;  // x stock yaw inertia (the engine adds 1.5x at spawn)
    float stability        = 0.5f;   // transient yaw damper (0 = raw, 1 = very settled at speed)
    float size_effect      = 1.0f;   // how much length / mass make big vehicles heavier to steer (0 = off)
    float drift            = 0.6f;   // 0..1: how much the throttle keeps the rear loose in a held slide

    // [lights]
    int   headlights       = 1;      // 0 off, 1 follow the day/night cycle, 2 always on (testing)
    int   traffic_lights   = 1;      // at night every vehicle's own headlight glow comes on too
    float headlight_range  = 22.0f;  // how far your headlights reach (units)
    float headlight_fov    = 55.0f;  // beam width, degrees
    float headlight_intensity = 1.6f;// x the car's own lamp colour
    float night_below      = 0.06f;  // sun elevation (-1..1) under which it counts as night
    int   headlight_cookie = 2;      // beam shape: 2 the lamp's own flare texture, 1 its bulb, 0 none
    int   headlight_beam = 0;        // the lamp's visible beam mesh on your car: 0 hidden, 1 as shipped
    int   headlight_autofix = 1;     // a headlight the car's data has shining backwards / up is turned ahead (0 = as shipped)
    int   coop_pad_p1 = -1;          // [coop] each player's XInput port: -1 auto, 0..3, 4 = none
    int   coop_pad_p2 = -1;
    int   coop_split_cuts = 1;       // [coop] a cut-scene in one player's view freezes only that player (coopcut.h)

    // [streetlamps] - real light under the street lamps (streetlamps.h)
    int   sl_mode          = 1;      // 0 off, 1 at night, 2 debug: one light at player 1's feet, any time of day
    int   sl_max_lights    = 64;     // engine lights handed over at once (a hard cap), split between the players (0..96 = sl::K)
    float sl_radius        = 3.0f;   // u, a single-arm lamp's light radius (the other kinds scale from it)
    float sl_range         = 35.0f;  // u, lamps this close to a player are lit (fading out from 0.8x; nearer if the share runs out)
    float sl_intensity     = 1.0f;   // brightness (x; the light goes with it linearly)
    float sl_falloff       = 0.02f;  // the light's FalloffFactor (desc+0xB4; the engine's default 0.02)
    float sl_head_drop     = 0.1f;   // u the light sits below the lamp head (keeps the pole top from blowing out)
    int   sl_fixtures      = 0;      // 1 = wall, garden and small lantern lights too
    int   sl_log           = 0;      // 1 = a status line in lcudrive.log once a second
    float sl_col[3][3]     = { { 1.0f, 0.79f, 0.53f },     // sodium: the tall street lamps
                               { 1.0f, 0.93f, 0.84f },     // warm white: park lamps, fixtures
                               { 1.0f, 0.59f, 0.35f } };   // red-orange: Chinese lamp posts and lanterns

    // [heli]
    int   heli_enable      = 1;      // the physical flight model on the helicopter you fly
    float heli_top_speed   = 45.0f;  // u/s at full forward tilt
    float heli_max_tilt    = 34.0f;  // deg of pitch / roll at full stick
    float heli_climb       = 6.0f;   // u/s at full ascend
    float heli_descend     = 8.0f;   // u/s at full descend
    float heli_thrust_g    = 2.0f;   // rotor thrust capacity in g
    float heli_yaw_rate    = 100.0f; // deg/s at full turn
    float heli_turn_bank   = 0.6f;   // 0..1 bank into turns
    float heli_brake       = 0.7f;   // 0..1 tilt back to stop when the stick is released
    float heli_response    = 0.15f;  // s, attitude response
    float heli_land_height = 0.35f;  // height above ground that counts as touching
    float heli_yaw_sign    = 1.0f;   // input polarities (flip to -1 if a control is backwards)
    float heli_fwd_sign    = 1.0f;
    float heli_vert_sign   = 1.0f;
    int   heli_stick_direct = 1;     // a controller's left stick flies it directly (not camera-relative)
    int   heli_exit_anywhere = 1;    // the exit button works off the pads too
    int   heli_bail        = 1;      // ...in the air as well: you jump out, the heli is left to fall
    float heli_park_height = 1.2f;   // at or below this height (and slow) an exit lands and parks it
    int   heli_free_flight = 1;      // no turn-around walls, no height cap, no minimum-height floors
    float heli_crash_spin  = 5.0f;   // rad/s an abandoned helicopter spins up to (0 = no spin)
    int   heli_wreck_on_crash = 1;   // an abandoned helicopter that hits the ground hard is destroyed
    float heli_crash_speed = 6.0f;   // u/s impact that destroys an abandoned one
    int   heli_crash_damage = 1;     // crashes while flown cost health (the game never damages helicopters)
    float heli_damage_speed = 7.0f;  // u/s impact for the first point; x2 = 2 points, x2.67 = destroyed
    int   plane_enable      = 1;     // [plane] the Hover-class AIRCRAFT_* fly as aeroplanes (plane.h), not as helicopters
    // (2026-11-13: none by default - CLOUD is a jetpack, HERA a helicopter, JUPITER and UFO saucers; with an
    //  empty list they fly with the helicopter model again. The aeroplanes are [airplane], airplane.h)
    char  plane_models[96]  = "";    // which (the name after AIRCRAFT_), or "all"
    int   plane_stick_direct = 1;    // a controller flies it directly: stick = bank / pitch, RT / LT = throttle
    int   plane_crash_damage = 1;    // crashes and bad landings cost health
    float plane_damage_speed = 7.5f; // u/s impact for the first point; 15 = two, 20 = destroyed
    float plane_turn_sign   = 1.0f;  // live calibration (plane_model.h turnSign / fwdSign)
    float plane_fwd_sign    = 1.0f;
    float plane_gear_height = -1.0f; // < 0 = the airframe's own (plane_model.h)
    float plane_auto_level  = -1.0f;
    float plane_level_path_rate = -1.0f;
    int   plane_airport     = 1;     // [plane] airport: planes wait at the airport while a player is near (airport.h)
    int   plane_airport_count = 3;   // how many stands have one, 1..6 (stand 0 = the runway start; airport_model.h StandAt)
    char  plane_airport_model[48] = "Aircraft_Elifant";   // which (a model name AITypes knows: Aircraft_Elifant / _Cloud / _Hera ...)
    // one model per stand, in stand order; a missing entry, a model the game does not have or one nothing flies = airport_model
    char  plane_airport_models[200] = "Aircraft_Elifant, Aircraft_Pegasus, Aircraft_ThunderRoc, Aircraft_Elifant, Aircraft_Pegasus, Aircraft_ThunderRoc";
    char  plane_airport_pin[48]   = "DefaultVehicle";     // its map pin (mappins.txt pinName; "" = none)
    float plane_airport_near  = 90.0f;   // u from the stand: a player this close gets a plane there
    float plane_airport_far   = 220.0f;  // u: nobody this close -> the unused plane fades out
    float plane_airport_leave = 60.0f;   // u: a plane left elsewhere fades once nobody is this close (after 10 s)

    // [airplane] real aeroplanes on their own wheels (airplane.h; plane_model.h gearMode 1)
    int   airplane_enable       = 1;
    char  airplane_models[96]   = "ELIFANT, PEGASUS, THUNDERROC";   // car-class AIRCRAFT_* that fly (the name after AIRCRAFT_), or "all"
    int   airplane_stick_direct = 1;     // a controller flies it directly (0 = the game's steer / throttle fields)
    float airplane_steer_sign   = 1.0f;  // nose-wheel direction (the watchdog logs when it looks reversed)
    float airplane_turn_sign    = 1.0f;  // bank direction in the air
    float airplane_fwd_sign     = 1.0f;  // pitch direction (forward = nose down)
    float airplane_rotate_deg   = 8.0f;  // most nose-up attitude on the main wheels, 2..9 (Elifant's tail strikes at 11)
    float airplane_side_grip    = 1.0f;  // tyre side friction (x the load)
    float airplane_rolling      = 0.015f;// rolling resistance (x the load)
    float airplane_brake_mu     = 0.7f;  // main-wheel brakes (x the load)
    float airplane_reverse_g    = 0.3f;  // reverse thrust at idle lever + LT on the wheels (0 = none)
    float airplane_taxi_reverse_speed = 3.0f;   // u/s it backs up at: stopped, lever idle, LT held (0 = never)
    int   airplane_crash_damage = 1;     // bad touchdowns cost health (collisions: the game's own car damage)
    float airplane_crash_destroy_speed = 15.0f;  // u/s: a hit this hard (or a touchdown sinking half this fast) destroys the plane; 0 = off
    int   beeps = 0;                     // [general] beeps: 0 = the mod never beeps (2026-11-15: beeps off by default)
    float airplane_max_velocity = 25.0f; // written into the plane's definition (0 = left alone)
    float airplane_cam_height   = 1.6f;  // chase camera, slow (0 = the definition's)
    float airplane_cam_distance = 6.5f;
    float airplane_cam_height_fast   = 2.2f;   // ... at max_velocity
    float airplane_cam_distance_fast = 9.0f;
    int   airplane_abandon_glide  = 1;   // a plane left in the air glides down empty
    int   airplane_bail_parachute = 1;   // getting out in the air arms the parachute
    int   airplane_force_lod1     = 0;   // a player's plane at sim LOD 5 on the wheels is held at 1 (an experiment, off)
    int   airplane_stall_horn     = 1;   // double beeps while the wing is stalled
    int   airplane_engine_rpm     = 1;   // the engine sound's RPM follows the lever
    // the retractable gear (2026-11-14): the horn button in the air puts it up / down
    int   airplane_gear_retract   = 1;   // 0 = the gear stays down (the horn is only a horn)
    float airplane_gear_up_speed  = 56.0f;  // u/s level at full lever with the gear up (down: the airframe's 45)
    float airplane_gear_time      = 2.0f;   // s the gear travels
    float airplane_gear_belly_speed = 10.0f; // a belly landing costs 1 point + 1 per this much speed
    float airplane_gear_belly_decel = 5.0f;  // u/s^2 sliding on the hull
    int   airplane_gear_auto_down = 0;   // 1 = the gear comes down by itself low over the ground
    float airplane_gear_warn_height = 6.0f;  // u: beeps while the gear is up this low and descending (0 = no cue)
    int   airplane_gear_visual    = 1;   // the gear is drawn going up into the hull: the Elifant's wheels, the Pegasus' / ThunderRoc's wheels + legs (0 = always drawn down)
    // the propellers (2026-11-14): the ThunderRoc's four turn with its engines (contrib_planes v2's model)
    int   airplane_props          = 1;   // 0 = they stand still
    int   airplane_skid_fix       = 1;   // 1 = tyre marks / squeal / smoke only for a real skid (0 = the game's own rule)
    float airplane_prop_idle      = 2.0f;   // turns per second with someone aboard, lever shut
    float airplane_prop_max       = 7.0f;   // ... at full power
    int   heli_parachute   = 1;      // after bailing out, JUMP while falling opens a parachute
    int   heli_chute_cut   = 1;      // JUMP again under the canopy cuts it away (it can be reopened)
    float heli_chute_fall  = 3.5f;   // u/s sink under the canopy (the game's own is 2; 0 = leave it)

    // [bike]
    int   bike_enable        = 1;      // stunts on the motorbike you ride
    float bike_wheelie_speed = 4.0f;   // u/s before pulling back lifts the front
    float bike_wheelie_angle = 26.0f;  // deg at full pull-back (past ~28 the collision box scrapes)
    int   bike_drive         = 1;      // lcudrive's engine on the rear tyre + real aero drag
    float bike_top_speed     = 17.0f;  // u/s flat out
    float bike_grip_g        = 1.0f;   // the turn the tyres can hold, in g: sets the steering limit
                                       // and the spin guard at speed (0 = off)
    float bike_power         = 1.0f;   // acceleration (x)
    int   bike_brake_slide   = 0;      // 1 = the brake button is the game's power slide again (it
                                       // spins the bike out at speed); 0 = it brakes
    float bike_stoppie_angle = 22.0f;  // deg nose-down at full push while braking
    float bike_air_flip      = 11.0f;  // rad/s^2: how hard the stick flips it in the air
    float bike_air_spin      = 8.0f;   // rad/s^2: steering spins it in the air (0 = off)
    int   bike_lean_keys     = 1;      // keyboard: S = lean back, W = lean forward

    // [jetpack] - the astronaut's (Notes\30)
    int   jp_enable          = 1;
    float jp_height          = 1.1f;   // climb above where you press (the game's own: 0.55)
    float jp_time            = 6.0f;   // seconds of fuel (the game's own: 1.5)
    int   jp_resume          = 1;      // press again in the air to carry on with the fuel left
    int   jp_keep_ceiling    = 1;      // ...but never above the first flight's height
    int   jp_unlimited       = 0;      // [jp-unl] 1 = no height limit: holding JUMP keeps climbing until the fuel is gone
    float jp_climb_speed     = 3.0f;   // [jp-unl] u/s while climbing without a limit (the game's own climb: ~2.4 near the start)

    // [wanted] - GTA-style police (Notes\34). Step 1: the level is set by hand.
    int   wanted_enable          = 1;
    int   wanted_debug_level     = 0;      // the level to start at (Ctrl+Alt+L steps it live)
    float wanted_radius          = 60.0f;  // u: police traffic this close joins the chase (+10 per level)
    float wanted_speed           = 25.0f;  // the chase AI's pursuit speed (the game floors it at 6.5)
    float wanted_evade_base      = 15.0f;  // s out of sight before a level drops...
    float wanted_evade_per_level = 5.0f;   // ...plus this per level
    int   wanted_police_boost    = 1;      // chasing police get a real engine (wanted.h); 0 = stock
    float wanted_police_speed    = 17.0f;  // u/s their top speed while chasing
    float wanted_police_power    = 1.0f;   // their acceleration (x)
    float wanted_police_grip     = 1.25f;  // x their tyres' cornering and braking grip
    float wanted_police_catchup  = 0.15f;  // up to this much faster when far behind you
    float wanted_police_ram      = 1.3f;   // x power (and top speed) on your bumper
    int   wanted_crimes          = 1;      // crimes raise the level (0 = only Ctrl+Alt+L)
    float wanted_crime_run_over  = 0.4f;   // heat per crime (a brick = 1.0; / (1 + level_damp x level),
    float wanted_crime_ram_police = 0.6f;  // x unseen when no police saw it)
    float wanted_crime_wreck     = 0.4f;
    float wanted_crime_steal_car = 0.5f;     // taking a car out of the police uniform (old key: crime_robber_car)
    float wanted_crime_steal_police = 0.8f;  // ...a police car
    float wanted_crime_hit_cop   = 0.8f;
    float wanted_crime_hit_person = 0.2f;
    float wanted_crime_punch     = 0.15f;
    float wanted_crime_shoot_person = 0.3f;   // the ray gun: a pedestrian frozen,
    float wanted_crime_shoot_prop = 0.15f;    // a prop or a LEGO object smashed with it
    float wanted_level_damp      = 1.0f;   // each brick harder to earn than the last
    float wanted_unseen          = 0.35f;  // x a crime no police saw...
    float wanted_unseen_cap      = 1.99f;  // ...which can't take the heat past this
    int   wanted_gadgets         = 1;      // the police stun / tie you up on foot
    float wanted_stun_s          = 3.0f;
    float wanted_tie_s           = 4.0f;
    int   wanted_busted_studs    = 1000;   // x the level
    int   wanted_busted_station  = 1;      // busted: off to the police station (0 = released on the spot)
    int   wanted_busted_station_coop = 0;  // ...in co-op too: the trip is a level change, it takes BOTH players (0 = let go on the spot)
    float wanted_busted_grace    = 15.0f;  // s after a bust when crimes don't count (game time)
    // [busted1 2026-11-18] busted: fade out, and ONLY the busted player wakes up outside the police station
    // (wanted_busted.h; no level change, so the other player plays on). 0 = the old busted_station behaviour.
    int   wanted_busted_outside  = 1;
    float wanted_busted_outside_x = -178.291f;   // the spot: the story's own "walk out of the station" locator
    float wanted_busted_outside_y = 3.343f;      // (Story_A1_M2_ExitPoliceStation_PlayerWalkTo, lego_city_ai.led)
    float wanted_busted_outside_z = -222.379f;
    float wanted_busted_outside_heading = 20.0f; // degrees, 0 = +z, 90 = +x: away from the station door
    float wanted_busted_outside_load = 2.5f;     // s held (black) at the spot after a far jump, while it loads
    float wanted_busted_fade     = 0.5f;         // s for the fade out / in (0 = a cut)
    // [station1 2026-11-24] the pause menu's "Return to Police Station": "outside" = only the player who picked it,
    // to the busted spot above, no level change (station.h); "stock" = the game's own trip to the lobby (both players)
    char  station_return[16]     = "outside";
    int   wanted_hud             = 1;      // the brick meter (3Dmigoto overlay)
    int   wanted_officers        = 1;      // officers get out of their cars, walking cops join in (Notes\38)
    int   wanted_officers_bikes  = 1;      // ...and off police motorbikes too
    float wanted_officer_get_out = 15.0f;  // u: a chasing police car this close to you on foot stops for its officer
    float wanted_officer_join    = 25.0f;  // u: walking cops this close join the chase
    float wanted_officer_fire    = 10.0f;  // u: an officer this close may fire his grapple (the tie)
    float wanted_officer_grapple = 0.06f;  // chance per second of a grapple at level 1 (+0.02 a level)
    int   wanted_officers_fight  = 1;      // up close they fight you, like the game's hostile NPCs
    int   wanted_officer_hits    = 2;      // your blows that beat an officer on foot (he falls apart)
    int   wanted_boats           = 1;      // police boats chase you on the water (wanted.h; Notes\43 §1, 2026-11-13)
    float wanted_boat_radius     = 90.0f;  // u: police boats this close join (boat spawns sit ~4x further out than cars)
    float wanted_boat_linger     = 8.0f;   // s back on land before the police boats give up
    char  wanted_boat_driver[64] = {};     // the officer at a traffic police boat's wheel ("" = the game's own choice)
    int   wanted_boat_minnow     = 1;      // levels 4-5 on the water: a second boat (Police_Minnow) takes the bikes' record
    int   wanted_boat_arrest     = 1;      // a manned police boat alongside arrests you (swimming, or stopped in a boat)
    float wanted_boat_arrest_range = 6.0f; // u: "alongside"
    float wanted_boat_arrest_s   = 3.0f;   // s alongside before the bust
    int   wanted_heli            = 1;      // police helicopters at the top levels (wanted_heli.h; Notes\43 §2, 2026-11-13)
    char  wanted_heli_model[64]  = {};     // which one (Police_Responder, Enforcer 9,0: the one with a searchlight)
    int   wanted_heli_level      = 4;      // one from this level...
    int   wanted_heli_level2     = 5;      // ...two from this one (6 = never two)
    float wanted_heli_height4    = 10.0f;  // u above you below level 5...
    float wanted_heli_height5    = 8.0f;   // ...and at level 5 (lower, closer)
    float wanted_heli_radius     = 14.0f;  // u: the circle it flies round you when you stop
    float wanted_heli_speed      = 36.0f;  // u/s flat out (your helicopter does 45)
    float wanted_heli_sight      = 45.0f;  // u, horizontally: it has you in sight = a pursuer, a witness (0 = never)
    float wanted_heli_hold       = 15.0f;  // s it alone holds the level once the cars lost you (0 = for as long as it sees you)
    int   wanted_heli_siren      = 1;      // its siren and siren lamps
    int   wanted_heli_news       = 1;      // the radio's helicopter bulletin when the first one has you in sight
    int   wanted_heli_searchlight = 1;     // at night its searchlight follows you (its own fault switch)
    float wanted_heli_beam_fov   = 26.0f;  // deg, the spot's cone
    float wanted_heli_beam_range = 45.0f;  // u: the light's reach (never less than the distance to you + 8)
    float wanted_heli_beam_intensity = 2.0f;   // x the lamp's shipped colour
    int   wanted_heli_beam_axis  = -1;     // which way along the lamp's axis a spot shines [U]: -1 / 1 (the log's calibration line)
    int   wanted_heli_beam_mesh  = 1;      // the visible beam down to you (0 = only the light on the ground)
    int   wanted_flee            = 1;      // civilian cars near a fast chase flee it (wanted_flee.h, the cut DriverlessFlee)
    int   wanted_flee_level      = 2;      // ...from this level (1..5)
    float wanted_flee_radius     = 30.0f;  // u round the chased player
    float wanted_flee_speed      = 16.0f;  // u/s they flee at

    // [life] - GTA-style street scenes (Notes\40)
    int   life_enable            = 1;
    int   life_max_scenes        = 3;      // at once
    int   life_max_people        = 10;     // in scenes at once
    float life_every             = 10.0f;  // s between new scenes, on average
    float life_fights            = 1.0f;   // x how often arguments come up (a third become fights; 0 = none)
    float life_radius            = 45.0f;  // u: how far from you people are cast...
    float life_min_radius        = 18.0f;  // ...and how near (the swap from crowd to actor out of your face)
    int   life_reactions         = 1;      // the crowd runs from a police chase or your crash, scatters from punches
    int   life_crowd_chats       = 1;      // passers-by who meet stop for a chat (the crowd's own, no actors)
    float life_crowd_chat_every  = 6.0f;   // s, at most one new pair this often
    // the krawlie dance party (life.h Party; the cut PumpUpTheKrawlieJam, Notes\46)
    int   life_party_hotkey      = 1;      // Ctrl+Alt+B (player 1) and the pad chord: a dance party round you
    int   life_party_pad         = 0xC0;   // the pad chord held 0.8 s (XInput bits, parsed from "LS+RS" = 0xC0; 0 = none)
    float life_party_radius      = 30.0f;  // u round the player who asked
    float life_party_every       = 900.0f; // s: by the beach party, one now and then, on average (0 = never)

    // [radio] - the GTA-style radio (radio.h, Notes\35)
    int   radio_enable        = 1;
    float radio_volume        = 0.8f;     // 0..1.5
    int   radio_in_heli       = 1;        // helicopters and planes have one too
    int   radio_pad_next      = 0;        // controller buttons (parsed from names; default D-pad
    int   radio_pad_prev      = 0;        // right / left)
    float radio_duck_db       = -9.0f;    // a song under a host's talk-over
    float radio_static_db     = -16.0f;   // the tuning static between stations
    int   radio_start_station = 0;        // 1..6 = that station first; 0 = a random one
    float radio_game_music    = 0.0f;     // the game's own music while the radio plays (0 = silent, 1 = as is)
    char  radio_breaking[256] = {};       // stations with wanted-mode news (ids; "all" / "none")
    float radio_breaking_gap  = 240.0f;   // seconds between two wanted bulletins on one station
    // [missions1] the mission newscasts (radio.h, Notes\68): a flash after each main story mission
    int   radio_mission_news  = 1;
    int   radio_mission_interrupt = 1;    // 1 = cut in at the next song boundary; 0 = wait for the station's next news break
    char  radio_mission_stations[256] = {};   // stations that read it (ids; "all")
    float radio_mission_keep_h = 6.0f;    // hours a flash waits to be heard (then: recaps only)
    float radio_mission_recap_chance = 0.35f; // per news break on the tuned station
    int   radio_mission_recap_max = 2;    // recaps per mission
    float radio_mission_recap_h = 3.0f;   // hours after the flash recaps may come
    float radio_mission_recap_gap = 20.0f;// minutes between two recaps of one mission
    int   radio_mission_recap_reuse = 1;  // no news:mission_recap clip: the flash itself as the recap
    int   radio_mission_once = 0;         // 1 = a mission's news only ever once (whatever the save)
    int   radio_pad_block     = 1;        // hide the radio's D-pad buttons from the game while in a vehicle
                                          // (D-pad left / right also open the Criminal Scanner)
    char  radio_manifest[MAX_PATH] = {};  // relative to the DLL's folder
    char  radio_root[MAX_PATH] = {};      // the content folder (empty = the manifest's folder)
    // [radio] the music (radio.h, 2026-11-13): ai | library | service | talk, per station too
    char  radio_music[16]         = "ai";
    RadioStationKeys radio_station[16] = {};   // music_<id> / playlist_<id> / genre_<id>
    int   radio_station_n         = 0;
    char  radio_lib_folders[2048] = {};   // UTF-8, '|' separated (empty = the Windows Music library)
    int   radio_lib_min           = 8;
    char  radio_lib_short[8]      = "talk";
    char  radio_lib_skip[512]     = "horrorcore, gangsta, drill";
    char  radio_lib_holiday[8]    = "season";
    char  radio_lib_unsorted[48]  = "skip";
    int   radio_lib_talkover      = 0;
    char  radio_lib_cache[MAX_PATH] = "lcudrive_music.cache";
    char  radio_svc_app[128]      = "auto";
    float radio_svc_volume        = 0.8f;   // 0..1 (x the app's own volume)
    int   radio_svc_volctl        = 1;
    float radio_svc_lead          = 0.6f;   // 0..3 s
    int   radio_svc_max_min       = 15;     // 0..240
    int   radio_svc_open_tune     = 0;
    // [radiologo1] the station logo overlay (radiologo.h, ShaderFixes\radiologo_fx.ini; IniParams 16 / 17)
    int   radio_logo              = 1;      // show the station's logo when it changes (0 = never)
    float radio_logo_seconds      = 7.0f;   // on screen, its 1 s fade-out included (3..20)
    int   radio_logo_corner       = 0;      // 0 bottom right, 1 top right, 2 bottom centre, 3 top centre, 4 right middle
    float radio_logo_scale        = 1.0f;   // 1 = 15% of the screen height (0.4..2.5)
    int   radio_logo_on_enter     = 1;      // also when you get into a vehicle with the radio on

    // [menu] - Mod Options in the pause menu (pausemenu.h); read at startup only
    int   menu_enable      = 1;      // 0 = no Mod Options row (the ini still works)
    // [world] - the sea round Port Atlas (lcudrive.cpp RunSafeSea)
    int   world_safe_sea   = 1;      // 1 = no deep-water shark east of Fresco (Port Atlas has no shallows yet)
    float world_safe_sea_x = 285.0f; // the safe sea starts at this world x (Fresco's east quay is at 277)
    // [shark] - the rare shark (shark.h, Notes 61): swim anywhere; a shark only after a long swim
    int   shark_enable         = 1;      // 0 = not hooked: the game's own shark (restart to change)
    int   shark_classic        = 0;      // 1 = hooked, but the game's own rule (the shark at once over deep water)
    float shark_grace_s        = 180.0f; // s of non-stop deep-water swimming before any chance at all
    float shark_chance_per_min = 0.05f;  // chance per minute right after the grace (0 = never)
    float shark_ramp           = 0.5f;   // + this x chance_per_min for every further minute
    float shark_max_per_min    = 0.5f;   // the chance per minute never goes above this
    float shark_warn_s         = 0.0f;   // s of warning before the bite (no stock cue is callable yet)
    float shark_guard_s        = 15.0f;  // never within this many s of a respawn / teleport
    // [voice] - the cut voice lines (voice.h, Notes\46 §4)
    int   voice_enable         = 1;
    int   voice_shark          = 1;      // Chase after the deep-water shark took him
    int   voice_order_car      = 1;      // Chase at a car call-in point
    int   voice_phone          = 1;      // Chase when a video call rang out unanswered
    int   voice_swim           = 1;      // Chase's first swim
    int   voice_ped_thanks     = 1;      // a passer-by thanks (the pickpocket caught, a gift)
    int   voice_ped_nothanks   = 1;      // ...is let down (the thief got away)
    int   voice_ped_witness    = 1;      // ...reacts to your crash
    int   voice_once           = 1;      // shark / call-in / swim: once per session per player
    float voice_chase_cooldown = 300.0f; // s before the same one again (once = 0)
    float voice_phone_cooldown = 60.0f;  // s between two "answer that call" lines
    float voice_ped_gap        = 12.0f;  // s between two passers-by's lines
    float voice_ped_hear       = 30.0f;  // u: a passer-by speaks only this near a player
    float voice_falloff        = 40.0f;  // u: how far the lines carry

    // [dino] - the rideable T-Rex at the airport (dino.h)
    int   dino_enable      = 1;      // T-Rexes wait at the airport while a player is near
    int   dino_count       = 2;      // how many, 0..4 (one each for two players)
    char  dino_model[48]   = "Behemoth";   // an AITypes.txt Character.RideableCreature type; Behemoth = the T-Rex
    float dino_x           = 183.0f; // the first one's stand (the open apron south-west of the dock basin)
    float dino_y           = 1.64f;
    float dino_z           = 324.0f;
    float dino_step_x      = 6.0f;   // each further one stands this far on from the last
    float dino_step_z      = -1.0f;
    float dino_heading     = -45.0f; // degrees; 0 = +z (toward the runway quay), 90 = +x (out to sea)
    float dino_near        = 90.0f;  // u from a stand: a player this close gets a T-Rex on it
    float dino_far         = 220.0f; // u: nobody this close -> the waiting T-Rex goes
    float dino_leave       = 60.0f;  // u: one left elsewhere goes once nobody is this close (after 10 s)
    int   dino_invulnerable = 1;     // as the story mission's (SetInvulnerable)
    // [dino] ...2: a second group of stands with another creature (dino.h; the cut Pteranodon on Port
    // Atlas's lookout terrace - needs its aitypes.txt line + character .txt from a PATCH.DAT, else it stays empty)
    int   dino2_count      = 2;      // 0..4
    char  dino2_model[48]  = "Pteranodon";   // a Character.RideableCreature type ("" = none)
    float dino2_x          = 427.0f; // the north rim of the lookout terrace, beside the leaning tower
    float dino2_y          = 19.62f;
    float dino2_z          = 29.5f;
    float dino2_step_x     = 8.0f;
    float dino2_step_z     = 0.0f;
    float dino2_heading    = 0.0f;   // +z: out over the sea
    // [pteranodon] - the Pteranodon flies (ptero.h / ptero_model.h, Notes\46 §4.4)
    int   ptero_enable     = 1;      // a ridden Pteranodon flies: JUMP takes off / beats the wings, the left stick banks and pitches
    char  ptero_creature[48] = "pteranodon";  // a ridden creature whose definition name holds this flies
    float ptero_cruise     = 11.0f;  // u/s: the hands-off glide speed
    float ptero_stall      = 5.5f;   // u/s: below this the wings stop carrying it (the nose drops)
    float ptero_max_speed  = 26.0f;  // u/s: the fastest dive
    float ptero_glide      = 6.0f;   // deg: the hands-off glide slope (sink = cruise * sin)
    float ptero_climb      = 24.0f;  // deg: the path at full pull (speed bleeds away)
    float ptero_dive       = 55.0f;  // deg: the path at full push (speed builds)
    float ptero_flap_lift  = 2.5f;   // u/s up per wingbeat
    float ptero_flap_rate  = 0.45f;  // s between wingbeats while JUMP is held
    float ptero_flap_climb = 16.0f;  // deg the path tilts up while the wings work (holding A climbs)
    float ptero_takeoff_up = 5.5f;   // u/s up at the take-off leap
    float ptero_bank       = 40.0f;  // deg of bank at full stick
    float ptero_turn       = 80.0f;  // deg/s of turn at full bank
    float ptero_ceiling    = 200.0f; // world y: no climbing above it (the helicopter's servo ceiling, Notes\27)
    float ptero_sea_y      = 0.35f;  // the sea's surface: it skims, never swims (-9999 = only water the game reports)
    float ptero_skim       = 0.6f;   // u above the water its feet are held
    float ptero_tilt       = 1.0f;   // 0..1 of the bank / pitch the body shows (0 = it only turns)
    float ptero_tilt_pitch = 25.0f;  // deg: the body's pitch limit
    int   ptero_pitch_sign = 1;      // 1 = stick forward dives (as the planes); -1 = stick forward climbs
    float ptero_auto_glide = 2.5f;   // u/s: a ridden one falling faster than this opens its wings (0 = never)
    int   ptero_chute      = 1;      // getting off in the air: the rider's parachute opens by itself
    int   ptero_anims      = 1;      // the creature's wings and the rider's ride pose (Notes\46 §4.5); 0 = left to the game
    float ptero_coast      = 30.0f;  // s a riderless one glides down before it is left to the game
    int   ptero_bounds     = 1;      // turn back at the edge of the world box below
    float ptero_min_x      = -400.0f;
    float ptero_max_x      = 760.0f;
    float ptero_min_z      = -460.0f;
    float ptero_max_z      = 520.0f;
    // [sites] - vehicles waiting at stands anywhere, and Port Atlas as a place (sites.h)
    int   sites_enable     = 1;      // the stands' vehicles appear while a player is near
    float sites_near       = 70.0f;  // u from a stand: a player this close gets its vehicle there
    float sites_far        = 200.0f; // u: nobody this close -> the waiting vehicle fades out
    float sites_leave      = 120.0f; // u: one left elsewhere fades once nobody is this close (after 10 s)
    int   sites_park       = 1;      // SetParked while waiting: 0 nothing, 1 cars and planes, 2 boats as well
    int   sites_heli_settle = 1;     // a waiting helicopter's pilot state 2 -> 0: it rests on its stand
    char  sites_pin[48]    = "DefaultVehicle";   // a waiting vehicle's map pin (mappins.txt pinName; "" = none)
    sitem::Stand sites_stand[sitem::kMaxSlots] = {};    // stand1..stand12 (LoadConfig: SitesDefault when the key is missing);
                                                        // 13..24 = the life-spot data's parked cars ([spots] cars)
    int   sites_atlas      = 1;      // Port Atlas is a place: the radio's district 23 inside the box, arrive / leave logged
    float sites_atlas_x0   = 350.0f, sites_atlas_x1 = 580.0f, sites_atlas_z0 = -175.0f, sites_atlas_z1 = 50.0f;

    // [spots] life at the new places (spots.h, Notes\51): people, parked cars and driving cars from the life-spot data
    int   spots_enable     = 1;      // 0 = none of it (people, parked scenery cars, driving cars)
    int   spots_people     = 1;      // people at the spots
    int   spots_max_people = 10;     // at once, everywhere (1..16)
    int   spots_per_spot   = 4;      // at one spot (a spot's own count is the other limit)
    float spots_near       = 55.0f;  // u: a player this close to a spot fills it
    float spots_far        = 85.0f;  // u: nobody this close -> its people go
    float spots_min_spawn  = 12.0f;  // u: nobody appears closer to a player than this
    float spots_walk       = 0.35f;  // the share of a person's turns that is a walk to another point (0 = they stay)
    float spots_walk_speed = 1.0f;   // u/s a person walks at: MoveTo's own pace pick, as the game's WalkTo scripts (0.8-1);
                                     // without it they used the definition's slowest pace = the sneaking tiptoe (11-25)
    float spots_night      = 0.5f;  // the share of each spot's people out after dark
    int   spots_invulnerable = 1;    // as the scripts' bystanders (0 = they can be knocked apart)
    int   spots_cars       = 1;      // the data's parked cars (extra [sites] stands, no map pin)
    // [diycars1] [diycars] - the DIY cars' garage (diycars_model.h): their stands follow the spot cars in the extra slots
    int   diy_enable       = 1;      // 0 = no DIY cars wait anywhere
    float diy_spacing      = 4.5f;   // u between two cars of the garage row (stands must be 4 u apart)
    char  diy_garage[128]  = "";     // x, y, z, heading [, label] (empty = no garage)
    char  diy_cars[512]    = "";     // the DIY cars, AITypes names (SmallCars_CraterDIY1, ...)
    int   diy_first        = sitem::kMaxSlots;   // the first [sites] slot holding a DIY car (sites.h: not gated by [spots])
    int   diy_count        = 0;
    int   spots_drive      = 8;      // how many of the data's routes have a car driving them (0..8; 8 = all)
    float spots_drive_speed = 1.0f;  // x the route's own speed
    int   spots_log        = 0;      // 1 = a line every 10 s per driving car (steer / throttle / speed)
    char  spots_file[64]   = "lcudrive_spots.txt";   // beside lcudrive.ini; missing = the built-in table
    uint32_t spots_stamp   = 0;      // changes when the data does (spots.h lets everyone go and starts again)
    spm::Data spots_data   = {};     // the data in force
    // [round3] [animals] animals at the Wilds, the farm, the beaches, the quays (animals.h, Notes\64)
    int   animals_enable    = 1;      // 0 = no animals
    int   animals_max       = 10;     // at once, everywhere (0..16)
    int   animals_per_herd  = 6;      // at one herd (a herd's own count is the other limit)
    float animals_near      = 70.0f;  // u: a player this close to a herd's centre fills it...
    float animals_min_spawn = 25.0f;  // u: ...only while every player is further than this from it (no pop-in)
    float animals_far       = 100.0f; // u: nobody this close -> it goes
    float animals_shy       = 1.0f;   // x every behaviour's shy distance (0 = they never run or fly off)
    float animals_respawn   = 60.0f;  // s: a bird that flew off is back after this
    int   animals_invulnerable = 1;   // nothing can hurt them (and they never hurt anyone: no order is ever at a player)
    char  animals_file[64]  = "lcudrive_animals.txt";   // beside lcudrive.ini; missing = no animals
    uint32_t animals_stamp  = 0;      // changes when the data does
    anm::Data animals_data  = {};
    // [round3] [npc] the city's people speak + behave (npc_glue.h, Notes 62)
    int   npc_enable     = 1;         // 0 = all of the NPC overhaul off
    int   npc_voices     = 1;         // their lines (stage 1)
    int   npc_reactions  = 1;         // lines when something happens near people
    int   npc_greetings  = 1;         // passers-by greeting you, fans
    int   npc_smalltalk  = 1;         // overheard conversations, phone calls, weather / hour remarks
    int   npc_subtitles  = 0;         // 1 = their lines as subtitles (the NPCS_ flow files)
    int   npc_per_hour   = 150;       // at most this many lines an hour (both players together)
    float npc_hear       = 24.0f;     // u: lines further than this from both players are not spent
    float npc_chat_every = 45.0f;     // s between overheard exchanges, on average
    int   npc_max_loaded = 6;         // voice banks kept loaded (route game)
    int   npc_route      = 2;         // 0 = game (the dialogue system), 1 = xaudio (lcudrive's own audio), 2 = auto
    float npc_volume     = 1.0f;      // x the xaudio route's level
    int   npc_life       = 1;         // stage 2: street-life scenes re-weighted by the hour / weather / place
    int   npc_spots      = 1;         // stage 4: the people at the life spots say a line now and then
    char  npc_table[64]  = "npc_runtime.tsv";   // beside lcudrive.dll
    char  npc_vo_dir[64] = "npc_vo";            // the xaudio route's paks (a copy of the PATCH.DAT ones), beside lcudrive.dll
    // [backrooms1] [backrooms] the Backrooms behind the village door (backrooms.h, Notes 54). Every key has an off switch.
    int   backrooms_enable    = 1;   // 0 = the village door is only a door (anyone inside is brought out)
    int   backrooms_statues   = 1;   // the watching statues
    int   backrooms_colossi   = 0;   // [br4] the SCALED-ACTOR colossi (0: the pool halls' giants are brick-built level geometry now - perfectly still;
                                     //  1 also spawns the old scaled actors in the same places: unproven, for comparison only)
    int   backrooms_goal_studs = 25000;   // [br4] (no longer used: [br5] vault_studs)
    int   backrooms_vault_studs = 50000000;   // [br5] studs added to the bank on walking into the vault with every brick found (0 = none; the bank is clamped at 999,999,999,999)
    int   backrooms_easy_key  = 0x08;     // [br4] the keyboard's easy way out: hold this virtual key (Backspace) for easy_out_hold seconds
    char  backrooms_easy_buttons[32] = "LB+RB";   // [br4] the controller's: hold these together (names as [drive] buttons; NONE = off)
    int   backrooms_hud       = 1;        // [br4] the goal counter / arrows / reminders on screen (0 = none; the goal still counts)
    int   backrooms_workers   = 1;   // [br3] calm people in white at the consoles of the rare lab rooms (they do not react)
    int   backrooms_vehicles  = 1;   // [br3] the game's own vehicles stood in their rooms (the pump cart in the rail rooms)
    int   backrooms_freeze    = 1;   // [freeze] statues / colossi / workers hold perfectly still (anim speed 0; 0 = the engine's idle breathing)
    int   backrooms_worker_repose = 1; // [freeze] a worker takes a new pose every 30-90 s while nobody looks (0 = never moves)
    int   backrooms_effects   = 1;   // the shader overlay at all (0 = no IniParams write)
    int   backrooms_handicam  = 1;   // the camcorder look (0 = only the grade, vignette and slow wobble)
    int   backrooms_intensity = 2;   // 0..3
    int   backrooms_pillarbox = 0;   // 4:3 black bars
    int   backrooms_blink_ms  = 120; // [fix2] the LONGEST black at a connector crossing - it ends as soon as the camera has caught up (0..400; 0 = none)
    int   backrooms_visit     = 0;   // a CHANGE to 1 = both players are dropped in now (testing)
    int   backrooms_leave     = 0;   // a CHANGE to 1 = out at once, both players at the village
    char  backrooms_file[64]  = "lcudrive_backrooms.txt";   // beside the DLL; missing = the built-in table (backrooms_table.h)
    int   backrooms_trace     = 1;   // [fix1] a log line every 0.25 s of the arrival (position, speed, on-ground, probe, level)
    // [fix2] the camera at a connector (backrooms.h): 2 = the engine's camera cut (its respawn recipe), 1 = only the snap bit, 0 = none;
    // the render-copy hold keeps each view at its offset to the player until the game camera is within portal_catch u;
    // the black runs from the move until the camera caught up, at most blink_ms
    int   backrooms_portal_snap  = 2;
    int   backrooms_portal_hold  = 1;
    int   backrooms_portal_black = 1;
    float backrooms_portal_catch = 1.0f;

    // [weather] rain, fog and weather zones (weather.h, Notes\53). Everything off = the game untouched.
    int   weather_enable   = 1;      // 0 = none of it (no engine fog / rain writes, the shader slots at 0)
    int   weather_rain     = 4;      // everywhere: 0 off, 1 drizzle, 2 rain, 3 storm, 4 changing (2026-11-25 default)
    int   weather_fog      = 0;      // everywhere: 0 off, 1 light, 2 heavy
    int   weather_zones    = 1;      // the weather zones of the data ("Village Weather"); 0 = only what is set above
    int   weather_engine_fog  = 1;   // drive the engine's own exponential height fog (the thick, low fog)
    int   weather_engine_rain = 0;   // EXPERIMENTAL: switch the engine's own rain stage on (3D drops, splashes)
    int   weather_screen_rain = 1;   // rain drawn on the screen by the main-filter shaders (IniParams 4.y)
    float weather_overcast = 1.0f;   // x the grey, flat light under rain and fog (0 = no grade)
    float weather_mist     = 1.0f;   // x the drifting veils on the screen in fog (0 = none)
    float weather_blend    = 4.0f;   // s: the time the weather takes to change
    float weather_night_fog = 0.6f;  // the share of the fog kept after dark
    float weather_vis_light = 150.0f;   // u: light fog - where the veil has taken ~90 %
    float weather_vis_heavy = 48.0f;    // u: heavy fog
    float weather_low_light = 0.04f;    // light fog: the ground fog's density at height 0
    float weather_low_heavy = 0.12f;    // heavy fog
    float weather_thick_light = 12.0f;  // u: light fog: the ground fog's thickness
    float weather_thick_heavy = 7.0f;   // u: heavy fog
    float weather_weird    = 0.45f;  // a weird sub-zone: sight x (1 - this), ground fog x (1 + 2 x this)
    float weather_shower   = 360.0f; // s: one turn of a zone's rain (it rains the zone's duty share of it)
    float weather_spell    = 300.0f; // s: rain = 4 (Changing): how long one spell of weather lasts
    float weather_wet_share = 0.3f;  // rain = 4: the share of the spells with rain in them
    int   weather_log      = 0;      // 1 = a line every 5 s per view (where, the mix, what was written)
    char  weather_file[64] = "lcudrive_weather.txt";   // beside lcudrive.ini; missing or empty = no zones
    uint32_t weather_stamp = 0;      // changes when the data does
    wxm::Table weather_data = {};    // the zones in force
    // [gfx] the graphics overhaul (gfx.h, Notes\57). preset 0 = Stock: nothing written anywhere.
    int   gfx_preset       = 1;      // 0 Stock, 1 Enhanced, 2 Performance
    float gfx_grade        = 1.0f;   // x the time-of-day colour grade (engine colour stage)
    float gfx_ao           = 1.0f;   // x the change to ambient occlusion
    float gfx_detail       = 1.0f;   // x the change to object draw distance
    float gfx_rim          = 1.0f;   // x the night rim light on figures
    float gfx_haze         = 1.0f;   // x the aerial haze (only where the level has height fog)
    float gfx_haze_dist    = 400.0f; // u: where the haze reaches its full depth
    float gfx_ca           = 1.0f;   // x chromatic aberration (lens block, main-filter shaders)
    float gfx_sharpen      = 1.0f;   // x sharpening
    float gfx_vignette     = 1.0f;   // x vignette
    float gfx_grain        = 1.0f;   // x film grain
    float gfx_heat         = 0.0f;   // heat shimmer round noon (0 = off, up to 2)
    int   gfx_camera       = 1;      // camera feel: 0 off, 1 subtle, 2 strong
    float gfx_camera_scale = 1.0f;   // x the camera feel
    int   gfx_bank_sign    = 1;      // +1 / -1: which way the camera leans into a turn
    // [fix1] the far plane: the sky dome is a world-fixed mesh round the map centre; far out (the Outport, the Wilds)
    // its far side lies beyond the camera's far plane and is cut. Raised on the render copy only, only out there.
    int   gfx_far_fix      = 1;      // 0 = the game's far plane everywhere
    float gfx_far_from     = 600.0f; // only where the camera is this far (x, z) from the map centre
    float gfx_far_dome     = 1600.0f;// the dome's radius (u): far >= distance from the centre + this, x 1.05
    float gfx_far_max      = 6000.0f;// never beyond this
    int   gfx_reshade_aware = 1;     // 1 = no engine grade while ReShade's LCU_DayNightCycle is on (no double grade)
    int   gfx_reshade_grade = 0;     // (read from ReShadePreset.ini at load: is that technique on?)
    // [gfx] gfx2 (gfx2.h, Notes\57 section 9): each part has a switch and a scale
    int   gfx_bloom        = 1;      // the engine's bloom by the time of day (night glow)
    float gfx_bloom_scale  = 1.0f;
    int   gfx_far_blur     = 1;      // the very light distance-only blur (main-filter shaders; off in split-screen)
    float gfx_far_blur_scale = 1.0f;
    float gfx_far_blur_start = 140.0f;   // u
    float gfx_far_blur_full  = 420.0f;   // u
    int   gfx_sun_rays     = 1;      // sun shafts + lens flare (main-filter shaders; off in split-screen)
    float gfx_sun_rays_scale = 1.0f;
    float gfx_flare_scale  = 1.0f;
    int   gfx_moonlight    = 1;      // the night ambient floor (the deferred light pass)
    float gfx_moonlight_scale = 1.0f;
    int   gfx_rt           = 0;      // [gfx3] ray-traced light: screen-space bounce light + occlusion (one view only)
    float gfx_rt_scale     = 1.0f;   //        x its strength (0 .. 2)
    int   gfx_reflections  = 1;      // the live cube map's exposure up while it rains
    float gfx_reflections_scale = 1.0f;
    int   gfx_split_fx     = 0;      // 1 = far blur / sun rays in split-screen too
    int   gfx_fps          = 0;      // the frame counter overlay
    int   gfx_auto         = 0;      // the Auto governor
    float gfx_auto_target  = 60.0f;  // fps
    // [labels1] [maplabels]: map pins + location names for the new places (maplabels.h, Notes 59 section 6)
    int   maplabels_enable         = 1;   // 0 = nothing (read at startup: the hooks are not installed)
    int   maplabels_label          = 1;   // the map's location line names the new places
    int   maplabels_pins_districts = 1;   // pins for the new districts / the Wilds' regions (when the map builds its pins)
    int   maplabels_pins_places    = 1;   // stations, Sky Rail, landmarks
    int   maplabels_pins_boats     = 1;   // marinas and boat posts
    marm::Config marina = {};        // [marina1] [marina]: berths, anchored boats, boat stations (marina.h, Notes 56)
    btm::Params  boats  = {};        // [boats2] [boats]: spawn spacing, file breaker, cap (boats.h, Notes 56 section 8)
    // [debug]
    int   log_every        = 0;      // >0: one state line in lcudrive.log every N player frames
};

// [sites] standN when the ini has no such key (the shipped ini spells the same lines out). The Port
// Atlas three are placeholders by the test deck (deck top y 1.00 to x 386, lip to 387.5, open water
// beyond [P KdTerrain of portatlas_2717]); the fourth is off the kerb of the car park on Fresco's east
// shore (kerb x 302.0..302.2, z -80.4..-70.0, quay y 1.20; no terrain within 5 u of the berth [P]).
static const char* SitesDefault(int i)
{
    static const char* const k[4] = {
        "Naval_Arowana, boat, 394.5, 0.35, -90.0, 180, Port Atlas harbour berth 1",
        "Naval_Surger, boat, 394.5, 0.35, -104.0, 180, Port Atlas harbour berth 2",
        "Aircraft_Hera, heli, 404.0, 1.25, -101.0, 90, Port Atlas helicopter apron",
        "Naval_Arowana, boat, 307.5, 0.35, -75.2, 90, Fresco east quay",
    };
    return i >= 0 && i < 4 ? k[i] : "";
}

// A controller chord: pad::Parse names joined with '+' ("LS+RS", "BACK+DUP"); all must be held. A part
// that is no button name makes the whole chord 0 (none), as does "NONE".
static uint32_t ParsePadChord(const char* s)
{
    uint32_t r = 0;
    char part[16];
    int n = 0;
    for (const char* p = s; ; ++p)
    {
        const char c = *p;
        if (c == '+' || c == 0)
        {
            part[n] = 0;
            char* a = part;
            while (*a == ' ' || *a == '\t') ++a;
            for (int k = static_cast<int>(strlen(a)) - 1; k >= 0 && (a[k] == ' ' || a[k] == '\t'); --k) a[k] = 0;
            const uint32_t b = pad::Parse(a);
            if (!b) return 0;
            r |= b;
            n = 0;
            if (!c) break;
        }
        else if (n < 15) part[n++] = c;
        else return 0;
    }
    return r;
}

static float IniF(const char* path, const char* sec, const char* key, float def)
{
    char buf[64];
    char defs[32];
    _snprintf_s(defs, sizeof(defs), _TRUNCATE, "%.6g", def);
    GetPrivateProfileStringA(sec, key, defs, buf, sizeof(buf), path);
    for (char* p = buf; *p; ++p) if (*p == ';' || *p == '#') { *p = 0; break; }
    char* end = nullptr;
    const float v = strtof(buf, &end);
    return (end != buf && IsFin(v)) ? v : def;
}

static int IniI(const char* path, const char* sec, const char* key, int def)
{
    char buf[64];
    char defs[32];
    _snprintf_s(defs, sizeof(defs), _TRUNCATE, "%d", def);
    GetPrivateProfileStringA(sec, key, defs, buf, sizeof(buf), path);
    for (char* p = buf; *p; ++p) if (*p == ';' || *p == '#') { *p = 0; break; }
    char* end = nullptr;
    const long v = strtol(buf, &end, 0);                 // accepts 0x20 as well as 32
    return (end != buf) ? static_cast<int>(v) : def;
}

// A string value, cut at a comment and trimmed.
static void IniS(const char* path, const char* sec, const char* key, const char* def, char* out, int cap)
{
    GetPrivateProfileStringA(sec, key, def, out, cap, path);
    for (char* p = out; *p; ++p) if (*p == ';' || *p == '#') { *p = 0; break; }
    for (int n = static_cast<int>(strlen(out)); n > 0 && (out[n - 1] == ' ' || out[n - 1] == '\t'); --n) out[n - 1] = 0;
}

// A colour, "r, g, b" (commas or spaces), each 0..4; anything malformed keeps the default.
static void IniRGB(const char* path, const char* sec, const char* key, const float def[3], float out[3])
{
    char buf[96];
    IniS(path, sec, key, "", buf, sizeof(buf));
    for (char* p = buf; *p; ++p) if (*p == ',') *p = ' ';
    float v[3] = {};
    const bool ok = buf[0] && sscanf_s(buf, "%f %f %f", &v[0], &v[1], &v[2]) == 3 && IsFin(v[0]) && IsFin(v[1]) && IsFin(v[2]);
    for (int k = 0; k < 3; ++k) out[k] = ok ? Clampf(v[k], 0.0f, 4.0f) : def[k];
}

// A string that can hold a path or a link: a comment starts only at ';' or '#' after a space or a
// tab (a folder can be called "Mix #2", a link can carry '#'), trimmed, outer quotes dropped. Kept
// as UTF-8: the bytes as they are when they are valid UTF-8, else from the ANSI code page.
static void IniTextClean(char* out, int cap)
{
    int a = 0;
    while (out[a] == ' ' || out[a] == '\t') ++a;
    if (a) memmove(out, out + a, strlen(out + a) + 1);
    int n = 0;
    if (out[0] == '"')
    {
        // "a quoted value": everything up to the closing quote, whatever follows (a comment) dropped
        const char* q = strchr(out + 1, '"');
        n = q ? static_cast<int>(q - out - 1) : static_cast<int>(strlen(out + 1));
        memmove(out, out + 1, static_cast<size_t>(n));
        out[n] = 0;
    }
    else
    {
        for (char* p = out; *p; ++p)
            if ((*p == ';' || *p == '#') && (p == out || p[-1] == ' ' || p[-1] == '\t')) { *p = 0; break; }
        n = static_cast<int>(strlen(out));
        while (n > 0 && (out[n - 1] == ' ' || out[n - 1] == '\t' || out[n - 1] == '\r' || out[n - 1] == '\n')) out[--n] = 0;
    }
    if (n > 0 && MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, out, n, nullptr, 0) <= 0)
    {
        wchar_t w[2048] = {};
        const int wn = MultiByteToWideChar(CP_ACP, 0, out, n, w, 2047);
        if (wn > 0)
        {
            const int m = WideCharToMultiByte(CP_UTF8, 0, w, wn, out, cap - 1, nullptr, nullptr);
            out[m > 0 ? m : 0] = 0;
        }
    }
}

// GetPrivateProfileStringA drops the quotes of a value that is quoted from end to end ("D:\Mix #2"
// comes back as D:\Mix #2, and the comment rule would then cut it), so the value is taken raw from
// the section (GetPrivateProfileSectionA keeps the quotes): the first line of that key, as the API
// would pick it.
static void IniText(const char* path, const char* sec, const char* key, const char* def, char* out, int cap)
{
    strncpy_s(out, static_cast<size_t>(cap), def, _TRUNCATE);
    const DWORD bcap = 32768;
    char* buf = new (std::nothrow) char[bcap];
    if (!buf) { GetPrivateProfileStringA(sec, key, def, out, cap, path); IniTextClean(out, cap); return; }
    const DWORD got = GetPrivateProfileSectionA(sec, buf, bcap, path);
    const size_t kn = strlen(key);
    for (const char* line = buf; got && line < buf + got && *line; line += strlen(line) + 1)
    {
        const char* p = line;
        while (*p == ' ' || *p == '\t') ++p;
        if (*p == ';' || *p == '#') continue;
        const char* eq = strchr(p, '=');
        if (!eq) continue;
        size_t n = static_cast<size_t>(eq - p);
        while (n > 0 && (p[n - 1] == ' ' || p[n - 1] == '\t')) --n;
        if (n != kn || _strnicmp(p, key, kn) != 0) continue;
        strncpy_s(out, static_cast<size_t>(cap), eq + 1, _TRUNCATE);
        break;
    }
    delete[] buf;
    IniTextClean(out, cap);
}

// [radio] music_<id>, playlist_<id>, genre_<id>: the whole section is read and the keys merged per
// station id (lower-cased; at most 16). Ids the stations don't have are kept: the radio logs them.
static void LoadRadioStations(const char* path, Config& c)
{
    memset(c.radio_station, 0, sizeof(c.radio_station));
    c.radio_station_n = 0;
    const DWORD cap = 32768;
    char* buf = new (std::nothrow) char[cap];
    if (!buf) return;
    const DWORD got = GetPrivateProfileSectionA("radio", buf, cap, path);
    for (const char* line = buf; got && line < buf + got && *line; line += strlen(line) + 1)
    {
        while (*line == ' ' || *line == '\t') ++line;
        if (*line == ';' || *line == '#') continue;
        const char* eq = strchr(line, '=');
        if (!eq) continue;
        char key[128] = {};
        int kn = static_cast<int>(eq - line);
        while (kn > 0 && (line[kn - 1] == ' ' || line[kn - 1] == '\t')) --kn;
        if (kn <= 0 || kn >= static_cast<int>(sizeof(key))) continue;
        for (int i = 0; i < kn; ++i) key[i] = static_cast<char>(line[i] >= 'A' && line[i] <= 'Z' ? line[i] + 32 : line[i]);
        int field = -1; const char* id = nullptr;
        if (strncmp(key, "music_", 6) == 0) { field = 0; id = key + 6; }
        else if (strncmp(key, "playlist_", 9) == 0) { field = 1; id = key + 9; }
        else if (strncmp(key, "genre_", 6) == 0) { field = 2; id = key + 6; }
        if (field < 0 || !id[0] || strlen(id) >= sizeof(c.radio_station[0].id)) continue;
        RadioStationKeys* s = nullptr;
        for (int i = 0; i < c.radio_station_n; ++i) if (strcmp(c.radio_station[i].id, id) == 0) s = &c.radio_station[i];
        if (!s)
        {
            if (c.radio_station_n >= 16) continue;
            s = &c.radio_station[c.radio_station_n++];
            strcpy_s(s->id, id);
        }
        char val[1024] = {};
        strncpy_s(val, eq + 1, _TRUNCATE);
        IniTextClean(val, sizeof(val));
        if (field == 0) strncpy_s(s->music, val, _TRUNCATE);
        else if (field == 1) strncpy_s(s->playlist, val, _TRUNCATE);
        else strncpy_s(s->genre, val, _TRUNCATE);
    }
    delete[] buf;
}

static void LoadConfig(const char* path, Config& c)
{
    Config d;
    c.enable           = IniI(path, "general", "enable", d.enable);
    c.bridge           = IniI(path, "general", "bridge", d.bridge);
    c.telemetry        = IniI(path, "general", "telemetry", d.telemetry);
    c.bridge_speed     = Clampf(IniF(path, "general", "bridge_speed", d.bridge_speed), 1.0f, 500.0f);
    c.handbrake_key    = IniI(path, "general", "handbrake_key", d.handbrake_key);
    c.brake_key_brakes = IniI(path, "general", "brake_key_brakes", d.brake_key_brakes);
    c.engine_sound     = IniI(path, "general", "engine_sound", d.engine_sound);
    c.mission_stock    = IniI(path, "general", "mission_stock", d.mission_stock) ? 1 : 0;                   // [missions1]
    c.mission_stock_side = IniI(path, "general", "mission_stock_side", d.mission_stock_side) ? 1 : 0;
    c.mission_stock_enter = Clampf(IniF(path, "general", "mission_stock_enter", d.mission_stock_enter), 0.0f, 60.0f);
    c.mission_stock_exit = Clampf(IniF(path, "general", "mission_stock_exit", d.mission_stock_exit), 0.0f, 120.0f);
    c.mission_stock_safe_speed = Clampf(IniF(path, "general", "mission_stock_safe_speed", d.mission_stock_safe_speed), 0.1f, 100.0f);
    c.mission_detect_stable = Clampf(IniF(path, "general", "mission_detect_stable", d.mission_detect_stable), 0.2f, 30.0f);
    c.mission_detect_settle = Clampf(IniF(path, "general", "mission_detect_settle", d.mission_detect_settle), 0.0f, 600.0f);
    c.mission_detect_absent = Clampf(IniF(path, "general", "mission_detect_absent", d.mission_detect_absent), 1.0f, 600.0f);
    {
        char buf[32];
        GetPrivateProfileStringA("general", "pad_handbrake", "NONE", buf, sizeof(buf), path);
        for (char* q = buf; *q; ++q) if (*q == ';' || *q == '#' || *q == ' ' || *q == '\t') { *q = 0; break; }
        c.pad_handbrake = static_cast<int>(pad::Parse(buf));
    }
    c.pedal_handbrake  = IniI(path, "general", "pedal_handbrake", d.pedal_handbrake);
    c.brake_drift      = Clampf(IniF(path, "general", "brake_drift", d.brake_drift), 0.0f, 1.0f);
    c.stick_deadzone   = Clampf(IniF(path, "general", "stick_deadzone", d.stick_deadzone), 0.0f, 0.5f);
    c.steer_curve      = Clampf(IniF(path, "general", "steer_curve", d.steer_curve), 0.5f, 3.0f);

    c.power            = Clampf(IniF(path, "feel", "power", d.power), 0.2f, 5.0f);
    c.top_speed        = Clampf(IniF(path, "feel", "top_speed", d.top_speed), 0.4f, 3.0f);
    c.grip             = Clampf(IniF(path, "feel", "grip", d.grip), 0.3f, 3.0f);
    c.balance          = Clampf(IniF(path, "feel", "balance", d.balance), -1.0f, 1.0f);
    c.brakes           = Clampf(IniF(path, "feel", "brakes", d.brakes), 0.1f, 3.0f);
    c.steer_speed      = Clampf(IniF(path, "feel", "steer_speed", d.steer_speed), 0.2f, 4.0f);
    c.steer_lock       = Clampf(IniF(path, "feel", "steer_lock", d.steer_lock), 0.3f, 2.0f);
    c.weight           = Clampf(IniF(path, "feel", "weight", d.weight), 0.0f, 2.0f);
    c.handbrake        = Clampf(IniF(path, "feel", "handbrake", d.handbrake), 0.0f, 3.0f);
    c.breakaway        = Clampf(IniF(path, "feel", "breakaway", d.breakaway), 0.0f, 2.0f);
    c.traction_control = Clampf(IniF(path, "feel", "traction_control", d.traction_control), 0.0f, 1.0f);
    c.yaw_damping      = Clampf(IniF(path, "feel", "yaw_damping", d.yaw_damping), 0.0f, 3.0f);
    c.yaw_inertia      = Clampf(IniF(path, "feel", "yaw_inertia", d.yaw_inertia), 0.3f, 3.0f);
    c.stability        = Clampf(IniF(path, "feel", "stability", d.stability), 0.0f, 2.0f);
    c.size_effect      = Clampf(IniF(path, "feel", "size_effect", d.size_effect), 0.0f, 2.0f);
    c.drift            = Clampf(IniF(path, "feel", "drift", d.drift), 0.0f, 1.0f);

    c.headlights       = IniI(path, "lights", "headlights", d.headlights);
    c.traffic_lights   = IniI(path, "lights", "traffic_lights", d.traffic_lights);
    c.headlight_range  = Clampf(IniF(path, "lights", "headlight_range", d.headlight_range), 2.0f, 80.0f);
    c.headlight_fov    = Clampf(IniF(path, "lights", "headlight_fov", d.headlight_fov), 10.0f, 120.0f);
    c.headlight_intensity = Clampf(IniF(path, "lights", "headlight_intensity", d.headlight_intensity), 0.1f, 8.0f);
    c.night_below      = Clampf(IniF(path, "lights", "night_below", d.night_below), -1.0f, 1.0f);
    c.headlight_cookie = IniI(path, "lights", "headlight_cookie", d.headlight_cookie);
    if (c.headlight_cookie < 0 || c.headlight_cookie > 2) c.headlight_cookie = d.headlight_cookie;
    c.headlight_beam = IniI(path, "lights", "headlight_beam", d.headlight_beam) ? 1 : 0;
    c.headlight_autofix = IniI(path, "lights", "headlight_autofix", d.headlight_autofix) ? 1 : 0;
    for (int pl = 0; pl < 2; ++pl)
    {
        char pb[32];
        IniS(path, "coop", pl ? "pad_p2" : "pad_p1", "auto", pb, sizeof(pb));
        int v = -1;                                             // auto
        if (pb[0] >= '0' && pb[0] <= '3' && !pb[1]) v = pb[0] - '0';
        else if (!_stricmp(pb, "none")) v = 4;
        (pl ? c.coop_pad_p2 : c.coop_pad_p1) = v;
    }
    c.coop_split_cuts = IniI(path, "coop", "split_cuts", d.coop_split_cuts) ? 1 : 0;

    c.sl_mode          = IniI(path, "streetlamps", "streetlamps", d.sl_mode);
    if (c.sl_mode < 0 || c.sl_mode > 2) c.sl_mode = d.sl_mode;
    c.sl_max_lights    = IniI(path, "streetlamps", "max_lights", d.sl_max_lights);
    if (c.sl_max_lights < 0) c.sl_max_lights = 0;
    if (c.sl_max_lights > 96) c.sl_max_lights = 96;
    c.sl_radius        = Clampf(IniF(path, "streetlamps", "lamp_radius", d.sl_radius), 0.5f, 20.0f);
    c.sl_range         = Clampf(IniF(path, "streetlamps", "lamp_range", d.sl_range), 10.0f, 80.0f);
    c.sl_intensity     = Clampf(IniF(path, "streetlamps", "lamp_intensity", d.sl_intensity), 0.0f, 8.0f);
    c.sl_falloff       = Clampf(IniF(path, "streetlamps", "lamp_falloff", d.sl_falloff), 0.0f, 1.0f);
    c.sl_head_drop     = Clampf(IniF(path, "streetlamps", "lamp_head_drop", d.sl_head_drop), -0.5f, 1.5f);
    c.sl_fixtures      = IniI(path, "streetlamps", "lamp_fixtures", d.sl_fixtures) ? 1 : 0;
    c.sl_log           = IniI(path, "streetlamps", "log", d.sl_log) ? 1 : 0;
    IniRGB(path, "streetlamps", "colour_sodium", d.sl_col[0], c.sl_col[0]);
    IniRGB(path, "streetlamps", "colour_white", d.sl_col[1], c.sl_col[1]);
    IniRGB(path, "streetlamps", "colour_lantern", d.sl_col[2], c.sl_col[2]);

    c.heli_enable      = IniI(path, "heli", "enable", d.heli_enable);
    c.heli_top_speed   = Clampf(IniF(path, "heli", "top_speed", d.heli_top_speed), 3.0f, 80.0f);
    c.heli_max_tilt    = Clampf(IniF(path, "heli", "max_tilt", d.heli_max_tilt), 5.0f, 60.0f);
    c.heli_climb       = Clampf(IniF(path, "heli", "climb_rate", d.heli_climb), 0.5f, 40.0f);
    c.heli_descend     = Clampf(IniF(path, "heli", "descend_rate", d.heli_descend), 0.5f, 40.0f);
    c.heli_thrust_g    = Clampf(IniF(path, "heli", "thrust_g", d.heli_thrust_g), 1.1f, 6.0f);
    c.heli_yaw_rate    = Clampf(IniF(path, "heli", "yaw_rate", d.heli_yaw_rate), 10.0f, 400.0f);
    c.heli_turn_bank   = Clampf(IniF(path, "heli", "turn_bank", d.heli_turn_bank), 0.0f, 1.0f);
    c.heli_brake       = Clampf(IniF(path, "heli", "brake_assist", d.heli_brake), 0.0f, 1.0f);
    c.heli_response    = Clampf(IniF(path, "heli", "response", d.heli_response), 0.03f, 2.0f);
    c.heli_land_height = Clampf(IniF(path, "heli", "land_height", d.heli_land_height), 0.02f, 3.0f);
    c.heli_yaw_sign    = IniF(path, "heli", "yaw_sign", d.heli_yaw_sign) < 0 ? -1.0f : 1.0f;
    c.heli_fwd_sign    = IniF(path, "heli", "fwd_sign", d.heli_fwd_sign) < 0 ? -1.0f : 1.0f;
    c.heli_vert_sign   = IniF(path, "heli", "vert_sign", d.heli_vert_sign) < 0 ? -1.0f : 1.0f;
    c.heli_stick_direct = IniI(path, "heli", "stick_direct", d.heli_stick_direct);
    c.heli_exit_anywhere = IniI(path, "heli", "exit_anywhere", d.heli_exit_anywhere);
    c.heli_bail        = IniI(path, "heli", "bail_out", d.heli_bail);
    c.heli_park_height = Clampf(IniF(path, "heli", "park_height", d.heli_park_height), 0.1f, 5.0f);
    c.heli_free_flight = IniI(path, "heli", "free_flight", d.heli_free_flight);
    c.heli_crash_spin  = Clampf(IniF(path, "heli", "crash_spin", d.heli_crash_spin), 0.0f, 15.0f);
    c.heli_wreck_on_crash = IniI(path, "heli", "wreck_on_crash", d.heli_wreck_on_crash);
    c.heli_crash_speed = Clampf(IniF(path, "heli", "crash_speed", d.heli_crash_speed), 2.0f, 60.0f);
    c.heli_crash_damage = IniI(path, "heli", "crash_damage", d.heli_crash_damage);
    c.heli_damage_speed = Clampf(IniF(path, "heli", "damage_speed", d.heli_damage_speed), 2.0f, 60.0f);
    c.plane_enable       = IniI(path, "plane", "enable", d.plane_enable);
    IniS(path, "plane", "models", d.plane_models, c.plane_models, sizeof(c.plane_models));
    c.plane_stick_direct = IniI(path, "plane", "stick_direct", d.plane_stick_direct);
    c.plane_crash_damage = IniI(path, "plane", "crash_damage", d.plane_crash_damage);
    c.plane_damage_speed = Clampf(IniF(path, "plane", "damage_speed", d.plane_damage_speed), 2.0f, 60.0f);
    c.plane_turn_sign    = IniF(path, "plane", "turn_sign", d.plane_turn_sign) < 0.0f ? -1.0f : 1.0f;
    c.plane_fwd_sign     = IniF(path, "plane", "fwd_sign", d.plane_fwd_sign) < 0.0f ? -1.0f : 1.0f;
    c.plane_gear_height  = Clampf(IniF(path, "plane", "gear_height", d.plane_gear_height), -1.0f, 3.0f);
    c.plane_auto_level   = Clampf(IniF(path, "plane", "auto_level", d.plane_auto_level), -1.0f, 3.0f);
    c.plane_level_path_rate = Clampf(IniF(path, "plane", "level_path_rate", d.plane_level_path_rate), -1.0f, 2.0f);
    c.plane_airport      = IniI(path, "plane", "airport", d.plane_airport);
    c.plane_airport_count = IniI(path, "plane", "airport_count", d.plane_airport_count);
    c.plane_airport_count = c.plane_airport_count < 1 ? 1 : c.plane_airport_count > 6 ? 6 : c.plane_airport_count;
    IniS(path, "plane", "airport_models", d.plane_airport_models, c.plane_airport_models, sizeof(c.plane_airport_models));
    IniS(path, "plane", "airport_model", d.plane_airport_model, c.plane_airport_model, sizeof(c.plane_airport_model));
    IniS(path, "plane", "airport_pin", d.plane_airport_pin, c.plane_airport_pin, sizeof(c.plane_airport_pin));
    c.plane_airport_near  = Clampf(IniF(path, "plane", "airport_near", d.plane_airport_near), 20.0f, 200.0f);
    c.plane_airport_far   = Clampf(IniF(path, "plane", "airport_far", d.plane_airport_far), c.plane_airport_near + 20.0f, 600.0f);
    c.plane_airport_leave = Clampf(IniF(path, "plane", "airport_leave", d.plane_airport_leave), 20.0f, 300.0f);

    {
        // 0 = leave the definition's value; otherwise clamped into a sane range
        auto zeroOr = [](float v, float lo, float hi) { return v <= 0.0f ? 0.0f : Clampf(v, lo, hi); };
        c.airplane_enable       = IniI(path, "airplane", "enable", d.airplane_enable);
        IniS(path, "airplane", "models", d.airplane_models, c.airplane_models, sizeof(c.airplane_models));
        c.airplane_stick_direct = IniI(path, "airplane", "stick_direct", d.airplane_stick_direct);
        c.airplane_steer_sign   = IniF(path, "airplane", "steer_sign", d.airplane_steer_sign) < 0.0f ? -1.0f : 1.0f;
        c.airplane_turn_sign    = IniF(path, "airplane", "turn_sign", d.airplane_turn_sign) < 0.0f ? -1.0f : 1.0f;
        c.airplane_fwd_sign     = IniF(path, "airplane", "fwd_sign", d.airplane_fwd_sign) < 0.0f ? -1.0f : 1.0f;
        c.airplane_rotate_deg   = Clampf(IniF(path, "airplane", "rotate_deg", d.airplane_rotate_deg), 2.0f, 9.0f);   // the model caps it at tailStrikeDeg - 2 = 9
        c.airplane_side_grip    = Clampf(IniF(path, "airplane", "side_grip", d.airplane_side_grip), 0.3f, 2.0f);
        c.airplane_rolling      = Clampf(IniF(path, "airplane", "rolling", d.airplane_rolling), 0.0f, 0.2f);
        c.airplane_brake_mu     = Clampf(IniF(path, "airplane", "brake_mu", d.airplane_brake_mu), 0.0f, 1.5f);
        c.airplane_reverse_g    = Clampf(IniF(path, "airplane", "reverse_g", d.airplane_reverse_g), 0.0f, 0.8f);
        c.airplane_taxi_reverse_speed = Clampf(IniF(path, "airplane", "taxi_reverse_speed", d.airplane_taxi_reverse_speed), 0.0f, 8.0f);
        c.airplane_crash_damage = IniI(path, "airplane", "crash_damage", d.airplane_crash_damage);
        c.airplane_crash_destroy_speed = Clampf(IniF(path, "airplane", "crash_destroy_speed", d.airplane_crash_destroy_speed), 0.0f, 200.0f);
        c.beeps = IniI(path, "general", "beeps", d.beeps) ? 1 : 0;
        c.airplane_max_velocity = zeroOr(IniF(path, "airplane", "max_velocity", d.airplane_max_velocity), 8.0f, 60.0f);
        c.airplane_cam_height   = zeroOr(IniF(path, "airplane", "cam_height", d.airplane_cam_height), 0.3f, 20.0f);
        c.airplane_cam_distance = zeroOr(IniF(path, "airplane", "cam_distance", d.airplane_cam_distance), 0.3f, 20.0f);
        c.airplane_cam_height_fast   = zeroOr(IniF(path, "airplane", "cam_height_fast", d.airplane_cam_height_fast), 0.3f, 20.0f);
        c.airplane_cam_distance_fast = zeroOr(IniF(path, "airplane", "cam_distance_fast", d.airplane_cam_distance_fast), 0.3f, 20.0f);
        c.airplane_abandon_glide  = IniI(path, "airplane", "abandon_glide", d.airplane_abandon_glide);
        c.airplane_bail_parachute = IniI(path, "airplane", "bail_parachute", d.airplane_bail_parachute);
        c.airplane_force_lod1     = IniI(path, "airplane", "force_lod1", d.airplane_force_lod1);
        c.airplane_stall_horn     = IniI(path, "airplane", "stall_horn", d.airplane_stall_horn);
        c.airplane_engine_rpm     = IniI(path, "airplane", "engine_rpm", d.airplane_engine_rpm);
        c.airplane_gear_retract   = IniI(path, "airplane", "gear_retract", d.airplane_gear_retract);
        c.airplane_gear_up_speed  = Clampf(IniF(path, "airplane", "gear_up_top_speed", d.airplane_gear_up_speed), 45.0f, 67.5f);   // the airframe's 45 .. 1.5x
        c.airplane_gear_time      = Clampf(IniF(path, "airplane", "gear_time", d.airplane_gear_time), 0.2f, 10.0f);
        c.airplane_gear_belly_speed = Clampf(IniF(path, "airplane", "gear_belly_speed", d.airplane_gear_belly_speed), 2.0f, 100.0f);
        c.airplane_gear_belly_decel = Clampf(IniF(path, "airplane", "gear_belly_decel", d.airplane_gear_belly_decel), 0.0f, 30.0f);
        c.airplane_gear_auto_down = IniI(path, "airplane", "gear_auto_down", d.airplane_gear_auto_down);
        c.airplane_gear_warn_height = Clampf(IniF(path, "airplane", "gear_warn_height", d.airplane_gear_warn_height), 0.0f, 50.0f);
        c.airplane_gear_visual    = IniI(path, "airplane", "gear_visual", d.airplane_gear_visual);
        c.airplane_props          = IniI(path, "airplane", "props", d.airplane_props);
        c.airplane_skid_fix       = IniI(path, "airplane", "skid_fix", d.airplane_skid_fix) ? 1 : 0;
        c.airplane_prop_idle      = Clampf(IniF(path, "airplane", "prop_idle", d.airplane_prop_idle), 0.0f, 8.0f);
        c.airplane_prop_max       = Clampf(IniF(path, "airplane", "prop_max", d.airplane_prop_max), c.airplane_prop_idle, 8.0f);   // 8: a frame turns it < 50 deg at 60 fps
    }
    c.heli_parachute   = IniI(path, "heli", "parachute", d.heli_parachute);
    c.heli_chute_cut   = IniI(path, "heli", "chute_cut", d.heli_chute_cut);
    c.heli_chute_fall  = Clampf(IniF(path, "heli", "chute_fall_speed", d.heli_chute_fall), 0.0f, 15.0f);

    c.bike_enable        = IniI(path, "bike", "enable", d.bike_enable);
    c.bike_wheelie_speed = Clampf(IniF(path, "bike", "wheelie_speed", d.bike_wheelie_speed), 0.5f, 40.0f);
    c.bike_wheelie_angle = Clampf(IniF(path, "bike", "wheelie_angle", d.bike_wheelie_angle), 5.0f, 80.0f);
    c.bike_stoppie_angle = Clampf(IniF(path, "bike", "stoppie_angle", d.bike_stoppie_angle), 0.0f, 60.0f);
    c.bike_air_flip      = Clampf(IniF(path, "bike", "air_flip", d.bike_air_flip), 0.0f, 40.0f);
    c.bike_air_spin      = Clampf(IniF(path, "bike", "air_spin", d.bike_air_spin), 0.0f, 40.0f);
    c.bike_lean_keys     = IniI(path, "bike", "lean_keys", d.bike_lean_keys);
    c.bike_drive         = IniI(path, "bike", "drive", d.bike_drive);
    c.bike_top_speed     = Clampf(IniF(path, "bike", "top_speed", d.bike_top_speed), 5.0f, 60.0f);
    c.bike_power         = Clampf(IniF(path, "bike", "power", d.bike_power), 0.2f, 4.0f);
    c.bike_grip_g        = Clampf(IniF(path, "bike", "grip", d.bike_grip_g), 0.0f, 3.0f);
    c.bike_brake_slide   = IniI(path, "bike", "brake_slide", d.bike_brake_slide);

    c.jp_enable          = IniI(path, "jetpack", "enable", d.jp_enable);
    c.jp_height          = Clampf(IniF(path, "jetpack", "height", d.jp_height), 0.1f, 20.0f);
    c.jp_time            = Clampf(IniF(path, "jetpack", "time", d.jp_time), 0.2f, 120.0f);
    c.jp_resume          = IniI(path, "jetpack", "resume", d.jp_resume);
    c.jp_keep_ceiling    = IniI(path, "jetpack", "keep_ceiling", d.jp_keep_ceiling);
    c.jp_unlimited       = IniI(path, "jetpack", "unlimited", d.jp_unlimited) ? 1 : 0;
    c.jp_climb_speed     = Clampf(IniF(path, "jetpack", "climb_speed", d.jp_climb_speed), 1.0f, 12.0f);

    c.wanted_enable      = IniI(path, "wanted", "enable", d.wanted_enable);
    c.wanted_debug_level = IniI(path, "wanted", "debug_level", d.wanted_debug_level);
    if (c.wanted_debug_level < 0 || c.wanted_debug_level > 5) c.wanted_debug_level = 0;
    c.wanted_radius      = Clampf(IniF(path, "wanted", "radius", d.wanted_radius), 10.0f, 150.0f);
    c.wanted_speed       = Clampf(IniF(path, "wanted", "chase_speed", d.wanted_speed), 6.5f, 60.0f);
    c.wanted_evade_base  = Clampf(IniF(path, "wanted", "evade_base", d.wanted_evade_base), 1.0f, 120.0f);
    c.wanted_evade_per_level = Clampf(IniF(path, "wanted", "evade_per_level", d.wanted_evade_per_level), 0.0f, 60.0f);
    c.wanted_police_boost   = IniI(path, "wanted", "police_boost", d.wanted_police_boost);
    c.wanted_police_speed   = Clampf(IniF(path, "wanted", "police_top_speed", d.wanted_police_speed), 6.0f, 40.0f);
    c.wanted_police_power   = Clampf(IniF(path, "wanted", "police_power", d.wanted_police_power), 0.2f, 4.0f);
    c.wanted_police_grip    = Clampf(IniF(path, "wanted", "police_grip", d.wanted_police_grip), 0.5f, 3.0f);
    c.wanted_police_catchup = Clampf(IniF(path, "wanted", "police_catchup", d.wanted_police_catchup), 0.0f, 1.0f);
    c.wanted_police_ram     = Clampf(IniF(path, "wanted", "police_ram", d.wanted_police_ram), 1.0f, 3.0f);
    c.wanted_crimes         = IniI(path, "wanted", "crimes", d.wanted_crimes);
    c.wanted_crime_run_over = Clampf(IniF(path, "wanted", "crime_run_over", d.wanted_crime_run_over), 0.0f, 6.0f);
    c.wanted_crime_ram_police = Clampf(IniF(path, "wanted", "crime_ram_police", d.wanted_crime_ram_police), 0.0f, 6.0f);
    c.wanted_crime_wreck    = Clampf(IniF(path, "wanted", "crime_wreck", d.wanted_crime_wreck), 0.0f, 6.0f);
    c.wanted_crime_steal_car = Clampf(IniF(path, "wanted", "crime_steal_car",
                                           IniF(path, "wanted", "crime_robber_car", d.wanted_crime_steal_car)), 0.0f, 6.0f);
    c.wanted_crime_steal_police = Clampf(IniF(path, "wanted", "crime_steal_police", d.wanted_crime_steal_police), 0.0f, 6.0f);
    c.wanted_crime_hit_cop  = Clampf(IniF(path, "wanted", "crime_hit_cop", d.wanted_crime_hit_cop), 0.0f, 6.0f);
    c.wanted_crime_hit_person = Clampf(IniF(path, "wanted", "crime_hit_person", d.wanted_crime_hit_person), 0.0f, 6.0f);
    c.wanted_crime_punch    = Clampf(IniF(path, "wanted", "crime_punch", d.wanted_crime_punch), 0.0f, 6.0f);
    c.wanted_crime_shoot_person = Clampf(IniF(path, "wanted", "crime_shoot_person", d.wanted_crime_shoot_person), 0.0f, 6.0f);
    c.wanted_crime_shoot_prop = Clampf(IniF(path, "wanted", "crime_shoot_prop", d.wanted_crime_shoot_prop), 0.0f, 6.0f);
    c.wanted_level_damp     = Clampf(IniF(path, "wanted", "level_damp", d.wanted_level_damp), 0.0f, 5.0f);
    c.wanted_unseen         = Clampf(IniF(path, "wanted", "unseen", d.wanted_unseen), 0.0f, 1.0f);
    c.wanted_unseen_cap     = Clampf(IniF(path, "wanted", "unseen_cap", d.wanted_unseen_cap), 0.0f, 5.99f);
    c.wanted_gadgets        = IniI(path, "wanted", "gadgets", d.wanted_gadgets);
    c.wanted_stun_s         = Clampf(IniF(path, "wanted", "stun_seconds", d.wanted_stun_s), 0.5f, 15.0f);
    c.wanted_tie_s          = Clampf(IniF(path, "wanted", "tie_seconds", d.wanted_tie_s), 1.5f, 15.0f);
    c.wanted_busted_studs   = IniI(path, "wanted", "busted_studs", d.wanted_busted_studs);
    if (c.wanted_busted_studs < 0) c.wanted_busted_studs = 0;
    c.wanted_busted_station = IniI(path, "wanted", "busted_station", d.wanted_busted_station);
    c.wanted_busted_station_coop = IniI(path, "wanted", "busted_station_coop", d.wanted_busted_station_coop) ? 1 : 0;
    c.wanted_busted_grace   = Clampf(IniF(path, "wanted", "busted_grace", d.wanted_busted_grace), 0.0f, 120.0f);
    // [busted1]
    c.wanted_busted_outside = IniI(path, "wanted", "busted_outside", d.wanted_busted_outside) ? 1 : 0;
    c.wanted_busted_outside_x = Clampf(IniF(path, "wanted", "busted_outside_x", d.wanted_busted_outside_x), -5000.0f, 5000.0f);
    c.wanted_busted_outside_y = Clampf(IniF(path, "wanted", "busted_outside_y", d.wanted_busted_outside_y), -200.0f, 1000.0f);
    c.wanted_busted_outside_z = Clampf(IniF(path, "wanted", "busted_outside_z", d.wanted_busted_outside_z), -5000.0f, 5000.0f);
    c.wanted_busted_outside_heading = Clampf(IniF(path, "wanted", "busted_outside_heading", d.wanted_busted_outside_heading), -360.0f, 360.0f);
    c.wanted_busted_outside_load = Clampf(IniF(path, "wanted", "busted_outside_load", d.wanted_busted_outside_load), 0.5f, 10.0f);
    c.wanted_busted_fade    = Clampf(IniF(path, "wanted", "busted_fade", d.wanted_busted_fade), 0.0f, 3.0f);
    // [station1] [general] station_return = outside | stock (anything else = outside); stored spelled as one of the two,
    // so the Mod Options choice always shows which
    IniS(path, "general", "station_return", "outside", c.station_return, sizeof(c.station_return));
    strcpy_s(c.station_return, sizeof(c.station_return), stn::ParseOutside(c.station_return) ? "outside" : "stock");
    c.wanted_hud            = IniI(path, "wanted", "hud", d.wanted_hud);
    c.wanted_officers       = IniI(path, "wanted", "officers", d.wanted_officers);
    c.wanted_officers_bikes = IniI(path, "wanted", "officers_bikes", d.wanted_officers_bikes);
    c.wanted_officer_get_out = Clampf(IniF(path, "wanted", "officer_get_out_range", d.wanted_officer_get_out), 0.0f, 40.0f);
    c.wanted_officer_join   = Clampf(IniF(path, "wanted", "officer_join_range", d.wanted_officer_join), 0.0f, 60.0f);
    c.wanted_officer_fire   = Clampf(IniF(path, "wanted", "officer_fire_range", d.wanted_officer_fire), 2.0f, 20.0f);
    c.wanted_officer_grapple = Clampf(IniF(path, "wanted", "officer_grapple_rate", d.wanted_officer_grapple), 0.0f, 2.0f);
    c.wanted_officers_fight = IniI(path, "wanted", "officers_fight", d.wanted_officers_fight);
    c.wanted_officer_hits   = IniI(path, "wanted", "officer_hits", d.wanted_officer_hits);
    if (c.wanted_officer_hits < 1 || c.wanted_officer_hits > 20) c.wanted_officer_hits = d.wanted_officer_hits;
    c.wanted_boats          = IniI(path, "wanted", "boats", d.wanted_boats);
    c.wanted_boat_radius    = Clampf(IniF(path, "wanted", "boat_radius", d.wanted_boat_radius), 20.0f, 200.0f);
    c.wanted_boat_linger    = Clampf(IniF(path, "wanted", "boat_linger", d.wanted_boat_linger), 1.0f, 120.0f);
    IniS(path, "wanted", "boat_driver", "NPC_HarbourCop01", c.wanted_boat_driver, sizeof(c.wanted_boat_driver));
    c.wanted_boat_minnow    = IniI(path, "wanted", "boat_minnow", d.wanted_boat_minnow);
    c.wanted_boat_arrest    = IniI(path, "wanted", "boat_arrest", d.wanted_boat_arrest);
    c.wanted_boat_arrest_range = Clampf(IniF(path, "wanted", "boat_arrest_range", d.wanted_boat_arrest_range), 2.0f, 20.0f);
    c.wanted_boat_arrest_s  = Clampf(IniF(path, "wanted", "boat_arrest_seconds", d.wanted_boat_arrest_s), 0.5f, 30.0f);
    c.wanted_heli           = IniI(path, "wanted", "heli", d.wanted_heli);
    c.wanted_flee           = IniI(path, "wanted", "flee", d.wanted_flee) ? 1 : 0;
    c.wanted_flee_level     = IniI(path, "wanted", "flee_level", d.wanted_flee_level);
    c.wanted_flee_level     = c.wanted_flee_level < 1 ? 1 : c.wanted_flee_level > 5 ? 5 : c.wanted_flee_level;
    c.wanted_flee_radius    = Clampf(IniF(path, "wanted", "flee_radius", d.wanted_flee_radius), 5.0f, 80.0f);
    c.wanted_flee_speed     = Clampf(IniF(path, "wanted", "flee_speed", d.wanted_flee_speed), 6.0f, 40.0f);
    IniS(path, "wanted", "heli_model", "Police_Responder", c.wanted_heli_model, sizeof(c.wanted_heli_model));
    if (!c.wanted_heli_model[0]) strcpy_s(c.wanted_heli_model, "Police_Responder");
    c.wanted_heli_level     = IniI(path, "wanted", "heli_level", d.wanted_heli_level);
    if (c.wanted_heli_level < 1 || c.wanted_heli_level > 5) c.wanted_heli_level = d.wanted_heli_level;
    c.wanted_heli_level2    = IniI(path, "wanted", "heli_level2", d.wanted_heli_level2);
    if (c.wanted_heli_level2 < c.wanted_heli_level) c.wanted_heli_level2 = c.wanted_heli_level;
    if (c.wanted_heli_level2 > 6) c.wanted_heli_level2 = 6;
    c.wanted_heli_height4   = Clampf(IniF(path, "wanted", "heli_height4", d.wanted_heli_height4), 6.0f, 60.0f);
    c.wanted_heli_height5   = Clampf(IniF(path, "wanted", "heli_height5", d.wanted_heli_height5), 6.0f, 60.0f);
    c.wanted_heli_radius    = Clampf(IniF(path, "wanted", "heli_radius", d.wanted_heli_radius), 6.0f, 60.0f);
    c.wanted_heli_speed     = Clampf(IniF(path, "wanted", "heli_speed", d.wanted_heli_speed), 10.0f, 80.0f);
    c.wanted_heli_sight     = Clampf(IniF(path, "wanted", "heli_sight", d.wanted_heli_sight), 0.0f, 200.0f);
    c.wanted_heli_hold      = Clampf(IniF(path, "wanted", "heli_hold", d.wanted_heli_hold), 0.0f, 600.0f);
    c.wanted_heli_siren     = IniI(path, "wanted", "heli_siren", d.wanted_heli_siren);
    c.wanted_heli_news      = IniI(path, "wanted", "heli_news", d.wanted_heli_news);
    c.wanted_heli_searchlight = IniI(path, "wanted", "heli_searchlight", d.wanted_heli_searchlight);
    c.wanted_heli_beam_fov  = Clampf(IniF(path, "wanted", "heli_beam_fov", d.wanted_heli_beam_fov), 5.0f, 90.0f);
    c.wanted_heli_beam_range = Clampf(IniF(path, "wanted", "heli_beam_range", d.wanted_heli_beam_range), 10.0f, 150.0f);
    c.wanted_heli_beam_intensity = Clampf(IniF(path, "wanted", "heli_beam_intensity", d.wanted_heli_beam_intensity), 0.1f, 10.0f);
    c.wanted_heli_beam_axis = IniI(path, "wanted", "heli_beam_axis", d.wanted_heli_beam_axis) < 0 ? -1 : 1;
    c.wanted_heli_beam_mesh = IniI(path, "wanted", "heli_beam_mesh", d.wanted_heli_beam_mesh);

    c.life_enable      = IniI(path, "life", "enable", d.life_enable);
    c.life_max_scenes  = IniI(path, "life", "max_scenes", d.life_max_scenes);
    if (c.life_max_scenes < 0) c.life_max_scenes = 0;
    c.life_max_people  = IniI(path, "life", "max_people", d.life_max_people);
    if (c.life_max_people < 0) c.life_max_people = 0;
    if (c.life_max_people > 24) c.life_max_people = 24;
    c.life_every       = Clampf(IniF(path, "life", "every", d.life_every), 2.0f, 600.0f);
    c.life_fights      = Clampf(IniF(path, "life", "fights", d.life_fights), 0.0f, 10.0f);
    c.life_radius      = Clampf(IniF(path, "life", "radius", d.life_radius), 10.0f, 80.0f);
    c.life_min_radius  = Clampf(IniF(path, "life", "min_radius", d.life_min_radius), 0.0f, c.life_radius - 5.0f);
    c.life_reactions   = IniI(path, "life", "crowd_reactions", d.life_reactions);
    c.life_crowd_chats = IniI(path, "life", "crowd_chats", d.life_crowd_chats);
    c.life_crowd_chat_every = Clampf(IniF(path, "life", "crowd_chat_every", d.life_crowd_chat_every), 1.0f, 600.0f);
    c.life_party_hotkey = IniI(path, "life", "party_hotkey", d.life_party_hotkey) ? 1 : 0;
    {
        char pb[64];
        GetPrivateProfileStringA("life", "party_pad", "LS+RS", pb, sizeof(pb), path);
        for (char* q = pb; *q; ++q) if (*q == ';' || *q == '#') { *q = 0; break; }
        c.life_party_pad = static_cast<int>(ParsePadChord(pb));
    }
    c.life_party_radius = Clampf(IniF(path, "life", "party_radius", d.life_party_radius), 5.0f, 80.0f);
    c.life_party_every  = Clampf(IniF(path, "life", "party_every", d.life_party_every), 0.0f, 36000.0f);

    c.radio_enable        = IniI(path, "radio", "enable", d.radio_enable);
    c.radio_volume        = Clampf(IniF(path, "radio", "volume", d.radio_volume), 0.0f, 1.5f);
    c.radio_in_heli       = IniI(path, "radio", "in_heli", d.radio_in_heli);
    c.radio_duck_db       = Clampf(IniF(path, "radio", "talkover_duck_db", d.radio_duck_db), -40.0f, 0.0f);
    c.radio_static_db     = Clampf(IniF(path, "radio", "static_db", d.radio_static_db), -60.0f, 0.0f);
    c.radio_start_station = IniI(path, "radio", "start_station", d.radio_start_station);
    c.radio_game_music    = Clampf(IniF(path, "radio", "game_music", d.radio_game_music), 0.0f, 1.0f);
    c.radio_pad_block     = IniI(path, "radio", "pad_block", d.radio_pad_block);
    IniS(path, "radio", "breaking_news", "brick-talk, bright-lights", c.radio_breaking, sizeof(c.radio_breaking));
    c.radio_breaking_gap  = Clampf(IniF(path, "radio", "breaking_news_gap", d.radio_breaking_gap), 30.0f, 3600.0f);
    c.radio_mission_news  = IniI(path, "radio", "mission_news", d.radio_mission_news) ? 1 : 0;              // [missions1]
    c.radio_mission_interrupt = IniI(path, "radio", "mission_news_interrupt", d.radio_mission_interrupt) ? 1 : 0;
    IniS(path, "radio", "mission_news_stations", "all", c.radio_mission_stations, sizeof(c.radio_mission_stations));
    c.radio_mission_keep_h = Clampf(IniF(path, "radio", "mission_news_keep_hours", d.radio_mission_keep_h), 0.1f, 720.0f);
    c.radio_mission_recap_chance = Clampf(IniF(path, "radio", "mission_news_recap_chance", d.radio_mission_recap_chance), 0.0f, 1.0f);
    c.radio_mission_recap_max = IniI(path, "radio", "mission_news_recap_max", d.radio_mission_recap_max);
    if (c.radio_mission_recap_max < 0) c.radio_mission_recap_max = 0;
    if (c.radio_mission_recap_max > 20) c.radio_mission_recap_max = 20;
    c.radio_mission_recap_h = Clampf(IniF(path, "radio", "mission_news_recap_hours", d.radio_mission_recap_h), 0.0f, 720.0f);
    c.radio_mission_recap_gap = Clampf(IniF(path, "radio", "mission_news_recap_gap", d.radio_mission_recap_gap), 1.0f, 1440.0f);
    c.radio_mission_recap_reuse = IniI(path, "radio", "mission_news_recap_reuse", d.radio_mission_recap_reuse) ? 1 : 0;
    c.radio_mission_once = IniI(path, "radio", "mission_news_once", d.radio_mission_once) ? 1 : 0;
    {
        char buf[32];
        IniS(path, "radio", "pad_next", "DRIGHT", buf, sizeof(buf));
        c.radio_pad_next = static_cast<int>(pad::Parse(buf));
        IniS(path, "radio", "pad_prev", "DLEFT", buf, sizeof(buf));
        c.radio_pad_prev = static_cast<int>(pad::Parse(buf));
    }
    IniS(path, "radio", "manifest", "..\\Radio\\radio_runtime.tsv", c.radio_manifest, sizeof(c.radio_manifest));
    IniS(path, "radio", "root", "", c.radio_root, sizeof(c.radio_root));
    IniText(path, "radio", "music", "ai", c.radio_music, sizeof(c.radio_music));
    LoadRadioStations(path, c);
    IniText(path, "radio", "library_folders", "", c.radio_lib_folders, sizeof(c.radio_lib_folders));
    c.radio_lib_min = IniI(path, "radio", "library_min_tracks", d.radio_lib_min);
    if (c.radio_lib_min < 1) c.radio_lib_min = 1;
    if (c.radio_lib_min > 1000) c.radio_lib_min = 1000;
    IniText(path, "radio", "library_short", "talk", c.radio_lib_short, sizeof(c.radio_lib_short));
    IniText(path, "radio", "library_skip", d.radio_lib_skip, c.radio_lib_skip, sizeof(c.radio_lib_skip));
    IniText(path, "radio", "library_holiday", "season", c.radio_lib_holiday, sizeof(c.radio_lib_holiday));
    IniText(path, "radio", "library_unsorted", "skip", c.radio_lib_unsorted, sizeof(c.radio_lib_unsorted));
    c.radio_lib_talkover = IniI(path, "radio", "library_talkover", d.radio_lib_talkover) ? 1 : 0;
    IniText(path, "radio", "library_cache", d.radio_lib_cache, c.radio_lib_cache, sizeof(c.radio_lib_cache));
    IniText(path, "radio", "service_app", "auto", c.radio_svc_app, sizeof(c.radio_svc_app));
    if (!c.radio_svc_app[0]) strcpy_s(c.radio_svc_app, "auto");
    c.radio_svc_volume    = Clampf(IniF(path, "radio", "service_volume", d.radio_svc_volume), 0.0f, 1.0f);   // x its own: Windows caps at 1
    c.radio_svc_volctl    = IniI(path, "radio", "service_volume_control", d.radio_svc_volctl) ? 1 : 0;
    c.radio_svc_lead      = Clampf(IniF(path, "radio", "service_pause_lead", d.radio_svc_lead), 0.0f, 3.0f);
    c.radio_svc_max_min   = IniI(path, "radio", "service_max_track_min", d.radio_svc_max_min);
    if (c.radio_svc_max_min < 0) c.radio_svc_max_min = 0;
    if (c.radio_svc_max_min > 240) c.radio_svc_max_min = 240;
    c.radio_svc_open_tune = IniI(path, "radio", "service_open_tune", d.radio_svc_open_tune) ? 1 : 0;
    {   // [radiologo1]
        c.radio_logo          = IniI(path, "radio", "logo", d.radio_logo) ? 1 : 0;
        c.radio_logo_seconds  = Clampf(IniF(path, "radio", "logo_seconds", d.radio_logo_seconds), 3.0f, 20.0f);
        char lc[32] = {};
        IniText(path, "radio", "logo_corner", "0", lc, sizeof(lc));
        c.radio_logo_corner   = rlm::CornerFromText(lc, 0);
        c.radio_logo_scale    = Clampf(IniF(path, "radio", "logo_scale", d.radio_logo_scale), 0.4f, 2.5f);
        c.radio_logo_on_enter = IniI(path, "radio", "logo_on_enter", d.radio_logo_on_enter) ? 1 : 0;
    }

    c.menu_enable      = IniI(path, "menu", "enable", d.menu_enable) ? 1 : 0;
    c.world_safe_sea   = IniI(path, "world", "safe_sea", d.world_safe_sea) ? 1 : 0;
    c.world_safe_sea_x = Clampf(IniF(path, "world", "safe_sea_x", d.world_safe_sea_x), -500.0f, 2000.0f);
    c.shark_enable         = IniI(path, "shark", "enable", d.shark_enable) ? 1 : 0;
    c.shark_classic        = IniI(path, "shark", "classic", d.shark_classic) ? 1 : 0;
    c.shark_grace_s        = Clampf(IniF(path, "shark", "grace_s", d.shark_grace_s), 0.0f, 3600.0f);
    c.shark_chance_per_min = Clampf(IniF(path, "shark", "chance_per_min", d.shark_chance_per_min), 0.0f, 1.0f);
    c.shark_ramp           = Clampf(IniF(path, "shark", "ramp", d.shark_ramp), 0.0f, 10.0f);
    c.shark_max_per_min    = Clampf(IniF(path, "shark", "max_per_min", d.shark_max_per_min), 0.0f, 1.0f);
    c.shark_warn_s         = Clampf(IniF(path, "shark", "warn", d.shark_warn_s), 0.0f, 30.0f);
    c.shark_guard_s        = Clampf(IniF(path, "shark", "guard_s", d.shark_guard_s), 0.0f, 120.0f);
    c.voice_enable         = IniI(path, "voice", "enable", d.voice_enable) ? 1 : 0;
    c.voice_shark          = IniI(path, "voice", "shark", d.voice_shark) ? 1 : 0;
    c.voice_order_car      = IniI(path, "voice", "order_car", d.voice_order_car) ? 1 : 0;
    c.voice_phone          = IniI(path, "voice", "phone", d.voice_phone) ? 1 : 0;
    c.voice_swim           = IniI(path, "voice", "swim", d.voice_swim) ? 1 : 0;
    c.voice_ped_thanks     = IniI(path, "voice", "ped_thanks", d.voice_ped_thanks) ? 1 : 0;
    c.voice_ped_nothanks   = IniI(path, "voice", "ped_nothanks", d.voice_ped_nothanks) ? 1 : 0;
    c.voice_ped_witness    = IniI(path, "voice", "ped_witness", d.voice_ped_witness) ? 1 : 0;
    c.voice_once           = IniI(path, "voice", "once", d.voice_once) ? 1 : 0;
    c.voice_chase_cooldown = Clampf(IniF(path, "voice", "chase_cooldown", d.voice_chase_cooldown), 10.0f, 36000.0f);
    c.voice_phone_cooldown = Clampf(IniF(path, "voice", "phone_cooldown", d.voice_phone_cooldown), 5.0f, 3600.0f);
    c.voice_ped_gap        = Clampf(IniF(path, "voice", "ped_gap", d.voice_ped_gap), 0.0f, 3600.0f);
    c.voice_ped_hear       = Clampf(IniF(path, "voice", "ped_hear", d.voice_ped_hear), 2.0f, 100.0f);
    c.voice_falloff        = Clampf(IniF(path, "voice", "falloff", d.voice_falloff), 4.0f, 200.0f);
    c.dino_enable      = IniI(path, "dino", "enable", d.dino_enable) ? 1 : 0;
    c.dino_count       = IniI(path, "dino", "count", d.dino_count);
    c.dino_count       = c.dino_count < 0 ? 0 : c.dino_count > 4 ? 4 : c.dino_count;
    IniS(path, "dino", "model", d.dino_model, c.dino_model, sizeof(c.dino_model));
    c.dino_x           = Clampf(IniF(path, "dino", "x", d.dino_x), -2000.0f, 2000.0f);
    c.dino_y           = Clampf(IniF(path, "dino", "y", d.dino_y), -100.0f, 500.0f);
    c.dino_z           = Clampf(IniF(path, "dino", "z", d.dino_z), -2000.0f, 2000.0f);
    c.dino_step_x      = Clampf(IniF(path, "dino", "step_x", d.dino_step_x), -50.0f, 50.0f);
    c.dino_step_z      = Clampf(IniF(path, "dino", "step_z", d.dino_step_z), -50.0f, 50.0f);
    c.dino_heading     = Clampf(IniF(path, "dino", "heading", d.dino_heading), -360.0f, 360.0f);
    c.dino_near        = Clampf(IniF(path, "dino", "near", d.dino_near), 20.0f, 200.0f);
    c.dino_far         = Clampf(IniF(path, "dino", "far", d.dino_far), c.dino_near + 20.0f, 600.0f);
    c.dino_leave       = Clampf(IniF(path, "dino", "leave", d.dino_leave), 20.0f, 300.0f);
    c.dino_invulnerable = IniI(path, "dino", "invulnerable", d.dino_invulnerable) ? 1 : 0;
    c.dino2_count      = IniI(path, "dino", "count2", d.dino2_count);
    c.dino2_count      = c.dino2_count < 0 ? 0 : c.dino2_count > 4 ? 4 : c.dino2_count;
    IniS(path, "dino", "model2", d.dino2_model, c.dino2_model, sizeof(c.dino2_model));
    c.dino2_x          = Clampf(IniF(path, "dino", "x2", d.dino2_x), -2000.0f, 2000.0f);
    c.dino2_y          = Clampf(IniF(path, "dino", "y2", d.dino2_y), -100.0f, 500.0f);
    c.dino2_z          = Clampf(IniF(path, "dino", "z2", d.dino2_z), -2000.0f, 2000.0f);
    c.dino2_step_x     = Clampf(IniF(path, "dino", "step_x2", d.dino2_step_x), -50.0f, 50.0f);
    c.dino2_step_z     = Clampf(IniF(path, "dino", "step_z2", d.dino2_step_z), -50.0f, 50.0f);
    c.dino2_heading    = Clampf(IniF(path, "dino", "heading2", d.dino2_heading), -360.0f, 360.0f);
    c.ptero_enable     = IniI(path, "pteranodon", "enable", d.ptero_enable) ? 1 : 0;
    IniS(path, "pteranodon", "creature", d.ptero_creature, c.ptero_creature, sizeof(c.ptero_creature));
    if (!c.ptero_creature[0]) strcpy_s(c.ptero_creature, d.ptero_creature);
    c.ptero_stall      = Clampf(IniF(path, "pteranodon", "stall", d.ptero_stall), 1.0f, 30.0f);
    c.ptero_cruise     = Clampf(IniF(path, "pteranodon", "cruise", d.ptero_cruise), c.ptero_stall + 1.0f, 60.0f);
    c.ptero_max_speed  = Clampf(IniF(path, "pteranodon", "max_speed", d.ptero_max_speed), c.ptero_cruise, 80.0f);
    c.ptero_glide      = Clampf(IniF(path, "pteranodon", "glide", d.ptero_glide), 1.0f, 30.0f);
    c.ptero_climb      = Clampf(IniF(path, "pteranodon", "climb", d.ptero_climb), 0.0f, 60.0f);
    c.ptero_dive       = Clampf(IniF(path, "pteranodon", "dive", d.ptero_dive), 5.0f, 80.0f);
    c.ptero_flap_lift  = Clampf(IniF(path, "pteranodon", "flap_lift", d.ptero_flap_lift), 0.0f, 15.0f);
    c.ptero_flap_rate  = Clampf(IniF(path, "pteranodon", "flap_rate", d.ptero_flap_rate), 0.2f, 3.0f);
    c.ptero_flap_climb = Clampf(IniF(path, "pteranodon", "flap_climb", d.ptero_flap_climb), 0.0f, 45.0f);
    c.ptero_takeoff_up = Clampf(IniF(path, "pteranodon", "takeoff_up", d.ptero_takeoff_up), 1.0f, 20.0f);
    c.ptero_bank       = Clampf(IniF(path, "pteranodon", "bank", d.ptero_bank), 5.0f, 80.0f);
    c.ptero_turn       = Clampf(IniF(path, "pteranodon", "turn", d.ptero_turn), 10.0f, 360.0f);
    c.ptero_ceiling    = Clampf(IniF(path, "pteranodon", "ceiling", d.ptero_ceiling), 10.0f, 5000.0f);
    c.ptero_sea_y      = Clampf(IniF(path, "pteranodon", "sea_y", d.ptero_sea_y), -9999.0f, 100.0f);
    c.ptero_skim       = Clampf(IniF(path, "pteranodon", "skim", d.ptero_skim), 0.1f, 5.0f);
    c.ptero_tilt       = Clampf(IniF(path, "pteranodon", "tilt", d.ptero_tilt), 0.0f, 1.0f);
    c.ptero_tilt_pitch = Clampf(IniF(path, "pteranodon", "tilt_pitch", d.ptero_tilt_pitch), 0.0f, 60.0f);
    c.ptero_pitch_sign = IniI(path, "pteranodon", "pitch_sign", d.ptero_pitch_sign) < 0 ? -1 : 1;
    c.ptero_auto_glide = Clampf(IniF(path, "pteranodon", "auto_glide", d.ptero_auto_glide), 0.0f, 30.0f);
    c.ptero_chute      = IniI(path, "pteranodon", "chute", d.ptero_chute) ? 1 : 0;
    c.ptero_anims      = IniI(path, "pteranodon", "anims", d.ptero_anims) ? 1 : 0;
    c.ptero_coast      = Clampf(IniF(path, "pteranodon", "coast", d.ptero_coast), 0.0f, 300.0f);
    c.ptero_bounds     = IniI(path, "pteranodon", "bounds", d.ptero_bounds) ? 1 : 0;
    c.ptero_min_x      = Clampf(IniF(path, "pteranodon", "min_x", d.ptero_min_x), -5000.0f, 5000.0f);
    c.ptero_max_x      = Clampf(IniF(path, "pteranodon", "max_x", d.ptero_max_x), c.ptero_min_x + 50.0f, 5000.0f);
    c.ptero_min_z      = Clampf(IniF(path, "pteranodon", "min_z", d.ptero_min_z), -5000.0f, 5000.0f);
    c.ptero_max_z      = Clampf(IniF(path, "pteranodon", "max_z", d.ptero_max_z), c.ptero_min_z + 50.0f, 5000.0f);
    c.sites_enable     = IniI(path, "sites", "enable", d.sites_enable) ? 1 : 0;
    c.sites_near       = Clampf(IniF(path, "sites", "near", d.sites_near), 20.0f, 200.0f);
    c.sites_far        = Clampf(IniF(path, "sites", "far", d.sites_far), c.sites_near + 20.0f, 600.0f);
    c.sites_leave      = Clampf(IniF(path, "sites", "leave", d.sites_leave), 20.0f, 600.0f);
    c.sites_park       = IniI(path, "sites", "park", d.sites_park);
    c.sites_park       = c.sites_park < 0 ? 0 : c.sites_park > 2 ? 2 : c.sites_park;
    c.sites_heli_settle = IniI(path, "sites", "heli_settle", d.sites_heli_settle) ? 1 : 0;
    IniS(path, "sites", "pin", d.sites_pin, c.sites_pin, sizeof(c.sites_pin));
    {
        int nStands = 0;
        for (int i = 0; i < sitem::kMaxStands; ++i)
        {
            char key[16], val[200], err[80];
            _snprintf_s(key, sizeof(key), _TRUNCATE, "stand%d", i + 1);
            IniS(path, "sites", key, SitesDefault(i), val, sizeof(val));
            const int r = sitem::ParseStand(val, c.sites_stand[i], err, sizeof(err));
            if (r == 2) Log("config: [sites] %s = \"%s\" is not a stand (%s) - model, kind, x, y, z, heading\n", key, val, err);
            else if (r == 0)
            {
                ++nStands;
                const sitem::Stand& s = c.sites_stand[i];
                Log("config: sites %s = %s (%s) at (%.1f, %.2f, %.1f) heading %.0f%s%s\n", key, s.model, sitem::KindName(s.kind),
                    s.pos[0], s.pos[1], s.pos[2], s.heading, s.label[0] ? " - " : "", s.label);
            }
        }
        c.sites_atlas    = IniI(path, "sites", "atlas", d.sites_atlas) ? 1 : 0;
        c.sites_atlas_x0 = Clampf(IniF(path, "sites", "atlas_x0", d.sites_atlas_x0), -5000.0f, 5000.0f);
        c.sites_atlas_x1 = Clampf(IniF(path, "sites", "atlas_x1", d.sites_atlas_x1), -5000.0f, 5000.0f);
        c.sites_atlas_z0 = Clampf(IniF(path, "sites", "atlas_z0", d.sites_atlas_z0), -5000.0f, 5000.0f);
        c.sites_atlas_z1 = Clampf(IniF(path, "sites", "atlas_z1", d.sites_atlas_z1), -5000.0f, 5000.0f);
        Log("config: sites enable=%d stands=%d near=%.0f far=%.0f leave=%.0f park=%d heli_settle=%d pin=%s | Port Atlas %s x %.0f..%.0f "
            "z %.0f..%.0f\n", c.sites_enable, nStands, c.sites_near, c.sites_far, c.sites_leave, c.sites_park, c.sites_heli_settle,
            c.sites_pin[0] ? c.sites_pin : "(none)", c.sites_atlas ? "on" : "off", c.sites_atlas_x0, c.sites_atlas_x1,
            c.sites_atlas_z0, c.sites_atlas_z1);
    }
    // [spots]: the data first (lcudrive_spots.txt beside the ini, else the built-in table), then its parked cars into
    // the stands 13..24 of [sites]
    c.spots_enable     = IniI(path, "spots", "enable", d.spots_enable) ? 1 : 0;
    c.spots_people     = IniI(path, "spots", "people", d.spots_people) ? 1 : 0;
    c.spots_max_people = IniI(path, "spots", "max_people", d.spots_max_people);
    c.spots_max_people = c.spots_max_people < 0 ? 0 : c.spots_max_people > spm::kMaxPeople ? spm::kMaxPeople : c.spots_max_people;
    c.spots_per_spot   = IniI(path, "spots", "per_spot", d.spots_per_spot);
    c.spots_per_spot   = c.spots_per_spot < 0 ? 0 : c.spots_per_spot > 12 ? 12 : c.spots_per_spot;
    c.spots_near       = Clampf(IniF(path, "spots", "near", d.spots_near), 15.0f, 200.0f);
    c.spots_far        = Clampf(IniF(path, "spots", "far", d.spots_far), c.spots_near + 10.0f, 400.0f);
    c.spots_min_spawn  = Clampf(IniF(path, "spots", "min_spawn", d.spots_min_spawn), 3.0f, c.spots_near - 5.0f);
    c.spots_walk       = Clampf(IniF(path, "spots", "walk", d.spots_walk), 0.0f, 1.0f);
    c.spots_walk_speed = Clampf(IniF(path, "spots", "walk_speed", d.spots_walk_speed), 0.3f, 2.0f);
    c.spots_night      = Clampf(IniF(path, "spots", "night", d.spots_night), 0.0f, 1.0f);
    c.spots_invulnerable = IniI(path, "spots", "invulnerable", d.spots_invulnerable) ? 1 : 0;
    c.spots_cars       = IniI(path, "spots", "cars", d.spots_cars) ? 1 : 0;
    c.spots_drive      = IniI(path, "spots", "drive", d.spots_drive);
    c.spots_drive      = c.spots_drive < 0 ? 0 : c.spots_drive > spm::kMaxRoutes ? spm::kMaxRoutes : c.spots_drive;
    c.spots_drive_speed = Clampf(IniF(path, "spots", "drive_speed", d.spots_drive_speed), 0.3f, 2.5f);
    c.spots_log        = IniI(path, "spots", "log", d.spots_log) ? 1 : 0;
    IniS(path, "spots", "file", d.spots_file, c.spots_file, sizeof(c.spots_file));
    {
        const char* text = spt_table::kText;
        const char* from = "the built-in table";
        static char fileBuf[65536];                                      // (hotkey thread only; parsed at once)  [round3] was 32768
        char fpath[MAX_PATH] = {};
        bool nameOk = c.spots_file[0] != 0;
        for (const char* q = c.spots_file; *q; ++q) if (*q == '\\' || *q == '/' || *q == ':') nameOk = false;   // a bare file name only
        if (nameOk)
        {
            strncpy_s(fpath, path, _TRUNCATE);
            char* slash = strrchr(fpath, '\\');
            if (slash) slash[1] = 0; else fpath[0] = 0;
            strncat_s(fpath, c.spots_file, _TRUNCATE);
            FILE* f = nullptr;
            if (fopen_s(&f, fpath, "rb") == 0 && f)
            {
                const size_t n = fread(fileBuf, 1, sizeof(fileBuf) - 1, f);
                fclose(f);
                fileBuf[n] = 0;
                if (n > 0) { text = fileBuf; from = c.spots_file; }
            }
        }
        int taken = spm::Parse(text, c.spots_data);
        int which = -1;
        const char* wrong = spm::Validate(c.spots_data, &which);
        if (wrong && text != spt_table::kText)
        {
            Log("config: [spots] %s is not usable (%s, record %d) - the built-in table is used instead\n", from, wrong, which);
            from = "the built-in table";
            text = spt_table::kText;
            taken = spm::Parse(text, c.spots_data);
            wrong = spm::Validate(c.spots_data, &which);
        }
        if (wrong) { Log("config: [spots] the data is not usable (%s) - no life spots\n", wrong); memset(&c.spots_data, 0, sizeof(c.spots_data)); }
        if (c.spots_data.bad)
            Log("config: [spots] %d line(s) of %s were refused; the first is line %d: %s\n", c.spots_data.bad, from,
                c.spots_data.firstBadLine, c.spots_data.firstBad);
        uint32_t h = 2166136261u;                                        // the data's stamp (FNV-1a over the text in force)
        if (!wrong) for (const char* q = text; *q; ++q) { h ^= static_cast<unsigned char>(*q); h *= 16777619u; }
        c.spots_stamp = h ? h : 1;
        // (always filled: sites.h leaves these slots empty unless [spots] enable and cars are on - one ini key, one member)
        for (int i = 0; i < sitem::kMaxExtra; ++i)
        {
            sitem::Stand& st = c.sites_stand[sitem::kMaxStands + i];
            if (i < c.spots_data.nCars && i < spm::kMaxCars) st = c.spots_data.cars[i];
            else st = sitem::Stand{};
        }
        Log("config: spots enable=%d people=%d max_people=%d per_spot=%d near=%.0f far=%.0f min_spawn=%.0f walk=%.2f night=%.2f "
            "invulnerable=%d cars=%d drive=%d drive_speed=%.2f | %s: %d records - %d spots, %d points, %d parked cars, %d routes "
            "(%d waypoints)\n", c.spots_enable, c.spots_people, c.spots_max_people, c.spots_per_spot, c.spots_near, c.spots_far,
            c.spots_min_spawn, c.spots_walk, c.spots_night, c.spots_invulnerable, c.spots_cars, c.spots_drive, c.spots_drive_speed,
            from, taken, c.spots_data.nSpots, c.spots_data.nPts, c.spots_data.nCars, c.spots_data.nRoutes, c.spots_data.nWps);
    }
    // [diycars1] [diycars]: the garage row -> the [sites] slots after the life-spot cars
    {
        c.diy_enable  = IniI(path, "diycars", "enable", d.diy_enable) ? 1 : 0;
        c.diy_spacing = Clampf(IniF(path, "diycars", "spacing", d.diy_spacing), 4.0f, 20.0f);
        IniS(path, "diycars", "garage", d.diy_garage, c.diy_garage, sizeof(c.diy_garage));
        IniS(path, "diycars", "cars", d.diy_cars, c.diy_cars, sizeof(c.diy_cars));
        c.diy_first = sitem::kMaxSlots; c.diy_count = 0;
        diym::Garage g; char err[96] = {};
        const int r = diym::ParseGarage(c.diy_garage, g, err, sizeof(err));
        if (r == 2) Log("config: [diycars] garage = \"%s\" is not a garage (%s) - x, y, z, heading [, label]\n", c.diy_garage, err);
        char names[diym::kMaxCars][48] = {}; int bad = 0;
        const int n = diym::ParseCars(c.diy_cars, names, diym::kMaxCars, &bad);
        if (bad) Log("config: [diycars] cars: %d name(s) refused (not a model name, or more than %d)\n", bad, diym::kMaxCars);
        int first = sitem::kMaxStands + (c.spots_data.nCars < spm::kMaxCars ? c.spots_data.nCars : spm::kMaxCars);
        if (first > sitem::kMaxSlots) first = sitem::kMaxSlots;
        if (c.diy_enable && r == 0 && n > 0 && first < sitem::kMaxSlots)
        {
            c.diy_count = diym::Layout(g, names, n, c.diy_spacing, &c.sites_stand[first], sitem::kMaxSlots - first);
            c.diy_first = first;
        }
        Log("config: diycars enable=%d garage=%s cars=%d -> [sites] slots %d..%d (spacing %.1f)\n", c.diy_enable,
            r == 0 ? (g.label[0] ? g.label : "on") : "none", n, c.diy_first + 1, c.diy_first + c.diy_count, c.diy_spacing);
    }
    // [round3] [animals]: the herd data (lcudrive_animals.txt beside the ini; missing = none)
    c.animals_enable    = IniI(path, "animals", "enable", d.animals_enable) ? 1 : 0;
    c.animals_max       = IniI(path, "animals", "max", d.animals_max);
    c.animals_max       = c.animals_max < 0 ? 0 : c.animals_max > anm::kMaxAnimals ? anm::kMaxAnimals : c.animals_max;
    c.animals_per_herd  = IniI(path, "animals", "per_herd", d.animals_per_herd);
    c.animals_per_herd  = c.animals_per_herd < 1 ? 1 : c.animals_per_herd > 12 ? 12 : c.animals_per_herd;
    c.animals_near      = Clampf(IniF(path, "animals", "near", d.animals_near), 30.0f, 200.0f);
    c.animals_min_spawn = Clampf(IniF(path, "animals", "min_spawn", d.animals_min_spawn), 5.0f, c.animals_near - 10.0f);
    c.animals_far       = Clampf(IniF(path, "animals", "far", d.animals_far), c.animals_near + 10.0f, 400.0f);
    c.animals_shy       = Clampf(IniF(path, "animals", "shy", d.animals_shy), 0.0f, 3.0f);
    c.animals_respawn   = Clampf(IniF(path, "animals", "respawn", d.animals_respawn), 10.0f, 600.0f);
    c.animals_invulnerable = IniI(path, "animals", "invulnerable", d.animals_invulnerable) ? 1 : 0;
    IniS(path, "animals", "file", d.animals_file, c.animals_file, sizeof(c.animals_file));
    {
        static char aBuf[32768];                                        // (hotkey thread only; parsed at once)
        aBuf[0] = 0;
        const char* from = "(no file)";
        bool nameOk = c.animals_file[0] != 0;
        for (const char* q = c.animals_file; *q; ++q) if (*q == '\\' || *q == '/' || *q == ':') nameOk = false;
        if (nameOk)
        {
            char fpath[MAX_PATH] = {};
            strncpy_s(fpath, path, _TRUNCATE);
            char* slash = strrchr(fpath, '\\');
            if (slash) slash[1] = 0; else fpath[0] = 0;
            strncat_s(fpath, c.animals_file, _TRUNCATE);
            FILE* f = nullptr;
            if (fopen_s(&f, fpath, "rb") == 0 && f)
            {
                const size_t n = fread(aBuf, 1, sizeof(aBuf) - 1, f);
                fclose(f);
                aBuf[n] = 0;
                from = c.animals_file;
            }
        }
        const int taken = anm::Parse(aBuf, c.animals_data);
        if (c.animals_data.bad)
            Log("config: [animals] %d line(s) of %s were refused; the first is line %d: %s\n", c.animals_data.bad, from,
                c.animals_data.firstBadLine, c.animals_data.firstBad);
        if (c.animals_data.skipped)
            Log("config: [animals] %d herd(s) skipped - no character of that type in the game (the first: %s)\n",
                c.animals_data.skipped, c.animals_data.firstSkipped);
        uint32_t h = 2166136261u;
        for (const char* q = aBuf; *q; ++q) { h ^= static_cast<unsigned char>(*q); h *= 16777619u; }
        c.animals_stamp = h ? h : 1;
        Log("config: animals enable=%d max=%d per_herd=%d near=%.0f min_spawn=%.0f far=%.0f shy=%.2f respawn=%.0f invulnerable=%d"
            " | %s: %d herds\n", c.animals_enable, c.animals_max, c.animals_per_herd, c.animals_near, c.animals_min_spawn,
            c.animals_far, c.animals_shy, c.animals_respawn, c.animals_invulnerable, from, taken);
    }
    // [round3] [npc]
    c.npc_enable     = IniI(path, "npc", "enable", d.npc_enable) ? 1 : 0;
    c.npc_voices     = IniI(path, "npc", "voices", d.npc_voices) ? 1 : 0;
    c.npc_reactions  = IniI(path, "npc", "reactions", d.npc_reactions) ? 1 : 0;
    c.npc_greetings  = IniI(path, "npc", "greetings", d.npc_greetings) ? 1 : 0;
    c.npc_smalltalk  = IniI(path, "npc", "smalltalk", d.npc_smalltalk) ? 1 : 0;
    c.npc_subtitles  = IniI(path, "npc", "subtitles", d.npc_subtitles) ? 1 : 0;
    c.npc_per_hour   = IniI(path, "npc", "per_hour", d.npc_per_hour);
    c.npc_per_hour   = c.npc_per_hour < 0 ? 0 : c.npc_per_hour > 600 ? 600 : c.npc_per_hour;
    c.npc_hear       = Clampf(IniF(path, "npc", "hear", d.npc_hear), 6.0f, 60.0f);
    c.npc_chat_every = Clampf(IniF(path, "npc", "chat_every", d.npc_chat_every), 5.0f, 600.0f);
    c.npc_max_loaded = IniI(path, "npc", "max_loaded", d.npc_max_loaded);
    c.npc_max_loaded = c.npc_max_loaded < 1 ? 1 : c.npc_max_loaded > 12 ? 12 : c.npc_max_loaded;
    {
        char r[16] = {};
        IniS(path, "npc", "route", "auto", r, sizeof(r));
        c.npc_route = _stricmp(r, "game") == 0 ? 0 : _stricmp(r, "xaudio") == 0 ? 1 : 2;
    }
    c.npc_volume     = Clampf(IniF(path, "npc", "volume", d.npc_volume), 0.0f, 2.0f);
    c.npc_life       = IniI(path, "npc", "life", d.npc_life) ? 1 : 0;
    c.npc_spots      = IniI(path, "npc", "spots", d.npc_spots) ? 1 : 0;
    IniS(path, "npc", "table", d.npc_table, c.npc_table, sizeof(c.npc_table));
    IniS(path, "npc", "vo_dir", d.npc_vo_dir, c.npc_vo_dir, sizeof(c.npc_vo_dir));
    Log("config: npc enable=%d voices=%d reactions=%d greetings=%d smalltalk=%d subtitles=%d per_hour=%d hear=%.0f chat_every=%.0f "
        "max_loaded=%d route=%s volume=%.2f life=%d spots=%d table=%s vo_dir=%s\n", c.npc_enable, c.npc_voices, c.npc_reactions,
        c.npc_greetings, c.npc_smalltalk, c.npc_subtitles, c.npc_per_hour, c.npc_hear, c.npc_chat_every, c.npc_max_loaded,
        c.npc_route == 0 ? "game" : c.npc_route == 1 ? "xaudio" : "auto", c.npc_volume, c.npc_life, c.npc_spots, c.npc_table, c.npc_vo_dir);
    // [backrooms1] [backrooms]
    c.backrooms_enable    = IniI(path, "backrooms", "enable", d.backrooms_enable) ? 1 : 0;
    c.backrooms_statues   = IniI(path, "backrooms", "statues", d.backrooms_statues) ? 1 : 0;
    c.backrooms_colossi   = IniI(path, "backrooms", "colossi", d.backrooms_colossi) ? 1 : 0;
    c.backrooms_goal_studs = IniI(path, "backrooms", "goal_studs", d.backrooms_goal_studs);          // [br4]
    if (c.backrooms_goal_studs < 0) c.backrooms_goal_studs = 0; if (c.backrooms_goal_studs > 10000000) c.backrooms_goal_studs = 10000000;
    c.backrooms_vault_studs = IniI(path, "backrooms", "vault_studs", d.backrooms_vault_studs);       // [br5]
    if (c.backrooms_vault_studs < 0) c.backrooms_vault_studs = 0; if (c.backrooms_vault_studs > 2000000000) c.backrooms_vault_studs = 2000000000;
    Log("config: backrooms vault_studs=%d [br5]\n", c.backrooms_vault_studs);
    c.backrooms_easy_key  = IniI(path, "backrooms", "easy_out_key", d.backrooms_easy_key) & 0xFF;
    IniS(path, "backrooms", "easy_out_buttons", d.backrooms_easy_buttons, c.backrooms_easy_buttons, sizeof c.backrooms_easy_buttons);
    c.backrooms_hud       = IniI(path, "backrooms", "hud", d.backrooms_hud) ? 1 : 0;
    c.backrooms_workers   = IniI(path, "backrooms", "workers", d.backrooms_workers) ? 1 : 0;     // [br3]
    c.backrooms_vehicles  = IniI(path, "backrooms", "vehicles", d.backrooms_vehicles) ? 1 : 0;   // [br3]
    c.backrooms_freeze    = IniI(path, "backrooms", "freeze", d.backrooms_freeze) ? 1 : 0;       // [freeze]
    c.backrooms_worker_repose = IniI(path, "backrooms", "worker_repose", d.backrooms_worker_repose) ? 1 : 0;
    c.backrooms_effects   = IniI(path, "backrooms", "effects", d.backrooms_effects) ? 1 : 0;
    c.backrooms_handicam  = IniI(path, "backrooms", "handicam", d.backrooms_handicam) ? 1 : 0;
    c.backrooms_intensity = IniI(path, "backrooms", "intensity", d.backrooms_intensity);
    c.backrooms_intensity = c.backrooms_intensity < 0 ? 0 : c.backrooms_intensity > 3 ? 3 : c.backrooms_intensity;
    c.backrooms_pillarbox = IniI(path, "backrooms", "pillarbox", d.backrooms_pillarbox) ? 1 : 0;
    c.backrooms_blink_ms  = IniI(path, "backrooms", "blink_ms", d.backrooms_blink_ms);
    c.backrooms_blink_ms  = c.backrooms_blink_ms < 0 ? 0 : c.backrooms_blink_ms > 400 ? 400 : c.backrooms_blink_ms;
    c.backrooms_visit     = IniI(path, "backrooms", "visit", d.backrooms_visit) ? 1 : 0;
    c.backrooms_leave     = IniI(path, "backrooms", "leave", d.backrooms_leave) ? 1 : 0;
    c.backrooms_trace     = IniI(path, "backrooms", "trace", d.backrooms_trace) ? 1 : 0;   // [fix1]
    c.backrooms_portal_snap  = IniI(path, "backrooms", "portal_snap", d.backrooms_portal_snap);   // [fix2]
    c.backrooms_portal_snap  = c.backrooms_portal_snap < 0 ? 0 : c.backrooms_portal_snap > 2 ? 2 : c.backrooms_portal_snap;
    c.backrooms_portal_hold  = IniI(path, "backrooms", "portal_hold", d.backrooms_portal_hold) ? 1 : 0;
    c.backrooms_portal_black = IniI(path, "backrooms", "portal_black", d.backrooms_portal_black) ? 1 : 0;
    c.backrooms_portal_catch = IniF(path, "backrooms", "portal_catch", d.backrooms_portal_catch);
    if (!(c.backrooms_portal_catch >= 0.1f && c.backrooms_portal_catch <= 5.0f)) c.backrooms_portal_catch = d.backrooms_portal_catch;
    IniS(path, "backrooms", "file", d.backrooms_file, c.backrooms_file, sizeof(c.backrooms_file));
    for (const char* q = c.backrooms_file; *q; ++q)                      // a bare file name only (beside the DLL)
        if (*q == '\\' || *q == '/' || *q == ':') { strncpy_s(c.backrooms_file, d.backrooms_file, _TRUNCATE); break; }
    Log("config: backrooms enable=%d statues=%d colossi=%d effects=%d handicam=%d intensity=%d pillarbox=%d blink=%d ms visit=%d leave=%d file=%s\n",
        c.backrooms_enable, c.backrooms_statues, c.backrooms_colossi, c.backrooms_effects, c.backrooms_handicam, c.backrooms_intensity,
        c.backrooms_pillarbox, c.backrooms_blink_ms, c.backrooms_visit, c.backrooms_leave, c.backrooms_file);
    Log("config: backrooms trace=%d [fix1]\n", c.backrooms_trace);
    Log("config: backrooms goal_studs=%d easy_out_buttons=%s easy_out_key=0x%02X hud=%d colossi(actors)=%d [br4]\n", c.backrooms_goal_studs,
        c.backrooms_easy_buttons, c.backrooms_easy_key, c.backrooms_hud, c.backrooms_colossi);
    Log("config: backrooms workers=%d vehicles=%d [br3]\n", c.backrooms_workers, c.backrooms_vehicles);
    Log("config: backrooms freeze=%d worker_repose=%d [freeze]\n", c.backrooms_freeze, c.backrooms_worker_repose);
    Log("config: backrooms portal_snap=%d portal_hold=%d portal_black=%d portal_catch=%.2f u (black at most blink_ms=%d) [fix2]\n",
        c.backrooms_portal_snap, c.backrooms_portal_hold, c.backrooms_portal_black, c.backrooms_portal_catch, c.backrooms_blink_ms);
    // [labels1] [maplabels]
    c.maplabels_enable         = IniI(path, "maplabels", "enable", d.maplabels_enable) ? 1 : 0;
    c.maplabels_label          = IniI(path, "maplabels", "label", d.maplabels_label) ? 1 : 0;
    c.maplabels_pins_districts = IniI(path, "maplabels", "pins_districts", d.maplabels_pins_districts) ? 1 : 0;
    c.maplabels_pins_places    = IniI(path, "maplabels", "pins_places", d.maplabels_pins_places) ? 1 : 0;
    c.maplabels_pins_boats     = IniI(path, "maplabels", "pins_boats", d.maplabels_pins_boats) ? 1 : 0;
    Log("config: maplabels enable=%d label=%d pins_districts=%d pins_places=%d pins_boats=%d\n", c.maplabels_enable,
        c.maplabels_label, c.maplabels_pins_districts, c.maplabels_pins_places, c.maplabels_pins_boats);
    // [marina1] [marina]: berths, anchored boats, boat stations (marina_model.h Load, marina.h)
    marm::Load(c.marina, [&](const char* k, const char* def, char* out, size_t n) { IniS(path, "marina", k, def, out, static_cast<int>(n)); },
               [&](const char* fmt, auto... a) { Log(fmt, a...); });
    // [boats2] [boats]: the traffic boats (boats_model.h Params = the defaults; Sanitize clamps)
    c.boats.enable     = IniI(path, "boats", "enable", d.boats.enable);
    c.boats.spawn_gap  = IniF(path, "boats", "spawn_gap", d.boats.spawn_gap);
    c.boats.files      = IniI(path, "boats", "files", d.boats.files);
    c.boats.follow     = IniF(path, "boats", "follow", d.boats.follow);
    c.boats.lat        = IniF(path, "boats", "lat", d.boats.lat);
    c.boats.align      = IniF(path, "boats", "align", d.boats.align);
    c.boats.hold       = IniF(path, "boats", "hold", d.boats.hold);
    c.boats.keep       = IniF(path, "boats", "keep", d.boats.keep);
    c.boats.fade_view  = IniF(path, "boats", "fade_view", d.boats.fade_view);
    c.boats.gap_s      = IniF(path, "boats", "gap_s", d.boats.gap_s);
    c.boats.cap        = IniI(path, "boats", "cap", d.boats.cap);
    c.boats.cap_radius = IniF(path, "boats", "cap_radius", d.boats.cap_radius);
    c.boats.log        = IniI(path, "boats", "log", d.boats.log);
    btm::Sanitize(c.boats);
    Log("config: boats enable=%d spawn_gap=%.2f files=%d follow=%.0f lat=%.1f align=%.0f hold=%.1f keep=%.0f fade_view=%.0f gap_s=%.2f "
        "cap=%d cap_radius=%.0f log=%d [boats2]\n", c.boats.enable, c.boats.spawn_gap, c.boats.files, c.boats.follow, c.boats.lat,
        c.boats.align, c.boats.hold, c.boats.keep, c.boats.fade_view, c.boats.gap_s, c.boats.cap, c.boats.cap_radius, c.boats.log);
    // [weather]: the settings, then the zones (lcudrive_weather.txt beside the ini; no file = no zones)
    c.weather_enable   = IniI(path, "weather", "enable", d.weather_enable) ? 1 : 0;
    c.weather_rain     = IniI(path, "weather", "rain", d.weather_rain);
    c.weather_rain     = c.weather_rain < 0 ? 0 : c.weather_rain >= wxm::kUserRains ? wxm::kUserRains - 1 : c.weather_rain;   // 4 = Changing
    c.weather_fog      = IniI(path, "weather", "fog", d.weather_fog);
    c.weather_fog      = c.weather_fog < 0 ? 0 : c.weather_fog >= wxm::kFogs ? wxm::kFogs - 1 : c.weather_fog;
    c.weather_zones    = IniI(path, "weather", "zones", d.weather_zones) ? 1 : 0;
    c.weather_engine_fog  = IniI(path, "weather", "engine_fog", d.weather_engine_fog) ? 1 : 0;
    c.weather_engine_rain = IniI(path, "weather", "engine_rain", d.weather_engine_rain) ? 1 : 0;
    c.weather_screen_rain = IniI(path, "weather", "screen_rain", d.weather_screen_rain) ? 1 : 0;
    c.weather_overcast = Clampf(IniF(path, "weather", "overcast", d.weather_overcast), 0.0f, 2.0f);
    c.weather_mist     = Clampf(IniF(path, "weather", "mist", d.weather_mist), 0.0f, 2.0f);
    c.weather_blend    = Clampf(IniF(path, "weather", "blend", d.weather_blend), 0.5f, 60.0f);
    c.weather_night_fog = Clampf(IniF(path, "weather", "night_fog", d.weather_night_fog), 0.0f, 1.0f);
    c.weather_vis_light = Clampf(IniF(path, "weather", "vis_light", d.weather_vis_light), 20.0f, 2000.0f);
    c.weather_vis_heavy = Clampf(IniF(path, "weather", "vis_heavy", d.weather_vis_heavy), 12.0f, 2000.0f);
    c.weather_low_light = Clampf(IniF(path, "weather", "low_light", d.weather_low_light), 0.0f, 2.0f);
    c.weather_low_heavy = Clampf(IniF(path, "weather", "low_heavy", d.weather_low_heavy), 0.0f, 2.0f);
    c.weather_thick_light = Clampf(IniF(path, "weather", "thick_light", d.weather_thick_light), 0.5f, 500.0f);
    c.weather_thick_heavy = Clampf(IniF(path, "weather", "thick_heavy", d.weather_thick_heavy), 0.5f, 500.0f);
    c.weather_weird    = Clampf(IniF(path, "weather", "weird", d.weather_weird), 0.0f, 0.9f);
    c.weather_shower   = Clampf(IniF(path, "weather", "shower", d.weather_shower), 30.0f, 7200.0f);
    c.weather_spell    = Clampf(IniF(path, "weather", "spell", d.weather_spell), 60.0f, 3600.0f);
    c.weather_wet_share = Clampf(IniF(path, "weather", "wet_share", d.weather_wet_share), 0.0f, 1.0f);
    c.weather_log      = IniI(path, "weather", "log", d.weather_log) ? 1 : 0;
    IniS(path, "weather", "file", d.weather_file, c.weather_file, sizeof(c.weather_file));
    {
        static char wxBuf[16384];                                        // (hotkey thread only; parsed at once)
        const char* from = "no zone file";
        wxBuf[0] = 0;
        char fpath[MAX_PATH] = {};
        bool nameOk = c.weather_file[0] != 0;
        for (const char* q = c.weather_file; *q; ++q) if (*q == '\\' || *q == '/' || *q == ':') nameOk = false;   // a bare file name only
        if (nameOk)
        {
            strncpy_s(fpath, path, _TRUNCATE);
            char* slash = strrchr(fpath, '\\');
            if (slash) slash[1] = 0; else fpath[0] = 0;
            strncat_s(fpath, c.weather_file, _TRUNCATE);
            FILE* f = nullptr;
            if (fopen_s(&f, fpath, "rb") == 0 && f)
            {
                const size_t n = fread(wxBuf, 1, sizeof(wxBuf) - 1, f);
                fclose(f);
                wxBuf[n] = 0;
                from = c.weather_file;
            }
        }
        const int taken = wxm::Parse(wxBuf, c.weather_data);
        if (c.weather_data.bad)
            Log("config: [weather] %d line(s) of %s were refused; the first is line %d: %s\n", c.weather_data.bad, from,
                c.weather_data.firstBadLine, c.weather_data.firstBad);
        uint32_t h = 2166136261u;                                        // the data's stamp (FNV-1a over the text in force)
        for (const char* q = wxBuf; *q; ++q) { h ^= static_cast<unsigned char>(*q); h *= 16777619u; }
        c.weather_stamp = h ? h : 1;
        Log("config: weather enable=%d rain=%s fog=%s zones=%d engine_fog=%d engine_rain=%d screen_rain=%d overcast=%.2f mist=%.2f "
            "blend=%.1f night_fog=%.2f vis %.0f/%.0f low %.3f/%.3f thick %.1f/%.1f weird=%.2f shower=%.0f | %s: %d records - "
            "%d zones, %d subs\n", c.weather_enable, (c.weather_rain == wxm::kChanging ? "changing" : wxm::RainName(c.weather_rain)), wxm::FogName(c.weather_fog), c.weather_zones,
            c.weather_engine_fog, c.weather_engine_rain, c.weather_screen_rain, c.weather_overcast, c.weather_mist, c.weather_blend,
            c.weather_night_fog, c.weather_vis_light, c.weather_vis_heavy, c.weather_low_light, c.weather_low_heavy,
            c.weather_thick_light, c.weather_thick_heavy, c.weather_weird, c.weather_shower, from, taken,
            c.weather_data.nZones, c.weather_data.nSubs);
        for (int i = 0; i < c.weather_data.nZones; ++i)
        {
            const wxm::Zone& z = c.weather_data.zones[i];
            Log("config: weather zone %d \"%s\" at (%.0f, %.0f) radius %.0f + %.0f: fog %s, rain %s (%.0f%% of the time), "
                "overcast %.2f, %d sub-zone(s)\n", i, z.name, z.x, z.z, z.radius, z.falloff, wxm::FogName(z.fog),
                wxm::RainName(z.rain), z.duty * 100.0f, z.overcast, z.nSub);
        }
    }
    // [gfx] (gfx.h, Notes\57)
    c.gfx_preset       = IniI(path, "gfx", "preset", d.gfx_preset);
    c.gfx_preset       = c.gfx_preset < 0 ? 0 : c.gfx_preset >= gfxm::kPresets ? gfxm::kPresets - 1 : c.gfx_preset;
    c.gfx_grade        = Clampf(IniF(path, "gfx", "grade", d.gfx_grade), 0.0f, 2.0f);
    c.gfx_ao           = Clampf(IniF(path, "gfx", "ao", d.gfx_ao), 0.0f, 2.0f);
    c.gfx_detail       = Clampf(IniF(path, "gfx", "detail", d.gfx_detail), 0.0f, 2.0f);
    c.gfx_rim          = Clampf(IniF(path, "gfx", "rim", d.gfx_rim), 0.0f, 2.0f);
    c.gfx_haze         = Clampf(IniF(path, "gfx", "haze", d.gfx_haze), 0.0f, 2.0f);
    c.gfx_haze_dist    = Clampf(IniF(path, "gfx", "haze_dist", d.gfx_haze_dist), 50.0f, 5000.0f);
    c.gfx_ca           = Clampf(IniF(path, "gfx", "aberration", d.gfx_ca), 0.0f, 3.0f);
    c.gfx_sharpen      = Clampf(IniF(path, "gfx", "sharpen", d.gfx_sharpen), 0.0f, 2.5f);
    c.gfx_vignette     = Clampf(IniF(path, "gfx", "vignette", d.gfx_vignette), 0.0f, 2.5f);
    c.gfx_grain        = Clampf(IniF(path, "gfx", "grain", d.gfx_grain), 0.0f, 4.0f);
    c.gfx_heat         = Clampf(IniF(path, "gfx", "heat", d.gfx_heat), 0.0f, 2.0f);
    c.gfx_camera       = IniI(path, "gfx", "camera", d.gfx_camera);
    c.gfx_camera       = c.gfx_camera < 0 ? 0 : c.gfx_camera > 2 ? 2 : c.gfx_camera;
    c.gfx_camera_scale = Clampf(IniF(path, "gfx", "camera_scale", d.gfx_camera_scale), 0.0f, 2.0f);
    c.gfx_bank_sign    = IniI(path, "gfx", "bank_sign", d.gfx_bank_sign) < 0 ? -1 : 1;
    c.gfx_far_fix      = IniI(path, "gfx", "far_fix", d.gfx_far_fix) ? 1 : 0;                              // [fix1]
    c.gfx_far_from     = Clampf(IniF(path, "gfx", "far_from", d.gfx_far_from), 0.0f, 5000.0f);
    c.gfx_far_dome     = Clampf(IniF(path, "gfx", "far_dome", d.gfx_far_dome), 100.0f, 10000.0f);
    c.gfx_far_max      = Clampf(IniF(path, "gfx", "far_max", d.gfx_far_max), 500.0f, 20000.0f);
    Log("config: gfx far plane fix=%d from %.0f u, dome %.0f u, max %.0f u [fix1]\n", c.gfx_far_fix, c.gfx_far_from, c.gfx_far_dome, c.gfx_far_max);
    c.gfx_reshade_aware = IniI(path, "gfx", "reshade_aware", d.gfx_reshade_aware) ? 1 : 0;
    {
        // ReShade's second clock (LCU_DayNightCycle): ReShadePreset.ini beside the game exe. Read here, never in the render hook.
        char exe[MAX_PATH] = {};
        c.gfx_reshade_grade = 0;
        if (GetModuleFileNameA(nullptr, exe, MAX_PATH))
        {
            char* slash = std::strrchr(exe, '\\');
            if (slash) { slash[1] = 0; strncat_s(exe, "ReShadePreset.ini", _TRUNCATE); }
            FILE* f = nullptr;
            if (slash && fopen_s(&f, exe, "rb") == 0 && f)
            {
                static char rsBuf[16384];
                const size_t n = fread(rsBuf, 1, sizeof(rsBuf) - 1, f); rsBuf[n] = 0; fclose(f);
                c.gfx_reshade_grade = gfxm::ReShadeGradeOn(rsBuf) ? 1 : 0;
            }
        }
    }
    Log("config: gfx preset=%s grade=%.2f ao=%.2f detail=%.2f rim=%.2f haze=%.2f (%.0f u) aberration=%.2f sharpen=%.2f vignette=%.2f "
        "grain=%.2f heat=%.2f camera=%d x%.2f bank_sign=%d | ReShade LCU_DayNightCycle %s%s\n", gfxm::PresetName(c.gfx_preset),
        c.gfx_grade, c.gfx_ao, c.gfx_detail, c.gfx_rim, c.gfx_haze, c.gfx_haze_dist, c.gfx_ca, c.gfx_sharpen, c.gfx_vignette,
        c.gfx_grain, c.gfx_heat, c.gfx_camera, c.gfx_camera_scale, c.gfx_bank_sign, c.gfx_reshade_grade ? "ON" : "off",
        c.gfx_reshade_grade && c.gfx_reshade_aware ? " (the engine grade stays off while it is)" : "");
    // [gfx] gfx2 (gfx2.h)
    c.gfx_bloom        = IniI(path, "gfx", "bloom", d.gfx_bloom) ? 1 : 0;
    c.gfx_bloom_scale  = Clampf(IniF(path, "gfx", "bloom_scale", d.gfx_bloom_scale), 0.0f, 2.0f);
    c.gfx_far_blur     = IniI(path, "gfx", "far_blur", d.gfx_far_blur) ? 1 : 0;
    c.gfx_far_blur_scale = Clampf(IniF(path, "gfx", "far_blur_scale", d.gfx_far_blur_scale), 0.0f, 2.0f);
    c.gfx_far_blur_start = Clampf(IniF(path, "gfx", "far_blur_start", d.gfx_far_blur_start), 60.0f, 2000.0f);
    c.gfx_far_blur_full  = Clampf(IniF(path, "gfx", "far_blur_full", d.gfx_far_blur_full), 100.0f, 6000.0f);
    c.gfx_sun_rays     = IniI(path, "gfx", "sun_rays", d.gfx_sun_rays) ? 1 : 0;
    c.gfx_sun_rays_scale = Clampf(IniF(path, "gfx", "sun_rays_scale", d.gfx_sun_rays_scale), 0.0f, 2.0f);
    c.gfx_flare_scale  = Clampf(IniF(path, "gfx", "flare_scale", d.gfx_flare_scale), 0.0f, 2.0f);
    c.gfx_moonlight    = IniI(path, "gfx", "moonlight", d.gfx_moonlight) ? 1 : 0;
    c.gfx_moonlight_scale = Clampf(IniF(path, "gfx", "moonlight_scale", d.gfx_moonlight_scale), 0.0f, 2.0f);
    c.gfx_rt           = IniI(path, "gfx", "ray_traced", d.gfx_rt) ? 1 : 0;
    c.gfx_rt_scale     = Clampf(IniF(path, "gfx", "ray_traced_scale", d.gfx_rt_scale), 0.0f, 2.0f);
    c.gfx_reflections  = IniI(path, "gfx", "reflections", d.gfx_reflections) ? 1 : 0;
    c.gfx_reflections_scale = Clampf(IniF(path, "gfx", "reflections_scale", d.gfx_reflections_scale), 0.0f, 2.0f);
    c.gfx_split_fx     = IniI(path, "gfx", "split_fx", d.gfx_split_fx) ? 1 : 0;
    c.gfx_fps          = IniI(path, "gfx", "fps", d.gfx_fps) ? 1 : 0;
    c.gfx_auto         = IniI(path, "gfx", "auto", d.gfx_auto) ? 1 : 0;
    c.gfx_auto_target  = Clampf(IniF(path, "gfx", "auto_target", d.gfx_auto_target), 30.0f, 165.0f);
    Log("config: gfx2 bloom=%d x%.2f far_blur=%d x%.2f (%.0f .. %.0f u) sun_rays=%d x%.2f flare x%.2f moonlight=%d x%.2f reflections=%d x%.2f "
        "split_fx=%d fps=%d auto=%d (target %.0f)\n", c.gfx_bloom, c.gfx_bloom_scale, c.gfx_far_blur, c.gfx_far_blur_scale, c.gfx_far_blur_start,
        c.gfx_far_blur_full, c.gfx_sun_rays, c.gfx_sun_rays_scale, c.gfx_flare_scale, c.gfx_moonlight, c.gfx_moonlight_scale, c.gfx_reflections,
        c.gfx_reflections_scale, c.gfx_split_fx, c.gfx_fps, c.gfx_auto, c.gfx_auto_target);
    c.log_every        = IniI(path, "debug", "log_every", d.log_every);
}

static void LogConfig(const Config& c)
{
    Log("config: menu enable=%d (Mod Options in the pause menu; read at startup)\n", c.menu_enable);
    Log("config: world safe_sea=%d from x %.0f (no deep-water shark round Port Atlas)\n", c.world_safe_sea, c.world_safe_sea_x);
    Log("config: shark enable=%d classic=%d grace=%.0f s chance=%.3f/min ramp=%.2f max=%.2f warn=%.0f s guard=%.0f s\n",
        c.shark_enable, c.shark_classic, c.shark_grace_s, c.shark_chance_per_min, c.shark_ramp, c.shark_max_per_min,
        c.shark_warn_s, c.shark_guard_s);
    Log("config: voice enable=%d shark=%d order_car=%d phone=%d swim=%d ped_thanks=%d ped_nothanks=%d ped_witness=%d once=%d "
        "chase_cooldown=%.0f phone_cooldown=%.0f ped_gap=%.0f ped_hear=%.0f falloff=%.0f\n", c.voice_enable, c.voice_shark,
        c.voice_order_car, c.voice_phone, c.voice_swim, c.voice_ped_thanks, c.voice_ped_nothanks, c.voice_ped_witness, c.voice_once,
        c.voice_chase_cooldown, c.voice_phone_cooldown, c.voice_ped_gap, c.voice_ped_hear, c.voice_falloff);
    Log("config: dino enable=%d count=%d model=%s at (%.1f, %.2f, %.1f) step (%.1f, %.1f) heading=%.0f near=%.0f far=%.0f "
        "leave=%.0f invulnerable=%d\n", c.dino_enable, c.dino_count, c.dino_model, c.dino_x, c.dino_y, c.dino_z, c.dino_step_x,
        c.dino_step_z, c.dino_heading, c.dino_near, c.dino_far, c.dino_leave, c.dino_invulnerable);
    Log("config: dino group 2: count2=%d model2=%s at (%.1f, %.2f, %.1f) step (%.1f, %.1f) heading2=%.0f\n", c.dino2_count,
        c.dino2_model[0] ? c.dino2_model : "(none)", c.dino2_x, c.dino2_y, c.dino2_z, c.dino2_step_x, c.dino2_step_z, c.dino2_heading);
    Log("config: pteranodon enable=%d creature=%s cruise=%.1f stall=%.1f max_speed=%.1f glide=%.1f climb=%.0f dive=%.0f flap_lift=%.1f "
        "flap_rate=%.2f flap_climb=%.0f takeoff_up=%.1f bank=%.0f turn=%.0f ceiling=%.0f sea_y=%.2f skim=%.2f tilt=%.2f tilt_pitch=%.0f "
        "pitch_sign=%d auto_glide=%.1f chute=%d anims=%d coast=%.0f bounds=%d (%.0f..%.0f, %.0f..%.0f)\n", c.ptero_enable, c.ptero_creature,
        c.ptero_cruise, c.ptero_stall, c.ptero_max_speed, c.ptero_glide, c.ptero_climb, c.ptero_dive, c.ptero_flap_lift, c.ptero_flap_rate,
        c.ptero_flap_climb, c.ptero_takeoff_up, c.ptero_bank, c.ptero_turn, c.ptero_ceiling, c.ptero_sea_y, c.ptero_skim, c.ptero_tilt,
        c.ptero_tilt_pitch, c.ptero_pitch_sign, c.ptero_auto_glide, c.ptero_chute, c.ptero_anims, c.ptero_coast, c.ptero_bounds, c.ptero_min_x,
        c.ptero_max_x, c.ptero_min_z, c.ptero_max_z);
    Log("config: enable=%d bridge=%d telemetry=%d handbrake_key=0x%X pad_handbrake=0x%X "
        "deadzone=%.2f curve=%.2f brake_key_brakes=%d engine_sound=%d | power=%.2f top_speed=%.2f grip=%.2f balance=%+.2f brakes=%.2f "
        "steer_speed=%.2f steer_lock=%.2f weight=%.2f handbrake=%.2f breakaway=%.2f tc=%.2f "
        "yaw_damping=%.2f yaw_inertia=%.2f stability=%.2f size_effect=%.2f drift=%.2f pedal_hb=%d brake_drift=%.2f | "
        "headlights=%d traffic=%d range=%.1f "
        "fov=%.0f intensity=%.2f night_below=%.2f cookie=%d autofix=%d | log_every=%d\n",
        c.enable, c.bridge, c.telemetry, c.handbrake_key, c.pad_handbrake, c.stick_deadzone,
        c.steer_curve, c.brake_key_brakes, c.engine_sound,
        c.power, c.top_speed, c.grip, c.balance, c.brakes, c.steer_speed, c.steer_lock, c.weight,
        c.handbrake, c.breakaway, c.traction_control, c.yaw_damping, c.yaw_inertia, c.stability,
        c.size_effect, c.drift, c.pedal_handbrake, c.brake_drift,
        c.headlights, c.traffic_lights, c.headlight_range, c.headlight_fov, c.headlight_intensity,
        c.night_below, c.headlight_cookie, c.headlight_autofix, c.log_every);
    Log("config: streetlamps=%d max_lights=%d radius=%.2f range=%.0f intensity=%.2f falloff=%.3f "
        "head_drop=%.2f fixtures=%d log=%d colours sodium %.2f/%.2f/%.2f white %.2f/%.2f/%.2f lantern %.2f/%.2f/%.2f\n",
        c.sl_mode, c.sl_max_lights, c.sl_radius, c.sl_range, c.sl_intensity, c.sl_falloff, c.sl_head_drop,
        c.sl_fixtures, c.sl_log, c.sl_col[0][0], c.sl_col[0][1], c.sl_col[0][2], c.sl_col[1][0], c.sl_col[1][1],
        c.sl_col[1][2], c.sl_col[2][0], c.sl_col[2][1], c.sl_col[2][2]);
    Log("config: heli enable=%d top_speed=%.1f max_tilt=%.0f climb=%.1f descend=%.1f thrust_g=%.2f "
        "yaw_rate=%.0f turn_bank=%.2f brake=%.2f response=%.2f land_height=%.2f signs=%+.0f/%+.0f/%+.0f\n",
        c.heli_enable, c.heli_top_speed, c.heli_max_tilt, c.heli_climb, c.heli_descend, c.heli_thrust_g,
        c.heli_yaw_rate, c.heli_turn_bank, c.heli_brake, c.heli_response, c.heli_land_height,
        c.heli_yaw_sign, c.heli_fwd_sign, c.heli_vert_sign);
    Log("config: heli exit_anywhere=%d bail_out=%d park_height=%.2f free_flight=%d crash_spin=%.1f "
        "wreck_on_crash=%d crash_speed=%.1f parachute=%d chute_cut=%d chute_fall=%.1f\n",
        c.heli_exit_anywhere, c.heli_bail, c.heli_park_height, c.heli_free_flight, c.heli_crash_spin,
        c.heli_wreck_on_crash, c.heli_crash_speed, c.heli_parachute, c.heli_chute_cut, c.heli_chute_fall);
    Log("config: bike enable=%d wheelie_speed=%.1f wheelie_angle=%.0f stoppie_angle=%.0f air_flip=%.1f "
        "air_spin=%.1f lean_keys=%d drive=%d top_speed=%.1f power=%.2f grip=%.2f brake_slide=%d | heli stick_direct=%d\n",
        c.bike_enable, c.bike_wheelie_speed, c.bike_wheelie_angle, c.bike_stoppie_angle, c.bike_air_flip,
        c.bike_air_spin, c.bike_lean_keys, c.bike_drive, c.bike_top_speed, c.bike_power, c.bike_grip_g,
        c.bike_brake_slide, c.heli_stick_direct);
    Log(">>> airplane: models %s fly as aeroplanes on their own wheels (airplane.h)%s; Hover planes ([plane] models): %s\n",
        c.airplane_models[0] ? c.airplane_models : "none", c.airplane_enable ? "" : " - OFF ([airplane] enable = 0)",
        c.plane_enable && c.plane_models[0] ? c.plane_models : "none");
    Log("config: airplane stick_direct=%d signs steer %+.0f turn %+.0f fwd %+.0f rotate=%.1f side_grip=%.2f rolling=%.3f "
        "brake_mu=%.2f reverse_g=%.2f crash_damage=%d max_velocity=%.1f cam %.2f/%.2f fast %.2f/%.2f glide=%d chute=%d "
        "force_lod1=%d horn=%d rpm=%d | airport=%d count=%d model=%s models=%s pin=%s near=%.0f far=%.0f leave=%.0f\n",
        c.airplane_stick_direct, c.airplane_steer_sign, c.airplane_turn_sign, c.airplane_fwd_sign, c.airplane_rotate_deg,
        c.airplane_side_grip, c.airplane_rolling, c.airplane_brake_mu, c.airplane_reverse_g, c.airplane_crash_damage,
        c.airplane_max_velocity, c.airplane_cam_height, c.airplane_cam_distance, c.airplane_cam_height_fast,
        c.airplane_cam_distance_fast, c.airplane_abandon_glide, c.airplane_bail_parachute, c.airplane_force_lod1,
        c.airplane_stall_horn, c.airplane_engine_rpm, c.plane_airport, c.plane_airport_count, c.plane_airport_model,
        c.plane_airport_models[0] ? c.plane_airport_models : "(airport_model everywhere)",
        c.plane_airport_pin[0] ? c.plane_airport_pin : "(none)", c.plane_airport_near, c.plane_airport_far, c.plane_airport_leave);
    Log("config: airplane gear retract=%d up_top_speed=%.1f time=%.1f belly_speed=%.1f belly_decel=%.1f auto_down=%d warn_height=%.1f "
        "visual=%d; props=%d idle=%.1f max=%.1f turns/s\n",
        c.airplane_gear_retract, c.airplane_gear_up_speed, c.airplane_gear_time, c.airplane_gear_belly_speed,
        c.airplane_gear_belly_decel, c.airplane_gear_auto_down, c.airplane_gear_warn_height, c.airplane_gear_visual,
        c.airplane_props, c.airplane_prop_idle, c.airplane_prop_max);
    Log("config: jetpack enable=%d height=%.2f time=%.1f resume=%d keep_ceiling=%d\n",
        c.jp_enable, c.jp_height, c.jp_time, c.jp_resume, c.jp_keep_ceiling);
    Log("config: wanted enable=%d debug_level=%d radius=%.0f chase_speed=%.1f evade=%.0f+%.0f/level | police "
        "boost=%d top_speed=%.1f power=%.2f grip=%.2f catchup=%.2f ram=%.2f\n",
        c.wanted_enable, c.wanted_debug_level, c.wanted_radius, c.wanted_speed, c.wanted_evade_base,
        c.wanted_evade_per_level, c.wanted_police_boost, c.wanted_police_speed, c.wanted_police_power,
        c.wanted_police_grip, c.wanted_police_catchup, c.wanted_police_ram);
    Log("config: wanted crimes=%d (run_over %.2f ram_police %.2f wreck %.2f steal_car %.2f steal_police %.2f hit_cop %.2f "
        "hit_person %.2f punch %.2f shoot_person %.2f shoot_prop %.2f) gadgets=%d stun=%.1f s tie=%.1f s busted studs=%d x level, station=%d "
        "grace=%.0f s hud=%d | officers=%d get_out=%.0f join=%.0f fire=%.0f grapple=%.2f/s fight=%d\n",
        c.wanted_crimes, c.wanted_crime_run_over, c.wanted_crime_ram_police, c.wanted_crime_wreck,
        c.wanted_crime_steal_car, c.wanted_crime_steal_police, c.wanted_crime_hit_cop, c.wanted_crime_hit_person, c.wanted_crime_punch,
        c.wanted_crime_shoot_person, c.wanted_crime_shoot_prop,
        c.wanted_gadgets, c.wanted_stun_s, c.wanted_tie_s, c.wanted_busted_studs, c.wanted_busted_station,
        c.wanted_busted_grace, c.wanted_hud, c.wanted_officers, c.wanted_officer_get_out, c.wanted_officer_join,
        c.wanted_officer_fire, c.wanted_officer_grapple, c.wanted_officers_fight);
    Log("config: wanted busted_outside=%d at (%.2f, %.2f, %.2f) heading %.0f, load hold %.1f s, fade %.2f s\n",   // [busted1]
        c.wanted_busted_outside, c.wanted_busted_outside_x, c.wanted_busted_outside_y, c.wanted_busted_outside_z,
        c.wanted_busted_outside_heading, c.wanted_busted_outside_load, c.wanted_busted_fade);
    Log("config: wanted boats=%d radius=%.0f linger=%.0f s driver=%s minnow=%d | boat_arrest=%d range=%.1f u "
        "after %.1f s\n",
        c.wanted_boats, c.wanted_boat_radius, c.wanted_boat_linger,
        c.wanted_boat_driver[0] ? c.wanted_boat_driver : "(the game's)", c.wanted_boat_minnow,
        c.wanted_boat_arrest, c.wanted_boat_arrest_range, c.wanted_boat_arrest_s);
    Log("config: wanted heli=%d model=%s one from level %d, two from %d, %.0f / %.0f u up, circle %.0f u, %.0f u/s, "
        "in sight %.0f u, alone holds %.0f s, siren=%d news=%d | searchlight=%d fov=%.0f range=%.0f intensity=%.2f "
        "axis=%+d mesh=%d\n",
        c.wanted_heli, c.wanted_heli_model, c.wanted_heli_level, c.wanted_heli_level2, c.wanted_heli_height4,
        c.wanted_heli_height5, c.wanted_heli_radius, c.wanted_heli_speed, c.wanted_heli_sight, c.wanted_heli_hold,
        c.wanted_heli_siren, c.wanted_heli_news, c.wanted_heli_searchlight, c.wanted_heli_beam_fov,
        c.wanted_heli_beam_range, c.wanted_heli_beam_intensity, c.wanted_heli_beam_axis, c.wanted_heli_beam_mesh);
    Log("config: wanted flee=%d level=%d radius=%.0f u speed=%.0f u/s\n", c.wanted_flee, c.wanted_flee_level,
        c.wanted_flee_radius, c.wanted_flee_speed);
    Log("config: life enable=%d max_scenes=%d max_people=%d every=%.0f s fights=%.2f radius=%.0f-%.0f u "
        "crowd_reactions=%d crowd_chats=%d every %.0f s\n",
        c.life_enable, c.life_max_scenes, c.life_max_people, c.life_every, c.life_fights, c.life_min_radius,
        c.life_radius, c.life_reactions, c.life_crowd_chats, c.life_crowd_chat_every);
    Log("config: life dance party: hotkey=%d (Ctrl+Alt+B) pad=0x%X radius=%.0f u beach every %.0f s\n", c.life_party_hotkey,
        c.life_party_pad, c.life_party_radius, c.life_party_every);
    Log("config: missions stock=%d side=%d enter=%.1f exit=%.1f safe_speed=%.1f detect stable=%.1f settle=%.0f absent=%.0f | "   // [missions1]
        "radio mission_news=%d interrupt=%d stations=\"%s\" keep=%.1f h recap chance=%.2f max=%d hours=%.1f gap=%.0f min reuse=%d once=%d\n",
        c.mission_stock, c.mission_stock_side, c.mission_stock_enter, c.mission_stock_exit, c.mission_stock_safe_speed,
        c.mission_detect_stable, c.mission_detect_settle, c.mission_detect_absent, c.radio_mission_news, c.radio_mission_interrupt,
        c.radio_mission_stations, c.radio_mission_keep_h, c.radio_mission_recap_chance, c.radio_mission_recap_max,
        c.radio_mission_recap_h, c.radio_mission_recap_gap, c.radio_mission_recap_reuse, c.radio_mission_once);
    Log("config: radio logo=%d seconds=%.1f corner=%d scale=%.2f on_enter=%d\n", c.radio_logo, c.radio_logo_seconds,   // [radiologo1]
        c.radio_logo_corner, c.radio_logo_scale, c.radio_logo_on_enter);
    Log("config: radio enable=%d volume=%.2f in_heli=%d pad_next=0x%X pad_prev=0x%X pad_block=%d duck=%.0f dB "
        "static=%.0f dB start=%d game_music=%.2f breaking=[%s] every %.0f s manifest=%s root=%s\n",
        c.radio_enable, c.radio_volume, c.radio_in_heli, c.radio_pad_next, c.radio_pad_prev, c.radio_pad_block,
        c.radio_duck_db, c.radio_static_db, c.radio_start_station, c.radio_game_music, c.radio_breaking,
        c.radio_breaking_gap, c.radio_manifest, c.radio_root[0] ? c.radio_root : "(manifest's)");
    {
        int folders = c.radio_lib_folders[0] ? 1 : 0;
        for (const char* p = c.radio_lib_folders; *p; ++p) if (*p == '|') ++folders;
        char fl[32]; _snprintf_s(fl, sizeof(fl), _TRUNCATE, "%d", folders);
        Log("config: radio music=%s folders=%s min=%d short=%s skip=\"%s\" holiday=%s unsorted=%s talkover=%d cache=%s "
            "service_app=%s service_volume=%.2f volume_control=%d pause_lead=%.1f max_track=%d min open_tune=%d\n",
            c.radio_music, folders ? fl : "(Windows Music library)", c.radio_lib_min, c.radio_lib_short, c.radio_lib_skip,
            c.radio_lib_holiday, c.radio_lib_unsorted, c.radio_lib_talkover, c.radio_lib_cache, c.radio_svc_app, c.radio_svc_volume,
            c.radio_svc_volctl, c.radio_svc_lead, c.radio_svc_max_min, c.radio_svc_open_tune);
        for (int i = 0; i < c.radio_station_n && i < 16; ++i)
        {
            const RadioStationKeys& s = c.radio_station[i];
            // a link as Spotify's canonical form, a web link without its ?query / #fragment (share
            // links carry an ?si= id tied to the account that shared them)
            std::string canon;
            const uint8_t form = s.playlist[0] ? radio::PlaylistForm(s.playlist, &canon) : radio::PL_NONE;
            std::string shown = s.playlist[0] ? std::string(s.playlist) : std::string("-");
            if (form == radio::PL_SPOTIFY) shown = canon;
            else if (form == radio::PL_WEB) { shown = canon; const size_t q = shown.find_first_of("?#"); if (q != std::string::npos) shown.resize(q); }
            Log("config: radio station %s music=%s playlist=%s%s%s%s genre+=\"%s\"\n", s.id, s.music[0] ? s.music : "(global)",
                shown.c_str(), s.playlist[0] ? " (" : "", s.playlist[0] ? radio::kPlFormNames[form] : "",
                s.playlist[0] ? ")" : "", s.genre);
        }
    }
}
