// Pure rules of probe team missions (probe), extracted so they can be unit tested
// (tests/unit/test_probe_rules.cpp). No game state: every input is a parameter. These are pieces
// of the original's probe function, not original functions themselves (no ADDR).
#pragma once
#include <cstdint>

namespace probe_rules {

// Mind control cost of a unit adjusted by the target's PROBE rating: +50% at 1, x2 at 2, -25% at
// -1, halved at -2 to -6. Returns false when the rating (3 and more) makes the target immune;
// ratings outside -6..9 leave the cost unchanged.
inline bool adjust_unit_subvert_cost(int32_t& cost, int32_t probe) {
    if ((uint32_t)(probe + 6) <= 15) {
        switch (probe) {
        case 0: break;
        case 1: cost += cost / 2; break;
        case 2: cost *= 2; break;
        case -6:
        case -5:
        case -4:
        case -3:
        case -2: cost /= 2; break;
        case -1: cost += -(cost / 4); break;
        default: return false;
        }
    }
    return true;
}

// Mind control cost of a base adjusted by its PROBE rating (the faction's, -1 with a Genejack
// Factory, +2 with a Covert Ops Center): +50% at 1, x2 at 2, -25% at -1, halved at -2. Returns
// false at 3 (immune); other ratings leave the cost unchanged.
inline bool adjust_base_mind_control_cost(int32_t& cost, int32_t probe) {
    if ((uint32_t)(probe + 2) <= 5) {
        switch (probe) {
        case 1: cost += cost / 2; break;
        case 2: cost *= 2; break;
        case 3: return false;
        case -2: cost /= 2; break;
        case -1: cost += -(cost / 4); break;
        }
    }
    return true;
}

}  // namespace probe_rules
