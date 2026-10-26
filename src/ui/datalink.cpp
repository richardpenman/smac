// The datalinks (help browser): Datalink_exec (0x429180) on DatalinkWin and the methods it runs
// (ui module 0x428FC0..0x432840).
//
// Reconstructed (asm-faithful in the categories, ids, lists and navigation):
//   * the 16 categories (Datalink_set_cat and set_cat_*: buttons of the main interface, labels
//     712..727): concepts and advanced concepts (conceptsx.txt TITLES / ADVTITLES), all categories
//     (the union, sorted), unit types (the basic units, but unit 4), chassis, reactors, weapons,
//     armour, abilities (the enabled ones: preq_tech > -2), terraforming (land and sea orders),
//     facilities, secret projects, social models (with their categories), social effects (but
//     TALENT), technologies and faction profiles; entry ids are category * 10000 + entry
//     (Datalink_construct_id / parse_id); lists are sorted by name (StringList_sort) except the
//     social models;
//   * the entry (Datalink_draw_entry and draw_*): its title and the section texts of helpx.txt the
//     original uses for it (HELPUNIT / UNITDESC, HELPCHASSIS / CHASSISDESC, HELPREACTOR,
//     HELPWEAPON(2) / WEAPONDESC, HELPDEFENSE(2) / ARMORDESC, HELPABIL / ABILDESC,
//     HELPTERRALAND / HELPTERRASEA, HELPFAC(FREE) / HELPFAC<n> + BLURBSX FAC<n>, HELPPROJ /
//     HELPPROJ<n> + BLURBSX PROJECT<n>, HELPSOC<f>[<m>], HELPEFFECT<n>, conceptsx CONCEPT<n> /
//     ADVCONCEPT<n>; a technology: the labs report's tree (Datalink_draw_tech: ReportWin_draw_labs
//     in the entry's rectangle, labs.cpp; its buttons and links move through the technologies)
//     and BLURBSX TECH<n>; a faction: its file's
//     DATALINKS1 / DATALINKS2 / BLURB), with "$LINK<text=id>" links (Datalink_on_link_clicked: a
//     click opens that entry; ids above 1000 within a category are ignored), the history of the
//     last 20 entries (BACK, Datalink_on_button_clicked 0x10; with one entry BACK closes), EXIT
//     (0x11), Enter / Esc close, other keys go to the list (arrows, Home / End, PgUp / PgDn).
//   * the entry points: help_any, help_topic, help_tech, help_project (help.cpp), F1 on the main
//     view (techs), the DATALINKS button of the reports (ReportWin_on_button_clicked: per report
//     kind), right clicks of the research window, links of the new technology window.
// Drawing (approximated): the window (832 x 525 above the main interface, its rectangles as
// Datalink_init sets them: category title and list on the left, the entry's title, picture and
// two text boxes on the right, BACK / EXIT below), the category buttons in the main interface's
// left and right panes; pictures: facilities and projects (facs\, projs\), technologies (mono in
// the category colour), units (veh_draw); the quotes of technologies, facilities and projects
// (BLURBSX) in the main interface's centre pane. Not reconstructed: scroll bars.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "engine/design.h"
#include "engine/diplo_act.h"
#include "engine/globals.h"
#include "engine/parse.h"
#include "engine/proto.h"
#include "engine/rules.h"
#include "engine/strings.h"
#include "engine/tech.h"
#include "engine/text.h"
#include "engine/ui_bridge.h"
#include "engine/veh.h"
#include "ui/basewin.h"
#include "ui/console.h"
#include "ui/font_text.h"
#include "ui/help.h"
#include "ui/labs.h"
#include "ui/popup.h"
#include "ui/screenwin.h"
#include "ui/scroll.h"
#include "ui/techwin.h"
#include "ui/vehdraw.h"

namespace {

struct Item {
    std::string text;
    int32_t id;
};

struct DatalinkState : ScreenWin {
    int32_t cat = 0;          // +0x29DC
    int32_t index = 0;        // +0x29E0
    int32_t history[20]{};    // +0x29E4
    int32_t hist_n = 0;       // +0x2A34
    int32_t restoring = 0;    // +0x2A38
    std::vector<Item> items;  // the ListBox (+0xF628)
    int32_t sel = 0, top = 0;
    std::string title;
    HelpText text1, text2;    // the entry's texts (the stats beside the picture, the description)
    HelpText blurb;           // the quote in the main interface's centre pane (+0x130B4)
    int32_t picture = 0;      // 0 none, 1 facility / project, 2 technology, 3 unit
    int32_t picture_id = 0;
    UiRect r_cat{0x1E, 7, 0xE6, 0x1B}, r_list{0x1E, 0x1D, 0xE6, 0x1CD}, r_title{0xEF, 7, 0x302, 0x22},
        r_text1{0xEF, 0x24, 0x302, 0xF9}, r_text2{0xEF, 0xFB, 0x302, 0x1CD}, r_pic{0xF8, 0x2C, 0x177, 0xAB};

    void redraw() override;
    void on_button(int32_t id) override;
    void on_key(int32_t key) override;
    void on_click(int32_t parent, int32_t index, int32_t type, int32_t x, int32_t y, bool right,
                  bool dbl) override;
    void log_state() override;
    void on_move(int32_t x, int32_t y) override {
        if (cat == 14) labs_mouse(this, &canvas, x, y);
    }

    void set_cat(int32_t c, int32_t force);  // 0x42A2C0
    void fill(int32_t c);                   // the set_cat_* functions
    void select_id(int32_t id);             // ListBox_set_selected_id
    void entry();                           // Datalink_draw_entry's choice and history
    void build_entry(int32_t c, int32_t i); // the draw_* functions' texts
    void link(int32_t id);                  // Datalink_on_link_clicked
    int32_t visible() const { return (r_list.bottom - r_list.top - 6) / 16; }
};

void add_items(std::vector<Item>& v, int32_t c) {
    auto item = [&](const std::string& t, int32_t id) {
        std::string s = t;
        if (GameState & 0x1000) s += "|" + std::to_string(id);
        v.push_back({s, id});
    };
    switch (c) {
    case 0:
    case 1:
        if (!text_open("CONCEPTSX", c ? "ADVTITLES" : "TITLES")) {
            for (int32_t i = 0;; i++) {
                char* t = text_get();
                if (!t || !*t) break;
                item(t, i);
            }
            text_close();
        }
        break;
    case 3:
        for (int32_t i = 0; i < 64; i++)
            if ((Units[i].unit_flags & 1) && i != 4) item(Units[i].name, 30000 + i);
        break;
    case 4:
        for (int32_t i = 0; i < 9; i++)
            if (Chassis[i].preq_tech > -2) item(Chassis[i].offsv1_name, 40000 + i);
        break;
    case 5:
        for (int32_t i = 0; i < 4; i++)
            if (Reactor[i].preq_tech > -2) item(Reactor[i].name, 50001 + i);
        break;
    case 6:
        for (int32_t i = 0; i < 26; i++)
            if (Weapon[i].preq_tech > -2) item(Weapon[i].name, 60000 + i);
        break;
    case 7:
        for (int32_t i = 0; i < 14; i++)
            if (Armor[i].preq_tech > -2) item(Armor[i].name, 70000 + i);
        break;
    case 8:
        for (int32_t i = 0; i < 29; i++)
            if (Ability[i].preq_tech > -2) item(Ability[i].name, 80000 + i);
        break;
    case 9:
        for (int32_t i = 0; i < 20; i++) {
            if (Terraform[i].preq_tech > -2) item(Order[4 + i].order, 90000 + i);
            if (i != 16 && i != 17 && Terraform[i].preq_tech_sea > -2) item(Order[4 + i].order_sea, 90020 + i);
        }
        break;
    case 10:
        for (int32_t i = 1; i < 0x46; i++)
            if (Facility[i].preq_tech > -2) item(Facility[i].name, 100000 + i);
        break;
    case 11:
        for (int32_t i = 0x46; i < 0x86; i++) {
            if (Facility[i].preq_tech < -1) continue;
            std::string n = Facility[i].name;
            // "The X" is listed as "X" (English)
            if (GameLanguage == 0 && n.compare(0, 4, "The ") == 0) n = n.substr(4);
            item(n, 110000 + i);
        }
        break;
    case 12:
        for (int32_t f = 0; f < 4; f++) {
            v.push_back({SocialField[f].field_name, 120000 + f * 5});
            for (int32_t m = 0; m < 4; m++) item(SocialField[f].soc_name[m], 120001 + f * 5 + m);
        }
        break;
    case 13:
        for (int32_t i = 0; i < 11; i++)
            if (i != 3) item(SocialParam[i].set2, 130000 + i);
        break;
    case 14:
        for (int32_t i = 0; i < 89; i++)
            if (Tech[i].preq_tech1 > -2 && Tech[i].preq_tech2 > -2) item(Tech[i].name, 140000 + i);
        break;
    case 15:
        for (int32_t f = 1; f < 8; f++) {
            GenderDefault = MFactions[f].noun_gender;
            PluralDefault = MFactions[f].is_noun_plural;
            item(MFactions[f].noun_faction, 150000 + f);
        }
        break;
    }
}

void DatalinkState::fill(int32_t c) {
    items.clear();
    if (c == 2) {
        for (int32_t k : {3, 4, 5, 6, 7, 8, 9, 0, 10, 11, 12, 13, 14, 15}) add_items(items, k);
    } else {
        add_items(items, c);
    }
    if (c != 12)
        std::stable_sort(items.begin(), items.end(), [](const Item& a, const Item& b) { return a.text < b.text; });
}

void DatalinkState::set_cat(int32_t c, int32_t force) {
    if (!force && cat == c && !items.empty()) return;
    cat = c;
    fill(c);
    sel = 0;
    top = 0;
}

void DatalinkState::select_id(int32_t id) {
    for (size_t i = 0; i < items.size(); i++)
        if (items[i].id == id) {
            sel = (int32_t)i;
            break;
        }
    if (sel < top) top = sel;
    if (sel >= top + visible()) top = sel - visible() + 1;
}

void DatalinkState::link(int32_t v) {
    int32_t i = v % 10000;
    if (i > 1000) return;
    set_cat(v / 10000, 0);
    index = i;
    select_id(cat * 10000 + i);
    entry();
}

// Datalink_draw_entry's bookkeeping: the selected entry and the history.
void DatalinkState::entry() {
    if (items.empty()) return;
    int32_t id = items[sel].id;
    int32_t c = id / 10000;
    index = id % 10000;
    if (!restoring && (hist_n == 0 || history[hist_n - 1] != id)) {
        if (hist_n == 20) {
            for (int32_t i = 0; i < 19; i++) history[i] = history[i + 1];
            hist_n--;
        }
        history[hist_n++] = id;
    }
    build_entry(c, index);
}

void load_section(HelpText* out, const char* file, const std::string& label) {
    HelpText t;
    if (!help_load_text(&t, file, label.c_str(), true)) return;
    for (const std::string& s : t.store) out->add(s.c_str());
}

// The lines of a HELPX template section after its #xs / #caption lines.
void load_template(HelpText* out, const char* label) {
    if (text_open("HELPX", label)) return;
    char* line = text_get();
    while (line && *line == '#') line = text_get();
    for (; line && *line && *line != '#'; line = text_get()) {
        char buf[2048];
        buf[0] = 0;
        parse_string(line, buf);
        out->add(buf);
    }
    text_close();
}

void DatalinkState::build_entry(int32_t c, int32_t i) {
    text1.clear();
    text2.clear();
    blurb.clear();
    picture = 0;
    title.clear();
    auto preq = [&](int32_t slot_str, int32_t slot_num, int32_t t) {
        parse_says(slot_str, t >= 0 && t < 89 ? Tech[t].name : label_get(25), -1, -1);
        parse_num(slot_num, t + 140000);
    };
    switch (c) {
    case 0:
    case 1:
        for (const Item& it : items)
            if (it.id == c * 10000 + i) title = it.text;
        if (title.empty()) {
            std::vector<Item> v;
            add_items(v, c);
            for (const Item& it : v)
                if (it.id == i) title = it.text;
        }
        load_section(&text2, "CONCEPTSX", (c ? "ADVCONCEPT" : "CONCEPT") + std::to_string(i));
        break;
    case 3: {
        const UNIT& u = Units[i];
        title = u.name;
        parse_says(0, u.name, -1, -1);
        int32_t veh = veh_fake(i, CurrentPlayerFaction);
        (void)veh;
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%d", Weapon[u.weapon_id].offense_value);
        parse_says(2, Weapon[u.weapon_id].offense_value < 0 ? label_get(196) : buf, -1, -1);
        std::snprintf(buf, sizeof(buf), "%d", Armor[u.armor_id].defense_value);
        parse_says(3, Armor[u.armor_id].defense_value < 0 ? label_get(196) : buf, -1, -1);
        parse_num(1, Chassis[u.chassis_id].speed);
        parse_num(0, u.cost);
        preq(1, 2, u.preq_tech);
        load_template(&text1, "HELPUNIT");
        load_section(&text2, "HELPX", "UNITDESC" + std::to_string(i));
        picture = 3;
        picture_id = i;
        break;
    }
    case 4:
        title = Chassis[i].offsv1_name;
        parse_says(0, title.c_str(), -1, -1);
        parse_says(1, TriadName[Chassis[i].triad], -1, -1);
        parse_num(0, Chassis[i].speed);
        parse_says(2, Chassis[i].preq_tech >= 0 && Chassis[i].preq_tech < 89 ? Tech[Chassis[i].preq_tech].name : label_get(25), -1, -1);
        parse_num(3, Chassis[i].preq_tech + 140000);
        load_template(&text1, "HELPCHASSIS");
        load_section(&text2, "HELPX", "CHASSISDESC" + std::to_string(i));
        break;
    case 5: {
        int32_t r = i - 1;
        title = Reactor[r].name;
        parse_says(0, title.c_str(), -1, -1);
        parse_num(0, i);
        preq(1, 1, Reactor[r].preq_tech);
        load_template(&text1, "HELPREACTOR");
        break;
    }
    case 6: {
        const CWeapon& w = Weapon[i];
        title = w.name;
        parse_says(0, w.name, -1, -1);
        parse_says(1, w.offense_value < 0 ? label_get(196) : w.mode < 3 ? OffenseMode[w.mode].name : label_get(128),
                  -1, -1);
        parse_num(0, w.offense_value);
        parse_num(1, w.cost);
        preq(2, 2, w.preq_tech);
        load_template(&text1, w.offense_value == (int32_t)w.cost ? "HELPWEAPON2" : "HELPWEAPON");
        load_section(&text2, "HELPX", "WEAPONDESC" + std::to_string(i));
        break;
    }
    case 7: {
        const CArmor& a = Armor[i];
        title = a.name;
        parse_says(0, a.name, -1, -1);
        parse_num(0, a.defense_value);
        parse_says(1, label_get(196), -1, -1);
        preq(2, 2, a.preq_tech);
        load_template(&text1, a.defense_value < 0 ? "HELPDEFENSE2" : "HELPDEFENSE");
        load_section(&text2, "HELPX", "ARMORDESC" + std::to_string(i));
        break;
    }
    case 8: {
        const CAbility& a = Ability[i];
        title = a.name;
        parse_says(0, a.name, -1, -1);
        parse_says(2, a.description, -1, -1);
        parse_says(3, std::to_string(a.cost).c_str(), -1, -1);
        parse_says(1, a.preq_tech >= 0 && a.preq_tech < 89 ? Tech[a.preq_tech].name : label_get(25), -1, -1);
        parse_num(0, a.preq_tech + 140000);
        load_template(&text1, "HELPABIL");
        load_section(&text2, "HELPX", "ABILDESC" + std::to_string(i));
        break;
    }
    case 9: {
        bool sea = i >= 20;
        int32_t t = sea ? i - 20 : i;
        title = sea ? Order[4 + t].order_sea : Order[4 + t].order;
        parse_says(0, title.c_str(), -1, -1);
        preq(1, 1, sea ? Terraform[t].preq_tech_sea : Terraform[t].preq_tech);
        load_template(&text1, "HELPTERRAFORM");
        load_section(&text2, "HELPX", (sea ? "HELPTERRASEA" : "HELPTERRALAND") + std::to_string(t));
        break;
    }
    case 10:
    case 11: {
        const CFacility& f = Facility[i];
        title = f.name;
        parse_says(0, f.name, -1, -1);
        parse_num(0, f.cost * cost_factor(CurrentPlayerFaction, 1, -1));
        parse_num(1, f.maint);
        preq(1, 2, f.preq_tech);
        bool free = c == 10 && f.free_tech >= 0 && f.free_tech < 89;
        if (free) {
            parse_says(2, Tech[f.free_tech].name, -1, -1);
            parse_num(3, f.free_tech + 140000);
        }
        load_template(&text1, c == 11 ? "HELPPROJ" : free ? "HELPFACFREE" : "HELPFAC");
        load_section(&text2, "HELPX", (c == 11 ? "HELPPROJ" : "HELPFAC") + std::to_string(i));
        load_section(&blurb, "BLURBSX", c == 11 ? "PROJECT" + std::to_string(i - 0x46) : "FAC" + std::to_string(i));
        picture = 1;
        picture_id = i;
        break;
    }
    case 12: {
        int32_t f = i / 5, m = i % 5 - 1;
        title = m < 0 ? SocialField[f].field_name : SocialField[f].soc_name[m];
        load_section(&text2, "HELPX", "HELPSOC" + std::to_string(f) + (m < 0 ? "" : std::to_string(m)));
        break;
    }
    case 13:
        title = SocialParam[i].set2;
        load_section(&text2, "HELPX", "HELPEFFECT" + std::to_string(i));
        break;
    case 14:
        // Datalink_draw_tech: the title, the labs report's tree (ReportWin_draw_labs in the
        // entry's rectangle, drawn by redraw) and the quote.
        title = Tech[i].name;
        load_section(&blurb, "BLURBSX", "TECH" + std::to_string(i));
        break;
    case 15: {
        title = MFactions[i].noun_faction;
        std::string file = MFactions[i].filename;
        load_section(&text1, file.c_str(), "DATALINKS1");
        load_section(&text1, file.c_str(), "DATALINKS2");
        load_section(&text2, file.c_str(), "BLURB");
        break;
    }
    }
    text1.finish();
    text2.finish();
    blurb.finish();
}

void DatalinkState::redraw() {
    Buffer* c = &canvas;
    UiRect all{0, 0, c->width, c->height};
    Buffer_set_clip(c, &all);
    buffer_clear_links(c);
    Buffer_fill_3(c, &all, 9);
    UiRect r{0x17, 0, 0x309, c->height};
    Buffer_box_sprite(c, &r, box_def(9));
    UiRect u{std::min(r_text1.left, r_title.left) - 3, r_title.top - 3, r_text2.right + 3, r_text2.bottom + 3};
    Buffer_box_sprite(c, &u, box_def(3));
    r = r_cat;
    Buffer_box_sprite(c, &r, box_def(6));
    r = {r_cat.left - 3, r_cat.top - 3, r_list.right + 3, r_list.bottom + 3};
    Buffer_box_sprite(c, &r, box_def(3));
    r = r_cat;
    Buffer_box_sprite(c, &r, box_def(6));
    r = r_list;
    Buffer_box_sprite(c, &r, box_def(6));
    // The category's title and list.
    sw_font(c, find_font(0xE, 1), 0xE0);
    sw_text_cent(this, c, label_get(712 + cat), {r_cat.left + 3, r_cat.top + 3, r_cat.right - 3, r_cat.bottom - 3});
    UiRect lr{r_list.left + 3, r_list.top + 3, r_list.right - 3, r_list.bottom - 3};
    // The list's scroll bar (ListBox's Scroll, scroll_normal) at its right.
    if ((int32_t)items.size() > visible()) {
        ScrollBar sb = scroll_for_list(&lr, false, top, (int32_t)items.size(), visible());
        scroll_draw(c, sb);
    }
    Buffer_set_clip(c, &lr);
    for (int32_t k = 0; k < visible() && top + k < (int32_t)items.size(); k++) {
        const Item& it = items[top + k];
        int32_t y = lr.top + k * 16;
        if (top + k == sel) {
            UiRect hl{lr.left, y, lr.right, y + 16};
            Buffer_box_sprite(c, &hl, box_def(2));
        }
        std::string t = it.text.substr(0, it.text.find('|'));
        sw_font(c, find_font(0xC, top + k == sel ? 1 : 0), top + k == sel ? 0xE1 : 0x98);
        Buffer_write_l_3(c, t.c_str(), lr.left + 4, y + 1, (int32_t)t.size());
    }
    Buffer_set_clip(c, &all);
    // The entry.
    r = r_title;
    Buffer_box_sprite(c, &r, box_def(6));
    UiRect t1 = r_text1, t2 = r_text2;
    bool tree = cat == 14 && !items.empty();
    if (tree) {
        UiRect tu{r_text1.left, r_text1.top, r_text2.right, r_text2.bottom};  // +0x10334
        Buffer_box_sprite(c, &tu, box_def(0));
    } else {
        Buffer_box_sprite(c, &t1, box_def(0));
        Buffer_box_sprite(c, &t2, box_def(0));
    }
    sw_font(c, find_font(0x12, 1), 0xE0);
    texts.push_back("title " + sw_upper(title));
    std::string up = sw_upper(title);
    Buffer_write_cent_l_7(c, up.c_str(), &r_title, (int32_t)up.size());
    if (tree) {
        labs_tree_tech() = index;
        ReportWinKind = 2;
        UiRect tu{r_text1.left, r_text1.top, r_text2.right, r_text2.bottom};
        labs_draw(this, c, true, CurrentPlayerFaction, tu, &texts);
    } else {
        labs_clear(this);
    }
    int32_t tx = r_text1.left + 6;
    if (picture) {
        UiRect p = r_pic;
        Buffer_box_sprite(c, &p, box_def(0));
        Buffer_set_clip(c, &p);
        if (picture == 1) {
            Sprite* sp = basewin_facility_picture(picture_id);
            if (sp && sp->width) {
                BlitDestScale = p.right - p.left - 6;
                BlitSourceScale = 0xD7;
                pfncSpriteStretchCopyToCanvas(sp, c, 9, p.left + 3, p.top + 3);
                BlitDestScale = BlitSourceScale = 1;
            }
        } else if (picture == 2) {
            Sprite* sp = tech_picture(picture_id);
            if (sp && sp->width) {
                BlitDestScale = p.right - p.left - 6;
                BlitSourceScale = 0xD7;
                Sprite_draw_mono_2(sp, c, 9, p.left + 3, p.top + 3, mandate_color(tech_category(picture_id)));
                BlitDestScale = BlitSourceScale = 1;
            }
        } else {
            int32_t veh = veh_fake(picture_id, CurrentPlayerFaction);
            veh_draw(c, veh, p.left + 10, p.top + 20, 0xE, 0x1048, 0);
        }
        Buffer_set_clip(c, &all);
        tx = r_pic.right + 6;
    }
    Font* f0 = find_font(0xE, 0);
    Font* f1 = find_font(0xE, 1);
    Font* f2 = find_font(0xE, 2);
    Font* f3 = find_font(0xE, 3);
    auto write = [&](const HelpText& h, const UiRect& box, int32_t x, int32_t idx) {
        UiRect b{box.left + 6, box.top + 6, box.right - 6, box.bottom - 6};
        Buffer_set_clip(c, &b);
        Buffer_set_font(c, f0, f1, f2, f3);
        Buffer_set_text_color(c, 0x98, -1, 1, 1);
        Buffer_set_text_color2(c, 0x96, -1, 1, 1);
        Buffer_set_text_color_hyper(c, 0x96, -1, 1, 1);
        Buffer_write_strings(c, h.lines.data(), (int32_t)h.lines.size(), x, b.top, b.right - x, 0);
        for (const TextLine& l : h.lines) texts.push_back("text" + std::to_string(idx) + " " + l.text);
    };
    write(text1, r_text1, tx, 1);
    write(text2, r_text2, r_text2.left + 6, 2);
    Buffer_set_clip(c, &all);
    sw_restore_iface(this);
    // The quote in the main interface's centre pane.
    UiRect pc = mi_rect(kMiPaneCenter);
    UiRect b{pc.left + 3, pc.top + 3, pc.right - 3, pc.bottom - 6};
    Buffer_set_clip(&mi, &b);
    Buffer_set_font(&mi, find_font(0xC, 0), find_font(0xC, 1), find_font(0xC, 2), nullptr);
    Buffer_set_text_color(&mi, 0x98, -1, 1, 1);
    Buffer_set_text_color2(&mi, 0x96, -1, 1, 1);
    Buffer_write_strings(&mi, blurb.lines.data(), (int32_t)blurb.lines.size(), b.left, b.top, b.right - b.left, 0);
    for (const TextLine& l : blurb.lines) texts.push_back(std::string("blurb ") + l.text);
    UiRect mall{0, 0, mi.width, mi.height};
    Buffer_set_clip(&mi, &mall);
}

void DatalinkState::on_button(int32_t id) {
    if (cat == 14 && labs_button(id)) {  // ReportWin_on_button_clicked: Datalink_set_index
        link(14 * 10000 + labs_tree_tech());
        return;
    }
    if (id == 0x10) {  // BACK
        if (hist_n < 2) {
            sw_close(this);
            return;
        }
        hist_n--;
        int32_t v = history[hist_n - 1];
        set_cat(v / 10000, 0);
        index = v % 10000;
        select_id(cat * 10000 + index);
        entry();
        return;
    }
    if (id == 0x11) {  // EXIT
        sw_close(this);
        return;
    }
    if (id >= 0x100 && id < 0x110) {  // a category button (Datalink_on_iface_group_clicked)
        set_cat(id - 0x100, 0);
        restoring = 1;
        sel = 0;
        top = 0;
        restoring = 0;
        entry();
    }
}

void DatalinkState::on_key(int32_t key) {
    int32_t vk = key & 0xFFFF;
    if (vk == 0xD || vk == 0x1B) {
        sw_close(this);
        return;
    }
    int32_t n = (int32_t)items.size();
    if (!n) return;
    int32_t old = sel;
    switch (vk) {
    case 0x26: case 0x68: sel = std::max(0, sel - 1); break;
    case 0x28: case 0x62: sel = std::min(n - 1, sel + 1); break;
    case 0x21: case 0x69: sel = std::max(0, sel - visible()); break;
    case 0x22: case 0x63: sel = std::min(n - 1, sel + visible()); break;
    case 0x24: case 0x67: sel = 0; break;
    case 0x23: case 0x61: sel = n - 1; break;
    default:
        // A letter: the next item starting with it.
        if (!(key & 0x60000) && vk >= 'A' && vk <= 'Z')
            for (int32_t k = 1; k <= n; k++) {
                const Item& it = items[(sel + k) % n];
                if (!it.text.empty() && std::toupper((uint8_t)it.text[0]) == vk) {
                    sel = (sel + k) % n;
                    break;
                }
            }
        break;
    }
    if (sel < top) top = sel;
    if (sel >= top + visible()) top = sel - visible() + 1;
    if (sel != old) entry();
}

void DatalinkState::on_click(int32_t parent, int32_t, int32_t, int32_t x, int32_t y, bool right, bool) {
    if (parent || right) return;
    if (const std::string* t = buffer_link_at(&canvas, x, y)) {
        link(std::atoi(t->c_str()));
        return;
    }
    UiRect lr{r_list.left + 3, r_list.top + 3, r_list.right - 3, r_list.bottom - 3};
    ScrollBar sb = scroll_for_list(&lr, false, top, (int32_t)items.size(), visible());
    if ((int32_t)items.size() <= visible()) lr.right = r_list.right - 3;
    else if (int32_t d = scroll_click(sb, x, y, visible())) {
        top = std::max(0, std::min(top + d, sb.max));
        return;
    }
    if (x >= r_list.left + 3 && x < lr.right && y >= r_list.top + 3 && y < r_list.bottom - 3) {
        int32_t k = top + (y - r_list.top - 3) / 16;
        if (k < (int32_t)items.size() && k != sel) {
            sel = k;
            entry();
        }
    }
}

void DatalinkState::log_state() {
    std::string h;
    for (int32_t i = 0; i < hist_n; i++) h += " " + std::to_string(history[i]);
    std::fprintf(stderr, "datalink cat %d index %d sel %d items %d history%s\n", cat, index, sel, (int32_t)items.size(),
                 h.c_str());
}

DatalinkState* g_dl = nullptr;

}  // namespace

void THISCALL Datalink_exec(uint8_t*, int32_t cat, int32_t id) {
    if (g_dl) {  // (opened again from inside: a link of the open window)
        g_dl->link(cat * 10000 + id);
        return;
    }
    int32_t saved_kind = ReportWinKind, saved_tech = labs_tree_tech();
    DatalinkState w;
    w.name = "datalink";
    w.slide = true;  // Datalink_on_status: slide_show / slide_hide
    int32_t extra = ScreenWidth == 800 ? 0 : 0x8A;
    int32_t H = (ScreenWidth == 800 ? 600 : 0x2E2) - 0xD5;
    int32_t X = (ScreenWidth - 800) / 2, Y = (ScreenHeight - 0xD5) - H;
    w.r_list.bottom = 0x143 + extra;
    w.r_text1.bottom = 0xB4 + extra / 2;
    w.r_text2 = {0xEF, 0xB6 + extra / 2, 0x302, extra / 2 * 2 + 0x143};
    sw_init(&w, X, Y, 0x340, H, true);
    // The category buttons: 0..7 in the left pane (top to bottom), 8..15 in the right pane (bottom to
    // top), 0x18 pixels apart from the pane's bottom.
    UiRect q = mi_rect(kMiPaneQueue), p = mi_rect(kMiPaneRight);
    Font* bf = find_font(0xA, 1);
    for (int32_t k = 0; k < 16; k++) {
        const UiRect& pane = k < 8 ? q : p;
        int32_t slot = k < 8 ? 7 - k : k - 8;
        int32_t bottom = pane.bottom - 3 - slot * 0x18;
        sw_button(&w, label_get(712 + k), 0x100 + k, pane.left + 4, bottom - 0x16, pane.right - 4 - (pane.left + 4),
                  0x16, 1, bf);
    }
    sw_button(&w, sw_upper(label_get(664)).c_str(), 0x10, 0x98, extra + 0x14E, 0x18E - 0x98, 0x14, 0, find_font(0xE, 1));
    sw_button(&w, sw_upper(label_get(39)).c_str(), 0x11, 0x192, extra + 0x14E, 0x18E - 0x98, 0x14, 0, find_font(0xE, 1));
    w.set_cat(cat, 1);
    w.index = id;
    w.select_id(w.cat * 10000 + id);
    w.entry();
    g_dl = &w;
    sw_run(&w);
    g_dl = nullptr;
    sw_clear_buttons(&w);
    ReportWinKind = saved_kind;
    labs_tree_tech() = saved_tech;
}
