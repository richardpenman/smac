// Prototypes: cost and creation (original module proto, 0x5A59B0-0x5A66E0).
#pragma once
#include "engine/globals.h"

GLOBAL(int32_t, ProtoSortCount, 0x945B40);      // proto_sort: number of listed prototypes
GLOBAL(int32_t[128], ProtoSortList, 0x945B44);  // proto_sort: unit ids in display order

// Moves a vehicle to (x, y) (top of the stack there).
ADDR(0x5A59B0) void GAMEAPI veh_put(int32_t veh_id, int32_t x, int32_t y);
// Remaining hit points of a vehicle.
ADDR(0x5A59E0) int32_t GAMEAPI veh_health(int32_t veh_id);

// Mineral cost (rows) of a design: chassis, weapon, armor, ability flags, reactor.
ADDR(0x5A5A60) int32_t GAMEAPI proto_cost(int32_t chassis_id, int32_t weapon_id, int32_t armor_id,
                                          uint32_t ability_flags, int32_t reactor_id);
// Cost of a design without abilities.
ADDR(0x5A5D00) int32_t GAMEAPI base_cost(int32_t unit_id);
// Fills Units[unit_id] with a design and derives cost, carrying capacity, plan, group and flags.
// Faction designs (unit_id >= 64) are marked "prototyped" when an equal or better design exists.
ADDR(0x5A5D40) void GAMEAPI make_proto(int32_t unit_id, int32_t chassis_id, int32_t weapon_id, int32_t armor_id,
                                       uint32_t ability_flags, int32_t reactor_id);
// Sorts the basic and faction prototypes for the design workshop (ProtoSortList).
ADDR(0x5A6270) void GAMEAPI proto_sort(int32_t faction_id);
// Same for the prototype list of the design window (obsolete designs last).
ADDR(0x5A63D0) void GAMEAPI proto_sort_2(int32_t faction_id);
// 0x5A6510: index (0-8) of the NearOffset direction with the signs of (dx, dy), or -1.
ADDR(0x5A6510) int32_t GAMEAPI compass_index(int32_t dx, int32_t dy);
// Index of offset (dx, dy) among the first count TableOffset entries, or -1.
ADDR(0x5A65A0) int32_t GAMEAPI radius_move(int32_t dx, int32_t dy, int32_t count);
// Same for the (wrapped) offset from (x1, y1) to (x2, y2).
ADDR(0x5A65D0) int32_t GAMEAPI radius_move_2(int32_t x1, int32_t y1, int32_t x2, int32_t y2, int32_t count);
// Index (0-8) of the NearOffset direction from (x1, y1) towards (x2, y2), or -1.
ADDR(0x5A6630) int32_t GAMEAPI compass_move(int32_t x1, int32_t y1, int32_t x2, int32_t y2);
