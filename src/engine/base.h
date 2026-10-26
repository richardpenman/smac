// Base logic (original module around 0x4E39D0-0x4F8090).
//
// Most per-base computations work on the "current base" (set_base): CurrentBaseID and the pointer
// CurrentBase (CurrentBasePtr here, an address of the original image). base_compute refreshes the
// derived values of a base (support, yields, nutrients, minerals, energy, psych) into the BASE
// fields and the Base* globals.
#pragma once
#include "engine/game_state.h"
#include "engine/globals.h"
#include "engine/base_misc.h"

// ---- globals ------------------------------------------------------------------------------------

// CurrentBase: original address of Bases[CurrentBaseID] (a BASE* of the 32-bit image).
GLOBAL(uint32_t, CurrentBasePtr, 0x90EA30);
GLOBAL(int32_t[6], CostRatioByDiff, 0x689378);  // cost_factor: base cost ratio per difficulty
GLOBAL(int32_t, BaseBitErrorShown, 0x90EA38);  // base_at: base-bits error dialog in progress
GLOBAL(int32_t, BaseYieldFoodShort, 0x90E900);  // base_yield: nutrients sufficed while choosing tiles
GLOBAL(int32_t, BaseWorkersLeft, 0x90E938);     // base_yield: citizens left to place (pop + 1 at start)
GLOBAL(int32_t, UpgradeAllProtos, 0x90EA3C);  // upgrade dialog: "upgrade all" chosen this round
// base_reset: production choices for the four AI priorities and their scores (base_build).
GLOBAL(int32_t[4], BasePriorityChoice, 0x90EA44);
GLOBAL(int32_t[4], BasePriorityScore, 0x90EA54);

// ---- helpers (not original functions) -----------------------------------------------------------

constexpr uint32_t kBasesAddr = 0x97D040;  // address of Bases[0] in the original image
// The current base (*CurrentBase).
inline BASE& cur_base() { return *reinterpret_cast<BASE*>(mem::host(CurrentBasePtr)); }
// Whether a base has a facility (bit test; facility_id < 96).
inline bool base_fac(int32_t base_id, int32_t facility_id) {
    return (game().bases[base_id].facilities_built[facility_id >> 3] & (1 << (facility_id & 7))) != 0;
}
// Owner of a secret project (-1 none or destroyed).
inline int32_t project_owner(int32_t project_id) {
    int32_t base_id = SecretProjects[project_id];
    return base_id < 0 ? -1 : (uint8_t)game().bases[base_id].faction_id;
}
// Whether faction_id owns secret project project_id (index into SecretProjects).
inline bool has_sp(int32_t project_id, int32_t faction_id) {
    int32_t base_id = SecretProjects[project_id];
    return base_id >= 0 && (uint8_t)game().bases[base_id].faction_id == (uint32_t)faction_id;
}

// ---- base module --------------------------------------------------------------------------------

// Makes base_id the current base (CurrentBaseID, CurrentBase).
ADDR(0x4E39D0) void GAMEAPI set_base(int32_t base_id);
// Appends the name of a base (or the "no base" label for -1) to buffer.
ADDR(0x4E3A00) void GAMEAPI say_base(char* buffer, int32_t base_id);
// Base at (x, y), or -1. Repairs the tile base bits when they claim a base that does not exist.
ADDR(0x4E3A50) int32_t GAMEAPI base_at(int32_t x, int32_t y);
// Nearest base to (x, y) (the last one on ties); its distance goes to BaseFindDist. -1 if none.
ADDR(0x4E3B80) int32_t GAMEAPI base_find(int32_t x, int32_t y);
// Nearest base of faction_id to (x, y).
ADDR(0x4E3C60) int32_t GAMEAPI base_find_faction(int32_t x, int32_t y, int32_t faction_id);
// Nearest base to (x, y) with filters: region (-1 any), owner faction_id (-1: any owner equal to
// faction_id_2, or with faction_id_2 == -2 any faction with a pact), faction_id_3 (-1 any: owner or
// a faction that sees the base). BaseFindDist is 9999 when none is found.
ADDR(0x4E3D50) int32_t GAMEAPI base_find_filtered(int32_t x, int32_t y, int32_t faction_id, int32_t region,
                                           int32_t faction_id_2, int32_t faction_id_3);
// Owner of the territory at (x, y) as seen by faction_id (-1 none or not visible without a
// commlink unless ignore_comm). For foreign territory, *base_id receives the claiming base.
ADDR(0x4E3EF0) int32_t GAMEAPI whose_territory(int32_t faction_id, int32_t x, int32_t y, int32_t* base_id,
                                               int32_t ignore_comm);
// Base claiming the foreign territory at (x, y) when one of the two factions is human and they
// have no treaty; -1 otherwise.
ADDR(0x4E3FA0) int32_t GAMEAPI base_territory(int32_t faction_id, int32_t x, int32_t y);
// Best available specialist type for the current base.
ADDR(0x4E4020) int32_t GAMEAPI best_specialist();
// Name for a new base of faction_id (sea base names when is_sea); is_final consumes the name.
ADDR(0x4E4090) void GAMEAPI name_base(int32_t faction_id, char* name, int32_t is_final, int32_t is_sea);
// Marks the work radius of a base on the map (base radius bit, user) and clears AI sites nearby.
ADDR(0x4E4350) void GAMEAPI base_mark(int32_t base_id);
// Cost multiplier (x10 of the base cost) of nutrients (is_mineral 0) or minerals (1) for a faction;
// base_id (-1 none) adds facility/golden-age bonuses to the growth rating.
ADDR(0x4E4430) int32_t GAMEAPI cost_factor(int32_t faction_id, int32_t is_mineral, int32_t base_id);
// Retooling category of a production item for a base (penalty-free changes keep the category).
ADDR(0x4E4700) int32_t GAMEAPI base_making(int32_t item_id, int32_t base_id);
// Minerals lost by the retooling penalty if the production of a base changes now.
ADDR(0x4E4810) int32_t GAMEAPI base_lose_minerals(int32_t base_id, int32_t item_id);
// Sets (add != 0) or clears a facility bit of a base.
ADDR(0x4E48B0) void GAMEAPI set_fac(int32_t facility_id, int32_t base_id, int32_t add);
// Whether the faction already heard the announcement of a facility.
ADDR(0x4E4900) int32_t GAMEAPI has_fac_announced(int32_t faction_id, int32_t facility_id);
ADDR(0x4E4960) void GAMEAPI set_fac_announced(int32_t faction_id, int32_t facility_id, int32_t add);
// Index (1-20) of the first ocean tile within radius 1 (or 2 with is_base_radius), or 0.
ADDR(0x4E49D0) int32_t GAMEAPI is_coast(int32_t x, int32_t y, int32_t is_base_radius);
// Initial production of a new base: the best available defensive unit.
ADDR(0x4E4AA0) void GAMEAPI base_first(int32_t base_id);
// Founds a base of faction_id at (x, y). Returns its id, or -1 if the table is full.
ADDR(0x4E4B80) int32_t GAMEAPI base_init(int32_t faction_id, int32_t x, int32_t y);
// Removes a base (units rehomed or killed, ids renumbered, territory recomputed).
ADDR(0x4E5250) void GAMEAPI base_kill(int32_t base_id);
// Changes the production of a base (retooling penalty, secret project announcements).
ADDR(0x4E5A60) void GAMEAPI base_change(int32_t base_id, int32_t item_id);
// Lets the AI or the governor choose the production of a base.
ADDR(0x4E5FE0) void GAMEAPI base_reset(int32_t base_id, int32_t has_gov);
// base_reset for the AI bases of faction_id in a region (-1 all); with no_units only for bases
// without units.
ADDR(0x4E6300) void GAMEAPI bases_reset(int32_t region, int32_t faction_id, int32_t no_units);
// Morale bonus of units built in a base for a triad (training facilities and projects).
ADDR(0x4E6400) int32_t GAMEAPI morale_mod(int32_t base_id, int32_t faction_id, int32_t triad);
// Morale bonus of native life bred in a base.
ADDR(0x4E65C0) int32_t GAMEAPI breed_mod(int32_t base_id, int32_t faction_id);
// Number of psi-attack bonuses of a base's faction (projects and facilities).
ADDR(0x4E6740) int32_t GAMEAPI worm_mod(int32_t base_id, int32_t faction_id);

// Base tile allocation (worker tiles, specialists) for the current base.
ADDR(0x4E68B0) void GAMEAPI farm_compute();
// Nutrients / minerals / energy of tile (x, y) worked by base_id of faction_id (flag: see the
// definitions).
ADDR(0x4E6E50) int32_t GAMEAPI crop_yield(int32_t faction_id, int32_t base_id, int32_t x, int32_t y, int32_t flag);
ADDR(0x4E7310) int32_t GAMEAPI mine_yield(int32_t faction_id, int32_t base_id, int32_t x, int32_t y, int32_t flag);
ADDR(0x4E7750) int32_t GAMEAPI energy_yield(int32_t faction_id, int32_t base_id, int32_t x, int32_t y, int32_t flag);
// Yield of resource type (0 nutrients, 1 minerals, 2 energy) of a tile.
ADDR(0x4E7DC0) int32_t GAMEAPI resource_yield(int32_t type, int32_t faction_id, int32_t base_id, int32_t x, int32_t y);
// Adds / removes the yields of radius tile i (worked by the current base) to / from the base's
// intake; returns the total yield (no callers in the original).
ADDR(0x4E7E50) int32_t GAMEAPI base_tile_intake_add(int32_t i);
ADDR(0x4E7F80) int32_t GAMEAPI base_tile_intake_remove(int32_t i);
// Resource intake of the current base: worked tiles, specialists.
ADDR(0x4E80B0) void GAMEAPI base_yield();
// Unit support of the current base.
ADDR(0x4E9550) void GAMEAPI base_support();
ADDR(0x4E9B70) void GAMEAPI base_nutrient();
ADDR(0x4E9CB0) void GAMEAPI base_minerals();
// Energy credits from black-market style energy reserves (faction economy).
ADDR(0x4EA1F0) int32_t GAMEAPI black_market(int32_t energy);
ADDR(0x4EA4A0) void GAMEAPI psych_check(int32_t faction_id, int32_t* drones, int32_t* talents);
ADDR(0x4EA540) void GAMEAPI base_psych();
ADDR(0x4EB490) int32_t GAMEAPI base_rank(int32_t faction_id, int32_t position);
ADDR(0x4EB560) void GAMEAPI base_energy();
// Recomputes the current base's derived values (always with force, else when another base was
// computed last).
ADDR(0x4EC3B0) void GAMEAPI base_compute(int32_t force);
ADDR(0x4EC3F0) int32_t GAMEAPI base_connect(int32_t base_id);
ADDR(0x4ECB90) int32_t GAMEAPI crappy(int32_t a1, int32_t a2);
ADDR(0x4ECBF0) void GAMEAPI base_terraform(int32_t flag);
ADDR(0x4EEF80) int32_t GAMEAPI pop_goal_fac(int32_t base_id);
ADDR(0x4EF090) int32_t GAMEAPI pop_goal(int32_t base_id);
// Population growth or starvation of the current base. Returns 1 if the base died.
ADDR(0x4EF1C0) int32_t GAMEAPI base_growth();
ADDR(0x4EFB50) void GAMEAPI do_upgrade(int32_t veh_id, int32_t unit_id, int32_t a3);
ADDR(0x4EFD50) int32_t GAMEAPI upgrade_cost(int32_t faction_id, int32_t new_unit_id, int32_t old_unit_id);
ADDR(0x4EFE80) int32_t GAMEAPI upgrade_prototype(int32_t faction_id, int32_t new_unit_id, int32_t old_unit_id,
                                                 int32_t flag);
ADDR(0x4F0460) void GAMEAPI upgrade_prototypes(int32_t faction_id, int32_t unit_id);
ADDR(0x4F0650) void GAMEAPI upgrade_any_prototypes(int32_t faction_id);
ADDR(0x4F06E0) int32_t GAMEAPI base_queue(int32_t base_id);
ADDR(0x4F07E0) void GAMEAPI base_production();
ADDR(0x4F3FE0) void GAMEAPI base_hurry();
ADDR(0x4F4830) void GAMEAPI base_check_support();
ADDR(0x4F4DC0) void GAMEAPI base_energy_costs();
ADDR(0x4F4E80) void GAMEAPI base_research();
ADDR(0x4F4FB0) void GAMEAPI drone_riot();
ADDR(0x4F5CE0) void GAMEAPI base_drones();
ADDR(0x4F5F30) void GAMEAPI base_doctors();
// Maintenance (energy) of a facility for a faction.
ADDR(0x4F6510) int32_t GAMEAPI fac_maint(int32_t facility_id, int32_t faction_id);
ADDR(0x4F65F0) void GAMEAPI base_maint();
ADDR(0x4F67F0) void GAMEAPI base_ecology();
ADDR(0x4F79C0) int32_t GAMEAPI base_upkeep(int32_t base_id);
ADDR(0x4F7FE0) void GAMEAPI make_base_unique(int32_t base_id);

// base_misc (0x4F8090-0x500120: vector_dist, has_project, base_claimed, base_build): base_misc.h.

// ---- facility helper of the UI module ----
// Whether a base has a facility (facility_id < 65), or has it among the first queue_count queue
// items.
ADDR(0x421670) int32_t GAMEAPI has_fac(int32_t facility_id, int32_t base_id, int32_t queue_count);

// ---- monuments (ui module, engine state only): records of base_production ----
ADDR(0x476EE0) void GAMEAPI mon_prototype_built(int32_t faction_id);
ADDR(0x476FE0) void GAMEAPI mon_facility_built(int32_t faction_id, const char* base_name);
ADDR(0x477100) void GAMEAPI mon_secret_project(int32_t faction_id, int32_t project_id);
// First base conquered (capture_base): base_name is recorded as enemy_base_name.
ADDR(0x477320) void GAMEAPI mon_conquer_base(int32_t faction_id, const char* base_name);
ADDR(0x477440) void GAMEAPI mon_naval_unit_built(int32_t faction_id);
ADDR(0x477540) void GAMEAPI mon_air_unit_built(int32_t faction_id);
ADDR(0x477640) void GAMEAPI mon_native_life_bred(int32_t faction_id);
ADDR(0x477740) void GAMEAPI mon_first_in_space(int32_t faction_id);
ADDR(0x477840) void GAMEAPI mon_built_preserve(int32_t faction_id);
// Transcendence: faction_id's monument records the ascent (by_faction_id: through whom, or -1).
ADDR(0x4779D0) void GAMEAPI mon_winning_trans(int32_t faction_id, int32_t by_faction_id);
// Diplomatic/economic victory: faction_id's monument records the unification of Planet (other_id:
// the partner of a cooperative victory, or -1); shown to a viewer that is not alien.
ADDR(0x477940) void GAMEAPI mon_winning_unify(int32_t faction_id, int32_t other_id);
