// PlanWin (ui 0x48AE40..0x48BD80): the go-to list's map preview in the world map pane (planwin.cpp).
#pragma once
#include <cstdint>

#include "engine/memmap.h"
#include "ui/gfx.h"

// PlanWin_show (0x48BC60) / PlanWin_hide (0x48BC50): shown / hidden.
void planwin_open();
void planwin_close();
bool planwin_shown();
// PlanWin_clear_lines (0x48AF20) / PlanWin_add_line (0x48AE40): the route lines (at most four).
void planwin_clear_lines();
void planwin_add_line(int32_t x1, int32_t y1, int32_t x2, int32_t y2, int32_t color);
// PlanWin_draw_background (0x48B3F0): the view's terrain as the background.
void planwin_redraw_back();
// PlanWin_zoom_tile (0x48B8E0) / PlanWin_zoom_route (0x48B630): the zoom around a tile / a route
// (force: redraw the background).
void planwin_zoom_tile(int32_t x, int32_t y, int32_t force);
void planwin_zoom_route(int32_t x1, int32_t y1, int32_t x2, int32_t y2, int32_t force);
// PlanWin_center_between (0x48B450): centred between the middles of two pairs of tiles (the zoom kept).
void planwin_center_between(int32_t x1, int32_t y1, int32_t x2, int32_t y2, int32_t x3, int32_t y3, int32_t x4,
                            int32_t y4);
// PlanWin_add_base_line (0x48AE90): a line from base a to base b.
void planwin_add_base_line(int32_t a, int32_t b, int32_t color);
// PlanWin_redraw (0x48AF30): the canvas from the background, bases, end points and lines.
void planwin_draw();
// PlanWin_blink / line_timer while it is shown.
void planwin_tick(uint32_t now_ms);
// Its canvas into the main interface's world map pane.
void planwin_compose(Buffer* mi);
// Test aid (PLANSTATE): "planwin shown S zoom Z centre X,Y lines N (x1,y1)-(x2,y2) cC ..." on stderr.
void planwin_log_state();
