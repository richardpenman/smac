// Unit movement: terrain move costs, ZOC helpers, supply options and unit orders (original module
// around 0x593510-0x59A1B0).
#pragma once
#include "engine/globals.h"

// Movement points (road moves) a unit of faction_id spends moving from (x1, y1) to the adjacent
// (x2, y2). faction_id 0 (natives) ignores roads/tubes/rivers. ignore_terrain: no rough-terrain
// extras (unit speed equal to a road move, see Path_find).
ADDR(0x593510)
int32_t GAMEAPI hex_cost(int32_t unit_id, int32_t faction_id, int32_t x1, int32_t y1, int32_t x2, int32_t y2,
                         int32_t ignore_terrain);
// Neighbour of (x, y) holding units of a faction without a pact with faction_id (same land/sea
// type) that is farthest from (x2, y2): written to *x_out, *y_out (unchanged if none).
ADDR(0x593830)
void GAMEAPI
    quick_zoc(int32_t x, int32_t y, int32_t faction_id, int32_t x2, int32_t y2, int32_t* x_out, int32_t* y_out);
// A supply crawler of a human player enters a (friendly) base: asks whether to help build the
// current production (the crawler is consumed: returns 1) or to convoy resources (order 3: returns
// 0). AI factions: 0. With VehAttackFlags bit 0 (an interactive move) and not bit 1, returns 1
// once the choice is made.
ADDR(0x593A00) int32_t GAMEAPI supply_options(int32_t veh_id, int32_t base_id);
// Moves veh_id one step in direction dir (0-7; -1 or 8: end of move, no step), with everything
// that happens on the way: zones of control, attacks (battle_fight), base capture, probe actions,
// native life, supply pods, monoliths, artifacts, aircraft fuel. flags (VehAttackFlags): bit 0 an
// order given interactively, bit 1 the move is carried out (else only checked: attack odds etc.).
// Returns non-zero if the unit moved and may go on.
ADDR(0x5947C0) int32_t GAMEAPI order_veh(int32_t veh_id, int32_t dir, int32_t flags);
// Probe team action of veh_id against a base (base_id) or a unit stack (probe module, 0x59F120).
ADDR(0x59F120) int32_t GAMEAPI probe(int32_t veh_id, int32_t base_id, int32_t stack_veh_id, int32_t is_interactive);
// order_veh's tutorial counters (shown every fourth time) and tutorial switch.
GLOBAL(int32_t, TutZocMessageCount, 0x945AF4);
GLOBAL(int32_t, TutSlowFungusCount, 0x945AF8);
GLOBAL(int32_t, TutDisabledFlag, 0x8CC210);
