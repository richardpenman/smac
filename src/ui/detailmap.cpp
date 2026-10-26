// The world map pane's detail map (WorldWin_set_detail_map 0x4C3950 / _set_world_map 0x4C3990 /
// _show_all 0x4C39D0, reconstructed from the asm): the second globe button (0x3EF) hides the
// world map (WorldWin) and shows the second map window, MapWins[1] (mapwin_system_init's window
// over the pane: MapWin_init_2's rectangle 0x939F08, zoom -8, MapWin_clear's options 0), the
// first (0x3EE) the world map again; WorldWin +0x239CC keeps the choice. DiploPop's globe
// buttons (DiploPop_on_iface_group_clicked) do the same in the diplomacy mode.
// Approximated: the detail map takes the main view's display options (MapWin_clear leaves 0,
// which draws no unit; the original's MapWins[1] shows them) and is centred on the map cursor at each console redraw (the original's
// MapWins[1] follows the cursor through the map's focus calls).
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

std::vector<uint8_t> g_obj(0x22600);  // the MapWin object (GraphicWin virtual base at +0x21A6C)
Buffer g_canvas;
bool g_detail = false;  // WorldWin +0x239CC

uint8_t* dm() { return g_obj.data(); }

}  // namespace

bool worldwin_detail_shown() { return g_detail; }

// WorldWin_set_detail_map (0x4C3950).
void THISCALL WorldWin_set_detail_map(uint8_t*) {
    g_detail = true;
    std::fprintf(stderr, "worldwin detail map\n");
    ui_invalidate();
}

// WorldWin_set_world_map (0x4C3990).
void THISCALL WorldWin_set_world_map(uint8_t*) {
    if (g_detail) std::fprintf(stderr, "worldwin world map\n");
    g_detail = false;
    ui_invalidate();
}

// The pane: the detail map (MapWins[1] redrawn, centred on the cursor) or the world map.
void worldpane_draw(Buffer* mi) {
    if (!g_detail) {
        worldwin_draw();
        return;
    }
    UiRect pane = mi_rect(0xCFC);
    int32_t w = std::max(1, pane.right - pane.left), h = std::max(1, pane.bottom - pane.top);
    if (!ui_mapwin_native(dm()) || g_canvas.width != w || g_canvas.height != h) {
        Buffer_ctor(&g_canvas);
        Buffer_init(&g_canvas, w, h, 8, 0);
        ui_mapwin_attach(dm(), kMapWinVBase, &g_canvas);
        MapWin_clear(dm(), 1);
        mw(dm(), 0x1DD74) = 1;
        mw(dm(), kMwZoom) = -8;
    }
    if (MapWins[0]) mw(dm(), kMwOptions) = mw(MapWins[0], kMwOptions);
    mw(dm(), kMwCenterX) = MapWinCursorX[MapWinCursorIndex];
    mw(dm(), kMwCenterY) = MapWinCursorY[MapWinCursorIndex];
    pfncZoomProcessing(dm());
    pfncRedrawMap(dm(), 0);
    Buffer_copy_4(&g_canvas, mi, 0, 0, pane.left, pane.top, w, h);
}
