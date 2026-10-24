// Objectives, score and replay (original module around 0x5ABFF0-0x5AE120).
#pragma once
#include "engine/globals.h"

ADDR(0x5AC060) int32_t GAMEAPI is_objective(int32_t base_id);
// Records a base event (0 founded, 1 captured, 2 destroyed) for the replay.
ADDR(0x5ADE80) void GAMEAPI replay_base(int32_t event, int32_t x, int32_t y, int32_t faction_id);
// Whether the Voice of Planet has been built (the Ascent to Transcendence can be completed).
// Callers push an argument the function ignores.
ADDR(0x5AC680) int32_t GAMEAPI ascending();
