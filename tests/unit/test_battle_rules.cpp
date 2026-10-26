// Pure combat formulas (engine/battle_rules.h). Rows marked "recorded" are inputs and results taken
// from native games of the harness scenarios (tiny20, late140, probe131, cov_missiles), which are
// identical to the original's; the other expectations follow the original's code.
#include "engine/battle_rules.h"

#include "unit.h"

using namespace battle_rules;

TEST_CASE("percent_bonus: (percent + 100) * value / 100, truncated toward zero") {
    // recorded
    CHECK(percent_bonus(224, -50) == 112);
    CHECK(percent_bonus(224, -30) == 156);
    CHECK(percent_bonus(224, -10) == 201);
    CHECK(percent_bonus(576, 10) == 633);
    CHECK(percent_bonus(1024, 25) == 1280);
    CHECK(percent_bonus(1152, 50) == 1728);
    CHECK(percent_bonus(1080, 100) == 2160);
    // from the code
    CHECK(percent_bonus(7, 0) == 7);
    CHECK(percent_bonus(-7, 50) == -10);
    CHECK(percent_bonus(5, -100) == 0);
}

TEST_CASE("attack_odds: recorded ratings of attacks (is_real 0)") {
    struct Row {
        int32_t atk_hp, max_atk, atk_power, offense, def_hp, max_def, def_power, defense;
        bool arty;
        int32_t odds;
    };
    const Row rows[] = {
        {20, 20, 1, 2048, 38, 40, 1, 5632, false, 3}, {40, 40, 1, 4032, 20, 20, 1, 2560, false, 12},
        {40, 40, 4, 537, 10, 10, 1, 864, false, 1},   {10, 10, 1, 768, 5, 10, 1, 720, true, 8},
        {10, 10, 1, 768, 6, 10, 1, 720, true, 7},     {10, 10, 1, 230, 37, 40, 4, 320, false, 24},
        {15, 20, 1, 4992, 40, 40, 1, 9600, false, 3}, {10, 10, 1, 1760, 10, 10, 1, 2, false, 4693},
        {10, 10, 1, 1408, 10, 10, 1, 3, false, 2816}, {10, 10, 1, 1152, 10, 10, 1, 168, true, 27},
        {10, 10, 1, 1280, 10, 10, 1, 280, true, 18},  {10, 10, 1, 448, 10, 10, 1, 1008, true, 1},
    };
    for (const Row& r : rows) {
        CAPTURE(r.odds);
        CHECK(attack_odds(r.atk_hp, r.max_atk, r.atk_power, r.offense, r.def_hp, r.max_def, r.def_power, r.defense,
                          r.arty) == r.odds);
    }
}

TEST_CASE("reduce_odds: common factors, then scaled down while both are large") {
    int32_t a = 6, b = 4;
    reduce_odds(a, b);
    CHECK(a == 3);  // 2 divides both; then min(3, 2) <= 3 stops
    CHECK(b == 2);
    a = 100, b = 100;
    reduce_odds(a, b);
    CHECK(a == 1);
    CHECK(b == 1);
    a = 7, b = 5;
    reduce_odds(a, b);
    CHECK(a == 7);
    CHECK(b == 5);
    a = 0, b = 9;  // no common factors with 0 (stops at d = 9)
    reduce_odds(a, b);
    CHECK(a == 0);
    CHECK(b == 9);
    // Primes: no factors; 30011 and 20011 are scaled by 10000.
    a = 30011, b = 20011;
    reduce_odds(a, b);
    CHECK(a == 3);
    CHECK(b == 2);
    a = 1201, b = 701;  // both > 199: / 100
    reduce_odds(a, b);
    CHECK(a == 12);
    CHECK(b == 7);
}

TEST_CASE("bombard_damage_range and bombard_hp_floor: recorded bombardments") {
    CHECK(bombard_damage_range(3, 2, 768, 800) == 2);
    CHECK(bombard_damage_range(3, 2, 1280, 360) == 6);
    CHECK(bombard_damage_range(3, 2, 384, 480) == 2);
    CHECK(bombard_damage_range(3, 2, 768, 4) == 289);
    CHECK(bombard_damage_range(3, 2, 576, 448) == 2);
    // from the code: the denominator is at least 1, the range 1..999
    CHECK(bombard_damage_range(3, 2, 100, 0) == 301);
    CHECK(bombard_damage_range(3, 2, 10000, 1) == 999);
    CHECK(bombard_damage_range(0, 2, 100, 100) == 1);
    // recorded
    CHECK(bombard_hp_floor(100, 10) == 0);
    CHECK(bombard_hp_floor(50, 10) == 5);
    CHECK(bombard_hp_floor(50, 20) == 10);
    CHECK(bombard_hp_floor(50, 40) == 20);
    CHECK(bombard_hp_floor(99, 10) == 1);  // rounded up
}

TEST_CASE("attack_moves: at most a full move") {
    // recorded (move rate 3: one move is 3 movement points)
    CHECK(attack_moves(12, 3) == 3);
    CHECK(attack_moves(30, 3) == 3);
    // from the code
    CHECK(attack_moves(2, 3) == 2);
    CHECK(attack_moves(0, 3) == 0);
    CHECK(attack_moves(5, -1) == 0);
}

TEST_CASE("withdraw_angle: 0 toward the attacker, 4 straight away") {
    // The attack came in direction dir: dir ^ 4 points back to the attacker.
    for (int32_t dir = 0; dir < 8; dir++) {
        CHECK(withdraw_angle(dir ^ 4, dir) == 0);
        CHECK(withdraw_angle(dir, dir) == 4);
        CHECK(withdraw_angle(((dir ^ 4) + 1) & 7, dir) == 1);
        CHECK(withdraw_angle(((dir ^ 4) + 7) & 7, dir) == 1);
        CHECK(withdraw_angle(((dir ^ 4) + 3) & 7, dir) == 3);
        CHECK(withdraw_angle(((dir ^ 4) + 5) & 7, dir) == 3);
    }
}
