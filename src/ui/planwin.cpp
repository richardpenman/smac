// PlanWin (ui 0x48AE40..0x48BD80): the go-to list's map preview. While a go-to / find-base list is
// open the main interface's world map pane shows a second map window (a MapWin with its own
// canvas and a background copy) zoomed around the route: the bases the viewer knows, the route's
// end points (a stand-in unit where no base is) and the route as a dashed line in the faction's
// colour, the highlighted end points blinking (PlanWin_blink, 150 ms) and the dashes moving
// (line_timer, 100 ms).
//
// The object's fields (PlanWin, GraphicWin virtual base at +0x22050): +0x21A68 shown, +0x21A6C
// the blink state, +0x21A70 the background Buffer, +0x21FF8 the number of lines (at most 4),
// +0x21FFC their colours, +0x2200C their end points (x1, y1, x2, y2).
#include "ui/planwin.h"

#include <algorithm>
#include <cstring>
#include <vector>

#include "engine/globals.h"
#include "engine/map.h"
#include "engine/ui_bridge.h"
#include "engine/veh.h"
#include "platform/ui_platform.h"
#include "ui/basewin.h"
#include "ui/console.h"
#include "ui/mapwin.h"
#include "ui/sprites.h"
#include "ui/vehdraw.h"
#include "ui/win.h"

GLOBAL(int32_t, DashPhaseP, 0x939FD4);  // the dash pattern counter (line_timer)

namespace {

constexpr uint32_t kPwVBase = 0x22050;
constexpr uint32_t kPwShown = 0x21A68, kPwBlink = 0x21A6C, kPwLines = 0x21FF8, kPwColors = 0x21FFC,
                   kPwEnds = 0x2200C;
std::vector<uint8_t> g_obj(0x22600);
Buffer g_canvas, g_back;
uint32_t g_last_blink = 0, g_last_line = 0;

uint8_t* pw() { return g_obj.data(); }
int32_t& f(uint32_t off) { return mw(pw(), off); }
// The offsets around a tile that must be in the view (0x66EFBC / 0x66F440: 81 entries, the 25
// nearest first).
int32_t ring_x(int32_t i) { return TableOffsetX[i]; }
int32_t ring_y(int32_t i) { return TableOffsetY[i]; }

int32_t wrapx(int32_t x) {
    if (MapToggleFlat & 1) return x;
    if (x < 0) return x + MapAreaX;
    if (MapAreaX <= x) return x - MapAreaX;
    return x;
}
// The tile and the `count` tiles around it on the map are in the window's tile range.
bool surroundings_in_view(int32_t x, int32_t y, int32_t count) {
    uint8_t* w = pw();
    for (int32_t i = 0; i < count; i++) {
        int32_t tx = wrapx(ring_x(i) + x), ty = ring_y(i) + y;
        if (ty < 0 || ty >= MapAreaY || tx < 0 || tx >= MapAreaX) continue;
        if (ty < mw(w, kMwOriginY) || mw(w, kMwRowsBelow) + mw(w, kMwRowsAbove) + mw(w, kMwOriginY) <= ty) return false;
        int32_t right = mw(w, kMwColsEven) + mw(w, kMwColsOdd) + mw(w, kMwOriginX);
        if (!(MapToggleFlat & 1)) {
            if (tx < mw(w, kMwOriginX)) tx += MapAreaX;
            if (right <= tx) tx -= MapAreaX;
        }
        if (tx < mw(w, kMwOriginX) || right <= tx) return false;
    }
    return true;
}
// A base the viewer knows: omniscient, or its tile seen and the base its own or seen by it.
bool base_known(int32_t i, int32_t viewer) {
    if (GameState & 0x80) return true;
    const BASE& b = Bases[i];
    if (!(Factions[viewer].player_flags & 0x200) && !(map_tile(b.x, b.y)->visibility & (1 << viewer))) return false;
    return (uint8_t)b.faction_id == viewer || (reinterpret_cast<const uint8_t*>(&b)[8] & (1 << viewer));
}

}  // namespace

bool planwin_shown() { return f(kPwShown) != 0; }

// FUN_0048BC60: PlanWin set up and shown (MapWin_clear(1), MapWin_init_2(3, 0): the world map
// pane's rectangle, options 0x80000082; zoom -14, an empty background).
void planwin_open() {
    UiRect pane = mi_rect(0xCFC);
    int32_t w = std::max(1, pane.right - pane.left), h = std::max(1, pane.bottom - pane.top);
    if (!ui_mapwin_native(pw()) || g_canvas.width != w || g_canvas.height != h) {
        Buffer_ctor(&g_canvas);
        Buffer_init(&g_canvas, w, h, 8, 0);
        ui_mapwin_attach(pw(), kPwVBase, &g_canvas);
    }
    f(kPwShown) = 1;
    f(kPwLines) = 0;
    f(kMwIndex) = 1;
    MapWin_clear(pw(), 1);
    f(kMwOptions) = (int32_t)0x80000082;
    f(kMwZoom) = -0xE;
    Buffer_ctor(&g_back);
    Buffer_init(&g_back, w, h, 0, 0);
    g_last_blink = g_last_line = platform::ticks_ms();
    std::fprintf(stderr, "planwin open %dx%d\n", w, h);
    ui_invalidate();
}

// FUN_0048BC50: hidden (+0x21A68 cleared, MapWin_close).
void planwin_close() {
    f(kPwShown) = 0;
    ui_invalidate();
}

// FUN_0048AF20.
void planwin_clear_lines() { f(kPwLines) = 0; }

// FUN_0048AE40: a line from (x1, y1) to (x2, y2) in `color` (at most four).
void planwin_add_line(int32_t x1, int32_t y1, int32_t x2, int32_t y2, int32_t color) {
    int32_t n = f(kPwLines);
    if (n >= 4) return;
    int32_t* e = &f(kPwEnds + (uint32_t)n * 0x10);
    e[0] = x1;
    e[1] = y1;
    e[2] = x2;
    e[3] = y2;
    f(kPwColors + (uint32_t)n * 4) = color;
    f(kPwLines) = n + 1;
}

// FUN_0048B3F0: the terrain redrawn into the canvas (options 0x84010002) and kept as background.
void planwin_redraw_back() {
    f(kMwOptions) = (int32_t)0x84010002;
    Buffer_fill_2(&g_canvas, 0);
    pfncRedrawMap(pw(), 0);
    Buffer_copy_4(&g_canvas, &g_back, 0, 0, 0, 0, g_canvas.width, g_canvas.height);
}

// FUN_0048B8E0: zoom in from -13 while the 81 tiles around (x, y) stay in view (to -11 at most);
// the background redrawn when forced or the view changed. The centre is left as it is.
void planwin_zoom_tile(int32_t x, int32_t y, int32_t force) {
    int32_t zoom0 = f(kMwZoom), cx0 = f(kMwCenterX), cy0 = f(kMwCenterY);
    f(kMwZoom) = -0xE;
    for (;;) {
        f(kMwZoom)++;
        pfncZoomProcessing(pw());
        if (!surroundings_in_view(x, y, 81)) {
            f(kMwZoom)--;
            break;
        }
        if (-0xB < f(kMwZoom)) break;
    }
    pfncZoomProcessing(pw());
    if (force || cx0 != f(kMwCenterX) || cy0 != f(kMwCenterY) || zoom0 != f(kMwZoom)) planwin_redraw_back();
}

// FUN_0048B630: centred on (x1, y1), zoomed in from -13 while the 25 tiles around both ends stay
// in view (up to -10).
void planwin_zoom_route(int32_t x1, int32_t y1, int32_t x2, int32_t y2, int32_t force) {
    int32_t zoom0 = f(kMwZoom), cx0 = f(kMwCenterX), cy0 = f(kMwCenterY);
    // (the original computes the route's middle column with xrange and leaves it unused)
    f(kMwCenterX) = x1;
    f(kMwCenterY) = y1;
    f(kMwZoom) = -0xE;
    do {
        f(kMwZoom)++;
        pfncZoomProcessing(pw());
        if (!surroundings_in_view(x1, y1, 25) || !surroundings_in_view(x2, y2, 25)) {
            f(kMwZoom)--;
            break;
        }
    } while (f(kMwZoom) < -10);
    pfncZoomProcessing(pw());
    if (force || cx0 != f(kMwCenterX) || cy0 != f(kMwCenterY) || zoom0 != f(kMwZoom)) planwin_redraw_back();
}

// FUN_0048AF30 (PlanWin's redraw): the background; the bases the viewer knows (an end point's
// highlighted while the blink is on: flags 0x200); a stand-in unit (veh_fake) at the end points
// without a base; the lines, dashed 4 on / 4 off with the moving pattern.
void planwin_draw() {
    if (!f(kPwShown) || !g_canvas.data || !g_back.data) return;
    uint8_t* w = pw();
    Buffer_copy_4(&g_back, &g_canvas, 0, 0, 0, 0, g_canvas.width, g_canvas.height);
    const int32_t viewer = CurrentPlayerFaction, n = f(kPwLines);
    const bool blink = f(kPwBlink) != 0;
    auto end = [&](int32_t k, int32_t j) { return &f(kPwEnds + (uint32_t)k * 0x10 + (uint32_t)j * 8); };
    for (int32_t i = 0; i < BaseCount; i++) {
        if (!base_known(i, viewer)) continue;
        bool hit = false;
        if (blink)
            for (int32_t k = 0; k < n; k++)
                for (int32_t j = 0; j < 2; j++)
                    if (Bases[i].x == end(k, j)[0] && Bases[i].y == end(k, j)[1]) hit = true;
        int32_t px, py;
        pfncTileToPoint(w, Bases[i].x, Bases[i].y, &px, &py);
        base_draw(&g_canvas, i, px, py - mw(w, kMwHalfH), mw(w, kMwZoom), (blink && hit ? 0x200 : 0) | 0x100);
    }
    for (int32_t k = 0; k < n; k++) {
        for (int32_t j = 1; j >= 0; j--) {
            int32_t ex = end(k, j)[0], ey = end(k, j)[1];
            bool base = false;
            for (int32_t i = 0; i < BaseCount && !base; i++)
                if (base_known(i, viewer) && Bases[i].x == ex && Bases[i].y == ey) base = true;
            if (base) continue;
            int32_t px, py;
            pfncTileToPoint(w, ex, ey, &px, &py);
            int32_t v = veh_fake(2, CurrentPlayerFaction);
            veh_draw(&g_canvas, v, px, py - mw(w, kMwHalfH), mw(w, kMwZoom), ((blink ? 2 : 0) | 1) << 8, 1);
        }
    }
    for (int32_t k = 0; k < n; k++) {
        int32_t x1, y1, x2, y2;
        pfncTileToPoint(w, end(k, 0)[0], end(k, 0)[1], &x1, &y1);
        pfncTileToPoint(w, end(k, 1)[0], end(k, 1)[1], &x2, &y2);
        buffer_dashed_line(&g_canvas, x1 + mw(w, kMwHalfW), y1 + mw(w, kMwHalfH), x2 + mw(w, kMwHalfW),
                           y2 + mw(w, kMwHalfH), 0, f(kPwColors + (uint32_t)k * 4), 4, 4, (4 - 1 + 4) & DashPhaseP);
    }
    ui_invalidate();
}

// PlanWin_blink (blink_timer, 150 ms) and line_timer's part (100 ms: the pattern moves).
void planwin_tick(uint32_t now) {
    if (!f(kPwShown)) return;
    bool changed = false;
    if (now - g_last_blink >= 150) {
        g_last_blink = now;
        f(kPwBlink) = f(kPwBlink) == 0;
        changed = true;
    }
    if (now - g_last_line >= 100) {
        g_last_line = now;
        DashPhaseP--;
        changed = true;
    }
    if (changed) planwin_draw();
}

void planwin_compose(Buffer* mi) {
    if (!f(kPwShown) || !g_canvas.data) return;
    UiRect pane = mi_rect(0xCFC);
    Buffer_copy_4(&g_canvas, mi, 0, 0, pane.left, pane.top, std::min(g_canvas.width, pane.right - pane.left),
                  std::min(g_canvas.height, pane.bottom - pane.top));
}

void planwin_log_state() {
    std::fprintf(stderr, "planwin shown %d zoom %d centre %d,%d lines %d", f(kPwShown), f(kMwZoom), f(kMwCenterX),
                 f(kMwCenterY), f(kPwLines));
    for (int32_t k = 0; k < f(kPwLines); k++) {
        const int32_t* e = &f(kPwEnds + (uint32_t)k * 0x10);
        std::fprintf(stderr, " (%d,%d)-(%d,%d) c%d", e[0], e[1], e[2], e[3], f(kPwColors + (uint32_t)k * 4));
    }
    std::fprintf(stderr, "\n");
}

// FUN_0048B450: the centre between two pairs of tiles: the middle column of each pair (across the
// map's wrap when that is shorter), then of the two middles; the row the mean of the pairs' means.
// The zoom is left as it is.
void planwin_center_between(int32_t x1, int32_t y1, int32_t x2, int32_t y2, int32_t x3, int32_t y3, int32_t x4,
                            int32_t y4) {
    const bool wrap = !(MapToggleFlat & 1);
    auto mid = [&](int32_t a, int32_t b) {
        int32_t d = a - b < 0 ? b - a : a - b, m;
        if (wrap && MapAreaX / 2 < d) {
            m = (d - MapAreaX / 2) / 2 + std::max(a, b);
        } else {
            m = d / 2 + std::min(a, b);
            if (!wrap) return m;
        }
        if (m < 0) m += MapAreaX;
        else if (MapAreaX <= m) m -= MapAreaX;
        return m;
    };
    int32_t m1 = mid(x1, x2), m2 = mid(x3, x4);
    f(kMwCenterX) = mid(m1, m2);
    f(kMwCenterY) = ((y2 + y1) / 2 + (y4 + y3) / 2) / 2;
}

// FUN_0048AE90: a line from base a to base b.
void planwin_add_base_line(int32_t a, int32_t b, int32_t color) {
    if (a < 0 || b < 0) return;  // (the original reads the base array at the index)
    planwin_add_line(Bases[a].x, Bases[a].y, Bases[b].x, Bases[b].y, color);
}
