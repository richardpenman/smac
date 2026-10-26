// Terraforming and unit actions (original module around 0x4C9420-0x4CF7C0).
//
// The action_* functions carry out unit orders (terraforming, founding bases, bombardment,
// air drops, payload missiles, ...). Callers are the AI (enemy_move, alien_move, air_power,
// enemy_veh), the console (human orders) and the multiplayer daemon.
#pragma once
#include "engine/globals.h"

// ---- action module ------------------------------------------------------------------------------

// AI cost estimate of terraforming the altitude at (x, y) for faction_id (1..30000).
ADDR(0x4C9420) int32_t GAMEAPI terraform_cost(int32_t x, int32_t y, int32_t faction_id);
// Founds a base with colony pod veh_id (named `name` unless null/empty). Returns the base id, or
// -1 (the pod skips its turn).
ADDR(0x4C96E0) int32_t GAMEAPI action_build(int32_t veh_id, const char* name);
// 0x4C99D0 (no callers): turns needed by a former (doubled rate) to build a road or magtube at
// (x, y), as action_terraform computes for road items.
ADDR(0x4C99D0) int32_t GAMEAPI road_rate(int32_t x, int32_t y);
// Terraforming progress per turn of a former on a terraform item.
ADDR(0x4C9A50) int32_t GAMEAPI contribution(int32_t veh_id, int32_t terraform_id);
// Works on terraform item terraform_id with former veh_id. is_action 0: returns the number of
// turns still needed (with the stack's other formers on the same order). Otherwise adds this
// turn's work; returns 1 when the item was completed (0 otherwise).
ADDR(0x4C9B00) int32_t GAMEAPI action_terraform(int32_t veh_id, int32_t terraform_id, int32_t is_action);
// Nerve staples base_id (may fail after the second time, always after the eighth): atrocity.
ADDR(0x4CA7F0) void GAMEAPI action_staple(int32_t base_id);
// Destroys terrain improvements at (x, y) (bombardment / artillery if in range, else the unit's
// own tile): items (<= 0: any) are removed one at a time in Terraform order.
ADDR(0x4CAA50) void GAMEAPI action_destroy(int32_t veh_id, int32_t items, int32_t x, int32_t y);
// One step of a go-to order (orders 24, 25, 88).
ADDR(0x4CB310) void GAMEAPI action_go_to(int32_t veh_id);
// One step of a road-to / tube-to order (orders 27, 28).
ADDR(0x4CB580) void GAMEAPI action_road_to(int32_t veh_id);
// Sends veh_id to the nearest friendly base (or air base / carrier for air units). mode bit 0
// and 4: only when out of range; < 0: test only. Returns 1 if a destination was found.
ADDR(0x4CBAA0) int32_t GAMEAPI action_home(int32_t veh_id, int32_t mode);
// Air drop of veh_id to (x, y). flags bit 0: messages to a human player, bit 1: execute (else
// test). Returns 1 if the drop is (was) possible.
ADDR(0x4CC360) int32_t GAMEAPI action_airdrop(int32_t veh_id, int32_t x, int32_t y, int32_t flags);
// Multiplayer: "feature not available" message.
ADDR(0x4CD060) void GAMEAPI action_feature();
// Artillery fire of veh_id at (x, y) (human order).
ADDR(0x4CD090) void GAMEAPI action_arty(int32_t veh_id, int32_t x, int32_t y);
// Self-destruct of veh_id: damages the vehicles around (bases excepted) by its reactor and weapon.
ADDR(0x4CD2F0) void GAMEAPI action_destruct(int32_t veh_id);
// Obliterates base_id with veh_id's faction (atrocity if the rules say so).
ADDR(0x4CD4E0) void GAMEAPI action_oblit(int32_t veh_id, int32_t base_id);
// Whether veh_id may patrol to (x, y).
ADDR(0x4CD6A0) int32_t GAMEAPI valid_patrol(int32_t veh_id, int32_t x, int32_t y);
// Adds a patrol waypoint (x, y) to veh_id's orders.
ADDR(0x4CDA30) void GAMEAPI action_patrol(int32_t veh_id, int32_t x, int32_t y);
// Orbital defence (and flechette defence of bases near (x, y)) of faction_id_def against a
// payload missile of faction_id_atk. Returns 1 if the missile was shot down.
ADDR(0x4CDAF0)
int32_t GAMEAPI shoot_it(int32_t faction_id_atk, int32_t faction_id_def, int32_t x, int32_t y, int32_t is_fungal);
// Tectonic payload of veh_id detonating at (x, y).
ADDR(0x4CE210) void GAMEAPI action_tectonic(int32_t veh_id, int32_t x, int32_t y);
// 0x4CE6B0 (no callers): plants fungus at (x, y) as action_fungal does for each tile.
ADDR(0x4CE6B0) void GAMEAPI fungal_tile(int32_t x, int32_t y);
// Fungal payload of veh_id detonating at (x, y).
ADDR(0x4CE790) void GAMEAPI action_fungal(int32_t veh_id, int32_t x, int32_t y);
// Gives veh_id (and the units it carries) to faction_id.
ADDR(0x4CEDE0) void GAMEAPI action_give(int32_t veh_id, int32_t faction_id);
// Moves veh_id through the Psi Gate to base_id.
ADDR(0x4CF380) void GAMEAPI action_gate(int32_t veh_id, int32_t base_id);
// Satellite attack of faction_id's orbital defence on faction_id_def's satellite type (0..2
// satellites, 4: Orbital Defense Pod of base_id).
ADDR(0x4CF480)
void GAMEAPI action_sat_attack(int32_t faction_id, int32_t faction_id_def, int32_t type, int32_t base_id);
// Carries out veh_id's current order (terraforming, go to, road to). Returns 0 if it has none.
ADDR(0x4CF740) int32_t GAMEAPI action(int32_t veh_id);

// ---- monuments (UI module 0x476B70; game state) -------------------------------------------------

// Records the "colony founded" monument of faction_id with the base name.
ADDR(0x476B70) void GAMEAPI mon_colony_founded(int32_t faction_id, const char* base_name);

// ---- callees of other modules (not reconstructed yet) ---------------------------------------------

// map (multiplayer): sends veh_id's state to the other players.
ADDR(0x593220) void GAMEAPI synch_veh(int32_t veh_id);
ADDR(0x592E10) void GAMEAPI message_veh(int32_t type, int32_t veh_id, int32_t a3, int32_t a4);
// net: multiplayer locks and remote execution (NetDaemon object NetState).
ADDR(0x531020)
int32_t THISCALL NetDaemon_lock_veh(uint8_t* net, int32_t* veh_id, int32_t a2, int32_t a3, int32_t a4, int32_t a5);
ADDR(0x5310F0) void THISCALL NetDaemon_unlock_veh(uint8_t* net);
ADDR(0x531300) void THISCALL NetDaemon_await_exec(uint8_t* net, int32_t a1);
ADDR(0x531480) void THISCALL NetDaemon_await_synch(uint8_t* net);
ADDR(0x531700)
int32_t THISCALL
    NetDaemon_maybe_lock(uint8_t* net, int32_t* veh_id, int32_t a2, int32_t a3, int32_t a4, int32_t a5, int32_t a6);
ADDR(0x531780) int32_t THISCALL NetDaemon_order_veh(uint8_t* net, int32_t veh_id, int32_t dir, int32_t a3);
// diplo_act
ADDR(0x50BCC0) void GAMEAPI double_cross(int32_t faction_id, int32_t faction_id_with, int32_t a3);
// Breaks any treaty with faction_id_with (double cross) and declares vendetta; 1 if a treaty was broken.
ADDR(0x50C2E0) int32_t GAMEAPI act_of_aggression(int32_t faction_id, int32_t faction_id_with);
ADDR(0x50C510) void GAMEAPI capture_base(int32_t base_id, int32_t faction_id, int32_t is_probe);
ADDR(0x50DE50) int32_t GAMEAPI base_on_sea(int32_t base_id, int32_t region);
ADDR(0x50E160) int32_t GAMEAPI port_to_port(int32_t base_id, int32_t base_id_2);
// diplomacy
// Adds amount to faction_id's friction with faction_id_with (0..20).
ADDR(0x53A030) void GAMEAPI cause_friction(int32_t faction_id, int32_t faction_id_with, int32_t amount);
ADDR(0x55EEE0) void GAMEAPI atrocity(int32_t faction_id, int32_t faction_id_vs, int32_t type, int32_t a4);
ADDR(0x55F770) int32_t GAMEAPI break_treaty(int32_t faction_id, int32_t faction_id_with, int32_t a3);

// ---- globals --------------------------------------------------------------------------------------

// TutWinMapState per terrain kind ([0] land, [1] sea): terraform tutorial messages already shown.
GLOBAL(int32_t[2], TutWinMapStates, 0x9A64B4);
