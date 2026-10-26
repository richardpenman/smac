// Pure rules of a unit's step (order_veh), extracted so they can be unit tested
// (tests/unit/test_move_rules.cpp). No game state: every input is a parameter. These are pieces
// of the original's order_veh, not original functions themselves (no ADDR).
#pragma once
#include <cstdint>

namespace move_rules {

// Hit points of a unit: 10 per reactor level (1-100), 1 for an alien artifact (plan 12).
inline int32_t unit_hit_points(int32_t plan, int32_t reactor_id) {
    return plan == 12 ? 1 : (reactor_id < 1 ? 1 : reactor_id > 100 ? 100 : reactor_id) * 10;
}

// Percent chance that native life appears in front of a unit entering fungus, before the sensors
// and bases around the tile cut it: the distance to the nearest base (at most 32) plus 10 per
// native life level, halved on a tile where supply pods may not appear (and again at native life
// 0), at sea (likewise), on a road, and for a unit that is weak against natives (the caller's
// test: a predefined unit with a psi weapon or the Mind Worm).
inline int32_t native_life_chance(int32_t base_dist, int32_t native_life, bool supply_removed, bool sea, bool road,
                                  bool weak_unit) {
    int32_t chance = base_dist < 0 ? 0 : base_dist > 32 ? 32 : base_dist;
    chance += native_life * 10;
    if (supply_removed) {
        chance /= 2;
        if (!native_life) chance /= 2;
    }
    if (sea) {
        chance /= 2;
        if (!native_life) chance /= 2;
    }
    if (road) chance /= 2;
    if (weak_unit) chance /= 2;
    return chance;
}

// Each sensor or base within two tiles of the tile cuts the chance: to a quarter for the tile and
// its 8 neighbours (index < 9 of TableOffset), to half for the outer ring.
inline int32_t native_life_cover(int32_t chance, int32_t table_index) {
    return table_index < 9 ? chance / 4 : chance / 2;
}

// Where an AI colony pod at sea on its way to (wx, wy) asks for a transport: the midpoint of its
// tile (tx, ty) and the waypoint, moved onto a map square (x even, then odd again when y is odd,
// so that x + y stays even).
inline void pod_meeting_point(int32_t tx, int32_t ty, int32_t wx, int32_t wy, int32_t& mx, int32_t& my) {
    mx = (wx + tx) / 2;
    my = (wy + ty) / 2;
    if (mx & 1) mx--;
    if (my & 1) mx++;
}

}  // namespace move_rules
