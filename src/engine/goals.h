// Goals, sites, monoliths, supply pods (original module around 0x579770-0x57D1F0).
#pragma once
#include "engine/globals.h"
// want_monolith, monolith, goody_box, study_artifact, valid_tech_leap (same module): engine/goody.h.
#include "engine/goody.h"

// CRT random number 0..n-1 (0 for n <= 1).
ADDR(0x579770) int32_t GAMEAPI rnd(int32_t n);
// Horizontal distance between two columns (wrapped).
ADDR(0x579790) int32_t GAMEAPI x_dist(int32_t x1, int32_t x2);
// AI site value of a tile (0 for unavailable tiles); computed with world_site and cached on the map.
ADDR(0x5797D0) int32_t GAMEAPI site_at(int32_t x, int32_t y, int32_t sea);
// Whether faction_id has explored (x, y).
ADDR(0x579840) int32_t GAMEAPI is_known(int32_t x, int32_t y, int32_t faction_id);
// Owner of the base at (x, y), or -1.
ADDR(0x5798A0) int32_t GAMEAPI base_who(int32_t x, int32_t y);
// Owner of the base or units at (x, y), or -1.
ADDR(0x5798E0) int32_t GAMEAPI anything_at(int32_t x, int32_t y);
// Remaining movement points of a vehicle (0-999).
ADDR(0x579960) int32_t GAMEAPI veh_moves(int32_t veh_id);
// Reactor power x10 of a vehicle's unit (supply crawlers: 1).
ADDR(0x5799A0) int32_t GAMEAPI proto_power(int32_t veh_id);
// Whether a base is coastal (is_coast at its tile).
ADDR(0x579A00) int32_t GAMEAPI is_port(int32_t base_id, int32_t is_base_radius);
// Ages faction_id's goals (each lives two turns) and re-adds its sites as goals.
ADDR(0x579D80) void GAMEAPI wipe_goals(int32_t faction_id);
// 0x579FF0 (no callers): adds a monolith site at (x, y) for every faction that has seen one there.
ADDR(0x579FF0) void GAMEAPI monolith_sites(int32_t x, int32_t y);
// Top of the stack containing veh_id, or -1.
ADDR(0x579920) int32_t GAMEAPI veh_top(int32_t veh_id);
// Adds an AI goal of a type and priority for a faction at (x, y).
ADDR(0x579A30) void GAMEAPI add_goal(int32_t faction_id, int32_t type, int32_t priority, int32_t x, int32_t y,
                                     int32_t base_id);
// Clears all AI goals and sites of faction_id (type -1).
ADDR(0x579E00) void GAMEAPI init_goals(int32_t faction_id);
// Removes faction_id's AI sites of a type within distance of (x, y).
ADDR(0x579E70) void GAMEAPI del_site(int32_t faction_id, int32_t type, int32_t x, int32_t y, int32_t distance);
// Adds (or raises the priority of) an AI site of faction_id, with its goal (add_goal).
ADDR(0x579B70) void GAMEAPI add_site(int32_t faction_id, int32_t type, int32_t priority, int32_t x, int32_t y);
// Whether faction_id has an AI site of a type at (x, y).
ADDR(0x579D20) int32_t GAMEAPI at_site(int32_t faction_id, int32_t type, int32_t x, int32_t y);
// Whether faction_id has an AI goal of a type at (x, y).
ADDR(0x579CC0) int32_t GAMEAPI at_goal(int32_t faction_id, int32_t type, int32_t x, int32_t y);
// Empty text passed to danger() by add_site (a zeroed buffer at 0x939FF0).
GLOBAL(char[4], DangerEmptyText, 0x939FF0);
