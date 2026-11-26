# LegoCity Expanded

A free, fan-made mod for **LEGO City Undercover** (Steam, PC) that adds a new driving model, a living city, new places to explore, a radio, weather and day and night, and a lot more. Version {VERSION}.

**Install in short:** download `Setup.exe` and the `.lcupak` files from the [Releases page](https://github.com/01010110k/LegoCity_Expanded/releases) into one folder, close the game, run `Setup.exe`, press **Install**. It finds your Steam copy, backs up your saves and builds the mod from your own game files (about 15 minutes the first time). Details below.

---
## Why this exists

I've worked in game development for over 12 years. I made this mod for fun, playing LEGO City Undercover in split-screen with my kid and building the things we kept wishing the game had. It's free, and it's for everyone.

Play it, share it, take it apart, make it yours. The code is MIT-licensed, so you're welcome to learn from it, build on it, and put your own spin on it. If you make something cool, I'd love to see it.

## About the radio and the voices

The radio stations (DJs, songs, news and ads) and the new street voices were made with ElevenLabs. They're optional:
- **In Setup:** untick **Radio** (it's also the biggest part of the download).
- **In the game:** pause → **Mod Options** → **Radio and Co-op** → turn off **Car Radio**, or turn on **Game Music** to keep the game's own music. Street voices are under **City Life → People Talk**.

Free music options are on the way. My kid loved these songs and thought they were hilarious, though, so give them a listen first.

---

## What's in it

**Driving and getting around**
- A new driving model: real engines, gears, tyres and grip, with drift and traction settings. The game's own handling is one switch away.
- Motorbikes with a proper engine and stunts (wheelies, stoppies, flips).
- A jetpack with no height limit.
- Helicopters with rotor physics, no invisible walls, crash damage, and a parachute when you jump out in mid-air. Land a helicopter anywhere.
- A flyable airliner at the airport.
- A faster sky rail.
- A DIY car garage on the police station promenade.
- Boats at new marinas, and boat traffic on the water.

**The police**
- A wanted system: crimes bring the police after you, up to five bricks.
- Officers who jump out and chase you on foot, police boats on the water, and police helicopters at high levels.
- A wanted meter on screen, and a quicker "busted" (you wake up outside the police station).
- "Return to Police Station" drops you on the pavement outside instead of a loading screen into the lobby (switchable).
- Story missions stay as the game made them: the mod's gameplay changes step aside while a mission runs, and the radio news reports on your missions.

**A living city**
- **A full day and night cycle**: the sun moves across the sky, sunsets turn gold, and at night the stars come out and the city lights up - real headlights on every car and street lamps that light the road.
- Weather that changes by itself: clear spells, clouds, mist and showers come and go (or pick fixed rain, a storm, or none). Rain falls into the distance, stays out of interiors, and the light goes grey under it. A dreary village keeps its own fog.
- Street life: people chat, busk, dance, jog, argue and react to your stunts and crashes, with new voices.
- Animals in the countryside: rabbits, goats, gulls, a sleepy bear...
- A radio in every vehicle: stations with songs, hosts, call-ins, ads and news about your crimes.
- A graphics pass: colour by time of day, deeper shading, a soft night glow, sun rays (all switchable).

**New places**
- **Port Atlas**, a new harbour town with a coast and a harbour.
- **Woods West**, the forest beyond the national park, now with trees, roads and paths.
- **The Wilds**, the big northern wilderness: lakes, hikers, cabins, farms and small villages, joined up by the Wilds Road.
- **Herring Cove** (the Outport), a strange, quiet fishing village on the far east coast.
- **Saltwind Downs**, the south-west island, with roads and a village.
- Villages along the new roads: Kingswood Green, Crossways, Kestrel Water, Sawyers Rest and Eastwood Haven.
- New docks with buildings and cranes.
- Traffic on all the new roads, with a countryside mix: SUVs, campers, muscle cars, trucks, tractors, motorbikes, police and fire trucks. Parked cars of many models at the new places.
- Castles built from real LEGO set designs at true brick size.
- The map shows the new land, with labels for the new places.
- There might be some **BlockRooms** hidden somewhere. See if you can find them.

**Co-op**
- Each player can have their own controller for the mod's extra buttons.
- A cut-scene in one player's view only freezes that player.

Everything can be switched on, off or tuned in the game: pause → **Mod Options** (right under the game's own Options).

---

## What you need

- **LEGO City Undercover on Steam** (PC, the current Steam version). Setup checks your game's exe and refuses any other version, so it can't half-work on the wrong copy.
- **Windows 10 or 11, 64-bit.**
- **Free disk space** on the drive the game is installed on: about {DISK_GB} GB (the mod takes about {INSTALLED_GB} GB once installed).
- About 15 minutes the first time: Setup builds one big file from your own game files.
- **No internet connection is needed** to install or play. Setup downloads nothing and sends nothing anywhere.

## Download

Download these files from the **[Releases page](https://github.com/01010110k/LegoCity_Expanded/releases)** and put them **all in the same folder**:

| File | What it is | Size |
|---|---|---|
| `Setup.exe` | the installer (and uninstaller) | {SETUP_MB} MB |
| `core.lcupak` | the mod itself - **required** | {CORE_MB} MB |
| `graphics.lcupak` | the graphics pack: day and night, lights, rain, the on-screen meters (3Dmigoto shaders) - recommended | {GRAPHICS_MB} MB |
| `radio.lcupak` | the radio stations - optional | {RADIO_MB} MB |
| `SHA256SUMS.txt` | checksums, if you want to check the download | - |

If a file is split into parts (`.001`, `.002` ...), download every part; Setup reads them together.

---

## What Setup does, step by step

Run `Setup.exe`. Nothing changes in your game folder until you press **Install**, and every step below is written to a log.

1. **Finds the game.** It reads where Steam is installed (from the Windows registry) and looks through your Steam library folders for LEGO City Undercover. If it can't find it, press **Browse** and choose the game folder yourself.
2. **Checks your game version.** It checks that `LEGOLCUR_DX11.exe` is exactly the Steam version this mod was made for. Any other version: Setup stops and changes nothing.
3. **Checks the game's archives.** It checks that `GAME.DAT` ... `GAME8.DAT` are the stock files. If another mod changed them, Setup stops and tells you to use Steam's "Verify integrity of game files".
4. **Looks for other mods' files.** If the game folder already has files from another mod or tool in the places this mod uses (for example `dinput8.dll`, `d3d11.dll`, `dxgi.dll` (ReShade), `d3dx.ini`, a `ShaderFixes` folder or a `PATCH.DAT`), Setup lists them and asks. You can let it move them to a backup folder (`LCUMod_Backup`, in the game folder) - they are put back when you uninstall - or cancel.
5. **Checks disk space.**
6. **Backs up your saves** (if the box is ticked - it is by default). Your LEGO City Undercover saves from Steam and the game's settings file are copied to `Documents\LegoCity Expanded save backups\<date>`. Setup only reads your saves; it never changes or deletes them, and neither does the uninstaller.
7. **Builds `PATCH.DAT` from your own game files.** This is the big step (a few minutes). The mod's new places and changes live in one file, `PATCH.DAT`, which the game loads by itself, ahead of its own archives. Most of it is the game's own content (buildings, textures, models) rearranged, so it can't be shared in the download - instead the download carries a *recipe*, and Setup rebuilds `PATCH.DAT` on your PC from your own copy of the game, plus the mod's own new content. To read the game's packed files Setup uses the decompressor inside your own copy of the game's exe (it doesn't modify the exe). Every rebuilt piece is checked, and the finished file must match a checksum exactly; if anything differs, Setup deletes the half-built file and changes nothing.
8. **Copies the mod's files** (see the list below) and records every file it added or replaced, with checksums, in `LCUMod\install-manifest.tsv`.
9. **Registers the uninstaller** in Windows (Settings → Apps), just for your user account.

Setup does not need administrator rights when your Steam folder is writable (it normally is). If it isn't, Setup asks for permission once.

### Every file and folder Setup adds

In the game folder (`...\steamapps\common\LEGO City Undercover\`):

| Added | Part | What it is |
|---|---|---|
| `PATCH.DAT` | core | the mod's world content, rebuilt from your game files (step 7) |
| `dinput8.dll` | core | a small loader: the game loads it, it loads the mod. It passes everything else on to Windows' own `dinput8.dll` |
| `LCUMod\` | core | the mod: `lcudrive.dll`, its settings file `lcudrive.ini`, data files, the street voices (`npc_vo\` is a copy of voice files inside PATCH.DAT), the uninstaller, the install manifest and logs |
| `LCUMod\Radio\` | radio | the radio stations |
| `d3d11.dll`, `d3dx.ini` | graphics | 3Dmigoto 1.3.16, the shader tool the graphics pack runs on |
| `ShaderFixes\` | graphics | the graphics pack's shaders |
| `Mods\Rain\`, `Mods\RadioLogo\` | graphics | textures for the rain and the radio's station logos |

Setup changes **none** of the game's own files: not the exe, not `GAME*.DAT`, not your saves. ReShade is **not** included.

---

## Turning things off

- **In the game:** pause → **Mod Options**. Every feature has a switch there. **Advanced → Switch Off Until Restart** turns the whole mod off until you restart the game.
- **By hand:** `LCUMod\lcudrive.ini` (a text file, every setting explained in it). Press Ctrl+Alt+R in the game to reload it.
- **Without the graphics pack:** run Setup again (**Update / Repair**) and untick it.

## Uninstalling

Any of these does the same thing:
- Windows **Settings → Apps → LegoCity Expanded → Uninstall**, or
- run `LCUMod\Uninstall.exe` in the game folder, or
- run `Setup.exe` and press **Uninstall**.

The uninstaller removes exactly the files listed in `LCUMod\install-manifest.tsv` (and the log / cache files the mod and 3Dmigoto create while the game runs), puts back anything Setup moved to `LCUMod_Backup`, and removes its Windows entry. It never touches `GAME*.DAT` or your saves. Afterwards the game folder is as it was before.

**Updating:** run the new version's `Setup.exe` over the old one. It updates the files, keeps your earlier settings as `LCUMod\lcudrive.ini.old`, and only rebuilds `PATCH.DAT` when it changed.

---

## The "Windows protected your PC" warning

Setup isn't code-signed (signing certificates cost money, and this is a free hobby project), so Windows SmartScreen may show **"Windows protected your PC - Microsoft Defender SmartScreen prevented an unrecognized app from starting"** the first time. To run it, click **More info**, then **Run anyway**. If you'd rather check the files first, compare them with `SHA256SUMS.txt` (PowerShell: `Get-FileHash Setup.exe`). The full source code of Setup and the mod is published with the release.

Some antivirus programs are wary of `dinput8.dll` / `d3d11.dll` loaders in game folders, because that's how game mods (and 3Dmigoto, and ReShade) hook into a game. That's what these files do here, and nothing else.

## Known issues - please read

This is a hobby project, tested by one family on one PC. Be kind to it:
- **Some of the newest areas have hardly been played yet**: the roads and villages of the Wilds, Saltwind Downs, the castles and the newest map labels. Expect rough edges: steep banks, odd collisions, places you can fall through, trees in odd spots.
- **Heavy areas.** The new areas use a lot of memory. On PCs with little RAM the game can stutter or crash while loading them.
- **The first start after installing is slow**, while 3Dmigoto prepares the shaders. Later starts are faster.
- **3D Rain (Experimental)** wakes up a rain effect the game shipped but never used. If it looks wrong, switch it off (Mod Options → Lights and Weather).
- The radio's menu text says "six stations"; there are more.
- Controllers: the mod's extra buttons follow the controllers set under Mod Options → Radio and Co-op. If a button does nothing, check there first.

## Troubleshooting

- **The game doesn't start, or crashes at once:** uninstall (see above) and check the game starts without the mod. Then reinstall without the graphics pack to see whether that's the part that clashes (another overlay, an old ReShade, or a graphics tool can).
- **Setup says the game version isn't supported:** the mod only works with the exact Steam version. If Steam updated the game, a new release of the mod is needed.
- **Setup says GAME.DAT (or another GAME file) isn't stock:** in Steam, right-click the game → Properties → Installed Files → **Verify integrity of game files**, then run Setup again.
- **Something else:** please report it with these files (they contain the paths on your PC, so look them over first):
  - the Setup log: `LCUMod\setup.log` in the game folder (or `%TEMP%\LCUMod-setup.log` if the install didn't finish),
  - the mod's log: `LCUMod\lcudrive.log` (copy it before you start the game again),
  - after a crash, the newest `LEGOLCUR_DX11.exe.*.dmp` in `%LOCALAPPDATA%\CrashDumps`, if Windows wrote one,
  - and what you did just before it went wrong.

---

## Licences and credits

- The mod's own code is under the **MIT licence** (`LICENSE`).
- MIT does **not** cover the game's own files: none are in the download - `PATCH.DAT` is rebuilt from your own copy.
- The LEGO set designs used for some buildings come from the LDraw.org model repository under **CCAL 2.0**, and LDraw parts under their own licence - credited in `CREDITS.md`.
- 3Dmigoto 1.3.16 (`d3d11.dll`, graphics pack) is GPL v3, shipped unchanged; its licence and a link to its source are in `licenses\` and `CREDITS.md`.
- The radio audio, the street voices and the mod's own world content (new places, data, settings): Licensed under **CC BY-NC 4.0** (https://creativecommons.org/licenses/by-nc/4.0/): share and remix it with credit, not for commercial use.
- Full credits: `CREDITS.md`.

## Disclaimer

LegoCity Expanded is a free, non-commercial fan project. It is not affiliated with, endorsed, sponsored or approved by the LEGO Group, TT Games or Warner Bros. Games. LEGO, the LEGO logo and the Minifigure are trademarks of the LEGO Group. LEGO City Undercover is a trademark of its respective owners. You need your own copy of the game; this mod contains none of its files.
