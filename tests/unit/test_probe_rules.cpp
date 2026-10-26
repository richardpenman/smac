// Pure rules of probe team missions (engine/probe_rules.h). Rows marked "recorded" are inputs and
// results taken from native games of the harness scenarios (cov_probes, probe131, late140,
// cov_late300), which are identical to the original's; the other expectations follow the code.
#include "engine/probe_rules.h"

#include "unit.h"

using namespace probe_rules;

namespace {
int32_t unit_cost(int32_t cost, int32_t probe) { return adjust_unit_subvert_cost(cost, probe) ? cost : -1; }
int32_t base_cost(int32_t cost, int32_t probe) { return adjust_base_mind_control_cost(cost, probe) ? cost : -1; }
}  // namespace

TEST_CASE("adjust_unit_subvert_cost: PROBE rating of the unit's faction") {
    // recorded
    CHECK(unit_cost(1090, 0) == 1090);
    CHECK(unit_cost(116, 2) == 232);
    // from the code
    CHECK(unit_cost(100, 1) == 150);
    CHECK(unit_cost(101, -1) == 76);  // -25%, the quarter rounded toward zero
    for (int32_t p = -6; p <= -2; p++) CHECK(unit_cost(101, p) == 50);
    for (int32_t p = 3; p <= 9; p++) CHECK(unit_cost(100, p) == -1);  // immune
    CHECK(unit_cost(100, 10) == 100);  // outside -6..9: unchanged
    CHECK(unit_cost(100, -7) == 100);
}

TEST_CASE("adjust_base_mind_control_cost: PROBE rating at the base") {
    // recorded
    CHECK(base_cost(1090, 2) == 2180);
    CHECK(base_cost(127, 0) == 127);
    CHECK(base_cost(936, 1) == 1404);
    // from the code
    CHECK(base_cost(101, -1) == 76);
    CHECK(base_cost(101, -2) == 50);
    CHECK(base_cost(100, 3) == -1);  // immune
    CHECK(base_cost(100, 4) == 100);  // outside -2..3: unchanged
    CHECK(base_cost(100, -3) == 100);
}
