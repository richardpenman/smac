// Scroll bars: see scroll.h.
//
// Faithful to the asm: the sprites (load_icons 0x4547E0: ICONS.PCX, colours 6 and 7 made
// transparent; scroll_normal: 24 x 24 at (0x68 + 0x19 k, 1 + 0x19 row), the thumb 18 x 18 at
// (0x68, 0x65); scroll_small: 10 x 10 at (0xC0 + 0xB k, 1 + 0xB row), the thumb at (0xE1, 1);
// rows up, down, left, right, k the button's state), Scroll_ctor's defaults (no border, track
// colour 7, bevel 2 pixels 255 / 248, buttons on colour 247), Scroll_init's buttons (squares of
// the bar's width at its ends) and Scroll_compute_thumb_rect (the thumb from w + 1 to H - 2w).
// Only vertical bars are drawn (the lists'); a held button is drawn pressed (its third sprite), the
// thumb dragged (scroll_press / scroll_motion / scroll_release). Not drawn: the buttons' hover
// sprite (the second); not done: the repeat while a button or the track is held (BaseButton's
// timer, Scroll_on_button_pressed), the dragged thumb kept where it was dropped (+0xA3C: here it
// goes back to the position's place on release).
// Colours 7, 247, 248 and 255 are Windows static colours in OpenSMAC's palette (light grey, grey,
// white) but show black in the original's screenshots (tests/placeholders/compare.sh, the
// datalinks' list): the bar draws them black (kBlack) until the palette's static entries are
// settled.
#include "ui/scroll.h"

#include <algorithm>
#include <cstdio>

#include "ui/basewin_internal.h"
#include "ui/gfx.h"
#include "ui/sprites.h"

namespace {

Sprite g_buttons[2][4][3];  // [small][row][state]
Sprite g_thumb[2];
bool g_loaded = false;

void load() {
    if (g_loaded) return;
    g_loaded = true;
    Buffer art;
    Buffer_ctor(&art);
    if (Buffer_load_pcx(&art, "ICONS.PCX", nullptr, 10, 0xEC)) return;
    Buffer_change_color(&art, 0x107, 0x109);
    Buffer_change_color(&art, 0x106, 0x109);
    for (int32_t row = 0; row < 4; row++)
        for (int32_t k = 0; k < 3; k++) {
            pfncSpriteFromCanvasRectTrans(&g_buttons[0][row][k], &art, 0x109, 0x68 + k * 0x19, 1 + row * 0x19, 0x18,
                                          0x18, 0);
            pfncSpriteFromCanvasRectTrans(&g_buttons[1][row][k], &art, 0x109, 0xC0 + k * 0xB, 1 + row * 0xB, 10, 10, 0);
        }
    pfncSpriteFromCanvasRectTrans(&g_thumb[0], &art, 0x109, 0x68, 0x65, 0x12, 0x12, 0);
    pfncSpriteFromCanvasRectTrans(&g_thumb[1], &art, 0x109, 0xE1, 1, 10, 10, 0);
}

constexpr int32_t kBlack = 0;

void fill(Buffer* c, const UiRect& r, int32_t color) {
    UiRect rr = r;
    (void)color;
    Buffer_fill_3(c, &rr, kBlack);
}

void bevel(Buffer* c, UiRect r) {
    for (int32_t i = 0; i < 2; i++) {
        Buffer_box_2(c, &r, kBlack, kBlack);  // (255, 248)
        r.left++;
        r.top++;
        r.right--;
        r.bottom--;
    }
}

void centred(Buffer* c, Sprite* s, const UiRect& r) {
    if (!s->width) return;
    sprite_copy1(s, c, s->key, ((r.right - s->full_width) - r.left) / 2 + r.left,
                 ((r.bottom - s->full_height) - r.top) / 2 + r.top, 1, 1);
}

// The hold of the left button on a bar (the bar known by its rectangle: one bar is held at a time).
struct Held {
    bool active = false;  // the button is down
    bool fresh = false;  // pressed since the last scroll_click on the bar
    UiRect r{};
    int32_t part = SCROLL_NONE;
    bool over = false;  // the mouse over the held button
    int32_t drag = -1;  // Scroll +0xA3C: the dragged thumb's offset in the bar (-1: from the position)
};
Held g_held;

bool same(const UiRect& a, const UiRect& b) {
    return a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom;
}
bool held_here(const ScrollBar& s) { return g_held.active && same(g_held.r, s.r); }

// The bar's thickness (w) and length (h) whichever way it lies, and a point's coordinate along it.
int32_t thick(const ScrollBar& s) { return s.horizontal ? s.r.bottom - s.r.top : s.r.right - s.r.left; }
int32_t length(const ScrollBar& s) { return s.horizontal ? s.r.right - s.r.left : s.r.bottom - s.r.top; }
int32_t along(const ScrollBar& s, int32_t x, int32_t y) { return s.horizontal ? x - s.r.left : y - s.r.top; }
UiRect span(const ScrollBar& s, int32_t a, int32_t b) {
    return s.horizontal ? UiRect{s.r.left + a, s.r.top, s.r.left + b, s.r.bottom}
                        : UiRect{s.r.left, s.r.top + a, s.r.right, s.r.top + b};
}

// Scroll_compute_thumb_rect (no border, buttons at both ends): the thumb, a square of the bar's
// thickness, w + 1 + (pos - min) * (h - 3w - 1) / (max - min) from the bar's start; while dragged
// (+0xA3C set by Scroll_on_mouse_move) at the mouse less half a thumb, kept between the buttons
// (w .. h - 2w).
UiRect thumb(const ScrollBar& s) {
    int32_t w = thick(s), h = length(s);
    int32_t off = w + 1;
    if (held_here(s) && g_held.part == SCROLL_THUMB && g_held.drag != -1) {
        off = std::max(w, std::min(g_held.drag, h - 2 * w));
    } else if (s.max != s.min) {
        off += ((s.pos - s.min) * (h - 3 * w - 1)) / (s.max - s.min);
    }
    return span(s, off, off + w);
}

}  // namespace

UiRect scroll_thumb(const ScrollBar& s) { return thumb(s); }

int32_t scroll_part(const ScrollBar& s, int32_t x, int32_t y) {
    if (x < s.r.left || x >= s.r.right || y < s.r.top || y >= s.r.bottom) return SCROLL_NONE;
    int32_t w = thick(s), a = along(s, x, y);
    if (a < w) return SCROLL_UP;
    if (a >= length(s) - w) return SCROLL_DOWN;
    UiRect t = thumb(s);
    int32_t t0 = s.horizontal ? t.left - s.r.left : t.top - s.r.top;
    if (a < t0) return SCROLL_PAGE_UP;
    if (a >= t0 + w) return SCROLL_PAGE_DOWN;
    return SCROLL_THUMB;
}

int32_t scroll_press(const ScrollBar& s, int32_t x, int32_t y) {
    g_held = Held{};
    int32_t part = scroll_part(s, x, y);
    if (part == SCROLL_NONE) return part;
    g_held.active = g_held.fresh = true;
    g_held.r = s.r;
    g_held.part = part;
    g_held.over = true;
    std::fprintf(stderr, "scroll press part %d (bar %d,%d..%d,%d pos %d of %d..%d)\n", part, s.r.left, s.r.top, s.r.right,
                 s.r.bottom, s.pos, s.min, s.max);
    return part;
}

bool scroll_motion(const ScrollBar& s, int32_t x, int32_t y, int32_t* pos) {
    if (!held_here(s)) return false;
    bool in = x >= s.r.left && x < s.r.right && y >= s.r.top && y < s.r.bottom;
    if (g_held.part == SCROLL_UP || g_held.part == SCROLL_DOWN) {
        // BaseButton_on_mouse_move: the button pressed while the mouse is over it.
        bool over = scroll_part(s, x, y) == g_held.part;
        if (over == g_held.over) return false;
        g_held.over = over;
        return true;
    }
    if (g_held.part != SCROLL_THUMB || !in) return false;  // (in_box_2: moves off the bar ignored)
    // Scroll_on_mouse_move, the thumb held: the mouse less a button's length (+0xA3C), the position
    // (a - w - w / 2) * (max - min) / (h - 3w) + min, kept in the range.
    int32_t w = thick(s), h = length(s);
    int32_t a = along(s, x, y) - w;
    g_held.drag = a + (w >> 1);
    int32_t p = s.min;
    if (h - 3 * w != 0) p = ((a - (w >> 1)) * (s.max - s.min)) / (h - 3 * w) + s.min;
    p = std::max(s.min, std::min(p, s.max));
    *pos = p;
    return true;
}

int32_t scroll_release() {
    int32_t part = g_held.active ? g_held.part : SCROLL_NONE;
    if (part != SCROLL_NONE) std::fprintf(stderr, "scroll release part %d\n", part);
    g_held.active = false;
    g_held.drag = -1;
    return part;
}

int32_t scroll_held() { return g_held.active ? g_held.part : SCROLL_NONE; }

ScrollBar scroll_for_list(UiRect* list, bool small, int32_t top, int32_t count, int32_t page) {
    ScrollBar s;
    s.small = small;
    int32_t w = small ? kScrollSmall : kScrollNormal;
    s.r = {list->right - w, list->top, list->right, list->bottom};
    list->right -= w;
    s.min = 0;
    s.max = std::max(0, count - page);
    s.pos = std::max(0, std::min(top, s.max));
    return s;
}

void scroll_draw(Buffer* c, const ScrollBar& s) {
    load();
    int32_t w = s.r.right - s.r.left;
    int32_t sm = s.small ? 1 : 0;
    UiRect old = c->clip;
    Buffer_set_clip(c, &s.r);
    fill(c, s.r, 7);
    UiRect t = thumb(s);
    fill(c, t, 7);
    centred(c, &g_thumb[sm], t);
    bevel(c, t);
    UiRect up{s.r.left, s.r.top, s.r.left + w, s.r.top + w};
    UiRect down{s.r.left, s.r.bottom - w, s.r.left + w, s.r.bottom};
    for (int32_t k = 0; k < 2; k++) {
        const UiRect& b = k ? down : up;
        fill(c, b, 247);
        Buffer_set_clip(c, &b);
        // FlatButton_on_redraw: the third sprite while pressed.
        bool pressed = held_here(s) && g_held.over && g_held.part == (k ? SCROLL_DOWN : SCROLL_UP);
        centred(c, &g_buttons[sm][k][pressed ? 2 : 0], b);
        bevel(c, b);
        Buffer_set_clip(c, &s.r);
    }
    Buffer_set_clip(c, &old);
}

int32_t scroll_click(const ScrollBar& s, int32_t x, int32_t y, int32_t page) {
    int32_t part = scroll_part(s, x, y);
    if (g_held.fresh && same(g_held.r, s.r)) {
        // The part pressed (the press's capture): released elsewhere, or the thumb, nothing.
        g_held.fresh = false;
        if (part != g_held.part) return 0;
    }
    switch (part) {
    case SCROLL_UP: return -1;
    case SCROLL_DOWN: return 1;
    case SCROLL_PAGE_UP: return -page;
    case SCROLL_PAGE_DOWN: return page;
    default: return 0;
    }
}
