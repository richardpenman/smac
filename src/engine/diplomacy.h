// Diplomacy (original module around 0x538F30-0x55F930).
#pragma once
#include "engine/globals.h"

// First contact / encounter between two factions at (x, y).
ADDR(0x55E370) void GAMEAPI encounter(int32_t faction_id, int32_t faction_id_with, int32_t x, int32_t y,
                                      int32_t is_monolith, int32_t is_native);
// Whether faction_id is the "great satan" (overwhelming leader); is_aggressive selects the rule.
ADDR(0x539C00) int32_t GAMEAPI great_satan(int32_t faction_id, int32_t is_aggressive);
// Whether faction_id (seen from faction_id_vs) is close to a diplomatic/council victory.
ADDR(0x539D40) int32_t GAMEAPI aah_ooga(int32_t faction_id, int32_t faction_id_vs);
// Whether a human faction is close to victory (the AI then unites against it).
ADDR(0x539E40) int32_t GAMEAPI climactic_battle();
// Whether faction_id wants to attack faction_id_target (odds_override: extra aggression).
ADDR(0x55BC80) int32_t GAMEAPI wants_to_attack(int32_t faction_id, int32_t faction_id_target, int32_t odds_override);
// Whether the game is near its end for faction_id (a faction close to victory).
ADDR(0x539EF0) int32_t GAMEAPI at_climax(int32_t faction_id);

// ---- debug/checksum helpers at the start of the module (0x538F30-0x539160; net_misc.cpp) ----
// Debug alert: logs label/text/values and shows the DANGER popup with them.
ADDR(0x538F30) void GAMEAPI danger(const char* label, const char* text, int32_t v1, int32_t v2, int32_t v3);
// XOR of all bytes of a file (0 if it cannot be opened or is empty).
ADDR(0x538FE0) int32_t GAMEAPI checksum_file(const char* filename);
// XOR of size bytes into seed. Only the low byte is the result; the upper bytes of the return
// value are the register residue of the original (see net_misc.cpp).
ADDR(0x539090) int32_t GAMEAPI checksum(const uint8_t* data, int32_t size, int32_t seed);
// Password check value: XOR of the upper-cased characters + 1 (0 for an empty password).
ADDR(0x5390C0) int32_t GAMEAPI checksum_password(const char* password);
// Sets diplomatic status bits of faction_id toward faction_id_with (and the reverse where mutual).
ADDR(0x55B870) void GAMEAPI treaty_on(int32_t faction_id, int32_t faction_id_with, uint32_t status);
// Clears diplomatic status bits between two factions in both directions (with dependent bits:
// any of 7 also clears 0x1100000, 0x10 also clears 0x8040; 0x20 clears agenda bits 0xC00).
ADDR(0x55B760) void GAMEAPI treaty_off(int32_t faction_id, int32_t faction_id_with, uint32_t status);
// Sets agenda bits between two factions in both directions (0x20 first clears agenda bit 0x40).
ADDR(0x55BA80) void GAMEAPI agenda_on(int32_t faction_id, int32_t faction_id_with, uint32_t agenda);
// Sets (add != 0) or clears diplomatic status bits of faction_id toward faction_id_with; setting
// 0x40 (vendetta) also sets diplo_merc toward faction_id_with to 50.
ADDR(0x55BB30) void GAMEAPI set_treaty(int32_t faction_id, int32_t faction_id_with, uint32_t status, int32_t add);
// Sets (add != 0) or clears agenda bits of faction_id toward faction_id_with.
ADDR(0x55BBA0) void GAMEAPI set_agenda(int32_t faction_id, int32_t faction_id_with, uint32_t agenda, int32_t add);
// Reputation penalty of faction_id toward faction_id_with: integrity blemishes minus the gifts
// made to faction_id_with, clamped to 0..99.
ADDR(0x53A100) int32_t GAMEAPI reputation(int32_t faction_id, int32_t faction_id_with);
// Clears agenda bits between two factions in both directions.
ADDR(0x55B820) void GAMEAPI agenda_off(int32_t faction_id, int32_t faction_id_with, uint32_t agenda);
// Ends a pact between two factions (units withdrawn from each other's bases, messages).
ADDR(0x53C650) void GAMEAPI pact_ends(int32_t faction_id, int32_t faction_id_with);
// Moves faction_id's units in or in the territory of faction_id_with's bases to the nearest own
// base they can reach. Returns the number of units moved.
ADDR(0x53C370) int32_t GAMEAPI pact_withdraw(int32_t faction_id, int32_t faction_id_with);
// communicate (0x54FFD0), the diplomacy dialog with a human player: engine/ui_bridge.h.
// Multiplayer: takes / releases the global diplomatic lock. diplo_lock returns non-zero when the
// player gave up waiting.
ADDR(0x539820) int32_t GAMEAPI diplo_lock(int32_t timeout);
ADDR(0x5398C0) void GAMEAPI diplo_unlock();
// Major atrocity (planet buster) by faction_id against faction_id_vs (-1 none): sanctions, the
// council's and every faction's reactions.
ADDR(0x55F450) void GAMEAPI major_atrocity(int32_t faction_id, int32_t faction_id_vs);
// Whether faction_id's and faction_id_with's areas in region connect (path search).
ADDR(0x53A780) int32_t GAMEAPI contiguous(int32_t faction_id, int32_t faction_id_with, int32_t region, int32_t a4);
// Units of faction_id_with in faction_id's land territory (outside regions where both fight a
// common enemy with bases there). Counts 1 per unit (+1 on an improved tile); flags bit 0: units
// near the claiming base (or any, with friction/a revenge or betrayal mood) are warned (flags |= 3)
// or, if already warned, count 1 more (+2 more with a treaty); bit 1: also with agenda bit 0x200.
// Marks the units (state 0x400), sets faction_id's player flag 0x10000 when any are found.
// base_id_out: the claiming base of the last one; count_out: warned units already flagged.
ADDR(0x55EB80) int32_t GAMEAPI territory(int32_t faction_id, int32_t faction_id_with, int32_t flags,
                                         int32_t* base_id_out, int32_t* count_out);
// AI factions with a treaty trade technology.
ADDR(0x55D430) void GAMEAPI enemies_trade_tech(int32_t faction_id, int32_t faction_id_with);

// ---- second half of the module (0x54B140-0x55F930; diplomacy_b.cpp) ----
// Native emulation state of wants_to_attack's uninitialised slot array (known uninitialised read
// #4, docs/RECONSTRUCTION.md): [f] is faction f's slot. Not original memory.
extern int32_t WantsToAttackResidue[8];
// Whether faction_id is the overwhelming human leader (top ranked, more than max(4, (turn+25)/50)
// bases, difficulty above 1, intense rivalry or is_aggressive).
ADDR(0x539B70) int32_t GAMEAPI great_beelzebub(int32_t faction_id, int32_t is_aggressive);
// Both factions share their maps (visibility and seen improvements; traded_maps bits).
ADDR(0x543A20) void GAMEAPI trade_maps(int32_t faction_id, int32_t faction_id_with);
// Value of base_id to faction_id when faction_id_with offers or demands it (-1: none or not
// reachable): energy, size, nearby bases, facilities, projects, garrison; is_threat halves it
// where both have bases, is_gift multiplies by 4.
ADDR(0x54CB50) int32_t GAMEAPI value_of_base(int32_t base_id, int32_t faction_id, int32_t faction_id_with,
                                             int32_t is_threat, int32_t is_gift);
// Hands base_id over to faction_id: nearby units of the old owner move to its nearest base or
// change sides, production is reset, the base's supported units of the old owner are killed.
ADDR(0x54CF40) void GAMEAPI give_a_base(int32_t base_id, int32_t faction_id);
// Text helpers of the diplomacy dialogs (faction strings; get_insult_leader sets the parse
// gender defaults).
ADDR(0x5590E0) const char* GAMEAPI get_adjective_leader(int32_t faction_id);
ADDR(0x559100) const char* GAMEAPI get_adjective_insult_leader(int32_t faction_id);
ADDR(0x559120) const char* GAMEAPI get_adjective_faction(int32_t faction_id);
ADDR(0x559140) const char* GAMEAPI get_adjective_insult_faction(int32_t faction_id);
ADDR(0x559160) const char* GAMEAPI get_insult_leader(int32_t faction_id);
// Whether region is no real region (0 or 0x3F in the low 6 bits).
ADDR(0x5591C0) int32_t GAMEAPI bad_reg(int32_t region);
// Agenda bits of faction_id toward faction_id_with.
ADDR(0x5591E0) uint32_t GAMEAPI has_agenda(int32_t faction_id, int32_t faction_id_with, uint32_t agenda);
// value rounded down to a multiple of step (FUN_00559210).
ADDR(0x559210) int32_t GAMEAPI round_down(int32_t value, int32_t step);
// First contact of a human faction: the commlink dialog (communicate) or, in hotseat/PBEM and
// multiplayer, the pending-contact flags and NETMET messages; sets truce + informal truce.
ADDR(0x55C430) void GAMEAPI comm_check(int32_t faction_id, int32_t faction_id_with);
// Viewer messages (NetMsg): a pact partner attacks / ends the pact / signs a truce or treaty.
ADDR(0x55C730) void GAMEAPI pact_attacks(int32_t faction_id, int32_t faction_id_tgt);
ADDR(0x55CCA0) void GAMEAPI pact_unpact(int32_t faction_id, int32_t faction_id_with);
ADDR(0x55D040) void GAMEAPI pact_truce(int32_t faction_id, int32_t faction_id_with);
ADDR(0x55D190) void GAMEAPI pact_treaty(int32_t faction_id, int32_t faction_id_with);
// AI factions met on the map go to war (vendetta, map exchange, revenge moods may turn into
// grudges).
ADDR(0x55C840) void GAMEAPI enemies_war(int32_t faction_id, int32_t faction_id_with);
// A pact between two AI factions ends (messages to the viewer).
ADDR(0x55CE20) void GAMEAPI enemies_unpact(int32_t faction_id, int32_t faction_id_with);
// After a pact against faction_id_tgt: faction_id joins the war (or wants to talk to it).
ADDR(0x55D2A0) void GAMEAPI enemies_team_up(int32_t faction_id, int32_t faction_id_with, int32_t faction_id_tgt);
// Two AI factions that do not want war improve relations: truce, treaty, or a pact against a
// common enemy (else an existing pact ends). trade_tech: first trade technologies.
ADDR(0x55D760) void GAMEAPI enemies_treaty(int32_t faction_id, int32_t faction_id_with, int32_t trade_tech);
