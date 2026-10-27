// Pure rules of the AI (ai.cpp, ai_move.cpp), extracted so they can be unit tested
// (tests/unit/test_ai_rules.cpp). No game state: every input is a parameter. These are pieces of
// the original's AI functions (can_terraform, air_power, enemy_strategy, enemy_move), not original
// functions themselves (no ADDR).
#pragma once
#include <cstdint>

#include "engine/types/veh.h"

namespace ai_rules {

// can_terraform on a land tile with no mine, solar collector or special improvement, for a faction
// that has the mine's technology: the order after the mine rules, given the order chosen so far
// (`result`: 0 or the solar collector). A mineral bonus (bonus 2) or a rocky tile (rocky 2)
// chooses the mine; a mine is also chosen on a rolling flat tile (rocky 1, elevation 0) or an
// energy bonus (bonus 3) when mines keep their nutrients (`mine_keeps_nutrients`, alpha.txt's
// nutrient effect of mines >= 0), on rocky tiles, or when the tile yields at most one nutrient
// (counting a farm still to build). With `flag` (the caller wants an improvement in any case) a
// farm with nothing chosen yet gets a mine too. The original's labels: L_mine_checks, L_mine_set,
// L_mine_flag; the solar tail and the base's wishes follow in can_terraform.
inline int32_t land_mine_order(int32_t result, int32_t bonus, int32_t rocky, int32_t elev, int32_t food, bool farm,
                               bool flag, bool mine_keeps_nutrients) {
    enum class Step { checks, set, flag_only, none };
    Step step;
    if (bonus == 2 || rocky == 2) result = ORDER_MINE;
    if (bonus == 2 && rocky == 2) {
        step = Step::set;
    } else if (bonus != 2 && rocky == 2) {
        step = Step::checks;
    } else if (mine_keeps_nutrients) {
        step = Step::checks;
    } else {
        int32_t t = (farm ? 0 : 1) + food;
        if (t <= 1) step = Step::checks;
        else if (rocky != 0 || t <= 2) step = Step::none;
        else step = Step::flag_only;
    }
    if (step == Step::checks) {
        if (rocky == 1 && elev == 0) result = ORDER_MINE;
        step = bonus != 3 ? Step::flag_only : Step::set;
    }
    if (step == Step::set) {
        result = ORDER_MINE;
        step = Step::flag_only;
    }
    if (step == Step::flag_only && flag && farm && !result) result = ORDER_MINE;
    return result;
}

// air_power's two reaches of an air unit, in tiles: `attack` (how far it may go to attack, half of
// it there and back) and `ferry` (how far it can still fly to a base), from the chassis' range (0:
// unlimited, six turns' moves; 1: what is left of this turn, plus a whole turn more for the attack
// of an undamaged unit; more: the turns left in the air), the unit's speed and moves spent, and the
// turns it has already flown (`movement_turns`). `move_rate` is the road move rate of alpha.txt.
// The original's iVar11 (attack) and iVar10 (ferry).
struct AirReach {
    int32_t attack;
    int32_t ferry;
};
inline AirReach air_reach(int32_t chassis_range, int32_t speed, int32_t moves_spent, bool damaged,
                          int32_t movement_turns, int32_t move_rate) {
    int32_t attack, ferry;
    if (chassis_range == 0) {
        ferry = speed * 6;
        attack = ferry;
    } else {
        int32_t left = speed - moves_spent;
        left = left < 0 ? 0 : left > 999 ? 999 : left;
        if (chassis_range == 1) {
            ferry = left;
            attack = damaged ? left : speed + left;
        } else {
            attack = speed * (chassis_range - movement_turns - 1) + left;
            ferry = attack;
        }
    }
    return {attack / move_rate, ferry / move_rate};
}

// air_power's bombing run: a target base's value divided by the number of legs (half the attack
// reach, clamped to 1-99) it takes to get there (`dist` in air_dist tiles).
inline int32_t bombing_score(int32_t value, int32_t dist, int32_t attack_reach) {
    int32_t step = attack_reach / 2;
    step = step < 1 ? 1 : step > 99 ? 99 : step;
    return value / ((dist - 1) / step + 1);
}

// enemy_strategy: the attackers gathered at the attack target's staging base that are enough to
// launch the attack: two thirds of the faction's bases in the target's region, 3 to 8.
inline int32_t attack_force_limit(int32_t region_bases) {
    int32_t limit = region_bases * 2 / 3;
    return limit < 3 ? 3 : limit > 8 ? 8 : limit;
}

// enemy_strategy: a defensive unit is marked as part of a base's garrison (state 0x10000000) when
// the base wants more defenders (`want`, garrison_check's result) than it has plus a margin: the
// wanted number again (the original calls garrison_check a second time for it), minus one above 5
// and minus another above 2.
inline bool garrison_short(int32_t want, int32_t margin, int32_t have) {
    if (margin > 5) margin--;
    if (margin > 2) margin--;
    return want > have + margin;
}

}  // namespace ai_rules
