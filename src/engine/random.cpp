#include "engine/random.h"

#include "engine/log.h"

void GAMEAPI game_reseed(uint32_t seed) { GameRandomSeed = seed; }

uint32_t GAMEAPI game_random_state() { return GameRandomSeed; }

int32_t GAMEAPI game_random(int32_t lo, int32_t hi) {
    if (lo > hi) {
        int32_t t = lo;
        lo = hi;
        hi = t;
    }
    GameRandomSeed = GameRandomSeed * 1664525u + 1013904223u;
    // The original multiplies and shifts unsigned (shr), not arithmetically.
    return lo + (int32_t)(((uint32_t)(hi - lo) * (GameRandomSeed & 0xFFFFu)) >> 16);
}

void GAMEAPI game_srand(uint32_t seed) {
    log_say_2("Reseed to", (int32_t)seed, 0, 0);
    crt_srand(seed);
}

#ifndef SMACX_HYBRID
static uint32_t g_crt_holdrand = 1;

void GAMEAPI crt_srand(uint32_t seed) { g_crt_holdrand = seed; }

int32_t GAMEAPI game_rand() {
    g_crt_holdrand = g_crt_holdrand * 214013u + 2531011u;
    return (int32_t)((g_crt_holdrand >> 16) & 0x7FFF);
}

uint32_t crt_rand_state() { return g_crt_holdrand; }
void crt_set_rand_state(uint32_t state) { g_crt_holdrand = state; }
#endif
