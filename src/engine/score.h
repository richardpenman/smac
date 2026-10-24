// Objectives, score and replay (original module around 0x5ABFF0-0x5AE120).
#pragma once
#include "engine/globals.h"

// Score scaled by the difficulty rating of faction_id (end game screen).
ADDR(0x5ABFF0) int32_t GAMEAPI get_rating(int32_t faction_id, int32_t score);
// Whether the base is a scenario objective.
ADDR(0x5AC060) int32_t GAMEAPI is_objective(int32_t base_id);
// Scenario objective points of faction_id (with incl_pact and cooperative victory: plus those of
// its pact partners). 0 for a dead faction.
ADDR(0x5AC110) int32_t GAMEAPI num_objectives(int32_t faction_id, int32_t incl_pact);
// Most objective points of any faction; winner_id receives the (last) best faction, tied_count the
// number of other factions with as many points (pact partners excluded with cooperative victory).
ADDR(0x5AC5A0) int32_t GAMEAPI most_objectives(int32_t* winner_id, int32_t* tied_count);
// Whether faction_id is building the Ascent to Transcendence (see the BUG note).
ADDR(0x5AC630) int32_t GAMEAPI transcending(int32_t faction_id);
// Ranking scores of all factions (FactionRankingsUnk, FactionTurnMight); with full also the
// rankings, the human leader/trailer, and AI betrayal plans against a runaway leader.
ADDR(0x5AC690) void GAMEAPI rankings(int32_t full);
// Score of faction_id: total into score and the nine categories into table (both default to the
// faction's own fields); is_query skips the stored final score and the highest-score check.
ADDR(0x5ACBE0) void GAMEAPI compute_score(int32_t faction_id, int32_t* score, int32_t* table, int32_t is_query);
// Records a base event (0 founded, 1 captured, 2 destroyed) for the replay.
ADDR(0x5ADE80) void GAMEAPI replay_base(int32_t event, int32_t x, int32_t y, int32_t faction_id);
// Whether the Voice of Planet has been built (the Ascent to Transcendence can be completed).
// Callers push an argument the function ignores.
ADDR(0x5AC680) int32_t GAMEAPI ascending();
