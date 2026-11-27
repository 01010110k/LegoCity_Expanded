# Changelog

## {VERSION}

The co-op crash fix. If you have an earlier version, run the new `Setup.exe` over it.
- Crashes after a minute or two of two-player, and now and then in busy places alone: fixed at the root. The game keeps a table of every loaded piece of the map with room for 2,048, and the mod's new areas had nearly filled it. The table now holds 4,096. (0.1.1 only caught some of the crashes this caused.)
- Traffic on the new roads now has drivers. The game only seated them inside the original city.
- The mod's log says at start whether the bigger table is in use, and how full it gets.
- If the game ever fails to start after this update, put an empty file named `lcu_stock_level_table.txt` in the game folder to switch the bigger table off, and please report it.

## 0.1.1

Fixes and small changes.
- Co-op: fixed a crash that could happen a minute or so into a split-screen session, when part of the map unloaded.
- Falling through floors: fixed on the train bridge from Fresco to Port Atlas, the sky rail, the Fresco Quay footbridge and steps, and some hills in Saltwind Downs and the Wilds.
- Police: pulling a driver out of a car now gets you noticed, and a police car more so. Not when you wear a police uniform. Empty parked cars don't count.
- West docks: a tidy-up. A proper road surface, no more queue of stuck cars, fewer invisible walls, missing collision filled in, more people and life.
- Sharks: the deep-water shark is now rare and only comes after a long swim, as it was meant to.
- The mod's log now records the game's own script errors, which helps with bug reports.

## 0.1.0 - first public release

Everything is new in this release:
- Driving: a new handling model (engines, gears, tyres, grip, drift and traction settings), motorbike engine and stunts, a jetpack with no height limit, a faster sky rail, a DIY car garage, marinas and boat traffic.
- Aircraft: helicopter rotor physics, no invisible walls, parachute, crash damage, an airliner at the airport.
- Police: a wanted system with officers on foot, police boats and helicopters, an on-screen wanted meter, a quicker busted; "Return to Police Station" drops you outside; story missions keep their own rules and make the radio news.
- City: day and night with real headlights and street lamps, weather that changes by itself (showers, clouds, mist, storms; rain into the distance, never indoors), street life and voices, animals, a car radio, a graphics pass.
- New places: Port Atlas, Woods West, the Wilds and its villages, new docks, traffic with a countryside mix (police and fire trucks included) on every new road, a map of the new land, Herring Cove, Saltwind Downs, castles, and something hidden.
- Co-op: per-player controllers for the mod's buttons, split cut-scenes.
- Mod Options in the pause menu for all of it.
- Setup / Uninstall: game check, save backup, PATCH.DAT rebuilt from the player's own game files, exact uninstall.
