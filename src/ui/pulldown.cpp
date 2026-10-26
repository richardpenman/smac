// PullDown (gfx 0x5F8800..0x5FA790): see pulldown.h.
#include "ui/pulldown.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>

#include "engine/globals.h"
#include "platform/ui_platform.h"
#include "ui/basewin.h"
#include "ui/basewin_internal.h"
#include "ui/font_text.h"
#include "ui/popup.h"
#include "ui/win.h"

namespace {

constexpr int32_t kMargin = 10;  // 0x696E04
constexpr int32_t kBorder = 1;  // 0x696E1C (popups_normal)
constexpr int32_t kKey = 9;

std::vector<PullDown*> g_open;

// The pull-down font (0x9B7B68 = 0x9457EC, config_popups: size 16, 12 at 800 pixels wide).
Font* pd_font() { return find_font(ScreenWidth == 800 ? 12 : 16, 0); }
int32_t line_height() {
    Font* f = pd_font();
    return f->field_0 < 0 ? f->height : f->ascent_above + f->field_0;
}

int32_t find(const PullDown* self, int32_t id) {
    for (size_t i = 0; i < self->items.size() && i < 64; i++) {
        if (self->items[i].id == -1) return -1;
        if (self->items[i].id == id) return (int32_t)i;
    }
    return -1;
}

// PullDown_compute_length (0x5F9D30) for one added string (with its '&' and '|').
void compute_length(PullDown* self, const char* s) {
    Font* f = pd_font();
    const char* bar = std::strchr(s, '|');
    if (!bar) {
        self->max_width = std::max(self->max_width, Font_width(f, s));
        return;
    }
    int32_t w = Font_width_2(f, s, (int32_t)(bar - s)) + Font_width(f, "  ") * 2 + Font_width(f, bar + 1);
    self->max_width = std::max(self->max_width, w);
}

// AlphaMenu_fill_func (0x403AF0): the row (selected: box 29, else box 27) over the window,
// clipped to x 1..width-1 of the row.
void fill_row(PullDown* self, bool selected, const UiRect& row) {
    UiRect clip = {1, row.top, self->w - 1, row.bottom};
    Buffer_set_clip(&self->canvas, &clip);
    UiRect all = {0, 0, self->w, self->h};
    Buffer_box_sprite(&self->canvas, &all, box_def(selected ? 29 : 27));
    Buffer_set_clip(&self->canvas, &all);
}

// PullDown_on_redraw (0x5F98B0) with AlphaMenu's fill functions.
void redraw(PullDown* self) {
    Buffer* c = &self->canvas;
    Font* f = pd_font();
    UiRect all = {0, 0, self->w, self->h};
    Buffer_set_clip(c, &all);
    Buffer_set_font(c, f, nullptr, nullptr, nullptr);
    Buffer_box_sprite(c, &all, box_def(27));  // AlphaMenu_backfill_func (0x403B70)
    int32_t lh = Buffer_text_line_height(c);
    UiRect r = {kMargin + kBorder, kBorder, self->w - kMargin - kBorder, kBorder + lh};
    for (size_t i = 0; i < self->items.size(); i++) {
        const PullDownItem& it = self->items[i];
        if (!(it.flags & 1)) continue;
        if (it.flags & 4) {
            int32_t width = self->w - 2 * kBorder;
            Buffer_hline_2(c, kBorder, kBorder + width, lh / 2 + r.top, text_color(0x8C6C1C));
            Buffer_hline_2(c, kBorder, kBorder + width, lh / 2 + 1 + r.top, text_color(0x8C6C1C));
        } else {
            int32_t color;
            if ((int32_t)i == self->selected) {
                fill_row(self, true, r);
                color = (it.flags & 2) ? text_color(0x8C6BF4) : text_color(0x8C6C0C);
            } else {
                fill_row(self, false, r);
                color = (it.flags & 2) ? text_color(0x8C6C14) : text_color(0x8C6C04);
            }
            Buffer_set_text_color(c, color, -1, 1, 1);
            if (!it.text.empty()) Buffer_write_l_4(c, it.text.c_str(), &r, (int32_t)it.text.size());
            if (it.accel >= 0 && it.accel < (int32_t)it.text.size()) {
                int32_t cw = Buffer_text_width_2(c, it.text.c_str() + it.accel, 1);
                int32_t y = r.bottom - (r.bottom - r.top - Buffer_text_line_height(c)) / 2 - f->internal_leading;
                int32_t x = Buffer_text_width_2(c, it.text.c_str(), it.accel) + r.left;
                Buffer_hline_2(c, x, x + cw, y, color);
            }
            if (!it.shortcut.empty()) Buffer_write_right_l_6(c, it.shortcut.c_str(), &r, (int32_t)it.shortcut.size());
        }
        r.top += lh;
        r.bottom += lh;
    }
    Buffer_set_clip(c, nullptr);
    ui_invalidate();
}

// The shown row under y (window coordinates) as an item index; -1 none or a separator
// (PullDown_mouse_move / on_left_up).
int32_t row_at(const PullDown* self, int32_t y) {
    int32_t row = (y - kBorder) / line_height();
    if (y - kBorder < 0) return -1;
    int32_t n = (int32_t)self->items.size(), seen = -1, i = 0;
    for (; i < n; i++) {
        if (self->items[i].flags & 1) seen++;
        if (seen == row) break;
    }
    if (i == n || (self->items[i].flags & 4)) return -1;
    return i;
}

bool inside(const PullDown* self, int32_t x, int32_t y) {
    return x >= self->x && x < self->x + self->w && y >= self->y && y < self->y + self->h;
}

}  // namespace

void PullDown_init(PullDown* self, bool menu) {
    self->items.clear();
    self->max_width = 0;
    self->selected = -1;
    self->lines = 0;
    self->dirty = true;
    self->menu = menu;
}

int32_t PullDown_add_item(PullDown* self, int32_t id, const char* text) {
    if (self->items.size() >= 64) return 1;
    self->items.emplace_back();  // the count grows first (an error below leaves an empty row)
    if (!text || id == -1) return 3;
    if (find(self, id) >= 0) return 1;
    PullDownItem& it = self->items.back();
    it.id = id;
    std::string s = text;
    size_t bar = s.find('|');
    std::string label = bar == std::string::npos ? s : s.substr(0, bar);
    size_t amp = label.find('&');
    if (amp == std::string::npos || (amp + 1 < label.size() && label[amp + 1] == ' ')) {
        it.text = label;
    } else {
        it.text = label.substr(0, amp);
        if (amp + 1 >= label.size() || label[amp + 1] != '&') it.accel = (int32_t)it.text.size();
        it.text += label.substr(amp + 1);
    }
    if (bar != std::string::npos) it.shortcut = s.substr(bar + 1);
    it.flags = 1;
    self->dirty = true;
    compute_length(self, text);
    self->lines++;
    return 0;
}

int32_t PullDown_add_separator(PullDown* self, int32_t id) {
    if (self->items.size() >= 64) return 1;
    if (id == -1) return 3;
    PullDownItem it;
    it.id = id;
    it.flags = 5;
    self->items.push_back(it);
    self->lines++;
    self->dirty = true;
    return 0;
}

int32_t PullDown_hide_item(PullDown* self, int32_t id) {
    int32_t i = find(self, id);
    if (i < 0) return 0xB;
    if (self->items[i].flags & 1) {
        self->items[i].flags &= ~1u;
        self->lines--;
    }
    self->dirty = true;
    return 0;
}

int32_t PullDown_show_item(PullDown* self, int32_t id) {
    int32_t i = find(self, id);
    if (i < 0) return 0xB;
    if (!(self->items[i].flags & 1)) {
        self->items[i].flags |= 1;
        self->lines++;
    }
    self->dirty = true;
    return 0;
}

int32_t PullDown_disable_item(PullDown* self, int32_t id) {
    int32_t i = find(self, id);
    if (i < 0) return 0xB;
    self->items[i].flags |= 2;
    self->dirty = true;
    return 0;
}

int32_t PullDown_enable_item(PullDown* self, int32_t id) {
    int32_t i = find(self, id);
    if (i < 0) return 0xB;
    self->items[i].flags &= ~2u;
    self->dirty = true;
    return 0;
}

int32_t PullDown_check_item(PullDown* self, int32_t id) {
    int32_t i = find(self, id);
    if (i < 0) return 0xB;
    self->items[i].flags |= 8;
    self->dirty = true;
    return 0;
}

int32_t PullDown_get_selected(const PullDown* self) {
    int32_t s = self->selected;
    if (s == -1 || s >= (int32_t)self->items.size() || (self->items[s].flags & 2)) return -1;
    return s;
}

int32_t pulldown_item_id(const PullDown* self, int32_t index) {
    if (index < 0 || index >= (int32_t)self->items.size()) return -1;
    return self->items[index].id;
}

int32_t PullDown_show(PullDown* self, int32_t x, int32_t y) {
    textcolor_load();
    popup_art_init();
    // Size: the widest row plus the margins; the rows of the font's line height.
    self->w = self->max_width + (kBorder + kMargin) * 2;
    self->h = self->lines * line_height() + kBorder * 2;
    self->selected = -1;
    if (!self->menu) {
        x++, y++;
        if (x + self->w > ScreenWidth) x -= self->w;
        if (y + self->h > ScreenHeight) y -= self->h;
        x = std::max(x, 0);
        y = std::max(y, 0);
    } else {
        x = std::max(x, 0);
        y = std::max(y, 0);
        if (x + self->w > ScreenWidth) x = ScreenWidth - self->w;
        if (y + self->h > ScreenHeight) y = ScreenHeight - self->h;
    }
    self->x = x;
    self->y = y;
    Buffer_ctor(&self->canvas);
    Buffer_init(&self->canvas, self->w, self->h, 8, 0);
    redraw(self);
    g_open.push_back(self);
    self->shown = true;
    std::fprintf(stderr, "pulldown shown: %d items at %d,%d (%dx%d)\n", (int32_t)self->items.size(), x, y, self->w,
                 self->h);
    for (const PullDownItem& it : self->items)
        if ((it.flags & 5) == 1)
            std::fprintf(stderr, "pulldown item %d%s: %s\n", it.id, (it.flags & 2) ? " (disabled)" : "",
                         it.text.c_str());
    // The modal loop (Win modal, vtable +0xE4).
    bool done = false;
    auto set_sel = [&](int32_t s) {
        if (s != self->selected) {
            self->selected = s;
            redraw(self);
        }
    };
    while (!done) {
        UiInput in;
        if (!ui_wait_input(&in)) {
            self->selected = -1;
            break;
        }
        int32_t lx = in.x - self->x, ly = in.y - self->y;
        switch (in.type) {
        case UiInput::KEY: {
            int32_t key = in.key & ~0x40000, vk = key & 0xFFFF;  // PullDown_on_key_click
            int32_t n = (int32_t)self->items.size(), cur = self->selected;
            if (vk == VKEY_RETURN) {
                done = true;
            } else if (vk == VKEY_ESCAPE) {
                self->selected = -1;
                done = true;
            } else if (vk == VKEY_UP || vk == 0x68) {
                int32_t s = cur < 1 ? n - 1 : cur - 1;
                while (s >= 0 && !((self->items[s].flags & 1) && !(self->items[s].flags & 4))) s--;
                set_sel(s < 0 ? -1 : s);
            } else if (vk == VKEY_DOWN || vk == 0x62) {
                int32_t s = cur == n - 1 ? 0 : cur + 1;
                while (s < n && !((self->items[s].flags & 1) && !(self->items[s].flags & 4))) s++;
                set_sel(s == n ? -1 : s);
            } else {
                // The accelerators (toupper / tolower of the character).
                int32_t ch = vk;
                for (int32_t i = 0; i < n; i++) {
                    const PullDownItem& it = self->items[i];
                    if (!(it.flags & 1) || (it.flags & 2) || it.accel < 0) continue;
                    char a = it.text[it.accel];
                    if (a == std::toupper(ch) || a == std::tolower(ch)) {
                        self->selected = i;
                        redraw(self);
                        done = true;
                        break;
                    }
                }
            }
            break;
        }
        case UiInput::MOVE:
            // PullDown_on_mouse_move: the row under the mouse; outside: none.
            if (inside(self, in.x, in.y)) set_sel(row_at(self, ly));
            else set_sel(-1);
            (void)lx;
            break;
        case UiInput::LDOWN:
        case UiInput::RDOWN:
            // PullDown_on_left_down: a press outside closes (nothing chosen).
            if (!inside(self, in.x, in.y)) {
                self->selected = -1;
                done = true;
            }
            break;
        case UiInput::LUP:
        case UiInput::RUP:
            // PullDown_on_left_up: a release on a row chooses it (a separator: none, stays open).
            if (inside(self, in.x, in.y)) {
                self->selected = row_at(self, ly);
                if (self->selected == -1) redraw(self);
                else done = true;
            }
            break;
        }
    }
    g_open.erase(std::remove(g_open.begin(), g_open.end(), self), g_open.end());
    self->shown = false;
    ui_invalidate();
    std::fprintf(stderr, "pulldown answer: %d (id %d)\n", self->selected,
                 pulldown_item_id(self, PullDown_get_selected(self)));
    return self->selected;
}

void pulldown_draw_windows(Buffer* screen) {
    for (PullDown* p : g_open) {
        const Buffer& c = p->canvas;
        for (int32_t j = 0; j < c.height; j++) {
            int32_t y = p->y + j;
            if (y < 0 || y >= screen->height) continue;
            const uint8_t* src = c.data + (size_t)j * c.stride;
            uint8_t* dst = screen->data + (size_t)y * screen->stride;
            for (int32_t i = 0; i < c.width; i++) {
                int32_t x = p->x + i;
                if (x < 0 || x >= screen->width || src[i] == kKey) continue;
                dst[x] = src[i];
            }
        }
        ui_layer_mark(p->x, p->y, &c, 0, kKey);
    }
}

// ---- the menu column's pull-downs (AlphaMenu: the open menu's PullDown, drawn while the column
// runs its own input) ----

void pulldown_place(PullDown* self, int32_t x, int32_t y) {
    textcolor_load();
    popup_art_init();
    self->w = self->max_width + (kBorder + kMargin) * 2;
    self->h = self->lines * line_height() + kBorder * 2;
    x = std::max(x, 0);
    y = std::max(y, 0);
    if (x + self->w > ScreenWidth) x = ScreenWidth - self->w;
    if (y + self->h > ScreenHeight) y = ScreenHeight - self->h;
    self->x = x;
    self->y = y;
}

void pulldown_render(PullDown* self, Buffer* screen) {
    Buffer_ctor(&self->canvas);
    Buffer_init(&self->canvas, std::max(1, self->w), std::max(1, self->h), 8, 0);
    redraw(self);
    const Buffer& c = self->canvas;
    for (int32_t j = 0; j < c.height; j++) {
        int32_t y = self->y + j;
        if (y < 0 || y >= screen->height) continue;
        const uint8_t* src = c.data + (size_t)j * c.stride;
        uint8_t* dst = screen->data + (size_t)y * screen->stride;
        for (int32_t i = 0; i < c.width; i++) {
            int32_t x = self->x + i;
            if (x < 0 || x >= screen->width || src[i] == kKey) continue;
            dst[x] = src[i];
        }
    }
}

int32_t pulldown_row_at(const PullDown* self, int32_t y) { return row_at(self, y); }
