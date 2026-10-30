// OpenSMAC's replacement of the headless "mapview" area: the map drawing entry points the engine calls
// (engine/ui_bridge.h). pfncRedrawMap and pfncMoveMap are in mapwin.cpp, MapWin_draw_radius in
// mapwin_radius.cpp. The engine's draw_tile family redraws the changed tiles at once on every open
// map window (the main view and the active ones), as the original; draw_map marks the main view
// for a full redraw at the next presentation (wait_task / do_non_input). MapWin_focus is
// reconstructed.
#include "engine/map.h"
#include "engine/ui_bridge.h"
#include "ui/console.h"
#include "ui/mapwin.h"
#include "ui/win.h"

void GAMEAPI draw_radius(int32_t x, int32_t y, int32_t radius, int32_t mode) {
    for_each_mapwin([&](int32_t, uint8_t* w) { MapWin_draw_radius(w, x, y, radius, mode); });
}
void GAMEAPI draw_tile(int32_t x, int32_t y, int32_t mode) {
    for_each_mapwin([&](int32_t, uint8_t* w) { MapWin_draw_radius(w, x, y, 0, mode); });
}
void GAMEAPI draw_tiles(int32_t x, int32_t y, int32_t mode) {
    for_each_mapwin([&](int32_t, uint8_t* w) { MapWin_draw_radius(w, x, y, 1, mode); });
}
// With the "radar" preference (GameMorePreferences bit 0) the tiles around (x, y) (radius 2, 1 more
// with has_radar) are redrawn and shown at once (the viewer's faction number passed as the
// mode, as the original does), else the tile with `mode`.
void GAMEAPI draw_tile_fixup(int32_t x, int32_t y, int32_t has_radar, int32_t mode) {
    if (GameMorePreferences & MPREF_MAP_SHOW_FOG_WAR) {
        int32_t viewer = CurrentPlayerFaction;
        for_each_mapwin([&](int32_t, uint8_t* w) { MapWin_draw_radius(w, x, y, (has_radar != 0) + 1, viewer); });
        return;
    }
    for_each_mapwin([&](int32_t, uint8_t* w) { MapWin_draw_radius(w, x, y, 0, mode); });
}
void GAMEAPI draw_tile_fixup_pair(int32_t x1, int32_t y1, int32_t x2, int32_t y2, int32_t has_radar, int32_t mode) {
    if (!(GameMorePreferences & MPREF_MAP_SHOW_FOG_WAR)) {
        for_each_mapwin([&](int32_t, uint8_t* w) { MapWin_draw_radius(w, x1, y1, 0, mode); });
        return;
    }
    for_each_mapwin(
        [&](int32_t, uint8_t* w) { MapWin_draw_radius(w, x1, y1, (has_radar != 0) + 1, CurrentPlayerFaction); });
    if (x2 != -1) {
        int32_t viewer = CurrentPlayerFaction;
        for_each_mapwin([&](int32_t, uint8_t* w) { MapWin_draw_radius(w, x2, y2, (has_radar != 0) + 1, viewer); });
    }
}
void GAMEAPI draw_map(int32_t) { ui_invalidate_map(); }
// The caption is shown by the placeholder console panel.
void THISCALL MapWin_main_caption(uint8_t*) { ui_invalidate(); }
// The native terrain cache is cleared whole (the original clears the tile's entries).
void THISCALL MapWin_clear_alt(uint8_t* win, int32_t, int32_t, int32_t) {
    if (ui_mapwin_native(win)) ui_mapwin_clear_cache(win);
    ui_invalidate_map();
}
void THISCALL MapWin_clear_terrain(uint8_t* win) {
    if (ui_mapwin_native(win)) ui_mapwin_clear_cache(win);
    ui_invalidate_map();
}
void ui_mapwins_clear_terrain() {
    for_each_mapwin([](int32_t, uint8_t* w) { MapWin_clear_terrain(w); });
}
void ui_mapwins_clear_alt(int32_t x, int32_t y) {
    for_each_mapwin([&](int32_t, uint8_t* w) {
        map_win(w).scrolling = 0;  // full redraw
        MapWin_clear_alt(w, x, y, 0);
    });
}

int32_t THISCALL MapWin_focus(uint8_t* win, int32_t x, int32_t y) {
    if (win == &MainConsole) {
        // The main view keeps the tile when it is inside the visible range (pfncZoomProcessing).
        if (x >= VisibleTiles.left && x < VisibleTiles.right && y >= VisibleTiles.top && y < VisibleTiles.bottom)
            return 0;
        if (!map_win(win).index) {
            console_win(win).cursor_shown = MapWinInVehMode == 0;
            console_win(win).cursor_on = 1;
        }
        map_win(win).scrolling = 1;
        pfncMoveMap(win, x, y, 1);
        map_win(win).scrolling = 0;
        return 1;
    }
    int32_t changed = 0;
    int32_t ox = map_win(win).origin_x;
    int32_t right = ox - 2 + map_win(win).cols_even * 2;
    bool wrap = !(MapToggleFlat & 1);
    if (wrap) {
        if (right < 0) right += MapAreaX;
        else if (MapAreaX <= right) right -= MapAreaX;
    }
    if (wrap && x <= map_win(win).cols_odd * 2 && MapAreaX - map_win(win).cols_odd * 2 <= ox) x += MapAreaX;
    int32_t margin = (map_win(win).options & 0x80000000) ? 4 : 2;
    if (!map_win(win).pixel_x) {
        if (!wrap) {
            int32_t hi = MapAreaX - margin - 1;
            int32_t c = margin;
            if (margin <= x && margin <= hi) c = x > hi ? hi : x;
            x = c;
            if (x & 1) x--;
            if (y & 1) x++;
        }
        if (x <= ox + margin || right - margin <= x) changed = 1;
    }
    if (!map_win(win).pixel_y) {
        int32_t hi = MapAreaY - margin - 1;
        int32_t c = margin;
        if (margin <= y && margin <= hi) c = y > hi ? hi : y;
        y = c;
        if (x & 1) x--;
        if (y & 1) x++;
        int32_t oy = map_win(win).origin_y;
        if (y <= oy + margin || oy - 2 + map_win(win).rows_below * 2 - margin <= y) changed = 1;
    }
    if (!changed) return 0;
    if (!map_win(win).index) {
        console_win(&MainConsole).cursor_shown = MapWinInVehMode == 0;
        console_win(&MainConsole).cursor_on = 1;
    }
    pfncMoveMap(win, x, y, 1);
    return changed;
}
