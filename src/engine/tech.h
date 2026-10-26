// Technology (original module around 0x5B9C40-0x5BED20).
//
// Tech ids: 0-88 technologies (88 is the repeatable "future" tech), -1 none; in the say/value
// functions 89-96 stand for a faction's knowledge (89 + faction id), 97+ for a unit (97 + unit id)
// and 9999 for "nothing" (a stolen tech that could not be chosen).
#pragma once
#include "engine/globals.h"

// Appends the name of a tech id (or faction / unit, see above) to buffer; with with_category,
// also " (category depth)".
ADDR(0x5B9C40) void GAMEAPI say_tech_2(char* buffer, int32_t tech_id, int32_t with_category);
// say_tech_2 into a static buffer (0x945E80) that is returned.
ADDR(0x5B9EF0) char* GAMEAPI tech_name_2(int32_t tech_id, int32_t with_category);
GLOBAL(char[256], TechNameBuffer, 0x945E80);
// Whether faction_id knows tech_id (-1: always; faction 0 never; tech 88 never).
ADDR(0x5B9F20) int32_t GAMEAPI has_tech(int32_t tech_id, int32_t faction_id);
// Depth of a tech in the prerequisite tree (level for no tech).
ADDR(0x5B9F90) int32_t GAMEAPI tech_recurse(int32_t tech_id, int32_t level);
// Main category of a tech: 0 explore (colonize), 1 discover (tech), 2 build (infra), 3 conquer.
ADDR(0x5B9FE0) int32_t GAMEAPI tech_category(int32_t tech_id);
// Whether a facility is made redundant at faction_id's bases by an owned secret project.
ADDR(0x5BA030) int32_t GAMEAPI redundant(int32_t facility_id, int32_t faction_id);
// Whether faction_id can build facility_id (at base_id, -1: anywhere; queue_count first queue
// entries count as built).
ADDR(0x5BA0E0)
int32_t GAMEAPI facility_avail(int32_t facility_id, int32_t faction_id, int32_t base_id, int32_t queue_count);
// Whether faction_id can build unit_id at base_id (prototype/obsolescence/triad checks).
ADDR(0x5BA910) int32_t GAMEAPI veh_avail(int32_t unit_id, int32_t faction_id, int32_t base_id);
// Whether faction_id may do terraforming action terraform_id (on sea tiles: is_sea).
ADDR(0x5BAB40) int32_t GAMEAPI terrain_avail(int32_t terraform_id, int32_t is_sea, int32_t faction_id);
// Whether faction_id can research tech_id now (not known, prerequisites known).
ADDR(0x5BAC20) int32_t GAMEAPI tech_avail(int32_t tech_id, int32_t faction_id);
// "The secret of <tech>" announcement (unused by the game).
ADDR(0x5BADC0) void GAMEAPI tech_secret(int32_t tech_id, int32_t faction_id);
// Recomputes the tech-derived faction bonuses (commerce, fungus yields).
ADDR(0x5BAE60) void GAMEAPI tech_effects(int32_t faction_id);
// faction_id gains tech_id. from: source faction (0: own research; -1: Network Backbone-like
// project spread; -2: shared by the faction's tech-sharing ability, with is_shared). Also handles
// 89-96 (commlink with a faction), 97+ (a unit design) and >= 9999 (the maps of faction `from`).
ADDR(0x5BB000) void GAMEAPI tech_achieved(int32_t faction_id, int32_t tech_id, int32_t from, int32_t is_shared);
// Tech of the "tech tree" tutorial message (TREETIME).
GLOBAL(int32_t, TutorialTreeTech, 0x691900);
// Whether preq_id is tech_id or among its prerequisites up to `depth` levels.
ADDR(0x5BCB60) int32_t GAMEAPI tech_is_preq(int32_t preq_id, int32_t tech_id, int32_t depth);
// AI value of tech_id (see above for 89+) to faction_id; is_simple skips the strategic terms.
ADDR(0x5BCBE0) int32_t GAMEAPI tech_val(int32_t tech_id, int32_t faction_id, int32_t is_simple);
// AI research choice (random, weighted by tech_val). Returns a tech id or -1.
ADDR(0x5BDC10) int32_t GAMEAPI tech_ai(int32_t faction_id);
// Category weights of a tech (1 for ids above 88; tech_mil 0).
ADDR(0x5BDD70) int32_t GAMEAPI tech_mil(int32_t tech_id);
ADDR(0x5BDD90) int32_t GAMEAPI tech_tech(int32_t tech_id);
ADDR(0x5BDDC0) int32_t GAMEAPI tech_infra(int32_t tech_id);
ADDR(0x5BDDF0) int32_t GAMEAPI tech_colonize(int32_t tech_id);
// Whether a new design unit_id is better (weapon, armor or speed) than every active prototype
// of faction_id with the same plan and triad.
ADDR(0x5BE100) int32_t GAMEAPI wants_prototype(int32_t unit_id, int32_t faction_id);
// Chooses the next research goal (multiplayer: waits for the remote player's choice).
ADDR(0x5BE380) int32_t GAMEAPI tech_selection(int32_t faction_id);
// Completes the current research goal. Returns the tech gained or -1 (saved for later).
ADDR(0x5BE530) int32_t GAMEAPI tech_advance(int32_t faction_id);
// Research cost of the next tech for faction_id.
ADDR(0x5BE6B0) int32_t GAMEAPI tech_rate(int32_t faction_id);
// Adds labs to the faction's research (doubled during the climactic battle before the
// Transcendence prerequisites).
ADDR(0x5BE940) void GAMEAPI tech_research(int32_t faction_id, int32_t labs);
// Opens a text section (text_open result: 0 on success); the "SCRIPT" file of an alien viewer
// faction prefers alienIscript.
ADDR(0x5BECA0) int32_t GAMEAPI X_text_open(const char* filename, const char* section);

// ---- monuments (ui module, engine state only) ----
// Records the faction's first discovered tech in its monument (first in the race: "who").
ADDR(0x476C90) void GAMEAPI mon_tech_discovered(int32_t faction_id, int32_t tech_id);
// Records a discovered secret (Tech flag 1) in the monument; later ones fill two more years.
ADDR(0x476DA0) void GAMEAPI mon_secrets_of_tech(int32_t faction_id);
