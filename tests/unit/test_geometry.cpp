// Map geometry of the original: distances, wrapping, directions and the radius table. All are
// fcompare entries (harness/oracle/fcompare_table.h: x_dist, vector_dist, tile_distance,
// compass_index, radius_move, radius_move_tiles, round_down), i.e. proven equal to the original on the
// harness scenarios' states; the values here are worked out by hand from the original's code.
// Coordinates are the original's: x steps by 2 along a row, rows alternate odd/even x.
#include <algorithm>
#include <cstdlib>
#include <initializer_list>

#include "engine/base_misc.h"
#include "engine/diplomacy.h"
#include "engine/goals.h"
#include "engine/map.h"
#include "engine/probe.h"
#include "engine/proto.h"
#include "engine/veh.h"
#include "unit.h"

TEST_CASE("vector_dist: the original's octagonal distance") {
    // big - ((((a + b) >> 1) - small + 1) >> 1) with a = |dx|, b = |dy|
    CHECK(vector_dist(0, 0) == 0);
    CHECK(vector_dist(1, 1) == 1);  // a neighbouring tile
    CHECK(vector_dist(2, 0) == 1);  // the next tile in the same row
    CHECK(vector_dist(2, 2) == 2);
    CHECK(vector_dist(3, 0) == 2);
    CHECK(vector_dist(4, 0) == 3);
    CHECK(vector_dist(10, 0) == 7);
    CHECK(vector_dist(-5, 3) == 4);
    for (int32_t dx = -20; dx <= 20; dx++) {
        for (int32_t dy = -20; dy <= 20; dy++) {
            int32_t d = vector_dist(dx, dy);
            REQUIRE(d == vector_dist(-dx, dy));
            REQUIRE(d == vector_dist(dy, dx));
            REQUIRE(d >= std::max(std::abs(dx), std::abs(dy)) / 2);
        }
    }
}

TEST_CASE("x_dist: horizontal distance, wrapped on round maps") {
    SUBCASE("round map") {
        MapDims dims(80, 40, false);
        CHECK(x_dist(0, 79) == 1);
        CHECK(x_dist(79, 0) == 1);
        CHECK(x_dist(0, 40) == 40);  // exactly half way: not wrapped
        CHECK(x_dist(0, 41) == 39);
        CHECK(x_dist(10, 14) == 4);
    }
    SUBCASE("flat map") {
        MapDims dims(80, 40, true);
        CHECK(x_dist(0, 79) == 79);
        CHECK(x_dist(0, 41) == 41);
    }
}

TEST_CASE("tile_distance and veh_vector_dist: the same wrapped distance") {
    // veh_vector_dist is the distance the original inlines; tile_distance (0x5A5910) is the function.
    // Both must agree on every pair of tiles (a candidate for merging in the cleanup).
    for (bool flat : {false, true}) {
        MapDims dims(40, 20, flat);
        for (int32_t x1 = 0; x1 < 40; x1 += 3) {
            for (int32_t y1 = 0; y1 < 20; y1 += 2) {
                for (int32_t x2 = 0; x2 < 40; x2++) {
                    for (int32_t y2 = 0; y2 < 20; y2++) {
                        int32_t d = tile_distance(x1, y1, x2, y2);
                        REQUIRE(d == veh_vector_dist(x1, y1, x2, y2));
                        REQUIRE(d == vector_dist(x_dist(x1, x2), y1 - y2));
                    }
                }
            }
        }
    }
    MapDims dims(40, 20, false);
    CHECK(tile_distance(0, 0, 39, 1) == 1);  // across the date line
    CHECK(tile_distance(0, 0, 4, 0) == 3);
}

TEST_CASE("compass_index: direction of a delta, by sign only (NearOffset order)") {
    // NearOffsetX/Y: NE, E, SE, S, SW, W, NW, N, centre
    CHECK(compass_index(1, -1) == 0);
    CHECK(compass_index(5, -3) == 0);
    CHECK(compass_index(3, 0) == 1);
    CHECK(compass_index(2, 7) == 2);
    CHECK(compass_index(0, 5) == 3);
    CHECK(compass_index(-1, 1) == 4);
    CHECK(compass_index(-4, 0) == 5);
    CHECK(compass_index(-1, -1) == 6);
    CHECK(compass_index(0, -2) == 7);
    CHECK(compass_index(0, 0) == 8);
}

TEST_CASE("radius_move: index of a delta in the radius table (TableOffset order)") {
    // TableOffset starts with the centre, then the eight neighbours in NearOffset order.
    CHECK(radius_move(0, 0, 1) == 0);
    for (int32_t i = 0; i < 8; i++) CHECK(radius_move(NearOffsetX[i], NearOffsetY[i], 9) == i + 1);
    CHECK(radius_move(2, -2, 9) == -1);  // the first tile of radius 2 is index 9
    CHECK(radius_move(2, -2, 10) == 9);
    CHECK(radius_move(1, 0, 289) == -1);  // not a tile (x + y must be even)
    for (int32_t i = 0; i < 289; i++) REQUIRE(radius_move(TableOffsetX[i], TableOffsetY[i], 289) == i);
}

TEST_CASE("radius_move_tiles: radius index between two tiles, wrapped") {
    MapDims dims(40, 20, false);
    CHECK(radius_move_tiles(10, 10, 10, 10, 9) == 0);
    CHECK(radius_move_tiles(10, 10, 11, 9, 9) == 1);
    CHECK(radius_move_tiles(0, 10, 39, 9, 9) == 7);  // dx -1 across the date line: NW
    CHECK(radius_move_tiles(10, 10, 30, 10, 289) == -1);
}

TEST_CASE("round_down: quotient truncated toward zero, times step") {
    CHECK(round_down(7, 5) == 5);
    CHECK(round_down(10, 5) == 10);
    CHECK(round_down(-7, 5) == -5);  // toward zero, not down
    CHECK(round_down(0, 3) == 0);
}

TEST_CASE("in_rect / map_scale") {
    CHECK(in_rect(2, 3, 0, 0, 4, 4) == 1);
    CHECK(in_rect(4, 4, 0, 0, 4, 4) == 1);  // bounds inclusive
    CHECK(in_rect(5, 4, 0, 0, 4, 4) == 0);
    SaveGlobal<int32_t> keep(MapScaleExtra);
    MapScaleExtra = 0;
    CHECK(map_scale(10) == 10);
    MapScaleExtra = 8;
    CHECK(map_scale(10) == 15);
    CHECK(map_scale(-3) == -4);  // (24 * -3) / 16, truncated
}
