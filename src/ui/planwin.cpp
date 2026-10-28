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
// The object (a MapWin, then PlanWin's fields; GraphicWin virtual base at +0x22050).
struct PlanWin {
    MapWin map;  // +0x0 (map.unk_21a68: shown)
    int32_t blink;  // +0x21A6C: the blink state
    uint8_t back[0x21FF8 - 0x21A70];  // +0x21A70: the background Buffer (native: g_back)
    int32_t lines;  // +0x21FF8: the number of lines (at most 4)
    int32_t colors[4];  // +0x21FFC
    int32_t ends[4][4];  // +0x2200C: x1, y1, x2, y2
};
static_assert(offsetof(PlanWin, blink) == 0x21A6C, "PlanWin layout");
static_assert(offsetof(PlanWin, lines) == 0x21FF8, "PlanWin layout");
static_assert(offsetof(PlanWin, colors) == 0x21FFC, "PlanWin layout");
static_assert(offsetof(PlanWin, ends) == 0x2200C, "PlanWin layout");
std::vector<uint8_t> g_obj(0x22600);
Buffer g_canvas, g_back;
uint32_t g_last_blink = 0, g_last_line = 0;

uint8_t* pw() { return g_obj.data(); }
PlanWin& plan() { return *reinterpret_cast<PlanWin*>(pw()); }
int32_t& shown() { return plan().map.unk_21a68; }
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
        if (ty < map_win(w).origin_y || map_win(w).rows_below + map_win(w).rows_above + map_win(w).origin_y <= ty)
            return false;
        int32_t right = map_win(w).cols_even + map_win(w).cols_odd + map_win(w).origin_x;
        if (!(MapToggleFlat & 1)) {
            if (tx < map_win(w).origin_x) tx += MapAreaX;
            if (right <= tx) tx -= MapAreaX;
        }
        if (tx < map_win(w).origin_x || right <= tx) return false;
    }
    return true;
}
// A base the viewer knows: omniscient, or its tile seen and the base its own or seen by it.
bool base_known(int32_t i, int32_t viewer) {
    if (GameState & STATE_OMNISCIENT_VIEW) return true;
    const BASE& b = Bases[i];
    if (!(Factions[viewer].player_flags & 0x200) && !(map_tile(b.x, b.y)->visibility & (1 << viewer))) return false;
    return (uint8_t)b.faction_id == viewer || (b.visibility & (1 << viewer));
}

}  // namespace

bool planwin_shown() { return shown() != 0; }

// PlanWin_show (0x48BC60): PlanWin set up and shown (MapWin_clear(1), MapWin_init_2(3, 0): the world map
// pane's rectangle, options 0x80000082; zoom -14, an empty background).
void planwin_open() {
    UiRect pane = main_interface().world_map;
    int32_t w = std::max(1, pane.right - pane.left), h = std::max(1, pane.bottom - pane.top);
    if (!ui_mapwin_native(pw()) || g_canvas.width != w || g_canvas.height != h) {
        Buffer_ctor(&g_canvas);
        Buffer_init(&g_canvas, w, h, 8, 0);
        ui_mapwin_attach(pw(), kPwVBase, &g_canvas);
    }
    shown() = 1;
    plan().lines = 0;
    plan().map.index = 1;
    MapWin_clear(pw(), 1);
    plan().map.options = (int32_t)0x80000082;
    plan().map.zoom = -0xE;
    Buffer_ctor(&g_back);
    Buffer_init(&g_back, w, h, 0, 0);
    g_last_blink = g_last_line = platform::ticks_ms();
    std::fprintf(stderr, "planwin open %dx%d\n", w, h);
    ui_invalidate();
}

// PlanWin_hide (0x48BC50): hidden (+0x21A68 cleared, MapWin_close).
void planwin_close() {
    shown() = 0;
    ui_invalidate();
}

// PlanWin_clear_lines (0x48AF20).
void planwin_clear_lines() { plan().lines = 0; }

// PlanWin_add_line (0x48AE40): a line from (x1, y1) to (x2, y2) in `color` (at most four).
void planwin_add_line(int32_t x1, int32_t y1, int32_t x2, int32_t y2, int32_t color) {
    int32_t n = plan().lines;
    if (n >= 4) return;
    int32_t* e = plan().ends[n];
    e[0] = x1;
    e[1] = y1;
    e[2] = x2;
    e[3] = y2;
    plan().colors[n] = color;
    plan().lines = n + 1;
}

// PlanWin_draw_background (0x48B3F0): the terrain redrawn into the canvas (options 0x84010002) and kept as background.
void planwin_redraw_back() {
    plan().map.options = (int32_t)0x84010002;
    Buffer_fill_2(&g_canvas, 0);
    pfncRedrawMap(pw(), 0);
    Buffer_copy_4(&g_canvas, &g_back, 0, 0, 0, 0, g_canvas.width, g_canvas.height);
}

// PlanWin_zoom_tile (0x48B8E0): zoom in from -13 while the 81 tiles around (x, y) stay in view (to -11 at most);
// the background redrawn when forced or the view changed. The centre is left as it is.
void planwin_zoom_tile(int32_t x, int32_t y, int32_t force) {
    int32_t zoom0 = plan().map.zoom, cx0 = plan().map.center_x, cy0 = plan().map.center_y;
    plan().map.zoom = -0xE;
    for (;;) {
        plan().map.zoom++;
        pfncZoomProcessing(pw());
        if (!surroundings_in_view(x, y, 81)) {
            plan().map.zoom--;
            break;
        }
        if (-0xB < plan().map.zoom) break;
    }
    pfncZoomProcessing(pw());
    if (force || cx0 != plan().map.center_x || cy0 != plan().map.center_y || zoom0 != plan().map.zoom)
        planwin_redraw_back();
}

// PlanWin_zoom_route (0x48B630): centred on (x1, y1), zoomed in from -13 while the 25 tiles around both ends stay
// in view (up to -10).
void planwin_zoom_route(int32_t x1, int32_t y1, int32_t x2, int32_t y2, int32_t force) {
    int32_t zoom0 = plan().map.zoom, cx0 = plan().map.center_x, cy0 = plan().map.center_y;
    // (the original computes the route's middle column with xrange and leaves it unused)
    plan().map.center_x = x1;
    plan().map.center_y = y1;
    plan().map.zoom = -0xE;
    do {
        plan().map.zoom++;
        pfncZoomProcessing(pw());
        if (!surroundings_in_view(x1, y1, 25) || !surroundings_in_view(x2, y2, 25)) {
            plan().map.zoom--;
            break;
        }
    } while (plan().map.zoom < -10);
    pfncZoomProcessing(pw());
    if (force || cx0 != plan().map.center_x || cy0 != plan().map.center_y || zoom0 != plan().map.zoom)
        planwin_redraw_back();
}

// PlanWin_redraw (0x48AF30) (PlanWin's redraw): the background; the bases the viewer knows (an end point's
// highlighted while the blink is on: flags 0x200); a stand-in unit (veh_fake) at the end points
// without a base; the lines, dashed 4 on / 4 off with the moving pattern.
void planwin_draw() {
    if (!shown() || !g_canvas.data || !g_back.data) return;
    uint8_t* w = pw();
    Buffer_copy_4(&g_back, &g_canvas, 0, 0, 0, 0, g_canvas.width, g_canvas.height);
    const int32_t viewer = CurrentPlayerFaction, n = plan().lines;
    const bool blink = plan().blink != 0;
    auto end = [&](int32_t k, int32_t j) { return &plan().ends[k][j * 2]; };
    for (int32_t i = 0; i < BaseCount; i++) {
        if (!base_known(i, viewer)) continue;
        bool hit = false;
        if (blink)
            for (int32_t k = 0; k < n; k++)
                for (int32_t j = 0; j < 2; j++)
                    if (Bases[i].x == end(k, j)[0] && Bases[i].y == end(k, j)[1]) hit = true;
        int32_t px, py;
        pfncTileToPoint(w, Bases[i].x, Bases[i].y, &px, &py);
        base_draw(&g_canvas, i, px, py - map_win(w).half_h, map_win(w).zoom, (blink && hit ? 0x200 : 0) | 0x100);
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
            veh_draw(&g_canvas, v, px, py - map_win(w).half_h, map_win(w).zoom, ((blink ? 2 : 0) | 1) << 8, 1);
        }
    }
    for (int32_t k = 0; k < n; k++) {
        int32_t x1, y1, x2, y2;
        pfncTileToPoint(w, end(k, 0)[0], end(k, 0)[1], &x1, &y1);
        pfncTileToPoint(w, end(k, 1)[0], end(k, 1)[1], &x2, &y2);
        buffer_dashed_line(&g_canvas, x1 + map_win(w).half_w, y1 + map_win(w).half_h, x2 + map_win(w).half_w,
                           y2 + map_win(w).half_h, 0, plan().colors[k], 4, 4, (4 - 1 + 4) & DashPhaseP);
    }
    ui_invalidate();
}

// PlanWin_blink (blink_timer, 150 ms) and line_timer's part (100 ms: the pattern moves).
void planwin_tick(uint32_t now) {
    if (!shown()) return;
    bool changed = false;
    if (now - g_last_blink >= 150) {
        g_last_blink = now;
        plan().blink = plan().blink == 0;
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
    if (!shown() || !g_canvas.data) return;
    UiRect pane = main_interface().world_map;
    Buffer_copy_4(&g_canvas, mi, 0, 0, pane.left, pane.top, std::min(g_canvas.width, pane.right - pane.left),
                  std::min(g_canvas.height, pane.bottom - pane.top));
}

void planwin_log_state() {
    std::fprintf(stderr, "planwin shown %d zoom %d centre %d,%d lines %d", shown(), plan().map.zoom,
                 plan().map.center_x, plan().map.center_y, plan().lines);
    for (int32_t k = 0; k < plan().lines; k++) {
        const int32_t* e = plan().ends[k];
        std::fprintf(stderr, " (%d,%d)-(%d,%d) c%d", e[0], e[1], e[2], e[3], plan().colors[k]);
    }
    std::fprintf(stderr, "\n");
}

// PlanWin_center_between (0x48B450): the centre between two pairs of tiles: the middle column of each pair (across the
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
    plan().map.center_x = mid(m1, m2);
    plan().map.center_y = ((y2 + y1) / 2 + (y4 + y3) / 2) / 2;
}

// PlanWin_add_base_line (0x48AE90): a line from base a to base b.
void planwin_add_base_line(int32_t a, int32_t b, int32_t color) {
    if (a < 0 || b < 0) return;  // (the original reads the base array at the index)
    planwin_add_line(Bases[a].x, Bases[a].y, Bases[b].x, Bases[b].y, color);
}
