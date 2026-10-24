// New game setup (original module around 0x5AE120-0x5B4210).
#pragma once
#include "engine/globals.h"

// Viewer's landing sequence on the first turn (control_turn): the drop pod animation and the
// first base name; alien and Cult viewers start at turn 5.
ADDR(0x5AE120) void GAMEAPI crash_landing(int32_t faction_id);
// First prototype of faction_id (basic units, then its own) with the given plan that it may
// build, or -1.
ADDR(0x5AED50) int32_t GAMEAPI get_plan(int32_t faction_id, int32_t plan);
// "Time warp" start (RULES_TIME_WARP): advances every faction's first base, techs and units as if
// some turns had passed.
ADDR(0x5AEDE0) void GAMEAPI time_warp();
// Compensates factions with poor starting positions: extra colony pods and supply/unity pods.
ADDR(0x5B0420) void GAMEAPI balance();
// Scenario start: moves the vehicles flagged for random placement (VFLAG_START_RAND_*).
ADDR(0x5B0A30) void GAMEAPI scenario_setup();
// Recomputes the faction's base social modifiers (SE_*_base) from its SOCIAL faction bonuses.
ADDR(0x5B0D70) void GAMEAPI compute_faction_modifiers(int32_t faction_id);
// (Re)starts a faction: resets its state and lands it (new game, late arrival, respawn after
// elimination or a freed leader with force != 0). killer_id: the faction that eliminated it,
// -1 for a new game (-2 when reloading), -282 for a faction arriving in the course of the game.
// Returns 1 if the faction landed, 0 if it was eliminated (or is not playing).
ADDR(0x5B0E00) int32_t GAMEAPI setup_player(int32_t faction_id, int32_t killer_id, int32_t force);
// Removes a faction that lost its last base/unit (or by request); returns non-zero when eliminated.
ADDR(0x5B3380) int32_t GAMEAPI eliminate_player(int32_t faction_id, int32_t killer_id);
// Scenario objectives and mission years back to the rules' defaults (older savegames).
ADDR(0x5B38D0) void GAMEAPI clear_scenario();
// New game (reload == 0) or restart of the game state (reload != 0): resets the game, sets up all
// factions, then balances the starting positions (or applies the time warp).
ADDR(0x5B3920) void GAMEAPI setup_game(int32_t reload);
