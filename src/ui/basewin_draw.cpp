// Base window panes (basewin.h): BaseWin_draw_* and pfncDrawCityMap.
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "engine/base.h"
#include "engine/crt.h"
#include "engine/globals.h"
#include "engine/strings.h"
#include "engine/zoc.h"
#include "engine/tech.h"
#include <algorithm>
#include "ui/font_text.h"
#include "ui/sprites.h"
#include "ui/texture.h"
#include "ui/colortables.h"
#include "ui/mapwin.h"
#include "ui/vehdraw.h"
#include "engine/map.h"
#include "engine/veh.h"
#include "engine/diplo_act.h"
#include "engine/design.h"
#include "ui/basewin_internal.h"
#include "ui/voxel.h"
#include "ui/scroll.h"
#include "ui/win.h"
#include "ui/console.h"
#include "platform/ui_platform.h"

// textcolor_init (0x4BA220): the text colour pairs of text.pcx into the memory image.
void textcolor_load() {
    static bool done = false;
    if (done) return;
    done = true;
    Buffer t;
    Buffer_ctor(&t);
    if (Buffer_load_pcx(&t, "text.pcx", nullptr, 10, 0xEC)) return;
    auto pairs = [&](uint32_t addr, uint32_t end, int32_t x1, int32_t x2, int32_t y) {
        for (uint32_t a = addr; a < end; a += 8, y += 0x10) {
            int32_t* p = reinterpret_cast<int32_t*>(mem::host(a));
            p[-1] = Buffer_get_pixel(&t, x1, y);
            p[0] = Buffer_get_pixel(&t, x2, y);
            if (p[0] == 9) p[0] = -1;
        }
    };
    pairs(0x8C6B90, 0x8C6BD8, 8, 0x12, 0x15);
    pairs(0x8C6CA8, 0x8C6D90, 0xCD, 0xD7, 0x24);
    pairs(0x8C6D90, 0x8C6DE8, 0x173, 0x17D, 0x24);
    pairs(0x8C6E30, 0x8C6E68, 0x234, 0x23E, 0x24);
    pairs(0x8C6C70, 0x8C6CA8, 0x232, 0x23C, 0xB0);
    pairs(0x8C6DE8, 0x8C6E08, 0x2CE, 0x2D8, 0x25);
    pairs(0x8C6BD8, 0x8C6BF8, 7, 0x11, 0xBE);
    pairs(0x8C6BF8, 0x8C6C28, 6, 0x10, 0x126);
    pairs(0x8C6E08, 0x8C6E30, 0x175, 0x17F, 0x11E);
    pairs(0x8C6B60, 0x8C6B90, 0x175, 0x17F, 0x19E);
    int32_t y = 0x1A4;
    for (uint32_t a = 0x8C6C28; a < 0x8C6C70; a += 12, y += 0x10) {
        int32_t* p = reinterpret_cast<int32_t*>(mem::host(a));
        p[-1] = Buffer_get_pixel(&t, 8, y);
        p[0] = Buffer_get_pixel(&t, 0x12, y);
        p[1] = Buffer_get_pixel(&t, 0x1C, y);
    }
}

// ==== helpers ====================================================================================

namespace {

Buffer* bwc() { return &g_bw.canvas; }
Buffer* mic() { return &g_bw.mi_canvas; }
Font* bw_font() { return reinterpret_cast<Font*>(&BaseWin + kBwFont); }
int32_t tc(uint32_t addr) { return text_color(addr); }

// A font's line height as the window code reads it (+0x0 >= 0: +0x0 + ascent above leading).
int32_t line_h(const Font* f) { return f->field_0 < 0 ? f->height : f->field_0 + f->ascent_above; }

void inset(UiRect* r, int32_t d) {
    r->left += d;
    r->top += d;
    r->right -= d;
    r->bottom -= d;
}
void write_l(Buffer* b, const char* s, int32_t x, int32_t y) { Buffer_write_l_3(b, s, x, y, (int32_t)std::strlen(s)); }
void cat(const char* s) { crt_strcat(StrBuffer, s); }
void cat_num(int32_t v) {
    char t[80];
    std::snprintf(t, sizeof(t), "%d", v);
    crt_strcat(StrBuffer, t);
}
void upper(char* s) {
    for (; *s; s++) *s = (char)std::toupper((uint8_t)*s);
}

}  // namespace

// find_font (0x5882F0): the config_popups font nearest to `size` pixels (Arial below 13 pixels,
// Arial Narrow bold from 13), style 0..3 (plain, bold, italic, underline).
Font* GAMEAPI find_font(int32_t size, int32_t style) {
    static const int32_t kSizes[12] = {40, 36, 32, 24, 22, 20, 18, 16, 14, 12, 10, 8};
    static const int32_t kStyles[4] = {0, 1, 2, 4};
    static Font fonts[12][4];
    static bool init = false;
    if (!init) {
        init = true;
        for (int32_t i = 0; i < 12; i++)
            for (int32_t j = 0; j < 4; j++) {
                Font_ctor(&fonts[i][j]);
                Font_init(&fonts[i][j], kSizes[i] < 13 ? "Arial" : "Arial Narrow", kSizes[i],
                          (kSizes[i] > 12 ? 1 : 0) | kStyles[j]);
            }
    }
    int32_t best = 0, dist = 9999;
    for (int32_t i = 0; i < 12; i++) {
        int32_t d = std::abs(kSizes[i] - size);
        if (d < dist) {
            best = i;
            dist = d;
        }
    }
    return &fonts[best][style];
}

// ==== BaseWin_draw_info (0x415B20): the base name in the main interface ===========================

void THISCALL BaseWin_draw_info(uint8_t* self, int32_t flag) {
    Buffer* c = mic();
    UiRect old = c->clip;
    UiRect r = base_win().rect_staple_mi;
    inset(&r, -3);
    UiRect all{0, 0, c->width, c->height};
    Buffer_set_clip(c, &all);
    Buffer_box_sprite(c, &r, box_def(3));
    UiRect n = base_win().rect_name_mi;
    Buffer_set_clip(c, &n);
    r = n;
    Buffer_box_sprite(c, &r, box_def(3));
    int32_t d = -6 - g_bw.buttons[2].sprite[0]->full_width;
    r.left -= d;
    r.right += d;
    r.top += 3;
    r.bottom -= 3;
    Buffer_box_sprite(c, &r, box_def(0));
    inset(&r, 3);
    BASE& b = cur_base();
    StrBuffer[0] = 0;
    cat(b.name);
    if ((uint8_t)b.faction_id != CurrentPlayerFaction) {
        int32_t f = (uint8_t)b.faction_id;
        cat(" ");
        cat("(");
        GenderDefault = MFactions[f].noun_gender;
        PluralDefault = MFactions[f].is_noun_plural;
        cat(MFactions[f].noun_faction);
        cat(")");
    }
    upper(StrBuffer);
    int32_t size = 0x20;
    do {
        if (Font_width(find_font(size, 1), StrBuffer) <= r.right - r.left) break;
        size -= 2;
    } while (size > 0);
    Buffer_set_text_color(c, tc(0x68FA30 + (uint8_t)b.faction_id * 4), -1, 1, 1);
    Buffer_set_font(c, find_font(size, 1), nullptr, nullptr, nullptr);
    Buffer_write_cent_l_7(c, StrBuffer, &r, (int32_t)std::strlen(StrBuffer));
    Buffer_set_clip(c, &old);
    (void)flag;
    ui_invalidate();
}

// ==== BaseWin_draw_misc (0x415730): mission year, energy, eco-damage ==============================

void THISCALL BaseWin_draw_misc(uint8_t* self, int32_t flag) {
    Buffer* c = bwc();
    UiRect r = base_win().rect_misc;
    Buffer_set_clip(c, &r);
    Buffer_box_sprite(c, &r, box_def(3));
    inset(&r, 3);
    Buffer_box_sprite(c, &r, box_def(0));
    inset(&r, 3);
    Font* f = find_font(0xC, 0);
    Buffer_set_font(c, f, nullptr, nullptr, nullptr);
    int32_t lh = line_h(f);
    int32_t y = ((r.bottom - lh * 3) - r.top) / 2 + r.top;
    Buffer_set_text_color(c, tc(0x8C6CFC), tc(0x8C6D00), 1, 1);
    StrBuffer[0] = 0;
    cat(label_get(0xB68 / 4));
    int32_t x = Buffer_write_l_3(c, StrBuffer, r.left, y, (int32_t)std::strlen(StrBuffer));
    StrBuffer[0] = 0;
    Buffer_set_text_color(c, tc(0x8C6D04), tc(0x8C6D08), 1, 1);
    cat(" ");
    say_year(StrBuffer);
    Buffer_write_l_3(c, StrBuffer, x, y, (int32_t)std::strlen(StrBuffer));
    y += line_h(f);
    int32_t fid = (uint8_t)cur_base().faction_id;
    StrBuffer[0] = 0;
    cat(label_get(0x90 / 4));
    cat(": ");
    Buffer_set_text_color(c, tc(0x8C6CFC), tc(0x8C6D00), 1, 1);
    x = Buffer_write_l_3(c, StrBuffer, r.left, y, (int32_t)std::strlen(StrBuffer));
    StrBuffer[0] = 0;
    cat_num(Factions[fid].energy_credits - Factions[fid].hurry_cost_total);
    Buffer_set_text_color(c, tc(0x8C6D04), tc(0x8C6D08), 1, 1);
    Buffer_write_l_3(c, StrBuffer, x, y, (int32_t)std::strlen(StrBuffer));
    int32_t lh2 = line_h(f);
    StrBuffer[0] = 0;
    cat(label_get(0xEF8 / 4));
    cat(": ");
    cat(" ");
    cat_num(cur_base().eco_damage);
    if (cur_base().eco_damage == 0) Buffer_set_text_color(c, tc(0x8C6CE4), tc(0x8C6CE8), 1, 1);
    else Buffer_set_text_color(c, tc(0x8C6CEC), tc(0x8C6CF0), 1, 1);
    Buffer_write_l_3(c, StrBuffer, r.left, y + lh2, (int32_t)std::strlen(StrBuffer));
    (void)flag;
    ui_invalidate();
}

// ==== BaseWin_draw_nutrients (0x411240): the nutrient box and the growth line =====================

void THISCALL BaseWin_draw_nutrients(uint8_t* self, int32_t flag) {
    Buffer* c = bwc();
    Font font;
    Font_ctor(&font);
    UiRect old = c->clip;
    UiRect pane = base_win().rect_nut_pane;
    Buffer_set_clip(c, &pane);
    Buffer_box_sprite(c, &pane, box_def(3));
    Font_init(&font, "Arial", ScreenWidth == 800 ? 10 : 0xC, 0);
    UiRect t = base_win().rect_nut_title;
    Buffer_box_sprite(c, &t, box_def(6));
    t.left += 3;
    t.bottom -= 3;
    t.right -= 3;
    t.top += 3;
    Buffer_set_font(c, bw_font(), nullptr, nullptr, nullptr);
    Buffer_set_text_color(c, tc(0x8C6CA4), tc(0x8C6CA8), 1, 1);
    StrBuffer[0] = 0;
    cat(label_get(0x820 / 4));
    upper(StrBuffer);
    Buffer_write_cent_l_7(c, StrBuffer, &t, (int32_t)std::strlen(StrBuffer));
    t = base_win().rect_nutrients;
    Buffer_box_sprite(c, &t, box_def(0));
    t.left += 3;
    t.bottom -= 3;
    t.right -= 3;
    t.top += 3;
    Buffer_set_font(c, &font, nullptr, nullptr, nullptr);
    Buffer_set_text_color(c, tc(0x8C6CFC), tc(0x8C6D00), 1, 1);
    UiRect box{t.left, t.top, t.right, t.bottom - line_h(&font) * 2};
    BASE& b = cur_base();
    int32_t have = b.nutrients_accumulated;
    int32_t per = cost_factor((uint8_t)b.faction_id, 0, CurrentBaseID);
    int32_t need = (b.pop_size + 1) * per;
    int32_t surplus = cur_base().nutrient_surplus;
    int32_t w = box.right - box.left;
    if (per * 2 < w) {
        int32_t h = box.bottom - box.top;
        int32_t rows = (need / per) * 2;
        while (rows - h != 0 && h <= rows) {
            per *= 2;
            rows = (need / per) * 2;
        }
    } else {
        per = 0;
        if (need > 0) do
                per++;
            while (per * per < need);
        per += 2;
    }
    int32_t nrows = need / per;
    if (nrows * per < need) nrows++;
    int32_t cw = w / per;
    int32_t ch = (box.bottom - box.top) / nrows;
    int32_t y0 = ((box.bottom - box.top) - ch * nrows) / 2 + box.top;
    int32_t x0 = (w - cw * per) / 2 + box.left;
    int32_t x1 = cw + x0, y1 = ch + y0;
    if (ch > 7) {
        x0++;
        y0++;
    }
    x1--;
    y1--;
    int32_t n = 0;
    for (int32_t row = 0; row < nrows && n <= need; row++) {
        int32_t cx0 = x0, cx1 = x1;
        for (int32_t col = 0; col < per; col++) {
            UiRect cell{cx0, y0, cx1, y1};
            if (n < have) {
                if (ch < 8) Buffer_fill_3(c, &cell, 0x95);
                else Buffer_tile_2(c, ArtSprite(0x6A7628 + 0xBD4C), cx0, y0, cx0, y0, cx1 - cx0, y1 - y0);
            } else if (ch > 7) {
                Buffer_tile_2(c, ArtSprite(0x6A7628 + 0xBD20), cx0, y0, cx0, y0, cx1 - cx0, y1 - y0);
            } else {
                Buffer_fill_3(c, &cell, 0x98);
            }
            if (surplus < 0) {
                if (surplus + have <= n && n < have) Buffer_box_2(c, &cell, 0x6C, 0x6C);
            } else if (surplus > 0 && n + 1 <= surplus + have && have < n + 1) {
                Buffer_box_2(c, &cell, 0x95, 0x95);
            }
            cx0 += cw;
            cx1 += cw;
            n++;
            if (n > need) break;
        }
        y0 += ch;
        y1 += ch;
    }
    // The growth line: turns to grow (or to starve), "stagnant", or "!" .
    have = cur_base().nutrients_accumulated;
    int32_t rest = cost_factor((uint8_t)cur_base().faction_id, 0, CurrentBaseID) * (cur_base().pop_size + 1) - have;
    int32_t turns;
    int32_t s = cur_base().nutrient_surplus;
    if (rest < 1) turns = 1;
    else if (s >= 1) turns = (s - 1 + rest) / s + 1;
    else if (s < 0) turns = (-s - 1 + have) / -s + 1;
    else turns = flag;
    StrBuffer[0] = 0;
    cat(label_get(s >= 0 ? 0x2F : 0x33));
    if (cur_base().nutrient_surplus < 1 && rest > 0) {
        if (cur_base().nutrient_surplus == 0) {
            cat(": ");
            cat("-");
            cat(label_get(200 / 4));
            cat("-");
        } else {
            cat("!");
        }
    } else {
        if (turns < 0) turns = 0;
        else if (turns > 9999) turns = 9999;
        cat(": ");
        cat_num(turns);
        cat(" ");
        cat(label_get(turns == 1 ? 0xC0 / 4 : 0xC4 / 4));
    }
    Buffer_write_cent_l_5(c, StrBuffer, t.left, t.bottom - line_h(&font) - 2, box.right - box.left,
                          (int32_t)std::strlen(StrBuffer));
    Buffer_set_clip(c, &old);
    ui_invalidate();
}

// ==== BaseWin_draw_commerce (0x410870): commerce with the other factions ==========================

void THISCALL BaseWin_draw_commerce(uint8_t* self, int32_t flag) {
    Buffer* c = bwc();
    Font font;
    Font_ctor(&font);
    int32_t this_base = -1;
    for (int32_t i = 0; i < BaseCount; i++)
        if (kBasesAddr + (uint32_t)i * sizeof(BASE) == CurrentBasePtr) this_base = i;
    UiRect old = c->clip;
    UiRect pane = base_win().rect_commerce_pane;
    Buffer_set_clip(c, &pane);
    Buffer_box_sprite(c, &pane, box_def(3));
    Font_init(&font, "Arial", 10, 1);
    UiRect t = base_win().rect_commerce;
    Buffer_box_sprite(c, &t, box_def(6));
    inset(&t, 3);
    Buffer_set_font(c, bw_font(), nullptr, nullptr, nullptr);
    Buffer_set_text_color(c, tc(0x8C6CA4), tc(0x8C6CA8), 1, 1);
    StrBuffer[0] = 0;
    if (MFactions[(uint8_t)Bases[base_win().base].faction_id].rule_flags & 0x80) {
        cat(label_get(0x114C / 4));
    } else {
        cat(label_get(0x4E4 / 4));
        upper(StrBuffer);
    }
    Buffer_write_cent_l_7(c, StrBuffer, &t, (int32_t)std::strlen(StrBuffer));
    Buffer_set_font(c, &font, nullptr, nullptr, nullptr);
    Buffer_set_text_color(c, tc(0x8C6CFC), tc(0x8C6D00), 1, 1);
    t = base_win().rect_commerce2;
    Buffer_box_sprite(c, &t, box_def(0));
    inset(&t, 3);
    int32_t f = (uint8_t)cur_base().faction_id;
    int32_t step = (t.bottom - t.top) / 6;
    bool any = false;
    const Faction& me = Factions[f];
    if (!(MFactions[f].rule_flags & 0x80)) {
        int32_t y = t.top;
        for (int32_t i = 1; i < 8; i++) {
            const MFaction& m = MFactions[i];
            if (i == f || !(me.diplo_status[i] & 3) || (!BaseCommerceImport[i] && !BaseCommerceExport[i]) ||
                (m.rule_flags & 0x80))
                continue;
            PluralDefault = m.is_noun_plural;
            GenderDefault = m.noun_gender;
            any = true;
            StrBuffer[0] = 0;
            cat(m.noun_faction);
            Buffer_set_text_color(c, FactionColorText[i], -1, 1, 1);
            upper(StrBuffer);
            bool sanctions = me.sanction_turns || Factions[i].sanction_turns;
            if (!sanctions) {
                cat(": ");
                cat("+");
                cat_num(BaseCommerceImport[i]);
            }
            Buffer_write_l_3(c, StrBuffer, t.left, y, (int32_t)std::strlen(StrBuffer));
            StrBuffer[0] = 0;
            int32_t y2 = font.ascent_above + 1 + y;
            int32_t x = t.left + 5;
            if (!sanctions) {
                cat(label_get((me.diplo_status[i] & 1) ? 0xA14 / 4 : 0xA10 / 4));
                x = Buffer_write_l_3(c, StrBuffer, x, y2, (int32_t)std::strlen(StrBuffer));
                if (BaseCommerceExport[i] != BaseCommerceImport[i]) {
                    Buffer_set_text_color(c, FactionColorText[i], -1, 1, 1);
                    StrBuffer[0] = 0;
                    cat(" ");
                    cat("(");
                    cat(label_get(0xA20 / 4));
                    cat(" ");
                    cat("+");
                    cat_num(BaseCommerceExport[i]);
                    cat(")");
                    Buffer_write_l_3(c, StrBuffer, x, y2, (int32_t)std::strlen(StrBuffer));
                }
            } else {
                Buffer_set_text_color(c, tc(0x8C6D14), tc(0x8C6D18), 1, 1);
                cat(label_get(0x708 / 4));
                cat(" ");
                cat("(");
                cat_num(std::max(Factions[i].sanction_turns, me.sanction_turns));
                cat(")");
                upper(StrBuffer);
                Buffer_write_l_3(c, StrBuffer, x, y2, (int32_t)std::strlen(StrBuffer));
            }
            y += step;
        }
    } else {
        // Aliens: energy from the base's facilities and projects.
        Buffer_set_font(c, bw_font(), nullptr, nullptr, nullptr);
        int32_t n = 0;
        for (int32_t i = 0; i < 0x46; i++)
            if (cur_base().facilities_built[i >> 3] & (1 << (i & 7))) n++;
        for (int32_t i = 0; i < 64; i++)
            if (SecretProjects[i] == this_base) n += 5;
        n /= 2;
        t = base_win().rect_commerce;
        inset(&t, 3);
        if (me.sanction_turns) {
            StrBuffer[0] = 0;
            cat(label_get(0x1194 / 4));
            Buffer_set_text_color(c, tc(0x8C6D14), tc(0x8C6D18), 1, 1);
            Buffer_write_cent_l_7(c, StrBuffer, &t, (int32_t)std::strlen(StrBuffer));
            any = true;
        } else if (n) {
            StrBuffer[0] = 0;
            Buffer_set_text_color(c, 0xFF, -1, 1, 1);
            cat("+");
            cat_num(n);
            cat(" ");
            cat(label_get(0x90 / 4));
            upper(StrBuffer);
            Buffer_write_cent_l_7(c, StrBuffer, &t, (int32_t)std::strlen(StrBuffer));
            any = true;
        }
        Buffer_set_font(c, &font, nullptr, nullptr, nullptr);
    }
    if (!any) {
        Buffer_set_font(c, bw_font(), nullptr, nullptr, nullptr);
        Buffer_set_text_color(c, tc(0x8C6CFC), tc(0x8C6D00), 1, 1);
        StrBuffer[0] = 0;
        cat(label_get(100 / 4));
        UiRect r = base_win().rect_commerce2;
        Buffer_write_cent_l_7(c, StrBuffer, &r, (int32_t)std::strlen(StrBuffer));
    }
    Buffer_set_clip(c, &old);
    (void)flag;
    ui_invalidate();
}

// ==== BaseWin_draw_facilities (0x40FCC0): the facilities list ======================================

ScrollBar g_fac_bar, g_sup_bar;
bool g_garrison_arrows = false;
bool g_fac_bar_shown = false, g_sup_bar_shown = false;
int32_t g_fac_bar_page = 1;

void THISCALL BaseWin_draw_facilities(uint8_t* self, int32_t flag) {
    Buffer* c = bwc();
    Font font;
    Font_ctor(&font);
    UiRect old = c->clip;
    UiRect pane = base_win().rect_fac_pane;
    Buffer_set_clip(c, &pane);
    spot_kill_type(&g_bw.spots_win, 2);
    Font_init(&font, "Arial", ScreenWidth != 800 ? 12 : 10, 0);
    Buffer_box_sprite(c, &pane, box_def(3));
    StrBuffer[0] = 0;
    UiRect t = base_win().rect_fac_title;
    Buffer_box_sprite(c, &t, box_def(6));
    inset(&t, 3);
    Buffer_set_font(c, bw_font(), nullptr, nullptr, nullptr);
    cat(label_get(0x110 / 4));
    Buffer_set_text_color(c, tc(0x8C6CA4), tc(0x8C6CA8), 1, 1);
    upper(StrBuffer);
    Buffer_write_cent_l_7(c, StrBuffer, &t, (int32_t)std::strlen(StrBuffer));
    Buffer_set_font(c, &font, nullptr, nullptr, nullptr);
    Buffer_set_text_color(c, tc(0x8C6CFC), tc(0x8C6D00), 1, 1);
    t = base_win().rect_fac_list;
    if (g_bw.buttons[0x15].shown) t.bottom = base_win().rect_editor.top;
    Buffer_box_sprite(c, &t, box_def(0));
    inset(&t, 3);
    int32_t lh = line_h(&font);
    int32_t rows = (t.bottom - t.top) / lh;
    if (lh * rows + font.ascent_above <= t.bottom - t.top) rows++;
    int32_t faction = (uint8_t)cur_base().faction_id;
    auto listed = [&](int32_t i) {
        if (i >= 0x46) return SecretProjects[i - 0x46] == CurrentBaseID;
        if (i < 0x41 && base_fac(CurrentBaseID, i)) return true;
        return redundant(i, faction) != 0;
    };
    int32_t count = 0;
    for (int32_t i = 0; i < 0x86; i++)
        if (listed(i)) count++;
    // count >= rows: a scroll bar (scroll_small) at the right edge, range 0 .. count - 1, at the
    // first shown facility (BaseWin_draw_facilities: Scroll_init_2).
    g_fac_bar_shown = count >= rows;
    if (g_fac_bar_shown) {
        g_fac_bar.r = {t.right - 0xC, t.top, t.right, t.bottom};
        g_fac_bar.small = true;
        g_fac_bar.min = 0;
        g_fac_bar.max = count - 1;
        g_fac_bar.pos = std::max(0, std::min(base_win().fac_scroll, count - 1));
        g_fac_bar_page = rows - 1;
    }
    int32_t y = t.top, index = -1;
    for (int32_t k = 0; k < 0x86; k++) {
        int32_t fac = k == 0 ? 0x1A : (k == 0x1A ? 0 : k);
        if (!listed(fac)) continue;
        index++;
        if (index < base_win().fac_scroll || index >= base_win().fac_scroll + rows) continue;
        StrBuffer[0] = 0;
        int32_t col = tc(0x8C6D44), sh = tc(0x8C6D48);
        if (fac < 0x46 && redundant(fac, faction)) {
            col = tc(0x8C6D4C);
            sh = tc(0x8C6D50);
            if (fac < 0x41 && base_fac(CurrentBaseID, fac)) cat("*");
        }
        Buffer_set_text_color(c, col, sh, 1, 1);
        cat(Strings_get(&StringTable, Facility[fac].name));
        if (fac == 8 && (cur_base().state_flags & 0x400)) {
            cat(" ");
            cat("(");
            cat(label_get(0x3A0 / 4));
            cat(")");
        }
        int32_t w = t.right - t.left;
        while (Font_width(&font, StrBuffer) > w && std::strlen(StrBuffer)) {
            char* sp = std::strrchr(StrBuffer, ' ');
            if (sp) *sp = 0;
            else StrBuffer[std::strlen(StrBuffer) - 1] = 0;
        }
        Buffer_write_l_3(c, StrBuffer, t.left + 1, y, (int32_t)std::strlen(StrBuffer));
        spot_add(&g_bw.spots_win, fac, 2, t.left, y, t.right - t.left, lh);
        y += lh;
    }
    if (g_fac_bar_shown) scroll_draw(c, g_fac_bar);
    Buffer_set_clip(c, &old);
    (void)flag;
    ui_invalidate();
}

// ==== BaseWin_draw_expenses (0x412AA0): the nutrient / mineral / energy rows ======================

namespace {

// A text segment of a row, left aligned and vertically centred in r from r->left; returns the end.
int32_t seg(Buffer* c, UiRect* r, const char* text) {
    if (!*text) return 0;
    return Buffer_write_l_4(c, text, r, (int32_t)std::strlen(text));
}
// Zero padding of a three-digit column (the number itself is written by the caller).
void pad3(Buffer* c, UiRect* r, int32_t v) {
    int32_t a = v < 0 ? -v : v;
    if (a < 100) r->left = seg(c, r, "0");
    if (a < 10) r->left = seg(c, r, "0");
}
const char* num(int32_t v) {
    static char t[80];
    std::snprintf(t, sizeof(t), "%d", v);
    return t;
}

struct ExpRow {
    uint32_t label;  // TextLabels offset
    uint32_t color;  // label / number colour pair
    int32_t intake, cost;  // first and second columns
    int32_t used;  // cells of the first kind (consumption, or energy inefficiency)
    uint32_t used_sprite, surplus_sprite;  // BaseWin sprite offsets
    int32_t surplus;
    uint32_t surplus_color;
};

}  // namespace

void THISCALL BaseWin_draw_expenses(uint8_t* self, int32_t flag) {
    bool w1024 = ScreenWidth != 800;
    if (w1024) BaseWin_draw_energy_alloc(self, flag);
    else if (base_win().energy_layout && flag < 2) {
        BaseWin_draw_energy_alloc(self, flag);
        return;
    }
    Buffer* c = bwc();
    Font font;
    Font_ctor(&font);
    Font_init(&font, "Arial", 0xC, 1);
    UiRect old = c->clip;
    UiRect pane = base_win().rect_expenses;
    Buffer_set_clip(c, &pane);
    UiRect r{0x17, 0, 0x2BF, 0x194};
    Buffer_box_sprite(c, &r, box_def(9));
    Buffer_box_sprite(c, &pane, box_def(3));
    r = {0x12A, 0x107, 0x2B9, 0x145};
    Buffer_box_sprite(c, &r, box_def(0));
    BASE& b = cur_base();
    int32_t n_cost = b.nutrient_consumption + b.nutrient_inefficiency;
    int32_t m_cost = b.mineral_inefficiency + b.mineral_consumption;
    int32_t e_cost = b.energy_inefficiency + b.energy_consumption;
    int32_t ns = b.nutrient_surplus, ms = b.mineral_surplus, es = b.energy_surplus;
    int32_t a = std::abs(ms) + m_cost, n = std::abs(ns) + n_cost;
    int32_t widest = a > n ? a : n;
    int32_t e = std::abs(es) + e_cost;
    if (e > widest) widest = e;
    // FP: cell width 194 / widest, between 1 and 5, truncated.
    float v = 194.0f / (float)widest;
    if (!(5.0f < v) && !(1.0f < v || 1.0f == v)) v = 1.0f;
    else if (5.0f < v) v = 5.0f;
    int32_t cw = (int32_t)v;
    int32_t cells = 0xC2 / cw + 1;
    int32_t bar_w = cw * cells;
    Buffer_set_font(c, &font, nullptr, nullptr, nullptr);
    int32_t asc = font.ascent_above;
    ExpRow rows[3] = {
        {0x820, 0x8C6CC4, b.nutrient_intake_2, n_cost, b.nutrient_consumption, 0xBD20, 0xBD4C, ns, 0x8C6CCC},
        {0xA0, 0x8C6CD4, b.mineral_intake_2, m_cost, b.mineral_consumption, 0xBCC8, 0xBCF4, ms, 0x8C6CDC},
        {0x90, 0x8C6CB4, b.energy_intake_2, e_cost, b.energy_inefficiency, 0xBDA4, 0xBC9C, es, 0x8C6CBC}};
    static const uint32_t kRowRects[3] = {offsetof(BaseWindow, rect_exp[1]), offsetof(BaseWindow, rect_exp[2]),
                                          offsetof(BaseWindow, rect_exp[0])};
    int32_t top = 0x107, eq_x = 0, minus_x = 0;
    bool deficit = false;
    for (int32_t k = 0; k < 3; k++, top += 0x15) {
        const ExpRow& row = rows[k];
        bw_rect(kRowRects[k]) = {0xA9, top, 0x2B8, top + 0x14};
        UiRect lr{k == 0 && !w1024 ? 0xBF : (w1024 ? base_win().rect_expenses.left + 3 : 0xA9), top, 0x128, top + 0x14};
        Buffer_box_sprite(c, &lr, box_def(6));
        inset(&lr, 3);
        Buffer_set_text_color(c, tc(row.color), tc(row.color + 4), 1, 1);
        StrBuffer[0] = 0;
        cat(label_get((int32_t)(row.label / 4)));
        upper(StrBuffer);
        Buffer_write_cent_l_7(c, StrBuffer, &lr, (int32_t)std::strlen(StrBuffer));
        UiRect t{0x133, top + 3, 0x2B8 - 3, top + 0x14 - 3};
        int32_t in = row.intake;
        if (in >= 0) Buffer_set_text_color(c, tc(row.color), tc(row.color + 4), 1, 1);
        else Buffer_set_text_color(c, tc(0x8C6D14), tc(0x8C6D18), 1, 1);
        if (in < 0) t.left = seg(c, &t, "-");
        pad3(c, &t, in);
        t.left = seg(c, &t, num(std::abs(in)));
        if (k == 1) minus_x = t.left;
        t.left = seg(c, &t, " - ");
        int32_t cost = row.cost;
        if (cost < 100) t.left = seg(c, &t, "0");
        if (cost < 10) t.left = seg(c, &t, "0");
        if (k == 1) Buffer_set_text_color(c, tc(row.color), tc(row.color + 4), 1, 1);
        StrBuffer[0] = 0;
        cat(num(std::abs(cost)));
        cat(" ");
        cat(" ");
        t.left = seg(c, &t, StrBuffer);
        // The bar: cells of cw pixels.
        int32_t inner = cw - 1 < 1 ? 1 : cw - 1;
        int32_t y0 = (t.bottom - t.top - asc) / 2 + t.top, y1 = y0 + asc;
        int32_t x = t.left, drawn = 0;
        auto sprite_cells = [&](uint32_t spr, int32_t count) {
            for (int32_t i = 0; i < count; i++, x += cw)
                Buffer_tile_2(c, ArtSprite(0x6A7628 + spr), x, y0, x, y0, inner, y1 - y0);
        };
        auto empty_cells = [&](int32_t count) {
            for (int32_t i = 0; i < count; i++, x += cw) {
                UiRect cell{x, y0, x + inner, y1};
                Buffer_fill_3(c, &cell, 0xF0);
            }
        };
        int32_t s = row.surplus;
        if (s > 0) {
            if (row.used > 0) {
                sprite_cells(row.used_sprite, row.used);
                drawn = row.used;
            }
            int32_t rest = cells - s - row.used;
            if (rest > 0) {
                drawn += rest;
                empty_cells(rest);
            }
            drawn += s;
            sprite_cells(row.surplus_sprite, s);
        } else {
            if (s < 0) deficit = true;
            if (row.used > 0) {
                sprite_cells(row.used_sprite, row.used);
                drawn = row.used;
            }
            for (int32_t i = 0; i < -s; i++) {
                sprite_cells(0xBDA4, 1);
                drawn++;
            }
        }
        if (drawn < cells) empty_cells(cells - drawn);
        t.left += bar_w;
        if (k == 2) eq_x = t.left;
        StrBuffer[0] = 0;
        cat("  =  ");
        if (s < 0) Buffer_set_text_color(c, tc(0x8C6D14), tc(0x8C6D18), 1, 1);
        else if (k == 1 && b.queue_items[0] == -0x45) Buffer_set_text_color(c, tc(0x8C6D5C), tc(0x8C6D60), 1, 1);
        else Buffer_set_text_color(c, tc(row.surplus_color), tc(row.surplus_color + 4), 1, 1);
        cat(s >= 0 ? "+" : "-");
        t.left = seg(c, &t, StrBuffer);
        pad3(c, &t, s);
        StrBuffer[0] = 0;
        cat(num(std::abs(s)));
        if (k == 1 && s >= 0 && b.queue_items[0] == -0x45) cat("*");
        t.left = seg(c, &t, StrBuffer);
    }
    // The legend: INTAKE - (CONSUMPTION + INEFFICIENCY) = SURPLUS / DEFICIT.
    Buffer_set_text_color(c, tc(0x8C6CAC), tc(0x8C6CB0), 1, 1);
    UiRect lr{0xA9, top, 0x2B8, top + 0x14};
    Buffer_box_sprite(c, &lr, box_def(6));
    lr.left = eq_x;
    StrBuffer[0] = 0;
    cat("  =  ");
    if (deficit) {
        Buffer_set_text_color(c, tc(0x8C6D14), tc(0x8C6D18), 1, 1);
        cat(label_get(0xAE4 / 4));
    } else {
        cat(label_get(0xD0 / 4));
    }
    upper(StrBuffer);
    seg(c, &lr, StrBuffer);
    Buffer_set_text_color(c, tc(0x8C6CAC), tc(0x8C6CB0), 1, 1);
    lr.left = lr.right = minus_x;
    StrBuffer[0] = 0;
    cat(label_get(0xA3C / 4));
    upper(StrBuffer);
    Buffer_write_right_l_6(c, StrBuffer, &lr, (int32_t)std::strlen(StrBuffer));
    lr.left = minus_x;
    lr.left = seg(c, &lr, " - ") + 5;
    int32_t ineff = b.energy_inefficiency;
    StrBuffer[0] = 0;
    if (ineff) cat("(");
    cat(label_get(0xA40 / 4));
    if (ineff) cat(" + ");
    upper(StrBuffer);
    int32_t x = seg(c, &lr, StrBuffer);
    if (ineff) {
        Buffer_set_text_color(c, tc(0x8C6D14), tc(0x8C6D18), 1, 1);
        StrBuffer[0] = 0;
        cat(label_get(0xE0 / 4));
        upper(StrBuffer);
        lr.left = x;
        x = seg(c, &lr, StrBuffer);
        Buffer_set_text_color(c, tc(0x8C6CAC), tc(0x8C6CB0), 1, 1);
        lr.left = x;
        seg(c, &lr, ")");
    }
    Buffer_set_clip(c, &old);
    ui_invalidate();
}

// ==== BaseWin_draw_energy_alloc (0x411980): economy / labs / psych ================================

void THISCALL BaseWin_draw_energy_alloc(uint8_t* self, int32_t flag) {
    Buffer* c = bwc();
    Font font;
    Font_ctor(&font);
    bool w1024 = ScreenWidth != 800;
    int32_t f = (uint8_t)cur_base().faction_id;
    UiRect old = c->clip;
    UiRect pane = base_win().rect_alloc;
    Buffer_set_clip(c, &pane);
    Font_init(&font, "Arial", 0xC, 1);
    Buffer_set_font(c, &font, nullptr, nullptr, nullptr);
    const UiRect& cl = base_win().client;
    UiRect r{cl.left + 0x17, cl.top, cl.right - 0x37, cl.bottom};
    Buffer_box_sprite(c, &r, box_def(9));
    Buffer_box_sprite(c, &pane, box_def(3));
    int32_t dy = w1024 ? pane.bottom - pane.top + 3 : 0;
    r = {0x15A, 0x11C + dy, 0x2B8, 0x15A + dy};
    Buffer_box_sprite(c, &r, box_def(0));
    UiRect row{0xA9, 0x107 + dy, 0x2B8, 0x11B + dy};
    Buffer_set_font(c, &font, nullptr, nullptr, nullptr);
    const Faction& fa = Factions[f];
    int32_t psych = fa.SE_alloc_psych, labs = fa.SE_alloc_labs, econ = 10 - psych - labs;
    int32_t e = cur_base().energy_surplus;
    e = e < 0 ? 0 : (e > 9999 ? 9999 : e);
    int32_t psych_e = (psych * e + 4) / 10;
    if (psych_e < 0 || e < 0) psych_e = 0;
    else if (psych_e > e) psych_e = e;
    int32_t rem = e - psych_e;
    int32_t econ_e = (econ * e + 4) / 10;
    if (econ_e < 0 || rem < 0) econ_e = 0;
    else if (econ_e > rem) econ_e = rem;
    int32_t labs_e = e - econ_e - psych_e;
    BASE& b = cur_base();
    UiRect t{w1024 ? base_win().rect_alloc.left + 3 : 0xBF, row.top, row.right, row.bottom};
    Buffer_box_sprite(c, &t, box_def(6));
    inset(&t, 3);
    Buffer_set_text_color(c, tc(0x8C6CB4), tc(0x8C6CB8), 1, 1);
    StrBuffer[0] = 0;
    cat(label_get(0x90 / 4));
    cat(" ");
    cat(label_get(0xA38 / 4));
    upper(StrBuffer);
    if (w1024) Buffer_write_cent_l_7(c, StrBuffer, &t, (int32_t)std::strlen(StrBuffer));
    else Buffer_write_l_4(c, StrBuffer, &t, (int32_t)std::strlen(StrBuffer));
    struct Row {
        uint32_t color, label, rect;
        int32_t pct, energy, bonus, total;
        bool stockpile;
    } rows[3] = {
        {0x8C6D5C, 0x5B8, offsetof(BaseWindow, rect_exp[5]), econ, econ_e, b.economy_total - econ_e, b.economy_total,
         b.queue_items[0] == -0x45 && b.mineral_surplus > 0},
        {0x8C6D64, 0xF0, offsetof(BaseWindow, rect_exp[4]), labs, labs_e, b.labs_total - labs_e, b.labs_total, false},
        {0x8C6D6C, 0x4F8, offsetof(BaseWindow, rect_exp[3]), psych, psych_e, b.psych_total - psych_e, b.psych_total,
         false}};
    for (const Row& rw : rows) {
        row.top += 0x15;
        row.bottom += 0x15;
        bw_rect(rw.rect) = row;
        Buffer_set_text_color(c, tc(rw.color), tc(rw.color + 4), 1, 1);
        t = {row.left, row.top, 0x129, row.bottom};
        Buffer_box_sprite(c, &t, box_def(6));
        inset(&t, 3);
        StrBuffer[0] = 0;
        cat(label_get((int32_t)(rw.label / 4)));
        upper(StrBuffer);
        Buffer_write_cent_l_7(c, StrBuffer, &t, (int32_t)std::strlen(StrBuffer));
        t = {0x12B, row.top, 0x158, row.bottom};
        Buffer_box_sprite(c, &t, box_def(6));
        t.right = 0x150;
        StrBuffer[0] = 0;
        cat_num(rw.pct * 10);
        cat("%");
        Buffer_write_right_l_6(c, StrBuffer, &t, (int32_t)std::strlen(StrBuffer));
        t.right = 0x178;
        StrBuffer[0] = 0;
        cat_num(rw.energy);
        Buffer_write_right_l_6(c, StrBuffer, &t, (int32_t)std::strlen(StrBuffer));
        UiRect w{0x178, row.top, row.right, row.bottom};
        StrBuffer[0] = 0;
        cat(" ");
        cat(label_get(0x90 / 4));
        upper(StrBuffer);
        cat("  +");
        Buffer_set_text_color(c, tc(0x8C6CAC), tc(0x8C6CB0), 1, 1);
        w.left = Buffer_write_l_4(c, StrBuffer, &w, (int32_t)std::strlen(StrBuffer));
        t.right = w.left + 0x1A;
        Buffer_set_text_color(c, tc(rw.color), tc(rw.color + 4), 1, 1);
        StrBuffer[0] = 0;
        cat_num(rw.bonus);
        if (rw.stockpile) cat("*");
        Buffer_write_right_l_6(c, StrBuffer, &t, (int32_t)std::strlen(StrBuffer));
        Buffer_set_text_color(c, tc(0x8C6CAC), tc(0x8C6CB0), 1, 1);
        w.left = t.right;
        StrBuffer[0] = 0;
        cat(" ");
        cat(label_get(0x17C / 4));
        upper(StrBuffer);
        cat(" ");
        cat("=");
        w.left = Buffer_write_l_4(c, StrBuffer, &w, (int32_t)std::strlen(StrBuffer));
        t.right = w.left + 0x1D;
        Buffer_set_text_color(c, tc(rw.color), tc(rw.color + 4), 1, 1);
        StrBuffer[0] = 0;
        cat_num(rw.total);
        Buffer_write_right_l_6(c, StrBuffer, &t, (int32_t)std::strlen(StrBuffer));
    }
    Buffer_set_clip(c, &old);
    (void)flag;
    ui_invalidate();
}

// ==== BaseWin_draw_production (0x40E360): the item being built ====================================

namespace {

// Facility and project pictures (load_misc 0x454D10: facs\facNNN.pcx at 0x787200 [70],
// projs\projNNN.pcx at 0x759320 [64]; loaded when first drawn, as Sprite_init's sprites are).
Sprite* facility_picture(int32_t fac) {
    uint32_t addr = fac < 0x46 ? 0x787200 + (uint32_t)fac * 0x2C : 0x759320 + (uint32_t)(fac - 0x46) * 0x2C;
    Sprite* sp = ArtSprite(addr);
    if (sp->full_width) return sp;
    char name[64];
    if (fac < 0x46) {
        int32_t n = fac > 64 ? fac - 0x1F : fac;
        std::snprintf(name, sizeof(name), "facs/%sfac0%s%d", fac >= 34 && fac <= 64 ? "x" : "", fac < 10 ? "0" : "", n);
    } else {
        int32_t n = fac - 0x46;
        std::snprintf(name, sizeof(name), "projs/proj0%s%d", n < 10 ? "0" : "", n);
    }
    Buffer art;
    Buffer_ctor(&art);
    if (!Buffer_load_pcx(&art, name, nullptr, 10, 0xEC)) {
        // The whole picture (a sprite loaded from its file is not trimmed).
        sp->pixels.resize((size_t)art.width * art.height);
        for (int32_t y = 0; y < art.height; y++)
            std::memcpy(&sp->pixels[(size_t)y * art.width], art.data + (size_t)y * art.stride, (size_t)art.width);
        sp->key = 9;
        sp->stride = art.width;
        sp->width = art.width;
        sp->height = art.height;
        sp->x_offset = sp->y_offset = 0;
    }
    sp->full_width = 0xD7;
    sp->full_height = 0xD7;
    return sp;
}

// ProdWin_facility_mandate (0x493B70): the mandate whose colour a facility is drawn in.
int32_t facility_mandate(int32_t fac) {
    if (fac > 0x45) return 1;
    switch (fac) {
    case 3:
    case 0x1F:
    case 0x20: return 0;
    case 4:
    case 0x1B:
    case 0x1C:
    case 0x1D:
    case 0x1E:
    case 0x44: return 3;
    case 8:
    case 0xF:
    case 0x10:
    case 0x11: return 1;
    default: return 2;
    }
}

}  // namespace

Sprite* basewin_facility_picture(int32_t fac) { return facility_picture(fac); }

void THISCALL BaseWin_draw_production(uint8_t* self, int32_t flag) {
    Buffer* c = mic();
    Font fa, fb;
    Font_ctor(&fa);
    Font_ctor(&fb);
    UiRect old = c->clip;
    UiRect pane = base_win().rect_queue_mi;
    UiRect all{0, 0, c->width, c->height};
    Buffer_set_clip(c, &all);
    BoxDef box = *box_def(3);
    box.edge[3] = nullptr;
    box.corner[3] = nullptr;
    UiRect r = pane;
    Buffer_box_sprite(c, &r, &box);
    const UiRect& q = base_win().rect_queue_box_mi;
    r = {q.left - 5, q.top + 5, q.right - 5, q.bottom + 5};
    box.corner[0] = nullptr;
    box.edge[0] = nullptr;
    box.corner[1] = nullptr;
    Buffer_box_sprite(c, &r, &box);
    {
        const UiRect &a = base_win().rect_change_mi, &b = base_win().rect_hurry_mi;
        r = {std::min(a.left, b.left) - 3, std::min(a.top, b.top) - 3, std::max(a.right, b.right) + 3,
             std::max(a.bottom, b.bottom) + 3};
        static Sprite blank;
        if (!blank.width) {
            Buffer bl;
            Buffer_ctor(&bl);
            Buffer_init(&bl, 6, 6, 8, 0);
            UiRect br{0, 0, 6, 6};
            Buffer_fill_3(&bl, &br, 9);
            blank.pixels.assign(36, 9);
            blank.key = 9;
            blank.stride = 6;
            blank.width = blank.height = blank.full_width = blank.full_height = 6;
        }
        box = *box_def(3);
        box.corner[0] = &blank;
        box.edge[2] = nullptr;
        box.corner[2] = nullptr;
        Buffer_box_sprite(c, &r, &box);
    }
    Buffer_set_clip(c, &pane);
    r = pane;
    inset(&r, 3);
    Buffer_box_sprite(c, &r, box_def(0));
    inset(&r, 3);
    spot_kill_type(&g_bw.spots_iface, 5);
    spot_add(&g_bw.spots_iface, 0, 5, pane.left, pane.top, pane.right - pane.left, pane.bottom - pane.top);
    Font_init(&fa, "Arial", ScreenWidth == 800 ? 10 : 0xC, 0);
    Font_init(&fb, "Arial", 0xC, 1);
    BASE& b = cur_base();
    int32_t item = b.queue_items[0];
    int32_t fac = item < 0 ? -item : -1;
    int32_t proto = 0;
    if (item >= 0) veh_cost(item, CurrentBaseID, &proto);
    int32_t cy = r.top + 0x28, cx = (r.left + r.right) / 2;
    int32_t minerals, per, total;
    if (item < 0) {
        int32_t color = mandate_color(fac < 0x46 ? facility_mandate(fac) : 1);
        Sprite* sp = facility_picture(fac);
        int32_t ss = fac < 0x46 ? facility_picture(0)->full_width : facility_picture(0x46)->full_width;
        int32_t od = BlitDestScale, os = BlitSourceScale;
        BlitDestScale = 0x28;
        BlitSourceScale = ss;
        Sprite_draw_mono_2(sp, c, sp->key & 0xFF, cx - 0x14, cy - 0x14, color);
        BlitDestScale = od;
        BlitSourceScale = os;
        r.top += 0x41;
        StrBuffer[0] = 0;
        cat(Strings_get(&StringTable, Facility[fac].name));
        upper(StrBuffer);
        Buffer_set_text_color(c, tc(0x8C6D2C), tc(0x8C6D30), 1, 1);
        Font* keep = c->fonts[0];
        Buffer_set_font(c, &fb, nullptr, nullptr, nullptr);
        int32_t y = Buffer_wrap_cent_4(c, StrBuffer, r.left, r.top, r.right - r.left);
        if (keep) c->fonts[0] = keep;
        r.right -= 2;
        r.top = y + 2;
        r.left += 2;
        r.bottom = r.bottom - 2 - line_h(&fa) * 2;
        minerals = cur_base().minerals_accumulated;
        per = cost_factor((uint8_t)cur_base().faction_id, 1, -1);
        total = Facility[fac].cost;
    } else {
        int32_t wide = (r.right - r.left) * 3 / 4;
        int32_t hh = wide * 0x4B / 100;
        int32_t fake = veh_fake(item, (uint8_t)b.faction_id);
        if (item < 0x40 && unit_psi_weapon(item))
            Vehs[fake].morale = (uint8_t)(breed_mod(CurrentBaseID, (uint8_t)cur_base().faction_id) + 1);
        ZoomDenom = 100;
        ZoomNum = wide;
        if (veh_draw(c, fake, cx - wide / 2, cy - hh / 2, 1, 0xCC, 0)) {
            // The 3D model (BaseWin + 0x1D170, its camera of BaseWin_init: angles (0, 0, 0.785),
            // light and shadow straight down): vehdraw_construct_vehicle, the scale wide * 0.0007,
            // turned (BaseWin + 0x1E670) + 30 degrees, rendered at the pane's centre.
            static VoxelScene scene;
            static bool ready = false;
            if (!ready) {
                Scene_ctor(&scene);
                Scene_init(&scene);
                ready = true;
            }
            float angles[3] = {0.0f, 0.0f, 0.785f}, mat[9];
            mat3_from_euler(angles, mat);
            mat[6] = -mat[6];
            mat[7] = -mat[7];
            mat[8] = -mat[8];
            const float pos[3] = {(float)((double)mat[6] * -10000.0f), (float)((double)mat[7] * -10000.0f),
                                  (float)((double)mat[8] * -10000.0f)};
            Scene_set_camera(&scene, pos, mat);
            const float down[3] = {0.0f, 0.0f, 1.0f};
            std::memcpy(scene.v_shadow, down, 12);
            std::memcpy(scene.v_light, down, 12);
            scene.count = 0;
            vehdraw_construct_vehicle(fake, &scene);
            Scene_set_scale(&scene, (float)((double)wide * 0.0007));
            Scene_set_rotation(&scene, (float)((double)(base_win().model_turn + 0x1E) * 0.017444445), 0.0f, 0.0f);
            Scene_render(&scene, c, cx, cy, 0);
        }
        r.top += 0x41;
        ZoomNum = -1;
        ZoomDenom = -1;
        StrBuffer[0] = 0;
        cat(Units[item].name);
        upper(StrBuffer);
        Buffer_set_text_color(c, tc(0x8C6CA4), tc(0x8C6CA8), 1, 1);
        Buffer_set_font(c, &fb, nullptr, nullptr, nullptr);
        r.top = Buffer_wrap_cent_4(c, StrBuffer, r.left, r.top, r.right - r.left);
        if (proto) {
            Buffer_set_text_color(c, tc(0x8C6D34), tc(0x8C6D38), 1, 1);
            StrBuffer[0] = 0;
            cat(label_get(0x2E4 / 4));
            Buffer_set_font(c, &fa, nullptr, nullptr, nullptr);
            cat(" ");
            cat("+");
            cat_num(prototype_factor(item));
            cat("%");
            r.top = Buffer_wrap_cent_4(c, StrBuffer, r.left, r.top, r.right - r.left);
        }
        r.left += 2;
        r.top += 2;
        r.right -= 2;
        r.bottom = r.bottom - 2 - line_h(&fa) * 2;
        minerals = cur_base().minerals_accumulated;
        per = cost_factor((uint8_t)cur_base().faction_id, 1, -1);
        total = veh_cost(item, CurrentBaseID, &proto);
    }
    total *= per;
    if (total) {
        int32_t s = cur_base().mineral_surplus;
        int32_t mx = total < minerals ? minerals : total, mn = total < minerals ? total : minerals;
        int32_t w = r.right - r.left;
        if (per * 2 < w) {
            int32_t h = r.bottom - r.top;
            while ((mx / per) * 2 > h) per *= 2;
        } else {
            per = 0;
            if (mx > 0) do
                    per++;
                while (per * per < mx);
            per += 2;
        }
        int32_t rows = mx / per;
        if (rows * per < mx) rows++;
        int32_t cw = w / per;
        int32_t ch = (r.bottom - r.top) / rows;
        int32_t y0 = ((r.bottom - ch * rows) - r.top) / 2 + r.top;
        int32_t x0 = ((r.right - cw * per) - r.left) / 2 + r.left;
        int32_t x1 = x0 + cw - 1, y1 = y0 + ch - 1;
        if (ch >= 8) {
            x0++;
            y0++;
        }
        int32_t n = 0;
        for (int32_t row = 0; row < rows && n <= mx; row++) {
            int32_t cx0 = x0, cx1 = x1;
            for (int32_t col = 0; col < per; col++) {
                UiRect cell{cx0, y0, cx1, y1};
                if (n < mn) {
                    if (ch < 8) Buffer_fill_3(c, &cell, 0x42);
                    else Buffer_tile_2(c, ArtSprite(0x6A7628 + 0xBCF4), cx0, y0, cx0, y0, cx1 - cx0, y1 - y0);
                } else if (ch < 8) {
                    Buffer_fill_3(c, &cell, total <= minerals ? 0x6C : 0xDF);
                } else {
                    Sprite* t = ArtSprite(0x6A7628 + (minerals < total ? 0xBCC8 : 0xBDA4));
                    Buffer_tile_2(c, t, cx0, y0, cx0, y0, cx1 - cx0, y1 - y0);
                }
                if (minerals < total) {
                    if (s < 0) {
                        if (mn + s <= n && n < mn) Buffer_box_2(c, &cell, 0x6C, 0x6C);
                    } else if (s > 0 && n + 1 <= mn + s && mn < n + 1) {
                        Buffer_box_2(c, &cell, 0x42, 0x42);
                    }
                }
                cx0 += cw;
                cx1 += cw;
                n++;
                if (n > mx) break;
            }
            y0 += ch;
            y1 += ch;
        }
        bool done = fac >= 0 && ((fac < 0x41 && base_fac(CurrentBaseID, fac)) ||
                                 (fac >= 0x46 && SecretProjects[fac - 0x46] == CurrentBaseID));
        StrBuffer[0] = 0;
        Font* f;
        if (done) {
            cat(label_get(0x7A4 / 4));
            Buffer_set_text_color(c, tc(0x8C6D3C), tc(0x8C6D40), 1, 1);
            f = &fb;
        } else {
            if (cur_base().mineral_surplus < 1) {
                cat(label_get(0x10C / 4));
            } else {
                cat(label_get(0xC4 / 4));
                upper(StrBuffer);
                cat(": ");
                cat(" ");
                if (minerals < total) {
                    int32_t sp = cur_base().mineral_surplus;
                    int32_t d = sp < 1 ? 1 : (sp > 99999 ? 99999 : sp);
                    int32_t turns = (sp - 1 + (total - minerals)) / d;
                    turns = turns < 0 ? 0 : (turns > 999999 ? 999999 : turns);
                    cat_num(turns);
                } else {
                    cat_num(0);
                }
            }
            r.top = r.bottom;
            r.bottom = r.bottom + fa.ascent_above * 2;
            Buffer_set_text_color(c, tc(0x8C6CFC), tc(0x8C6D00), 1, 1);
            f = &fa;
        }
        Font* keep = c->fonts[0];
        c->fonts[0] = f;
        Buffer_write_cent_l_7(c, StrBuffer, &r, (int32_t)std::strlen(StrBuffer));
        c->fonts[0] = keep;
    }
    Buffer_set_clip(c, &old);
    (void)flag;
    ui_invalidate();
}

// ==== BaseWin_draw_queue (0x415080): the production queue ========================================

void THISCALL BaseWin_draw_queue(uint8_t* self, int32_t flag) {
    if (flag && !Win_is_visible(self)) return;
    Buffer* c = mic();
    Font f1, f2;
    Font_ctor(&f1);
    Font_ctor(&f2);
    UiRect old = c->clip;
    UiRect pane = base_win().rect_queue_box_mi;
    Buffer_set_clip(c, &pane);
    Buffer_box_sprite(c, &pane, box_def(3));
    bool w800 = ScreenWidth == 800;
    Font_init(&f1, "Arial", w800 ? 10 : 0xC, 0);
    f1.field_0 = 4;
    Font_init(&f2, "Arial", w800 ? 8 : 10, 0);
    f2.field_0 = 4;
    spot_kill_type(&g_bw.spots_iface, 8);
    UiRect t = base_win().rect_queue_list_mi;
    Buffer_box_sprite(c, &t, box_def(6));
    inset(&t, 3);
    Buffer_set_font(c, bw_font(), nullptr, nullptr, nullptr);
    Buffer_set_text_color(c, tc(0x8C6CA4), tc(0x8C6CA8), 1, 1);
    StrBuffer[0] = 0;
    cat(label_get(0x77C / 4));
    upper(StrBuffer);
    Buffer_write_cent_l_7(c, StrBuffer, &t, (int32_t)std::strlen(StrBuffer));
    Buffer_set_font(c, &f1, nullptr, nullptr, nullptr);
    t = base_win().rect_queue_list2_mi;
    Buffer_box_sprite(c, &t, box_def(0));
    int32_t pad = w800 ? 5 : 2;
    t.right -= 5;
    t.left += 5;
    int32_t y = t.top + pad;
    for (int32_t slot = 1; slot < 9; slot++) {
        Buffer_set_font(c, &f1, nullptr, nullptr, nullptr);
        int32_t lh = line_h(&f1);
        UiRect row{t.left, y, t.right, y + lh};
        spot_add(&g_bw.spots_iface, slot + 2, 8, row.left, row.top, row.right - row.left, row.bottom - row.top);
        if (base_win().queue_sel == slot)
            Buffer_tile(c, box_def(29)->center, row.left, row.top, row.left, row.top, row.right - row.left,
                        row.bottom - row.top);
        else if (base_win().queue_hilite == slot)
            Buffer_tile(c, box_def(27)->center, row.left, row.top, row.left, row.top, row.right - row.left,
                        row.bottom - row.top);
        StrBuffer[0] = 0;
        cat_num(slot);
        cat(". ");
        char name[256];
        name[0] = 0;
        if (cur_base().queue_size < slot) {
            Buffer_set_text_color(c, tc(0x8C6D84), tc(0x8C6D88), 1, 1);
            crt_strcat(name, label_get(0x730 / 4));
        } else {
            Buffer_set_text_color(c, tc(0x8C6D7C), tc(0x8C6D80), 1, 1);
            int32_t item = cur_base().queue_items[slot];
            crt_strcat(name, item < 0 ? Strings_get(&StringTable, Facility[-item].name) : Units[item].name);
        }
        if (base_win().queue_sel == slot || base_win().queue_hilite == slot) Buffer_set_text_color(c, 0x41, -1, 1, 1);
        const char* n = name;
        if (GameLanguage == 0 && !std::strncmp(name, "The ", 4)) n = name + 4;
        cat(n);
        UiRect w = row;
        int32_t avail = w.right - w.left;
        if (avail < Font_width(&f1, StrBuffer)) {
            Buffer_set_font(c, &f2, nullptr, nullptr, nullptr);
            avail = w.right - w.left;
            int32_t tw = Font_width(&f2, StrBuffer);
            if (avail < tw) w.right -= Font_width(&f2, "+");
            Buffer_set_clip(c, &w);
            Buffer_write_l_4(c, StrBuffer, &w, (int32_t)std::strlen(StrBuffer));
            UiRect all{0, 0, c->width, c->height};
            Buffer_set_clip(c, &all);
            if (avail < tw) {
                w.left = w.right;
                Buffer_write_l_4(c, "+", &w, 1);
            }
        } else {
            Buffer_set_clip(c, &w);
            Buffer_write_l_4(c, StrBuffer, &w, (int32_t)std::strlen(StrBuffer));
            UiRect all{0, 0, c->width, c->height};
            Buffer_set_clip(c, &all);
        }
        y += line_h(&f1);
    }
    Buffer_set_clip(c, &old);
    ui_invalidate();
}

// ==== pfncDrawCityMap (0x40F0F0): the resource map ===============================================

namespace {

// Polygon edge of polygon / polygon_dest (polygon_edge (0x626470)).
struct PolyEdge {
    int32_t dir, count, end, x, xstep, sign, err, rem, dy;
};
bool poly_edge(const int32_t* pts, int32_t n, int32_t bottom, PolyEdge* e, int32_t start) {
    if (start == bottom) return false;
    int32_t next;
    for (;;) {
        next = e->dir + start;
        if (n - 1 < next) next = 0;
        else if (next < 0) next = n - 1;
        e->count = pts[next * 2 + 1] - pts[start * 2 + 1];
        if (e->count != 0) break;
        start = next;
        if (next == bottom) return false;
    }
    if (e->count < 0) return false;
    e->end = next;
    e->x = pts[start * 2];
    int32_t dx = pts[next * 2] - pts[start * 2];
    if (dx < 0) {
        e->sign = -1;
        dx = -dx;
        e->err = 1 - e->count;
        e->xstep = -(dx / e->count);
    } else {
        e->sign = 1;
        e->xstep = dx / e->count;
        e->err = 0;
    }
    e->rem = dx % e->count;
    e->dy = e->count;
    return true;
}
bool poly_step(const int32_t* pts, int32_t n, int32_t bottom, PolyEdge* e) {
    if (--e->count == 0) return poly_edge(pts, n, bottom, e, e->end);
    e->err += e->rem;
    e->x += e->xstep;
    if (e->err > 0) {
        e->x += e->sign;
        e->err -= e->dy;
    }
    return true;
}

// The resource icons of NEWICONS.PCX (load_icons 0x4547E0): 0x7A72A0 [4 resources x 8
// amounts], 0x78A520 (no resources).
void load_resource_icons() {
    static bool done = false;
    if (done) return;
    done = true;
    Buffer art;
    Buffer_ctor(&art);
    if (Buffer_load_pcx(&art, "NEWICONS.PCX", nullptr, 10, 0xEC)) return;
    auto extract = [&](uint32_t addr, int32_t x, int32_t y, int32_t w, int32_t h) {
        Buffer_change_color(&art, 0x107, 0x109);
        Buffer_change_color(&art, 0x106, 0x109);
        pfncSpriteFromCanvasRectTrans(ArtSprite(addr), &art, 0x109, x, y, w, h, 0);
    };
    extract(0x78A520, 1, 0xAE, 0x16, 0x16);
    uint32_t a = 0x7A72A0;
    for (int32_t row = 0, y = 0x130; row < 4; row++, y += 0x29)
        for (int32_t col = 0, x = 0xAE; col < 8; col++, x += 0x29, a += 0x2C) extract(a, x, y, 0x28, 0x28);
    // The support icons (load_icons): 0x75B450 [4 x 4, 22 pixels: by resource], 0x75AF10 [2].
    for (int32_t i = 0; i < 16; i++)
        extract(0x75B450 + (uint32_t)i * 0x2C, 1 + (i % 4) * 0x17, 0xD + (i / 4) * 0x17, 0x16, 0x16);
    extract(0x75AF10, 1, 0xDC, 0x16, 0x16);
    extract(0x75AF3C, 1, 0xF3, 0x16, 0x16);
    // The citizens (load_cursors 0x454FC0): NEWICONS.PCX faces 0x78CAD0 [4 moods x 2 looks] and
    // specialists 0x7765C0 [7]; ALIENCIT.PCX 0x7AD1B0 [4] and 0x798CD0 [7].
    int32_t x = 1;
    for (int32_t mood = 0; mood < 4; mood++)
        for (int32_t look = 0; look < 2; look++, x += 0x27)
            extract(0x78CAD0 + (uint32_t)(mood * 0x2C + look * 0xB0), x, 0x1F5, 0x26, 0x30);
    for (int32_t i = 0; i < 7; i++, x += 0x27) extract(0x7765C0 + (uint32_t)i * 0x2C, x, 0x1F5, 0x26, 0x30);
    // Small citizens of the psych view: 0x776420 [4 moods x 2 looks], 0x779B78 [7].
    x = 0x2C;
    for (int32_t mood = 0; mood < 4; mood++)
        for (int32_t look = 0; look < 2; look++, x += 0x1F)
            extract(0x776420 + (uint32_t)(mood * 0x2C + look * 0xB0), x, 0x1CA, 0x1E, 0x26);
    for (int32_t i = 0; i < 7; i++, x += 0x1F) extract(0x779B78 + (uint32_t)i * 0x2C, x, 0x1CA, 0x1E, 0x26);
    if (!Buffer_load_pcx(&art, "ALIENCIT.PCX", nullptr, 10, 0xEC)) {
        int32_t ax = 1;
        for (int32_t i = 0; i < 4; i++, ax += 0x1F) extract(0x75B180 + (uint32_t)i * 0x2C, ax, 1, 0x1E, 0x26);
        for (int32_t i = 0; i < 7; i++, ax += 0x1F) extract(0x75B710 + (uint32_t)i * 0x2C, ax, 1, 0x1E, 0x26);
        x = 1;
        for (int32_t i = 0; i < 4; i++, x += 0x27) extract(0x7AD1B0 + (uint32_t)i * 0x2C, x, 0x29, 0x26, 0x30);
        for (int32_t i = 0; i < 7; i++, x += 0x27) extract(0x798CD0 + (uint32_t)i * 0x2C, x, 0x29, 0x26, 0x30);
    }
    // The colour table of the tiles another base works (colortables_init: 0x6F0F7C, palette colour
    // 0x83 blended 75:50).
    Palette_create_table_from_color(&GamePalette, 0x83, mem::host(0x6F0F7C), 0, 0x100, 0x4B, 0x32);
}

Sprite* res_icon(int32_t i) { return ArtSprite(0x7A72A0 + (uint32_t)i * 0x2C); }

const int32_t kCityDx[21] = {-1, 1, -2, 0, 2, -3, -1, 1, 3, -2, 0, 2, -3, -1, 1, 3, -2, 0, 2, -1, 1};
const int32_t kCityDy[21] = {-3, -3, -2, -2, -2, -1, -1, -1, -1, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 3, 3};

}  // namespace

// Buffer_write_right_l_5 (0x5DD3C0-): len characters ending at x.
int32_t buffer_write_right(Buffer* b, const char* text, int32_t x, int32_t y, int32_t len) {
    if (!text) return x;
    if (!b->fonts[0] || !b->fonts[0]->handle) return 3;
    int32_t n = (int32_t)std::strlen(text);
    if (n < len) len = n;
    if (len <= 0) return x;
    return Buffer_write_multi_font_raw_l(b, text, x - Buffer_text_width_2(b, text, len), y, len);
}

// polygon_dest (0x626E10): the polygon's pixels translated through `table`.
int32_t GAMEAPI polygon_dest(Buffer* self, const int32_t* pts, int32_t n, const uint8_t* table) {
    if (!pts || !self || !self->data) return 0x10;
    int32_t right = self->clip.right - 1, bottom_clip = self->clip.bottom - 1;
    int32_t top_y = 0x7FFF, bottom_y = -0x7FFD, top = 0, bottom = 0;
    for (int32_t i = 0; i < n; i++) {
        int32_t y = pts[i * 2 + 1];
        if (y < top_y) {
            top_y = y;
            top = i;
        }
        if (bottom_y < y) {
            bottom_y = y;
            bottom = i;
        }
    }
    if (top_y >= bottom_y) return 0;
    PolyEdge a{}, b{};
    a.dir = -1;
    b.dir = 1;
    if (!poly_edge(pts, n, bottom, &a, top) || !poly_edge(pts, n, bottom, &b, top)) return 0;
    for (int32_t y = top_y; y < bottom_clip; y++) {
        if (self->clip.top <= y) {
            int32_t lo = a.x < b.x ? a.x : b.x, hi = a.x < b.x ? b.x : a.x;
            if (lo < hi && self->clip.left < hi && lo < right) {
                int32_t end = hi < right ? hi : right, start = lo < self->clip.left ? self->clip.left : lo;
                uint8_t* p = self->data + (size_t)y * self->stride;
                for (int32_t x = start; x < end; x++) p[x] = table[p[x]];
            }
        }
        if (!poly_step(pts, n, bottom, &a) || !poly_step(pts, n, bottom, &b)) break;
    }
    return 0;
}

void THISCALL pfncDrawCityMap(uint8_t* self, int32_t flag) {
    load_resource_icons();
    Buffer* c = bwc();
    UiRect old = c->clip;
    UiRect m = base_win().rect_map;
    UiRect r = m;
    Buffer_set_clip(c, &r);
    Buffer_box_sprite(c, &m, box_def(15));
    inset(&r, 1);
    Buffer_fill_3(c, &r, 0x41);
    spot_kill_type(&g_bw.spots_win, 0);
    spot_add(&g_bw.spots_win, 0, 0, m.left, m.top, m.right - m.left, m.bottom - m.top);
    int32_t mode = base_win().pane;
    bool psych = flag < 2 ? mode == 1 : flag == 2;
    bool support = flag < 2 ? mode == 2 : flag == 3;
    if (psych || support) {
        if (psych) BaseWin_draw_psych(self, flag);
        else BaseWin_draw_support(self, flag);
    } else {
        int32_t q = ZoomNum < 1 ? 0x32 : ZoomNum * 0x32 / ZoomDenom;
        q = (q + 1) / 4;  // FP-free: truncation toward zero as the original's sar with rounding
        int32_t x0 = (m.right - m.left) / 2 - q * 4 + m.left;
        int32_t y0 = q * 6 + 4 + m.top;
        int32_t q2 = q * 2;
        uint8_t* city = mem::host(0x7D4060);
        int32_t& elev = map_win(city).elev_scale;
        elev = 0;
        ZoomNum = 1;
        ZoomDenom = 1;
        BASE& b = cur_base();
        auto radius_index = [](int32_t dx, int32_t dy) {
            for (int32_t i = 0; i < 0x15; i++)
                if (dx == TableOffsetX[i] && dy == TableOffsetY[i]) return i;
            return 0;
        };
        for (int32_t k = 0; k < 21; k++) {
            int32_t dx = kCityDx[k], dy = kCityDy[k];
            int32_t x = b.x + dx;
            if (!(MapToggleFlat & 1)) {
                if (x < 0) x += MapAreaX;
                else if (x >= MapAreaX) x -= MapAreaX;
            }
            int32_t y = b.y + dy;
            if (y < 0 || y >= MapAreaY || x < 0 || x >= MapAreaX) continue;
            int32_t i = radius_index(dx, dy);
            uint32_t tf = (uint32_t)BaseTileFlags[i];
            if (tf & 2) continue;
            int32_t px = dx * q * 4 + x0, py = dy * q * 2 + y0;
            MapWin_gen_terrain_poly(c, nullptr, 0, (uint8_t)b.faction_id, i, 0x4010621, x, y, px, py);
            if (tf & 0x20) polygon_dest(c, &TileCorners[0][0], 4, mem::host(0x6F0F7C));
            if (!(tf & 4) && i != 0) {
                if (tf & 8) {
                    int32_t v = veh_at(x, y);
                    if (v >= 0) veh_draw(c, v, px, py - q2, 0, 0, 0);
                } else if (tf & 0x10) {
                    polygon_dest(c, &TileCorners[0][0], 4, mem::host(0x6F0F7C));
                    Texture_draw_trans_3(ArtTexture(0x787FB8), c, &TileCorners[0][0], 0);  // the cursor texture
                }
            } else {
                int32_t bid = base_at(x, y);
                if (bid >= 0) base_draw(c, bid, px, py - q2, 0, 0);
            }
        }
        elev <<= 2;
        ZoomNum = 1;
        ZoomDenom = 1;
        BlitDestScale = 1;
        BlitSourceScale = 1;
        for (int32_t k = 0; k < 21; k++) {
            int32_t dx = kCityDx[k], dy = kCityDy[k];
            BASE& cb = cur_base();
            int32_t x = cb.x + dx;
            if (!(MapToggleFlat & 1)) {
                if (x < 0) x += MapAreaX;
                else if (x >= MapAreaX) x -= MapAreaX;
            }
            int32_t y = cb.y + dy;
            if (y < 0 || y >= MapAreaY || x < 0 || x >= MapAreaX) continue;
            int32_t i = radius_index(dx, dy);
            if ((BaseTileFlags[i] & 2) || !(cb.worked_tiles & (1u << i))) continue;
            int32_t px = dx * q2 * 2 + x0;
            // (the corner heights of the tile and the base cancel: the window's elevation is 0)
            int32_t yy = dy * q2 + y0;
            int32_t res[4], extra[4], sum = 0;
            for (int32_t t = 0; t < 4; t++) {
                BaseTerraformReduce = 0;
                res[t] = resource_yield(t, (uint8_t)cur_base().faction_id, CurrentBaseID, x, y);
                sum += res[t];
                extra[t] = BaseTerraformReduce;
            }
            int32_t total = 0;
            if (sum) {
                for (int32_t t = 0; t < 4; t++) {
                    if (!res[t]) continue;
                    int32_t idx = res[t] - 1 < 0 ? 0 : (res[t] - 1 > 7 ? 7 : res[t] - 1);
                    int32_t w = res_icon(idx + t * 8)->width;
                    total += ZoomNum < 1 ? (w * 0x11) / 16 : w * ZoomNum / ZoomDenom;
                }
            } else {
                int32_t w = ArtSprite(0x78A520)->full_width;
                total = ZoomNum < 1 ? (w * 0x11) / 16 : w * ZoomNum / ZoomDenom;
            }
            int32_t ix = (q2 * 4) / 2 - total / 2 + px;
            int32_t ih = ZoomNum < 1 ? 0x2A : ZoomNum * 0x28 / ZoomDenom;
            int32_t iy = (yy - (ih >> 1)) + q2;
            for (int32_t t = 0; t < 4; t++) {
                if (!res[t]) continue;
                int32_t idx = res[t] - 1 < 0 ? 0 : (res[t] - 1 > 7 ? 7 : res[t] - 1);
                int32_t idx2 = extra[t] + idx;
                idx2 = idx2 < 0 ? 0 : (idx2 > 7 ? 7 : idx2);
                if (idx == idx2) {
                    Sprite* sp = res_icon(idx + t * 8);
                    sprite_copy1(sp, c, sp->key, ix, iy, 1, 1);
                } else {
                    Sprite* big = res_icon(idx2 + t * 8);
                    int32_t od = BlitDestScale, os = BlitSourceScale;
                    BlitDestScale = 1;
                    BlitSourceScale = 1;
                    Sprite_draw_mono_2(big, c, big->key & 0xFF, ix, iy, 0);
                    BlitDestScale = od;
                    BlitSourceScale = os;
                    Sprite* sp = res_icon(idx + t * 8);
                    sprite_copy1(sp, c, sp->key, (idx2 - idx) / 2 + ix, iy, 1, 1);
                }
                ix += res_icon(idx2 + t * 8)->width;
            }
            if (!sum) {
                Sprite* z = ArtSprite(0x78A520);
                pfncSpriteStretchCopyToCanvas(z, c, z->key & 0xFF, ix, iy);
            }
        }
        // The base's state: a line in the map's top right corner.
        Buffer_set_font(c, find_font(0xC, 1), nullptr, nullptr, nullptr);
        StrBuffer[0] = 0;
        uint32_t ev = (uint32_t)cur_base().event_flags;
        struct {
            uint32_t bit, label, what, color;
        } kEvents[6] = {{0x200, 0xBE0, 0xBD8, 0x8C6CC4},  {0x400, 0xBE4, 0xBDC, 0x8C6CC4},
                        {0x800, 0xBE8, 0xBD8, 0x8C6CD4},  {0x1000, 0xBEC, 0xBDC, 0x8C6CD4},
                        {0x2000, 0xBF0, 0xBD8, 0x8C6CB4}, {0x4000, 0xBF4, 0xBDC, 0x8C6CB4}};
        bool any = false;
        for (auto& e : kEvents) {
            if (!(ev & e.bit)) continue;
            cat(label_get((int32_t)(e.label / 4)));
            cat(": ");
            cat(label_get((int32_t)(e.what / 4)));
            Buffer_set_text_color(c, tc(e.color), tc(e.color + 4), 1, 1);
            any = true;
            break;
        }
        if (!any && DustCloudDuration > 0) {
            cat(label_get(0xBF8 / 4));
            cat(": ");
            cat(label_get(0xBDC / 4));
            Buffer_set_text_color(c, tc(0x8C6CB4), tc(0x8C6CB8), 1, 1);
        }
        buffer_write_right(c, StrBuffer, m.right - 2, m.top + 2, (int32_t)std::strlen(StrBuffer));
    }
    BlitDestScale = 1;
    BlitSourceScale = 1;
    ZoomNum = -1;
    ZoomDenom = -1;
    Buffer_set_clip(c, &old);
    ui_invalidate();
}

// ==== the lower pane modes of the map: psych (BaseWin_draw_psych 0x408B20) and support ===========

namespace {
// BaseWin_psych_row (0x408720): a row of the psych view's citizens.
void psych_row(Buffer* c, int32_t x, int32_t y, int32_t w, int32_t step, int32_t talents, int32_t drones,
               int32_t sdrones) {
    BASE& b = cur_base();
    if (b.pop_size > 1) {
        int32_t s = (w - step) / (b.pop_size - 1);
        step = (s < 1 || step < 1) ? 1 : (s <= step ? s : step);
    }
    uint32_t seed = platform::ticks_ms();
    auto look = [&seed]() {
        seed = seed * 0x19660D + 0x3C6EF35F;
        return (int32_t)((2u * (seed & 0xFFFF)) >> 16);
    };
    auto put = [&](uint32_t addr) {
        Sprite* sp = ArtSprite(addr);
        pfncSpriteStretchCopyToCanvas(sp, c, sp->key & 0xFF, x, y);
        x += step;
    };
    bool al = ExpansionEnabled && (MFactions[(uint8_t)b.faction_id].rule_flags & 0x80);
    int32_t workers = b.pop_size - b.specialist_total - talents - drones;
    for (int32_t i = 0; i < talents; i++) put(al ? 0x75B180 : 0x776420 + (uint32_t)look() * 0xB0);
    for (int32_t i = 0; i < workers; i++) put(al ? 0x75B1AC : 0x77644C + (uint32_t)look() * 0xB0);
    for (int32_t i = 0; i < drones; i++) {
        int32_t mood = (drones - sdrones <= i) + 2;
        put(al ? 0x75B180 + (uint32_t)mood * 0x2C : 0x776420 + (uint32_t)(mood + look() * 4) * 0x2C);
    }
    for (int32_t i = 0; i < cur_base().specialist_total; i++) {
        int32_t t = i < 0x10 ? (cur_base().specialist_types[i >> 3] >> ((i & 7) << 2)) & 0xF : best_specialist();
        put((al ? 0x75B710 : 0x779B78) + (uint32_t)t * 0x2C);
    }
}
}  // namespace

void THISCALL BaseWin_draw_psych(uint8_t* self, int32_t) {
    load_resource_icons();
    Buffer* c = bwc();
    Font font;
    Font_ctor(&font);
    UiRect m = base_win().rect_map;
    Buffer_set_clip(c, &m);
    Buffer_fill_3(c, &m, 0);
    Font_init(&font, "Arial", 0xC, 1);
    int32_t f = (uint8_t)cur_base().faction_id;
    int32_t width = m.right - m.left;
    int32_t row_h = ArtSprite(0x776420)->full_height + 1;
    ZoomNum = 0x1E;
    ZoomDenom = 0x26;
    Buffer_set_font(c, &font, nullptr, nullptr, nullptr);
    int32_t ty = row_h / 2 - font.ascent_above / 2;
    int32_t y = m.top + 2;
    int32_t row = 0;
    for (int32_t k = 0; k < 5; k++, y += row_h, row++) {
        if (row != 0) {
            while (row < 5 && BasePsychTalents[row] == BasePsychTalents[row - 1] &&
                   BasePsychNDrones[row] == BasePsychNDrones[row - 1] &&
                   BasePsychSDrones[row] == BasePsychSDrones[row - 1])
                row++;
        }
        if (row > 4) continue;
        int32_t w2 = width / 2 - 4;
        int32_t lx = (m.right - w2) - 2;
        StrBuffer[0] = 0;
        BASE& b = cur_base();
        switch (row) {
        case 0:
            if (b.assimilation_turns_left && b.faction_id_former != b.faction_id) cat(label_get(0xF28 / 4));
            else cat(label_get(b.nerve_staple_turns_left ? 0xF2C / 4 : 0x508 / 4));
            break;
        case 1: cat(label_get(0x50C / 4)); break;
        case 2: cat(label_get(0x510 / 4)); break;
        case 3:
            cat(label_get((Factions[f].SE_police_pending + (base_fac(CurrentBaseID, 0x23) ? 2 : 0) < -1) + 0x145));
            break;
        default: cat(label_get(0x51C / 4)); break;
        }
        Buffer_set_text_color(c, tc(0x8C6D24), tc(0x8C6D28), 1, 1);
        Buffer_write_cent_l_5(c, StrBuffer, lx, ty - 2 + y, w2, (int32_t)std::strlen(StrBuffer));
        psych_row(c, m.left + 2, y, w2, ArtSprite(0x776420)->full_width + 1, BasePsychTalents[row],
                  BasePsychNDrones[row], BasePsychSDrones[row]);
    }
    ZoomNum = -1;
    ZoomDenom = -1;
    ui_invalidate();
}

// BaseWin_draw_support (0x408EB0): a map of the planet around the base, drawn by the window's own
// map window (the MapWin member at +0x1E674, MapWin_init_2(2): the pane's rectangle) at the
// largest zoom step (2, 1, 0, ...) at which the whole map fits, centred on the base, with options
// 0x200402; copied into the pane, centred.
void THISCALL BaseWin_draw_support(uint8_t* self, int32_t) {
    static Buffer canvas;
    Buffer* c = bwc();
    UiRect m = base_win().rect_map;
    UiRect saved = c->clip;
    Buffer_set_clip(c, &m);
    Buffer_fill_3(c, &m, 0);
    uint8_t* map = self + 0x1E674;
    int32_t w = m.right - m.left, h = m.bottom - m.top;
    if (w <= 0 || h <= 0) return;
    // MapWin_init_2(2, 0): the window over the pane (options 0x80000082, zoom -7, replaced below).
    MapWinNative* n = ui_mapwin_native(map);
    if (!n || canvas.width != w || canvas.height != h) {
        Buffer_ctor(&canvas);
        Buffer_init(&canvas, w, h, 8, 0);
        ui_mapwin_attach(map, kMapWinVBase, &canvas);
    }
    map_win(map).active = 1;
    map_win(map).options = (int32_t)0x80000082;
    map_win(map).zoom = -7;
    map_win(map).elev_scale = 0x19 >> 1;
    map_win(map).options = 0x200402;
    int32_t zoom = 2, step_w = 900, step_h = 0x1C2, half_x = MapAreaX / 2;
    do {
        int32_t tw = ZoomNum < 1 ? step_w / 16 : ZoomNum * 0x32 / ZoomDenom;
        if (half_x * tw <= w) {
            int32_t th = ZoomNum < 1 ? step_h / 16 : ZoomNum * 0x19 / ZoomDenom;
            if ((MapAreaY / 2) * th <= h) break;
        }
        zoom--;
        step_w -= 0x32;
        step_h -= 0x19;
    } while (step_w > 0x32);
    map_win(map).zoom = zoom;
    map_win(map).center_x = Bases[CurrentBaseID].x;
    map_win(map).center_y = Bases[CurrentBaseID].y;
    pfncRedrawMap(map, 1);  // vtable +0xF8
    Buffer_copy_4(&canvas, c, 0, 0, (m.right - m.left) / 2 - w / 2 + m.left, (m.bottom - m.top) / 2 - h / 2 + m.top, w,
                  h);
    Buffer_set_clip(c, &saved);
    ui_invalidate();
}

// ==== BaseWin_draw_pop (0x414B40): the citizens ===================================================

void THISCALL BaseWin_draw_pop(uint8_t* self, int32_t flag) {
    load_resource_icons();
    Buffer* c = mic();
    // The faces' looks come from a generator seeded with the clock (Random_reseed(timeGetTime())).
    uint32_t seed = platform::ticks_ms();
    auto random_get = [&seed](int32_t lo, int32_t hi) {
        seed = seed * 0x19660D + 0x3C6EF35F;
        return (int32_t)(((uint32_t)(hi - lo) * (seed & 0xFFFF)) >> 16) + lo;
    };
    UiRect pane = base_win().rect_pop_mi;
    UiRect old = c->clip;
    Buffer_set_clip(c, &pane);
    Buffer_box_sprite(c, &pane, box_def(3));
    spot_kill_type(&g_bw.spots_iface, 1);
    UiRect r = pane;
    inset(&r, 3);
    Buffer_set_clip(c, &r);
    BASE& b = cur_base();
    int32_t pop = b.pop_size;
    int32_t h = r.bottom - r.top, step;
    if (pop <= 1) step = h;
    else {
        int32_t s = ((r.right - r.left) - h) / (pop - 1);
        step = (s < 1 || h < 1) ? 1 : (s > h ? h : s);
    }
    bool al = ExpansionEnabled && (MFactions[(uint8_t)b.faction_id].rule_flags & 0x80);
    int32_t x = r.left;
    for (int32_t i = 0; i < cur_base().pop_size; i++, x += step) {
        BASE& cb = cur_base();
        int32_t spec = cb.specialist_total;
        int32_t index, y = (h - 0x30) / 2 + r.top;
        Sprite* sp;
        if (i < cb.pop_size - spec) {
            int32_t mood = i >= cb.talent_total ? 1 : 0;
            if (i >= cb.pop_size - cb.drone_total - spec) mood = 2;
            if (i >= cb.pop_size - cb.superdrone_total - spec) mood = 3;
            if (al) sp = ArtSprite(0x7AD1B0 + (uint32_t)mood * 0x2C);
            else sp = ArtSprite(0x78CAD0 + (uint32_t)(mood + random_get(0, 2) * 4) * 0x2C);
            index = -1;
        } else {
            index = spec - cb.pop_size + i;
            int32_t type =
                index < 0x10 ? (cb.specialist_types[index >> 3] >> ((index & 7) << 2)) & 0xF : best_specialist();
            sp = ArtSprite((al ? 0x798CD0 : 0x7765C0) + (uint32_t)type * 0x2C);
        }
        sprite_copy1(sp, c, sp->key, x, y, 1, 1);
        spot_add(&g_bw.spots_iface, index, 1, x, r.top, h, h);
    }
    Buffer_set_clip(c, &old);
    (void)flag;
    ui_invalidate();
}

// ==== BaseWin_draw_supported (0x40C850): the units the base supports =============================

namespace {
// The support cost icons of a unit (BaseWin_draw_supported); `step` keeps its value from one unit
// to the next, as the original's stack slot does.
void support_icons(Buffer* c, int32_t veh, int32_t x, int32_t y, int32_t cw, int32_t* step) {
    const VEH& v = Vehs[veh];
    int32_t f = (uint8_t)cur_base().faction_id;
    const Faction& fa = Factions[f];
    int32_t minerals = 0, n = 0;
    if (v.state & 0x10) minerals = has_abil(v.unit_id, 0x10000) ? 0 : (fa.SE_support_pending < -3) + 1;
    n = minerals;
    int32_t police = (fa.SE_police_pending + (base_fac(CurrentBaseID, 0x23) ? 1 : 0) * 2 < -4) + 1;
    if (v.state & 0x600000) n += police;
    int32_t convoy = 0;
    bool convoy_unit = Units[v.unit_id].plan == 10 && v.order == 3;
    bool in_base = false;
    if (convoy_unit) {
        MAP* sq = map_tile(v.x, v.y);
        in_base = (sq->items & 1) && (sq->val2 & 0xF) <= 7;
        if (!in_base) {
            convoy = resource_yield(v.order_auto_type, f, CurrentBaseID, v.x, v.y);
            n += convoy;
        } else {
            convoy = 1;
            n += 1;
        }
    }
    if (!n) return;
    auto put = [&](uint32_t addr) {
        Sprite* sp = ArtSprite(addr);
        pfncSpriteStretchCopyToCanvas(sp, c, sp->key & 0xFF, x, y);
        x += *step;
    };
    if (v.state & 0x400000) put(0x75AF10);
    else if (v.state & 0x200000)
        for (int32_t k = 0; k < police; k++) put(0x75AF3C);
    *step = 0xD;
    if (n > 1) {
        int32_t s = (cw - 0xD) / (n - 1);
        *step = s < 1 ? 1 : (s > 0xD ? 0xD : s);
    }
    for (int32_t k = 0; k < minerals; k++) put(0x75B52C);
    if (convoy_unit) {
        if (!in_base)
            for (int32_t k = 0; k < convoy; k++) put(0x75B4A8 + (uint32_t)v.order_auto_type * 0xB0);
        else put(0x75B47C + (uint32_t)v.order_auto_type * 0xB0);
    }
}
}  // namespace

void THISCALL BaseWin_draw_supported(uint8_t* self, int32_t flag) {
    load_resource_icons();
    Buffer* c = mic();
    Font font;
    Font_ctor(&font);
    UiRect old = c->clip;
    UiRect pane = base_win().rect_prod_pane_mi;
    Buffer_set_clip(c, &pane);
    Buffer_box_sprite(c, &pane, box_def(3));
    Font_init(&font, "Arial", 10, 1);
    spot_kill_type(&g_bw.spots_iface, 7);
    UiRect t = base_win().rect_prod_mi;
    Buffer_box_sprite(c, &t, box_def(6));
    inset(&t, 3);
    Buffer_set_font(c, bw_font(), nullptr, nullptr, nullptr);
    Buffer_set_text_color(c, tc(0x8C6CA4), tc(0x8C6CA8), 1, 1);
    StrBuffer[0] = 0;
    cat(label_get(0x100 / 4));
    upper(StrBuffer);
    Buffer_write_cent_l_7(c, StrBuffer, &t, (int32_t)std::strlen(StrBuffer));
    Buffer_set_font(c, &font, nullptr, nullptr, nullptr);
    UiRect r = base_win().rect_prod2_mi;
    Buffer_box_sprite(c, &r, box_def(0));
    inset(&r, 3);
    int32_t f = (uint8_t)cur_base().faction_id;
    int32_t cw, ch, sx;
    if (ZoomNum < 1) {
        BlitDestScale = 10;
        BlitSourceScale = 0x10;
        cw = 0x3E;
        ch = 0x2E;
        sx = 0xD;
    } else {
        BlitDestScale = ZoomNum;
        BlitSourceScale = ZoomDenom;
        cw = ZoomNum * 100 / ZoomDenom;
        ch = ZoomNum * 0x4B / ZoomDenom;
        sx = ZoomNum * 0x16 / ZoomDenom;
    }
    int32_t count = 0;
    for (int32_t i = 0; i < VehCount; i++)
        if (Vehs[i].faction_id == (uint32_t)f && Vehs[i].home_base_id == CurrentBaseID) count++;
    int32_t cols = (r.right - r.left - 0xC) / cw;
    cols = cols < 1 ? 1 : (cols > 3 ? 3 : cols);
    int32_t rows = (cols + count - 1) / cols;
    rows = rows < 1 ? 1 : (rows > 3 ? 3 : rows);
    int32_t x0;
    if (count > rows * cols) {
        x0 = (r.right - (count < cols ? count : cols) * cw - r.left - 0xC) / 2;
        // A scroll bar (scroll_small) at the right edge, range 0 .. rows - 1 (BaseWin_draw_supported:
        // Scroll_init_2 on the main interface).
        base_win().sup_bar = 1;
        g_sup_bar_shown = true;
        g_sup_bar.r = {r.right - 0xF, r.top, r.right, r.bottom};
        g_sup_bar.small = true;
        g_sup_bar.min = 0;
        g_sup_bar.max = (count + cols - 1) / cols - 1;
        g_sup_bar.pos = std::max(0, std::min(base_win().sup_scroll, g_sup_bar.max));
    } else {
        x0 = (r.right - (count < cols ? count : cols) * cw - r.left) / 2;
        base_win().sup_bar = 0;
        g_sup_bar_shown = false;
    }
    x0 += r.left;
    int32_t y = r.top + (r.bottom - rows * ch - r.top) / 2;
    int32_t x = x0, drawn = 0, row = 0, k = -1, step = 0;
    for (int32_t i = 0; i < VehCount; i++) {
        if (Vehs[i].faction_id != (uint32_t)f || Vehs[i].home_base_id != CurrentBaseID) continue;
        k++;
        if (base_win().sup_bar && !(base_win().sup_scroll * cols <= k && k < (base_win().sup_scroll + rows) * cols))
            continue;
        veh_draw(c, i, x, y, -6, 0x48, 0);
        if (ZoomNum < 1) {
            BlitDestScale = 10;
            BlitSourceScale = 0x10;
        } else {
            BlitDestScale = ZoomNum;
            BlitSourceScale = ZoomDenom;
        }
        support_icons(c, i, x, (y - sx) + ch, cw, &step);
        spot_add(&g_bw.spots_iface, i, 7, x, y, cw, ch);
        x += cw;
        drawn++;
        if (drawn % cols == 0) {
            x = x0;
            y += ch;
            if (++row >= rows) break;
        }
    }
    BlitDestScale = 1;
    BlitSourceScale = 1;
    if (base_win().sup_bar && g_sup_bar_shown) scroll_draw(c, g_sup_bar);
    Buffer_set_clip(c, &old);
    (void)flag;
    ui_invalidate();
}

// ==== BaseWin_draw_garrison (0x4103B0): the units in the base ====================================

void THISCALL BaseWin_draw_garrison(uint8_t* self, int32_t flag) {
    Buffer* c = mic();
    Font font;
    Font_ctor(&font);
    UiRect old = c->clip;
    UiRect pane = base_win().rect_garrison_mi;
    Buffer_set_clip(c, &pane);
    Font_init(&font, "Arial", 0xC, 0);
    int32_t lh = line_h(&font);
    UiRect r = pane;
    Buffer_tile(c, box_def(9)->center, r.left, r.top, r.left, r.top, r.right - r.left, r.bottom - r.top);
    Buffer_box_sprite(c, &r, box_def(3));
    inset(&r, 3);
    spot_kill_type(&g_bw.spots_iface, 6);
    int32_t cw = ZoomNum < 1 ? 0x38 : ZoomNum * 100 / ZoomDenom;
    int32_t top = veh_at(cur_base().x, cur_base().y), per = 0;
    if (top >= 0) {
        int32_t v = top, count = 0;
        while (Vehs[v].prev_veh_id_stack >= 0) v = Vehs[v].prev_veh_id_stack;
        for (; v >= 0; v = Vehs[v].next_veh_id_stack) count++;
        per = (r.right - r.left) / (cw + 5);
        if (per < count) {
            // The main interface's stack arrows at both ends of the pane: 0x3F1 at the bottom
            // pane's left (+0xABC, 0x4105A7) and 0x3F2 (+0xACC); the cells share the width
            // between them.
            stack_arrows_load();
            r.left = main_interface().rect_abc.right + 1;
            r.right = main_interface().arrow_right.left - 1;
            cw = (r.right - r.left) / per - 5;
            g_garrison_arrows = true;
        } else {
            g_garrison_arrows = false;
        }
        v = top;
        while (Vehs[v].prev_veh_id_stack >= 0) v = Vehs[v].prev_veh_id_stack;
        int32_t x = r.left, y = r.top, h = r.bottom - r.top;
        for (int32_t n = 0; v >= 0 && n < per; n++) {
            UiRect cl{x, y, x + cw, y + h};
            Buffer_set_clip(c, &cl);
            veh_draw(c, v, x, y, -7, 0x48, 0);
            veh_draw(c, v, x, y, 0, 0x1800, 0);
            StrBuffer[0] = 0;
            say_stats_short(StrBuffer, Vehs[v].unit_id);
            if ((GameState & STATE_SCENARIO_EDITOR) && (GameState & STATE_DEBUG_MODE)) {
                cat(" ");
                cat("(");
                cat_num(v);
                cat(")");
            }
            Buffer_set_font(c, &font, nullptr, nullptr, nullptr);
            Buffer_set_text_color(c, tc(0x8C6D1C), tc(0x8C6D20), 1, 1);
            int32_t tw = ZoomNum < 1 ? 0x38 : ZoomNum * 100 / ZoomDenom;
            Buffer_write_cent_l_5(c, StrBuffer, x, (y + h - lh) + 6, tw, (int32_t)std::strlen(StrBuffer));
            spot_add(&g_bw.spots_iface, v, 6, x, y, cw + 5, h);
            x += cw + 5;
            v = Vehs[v].next_veh_id_stack;
        }
    }
    UiRect all{0, 0, c->width, c->height};
    Buffer_set_clip(c, &all);
    if (g_garrison_arrows) {
        UiRect mall{0, 0, c->width, c->height};
        Buffer_set_clip(c, &mall);
        stack_arrows_draw(c);
    }
    Buffer_set_clip(c, &old);
    (void)flag;
    ui_invalidate();
}
