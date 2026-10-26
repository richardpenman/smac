// Pure rule computations of the base upkeep (base_yield, base_growth, base_production), extracted
// so they can be unit tested (tests/unit/test_base_rules.cpp). No game state: every input is a
// parameter. These are pieces of original functions, not original functions themselves (no ADDR).
#pragma once
#include <cstdint>

#include "engine/types/enums.h"

namespace base_rules {

// Worker priorities of base_yield's tile choice: the AI personality, or a human governor's.
struct YieldPriorities {
    int32_t growth = 0, tech = 0, wealth = 0, power = 0, fight = 0;
};

// Priorities of a governed human base, from its governor_flags (bits 24..27: explore/discover/build/
// conquer); `ai_fight` is the faction's AI_fight, used when no priority is set.
inline YieldPriorities governor_priorities(uint32_t governor_flags, int32_t ai_fight) {
    YieldPriorities p;
    p.growth = (governor_flags >> 24) & 1;
    p.tech = (governor_flags >> 24) & 2;
    p.wealth = (governor_flags >> 25) & 2;
    p.power = (governor_flags >> 26) & 2;
    if (p.power) p.fight = 2;
    else if (p.growth || p.wealth) p.fight = 0;
    else if (p.tech) p.fight = -2;
    else p.fight = ai_fight;
    return p;
}

// Nutrient/mineral/energy intake from orbital satellites: half (rounded up) unless the base has an
// Aerospace Complex or the faction a Space Elevator / Cloudbase Academy; at most the population.
inline int32_t satellite_intake(int32_t satellites, bool full_effect, int32_t pop_size) {
    int32_t intake = full_effect ? satellites : (satellites + 1) / 2;
    return intake > pop_size ? pop_size : intake;
}

inline int32_t clamp_1_99(int32_t v) { return v < 1 ? 1 : (v > 99 ? 99 : v); }

// The base's state as base_yield scores a tile or a specialist for its next worker.
struct WorkerScoring {
    YieldPriorities prio;
    int32_t pop_size = 0;
    int32_t workers_left = 0;  // BaseWorkersLeft
    int32_t nutrient_intake = 0;  // nutrient_intake_2 so far
    int32_t mineral_intake = 0;  // mineral_intake_2 so far
    int32_t energy_intake = 0;  // energy_intake_2 so far
    int32_t specialist_adjust = 0;  // specialists chosen so far
    int32_t req_citizen = 2;  // Rules.nutrient_intake_req_citizen (never 0 in the rules)
    int32_t forces_maint = 0;  // BaseForcesMaintCost
    bool building_project = false;  // queue_items[0] < -70: a secret project
};

// Score of working a tile with the given yields. `food_short` is set when the base already has
// enough nutrients for its population (the original's BaseYieldFoodShort = 1).
inline int32_t tile_work_score(const WorkerScoring& s, int32_t food, int32_t prod, int32_t energy, bool* food_short) {
    int32_t extra = 0;
    int32_t need = s.pop_size * s.req_citizen;
    if (s.workers_left / s.req_citizen + s.nutrient_intake < need) {
        extra = s.nutrient_intake + s.workers_left >= need ? 2 : 8;
        if (food >= 0 && food <= extra) food *= 2;
        else if (food >= 0) food += extra;
    } else {
        *food_short = true;
    }
    int32_t pop = s.pop_size;
    food *= 2;
    prod *= 2;
    energy *= 2;
    if (s.nutrient_intake <= pop + need && !food) {
        prod /= 2;
        energy /= 2;
    }
    if (!prod && !extra) food /= 2;
    int32_t div_food = clamp_1_99((s.nutrient_intake - pop / 3 - need + s.workers_left) * 2 - 2);
    int32_t score = (s.prio.growth + 4) * food * 256 / div_food;
    int32_t k = s.building_project ? 2 : -s.prio.tech;
    int32_t div_prod = clamp_1_99((s.mineral_intake - s.forces_maint + 1) / 2);
    score += (k + s.prio.fight + s.prio.power + s.prio.wealth + 6) * pop * prod * 64 / div_prod;
    int32_t pop_e = clamp_1_99(pop - s.prio.power);
    int32_t div_energy = clamp_1_99(s.energy_intake);
    score += (s.prio.wealth + s.prio.tech * 2 + 1) * pop_e * energy * 64 / div_energy;
    return score;
}

// Score of a specialist with the given Citizen bonuses, compared with the tiles' scores.
inline int32_t specialist_work_score(const WorkerScoring& s, int32_t psych, int32_t econ, int32_t labs) {
    int32_t div = clamp_1_99(s.energy_intake + s.specialist_adjust);
    int32_t pop = s.pop_size;
    return psych * 64 + (s.prio.wealth + 2) * pop * econ * 64 / div + (s.prio.tech + 2) * pop * labs * 64 / div;
}

// Eco-damage of one radius tile: its terraforming items (counted twice when worked), plus
// borehole 8, mirror 6, condenser 4, minus 1 for a forest; Tree Farm and Hybrid Forest halve it,
// both together cancel it.
inline int32_t tile_eco_damage(uint32_t items, bool worked, bool tree_farm, bool hybrid_forest) {
    int32_t damage = 0;
    for (uint32_t v = items & 0x1C8805C; v; v &= v - 1) damage++;  // bit_count
    if (worked) damage *= 2;
    if (items & BIT_THERMAL_BORE) damage += 8;
    if (items & BIT_ECH_MIRROR) damage += 6;
    if (items & BIT_CONDENSER) damage += 4;
    if (items & BIT_FOREST) damage--;
    if (tree_farm && hybrid_forest) damage = 0;
    if (tree_farm || hybrid_forest) damage /= 2;
    return damage;
}

// Minerals kept after completing an item: a human base keeps at most the retooling exemption
// (and at most its surplus, never below 0); an AI base keeps everything.
inline int32_t carried_minerals(int32_t accumulated, int32_t surplus, int32_t exemption, bool human) {
    int32_t limit = exemption <= surplus ? exemption : surplus;
    if (accumulated > limit && human) return limit < 0 ? 0 : limit;
    return accumulated;
}

// base_growth's low-nutrient warning: the stock is falling and will be gone within five turns
// (but is not already gone this turn).
inline bool low_nutrients_due(int32_t stock, int32_t surplus) {
    if (surplus >= 0) return false;
    return !(stock - surplus < 0 || stock + 5 * surplus >= 0);
}

// Voice file number of a completed facility (voices\facNN.mp3): satellites 65..69 use 34..38.
inline int32_t facility_voice_number(int32_t fac) { return fac >= 65 && fac <= 69 ? fac - 31 : fac; }

}  // namespace base_rules
