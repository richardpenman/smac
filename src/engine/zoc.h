// Game year and zones of control (original module zoc, 0x5C89A0-0x5C8DA0). The rest of the
// address range (0x5C8DA0-0x5C9860) is a UI window class (gamma/palette settings).
#pragma once
#include "engine/globals.h"

// Mission year of a turn.
ADDR(0x5C89A0) int32_t GAMEAPI game_year(int32_t turn);
// Appends the current mission year to buffer.
ADDR(0x5C89B0) void GAMEAPI say_year(char* buffer);
// Owner + 1 of the first non-pact base or unit adjacent to (x, y), or 0.
ADDR(0x5C89F0) int32_t GAMEAPI zoc_any(int32_t x, int32_t y, int32_t faction_id);
// Highest owner + 1 of the non-pact units adjacent to (x, y), or 0.
ADDR(0x5C8AC0) int32_t GAMEAPI zoc_veh(int32_t x, int32_t y, int32_t faction_id);
// Owner + 1 of an adjacent non-pact unit on the same kind of terrain (land/sea) that faction_id
// can see, or 0.
ADDR(0x5C8BA0) int32_t GAMEAPI zoc_sea(int32_t x, int32_t y, int32_t faction_id);
// Zone of control for a move to (x, y): 0 in bases, else zoc_sea.
ADDR(0x5C8D40) int32_t GAMEAPI zoc_move(int32_t x, int32_t y, int32_t faction_id);
