// The world map pane's detail map (WorldWin_set_detail_map 0x4C3950 / _set_world_map 0x4C3990 /
// _show_all 0x4C39D0, reconstructed from the asm): the second globe button (0x3EF) hides the
// world map (WorldWin) and shows the second map window, MapWins[1], the first (0x3EE) the world
// map again; WorldWin +0x239CC keeps the choice. DiploPop's globe buttons
// (DiploPop_on_iface_group_clicked) do the same in the diplomacy mode.
// MapWins[1] is set up by desktop_init's mapwin_system_init (0x471030; detailmap_system_init
// here): MapWin_init_2(0, 0) clears it (MapWin_init's MapWin_clear: the map's middle), makes it
// active and gives it its own display options, 0x40010EE3 (0x10EE3 with 0x40000000 for window 1,
// and 0x8C0), zoom -8, its window over the pane (the rectangle 0x939F08). It is a map window of its
// own: it moves only where the original moves it -- Console_focus's MapWin_focus for the focus
// calls whose bits it has (0x40000000: another faction's events, or every faction's), the world
// map pane's shift click and its menu's item 4 (worldwin_show_detail_at: pfncMoveMap to the main
// view's centre) -- and the loops over the active map windows (the patrol routes, the menu's
// option clearing, the tiles redrawn, moves and explosions) include it.
#include <algorithm>
#include <cstdio>
#include <vector>

#include "engine/globals.h"
#include "engine/ui_bridge.h"
#include "ui/basewin.h"
#include "ui/console.h"
#include "ui/mapwin.h"
#include "ui/console_frame.h"
#include "ui/win.h"

namespace {

Buffer g_canvas;
bool g_detail = false;  // WorldWin +0x239CC

uint8_t* dm() { return MapWins[1]; }

// The window's canvas: the pane's size (MapWin_init_2's rectangle 0x939F08).
bool attach() {
    uint8_t* w = dm();
    if (!w) return false;
    UiRect pane = main_interface().world_map;
    int32_t pw = std::max(1, pane.right - pane.left), ph = std::max(1, pane.bottom - pane.top);
    if (!ui_mapwin_native(w) || g_canvas.width != pw || g_canvas.height != ph) {
        Buffer_ctor(&g_canvas);
        Buffer_init(&g_canvas, pw, ph, 8, 0);
        ui_mapwin_attach(w, kMapWinVBase, &g_canvas);
    }
    return true;
}

}  // namespace

// mapwin_system_init's part for MapWins[1] (0x471030 -> MapWin_init_2(0, 0) 0x470A90).
void detailmap_system_init(int32_t reset) {
    uint8_t* w = dm();
    if (!w) return;
    // (A loaded game keeps the window's saved state, active in the original's saves; OpenSMAC's
    // earlier saves hold zeros there: the window is made active as for a new game.)
    (void)reset;
    if (!attach()) return;
    MapWin_clear(w, 1);  // (MapWin_init)
    map_win(w).active = 1;
    map_win(w).zoom = -8;
    map_win(w).options = 0x40010EE3;
}

bool worldwin_detail_shown() { return g_detail; }

// WorldWin_set_detail_map (0x4C3950).
void THISCALL WorldWin_set_detail_map(uint8_t*) {
    g_detail = true;
    std::fprintf(stderr, "worldwin detail map\n");
    ui_invalidate();
}

void worldwin_show_detail_at(int32_t x, int32_t y) {
    std::fprintf(stderr, "worldwin detail map at %d,%d\n", x, y);
    if (dm() && attach()) pfncMoveMap(dm(), x, y, 1);
    WorldWin_set_detail_map(&WorldWin);
}

// WorldWin_set_world_map (0x4C3990).
void THISCALL WorldWin_set_world_map(uint8_t*) {
    if (g_detail) std::fprintf(stderr, "worldwin world map\n");
    g_detail = false;
    ui_invalidate();
}

// The pane: the detail map (MapWins[1] redrawn where it is) or the world map.
void worldpane_draw(Buffer* mi) {
    if (!g_detail || !dm() || !attach()) {
        worldwin_draw();
        return;
    }
    UiRect pane = main_interface().world_map;
    pfncZoomProcessing(dm());
    pfncRedrawMap(dm(), 0);
    Buffer_copy_4(&g_canvas, mi, 0, 0, pane.left, pane.top, g_canvas.width, g_canvas.height);
}

// Test aid (INFOSTATE): the detail map's state.
void detailmap_log_state() {
    if (!dm()) return;
    const MapWin& m = map_win(dm());
    std::fprintf(stderr, "detailmap shown %d active %d options %08X zoom %d centre %d,%d\n", g_detail ? 1 : 0, m.active,
                 (uint32_t)m.options, m.zoom, m.center_x, m.center_y);
}
