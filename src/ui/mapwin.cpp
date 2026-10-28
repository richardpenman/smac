// Map window geometry and terrain rendering (see mapwin.h).
#include "ui/mapwin.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>

#include "engine/base.h"
#include "engine/globals.h"
#include "engine/map.h"
#include "engine/ui_bridge.h"
#include "engine/util.h"
#include "engine/veh.h"
#include "ui/sprites.h"
#include "ui/texture.h"

// Landmark-shaped light offsets and the lighting table built by MapWin_compute_lighting_table.
constexpr uint32_t kLightTable = 0x7F6688;  // 4 x 49 signed bytes
constexpr uint32_t kLandmarkTileIndex = 0x685684;

namespace {

std::map<uint8_t*, MapWinNative> g_windows;

int32_t trunc_div16(int32_t v) { return (v + ((v >> 31) & 0xF)) >> 4; }
int32_t trunc_div4(int32_t v) { return (v + ((v >> 31) & 3)) >> 2; }

// Zoom-scaled value: (zoom + 16) * base / 16, or the fixed zoom factor.
int32_t zoom_scale(int32_t zoom, int32_t base) {
    if (ZoomNum < 1) return trunc_div16((zoom + 0x10) * base);
    return ZoomNum * base / ZoomDenom;
}

int32_t wrap_x(int32_t x) {
    if ((MapToggleFlat & 1) == 0) {
        if (x < 0) x += MapAreaX;
        else if (x >= MapAreaX) x -= MapAreaX;
    }
    return x;
}

bool in_map(int32_t x, int32_t y) { return y >= 0 && y < MapAreaY && x >= 0 && x < MapAreaX; }

MAP* tile(int32_t x, int32_t y) { return &MapTiles[MapHalfX * y + (x >> 1)]; }

}  // namespace

uint8_t* ui_mapwin_cache_entry(uint8_t* win, int32_t x, int32_t y) {
    MapWinNative* n = ui_mapwin_native(win);
    if (!n || n->tile_cache.empty()) return nullptr;
    size_t i = ((size_t)(MapAreaX >> 1) * y + (x >> 1)) * 0xC;
    if (i + 0xC > n->tile_cache.size()) return nullptr;
    return &n->tile_cache[i];
}

namespace {

uint8_t* cache_entry(uint8_t* win, int32_t x, int32_t y) { return ui_mapwin_cache_entry(win, x, y); }

int32_t gw_width(uint8_t* win) { return graphic_win(win + ui_mapwin_native(win)->vbase).width; }
int32_t gw_height(uint8_t* win) { return graphic_win(win + ui_mapwin_native(win)->vbase).height; }

// Landmark detail value of corner `corner` from one of the landmark overlay tables. Removed
// landmarks have a negative code: the original then reads before the index table.
int32_t landmark_detail(uint32_t table, uint32_t lm, int32_t corner, bool is_signed) {
    int32_t code = (int32_t)lm >> 24;
    uint8_t cell = *mem::host((uint32_t)(kLandmarkTileIndex + corner + code * 5));
    uint8_t v = *mem::host(table + cell);
    return is_signed ? (int32_t)(int8_t)v : (int32_t)v;
}

}  // namespace

MapWinNative* ui_mapwin_attach(uint8_t* win, uint32_t vbase, Buffer* canvas) {
    MapWinNative& n = g_windows[win];
    n.win = win;
    n.vbase = vbase;
    n.canvas = canvas;
    graphic_win(win + vbase).width = canvas->width;
    graphic_win(win + vbase).height = -canvas->height;
    graphic_win(win + vbase).client = {0, 0, canvas->width, canvas->height};
    // What MapWin_ctor (0x4626E0) initialises (native builds never run the original's
    // constructors): the zoom/height caches and the label fonts.
    map_win(win).last_zoom = -9999;
    map_win(win).last_elev = -1;
    Font_ctor(mapwin_font(win, 0));
    Font_ctor(mapwin_font(win, 1));
    Font_ctor(mapwin_font(win, 2));
    ui_mapwin_clear_cache(win);
    return &n;
}

MapWinNative* ui_mapwin_native(uint8_t* win) {
    auto it = g_windows.find(win);
    return it == g_windows.end() ? nullptr : &it->second;
}

void ui_mapwin_clear_cache(uint8_t* win) {
    MapWinNative* n = ui_mapwin_native(win);
    if (!n) return;
    n->tile_cache.assign((size_t)std::max(MapAreaTiles, 0) * 0xC, 0);
    for (size_t i = 0; i < n->tile_cache.size(); i += 0xC) std::memset(&n->tile_cache[i], 0xFF, 9);
}

bool ui_mapdump_read_header(const char* path, MapDumpHeader* out) {
    FILE* f = std::fopen(path, "rb");
    if (!f) return false;
    char magic[8];
    bool ok = std::fread(magic, 1, 8, f) == 8 && !std::memcmp(magic, "SMXMAP1", 8) &&
              std::fread(out, sizeof(*out), 1, f) == 1;
    std::fclose(f);
    return ok;
}

bool ui_mapdump_write(uint8_t* win, const char* path, int32_t viewer) {
    MapWinNative* n = ui_mapwin_native(win);
    if (!n || !n->canvas || !n->canvas->data) return false;
    Buffer* c = n->canvas;
    MapDumpHeader h;
    h.width = c->width;
    h.height = c->height;
    h.viewer = viewer;
    h.zoom = map_win(win).zoom;
    h.cx = map_win(win).center_x;
    h.cy = map_win(win).center_y;
    h.turn = CurrentTurn;
    FILE* f = std::fopen(path, "wb");
    if (!f) return false;
    std::fwrite("SMXMAP1", 1, 8, f);
    std::fwrite(&h, sizeof(h), 1, f);
    std::fwrite(GamePalette.rgb, 3, 256, f);
    for (int32_t y = 0; y < c->height; y++) std::fwrite(c->data + (size_t)y * c->stride, 1, (size_t)c->width, f);
    return std::fclose(f) == 0;
}

bool ui_mapdump_like(uint8_t* win, const char* path, const char* like, int32_t scroll_from_x, int32_t scroll_from_y,
                     uint32_t options, int32_t radius_x, int32_t radius_y, int32_t radius_r) {
    MapWinNative* n = ui_mapwin_native(win);
    if (!n || !n->canvas) return false;
    int32_t viewer = CurrentPlayerFaction;
    MapDumpHeader ref;
    if (like && *like && ui_mapdump_read_header(like, &ref)) {
        if (ref.width != n->canvas->width || ref.height != n->canvas->height) {
            Buffer_init(n->canvas, ref.width, ref.height, 8, 0);
            graphic_win(win + n->vbase).width = ref.width;
            graphic_win(win + n->vbase).height = -ref.height;
            graphic_win(win + n->vbase).client = {0, 0, ref.width, ref.height};
        }
        TileVariantBase = (uint32_t)ref.reserved;
        // The oracle dumps after control_turn's redraw of the current view: the terrain cache
        // (whose unfilled corner heights the outline of unexplored tiles reads) holds what that
        // redraw computed. Same sequence here.
        ui_mapwin_clear_cache(win);
        pfncRedrawMap(win, 1);
        CurrentPlayerFaction = ref.viewer;
        map_win(win).zoom = ref.zoom;
        map_win(win).center_x = ref.cx;
        map_win(win).center_y = ref.cy;
    }
    if (scroll_from_x >= 0) {
        int32_t tx = map_win(win).center_x, ty = map_win(win).center_y;
        map_win(win).center_x = scroll_from_x;
        map_win(win).center_y = scroll_from_y;
        pfncRedrawMap(win, 0);
        map_win(win).scrolling = 1;
        pfncMoveMap(win, tx, ty, 0);
        map_win(win).scrolling = 0;
    } else {
        uint32_t old_options = (uint32_t)map_win(win).options;
        map_win(win).options = (int32_t)(old_options | options);
        pfncRedrawMap(win, 1);
        if (radius_r >= 0) {  // on the canvas cleared to 0, as the oracle
            Buffer_fill_2(n->canvas, 0);
            // MapWin_draw_radius(.., 0) (without its check for a pending full redraw)
            MapWin_gen_radius(win, radius_x, radius_y, radius_r,
                              (GameState & STATE_OMNISCIENT_VIEW) ? -1 : CurrentPlayerFaction, 0);
        }
        map_win(win).options = (int32_t)old_options;
    }
    bool ok = ui_mapdump_write(win, path, CurrentPlayerFaction);
    // <path>.pt: pfncPtToTile for every 4th pixel (int32 result, x, y), as the oracle writes it.
    if (FILE* pf = std::fopen((std::string(path) + ".pt").c_str(), "wb")) {
        for (int32_t py = 0; py < n->canvas->height; py += 4)
            for (int32_t px = 0; px < n->canvas->width; px += 4) {
                int32_t r[3] = {0, -1, -1};
                r[0] = pfncPtToTile(win, px, py, &r[1], &r[2]);
                std::fwrite(r, 4, 3, pf);
            }
        std::fclose(pf);
    }
    if (FILE* cf = std::fopen((std::string(path) + ".co").c_str(), "wb")) {  // corner offsets, as the oracle
        for (int32_t ty = 0; ty < MapAreaY; ty++)
            for (int32_t tx = ty & 1; tx < MapAreaX; tx += 2)
                for (int32_t c = 0; c < 5; c++) {
                    int32_t v = pfncMapGetCornerYOffset(win, tx, ty, c);
                    std::fwrite(&v, 4, 1, cf);
                }
        std::fclose(cf);
    }
    CurrentPlayerFaction = viewer;
    return ok;
}

void THISCALL MapWin_clear(uint8_t* win, int32_t index) {
    map_win(win).index = index;
    map_win(win).options = 0;
    map_win(win).zoom = CanvasWidth < 1000 ? -6 : 0;
    map_win(win).active = index == 0;
    int32_t cy = MapAreaY / 2, cx = MapAreaX / 2;
    map_win(win).center_y = cy;
    map_win(win).center_x = ((cx ^ cy) & 1) ^ cx;
    if (index == 0) {
        int32_t x = MapWinCursorX[MapWinCursorIndex], y = MapWinCursorY[MapWinCursorIndex];
        if (y >= 0 && y < MapAreaY && x >= 0 && x < MapAreaX) {
            map_win(win).center_x = x;
            map_win(win).center_y = y;
        }
    }
    map_win(win).cursor_id = 0x7F00;
    map_win(win).cursor = 0;
    ui_mapwin_clear_cache(win);  // the original frees the cache; it is rebuilt on demand
}

// ---- geometry -----------------------------------------------------------------------------------

void THISCALL pfncZoomProcessing(uint8_t* win) {
    map_win(win).pixel_x = 0;
    map_win(win).pixel_y = 0;
    int32_t q = trunc_div4(zoom_scale(map_win(win).zoom, 0x32) + 1);
    map_win(win).q = q;
    if (q < 1) map_win(win).q = 1;
    q = map_win(win).q;
    int32_t tile_h = q * 4, tile_w = q * 8;
    map_win(win).quarter_w = q * 2;
    map_win(win).half_w = q * 4;
    map_win(win).half_h = q * 2;
    map_win(win).tile_w = tile_w;
    map_win(win).tile_h = tile_h;
    int32_t width = gw_width(win), height = gw_height(win);  // height is negative
    map_win(win).cols_odd = (width - 1 + tile_w) / tile_w;
    map_win(win).rows_above = (q * 6 - height - 1) / tile_h;
    map_win(win).cols_even = (width + q * 4 - 1 + tile_w) / tile_w;
    map_win(win).rows_below = (q * 6 - height - 1) / tile_h;
    if (win == &MainConsole) {
        VisibleTiles.left = map_win(win).center_x - map_win(win).cols_odd + 4;
        VisibleTiles.right = map_win(win).cols_odd + map_win(win).center_x - 4;
        VisibleTiles.top = map_win(win).center_y - map_win(win).rows_above + 4;
        VisibleTiles.bottom = map_win(win).center_y + map_win(win).rows_above - 6;
    }
    int32_t half = MapAreaX / 2;
    if (half < map_win(win).cols_odd) {
        map_win(win).cols_odd = half;
        map_win(win).cols_even = half + 1;
        map_win(win).pixel_x = (width - map_win(win).cols_odd * map_win(win).tile_w) / 2;
    }
    int32_t half_y = MapAreaY / 2;
    if (half_y < map_win(win).rows_above) {
        map_win(win).rows_above = half_y;
        map_win(win).rows_below = half_y + 1;
        map_win(win).pixel_y = -((height + map_win(win).rows_above * map_win(win).tile_h) / 2);
    }
    map_win(win).cols_odd = std::min(map_win(win).cols_odd, MapAreaX / 2);
    map_win(win).cols_even = std::min(map_win(win).cols_even, MapAreaX / 2);
    int32_t oy = map_win(win).center_y - map_win(win).rows_above;
    int32_t max_y = MapAreaY + (2 - map_win(win).rows_above) * 2;
    if (oy < -2 || max_y < -2) oy = -2;
    else if (max_y < oy) oy = max_y;
    map_win(win).origin_y = oy;
    int32_t ox = map_win(win).center_x - map_win(win).cols_odd;
    if ((MapToggleFlat & 1) == 0) ox = wrap_x(ox);
    map_win(win).origin_x = ox;
    if (MapToggleFlat & 1) {
        int32_t max_x = MapAreaX + (2 - map_win(win).cols_odd) * 2;
        if (ox < -4 || max_x < -4) ox = -4;
        else if (max_x < ox) ox = max_x;
        map_win(win).origin_x = ox;
    }
    // The left column has the parity of the top row.
    map_win(win).origin_x = ((map_win(win).origin_x ^ map_win(win).origin_y) & 1) ^ map_win(win).origin_x;
    int32_t zoom = map_win(win).zoom;
    bool lifted = (map_win(win).options & 2) && zoom >= -10;
    int32_t lift = lifted ? (ZoomNum < 1 ? trunc_div16((zoom * 3 + 0x30) * 4) : ZoomNum * 0xC / ZoomDenom) : 0;
    int32_t lift2 = lifted ? (ZoomNum < 1 ? trunc_div16(zoom * 4 + 0x40) : ZoomNum * 4 / ZoomDenom) : 0;
    map_win(win).unit_lift = lift;
    map_win(win).unit_lift2 = lift2;
    map_win(win).elev_scale = 0x19;
    if (GameMorePreferences & MPREF_MAP_SHOW_FLAT_TERRAIN) {  // flat terrain
        map_win(win).elev_scale = 8;
        map_win(win).unit_lift = lift / 3;
        map_win(win).unit_lift2 = lift2 / 3;
    }
    MapWinNative* n = ui_mapwin_native(win);
    bool cache = n && !n->tile_cache.empty();
    if (map_win(win).elev_scale != map_win(win).last_elev) {
        map_win(win).last_elev = map_win(win).elev_scale;
        if (cache)
            for (size_t i = 0; i < n->tile_cache.size(); i += 0xC) std::memset(&n->tile_cache[i + 4], 0xFF, 5);
    }
    if (zoom != map_win(win).last_zoom) {
        if (cache)
            for (size_t i = 0; i < n->tile_cache.size(); i += 0xC) std::memset(&n->tile_cache[i], 0xFF, 9);
        map_win(win).last_zoom = zoom;
        Font* f0 = mapwin_font(win, 0);
        Font* f1 = mapwin_font(win, 1);
        Font* f2 = mapwin_font(win, 2);
        int32_t s = std::min(trunc_div16(map_win(win).tile_h * 5), 0xC);
        if (f0->size != s) {
            f0->size = s;
            Font_init(f0, "Arial", s, 0);
        }
        s = std::min(trunc_div16(map_win(win).tile_h * 5), 0xC);
        if (f2->size != s) {
            f2->size = s;
            Font_init(f2, "Arial", s, 2);
        }
        s = std::min((map_win(win).tile_h << 1) / 5, 0xC);
        if (f1->size != s) {
            f1->size = s;
            Font_init(f1, "Arial", s, 1);
        }
    }
}

int32_t THISCALL pfncPtToTile(uint8_t* win, int32_t px, int32_t py, int32_t* out_x, int32_t* out_y) {
    MapWinNative* n = ui_mapwin_native(win);
    if (!n || px >= graphic_win(win + n->vbase).width || py >= -graphic_win(win + n->vbase).height) return 1;
    px -= map_win(win).pixel_x;
    py -= map_win(win).pixel_y;
    if (px < 0 || py < 0) return 1;
    const bool flat = (MapToggleFlat & 1) != 0;
    auto wrap = [&](int32_t v) { return flat ? v : wrap_x(v); };
    int32_t ox = map_win(win).origin_x, tw = map_win(win).tile_w, hw = map_win(win).half_w;
    int32_t a = wrap(ox + (px / tw) * 2 + 1);
    int32_t y = map_win(win).origin_y + (py / map_win(win).tile_h) * 2 + 1;
    int32_t b = wrap(ox + ((px - hw) / tw) * 2 + 1);
    int32_t cur;
    bool left_edge;
    if (b == a) {
        cur = a + 1;
        left_edge = true;
    } else {
        cur = a - 1;
        left_edge = false;
    }
    y--;
    // (with the column moved right, the edge offsets are taken from the moved column)
    int32_t base = left_edge ? cur : a;
    int32_t l = wrap(base - ox - 1), r = wrap(base - ox);
    px -= hw * l;
    int32_t span = hw * r - hw * l;
    const int32_t oy = map_win(win).origin_y, hh = map_win(win).half_h;
    int32_t lhs, rhs;
    do {
        int32_t e1, e2;
        if (left_edge) {
            left_edge = false;
            e1 = (y - oy) * hh - pfncMapGetCornerYOffset(win, cur, y, 1);
            e2 = (y - oy - 1) * hh - pfncMapGetCornerYOffset(win, cur, y, 2);
            cur--;
        } else {
            left_edge = true;
            e1 = (y - oy - 1) * hh - pfncMapGetCornerYOffset(win, cur, y, 2);
            e2 = (y - oy) * hh - pfncMapGetCornerYOffset(win, cur, y, 3);
            cur++;
        }
        lhs = (e2 - e1) * px;
        rhs = (py - e1) * span;
        y++;
    } while (lhs <= rhs);
    y -= 2;
    cur = wrap(cur);
    *out_x = cur;
    *out_y = y;
    return (y >= 0 && y < MapAreaY && cur >= 0 && cur < MapAreaX) ? 0 : 1;
}

void THISCALL pfncTileToPoint(uint8_t* win, int32_t x, int32_t y, int32_t* px, int32_t* py) {
    int32_t ox = map_win(win).origin_x, oy = map_win(win).origin_y;
    int32_t cols = map_win(win).cols_even + map_win(win).cols_odd;
    int32_t d;
    bool in_view = false;
    if (y >= oy && y < map_win(win).rows_below + map_win(win).rows_above + oy) {
        int32_t x1 = x;
        if ((MapToggleFlat & 1) == 0) {
            if (x < ox) x1 = MapAreaX + x;
            if (cols + ox <= x1) x1 -= MapAreaX;
        }
        if (x1 >= ox && x1 < cols + ox) in_view = true;
    }
    if (in_view) {
        d = x - ox;
        if ((MapToggleFlat & 1) == 0) {
            if (d >= 0) {
                if (MapAreaX <= d) d -= MapAreaX;
            } else {
                d += MapAreaX;
            }
        }
    } else {
        // Outside the view: the nearer way around the map.
        d = x - ox;
        if (MapAreaX / 2 < d) d -= MapAreaX;
        if (d < -(MapAreaX / 2)) d += MapAreaX;
    }
    *px = map_win(win).half_w * (d - 1) + map_win(win).pixel_x;
    int32_t c4 = pfncMapGetCornerYOffset(win, x, y, 4), c3 = pfncMapGetCornerYOffset(win, x, y, 3);
    int32_t c2 = pfncMapGetCornerYOffset(win, x, y, 2), c1 = pfncMapGetCornerYOffset(win, x, y, 1);
    *py = ((y - oy) - 1) * map_win(win).half_h + map_win(win).pixel_y - ((c4 + c3 + c2 + c1) >> 2);
}

// ---- terrain heights and light ------------------------------------------------------------------

int32_t THISCALL pfncMapGetCornerYOffset(uint8_t* win, int32_t x, int32_t y, int32_t corner) {
    if (!in_map(x, y)) return 0;
    MAP* sq = tile(x, y);
    uint8_t climate = sq->climate;
    if ((climate & 0xE0) < 0x60) return 0;
    uint8_t* e = cache_entry(win, x, y);
    if (!e) return 0;
    int8_t cached = (int8_t)e[4 + corner];
    if (cached != -1) return cached;
    uint32_t lm = sq->landmarks;
    int32_t off = 0;
    if (lm & 2) off = landmark_detail(0x7D3B74, lm, corner, false);  // volcano
    else if (lm & 1) off = landmark_detail(0x7F65D0, lm, corner, true);  // crater
    else if (lm & 0x200) off = landmark_detail(0x7F655C, lm, corner, true);  // canyon
    else if (lm & 0x100) off = landmark_detail(0x7D3A78, lm, corner, true);  // mesa
    else if (lm & 0x800) off = landmark_detail(0x7F64E8, lm, corner, true);  // ridge
    int32_t scale = map_win(win).elev_scale, zoom = map_win(win).zoom;
    if (corner == 0) {
        int32_t v = ((climate >> 5) - 3) * zoom_scale(zoom, scale);
        if (off >= 1) {
            v += zoom_scale(zoom, scale) * off / 100;
            if (0x7E < v) v = 0x7F;
        } else if (off < 0) {
            v += zoom_scale(zoom, scale) * off / 100;
            if (v < -0x7E) v = -0x7F;
        }
        e[4] = (uint8_t)std::min(v, 0x7F);
        return v;
    }
    // A shared corner: the average height of the four tiles around it, written to all four.
    int32_t base = (corner - 1) * 4;
    int32_t sum = 0, alt_sum = 0, n = 0;
    bool edge = false;
    for (int32_t k = 0; k < 4; k++) {
        int32_t nx = wrap_x(CornerTileX[base + k] + x), ny = CornerTileY[base + k] + y;
        if (!in_map(nx, ny) || (tile(nx, ny)->climate & 0xE0) < 0x60) {
            edge = true;
            break;
        }
        sum += pfncMapGetCornerYOffset(win, nx, ny, 0);
        alt_sum += tile(nx, ny)->climate >> 5;
        n++;
    }
    int32_t v;
    if (edge) {
        v = 0;
        alt_sum = 0xC;
    } else {
        v = sum >> 2;
    }
    if (off >= 1) {
        v += zoom_scale(zoom, scale) * off / 100;
        if (0x7E < v) v = 0x7F;
    } else if (off < 0) {
        v += zoom_scale(zoom, scale) * off / 100;
        if (v < -0x7E) v = -0x7F;
    }
    for (int32_t k = 0; k < 4; k++) {
        int32_t nx = wrap_x(CornerTileX[base + k] + x), ny = CornerTileY[base + k] + y;
        if (!in_map(nx, ny)) continue;
        uint8_t* ne = cache_entry(win, nx, ny);
        if (!ne) continue;
        int32_t idx = (k - 2) & 3;  // this corner's index in the neighbour (0..3 for W N E S)
        ne[5 + idx] = (uint8_t)std::min(v, 0x7F);
        uint16_t rel = (uint16_t)(ne[10] | ne[11] << 8);
        int32_t shift = idx * 4;
        int32_t value = edge ? 4 : ((alt_sum - ((tile(nx, ny)->climate >> 3) & 0xFC)) + 4) & 0xF;
        rel = (uint16_t)((rel & ~(0xF << shift)) | (value << shift));
        ne[10] = (uint8_t)rel;
        ne[11] = (uint8_t)(rel >> 8);
    }
    return v;
}

void GAMEAPI MapWin_compute_lighting_table() {
    int8_t* out = reinterpret_cast<int8_t*>(mem::host(kLightTable));
    for (int32_t dir = 0; dir < 4; dir++) {
        for (int32_t c = -3, c_rev = 3; c <= 3; c++, c_rev--) {
            for (int32_t r = -3, r_rev = 3; r <= 3; r++, r_rev--) {
                int32_t a = 0, b = 0;
                switch (dir) {
                case 0:
                    a = c;
                    b = r;
                    break;
                case 1:
                    a = r_rev;
                    b = c;
                    break;
                case 2:
                    a = c_rev;
                    b = r_rev;
                    break;
                case 3:
                    a = r;
                    b = c_rev;
                    break;
                }
                a *= 100;
                b *= 100;
                int32_t root = quick_root(b * b + a * a);
                int32_t v = (b + a) * -3;
                if (root) v /= root;
                *out++ = (int8_t)v;
            }
        }
    }
}

int32_t THISCALL MapWin_get_brighting(uint8_t* win, int32_t x, int32_t y, int32_t corner) {
    if (!in_map(x, y)) return 0;
    uint8_t* e = cache_entry(win, x, y);
    if (!e) return -0x80;
    if (e[corner] != 0xFF) return (int32_t)e[corner] - 0x80;
    if ((tile(x, y)->climate & 0xE0) < 0x60) {
        e[corner] = 0x80;
        return 0;
    }
    pfncMapGetCornerYOffset(win, x, y, 1);
    int32_t a1 = e[10] & 0xF;
    pfncMapGetCornerYOffset(win, x, y, 2);
    int32_t a2 = e[10] >> 4;
    pfncMapGetCornerYOffset(win, x, y, 3);
    int32_t a3 = e[11] & 0xF;
    pfncMapGetCornerYOffset(win, x, y, 4);
    int32_t a4 = e[11] >> 4;
    const int8_t* t = reinterpret_cast<const int8_t*>(mem::host(kLightTable - 8));
    int32_t v = 0xFF;
    switch (corner) {
    case 0: v = t[a2 + a1 * 7]; break;
    case 1: v = t[0x31 + a3 + a2 * 7]; break;
    case 2: v = t[0x62 + a4 + a3 * 7]; break;
    case 3: v = t[0x93 + a1 + a4 * 7]; break;
    }
    e[corner] = (uint8_t)(v - 0x80);
    return v;
}

int32_t THISCALL MapWin_get_point_light(uint8_t* win, int32_t x, int32_t y, int32_t corner) {
    int32_t off = 0;
    uint32_t lm = tile(x, y)->landmarks;
    int32_t code = (int32_t)lm >> 24;
    if (lm & 1) {
        if (code >= 0) off = landmark_detail(0x685A24, lm, corner, true);
    } else if (lm & 2) {
        if (code >= 0) off = landmark_detail(0x685B60, lm, corner, true);
    } else if ((lm & 0x200) && code >= 0) {
        off = landmark_detail(0x7F6750, lm, corner, true);
    }
    int32_t sum = 0;
    if (corner == 0) {
        for (int32_t k = 0; k < 4; k++) sum += MapWin_get_brighting(win, x, y, k);
        return (sum >> 2) + off;
    }
    int32_t base = (corner - 1) * 4;
    for (int32_t k = 0, i = -2; k < 4; k++, i++) {
        int32_t nx = wrap_x(x + CornerTileX[base + k]), ny = y + CornerTileY[base + k];
        sum += MapWin_get_brighting(win, nx, ny, (i - 1) & 3);
        sum += MapWin_get_brighting(win, nx, ny, i & 3);
    }
    return (sum >> 3) + off;
}

int32_t GAMEAPI alt_get_ocean_detail(int32_t x, int32_t y, int32_t quadrant, int32_t vertex) {
    if (!in_map(x, y)) return 0;
    int32_t v;
    auto contour = [](int32_t tx, int32_t ty) { return (int32_t)tile(tx, ty)->contour; };
    if (vertex == 0) {
        v = contour(x, y);
    } else {
        if ((vertex & 1) == 0) {
            // Even vertex: a corner shared by four tiles.
            v = 0;
            for (int32_t k = 0; k < 4; k++) {
                int32_t nx = wrap_x(CornerTileX[quadrant * 4 + k] + x), ny = CornerTileY[quadrant * 4 + k] + y;
                if (in_map(nx, ny)) v += std::min(contour(nx, ny), 0x4F);
            }
            v >>= 2;
        } else {
            // Odd vertex: an edge midpoint shared with one neighbour.
            int32_t dir = (vertex + 3 + quadrant * 2) & 7;
            int32_t nx = wrap_x(NearOffsetX[dir] + x), ny = NearOffsetY[dir] + y;
            v = std::min(contour(x, y), 0x4F);
            if (in_map(nx, ny)) {
                int32_t c = contour(nx, ny);
                if (c > 0x4F) v = (v + 0x4F) >> 1;
                else v = (v + c) >> 1;
            } else {
                v >>= 1;
            }
        }
        if (vertex == 2) {
            int32_t ax = wrap_x(CornerTileX[quadrant * 4 + 1] + x), ay = CornerTileY[quadrant * 4 + 1] + y;
            if (in_map(ax, ay) && (tile(ax, ay)->climate & 0xE0) >= 0x60) v += 7;
            int32_t bx = wrap_x(CornerTileX[quadrant * 4 + 3] + x), by = CornerTileY[quadrant * 4 + 3] + y;
            if (in_map(bx, by) && (tile(bx, by)->climate & 0xE0) >= 0x60) v -= 3;
            goto clamp;
        }
    }
    {
        // Coast shapes of odd vertices next to land (or, for land tiles, next to water).
        bool ocean = (tile(x, y)->climate & 0xE0) < 0x60;
        uint32_t dir = (uint32_t)(quadrant * 2 - 4);
        for (int32_t i = 0; i < 3; i++, dir++) {
            int32_t nx = wrap_x(NearOffsetX[dir & 7] + x), ny = NearOffsetY[dir & 7] + y;
            if (!in_map(nx, ny)) continue;
            bool n_land = (tile(nx, ny)->climate & 0xE0) >= 0x60;
            if (n_land != ocean) continue;
            bool first = (vertex == 1 && i == 0), last = (vertex == 3 && i == 2);
            if (!first && !last) continue;
            bool q03 = quadrant == 0 || quadrant == 3, q01 = quadrant == 0 || quadrant == 1;
            if (quadrant > 3) return 0x3C;
            if (first) return (q03 == ocean) ? 0x39 : 0x43;
            return (q01 == ocean) ? 0x43 : 0x39;
        }
    }
clamp:
    if (v < 0) return 0;
    if (0x4F < v) return 0x4F;
    return v;
}

// ---- map drawing --------------------------------------------------------------------------------

namespace {
// The tile ranges a scrolled redraw draws (MapWin +0x21A48 the columns uncovered horizontally,
// +0x21A58 the rows uncovered vertically): x0, y0, x1, y1.
int32_t* scroll_cols(uint8_t* win) { return &map_win(win).scroll_cols[0]; }
int32_t* scroll_rows(uint8_t* win) { return &map_win(win).scroll_rows[0]; }
// Whether a scrolled redraw draws tile (x, y), with the ranges widened by `margin`.
bool in_scroll_range(uint8_t* win, int32_t x, int32_t y, int32_t margin) {
    for (int32_t* r : {scroll_rows(win), scroll_cols(win)}) {
        int32_t x0 = r[0] - margin, x1 = r[2] + margin;
        if (y < r[1] - margin || r[3] + margin < y) continue;
        if (x1 < x0 && (MapToggleFlat & 1) == 0) {
            if (x0 <= x || x < x1) return true;
        } else if (x0 <= x && x < x1) {
            return true;
        }
    }
    return false;
}
}  // namespace

void THISCALL pfncDrawMap(uint8_t* win, int32_t faction_id, int32_t terrain_only) {
    MapWinNative* n = ui_mapwin_native(win);
    Buffer* canvas = n->canvas;
    int32_t rows = map_win(win).rows_above + 1 + map_win(win).rows_below;
    const bool scrolling = map_win(win).scrolling != 0;
    // Every tile of the view, row by row, odd rows shifted by one column.
    auto for_each_tile = [&](int32_t margin, auto&& fn) {
        for (int32_t r = 0; r < rows; r++) {
            int32_t ty = map_win(win).origin_y + r;
            int32_t tx = map_win(win).origin_x + (r & 1);
            int32_t count = (r & 1) ? map_win(win).cols_odd : map_win(win).cols_even;
            for (int32_t c = 0; c < count; c++, tx += 2) {
                int32_t wx = tx;
                if ((MapToggleFlat & 1) == 0) wx = wrap_x(tx);
                if (ty < 0 || ty >= MapAreaY || wx < 0 || wx >= MapAreaX) continue;
                if (scrolling && !in_scroll_range(win, wx, ty, margin)) continue;
                fn(wx, ty);
            }
        }
    };
    if (!terrain_only) {
        if (!(map_win(win).options & 0x82000000) && !scrolling) Buffer_fill_2(canvas, 0x41);  // GraphicWin_fill
        for_each_tile(0, [&](int32_t tx, int32_t ty) {
            int32_t tpx, tpy;
            pfncTileToPoint(win, tx, ty, &tpx, &tpy);
            MapWin_gen_terrain_poly(canvas, win, map_win(win).zoom, faction_id, -1, map_win(win).options, tx, ty, tpx,
                                    tpy);
        });
    }
    for_each_tile(2, [&](int32_t tx, int32_t ty) {
        int32_t tpx, tpy;
        pfncTileToPoint(win, tx, ty, &tpx, &tpy);
        MapWin_draw_vehicles(win, tx, ty, tpx, tpy - map_win(win).half_h, faction_id);
    });
    MapWin_draw_base_names(win);
    if (map_win(win).options & 0x2000) {
        for_each_tile(2, [&](int32_t tx, int32_t ty) { MapWin_gen_overlays(win, tx, ty); });
    }
    if (map_win(win).options & 0x20000) MapWin_draw_legend(win);
    if (map_win(win).options & 0x100000) MapWin_draw_wars(win);
}

void THISCALL pfncRedrawMap(uint8_t* win, int32_t flag) {
    MapWinNative* n = ui_mapwin_native(win);
    if (!n) return;
    Buffer* canvas = n->canvas;
    UiRect rows_rect{0, 0, 0, 0}, cols_rect{0, 0, 0, 0};  // local_64, local_44
    if (map_win(win).pixel_x || map_win(win).pixel_y) map_win(win).scrolling = 0;
    int32_t ox0 = 0, oy0 = 0;
    if (map_win(win).scrolling) {
        ox0 = map_win(win).origin_x;
        oy0 = map_win(win).origin_y;
    }
    pfncZoomProcessing(win);
    const int32_t viewer = (GameState & STATE_OMNISCIENT_VIEW) ? -1 : CurrentPlayerFaction;
    if (!map_win(win).scrolling) {
        pfncDrawMap(win, viewer, flag < 0);
        return;  // (flag: GraphicWin_redraw shows the canvas; the platform presents it)
    }
    // Scrolled redraw: the canvas content moves with the view and only the uncovered tiles are
    // drawn.
    const int32_t ox = map_win(win).origin_x, oy = map_win(win).origin_y;
    if (ox0 == ox && oy0 == oy) return;
    const bool flat = (MapToggleFlat & 1) != 0;
    auto wrap = [&](int32_t v) { return flat ? v : wrap_x(v); };
    const int32_t cols = map_win(win).cols_even + map_win(win).cols_odd;
    const int32_t nrows = map_win(win).rows_below + map_win(win).rows_above;
    int32_t* cr = scroll_cols(win);
    int32_t* rr = scroll_rows(win);
    if (ox0 == ox) {
        cr[0] = cr[1] = cr[2] = cr[3] = 0;
    } else {
        int32_t x0, x1;
        if (ox0 < ox) {
            if (cols < ox - ox0) {
                x1 = (MapAreaX - ox) + (ox - 1) + 2 + ox0;
                x0 = ox - 1;
            } else {
                x0 = cols - 2 + ox0;
                x1 = (ox - ox0) + 2 + x0;
            }
        } else if (cols < ox0 - ox) {
            x0 = cols - 1 + ox0;
            x1 = (MapAreaX - ox0) + ox + x0;
        } else {
            x0 = ox;
            x1 = ox0 + 1;
        }
        cr[0] = wrap(x0) - 1;
        cr[1] = oy;
        cr[2] = wrap(x1) + 1;
        cr[3] = nrows + oy;
    }
    if (oy0 == oy) {
        rr[0] = rr[1] = rr[2] = rr[3] = 0;
    } else {
        int32_t y0, y1;
        if (oy0 < oy) {
            y0 = nrows - 1 + oy0;
            y1 = (y0 - oy0) + oy;
        } else {
            y0 = oy;
            y1 = oy0;
        }
        rr[0] = wrap(ox);
        rr[1] = y0 - 4;
        rr[2] = wrap(cols + ox);
        rr[3] = y1 + 4;
    }
    // The pixel shift between the old and the new origin tile.
    auto tile_point = [&](int32_t tx, int32_t ty, int32_t* px, int32_t* py) {
        pfncTileToPoint(win, tx, ty, px, py);
        *py += (pfncMapGetCornerYOffset(win, tx, ty, 4) + pfncMapGetCornerYOffset(win, tx, ty, 3) +
                pfncMapGetCornerYOffset(win, tx, ty, 2) + pfncMapGetCornerYOffset(win, tx, ty, 1)) >>
               2;
    };
    int32_t nx, ny, px0, py0;
    tile_point(ox, oy, &nx, &ny);
    tile_point(ox0, oy0, &px0, &py0);
    int32_t dy = py0 - ny, dx = px0 - nx;
    const int32_t w = canvas->width, h = canvas->height;
    Buffer_copy_4(canvas, canvas, dx < 0 ? -dx : 0, dy < 0 ? -dy : 0, dx > 0 ? dx : 0, dy > 0 ? dy : 0, w, h);
    for (int32_t i = 0; i < 4; i++) {
        CursorTileCorners[i][0] -= dx;
        CursorTileCorners[i][1] -= dy;
    }
    int32_t adx = dx < 0 ? -dx : dx;
    int32_t moved = ox - ox0 < 0 ? ox0 - ox : ox - ox0;
    if (cols < moved) {
        if (ox0 <= ox) cols_rect = {0, 0, adx, h};
        else cols_rect = {w - adx, 0, w, h};
    } else if (dx < 0) {
        cols_rect = {w + dx, 0, w + dx + adx, h};
    } else {
        cols_rect = {0, 0, dx, h};
    }
    Buffer_fill(canvas, cols_rect.left, 0, cols_rect.right - cols_rect.left, h, 0);
    if (dy < 0) rows_rect = {0, h + dy, w, h};
    else rows_rect = {0, 0, w, dy};
    Buffer_fill(canvas, 0, rows_rect.top, w, rows_rect.bottom - rows_rect.top, 0);
    auto empty = [](const UiRect& r) { return r.right <= r.left || r.bottom <= r.top; };
    if (!empty(rows_rect)) {
        rows_rect.top -= 10;
        if (empty(cols_rect)) {
            Buffer_set_clip(canvas, &rows_rect);
            pfncDrawMap(win, viewer, flag < 0);
        } else {
            int32_t saved[4];
            std::memcpy(saved, rr, sizeof(saved));
            rr[0] = cr[2];
            Buffer_set_clip(canvas, &cols_rect);
            pfncDrawMap(win, viewer, flag < 0);
            std::memcpy(rr, saved, sizeof(saved));
            std::memcpy(saved, cr, sizeof(saved));
            cr[0] = cr[2];
            Buffer_set_clip(canvas, &rows_rect);
            pfncDrawMap(win, viewer, flag < 0);
            std::memcpy(cr, saved, sizeof(saved));
        }
    } else {
        Buffer_set_clip(canvas, &cols_rect);
        pfncDrawMap(win, viewer, flag < 0);
    }
    // (mouse tracking: the window's mouse-move handler runs again here; not reconstructed)
    Buffer_set_clip(canvas, &graphic_win(win + n->vbase).client);
}

void THISCALL pfncMoveMap(uint8_t* win, int32_t x, int32_t y, int32_t redraw) {
    if ((x ^ y) & 1) {
        if (x & 1) x--;
        if (y & 1) x++;
    }
    int32_t cy0 = map_win(win).center_y, cx0 = map_win(win).center_x;
    map_win(win).center_x = (MapToggleFlat & 1) ? x : wrap_x(x);
    map_win(win).center_y = y;
    // (the main view's destination marker, TutWin_reset and the world window's follow-up are
    // not reconstructed)
    if (cx0 != map_win(win).center_x || cy0 != map_win(win).center_y) pfncRedrawMap(win, redraw);
}
