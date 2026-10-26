#include "engine/world.h"

#include "engine/base.h"
#include "engine/clock.h"
#include "engine/game_state.h"
#include "engine/log.h"
#include "engine/map.h"
#include "engine/net_misc.h"
#include "engine/path.h"
#include "engine/random.h"
#include "engine/strings.h"
#include "engine/turn.h"
#include "engine/ui_bridge.h"
#include "engine/util.h"
#include "engine/veh.h"

// ---- helpers (not original functions) -----------------------------------------------------------

static inline bool tile_ocean(const MAP* sq) { return (sq->climate & 0xE0) < 0x60; }
static inline int32_t tile_alt(const MAP* sq) { return sq->climate >> 5; }
static inline int32_t clamp(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : (v > hi ? hi : v); }

// Landmark name (CNatural::name) validated against the string table.
static const char* natural_name(int32_t id) { return Strings_get(&StringTable, Natural[id].name); }

// Random landmark site: the shared search loop of the landmark generators. draw() picks a tile,
// accept() tests it; a site within radius 8 of an existing landmark is rejected. Gives up after
// 1000 attempts.
template <class Draw, class Accept>
static bool find_landmark_site(int32_t& x, int32_t& y, Draw draw, Accept accept) {
    for (int32_t attempts = 0;;) {
        draw(x, y);
        if (++attempts > 999) return false;
        if (accept(x, y) && !near_landmark(x, y)) return true;
    }
}

// Usual site draw: rows 8..MapAreaY-9, any column with the parity of the row.
static void draw_site(int32_t& x, int32_t& y) {
    y = map_rand_mod(game().map.y - 16) + 8;
    x = map_rand_mod(game().map.x);
    x ^= (x ^ y) & 1;
}

static bool site_land(int32_t x, int32_t y) { return !map_is_ocean(x, y); }
static bool site_ocean(int32_t x, int32_t y) { return map_is_ocean(x, y); }

// Random start tile of build_continent/build_hills (4 tiles away from the map edges).
static void draw_seed(int32_t& x, int32_t& y) {
    int32_t flat = game().map.flat & 1;
    x = map_rand_mod(game().map.x - flat * 8) + (game().map.flat & 1) * 4;
    y = map_rand_mod(game().map.y - 8) + 4;
    if (x & 1) x--;
    if (y & 1) x++;
}

// Redraws the terrain of all map windows (world_climate / world_build epilogue).
static void redraw_terrain() {
    if (!GameHalted) {
        ui_mapwins_clear_terrain();
        WorldWin_clear_terrain(&WorldWindow);
    }
}

// ---- value noise --------------------------------------------------------------------------------

void THISCALL fractal_init(WorldFractal* self) {
    int8_t* row = self->seed;
    for (int32_t r = 0; r < 18; r++, row += 19) {
        for (int32_t i = 0; i < 18; i++) row[i] = (int8_t)(game_rand() % 16);
        row[18] = row[0];
    }
    for (int32_t i = 0; i < 18; i++) self->seed[18 * 19 + i] = self->seed[i];
    // The original also sums the eight neighbours of every cell here but discards the sum.
    for (int32_t r = 0; r <= 16; r++) {
        for (int32_t c = 0; c <= 16; c++) self->grid[r * 19 + c] = self->seed[r * 19 + c];
    }
    for (int32_t r = 0; r < 17; r++) self->grid[r * 19 + 16] = self->grid[r * 19];
    for (int32_t c = 0; c <= 16; c++) self->grid[16 * 19 + c] = self->grid[c];
}

int32_t THISCALL fractal_at(WorldFractal* self, int32_t x, int32_t y) {
    int32_t gx = ((x - 0x80) >> 8) & 0xF;
    int32_t gy = ((y - 0x80) >> 8) & 0xF;
    int32_t fy = ((y - 0x80) >> 3) & 0x1F;
    int32_t fx = ((x - 0x80) >> 3) & 0x1F;
    const int8_t* cell = &self->grid[gx * 19 + gy];
    int32_t a = (cell[19] * fx + cell[0] * (32 - fx)) * (32 - fy);
    int32_t b = (cell[1] * (32 - fx) + cell[20] * fx) * fy;
    return (a + b) >> 5;
}

int32_t THISCALL fractal_value(WorldFractal* fractal, int32_t x, int32_t y, int32_t fine) {
    int32_t a = fractal_at(fractal, x << 4, y << 4);
    int32_t b = fractal_at(fractal, x << fine, y << fine);
    return clamp((a * 5 + b * 2) * 7 >> 8, 0, 100);
}

// ---- altitude -----------------------------------------------------------------------------------

// Sets a ring tile in world_alt_set; with toggle, also clears the occupant nibble of empty tiles.
static void ring_alt_set(int32_t x, int32_t y, int32_t altitude, int32_t toggle) {
    if (!toggle) {
        alt_set(x, y, altitude);
        return;
    }
    alt_set_both(x, y, altitude);
    MAP* sq = map_tile(x, y);
    if (((sq->items & (BIT_BASE_IN_TILE | BIT_VEH_IN_TILE)) == 0 || (sq->val2 & 0xF) > 7) && bonus_at(x, y) == 0)
        owner_set(x, y, -1);
}

void GAMEAPI world_alt_set(int32_t x, int32_t y, int32_t altitude, int32_t toggle) {
    for (int32_t i = 0; i < 8; i++) MapBaseSubmergedCount[i] = 0;
    for (int32_t i = 0; i < 8; i++) MapBaseIdClosestSubmergedVeh[i] = -1;
    if (map_in_bounds(x, y)) {
        if (toggle)
            alt_set_both(x, y, altitude);
        else
            alt_set(x, y, altitude);
    }
    // Raise lower tiles around: ring r may be at most altitude - r.
    for (int32_t r = 1; r < altitude; r++) {
        int32_t target = altitude - r;
        bool changed = false;
        if (TableRange[r] <= TableRange[r - 1]) break;
        for (int32_t i = TableRange[r - 1]; i < TableRange[r]; i++) {
            int32_t x2 = map_wrap_x(x + TableOffsetX[i]);
            int32_t y2 = y + TableOffsetY[i];
            if (map_in_bounds(x2, y2) && map_alt(x2, y2) < target) {
                ring_alt_set(x2, y2, target, toggle);
                changed = true;
            }
        }
        if (!changed) break;
    }
    // Lower higher tiles around: ring r may be at most altitude + r.
    for (int32_t r = 1; r < 8 - altitude; r++) {
        int32_t target = altitude + r;
        bool changed = false;
        if (TableRange[r] <= TableRange[r - 1]) return;
        for (int32_t i = TableRange[r - 1]; i < TableRange[r]; i++) {
            int32_t x2 = map_wrap_x(x + TableOffsetX[i]);
            int32_t y2 = y + TableOffsetY[i];
            if (map_in_bounds(x2, y2) && target < map_alt(x2, y2)) {
                ring_alt_set(x2, y2, target, toggle);
                changed = true;
            }
        }
        if (!changed) return;
    }
}

void GAMEAPI world_raise_alt(int32_t x, int32_t y) {
    if (map_in_bounds(x, y)) {
        int32_t alt = map_alt(x, y);
        if (alt < 6) world_alt_set(x, y, alt + 1, 1);
    }
}

void GAMEAPI world_lower_alt(int32_t x, int32_t y) {
    if (map_in_bounds(x, y)) {
        int32_t alt = map_alt(x, y);
        if (alt != 0) world_alt_set(x, y, alt - 1, 1);
    }
}

// ---- continents ---------------------------------------------------------------------------------

void GAMEAPI brush(int32_t x, int32_t y, int32_t altitude) {
    bool changed = false;
    // Brush shape: TableOffset entries 0, 1 and 3 (entry 2 is skipped).
    for (int32_t k = 0; k < 4; k++) {
        if (k == 2) continue;
        int32_t bx = map_wrap_x(x + TableOffsetX[k]);
        int32_t by = y + TableOffsetY[k];
        if (!map_in_bounds(bx, by)) continue;
        MAP* sq = map_tile(bx, by);
        if (tile_alt(sq) == altitude) continue;
        // Do not touch land of another landmass (region 0 = not painted by this continent).
        bool blocked = false;
        for (int32_t i = 0; i < 8; i++) {
            int32_t x2 = map_wrap_x(bx + NearOffsetX[i]);
            int32_t y2 = by + NearOffsetY[i];
            if (map_in_bounds(x2, y2)) {
                MAP* nb = map_tile(x2, y2);
                if (!tile_ocean(nb) && nb->region == 0) blocked = true;
            }
        }
        if (blocked) continue;
        if (altitude == 3) {
            // Avoid diagonal land bridges: a shore-level corner tile between two ocean tiles.
            bool bridge = false;
            for (int32_t d = 0; d + 1 <= 7 && !bridge; d += 2) {
                int32_t cx = map_wrap_x(bx + NearOffsetX[d + 1]);
                int32_t cy = by + NearOffsetY[d + 1];
                if (!map_in_bounds(cx, cy) || (map_tile(cx, cy)->climate & 0xE0) != 0x60) continue;
                int32_t ax = map_wrap_x(bx + NearOffsetX[(d + 2) & 7]);
                int32_t ay = by + NearOffsetY[(d + 2) & 7];
                int32_t cx2 = map_wrap_x(bx + NearOffsetX[d & 7]);
                int32_t cy2 = by + NearOffsetY[d & 7];
                if (map_in_bounds(ax, ay) && map_in_bounds(cx2, cy2) && map_is_ocean(ax, ay) &&
                    map_is_ocean(cx2, cy2))
                    bridge = true;
            }
            if (bridge) continue;
        }
        sq->region = 1;
        changed |= tile_alt(map_tile(bx, by)) != altitude;
        world_alt_set(bx, by, altitude, 1);
        if ((GameState & STATE_OMNISCIENT_VIEW) && altitude > 3) temp_set(bx, by, 1);
        BrushVal2++;
        BrushVal1++;
    }
    if ((GameState & STATE_OMNISCIENT_VIEW) && changed) draw_radius(x, y, 2, 2);
}

void GAMEAPI paint_land(int32_t x, int32_t y, int32_t altitude, int32_t size) {
    int32_t steps = 0;
    BrushVal2 = 0;
    do {
        int32_t bx = x, by = y;
        int32_t repeats = 0;
        int32_t last_dir = -1;
        do {
            brush(bx, by, altitude);
            int32_t dir = (game_rand() & 7) | 1;
            if (dir == last_dir) {
                repeats++;
                int32_t limit = MapLandCoverage * MapLandCoverage;
                if (repeats > limit + 2) dir = (dir - limit + repeats - 2) & 7;
            } else {
                last_dir = dir;
                repeats = 0;
            }
            bx = map_wrap_x(bx + NearOffsetX[dir]);
            by += NearOffsetY[dir];
            steps++;
        } while (steps < 2000 && map_in_bounds(bx, by) && BrushVal2 < size);
        steps++;
    } while (steps < 2000 && BrushVal2 < size);
}

void GAMEAPI build_continent(int32_t count) {
    for (int32_t i = 0; i < game().map.tile_count; i++) game().map.tiles[i].region = 0;
    int32_t coverage = MapLandCoverage;
    if (coverage != 0 && BrushVal1 >= WorldBuildVal1) coverage--;
    int32_t size = WorldBuilder.continent_mod * coverage * coverage + WorldBuilder.continent_base;
    int32_t x, y;
    int32_t tries = 0;
    do {
        draw_seed(x, y);
        tries++;
    } while (tries < 100 && !map_is_ocean(x, y));
    if (!map_is_ocean(x, y)) return;
    int32_t ratio = (count * 3200) / game().map.tile_count;
    if (ratio > WorldBuilder.cont_size_ratio5 || BrushVal1 >= WorldBuildVal1) {
        size /= 4;
        if (BrushVal1 >= WorldBuildVal1) {
            if (ratio > WorldBuilder.cont_size_ratio5) size = game_rand() % 3 + 1;
        } else if (size < 4) {
            size = 4;
        }
    } else {
        if (ratio > WorldBuilder.cont_size_ratio4) {
            size /= 3;
        } else if (ratio > WorldBuilder.cont_size_ratio3) {
            size /= 2;
        } else if (ratio > WorldBuilder.cont_size_ratio2) {
            int32_t odds = 4 - MapLandCoverage;
            if (odds - 1 > 0 && game_rand() % odds != 0) size /= 2;
        } else if (ratio < WorldBuilder.cont_size_ratio1 && MapLandCoverage > 1) {
            size += size / 2;
        }
        if (BrushVal1 < WorldBuildVal1 && size < 4) size = 4;
    }
    paint_land(x, y, 3, size);
    do_all_non_input();
}

void GAMEAPI build_hills(int32_t altitude) {
    int32_t tries = 0;
    for (;;) {
        int32_t x, y;
        draw_seed(x, y);
        tries += 2;
        if (tries > 100) return;
        bool blocked = false;
        for (int32_t i = 0; i < 9; i++) {
            int32_t x2 = map_wrap_x(x + TableOffsetX[i]);
            int32_t y2 = y + TableOffsetY[i];
            if (map_in_bounds(x2, y2) && map_alt(x2, y2) < altitude - 1) {
                blocked = true;
                break;
            }
        }
        if (blocked) continue;
        int32_t extra = (2 - MapOceanCoverage) * WorldBuilder.plateau_mod;
        if (altitude != 4) extra /= 4;
        int32_t base = WorldBuilder.plateau_base;
        if (MapOceanCoverage >= 2) base /= 2;
        paint_land(x, y, altitude, base + extra);
        do_all_non_input();
        return;
    }
}

void GAMEAPI world_erosion() {
    WorldFractal fractal;
    fractal_init(&fractal);
    int32_t root_std, root_map;
    if (MapLandCoverage == 1) {
        root_std = quick_root(3200);
        root_map = quick_root(game().map.tile_count);
    } else {
        root_map = 1;
        root_std = 1;
    }
    // Persist from tile to tile like the original's locals (always set before use).
    int32_t ocean_near = 0;   // neighbours at altitude <= 2
    int32_t separated = 0;    // land of another large landmass nearby
    for (int32_t pass = 0; pass < 2; pass++) {
        if (pass != 0) world_shorelines();
        Path_continents(&Paths);
        for (int32_t y = 0; y < game().map.y; y++) {
            for (int32_t x = y & 1; x < game().map.x; x += 2) {
                MAP* sq = map_tile(x, y);
                int32_t detail = sq->contour;
                int32_t alt = tile_alt(sq);
                int32_t noise = fractal_value(&fractal, x, y, 7);
                // Pass 0 shapes the sea floor and the shelf, pass 1 the land.
                if ((alt <= 2) != (pass == 0)) continue;
                int32_t value = noise;
                if (alt == 2 || alt == 3) {
                    ocean_near = 0;
                    for (int32_t i = 0; i < 8; i++) {
                        int32_t x2 = map_wrap_x(x + NearOffsetX[i]);
                        int32_t y2 = y + NearOffsetY[i];
                        if (map_in_bounds(x2, y2) && (map_tile(x2, y2)->climate & 0xE0) <= 0x40) ocean_near++;
                    }
                    if (alt == 3) {
                        int32_t region = sq->region;
                        separated = 0;
                        for (int32_t i = 9; i < 25; i++) {
                            int32_t x2 = map_wrap_x(x + TableOffsetX[i]);
                            int32_t y2 = y + TableOffsetY[i];
                            if (!map_in_bounds(x2, y2)) continue;
                            MAP* nb = map_tile(x2, y2);
                            if ((nb->climate & 0xE0) > 0x40 && nb->region < 0x40 && nb->region != region) separated++;
                        }
                    } else {
                        separated = 0;
                        int32_t largest = -1;
                        for (int32_t i = 0; i < 8; i++) {
                            int32_t x2 = map_wrap_x(x + NearOffsetX[i]);
                            int32_t y2 = y + NearOffsetY[i];
                            if (!map_in_bounds(x2, y2)) continue;
                            MAP* nb = map_tile(x2, y2);
                            if ((nb->climate & 0xE0) <= 0x40) continue;
                            int32_t region = nb->region;
                            if (largest < 0) {
                                largest = region;
                            } else if (largest != region) {
                                if (Continents[largest].tile_count + Continents[region].tile_count >=
                                    (game().map.tile_count * 50) / 3200)
                                    separated = 1;
                                if (Continents[region].tile_count > Continents[largest].tile_count) largest = region;
                            }
                        }
                    }
                }
                if (alt <= 1) {
                    value = noise - WorldBuilder.deep_water;
                } else if (alt == 2) {
                    if (separated) ocean_near = clamp(ocean_near + 4, 0, 8);
                    if (noise < detail) value = WorldBuilder.shelf + noise;
                    if (value >= AltNatural[alt + 1]) value += -(((ocean_near - 3) * WorldBuilder.shelf) / 4);
                } else if (alt == 3) {
                    if (separated) ocean_near = clamp(ocean_near + 4, 0, 8);
                    int32_t inland = 8 - ocean_near;
                    value = (inland * WorldBuilder.plains * root_map) / (root_std * 8) + noise;
                    if (value < detail) {
                        int32_t ocean = clamp(MapOceanCoverage, 1, 2);
                        value += (((WorldBuilder.hills * MapErosiveForces) / 2 * inland) / (ocean * 8) +
                                  WorldBuilder.beach) *
                                 root_map / root_std;
                    } else {
                        int32_t near = clamp(ocean_near, 0, 8);
                        int32_t ocean = clamp(MapOceanCoverage, 1, 2);
                        value = ((value - detail) * (8 - near) * MapErosiveForces) / (ocean * 8) + detail;
                    }
                } else if (alt == 4) {
                    if (noise > detail) value = noise * 2 - detail;
                    value += WorldBuilder.hills * MapErosiveForces + WorldBuilder.peaks;
                } else {
                    value = WorldBuilder.hills * MapErosiveForces + noise;
                }
                int32_t result = (value + detail + 1) / 2;
                if (MapErosiveForces == 0 && result > 0x4A) result = 0x4A;
                alt_put_detail(x, y, result);
            }
            do_all_non_input();
        }
    }
}

// ---- terrain features ---------------------------------------------------------------------------

void GAMEAPI world_rocky() {
    for (int32_t y = 0; y < game().map.y; y++) {
        for (int32_t x = y & 1; x < game().map.x; x += 2) {
            uint32_t lm = map_tile(x, y)->landmarks;
            // Keep the crater bowl and the Unity wreck's shape as they are.
            if ((lm & 0x80000001) == 1 && (int32_t)(lm & 0xFF000000) < 0x15000000) continue;
            if ((lm & 0x80004000) == 0x4000) {
                uint32_t code = lm & 0xFF000000;
                if ((int32_t)code < 0x9000000 || code == 0xC000000 || code == 0xA000000 || code == 0x13000000 ||
                    code == 0xF000000 || code == 0x14000000 || code == 0x10000000)
                    continue;
            }
            rocky_set(x, y, minerals_at(x, y));
            MAP* sq = map_tile(x, y);
            if ((sq->landmarks & 0x80) && tile_ocean(sq)) continue;
            uint32_t bits;
            if (tile_ocean(sq)) {
                bits = 0x10000;
            } else if ((sq->landmarks & 0x80000004) == 4 && (game_rand() & 1) == 0) {
                bits = 0x10000;
            } else {
                uint8_t climate = map_tile(x, y)->climate;
                if ((climate & 0xE0) <= 0x80 && (climate & 0x18) <= 8) continue;
                bits = 0x20000;
            }
            bit_set(x, y, bits, 1);
        }
        do_all_non_input();
    }
}

void GAMEAPI world_fungus() {
    WorldFractal fractal;
    fractal_init(&fractal);
    int32_t spread = (MapNativeLifeForms - 1) * WorldBuilder.fungus;
    for (int32_t y = 0; y < game().map.y; y++) {
        for (int32_t x = y & 1; x < game().map.x; x += 2) {
            int32_t noise = fractal_value(&fractal, x, y, 8);
            uint32_t lm = map_tile(x, y)->landmarks;
            if ((lm & 0x80000002) == 2 && (int32_t)(lm & 0xFF000000) < 0x9000000) continue;  // volcano
            bool band = (noise >= 20 - spread && noise <= spread * 2 + 25) ||
                        (noise >= 40 - spread && noise <= spread * 2 + 45) ||
                        (noise >= 60 - spread && noise <= spread * 2 + 65);
            if (!band) {
                if (y != 0 && y != game().map.y - 1) continue;
                if ((game_rand() & 1) == 0) continue;
            }
            MAP* sq = map_tile(x, y);
            if ((sq->val3 & 0xC0) >= 0x80 && !tile_ocean(sq)) continue;
            sq->items |= BIT_FUNGUS;
            map_tile(x, y)->items &= ~FungusClearItems;
            if (game_rand() % 40 == 0 && !map_is_ocean(x, y) && ExpansionEnabled) veh_init(BSC_FUNGAL_TOWER, 0, x, y);
        }
        do_all_non_input();
    }
}

void GAMEAPI world_riverbeds() {
    for (int32_t i = 0; i < game().map.tile_count; i++) game().map.tiles[i].items &= ~BIT_RIVER_SRC;
    int32_t placed = 0;
    int32_t limit = (((WorldBuilder.rivers_rain_mod * MapCloudCover + WorldBuilder.rivers_base) *
                      (4 - MapOceanCoverage)) / 3 * game().map.tile_count) / 3200;
    for (int32_t attempts = 0;;) {
        if (placed >= limit) return;
        int32_t x = map_rand_mod(game().map.x);
        int32_t y = map_rand_mod(game().map.y);
        if (x & 1) x--;
        if (y & 1) x++;
        MAP* sq = map_tile(x, y);
        if (!tile_ocean(sq) && !(sq->items & BIT_RIVER_SRC)) {
            int32_t best = -1;
            uint32_t best_alt = 0;
            bool near_river = false;
            for (int32_t i = 0; i < 81; i++) {
                int32_t x2 = map_wrap_x(x + TableOffsetX[i]);
                int32_t y2 = y + TableOffsetY[i];
                if (!map_in_bounds(x2, y2)) continue;
                MAP* nb = map_tile(x2, y2);
                if (nb->items & BIT_RIVER_SRC) {
                    near_river = true;
                    break;
                }
                if (i < 49 && (uint32_t)tile_alt(nb) > best_alt) {
                    best = i;
                    best_alt = tile_alt(nb);
                }
            }
            if (!near_river && best >= 0) {
                bit_set(map_wrap_x(x + TableOffsetX[best]), y + TableOffsetY[best], 0x100, 1);
                placed++;
            }
        }
        if (++attempts > 3999) return;
    }
}

void GAMEAPI world_rivers() {
    for (int32_t i = 0; i < game().map.tile_count; i++) game().map.tiles[i].items &= 0xFFFFFD7F;
    for (int32_t sy = 0; sy < game().map.y; sy++) {
        for (int32_t sx = sy & 1; sx < game().map.x; sx += 2) {
            MAP* src = map_tile(sx, sy);
            if (!(src->items & BIT_RIVER_SRC) || tile_ocean(src)) continue;
            for (int32_t i = 0; i < game().map.tile_count; i++) game().map.tiles[i].landmarks &= 0xFF7FFFFF;
            game_reseed(sy * 47 + sx * 31);
            int32_t dir = game_random(0, 4);
            src = map_tile(sx, sy);
            int32_t cur_alt = tile_alt(src);
            src->landmarks |= 0x800000;
            int32_t steps = 0;
            int32_t merging = 0;      // the river runs into another one
            int32_t mouth = 1;        // the river may end in a delta (BIT 0x200)
            int32_t step_parity = 1;
            int32_t level_run = 1;    // steps since the altitude last changed
            int32_t cur_detail = src->contour;
            int32_t x = sx, y = sy;
            bool ended_on_river = false;
            if (src->items & BIT_THERMAL_BORE) {
                ended_on_river = true;
            } else {
                for (;;) {
                    int32_t preferred = (game_random(0, 2) + dir - (step_parity & 1)) & 3;
                    int32_t back = dir ^ 2;
                    int32_t count = 0, total = 0;
                    int32_t min_rise = 9999;
                    int32_t uphill_weight = 8;
                    int32_t weights[3] = {0, 0, 0}, merges[3] = {0, 0, 0}, dirs[3] = {0, 0, 0};
                    bool end_river = false;
                    for (int32_t d = 0; d < 4 && !end_river; d++) {
                        if (d == back) continue;
                        int32_t cx = map_wrap_x(x + DiagOffsetX[d]);
                        int32_t cy = y + DiagOffsetY[d];
                        if (!map_in_bounds(cx, cy)) continue;
                        MAP* cand = map_tile(cx, cy);
                        uint32_t items = cand->items;
                        if (items & 0x80) {
                            end_river = true;  // joins an existing river
                            break;
                        }
                        if (items & 0x100) {
                            int32_t beds = 0;
                            for (int32_t k = 0; k < 4; k++) {
                                int32_t x3 = map_wrap_x(cx + DiagOffsetX[k]);
                                int32_t y3 = cy + DiagOffsetY[k];
                                if (map_in_bounds(x3, y3) && (map_tile(x3, y3)->items & BIT_RIVER_SRC)) beds++;
                            }
                            if (beds > 1) {
                                end_river = true;
                                break;
                            }
                        }
                        uint8_t cand_climate = cand->climate;
                        if ((cand_climate >> 5) > cur_alt) continue;
                        int32_t cand_detail = cand->contour;
                        int32_t weight = clamp(cur_detail - cand_detail + 2, 1, 9999) << 5;
                        if (d == preferred) weight += 31;
                        if (cur_detail < cand_detail) {
                            if (cand_detail > min_rise) continue;
                            min_rise = cand_detail;
                            weight = uphill_weight++;
                        }
                        int32_t merge = 0;
                        int32_t higher = 0;
                        bool skip = false;
                        for (int32_t k = 0; k < 4; k++) {
                            if (k == (d ^ 2)) continue;
                            int32_t x3 = map_wrap_x(cx + DiagOffsetX[k]);
                            int32_t y3 = cy + DiagOffsetY[k];
                            if (!map_in_bounds(x3, y3)) continue;
                            MAP* t = map_tile(x3, y3);
                            if (tile_ocean(t) && (cand_climate & 0xE0) >= 0x60 && level_run < 5) weight /= 2;
                            if (t->contour > cand_detail) {
                                higher++;
                                if (t->items & BIT_RIVER) {
                                    skip = true;
                                    break;
                                }
                            }
                            if (t->landmarks & 0x800000) {
                                if (merging || tile_ocean(t)) {
                                    skip = true;
                                    break;
                                }
                                merge = 1;
                                weight /= 8;
                            }
                        }
                        if (skip || weight == 0) continue;
                        if (higher == 3) weight = 1;
                        total += weight;
                        weights[count] = weight;
                        merges[count] = merge;
                        dirs[count] = d;
                        count++;
                    }
                    if (end_river) {
                        ended_on_river = true;
                        break;
                    }
                    if (count == 0) break;
                    int32_t pick = game_random(0, total);
                    dir = 0;
                    for (int32_t i = 0; i < count; i++) {
                        if (pick < weights[i]) {
                            merging = merges[i];
                            dir = dirs[i];
                            break;
                        }
                        pick -= weights[i];
                    }
                    x = map_wrap_x(x + DiagOffsetX[dir]);
                    y += DiagOffsetY[dir];
                    MAP* sq = map_tile(x, y);
                    int32_t alt = tile_alt(sq);
                    if (alt != cur_alt) level_run = 0;
                    cur_alt = alt;
                    int32_t detail = sq->contour;
                    if (detail > cur_detail) {
                        // Carve the river bed: never uphill.
                        detail = AltNaturalLinear[cur_alt];
                        if (cur_detail - 1 > detail) detail = cur_detail - 1;
                        alt_put_detail(x, y, detail);
                    }
                    cur_detail = detail;
                    if (++steps >= 1000) break;
                    map_tile(x, y)->landmarks |= 0x800000;
                    step_parity++;
                    level_run++;
                    if (map_tile(x, y)->items & BIT_THERMAL_BORE) {
                        ended_on_river = true;
                        break;
                    }
                }
            }
            if (ended_on_river) mouth = 0;
            if (cur_alt >= 3) {
                MAP* sq = map_tile(x, y);
                uint32_t items = sq->items;
                if (!(items & 0x80) && mouth && ((items & 3) == 0 || (sq->val2 & 0xF) >= 8)) sq->items = items | 0x200;
            }
            for (int32_t i = 0; i < game().map.tile_count; i++) {
                if (game().map.tiles[i].landmarks & 0x800000) game().map.tiles[i].items |= BIT_RIVER;
            }
        }
        do_all_non_input();
    }
    for (int32_t i = 0; i < game().map.tile_count; i++) game().map.tiles[i].landmarks &= 0xFF7FFFFF;
}

void GAMEAPI world_shorelines() {
    for (int32_t y = 0; y < game().map.y; y++) {
        for (int32_t x = y & 1; x < game().map.x; x += 2) {
            int32_t alt = alt_natural(x, y);
            alt_set(x, y, alt);
            if ((GameState & STATE_OMNISCIENT_VIEW) && WorldAddTemperature) {
                if (alt > 2) temp_set(x, y, clamp(alt - 3, 0, 2));
                draw_tile(x, y, 2);
            }
        }
        do_all_non_input();
    }
    for (int32_t alt = 6; alt >= 2; alt--) {
        for (int32_t y = 0; y < game().map.y; y++) {
            for (int32_t x = y & 1; x < game().map.x; x += 2) {
                if (map_alt(x, y) == alt) world_alt_set(x, y, alt, 1);
            }
            do_all_non_input();
        }
    }
    if ((GameState & STATE_OMNISCIENT_VIEW) && WorldAddTemperature) {
        MapWin_clear_terrain(MapWins[0]);
        draw_map(1);
    }
}

int32_t GAMEAPI world_validate() {
    Path_continents(&Paths);
    int32_t largest = 0, second = 0;
    for (int32_t region = 1; region < 64; region++) {
        if ((region & 0x3F) == 0x3F || (region & 0x3F) == 0) continue;
        int32_t tiles = Continents[region].tile_count;
        if (tiles >= largest) {
            second = largest;
            largest = tiles;
        } else if (second <= tiles) {
            second = tiles;
        }
    }
    if (MapLandCoverage == 1) return second < (largest * 2) / 3;
    return second < largest / 2;
}

void GAMEAPI world_temperature() {
    game_reseed(MapRandomSeed + 17);
    int32_t solar = game().map.y / WorldBuilder.solar_energy;
    int32_t band = game().map.y / WorldBuilder.thermal_band;
    int32_t deviance = game().map.y / WorldBuilder.thermal_deviance;
    int32_t warming = game().map.y / WorldBuilder.global_warming;
    for (int32_t y = 0; y < game().map.y; y++) {
        for (int32_t x = y & 1; x < game().map.x; x += 2) {
            uint32_t lm = map_tile(x, y)->landmarks;
            if ((lm & 0x80000001) == 1 && (int32_t)(lm & 0xFF000000) < 0x15000000) continue;  // crater bowl
            int32_t jitter = game_random(0, deviance * 2);
            int32_t orbit_roll = game_random(0, MapPlanetaryOrbit + 1);
            int32_t half = game().map.y / 2;
            int32_t dist = half - jitter - y + deviance;  // distance from the equator
            if (dist < 0) dist = jitter - half - deviance + y;
            dist = dist - (MapPlanetaryOrbit - 1) * solar - MapSeaLevelCouncil * warming;
            int32_t zone = (band / 2 + dist * 2) / band;
            int32_t temp = zone <= 2 ? 3 : (zone <= 9) + 1;
            int32_t alt = map_alt(x, y);
            for (int32_t i = 0; i < 9; i++) {
                int32_t x2 = map_wrap_x(x + TableOffsetX[i]);
                int32_t y2 = y + TableOffsetY[i];
                if (!map_in_bounds(x2, y2)) continue;
                MAP* sq = map_tile(x2, y2);
                if ((sq->items & BIT_THERMAL_BORE) && (alt <= 4 || tile_alt(sq) >= alt)) {
                    temp++;
                    break;
                }
            }
            if (temp > 1) {
                if (alt >= 6) {
                    if (MapPlanetaryOrbit < 2 || game_random(0, 2) == 0) temp--;
                } else if (alt == 5 && orbit_roll == 0) {
                    temp--;
                }
            }
            temp_set(x, y, clamp(temp - 1, 0, 2));
        }
        do_all_non_input();
    }
}

void GAMEAPI world_raise_rainfall(int32_t x, int32_t y) {
    uint32_t rain = ((map_tile(x, y)->climate & 0x18) >> 3) + 1;
    climate_set(x, y, rain < 3 ? rain : 2);
}

void GAMEAPI world_rainfall() {
    int32_t viewer = CurrentPlayerFaction;
    RainfallViewerBaseID = -1;
    RainfallBaseFaction = -1;
    // Pass 1: mark moisture sources in the per-tile scratch byte (MAP::unk_1):
    // 0x01/0x04 near condensers, 0x80 downwind of the sea, 0x40 near cloud-trapping hills,
    // 0x20/0x10 west/east rain shadow of mountains.
    for (int32_t i = 0; i < game().map.tile_count; i++) game().map.tiles[i].unk_1 = 0;
    for (int32_t y = 0; y < game().map.y; y++) {
        for (int32_t x = y & 1; x < game().map.x; x += 2) {
            if (map_tile(x, y)->items & BIT_CONDENSER) {
                for (int32_t i = 0; i < 9; i++) {
                    int32_t x2 = map_wrap_x(x + TableOffsetX[i]);
                    int32_t y2 = y + TableOffsetY[i];
                    if (!map_in_bounds(x2, y2)) continue;
                    MAP* sq = map_tile(x2, y2);
                    sq->unk_1 |= (sq->unk_1 & 1) ? 4 : 1;
                }
            }
            if (map_is_ocean(x, y)) {
                for (int32_t k = 0, cx = x; k < 3; k++, cx -= 2) {
                    int32_t left = map_wrap_x(cx - 2);
                    int32_t cur = map_wrap_x(cx);
                    if (y >= 0 && y < game().map.y && left >= 0 && left < game().map.x && cur >= 0 &&
                        cur < game().map.x && map_alt(cur, y) <= map_alt(left, y) && k <= MapCloudCover)
                        map_tile(left, y)->unk_1 &= 0xEF;
                }
                int32_t x2 = map_wrap_x(x + 2);
                if (map_in_bounds(x2, y)) map_tile(x2, y)->unk_1 |= 0x80;
                if (MapCloudCover > 0) {
                    int32_t x4 = map_wrap_x(x + 4);
                    if (map_in_bounds(x4, y)) {
                        MAP* sq = map_tile(x4, y);
                        if ((sq->climate & 7) > 1 || MapCloudCover > 1) {
                            sq->unk_1 |= 0x80;
                            if (MapCloudCover > 1) {
                                int32_t x6 = map_wrap_x(x + 6);
                                if (y < game().map.y && x6 >= 0 && x6 < game().map.x && (map_tile(x6, y)->climate & 7))
                                    map_tile(x6, y)->unk_1 |= 0x80;
                            }
                        }
                    }
                }
            }
            MAP* sq = map_tile(x, y);
            int32_t alt = tile_alt(sq);
            if (alt <= 4) {
                if (alt >= 3 && (sq->items & BIT_FOREST)) {
                    for (int32_t y2 = y - 1; y2 <= y + 1; y2++) {
                        int32_t x2 = map_wrap_x(x + 1);
                        if (map_in_bounds(x2, y2)) map_tile(x2, y2)->unk_1 |= 0x40;
                    }
                    int32_t x2 = map_wrap_x(x + 2);
                    if (map_in_bounds(x2, y)) map_tile(x2, y)->unk_1 |= 0x40;
                }
            } else {
                int32_t mass = alt > 5 ? WorldBuilder.cloudmass_peaks : WorldBuilder.cloudmass_hills;
                int32_t west = (MapCloudCover - 1) * WorldBuilder.rainfall_coeff + mass;
                if (west >= 1) {
                    for (int32_t i = -1, cx = x - 2; i >= -west; i--, cx -= 2) {
                        int32_t x2 = map_wrap_x(cx);
                        if (!map_in_bounds(x2, y)) continue;
                        MAP* nb = map_tile(x2, y);
                        if (tile_ocean(nb) || tile_alt(nb) >= alt) break;
                        nb->unk_1 |= 0x20;
                    }
                }
                int32_t east = mass - (MapCloudCover - 1) * WorldBuilder.rainfall_coeff;
                for (int32_t i = 1, cx = x + 2; i <= east; i++, cx += 2) {
                    int32_t x2 = map_wrap_x(cx);
                    if (!map_in_bounds(x2, y)) continue;
                    MAP* nb = map_tile(x2, y);
                    if (tile_ocean(nb) || tile_alt(nb) >= alt) break;
                    nb->unk_1 |= 0x10;
                }
            }
        }
        do_all_non_input();
    }
    // Pass 2: sweep west to east carrying moisture across the land.
    int32_t moist = 0;  // carried moisture
    int32_t belt = 0;   // flat land crossed while moist
    int32_t run = 0;    // high land crossed
    MAP* walk = game().map.tiles;
    for (int32_t y = 0; y < game().map.y; y++) {
        for (int32_t x = y & 1; x < game().map.x; x += 2, walk++) {
            if (y < 1 || y >= game().map.y - 1) {
                walk->climate &= 0xE7;
                continue;
            }
            MAP* sq = map_tile(x, y);
            int32_t bonus = 0;
            int32_t rain = 0;
            uint32_t lm = sq->landmarks;
            uint8_t climate = sq->climate;
            int32_t temp = climate & 7;
            if ((lm & 0x80000040) == 0x40 || (lm & 0x80000100) == 0x100) moist = 1;
            uint8_t flags = walk->unk_1;
            if (flags & 0x20) {
                rain = 1;
                belt = 0;
                moist = 1;
            }
            int32_t alt = climate >> 5;
            int32_t cloud = MapCloudCover;
            if (flags & 0x80) {
                moist = temp > 1;
                run = 0;
                belt = 0;
                if (cloud >= 1) bonus = 1;
            }
            if (flags & 0x10) {
                if (moist) moist--;
                bonus--;
            }
            if ((flags & 0x40) && alt <= 4) {
                run = 0;
                belt = 0;
                if (bonus < 0) bonus = 0;
            }
            if (alt > 3) {
                run++;
                if (run > WorldBuilder.rainfall_coeff * cloud * 4 + 1 && moist) moist--;
            } else if (alt < 3 || moist == 0 || run != 0) {
                if (alt < 3) {
                    belt = 0;
                    moist = 1;
                }
                if (run != 0) {
                    run += -1 - cloud;
                    if (run < 1) {
                        run = 0;
                        belt = 0;
                        if (cloud != 0 || alt < 3) moist = 1;
                    }
                }
            } else {
                belt++;
                if (belt > cloud * 4 + 2) {
                    moist = 0;
                    belt = 0;
                }
            }
            if (sq->items & BIT_RIVER) {  // river
                if ((x >> 1) % 3 != 0 && bonus < 0) bonus = 0;
                moist = 1;
                belt = 0;
            }
            if (flags & 2) {
                rain--;
                moist = 0;
            } else if (moist) {
                bonus += moist;
            }
            if (alt <= (temp != 0) + 4) {
                rain += bonus;
            } else if (cloud - alt + temp + 5 >= 2 && bonus > 0) {
                rain++;
            }
            if ((lm & 0x80000004) == 4 && (climate & 0xE0) >= 0x60 && alt < 6) {  // jungle
                int32_t code = (int32_t)lm >> 24;
                if (code < 0) code = -code;
                if (code < 49)
                    rain += 2;
                else if (code < 81 && alt < 5)
                    rain += 2;
                else
                    rain++;
            }
            if ((lm & 0x80000040) == 0x40) rain = 0;  // dunes
            if ((lm & 0x80004000) == 0x4000 || rain < 0)
                rain = 0;
            else if (rain > 2)
                rain = 2;
            if (flags & 1) {
                moist = 1;
                if (rain < 2) rain++;
            }
            if (flags & 4) {
                moist = 1;
                if (rain < 2) rain++;
            }
            if (CurrentTurn != 0) {
                int32_t old = (walk->climate >> 3) & 3;
                if (old != rain && RainfallViewerBaseID < 0) {
                    if (old > rain && (RainfallBaseFaction < 0 ||
                                       !((uint8_t)(1 << (RainfallBaseFaction & 31)) & FactionStatus[0]))) {
                        int32_t base_id = base_find(x, y);
                        if (base_id >= 0) RainfallBaseFaction = (uint8_t)game().bases[base_id].faction_id;
                    }
                    int32_t base_id = base_find_faction(x, y, viewer);
                    if (base_id >= 0 && BaseFindDist <= 2) RainfallViewerBaseID = base_id;
                }
            }
            walk->climate = (walk->climate & 0xE7) | (uint8_t)(rain << 3);
        }
        do_all_non_input();
    }
    // Pass 3: arid tiles next to rainy land become moist.
    for (int32_t y = 0; y < game().map.y; y++) {
        for (int32_t x = y & 1; x < game().map.x; x += 2) {
            if (map_tile(x, y)->climate & 0x18) continue;
            for (int32_t i = 0; i < 8; i++) {
                int32_t x2 = map_wrap_x(x + NearOffsetX[i]);
                int32_t y2 = y + NearOffsetY[i];
                if (!map_in_bounds(x2, y2)) continue;
                uint8_t c = map_tile(x2, y2)->climate;
                if ((c & 0xE0) >= 0x60 && (c & 0x18) == 0x10) {
                    climate_set(x, y, 1);
                    break;
                }
            }
        }
        do_all_non_input();
    }
}

int32_t GAMEAPI world_site(int32_t x, int32_t y, int32_t sea) {
    // Nearby bases (radius 3 ring) make the site less attractive.
    int32_t bases = 0;
    for (int32_t i = 21; i < 49; i++) {
        int32_t x2 = map_wrap_x(x + TableOffsetX[i]);
        int32_t y2 = y + TableOffsetY[i];
        if (!map_in_bounds(x2, y2)) continue;
        MAP* sq = map_tile(x2, y2);
        if (!(sq->items & BIT_BASE_IN_TILE)) continue;
        bases++;
        if (i - 21 < 4) {
            bases++;
            if (map_is_ocean(x, y) && tile_ocean(sq)) bases += 2;
        }
    }
    int32_t farm = 0, forest = 0, fungus = 0, score = 0, rocky = 0, big_sea = 0, rivers = 0, deep = 0, ocean = 0;
    MAP* last = nullptr;  // last tile examined (the original tests its flags after the loop)
    for (int32_t i = 0; i < 21; i++) {
        int32_t x2 = map_wrap_x(x + TableOffsetX[i]);
        int32_t y2 = y + TableOffsetY[i];
        if (!map_in_bounds(x2, y2)) continue;
        MAP* sq = map_tile(x2, y2);
        last = sq;
        int32_t step = i != 0 ? 1 : -1;
        if (tile_ocean(sq)) {
            ocean++;
            if ((sq->climate & 0xE0) < 0x40) deep++;
            if (i < 9 && Continents[sq->region].tile_count >= 50) big_sea = 1;
            if (i != 0 && bonus_at(x2, y2) != 0) {
                MAP* t = map_tile(x2, y2);
                if (!(t->items & BIT_FUNGUS) || (t->climate & 0xE0) < 0x40) score++;
            }
            continue;
        }
        int32_t rock = sq->val3 >> 6;
        if (rock != 0 && i != 0 && rock > 1) rocky++;
        if (!(sq->items & BIT_FUNGUS)) {
            if (bonus_at(x2, y2) != 0) score += i != 0 ? 2 : -1;
            MAP* t = map_tile(x2, y2);
            uint32_t lm = t->landmarks;
            if ((lm & 0x80000002) == 2 && (int32_t)(lm & 0xFF000000) < 0x9000000) score += step;
            if ((lm & 0x80000001) == 1 && (int32_t)(lm & 0xFF000000) < 0x9000000) score += i != 0 ? 2 : -1;
            if ((lm & 0x80000008) == 8) score += i != 0 ? 1 : -3;
            if ((lm & 0x80000004) == 4 && !tile_ocean(t)) score += 2;
            if (i != 0 && goody_at(x2, y2)) score++;
        }
        int32_t rain = (sq->climate >> 3) & 3;
        MAP* t = map_tile(x2, y2);
        int32_t high = tile_alt(t) - 2;
        if (high < 0) high = 0;
        if (rock == 2 || (t->landmarks & 0x80000002) == 2) rain = 0;
        uint32_t items = sq->items;
        uint32_t fung = items & 0x20;
        if (fung) fungus++;
        if (items & 0x1000) continue;
        bool forestry = false;
        if (items & 0x202000) {
            forestry = true;
        } else if (rain != 0 && rock == 1) {
            if (high != 0 && !fung) {
                forestry = true;
            } else {
                farm += step;
                if (rain > rock && !fung) farm += step;
            }
        }
        if (forestry) {
            forest += step;
            if ((items & 0x2000) && !(items & 0x200000) && (rain > 1 || high > 1)) forest += i != 0 ? 1 : -2;
        }
        if (items & 0x80) rivers++;
    }
    score += rivers / 4;
    if (big_sea) score += sea ? -2 : 4;
    if (last->items & (BIT_CANAL_COAST | BIT_DOUBLE_SEA)) score += 4;
    if (sea)
        score += deep / 2 + (ocean - big_sea) / 2;
    else
        score += -(deep / 2) - (ocean - big_sea) / 2;
    int32_t total = (farm + 1) / 3 + score + (forest * 3 + 6) / 4;
    if (CurrentTurn > 150) total += fungus / -3;
    MAP* center = map_tile(x, y);
    int32_t at_sea = (center->climate & 0xE0) < 0x60;
    total += -(rocky / 4) - ((at_sea != sea) + 1) * bases;
    uint32_t items = center->items;
    if (((items & 0x20) && (center->climate & 0xE0) >= 0x40) || (items & 0x2000)) return 0;
    if ((center->val3 & 0xC0) > 0x40 && !at_sea) return 0;
    if (at_sea != sea && bases >= 4) return 0;
    return clamp(total, 1, 15);
}

void GAMEAPI world_analysis() {
    for (int32_t i = 0; i < 128; i++) Continents[i].open_terrain = 0;
    MAP* walk = game().map.tiles;
    for (int32_t y = 0; y < game().map.y; y++) {
        for (int32_t x = y & 1; x < game().map.x; x += 2, walk++) {
            MAP* sq = map_tile(x, y);
            uint32_t region = sq->region;
            uint8_t alt_bits = sq->climate & 0xE0;
            if (alt_bits >= 0x60 && (sq->climate & 0x18) && (sq->val3 & 0xC0) < 0x80 &&
                (!(sq->items & BIT_FUNGUS) || alt_bits < 0x40))
                Continents[region].open_terrain++;
            walk->items &= 0xBFFFBFFF;
            if (map_is_ocean(x, y)) continue;
            int32_t prev = -1, coast_changes = 0, outer_changes = 0;
            for (int32_t k = 0; k < 20; k++) {
                int32_t x2 = map_wrap_x(x + TableOffsetX[k + 1]);
                int32_t y2 = y + TableOffsetY[k + 1];
                if (!map_in_bounds(x2, y2)) continue;
                MAP* nb = map_tile(x2, y2);
                int32_t water = tile_ocean(nb);
                if (k < 8) {
                    if (water != prev) {
                        coast_changes++;
                        prev = water;
                        if (coast_changes > 3 && Continents[region].tile_count >= 80) walk->items |= BIT_DOUBLE_SEA;
                    }
                } else {
                    if (water != prev || k == 8) {
                        outer_changes++;
                        prev = water;
                    }
                    if (y2 > 4 && y2 < game().map.y - 4 && nb->region < 0x40 && nb->region != region &&
                        Continents[nb->region].tile_count >= 40 && Continents[region].tile_count >= 40)
                        walk->items |= BIT_CANAL_COAST;
                }
            }
            if (outer_changes < 4) walk->items &= 0xBFFFFFFF;
        }
        do_all_non_input();
    }
    for (int32_t i = 0; i < game().map.tile_count; i++) game().map.tiles[i].val2 &= 0xF;
}

void GAMEAPI world_alt_put_detail(int32_t x, int32_t y) { alt_put_detail(x, y, AltNatural[3]); }

void GAMEAPI world_polar_caps() {
    for (int32_t x = 0; x < game().map.x; x += 2) {
        alt_put_detail(x, 0, AltNatural[3]);
        alt_put_detail(x + 1, game().map.y - 1, AltNatural[3]);
    }
    for (int32_t i = 0; i < game().map.x / 16; i++) {
        alt_put_detail(map_rand_mod(game().map.x / 2) * 2, 0, AltNatural[3]);
        alt_put_detail(map_rand_mod(game().map.x / 2) * 2 + 1, 1, AltNatural[3]);
        alt_put_detail(map_rand_mod(game().map.x / 2) * 2 + 1, game().map.y - 1, AltNatural[3]);
        alt_put_detail(map_rand_mod(game().map.x / 2) * 2, game().map.y - 2, AltNatural[3]);
    }
}

void GAMEAPI world_climate() {
    do_checksums(2);
    log_say_values("-- ADJUSTING WORLD CLIMATE --", 0, 0, 0);
    world_shorelines();
    world_temperature();
    world_rivers();
    world_rainfall();
    Path_continents(&Paths);
    world_analysis();
    if (!WorldSkipTerritory) reset_territory();
    game_reseed(engine_logic_time());  // the original reseeds from timeGetTime()
    redraw_terrain();
    log_say_values("-- WORLD CLIMATE COMPLETE --", 0, 0, 0);
    do_checksums(3);
}

void GAMEAPI world_linearize_contours() {
    for (int32_t y = 0; y < game().map.y; y++) {
        for (int32_t x = y & 1; x < game().map.x; x += 2) {
            int32_t alt = alt_natural(x, y);
            // BUG(original): for altitude 6 the upper bound is entry 10, not 7.
            int32_t upper = alt < 6 ? AltNaturalLinear[alt + 1] : AltNaturalLinear[alt + 4];
            int32_t detail = map_tile(x, y)->contour;
            alt_put_detail(x, y,
                           ((upper - AltNaturalLinear[alt]) * (detail - AltNaturalDefault[alt])) /
                                   (AltNaturalDefault[alt + 1] - AltNaturalDefault[alt]) +
                               AltNaturalLinear[alt]);
        }
        do_all_non_input();
    }
    for (int32_t i = 0; i < 11; i++) AltNatural[i] = AltNaturalLinear[i];
}

int32_t GAMEAPI near_landmark(int32_t x, int32_t y) {
    for (int32_t i = 0; i < 289; i++) {
        int32_t x2 = map_wrap_x(x + TableOffsetX[i]);
        int32_t y2 = y + TableOffsetY[i];
        if (map_in_bounds(x2, y2) && (map_tile(x2, y2)->landmarks & 0xFF000000)) return 1;
    }
    return 0;
}

// ---- landmarks ----------------------------------------------------------------------------------

void GAMEAPI world_crater(int32_t x, int32_t y) {
    if (!map_in_bounds(x, y) && !find_landmark_site(x, y, draw_site, site_land)) return;
    world_alt_set(x, y, 0, 1);
    world_alt_set(x, y, 6, 1);
    world_alt_set(x, y, 3, 1);
    world_alt_set(x, y, 2, 1);
    world_alt_set(x, y, 3, 1);
    uint8_t climate = map_tile(x, y)->climate;
    for (int32_t i = 0; i < 49; i++) {
        int32_t x2 = map_wrap_x(x + TableOffsetX[i]);
        int32_t y2 = y + TableOffsetY[i];
        if (!map_in_bounds(x2, y2)) continue;
        bit_set(x2, y2, 0x100, 0);
        bit2_set(x2, y2, LM_CRATER, 1);
        code_set(x2, y2, i);
        if (i < 21) {
            rocky_set(x2, y2, 1);
            temp_set(x2, y2, climate & 7);
        }
    }
    new_landmark(x, y, natural_name(0));
}

void GAMEAPI world_monsoon(int32_t x, int32_t y) {
    world_rainfall();
    if (!map_in_bounds(x, y)) {
        auto draw = [](int32_t& sx, int32_t& sy) {
            sy = (game_rand() & 3) + game().map.y / 2 - 2;
            sx = map_rand_mod(game().map.x);
            sx ^= (sx ^ sy) & 1;
        };
        auto accept = [](int32_t sx, int32_t sy) {
            int32_t land = 0;
            for (int32_t i = 0; i < 121; i++) {
                int32_t x2 = map_wrap_x(sx + TableOffsetX[i]);
                int32_t y2 = sy + TableOffsetY[i];
                if (map_in_bounds(x2, y2) && !map_is_ocean(x2, y2)) land++;
            }
            return !map_is_ocean(sx, sy) && is_coast(sx, sy, 1) && land >= 40 &&
                   (map_tile(sx, sy)->climate & 0x18) > 8;
        };
        if (!find_landmark_site(x, y, draw, accept)) return;
    }
    for (int32_t i = 0; i < 121; i++) {
        int32_t x2 = map_wrap_x(x + TableOffsetX[i]);
        int32_t y2 = y + TableOffsetY[i];
        int32_t dy = y2 - y;
        if (dy < 0) dy = -dy;
        if (dy >= 9 || !map_in_bounds(x2, y2)) continue;
        if (i < 21 && map_is_ocean(x2, y2)) world_alt_set(x2, y2, 3, 1);
        bit2_set(x2, y2, LM_JUNGLE, 1);
        code_set(x2, y2, i);
    }
    new_landmark(x, y, natural_name(2));
}

void GAMEAPI world_sargasso(int32_t x, int32_t y) {
    if (!map_in_bounds(x, y) && !find_landmark_site(x, y, draw_site, site_ocean)) return;
    bool gap = false;
    for (int32_t i = 0; i < 81; i++) {
        int32_t x2 = map_wrap_x(x + TableOffsetX[i]);
        int32_t y2 = y + TableOffsetY[i];
        if (!map_in_bounds(x2, y2) || (!map_is_ocean(x2, y2) && i >= 21)) continue;
        world_alt_set(x2, y2, 2, 1);
        bit2_set(x2, y2, LM_SARGASSO, 1);
        code_set(x2, y2, i);
        if (i < 5) bit_set(x2, y2, 0x10000000, 1);
        if ((gap || i < 25 || game_rand() % 24 != 0) && (i < 49 || game_rand() % 3 != 0))
            bit_set(x2, y2, 0x20, 1);
        else
            gap = true;
    }
    new_landmark(x, y, natural_name(4));
}

void GAMEAPI world_ruin(int32_t x, int32_t y) {
    if (!map_in_bounds(x, y) && !find_landmark_site(x, y, draw_site, site_land)) return;
    for (int32_t i = 0; i < 25; i++) {
        int32_t x2 = map_wrap_x(x + TableOffsetX[i]);
        int32_t y2 = y + TableOffsetY[i];
        if (!map_in_bounds(x2, y2)) continue;
        world_alt_set(x2, y2, 3, 1);
        if (!map_is_ocean(x2, y2)) {
            bit_set(x2, y2, 0x100020, 1);
            if (i == 0)
                bit_set(x2, y2, 0x20, 0);
            else if (i <= 8)
                bit_set(x2, y2, 0x2000, 1);
        } else if (i != 0 && i <= 8) {
            bit_set(x2, y2, 0x2000, 1);
        }
        bit2_set(x2, y2, LM_RUINS, 1);
        code_set(x2, y2, i);
    }
    new_landmark(x, y + 2, natural_name(5));
}

void GAMEAPI world_dune(int32_t x, int32_t y) {
    world_rainfall();
    if (!map_in_bounds(x, y)) {
        auto draw = [](int32_t& sx, int32_t& sy) {
            int32_t r = map_rand_mod(game().map.y / 2);
            sy = game().map.y / 2 - game().map.y / 4 + r;
            sx = map_rand_mod(game().map.x);
            sx ^= (sx ^ sy) & 1;
        };
        auto accept = [](int32_t sx, int32_t sy) {
            uint8_t c = map_tile(sx, sy)->climate;
            return (c & 0xE0) >= 0x60 && (c & 0x18) == 0;
        };
        if (!find_landmark_site(x, y, draw, accept)) return;
    }
    for (int32_t i = 0; i < 25; i++) {
        int32_t x2 = map_wrap_x(x + TableOffsetX[i]);
        int32_t y2 = y + TableOffsetY[i];
        if (!map_in_bounds(x2, y2)) continue;
        if (map_is_ocean(x2, y2) && i >= 9 && game_rand() % 3 == 0) continue;
        world_alt_set(x2, y2, (i == 2 || i == 7) ? 4 : 3, 1);
        bit_set(x2, y2, 0x20, 0);
        bit2_set(x2, y2, LM_DUNES, 1);
        rocky_set(x2, y2, 0);
        code_set(x2, y2, i);
    }
    new_landmark(x, y + 2, natural_name(6));
}

void GAMEAPI world_diamond(int32_t x, int32_t y) {
    if (!map_in_bounds(x, y) && !find_landmark_site(x, y, draw_site, site_land)) return;
    for (int32_t i = 0; i < 21; i++) {
        int32_t x2 = map_wrap_x(x + TableOffsetX[i]);
        int32_t y2 = y + TableOffsetY[i];
        if (!map_in_bounds(x2, y2)) continue;
        if (i >= 9 && map_is_ocean(x2, y2) && game_rand() % 3 == 0) continue;
        world_alt_set(x2, y2, 3, 1);
        bit2_set(x2, y2, LM_URANIUM, 1);
        rocky_set(x2, y2, 0);
        bit_set(x2, y2, 0x20, 0);
        code_set(x2, y2, i);
    }
    new_landmark(x, y, natural_name(3));
}

void GAMEAPI world_fresh(int32_t x, int32_t y) {
    uint32_t region;
    if (!map_in_bounds(x, y)) {
        // The largest small sea (3..32 tiles).
        int32_t best = 0;
        region = 0xFFFFFFFF;
        for (uint32_t r = 64; r < 128; r++) {
            int32_t tiles = Continents[r].tile_count;
            if (tiles > 2 && tiles < 33 && best <= tiles) {
                region = r;
                best = tiles;
            }
        }
        if ((int32_t)region < 0) return;
    } else {
        region = map_tile(x, y)->region;
        if (!map_is_ocean(x, y)) return;
    }
    uint32_t first = 0xFFFFFFFF;  // column of the first tile of the region found
    bool named = false;
    for (int32_t row = game().map.y - 1; row >= 0; row--) {
        for (int32_t col = row & 1; col < game().map.x; col += 2) {
            if (map_tile(col, row)->region == region) {
                bit2_set(col, row, LM_FRESH, 1);
                if ((int32_t)first < 0) first = col;
            } else if (!named && (int32_t)first >= 0) {
                int32_t mid = ((int32_t)first - 2 + col) / 2;
                new_landmark(((mid ^ row) & 1) ^ mid, row, natural_name(7));
                named = true;
            }
        }
    }
}

void GAMEAPI world_volcano(int32_t x, int32_t y, int32_t skip_label) {
    if (!map_in_bounds(x, y) && !find_landmark_site(x, y, draw_site, site_land)) return;
    world_alt_set(x, y, 0, 1);
    world_alt_set(x, y, 6, 1);
    for (int32_t i = 0; i < 9; i++) {
        int32_t x2 = map_wrap_x(x + TableOffsetX[i]);
        int32_t y2 = y + TableOffsetY[i];
        if (!map_in_bounds(x2, y2)) continue;
        bit2_set(x2, y2, LM_VOLCANO, 1);
        code_set(x2, y2, i);
        uint32_t items = map_tile(x2, y2)->items;
        uint32_t kept = i == 0 ? items & 0xE2377E83 : items & 0xEE377E83;
        bit_put(x2, y2, kept | 0x100000);
        rocky_set(x2, y2, 2);
    }
    MountPlanetX = x;
    MountPlanetY = y;
    if (!skip_label) new_landmark(x, y + 2, natural_name(1));
}

void GAMEAPI world_borehole(int32_t x, int32_t y) {
    if (!map_in_bounds(x, y) && !find_landmark_site(x, y, draw_site, site_land)) return;
    // The three bore holes: shape picked from the clock (logic clock in deterministic mode).
    uint32_t t = engine_logic_time();
    int32_t s = t & 7;
    int32_t shape = (int32_t)t >> 3 & 3;
    int32_t hole1 = 8, hole2 = s / 3 + 5, hole3 = 3 - s % 3;
    if (shape & 2) {
        int32_t base;
        hole2--;
        hole3--;
        if (shape & 1) {
            hole2++;
            base = 8;
            hole3++;
        } else {
            hole2--;
            base = 6;
            hole3--;
        }
        hole1 = ((base + 8) & 7) + 1;
        hole2 = ((hole2 + 8) & 7) + 1;
        hole3 = ((hole3 + 8) & 7) + 1;
    }
    for (int32_t i = 0; i < 9; i++) {
        int32_t x2 = map_wrap_x(x + TableOffsetX[i]);
        int32_t y2 = y + TableOffsetY[i];
        if (!map_in_bounds(x2, y2)) continue;
        world_alt_set(x2, y2, 3, 1);
        bit_set(x2, y2, 0x100000, 1);
        if (i == hole1 || i == hole2 || i == hole3) {
            bit_set(x2, y2, 0x1000000, 1);
            bit2_set(x2, y2, LM_BOREHOLE, 1);
            code_set(x2, y2, i);
        }
    }
    // Raise ocean tiles around each hole to shore level.
    const int32_t holes[3] = {hole1, hole2, hole3};
    for (int32_t h = 0; h < 3; h++) {
        int32_t hx = map_wrap_x(x + TableOffsetX[holes[h]]);
        for (int32_t k = 1; k < 9; k++) {
            int32_t x2 = map_wrap_x(hx + TableOffsetX[k]);
            int32_t y2 = TableOffsetY[k] + TableOffsetY[holes[h]] + y;
            if (map_in_bounds(x2, y2) && map_is_ocean(x2, y2)) world_alt_set(x2, y2, 3, 1);
        }
    }
    bit2_set(x, y, LM_BOREHOLE, 1);
    new_landmark(x, y, natural_name(12));
}

void GAMEAPI world_temple(int32_t x, int32_t y) {
    if (!map_in_bounds(x, y) && !find_landmark_site(x, y, draw_site, site_land)) return;
    for (int32_t i = 0; i < 9; i++) {
        int32_t x2 = map_wrap_x(x + TableOffsetX[i]);
        int32_t y2 = y + TableOffsetY[i];
        if (!map_in_bounds(x2, y2)) continue;
        world_alt_set(x2, y2, 3, 1);
        bit_set(x2, y2, 0x100000, 1);
        bit2_set(x2, y2, LM_NEXUS, 1);
        code_set(x2, y2, i);
    }
    new_landmark(x, y, natural_name(13));
}

void GAMEAPI world_unity(int32_t x, int32_t y) {
    if (!ExpansionEnabled) return;
    if (!map_in_bounds(x, y) && !find_landmark_site(x, y, draw_site, site_land)) return;
    for (int32_t i = 0; i < 25; i++) {
        int32_t x2 = map_wrap_x(x - 1 + TableOffsetX[i]);
        int32_t y2 = y - 1 + TableOffsetY[i];
        if (map_in_bounds(x2, y2)) world_alt_set(x2, y2, 4, 1);
    }
    for (int32_t i = 0; i < 25; i++) {
        int32_t x2 = map_wrap_x(x + 1 + TableOffsetX[i]);
        int32_t y2 = y + 1 + TableOffsetY[i];
        if (map_in_bounds(x2, y2)) world_alt_set(x2, y2, 4, 1);
    }
    for (int32_t i = 0; i < 25; i++) {
        int32_t x2 = map_wrap_x(x + TableOffsetX[i]);
        int32_t y2 = y + TableOffsetY[i];
        if (!map_in_bounds(x2, y2)) continue;
        rocky_set(x2, y2, 0);
        bit_set(x2, y2, 0x100, 0);
        bit_set(x2, y2, 0x20, 0);
        bit2_set(x2, y2, LM_UNITY, 1);
        code_set(x2, y2, i);
        bit_set(x2, y2, (i == 0 || i == 8 || i == 10 || i == 19) ? 0x10000000 : 0x100000, 1);
    }
    new_landmark(x, y, natural_name(14));
}

void GAMEAPI world_fossil(int32_t x, int32_t y) {
    if (!map_in_bounds(x, y) && !find_landmark_site(x, y, draw_site, site_ocean)) return;
    for (int32_t i = 0; i < 6; i++) {
        int32_t x2 = map_wrap_x(x + TableOffsetX[i]);
        int32_t y2 = y + TableOffsetY[i];
        if (!map_in_bounds(x2, y2) || !map_is_ocean(x2, y2)) continue;
        world_alt_set(x2, y2, 1, 1);
        bit2_set(x2, y2, LM_FOSSIL, 1);
        code_set(x2, y2, i);
    }
    new_landmark(x, y, natural_name(15));
}

void GAMEAPI world_canyon(int32_t x, int32_t y) {
    if (!map_in_bounds(x, y) && !find_landmark_site(x, y, draw_site, site_land)) return;
    for (int32_t k = 0; k < 12; k++) {
        int32_t i = CanyonOffsets[k];
        int32_t x2 = map_wrap_x(x + TableOffsetX[i]);
        int32_t y2 = y + TableOffsetY[i];
        if (!map_in_bounds(x2, y2)) continue;
        world_alt_set(x2, y2, 4, 1);
        world_alt_set(x2, y2, 3, 1);
    }
    for (int32_t k = 0; k < 12; k++) {
        int32_t i = CanyonOffsets[k];
        int32_t x2 = map_wrap_x(x + TableOffsetX[i]);
        int32_t y2 = y + TableOffsetY[i];
        if (!map_in_bounds(x2, y2)) continue;
        bit2_set(x2, y2, LM_CANYON, 1);
        code_set(x2, y2, i);
    }
    new_landmark(x, y + 2, natural_name(9));
}

void GAMEAPI world_mesa(int32_t x, int32_t y) {
    if (!map_in_bounds(x, y) && !find_landmark_site(x, y, draw_site, site_land)) return;
    for (int32_t i = 0; i < 49; i++) {
        int32_t x2 = map_wrap_x(x + TableOffsetX[i]);
        int32_t y2 = y + TableOffsetY[i];
        if (!map_in_bounds(x2, y2)) continue;
        world_alt_set(x2, y2, (i < 25) + 3, 1);
        bit2_set(x2, y2, LM_MESA, 1);
        code_set(x2, y2, i);
    }
    new_landmark(x, y + 2, natural_name(8));
}

void GAMEAPI world_ridge(int32_t x, int32_t y) {
    if (!map_in_bounds(x, y) && !find_landmark_site(x, y, draw_site, site_land)) return;
    for (int32_t k = 0; k < 13; k++) {
        int32_t i = RidgeOffsets[k];
        int32_t x2 = map_wrap_x(x + TableOffsetX[i]);
        int32_t y2 = y + TableOffsetY[i];
        if (map_in_bounds(x2, y2)) world_alt_set(x2, y2, 4, 1);
    }
    for (int32_t k = 0; k < 13; k++) {
        int32_t i = RidgeOffsets[k];
        int32_t x2 = map_wrap_x(x + TableOffsetX[i]);
        int32_t y2 = y + TableOffsetY[i];
        if (!map_in_bounds(x2, y2)) continue;
        bit2_set(x2, y2, LM_RIDGE, 1);
        code_set(x2, y2, i);
    }
    new_landmark(x, y + 2, natural_name(11));
}

void GAMEAPI world_geothermal(int32_t x, int32_t y) {
    if (!map_in_bounds(x, y) && !find_landmark_site(x, y, draw_site, site_ocean)) return;
    bool gap = false;
    for (int32_t i = 0; i < 25; i++) {
        int32_t x2 = map_wrap_x(x + TableOffsetX[i]);
        int32_t y2 = y + TableOffsetY[i];
        if (!map_in_bounds(x2, y2) || (!map_is_ocean(x2, y2) && i >= 9)) continue;
        if ((gap || i == 0 || game_rand() % 25 != 0) && (i < 9 || game_rand() % 3 != 0)) {
            world_alt_set(x2, y2, 2, 1);
            bit2_set(x2, y2, LM_GEOTHERMAL, 1);
            code_set(x2, y2, i);
        } else {
            gap = true;
        }
    }
    new_landmark(x, y, natural_name(10));
}

// Landmark sequence shared by world_landmarks and world_build.
static void place_landmarks() {
    world_monsoon(-1, -1);
    world_crater(-1, -1);
    world_volcano(-1, -1, 0);
    world_mesa(-1, -1);
    world_ridge(-1, -1);
    world_diamond(-1, -1);
    world_ruin(-1, -1);
    if (ExpansionEnabled) {
        world_unity(-1, -1);
        if (ExpansionEnabled) world_fossil(-1, -1);
    }
    world_temple(-1, -1);
    world_borehole(-1, -1);
    world_sargasso(-1, -1);
    world_dune(-1, -1);
    world_fresh(-1, -1);
    world_geothermal(-1, -1);
    fixup_landmarks();
}

void GAMEAPI world_landmarks() { place_landmarks(); }

void GAMEAPI world_build() {
    int32_t continents = 0;
    for (int32_t attempt = 0;;) {
        for (int32_t i = 0; i < 11; i++) AltNatural[i] = AltNaturalDefault[i];
        WorldAddTemperature = 1;
        WorldSkipTerritory = 1;
        // Random world parameters (size, ocean, land, erosion, orbit, clouds, natives) for -1.
        int32_t* params = &MapSizePlanet;
        for (int32_t i = 0; i < 7; i++) {
            if (params[i] < 0) params[i] = game_rand() % 3;
        }
        map_wipe();
        game().map.flat = 0;
        int32_t islands = (WorldBuilder.islands * game().map.tile_count) / 3200;
        WorldBuildVal1 =
            (((2 - MapOceanCoverage) * WorldBuilder.land_mod + WorldBuilder.land_base) * game().map.tile_count) / 3200;
        BrushVal1 = 0;
        if (GameState & STATE_OMNISCIENT_VIEW) {
            MapWin_clear_terrain(MapWins[0]);
            draw_map(1);
        }
        for (int32_t tries = 0; (BrushVal1 < WorldBuildVal1 || continents < islands) && ++tries < 100;) {
            build_continent(++continents);
        }
        if (MapErosiveForces >= 1) {
            int32_t hills =
                (((MapErosiveForces - 1) * WorldBuilder.hills_mod + WorldBuilder.hills_base) * game().map.tile_count) /
                3200;
            if (MapOceanCoverage == 2) hills /= 2;
            for (; hills > 0; hills--) build_hills(4);
        }
        world_erosion();
        if (!(game().map.flat & 1) && MapOceanCoverage < 2 && (MapOceanCoverage == 0 || game_rand() % 3 == 0))
            world_polar_caps();
        world_shorelines();
        if ((GameState & STATE_OMNISCIENT_VIEW) || !world_validate() || ++attempt >= 4) break;
    }
    world_temperature();
    world_riverbeds();
    world_fungus();
    place_landmarks();
    world_linearize_contours();
    WorldAddTemperature = 0;
    world_climate();
    world_rocky();
    WorldSkipTerritory = 0;
    redraw_terrain();
}
