// The two random generators (engine/random.h): the game's LCG (game_random, state GameRandomSeed,
// shared with the UI palette code in the original) and MSVC's rand() (game_rand). Every harness
// snapshot compares both states (NAME.rng), so these are proven against the original; the values
// below follow from the constants 1664525 / 1013904223 and 214013 / 2531011.
#include "engine/random.h"

#include "unit.h"

TEST_CASE("game_random: LCG step, then lo + ((hi - lo) * (state & 0xFFFF)) >> 16 unsigned") {
    SaveGlobal<uint32_t> keep(GameRandomSeed);
    game_reseed(0);
    CHECK(game_random(0, 0x10000) == 0xF35F);  // 1013904223 = 0x3C6EF35F
    CHECK(game_random_state() == 0x3C6EF35Fu);
    uint32_t next = 0x3C6EF35Fu * 1664525u + 1013904223u;
    CHECK(game_random(0, 0x10000) == (int32_t)(next & 0xFFFF));
    CHECK(game_random_state() == next);

    // Inverted bounds are swapped; equal bounds still advance the state.
    game_reseed(12345);
    int32_t a = game_random(0, 100);
    game_reseed(12345);
    CHECK(game_random(100, 0) == a);
    uint32_t before = game_random_state();
    CHECK(game_random(7, 7) == 7);
    CHECK(game_random_state() != before);

    // hi itself is never reached.
    game_reseed(99);
    for (int32_t i = 0; i < 1000; i++) {
        int32_t v = game_random(-50, 50);
        REQUIRE(v >= -50);
        REQUIRE(v < 50);
    }
    // The product is 32-bit unsigned: for spans of 65536 and more it wraps, so the result lies in
    // [lo, lo + 65536) rather than spreading over the whole span (as in the original).
    for (int32_t i = 0; i < 1000; i++) {
        int32_t v = game_random(-1000000, 1000000);
        REQUIRE(v >= -1000000);
        REQUIRE(v < -1000000 + 65536);
    }
}

TEST_CASE("game_rand: MSVC rand()") {
    crt_srand(1);
    CHECK(game_rand() == 41);
    CHECK(game_rand() == 18467);
    crt_srand(0);
    CHECK(game_rand() == 38);  // (2531011 >> 16) & 0x7FFF
    crt_srand(1);
}
