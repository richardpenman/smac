// Random number generation (original module around 0x625750, plus the MSVC CRT generator).
#pragma once
#include "engine/memmap.h"

// Game LCG state (0x9BB568): x = x * 1664525 + 1013904223.
GLOBAL(uint32_t, GameRandomSeed, 0x9BB568);

ADDR(0x6257E0) void GAMEAPI game_reseed(uint32_t seed);
ADDR(0x625800) uint32_t GAMEAPI game_random_state();
// Uniform value in [min(lo,hi), max(lo,hi)).
ADDR(0x625810) int32_t GAMEAPI game_random(int32_t lo, int32_t hi);

// MSVC CRT srand/rand (holdrand lives in CRT per-thread data, outside the memory image).
ADDR_NATIVE(0x646010) void GAMEAPI crt_srand(uint32_t seed);
ADDR_NATIVE(0x64601D) int32_t GAMEAPI game_rand();
// Seeds the CRT generator and logs the seed (0x538FB0).
ADDR(0x538FB0) void GAMEAPI game_srand(uint32_t seed);

// CRT generator state access for the harness (native builds only).
uint32_t crt_rand_state();
void crt_set_rand_state(uint32_t state);
