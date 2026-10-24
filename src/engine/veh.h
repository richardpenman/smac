// Vehicles (original module around 0x5BED20-0x5C2020).
//
// Vehicles of one tile form a doubly linked stack (next_veh_id_stack / prev_veh_id_stack); veh_at
// returns its top (no previous vehicle). The X_pop* popup wrappers in this address range are UI
// (see ui_bridge.h); fractal_init/fractal_at at its end belong to world generation (world.h).
#pragma once
#include "engine/globals.h"

// ---- helpers (not original functions) -----------------------------------------------------------

inline int32_t unit_triad(int32_t unit_id) { return Chassis[Units[unit_id].chassis_id].triad; }
inline int32_t veh_triad(int32_t veh_id) { return unit_triad(Vehs[veh_id].unit_id); }
// Psi weapon (negative offense value).
inline bool unit_psi_weapon(int32_t unit_id) { return Weapon[Units[unit_id].weapon_id].offense_value < 0; }
inline bool unit_psi_armor(int32_t unit_id) { return Armor[Units[unit_id].armor_id].defense_value < 0; }
// Native life form: a basic unit with a psi weapon, or the Spore Launcher (basic unit 15).
inline bool unit_is_native(int32_t unit_id) { return unit_id < 64 && (unit_psi_weapon(unit_id) || unit_id == 15); }
// Whether a base (any id: the original indexes Bases[] without a range check) has a facility.
inline bool base_has_fac(int32_t base_id, int32_t facility_id) {
    const uint8_t* built = reinterpret_cast<const uint8_t*>(&Bases[0]) + base_id * (int32_t)sizeof(BASE) +
                           offsetof(BASE, facilities_built);
    return (built[facility_id / 8] & (1 << (facility_id & 7))) != 0;
}
// Faction's diplomatic status with another faction. The original indexes the table without a range
// check (other_id -1 reads the preceding field, faction_id -1 the memory before Factions[0]).
inline uint32_t diplo_status_raw(int32_t faction_id, int32_t other_id) {
    const uint8_t* p = reinterpret_cast<const uint8_t*>(&Factions[0].diplo_status[0]) +
                       faction_id * (int32_t)sizeof(Faction) + other_id * 4;
    return *reinterpret_cast<const uint32_t*>(p);
}
// Top of the stack containing veh_id (inline copy of veh_top); -1 for -1.
inline int32_t stack_top(int32_t veh_id) {
    if (veh_id < 0) return veh_id;
    while (Vehs[veh_id].prev_veh_id_stack >= 0) veh_id = Vehs[veh_id].prev_veh_id_stack;
    return veh_id;
}
// Not an original function: the distance measure the original inlines (vector_dist, 0x4F8090 is the
// called form): max(dx, dy) - ((dx + dy) / 2 - min(dx, dy) + 1) / 2 with wrapped dx.
int32_t veh_vector_dist(int32_t x1, int32_t y1, int32_t x2, int32_t y2);

// ---- veh module ---------------------------------------------------------------------------------

// Unity wreckage pod effects (landmark code at the tile): Unity rover/chopper, energy, radar.
// Returns 1 if the tile had a Unity wreck effect.
ADDR(0x5BED20) int32_t GAMEAPI do_unity_crash(int32_t x, int32_t y, int32_t faction_id);
// 1 if the tile has a sensor, 2 if the nearest base (2 tiles away on both axes) has a
// Geosynchronous Survey Pod, else 0.
ADDR(0x5BF010) int32_t GAMEAPI is_sensor(int32_t x, int32_t y);
// Whether the faction owns a visible Monolith (temple) within its territory.
ADDR(0x5BF130) int32_t GAMEAPI has_temple(int32_t faction_id);
// Whether unit_id has ability_flag: from its design, from its faction's ability bonuses (when the
// ability's tech is known), Fungal Tower-like special cases. Returns 1 or 0.
ADDR(0x5BF1F0) int32_t GAMEAPI has_abil(int32_t unit_id, uint32_t ability_flag);
// Top vehicle of the stack at (x, y), or -1. Rebuilds the tile vehicle bits on inconsistencies.
ADDR(0x5BFE90) int32_t GAMEAPI veh_at(int32_t x, int32_t y);
// Unlinks a vehicle from its stack (position -1, -1; old position in VehLiftX/Y). Returns veh_id.
ADDR(0x5BFFA0) int32_t GAMEAPI veh_lift(int32_t veh_id);
// Puts a lifted vehicle on top of the stack at (x, y) (owner and vehicle bits of the tile). Returns
// veh_id.
ADDR(0x5C0080) int32_t GAMEAPI veh_drop(int32_t veh_id, int32_t x, int32_t y);
// Sentry order (board/sleep).
ADDR(0x5C01A0) void GAMEAPI sleep(int32_t veh_id);
// Moves a vehicle to the bottom of its stack.
ADDR(0x5C01D0) void GAMEAPI veh_demote(int32_t veh_id);
// Moves a vehicle to the top of its stack.
ADDR(0x5C0260) void GAMEAPI veh_promote(int32_t veh_id);
// Initializes vehicle slot veh_id as a fresh unit of a faction (off map).
ADDR(0x5C02D0) void GAMEAPI veh_clear(int32_t veh_id, int32_t unit_id, int32_t faction_id);
// Creates a vehicle at (x, y). Returns its id or -1 if the vehicle table is full.
ADDR(0x5C03D0) int32_t GAMEAPI veh_init(int32_t unit_id, int32_t faction_id, int32_t x, int32_t y);
// Removes a vehicle from the table (later ids shift down).
ADDR(0x5C08C0) void GAMEAPI veh_kill(int32_t veh_id);
// Kills a vehicle and the units it carries, then redraws its tile.
ADDR(0x5C0B00) void GAMEAPI kill(int32_t veh_id);
// Nearest vehicle (of faction_id if >= 0, skipping veh_id_skip) to (x, y); the distance goes to
// VehFindDist (only when one is found). Returns its id or -1.
ADDR(0x5C0CB0) int32_t GAMEAPI veh_find(int32_t x, int32_t y, int32_t faction_id, int32_t veh_id_skip);
// Whether a unit can bombard (artillery); sea units return sea_triad_retn.
ADDR(0x5C0DB0) int32_t GAMEAPI can_arty(int32_t unit_id, int32_t sea_triad_retn);
// Morale level 0-6 of a vehicle (native units: morale_alien).
ADDR(0x5C0E40) int32_t GAMEAPI morale_veh(int32_t veh_id, int32_t check_drone_riot, int32_t faction_id_vs_native);
// Offense value of a unit against veh_id_def (-1: none), x8 when a defender is given.
ADDR(0x5C1150) int32_t GAMEAPI offense_proto(int32_t unit_id, int32_t veh_id_def, int32_t is_bombard);
// Defense value of a unit against veh_id_atk (-1: none), x8 when an attacker is given.
ADDR(0x5C1290) int32_t GAMEAPI armor_proto(int32_t unit_id, int32_t veh_id_atk, int32_t is_bombard);
// Movement points (road moves) of a unit design.
ADDR(0x5C13B0) int32_t GAMEAPI speed_proto(int32_t unit_id);
// Movement points of a vehicle (projects, elite morale, damage).
ADDR(0x5C1540) int32_t GAMEAPI speed(int32_t veh_id, int32_t skip_morale);
// Emulation of an original uninitialised read (not an original global): the dead stack value that
// speed() leaves where Path_find later reads its uninitialised "start tile in ZOC" flag. Non-zero
// exactly when the original would leave a non-zero value; set by speed(), read by Path_move.
// BUG(original); see docs/RECONSTRUCTION.md "Known uninitialised reads".
extern int32_t SpeedStackResidue;
// Cargo capacity of a vehicle (native transports: morale + 1).
ADDR(0x5C1760) int32_t GAMEAPI veh_cargo(int32_t veh_id);
// Prototype extra cost percentage for a unit's triad (0 for factions without the penalty).
ADDR(0x5C17D0) int32_t GAMEAPI prototype_factor(int32_t unit_id);
// Production cost of a unit at a base (-1: none); has_proto_cost receives the prototype flag.
ADDR(0x5C1850) int32_t GAMEAPI veh_cost(int32_t unit_id, int32_t base_id, int32_t* has_proto_cost);
// Whether the viewer can select the vehicle.
ADDR(0x5C1A20) int32_t GAMEAPI veh_selectable(int32_t veh_id);
// Whether the viewer's vehicle has not moved yet this turn.
ADDR(0x5C1AB0) int32_t GAMEAPI veh_unmoved(int32_t veh_id);
// Whether the viewer's vehicle is ready for orders.
ADDR(0x5C1B70) int32_t GAMEAPI veh_ready(int32_t veh_id);
// Whether a land unit is held in an air transport outside a base or airbase.
ADDR(0x5C1C40) int32_t GAMEAPI veh_jail(int32_t veh_id);
// Uses up all movement points.
ADDR(0x5C1D20) void GAMEAPI veh_skip(int32_t veh_id);
// Scratch vehicle (slot 2048) for displaying a unit design. Returns 2048.
ADDR(0x5C1D50) int32_t GAMEAPI veh_fake(int32_t unit_id, int32_t faction_id);
// Cancels orders (terraforming progress is reduced). Returns veh_id.
ADDR(0x5C1D70) int32_t GAMEAPI veh_wake(int32_t veh_id);
