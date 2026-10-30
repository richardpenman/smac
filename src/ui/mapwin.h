// Map window (MapWin, ui module 0x4626E0..0x4712D0): the isometric map view.
//
// The window objects live in the engine memory image (the main view is MainConsole at 0x9156B0,
// a Console derived from MapWin; ui_bridge.h MapWins[]), so their integer fields are read and
// written at the original byte offsets, through the typed view MapWin (engine/ui_bridge.h,
// map_win(win).zoom) and GraphicWin (ui/gfx.h) at the virtual base. What the original keeps behind pointers
// (the GraphicWin canvas, the per-tile terrain cache at +4) is native: MapWinNative.
//
// Functions declared in engine/ui_bridge.h (pfncRedrawMap, pfncMoveMap) are defined here under
// their own names: OpenSMAC replaces the headless "mapview" area with them (src/headless
// ui_headless_mapview.cpp; the rest of that area is in src/ui/win_mapview.cpp).
#pragma once
#include <cstdint>
#include <vector>

#include "engine/memmap.h"
#include "engine/ui_bridge.h"
#include "ui/gfx.h"

// Byte offset of the GraphicWin virtual base: MapWin 0x21A6C, Console (the main view) 0x23D94.
constexpr uint32_t kMapWinVBase = 0x21A6C;
constexpr uint32_t kConsoleVBase = 0x23D94;

// Fixed zoom factor (piZoomNum / piZoomDenom; used when ZoomNum > 0, else the window's zoom step).
GLOBAL(int32_t, ZoomNum, 0x691E6C);
GLOBAL(int32_t, ZoomDenom, 0x691E70);

// The label fonts of a map window (MapWin fonts, ui/gfx.h Font).
inline Font* mapwin_font(uint8_t* win, int32_t i) { return reinterpret_cast<Font*>(map_win(win).fonts[i]); }

// ---- original globals of the map drawing ----
GLOBAL(UiRect, VisibleTiles, 0x7D3C28);  // prVisibleTiles: main view tile range
GLOBAL(int32_t[4][2], TileCorners, 0x7D3C08);  // W, N, E, S corner points of the last tile drawn
GLOBAL(int32_t[4][2], CursorTileCorners, 0x7D3BE8);  // TileCorners of the cursor tile
GLOBAL(int32_t, TerrainDefaultLight, 0x686354);  // light of tiles not currently seen
// Sub-polygon tables of the tile (4 triangles: centre + two diamond corners).
GLOBAL(int32_t[12], TilePolyX, 0x685F20);  // in 2q units
GLOBAL(int32_t[12], TilePolyY, 0x685F50);  // in q units
GLOBAL(int32_t[12], TilePolyCorner, 0x685F80);  // corner index of each vertex (0 centre, 1..4 W N E S)
GLOBAL(int32_t[12][2], TilePolyUV, 0x686090);  // texture coordinates of each vertex
// The 4 tiles around each tile corner (corner c: entries (c-1)*4 .. +3), and 4-vertex quads.
GLOBAL(int32_t[16], CornerTileX, 0x685EA0);
GLOBAL(int32_t[16], CornerTileY, 0x685EE0);
GLOBAL(uint8_t[256][2], RainTransition, 0x685484);  // [neighbour mask]: texture variant, rotation
GLOBAL(int32_t[16][2], OceanQuadUV, 0x685CA8);
GLOBAL(int32_t[16][2], CoastQuadUV, 0x6860F0);
GLOBAL(int32_t[80], OceanDepthLight, 0x6861F0);  // ocean depth (detail) -> light
GLOBAL(int32_t[4][4][2], WaterQuadPoints, 0x7D3AF0);  // the 4 water quads of the last tile drawn
GLOBAL(int32_t[4][4], WaterQuadLight, 0x7F6644);  // their depth lights
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

// Native parts of a map window: what the original keeps behind pointers (its canvas, the tile
// cache). One per window object, made by ui_mapwin_attach (the part of MapWin_ctor 0x4626E0 and
// GraphicWin_init that native builds run) and found by ui_mapwin_native.
class MapWinNative {
public:
    MapWinNative() = default;
    // Binds the window object `win` (GraphicWin at +vbase) to `canvas`: the GraphicWin's size and
    // client rectangle from the canvas, MapWin_ctor's caches and label fonts, the tile cache cleared.
    void attach(uint8_t* win, uint32_t vbase, Buffer* canvas);
    // MapWin_clear_terrain for the native cache (all entries unknown, 0xFF).
    void clear_cache();
    // The tile cache entry of (x, y) (12 bytes; nullptr without cache).
    uint8_t* cache_entry(int32_t x, int32_t y);
    // The object's fields: its MapWin and its GraphicWin (at the virtual base).
    MapWin& fields() const { return map_win(win); }
    GraphicWin& graphic() const { return graphic_win(win + vbase); }

    uint8_t* win = nullptr;  // the window object (memory image, or a native buffer of its layout)
    uint32_t vbase = kMapWinVBase;  // virtual base offset of its GraphicWin
    Buffer* canvas = nullptr;  // GraphicWin canvas (+vbase+0x444)
    std::vector<uint8_t> tile_cache;  // +4: 12 bytes per tile, see pfncMapGetCornerYOffset
};
// Registers the native parts of a window (MapWinNative::attach) / finds them (nullptr: none).
MapWinNative* ui_mapwin_attach(uint8_t* win, uint32_t vbase, Buffer* canvas);
MapWinNative* ui_mapwin_native(uint8_t* win);
// MapWinNative::clear_cache of the window, if it has native parts.
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
// opensmac --dump: when `like` names a dump, takes its view (canvas size, viewer, zoom, centre,
// tile address), redraws the window fully and writes its canvas to `path` (the viewer is
// restored afterwards). Without `like`, dumps the current view.
// scroll_from (x >= 0): the dumped redraw is a scrolled one, from a full redraw centred there
// (the oracle's mapdump scrollfrom=X,Y).
// options: bits OR'd into the window's options for the redraw (the oracle's options=); radius_r
// >= 0: the full redraw is followed by MapWin_draw_radius(radius_x, radius_y, radius_r, 0) on the
// canvas cleared to 0 (radius=).
bool ui_mapdump_like(uint8_t* win, const char* path, const char* like, int32_t scroll_from_x = -1,
                     int32_t scroll_from_y = -1, uint32_t options = 0, int32_t radius_x = 0, int32_t radius_y = 0,
                     int32_t radius_r = -1);

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
ADDR_NATIVE(0x4632D0)
int32_t GAMEAPI MapWin_gen_terrain_poly(Buffer* dest, uint8_t* win, int32_t zoom, int32_t faction_id, int32_t a5,
                                        int32_t options, int32_t x, int32_t y, int32_t px, int32_t py);
ADDR_NATIVE(0x466CA0)
void THISCALL MapWin_draw_vehicles(uint8_t* win, int32_t x, int32_t y, int32_t px, int32_t py, int32_t faction_id);
ADDR_NATIVE(0x467190)
void THISCALL MapWin_draw_bases(uint8_t* win, int32_t x, int32_t y, int32_t px, int32_t py, int32_t faction_id);
ADDR_NATIVE(0x467DE0) void THISCALL MapWin_draw_base_names(uint8_t* win);
// Draws the whole view: terrain, units and bases, base names (terrain_only: skip the terrain fill).
ADDR_NATIVE(0x469CA0) void THISCALL pfncDrawMap(uint8_t* win, int32_t faction_id, int32_t terrain_only);
// Base sprite with its population box and name plate at (px, py).
ADDR_NATIVE(0x55AF20)
void GAMEAPI base_draw(Buffer* dest, int32_t base_id, int32_t px, int32_t py, int32_t zoom, int32_t flags);
// veh_draw and the unit art are in ui/vehdraw.h.
// The map label font cache (label_font (0x5592D0)): Arial of the zoom unit * num / den pixels (10 -> 11),
// three fonts kept, least recently used replaced.
Font* ui_label_font(int32_t zoom, int32_t num, int32_t den, int32_t style);

// Redraws the view: in full, or (MapWin +0x21A44 set) moved by the scroll with only the uncovered
// rows and columns drawn.
void THISCALL pfncRedrawMap(uint8_t* win, int32_t flag);  // 0x46A550

// ---- partial redraws and overlays (mapwin_radius.cpp) ----
// The screen rectangle of the tiles within `radius` of (x, y) (radius 0: the tile widened by a
// quarter on each side), clipped to the client area; 0 when empty.
ADDR_NATIVE(0x468CD0)
int32_t THISCALL MapWin_compute_clip(uint8_t* win, int32_t x, int32_t y, int32_t radius, UiRect* out);
// Redraws the tiles around (x, y) inside MapWin_compute_clip's rectangle: the terrain (unless
// no_terrain), the units and bases, the base names, the overlays and legend when shown.
ADDR_NATIVE(0x469190)
void THISCALL
    MapWin_gen_radius(uint8_t* win, int32_t x, int32_t y, int32_t radius, int32_t faction_id, int32_t no_terrain);
ADDR_NATIVE(0x468F90)
void THISCALL MapWin_draw_vehicles_2(uint8_t* win, int32_t x, int32_t y, int32_t radius, int32_t faction_id);
// MapWin_gen_radius for the viewer (mode < 0: without the terrain); mode != 0 shows the change.
ADDR_NATIVE(0x46A2A0)
void THISCALL MapWin_draw_radius(uint8_t* win, int32_t x, int32_t y, int32_t radius, int32_t mode);
// MapWin_draw_radius (radius 0) when (x, y) is in the window's tile range.
ADDR_NATIVE(0x46A4A0) void THISCALL MapWin_draw_tile(uint8_t* win, int32_t x, int32_t y, int32_t mode);
ADDR_NATIVE(0x468C20)
void THISCALL MapWin_gen_tile(uint8_t* win, int32_t x, int32_t y, int32_t faction_id, int32_t units);
// Options 0x2000: the AI goals at (x, y) (the faction's initial when omniscient, the goal's letter
// and priority).
ADDR_NATIVE(0x4688E0) void THISCALL MapWin_gen_overlays(uint8_t* win, int32_t x, int32_t y);
// Options 0x20000: the keypad digits on the 8 tiles around the cursor.
ADDR_NATIVE(0x469720) void THISCALL MapWin_draw_legend(uint8_t* win);
// Options 0x100000: the factions' names and the table of their diplomatic states.
ADDR_NATIVE(0x4698D0) void THISCALL MapWin_draw_wars(uint8_t* win);
// MapWin_tile_in_view (0x4216F0; basewin.cpp): (x, y) is in the view at least `margin` tiles from
// its edges.
bool mapwin_tile_in_view(uint8_t* win, int32_t x, int32_t y, int32_t margin);
// Centres the view on (x, y) and redraws it.
void THISCALL pfncMoveMap(uint8_t* win, int32_t x, int32_t y, int32_t redraw);  // 0x46B1F0

// ---- destination lines (mapwin_dest.cpp) ----
GLOBAL(int32_t, DestLineCost, 0x7D3B70);  // path cost summed by MapWin_dest_line (move units)
// MapWin_dest_line: the path a unit (unit_id; mode: triad, 2 straight) of faction_id would take
// from (x1, y1) to (x2, y2): drawn tile by tile (another region / unknown tiles: straight), its
// cost in DestLineCost. solid: on the screen overlay (the go-to drag), else dashed (on, off) on
// the view.
ADDR_NATIVE(0x467430)
void THISCALL
    MapWin_dest_line(uint8_t* win, int32_t x1, int32_t y1, int32_t x2, int32_t y2, int32_t unit_id, int32_t faction_id,
                     int32_t mode, int32_t patrol, int32_t on, int32_t off, int32_t solid);
ADDR_NATIVE(0x4672C0)
void THISCALL MapWin_direct_dest_line(uint8_t* win, int32_t x1, int32_t y1, int32_t x2, int32_t y2, int32_t triad,
                                      int32_t on, int32_t off, int32_t solid);
// MapWin_draw_base_dest: the destination lines of the map display options (0x40000 rally points,
// 0x400000 patrols, 0x800000 go-to orders); redraw: update the view when one was drawn.
ADDR_NATIVE(0x467970) void THISCALL MapWin_draw_base_dest(uint8_t* win, int32_t redraw);
// Buffer_line (0x5E09D0): a dashed line (runs of `on` pixels of c1, `off` of c2; -1 transparent).
void buffer_dashed_line(Buffer* b, int32_t x1, int32_t y1, int32_t x2, int32_t y2, int32_t c1, int32_t c2, int32_t on,
                        int32_t off, int32_t phase);
