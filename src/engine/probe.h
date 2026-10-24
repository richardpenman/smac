// Probe teams (original module around 0x59E980-0x5A59B0): infiltration, technology theft,
// sabotage, energy theft, drone riots, assassination, mind control of bases and units, genetic
// plague, freeing captured leaders; their success, survival and diplomatic consequences.
#pragma once
#include "engine/globals.h"

// ---- probe module -----------------------------------------------------------------------------

// Distance from (x, y) to faction_id's headquarters base (vector_dist), 12 without one.
ADDR(0x59E980) int32_t GAMEAPI vulnerable(int32_t faction_id, int32_t x, int32_t y);
// Energy cost for faction_id to mind control base_id (corner_market: as part of cornering the
// market). -1 if the base is its owner's headquarters (only when not cornering the market).
ADDR(0x59EA80) int32_t GAMEAPI mind_control(int32_t base_id, int32_t faction_id, int32_t is_corner_market);
// Energy cost for faction_id to corner the Global Energy Market (at least 1000).
ADDR(0x59EE50) int32_t GAMEAPI corner_market(int32_t faction_id);
// Writes the success (and survival, if different) chances of a probe of the given morale and
// difficulty against ProbeTargetFactionID (base_id: its defenses, -1 none) into parse string n;
// returns the success chance (difficulty < 0: "100%", returns the difficulty).
ADDR(0x59EEE0) int32_t GAMEAPI success_rates(int32_t n, int32_t morale, int32_t difficulty, int32_t base_id);
// Probe team veh_id acts against base_id (or, base_id < 0, against unit veh_id_tgt). commit 0:
// only tests whether the action is possible. VehAttackFlags bit 0: messages and menus for a human
// player, bit 1: carry out. Returns 0 if the action was refused or cancelled, else 1.
ADDR(0x59F120) int32_t GAMEAPI probe(int32_t veh_id, int32_t base_id, int32_t veh_id_tgt, int32_t commit);
// Appends text label `id` to StrBuffer.
ADDR(0x5A5880) void GAMEAPI say_label(int32_t id);
// Appends a space to StrBuffer.
ADDR(0x5A58B0) void GAMEAPI space();
// Sets the parse defaults (GenderDefault, PluralDefault).
ADDR(0x5A58E0) void GAMEAPI parse_set(int32_t gender, int32_t plural);
// Distance measure between two tiles (vector_dist of the wrapped deltas).
ADDR(0x5A5910) int32_t GAMEAPI vector_dist_2(int32_t x1, int32_t y1, int32_t x2, int32_t y2);

// Probe state shared with success_rates: ProbeHasAlgoEnhancement, ProbeTargetFactionID,
// ProbeTargetHasHSA (globals.h).

// ---- small callees of other modules (reconstructed here) ----------------------------------------

// diplo_act: base energy a probe can steal (pop size * owner's credits / (owner's population + 1)).
ADDR(0x50C4B0) int32_t GAMEAPI steal_energy(int32_t base_id);
// diplo_act: faction_id takes a technology of faction_id_from (tech_pick); is_steal selects the
// label. Returns 1 if nothing was taken.
ADDR(0x50C340) int32_t GAMEAPI steal_tech(int32_t faction_id, int32_t faction_id_from, int32_t is_steal);
// diplo_act text helpers: faction adjective; noun, leader name and title (setting the parse
// gender/plural defaults); popp of SCRIPT; NetMsg_pop with a -5 s delay; number into StrBuffer.
ADDR(0x50B910) const char* GAMEAPI get_adjective(int32_t faction_id);
ADDR(0x50B930) const char* GAMEAPI get_noun(int32_t faction_id);
ADDR(0x559060) const char* GAMEAPI get_name(int32_t faction_id);
ADDR(0x5590A0) const char* GAMEAPI get_title(int32_t faction_id);
ADDR(0x50B970) int32_t GAMEAPI popp_2(const char* label, const char* pcx, int32_t a3);
ADDR(0x50B9E0) int32_t GAMEAPI NetMsg_pop_2(const char* label, const char* pcx);
ADDR(0x50B8A0) void GAMEAPI say_num(int32_t value);
// game: "pact brother/sister" label for faction_id addressing faction_id_with.
ADDR(0x58FB00) const char* GAMEAPI get_pact_hood(int32_t faction_id, int32_t faction_id_with);
// diplomacy: whether the viewer has intelligence on faction_id (infiltration, Empath Guild,
// governor).
ADDR(0x55BC00) int32_t GAMEAPI spying(int32_t faction_id);
// diplomacy: say_tech_2 into StrBuffer.
ADDR(0x5591A0) void GAMEAPI say_tech(int32_t tech_id, int32_t with_category);
// diplomacy: multiplayer-aware treaty and technology changes (single player: the plain call).
ADDR(0x5391C0) void GAMEAPI net_treaty_on(int32_t faction_id, int32_t faction_id_with, uint32_t status, int32_t wait);
ADDR(0x539230) void GAMEAPI net_treaty_off(int32_t faction_id, int32_t faction_id_with, uint32_t status, int32_t wait);
ADDR(0x5392A0) void GAMEAPI net_set_treaty(int32_t faction_id, int32_t faction_id_with, uint32_t status, int32_t add,
                                           int32_t wait);
ADDR(0x5395F0) void GAMEAPI net_tech(int32_t faction_id, int32_t tech_id, int32_t from, int32_t wait);

// ---- callees of other modules (not reconstructed) ----------------------------------------------

// diplomacy: re-evaluates the AI's diplomatic stance of faction_id towards faction_id_with.
ADDR(0x53A980) void GAMEAPI diplomacy_check(int32_t faction_id, int32_t faction_id_with, int32_t a3);
// diplomacy: sets the diplomacy dialog globals (Diplo*) for faction_id and faction_id_with.
ADDR(0x5399A0) void GAMEAPI diplomacy_caption(int32_t faction_id, int32_t faction_id_with);
// net: waits for a diplomatic message to be acknowledged (NetDaemon object NetState).
ADDR(0x5315C0) void THISCALL NetDaemon_await_diplo(uint8_t* net, int32_t type);
