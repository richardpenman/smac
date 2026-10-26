// Script-driven popups of the original UI (see popup.h).
#include "ui/popup.h"
#include "ui/labs.h"
#include "ui/sound.h"
#include "ui/sprites.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>

#include "engine/crt.h"
#include "engine/globals.h"
#include "engine/parse.h"
#include "engine/prefs.h"
#include "engine/rules.h"
#include "engine/strings.h"
#include "engine/text.h"
#include "engine/ui_bridge.h"
#include "platform/ui_platform.h"
#include "engine/game.h"
#include "ui/win.h"
#include "ui/infowin.h"

namespace {

// ---- art ---------------------------------------------------------------------------------------

constexpr int32_t kKey = 9;  // transparent index (DAT_00696D14)

BoxDef g_boxes[51];
// The larger frame of tutorial / interface-window popups (0x6846B0; interface.pcx at 0x128..0x1E1,
// 0x2C7..0x34D: 61x44 corners and edges around a 61x44 centre).
BoxDef g_box_large;
Sprite g_box_large_sprites[8];
Buffer g_box_large_center;
Sprite g_box_sprites[51][8];
Buffer g_box_centers[51];
bool g_box_center_used[51];

// interface.pcx button bars (load_interface): 9 rows of {left cap, middle, right cap}; popup
// buttons use rows 6..8 (normal, highlighted, pressed).
Sprite g_bars[9][3];
// interface.pcx check box marks (0x7794D8, 3 states).
Sprite g_checks[3];

// Text colour pairs {colour, shadow} of text.pcx at (0x232, 0xB0 + 16i) / (0x23C, ..): 0x8C6C6C.
int32_t g_popup_text_colors[7][2];
// iface.pcx colours sampled at (0x3A3, 2 + 11i) (0x68A5A0..0x68A5C4).
int32_t g_iface_colors[10];

// Fonts of config_popups: sizes x styles (0x93FC58 + size * 0xA0 + style * 0x28).
const int32_t kFontSizes[12] = {40, 36, 32, 24, 22, 20, 18, 16, 14, 12, 10, 8};
const int32_t kFontStyles[4] = {0, 1, 2, 4};
Font g_fonts[12][4];
Font g_caption_font;  // 0x7CFE8C
bool g_art_ok = false;

int32_t closest_size(int32_t size) {
    int32_t best = 0, dist = 9999;
    for (int32_t i = 0; i < 12; i++) {
        int32_t d = std::abs(kFontSizes[i] - size);
        if (d < dist) {
            dist = d;
            best = i;
        }
    }
    return best;
}

void load_pcx(Buffer* canvas, const char* file) {
    Buffer_ctor(canvas);
    Buffer_load_pcx(canvas, file, nullptr, 10, 0xEC);
}

// load_interface's extraction: colours 0x106/0x107 (raw 252, 253: the cell outlines) become the
// transparent key first.
void extract(Sprite* s, Buffer* canvas, int32_t x, int32_t y, int32_t w, int32_t h) {
    Buffer_change_color(canvas, 0x107, 0x109);
    Buffer_change_color(canvas, 0x106, 0x109);
    pfncSpriteFromCanvasRectTrans(s, canvas, 0x109, x, y, w, h, 0);
}

// pfncSpriteStretchCopyToCanvas1 (0x5E4B4A): the sprite drawn at scale dest/source.
void sprite_copy(Sprite* s, Buffer* dest, int32_t x, int32_t y, int32_t ds = 1, int32_t ss = 1) {
    if (!s || !s->width) return;
    int32_t od = BlitDestScale, os = BlitSourceScale;
    BlitDestScale = ds;
    BlitSourceScale = ss;
    pfncSpriteStretchCopyToCanvas(s, dest, s->key, x, y);
    BlitDestScale = od;
    BlitSourceScale = os;
}

bool intersect(UiRect* out, const UiRect& a, const UiRect& b) {
    out->left = std::max(a.left, b.left);
    out->top = std::max(a.top, b.top);
    out->right = std::min(a.right, b.right);
    out->bottom = std::min(a.bottom, b.bottom);
    return out->left < out->right && out->top < out->bottom;
}

}  // namespace

BoxDef* box_def(int32_t index) { return (index >= 0 && index < 51) ? &g_boxes[index] : nullptr; }
BoxDef* box_def_large() { return &g_box_large; }

void GAMEAPI load_box_sprites() {
    Buffer canvas;
    load_pcx(&canvas, "interface.pcx");
    // (The first sprites of the original, 0x7AC040.. and 0x788038.., are not popup art.)
    int32_t n = 0;
    for (int32_t top = 0xE5; n < 51; top += 0x27) {
        for (int32_t x = 0x21; x < 0x96 && n < 51; x += 0x27, n++) {
            Sprite* s = g_box_sprites[n];
            int32_t bottom = top + 0x20;
            pfncSpriteFromCanvasRectTrans(&s[0], &canvas, kKey, x - 0x20, top, 6, 6, 0);
            pfncSpriteFromCanvasRectTrans(&s[1], &canvas, kKey, x, top, 6, 6, 0);
            pfncSpriteFromCanvasRectTrans(&s[2], &canvas, kKey, x - 0x20, bottom, 6, 6, 0);
            pfncSpriteFromCanvasRectTrans(&s[3], &canvas, kKey, x, bottom, 6, 6, 0);
            pfncSpriteFromCanvasRectTrans(&s[4], &canvas, kKey, x - 0x19, top, 0x18, 6, 0);
            pfncSpriteFromCanvasRectTrans(&s[5], &canvas, kKey, x - 0x19, bottom, 0x18, 6, 0);
            pfncSpriteFromCanvasRectTrans(&s[6], &canvas, kKey, x - 0x20, top + 7, 6, 0x18, 0);
            pfncSpriteFromCanvasRectTrans(&s[7], &canvas, kKey, x, top + 7, 6, 0x18, 0);
            Buffer* c = &g_box_centers[n];
            Buffer_ctor(c);
            Buffer_init(c, 0x18, 0x18, 0, 0);
            for (int32_t j = 0; j < 0x18; j++)
                for (int32_t i = 0; i < 0x18; i++)
                    c->data[(size_t)j * c->stride + i] = (uint8_t)Buffer_get_pixel(&canvas, x - 0x19 + i, top + 7 + j);
            g_box_center_used[n] = Buffer_get_pixel(c, 0, 0) != kKey;
            BoxDef& b = g_boxes[n];
            for (int32_t i = 0; i < 4; i++) {
                b.corner[i] = s[i].width ? &s[i] : nullptr;
                b.edge[i] = s[4 + i].width ? &s[4 + i] : nullptr;
            }
            b.center = g_box_center_used[n] ? c : nullptr;
            b.key = 7;
        }
    }
    // The large frame (rest of load_box_sprites): TL, top, TR / left, right / BL, bottom, BR.
    static const int32_t kAt[8][2] = {{0x128, 0x2C7}, {0x166, 0x2C7}, {0x1A4, 0x2C7}, {0x128, 0x2F4},
                                      {0x1A4, 0x2F4}, {0x128, 0x321}, {0x166, 0x321}, {0x1A4, 0x321}};
    for (int32_t i = 0; i < 8; i++)
        pfncSpriteFromCanvasRectTrans(&g_box_large_sprites[i], &canvas, kKey, kAt[i][0], kAt[i][1], 0x3D, 0x2C, 0);
    Buffer_ctor(&g_box_large_center);
    Buffer_init(&g_box_large_center, 0x3D, 0x2C, 0, 0);
    for (int32_t j = 0; j < 0x2C; j++)
        for (int32_t i = 0; i < 0x3D; i++)
            g_box_large_center.data[(size_t)j * g_box_large_center.stride + i] =
                (uint8_t)Buffer_get_pixel(&canvas, 0x166 + i, 0x2F4 + j);
    Sprite* l = g_box_large_sprites;
    g_box_large = {{&l[0], &l[2], &l[5], &l[7]}, {&l[1], &l[6], &l[3], &l[4]}, &g_box_large_center, 7};
    // The tree's arrows (0x75AFD0 [3]: plain, -, highlighted; 7 x 13 at (0x8C + 8k, 0x25)).
    labs_load_arrows(&canvas);
}

int32_t THISCALL Buffer_tile_2(Buffer* self, Sprite* tile, int32_t sx, int32_t sy, int32_t x, int32_t y, int32_t w,
                               int32_t h) {
    if (!self->data) return 7;
    UiRect old = self->clip, r = {x, y, x + w, y + h}, c;
    if (!intersect(&c, r, old)) return 0;
    Buffer_set_clip(self, &c);
    int32_t tw = tile->full_width, th = tile->full_height;
    if (tw && th) {
        int32_t nx = w / tw + (w % tw ? 1 : 0), ny = h / th + (h % th ? 1 : 0);
        int32_t x0 = x - sx % tw, y0 = y - sy % th;
        if (nx * tw + x0 < x + w) nx++;
        if (ny * th + y0 < y + h) ny++;
        for (int32_t j = 0, py = y0; j < ny; j++, py += th)
            for (int32_t i = 0, px = x0; i < nx; i++, px += tw) sprite_copy(tile, self, px, py);
    }
    Buffer_set_clip(self, &old);
    return 0;
}

namespace {
// Buffer_copy_4 / Buffer_copy_mask restricted to the clip rectangle: src (sx, sy, w, h) to (x, y);
// mask >= 0: only over destination pixels of that colour.
void copy_rect(Buffer* self, Buffer* src, int32_t sx, int32_t sy, int32_t x, int32_t y, int32_t w, int32_t h,
               int32_t mask) {
    for (int32_t j = 0; j < h; j++) {
        int32_t dy = y + j, syy = sy + j;
        if (dy < self->clip.top || dy >= self->clip.bottom || dy < 0 || dy >= self->height) continue;
        if (syy < 0 || syy >= src->height) continue;
        for (int32_t i = 0; i < w; i++) {
            int32_t dx = x + i, sxx = sx + i;
            if (dx < self->clip.left || dx >= self->clip.right || dx < 0 || dx >= self->width) continue;
            if (sxx < 0 || sxx >= src->width) continue;
            uint8_t* d = self->data + (size_t)dy * self->stride + dx;
            if (mask >= 0 && *d != (uint8_t)mask) continue;
            *d = src->data[(size_t)syy * src->stride + sxx];
        }
    }
}

int32_t tile_buffer(Buffer* self, Buffer* tile, int32_t sx, int32_t sy, int32_t x, int32_t y, int32_t w, int32_t h,
                    int32_t mask) {
    if (!self->data) return 7;
    UiRect old = self->clip, r = {x, y, x + w, y + h}, c;
    if (!intersect(&c, r, old)) return 0;
    Buffer_set_clip(self, &c);
    int32_t tw = tile->width, th = tile->height;
    if (tw && th) {
        int32_t nx = w / tw + (w % tw ? 1 : 0), ny = h / th + (h % th ? 1 : 0);
        int32_t x0 = x - sx % tw, y0 = y - sy % th;
        if (nx * tw + x0 < x + w) nx++;
        if (ny * th + y0 < y + h) ny++;
        for (int32_t j = 0, py = y0; j < ny; j++, py += th)
            for (int32_t i = 0, px = x0; i < nx; i++, px += tw) copy_rect(self, tile, 0, 0, px, py, tw, th, mask);
    }
    Buffer_set_clip(self, &old);
    return 0;
}
}  // namespace

int32_t THISCALL Buffer_tile(Buffer* self, Buffer* tile, int32_t sx, int32_t sy, int32_t x, int32_t y, int32_t w,
                             int32_t h) {
    return tile_buffer(self, tile, sx, sy, x, y, w, h, -1);
}

int32_t THISCALL Buffer_tile_mask(Buffer* self, Buffer* tile, int32_t sx, int32_t sy, int32_t x, int32_t y, int32_t w,
                                  int32_t h, int32_t mask) {
    return tile_buffer(self, tile, sx, sy, x, y, w, h, mask);
}

void THISCALL Buffer_box_sprite(Buffer* self, const UiRect* r, const BoxDef* b) {
    if (!r || !b) return;
    auto corner_mask = [&](int32_t x, int32_t y, Sprite* s) {
        if (b->center) Buffer_tile_mask(self, b->center, x, y, x, y, s->full_width, s->full_height, b->key);
    };
    Sprite* const* c = b->corner;
    Sprite* const* e = b->edge;
    if (c[0]) {
        sprite_copy(c[0], self, r->left, r->top);
        corner_mask(r->left, r->top, c[0]);
    }
    if (c[1]) {
        int32_t x = r->right - c[1]->full_width;
        sprite_copy(c[1], self, x, r->top);
        corner_mask(x, r->top, c[1]);
    }
    if (c[2]) {
        int32_t y = r->bottom - c[2]->full_height;
        sprite_copy(c[2], self, r->left, y);
        corner_mask(r->left, y, c[2]);
    }
    if (c[3]) {
        int32_t x = r->right - c[3]->full_width, y = r->bottom - c[3]->full_height;
        sprite_copy(c[3], self, x, y);
        corner_mask(x, y, c[3]);
    }
    if (e[0]) {  // top
        int32_t x = r->left, w = r->right - r->left, h = e[0]->full_height;
        if (c[0]) {
            x += c[0]->full_width;
            w -= c[0]->full_width;
        }
        if (c[1]) w -= c[1]->full_width;
        Buffer_tile_2(self, e[0], 0, 0, x, r->top, w, h);
        if (b->center) Buffer_tile_mask(self, b->center, x, r->top, x, r->top, w, h, b->key);
    }
    if (e[1]) {  // bottom
        int32_t h = e[1]->full_height, x = r->left, y = r->bottom - h, w = r->right - r->left;
        if (c[2]) {
            w -= c[2]->full_width;
            x += c[2]->full_width;
        }
        if (c[3]) w -= c[3]->full_width;
        Buffer_tile_2(self, e[1], 0, 0, x, y, w, h);
        if (b->center) Buffer_tile_mask(self, b->center, x, y, x, y, w, h, b->key);
    }
    if (e[2]) {  // left
        int32_t w = e[2]->full_width, y = r->top, h = r->bottom - r->top;
        if (c[0]) {
            y += c[0]->full_height;
            h -= c[0]->full_height;
        }
        if (c[2]) h -= c[2]->full_height;
        Buffer_tile_2(self, e[2], 0, 0, r->left, y, w, h);
        if (b->center) Buffer_tile_mask(self, b->center, r->left, y, r->left, y, w, h, b->key);
    }
    if (e[3]) {  // right
        int32_t w = e[3]->full_width, x = r->right - w, y = r->top, h = r->bottom - r->top;
        if (c[1]) {
            y += c[1]->full_height;
            h -= c[1]->full_height;
        }
        if (c[3]) h -= c[3]->full_height;
        Buffer_tile_2(self, e[3], 0, 0, x, y, w, h);
        if (b->center) Buffer_tile_mask(self, b->center, x, y, x, y, w, h, b->key);
    }
    if (b->center) {
        int32_t x = r->left, y = r->top, w = r->right - r->left, h = r->bottom - r->top;
        if (e[2]) {
            x += e[2]->full_width;
            w -= e[2]->full_width;
        }
        if (e[0]) {
            y += e[0]->full_height;
            h -= e[0]->full_height;
        }
        if (e[3]) w -= e[3]->full_width;
        if (e[1]) h -= e[1]->full_height;
        Buffer_tile(self, b->center, x, y, x, y, w, h);
    }
}

static int32_t g_main_font = 7, g_button_font = 7;  // config_popups' font sizes (indices of kFontSizes)

int32_t g_iface_color(int32_t i) {
    popup_art_init();
    return g_iface_colors[i];
}

void popup_art_init() {
    if (g_art_ok) return;
    g_art_ok = true;
    // Loading art and reading preferences use StrBuffer, which callers may be passing as a label.
    std::string saved(StrBuffer);
    int32_t main = prefs_get_2("MainFontSize", 16, 0);
    if (ScreenWidth == 800) main -= 2;
    g_main_font = closest_size(main);
    g_button_font = closest_size(16);
    // iface.pcx colours (load_interface).
    Buffer iface;
    load_pcx(&iface, "iface.pcx");
    for (int32_t i = 0; i < 10; i++) g_iface_colors[i] = Buffer_get_pixel(&iface, 0x3A3, 2 + 11 * i);
    // interface.pcx: button bars and check marks (load_interface), box sprites.
    Buffer canvas;
    load_pcx(&canvas, "interface.pcx");
    for (int32_t row = 0, y = 1; row < 9; row++, y += 0x15) {
        extract(&g_bars[row][0], &canvas, 0x40, y, 3, 0x14);
        extract(&g_bars[row][1], &canvas, 0x44, y, 0x43, 0x14);
        extract(&g_bars[row][2], &canvas, 0x88, y, 3, 0x14);
        // The same sprites at their original addresses (0x77A5C8 + row * 0x84: FlatButton_ctor's
        // default look, the base window's and the screens' buttons).
        for (int32_t part = 0; part < 3; part++)
            *ArtSprite(0x77A5C8 + (uint32_t)row * 0x84 + (uint32_t)part * 0x2C) = g_bars[row][part];
    }
    for (int32_t i = 0, y = 1; i < 3; i++, y += 0x13) extract(&g_checks[i], &canvas, 0xE3, y, 0x12, 0x12);
    load_box_sprites();
    // text.pcx colours (textcolor_init).
    Buffer text;
    load_pcx(&text, "text.pcx");
    for (int32_t i = 0; i < 7; i++) {
        g_popup_text_colors[i][0] = Buffer_get_pixel(&text, 0x232, 0xB0 + 16 * i);
        int32_t s = Buffer_get_pixel(&text, 0x23C, 0xB0 + 16 * i);
        g_popup_text_colors[i][1] = s == kKey ? -1 : s;
    }
    // Fonts (config_popups): Arial Narrow from 13 pixels up, bold from 13 up.
    for (int32_t i = 0; i < 12; i++) {
        for (int32_t s = 0; s < 4; s++) {
            Font_ctor(&g_fonts[i][s]);
            int32_t size = kFontSizes[i];
            Font_init(&g_fonts[i][s], size < 13 ? "Arial" : "Arial Narrow", size, (size > 12 ? 1 : 0) | kFontStyles[s]);
        }
    }
    // The caption font: MainInterface's font at +0x2166C (0x7AE820 + 0x2166C = 0x7CFE8C), Arial 14
    // bold (MainInterface_init).
    Font_ctor(&g_caption_font);
    Font_init(&g_caption_font, "Arial", 14, 1);
    crt_strcpy(StrBuffer, saved.c_str());
}

// ==== BasePop ====================================================================================

class BasePop {
public:
    // ---- script contents ----
    std::vector<std::unique_ptr<char[]>> owned;  // string storage
    std::vector<TextLine> strings;               // +0x2180 list (text lines)
    std::vector<const char*> buttons;            // +0x2150 list (#button), count +0x20F4
    struct Item {
        const char* text;
        int32_t id;
        int32_t lines;  // wrapped lines (RadioButton_calculate_dimensions)
    };
    std::vector<Item> items;           // dialog items (+0xC4 list of the Dialog)
    std::vector<std::string> edit;     // EditGroup texts (one per item)
    std::string edit_default;          // EditGroup_set_default
    uint32_t flags = 0;                // +0x30A8
    int32_t mode = 0;                  // +0x30AC: #extra + PopupDialogState * 10000
    int32_t wave = -1, wave_vol = 0x7F;  // the show sound block (+0x2E64): #wave (+0x104), #vol (+0x108)
    int32_t item_type = 0x10;          // +0x2350: 1 check box, 2 list box, 4 edit group, 0x10 radio, -1 none
    int32_t active = -1;               // +0xA44: initial selection (edit groups: text limit)
    int32_t choice = 0;                // +0xA40
    int32_t button = 0;                // +0x3100: #button pressed (0: OK)
    int32_t check_bits = 0;            // +0x2314: check box states
    char* caption = nullptr;           // +0xA48
    Sprite picture;                    // +0x2118 (+0x2144 when loaded)
    bool has_picture = false;
    int32_t pic_scale_d = 1, pic_scale_s = 1;  // +0x3104 / +0x3108
    std::string ok_text = "OK", cancel_text = "Cancel";  // BasePop_set_ok_text / set_cancel_text
    // ---- dialog (item group) geometry: +0x21D0 virtual base ----
    int32_t dlg_flags = 0;             // +0x21F0 bits 0/1: fixed height/width
    int32_t dlg_width = 0;             // +0x21FC (#xs)
    int32_t dlg_height = 0;            // +0x2200 (#ys)
    int32_t icon_w = 3;                // +0x2204
    int32_t margin = 0;                // +0x2210
    int32_t spacing = 3;               // +0x2214
    int32_t row_h = 0x13;              // +0x2218
    int32_t cols = 1;                  // +0x221C (#col)
    int32_t max_rows = 1000;           // +0x2220 (#listbox N / #checkbox N)
    int32_t total_w = 0;               // +0x222C
    int32_t total_h = 0;               // +0x2230
    int32_t item_w = 0;                // +0x2234
    int32_t item_h = 0;                // +0x2238
    int32_t rows = 0;                  // +0x2228
    int32_t selected = 0;              // +0x22BC
    int32_t hover = -1;
    int32_t top_item = 0;              // list box scroll position
    // ---- margins and computed layout ----
    int32_t def_left = 0x1A, def_right = 0x3A, def_top = 3, def_bottom = 0x32;  // +0x21B0..0x21BC
    int32_t def_hpad = 0, def_vspace = 4;                                      // +0x21C0, +0x21C4
    int32_t text_indent = 0;           // +0x21C8
    int32_t text_x = -1;               // +0x21CC
    int32_t n_buttons = 0;             // +0x30B0
    int32_t win_x = 0, win_y = 0;      // +0x30B4, +0x30B8
    int32_t width = 0, height = 0;     // +0x30BC, +0x30C0
    int32_t dlg_h = 0;                 // +0x30C4
    int32_t button_w = 0, button_h = 0;  // +0x30CC, +0x30D0
    int32_t text_h = 0;                // +0x30D4
    int32_t pic_w = 0, pic_h = 0;      // +0x30D8, +0x30DC
    int32_t hpad = 0, vspace = 4;      // +0x30E0, +0x30E4
    int32_t top = 3, bottom = 0x32, left = 0x1A, right = 0x3A;  // +0x30E8..0x30F4
    int32_t req_x = 0x1000, req_y = 0x1000;                    // +0x30F8, +0x30FC
    int32_t item_x = 0, item_y = 0;    // where the dialog was attached
    // ---- fonts and colours ----
    Font* str_font[4] = {};            // +0x3110
    int32_t str_color[4][4] = {};      // +0x3120 colour, +0x3130 shadow, +0x3140 dx, +0x3150 dy
    Font* btn_font[3] = {};            // +0x316C
    int32_t btn_color = 0, btn_shadow = -1;
    Font* dlg_font[3] = {};
    // ---- window ----
    Buffer canvas;                     // the GraphicWin's Buffer (+0x444)
    bool shown = false;
    struct Button {
        UiRect r;
        int32_t id;
        const char* text;
    };
    std::vector<Button> btns;
    int32_t pressed_button = 0x7FFF;   // button under the mouse while held
    bool edit_fresh = true;            // the edit text is still the selected default
    std::string label;                 // the (last) section read, for the log

    BasePop() { Buffer_ctor(&canvas); }

    const char* keep(const char* s) {
        size_t n = std::strlen(s);
        owned.emplace_back(new char[n + 1]);
        std::memcpy(owned.back().get(), s, n + 1);
        return owned.back().get();
    }
};

namespace {

std::vector<BasePop*> g_windows;  // shown popups, bottom to top

bool interface_visible() {
    // pfncWinIsVisible(pMain): the main interface (console) is up once the game screen exists.
    return ui_screen_canvas() != nullptr && MapWins[0] != nullptr && !ui_desktop();
}

// Height of the main interface's console (DAT_007AF59C) at 1024x768; popups rise from it.
int32_t interface_height() { return ScreenWidth >= 1024 ? 213 : 213 * ScreenHeight / 768; }

// BasePop_init (0x6015B0) without the window: resets the contents.
void basepop_init(BasePop* p, uint32_t flags) {
    p->strings.clear();
    p->buttons.clear();
    p->items.clear();
    p->edit.clear();
    p->owned.clear();
    p->caption = nullptr;
    p->has_picture = false;
    p->ok_text = "OK";
    p->cancel_text = "Cancel";
    p->flags = flags;
    p->mode = 0;
    p->choice = 0;
    p->button = 0;
    p->dlg_flags &= ~3;
    p->dlg_width = 0;
    p->dlg_height = 0;
    p->cols = 1;
    p->max_rows = 1000;
    p->req_x = p->req_y = 0x1000;
    p->text_x = -1;
    p->text_indent = 0;
    if (flags & 1) p->item_type = 1;
    else if (flags & 2) p->item_type = 2;
    else if (flags & 4) p->item_type = 4;
    else p->item_type = 0x10;
}

int32_t scaled(BasePop* p, int32_t v) {
    return (!(p->flags & 0x400) && ScreenWidth > 0x3FF) ? v * 3 / 2 : v;
}

int32_t atoi_s(const char* s) { return std::atoi(s); }

// The section header keyword at p (case-insensitive prefix compare, FUN_0064FDF0 = _strnicmp).
bool kw(const char* p, const char* word) {
    size_t n = std::strlen(word);
    for (size_t i = 0; i < n; i++)
        if (std::tolower((unsigned char)p[i]) != word[i]) return false;
    return true;
}

}  // namespace

BasePop* popup_new() {
    popup_art_init();
    BasePop* p = new BasePop();
    // BasePop_ctor's defaults (popups_normal; config_popups' main font size).
    int32_t mi = g_main_font, bi = g_button_font;
    p->str_font[0] = &g_fonts[mi][0];
    p->str_font[1] = &g_fonts[mi][1];
    p->str_font[2] = &g_fonts[mi][2];
    p->str_font[3] = nullptr;
    p->dlg_font[0] = &g_fonts[mi][0];
    p->dlg_font[1] = &g_fonts[mi][1];
    p->dlg_font[2] = &g_fonts[mi][2];
    p->btn_font[0] = &g_fonts[bi][0];
    p->btn_font[1] = &g_fonts[bi][1];
    p->btn_font[2] = &g_fonts[bi][2];
    for (int32_t i = 0; i < 3; i++) {
        p->str_color[i][0] = g_iface_colors[1];  // DAT_0068A5A4
        p->str_color[i][1] = -1;
        p->str_color[i][2] = p->str_color[i][3] = 1;
    }
    p->str_color[3][0] = 2;  // hyperlinks
    p->str_color[3][1] = -1;
    p->str_color[3][2] = p->str_color[3][3] = 1;
    p->btn_color = 0;  // BaseButton_set_def_text_color(0, -1, 1, 1)
    p->btn_shadow = -1;
    return p;
}

void popup_delete(BasePop* pop) {
    if (!pop) return;
    g_windows.erase(std::remove(g_windows.begin(), g_windows.end(), pop), g_windows.end());
    delete pop;
}

// ---- BasePop_start (0x601BF0) ----

int32_t BasePop_start(BasePop* self, const char* file, const char* label, int32_t active, const char* edit_default,
                      uint32_t flags, uint8_t*) {
    bool text_mode = true;
    if (!(flags & 0x80)) basepop_init(self, flags);
    if (flags & 1) self->item_type = 1;
    else if (flags & 2) self->item_type = 2;
    else if (flags & 4) self->item_type = 4;
    else self->item_type = 0x10;
    self->active = active;
    if (!label) return 3;
    if (edit_default) self->edit_default = edit_default;
    self->label = label;
    if (text_open(file ? file : ScriptFileName, label)) return 1;
    for (;;) {
        char* line = text_get();
        if (!line) return 1;
        if (!*line) {
            text_mode = false;
            continue;
        }
        char buf[256];
        buf[0] = 0;
        parse_string(line, buf);
        if (buf[0] == '#') {
            char* a = buf + 1;
            auto arg = [&](size_t skip) {
                char* s = a + skip;
                purge_spaces(s);
                purge_leading(s);
                return s;
            };
            if (kw(a, "xs")) {
                self->dlg_width = scaled(self, atoi_s(arg(2)));
            } else if (kw(a, "ys")) {
                self->dlg_height = scaled(self, atoi_s(arg(2)));
            } else if (kw(a, "x ")) {
                int32_t v = atoi_s(arg(1));
                if (v != 0x2000) self->req_x = v;
            } else if (kw(a, "y ")) {
                int32_t v = atoi_s(arg(1));
                if (v != 0x2000) self->req_y = v;
            } else if (kw(a, "col ")) {
                self->cols = std::max(1, atoi_s(arg(3)));
            } else if (kw(a, "caption")) {
                char* s = arg(7);
                if (*s) self->caption = const_cast<char*>(self->keep(s));
            } else if (kw(a, "checkbox")) {
                self->flags |= 1;
                self->item_type = 1;
                char* s = arg(8);
                if (*s) self->max_rows = atoi_s(s);
            } else if (kw(a, "editbox")) {
                self->item_type = 4;
                self->flags |= 4;
            } else if (kw(a, "listbox")) {
                self->flags |= 2;
                self->item_type = 2;
                char* s = arg(7);
                if (*s) self->max_rows = atoi_s(s);
            } else if (kw(a, "active")) {
                self->active = atoi_s(arg(6));
            } else if (kw(a, "text")) {
                text_mode = true;
            } else if (kw(a, "itemlist")) {
                text_mode = false;
            } else if (kw(a, "button")) {
                char* s = arg(6);
                if (*s) self->buttons.push_back(self->keep(s));
            } else if (kw(a, "extra")) {
                self->mode = atoi_s(arg(5));
            } else if (kw(a, "wave") && a[4] != '2') {
                // The show sound's CPU voice (popup_wave_callback when the popup is shown).
                char* s = arg(4);
                if (*s >= '0' && *s <= '9') self->wave = atoi_s(s);
            } else if (kw(a, "vol") && a[3] != '2') {
                self->wave_vol = atoi_s(arg(3));
            } else if (kw(a, "wave") || kw(a, "vol") || kw(a, "pitch") || kw(a, "fade") || kw(a, "pan")) {
                // The close sound and the pitch, fade and pan of the sound blocks: unused by the
                // game's scripts with a voice.
            } else if (kw(a, "sprite")) {
                BasePop_set_picture(self, arg(6));
            } else if (a[0] != '#') {
                // the next section: done
                text_close();
                return 0;
            }
            continue;
        }
        if (text_mode) {
            BasePop_string(self, buf);
        } else if (self->item_type == 4) {
            // EditGroup_edit(label, default): the default text goes to the first field.
            self->items.push_back({self->keep(buf), (int32_t)self->items.size(), 1});
            self->edit.push_back(self->edit.empty() ? self->edit_default : std::string());
        } else {
            BasePop_item(self, buf, (int32_t)self->items.size());
        }
    }
}

int32_t BasePop_string(BasePop* self, const char* text) {
    if (!text) return 3;
    int32_t mode = 0;
    if (*text == '^') {
        if (!text[1]) {
            mode = 3;
            text++;
        } else if (text[1] == '^') {
            mode = 2;
            text += text[2] == '^' ? 3 : 2;
        } else {
            mode = 1;
            text++;
        }
    }
    self->strings.push_back({const_cast<char*>(self->keep(text)), mode});
    return 0;
}

int32_t BasePop_item(BasePop* self, const char* text, int32_t id) {
    if (!text) return 3;
    self->items.push_back({self->keep(text), id, 1});
    // Edit groups: the item is a field (EditGroup_edit; the default text goes to the first).
    if (self->item_type == 4) self->edit.push_back(self->edit.empty() ? self->edit_default : std::string());
    return 0;
}

int32_t BasePop_button(BasePop* self, const char* text) {
    if (!text) return 3;
    self->buttons.push_back(self->keep(text));
    return 0;
}

void BasePop_set_loc(BasePop* self, int32_t x, int32_t y) {
    if (x != 0x2000) self->req_x = x;
    if (y != 0x2000) self->req_y = y;
}

void BasePop_set_width(BasePop* self, int32_t w) { self->dlg_width = scaled(self, w); }
void BasePop_set_max_rows(BasePop* self, int32_t rows) { self->max_rows = rows; }  // as #listbox N

void BasePop_set_ok_text(BasePop* self, const char* text) { self->ok_text = text ? text : ""; }
void BasePop_set_cancel_text(BasePop* self, const char* text) { self->cancel_text = text ? text : ""; }
void BasePop_set_picture_sprite(BasePop* self, const Sprite* s) {
    self->has_picture = s && s->width;
    if (self->has_picture) self->picture = *s;
}
void BasePop_size(BasePop* self, int32_t* w, int32_t* h) {
    *w = self->width;
    *h = self->height;
}

void BasePop_set_picture(BasePop* self, const char* pcx) {
    if (!pcx || !*pcx) return;
    Buffer b;
    Buffer_ctor(&b);
    int32_t faction = -1;
    if (std::sscanf(pcx, "faction:%d", &faction) == 1 && faction >= 0 && faction < 8) {
        // A leader portrait (FactionPortraits, load_faction_art): sprite 28 of the faction's art.
        std::string file = std::string(MFactions[faction].filename) + ".pcx";
        if (Buffer_load_pcx(&b, file.c_str(), nullptr, 10, 0xEC) && Buffer_load_pcx(&b, "faction.pcx", nullptr, 10, 0xEC))
            return;
        if (!pfncSpriteFromCanvasRectTrans(&self->picture, &b, 9, 0x130, 0x1C9, 0x83, 0x9D, 0) ||
            self->picture.width) {
            self->has_picture = true;
            self->picture.full_width = 0x83;
            self->picture.full_height = 0x9D;
        }
        return;
    }
    if (Buffer_load_pcx(&b, pcx, nullptr, 10, 0xEC)) return;
    // Sprite_init: the whole picture (index 255 -> 9 transparent).
    if (!pfncSpriteFromCanvasRectTrans(&self->picture, &b, 0x109, 0, 0, b.width, b.height, 0) ||
        self->picture.width) {
        self->has_picture = true;
        self->picture.full_width = b.width;
        self->picture.full_height = b.height;
    }
}

int32_t BasePop_button_pressed(BasePop* self) { return self->button; }
int32_t BasePop_check_state(BasePop* self) { return self->check_bits; }
void BasePop_set_check_state(BasePop* self, int32_t bits) { self->check_bits = bits; }
const char* BasePop_edit_text(BasePop* self, int32_t i) {
    return (i >= 0 && i < (int32_t)self->edit.size()) ? self->edit[i].c_str() : "";
}
int32_t BasePop_item_count(BasePop* self) { return (int32_t)self->items.size(); }
const char* BasePop_item_text(BasePop* self, int32_t i) {
    return i >= 0 && i < (int32_t)self->items.size() ? self->items[i].text : nullptr;
}
int32_t BasePop_item_id(BasePop* self, int32_t i) {
    return i >= 0 && i < (int32_t)self->items.size() ? self->items[i].id : -1;
}
int32_t BasePop_active(BasePop* self) { return self->active; }
// EditGroup_edit (0x611F80): an edit field labelled `label` holding `text` (`max` characters).
void BasePop_edit(BasePop* self, const char* label, const char* text, int32_t) {
    BasePop_item(self, label, (int32_t)self->items.size());
    if (!self->edit.empty()) self->edit.back() = text ? text : "";
}
void BasePop_set_mode(BasePop* self, int32_t mode) { self->mode = mode; }
void BasePop_set_active(BasePop* self, int32_t id) { self->active = id; }
const char* BasePop_caption(BasePop* self) { return self->caption; }

std::string BasePop_text(BasePop* self) {
    std::string s;
    for (size_t i = 0; i < self->strings.size(); i++) {
        if (i) s += " ";
        s += self->strings[i].text;
    }
    return s;
}

namespace {

// ---- layout ----

void set_string_fonts(BasePop* p) {
    Buffer* b = &p->canvas;
    Buffer_set_text_color(b, p->str_color[0][0], p->str_color[0][1], p->str_color[0][2], p->str_color[0][3]);
    Buffer_set_text_color2(b, p->str_color[1][0], p->str_color[1][1], p->str_color[1][2], p->str_color[1][3]);
    Buffer_set_text_color3(b, p->str_color[2][0], p->str_color[2][1], p->str_color[2][2], p->str_color[2][3]);
    Buffer_set_text_color_hyper(b, p->str_color[3][0], p->str_color[3][1], p->str_color[3][2], p->str_color[3][3]);
    Buffer_set_font(b, p->str_font[0], p->str_font[1], p->str_font[2], p->str_font[3]);
}

void set_dialog_fonts(BasePop* p) { Buffer_set_font(&p->canvas, p->dlg_font[0], p->dlg_font[1], p->dlg_font[2], nullptr); }

// RadioButton_calculate_dimensions (0x60D380) / CheckBox_calculate_dimensions (0x60ED10) /
// EditGroup_calculate_dimensions (0x612150) / ListBox_calculate_dimensions (0x60A1E0, as a radio
// list of max_rows visible rows).
void dialog_calculate(BasePop* p) {
    Buffer* b = &p->canvas;
    set_dialog_fonts(p);
    int32_t n = (int32_t)p->items.size();
    if (!n) {
        p->total_w = p->total_h = 0;
        return;
    }
    if (p->item_type == 4) {
        int32_t label_w = 0, text_w = 0;
        for (int32_t i = 0; i < n; i++) {
            label_w = std::max(label_w, Buffer_text_width(b, p->items[i].text));
            int32_t w = Buffer_text_width(b, p->edit[i].c_str());
            if (text_w < w) text_w = w + p->margin * 2;
        }
        text_w = std::max(text_w, 10);
        p->rows = std::min(p->max_rows, n);
        p->item_w = p->icon_w + (p->spacing + p->margin) * 2 + text_w + label_w;
        p->item_h = Buffer_text_line_height(b) + p->spacing;
        p->total_w = std::max(p->item_w * p->cols, p->dlg_width);
        p->item_w = p->total_w / p->cols;
        p->total_h = p->item_h * p->rows + p->spacing;
        return;
    }
    p->rows = std::min(p->max_rows, n);
    int32_t cols = p->cols;
    if (p->item_type != 2 && p->rows < n) {
        cols = n / p->rows + 1;
        p->rows = n / cols;
        if (cols * p->rows < n) p->rows++;
    }
    p->cols = cols;
    int32_t avail = 100000;
    if ((p->dlg_flags & 2) && p->dlg_width) avail = p->dlg_width / cols - p->margin * 2 - p->icon_w;
    int32_t max_w = 0;
    for (BasePop::Item& it : p->items) {
        int32_t w = Buffer_text_width(b, it.text);
        max_w = std::max(max_w, w);
        it.lines = w / avail + 1;
    }
    p->item_w = p->spacing + p->icon_w + max_w;
    p->item_h = std::max(p->row_h, Buffer_text_height(b));
    if (!(p->dlg_flags & 2) || !p->dlg_width) {
        int32_t w = std::max(p->item_w * cols + p->margin * 2, p->margin * 2 + p->dlg_width);
        p->total_w = w;
        p->item_w = (w - p->margin * 2) / cols - p->spacing;
        p->total_w = w - p->spacing;
    } else {
        p->total_w = p->dlg_width;
        p->item_w = (p->dlg_width - p->margin * 2) / cols - p->spacing;
    }
    // Wrapped items (fixed width): each line count recomputed for the final width.
    if ((p->dlg_flags & 2) && p->dlg_width) {
        int32_t w = p->item_w - p->icon_w;
        for (BasePop::Item& it : p->items) it.lines = w > 0 ? Buffer_text_width(b, it.text) / w + 1 : 1;
    }
    if ((p->dlg_flags & 1) && p->dlg_height) {
        p->total_h = p->dlg_height;
        p->rows = p->dlg_height / p->item_h;
    } else {
        int32_t h = 0;
        int32_t lh = Buffer_text_line_height(b);
        for (int32_t i = 0; i < p->rows; i++) {
            int32_t ih = p->item_h;
            if (i < n && p->items[i].lines > 1 && ih <= lh * p->items[i].lines) ih = lh * p->items[i].lines;
            h += ih + p->spacing;
        }
        p->total_h = h;
    }
}

// Popup_calculate_dimensions (0x404B10) and BasePop_calculate_dimensions (0x603800).
int32_t calculate(BasePop* p) {
    Buffer* b = &p->canvas;
    bool main_visible = interface_visible();
    // Popup: the picture widens the dialog (DiploPop excepted).
    if (p->has_picture) {
        int32_t w = p->total_w ? p->total_w : p->dlg_width;
        w += p->picture.full_width * p->pic_scale_d / p->pic_scale_s + p->hpad;
        if (dword_9469FC) w = 0x2BC;  // the faction picker's text box (thumb_routine)
        else if (ScreenWidth == 800) w = std::min(w, 0x1EE);
        else if (ScreenWidth - 0x16E <= w) w = ScreenWidth - 0x16E;
        p->dlg_width = w;
    }
    int32_t state = PopupDialogState;
    p->mode += state * 10000;
    // Popup: the text colours of text.pcx (pairs 1..4 from 0x8C6C74; DiploPop's from 0x8C6E34).
    for (int32_t i = 0; i < 4; i++) {
        p->str_color[i][0] = g_popup_text_colors[1 + i][0];
        p->str_color[i][1] = g_popup_text_colors[1 + i][1];
    }
    if (ControlWaitLoop || state == 6) p->flags |= 0x200000;
    // Popup look.
    p->margin = 0;
    if (state != 5) p->row_h = 0x13;
    p->spacing = 3;
    int32_t popup_button_h = 0x14;
    if (state != 5) p->icon_w = p->item_type == 1 ? 0x1B : 3;
    p->def_top = p->caption ? 0x26 : 0x12;
    if (!main_visible || (state != 0 && state != 5)) {
        p->def_left = 10;
        p->def_right = 0xC;
        p->def_bottom = 0x12;
        if (state == 6 || state == 2) {
            p->def_left = 0x13;
            p->def_right = 0x15;
            p->def_top += 9;
        }
    } else {
        p->def_left = 0x21;
        p->def_right = 0x43;
        p->def_bottom = ScreenWidth != 800 ? 0x32 : 0x1E;
    }
    p->def_vspace = 4;
    p->def_hpad = 0;
    // BasePop_calculate_dimensions.
    set_string_fonts(p);
    p->n_buttons = (int32_t)p->buttons.size() + ((p->flags & 0x40) ? 1 : 0) + ((p->flags & 0x10100) ? 0 : 1);
    p->hpad = scaled(p, p->def_hpad);
    p->vspace = scaled(p, p->def_vspace);
    p->top = p->def_top;
    p->bottom = p->def_bottom;
    p->left = p->def_left;
    p->right = p->def_right;
    if (p->has_picture) {
        p->pic_w = p->picture.full_width * p->pic_scale_d / p->pic_scale_s + p->hpad +
                   (p->strings.empty() ? 0 : p->vspace);
        p->pic_h = p->picture.full_height * p->pic_scale_d / p->pic_scale_s;
    } else {
        p->pic_w = p->pic_h = 0;
    }
    dialog_calculate(p);
    set_string_fonts(p);
    int32_t dw = p->total_w ? p->total_w : p->dlg_width;
    if (p->text_x == -1) {
        p->width = (p->pic_w < dw ? dw : p->pic_w) + p->hpad * 2;
    } else {
        int32_t w = p->pic_w;
        if (w < dw) w = dw;
        p->width = p->text_x + p->hpad + w;
    }
    p->dlg_h = p->total_h;
    int32_t text_w = p->width - p->hpad * 2 - p->text_indent - p->pic_w;
    p->text_h = buffer_strings_height(b, p->strings.data(), (int32_t)p->strings.size(), text_w);
    if (p->n_buttons) {
        int32_t fh = p->btn_font[0] ? p->btn_font[0]->ascent_above : 0;
        p->button_h = fh * 7 / 4;
        p->button_w = (p->width - p->hpad * 2 - (p->n_buttons - 1) * p->vspace) / p->n_buttons;
    } else {
        p->button_h = p->button_w = 0;
    }
    p->button_h = popup_button_h;  // Popup_on_adjust_button_width
    if (!p->n_buttons) p->button_h = 0;
    if (p->flags & 0x100) p->button_h = 0;
    int32_t body = std::max(p->pic_h, p->text_h);
    p->height = p->dlg_h + p->hpad * 2 + body + p->button_h;
    if (p->dlg_h) p->height += p->vspace;
    if (p->button_h) p->height += p->vspace;
    p->height += p->bottom + p->top;
    p->width += p->right + p->left;
    // Popup: at least 300 pixels high in the main interface's frame.
    if (main_visible && (state == 0 || state == 5) && p->height < 300) {
        p->bottom += 300 - p->height;
        p->height = 300;
    }
    return 0;
}

// ---- drawing ----

void draw_items(BasePop* p);
void draw_buttons(BasePop* p);

// Popup_on_draw_background (0x405C80): frame, caption bar, text and item frames.
void draw_background(BasePop* p) {
    Buffer* b = &p->canvas;
    int32_t state = p->mode / 10000;
    UiRect all = {0, 0, b->width, b->height};
    Buffer_set_clip(b, &all);
    Buffer_fill_3(b, &all, kKey);
    bool main_visible = interface_visible();
    UiRect frame = all;
    if (main_visible && (state == 0 || state == 5)) {
        frame.left += 0x17;
        frame.right -= 0x37;
        frame.bottom += 5;
    }
    Buffer_box_sprite(b, &frame, (state == 6 || state == 2) ? &g_box_large : box_def(9));
    if (p->caption) {
        for (char* c = p->caption; *c; c++) *c = (char)std::toupper((unsigned char)*c);
        UiRect r = all;
        if (state == 6 || state == 2) {
            r.left += 9;
            r.right -= 9;
            r.top += 9;
            r.bottom -= 9;
        }
        if (!main_visible || (state != 0 && state != 5)) {
            r.left += 3;
            r.right -= 3;
        } else {
            r.left += 0x1A;
            r.right -= 0x3A;
        }
        r.left += 1;
        r.right -= 1;
        r.bottom = r.top + 0x1C;
        r.top += 4;
        Buffer_box_sprite(b, &r, box_def(3));
        r.left += 3;
        r.right -= 3;
        r.top += 3;
        r.bottom -= 3;
        Buffer_box_sprite(b, &r, box_def(6));
        r.left += 3;
        r.right -= 3;
        r.top += 3;
        r.bottom -= 3;
        Buffer_set_font(b, &g_caption_font, nullptr, nullptr, nullptr);
        Buffer_set_text_color(b, g_popup_text_colors[0][0], g_popup_text_colors[0][1], 1, 1);
        Buffer_write_cent_l_7(b, p->caption, &r, (int32_t)std::strlen(p->caption));
    }
    int32_t x = p->left + p->pic_w + p->text_indent + p->hpad;
    int32_t y = p->top + p->hpad;
    UiRect text = {x - 3, y - 3, frame.right - ((state == 6 || state == 2) ? 0x13 : 10) + 3, p->text_h + y};
    int32_t has_text = p->text_h;
    if (has_text) {
        Buffer_box_sprite(b, &text, box_def(0));
        y += p->vspace + p->text_h;
    }
    if (p->text_x >= 0) x = p->text_x;
    UiRect items = {};
    if (p->item_type != -1) {
        items = {x, y, x + p->total_w, y + p->total_h};
        if (has_text) {
            items.left = text.left;
            items.right = text.right;
        }
    }
    UiRect outer = items;
    if (has_text) {
        outer = text;
        if (p->item_type != -1 && !p->has_picture) {
            outer.left = std::min(text.left, items.left);
            outer.top = std::min(text.top, items.top);
            outer.right = std::max(text.right, items.right);
            outer.bottom = std::max(text.bottom, items.bottom);
        }
    }
    UiRect r = {outer.left - 3, outer.top - 3, outer.right + 3, outer.bottom + 3};
    Buffer_box_sprite(b, &r, box_def(3));
}

// BasePop_on_redraw (0x604AB0): background, picture, text; then the items and buttons.
void redraw(BasePop* p) {
    Buffer* b = &p->canvas;
    draw_background(p);
    int32_t y = p->top + p->hpad;
    int32_t x = p->hpad + p->left + p->text_indent;
    // Popup's picture callback (sprite_draw_func 0x404860): at (x - 5, y - 6), unscaled.
    if (p->has_picture) sprite_copy(&p->picture, b, x - 5, y - 6);
    x += p->pic_w;
    int32_t text_bottom = y;
    if (p->text_h) {
        set_string_fonts(p);
        int32_t w = p->width - p->hpad * 2 - p->right - p->pic_w - p->left - p->text_indent;
        text_bottom = Buffer_write_strings(b, p->strings.data(), (int32_t)p->strings.size(), x, y, w, 0);
    }
    // Where the item group goes (DAT_009B8CEC, DAT_009B8CE8).
    p->item_x = p->text_x >= 0 ? p->text_x : x - p->pic_w - p->text_indent;
    int32_t below = std::max(text_bottom, y + p->pic_h);
    p->item_y = below + (p->text_h ? p->vspace : 0);
    draw_items(p);
    draw_buttons(p);
}

// Popup_on_dialog_item_back_draw (0x405B20): the item's box (and check box mark) in state 0
// (normal), 1 (under the mouse) or 2 (selected / checked), and the text colour of that state.
void item_back(BasePop* p, int32_t state, UiRect r) {
    Buffer* b = &p->canvas;
    if (p->item_type == 1) {
        r.left += 3;
        Sprite* s = &g_checks[state];
        sprite_copy(s, b, r.left, (r.bottom - s->full_height - r.top) / 2 + r.top);
        r.left += 0x15;
    }
    Buffer_box_sprite(b, &r, box_def(27 + state));
    Buffer_set_text_color(b, g_popup_text_colors[6 - state][0], g_popup_text_colors[6 - state][1], 1, 1);
}

UiRect item_rect(BasePop* p, int32_t pos) {
    // RadioButton_draw_item's geometry: column-major, items of several lines are taller.
    int32_t col = pos / std::max(1, p->rows), row = pos - col * p->rows;
    int32_t lh = Buffer_text_line_height(&p->canvas);
    int32_t extra = 0;
    for (int32_t i = col * p->rows; i < pos; i++) {
        const BasePop::Item& it = p->items[i];
        if (it.lines > 1) {
            int32_t h = p->item_h;
            if (h <= lh * it.lines) h = lh * it.lines;
            extra += h - p->item_h;
        }
    }
    int32_t x = (p->item_w + p->spacing) * col + p->margin;
    int32_t y = (p->spacing >> 1) + extra + row * (p->item_h + p->spacing);
    int32_t h = p->item_h;
    const BasePop::Item& it = p->items[pos];
    if (it.lines > 1 && h <= lh * it.lines) h = lh * it.lines;
    return {p->item_x + x, p->item_y + y, p->item_x + x + p->item_w, p->item_y + y + h};
}

void draw_items(BasePop* p) {
    Buffer* b = &p->canvas;
    if (p->item_type == -1 || p->items.empty()) return;
    set_dialog_fonts(p);
    int32_t n = (int32_t)p->items.size();
    if (p->item_type == 4) {
        // EditGroup (EditGroup_attach 0x611AF0 / EditGroup_on_redraw 0x612450): each row is the
        // label at the icon offset and an EditBox right of the widest label (icon + 2 spacings),
        // spacing / 2 below the row top and spacing less high than the row. The default text is
        // selected (EditBox selection colours: iface.pcx colour 6 under black text).
        int32_t label_w = 0;
        for (int32_t i = 0; i < n; i++) label_w = std::max(label_w, Buffer_text_width(b, p->items[i].text));
        for (int32_t i = 0; i < n; i++) {
            int32_t y0 = p->item_y + i * p->item_h + (p->spacing >> 1);
            int32_t y1 = y0 + p->item_h - p->spacing;
            UiRect label = {p->item_x + p->margin + p->icon_w, y0, p->item_x + p->total_w, y1};
            Buffer_set_text_color(b, p->str_color[0][0], p->str_color[0][1], 1, 1);
            Buffer_write_l_4(b, p->items[i].text, &label, (int32_t)std::strlen(p->items[i].text));
            int32_t ex = p->item_x + p->icon_w + p->spacing * 2 + label_w;
            UiRect t = {ex, y0, p->item_x + p->total_w - p->spacing, y1};
            const std::string& s = p->edit[i];
            int32_t tw = Buffer_text_width(b, s.c_str());
            bool sel = i == p->selected && p->edit_fresh && !s.empty();
            if (sel) {
                UiRect r = {t.left, y0, t.left + tw, y1};
                Buffer_fill_3(b, &r, g_iface_colors[6]);
            }
            Buffer_set_text_color(b, sel ? 0 : p->str_color[0][0], -1, 1, 1);
            if (!s.empty()) Buffer_write_l_4(b, s.c_str(), &t, (int32_t)s.size());
            // The caret at the end of the text (EditBox; the original blinks it on a timer).
            if (i == p->selected) Buffer_vline_2(b, t.left + tw, y0, y1 - 1, p->str_color[0][0]);
        }
        return;
    }
    int32_t first = p->item_type == 2 ? p->top_item : 0;
    int32_t last = p->item_type == 2 ? std::min(n, first + p->rows) : n;
    for (int32_t i = first; i < last; i++) {
        UiRect r = item_rect(p, i - first);
        // RadioButton_draw_item / CheckBox_draw_item: state 2 for the selected (check boxes: checked)
        // item, 1 under the mouse, else 0.
        bool on = p->item_type == 1 ? (p->check_bits >> i & 1) != 0 : i == p->selected;
        int32_t state = on ? 2 : (i == p->hover ? 1 : 0);
        item_back(p, state, r);
        UiRect t = r;
        t.left += p->icon_w;
        const char* text = p->items[i].text;
        const char* bar = std::strchr(text, '|');
        std::string left(text, bar ? (size_t)(bar - text) : std::strlen(text));
        if (p->items[i].lines == 1) Buffer_write_l_4(b, left.c_str(), &t, (int32_t)left.size());
        else Buffer_wrap_2(b, left.c_str(), &t);
        if (bar) Buffer_write_right_l_6(b, bar + 1, &t, (int32_t)std::strlen(bar + 1));
    }
}

// FlatButton_on_redraw (0x607E50) with the interface.pcx button bars: left cap, tiled middle,
// right cap; the label centred (BaseButton fonts).
void draw_button(BasePop* p, const BasePop::Button& bt, int32_t state) {
    Buffer* b = &p->canvas;
    Sprite* s = g_bars[6 + state];
    int32_t w = bt.r.right - bt.r.left, h = bt.r.bottom - bt.r.top;
    sprite_copy(&s[0], b, bt.r.left, bt.r.top);
    sprite_copy(&s[2], b, bt.r.right - s[2].full_width, bt.r.top);
    Buffer_tile_2(b, &s[1], 0, 0, bt.r.left + s[0].full_width, bt.r.top, w - s[0].full_width - s[2].full_width,
                  s[1].full_height);
    Buffer_set_font(b, p->btn_font[0], p->btn_font[1], p->btn_font[2], nullptr);
    Buffer_set_text_color(b, p->btn_color, p->btn_shadow, 1, 1);
    int32_t lh = Buffer_text_line_height(b), th = Buffer_text_height(b);
    int32_t wh = Buffer_wrap_height(b, bt.text, w - 6);
    int32_t y = (h - (wh - lh + th)) / 2 + bt.r.top;
    Buffer_wrap_cent_4(b, bt.text, bt.r.left + 3, y, w - 6);
}

void draw_buttons(BasePop* p) {
    for (const BasePop::Button& bt : p->btns) draw_button(p, bt, bt.id == p->pressed_button ? 2 : 0);
}

// Button placement of BasePop_exec_3: right-anchored row at the bottom; Cancel rightmost, OK to its
// left, the #buttons from the left end of the row.
void place_buttons(BasePop* p) {
    p->btns.clear();
    if ((p->flags & 0x80100) || !p->n_buttons) return;
    int32_t x = -p->right, y = p->height - p->bottom - p->hpad - p->button_h + 3;
    auto at = [&](int32_t bx) {  // negative x: from the right edge of the window
        return bx < 0 ? p->width + bx - p->button_w : bx;
    };
    if (!(p->flags & 0x10000)) {
        int32_t ox = x;
        if (p->flags & 0x40) {
            if (x < 0) ox = x - (p->vspace + p->button_w);
            else ox = x + (p->vspace + p->button_w) * (int32_t)p->buttons.size();
        }
        int32_t l = at(ox);
        p->btns.push_back({{l, y, l + p->button_w, y + p->button_h}, -1, p->ok_text.c_str()});
    }
    int32_t ex = x;
    if (x < 0) ex = x - (p->vspace + p->button_w) * (p->n_buttons - 1);
    for (size_t i = 0; i < p->buttons.size(); i++) {
        int32_t l = at(ex);
        p->btns.push_back({{l, y, l + p->button_w, y + p->button_h}, (int32_t)i + 1, p->buttons[i]});
        ex += p->button_w + p->vspace;
    }
    if (p->flags & 0x40) {
        int32_t cx = x >= 0 ? x + (p->vspace + p->button_w) * (p->n_buttons - 1) : x;
        int32_t l = at(cx);
        p->btns.push_back({{l, y, l + p->button_w, y + p->button_h}, -2, p->cancel_text.c_str()});
    }
}

// ---- input ----

// The answer of BasePop_on_button_clicked (0x604300): the selected item (check boxes: their
// bits), -1 for Cancel with a Cancel button; extra buttons set +0x3100.
void on_button_clicked(BasePop* p, int32_t id) {
    p->choice = 0;
    if (!p->items.empty() && p->item_type != -1) {
        if (p->item_type == 1) p->choice = p->check_bits;
        else if (p->selected >= 0 && p->selected < (int32_t)p->items.size()) p->choice = p->items[p->selected].id;
    }
    if (id == -2) {
        if (p->flags & 0x40) p->choice = -1;
        p->button = 0;
    } else if (id != -1) {
        p->button = id;
    } else {
        p->button = 0;
    }
}

int32_t item_at(BasePop* p, int32_t x, int32_t y) {
    if (p->item_type == -1) return -1;
    int32_t n = (int32_t)p->items.size();
    int32_t first = p->item_type == 2 ? p->top_item : 0;
    int32_t last = p->item_type == 2 ? std::min(n, first + p->rows) : n;
    for (int32_t i = first; i < last; i++) {
        UiRect r = item_rect(p, i - first);
        if (x >= r.left && x < r.right && y >= r.top && y < r.bottom) return i;
    }
    return -1;
}

// WM_CHAR of a key for the edit field (US layout, as TranslateMessage).
int32_t key_char(int32_t key) {
    int32_t vk = key & 0xFFFF;
    bool shift = key & 0x10000;
    if (key & 0x20000) return 0;
    if (vk >= 'A' && vk <= 'Z') return shift ? vk : vk + 0x20;
    if (vk >= '0' && vk <= '9') return shift ? ")!@#$%^&*("[vk - '0'] : vk;
    if (vk == VKEY_SPACE) return ' ';
    struct Oem { int32_t vk; char plain, shifted; };
    static const Oem kOem[] = {{0xBA, ';', ':'}, {0xBB, '=', '+'}, {0xBC, ',', '<'}, {0xBD, '-', '_'},
                               {0xBE, '.', '>'}, {0xBF, '/', '?'}, {0xC0, '`', '~'}, {0xDB, '[', '{'},
                               {0xDC, '\\', '|'}, {0xDD, ']', '}'}, {0xDE, '\'', '"'}};
    for (const Oem& o : kOem)
        if (o.vk == vk) return shift ? o.shifted : o.plain;
    return 0;
}

// EditBox_on_key_down / EditBox_on_key_click: the first key replaces the selected default text.
bool edit_key(BasePop* p, int32_t key, bool* fresh) {
    if (p->item_type != 4 || p->edit.empty()) return false;
    std::string& s = p->edit[std::max(0, std::min(p->selected, (int32_t)p->edit.size() - 1))];
    int32_t vk = key & 0xFFFF;
    int32_t limit = p->active > 0 ? p->active : 255;
    if (vk == VKEY_BACK) {
        if (*fresh) s.clear();
        else if (!s.empty()) s.pop_back();
        *fresh = false;
        return true;
    }
    int32_t c = key_char(key);
    if (!c) return false;
    if (*fresh) s.clear();
    *fresh = false;
    if ((int32_t)s.size() < limit) s += (char)c;
    return true;
}

}  // namespace

// ---- BasePop_exec_3 (0x602600) ----

int32_t BasePop_exec_3(BasePop* p, int32_t no_display, int32_t) {
    // Popup_on_exec (0x405910): popups of mode 1 (message line) and 2 (base message) are not shown;
    // 0x3EA is a base message that is also shown.
    int32_t kind = p->mode % 10000;
    if (p->mode / 10000 != 1000 && kind) {
        std::string msg;
        for (size_t i = 0; i < p->strings.size(); i++) {
            if (i) msg += " ";
            msg += p->strings[i].text;
        }
        if (kind == 1 || kind == 2 || kind == 0x3EA) {
            // MainInterface_set_message (1) / MessageWin_add_base (2, 0x3EA: the info pane's list).
            if (kind == 1) MainInterface_set_message(&MainInterfaceWin, msg.c_str());
            else MessageWin_add_base(&MessageWin, (uint32_t)CurrentBaseID, msg.c_str());
            std::fprintf(stderr, "popup %s as message (mode %d): %s\n", p->label.c_str(), p->mode, msg.c_str());
            if (kind != 0x3EA) return -1;
        }
    }
    if (p->items.empty()) p->item_type = -1;
    calculate(p);
    // smacx's observer mode (no human player) takes the no-display path, as the oracle forces it
    // for unattended runs (harness/README.md "Determinism measures"), so observer games play as
    // the oracle's.
    // The opening screens' dialogs (desktop mode, before a game) are always shown.
    if (!FactionStatus[0] && !ui_desktop()) no_display = 1;
    if (no_display || (p->flags & 0x8000)) return 0;
    // BasePop_exec_3: the show sound block's callback (popup_wave_callback).
    if (p->wave >= 0) sound_popup_wave(p->wave, p->wave_vol);
    // The window (GraphicWin_init) and its position (Popup_show / BasePop_exec_3).
    Buffer_init(&p->canvas, std::max(1, p->width), std::max(1, p->height), 8, 0);
    int32_t state = p->mode / 10000;
    if (interface_visible() && (state == 0 || state == 5)) {
        p->win_x = (ScreenWidth - p->width + 0x20) / 2;
        p->win_y = ScreenHeight - p->height - interface_height();
    } else {
        int32_t x = p->req_x, y = p->req_y;
        p->win_x = x == 0x1000 ? (ScreenWidth - p->width) >> 1 : (x < 0 ? x - p->width + ScreenWidth : x);
        p->win_y = y == 0x1000 ? (ScreenHeight - p->height) >> 1 : (y < 0 ? y - p->height + ScreenHeight : y);
    }
    // Initial selection (BasePop_calculate_dimensions).
    p->selected = 0;
    if (p->active != -1 && p->item_type != 4 && p->item_type != 1) {
        for (size_t i = 0; i < p->items.size(); i++)
            if (p->items[i].id == p->active) p->selected = (int32_t)i;
    }
    place_buttons(p);
    redraw(p);
    g_windows.push_back(p);
    p->shown = true;
    ui_invalidate();
    std::fprintf(stderr, "popup %s shown: %dx%d at %d,%d (%d items)\n", p->label.c_str(), p->width, p->height,
                 p->win_x, p->win_y, (int32_t)p->items.size());
    ui_trace("popup shown");
    // Test aid: SMACX_POPUP_SHOTS=DIR writes a screenshot of every popup when it opens.
    if (const char* dir = std::getenv("SMACX_POPUP_SHOTS")) {
        static int32_t count = 0;
        char path[512];
        std::snprintf(path, sizeof(path), "%s/pop_%03d_%s.bmp", dir, count++, p->label.c_str());
        ui_screenshot(path);
    }
    // The modal loop (Win_set_modal) with the key handling of Win_key_down_event (arrows to the
    // item group), Win_key_click_event (Enter: OK, Esc: cancel) and the mouse.
    bool& fresh = p->edit_fresh;
    fresh = true;
    bool done = false;
    int32_t n = (int32_t)p->items.size();
    while (!done) {
        UiInput in;
        if (!ui_wait_input(&in)) {
            // No more input (scripted run ended / window closed): the OK button.
            on_button_clicked(p, -1);
            break;
        }
        bool dirty = false;
        if (in.type == UiInput::KEY) {
            int32_t key = in.key, vk = key & 0xFFFF;
            if (edit_key(p, key, &fresh)) {
                dirty = true;
            } else if (vk == VKEY_RETURN) {
                on_button_clicked(p, -1);
                done = true;
            } else if (vk == VKEY_ESCAPE) {
                on_button_clicked(p, -2);
                done = true;
            } else if (n && p->item_type != -1 && p->item_type != 4) {
                // RadioButton_on_key_down (0x60E020) / CheckBox / ListBox: arrows (and the keypad).
                int32_t s = p->selected, rows = std::max(1, p->rows);
                switch (vk) {
                case VKEY_UP: case 0x68: s = std::max(0, s - 1); break;
                case VKEY_DOWN: case 0x62: s = std::min(n - 1, s + 1); break;
                case VKEY_LEFT: case 0x64: s = s - rows < 0 ? s : s - rows; break;
                case VKEY_RIGHT: case 0x66: s = s + rows >= n ? s : s + rows; break;
                case VKEY_PRIOR: if (p->item_type == 2) s = std::max(0, s - rows); break;
                case VKEY_NEXT: if (p->item_type == 2) s = std::min(n - 1, s + rows); break;
                case VKEY_HOME: if (p->item_type == 2) s = 0; break;
                case VKEY_END: if (p->item_type == 2) s = n - 1; break;
                case VKEY_SPACE:
                    if (p->item_type == 1) p->check_bits ^= 1 << s;  // CheckBox_flip_state_id
                    break;
                default: break;
                }
                if (s != p->selected || vk == VKEY_SPACE) {
                    p->selected = s;
                    if (p->item_type == 2) {
                        if (s < p->top_item) p->top_item = s;
                        if (s >= p->top_item + p->rows) p->top_item = s - p->rows + 1;
                    }
                    dirty = true;
                }
            }
        } else {
            int32_t x = in.x - p->win_x, y = in.y - p->win_y;
            int32_t hit = item_at(p, x, y);
            int32_t btn = 0x7FFF;
            for (const BasePop::Button& bt : p->btns)
                if (x >= bt.r.left && x < bt.r.right && y >= bt.r.top && y < bt.r.bottom) btn = bt.id;
            if (in.type == UiInput::LDOWN) {
                if (btn != 0x7FFF) {
                    p->pressed_button = btn;
                    dirty = true;
                } else if (hit >= 0) {
                    if (p->item_type == 1) p->check_bits ^= 1 << hit;
                    else if (hit == p->selected && in.double_click) {
                        // BasePop_double_click: the item is the answer.
                        on_button_clicked(p, -1);
                        done = true;
                    }
                    p->selected = hit;
                    dirty = true;
                }
            } else if (in.type == UiInput::LUP) {
                if (p->pressed_button != 0x7FFF) {
                    if (btn == p->pressed_button) {
                        on_button_clicked(p, btn);
                        done = true;
                    }
                    p->pressed_button = 0x7FFF;
                    dirty = true;
                }
            } else if (in.type == UiInput::MOVE) {
                if (hit != p->hover) {
                    p->hover = hit;
                    dirty = true;
                }
            }
        }
        if (dirty && !done) {
            redraw(p);
            ui_invalidate();
        }
    }
    // Close (Win_release_modal, Popup_hide).
    BasePop_hide(p);
    // Edit groups: the texts go to the parse slots.
    if (p->item_type == 4)
        for (int32_t i = 0; i < 8 && i < (int32_t)p->edit.size(); i++) parse_says(i, p->edit[i].c_str(), -1, -1);
    std::fprintf(stderr, "popup %s answer: %d (button %d)\n", p->label.c_str(), p->choice, p->button);
    return p->choice;
}

void popup_draw_check(Buffer* b, int32_t x, int32_t y, int32_t state) {
    popup_art_init();
    sprite_copy(&g_checks[std::max(0, std::min(2, state))], b, x, y);
}

void BasePop_show(BasePop* p) {
    if (p->items.empty()) p->item_type = -1;
    calculate(p);
    Buffer_init(&p->canvas, std::max(1, p->width), std::max(1, p->height), 8, 0);
    int32_t x = p->req_x, y = p->req_y;
    p->win_x = x == 0x1000 ? (ScreenWidth - p->width) >> 1 : (x < 0 ? x - p->width + ScreenWidth : x);
    p->win_y = y == 0x1000 ? (ScreenHeight - p->height) >> 1 : (y < 0 ? y - p->height + ScreenHeight : y);
    p->selected = 0;
    place_buttons(p);
    redraw(p);
    if (!p->shown) g_windows.push_back(p);
    p->shown = true;
    ui_invalidate();
}

void BasePop_hide(BasePop* p) {
    g_windows.erase(std::remove(g_windows.begin(), g_windows.end(), p), g_windows.end());
    p->shown = false;
    ui_invalidate();
}

void popup_draw_windows(Buffer* screen) {
    for (BasePop* p : g_windows) {
        const Buffer& c = p->canvas;
        for (int32_t j = 0; j < c.height; j++) {
            int32_t y = p->win_y + j;
            if (y < 0 || y >= screen->height) continue;
            const uint8_t* src = c.data + (size_t)j * c.stride;
            uint8_t* dst = screen->data + (size_t)y * screen->stride;
            for (int32_t i = 0; i < c.width; i++) {
                int32_t x = p->win_x + i;
                if (x < 0 || x >= screen->width || src[i] == kKey) continue;
                dst[x] = src[i];
            }
        }
        ui_layer_mark(p->win_x, p->win_y, &c, 0, kKey);
    }
}
