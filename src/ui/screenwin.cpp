// Native frame of the full-screen modal windows (screenwin.h).
#include "ui/screenwin.h"
#include "ui/slide.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>

#include "engine/globals.h"
#include "engine/strings.h"
#include "ui/basewin.h"
#include "ui/font_text.h"
#include "ui/popup.h"

namespace {
// Never destroyed: global windows (e.g. the desktop) call sw_forget from their destructors at exit.
std::vector<ScreenWin*>& g_stack = *new std::vector<ScreenWin*>;

void blit_keyed(Buffer* screen, Buffer* src, int32_t x0, int32_t y0) {
    if (!src->data) return;
    for (int32_t y = 0; y < src->height; y++) {
        int32_t sy = y0 + y;
        if (sy < 0 || sy >= screen->height) continue;
        const uint8_t* s = src->data + (size_t)y * src->stride;
        uint8_t* d = screen->data + (size_t)sy * screen->stride;
        for (int32_t x = 0; x < src->width; x++) {
            int32_t sx = x0 + x;
            if (sx < 0 || sx >= screen->width || s[x] == 9) continue;
            d[sx] = s[x];
        }
    }
}

bool opaque(Buffer* c, int32_t px, int32_t py) {
    return c->data && px >= 0 && py >= 0 && px < c->width && py < c->height &&
           c->data[(size_t)py * c->stride + px] != 9;
}

BaseButton* button_at(ScreenWin* s, int32_t parent, int32_t x, int32_t y) {
    for (BaseButton* b : s->buttons)
        if (b->shown && b->parent == parent && in_box(x, y, b->r)) return b;
    return nullptr;
}
}  // namespace

std::string sw_upper(std::string s) {
    for (char& c : s) c = (char)std::toupper((uint8_t)c);
    return s;
}

std::string sw_label(uint32_t off) { return label_get((int32_t)(off / 4)); }

void sw_init(ScreenWin* s, int32_t x, int32_t y, int32_t w, int32_t h, bool iface) {
    popup_art_init();
    textcolor_load();
    basewin_mi_init();
    s->x = x;
    s->y = y;
    if (!s->canvas.data || s->canvas.width != w || s->canvas.height != h) {
        Buffer_ctor(&s->canvas);
        Buffer_init(&s->canvas, w, h, 8, 0);
    }
    UiRect all{0, 0, w, h};
    Buffer_set_clip(&s->canvas, &all);
    Buffer_fill_3(&s->canvas, &all, 9);
    s->iface = iface;
    if (iface) {
        Buffer* src = basewin_mi_canvas();
        if (!s->mi.data) {
            Buffer_ctor(&s->mi);
            Buffer_init(&s->mi, src->width, src->height, 8, 0);
        }
        sw_restore_iface(s);
    }
    spot_init(&s->spots[0], 0x100);
    spot_init(&s->spots[1], 0x100);
}

void sw_restore_iface(ScreenWin* s) {
    if (!s->mi.data) return;
    const std::vector<uint8_t>& back = basewin_mi_back();
    UiRect all{0, 0, s->mi.width, s->mi.height};
    Buffer_set_clip(&s->mi, &all);
    if (back.size() >= (size_t)s->mi.stride * s->mi.height) std::memcpy(s->mi.data, back.data(), back.size());
    // MainInterface_restore_back: the panes filled with the centre of box 9.
    Buffer* tile = box_def(9)->center;
    auto fill = [&](const UiRect& p, int32_t dx) {
        int32_t x = p.left + dx, y = p.top + 1;
        Buffer_tile(&s->mi, tile, x, y, x, y, p.right - 1 - x, p.bottom - 1 - y);
    };
    fill(mi_rect(kMiPaneQueue), 1);
    fill(mi_rect(kMiPaneBottom), 0);
    fill(mi_rect(kMiPaneCenter), 1);
    fill(mi_rect(kMiPaneRight), 1);
}

BaseButton* sw_button(ScreenWin* s, const char* text, int32_t id, int32_t x, int32_t y, int32_t w, int32_t h,
                      int32_t parent, Font* font) {
    BaseButton* b = new BaseButton();
    bw_button_ctor(b);
    bw_button_init(b, text, id, x, y, w, h, parent);
    b->font = font;
    b->shown = true;
    s->buttons.push_back(b);
    return b;
}

void sw_clear_buttons(ScreenWin* s) {
    for (BaseButton* b : s->buttons) delete b;
    s->buttons.clear();
    s->pressed = nullptr;
}

void sw_font(Buffer* c, Font* font, int32_t color, int32_t shadow) {
    Buffer_set_font(c, font, font, font, nullptr);
    Buffer_set_text_color(c, color, shadow, 1, 1);
    Buffer_set_text_color2(c, color, shadow, 1, 1);
    Buffer_set_text_color3(c, color, shadow, 1, 1);
}

// Texts are written with the font switches of the original's strings ("{...}" the second font
// slot, "[...]" the third: Buffer_write_multi_font_raw_l); sw_font sets every slot.
int32_t sw_text(ScreenWin* s, Buffer* c, const std::string& text, int32_t x, int32_t y) {
    if (s) s->texts.push_back(text);
    if (text.empty() || !c->fonts[0]) return x;
    return Buffer_write_multi_font_raw_l(c, text.c_str(), x, y, (int32_t)text.size());
}

int32_t sw_text_cent(ScreenWin* s, Buffer* c, const std::string& text, const UiRect& r) {
    if (s) s->texts.push_back(text);
    if (text.empty() || !c->fonts[0]) return 0;
    int32_t w = Buffer_text_width_2(c, text.c_str(), (int32_t)text.size());
    int32_t x = ((r.right - w) - r.left) / 2 + r.left;
    int32_t y = ((r.bottom - c->fonts[0]->ascent_above) - r.top) / 2 + r.top;
    return Buffer_write_multi_font_raw_l(c, text.c_str(), x, y, (int32_t)text.size());
}

int32_t sw_text_right(ScreenWin* s, Buffer* c, const std::string& text, const UiRect& r) {
    if (s) s->texts.push_back(text);
    if (text.empty()) return 0;
    return Buffer_write_right_l_6(c, text.c_str(), &r, (int32_t)text.size());
}

void sw_close(ScreenWin* s) {
    s->open = false;
    ui_invalidate();
}

static void push(ScreenWin* s) {
    s->seq = ui_window_seq();
    g_stack.push_back(s);
}

static void redraw(ScreenWin* s) {
    s->texts.clear();
    s->redraw();
    ui_invalidate();
}

void sw_show(ScreenWin* s) {
    if (std::find(g_stack.begin(), g_stack.end(), s) == g_stack.end()) push(s);
    redraw(s);
}

void sw_forget(ScreenWin* s) {
    g_stack.erase(std::remove(g_stack.begin(), g_stack.end(), s), g_stack.end());
}

void sw_hide(ScreenWin* s) {
    g_stack.erase(std::remove(g_stack.begin(), g_stack.end(), s), g_stack.end());
    ui_invalidate();
}

void sw_run(ScreenWin* s) {
    s->open = true;
    bool shown = std::find(g_stack.begin(), g_stack.end(), s) != g_stack.end();
    if (!shown) push(s);
    redraw(s);
    if (s->slide && !shown) {
        // slide_show: the window rises into view (drawn without it first).
        g_stack.erase(std::remove(g_stack.begin(), g_stack.end(), s), g_stack.end());
        ui_present();
        push(s);
        slide_show_window(&s->canvas, s->x, s->y, nullptr, false);
    }
    const UiRect& mp = mi_rect(kMiPos);
    // Win_set_modal's callback (a network game's messages while the window waits); a message may
    // close the window (Win_release_modal), which ends the wait.
    int32_t old_cb = ui_set_modal_callback(s->modal_cb);
    const bool* old_open = ui_set_modal_open(&s->open);
    while (s->open && !ControlTurnA) {
        if (Win_is_visible(&BaseWin)) {
            // A base window opened from the screen takes the input until it closes.
            wait_task();
            if (!Win_is_visible(&BaseWin)) redraw(s);
            continue;
        }
        UiInput in;
        if (!ui_wait_input(&in)) break;
        if (in.type == UiInput::KEY) {
            s->on_key(in.key);
            if (s->open) redraw(s);
            continue;
        }
        bool right = in.type == UiInput::RDOWN || in.type == UiInput::RUP;
        if (in.type == UiInput::MOVE) {
            ui_bubble_clear();  // (Win_clear_bubble_text)
            s->on_move(in.x - s->x, in.y - s->y);
            continue;
        }
        if (in.type == UiInput::RDOWN) continue;
        if (in.type == UiInput::LDOWN) ui_bubble_clear();
        int32_t parent = -1, lx = 0, ly = 0;
        if (s->iface && in_box(in.x, in.y, mp) &&
            (opaque(&s->mi, in.x - mp.left, in.y - mp.top) || button_at(s, 1, in.x - mp.left, in.y - mp.top))) {
            parent = 1;
            lx = in.x - mp.left;
            ly = in.y - mp.top;
        } else if (in.x >= s->x && in.y >= s->y && in.x < s->x + s->canvas.width && in.y < s->y + s->canvas.height) {
            parent = 0;
            lx = in.x - s->x;
            ly = in.y - s->y;
        }
        if (parent < 0) continue;
        BaseButton* b = button_at(s, parent, lx, ly);
        if (right && b && !b->bubble.empty()) {
            // BaseButton_on_right_click -> BaseButton_timer_callback(0): its help bubble over it.
            int32_t ox = parent ? mp.left : s->x, oy = parent ? mp.top : s->y;
            ui_bubble_set_rect(b->bubble.c_str(),
                               UiRect{b->r.left + ox, b->r.top + oy, b->r.right + ox, b->r.bottom + oy});
            continue;
        }
        if (right) {  // a right click (on release): the spot's menu
            int32_t index = -1, type = 0;
            spot_check(&s->spots[parent], lx, ly, &index, &type);
            s->on_click(parent, index, type, lx, ly, true, false);
            if (s->open) redraw(s);
            continue;
        }
        if (in.type == UiInput::LDOWN) {
            if (b) {
                b->pressed = 1;
                s->pressed = b;
                ui_invalidate();
            } else if (in.double_click) {
                int32_t index = -1, type = 0;
                spot_check(&s->spots[parent], lx, ly, &index, &type);
                s->on_click(parent, index, type, lx, ly, false, true);
                if (s->open) redraw(s);
            }
            continue;
        }
        // Release: a button's click when released over it, else a click on the spot.
        if (s->pressed) {
            BaseButton* p = s->pressed;
            p->pressed = 0;
            s->pressed = nullptr;
            ui_invalidate();
            if (p == b) {
                s->on_button(b->id);
                if (s->open) redraw(s);
            }
            continue;
        }
        if (b) continue;
        int32_t index = -1, type = 0;
        spot_check(&s->spots[parent], lx, ly, &index, &type);
        s->on_click(parent, index, type, lx, ly, false, false);
        if (s->open) redraw(s);
    }
    ui_set_modal_callback(old_cb);
    ui_set_modal_open(old_open);
    s->open = false;
    g_stack.erase(std::remove(g_stack.begin(), g_stack.end(), s), g_stack.end());
    ui_invalidate();
    if (s->slide) slide_hide_window(&s->canvas, s->x, s->y);  // slide_hide: it sinks out of view
}

ScreenWin* screen_top() { return g_stack.empty() ? nullptr : g_stack.back(); }

void screens_compose(Buffer* screen, uint32_t from, uint32_t to) {
    const UiRect& mp = mi_rect(kMiPos);
    for (ScreenWin* s : g_stack) {
        if (s->seq < from || s->seq >= to) continue;
        blit_keyed(screen, &s->canvas, s->x, s->y);
        ui_layer_mark(s->x, s->y, &s->canvas, s->layer);
        for (BaseButton* b : s->buttons) {
            if (!b->shown || b->parent != 0) continue;
            bw_button_draw(b);
            blit_keyed(screen, &b->canvas, s->x + b->r.left, s->y + b->r.top);
            ui_layer_mark(s->x + b->r.left, s->y + b->r.top, &b->canvas, 0);
        }
        if (!s->iface) continue;
        blit_keyed(screen, &s->mi, mp.left, mp.top);
        for (BaseButton* b : s->buttons) {
            if (!b->shown || b->parent != 1) continue;
            bw_button_draw(b);
            blit_keyed(screen, &b->canvas, mp.left + b->r.left, mp.top + b->r.top);
        }
    }
}

void screens_log_state() {
    ScreenWin* s = screen_top();
    if (!s) {
        std::fprintf(stderr, "screenstate none\n");
        return;
    }
    std::fprintf(stderr, "screenstate %s\n", s->name);
    for (const std::string& t : s->texts) std::fprintf(stderr, "screentext %s\n", t.c_str());
    s->log_state();
}

void screens_log_faction() {
    int32_t f = CurrentPlayerFaction;
    const Faction& fa = Factions[f];
    std::fprintf(stderr,
                 "factionstate f%d credits %d social %d %d %d %d pending %d %d %d %d paid %d alloc %d %d %d "
                 "effects %d %d %d %d %d %d %d %d %d %d %d\n",
                 f, fa.energy_credits, fa.SE_Politics, fa.SE_Economics, fa.SE_Values, fa.SE_Future,
                 fa.SE_Politics_pending, fa.SE_Economics_pending, fa.SE_Values_pending, fa.SE_Future_pending,
                 fa.SE_upheaval_cost_paid, 10 - fa.SE_alloc_psych - fa.SE_alloc_labs, fa.SE_alloc_psych,
                 fa.SE_alloc_labs, fa.SE_economy_pending, fa.SE_effic_pending, fa.SE_support_pending,
                 fa.SE_talent_pending, fa.SE_morale_pending, fa.SE_police_pending, fa.SE_growth_pending,
                 fa.SE_planet_pending, fa.SE_probe_pending, fa.SE_industry_pending, fa.SE_research_pending);
}
