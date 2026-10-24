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
// Diplomacy dialog between faction_id (human) and faction_id_with (AI or human); a2 selects the
// opening. Mostly UI with diplomatic state changes (later package).
ADDR(0x54FFD0) int32_t GAMEAPI communicate(int32_t faction_id, int32_t faction_id_with, int32_t a3);
// Multiplayer: takes / releases the global diplomatic lock. diplo_lock returns non-zero when the
// player gave up waiting.
ADDR(0x539820) int32_t GAMEAPI diplo_lock(int32_t timeout);
ADDR(0x5398C0) void GAMEAPI diplo_unlock();
// Major atrocity (planet buster) by faction_id against faction_id_vs (-1 none): sanctions, the
// council's and every faction's reactions.
ADDR(0x55F450) void GAMEAPI major_atrocity(int32_t faction_id, int32_t faction_id_vs);
