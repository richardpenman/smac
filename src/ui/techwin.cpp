// The research choice and the new technology window (techwin.h).
//
// tech_pick (0x5BEB70) runs the research dialog FUN_00486510 on a TechWin on its stack
// (asm-faithful in what it changes and returns):
//   * blind research (GameRules 0x200, no source faction): the viewer is asked for the research
//     priorities (Console_set_ai: TECHRANDOM) on the turns the original asks, then tech_ai picks;
//     a regular pick on turn 0 or of a computer player is tech_ai's;
//   * otherwise the candidates are listed by category (explore, discover, build, conquer): with a
//     source faction the techs it has that we lack and whose prerequisites we partly have; else
//     the available ones (tech_avail), of which a regular pick skips every third (by tech id,
//     tech_ranking and faction) once one is listed, except among the first seven techs while
//     tech_ranking is 0. Each listed tech draws two numbers of the game generator (game_random:
//     display weights the original computes and discards) and its tech_val;
//   * a computer player takes the listed tech with the highest tech_val (the last of equal ones),
//     a human player chooses in the window; -1 when nothing is listed.
// The window (FUN_004863F0 / FUN_00484C60 draw it, FUN_00486170 its keys, FUN_00485FC0 the mouse,
// FUN_004860B0 / FUN_00486110 the clicks): a grid of panels (columns by the number of choices,
// panel size from the screen size), each with the tech's name, its category and level
// (tech_recurse), its picture in the category colour, the TECHSHORTS.txt quote and, from the
// bottom up, what it leads to (techs, facilities, abilities, terraforming, models, chassis,
// weapons, armour, reactors, units) while there is room; with nine or more choices only the
// names. Arrows / keypad move the selection, a letter selects the first tech with that initial,
// Enter / space choose, H / F1 open the datalinks; the mouse selects, a click chooses, a right
// click opens the tech's datalinks page. The window cannot be cancelled.
// Not reconstructed: the panels' pixel-exact layout is the original's arithmetic but the glyphs
// are smacx's (font_text.h); the tutorial popups are shown as the original's (popt).
//
// Console_set_tech (0x515010): TECHCHANGE when research is under way (half the accumulated
// points are lost when the goal changes), tech_pick, synch_research. Console_set_ai (0x514F50):
// the TECHRANDOM check boxes into AI_fight / growth / tech / wealth, synch_ai.
//
// NewTechWin_exec (0x483F90, drawn by FUN_00483950): the 550 x 600 window of a technology the
// viewer discovered or acquired: title (OUR RESEARCHERS HAVE MADE A BREAKTHROUGH! / WE HAVE
// ACQUIRED TECHNOLOGY!), the tech's name in its category colour and picture, the four sections of
// help_tech_info (base facilities, technologies, unit advances, miscellaneous) with their links,
// the BLURBSX quote and OK. A click on a link opens that datalinks page, then the window again.
// The voice clip voices\techN.mp3 plays while the window is up (sound.cpp's Wave objects).
// Not reconstructed: the two picture buttons beside the quote (drawn, inactive), scroll bars of the
// sections.
#include "ui/techwin.h"
#include "ui/sound.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "engine/diplo_act.h"
#include "engine/globals.h"
#include "engine/net.h"
#include "engine/parse.h"
#include "engine/random.h"
#include "engine/rules.h"
#include "engine/strings.h"
#include "engine/tech.h"
#include "engine/ui_bridge.h"
#include "ui/basewin.h"
#include "ui/console.h"
#include "ui/font_text.h"
#include "ui/help.h"
#include "ui/mapwin.h"
#include "ui/popup.h"
#include "ui/screenwin.h"
#include "ui/sprites.h"

namespace {

bool is_human(int32_t f) { return (FactionStatus[0] & (1u << (f & 31))) != 0; }

// The column count of the grid by the number of choices (the table on FUN_00486510's stack).
int32_t grid_columns(int32_t n) {
    static const int8_t kCols[40] = {0, 1, 2, 3, 4, 5, 3, 4, 4, 5, 5, 6, 4, 5, 5, 5, 4, 5, 6, 5,
                                     5, 7, 6, 6, 6, 5, 7, 7, 7, 8, 8, 8, 8, 7, 7, 7, 8, 8, 8, 8};
    return n < 40 ? kCols[n] : 10;
}

std::string tech_name(int32_t t, int32_t f) {
    char buf[256];
    buf[0] = 0;
    say_tech_2(buf, t, 0);
    std::string s = buf;
    if (t == 88) s += " " + std::to_string(Factions[f].tech_count_transcendent + 1);
    return s;
}

struct TechWinState : ScreenWin {
    int32_t faction = 0;  // +0xA20
    int32_t count = 0;  // +0xA2C
    int32_t cols = 1, rows = 1;  // +0xA24, +0xA28
    int32_t pw = 0, ph = 0;  // +0xA14, +0xA18: panel size
    int32_t spacing = 10;  // +0xA1C
    int32_t sel = 0, sel_valid = 0;  // +0xA38, +0xA3C
    int32_t list[89]{};  // +0xA58
    bool chosen = false;
    std::string caption;
    Font fonts[4];  // +0xBBC, +0xBE4, +0xC0C (text), +0xC34
    // FUN_00484C60's cursors.
    int32_t px = 0, pwidth = 0, ptop = 0, pbottom = 0, a54 = 0;

    void redraw() override;
    void on_key(int32_t key) override;
    void on_click(int32_t parent, int32_t index, int32_t type, int32_t x, int32_t y, bool right, bool dbl) override;
    void log_state() override;

    UiRect panel_rect(int32_t i) const;
    int32_t draw_panel(int32_t i, int32_t highlight);  // FUN_004863F0
    int32_t draw_content(int32_t t, const UiRect& r);  // FUN_00484C60
    void bottom_line(const std::string& s);  // FUN_00484B80
    void mouse_move(int32_t x, int32_t y);  // FUN_00485FC0
    void key(int32_t ch, int32_t vk);  // FUN_00486170
};

TechWinState* g_techwin = nullptr;

UiRect TechWinState::panel_rect(int32_t i) const {
    int32_t l = (i % cols) * (pw + 1) + 1, t = (i / cols) * (ph + 1) + 0x1E;
    return {l, t, l + pw, t + ph};
}

void TechWinState::bottom_line(const std::string& s) {
    Buffer* c = &canvas;
    int32_t lh;
    if (!a54) {
        Buffer_set_font(c, &fonts[3], &fonts[3], &fonts[3], nullptr);
        a54 = 1;
        lh = Buffer_text_line_height(c);
    } else {
        Buffer_set_font(c, &fonts[0], &fonts[1], &fonts[2], nullptr);
        lh = Buffer_text_line_height(c);
    }
    if (ptop <= pbottom - lh) {
        int32_t h = Buffer_wrap_height(c, s.c_str(), pwidth - 2);
        pbottom -= h;
        Buffer_wrap_cent_4(c, s.c_str(), px + 1, pbottom, pwidth - 2);
    }
    texts.push_back("panel " + s);
}

int32_t TechWinState::draw_content(int32_t t, const UiRect& r) {
    if (t < 0) return 0;
    Buffer* c = &canvas;
    a54 = 0;
    Buffer_set_text_color(c, 231, 0, 1, 1);
    Buffer_set_text_color2(c, 231, 0, 1, 1);
    pwidth = r.right - r.left;
    px = r.left;
    ptop = r.top + spacing;
    pbottom = r.bottom - spacing / 2;
    int32_t size = count < 9 ? (ScreenWidth == 800 ? 0x12 : 0x14) : 0xC;
    Font* f2 = find_font(size, 2);
    Font* f1 = find_font(size, 1);
    Buffer_set_font(c, f1, f1, f2, nullptr);
    std::string name = tech_name(t, faction);
    texts.push_back("tech " + std::to_string(t) + " " + name);
    std::string shown = name;
    // A single word too wide for the panel is split where it stops fitting (so that it wraps).
    if (Buffer_text_width(c, name.c_str()) > pwidth - 2 && name.find(' ') == std::string::npos) {
        std::string head = name;
        while (!head.empty() && Buffer_text_width(c, head.c_str()) > pwidth - 2) head.pop_back();
        shown = head + " " + name.substr(head.size());
    }
    int32_t cat = tech_category(t);
    int32_t level = tech_recurse(t, 0);
    if (count > 8) {
        Buffer_set_text_color(c, mandate_color(cat), 0, 1, 1);
        int32_t h = Buffer_wrap_height(c, shown.c_str(), pwidth - 4);
        Buffer_wrap_cent_4(c, shown.c_str(), r.left + 2, (r.bottom - r.top - h) / 2 + r.top, pwidth - 4);
        return 1;
    }
    ptop = Buffer_wrap_cent_4(c, shown.c_str(), px + 2, ptop, pwidth - 6);
    Buffer_set_font(c, &fonts[0], &fonts[1], &fonts[2], nullptr);
    Buffer_set_text_color(c, mandate_color(cat), 0, 1, 1);
    std::string cl = std::string(Mandate[cat].name_caps) + " " + std::to_string(level);
    ptop = Buffer_wrap_cent_4(c, cl.c_str(), px + 1, ptop, pwidth - 2);
    texts.push_back("panel " + cl);
    Buffer_set_text_color(c, 231, 0, 1, 1);
    Buffer_set_text_color2(c, 0x5F, 0, 1, 1);
    int32_t ls;
    if (ScreenWidth == 800) ls = pw < 0xF0 ? -0xE : -0xC;
    else if (pw < 0xB4) ls = pw < 0x8D ? -0xE : -0xC;
    else ls = -10;
    if (t < 89) {
        ptop += spacing;
        int32_t scaled;
        if (ZoomNum < 1) {
            BlitDestScale = ls + 16;
            BlitSourceScale = 16;
            scaled = (ls + 16) * 0xD7 / 16;
        } else {
            scaled = ZoomNum * 0xD7 / ZoomDenom;
        }
        Sprite* sp = tech_picture(t);
        if (sp && sp->width) Sprite_draw_mono_2(sp, c, 9, (pwidth - scaled) / 2 + r.left, ptop, MandateColors[cat * 2]);
        ptop += scaled + spacing;
        BlitDestScale = 1;
        BlitSourceScale = 1;
    }
    HelpText quote;
    if (help_load_text(&quote, "TECHSHORTS", ("TECH" + std::to_string(t)).c_str(), true))
        ptop = Buffer_write_strings(c, quote.lines.data(), (int32_t)quote.lines.size(), px + 4, ptop, pwidth - 8, 0);
    a54 = 1;
    for (int32_t i = 0; i < 89; i++)
        if (Tech[i].preq_tech1 == t || Tech[i].preq_tech2 == t) bottom_line(std::string("[") + Tech[i].name + "]");
    for (int32_t i = 1; i < 70; i++)
        if (Facility[i].free_tech == t) bottom_line(std::string(Facility[i].name) + " " + label_get(311));
    for (int32_t i = 0; i < 10; i++)
        if (Citizen[i].preq_tech == t) bottom_line(Citizen[i].singular_name);
    for (int32_t i = 0; i < 29; i++)
        if (Ability[i].preq_tech == t) bottom_line(Ability[i].name);
    for (int32_t i = 0; i < 20; i++)
        for (int32_t k = 0; k < 2; k++) {
            int32_t preq = k ? Terraform[i].preq_tech_sea : Terraform[i].preq_tech;
            if (t == preq && (k != 1 || Terraform[i].preq_tech != Terraform[i].preq_tech_sea))
                bottom_line(k ? Order[4 + i].order_sea : Order[4 + i].order);
        }
    if (t == Rules.tech_preq_allow_3_energy_sq) bottom_line(std::string("{") + label_get(280) + "}");
    if (t == Rules.tech_preq_allow_3_minerals_sq) bottom_line(std::string("{") + label_get(279) + "}");
    if (t == Rules.tech_preq_allow_3_nutrients_sq) bottom_line(std::string("{") + label_get(278) + "}");
    for (int32_t i = 1; i < 134; i++) {
        if (Facility[i].preq_tech != t) continue;
        if (i < 70) bottom_line(std::string("{") + Facility[i].name + " (" + Facility[i].effect + ")}");
        else bottom_line(Facility[i].name);
    }
    for (int32_t field = 0; field < 4; field++)
        for (int32_t m = 0; m < 4; m++)
            if (SocialField[field].soc_preq_tech[m] == t)
                bottom_line(std::string("{") + SocialField[field].soc_name[m] + "}");
    for (int32_t i = 0; i < 9; i++)
        if (Chassis[i].preq_tech == t)
            bottom_line(std::string("{") + Chassis[i].offsv1_name + " (" + TriadName[Chassis[i].triad] + " " +
                        std::to_string(Chassis[i].speed) + ")}");
    for (int32_t i = 0; i < 14; i++) {
        if (Armor[i].preq_tech != t) continue;
        std::string s = std::string("{") + Armor[i].name + " (";
        // (a psi armour's line is not closed, as the original's)
        if (Armor[i].defense_value < 0) s += label_get(196);
        else s += std::to_string(Armor[i].defense_value) + ")}";
        bottom_line(s);
    }
    for (int32_t i = 0; i < 26; i++) {
        if (Weapon[i].preq_tech != t) continue;
        std::string s = std::string("{") + Weapon[i].name + " (";
        int8_t off = Weapon[i].offense_value;
        if (off == 0) s += std::string(label_get(128)).substr(0, 1) + " " + std::to_string(Weapon[i].cost);
        else if (off < 0) s += label_get(196);
        else s += std::to_string(off);
        bottom_line(s + ")}");
    }
    for (int32_t i = 0; i < 4; i++)
        if (Reactor[i].preq_tech == t)
            bottom_line(std::string(Reactor[i].name) + " (" + std::string(label_get(79)).substr(0, 1) + ": " +
                        std::to_string(i + 1) + ")");
    for (int32_t i = 0; i < 64; i++)
        if ((Units[i].unit_flags & 1) && Units[i].preq_tech == t)
            bottom_line(std::string("{") + label_get(197) + ": " + Units[i].name + "}");
    return 1;
}

int32_t TechWinState::draw_panel(int32_t i, int32_t highlight) {
    if (i >= count || i < 0) return 0;
    Buffer* c = &canvas;
    UiRect r = panel_rect(i);
    Buffer_set_clip(c, &r);
    r.right -= 3;
    r.bottom -= 3;
    if (r.left < 4) r.left = 3;
    r.right = std::min(r.right, c->width - 3);
    if (r.top < 0x1E) r.top = 0x1D;
    r.bottom = std::min(r.bottom, c->height - 3);
    Buffer_box_sprite(c, &r, box_def(highlight ? 2 : 0));
    int32_t ret = draw_content(list[i], r);
    UiRect all{0, 0, c->width, c->height};
    Buffer_set_clip(c, &all);
    return ret;
}

void TechWinState::redraw() {
    Buffer* c = &canvas;
    UiRect all{0, 0, c->width, c->height};
    Buffer_set_clip(c, &all);
    Buffer_fill_3(c, &all, 9);
    UiRect r = all;
    Buffer_box_sprite(c, &r, box_def(9));
    r = {all.left + 3, all.top + 3, all.right - 3, all.top + 3 + 0x17};
    Buffer_box_sprite(c, &r, box_def(6));
    r = {r.left + 3, r.top + 3, r.right - 3, r.bottom - 3};
    std::string cap = sw_upper(caption);
    Buffer_set_text_color(c, 0x9F, -1, 1, 1);
    Font* f = find_font(0x12, 1);
    Buffer_set_font(c, f, nullptr, nullptr, nullptr);
    Buffer_write_cent_l_7(c, cap.c_str(), &r, (int32_t)cap.size());
    texts.push_back(cap);
    for (int32_t i = 0; i < count; i++) draw_panel(i, sel == i);
}

void TechWinState::mouse_move(int32_t x, int32_t y) {
    y -= 0x1D;
    if (x < 0 || y < 0 || cols * (pw + 1) <= x || rows * (ph + 1) <= y) {
        sel_valid = 0;
        sel = -1;
        return;
    }
    int32_t idx = (y / (ph + 1)) * cols + x / (pw + 1);
    if (idx != sel || !sel_valid) {
        sel_valid = idx < count && idx >= 0 && list[idx] >= 0;
        sel = idx;
    }
}

void TechWinState::key(int32_t ch, int32_t vk) {
    int32_t col = sel % cols, row = sel / cols;
    bool confirm = false;
    switch (vk) {
    case 0x28:
    case 0x62: row = (row + 1) % rows; break;
    case 8:
    case 0x25:
    case 0x64:
        if (--col < 0) col = cols - 1;
        break;
    case 9:
    case 0x27:
    case 0x66: col = (col + 1) % cols; break;
    case 0x26:
    case 0x68:
        if (--row < 0) row = rows - 1;
        break;
    case 0xD:
    case 0x20:
    case 0x48:
    case 0x70:
    case 0x1000D: confirm = true; break;
    default: break;
    }
    if (!confirm) {
        int32_t next = cols * row + col;
        if (sel < 0) sel = 0;
        if (ch) {
            for (int32_t i = 0; i < count; i++) {
                std::string n = tech_name(list[i], faction);
                if (!n.empty() && (uint8_t)n[0] == (uint8_t)std::toupper(ch)) {
                    next = i;
                    break;
                }
            }
        }
        if (next != sel) {
            sel_valid = next < count && next >= 0;
            sel = next;
        }
        return;
    }
    if (sel < 0 || sel >= count) {
        sel = 0;
        sel_valid = 1;
    }
    if (list[sel] < 0) return;
    if (vk != 0x70 && vk != 0x48) {
        chosen = true;
        sw_close(this);
        return;
    }
    help_any();
}

void TechWinState::on_key(int32_t k) {
    int32_t vk = k & 0xFFFF;
    int32_t ch = 0;
    if (!(k & 0x60000) && vk >= 'A' && vk <= 'Z' && vk != 'H') ch = vk;
    if (!(k & 0x60000) && vk >= '0' && vk <= '9') ch = vk;
    // ctrl+Enter is 0x1000D in the original's key codes
    if (vk == 0xD && (k & 0x20000)) vk = 0x1000D;
    key(ch, vk);
}

void TechWinState::on_click(int32_t parent, int32_t, int32_t, int32_t x, int32_t y, bool right, bool) {
    if (parent != 0) return;
    mouse_move(x, y);  // the pointer moved there first
    if (!sel_valid) return;
    if (right) {
        Datalink_exec(&DatalinkWin, 0xE, list[sel]);
        return;
    }
    chosen = true;
    sw_close(this);
}

void TechWinState::log_state() {
    std::string l;
    for (int32_t i = 0; i < count; i++) l += " " + std::to_string(list[i]);
    std::fprintf(stderr, "techwin faction %d count %d cols %d rows %d panel %d %d sel %d list%s\n", faction, count,
                 cols, rows, pw, ph, sel, l.c_str());
}

// The research choice window of FUN_00486510 for the listed techs; returns the chosen tech.
int32_t techwin_run(int32_t f, const int32_t* list, int32_t n, const char* label) {
    TechWinState w;
    w.name = "techwin";
    w.faction = f;
    w.count = n;
    std::copy(list, list + n, w.list);
    BasePop* pop = popup_new();
    Popup_start(pop, ScriptFileName, label, -1, nullptr, 2);
    const char* cap = BasePop_caption(pop);
    w.caption = cap ? cap : "";
    popup_delete(pop);
    int32_t size_a, size_b;
    if (n < 9) {
        size_a = ScreenWidth == 800 ? 0xC : 0xE;
        size_b = ScreenWidth == 800 ? 0xE : 0x12;
    } else {
        size_a = 10;
        size_b = 0xC;
    }
    for (int32_t i = 0; i < 4; i++) Font_ctor(&w.fonts[i]);
    Font_init(&w.fonts[0], "Arial", size_a, 0);
    Font_init(&w.fonts[1], "Arial", size_a, 1);
    Font_init(&w.fonts[2], "Arial", size_a, 2);
    Font_init(&w.fonts[3], "Arial", size_b, 1);
    w.cols = grid_columns(n);
    w.rows = n / w.cols + (n % w.cols != 0);
    int32_t pw = n < 9 ? ScreenWidth / w.cols - 0x14 : (ScreenWidth - 0x14) / w.cols;
    pw = std::min(pw, ScreenWidth / 3);
    int32_t ph = std::max(pw * 3 / 2, ScreenHeight / 2);
    if (ScreenHeight < ph * w.rows + 0x1D) ph = n < 9 ? ScreenHeight / w.rows - 0x28 : (ScreenHeight - 0x45) / w.rows;
    w.pw = pw;
    w.ph = ph;
    int32_t W = (pw + 1) * w.cols + 1, H = (ph + 1) * w.rows + 0x1E;
    sw_init(&w, (ScreenWidth - W) / 2, (ScreenHeight - H) / 2, W, H, false);
    w.sel = 0;
    w.sel_valid = 0;
    g_techwin = &w;
    sw_run(&w);
    g_techwin = nullptr;
    sw_clear_buttons(&w);
    int32_t s = w.sel >= 0 && w.sel < n ? w.sel : 0;
    return w.list[s];
}

// ---- NewTechWin (0x811E40) ----------------------------------------------------------------------

struct NewTechWinState : ScreenWin {
    int32_t tech = 0, from = 0;  // +0xB24, +0xB28
    UiRect outer{}, ok{}, title{}, picture{}, blurb{}, b1{}, b2{};
    UiRect header[4]{}, box[4]{};  // the sections: headers and their text boxes
    HelpText sections[4];
    HelpText quote;
    int32_t link = -1;  // DAT_00686960: the link clicked
    BaseButton* ok_button = nullptr;

    void redraw() override;
    void on_button(int32_t id) override {
        if (id == -1) sw_close(this);
    }
    void on_key(int32_t key) override {
        int32_t vk = key & 0xFFFF;
        if (vk == 0xD || vk == 0x1B || vk == 0x20) sw_close(this);
    }
    void on_click(int32_t parent, int32_t, int32_t, int32_t x, int32_t y, bool right, bool) override {
        if (parent || right) return;
        const std::string* target = buffer_link_at(&canvas, x, y);
        if (!target) return;
        link = std::atoi(target->c_str());
        sw_close(this);
    }
    void log_state() override { std::fprintf(stderr, "newtechwin tech %d from %d\n", tech, from); }
};

void NewTechWinState::redraw() {
    Buffer* c = &canvas;
    UiRect all{0, 0, c->width, c->height};
    Buffer_set_clip(c, &all);
    buffer_clear_links(c);
    Buffer_fill_3(c, &all, 9);
    UiRect r = all;
    Buffer_box_sprite(c, &r, box_def_large());
    r = outer;
    Buffer_box_sprite(c, &r, box_def(3));
    r = {ok.left - 3, ok.top - 3, ok.right + 3, ok.bottom + 3};
    Buffer_box_sprite(c, &r, box_def(3));
    Font* f16 = find_font(0x10, 1);
    Buffer_set_font(c, f16, nullptr, nullptr, nullptr);
    Buffer_set_text_color(c, 0xE1, -1, 1, 1);
    r = title;
    Buffer_box_sprite(c, &r, box_def(0));
    std::string t = sw_upper(label_get(from < 1 ? 541 : 542));
    Buffer_write_cent_l_7(c, t.c_str(), &title, (int32_t)t.size());
    texts.push_back(t);
    static const int32_t kHeaders[4] = {68, 76, 956, 957};
    for (int32_t i = 0; i < 4; i++) {
        r = header[i];
        Buffer_box_sprite(c, &r, box_def(6));
        std::string h = sw_upper(label_get(kHeaders[i]));
        Buffer_write_cent_l_7(c, h.c_str(), &header[i], (int32_t)h.size());
        texts.push_back(h);
    }
    for (int32_t i = 0; i < 4; i++) {
        r = box[i];
        Buffer_box_sprite(c, &r, box_def(0));
    }
    r = picture;
    Buffer_box_sprite(c, &r, box_def(0));
    Buffer_set_font(c, find_font(0x28, 0), nullptr, nullptr, nullptr);
    int32_t cat = tech_category(tech);
    Buffer_set_text_color(c, mandate_color(cat), -1, 1, 1);
    int32_t lh = Buffer_text_line_height(c);
    UiRect nr{picture.left + 3, picture.top + 3, picture.right - 3, picture.top + 3 + lh};
    std::string name = tech_name(tech, CurrentPlayerFaction);
    Buffer_write_cent_l_7(c, name.c_str(), &nr, (int32_t)name.size());
    texts.push_back(name);
    int32_t top = picture.top + 3 + lh, bottom = picture.bottom - 3;
    Sprite* sp = tech_picture(tech);
    if (sp && sp->width) {
        BlitDestScale = 0xA0;
        BlitSourceScale = 0xD7;
        Sprite_draw_mono_2(sp, c, 9, (picture.right - picture.left - 0xA0) / 2 + picture.left,
                           (bottom - top - 0xA0) / 2 + top, mandate_color(cat));
        BlitDestScale = 1;
        BlitSourceScale = 1;
    }
    r = blurb;
    Buffer_box_sprite(c, &r, box_def(0));
    // The section texts (StringBoxes 4 pixels inside the boxes).
    Font* s3 = find_font(0xE, MultiplayerActive ? 2 : 3);
    Font* s2 = find_font(0xE, 2);
    Font* s1 = find_font(0xE, 1);
    Font* s0 = find_font(0xE, 0);
    for (int32_t i = 0; i < 4; i++) {
        UiRect b{box[i].left + 4, box[i].top + 4, box[i].right - 4, box[i].bottom - 4};
        Buffer_set_clip(c, &b);
        Buffer_set_font(c, s0, s1, s2, s3);
        Buffer_set_text_color(c, 0xE1, -1, 1, 1);
        Buffer_set_text_color_hyper(c, 0xE1, -1, 1, 1);
        Buffer_write_strings(c, sections[i].lines.data(), (int32_t)sections[i].lines.size(), b.left, b.top,
                             b.right - b.left, 0);
        for (const TextLine& l : sections[i].lines) texts.push_back("section " + std::to_string(i) + " " + l.text);
    }
    UiRect b{blurb.left + 3, blurb.top + 3, blurb.right - 3, blurb.bottom - 3};
    Buffer_set_clip(c, &b);
    Buffer_set_font(c, find_font(0xC, 0), find_font(0xC, 1), find_font(0xC, 2), nullptr);
    Buffer_set_text_color(c, 0x98, -1, 1, 1);
    Buffer_write_strings(c, quote.lines.data(), (int32_t)quote.lines.size(), b.left, b.top, b.right - b.left, 0);
    Buffer_set_clip(c, &all);
    // The two picture buttons beside the quote (ButtonGroup, the first set; not active here).
    r = b1;
    Buffer_box_sprite(c, &r, box_def(2));
    r = b2;
    Buffer_box_sprite(c, &r, box_def(0));
}

}  // namespace

int32_t THISCALL NewTechWin_exec(uint8_t*, int32_t tech_id, int32_t from) {
    NewTechWinState w;
    w.name = "newtech";
    w.tech = tech_id;
    w.from = from;
    const int32_t W = 0x226, H = 600;
    const int32_t x1 = 15, x2 = W - 15, bottom = H - 0x2D;
    w.outer = {12, 12, W - 12, H - 0x2A};
    w.ok = {15, H - 0x23, W - 15, H - 15};
    w.title = {x1, 15, x2, 15 + 0x17};
    w.picture = {x1, w.title.bottom + 3, x2, w.title.bottom + 0xDC};
    int32_t mid = ((x2 - x1) + 3) / 2 - 3 + x1;
    int32_t ha = w.title.bottom + 0xDF, ba = ha + 0x17, hb = ba + 0x4F, bb = hb + 0x17;
    w.header[0] = {x1, ha, mid, ha + 0x14};
    w.header[1] = {mid + 3, ha, x2, ha + 0x14};
    w.box[0] = {x1, ba, mid, ba + 0x4C};
    w.box[1] = {mid + 3, ba, x2, ba + 0x4C};
    w.header[2] = {x1, hb, mid, hb + 0x14};
    w.header[3] = {mid + 3, hb, x2, hb + 0x14};
    w.box[2] = {x1, bb, mid, bb + 0x4C};
    w.box[3] = {mid + 3, bb, x2, bb + 0x4C};
    w.b1 = {w.outer.right - 0x41, w.outer.bottom - 0x28, w.outer.right - 0x25, w.outer.bottom - 6};
    w.b2 = {w.outer.right - 0x22, w.outer.bottom - 0x28, w.outer.right - 6, w.outer.bottom - 6};
    w.blurb = {x1, bb + 0x4F, w.b1.left - 4, w.outer.bottom - 3};
    (void)bottom;
    for (int32_t i = 0; i < 4; i++) {
        HelpText& h = w.sections[i];
        help_tech_info([&](const char* line) { h.add(line); }, tech_id, CurrentPlayerFaction, i);
        h.finish();
    }
    help_load_text(&w.quote, "BLURBSX", ("TECH" + std::to_string(tech_id)).c_str(), true);
    sw_init(&w, (ScreenWidth - W) / 2, (ScreenHeight - H) / 2, W, H, false);
    w.ok_button = sw_button(&w, label_get(101), -1, w.ok.left, w.ok.top, w.ok.right - w.ok.left, w.ok.bottom - w.ok.top,
                            0, find_font(0xE, 1));
    // The voice clip (voices\techN.mp3 on FacilityVoice, at the voices group's volume) when the
    // viewer is human and voiceovers are on (GameMorePreferences 0x4000).
    std::string voice = "voices\\tech" + std::to_string(tech_id) + ".mp3";
    if ((GameMorePreferences & MPREF_AV_VOLUME_VOICE_TOGGLE) &&
        (FactionStatus[0] & (1 << (CurrentPlayerFaction & 31)))) {
        Wave_unload(&FacilityVoice);
        Wave_load(&FacilityVoice, voice.c_str());
        Wave_set_volume(&FacilityVoice, (uint32_t)sound_group_volume(0));
        Wave_play_2(&FacilityVoice);
    }
    for (;;) {
        w.link = -1;
        sw_run(&w);
        if (w.link == -1 || ControlTurnA) break;
        Datalink_exec(&DatalinkWin, w.link / 10000, w.link % 10000);
    }
    // Voiceovers stop when the window closes (GameMorePreferences 0x20000).
    if ((GameMorePreferences & MPREF_AV_VOLUME_VOICE_TOGGLE) &&
        (GameMorePreferences & MPREF_AV_VOICEOVER_STOP_CLOSE_POPUP))
        Sound_fade_2(&FacilityVoice, 2000);
    sw_clear_buttons(&w);
    return 0;
}

namespace {}  // namespace

Sprite* tech_picture(int32_t t) {
    if (t < 0 || t >= 89) return nullptr;
    Sprite* sp = ArtSprite(0x759E30 + (uint32_t)t * 0x2C);
    if (sp->full_width) return sp;
    char name[64];
    std::snprintf(name, sizeof(name), "techs/tech0%s%d", t < 10 ? "0" : "", t);
    Buffer art;
    Buffer_ctor(&art);
    if (!Buffer_load_pcx(&art, name, nullptr, 10, 0xEC)) {
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

int32_t GAMEAPI tech_pick(int32_t f, int32_t is_free, int32_t from, const char* label) {
    Faction& fa = Factions[f];
    bool blind = (GameRules & RULES_BLIND_RESEARCH) && from <= 0;
    if (blind && f == CurrentPlayerFaction && !MultiplayerActive &&
        ((fa.tech_ranking >> 1) % 5 == 1 || CurrentTurn < 12) && !SkipTechScreenA) {
        if ((GamePreferences & PREF_BSC_TUTORIAL_MSGS) && tut_check2(1)) popt("TUTOR", "TECHNOLOGY", -1, 0, 0);
        Console_set_ai(&MainConsole, f);
    }
    if (!is_free && (!CurrentTurn || !is_human(f) || blind) && from < 0) return tech_ai(f);
    if ((GamePreferences & PREF_BSC_TUTORIAL_MSGS) && !SkipTechScreenA && !blind && f == CurrentPlayerFaction &&
        tut_check2(1))
        popt("TUTOR", "TECHNOLOGY", -1, 0, 0);
    if (MultiplayerActive && ControlTurnC) FX_play(&Sounds, 0x21);
    if (!label) label = "TECHNOLOGY";
    int32_t list[89];
    int32_t listed = 0, best = -1, best_value = -1;
    for (int32_t category = 0; category < 4; category++) {
        for (int32_t t = 0; t < 89; t++) {
            if (from >= 0) {
                if (has_tech(t, f) || !has_tech(t, from)) continue;
                if (!has_tech(Tech[t].preq_tech1, f) && !has_tech(Tech[t].preq_tech2, f)) continue;
            } else {
                if (!tech_avail(t, f)) continue;
                if (listed && !is_free && (t + (f - fa.tech_ranking)) % 3 == 0 && (t > 6 || fa.tech_ranking)) continue;
            }
            if (tech_category(t) != category) continue;
            // The display weights: drawn and discarded.
            int32_t v = tech_tech(t) + tech_mil(t) * 2;
            game_random(0, v * 2 / 3 + 1);
            v = tech_tech(t) + (tech_colonize(t) + tech_infra(t)) * 2;
            game_random(0, v * 2 / 3 + 1);
            list[listed++] = t;
            int32_t value = tech_val(t, f, 0);
            if (value >= best_value) {
                best_value = value;
                best = t;
            }
        }
    }
    if (!listed) return -1;
    if (!is_human(f)) return best;
    return techwin_run(f, list, listed, label);
}

void THISCALL Console_set_tech(uint8_t*, int32_t f) {
    Faction& fa = Factions[f];
    if (fa.tech_accumulated && fa.tech_research_id >= 0) {
        parse_say(0, Tech[fa.tech_research_id].name, -1, -1);
        parse_num(0, fa.tech_accumulated);
        if (!X_pop("TECHCHANGE", 0)) return;
    }
    int32_t t = tech_pick(f, 0, -1, nullptr);
    if (t >= 0) {
        if (fa.tech_research_id >= 0 && t != fa.tech_research_id) fa.tech_accumulated /= 2;
        fa.tech_research_id = t;
    }
    synch_research(f);
}

void THISCALL Console_set_ai(uint8_t*, int32_t f) {
    Faction& fa = Factions[f];
    DialogChoices = fa.AI_fight != 0;
    if (fa.AI_growth) DialogChoices |= 2;
    if (fa.AI_tech) DialogChoices |= 4;
    if (fa.AI_wealth) DialogChoices |= 8;
    if (X_pop_6("TECHRANDOM", 1, 0) >= 0) {
        int32_t c = (int8_t)DialogChoices;
        fa.AI_fight = c & 1;
        fa.AI_growth = (c & 2) >> 1;
        fa.AI_tech = (c & 4) >> 2;
        fa.AI_wealth = (c & 8) >> 3;
        synch_ai(f);
    }
}

void techwin_log_state() {
    int32_t f = CurrentPlayerFaction;
    const Faction& fa = Factions[f];
    std::fprintf(stderr, "techstate f%d research %d accumulated %d cost %d ai %d %d %d %d rules %#x\n", f,
                 fa.tech_research_id, fa.tech_accumulated, fa.tech_cost, fa.AI_fight, fa.AI_growth, fa.AI_tech,
                 fa.AI_wealth, (uint32_t)GameRules & 0xFFFF);
}
