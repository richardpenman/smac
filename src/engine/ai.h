// Computer player strategy and unit movement (original module around 0x55F930-0x579770).
#pragma once
#include "engine/globals.h"

ADDR(0x579510) void GAMEAPI enemy_turn(int32_t faction_id);
// Refreshes the faction's best/enemy-best weapon, armor and speed values.
ADDR(0x560DD0) void GAMEAPI enemy_capabilities(int32_t faction_id);
// Number of defenders a base wants (AI garrison goal).
ADDR(0x560B30) int32_t GAMEAPI garrison_check(int32_t base_id);
// Number of defensive units a base wants.
ADDR(0x560D30) int32_t GAMEAPI defensive_check(int32_t base_id);
// Number of offensive units faction_id wants in a region for guard duty.
ADDR(0x560D50) int32_t GAMEAPI guard_check(int32_t faction_id, int32_t region);
// AI: the former order (ORDER_* terraform id) worth doing at (x, y) for base_id's needs, or 0.
ADDR(0x565320) int32_t GAMEAPI can_terraform(int32_t faction_id, int32_t x, int32_t y, int32_t flag,
                                             int32_t base_id);
