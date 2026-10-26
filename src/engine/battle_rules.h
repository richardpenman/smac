// Pure combat formulas of battle_compute and battle_resolve, extracted so they can be unit tested
// (tests/unit/test_battle_rules.cpp). No game state: every input is a parameter. These are pieces
// of original functions, not original functions themselves (no ADDR).
#pragma once
#include <cstdint>

namespace battle_rules {

// A strength raised (or, negative, lowered) by a percentage, as the combat modifiers compute it:
// (percent + 100) * value / 100, truncated toward zero.
inline int32_t percent_bonus(int32_t value, int32_t percent) { return (percent + 100) * value / 100; }

// The odds that battle_resolve returns when it only rates an attack (is_real 0): the attacker's
// remaining strength against the defender's, scaled by 8 (4 for artillery). A side's power is the
// damage it takes per lost round: its reactor in psi combat, else 1.
inline int32_t attack_odds(int32_t atk_hp, int32_t max_atk_hp, int32_t atk_power, int32_t offense, int32_t def_hp,
                           int32_t max_def_hp, int32_t def_power, int32_t defense, bool artillery) {
    return (artillery ? 4 : 8) * (atk_hp * def_power * offense / max_atk_hp) /
           (def_hp * atk_power * defense / max_def_hp + 1);
}

// The odds shown to a human attacker (GOODIDEA): the fraction a : b reduced by common factors
// below min(a, b), then scaled down while both numbers are large.
inline void reduce_odds(int32_t& a, int32_t& b) {
    for (int32_t d = 2;; d++) {
        int32_t lo = b <= a ? b : a;  // min(a, b)
        if (lo <= d) break;
        for (; a != 0 && b != 0 && a % d == 0 && b % d == 0; b /= d) a /= d;
    }
    for (; a > 19999 && b > 19999; b /= 10000) a /= 10000;
    for (; a > 9999 && b > 9999; b /= 5000) a /= 5000;
    for (; a > 1999 && b > 1999; b /= 1000) a /= 1000;
    for (; a > 999 && b > 999; b /= 500) a /= 500;
    for (; a > 199 && b > 199; b /= 100) a /= 100;
    for (; a > 99 && b > 99; b /= 50) a /= 50;
}

// Number of possible damage values of a bombardment (the damage is a random number below it):
// numerator * offense / (denominator * defense) + 1, both parts clamped as the original does.
inline int32_t bombard_damage_range(int32_t numerator, int32_t denominator, int32_t offense, int32_t defense) {
    int32_t den = denominator * defense;
    den = den < 1 ? 1 : den > 9999999 ? 9999999 : den;
    int32_t rounds = numerator * offense / den + 1;
    return rounds < 1 ? 1 : rounds > 999 ? 999 : rounds;
}

// Hit points a bombardment cannot remove: (100 - max_damage_percent)% of the unit's hit points,
// rounded up.
inline int32_t bombard_hp_floor(int32_t max_damage_percent, int32_t max_hp) {
    return ((100 - max_damage_percent) * max_hp + 99) / 100;
}

// Moves a unit attacks with: what it has left, at most a full move (move_rate_roads), none when
// the rules' move rate is negative. The offense is scaled by moves / move_rate.
inline int32_t attack_moves(int32_t moves_left, int32_t move_rate) {
    if (move_rate < 0) return 0;
    return moves_left > move_rate ? move_rate : moves_left;
}

// How far direction d turns away from the attack coming from direction `dir` (0: toward the
// attacker, 4: straight away), as a withdrawing defender rates its escape tiles.
inline int32_t withdraw_angle(int32_t d, int32_t dir) {
    uint32_t diff = (uint32_t)(d - (dir ^ 4));
    if ((int32_t)diff < 0) diff = (uint32_t)((dir ^ 4) - d);
    diff &= 7;
    if (diff > 4) diff = 8 - diff;
    return (int32_t)diff;
}

}  // namespace battle_rules
