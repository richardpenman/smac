// Map window geometry and terrain rendering (see mapwin.h).
#include "ui/mapwin.h"

#include <algorithm>
#include <cstring>
#include <map>

#include "engine/base.h"
#include "engine/globals.h"
#include "engine/map.h"
#include "engine/ui_bridge.h"
#include "engine/util.h"
#include "engine/veh.h"
#include "ui/sprites.h"

// ---- original globals used here ----
GLOBAL(UiRect, VisibleTiles, 0x7D3C28);    // prVisibleTiles: main view tile range (approximate)
GLOBAL(int32_t[4][2], TileCorners, 0x7D3C08);  // W, N, E, S corner points of the last tile drawn
GLOBAL(int32_t, TerrainDefaultLight, 0x686354);  // light of tiles not currently seen
// Sub-polygon tables of the tile (4 triangles: centre + two diamond corners).
GLOBAL(int32_t[12], TilePolyX, 0x685F20);       // in 2q units
GLOBAL(int32_t[12], TilePolyY, 0x685F50);       // in q units
GLOBAL(int32_t[12], TilePolyCorner, 0x685F80);  // corner index of each vertex (0 centre, 1..4 W N E S)
GLOBAL(int32_t[12][2], TilePolyUV, 0x686090);   // texture coordinates of each vertex
// The 4 tiles around each tile corner (corner c: entries (c-1)*4 .. +3), and 4-vertex quads.
GLOBAL(int32_t[16], CornerTileX, 0x685EA0);
GLOBAL(int32_t[16], CornerTileY, 0x685EE0);
GLOBAL(uint8_t[256][2], RainTransition, 0x685484);  // [neighbour mask]: texture variant, rotation
GLOBAL(int32_t[16][2], OceanQuadUV, 0x685CA8);
GLOBAL(int32_t[80], OceanDepthLight, 0x6861F0);     // ocean depth (detail) -> light
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

uint8_t* cache_entry(uint8_t* win, int32_t x, int32_t y) {
    MapWinNative* n = ui_mapwin_native(win);
    if (!n || n->tile_cache.empty()) return nullptr;
    size_t i = ((size_t)(MapAreaX >> 1) * y + (x >> 1)) * 0xC;
    if (i + 0xC > n->tile_cache.size()) return nullptr;
    return &n->tile_cache[i];
}

int32_t gw_width(uint8_t* win) { return mw(win, ui_mapwin_native(win)->vbase + kGwWidth); }
int32_t gw_height(uint8_t* win) { return mw(win, ui_mapwin_native(win)->vbase + kGwHeight); }

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
    mw(win, vbase + kGwWidth) = canvas->width;
    mw(win, vbase + kGwHeight) = -canvas->height;
    *reinterpret_cast<UiRect*>(win + vbase + kGwClientRect) = {0, 0, canvas->width, canvas->height};
    // What MapWin_ctor (0x4626E0) initialises (native builds never run the original's
    // constructors): the zoom/height caches and the label fonts.
    mw(win, kMwLastZoom) = -9999;
    mw(win, kMwLastElev) = -1;
    Font_ctor(reinterpret_cast<Font*>(win + kMwFont0));
    Font_ctor(reinterpret_cast<Font*>(win + kMwFont1));
    Font_ctor(reinterpret_cast<Font*>(win + kMwFont2));
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

void THISCALL MapWin_clear(uint8_t* win, int32_t index) {
    mw(win, 0x1DD78) = index;
    mw(win, kMwOptions) = 0;
    mw(win, kMwZoom) = CanvasWidth < 1000 ? -6 : 0;
    mw(win, kMwActive) = index == 0;
    int32_t cy = MapAreaY / 2, cx = MapAreaX / 2;
    mw(win, kMwCenterY) = cy;
    mw(win, kMwCenterX) = ((cx ^ cy) & 1) ^ cx;
    if (index == 0) {
        int32_t x = MapWinCursorX[MapWinCursorIndex], y = MapWinCursorY[MapWinCursorIndex];
        if (y >= 0 && y < MapAreaY && x >= 0 && x < MapAreaX) {
            mw(win, kMwCenterX) = x;
            mw(win, kMwCenterY) = y;
        }
    }
    mw(win, 0x1EF50) = 0x7F00;
    mw(win, 0x1EF4C) = 0;
    ui_mapwin_clear_cache(win);  // the original frees the cache; it is rebuilt on demand
}

// ---- geometry -----------------------------------------------------------------------------------

void THISCALL pfncZoomProcessing(uint8_t* win) {
    mw(win, kMwPixelX) = 0;
    mw(win, kMwPixelY) = 0;
    int32_t q = trunc_div4(zoom_scale(mw(win, kMwZoom), 0x32) + 1);
    mw(win, kMwQ) = q;
    if (q < 1) mw(win, kMwQ) = 1;
    q = mw(win, kMwQ);
    int32_t tile_h = q * 4, tile_w = q * 8;
    mw(win, kMwQuarterW) = q * 2;
    mw(win, kMwHalfW) = q * 4;
    mw(win, kMwHalfH) = q * 2;
    mw(win, kMwTileW) = tile_w;
    mw(win, kMwTileH) = tile_h;
    int32_t width = gw_width(win), height = gw_height(win);  // height is negative
    mw(win, kMwColsOdd) = (width - 1 + tile_w) / tile_w;
    mw(win, kMwRowsAbove) = (q * 6 - height - 1) / tile_h;
    mw(win, kMwColsEven) = (width + q * 4 - 1 + tile_w) / tile_w;
    mw(win, kMwRowsBelow) = (q * 6 - height - 1) / tile_h;
    if (win == &MainConsole) {
        VisibleTiles.left = mw(win, kMwCenterX) - mw(win, kMwColsOdd) + 4;
        VisibleTiles.right = mw(win, kMwColsOdd) + mw(win, kMwCenterX) - 4;
        VisibleTiles.top = mw(win, kMwCenterY) - mw(win, kMwRowsAbove) + 4;
        VisibleTiles.bottom = mw(win, kMwCenterY) + mw(win, kMwRowsAbove) - 6;
    }
    int32_t half = MapAreaX / 2;
    if (half < mw(win, kMwColsOdd)) {
        mw(win, kMwColsOdd) = half;
        mw(win, kMwColsEven) = half + 1;
        mw(win, kMwPixelX) = (width - mw(win, kMwColsOdd) * mw(win, kMwTileW)) / 2;
    }
    int32_t half_y = MapAreaY / 2;
    if (half_y < mw(win, kMwRowsAbove)) {
        mw(win, kMwRowsAbove) = half_y;
        mw(win, kMwRowsBelow) = half_y + 1;
        mw(win, kMwPixelY) = -((height + mw(win, kMwRowsAbove) * mw(win, kMwTileH)) / 2);
    }
    mw(win, kMwColsOdd) = std::min(mw(win, kMwColsOdd), MapAreaX / 2);
    mw(win, kMwColsEven) = std::min(mw(win, kMwColsEven), MapAreaX / 2);
    int32_t oy = mw(win, kMwCenterY) - mw(win, kMwRowsAbove);
    int32_t max_y = MapAreaY + (2 - mw(win, kMwRowsAbove)) * 2;
    if (oy < -2 || max_y < -2) oy = -2;
    else if (max_y < oy) oy = max_y;
    mw(win, kMwOriginY) = oy;
    int32_t ox = mw(win, kMwCenterX) - mw(win, kMwColsOdd);
    if ((MapToggleFlat & 1) == 0) ox = wrap_x(ox);
    mw(win, kMwOriginX) = ox;
    if (MapToggleFlat & 1) {
        int32_t max_x = MapAreaX + (2 - mw(win, kMwColsOdd)) * 2;
        if (ox < -4 || max_x < -4) ox = -4;
        else if (max_x < ox) ox = max_x;
        mw(win, kMwOriginX) = ox;
    }
    // The left column has the parity of the top row.
    mw(win, kMwOriginX) = ((mw(win, kMwOriginX) ^ mw(win, kMwOriginY)) & 1) ^ mw(win, kMwOriginX);
    int32_t zoom = mw(win, kMwZoom);
    bool lifted = (mw(win, kMwOptions) & 2) && zoom >= -10;
    int32_t lift = lifted ? (ZoomNum < 1 ? trunc_div16((zoom * 3 + 0x30) * 4) : ZoomNum * 0xC / ZoomDenom) : 0;
    int32_t lift2 = lifted ? (ZoomNum < 1 ? trunc_div16(zoom * 4 + 0x40) : ZoomNum * 4 / ZoomDenom) : 0;
    mw(win, kMwUnitLift) = lift;
    mw(win, kMwUnitLift2) = lift2;
    mw(win, kMwElevScale) = 0x19;
    if (GameMorePreferences & 0x10000) {  // flat terrain
        mw(win, kMwElevScale) = 8;
        mw(win, kMwUnitLift) = lift / 3;
        mw(win, kMwUnitLift2) = lift2 / 3;
    }
    MapWinNative* n = ui_mapwin_native(win);
    bool cache = n && !n->tile_cache.empty();
    if (mw(win, kMwElevScale) != mw(win, kMwLastElev)) {
        mw(win, kMwLastElev) = mw(win, kMwElevScale);
        if (cache)
            for (size_t i = 0; i < n->tile_cache.size(); i += 0xC) std::memset(&n->tile_cache[i + 4], 0xFF, 5);
    }
    if (zoom != mw(win, kMwLastZoom)) {
        if (cache)
            for (size_t i = 0; i < n->tile_cache.size(); i += 0xC) std::memset(&n->tile_cache[i], 0xFF, 9);
        mw(win, kMwLastZoom) = zoom;
        Font* f0 = reinterpret_cast<Font*>(win + kMwFont0);
        Font* f1 = reinterpret_cast<Font*>(win + kMwFont1);
        Font* f2 = reinterpret_cast<Font*>(win + kMwFont2);
        int32_t s = std::min(trunc_div16(mw(win, kMwTileH) * 5), 0xC);
        if (f0->size != s) {
            f0->size = s;
            Font_init(f0, "Arial", s, 0);
        }
        s = std::min(trunc_div16(mw(win, kMwTileH) * 5), 0xC);
        if (f2->size != s) {
            f2->size = s;
            Font_init(f2, "Arial", s, 2);
        }
        s = std::min((mw(win, kMwTileH) << 1) / 5, 0xC);
        if (f1->size != s) {
            f1->size = s;
            Font_init(f1, "Arial", s, 1);
        }
    }
}

void THISCALL pfncTileToPoint(uint8_t* win, int32_t x, int32_t y, int32_t* px, int32_t* py) {
    int32_t ox = mw(win, kMwOriginX), oy = mw(win, kMwOriginY);
    int32_t cols = mw(win, kMwColsEven) + mw(win, kMwColsOdd);
    int32_t d;
    bool in_view = false;
    if (y >= oy && y < mw(win, kMwRowsBelow) + mw(win, kMwRowsAbove) + oy) {
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
    *px = mw(win, kMwHalfW) * (d - 1) + mw(win, kMwPixelX);
    int32_t c4 = pfncMapGetCornerYOffset(win, x, y, 4), c3 = pfncMapGetCornerYOffset(win, x, y, 3);
    int32_t c2 = pfncMapGetCornerYOffset(win, x, y, 2), c1 = pfncMapGetCornerYOffset(win, x, y, 1);
    *py = ((y - oy) - 1) * mw(win, kMwHalfH) + mw(win, kMwPixelY) - ((c4 + c3 + c2 + c1) >> 2);
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
    if (lm & 2) off = landmark_detail(0x7D3B74, lm, corner, false);   // volcano
    else if (lm & 1) off = landmark_detail(0x7F65D0, lm, corner, true);  // crater
    else if (lm & 0x200) off = landmark_detail(0x7F655C, lm, corner, true);  // canyon
    else if (lm & 0x100) off = landmark_detail(0x7D3A78, lm, corner, true);  // mesa
    else if (lm & 0x800) off = landmark_detail(0x7F64E8, lm, corner, true);  // ridge
    int32_t scale = mw(win, kMwElevScale), zoom = mw(win, kMwZoom);
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
                case 0: a = c; b = r; break;
                case 1: a = r_rev; b = c; break;
                case 2: a = c_rev; b = r_rev; break;
                case 3: a = r; b = c_rev; break;
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

// ---- terrain ------------------------------------------------------------------------------------

int32_t GAMEAPI MapWin_gen_terrain_poly(Buffer* dest, uint8_t* win, int32_t zoom, int32_t faction_id, int32_t a5,
                                        int32_t options, int32_t x, int32_t y, int32_t px, int32_t py) {
    if (y < 0 || MapAreaY <= y || x < 0 || MapAreaX <= x) return 0;
    uint32_t opts = (uint32_t)options;
    MAP* sq = tile(x, y);
    uint32_t lm = sq->landmarks;
    if (win && win == &WorldMapWin && !(opts & 0x80000000u) && !(lm & 0x400000)) return 0;
    // Random_reseed(tile address): the variant choice is seeded per tile. Native: the tile's
    // offset in the map (the original's heap address is not reproducible).
    uint32_t rnd = (uint32_t)((sq - MapTiles) * (int32_t)sizeof(MAP));
    auto random_get = [&](int32_t lo, int32_t hi) {
        rnd = rnd * 0x19660D + 0x3C6EF35F;
        return ((hi - lo) * (int32_t)(rnd & 0xFFFF) >> 16) + lo;
    };
    bool ocean = (sq->climate & 0xE0) < 0x60;
    if (!ocean) {
        TextureLightMin = -4;
        TextureLightMax = 4;
    }
    int32_t q = trunc_div4(zoom_scale(zoom, 0x32) + 1);  // local_98
    int32_t q2 = q * 2;                                    // local_78
    int32_t lift = 0;                                      // local_44
    if (win) {
        lift = (pfncMapGetCornerYOffset(win, x, y, 4) + pfncMapGetCornerYOffset(win, x, y, 3) +
                pfncMapGetCornerYOffset(win, x, y, 2) + pfncMapGetCornerYOffset(win, x, y, 1)) >> 2;
    }
    py += lift;
    int32_t rain = (sq->climate & 0x18) >> 3;  // local_84
    int32_t alt = sq->climate >> 5;            // local_8c
    int32_t rocky = sq->val3 >> 6;             // local_38
    bool omniscient = faction_id >= 0 && (Factions[faction_id].player_flags & 0x200);
    uint32_t items;                            // local_24
    if (faction_id < 0 || omniscient) items = sq->items;
    else items = *reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(sq) + 0xC + faction_id * 4);
    bool unexplored_look = false;  // local_70
    (void)unexplored_look;
    bool seen = true;              // local_1c: currently in view (else dithered)
    if (!(opts & 0x1000000)) {
        if ((GameRules & 0x100) && !(GameState & 0x80) && !omniscient && !(sq->visibility & (1 << faction_id))) {
            rain = 0;
            rocky = 0;
            items &= ~0x20u;
            unexplored_look = true;
            seen = false;
            sq->landmarks |= 0x200000;
        } else {
            if ((GameState & 0x80) || faction_id < 1 || !(GameMorePreferences & 1) || zoom < -7) {
                seen = true;
            } else {
                // Approximation of the original's scan (units, sensors and bases of the faction
                // within the 25 tiles around): any unit or base of the faction there.
                seen = false;
                for (int32_t i = 0; i < 25 && !seen; i++) {
                    int32_t nx = wrap_x(TableOffsetX[i] + x), ny = TableOffsetY[i] + y;
                    if (!in_map(nx, ny)) continue;
                    int32_t b = base_at(nx, ny);
                    if (b >= 0 && Bases[b].faction_id == faction_id) seen = true;
                    for (int32_t v = veh_at(nx, ny); v >= 0 && !seen; v = Vehs[v].next_veh_id_stack)
                        if (Vehs[v].faction_id == faction_id) seen = true;
                }
            }
            if (seen) sq->landmarks &= ~0x200000u;
            else sq->landmarks |= 0x200000;
        }
    } else {
        rain = 0;
        rocky = 0;
        items = 0;
        unexplored_look = true;
        seen = true;
        sq->landmarks &= ~0x200000u;
    }
    bool no_terrain = (opts & 0x80000000u) != 0;  // local_20
    uint32_t coast[4] = {0, 0, 0, 0};             // local_108: water next to each quadrant
    uint32_t rain_mask = 0;                       // local_28
    uint32_t fungus_mask = 0;                     // local_40
    uint32_t jungle_mask = 0;                     // local_2c
    bool jungle = !ocean && (sq->landmarks & 0x80000004) == 4;  // local_30 (Monsoon Jungle)
    if (!no_terrain && !ocean) {
        for (int32_t k = 0; k < 8; k++) {
            int32_t nx = wrap_x(NearOffsetX[k] + x), ny = NearOffsetY[k] + y;
            if (!in_map(nx, ny) || (tile(nx, ny)->climate & 0xE0) >= 0x60) continue;
            int32_t h = k >> 1;
            if ((k & 1) == 0) {
                coast[(h + 1) & 3] |= 4;
                coast[(h - 2) & 3] |= 1;
            } else {
                coast[(h - 2) & 3] |= 2;
            }
        }
        if (rain || (items & 0x20) || jungle) {
            for (int32_t k = 0; k < 8; k++) {
                int32_t d = (k - 1) & 7;
                int32_t nx = wrap_x(NearOffsetX[d] + x), ny = NearOffsetY[d] + y;
                if (!in_map(nx, ny)) continue;
                MAP* n = tile(nx, ny);
                if ((n->items & 0x20) && (n->climate & 0xE0) >= 0x40) fungus_mask |= 1u << k;
                if (jungle && (n->landmarks & 0x80000004) == 4 && (n->climate & 0xE0) >= 0x60) jungle_mask |= 1u << k;
                if (rain && ((n->climate & 0xE0) < 0x60 || rain <= ((n->climate >> 3) & 3))) rain_mask |= 1u << k;
            }
        }
    } else if (!no_terrain && (items & 0x20)) {
        for (int32_t k = 0; k < 8; k++) {
            int32_t d = (k - 1) & 7;
            int32_t nx = wrap_x(NearOffsetX[d] + x), ny = NearOffsetY[d] + y;
            if (!in_map(nx, ny)) continue;
            MAP* n = tile(nx, ny);
            if ((n->items & 0x20) && (n->climate & 0xE0) >= 0x40) fungus_mask |= 1u << k;
        }
    }
    px += q2 * 2;
    py += q2;
    int32_t pts[12][2], uv[12][2], uv_rocky[12][2], light[3];
    // Unexplored: only the grid over black.
    if (a5 > 0x15 || (!(GameRules & 0x100) && faction_id >= 0 && !omniscient && !(sq->visibility & (1 << faction_id)))) {
        if (!no_terrain) {
            for (int32_t i = 0; i < 12; i++) {
                pts[i][0] = TilePolyX[i] * q2 + px;
                pts[i][1] = TilePolyY[i] * q + py;
                if (win) {
                    int32_t c = 0;
                    pfncMapGetCornerYOffset(win, x, y, TilePolyCorner[i]);
                    if (uint8_t* e = cache_entry(win, x, y)) c = (int8_t)e[4 + TilePolyCorner[i]];
                    pts[i][1] -= c;
                }
            }
            for (int32_t i = 0; i < 4; i++) {
                TileCorners[i][0] = pts[i * 3 + 1][0];
                TileCorners[i][1] = pts[i * 3 + 1][1];
            }
            for (int32_t i = 1; i <= 4; i++)
                Buffer_line_2(dest, TileCorners[i - 1][0], TileCorners[i - 1][1], TileCorners[i & 3][0],
                              TileCorners[i & 3][1], 0xC2);
        }
        TextureLightMin = -0x100;
        TextureLightMax = 0x100;
        return 0;
    }
    int32_t variant = 0;               // local_bc
    int32_t rnd_variant = random_get(0, 2);  // local_c4
    if (!(opts & 0x10000000)) {
        for (int32_t i = 0; i < 4; i++) {
            for (int32_t j = 0; j < 3; j++) {
                int32_t idx = i * 3 + j;
                pts[idx][0] = TilePolyX[idx] * q2 + px;
                pts[idx][1] = TilePolyY[idx] * q + py;
                int32_t r = idx;
                if (!ocean && !(items & 0x8000) && rain && !no_terrain)
                    r = j + ((RainTransition[rain_mask][1] + i) & 3) * 3;
                uv[idx][0] = TilePolyUV[r][0];
                uv[idx][1] = TilePolyUV[r][1];
                if (rocky) {
                    uv_rocky[idx][0] = TilePolyUV[idx][0];
                    uv_rocky[idx][1] = TilePolyUV[idx][1];
                }
                if (alt > 2 && rain) variant = RainTransition[rain_mask][0];
            }
        }
    }
    int32_t color = seen ? -1 : 0x41;
    int32_t corner_lights[4] = {0, 0, 0, 0};
    for (int32_t i = 0; i < 4; i++) {
        for (int32_t j = 0; j < 3; j++) {
            int32_t idx = i * 3 + j;
            if (alt < 3) {
                light[j] = i;
            } else if (win) {
                pts[idx][1] -= pfncMapGetCornerYOffset(win, x, y, TilePolyCorner[idx]);
                light[j] = seen ? MapWin_get_point_light(win, x, y, TilePolyCorner[idx]) : TerrainDefaultLight;
            } else {
                light[j] = 0;
            }
        }
        TileCorners[i][0] = pts[i * 3 + 1][0];
        TileCorners[i][1] = pts[i * 3 + 1][1];
        corner_lights[i] = light[1];  // local_b8
        if (ocean || no_terrain) continue;
        const int32_t* p = &pts[i * 3][0];
        if ((items & 0x8000) && (opts & 1)) {
            pfncImageCopytoCanvas2(&TexFarm[std::min(rnd_variant * 3 + rain, 8)], dest, p, light, 3, &uv[i * 3][0], color);
        } else if (rain == 0) {
            pfncImageCopytoCanvas2(&TexArid, dest, p, light, 3, &uv[i * 3][0], color);
        } else if (rain == 1) {
            pfncImageCopytoCanvas2(&TexMoist[variant & 15], dest, p, light, 3, &uv[i * 3][0], color);
        } else if (rain == 2) {
            pfncImageCopytoCanvas2(&TexRainy[variant & 15], dest, p, light, 3, &uv[i * 3][0], color);
        }
        if (rocky) Texture_draw_trans_5(&TexRocky[(rnd_variant + rocky * 2 - 2) & 3], dest, p, light, 3, &uv_rocky[i * 3][0], color);
    }
    // Water: four quads of four vertices with a depth value each; deep water where the depth
    // reaches 16, shallow water blended over it below.
    if (!(opts & 0x1000000) && !(opts & 0x2000000) && ocean && !no_terrain) {
        for (int32_t i = 0; i < 4; i++) {
            int32_t qp[4][2], quv[4][2], ql[4], ql2[4];
            bool deep = false;
            for (int32_t k = 0; k < 4; k++) {
                int32_t idx = ((i - 2 + k) & 3) + i * 4;
                qp[k][0] = CornerTileX[idx] * q2 + px;
                qp[k][1] = CornerTileY[idx] * q + py;
                ql[k] = OceanDepthLight[alt_get_ocean_detail(x, y, i, k)];
                quv[k][0] = OceanQuadUV[i * 4 + k][0];
                quv[k][1] = OceanQuadUV[i * 4 + k][1];
                deep |= ql[k] > 0xF;
            }
            if (deep) {
                for (int32_t k = 0; k < 4; k++) ql2[k] = ql[k] - 0x10;
                pfncImageCopytoCanvas2(&TexWater[1], dest, &qp[0][0], ql2, 4, &quv[0][0], color);
            }
            Texture_draw_4(&TexWater[0], dest, &qp[0][0], ql, 4, &quv[0][0], 0x10, color);
        }
    }
    // Grid (preference 0x100): the two upper edges, between land tiles (or water too with the
    // water-grid preference).
    if ((((GamePreferences & 0x100) && !no_terrain) || (opts & 0x4000000)) &&
        (!ocean || no_terrain || (GameMorePreferences & 0x800000))) {
        int32_t nx = wrap_x(x - 1);
        for (int32_t i = 1; i <= 2; i++, nx = wrap_x(nx + 2)) {
            int32_t ny = y - 1;
            if (!in_map(nx, ny)) continue;
            bool n_land = (tile(nx, ny)->climate & 0xE0) >= 0x60;
            int32_t c;
            if (n_land) {
                c = ocean ? 0xBE : 0x25;
            } else {
                if (!no_terrain && !(GameMorePreferences & 0x800000)) continue;
                c = 0xBE;
            }
            Buffer_line_2(dest, TileCorners[i - 1][0], TileCorners[i - 1][1], TileCorners[i & 3][0],
                          TileCorners[i & 3][1], c);
        }
    }
    // Monsoon Jungle over the tile, joined with jungle neighbours.
    if (jungle && !unexplored_look && !no_terrain) {
        int32_t rot = RainTransition[jungle_mask][1], juv[4][2], zero[4] = {0, 0, 0, 0};
        for (int32_t k = 0; k < 4; k++) {
            const int32_t* t = reinterpret_cast<const int32_t*>(mem::host(0x686098 + ((rot + k) & 3) * 0x18));
            juv[k][0] = t[0];
            juv[k][1] = t[1];
        }
        TextureKey = kArtKey;
        Texture_draw_trans_5(&TexJungle[std::min<int32_t>(RainTransition[jungle_mask][0], 14)], dest, &TileCorners[0][0],
                             zero, 4, &juv[0][0], -1);
    }
    // Forest over the tile; the variant joins it with forests on the diagonal neighbours.
    if ((items & 0x200000) && alt > 2 && (opts & 1) && !unexplored_look && !no_terrain) {
        int32_t mask = 0;
        for (int32_t k = 0; k < 4; k++) {
            int32_t nx = wrap_x(DiagOffsetX[k] + x), ny = DiagOffsetY[k] + y;
            if (in_map(nx, ny) && (tile(nx, ny)->items & 0x200000)) mask |= 1 << k;
        }
        int32_t corner_light[4] = {corner_lights[0], corner_lights[1], corner_lights[2], corner_lights[3]};
        const int32_t forest_uv[4][2] = {{0, 0x37}, {0, 0}, {0x37, 0}, {0x37, 0x37}};
        TextureKey = kArtKey;
        Texture_draw_trans_5(&TexForest[mask], dest, &TileCorners[0][0], corner_light, 4, &forest_uv[0][0], color);
    }
    // Xenofungus over the tile (4 corners), sea fungus on water.
    if ((items & 0x20) && alt > 1 && !unexplored_look && zoom > -10 && !no_terrain) {
        int32_t rot = RainTransition[fungus_mask][1], fuv[4][2], zero[4] = {0, 0, 0, 0};
        for (int32_t k = 0; k < 4; k++) {
            const int32_t* t = reinterpret_cast<const int32_t*>(mem::host(0x686098 + ((rot + k) & 3) * 0x18));
            fuv[k][0] = t[0];
            fuv[k][1] = t[1];
        }
        TextureKey = kArtKey;
        Texture* ft = &TexFungus[std::min<int32_t>(RainTransition[fungus_mask][0], 14) + (ocean ? 15 : 0)];
        Texture_draw_trans_5(ft, dest, &TileCorners[0][0], zero, 4, &fuv[0][0], color);
    }
    // Back at the tile's top-left; sprites are scaled with the zoom.
    px -= q2 * 2;
    py -= lift + q2;
    if (ZoomNum < 1) {
        BlitSourceScale = 0x10;
        BlitDestScale = zoom + 0x10;
    } else {
        BlitDestScale = ZoomNum;
        BlitSourceScale = ZoomDenom;
    }
    // Not drawn yet: landmark and river textures, coasts, roads and mag tubes, forest and fungus,
    // territory borders, resource and supply pod icons (see docs/UI_PLAN.md).
    bool known = !(no_terrain && !(opts & 0x10000000)) &&
                 (faction_id < 0 || omniscient || (sq->visibility & (1 << faction_id)));
    if (known) {
        auto sprite = [&](Sprite* s) { pfncSpriteStretchCopyToCanvas(s, dest, s->key, px, py); };
        if ((opts & 1) && (items & 0x8000) && ocean) sprite(&SprKelp);
        if (opts & 0x20) {  // improvements
            int32_t food = 0;
            if ((items & 0x8000) && zoom > -8) {
                int32_t owner = faction_id >= 0 ? faction_id : CurrentPlayerFaction;
                food = crop_yield(owner, -1, x, y, 0);
            }
            if ((items & 0x800) && zoom > -8 && !ocean) sprite(&SprBunker);
            if ((items & 0x40000) && zoom > -8 && !ocean) sprite(&SprAirbase);
            if ((items & 0x80000000u) && zoom > -8)
                Sprite_draw_dest(&SprSensor, dest, SprSensor.key, px, py, ShadowTable);
            if ((items & 0x8000) && zoom > -8 && !ocean) {
                int32_t k = std::max(0, std::min(3, food - 1));
                sprite((items & 0x80000) ? &SprEnricher[k] : &SprFarm[k]);
            }
            if ((items & 0x10) && zoom > -8) {
                if (!ocean) sprite(&SprMine);
                else if (ExpansionEnabled || alt == 2) sprite(&SprMineSea);
            }
            if ((items & 0x40) && zoom > -8) {
                if (!ocean) sprite(&SprSolar);
                else if (ExpansionEnabled || alt == 2) sprite(&SprSolarSea);
            }
            if ((items & 0x400000) && zoom > -8) sprite(&SprCondenser);
            if ((items & 0x800000) && zoom > -8) sprite(&SprMirror);
            if ((items & 0x1000000) && zoom > -8)
                sprite((sq->landmarks & 0x80001000) == 0x1000 ? &SprBoreholeGeo : &SprBorehole);
            if ((items & 0x8000) && zoom > -8 && !ocean && food > 4 && (items & 0x80000)) sprite(&SprEnricher[0]);
        }
        if ((opts & 0x200) && zoom > -8 && (sq->items & 0x2000))
            Sprite_draw_dest(&SprMonolith, dest, SprMonolith.key, px, py, ShadowTable);
    }
    BlitDestScale = 1;
    BlitSourceScale = 1;
    TextureLightMin = -0x100;
    TextureLightMax = 0x100;
    return 0;
}

// ---- map drawing --------------------------------------------------------------------------------

void THISCALL pfncDrawMap(uint8_t* win, int32_t faction_id, int32_t terrain_only) {
    MapWinNative* n = ui_mapwin_native(win);
    Buffer* canvas = n->canvas;
    int32_t rows = mw(win, kMwRowsAbove) + 1 + mw(win, kMwRowsBelow);
    // Every tile of the view, row by row, odd rows shifted by one column.
    auto for_each_tile = [&](auto&& fn) {
        for (int32_t r = 0; r < rows; r++) {
            int32_t ty = mw(win, kMwOriginY) + r;
            int32_t tx = mw(win, kMwOriginX) + (r & 1);
            int32_t count = (r & 1) ? mw(win, kMwColsOdd) : mw(win, kMwColsEven);
            for (int32_t c = 0; c < count; c++, tx += 2) {
                int32_t wx = tx;
                if ((MapToggleFlat & 1) == 0) wx = wrap_x(tx);
                if (ty < 0 || ty >= MapAreaY || wx < 0 || wx >= MapAreaX) continue;
                fn(wx, ty);
            }
        }
    };
    if (!terrain_only) {
        if (!(mw(win, kMwOptions) & 0x82000000) && !mw(win, kMwScrolling)) {
            UiRect all = {0, 0, canvas->width, canvas->height};
            Buffer_fill_3(canvas, &all, 0x41);  // GraphicWin_fill(0x41)
        }
        for_each_tile([&](int32_t tx, int32_t ty) {
            int32_t tpx, tpy;
            pfncTileToPoint(win, tx, ty, &tpx, &tpy);
            MapWin_gen_terrain_poly(canvas, win, mw(win, kMwZoom), faction_id, -1, mw(win, kMwOptions), tx, ty, tpx, tpy);
        });
    }
    for_each_tile([&](int32_t tx, int32_t ty) {
        int32_t tpx, tpy;
        pfncTileToPoint(win, tx, ty, &tpx, &tpy);
        MapWin_draw_vehicles(win, tx, ty, tpx, tpy - mw(win, kMwHalfH), faction_id);
    });
    MapWin_draw_base_names(win);
}

namespace native_ui {

void THISCALL pfncRedrawMap(uint8_t* win, int32_t flag) {
    MapWinNative* n = ui_mapwin_native(win);
    if (!n) return;
    mw(win, kMwScrolling) = 0;
    pfncZoomProcessing(win);
    Buffer_set_clip(n->canvas, nullptr);
    int32_t viewer = (GameState & 0x80) ? -1 : CurrentPlayerFaction;
    pfncDrawMap(win, viewer, flag < 0);
}

void THISCALL pfncMoveMap(uint8_t* win, int32_t x, int32_t y, int32_t redraw) {
    mw(win, kMwCenterX) = x;
    mw(win, kMwCenterY) = y;
    if (redraw) pfncRedrawMap(win, 1);
}

}  // namespace native_ui
