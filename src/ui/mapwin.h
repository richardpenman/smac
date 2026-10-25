// Map window (MapWin, ui module 0x4626E0..0x4712D0): the isometric map view.
//
// The window objects live in the engine memory image (the main view is MainConsole at 0x9156B0,
// a Console derived from MapWin; ui_bridge.h MapWins[]), so their integer fields are read and
// written at the original byte offsets (mapwin_field). What the original keeps behind pointers
// (the GraphicWin canvas, the per-tile terrain cache at +4) is native: MapWinNative.
//
// Functions declared in engine/ui_bridge.h (pfncRedrawMap, draw_map, pfncMoveMap, ...) are bound
// to the headless implementations in the engine link; their native versions are in namespace
// native_ui (same names) until the bridge selects the SDL UI (see docs/UI_PLAN.md).
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

// ---- original functions ----
// Resets a window (desktop_init -> mapwin_system_init): options cleared, zoom 0 (-6 below 1000
// pixels), centred on the map centre, or for the main view (index 0) on the cursor.
ADDR_NATIVE(0x462870) void THISCALL MapWin_clear(uint8_t* win, int32_t index);
ADDR_NATIVE(0x462980) void THISCALL pfncZoomProcessing(uint8_t* win);
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
// Unit at (px, py). Approximation: flag and a stand-in figure; the original renders the unit's
// voxel model (.cvr) or its Units.pcx sprite.
ADDR_NATIVE(0x5594F0) void GAMEAPI veh_draw(Buffer* dest, int32_t veh_id, int32_t px, int32_t py, int32_t zoom,
                                           int32_t mode, int32_t a7);

namespace native_ui {
// Full redraw of the view (scrolled redraws are not reconstructed yet: always full).
void THISCALL pfncRedrawMap(uint8_t* win, int32_t flag);  // 0x46A550
// Centres the view on (x, y) and redraws it.
void THISCALL pfncMoveMap(uint8_t* win, int32_t x, int32_t y, int32_t redraw);  // 0x46B1F0
}  // namespace native_ui
