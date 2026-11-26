// =============================================================================================
//  spawn_turn.h - whose world update this is, for the three stand spawners (airport.h's planes,
//  dino.h's T-Rexes, sites.h's stands). Each keeps "one spawn at a time, one preload outstanding"
//  among its own stands; this keeps them from calling the game's spawners in the SAME update:
//  PMThunk moves the turn on once per update (sitem::NextTurn, round the ones that are running),
//  and a spawner whose turn it is not answers its model "still streaming - again next tick"
//  without calling the game at all (no call, no preload taken, nothing to give back).
//  g_turn < 0 = not arbitrated (nothing set it): everyone may call, as before this file.
// =============================================================================================
#pragma once
#include "sites_model.h"

namespace spt {

static int g_turn = -1;
static inline bool Mine(int who) { return g_turn < 0 || g_turn == who; }

} // namespace spt
