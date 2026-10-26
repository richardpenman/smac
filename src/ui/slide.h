// slide_show / slide_hide (ui 0x45D440 / 0x45D970) for the native windows (slide.cpp): the window
// slides up into view between the interface's arms, or down out of view, with its sound.
#pragma once
#include <cstdint>
#include <vector>

#include "ui/gfx.h"

// slide_show(window, 0): `win` (the window's canvas, at sx, sy on the screen) rises into view; the
// arms are then drawn into it (and into `back`, its background copy, when given).
void slide_show_window(Buffer* win, int32_t sx, int32_t sy, std::vector<uint8_t>* back = nullptr);
// slide_hide(window, 0): the window's picture `win` sinks out of view; call it once the window is
// hidden (the screen behind is presented first).
void slide_hide_window(const Buffer* win, int32_t sx, int32_t sy);
