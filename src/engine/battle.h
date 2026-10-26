// Combat (original module around 0x500120-0x50B8A0): battle odds and strengths, the battle itself
// (battle_resolve), casualties, promotions, interceptors, planet busters and their orbital defense.
//
// The battle display (BattleWin, StatusWin, sounds, the boom animation, veh_scoot) is UI and
// bridged through ui_bridge.h; battle_resolve calls it only when the battle is shown to the viewer
// (see battle_resolve).
#pragma once
#include "engine/globals.h"

// ---- globals --------------------------------------------------------------------------------------

// Terrain label of the defender's tile (a TextLabels entry) set by defense_value for the odds
// display.
GLOBAL_NATIVE(char*, VehBattleDisplayTerrain, 0x90F550);
// Planet buster strength (reactor) shown by boom's explosion (planet_busting).
GLOBAL(int32_t, PlanetBusterBoomSize, 0x90F7D4);

// ---- functions ------------------------------------------------------------------------------------

// StrBuffer = "".
ADDR(0x500120) void GAMEAPI clear();
// Appends text to StrBuffer.
ADDR(0x500130) void GAMEAPI says(const char* text);
ADDR(0x500150) int32_t GAMEAPI alt_at(int32_t x, int32_t y);
ADDR(0x500180) int32_t GAMEAPI alt_detail_at(int32_t x, int32_t y);
ADDR(0x5001B0) uint32_t GAMEAPI bit_at(int32_t x, int32_t y);
ADDR(0x5001E0) int32_t GAMEAPI is_ocean(int32_t x, int32_t y);
ADDR(0x500220) int32_t GAMEAPI region_at(int32_t x, int32_t y);
// Faction of the units on a tile, or -1.
ADDR(0x500250) int32_t GAMEAPI veh_who(int32_t x, int32_t y);
// Whether the current base (CurrentBaseID) has the facility.
ADDR(0x500290) int32_t GAMEAPI has_fac_built(int32_t facility_id);
ADDR(0x5002E0) int32_t GAMEAPI base_project(int32_t project_id);
ADDR(0x5002F0) uint32_t GAMEAPI has_treaty(int32_t faction_id, int32_t faction_id_with, uint32_t treaty);
// Airdrop range of a faction (whole map with orbital insertion or the Space Elevator).
ADDR(0x500320) int32_t GAMEAPI drop_range(int32_t faction_id);
// FUN_00500380 (no callers): scans faction_id's drop-capable vehicles within airdrop range of (x, y)
// that stand in a base or airbase. Has no effect (its result is not returned).
ADDR(0x500380) void GAMEAPI drop_cover_check(int32_t faction_id, int32_t x, int32_t y);
// Planet buster strength of a unit design (its reactor; 0 if not a planet buster).
ADDR(0x5004F0) int32_t GAMEAPI planet_buster2(int32_t unit_id);
// Planet buster strength of a vehicle.
ADDR(0x500520) int32_t GAMEAPI planet_buster(int32_t veh_id);
// Orbital defense of faction_id_def against a planet buster of faction_id_atk aimed at (x, y):
// Orbital Defense Pods (each fires once per turn, 50%) and Flechette Defense Systems of nearby
// enemy bases. Returns 1 if the missile was shot down.
ADDR(0x500560) int32_t GAMEAPI shoot(int32_t faction_id_atk, int32_t faction_id_def, int32_t x, int32_t y);
// Detonates planet buster veh_id at (x, y): orbital defense, crater, destroyed bases and units,
// major atrocity.
ADDR(0x500B60) void GAMEAPI planet_busting(int32_t veh_id, int32_t x, int32_t y);
// Terrain defense multiplier (2: x1, 3: x1.5, 4: x2) of faction_id's unit veh_id_def at (x, y)
// against veh_id_atk (-1 none). Sets VehBattleDisplayTerrain.
ADDR(0x5010C0)
int32_t GAMEAPI defense_value(int32_t faction_id, int32_t x, int32_t y, int32_t veh_id_def, int32_t veh_id_atk);
// Morale level of a native (faction 0) vehicle.
ADDR(0x501350) int32_t GAMEAPI morale_alien(int32_t veh_id, int32_t faction_id_vs_native);
// Psi combat value scaled by the faction's PSI rule and psi-related secret projects (is_attack: 1 offense).
ADDR(0x501500) int32_t GAMEAPI psi_factor(int32_t value, int32_t faction_id, int32_t is_attack, int32_t is_fungal_twr);
// Offense strength of veh_id (weapon x morale), as attacker (is_defender 0) or as the defender
// fighting back in an artillery duel (1). Stores the morale used in VehBasicBattleMorale.
ADDR(0x5015B0)
int32_t GAMEAPI
    get_basic_offense(int32_t veh_id, int32_t veh_id_def, int32_t psi_combat, int32_t is_bombard, int32_t is_defender);
// Defense strength of veh_id (armor x morale) against veh_id_atk (-1 none).
ADDR(0x501940)
int32_t GAMEAPI get_basic_defense(int32_t veh_id, int32_t veh_id_atk, int32_t psi_combat, int32_t is_bombard);
// Clears the battle display modifiers (VehBattleModCount, VehBattleState).
ADDR(0x501D30) void GAMEAPI battle_init();
// Adds a modifier line (percent value, text) to the battle display of a side (0 offense, 1 defense).
ADDR(0x501D50) void GAMEAPI add_bat(int32_t side, int32_t value, const char* text);
// Offense and defense strengths of a battle with all modifiers (the odds display lines in
// VehBattleDisplay/VehBattleModifier). combat_type bits: 1 artillery, 2 artillery duel, 8/0x10
// air-to-air. Always returns 0.
ADDR(0x501DA0)
int32_t GAMEAPI battle_compute(int32_t veh_id_atk, int32_t veh_id_def, int32_t* offense_out, int32_t* defense_out,
                               int32_t combat_type);
// Best defender of the stack of veh_id_def against veh_id_atk (-1 none).
ADDR(0x5044D0) int32_t GAMEAPI best_defender(int32_t veh_id_def, int32_t veh_id_atk, int32_t check_arty);
// Explosion/combat animation at (x, y) (UI: animation and sound only; the result is unused).
// Headless: no effect (see ui_bridge.h).
ADDR_NATIVE(0x504AA0) int32_t GAMEAPI boom(int32_t x, int32_t y, int32_t flags);
// A unit dies in battle: counted in units_lost; a captured non-combat unit (killer of a unit that
// is an artifact... see battle.cpp) changes sides instead. Returns 1 if the vehicle was killed.
ADDR(0x505D80)
int32_t GAMEAPI battle_kill(int32_t veh_id, int32_t* kill_count, int32_t* capture_count, int32_t* energy,
                            int32_t veh_id_killer, int32_t faction_id_killer);
// battle_kill on every vehicle of a stack.
ADDR(0x506130)
void GAMEAPI battle_kill_stack(int32_t veh_id, int32_t* kill_count, int32_t* capture_count, int32_t* energy,
                               int32_t veh_id_killer, int32_t faction_id_killer);
// battle_kill on the dead (and native) vehicles of a stack.
ADDR(0x5061B0)
void GAMEAPI battle_kill_chaff(int32_t veh_id, int32_t* kill_count, int32_t* capture_count, int32_t* energy,
                               int32_t veh_id_killer, int32_t faction_id_killer);
// Possible promotion of a battle winner.
ADDR(0x5062B0) void GAMEAPI promote(int32_t veh_id);
// After a base became empty: transports of AI factions at war nearby head for it.
ADDR(0x506490) void GAMEAPI invasions(int32_t base_id);
// Air superiority unit of faction_id_def that scrambles to (x, y) against an air attack of
// faction_id_atk; returns 1 if one was moved there.
ADDR(0x506650) int32_t GAMEAPI interceptor(int32_t faction_id_def, int32_t faction_id_atk, int32_t x, int32_t y);
// Monument record "first enemy unit destroyed" of faction_id (ui module 0x477210; engine state:
// Monuments; shows the monuments window to the viewer).
ADDR(0x477210) void GAMEAPI mon_enemy_destroyed(int32_t faction_id, int32_t faction_id_killed);
// Attack by veh_id in direction dir (artillery: dir indexes TableOffset instead of NearOffset);
// returns battle_resolve's result.
ADDR(0x506A60)
int32_t GAMEAPI battle_fight(int32_t veh_id, int32_t dir, int32_t is_arty, int32_t is_real, int32_t* def_out);
// Attack by veh_id on (x, y). is_real 0: returns the odds (x8, or x4 for artillery) without fighting
// and the defender in def_out. Otherwise fights; returns 1 if the attacker cannot act any more...
// (see battle.cpp).
ADDR(0x506AF0)
int32_t GAMEAPI battle_resolve(int32_t veh_id, int32_t dir, int32_t x, int32_t y, int32_t is_arty, int32_t is_real,
                               int32_t* def_out);
