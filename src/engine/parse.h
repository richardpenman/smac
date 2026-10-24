// Text substitution: parse_string fills "$STRn", "$NUMn", "$<...>" markers from the parse slots set
// with parse_say/parse_says/parse_num (original module random, 0x625880-0x625FB0).
#pragma once
#include "engine/globals.h"

// Expands markers of src into dst:
//   $$ -> "$" kept with the following text, $NUMBERn / $NUMn / $HEXn -> ParseNumTable[n] (decimal),
//   $LINK< kept literally, $<g:a:b...> -> alternative chosen by gender/plurality,
//   any other "$...n" (first digit n) -> ParseStrBuffer[n].
// Returns 0, 3 (null argument) or 14 (bad slot number).
ADDR(0x625880) int32_t GAMEAPI parse_string(const char* src, char* dst);
// Sets number slot n (0-9). Returns 0, or 3 for a bad slot.
ADDR(0x625E30) int32_t GAMEAPI parse_num(int32_t n, int32_t value);
// Sets string slot n to the string-table string str (Strings_get) with gender and plurality
// (negative: GenderDefault / PluralDefault). Returns 0, or 3 for a bad slot.
ADDR(0x625E50) int32_t GAMEAPI parse_say(int32_t n, const char* str, int32_t gender, int32_t plural);
// As parse_say for any string; also fails (3) for a null string.
ADDR(0x625EC0) int32_t GAMEAPI parse_says(int32_t n, const char* str, int32_t gender, int32_t plural);
