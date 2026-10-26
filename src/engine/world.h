// Random world generation (original module around 0x5C2020-0x5C89A0) and the value-noise helper
// it uses (0x5C1E20-0x5C1F20, at the end of the veh address range).
#pragma once
#include "engine/globals.h"

// ---- globals ------------------------------------------------------------------------------------
GLOBAL(int32_t, RainfallBaseFaction, 0x9B22DC);  // world_rainfall: owner of a base that dried out
GLOBAL(int32_t, RainfallViewerBaseID, 0x9B22E0);  // world_rainfall: viewer's base near a change
GLOBAL(uint32_t, FungusClearItems, 0x6919EC);  // items removed when world_fungus plants fungus
GLOBAL(int32_t[12], CanyonOffsets, 0x6858D8);  // TableOffset indices of the canyon shape
GLOBAL(int32_t[13], RidgeOffsets, 0x685908);  // TableOffset indices of the ridge shape

// Value-noise grid used by world_erosion and world_fungus (a stack object in the original).
// grid: 17x17 cells (row stride 19) with wrapped edges; seed: 19 rows of random 0..15.
struct WorldFractal {
    int8_t grid[19 * 19];
    int8_t seed[19 * 19];
};
static_assert(sizeof(WorldFractal) == 0x2D2, "WorldFractal layout");

// 0x5C1E20: fills the grid with random values 0..15 (342 game_rand draws).
ADDR(0x5C1E20) void THISCALL fractal_init(WorldFractal* self);
// 0x5C1F20: bilinear interpolation of the grid at (x, y) in 1/256 cell units (result 0..480).
ADDR(0x5C1F20) int32_t THISCALL fractal_at(WorldFractal* self, int32_t x, int32_t y);
// 0x5C1FC0: noise value 0..100 mixing the coarse grid and a finer sampling (cell units << fine);
// world_erosion (fine = 7) and world_fungus (fine = 8) inline it, the original never calls it.
ADDR(0x5C1FC0) int32_t THISCALL fractal_value(WorldFractal* self, int32_t x, int32_t y, int32_t fine);

// ---- world generation ---------------------------------------------------------------------------

// Sets the altitude of a tile and smooths the surrounding tiles so that neighbouring altitudes
// differ by at most one step per ring. toggle: also adjust the altitude detail (alt_set_both).
ADDR(0x5C2020) void GAMEAPI world_alt_set(int32_t x, int32_t y, int32_t altitude, int32_t toggle);
ADDR(0x5C2380) void GAMEAPI world_raise_alt(int32_t x, int32_t y);
ADDR(0x5C23E0) void GAMEAPI world_lower_alt(int32_t x, int32_t y);
// Paints land of the given altitude around (x, y) unless it would join another landmass.
ADDR(0x5C2440) void GAMEAPI brush(int32_t x, int32_t y, int32_t altitude);
// Random diagonal walk from (x, y) brushing land until `size` tiles were painted.
ADDR(0x5C27F0) void GAMEAPI paint_land(int32_t x, int32_t y, int32_t altitude, int32_t size);
// Grows continent number `count` from a random ocean tile.
ADDR(0x5C28F0) void GAMEAPI build_continent(int32_t count);
ADDR(0x5C2B40) void GAMEAPI build_hills(int32_t altitude);
// Sets the altitude detail of every tile from fractal noise and the coastline shape.
ADDR(0x5C2CB0) void GAMEAPI world_erosion();
// Rockiness and rocky-terrain resources of every tile.
ADDR(0x5C32A0) void GAMEAPI world_rocky();
ADDR(0x5C3440) void GAMEAPI world_fungus();
// Places river sources (BIT_RIVERBED) near high ground.
ADDR(0x5C3680) void GAMEAPI world_riverbeds();
// Traces rivers downhill from every river source.
ADDR(0x5C38B0) void GAMEAPI world_rivers();
// Recomputes every tile's altitude from its altitude detail and smooths the coasts.
ADDR(0x5C3F70) void GAMEAPI world_shorelines();
// Non-zero if the largest landmass is too dominant (world_build retries).
ADDR(0x5C40F0) int32_t GAMEAPI world_validate();
ADDR(0x5C4170) void GAMEAPI world_temperature();
// 0x5C4410: raises the rainfall of a tile by one (maximum 2).
ADDR(0x5C4410) void GAMEAPI world_raise_rainfall(int32_t x, int32_t y);
ADDR(0x5C4470) void GAMEAPI world_rainfall();
// AI colonization value of a tile (1..15, 0 = unusable). sea: value for a sea base.
ADDR(0x5C4FD0) int32_t GAMEAPI world_site(int32_t x, int32_t y, int32_t sea);
// Open-terrain counts per region and the coast/strait flags of every tile.
ADDR(0x5C55C0) void GAMEAPI world_analysis();
ADDR(0x5C58C0) void GAMEAPI world_alt_put_detail(int32_t x, int32_t y);
ADDR(0x5C58E0) void GAMEAPI world_polar_caps();
// Recomputes temperature, rivers, rainfall and regions after the terrain changed.
ADDR(0x5C5A30) void GAMEAPI world_climate();
ADDR(0x5C5AE0) void GAMEAPI world_linearize_contours();
// Non-zero if a landmark lies within radius 8 of (x, y).
ADDR(0x5C5BD0) int32_t GAMEAPI near_landmark(int32_t x, int32_t y);

// Landmark generators: x, y < 0 (or off the map) picks a random site.
ADDR(0x5C5C70) void GAMEAPI world_crater(int32_t x, int32_t y);
ADDR(0x5C5EF0) void GAMEAPI world_monsoon(int32_t x, int32_t y);
ADDR(0x5C6200) void GAMEAPI world_sargasso(int32_t x, int32_t y);
ADDR(0x5C64A0) void GAMEAPI world_ruin(int32_t x, int32_t y);
ADDR(0x5C6740) void GAMEAPI world_dune(int32_t x, int32_t y);
ADDR(0x5C69E0) void GAMEAPI world_diamond(int32_t x, int32_t y);
ADDR(0x5C6C40) void GAMEAPI world_fresh(int32_t x, int32_t y);
// skip_label: do not name the landmark (Mount Planet eruptions).
ADDR(0x5C6DB0) void GAMEAPI world_volcano(int32_t x, int32_t y, int32_t skip_label);
ADDR(0x5C7020) void GAMEAPI world_borehole(int32_t x, int32_t y);
ADDR(0x5C7540) void GAMEAPI world_temple(int32_t x, int32_t y);
ADDR(0x5C7750) void GAMEAPI world_unity(int32_t x, int32_t y);
ADDR(0x5C7A80) void GAMEAPI world_fossil(int32_t x, int32_t y);
ADDR(0x5C7CB0) void GAMEAPI world_canyon(int32_t x, int32_t y);
ADDR(0x5C7F40) void GAMEAPI world_mesa(int32_t x, int32_t y);
ADDR(0x5C8150) void GAMEAPI world_ridge(int32_t x, int32_t y);
ADDR(0x5C83B0) void GAMEAPI world_geothermal(int32_t x, int32_t y);
ADDR(0x5C8630) void GAMEAPI world_landmarks();
// Generates a new random world (MapAreaX/MapAreaY and the world parameters must be set).
ADDR(0x5C86E0) void GAMEAPI world_build();
