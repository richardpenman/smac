// Sea connection helpers of the diplo_act module (0x50DDC0-0x50E310), used by the AI movement
// code (set_course, get_there, alien_base). Declared in action.h / diplo_act.h.
#include "engine/action.h"
#include "engine/diplo_act.h"
#include "engine/map.h"
#include "engine/veh.h"

int32_t GAMEAPI sea_coast(int32_t region_land, int32_t region_sea) {
    int32_t offset, mask;
    bitmask(region_sea & 0x3F, &offset, &mask);
    return (Continents[region_land].sea_coasts[offset] & (uint8_t)mask) != 0;
}

int32_t GAMEAPI base_on_sea(int32_t base_id, int32_t region) {
    if ((region & 0x3F) >= 0x3F) return 0;
    for (int32_t i = 0; i < 8; i++) {
        int32_t x = map_wrap_x(NearOffsetX[i] + Bases[base_id].x);
        int32_t y = NearOffsetY[i] + Bases[base_id].y;
        if (map_in_bounds(x, y) && map_is_ocean(x, y) && (map_tile(x, y)->region & 0x3F) == (region & 0x3F)) return 1;
    }
    return 0;
}

int32_t GAMEAPI port_to_coast(int32_t base_id, int32_t region) {
    int32_t bx = Bases[base_id].x, by = Bases[base_id].y;
    if ((uint32_t)region == map_tile(bx, by)->region) return 1;
    for (uint32_t i = 0; (int32_t)i < 8; i++) {
        int32_t x = map_wrap_x(NearOffsetX[i] + bx);
        int32_t y = NearOffsetY[i] + by;
        if (!map_in_bounds(x, y) || !map_is_ocean(x, y)) continue;
        int32_t offset, mask;
        bitmask(map_tile(x, y)->region & 0x3F, &offset, &mask);
        if (Continents[region].sea_coasts[offset] & (uint8_t)mask) return 1;
        i += 2 - (i & 1);  // skips the neighbours of this ocean tile
    }
    return 0;
}

int32_t GAMEAPI port_to_port(int32_t base_id, int32_t base_id_2) {
    uint32_t last = 0xFFFFFFFF;
    for (int32_t i = 0; i < 8; i++) {
        int32_t x = map_wrap_x(NearOffsetX[i] + Bases[base_id].x);
        int32_t y = NearOffsetY[i] + Bases[base_id].y;
        if (!map_in_bounds(x, y) || !map_is_ocean(x, y)) continue;
        uint32_t region = map_tile(x, y)->region;
        if (region == last) continue;
        last = region;
        if ((region & 0x3F) >= 0x3F) continue;
        for (int32_t j = 0; j < 8; j++) {
            int32_t x2 = map_wrap_x(NearOffsetX[j] + Bases[base_id_2].x);
            int32_t y2 = NearOffsetY[j] + Bases[base_id_2].y;
            if (map_in_bounds(x2, y2) && map_is_ocean(x2, y2) && (map_tile(x2, y2)->region & 0x3F) == (region & 0x3F)) {
                return 1;
            }
        }
    }
    return 0;
}

int32_t GAMEAPI sea_coasts(int32_t region) {
    // Number of land regions 1..62 that border sea region `region`.
    int32_t count = 0;
    for (int32_t i = 1; i < 63; i++) {
        int32_t offset, mask;
        bitmask(region & 0x3F, &offset, &mask);
        if (Continents[i].sea_coasts[offset] & (uint8_t)mask) count++;
    }
    return count;
}

int32_t GAMEAPI base_coast(int32_t base_id) {
    // Region of the last ocean tile found around the base (after one, its two following
    // neighbours are skipped), or -1. The original also computes the largest tile count of these
    // regions (reading Continents[region] even for region >= 128) but never uses it.
    uint32_t region = 0xFFFFFFFF;
    int32_t bx = Bases[base_id].x, by = Bases[base_id].y;
    for (uint32_t i = 0; (int32_t)i < 8; i++) {
        int32_t x = map_wrap_x(NearOffsetX[i] + bx);
        int32_t y = NearOffsetY[i] + by;
        if (!map_in_bounds(x, y) || !map_is_ocean(x, y)) continue;
        region = map_tile(x, y)->region;
        i += 2 - (i & 1);
    }
    return (int32_t)region;
}

int32_t GAMEAPI transport_base(int32_t base_id) {
    int32_t region = base_coast(base_id);
    if (region < 0) return 0;
    if (map_is_ocean(Bases[base_id].x, Bases[base_id].y)) return 1;
    int32_t count = 0;
    for (int32_t i = 1; i < 63; i++) {
        int32_t offset, mask;
        bitmask(region & 0x3F, &offset, &mask);
        if (Continents[i].sea_coasts[offset] & (uint8_t)mask) count++;
    }
    return count > 1;
}

int32_t GAMEAPI naval_base(int32_t base_id) {
    // Whether a foreign base shares a sea region with this one (port_to_port inlined).
    if (base_coast(base_id) < 0) return 0;
    for (int32_t i = 0; i < BaseCount; i++) {
        if ((int8_t)Bases[i].faction_id != (int8_t)Bases[base_id].faction_id && port_to_port(base_id, i)) return 1;
    }
    return 0;
}

int32_t GAMEAPI convoy(int32_t veh_id, int32_t base_id) {
    int32_t home = Vehs[veh_id].home_base_id;
    if (home < 0 || home == base_id) return 0;
    int32_t triad = (int8_t)Chassis[Units[Vehs[veh_id].unit_id].chassis_id].triad;
    if (triad == 2) return 1;
    uint8_t region = map_tile(Bases[base_id].x, Bases[base_id].y)->region;
    if (region == map_tile(Bases[home].x, Bases[home].y)->region && ((region > 0x3F) == (triad == 1))) return 1;
    if (triad != 0 && port_to_port(base_id, home)) return 1;
    return 0;
}
