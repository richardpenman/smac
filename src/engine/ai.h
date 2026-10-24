// Computer player strategy and unit movement (original module around 0x55F930-0x579770).
#pragma once
#include "engine/globals.h"

// Moves the AI units of faction_id for the turn, in ten passes by unit kind.
ADDR(0x579510) void GAMEAPI enemy_turn(int32_t faction_id);
// AI diplomacy of faction_id for the turn (treaties, demands, alliances).
ADDR(0x55F930) void GAMEAPI enemy_diplomacy(int32_t faction_id);
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
// AI strategy of faction_id for the turn: region plans, goals and production priorities.
ADDR(0x561080) void GAMEAPI enemy_strategy(int32_t faction_id);

// ---- AI strategy part 1 (enemy_move itself: see ai_move.cpp) -----------------------------------

// Harness hooks (not original): enemy_turn -> enemy_veh, and enemy_veh's message pump
// (do_all_non_input at 0x57928D). The oracle poisons the stack at these call sites.
extern int32_t (*g_harness_enemy_veh)(int32_t veh_id);
extern void (*g_harness_enemy_veh_pump)();

// AI move of one unit: 1 if the unit is done for the turn.
ADDR(0x579240) int32_t GAMEAPI enemy_veh(int32_t veh_id);
// AI move of one unit (strategy core; reconstructed separately).
ADDR(0x56B5B0) int32_t GAMEAPI enemy_move(int32_t veh_id);
// Orders veh_id to move to (x, y) with the AI order type.
ADDR(0x560AD0) void GAMEAPI go_to(int32_t veh_id, int32_t type, int32_t x, int32_t y);
// Orders a sea unit towards (x, y): there directly, or to the reachable ocean tile within radius 2
// of a land target that borders most free tiles of the target's region.
ADDR(0x564890) void GAMEAPI set_course(int32_t veh_id, int32_t type, int32_t x, int32_t y);
// Base gathering passengers for a transport: sets goals for colony pods / combat units.
ADDR(0x564B90) void GAMEAPI assemble_passengers(int32_t base_id, int32_t plan, int32_t priority);
// Whether a supply crawler may convoy from (x, y): no other convoy there, not a worked base tile.
ADDR(0x564D90) int32_t GAMEAPI can_convoy(int32_t x, int32_t y);
// Whether (x, y) is a good sensor site for faction_id.
ADDR(0x564EB0) int32_t GAMEAPI good_sensor(int32_t faction_id, int32_t x, int32_t y);
// FUN_00565160 (no callers): energy bonus tile, or next to an own/pact base with ocean access.
ADDR(0x565160) int32_t GAMEAPI ocean_access_near_base(int32_t faction_id, int32_t x, int32_t y);
// AI attack odds adjustment: odds scaled by stack values, goals, terrain and unit kinds.
ADDR(0x565F20) int32_t GAMEAPI compute_odds(int32_t odds, int32_t faction_id, int32_t veh_id, int32_t veh_id_tgt,
                                            int32_t base_id);
// Native unit target: nearest weighted base reachable from (x, y) (sets BaseFindDist), or -1.
ADDR(0x5665D0) int32_t GAMEAPI alien_base(int32_t veh_id, int32_t x, int32_t y);
// Native (alien) unit move.
ADDR(0x566810) int32_t GAMEAPI alien_move(int32_t veh_id);
// AI move of an air unit (enemy_move: dist_own = BaseFindDist to the nearest own base, zoc =
// zoc_move at the unit; x, y: in/out target tile).
ADDR(0x5678C0) int32_t GAMEAPI air_power(int32_t veh_id, int32_t dist_own, int32_t zoc, int32_t* x, int32_t* y);
// AI move of a planet buster.
ADDR(0x56A8A0) int32_t GAMEAPI enemy_planet_buster(int32_t veh_id);
// Whether veh_id can reach (x, y) (same region, or by sea/port connections).
ADDR(0x56B320) int32_t GAMEAPI get_there(int32_t veh_id, int32_t x, int32_t y);
// For own territory at (x, y): index (1-8) of the first neighbour that is ocean or foreign, 0 none.
ADDR(0x56B480) int32_t GAMEAPI coast_or_border(int32_t x, int32_t y, int32_t x_dst, int32_t y_dst,
                                               int32_t faction_id);
