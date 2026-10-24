// Base helpers and the AI production chooser (original module base_misc, 0x4F8090-0x500120).
#pragma once
#include "engine/globals.h"

// Distance measure of the original for offsets (dx, dy): max - ((|dx| + |dy|) / 2 - min + 1) / 2.
ADDR(0x4F8090) int32_t GAMEAPI vector_dist(int32_t dx, int32_t dy);
// Whether faction_id owns secret project project_id (index into SecretProjects).
ADDR(0x4F80D0) int32_t GAMEAPI has_project(int32_t project_id, int32_t faction_id);
// Whether item_id is the choice of an AI priority (BasePriorityChoice) with at least that score,
// for the priorities whose skip flag is 0.
ADDR(0x4F8110) int32_t GAMEAPI base_claimed(int32_t item_id, int32_t score, int32_t skip_0, int32_t skip_1,
                                            int32_t skip_2, int32_t skip_3);
// AI production choice for a base (base_id < 0: -1 - base_id, see the definition). The optional
// outputs receive extra choice information.
ADDR(0x4F81A0) int32_t GAMEAPI base_build(int32_t base_id, int32_t* a2, int32_t* a3, int32_t* score);
