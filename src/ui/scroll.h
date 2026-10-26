// Scroll bars (the Scroll class 0x6051D0..0x606EA0 and its users' scroll_normal / scroll_small):
// scroll.cpp. A vertical bar: the up and down buttons (squares of the bar's width: a sprite of
// ICONS.PCX on colour 247 with a two-pixel bevel 255 / 248), the track (colour 7) and the thumb
// (a square of the bar's width: its sprite, the same bevel), placed by Scroll_compute_thumb_rect.
#pragma once
#include <cstdint>

#include "ui/gfx.h"

// DAT_009B8DD4: the width of the bars scroll_normal (20) / scroll_small (12) give.
constexpr int32_t kScrollNormal = 0x14, kScrollSmall = 0xC;

struct ScrollBar {
    UiRect r{};           // the bar in its canvas
    bool small = false;   // scroll_small's sprites
    int32_t min = 0, max = 0, pos = 0;  // Scroll_set_range / Scroll_set_pos
};

// The list's scroll bar at the right of rectangle `list` (narrowed by the bar's width): first
// visible row `top` of `count` rows, `page` shown.
ScrollBar scroll_for_list(UiRect* list, bool small, int32_t top, int32_t count, int32_t page);
// Scroll_on_redraw with its two buttons.
void scroll_draw(Buffer* c, const ScrollBar& s);
// What a click at (x, y) does (Scroll_on_button_clicked / on_left_down): -1 / +1 a line up / down
// (the buttons), -page / +page above / below the thumb, 0 on the thumb or outside.
int32_t scroll_click(const ScrollBar& s, int32_t x, int32_t y, int32_t page);
