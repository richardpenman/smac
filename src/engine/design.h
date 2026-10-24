// Prototype design (original module design, 0x57D1F0-0x584D60): AI unit design (consider_designs,
// propose_proto), obsolescence (prune_protos, retire_proto), automatic names and stat strings.
// design_workshop and hq_list_draw in this range are UI (design workshop window).
#pragma once
#include "engine/globals.h"

// Cargo capacity of a transport design (sea chassis scale with the reactor).
ADDR(0x57D510) int32_t GAMEAPI transport_val(int32_t chassis_id, uint32_t ability_flags, int32_t reactor_id);
// Strategic values of an armor / weapon for a faction (psi values depend on the faction and the
// best enemy weapon/armor).
ADDR(0x57D270) int32_t GAMEAPI arm_strat(int32_t armor_id, int32_t faction_id);
ADDR(0x57D2E0) int32_t GAMEAPI weap_strat(int32_t weapon_id, int32_t faction_id);
// weap_strat of a unit's weapon.
ADDR(0x57D360) int32_t GAMEAPI weap_val(int32_t unit_id, int32_t faction_id);
// Twice the strategic armor value (faction_id < 0: the plain defense value).
ADDR(0x57D3F0) int32_t GAMEAPI arm_val(int32_t armor_id, int32_t faction_id);
// arm_val of a unit's armor.
ADDR(0x57D480) int32_t GAMEAPI armor_val(int32_t unit_id, int32_t faction_id);

// ---- stat strings (appended to buffer) ----
// Offense value with ability markers ("<" "(" "^" "~" "x" "e" "!" ")" ">" "r").
ADDR(0x57D560) void GAMEAPI say_offense(char* buffer, int32_t unit_id);
// Defense value with ability markers.
ADDR(0x57D6D0) void GAMEAPI say_defense(char* buffer, int32_t unit_id);
// "offense-defense-moves[*reactor]".
ADDR(0x57D7D0) void GAMEAPI say_stats_3(char* buffer, int32_t unit_id);
// Same with labels for psi values and sea/air triads.
ADDR(0x57D8E0) void GAMEAPI say_stats_2(char* buffer, int32_t unit_id);
// Long form with the plan name; separator (default "/") between offense, defense and moves.
ADDR(0x57DAA0) void GAMEAPI say_stats(char* buffer, int32_t unit_id, const char* separator);

// ---- savegame headers (used by the save module) ----
struct CrtFile;
// Reads a NUL-terminated header string (at most 256 characters kept) and the byte after it
// (the 0x1A written by header_write).
ADDR(0x57D1F0) void GAMEAPI header_check(char* buffer, CrtFile* file);
// Writes `header` with its NUL terminator, then 0x1A.
ADDR(0x57D240) void GAMEAPI header_write(const char* header, CrtFile* file);

// ---- design-attempt list ("bunglist": 64 entries of 8 bytes per faction) ----
// Entry: ability flags (4 bytes), chassis, weapon, armor, reactor. Designs a human player retired
// are not proposed again (is_bunged).
GLOBAL(uint8_t[8][64][8], BungList, 0x94B558);
// 0x57DEE0 (no callers): marks one entry as unused (all bytes 0xFF).
ADDR(0x57DEE0) void GAMEAPI bung_clear(uint8_t* entry);
// Marks every entry of the faction's list as unused (all bytes 0xFF).
ADDR(0x57DF00) void GAMEAPI clear_bunglist(int32_t faction_id);
// 0x57DF30: records unit_id's design in the faction's list (ring buffer, Faction.bunglist_next).
ADDR(0x57DF30) void GAMEAPI bung_add(int32_t faction_id, int32_t unit_id);
// Whether a human faction's list contains the design.
ADDR(0x57DFC0) int32_t GAMEAPI is_bunged(int32_t faction_id, int32_t chassis_id, int32_t weapon_id, int32_t armor_id,
                                         uint32_t ability_flags, int32_t reactor_id);

// ---- naming and component selection ----
// Automatic name of a design (written to name, at most ~256 characters). unit_id is the slot the
// design is meant for (its plan and reactor rank among the faction's designs choose the variant);
// other designs of the faction with the same name get a numeric suffix.
ADDR(0x57E040) void GAMEAPI name_proto(char* name, int32_t unit_id, int32_t faction_id, int32_t chassis_id,
                                       int32_t weapon_id, int32_t armor_id, uint32_t ability_flags,
                                       int32_t reactor_id);
// Best reactor (1-4) the faction knows.
ADDR(0x57EFA0) int32_t GAMEAPI best_reactor(int32_t faction_id);
// Best known chassis of a triad (-1: any) for a speed requirement (> 0: at least that speed, 2 points
// for exactly it; -1: missiles allowed; -2: air units with range 1 only). -1 if none.
ADDR(0x57EFF0) int32_t GAMEAPI pick_chassis(int32_t faction_id, int32_t triad, int32_t speed);
// Best known weapon costing at most cost (cost < 0: the first weapon with mode -cost); with
// combat_only only projectile/energy weapons.
ADDR(0x57F0B0) int32_t GAMEAPI weapon_budget(int32_t faction_id, int32_t cost, int32_t combat_only);
// Best known armor costing at most cost.
ADDR(0x57F150) int32_t GAMEAPI armor_budget(int32_t faction_id, int32_t cost);

// ---- prototypes ----
// Retires a faction prototype: its units are upgraded to the best design of the same plan, disbanded
// (their cost partly refunded to the base they are in) or killed; it leaves all build queues.
ADDR(0x57F1D0) void GAMEAPI retire_proto(int32_t unit_id, int32_t faction_id);
// Marks the faction's prototypes made obsolete by unit_id (net_synch: send the changes); for human
// players with the upgrade option, offers to replace them in the build queues.
ADDR(0x57F4B0) void GAMEAPI prune_protos(int32_t faction_id, int32_t unit_id, int32_t net_synch);
// Creates (or finds) a prototype for the design. plan: AI plan to assign (-1: keep; -2: mark as
// prototyped); name: nullptr for an automatic name. Returns the unit id, -1 if rejected, 2 for the
// basic design (1-1-1 infantry).
ADDR(0x580860) int32_t GAMEAPI propose_proto(int32_t faction_id, int32_t chassis_id, int32_t weapon_id,
                                             int32_t armor_id, uint32_t ability_flags, int32_t reactor_id,
                                             int32_t plan, const char* name);
// Index of the lowest set bit of an ability flag.
ADDR(0x581170) int32_t GAMEAPI abil_index(uint32_t ability_flag);
// Adds an ability to an empty ability set when the faction knows it and does not get it for free.
ADDR(0x581190) void GAMEAPI add_abil(uint32_t* ability_flags, uint32_t ability_flag, int32_t faction_id);
// AI: proposes the faction's current best designs for each role.
ADDR(0x581260) void GAMEAPI consider_designs(int32_t faction_id);
// Opens the design workshop (UI).
ADDR(0x583CB0) int32_t GAMEAPI design_new_veh(int32_t faction_id, int32_t unit_id);
// Whether an ability may be fitted to a chassis/weapon/armor combination.
ADDR(0x584B50) int32_t GAMEAPI abil_cond(int32_t ability_id, int32_t chassis_id, int32_t weapon_id, int32_t armor_id);
// 0x584D30 (no callers): strncpy that always terminates (dst[n - 1] = 0).
ADDR(0x584D30) void GAMEAPI strncpy_term(char* dst, const char* src, uint32_t n);

// Design workshop "suggest" mode (DesignWin): propose_proto records the design here instead of
// creating it. Mode: 0 off, 1 searching, 2 found an existing design, 3 found a new design.
GLOBAL(int32_t, DesignSuggestMode, 0x93FC54);
GLOBAL(int32_t, DesignSuggestPlan, 0x93FC38);  // plan filter (-1: any); set with mode 3
GLOBAL(uint32_t, DesignSuggestAbilities, 0x93FC34);
GLOBAL(int32_t, DesignSuggestWeapon, 0x93FC3C);
GLOBAL(int32_t, DesignSuggestChassis, 0x93FC40);
GLOBAL(int32_t, DesignSuggestReactor, 0x93FC44);
GLOBAL(int32_t, DesignSuggestArmor, 0x93FC50);
