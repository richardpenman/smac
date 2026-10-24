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
