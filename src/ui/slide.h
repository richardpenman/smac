// slide_show / slide_hide (ui 0x45D440 / 0x45D970) for the native windows (slide.cpp): the window
// slides up into view between the interface's arms, or down out of view, with its sound.
#pragma once
#include <cstdint>
#include <vector>

#include "ui/gfx.h"

// slide_show(window, 0): `win` (the window's canvas, at sx, sy on the screen) rises into view; the
// arms are then drawn into it (and into `back`, its background copy, when given).
// draw_arms false: the window draws its own arms (the screens of screenwin.h redraw their canvas).
void slide_show_window(Buffer* win, int32_t sx, int32_t sy, std::vector<uint8_t>* back = nullptr,
                       bool draw_arms = true);
// slide_hide(window, 0): the window's picture `win` sinks out of view; call it once the window is
// hidden (the screen behind is presented first).
void slide_hide_window(const Buffer* win, int32_t sx, int32_t sy);

// scroll_show (0x45DEA0): a window gets a scroll bar (a list popup, a report's list). With the
// interface animations, the console's outer right arm rises beside the window and the scroll rail
// (MainInterface_init's art: console_x2_A.pcx x 1, y 0x117..0x190) slides in; the rail and the arm
// end drawn into the window's canvas `win` (at sx, sy on the screen) at its right edge.
void scroll_show_window(Buffer* win, int32_t sx, int32_t sy);
// scroll_show's end state (the rail and the arm in the window), for windows that redraw their canvas.
void scroll_rail_draw(Buffer* win, int32_t sy);
// scroll_hide (0x45E540): the rail slides out and the arm sinks; the window's strip cleared and
// the arm drawn at its rest.
void scroll_hide_window(Buffer* win, int32_t sx, int32_t sy);
// The scroll bar's height for a window of height h (Popup_on_listbox_scroll_create's Scroll_init:
// the rail's caps and middle pieces, less 10); the bar is at (width - 0x2B, 0x15), 0x18 wide.
int32_t scroll_bar_height(int32_t h);
