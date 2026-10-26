// Pure rules of a unit's step (engine/move_rules.h). Rows marked "recorded" are inputs and results
// taken from native games of the harness scenarios (tiny20, late140, cov_late300, cov_natives),
// which are identical to the original's; the other expectations follow the code.
#include "engine/move_rules.h"

#include "unit.h"

using namespace move_rules;

TEST_CASE("unit_hit_points: 10 per reactor level, 1 for an artifact") {
    // recorded (aircraft out of fuel)
    CHECK(unit_hit_points(0, 2) == 20);
    CHECK(unit_hit_points(0, 3) == 30);
    CHECK(unit_hit_points(0, 4) == 40);
    // from the code
    CHECK(unit_hit_points(0, 1) == 10);
    CHECK(unit_hit_points(0, 0) == 10);  // reactor clamped to 1..100
    CHECK(unit_hit_points(0, 101) == 1000);
    CHECK(unit_hit_points(12, 4) == 1);  // alien artifact
}

TEST_CASE("native_life_chance: distance, native life level and halvings") {
    // recorded: base distance, native life 1, supply removed, sea, road, weak unit
    CHECK(native_life_chance(2, 1, false, false, false, false) == 12);
    CHECK(native_life_chance(2, 1, false, true, false, false) == 6);
    CHECK(native_life_chance(2, 1, false, true, false, true) == 3);
    CHECK(native_life_chance(2, 1, true, false, false, false) == 6);
    CHECK(native_life_chance(2, 1, true, false, true, false) == 3);
    CHECK(native_life_chance(2, 1, true, true, false, true) == 1);
    CHECK(native_life_chance(3, 1, false, false, false, false) == 13);
    CHECK(native_life_chance(3, 1, true, true, false, false) == 3);
    CHECK(native_life_chance(5, 1, false, true, false, false) == 7);
    CHECK(native_life_chance(7, 1, false, false, false, false) == 17);
    // from the code
    CHECK(native_life_chance(40, 0, false, false, false, false) == 32);  // distance at most 32
    CHECK(native_life_chance(-1, 2, false, false, false, false) == 20);
    CHECK(native_life_chance(20, 0, true, false, false, false) == 5);  // halved twice at native life 0
    CHECK(native_life_chance(20, 0, false, true, false, false) == 5);
    CHECK(native_life_chance(20, 2, true, true, true, true) == 2);
}

TEST_CASE("native_life_cover: sensors and bases around the tile") {
    CHECK(native_life_cover(13, 0) == 3);
    CHECK(native_life_cover(13, 8) == 3);
    CHECK(native_life_cover(13, 9) == 6);
    CHECK(native_life_cover(13, 24) == 6);
}

TEST_CASE("pod_meeting_point: midpoint on a map square") {
    int32_t mx, my;
    // recorded
    pod_meeting_point(0, 34, 22, 20, mx, my);
    CHECK((mx == 11 && my == 27));
    pod_meeting_point(10, 12, 12, 16, mx, my);
    CHECK((mx == 10 && my == 14));
    pod_meeting_point(10, 12, 16, 14, mx, my);
    CHECK((mx == 13 && my == 13));
    pod_meeting_point(1, 27, 14, 12, mx, my);
    CHECK((mx == 7 && my == 19));
    pod_meeting_point(14, 18, 13, 27, mx, my);
    CHECK((mx == 12 && my == 22));
    // from the code: x + y stays even
    for (int32_t x = 0; x < 8; x++)
        for (int32_t y = 0; y < 8; y++) {
            pod_meeting_point(x, y, 20, 30, mx, my);
            CHECK((mx + my) % 2 == 0);
        }
}
