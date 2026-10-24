// Supply pods, monoliths and alien artifacts (part of the original goals module, 0x579F80-0x57D1F0).
//
// Kept in their own file: the rest of the goals module (AI goals and sites) lives in goals.h.
#pragma once
#include "engine/globals.h"

// Whether faction_id's veh_id should visit a monolith (not yet upgraded, damaged or green).
ADDR(0x579F80) int32_t GAMEAPI want_monolith(int32_t veh_id);
// veh_id enters a monolith tile: repair, morale upgrade, monolith events for natives.
ADDR(0x57A050) void GAMEAPI monolith(int32_t veh_id);
// veh_id opens the supply pod at its tile. Returns non-zero if the vehicle is gone (killed by the
// pod's event). The original reads only veh_id; action_airdrop also pushes goody_at's result
// (kind), which is ignored.
ADDR(0x57A660) int32_t GAMEAPI goody_box(int32_t veh_id, int32_t kind);
// Alien artifact veh_id is studied at the network node of a base: a free technology. Returns 0
// (artifact kept) if the base's network node already holds an artifact, else 1.
ADDR(0x57CFA0) int32_t GAMEAPI study_artifact(int32_t veh_id);

// goals module: whether faction_id may get tech_id by a leap (only one prerequisite known).
ADDR(0x57CE50) int32_t GAMEAPI valid_tech_leap(int32_t tech_id, int32_t faction_id);
// Tutorial hint bits already shown (GamePrefs field at 0x9A64AC).
GLOBAL(int32_t, TutHintsShown, 0x9A64AC);
// UI module (0x4BA5F0), game state: whether tutorial hint `hint` should be shown now (marks it).
ADDR(0x4BA5F0) int32_t GAMEAPI tut_check(int32_t hint);
