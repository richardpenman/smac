// Pure rules of the AI (engine/ai_rules.h). The expectations follow the original's code (the
// blocks of can_terraform, air_power and enemy_strategy they were taken from); the functions they
// came from are checked against the original by fcompare and the harness scenarios.
#include "engine/ai_rules.h"

#include "unit.h"

using namespace ai_rules;

TEST_CASE("land_mine_order: mineral bonus and rocky tiles") {
    // bonus 2 (minerals) on a rocky tile: the mine in any case
    CHECK(land_mine_order(0, 2, 2, 0, 0, false, false, false) == ORDER_MINE);
    CHECK(land_mine_order(ORDER_SOLAR_COLLECTOR, 2, 2, 1, 3, true, true, true) == ORDER_MINE);
    // a mineral bonus alone chooses the mine before the other tests
    CHECK(land_mine_order(0, 2, 0, 1, 3, false, false, false) == ORDER_MINE);
    // rocky without a mineral bonus: the mine
    CHECK(land_mine_order(ORDER_SOLAR_COLLECTOR, 0, 2, 2, 0, false, false, false) == ORDER_MINE);
}

TEST_CASE("land_mine_order: flat and rolling tiles") {
    // rolling (rocky 1) at elevation 0 when mines keep their nutrients
    CHECK(land_mine_order(0, 0, 1, 0, 2, false, false, true) == ORDER_MINE);
    CHECK(land_mine_order(0, 0, 1, 1, 2, false, false, true) == 0);
    // an energy bonus (3) with the checks: the mine
    CHECK(land_mine_order(0, 3, 0, 1, 2, false, false, true) == ORDER_MINE);
    // at most one nutrient (counting a farm to build): the checks
    CHECK(land_mine_order(0, 0, 1, 0, 0, false, false, false) == ORDER_MINE);
    CHECK(land_mine_order(0, 0, 0, 0, 1, true, false, false) == 0);
    // two nutrients on a rolling tile: no mine, the solar collector kept
    CHECK(land_mine_order(ORDER_SOLAR_COLLECTOR, 0, 1, 1, 1, false, true, false) == ORDER_SOLAR_COLLECTOR);
    // three nutrients on a flat tile: only the flag's farm rule
    CHECK(land_mine_order(0, 0, 0, 1, 2, false, false, false) == 0);
    CHECK(land_mine_order(0, 0, 0, 1, 3, true, true, false) == ORDER_MINE);
    CHECK(land_mine_order(0, 0, 0, 1, 3, false, true, false) == 0);
    CHECK(land_mine_order(ORDER_SOLAR_COLLECTOR, 0, 0, 1, 3, true, true, false) == ORDER_SOLAR_COLLECTOR);
    // the flag's farm rule also after the checks
    CHECK(land_mine_order(0, 0, 1, 1, 0, true, true, true) == ORDER_MINE);
    CHECK(land_mine_order(0, 0, 1, 1, 0, true, false, true) == 0);
}

TEST_CASE("air_reach: chassis ranges") {
    // unlimited range (0): six turns of moves, both reaches
    AirReach r = air_reach(0, 12, 3, false, 0, 3);
    CHECK(r.attack == 24);
    CHECK(r.ferry == 24);
    // one-turn range (1): what is left this turn, the attack one turn more when undamaged
    r = air_reach(1, 30, 6, false, 0, 3);
    CHECK(r.attack == 18);
    CHECK(r.ferry == 8);
    r = air_reach(1, 30, 6, true, 0, 3);
    CHECK(r.attack == 8);
    CHECK(r.ferry == 8);
    // moves left are clamped to 0..999
    r = air_reach(1, 3, 9, false, 0, 3);
    CHECK(r.attack == 1);
    CHECK(r.ferry == 0);
    // longer ranges: the turns left in the air
    r = air_reach(2, 30, 0, false, 0, 3);
    CHECK(r.attack == 20);
    CHECK(r.ferry == 20);
    r = air_reach(2, 30, 12, false, 1, 3);  // the last turn: only what is left
    CHECK(r.attack == 6);
    CHECK(r.ferry == 6);
    r = air_reach(3, 24, 6, true, 1, 3);
    CHECK(r.attack == 14);
    CHECK(r.ferry == 14);
}

TEST_CASE("bombing_score: value per leg") {
    CHECK(bombing_score(640, 1, 20) == 640);
    CHECK(bombing_score(640, 10, 20) == 640);
    CHECK(bombing_score(640, 11, 20) == 320);
    CHECK(bombing_score(640, 21, 20) == 213);
    // the leg is clamped to 1..99
    CHECK(bombing_score(100, 4, 1) == 25);
    CHECK(bombing_score(100, 200, 400) == 33);
    CHECK(bombing_score(90, 0, 20) == 90);
}

TEST_CASE("attack_force_limit: two thirds of the region's bases, 3 to 8") {
    CHECK(attack_force_limit(0) == 3);
    CHECK(attack_force_limit(4) == 3);
    CHECK(attack_force_limit(6) == 4);
    CHECK(attack_force_limit(9) == 6);
    CHECK(attack_force_limit(12) == 8);
    CHECK(attack_force_limit(30) == 8);
}

TEST_CASE("garrison_short: wanted defenders against those present and a margin") {
    // up to two wanted defenders the margin covers them all: never short
    CHECK_FALSE(garrison_short(1, 1, 0));
    CHECK_FALSE(garrison_short(2, 2, 0));
    CHECK(garrison_short(3, 3, 0));  // margin 2
    CHECK_FALSE(garrison_short(3, 3, 1));
    CHECK(garrison_short(6, 6, 1));  // margin 4
    CHECK_FALSE(garrison_short(6, 6, 2));
}
