// Combat (original module around 0x500120-0x50B8A0). Only the small accessors at its start
// (0x500120-0x500380) are reconstructed so far.
#pragma once
#include "engine/globals.h"

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
// Morale level of a native (faction 0) vehicle.
ADDR(0x501350) int32_t GAMEAPI morale_alien(int32_t veh_id, int32_t faction_id_vs_native);
// Psi combat value scaled by the faction's PSI rule and psi-related secret projects (is_attack: 1 offense).
ADDR(0x501500) int32_t GAMEAPI psi_factor(int32_t value, int32_t faction_id, int32_t is_attack, int32_t is_fungal_twr);
// Planet buster strength of a unit design (0 if none).
ADDR(0x5004F0) int32_t GAMEAPI planet_buster2(int32_t unit_id);
// Explosion effect at (x, y) (animation and sound).
ADDR(0x504AA0) int32_t GAMEAPI boom(int32_t x, int32_t y, int32_t flags);
