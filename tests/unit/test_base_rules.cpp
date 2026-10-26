// Pure rules of the base upkeep and of base_build (engine/base_rules.h). Rows marked "recorded" are inputs and results
// taken from native games of the harness scenarios (tiny20, late140, cov_late300, small_wet), which
// are identical to the original's; the other expectations follow the original's code.
#include "engine/base_rules.h"

#include "unit.h"

using namespace base_rules;

namespace {
WorkerScoring scoring(int32_t growth, int32_t tech, int32_t wealth, int32_t power, int32_t fight, int32_t pop,
                      int32_t workers, int32_t nutrient, int32_t mineral, int32_t energy, int32_t adjust,
                      int32_t forces_maint, bool project) {
    WorkerScoring s;
    s.prio.growth = growth;
    s.prio.tech = tech;
    s.prio.wealth = wealth;
    s.prio.power = power;
    s.prio.fight = fight;
    s.pop_size = pop;
    s.workers_left = workers;
    s.nutrient_intake = nutrient;
    s.mineral_intake = mineral;
    s.energy_intake = energy;
    s.specialist_adjust = adjust;
    s.req_citizen = 2;
    s.forces_maint = forces_maint;
    s.building_project = project;
    return s;
}
}  // namespace

TEST_CASE("tile_work_score: recorded scores of the AI's tile choice") {
    struct Row {
        int32_t growth, tech, wealth, power, fight, pop, workers, nutrient, mineral, energy, forces;
        bool project;
        int32_t food, prod, energy_yield, score;
        bool food_short;
    };
    const Row rows[] = {
        // still short of nutrients: food weighted up
        {0, 1, 0, 1, 0, 13, 9, 17, 8, 27, 0, false, 1, 1, 3, 7104, false},
        {1, 1, 0, 0, 0, 7, 6, 5, 4, 4, 1, false, 3, 0, 4, 18048, false},
        {0, 0, 1, 0, -1, 10, 6, 16, 5, 3, 2, false, 1, 1, 0, 7936, false},
        {1, 0, 0, 0, -1, 2, 2, 2, 1, 2, 0, false, 2, 3, 2, 14336, false},
        {0, 1, 0, 1, 0, 9, 1, 17, 15, 20, 3, false, 0, 4, 1, 2380, false},
        {1, 1, 0, 0, 0, 9, 4, 9, 11, 15, 7, false, 0, 1, 1, 1555, false},
        {0, 0, 1, 0, -1, 2, 1, 3, 2, 6, 0, false, 0, 0, 0, 0, false},
        {0, 1, 0, 0, 0, 10, 9, 4, 4, 15, 0, true, 3, 0, 12, 15360, false},
        {1, 1, 0, 0, 0, 9, 5, 12, 7, 13, 3, true, 0, 0, 1, 132, false},
        // enough nutrients: food_short set, no food bonus
        {0, 1, 0, 1, 0, 15, 2, 29, 17, 22, 4, false, 1, 0, 3, 1757, true},
        {1, 1, 0, 0, 0, 11, 2, 23, 10, 48, 5, false, 2, 1, 6, 7994, true},
        {0, 0, 1, 0, -1, 8, 2, 18, 7, 5, 4, false, 3, 0, 0, 1536, true},
        {1, 0, 0, 0, -1, 1, 1, 2, 1, 1, 0, false, 1, 2, 1, 3968, true},
        {1, 1, 0, 0, 0, 15, 4, 30, 10, 25, 1, false, 0, 1, 1, 1075, true},
        {1, 0, 0, 0, -1, 1, 1, 2, 1, 2, 1, false, 0, 2, 1, 672, true},
        {0, 1, 0, 1, 0, 12, 2, 26, 14, 12, 1, true, 1, 0, 0, 1024, true},
        {1, 1, 0, 0, 0, 9, 1, 19, 13, 22, 4, true, 3, 0, 1, 3997, true},
        {1, 1, 0, 0, 0, 8, 1, 16, 11, 19, 4, true, 0, 0, 0, 0, true},
    };
    for (const Row& r : rows) {
        CAPTURE(r.score);
        WorkerScoring s = scoring(r.growth, r.tech, r.wealth, r.power, r.fight, r.pop, r.workers, r.nutrient, r.mineral,
                                  r.energy, 0, r.forces, r.project);
        bool food_short = false;
        CHECK(tile_work_score(s, r.food, r.prod, r.energy_yield, &food_short) == r.score);
        CHECK(food_short == r.food_short);
    }
}

TEST_CASE("specialist_work_score: recorded specialist scores") {
    // wealth, tech, pop, energy intake, specialists so far, psych/econ/labs bonus -> score
    struct Row {
        int32_t wealth, tech, pop, energy, adjust, psych, econ, labs, score;
    };
    const Row rows[] = {
        {0, 1, 14, 14, 3, 0, 3, 2, 632}, {0, 1, 13, 69, 0, 0, 3, 2, 144}, {0, 1, 10, 16, 0, 1, 0, 3, 424},
        {0, 1, 6, 17, 0, 0, 3, 2, 270},  {0, 1, 10, 24, 0, 2, 0, 0, 128}, {0, 1, 11, 14, 1, 0, 3, 2, 562},
    };
    for (const Row& r : rows) {
        WorkerScoring s = scoring(0, r.tech, r.wealth, 0, 0, r.pop, 0, 0, 0, r.energy, r.adjust, 0, false);
        CHECK(specialist_work_score(s, r.psych, r.econ, r.labs) == r.score);
    }
}

TEST_CASE("tile_eco_damage: recorded tiles, and the forest facilities") {
    CHECK(tile_eco_damage(0x40301000, false, false, false) == -1);  // forest, nothing else
    CHECK(tile_eco_damage(0x19000, false, false, false) == 1);  // farm
    CHECK(tile_eco_damage(0x19000, true, false, false) == 2);  // worked: twice
    CHECK(tile_eco_damage(0x10100c, false, false, false) == 2);
    CHECK(tile_eco_damage(0x10100c, true, false, false) == 4);
    CHECK(tile_eco_damage(0x401091cc, false, false, false) == 4);
    CHECK(tile_eco_damage(0x4018904c, true, false, false) == 10);
    CHECK(tile_eco_damage(0x501004, false, false, false) == 6);  // condenser
    // Borehole and mirror (from the code: +8, +6, also counted as items).
    CHECK(tile_eco_damage(BIT_THERMAL_BORE, false, false, false) == 9);
    CHECK(tile_eco_damage(BIT_ECH_MIRROR, false, false, false) == 7);
    // Tree Farm or Hybrid Forest halve the damage (toward zero), both cancel it.
    CHECK(tile_eco_damage(0x4018904c, true, true, false) == 5);
    CHECK(tile_eco_damage(0x4018904c, true, false, true) == 5);
    CHECK(tile_eco_damage(0x4018904c, true, true, true) == 0);
    CHECK(tile_eco_damage(0x40301000, false, true, false) == 0);
}

TEST_CASE("carried_minerals: the retooling exemption applies to human bases only") {
    // recorded (AI bases keep everything)
    CHECK(carried_minerals(38, 15, 10, false) == 38);
    CHECK(carried_minerals(8, 4, 10, false) == 8);
    CHECK(carried_minerals(0, 7, 10, false) == 0);
    // human: at most min(exemption, surplus), never negative
    CHECK(carried_minerals(38, 15, 10, true) == 10);
    CHECK(carried_minerals(38, 4, 10, true) == 4);
    CHECK(carried_minerals(3, 15, 10, true) == 3);
    CHECK(carried_minerals(5, -2, 10, true) == 0);
    CHECK(carried_minerals(-3, -2, 10, true) == -3);
}

TEST_CASE("low_nutrients_due: falling stock, gone within five turns") {
    // recorded
    CHECK(!low_nutrients_due(22, -4));
    CHECK(!low_nutrients_due(28, -5));
    CHECK(!low_nutrients_due(40, -2));
    CHECK(low_nutrients_due(0, -1));
    CHECK(low_nutrients_due(0, -4));
    // from the code
    CHECK(!low_nutrients_due(5, 0));
    CHECK(low_nutrients_due(19, -4));
    CHECK(!low_nutrients_due(20, -4));
    CHECK(!low_nutrients_due(-2, -1));  // already gone (stock - surplus < 0)
    CHECK(low_nutrients_due(-1, -1));
}

TEST_CASE("satellite_intake: half rounded up without the orbital facilities, at most the population") {
    CHECK(satellite_intake(0, false, 5) == 0);
    CHECK(satellite_intake(1, false, 5) == 1);
    CHECK(satellite_intake(4, false, 5) == 2);
    CHECK(satellite_intake(5, false, 5) == 3);
    CHECK(satellite_intake(4, true, 5) == 4);
    CHECK(satellite_intake(9, true, 5) == 5);
    CHECK(satellite_intake(20, false, 3) == 3);
}

TEST_CASE("governor_priorities: governor flags bits 24..27") {
    auto p = governor_priorities(0, 1);
    CHECK((p.growth | p.tech | p.wealth | p.power) == 0);
    CHECK(p.fight == 1);  // no priority: the AI personality's fight
    p = governor_priorities(1u << 24, 1);
    CHECK(p.growth == 1);
    CHECK(p.fight == 0);
    p = governor_priorities(1u << 25, -1);
    CHECK(p.tech == 2);
    CHECK(p.fight == -2);
    p = governor_priorities(1u << 26, -1);
    CHECK(p.wealth == 2);
    CHECK(p.fight == 0);
    p = governor_priorities((1u << 27) | (1u << 25), 0);
    CHECK(p.power == 2);
    CHECK(p.tech == 2);
    CHECK(p.fight == 2);
}

TEST_CASE("facility_voice_number: satellites use 34..38") {
    CHECK(facility_voice_number(1) == 1);
    CHECK(facility_voice_number(64) == 64);
    CHECK(facility_voice_number(65) == 34);
    CHECK(facility_voice_number(69) == 38);
    CHECK(facility_voice_number(70) == 70);
}

TEST_CASE("extra_unit_support: one more unit beyond the free support") {
    // recorded: SUPPORT rating, population, units supported -> extra minerals
    CHECK(extra_unit_support(-3, 10, 3) == 1);
    CHECK(extra_unit_support(-2, 2, 0) == 0);
    CHECK(extra_unit_support(-2, 10, 6) == 1);
    CHECK(extra_unit_support(-1, 1, 0) == 0);
    CHECK(extra_unit_support(-1, 1, 1) == 1);
    CHECK(extra_unit_support(0, 1, 0) == 0);
    CHECK(extra_unit_support(0, 10, 6) == 1);
    CHECK(extra_unit_support(1, 10, 5) == 1);
    CHECK(extra_unit_support(2, 1, 0) == 0);
    CHECK(extra_unit_support(2, 10, 4) == 1);
    CHECK(extra_unit_support(4, 10, 5) == 0);
    CHECK(extra_unit_support(4, 4, 4) == 1);
    // from the code: support -4 or less costs 2, -3 costs 1 from the first unit
    CHECK(extra_unit_support(-4, 5, 0) == 2);
    CHECK(extra_unit_support(-3, 5, 0) == 1);
    CHECK(extra_unit_support(-2, 5, 1) == 1);
    CHECK(extra_unit_support(1, 5, 2) == 0);
    CHECK(extra_unit_support(1, 5, 3) == 1);
}

TEST_CASE("governor_unit_mask: permissions a governed base needs per unit plan") {
    CHECK(governor_unit_mask(0, 0, 0, false) == 0x200u);
    CHECK(governor_unit_mask(1, 0, 0, false) == 0x1000u);
    CHECK(governor_unit_mask(1, 0x200, 0, false) == 0x200u);
    CHECK(governor_unit_mask(2, 0, 0, false) == 0x1000u);
    CHECK(governor_unit_mask(3, 0x1000, 0, false) == 0x1000u);  // land scout as defense
    CHECK(governor_unit_mask(3, 0x1000, 1, false) == 0x400000u);
    CHECK(governor_unit_mask(3, 0, 0, false) == 0x400000u);
    CHECK(governor_unit_mask(7, 0, 0, false) == 0x800000u);
    CHECK(governor_unit_mask(10, 0, 0, false) == 0x800000u);
    CHECK(governor_unit_mask(8, 0, 0, true) == 0xA0000u);
    CHECK(governor_unit_mask(12, 0, 0, false) == 0u);
    CHECK(governor_unit_mask(12, 0, 0, true) == 0x80000u);
}
