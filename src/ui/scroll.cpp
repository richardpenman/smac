// Scroll bars: see scroll.h.
//
// Faithful to the asm: the sprites (load_icons 0x4547E0: ICONS.PCX, colours 6 and 7 made
// transparent; scroll_normal: 24 x 24 at (0x68 + 0x19 k, 1 + 0x19 row), the thumb 18 x 18 at
// (0x68, 0x65); scroll_small: 10 x 10 at (0xC0 + 0xB k, 1 + 0xB row), the thumb at (0xE1, 1);
// rows up, down, left, right, k the button's state), Scroll_ctor's defaults (no border, track
// colour 7, bevel 2 pixels 255 / 248, buttons on colour 247), Scroll_init's buttons (squares of
// the bar's width at its ends) and Scroll_compute_thumb_rect (the thumb from w + 1 to H - 2w).
// Only vertical bars are drawn (the lists'); the buttons' pressed and hover states are not.
// Colours 7, 247, 248 and 255 are Windows static colours in smacx's palette (light grey, grey,
// white) but show black in the original's screenshots (tests/placeholders/compare.sh, the
// datalinks' list): the bar draws them black (kBlack) until the palette's static entries are
// settled.
#include "ui/scroll.h"

#include <algorithm>

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

UiRect thumb(const ScrollBar& s) {
    int32_t w = s.r.right - s.r.left, h = s.r.bottom - s.r.top;
    int32_t off = w + 1;
    if (s.max != s.min) off += ((s.pos - s.min) * (h - 3 * w - 1)) / (s.max - s.min);
    return {s.r.left, s.r.top + off, s.r.left + w, s.r.top + off + w};
}

}  // namespace

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
        centred(c, &g_buttons[sm][k][0], b);
        bevel(c, b);
        Buffer_set_clip(c, &s.r);
    }
    Buffer_set_clip(c, &old);
}

int32_t scroll_click(const ScrollBar& s, int32_t x, int32_t y, int32_t page) {
    if (x < s.r.left || x >= s.r.right || y < s.r.top || y >= s.r.bottom) return 0;
    int32_t w = s.r.right - s.r.left;
    if (y < s.r.top + w) return -1;
    if (y >= s.r.bottom - w) return 1;
    UiRect t = thumb(s);
    if (y < t.top) return -page;
    if (y >= t.bottom) return page;
    return 0;
}
