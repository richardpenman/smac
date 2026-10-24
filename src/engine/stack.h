// Spotting and unit stacks (original module around 0x5B5700-0x5B9C40).
#pragma once
#include "engine/globals.h"

// 0x5B5700: faction_id sees tile (x, y) (visibility bit, view of the tile items); marks the tile
// for redraw when the viewer discovers it.
ADDR(0x5B5700) void GAMEAPI spot_tile(int32_t x, int32_t y, int32_t faction_id);
// faction_id sees a base (and its population).
ADDR(0x5B57D0) void GAMEAPI spot_base(int32_t base_id, int32_t faction_id);
// faction_id sees the stack of veh_id and its tile.
ADDR(0x5B58E0) void GAMEAPI spot_stack(int32_t veh_id, int32_t faction_id);
// Nobody sees the stack of veh_id any more.
ADDR(0x5B5A70) void GAMEAPI unspot_stack(int32_t veh_id);
// faction_id sees tile (x, y) and the base or stack on it.
ADDR(0x5B5AD0) void GAMEAPI spot_loc(int32_t x, int32_t y, int32_t faction_id);
// 0x5B5E10: ends the alert/patrol states of a vehicle that spotted an enemy (go-to orders end).
ADDR(0x5B5E10) void GAMEAPI veh_alert_wake(int32_t veh_id);
// Whether veh_id (seeing spotted_veh_id of faction_id) should wake up, by diplomatic status and
// the alert preferences. Returns non-zero to wake.
ADDR(0x5B5EA0) uint32_t GAMEAPI want_to_wake(int32_t faction_id, int32_t veh_id, int32_t spotted_veh_id);
// Wakes the sentried units of a stack (except land units held in air transports outside bases).
ADDR(0x5B6060) void GAMEAPI wake_stack(int32_t veh_id);
// 0x5B61A0: stealth of a vehicle: 1 submerged/cloaked/invisible, 2 hidden in fungus, 0 visible.
ADDR(0x5B61A0) int32_t GAMEAPI veh_stealth(int32_t veh_id);
// Spotting after a vehicle moved or appeared: what it sees, who sees it, encounters, AI goals,
// waking of alert units. flag: 0 skips goals/waking/encounters in multiplayer.
ADDR(0x5B6260) void GAMEAPI spot_all(int32_t veh_id, int32_t flag);
// Moves the whole stack of veh_id to (x, y).
ADDR(0x5B8AF0) void GAMEAPI stack_put(int32_t veh_id, int32_t x, int32_t y);
// Moves the transports and carriers of a stack to its top.
ADDR(0x5B8B60) void GAMEAPI stack_sort(int32_t veh_id);
// Moves colony pods and combat units of a stack to its top.
ADDR(0x5B8C90) void GAMEAPI stack_sort_2(int32_t veh_id);
// Multiplayer: rebuilds the order of a stack. Returns the top of the stack of veh_id.
ADDR(0x5B8E10) int32_t GAMEAPI stack_fix(int32_t veh_id);
// Loads units into a transport (mode 0: marks the passengers, returns the remaining capacity;
// mode 1/2: moves transport and passengers off map to (-2, -2), returns veh_id).
ADDR(0x5B8EE0) int32_t GAMEAPI stack_veh(int32_t veh_id, int32_t mode);
// Kills every vehicle of the stack of veh_id.
ADDR(0x5B9510) void GAMEAPI stack_kill(int32_t veh_id);
// Counts or sums a property over the stack of veh_id (type 0-19, conditions per type).
ADDR(0x5B9580) int32_t GAMEAPI stack_check(int32_t veh_id, int32_t type, int32_t cond1, int32_t cond2, int32_t cond3);
