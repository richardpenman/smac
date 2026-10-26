// Map storage and tile accessors (original module around 0x590E90-0x593510).
//
// The map is a heap array of MAP tiles (MapTiles). Only tiles with (x + y) even exist; a tile is
// stored at index (x >> 1) + y * MapHalfX. Network messaging (message_*, synch_* except synch_bit)
// belongs to the multiplayer module and is not part of this file.
#pragma once
#include <cstdio>
#include "engine/game_state.h"
#include "engine/globals.h"
#include "engine/crt.h"

// ---- original constant tables (.rdata, re-created in source) ----------------------------------

// 0x66EF28 / 0x66EF3C: the four diagonal neighbours (NE, SE, SW, NW) plus the centre.
inline constexpr int32_t DiagOffsetX[5] = {1, 1, -1, -1, 0};
inline constexpr int32_t DiagOffsetY[5] = {-1, 1, 1, -1, 0};
// 0x66EF50 / 0x66EF74: the eight neighbours (clockwise from NE) plus the centre.
inline constexpr int32_t NearOffsetX[9] = {1, 2, 1, 0, -1, -2, -1, 0, 0};
inline constexpr int32_t NearOffsetY[9] = {-1, 0, 1, 2, 1, 0, -1, -2, 0};
// 0x66EFBC / 0x66F440: tile offsets ordered by distance (centre, ring 1, ring 2, ...).
extern const int32_t TableOffsetX[289];
extern const int32_t TableOffsetY[289];
// 0x66F8C4: number of TableOffset entries within radius r (r = 0..8).
inline constexpr int32_t TableRange[9] = {1, 9, 25, 49, 81, 121, 169, 225, 289};

// ---- globals ------------------------------------------------------------------------------------

GLOBAL(int32_t[11], AltNaturalDefault, 0x68FAF4);  // altitude-detail thresholds used by world_build
GLOBAL(int32_t[11], AltNaturalLinear, 0x68FB20);  // linearized thresholds (world_linearize_contours)
GLOBAL(int32_t[11], AltNatural, 0x68FB4C);  // current thresholds: detail - sea level -> altitude
GLOBAL(int32_t, GoodyCoefX, 0x68FB78);  // goody_at pod pattern coefficients
GLOBAL(int32_t, GoodyCoefY, 0x68FB7C);
GLOBAL(int32_t, MapResizeOldX, 0x945AD4);  // map_resize scratch
GLOBAL(int32_t, MapResizeOldY, 0x945AD8);
GLOBAL_NATIVE(MAP*, MapResizeOldTiles, 0x945AD0);
GLOBAL(int32_t, MapScaleExtra, 0x945AEC);  // map_scale_set / map_scale
GLOBAL(int32_t, MountPlanetX, 0x9A6804);
GLOBAL(int32_t, MountPlanetY, 0x9A6808);
// Landmark-overlay tables used by the map renderer (reset by fixup_landmarks / delete_landmark).
GLOBAL(uint8_t[245], LandmarkTileIndex, 0x685684);  // [landmark code * 5 + i] -> overlay cell
GLOBAL(uint8_t[113], LandmarkOverlayVolcanoInit, 0x68577C);
GLOBAL(uint8_t[113], LandmarkOverlayCraterInit, 0x6857F0);
GLOBAL(uint8_t[113], LandmarkOverlayMesaInit, 0x685864);
GLOBAL(uint8_t[113], LandmarkOverlayCanyonInit, 0x68593C);
GLOBAL(uint8_t[113], LandmarkOverlayRidgeInit, 0x6859B0);
GLOBAL(uint8_t[113], LandmarkOverlayMesa, 0x7D3A78);
GLOBAL(uint8_t[113], LandmarkOverlayVolcano, 0x7D3B74);
GLOBAL(uint8_t[113], LandmarkOverlayRidge, 0x7F64E8);
GLOBAL(uint8_t[113], LandmarkOverlayCanyon, 0x7F655C);
GLOBAL(uint8_t[113], LandmarkOverlayCrater, 0x7F65D0);

// ---- tile helpers (not original functions) ------------------------------------------------------

inline MAP* map_tile(int32_t x, int32_t y) { return &game().map.tiles[game().map.half_x * y + (x >> 1)]; }
inline int32_t map_alt(int32_t x, int32_t y) { return map_tile(x, y)->climate >> 5; }
inline bool map_is_ocean(int32_t x, int32_t y) { return (map_tile(x, y)->climate & 0xE0) < 0x60; }
// Horizontal wrap of the original (a single correction; flat maps do not wrap).
inline int32_t map_wrap_x(int32_t x) {
    if ((game().map.flat & 1) == 0) {
        if (x < 0) x += game().map.x;
        else if (x >= game().map.x) x -= game().map.x;
    }
    return x;
}
inline bool map_in_bounds(int32_t x, int32_t y) { return y >= 0 && y < game().map.y && x >= 0 && x < game().map.x; }
// Common draw pattern: n > 1 ? game_rand() % n : 0.
int32_t map_rand_mod(int32_t n);

// ---- map module ---------------------------------------------------------------------------------

ADDR(0x590E90) void GAMEAPI map_shutdown();
// Allocates the tile and abstract arrays for MapAreaX/MapAreaY. Returns 0 on success.
ADDR(0x590ED0) int32_t GAMEAPI map_init();
// Resets every tile to deep ocean, clears landmarks and picks a new MapRandomSeed.
ADDR(0x591040) void GAMEAPI map_wipe();
// Write/read the map header and arrays (CRT streams, engine/crt.h). Return 0 on success.
ADDR(0x5910B0) int32_t GAMEAPI map_write(CrtFile* file);
ADDR(0x591130) int32_t GAMEAPI map_read(CrtFile* file);
ADDR(0x591210) int32_t GAMEAPI abstract_at(int32_t x, int32_t y);
ADDR(0x591230) void GAMEAPI abstract_set(int32_t x, int32_t y, int32_t region);
ADDR(0x591260) void GAMEAPI alt_put_detail(int32_t x, int32_t y, int32_t detail);
// Sets the altitude of a tile. During a game, also handles landmarks, rockiness, bases and units
// that end up on the wrong side of the shoreline.
ADDR(0x591290) void GAMEAPI alt_set(int32_t x, int32_t y, int32_t altitude);
// Altitude implied by the tile's altitude detail and the sea level.
ADDR(0x5918A0) int32_t GAMEAPI alt_natural(int32_t x, int32_t y);
// alt_set, then adjusts the altitude detail to match the new altitude.
ADDR(0x5918F0) void GAMEAPI alt_set_both(int32_t x, int32_t y, int32_t altitude);
// Elevation in metres shown in the status window (-3000..3500).
ADDR(0x5919C0) int32_t GAMEAPI pfncGetElevation(int32_t x, int32_t y);
ADDR(0x591A80) void GAMEAPI climate_set(int32_t x, int32_t y, int32_t rainfall);
ADDR(0x591AD0) void GAMEAPI temp_set(int32_t x, int32_t y, int32_t temperature);
ADDR(0x591B10) void GAMEAPI owner_set(int32_t x, int32_t y, int32_t faction_id);
ADDR(0x591B50) void GAMEAPI site_set(int32_t x, int32_t y, int32_t site);
ADDR(0x591B90) void GAMEAPI region_set(int32_t x, int32_t y, int32_t region);
ADDR(0x591BC0) void GAMEAPI rocky_set(int32_t x, int32_t y, int32_t rocky);
ADDR(0x591C10) void GAMEAPI using_set(int32_t x, int32_t y, int32_t faction_id);
ADDR(0x591C50) void GAMEAPI lock_set(int32_t x, int32_t y, int32_t faction_id);
// Locks a tile for a faction. Returns 1 if another faction holds the lock.
ADDR(0x591C90) int32_t GAMEAPI lock_map(int32_t x, int32_t y, int32_t faction_id);
ADDR(0x591CF0) void GAMEAPI unlock_map(int32_t x, int32_t y, int32_t faction_id);
ADDR(0x591D30) void GAMEAPI bit_put(int32_t x, int32_t y, uint32_t items);
ADDR(0x591D60) void GAMEAPI bit_set(int32_t x, int32_t y, uint32_t items, int32_t add);
ADDR(0x591DB0) void GAMEAPI bit2_set(int32_t x, int32_t y, uint32_t landmarks, int32_t add);
ADDR(0x591E00) void GAMEAPI code_set(int32_t x, int32_t y, int32_t code);
// Copies the tile items into the faction's view of the tile.
ADDR(0x591E50) void GAMEAPI synch_bit(int32_t x, int32_t y, int32_t faction_id);
// Multiplayer: sends a prototype's obsolete flags to the other players.
ADDR(0x593420) void GAMEAPI synch_obs(int32_t unit_id);
// 0x591E90: faction using a land tile if it has a treaty (not a pact) with faction_id, else -1.
ADDR(0x591E90) int32_t GAMEAPI treaty_user_at(int32_t x, int32_t y, int32_t faction_id);
// Natural rockiness pattern of a tile (0 flat, 1 rolling, 2 rocky).
ADDR(0x591F00) int32_t GAMEAPI minerals_at(int32_t x, int32_t y);
// Resource bonus of a tile: 0 none, 1 nutrient, 2 mineral, 3 energy.
ADDR(0x592030) int32_t GAMEAPI bonus_at(int32_t x, int32_t y);
// Supply pod: 0 none, 1 supply pod, 2 unity pod pattern.
ADDR(0x592140) int32_t GAMEAPI goody_at(int32_t x, int32_t y);
// Appends a location description (base name and/or coordinates) to buffer.
ADDR(0x592250)
void GAMEAPI say_loc(char* buffer, int32_t x, int32_t y, int32_t near_base, int32_t coords, int32_t with_near);
// Clears the AI site value of every tile within radius 2.
ADDR(0x592400) void GAMEAPI site_radius(int32_t x, int32_t y);
// 0x592480: plants fungus on a tile (clearing improvements) and clears the nearby site values.
ADDR(0x592480) void GAMEAPI fungus_site_radius(int32_t x, int32_t y);
// Index of a landmark within radius_index of (x, y), or -1.
ADDR(0x592550) int32_t GAMEAPI find_landmark(int32_t x, int32_t y, int32_t radius_index);
// Adds a named landmark. Returns its index or -1 if the table is full.
ADDR(0x592600) int32_t GAMEAPI new_landmark(int32_t x, int32_t y, const char* name);
// Whether faction_id may name/erase a landmark at (x, y).
ADDR(0x592650) int32_t GAMEAPI valid_landmark(int32_t x, int32_t y, int32_t faction_id);
// Removes the named landmark nearest to (x, y) (radius 1).
ADDR(0x5926F0) void GAMEAPI kill_landmark(int32_t x, int32_t y);
// Removes the landmark terrain code of a tile (called when its altitude changes).
ADDR(0x5927D0) void GAMEAPI delete_landmark(int32_t x, int32_t y);
// Rebuilds the landmark overlay tables of the renderer from the map.
ADDR(0x592940) void GAMEAPI fixup_landmarks();
ADDR(0x592A80) void GAMEAPI set_dirty();
ADDR(0x592AF0) void GAMEAPI clear_dirty();
// 0x592B40: shrinks the map to width x height, resampling the old tiles.
ADDR(0x592B40) void GAMEAPI map_resize(int32_t width, int32_t height);
// 0x592D70 / 0x592D90: scale factor (16 + extra) / 16.
ADDR(0x592D70) void GAMEAPI map_scale_set(int32_t extra);
ADDR(0x592D90) int32_t GAMEAPI map_scale(int32_t value);
// 0x592DB0: whether (x, y) lies inside the rectangle [x1, x2] x [y1, y2].
ADDR(0x592DB0) int32_t GAMEAPI in_rect(int32_t x, int32_t y, int32_t x1, int32_t y1, int32_t x2, int32_t y2);

// on_map (0x4712A0) sits in the UI address range but is a plain bounds check.
ADDR(0x4712A0) int32_t GAMEAPI on_map(int32_t x, int32_t y);
