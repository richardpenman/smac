// File mapping, string and math utilities (original module around 0x6283A0-0x62D3A0).
#pragma once
#include "engine/memmap.h"

// Integer square root (Newton iteration).
ADDR(0x6290E0) int32_t GAMEAPI quick_root(int32_t value);

// Binary string to integer ('0'/'1' digits, stops at the first other character).
ADDR(0x6288D0) int32_t GAMEAPI btoi(const char* str);
// Hex string to integer (stops at the first non-hex character).
ADDR(0x6288F0) int32_t GAMEAPI htoi(const char* str);
// Number with optional prefix: "0b" binary, "0x" hex, "0d" decimal, else decimal (atoi).
ADDR(0x628950) int32_t GAMEAPI stoi(const char* str);
// Exchanges two 32-bit values (xor swap: both pointers must differ).
ADDR(0x628A50) void GAMEAPI swap(int32_t* a, int32_t* b);
// Exchanges two bytes (xor swap).
ADDR(0x628A80) void GAMEAPI swap_bytes(uint8_t* a, uint8_t* b);
// Number of significant bits (arithmetic shifts: never ends for negative values).
ADDR(0x628AB0) int32_t GAMEAPI bit_count_signed(int32_t value);
// 16.16 fixed-point division: (a << 16) / b.
ADDR(0x628AD0) int32_t GAMEAPI fixed_div(int32_t a, int32_t b);
// Searches backwards from end (inclusive) towards start (exclusive) for ch.
ADDR(0x628AF0) char* GAMEAPI memrchr(const char* start, const char* end, int32_t ch);
// First decimal digit in str, or nullptr.
ADDR(0x628B30) char* GAMEAPI findnum(const char* str);
// Clamps value to [min, max] (min wins when max < min). Original address in the ui module.
ADDR(0x422F00) int32_t GAMEAPI range(int32_t value, int32_t min, int32_t max);
