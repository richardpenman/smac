// The technology tree (ReportWin_draw_labs 0x498B30, ReportWin_labs_line 0x49B450,
// ReportWin_on_button_passover 0x49B220): see labs.h.
//
// Faithful to the asm: the geometry (the report's inner rectangle or the datalinks' entry
// rectangle; the columns and rows of the buttons, the centre box), the boxes drawn (box sprites by
// their index, the connector lines as clipped boxes, the arrows of interface.pcx), the texts and
// their fonts and colours, the two passes of the centre box (measure, then draw), the lists of
// what the technology allows (the original's order and formats; the social models and
// specialists written again after each item, as the original does), the prerequisites' and
// successors' buttons (at most four successors; their layouts for one to four) and the
// highlighting under the mouse. The report's top: the research (or "[LABORATORY SUMMARY]" with
// blind research), the progress bar (this turn's research as a hollow box), the DISCOV and
// NEXTDISCOV lines. The button font (0x6E84B4, a Font never initialised in the original: the
// buttons fall back to their default) is Arial 14 bold here, as the preferences window's.
#include "ui/labs.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "engine/diplo_act.h"
#include "engine/diplomacy.h"
#include "engine/globals.h"
#include "engine/parse.h"
#include "engine/rules.h"
#include "engine/strings.h"
#include "engine/tech.h"
#include "engine/turn.h"
#include "engine/ui_bridge.h"
#include "engine/zoc.h"
#include "ui/basewin.h"
#include "ui/console.h"
#include "ui/font_text.h"
#include "ui/help.h"
#include "ui/popup.h"
#include "ui/screenwin.h"
#include "ui/sprites.h"
#include "ui/techwin.h"

namespace {

// ReportWin's tree fields.
struct Tree {
    int32_t col[3]{};  // +0x594C..: the columns' x (prerequisites, centre, successors)
    int32_t cy = 0, y0 = 0, y4 = 0;  // +0x5958..: the centre row, the outer rows of three
    int32_t ya = 0, yb = 0, yc = 0, yd = 0;  // +0x5964..: the rows of four (and of two: yb, yc)
    int32_t bw = 0, bh = 0, cw = 0;  // +0x59A0..: button width, height; centre width
    UiRect centre{};  // +0xA28
    int32_t centre_h = 0;  // +0xA24: the centre box's text height
    UiRect btn[6]{};  // the buttons' rectangles (ids 0x379..0x37E)
    bool shown[6]{};
    int32_t hover = -1;  // the button under the mouse
};

Tree g_tree;
int32_t g_tech = 0;

Font* button_font() {
    static Font f;
    static bool init = false;
    if (!init) {
        init = true;
        Font_ctor(&f);
        Font_init(&f, "Arial", 0xE, 1);
    }
    return &f;
}

// The datalinks' fonts (Datalink_init at 1024 x 768: +0x1030C Arial 14, +0x102E4 Arial 14
// underlined, the links').
Font* dl_font(int32_t style) {
    static Font f[2];
    static bool init = false;
    if (!init) {
        init = true;
        Font_ctor(&f[0]);
        Font_init(&f[0], "Arial", 0xE, 0);
        Font_ctor(&f[1]);
        Font_init(&f[1], "Arial", 0xE, 4);
    }
    return &f[style ? 1 : 0];
}

Sprite g_arrows[3];  // 0x75AFD0

void box(Buffer* c, int32_t l, int32_t t, int32_t r, int32_t b, int32_t index) {
    UiRect rc{l, t, r, b};
    Buffer_box_sprite(c, &rc, box_def(index));
}

void clip(Buffer* c, int32_t l, int32_t t, int32_t r, int32_t b) {
    UiRect rc{l, t, r, b};
    Buffer_set_clip(c, &rc);
}

void clip_all(Buffer* c) {
    UiRect rc{0, 0, c->width, c->height};
    Buffer_set_clip(c, &rc);
}

void arrow(Buffer* c, bool hl, int32_t x, int32_t y) {
    Sprite* s = &g_arrows[hl ? 2 : 0];
    if (s->width) pfncSpriteStretchCopyToCanvas(s, c, s->key, x, y);
}

int32_t cat_frame(int32_t cat) {
    static const int32_t k[4] = {38, 41, 44, 47};
    return cat >= 0 && cat < 4 ? k[cat] : -1;
}
int32_t cat_fill(int32_t cat) {
    static const int32_t k[4] = {36, 39, 42, 45};
    return cat >= 0 && cat < 4 ? k[cat] : -1;
}

// tech_category; for no technology (-1) the original reads Tech[-1], the end of the armour table:
// its AI_wealth / AI_power are Armor[13]'s name and short name pointers, the latter allocated
// after the former: CONQUER (3).
int32_t category(int32_t t) { return t >= 0 && t < 89 ? tech_category(t) : (t == -1 ? 3 : -1); }

// A horizontal stretch of a box's top edge: the box {l, y, r, y + 0x14} drawn clipped to
// {l + 7, y, r - 7, y + 13} (the original's "+7, clip, -7" steps).
void hline(Buffer* c, int32_t l, int32_t y, int32_t r, int32_t index) {
    clip(c, l + 7, y, r - 7, y + 0x14 - 7);
    box(c, l, y, r, y + 0x14, index);
}

// ReportWin_labs_line: the line from a button at (x2, y2) (or (x1, y1) for a successor) to the
// centre, highlighted (hl) or not.
void labs_line(Buffer* c, int32_t x1, int32_t y1, int32_t x2, int32_t y2, int32_t w, int32_t h, bool hl) {
    Tree& t = g_tree;
    int32_t b = hl ? 5 : 3;
    int32_t h2 = h / 2;
    int32_t mid = t.cy + h2;
    (void)x2;
    if (x1 == t.col[1]) {
        // A prerequisite (left column).
        int32_t xm = (t.col[0] + t.col[1] + w) / 2;
        if (y2 == t.yb || y2 == t.yc) {
            UiRect r{t.col[0] + 3 + w, t.yb + h2, xm - 2, t.yc + h2};
            if (y2 == t.yb) r.bottom = mid;
            else r.top = mid + 1;
            Buffer_set_clip(c, &r);
            box(c, t.col[0] - 4 + w, t.yb + h2, xm - 2, t.yc + h2, b);
        } else {
            hline(c, t.col[0] - 4 + w, mid, xm + 5, b);
        }
        clip_all(c);
        hline(c, xm - 9, mid, t.col[1] + 4, b);
        clip_all(c);
        arrow(c, hl, t.col[1] - 10, t.cy - 6 + h2);
        return;
    }
    // A successor (right column, at (x1, y1)).
    int32_t xm = (t.cw + t.col[2] + t.col[1]) / 2;
    int32_t l = xm - 2;
    auto pair = [&](int32_t top, int32_t bottom) {
        UiRect r{l, top + h2, t.col[2] - 3, bottom + h2};
        if (y1 == top) {
            r.bottom = mid;
            arrow(c, hl, t.col[2] - 10, top - 6 + h2);
        } else {
            r.top = mid + 1;
            arrow(c, hl, t.col[2] - 10, bottom - 7 + h2);
        }
        Buffer_set_clip(c, &r);
        box(c, l, top + h2, t.col[2] + 4, bottom + h2, b);
    };
    if (y1 == t.y0 || y1 == t.y4) {
        pair(t.y0, t.y4);
    } else if (y1 == t.ya || y1 == t.yd) {
        pair(t.ya, t.yd);
    } else if (y1 != t.yb && y1 != t.yc) {
        arrow(c, hl, t.col[2] - 10, mid - 6);
        hline(c, xm - 9, mid, t.col[2] + 4, b);
    } else {
        pair(t.yb, t.yc);
    }
    clip_all(c);
    hline(c, t.cw - 4 + t.col[1], mid, xm + 5, b);
    clip_all(c);
}

// The tree's buttons (BaseButton_init, the font 0x6E84B4, white text for a known technology, the
// boxes 27..29) and the box around each.
void tree_button(ScreenWin* s, Buffer* c, int32_t slot, int32_t tech, int32_t x, int32_t y, int32_t faction) {
    Tree& t = g_tree;
    StrBuffer[0] = 0;
    say_tech_2(StrBuffer, tech, 0);
    std::string name = sw_upper(StrBuffer);
    BaseButton* b = sw_button(s, name.c_str(), 0x379 + slot, x, y, t.bw, t.bh, 0, button_font());
    if (has_tech(tech, faction)) {
        b->color = 0xFF;
        b->shadow = -1;
    }
    for (int32_t k = 0; k < 3; k++) b->box[k] = box_def(27 + k);
    t.btn[slot] = {x, y, x + t.bw, y + t.bh};
    t.shown[slot] = true;
    box(c, x - 3, y - 3, x + t.bw + 3, y + t.bh + 3, 3);
}

void clear_buttons(ScreenWin* s) {
    for (size_t i = 0; i < s->buttons.size();) {
        BaseButton* b = s->buttons[i];
        if (b->id >= 0x379 && b->id <= 0x37E) {
            if (s->pressed == b) s->pressed = nullptr;
            delete b;
            s->buttons.erase(s->buttons.begin() + (ptrdiff_t)i);
        } else {
            i++;
        }
    }
    for (bool& v : g_tree.shown) v = false;
}

// ReportWin_on_button_passover for button slot k.
void passover(Buffer* c, int32_t k, bool on) {
    Tree& t = g_tree;
    if (k < 0 || k > 5 || !t.shown[k]) return;
    UiRect r = t.btn[k];
    int32_t x1, y1, x2, y2;
    if (k < 2) {
        x1 = t.col[1];
        y1 = t.cy;
        x2 = r.left;
        y2 = r.top;
    } else {
        x1 = r.left;
        y1 = r.top;
        x2 = t.col[1];
        y2 = t.cy;
    }
    int32_t b = on ? 5 : 3;
    int32_t top = ((t.centre.bottom + t.centre.top) - t.centre_h) / 2;
    box(c, t.centre.left - 3, top - 9, t.cw + t.centre.left + 3, t.centre_h + top + 6, b);
    box(c, r.left - 3, r.top - 3, r.right + 3, r.bottom + 3, b);
    labs_line(c, x1, y1, x2, y2, t.bw, t.bh, on);
}

std::string link(const char* name, int32_t cat, int32_t index) { return help_link(name, cat, index); }

}  // namespace

int32_t& labs_tree_tech() { return g_tech; }

void labs_clear(ScreenWin* s) {
    clear_buttons(s);
    g_tree.hover = -1;
}

void labs_load_arrows(Buffer* art) {
    for (int32_t k = 0; k < 3; k++) pfncSpriteFromCanvasRectTrans(&g_arrows[k], art, 9, 0x8C + k * 8, 0x25, 7, 0xD, 0);
}

void labs_draw(ScreenWin* s, Buffer* c, bool dl, int32_t faction, const UiRect& area, std::vector<std::string>* log) {
    Tree& t = g_tree;
    clear_buttons(s);
    t.hover = -1;
    Font* f12 = find_font(0xC, 0);
    Font* f14 = find_font(0xE, 0);
    Font* f16 = find_font(0x10, 0);
    int32_t lh = f12->field_0 < 0 ? f12->height : f12->ascent_above + f12->field_0;
    const int32_t inner_bottom = 0x161 + 200 - 0x35;  // ReportWin + 0x5980
    bool w800 = ScreenWidth == 800;
    auto put = [&](const std::string& l) {
        if (log) log->push_back(l);
    };
    int32_t left, top, right, top_bottom;  // local_60, local_5c, local_58, local_54
    if (!dl) {
        // The report: its inner rectangle (ReportWin_reset) and the top box.
        left = area.left;
        top = area.top;
        int32_t w = area.right - area.left;
        t.bw = 0xBE;
        t.cw = 0xBE;
        t.bh = 0x28;
        right = w + left;
        top_bottom = top + 0x46;
        t.centre.top = (w800 ? 0 : 0x62) + 0x1D + top_bottom;
        t.centre.left = (w - 0xBE) / 2 + left;
        t.centre.right = t.centre.left + 0xBE;
        t.centre.bottom = t.centre.top + 0xAE;
        UiRect r2{left + 0x46, top + 3, right - 3, 0x2F};  // the research box (beside the picture)
        UiRect r3{left + 0x46, 0x31, right - 3, top + 0x43};  // the progress bar
        energy_compute(faction, 0);
        int32_t cost = tech_rate(faction);
        const Faction& f = Factions[faction];
        int32_t accum = f.tech_accumulated;
        int32_t research = f.tech_research_id;
        Buffer_set_font(c, f12, f12, f14, nullptr);
        Buffer_set_text_color(c, 0x99, -1, 1, 1);
        Buffer_set_text_color2(c, 0xF2, -1, 1, 1);
        Buffer_set_text_color3(c, 0xE1, -1, 1, 1);
        std::string s1 = "[";
        box(c, left, top, right, top_bottom, 3);
        bool blind = !(GameState & STATE_OMNISCIENT_VIEW) && (GameRules & RULES_BLIND_RESEARCH);
        int32_t tx = r2.left + 3, ty = top + 3 + 5;
        if (blind) {
            r2.left = left + 3;
            tx = left + 8;
            Buffer_box_sprite(c, &r2, box_def(2));
            s1 += label_get(0xAC4 / 4);
            s1 += "]";
            Buffer_write_cent_l_5(c, s1.c_str(), tx, ty, r2.right - r2.left, (int32_t)s1.size());
            put("labs research " + s1);
            r3.left = left + 3;
        } else {
            if (research >= 0) {
                Sprite* sp = tech_picture(research);
                if (sp && sp->width) {
                    BlitDestScale = 0x40;
                    BlitSourceScale = sp->full_width ? sp->full_width : 1;
                    Sprite_draw_mono_2(sp, c, sp->key, left + 3, top + 3, mandate_color(category(research)));
                    BlitDestScale = BlitSourceScale = 1;
                }
            }
            Buffer_box_sprite(c, &r2, box_def(2));
            s1 += label_get(0x254 / 4);
            s1 += "  ]";
            tx = Buffer_write_l_3(c, s1.c_str(), tx, ty, (int32_t)s1.size());
            std::string line = s1;
            StrBuffer[0] = 0;
            std::string s2 = "[";
            if (research >= 0) {
                say_tech_2(StrBuffer, research, 0);
                s2 += sw_upper(StrBuffer);
            }
            if (research == 0x58) {
                s2 += " " + std::to_string(f.tech_count_transcendent + 1);
            } else if (research >= 0) {
                s2 += "]";
                tx = Buffer_write_l_3(c, s2.c_str(), tx, ty, (int32_t)s2.size()) + 0xF;
                line += s2;
                int32_t cat = category(research);
                Buffer_set_text_color3(c, mandate_color(cat), -1, 1, 1);
                s2 = "[" + std::string(Mandate[cat].name_caps) + " " + std::to_string(tech_recurse(research, 0));
            }
            s2 += "]";
            Buffer_write_l_3(c, s2.c_str(), tx, ty, (int32_t)s2.size());
            put("labs research " + line + " " + s2);
            spot_kill_type(&s->spots[0], 0);
            spot_add(&s->spots[0], 0x29A, 0, left, top, right - left, top_bottom - top);
            tx = r2.left + 3;
            ty = top + 3 + 5;
        }
        ty += lh + 3;
        // The progress bar: the category's frame, the accumulated part filled, this turn's part
        // as a hollow box.
        int32_t cat = category(research);
        int32_t rr = r3.right;
        if (cat_frame(cat) >= 0) Buffer_box_sprite(c, &r3, box_def(cat_frame(cat)));
        int32_t p = (rr - r3.left) * accum / std::max(cost, 1) + r3.left;
        int32_t pe;
        if (p < r3.left || rr < r3.left) pe = r3.left;
        else pe = p <= rr ? p : rr;
        clip(c, r3.left, r3.top, pe, r3.bottom);
        if (cat_fill(cat) >= 0) Buffer_box_sprite(c, &r3, box_def(cat_fill(cat)));
        clip_all(c);
        int32_t nx = (((rr - r3.left) * FactionCurrentLabs) / 100) / std::max(cost, 1) + pe;
        int32_t ne = pe;
        if (pe <= nx && pe <= rr) ne = nx <= rr ? nx : rr;
        UiRect r4{pe, r3.top, ne, r3.bottom};
        Buffer_set_clip(c, &r4);
        if (cat_fill(cat) >= 0) box(c, r4.left - 7, r4.top - 7, r4.right + 7, r4.bottom + 7, cat_fill(cat));
        clip_all(c);
        clip(c, r4.left + 1, r4.top + 1, r4.right - 1, r4.bottom - 1);
        if (cat_frame(cat) >= 0) box(c, r4.left - 6, r4.top - 6, r4.right + 6, r4.bottom + 6, cat_frame(cat));
        clip_all(c);
        put("labs bar " + std::to_string(pe - r3.left) + " " + std::to_string(ne - pe) + " of " +
            std::to_string(rr - r3.left));
        Buffer_set_text_color3(c, 0xE1, -1, 1, 1);
        parse_num(0, FactionCurrentBreakthroughTurns);
        parse_it(ScriptFileName, "DISCOV");
        std::string d1 = StrBuffer;
        tx = Buffer_write_l_3(c, StrBuffer, tx, ty, (int32_t)std::strlen(StrBuffer)) + 4;
        int32_t turns;
        if (FactionCurrentLabs == 0) {
            turns = 9999;
        } else {
            int32_t rem = std::min(std::max(cost - accum, 0), 99999999);
            turns = (FactionCurrentLabs - 1 + rem * 100) / FactionCurrentLabs;
        }
        parse_num(0, turns);
        parse_num(1, game_year(turns + CurrentTurn));
        parse_it(ScriptFileName, "NEXTDISCOV");
        Buffer_write_l_3(c, StrBuffer, tx, ty, (int32_t)std::strlen(StrBuffer));
        put("labs discov " + d1 + " " + StrBuffer);
    } else {
        Buffer_set_text_color_hyper(c, 0xFE, -1, 1, 1);
        left = area.left + 3;
        top = area.top + 3;
        t.bw = 0x96;
        t.bh = 0x28;
        t.cw = 0xAA;
        top_bottom = area.top + 0x17;
        right = (area.right - area.left) - 6 + left;
        t.centre.top = area.top + 0x21;
        t.centre.left = ((area.right - area.left) - 0xB0) / 2 + left;
        t.centre.right = t.centre.left + 0xAA;
        t.centre.bottom = area.top + 0x111;
    }
    // The rows and columns of the buttons.
    int32_t y = lh + 0xF + top_bottom;
    int32_t avail = inner_bottom - y - 0x1E;
    int32_t width = right - left;
    if (!dl) {
        t.cy = (avail - t.bh) / 2 + y;
    } else {
        if (!w800) avail = inner_bottom - y - 0xE6;
        t.cy = (avail - t.bh) / 2 + y;
    }
    t.y4 = t.bh + 0x10 + t.cy;
    t.y0 = t.cy - t.bh - 0x10;
    for (int32_t i = 0; i < 3; i++) {
        (&t.ya)[i] = (i - 1) * (t.bh + 0x10) - (t.bh + 0x10) / 2 + t.cy;
        t.col[i] = ((width - t.bw) * i) / 2 - 5 * i + 5 + left;
    }
    t.col[1] = t.centre.left;
    t.yd = (t.bh + 0x10) * 2 - (t.bh + 0x10) / 2 + t.cy;
    int32_t tech = g_tech;
    int32_t leads[5] = {-1, -1, -1, -1, -1}, n = 0;
    for (int32_t k = 0; k < 0xB2; k++) {
        if (n > 4) break;
        int32_t p = (k & 1) ? Tech[k >> 1].preq_tech2 : Tech[k >> 1].preq_tech1;
        if (p == tech) leads[n++] = k >> 1;
    }
    int32_t p1 = tech >= 0 && tech < 89 ? Tech[tech].preq_tech1 : -1;
    int32_t p2 = tech >= 0 && tech < 89 ? Tech[tech].preq_tech2 : -1;
    std::string tl = "labs tree " + std::to_string(tech) + " preq";
    if (p1 < 0 || p2 < 0) {
        if (p1 != -1) {
            labs_line(c, t.col[1], t.cy, t.col[0], t.cy, t.bw, t.bh, false);
            tree_button(s, c, 0, p1, t.col[0], t.cy, faction);
            tl += " " + std::to_string(p1);
        }
    } else {
        labs_line(c, t.col[1], t.cy, t.col[0], t.yb, t.bw, t.bh, false);
        tree_button(s, c, 0, p1, t.col[0], t.yb, faction);
        labs_line(c, t.col[1], t.cy, t.col[0], t.yc, t.bw, t.bh, false);
        tree_button(s, c, 1, p2, t.col[0], t.yc, faction);
        tl += " " + std::to_string(p1) + " " + std::to_string(p2);
    }
    tl += " leads";
    if (n > 4) n = 4;
    int32_t rows[4] = {};
    if (n == 3) {
        rows[0] = t.y0;
        rows[1] = t.cy;
        rows[2] = t.y4;
    } else if (n == 2) {
        rows[0] = t.yb;
        rows[1] = t.yc;
    } else if (n == 1) {
        rows[0] = t.cy;
    } else if (n == 4) {
        rows[0] = t.ya;
        rows[1] = t.yb;
        rows[2] = t.yc;
        rows[3] = t.yd;
    }
    for (int32_t i = 0; i < n; i++) {
        labs_line(c, t.col[2], rows[i], t.col[1], t.cy, t.bw, t.bh, false);
        tree_button(s, c, 2 + i, leads[i], t.col[2], rows[i], faction);
        tl += " " + std::to_string(leads[i]);
    }
    put(tl);
    // The centre box: measured (pass 0), then drawn (pass 1).
    t.centre_h = 0;
    int32_t cx = t.centre.left, cyy = 0, tw = t.cw - 6;
    int32_t cat = category(tech);
    for (int32_t pass = 0; pass < 2; pass++) {
        Buffer_set_font(c, f16, f16, f16, nullptr);
        Buffer_set_text_color3(c, has_tech(tech, faction) ? 0xFF : 0xE1, -1, 1, 1);
        StrBuffer[0] = 0;
        if (tech >= 0) say_tech_2(StrBuffer, tech, 0);
        std::string s1 = "[" + sw_upper(StrBuffer) + "]";
        if (pass == 0) {
            t.centre_h += Buffer_wrap_height(c, s1.c_str(), tw);
        } else {
            cx = t.centre.left;
            cyy = ((t.centre.bottom + t.centre.top) - t.centre_h) / 2;
            box(c, cx - 3, cyy - 9, cx + t.cw + 3, cyy + t.centre_h + 6, 3);
            box(c, cx, cyy - 6, cx + t.cw, cyy + t.centre_h + 3, 0);
            cyy = Buffer_wrap_cent_4(c, s1.c_str(), cx + 3, cyy, tw);
            put("labs centre " + s1);
        }
        Buffer_set_font(c, f14, f14, f14, nullptr);
        Buffer_set_text_color3(c, mandate_color(cat), -1, 1, 1);
        std::string s2 = "[" + std::string(cat >= 0 ? label_get(0x209 + cat) : "") + " " +
                         std::to_string(tech >= 0 ? tech_recurse(tech, 0) : 0) + "]";
        if (pass == 0) {
            t.centre_h += Buffer_wrap_height(c, s2.c_str(), tw) + 0x3A;
        } else {
            cyy = Buffer_wrap_cent_4(c, s2.c_str(), cx + 3, cyy, tw);
            put("labs centre " + s2);
            Sprite* sp = tech_picture(tech);
            if (sp && sp->width) {
                BlitDestScale = 0x32;
                BlitSourceScale = sp->full_width ? sp->full_width : 1;
                Sprite_draw_mono_2(sp, c, sp->key, (t.cw - 0x34) / 2 + 3 + cx, cyy + 3, mandate_color(cat));
                BlitDestScale = BlitSourceScale = 1;
            }
            cyy += 0x3A;
        }
        if (tech == -1) continue;
        if (!dl) {
            Buffer_set_text_color(c, 0xE1, -1, 1, 1);
            Buffer_set_text_color2(c, 0xE1, -1, 1, 1);
        } else {
            buffer_text(c).hang = 10;  // DatalinkWin + 0x454: the wrapped lines' indent
            Buffer_set_font(c, dl_font(0), nullptr, nullptr, dl_font(1));
            Buffer_set_text_color(c, 0x99, -1, 1, 1);
            Buffer_set_text_color_hyper(c, 0x96, -1, 1, 1);
        }
        auto write = [&](const std::string& text) {
            if (pass == 0) {
                t.centre_h += Buffer_wrap_height(c, text.c_str(), tw);
            } else {
                UiRect r{cx + 3, cyy, cx + 3 + tw, cyy};
                cyy = Buffer_wrap_2(c, text.c_str(), &r);
                put("labs allows " + text);
            }
        };
        auto item = [&](std::string& str, uint32_t label, const char* name, int32_t lcat, int32_t idx) {
            str += str.empty() ? std::string(label_get(label / 4)) + ": " : std::string(", ");
            str += dl ? link(name, lcat, idx) : std::string(name);
        };
        std::string str;
        for (int32_t i = 0; i < 4; i++)
            if (Reactor[i].preq_tech == tech) item(str, 0x234, Reactor[i].name, 5, i + 1);
        if (!str.empty()) write(str);
        str.clear();
        for (int32_t i = 0; i < 9; i++)
            if (Chassis[i].preq_tech == tech) {
                item(str, 0x120, Chassis[i].offsv1_name, 4, i);
                str += " (" + std::to_string((int32_t)(uint8_t)Chassis[i].speed) + ")";
            }
        if (!str.empty()) write(str);
        str.clear();
        for (int32_t i = 0; i < 26; i++)
            if (Weapon[i].preq_tech == tech) {
                item(str, 0x124, Weapon[i].name, 6, i);
                str += " (";
                if (Weapon[i].offense_value < 0) str += label_get(0x310 / 4);
                else if (Weapon[i].mode > 2) str += label_get(0x200 / 4);
                else str += std::to_string(Weapon[i].offense_value);
                str += ")";
            }
        if (!str.empty()) write(str);
        str.clear();
        for (int32_t i = 0; i < 14; i++)
            if (Armor[i].preq_tech == tech) {
                item(str, 0x128, Armor[i].name, 7, i);
                str += " (";
                if (Armor[i].defense_value < 0) str += label_get(0x310 / 4);
                else str += std::to_string(Armor[i].defense_value);
                str += ")";
            }
        if (!str.empty()) write(str);
        str.clear();
        for (int32_t i = 0; i < 29; i++)
            if (Ability[i].preq_tech == tech) item(str, 0x12C, Ability[i].name, 8, i);
        if (!str.empty()) write(str);
        str.clear();
        for (int32_t i = 0; i < 64; i++)
            if ((Units[i].unit_flags & 1) && Units[i].preq_tech == tech) item(str, 0x318, Units[i].name, 3, i);
        if (!str.empty()) write(str);
        str.clear();
        for (int32_t i = 1; i < 134; i++) {
            const CFacility& fa = Facility[i];
            if (fa.preq_tech != tech && fa.free_tech != tech) continue;
            str += str.empty() ? std::string(label_get(0x110 / 4)) + ": " : std::string(", ");
            if (fa.free_tech == tech) str += "(";
            str += dl ? link(fa.name, i < 70 ? 10 : 11, i) : std::string(fa.name);
            if (fa.free_tech == tech) str += std::string(" ") + label_get(0x4DC / 4) + ")";
        }
        if (!str.empty()) write(str);
        // Social models and specialists: written again after each one (the original's loop).
        str.clear();
        for (int32_t k = 0; k < 4; k++)
            for (int32_t m = 0; m < 4; m++)
                if (SocialField[k].soc_preq_tech[m] == tech) {
                    str += str.empty() ? std::string(label_get(0x118 / 4)) + ": " : std::string(", ");
                    str += SocialField[k].soc_name[m];
                    write(str);
                }
        str.clear();
        for (int32_t i = 0; i < 7; i++)  // (0x946020..0x9460E4)
            if (Citizen[i].preq_tech == tech) {
                str += str.empty() ? std::string(label_get(0x4CC / 4)) + ": " : std::string(", ");
                str += Citizen[i].singular_name;
                write(str);
            }
    }
    if (dl) buffer_text(c).hang = 0;
    clip_all(c);
    if (s) {
        int32_t mx = 0, my = 0;
        ui_mouse_pos(&mx, &my);
        labs_mouse(s, c, mx - s->x, my - s->y);
    }
}

bool labs_button(int32_t id) {
    if (id < 0x379 || id > 0x37E) return false;
    int32_t t = g_tech;
    if (t < 0 || t >= 89) return true;
    if (id == 0x379) {
        g_tech = Tech[t].preq_tech1;
    } else if (id == 0x37A) {
        g_tech = Tech[t].preq_tech2;
    } else {
        int32_t n = 0;
        for (int32_t k = 0; k < 0xB2; k++) {
            int32_t p = (k & 1) ? Tech[k >> 1].preq_tech2 : Tech[k >> 1].preq_tech1;
            if (p == t && ++n == id - 0x37A) g_tech = k >> 1;
        }
    }
    return true;
}

void labs_mouse(ScreenWin* s, Buffer* c, int32_t x, int32_t y) {
    Tree& t = g_tree;
    int32_t k = -1;
    for (int32_t i = 0; i < 6; i++)
        if (t.shown[i] && x >= t.btn[i].left && x < t.btn[i].right && y >= t.btn[i].top && y < t.btn[i].bottom) k = i;
    if (k == t.hover) return;
    if (t.hover >= 0) passover(c, t.hover, false);
    t.hover = k;
    if (k >= 0) passover(c, k, true);
    if (s) s->texts.push_back("labs hover " + std::to_string(k >= 0 ? 0x379 + k : -1));
    ui_invalidate();
}
