// Goals, sites, monoliths, supply pods (original module around 0x579770-0x57D1F0).
#pragma once
#include "engine/globals.h"

// Top of the stack containing veh_id, or -1.
ADDR(0x579920) int32_t GAMEAPI veh_top(int32_t veh_id);
// Adds an AI goal of a type and priority for a faction at (x, y).
ADDR(0x579A30) void GAMEAPI add_goal(int32_t faction_id, int32_t type, int32_t priority, int32_t x, int32_t y,
                                     int32_t base_id);
// Removes faction_id's AI sites of a type within distance of (x, y).
ADDR(0x579E70) void GAMEAPI del_site(int32_t faction_id, int32_t type, int32_t x, int32_t y, int32_t distance);
// Whether faction_id has an AI goal of a type at (x, y).
ADDR(0x579CC0) int32_t GAMEAPI at_goal(int32_t faction_id, int32_t type, int32_t x, int32_t y);
