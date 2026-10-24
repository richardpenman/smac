// New game setup (original module around 0x5AE120-0x5B4210).
#pragma once
#include "engine/globals.h"

ADDR(0x5B3920) void GAMEAPI setup_game(int32_t reload);
// Scenario objectives and mission years back to the rules' defaults (older savegames).
ADDR(0x5B38D0) void GAMEAPI clear_scenario();
// Recomputes the faction's base social modifiers (SE_*_base) from its SOCIAL faction bonuses.
ADDR(0x5B0D70) void GAMEAPI compute_faction_modifiers(int32_t faction_id);
// Removes a faction that lost its last base/unit (or by request); returns non-zero when eliminated.
ADDR(0x5B3380) int32_t GAMEAPI eliminate_player(int32_t faction_id, int32_t flag);
