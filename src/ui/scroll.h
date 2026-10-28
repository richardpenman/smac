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
    UiRect r{};  // the bar in its canvas
    bool small = false;  // scroll_small's sprites
    bool horizontal = false;  // a bar wider than high (the buttons left and right)
    int32_t min = 0, max = 0, pos = 0;  // Scroll_set_range / Scroll_set_pos
};

// The parts of a bar (Scroll_on_left_down: the buttons, the track above / below the thumb, the
// thumb).
enum ScrollPart : int32_t { SCROLL_NONE, SCROLL_UP, SCROLL_DOWN, SCROLL_PAGE_UP, SCROLL_PAGE_DOWN, SCROLL_THUMB };

// The list's scroll bar at the right of rectangle `list` (narrowed by the bar's width): first
// visible row `top` of `count` rows, `page` shown.
ScrollBar scroll_for_list(UiRect* list, bool small, int32_t top, int32_t count, int32_t page);
// Scroll_on_redraw with its two buttons.
void scroll_draw(Buffer* c, const ScrollBar& s);
// What a click at (x, y) does (Scroll_on_button_clicked / on_left_down): -1 / +1 a line up / down
// (the buttons), -page / +page above / below the thumb, 0 on the thumb or outside. After a
// scroll_press on the bar, only the part pressed counts (a button released elsewhere, or the thumb
// dragged, does nothing).
int32_t scroll_click(const ScrollBar& s, int32_t x, int32_t y, int32_t page);
// The thumb's rectangle (Scroll_compute_thumb_rect; while dragged, centred on the mouse).
UiRect scroll_thumb(const ScrollBar& s);
// The part of the bar at (x, y) (SCROLL_NONE outside).
int32_t scroll_part(const ScrollBar& s, int32_t x, int32_t y);

// The left button on a bar (one at a time: the mouse's capture, DAT_009B7ABC):
// scroll_press at the press (Scroll_on_left_down, BaseButton_on_left_down) holds the part under
// (x, y) and returns it (SCROLL_NONE: not on the bar, nothing held). While held, a button is drawn
// pressed (FlatButton_on_redraw's third sprite) as long as the mouse is over it, and the thumb
// follows the mouse: scroll_motion at each move (Scroll_on_mouse_move) returns true when the bar
// must be redrawn and, the thumb dragged, sets *pos to its new position. scroll_release at the
// release (Scroll_on_left_up) ends the hold and returns the part that was held.
int32_t scroll_press(const ScrollBar& s, int32_t x, int32_t y);
bool scroll_motion(const ScrollBar& s, int32_t x, int32_t y, int32_t* pos);
int32_t scroll_release();
// The part held (SCROLL_NONE when the button is up).
int32_t scroll_held();
