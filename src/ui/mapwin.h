// Map window (MapWin, ui module 0x4626E0..0x4712D0): the isometric map view.
//
// The window objects live in the engine memory image (the main view is MainConsole at 0x9156B0,
// a Console derived from MapWin; ui_bridge.h MapWins[]), so their integer fields are read and
// written at the original byte offsets (mapwin_field). What the original keeps behind pointers
// (the GraphicWin canvas, the per-tile terrain cache at +4) is native: MapWinNative.
//
// Functions declared in engine/ui_bridge.h (pfncRedrawMap, pfncMoveMap) are defined here under
// their own names: smacx replaces the headless "mapview" area with them (src/headless
// ui_headless_mapview.cpp; the rest of that area is in src/ui/win_mapview.cpp).
#pragma once
#include <cstdint>
#include <vector>

#include "engine/memmap.h"
#include "ui/gfx.h"

// ---- MapWin fields (byte offsets of the original object) ----
constexpr uint32_t kMwTileCache = 0x4;        // per-tile terrain cache pointer (native: MapWinNative)
constexpr uint32_t kMwElevScale = 0x1DD6C;    // corner height step: 25 (8 with "flat terrain")
constexpr uint32_t kMwOptions = 0x1DD70;      // display option bits (MapWinOptions for the main view)
constexpr uint32_t kMwActive = 0x1DD74;
constexpr uint32_t kMwIndex = 0x1DD78;        // index in MapWins (0: main view, 1: world map)
constexpr uint32_t kMwTracking = 0x1DD80;     // mouse-over tracking active
constexpr uint32_t kMwZoom = 0x1DD98;         // zoom step (0: normal; < 0: smaller)
constexpr uint32_t kMwCenterX = 0x1DD9C;
constexpr uint32_t kMwCenterY = 0x1DDA0;
constexpr uint32_t kMwOriginX = 0x1DDA4;      // tile at the top-left of the view
constexpr uint32_t kMwOriginY = 0x1DDA8;
constexpr uint32_t kMwPixelX = 0x1DDAC;       // pixel offset of the view (map narrower than the window)
constexpr uint32_t kMwPixelY = 0x1DDB0;
constexpr uint32_t kMwTileW = 0x1DDB4;        // 8q
constexpr uint32_t kMwTileH = 0x1DDB8;        // 4q
constexpr uint32_t kMwHalfW = 0x1DDBC;        // 4q
constexpr uint32_t kMwHalfH = 0x1DDC0;        // 2q
constexpr uint32_t kMwQuarterW = 0x1DDC4;     // 2q
constexpr uint32_t kMwQ = 0x1DDC8;            // q: zoom unit
constexpr uint32_t kMwColsOdd = 0x1DDCC;      // tiles per odd row
constexpr uint32_t kMwRowsAbove = 0x1DDD0;
constexpr uint32_t kMwColsEven = 0x1DDD4;     // tiles per even row
constexpr uint32_t kMwRowsBelow = 0x1DDD8;
constexpr uint32_t kMwUnitLift = 0x1DDDC;     // unit/base height above the terrain (option 2)
constexpr uint32_t kMwUnitLift2 = 0x1DDE0;
constexpr uint32_t kMwLastZoom = 0x1EEC0;
constexpr uint32_t kMwLastElev = 0x1EEC4;
constexpr uint32_t kMwFont0 = 0x1EEC8;        // Font objects (map labels)
constexpr uint32_t kMwFont1 = 0x1EEF4;
constexpr uint32_t kMwFont2 = 0x1EF20;
constexpr uint32_t kMwScrolling = 0x21A44;    // incremental scroll redraw in progress
// Byte offset of the GraphicWin virtual base: MapWin 0x21A6C, Console (the main view) 0x23D94.
constexpr uint32_t kMapWinVBase = 0x21A6C;
constexpr uint32_t kConsoleVBase = 0x23D94;
// GraphicWin fields (offsets from the virtual base).
constexpr uint32_t kGwClientRect = 0x474;     // RECT: the drawable client area
constexpr uint32_t kGwWidth = 0x4C4;
constexpr uint32_t kGwHeight = 0x4C8;         // negative (top-down DIB)

// Fixed zoom factor (piZoomNum / piZoomDenom; used when ZoomNum > 0, else the window's zoom step).
GLOBAL(int32_t, ZoomNum, 0x691E6C);
GLOBAL(int32_t, ZoomDenom, 0x691E70);

inline int32_t& mw(uint8_t* win, uint32_t off) { return *reinterpret_cast<int32_t*>(win + off); }

// ---- original globals of the map drawing ----
GLOBAL(UiRect, VisibleTiles, 0x7D3C28);          // prVisibleTiles: main view tile range
GLOBAL(int32_t[4][2], TileCorners, 0x7D3C08);    // W, N, E, S corner points of the last tile drawn
GLOBAL(int32_t[4][2], CursorTileCorners, 0x7D3BE8);  // TileCorners of the cursor tile
GLOBAL(int32_t, TerrainDefaultLight, 0x686354);  // light of tiles not currently seen
// Sub-polygon tables of the tile (4 triangles: centre + two diamond corners).
GLOBAL(int32_t[12], TilePolyX, 0x685F20);        // in 2q units
GLOBAL(int32_t[12], TilePolyY, 0x685F50);        // in q units
GLOBAL(int32_t[12], TilePolyCorner, 0x685F80);   // corner index of each vertex (0 centre, 1..4 W N E S)
GLOBAL(int32_t[12][2], TilePolyUV, 0x686090);    // texture coordinates of each vertex
// The 4 tiles around each tile corner (corner c: entries (c-1)*4 .. +3), and 4-vertex quads.
GLOBAL(int32_t[16], CornerTileX, 0x685EA0);
GLOBAL(int32_t[16], CornerTileY, 0x685EE0);
GLOBAL(uint8_t[256][2], RainTransition, 0x685484);  // [neighbour mask]: texture variant, rotation
GLOBAL(int32_t[16][2], OceanQuadUV, 0x685CA8);
GLOBAL(int32_t[16][2], CoastQuadUV, 0x6860F0);
GLOBAL(int32_t[80], OceanDepthLight, 0x6861F0);     // ocean depth (detail) -> light
GLOBAL(int32_t[4][4][2], WaterQuadPoints, 0x7D3AF0); // the 4 water quads of the last tile drawn
GLOBAL(int32_t[4][4], WaterQuadLight, 0x7F6644);     // their depth lights
// The point whose surroundings GamePreferences 0x200 outlines (DAT_00685478 / _0068547C).
GLOBAL(int32_t, OutlineCenterX, 0x685478);
GLOBAL(int32_t, OutlineCenterY, 0x68547C);
// Landmark overlay tables [code]: texture index (-1: none) and rotation.
GLOBAL(int8_t[64][2], VolcanoOverlay, 0x685A98);
GLOBAL(int8_t[64][2], MesaOverlay, 0x685AFC);
GLOBAL(int8_t[64][2], CraterOverlay, 0x685BD4);
// Coast masks: [texel of the coast shape][coast bits 0..7] (0x684730, 53 x 8 bytes).
GLOBAL(uint8_t[53][8], CoastMask, 0x684730);
// Low 32 bits of the original's tile array address (paTiles): MapWin_gen_terrain_poly seeds the
// tile variants with the tile's address, so it depends on where the heap put the map. Native
// builds use this value as the address of tile 0 (0 by default; harness dumps record it).
extern uint32_t TileVariantBase;
// The tile cache entry of (x, y) (12 bytes; nullptr without cache).
uint8_t* ui_mapwin_cache_entry(uint8_t* win, int32_t x, int32_t y);

// Native parts of a map window.
struct MapWinNative {
    uint8_t* win = nullptr;            // object in the memory image
    uint32_t vbase = kMapWinVBase;     // virtual base offset of its GraphicWin
    Buffer* canvas = nullptr;          // GraphicWin canvas (+vbase+0x444)
    std::vector<uint8_t> tile_cache;   // +4: 12 bytes per tile, see pfncMapGetCornerYOffset
};
// Registers the native parts of a window; the GraphicWin size fields are set from the canvas.
MapWinNative* ui_mapwin_attach(uint8_t* win, uint32_t vbase, Buffer* canvas);
MapWinNative* ui_mapwin_native(uint8_t* win);
// MapWin_clear_terrain for the native cache (all entries unknown, 0xFF).
void ui_mapwin_clear_cache(uint8_t* win);
// Map canvas dumps (the oracle's `mapdump`, harness/README.md): "SMXMAP1\0", int32 width,
// height, viewer, zoom, centre x, centre y, turn, 0; 256 x RGB palette; width * height pixels.
struct MapDumpHeader {
    int32_t width = 0, height = 0, viewer = 0, zoom = 0, cx = 0, cy = 0, turn = 0, reserved = 0;
};
// Reads the header of a dump (false: not a dump).
bool ui_mapdump_read_header(const char* path, MapDumpHeader* out);
// Writes the window's canvas with the game palette.
bool ui_mapdump_write(uint8_t* win, const char* path, int32_t viewer);
// smacx --dump: when `like` names a dump, takes its view (canvas size, viewer, zoom, centre,
// tile address), redraws the window fully and writes its canvas to `path` (the viewer is
// restored afterwards). Without `like`, dumps the current view.
// scroll_from (x >= 0): the dumped redraw is a scrolled one, from a full redraw centred there
// (the oracle's mapdump scrollfrom=X,Y).
bool ui_mapdump_like(uint8_t* win, const char* path, const char* like, int32_t scroll_from_x = -1,
                     int32_t scroll_from_y = -1);

// ---- original functions ----
// Resets a window (desktop_init -> mapwin_system_init): options cleared, zoom 0 (-6 below 1000
// pixels), centred on the map centre, or for the main view (index 0) on the cursor.
ADDR_NATIVE(0x462870) void THISCALL MapWin_clear(uint8_t* win, int32_t index);
ADDR_NATIVE(0x462980) void THISCALL pfncZoomProcessing(uint8_t* win);
// Tile under the point (px, py) of the window: 0 when it is on the map, 1 otherwise. The row is
// found by walking down the zigzag of tile edges (with their corner heights) under the point.
ADDR_NATIVE(0x463040) int32_t THISCALL pfncPtToTile(uint8_t* win, int32_t px, int32_t py, int32_t* x, int32_t* y);
ADDR_NATIVE(0x462F00) void THISCALL pfncTileToPoint(uint8_t* win, int32_t x, int32_t y, int32_t* px, int32_t* py);
// Height of a tile corner above the flat map (corner 0: centre, 1..4: W, N, E, S), cached per
// tile; land only (ocean tiles are flat).
ADDR_NATIVE(0x46FE70) int32_t THISCALL pfncMapGetCornerYOffset(uint8_t* win, int32_t x, int32_t y, int32_t corner);
ADDR_NATIVE(0x470420) void GAMEAPI MapWin_compute_lighting_table();
ADDR_NATIVE(0x470550) int32_t THISCALL MapWin_get_brighting(uint8_t* win, int32_t x, int32_t y, int32_t corner);
ADDR_NATIVE(0x470790) int32_t THISCALL MapWin_get_point_light(uint8_t* win, int32_t x, int32_t y, int32_t corner);
// Depth value of an ocean tile corner (contour based; 0x39..0x43 at coasts).
ADDR_NATIVE(0x462190) int32_t GAMEAPI alt_get_ocean_detail(int32_t x, int32_t y, int32_t quadrant, int32_t vertex);
// Draws the terrain of tile (x, y) whose top-left is at (px, py).
ADDR_NATIVE(0x4632D0) int32_t GAMEAPI MapWin_gen_terrain_poly(Buffer* dest, uint8_t* win, int32_t zoom,
                                                             int32_t faction_id, int32_t a5, int32_t options,
                                                             int32_t x, int32_t y, int32_t px, int32_t py);
ADDR_NATIVE(0x466CA0) void THISCALL MapWin_draw_vehicles(uint8_t* win, int32_t x, int32_t y, int32_t px, int32_t py,
                                                        int32_t faction_id);
ADDR_NATIVE(0x467190) void THISCALL MapWin_draw_bases(uint8_t* win, int32_t x, int32_t y, int32_t px, int32_t py,
                                                     int32_t faction_id);
ADDR_NATIVE(0x467DE0) void THISCALL MapWin_draw_base_names(uint8_t* win);
// Draws the whole view: terrain, units and bases, base names (terrain_only: skip the terrain fill).
ADDR_NATIVE(0x469CA0) void THISCALL pfncDrawMap(uint8_t* win, int32_t faction_id, int32_t terrain_only);
// Base sprite with its population box and name plate at (px, py).
ADDR_NATIVE(0x55AF20) void GAMEAPI base_draw(Buffer* dest, int32_t base_id, int32_t px, int32_t py, int32_t zoom,
                                            int32_t flags);
// veh_draw and the unit art are in ui/vehdraw.h.
// The map label font cache (FUN_005592D0): Arial of the zoom unit * num / den pixels (10 -> 11),
// three fonts kept, least recently used replaced.
Font* ui_label_font(int32_t zoom, int32_t num, int32_t den, int32_t style);

// Full redraw of the view (scrolled redraws are not reconstructed yet: always full).
void THISCALL pfncRedrawMap(uint8_t* win, int32_t flag);  // 0x46A550
// Centres the view on (x, y) and redraws it.
void THISCALL pfncMoveMap(uint8_t* win, int32_t x, int32_t y, int32_t redraw);  // 0x46B1F0
