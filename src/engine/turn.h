// Turn processing (original module around 0x51F2E0-0x52DBA0).
#pragma once
#include "engine/globals.h"

ADDR(0x524870) int32_t GAMEAPI end_of_game(int32_t check_only);
ADDR(0x523DD0) void GAMEAPI reset_territory();
ADDR(0x5258C0) void GAMEAPI turn_upkeep();
ADDR(0x527290) void GAMEAPI faction_upkeep(int32_t faction_id);
ADDR(0x5275B0) void GAMEAPI control_turn();

// Harness hook (not in the original): called by control_turn immediately before each call to
// turn_upkeep. The differential harness uses it to snapshot state and stop after N turns.
extern void (*g_harness_before_turn_upkeep)();
// Story interlude / movie (announcements to the viewer).
ADDR(0x5230E0) void GAMEAPI interlude(int32_t id, int32_t a2, int32_t a3, int32_t a4);
// Number of faction_id's subspace generators (built and working); out receives extra counts.
ADDR(0x5247B0) int32_t GAMEAPI generators(int32_t faction_id, int32_t* out);
