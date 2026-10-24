// alpha(x).txt rule parsing (original module rules, 0x584D60-0x5882F0).
#pragma once
#include "engine/globals.h"

// ---- string tables filled by read_rules (pointers into the string table) ----
GLOBAL_NATIVE(char* [8], Compass, 0x945D48);
GLOBAL_NATIVE(char* [15], PlansShortName, 0x945FE0);
GLOBAL_NATIVE(char* [15], PlansFullName, 0x952360);
GLOBAL_NATIVE(char* [3], TriadName, 0x94F1A8);
GLOBAL_NATIVE(char* [6], Difficulty, 0x96C85C);
GLOBAL_NATIVE(char* [9], Mood, 0x94C9E4);
GLOBAL_NATIVE(char* [8], Repute, 0x946A30);

// Text file names (static string table at 0x691AFC: "ALPHAX", "HELPX", ..., 0x691B0C: "SCRIPT").
GLOBAL_NATIVE(const char*, AlphaxFileName, 0x691AFC);
GLOBAL_NATIVE(const char*, ScriptFileName, 0x691B0C);

// Tech id of a short tech name ("None": -1, "Disable": -2). Trailing blanks of name are removed in
// place. Unknown names show the BADTECHKEY error and yield -2.
ADDR(0x584D60) int32_t GAMEAPI tech_name(char* name);
// Chassis/weapon/armor id of a name ("None": -1, "Disable": -2; unknown: error popup, 0).
ADDR(0x584E40) int32_t GAMEAPI chas_name(char* name);
ADDR(0x584F40) int32_t GAMEAPI weap_name(char* name);
ADDR(0x585030) int32_t GAMEAPI arm_name(char* name);
// Reads the next line's first item as a number clamped to [min, max] (min wins if max < min).
ADDR(0x585120) int32_t GAMEAPI text_get_number(int32_t min, int32_t max);
// Reads the next line's first item as a tech name.
ADDR(0x585150) int32_t GAMEAPI tech_item();
// #RULES and #WORLDBUILDER. Returns 0 on success.
ADDR(0x585170) int32_t GAMEAPI read_basic_rules();
// #TECHNOLOGY (twice: short names with duplicate check, then the tech table). Returns 0 on success.
ADDR(0x585E30) int32_t GAMEAPI read_tech();
// Resets the faction's rule values (bonuses, research/hurry costs to 100%).
ADDR(0x585FE0) void GAMEAPI clear_faction(MFaction* faction);
// read_faction for MFactions[faction_id] (if not 0) and its artwork.
ADDR(0x586050) void GAMEAPI read_faction2(int32_t faction_id);
// Loads a faction definition (faction->filename .txt, section faction->search_key). toggle 2 reads
// the names only, without AI priorities, bonuses and social priorities.
ADDR(0x586090) void GAMEAPI read_faction(MFaction* faction, int32_t toggle);
// #BONUSNAMES, the faction list (#FACTIONS / #NEWFACTIONS, "Faction N" preferences), random
// factions ("JENN282") and every faction file. Returns 0 on success.
ADDR(0x586F30) int32_t GAMEAPI read_factions();
// Reads a noun gender ('M', 'F', 'N') and number ('1', '2') item, e.g. "m1".
ADDR(0x5871D0) void GAMEAPI noun_item(int32_t* gender, int32_t* plural);
// #UNITS: the predefined unit types. Returns 0 on success.
ADDR(0x587240) int32_t GAMEAPI read_units();
// Loads labels and all rule tables of alphax.txt (all_rules: also units and factions). Returns 0
// on success.
ADDR(0x5873C0) int32_t GAMEAPI read_rules(int32_t all_rules);

// Native builds: static data of the module (string pointers of the original data segment).
void rules_static_init();
