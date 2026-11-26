# Credits and licences - LegoCity Expanded {VERSION}

## The mod
- Code, world building, settings and tools: Victor Knai. MIT licence (`LICENSE`) for the mod's own code.
- Radio voices and music were generated with ElevenLabs under the author's paid plan.
- Built with the help of modern tools, AI included.
- The radio audio, the street voices and the mod's own world content (new places, data, settings): Licensed under **CC BY-NC 4.0** (https://creativecommons.org/licenses/by-nc/4.0/): share and remix it with credit, not for commercial use.

## What the MIT licence does not cover
- **The game's own files.** None are included. `PATCH.DAT` is rebuilt by Setup from the player's own copy of LEGO City Undercover; the download only carries a recipe (references into the player's game files) plus the mod's own new content.
- **The graphics pack's shaders** (`ShaderFixes\*_replace.txt`) are the game's shaders as decompiled by 3Dmigoto, with this mod's changes (time of day, lights, fog, rain). Shipping patched decompiled shaders is common practice for 3Dmigoto fixes; they only work with the game they came from.
- **LDraw models and parts** (below): CCAL 2.0 / CC BY, their authors' copyright.
- **3Dmigoto** (below): GPL v3, shipped unchanged.

## LEGO set models (LDraw.org Official Model Repository, CCAL 2.0)
Some buildings are adaptations of LDraw models of official LEGO sets, built from real parts at true brick size and changed for the game. Each model file states "Redistributable under CCAL version 2.0". Models and authors:

{LDRAW_CREDITS}

## LDraw parts library
Brick geometry comes from the LDraw parts library (www.ldraw.org), by the LDraw contributors, used under the LDraw Contributor Agreement licences (CC BY 2.0 / CC BY 4.0). The licence texts are included as `licenses\LDraw-CAlicense.txt` and `licenses\LDraw-CAlicense4.txt`.

## 3Dmigoto 1.3.16
`d3d11.dll` and the shader loading are 3Dmigoto (https://github.com/bo3b/3Dmigoto) by bo3b, Chiri, DarkStarSword and the 3Dmigoto contributors, shipped unchanged (version 1.3.16). 3Dmigoto is free software under the **GNU General Public License version 3**: its notice is `licenses\3Dmigoto-COPYING.txt` and the licence text `licenses\3Dmigoto-LICENSE.GPL.txt`. Its complete source code for this version is at https://github.com/bo3b/3Dmigoto/tree/1.3.16. The mod does not link against 3Dmigoto: the game loads `d3d11.dll` as a separate program file, and the mod's own code keeps its MIT licence.

## Not included
- ReShade is not part of this release (the mod no longer uses it).

## Trademarks
LEGO, the LEGO logo and the Minifigure are trademarks of the LEGO Group. LEGO City Undercover is a trademark of its respective owners. This mod is not affiliated with, endorsed, sponsored or approved by the LEGO Group, TT Games or Warner Bros. Games.
