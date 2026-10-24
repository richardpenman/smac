#include "engine/path.h"

#include <cstring>

#include "engine/diplo_act.h"
#include "engine/heap.h"
#include "engine/map.h"
#include "engine/ui_bridge.h"

void THISCALL Path_init(Path* self) {
    if (self->mapTable) mem_free(self->mapTable);
    if (self->xTable) mem_free(self->xTable);
    if (self->yTable) mem_free(self->yTable);
    self->mapTable = nullptr;
    self->yTable = nullptr;
    self->xTable = nullptr;
    self->xDst = -1;
    self->yDst = -1;
    self->mapTable = static_cast<int32_t*>(mem_get((uint32_t)MapAreaTiles * 4));
    self->xTable = static_cast<int16_t*>(mem_get((uint32_t)MapAreaTiles * 2));
    self->yTable = static_cast<int16_t*>(mem_get((uint32_t)MapAreaTiles * 2));
    std::memset(self->mapTable, 0, (size_t)MapAreaTiles * 4);
}

void THISCALL Path_make_abstract(Path* self) {
    for (int32_t ay = 0; ay < MapAbstractY; ay++) {
        for (int32_t ax = ay & 1; ax < MapAbstractX; ax += 2) {
            int32_t region = 0;
            for (int32_t i = 0; i < 9; i++) {
                int32_t x2 = map_wrap_x(ax * 5 + TableOffsetX[i]);
                int32_t y2 = ay * 5 + TableOffsetY[i];
                if (map_in_bounds(x2, y2) && !map_is_ocean(x2, y2)) {
                    region = map_tile(x2, y2)->region;
                    break;
                }
            }
            abstract_set(ax, ay, region);
        }
    }
}

void THISCALL Path_continent(Path* self, int32_t x, int32_t y, int32_t region) {
    Continent& cont = Continents[region];
    cont.tile_count = 0;
    int32_t fresh = 0;
    self->index2 = 0;
    self->index1 = 0;
    self->xTable[0] = (int16_t)x;
    self->yTable[self->index1] = (int16_t)y;
    self->index1++;
    region_set(x, y, region);
    bool ocean = map_is_ocean(x, y);
    do {
        int32_t cx = self->xTable[self->index2];
        int32_t cy = self->yTable[self->index2];
        self->index2++;
        cont.tile_count++;
        for (int32_t i = 0; i < 8; i++) {
            int32_t x2 = map_wrap_x(cx + NearOffsetX[i]);
            int32_t y2 = cy + NearOffsetY[i];
            // Tiles reached from the polar rows are not expanded.
            if (!map_in_bounds(x2, y2) || cy == 0 || cy == MapAreaY - 1) continue;
            MAP* sq = map_tile(x2, y2);
            if (((sq->climate & 0xE0) < 0x60) != ocean || sq->region != 0) continue;
            if (ocean && (sq->landmarks & LM_FRESH) && (sq->climate & 0xE0) < 0x60) fresh++;
            region_set(x2, y2, region);
            self->xTable[self->index1] = (int16_t)x2;
            self->yTable[self->index1] = (int16_t)y2;
            self->index1++;
        }
    } while (self->index2 != 0 && self->index1 != self->index2);
    if (fresh) {
        // The fresh-water flag sticks only if most of the sea is fresh water.
        int32_t tiles = cont.tile_count;
        for (int32_t ty = 0; ty < MapAreaY; ty++) {
            for (int32_t tx = ty & 1; tx < MapAreaX; tx += 2) {
                if (map_tile(tx, ty)->region == (uint32_t)region) bit2_set(tx, ty, LM_FRESH, (tiles * 3) / 4 <= fresh);
            }
        }
    }
    do_all_non_input();
}

// Moves every tile of region `from` to region `to`.
static void retag_region(uint32_t from, uint32_t to) {
    for (int32_t i = 0; i < MapAreaTiles; i++) {
        if (MapTiles[i].region == from) MapTiles[i].region = (uint8_t)to;
    }
}

void THISCALL Path_continents(Path* self) {
    int32_t land_count = 0;
    int32_t sea_count = 0x40;
    for (int32_t i = 0; i < MapAreaTiles; i++) MapTiles[i].region = 0;
    for (int32_t y = 1; y < MapAreaY - 1; y++) {
        for (int32_t x = y & 1; x < MapAreaX; x += 2) {
            if (map_tile(x, y)->region != 0) continue;
            int32_t first, last, region;
            if (map_is_ocean(x, y)) {
                first = 0x40;
                sea_count++;
                last = 0x7E;
                region = sea_count < 0x40 ? 0x40 : (sea_count > 0x7E ? 0x7E : sea_count);
            } else {
                first = 0;
                land_count++;
                last = 0x3E;
                region = land_count < 0 ? 0 : (land_count > 0x3E ? 0x3E : land_count);
            }
            Path_continent(self, x, y, region);
            if (region != last) continue;
            // Out of region numbers: merge the smallest region into the overflow region
            // (last + 1) and reuse its number.
            int32_t smallest = -1;
            int32_t smallest_count = Continents[region].tile_count;
            for (int32_t r = first; r < last; r++) {
                if (Continents[r].tile_count < smallest_count) {
                    smallest = r;
                    smallest_count = Continents[r].tile_count;
                }
            }
            if (smallest >= 0) {
                Continents[last + 1].tile_count += Continents[smallest].tile_count;
                Continents[smallest].tile_count = 0;
                retag_region(smallest, last + 1);
                Continents[smallest].tile_count += Continents[region].tile_count;
                Continents[region].tile_count = 0;
                retag_region(region, smallest);
            } else {
                Continents[last + 1].tile_count += Continents[region].tile_count;
                Continents[region].tile_count = 0;
                retag_region(region, last + 1);
            }
        }
    }
    // Polar rows: regions 0x3F (land) and 0x7F (water).
    for (int32_t x = 0; x < MapAreaX; x += 2) {
        int32_t region = map_is_ocean(x, 0) ? 0x7F : 0x3F;
        region_set(x, 0, region);
        Continents[region].tile_count++;
    }
    for (int32_t x = (MapAreaY - 1) & 1, y = MapAreaY - 1; x < MapAreaX; x += 2) {
        int32_t region = map_is_ocean(x, y) ? 0x7F : 0x3F;
        region_set(x, y, region);
        Continents[region].tile_count++;
    }
    int32_t largest = 0, total = 0;
    for (int32_t r = 1; r < 63; r++) {
        total += Continents[r].tile_count;
        if (largest < Continents[r].tile_count) largest = Continents[r].tile_count;
    }
    if (largest < (total * 4) / 5)
        GameState &= ~0x100;
    else
        GameState |= 0x100;  // one dominant landmass
    for (int32_t r = 0; r < 64; r++) std::memset(Continents[r].sea_coasts, 0, 8);
    for (int32_t y = 0; y < MapAreaY; y++) {
        for (int32_t x = y & 1; x < MapAreaX; x += 2) {
            uint32_t region = map_tile(x, y)->region;
            if (region >= 0x40) continue;
            for (int32_t i = 0; i < 8; i++) {
                int32_t x2 = map_wrap_x(x + NearOffsetX[i]);
                int32_t y2 = y + NearOffsetY[i];
                if (!map_in_bounds(x2, y2)) continue;
                uint32_t sea = map_tile(x2, y2)->region;
                if (sea < 0x40) continue;
                int32_t offset, mask;
                bitmask((int32_t)sea - 0x40, &offset, &mask);
                Continents[region].sea_coasts[offset] |= (uint8_t)mask;
                i += 2 - (i & 1);  // skip the next one or two neighbours
            }
        }
    }
    Path_make_abstract(self);
    do_all_non_input();
}
