// The game state as one object (docs/CLEANUP_PLAN.md, "Globals become game-state objects", stage 1).
//
// GameStateView groups the main records of the game: a view of reference members onto the
// globals of globals.h, which bind to the native records of engine/state.h; game().bases[i] is
// Bases[i].
// (GameState itself is the original's game state flags word, 0x9A64C0, hence the name of the view.)
//
// Engine code reaches the state through game(): game().bases[id], game().map.x, ... The globals
// stay declared, for the UI and for the code not yet migrated.
#pragma once
#include "engine/globals.h"

struct MapView {
    int32_t& x;  // MapAreaX: tiles per row times 2 (x steps by 2 along a row)
    int32_t& y;  // MapAreaY: rows
    int32_t& half_x;  // MapHalfX: x / 2
    int32_t& tile_count;  // MapAreaTiles: half_x * y
    int32_t& sq_root;  // MapAreaSqRoot: quick_root(tile_count)
    int32_t& flat;  // MapToggleFlat: bit 0 set when the map does not wrap
    MAP*& tiles;  // MapTiles: tile_count tiles, row by row
};

struct GameStateView {
    MapView map;
    Faction (&factions)[8];
    MFaction (&mfactions)[8];
    BASE (&bases)[512];
    int32_t& base_count;
    VEH (&vehs)[2049];  // [2048] + 1 scratch slot used by veh_fake
    int32_t& veh_count;
    CRules& rules;
    int32_t& turn;  // CurrentTurn
};

// Defined after every global it refers to in each translation unit, so it is initialised after them.
inline GameStateView g_game_state{{MapAreaX, MapAreaY, MapHalfX, MapAreaTiles, MapAreaSqRoot, MapToggleFlat, MapTiles},
                                  Factions,
                                  MFactions,
                                  Bases,
                                  BaseCount,
                                  Vehs,
                                  VehCount,
                                  Rules,
                                  CurrentTurn};

inline GameStateView& game() { return g_game_state; }
