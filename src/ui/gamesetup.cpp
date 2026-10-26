// The game setup of the opening screens: config_game (0x589D30) with the faction picker (PickWin,
// ui 0x4887A0..0x48ADF0), its text box (thumb_routine 0x5898A0) and PLANETFALL (planetfall
// 0x589180); see topmenu.h.
//
// config_game is reconstructed from the disassembly with the original's control flow (labels
// L_<address> where the original jumps back): difficulty (DIFFICULTY, in the menu column), the rules
// (USERULES: standard, current, or RULES' check boxes), the factions (PickWin in Crossfire; FACTIONX
// for scenarios and the original game), the leader's name (NAME / NAME2 / NAME3) or the faction's
// names (CUSTOMNAMES, also CHANGE NAME in the picker), then the random rivals (JENN282 slots: one
// alien faction brings the other; with none, half the games put both aliens in two random slots;
// the other random slots draw from FACTIONS / NEWFACTIONS / CUSTOMFACTIONS), the human player, the
// world (world_build), the factions (setup_game) and PLANETFALL. Multiplayer, hotseat and
// play-by-email paths (time controls, HOTSEATADD, the PBEM tamper seeds) are kept where they only
// set state; their dialogs are not shown (no network play).
//
// PickWin (790 x 300 at 5, 5, the game palette): the title bar, the three picture buttons of
// pickfac.pcx (the original factions, the new factions, all random), PLAY SELECTED / PLAY RANDOM
// FACTION (a radio group), the seven slots (list rows of 19 pixels with their arrow buttons: a menu
// of every faction and RANDOM), the line from the radio button to the selected row, INFO / QUOTE,
// CHANGE NAME, OK and CANCEL; below it the selected faction's portrait and BLURB (or its datalinks
// entry). Keys: Esc cancels, Enter is OK, the arrows move the selection.
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "engine/crt.h"
#include "engine/game.h"
#include "engine/globals.h"
#include "engine/log.h"
#include "engine/net_misc.h"
#include "engine/parse.h"
#include "engine/prefs.h"
#include "engine/random.h"
#include "engine/rules.h"
#include "engine/setup.h"
#include "engine/strings.h"
#include "engine/text.h"
#include "engine/ui_bridge.h"
#include "engine/world.h"
#include "platform/ui_platform.h"
#include "platform/win32.h"
#include "ui/basewin_internal.h"
#include "ui/console.h"
#include "ui/screenwin.h"
#include "ui/sound.h"
#include "ui/topmenu.h"
#include "ui/win.h"

namespace {

int32_t mem_i32(uint32_t addr) { return *reinterpret_cast<int32_t*>(mem::host(addr)); }
bool is_random(int32_t f) { return !crt_strcmp(MFactions[f].filename, "JENN282"); }

// The faction picker's text box (thumb_routine's popup, shown under the picker).
BasePop* g_thumb = nullptr;
int32_t g_show_datalinks = 0;  // dword_945818: the text box shows the datalinks entry, not the quote

// ---- PickWin ----

struct PickWin : ScreenWin {
    int32_t mode = 0;        // +0x5DA0 (config_game's mode)
    int32_t result = -1;     // +0x5DA8: the chosen faction (-1: cancelled)
    int32_t action = 0;      // +0x5DA4: 0 OK / 0x105 random / 0x106 CHANGE NAME / 0x107 INFO
    int32_t group = 0x104;   // the radio group: 0x104 play selected, 0x105 play random
    int32_t sel = 1;         // the list's selected id (1..7)
    int32_t saved_sel = 1;   // +0x5DB4 (the selection while PLAY RANDOM is on)
    int32_t customs = 0;     // +0x5DB0
    std::vector<std::string> files, nouns;  // +0x5DB8 / +0x5FF8: every faction of the lists
    Buffer pickfac;
    Sprite pics[9], arrows[3], pointer;
    UiRect list{}, title{}, inner{};
    Font* font = nullptr;

    UiRect row(int32_t i) const { return UiRect{list.left, list.top + i * 0x13, list.right, list.top + (i + 1) * 0x13}; }

    void load() {
        // PickWin_init: the factions of the three lists, their nouns, the art of pickfac.pcx.
        auto read_list = [&](const char* section, int32_t n) {
            if (text_open(AlphaxFileName, section)) return;
            for (int32_t i = 0; i < n; i++) {
                text_get();
                files.push_back(text_item());
            }
        };
        read_list("FACTIONS", 7);
        read_list("NEWFACTIONS", 7);
        customs = 0;
        if (!text_open(AlphaxFileName, "CUSTOMFACTIONS")) {
            text_get();
            while (*text_item()) {
                customs++;
                text_get();
            }
        }
        customs = std::max(0, std::min(10, customs));
        if (customs) read_list("CUSTOMFACTIONS", customs);
        for (const std::string& f : files) {
            std::string noun;
            if (!text_open(f.c_str(), f.c_str())) {
                text_get();
                text_item();
                text_item();
                noun = text_item();
                if (noun.size() > 0x17) noun.resize(0x17);
            }
            nouns.push_back(noun);
        }
        Buffer_ctor(&pickfac);
        Buffer_load_pcx(&pickfac, "pickfac.pcx", nullptr, 10, 0xEC);
        int32_t key = mem_i32(0x696D14);
        for (int32_t r = 0; r < 3; r++)
            for (int32_t c = 0; c < 3; c++)
                pfncSpriteFromCanvasRectTrans(&pics[c * 3 + r], &pickfac, key, 1 + c * 0x103, 1 + r * 0x5B, 0x102,
                                              0x5A, 0);
        for (int32_t i = 0; i < 3; i++)
            pfncSpriteFromCanvasRectTrans(&arrows[i], &pickfac, key, 1, 0x139 + i * 0x11, 0x10, 0x10, 0);
        pfncSpriteFromCanvasRectTrans(&pointer, &pickfac, key, 1, 0x139 + 3 * 0x11, 7, 0xD, 0);
    }

    void layout() {
        // The main interface's font (MainInterface +0x2166C: Arial 14 bold).
        static Font f14;
        static bool f14_ok = false;
        if (!f14_ok) {
            Font_ctor(&f14);
            Font_init(&f14, "Arial", 0xE, 1);
            f14_ok = true;
        }
        font = &f14;
        inner = UiRect{4, 4, 0x316 - 4, 300 - 4};
        title = UiRect{4, 4, 0x316 - 4, 4 + 0x1E};
        int32_t bw = ((inner.right - inner.left) - 6) / 3;  // 258
        int32_t y = title.top + 0x22;
        auto add = [&](const std::string& text, int32_t id, int32_t x, int32_t yy, int32_t w, int32_t h) {
            BaseButton* b = sw_button(this, text.c_str(), id, x, yy, w, h, 0, font);
            return b;
        };
        // The picture buttons (0x100..0x102): the sprites of pickfac.pcx per state.
        for (int32_t k = 0; k < 3; k++) {
            int32_t x = inner.left + 1 + k * (bw + 2);
            BaseButton* b = add("", 0x100 + k, x, y, 0x102, 0x5A);
            b->has_name = false;
            for (int32_t s = 0; s < 3; s++) b->sprite[s] = &pics[k * 3 + s];
        }
        int32_t pic_bottom = y + 0x5A;
        list = UiRect{inner.left + 0x15B + 3 - 0x12, pic_bottom + 3 + 3,
                      inner.left + 0x15B + (inner.right - inner.left) - 0x15E - 3 - 0x12, pic_bottom + 0x8B - 3};
        for (int32_t i = 0; i < 7; i++) {
            BaseButton* b = add("", 0x3039 + i, list.right + 2, list.top + i * 0x13, 0x10, 0x10);
            b->has_name = false;
            for (int32_t s = 0; s < 3; s++) b->sprite[s] = &arrows[s];
        }
        int32_t by = list.bottom + 7, w4 = (bw * 3) / 4 - 1;
        int32_t x = inner.left + 1;
        add(sw_label(g_show_datalinks ? 0x117C : 0xABC), 0x107, x, by, w4, 0x16);
        add(sw_label(0x1178), 0x106, x + w4 + 2, by, w4, 0x16);
        add(sw_upper(sw_label(0x194)), 0x108, x + 2 * (w4 + 2), by, w4, 0x16);
        add(sw_upper(sw_label(0x198)), 0x103, x + 3 * (w4 + 2), by, w4, 0x16);
        add(sw_label(0x1184), 0x104, inner.left + 2, list.top - 2, 0xD8, 0x16);
        add(sw_label(0x1180), 0x105, inner.left + 2, list.top - 2 + 0x19, 0xD8, 0x16);
        spot_init(&spots[0], 16);
        for (int32_t i = 0; i < 7; i++) spot_add(&spots[0], i + 1, 0, list.left, list.top + i * 0x13, list.right - list.left, 0x13);
    }

    std::string slot_text(int32_t f) {
        // FACTION's list: "<formal name>|(<description>)", or RANDOM.
        if (is_random(f)) return "RANDOM";
        return std::string(MFactions[f].formal_name_faction) + "|(" + MFactions[f].desc_name_faction + ")";
    }

    void redraw() override {  // PickWin_on_redraw (0x489660)
        UiRect all{0, 0, canvas.width, canvas.height};
        Buffer_set_clip(&canvas, &all);
        Buffer_fill_3(&canvas, &all, 9);
        Buffer_box_sprite(&canvas, &all, box_def(9));
        UiRect t = title;
        Buffer_box_sprite(&canvas, &t, box_def(3));
        t = UiRect{t.left + 3, t.top + 3, t.right - 3, t.bottom - 3};
        Buffer_box_sprite(&canvas, &t, box_def(6));
        sw_font(&canvas, font, mem_i32(0x8C6C6C), mem_i32(0x8C6C70));
        sw_text_cent(this, &canvas, sw_label(0x1168), t);
        UiRect lf{list.left - 3, list.top - 3, list.right + 0x15, list.bottom + 3};
        Buffer_box_sprite(&canvas, &lf, box_def(3));
        // The rows (PickWin_on_dialog_item_back_draw: item boxes 27..29 and their text colours).
        for (int32_t i = 0; i < 7; i++) {
            int32_t f = i + 1;
            if (mode == 2 && !(FactionStatus[1] & (1 << f))) continue;
            int32_t state = group == 0x105 ? 0 : (f == sel ? 2 : 0);
            UiRect r = row(i);
            Buffer_box_sprite(&canvas, &r, box_def(27 + state));
            int32_t c = mem_i32(0x8C6C9C - state * 8), sh = mem_i32(0x8C6CA0 - state * 8);
            sw_font(&canvas, font, c, sh);
            std::string text = slot_text(f);
            size_t bar = text.find('|');
            std::string left = text.substr(0, bar);
            sw_text(this, &canvas, left, r.left + 2, r.top + 3);
            if (bar != std::string::npos) {
                std::string right = text.substr(bar + 1);
                int32_t tw = Buffer_text_width(&canvas, right.c_str());
                sw_text(this, &canvas, right, r.right - 2 - tw, r.top + 3);
            }
        }
        // The line from the radio button to the selected row (or to every row: PLAY RANDOM).
        BaseButton* sel_btn = nullptr;
        for (BaseButton* b : buttons) {
            b->set_state = (b->id == group) ? 1 : 0;
            if (b->id == 0x104) sel_btn = b;
        }
        int32_t y8 = sel_btn ? sel_btn->r.top + 10 : 0;
        int32_t mid = sel_btn ? (sel_btn->r.right + list.left - 10) / 2 : 0;
        int32_t lx = list.left - 10;
        if (group == 0x105) {
            BaseButton* rnd = sel_btn;
            for (BaseButton* b : buttons)
                if (b->id == 0x105) rnd = b;
            int32_t cx = (sel_btn->r.left + sel_btn->r.right) / 2;
            Buffer_vline_2(&canvas, cx, rnd->r.bottom - 2, sel_btn->r.top + 0x41, 0xE0);
            for (int32_t i = 0; i < 7; i++) {
                int32_t yy = y8 + i * 0x13;
                Buffer_hline_2(&canvas, mid + 2, lx, yy, 0xE0);
                sprite_copy1(&pointer, &canvas, pointer.key, lx, yy - 6, 1, 1);
            }
            Buffer_vline_2(&canvas, mid, y8, y8 + 6 * 0x13, 0xE0);
        } else if (sel_btn && sel >= 1 && sel <= 7) {
            int32_t yy = (sel - 1) * 0x13 + y8;
            Buffer_hline_2(&canvas, sel_btn->r.right, mid - 2, y8, 0xE0);
            if (sel > 1) Buffer_vline_2(&canvas, mid, y8 + 2, yy - 2, 0xE0);
            Buffer_hline_2(&canvas, mid + 2, lx, yy, 0xE0);
            sprite_copy1(&pointer, &canvas, pointer.key, lx, yy - 6, 1, 1);
        }
    }

    void select(int32_t f) {
        if (f < 1 || f > 7 || f == sel) return;
        sel = f;
        thumb_routine(sel);  // the list's selection callback
    }
    void finish(int32_t r, int32_t act) {
        result = r;
        action = act;
        sw_close(this);
    }
    void slot_menu(int32_t slot) {
        // PopMenu of the slot's arrow: every faction, then RANDOM.
        BasePop* m = popup_new();
        BasePop_start(m, nullptr, "SMACXPICKSLOT", -1, nullptr, 0x42, nullptr);
        int32_t n = (int32_t)files.size();
        for (int32_t i = 0; i < n; i++) BasePop_item(m, nouns[i].c_str(), i);
        BasePop_item(m, label_get(0x1188 / 4), n);
        int32_t r = BasePop_exec_3(m, 0, 0);
        popup_delete(m);
        if (r < 0) return;
        MFaction& mf = MFactions[slot];
        if (r == n) {
            std::snprintf(mf.filename, sizeof(mf.filename), "%s", "JENN282");
            std::snprintf(mf.search_key, sizeof(mf.search_key), "%s", "JENN282");
        } else {
            std::snprintf(mf.filename, sizeof(mf.filename), "%s", files[r].c_str());
            std::snprintf(mf.search_key, sizeof(mf.search_key), "%s", files[r].c_str());
            read_faction2(slot);
        }
        thumb_routine(sel);
    }
    void on_button(int32_t id) override {  // PickWin_on_button_clicked (0x489BA0)
        if (id >= 0x100 && id <= 0x102) {
            if (id != 0x102) {
                if (text_open(AlphaxFileName, id == 0x100 ? "FACTIONS" : "NEWFACTIONS")) return;
                for (int32_t f = 1; f < 8; f++) {
                    text_get();
                    crt_strcpy(MFactions[f].filename, text_item());
                    crt_strcpy(MFactions[f].search_key, text_item());
                }
            }
            for (int32_t f = 1; f < 8; f++) {
                if (id == 0x102) {
                    crt_strcpy(MFactions[f].filename, "JENN282");
                    crt_strcpy(MFactions[f].search_key, "JENN282");
                } else {
                    read_faction2(f);
                }
            }
            thumb_routine(sel);
            return;
        }
        switch (id) {
        case 0x103: finish(-1, 0); return;  // CANCEL
        case 0x104:
        case 0x105:
            // PickWin_on_group_clicked: PLAY RANDOM keeps the selection aside.
            if (id == 0x105 && group != 0x105) saved_sel = sel;
            if (id == 0x104 && group == 0x105) sel = saved_sel;
            group = id;
            return;
        case 0x106:
        case 0x107: finish(sel, id); return;  // CHANGE NAME, INFO / QUOTE
        case 0x108: finish(group == 0x105 ? saved_sel : sel, group == 0x105 ? 0x105 : 0); return;  // OK
        default:
            if (id >= 0x3039 && id < 0x3039 + 7) slot_menu(id - 0x3038);
            return;
        }
    }
    void on_key(int32_t key) override {  // PickWin_on_key_click (0x48A7C0) and the list's keys
        switch (key & 0xFFFF) {
        case VKEY_ESCAPE: on_button(0x103); break;
        case VKEY_RETURN: on_button(0x108); break;
        case VKEY_UP: if (group == 0x104) select(sel > 1 ? sel - 1 : sel); break;
        case VKEY_DOWN: if (group == 0x104) select(sel < 7 ? sel + 1 : sel); break;
        case VKEY_HOME: if (group == 0x104) select(1); break;
        case VKEY_END: if (group == 0x104) select(7); break;
        default: break;
        }
    }
    void on_click(int32_t parent, int32_t index, int32_t, int32_t, int32_t, bool right, bool dbl) override {
        if (parent != 0 || right || index < 1 || group != 0x104) return;
        select(index);
        if (dbl) finish(index, 0);  // list_was_dblclicked
    }
    void log_state() override {
        std::fprintf(stderr, "screenstate pickwin selected=%d group=%x slots", sel, group);
        for (int32_t f = 1; f < 8; f++) std::fprintf(stderr, " %s", MFactions[f].filename);
        std::fprintf(stderr, "\n");
    }
};

PickWin* g_pick = nullptr;

// Closes the picker and its text box (Popup_close, GraphicWin_close).
void pick_close() {
    if (g_thumb) BasePop_hide(g_thumb);
    if (!g_pick) return;
    sw_hide(g_pick);
    sw_clear_buttons(g_pick);
    delete g_pick;
    g_pick = nullptr;
}

// The leader's name and gender, the faction's names (CUSTOMNAMES, config_game 0x58AA82). Returns
// false when cancelled.
bool custom_names(int32_t f) {
    MFaction& m = MFactions[f];
    for (;;) {
        BasePop* pop = popup_new();
        std::string gender = label_get(m.is_leader_female ? 0xB8C / 4 : 0xB88 / 4);
        Popup_start(pop, ScriptFileName, "CUSTOMNAMES", -1, nullptr, 0x44);
        BasePop_edit(pop, label_get(0x2B8 / 4), m.name_leader, 0x17);
        BasePop_edit(pop, label_get(0x2BC / 4), m.formal_name_faction, 0x27);
        BasePop_edit(pop, label_get(0x2C0 / 4), m.noun_faction, 0x17);
        BasePop_edit(pop, label_get(0x2C4 / 4), m.adj_name_faction, 0x17);
        BasePop_edit(pop, label_get(0x78C / 4), m.title_leader, 0x17);
        BasePop_edit(pop, label_get(0x790 / 4), m.adj_leader, 0x17);
        BasePop_edit(pop, label_get(0x794 / 4), gender.c_str(), 0x17);
        int32_t r = BasePop_exec_3(pop, 0, 0);
        int32_t button = BasePop_button_pressed(pop);
        popup_delete(pop);
        if (r < 0) return false;
        if (button) {  // Restore Defaults
            read_faction(&m, 0);
            load_faction_art(f);
            continue;
        }
        crt_strcpy(m.name_leader, ParseStrBuffer[0]);
        crt_strcpy(m.formal_name_faction, ParseStrBuffer[1]);
        crt_strcpy(m.noun_faction, ParseStrBuffer[2]);
        crt_strcpy(m.adj_name_faction, ParseStrBuffer[3]);
        crt_strcpy(m.title_leader, ParseStrBuffer[4]);
        crt_strcpy(m.adj_leader, ParseStrBuffer[5]);
        const char* fem = label_get(0xB8C / 4);
        m.is_leader_female = std::toupper((uint8_t)ParseStrBuffer[6][0]) == std::toupper((uint8_t)fem[0]);
        return true;
    }
}

}  // namespace

// ---- thumb_routine (0x5898A0) ----

void GAMEAPI thumb_routine(int32_t f) {
    if (g_pick) {
        g_pick->texts.clear();
        g_pick->redraw();
        ui_invalidate();
    }
    if (!g_thumb) g_thumb = popup_new();
    if (ExpansionEnabled && is_random(f)) {
        BasePop_hide(g_thumb);
        return;
    }
    BasePop_hide(g_thumb);
    BasePop_start(g_thumb, MFactions[f].filename, g_show_datalinks ? "DATALINKS1" : "BLURB", -1, nullptr, 0x10500,
                  nullptr);
    if (g_show_datalinks)
        BasePop_start(g_thumb, MFactions[f].filename, "DATALINKS2", -1, nullptr, 0x80, nullptr);
    char pic[16];
    std::snprintf(pic, sizeof(pic), "faction:%d", f);
    BasePop_set_picture(g_thumb, pic);
    BasePop_set_loc(g_thumb, 5, 0x136);
    dword_9469FC = 1;
    BasePop_show(g_thumb);
    dword_9469FC = 0;
}

// ---- planetfall (0x589180) ----

void GAMEAPI planetfall(int32_t f) {
    const MFaction& m = MFactions[f];
    PluralDefault = 0;
    GenderDefault = m.is_leader_female;
    parse_says(2, m.title_leader, -1, -1);
    GenderDefault = m.is_leader_female;
    PluralDefault = 0;
    parse_says(3, m.name_leader, -1, -1);
    parse_says(0, m.adj_name_faction, -1, -1);
    GenderDefault = m.noun_gender;
    PluralDefault = m.is_noun_plural;
    parse_says(1, m.noun_faction, -1, -1);
    StrBuffer[0] = 0;
    crt_strcat(StrBuffer, m.filename);
    win32::CharUpperA(StrBuffer);
    const char* label = "PLANETFALL";
    if ((m.rule_flags & RFLAG_ALIEN) && !crt_strcmp(StrBuffer, "USURPER")) label = "PLANETFALLX";
    else if (!crt_strcmp(StrBuffer, "FUNGBOY")) label = "PLANETFALLF";
    StrBuffer[0] = 0;
    crt_strcat(StrBuffer, label);
    if (CurrentTurn != 0) {
        parse_num(0, CurrentTurn);
        for (int32_t i = 0; i < 64; i++)
            if (SecretProjects[i] >= 0 && Bases[SecretProjects[i]].faction_id == f)
                parse_say(4, Facility[70 + i].name, -1, -1);
        crt_strcat(StrBuffer, "2");
    }
    X_pop(StrBuffer, 0);
}

// ---- config_game (0x589D30) ----

int32_t GAMEAPI config_game(uint32_t mode) {
    BasePop* pop = popup_new();
    int32_t pick = 0;        // esi: the player's faction
    uint32_t players = 0;    // ebx: the human players of a hotseat / PBEM game
    int32_t action = 0;      // the picker's action (DAT_008284BC)
    auto quit = [&](int32_t r) {
        popup_delete(pop);
        pick_close();
        return r;
    };
    if (mode & 1) {
        DiffLevel = DefaultPrefs.difficulty;
        GameRules = AlphaIniPrefs.rules;
        pick = DefaultPrefs.faction_id;
        goto L_58b34a;
    }

L_589da1:  // DIFFICULTY
    for (;;) {
        int32_t def = DefaultPrefs.difficulty, diff = DiffLevel;
        if (!(GameRules & 0x1000000)) {
            Popup_start(pop, ScriptFileName, "DIFFICULTY", -1, nullptr, 0);
            for (int32_t i = 0; i < 6; i++) {
                StrBuffer[0] = 0;
                crt_strcat(StrBuffer, Difficulty[i]);
                if (i == 0 || i == 5) {
                    crt_strcat(StrBuffer, " ");
                    crt_strcat(StrBuffer, "(");
                    crt_strcat(StrBuffer, label_get(i == 0 ? 0x1C4 / 4 : 0x1C8 / 4));
                    crt_strcat(StrBuffer, ")");
                }
                BasePop_item(pop, StrBuffer, i);
            }
            BasePop_set_active(pop, DefaultPrefs.difficulty);
            int32_t r = setupwin_do_menu_2(pop);
            if (r < 0) return quit(1);
            def = diff = r;
        }
        // L_589fe7: the rules
        for (;;) {
            DiffLevel = diff;
            DefaultPrefs.difficulty = def;
            players = 0;
            if (mode == 2) goto L_58a551;
            Popup_start(pop, ScriptFileName, "USERULES", -1, nullptr, 0);
            BasePop_set_active(pop, AlphaIniPrefs.customize);
            int32_t r = setupwin_do_menu_2(pop);
            if (r < 0) break;
            AlphaIniPrefs.customize = r;
            if (r == 0) {
                GameRules = (int32_t)(((uint32_t)GameRules & 0xFFFF0000u) | default_rules());
                goto L_58a551;
            }
            auto current = [&]() {
                uint32_t hi = (uint32_t)GameRules & 0xFFFF0000u;
                uint32_t lo = DiffLevel == 0 ? (((uint32_t)AlphaIniPrefs.rules & 0xFDFF) | 0x200)
                                             : ((uint32_t)AlphaIniPrefs.rules & 0xFFFF);
                GameRules = (int32_t)(hi | lo);
                GameState = (int32_t)(((uint32_t)GameState & 0xFE7FFFFFu) | (uint32_t)AlphaIniPrefs.semaphore);
                if (MultiplayerActive) GameRules |= 0x200;
            };
            if (r == 1) {
                current();
                goto L_58a551;
            }
            // RULES: the check boxes (DialogChoices bits in the order of the script's items).
            for (;;) {
                current();
                uint32_t g = (uint32_t)GameRules, c = 0;
                static const uint32_t kBits[16] = {0x800, 2, 8, 4, 0x1000, 1, 0x10, 0x20,
                                                   0x4000, 0x200, 0x40, 0x100, 0x2000, 0x8000, 0x80, 0x400};
                for (int32_t i = 0; i < 16; i++)
                    if (i == 11 ? !(g & kBits[i]) : (g & kBits[i]) != 0) c |= 1u << i;
                if (AlphaIniPrefs.semaphore & 0x800000) c |= 0x10000;
                if (AlphaIniPrefs.semaphore & 0x1000000) c |= 0x20000;
                DialogChoices = (int32_t)c;
                int32_t rr = X_pop_6("RULES", 0x41, 0);
                if (rr < 0) break;
                GameRules = (int32_t)((uint32_t)GameRules & 0xFFFF0000u);
                GameState = (int32_t)((uint32_t)GameState & 0xFE7FFFFFu);
                if (DialogToggle == 0) {
                    uint32_t d = (uint32_t)DialogChoices;
                    for (int32_t i = 0; i < 16; i++)
                        if (i == 11 ? !(d & (1u << i)) : (d & (1u << i)) != 0) GameRules |= (int32_t)kBits[i];
                    if (d & 0x10000) GameState |= 0x800000;
                    if (d & 0x20000) GameState |= 0x1000000;
                    AlphaIniPrefs.semaphore = GameState & 0x1800000;
                    AlphaIniPrefs.rules = GameRules;
                    g_show_datalinks = 0;
                    goto L_58a551;
                }
                // Standard Rules
                AlphaIniPrefs.rules = (int32_t)((uint32_t)GameRules | default_rules());
                AlphaIniPrefs.semaphore = GameState & 0x1800000;
                GameRules = AlphaIniPrefs.rules;
            }
        }
        if (GameRules & 0x1000000) return quit(1);
    }

L_58a551:  // the factions
    if ((GameRules & 0x100000) && !PbemActive) {  // the scenario's faction
        pick = CurrentPlayerFaction;
        goto L_58a56e;
    }
    if (!ExpansionEnabled || (mode & 2)) {
        // FACTIONX: a list of the factions with the text box.
        Popup_start(pop, ScriptFileName, "FACTIONX", -1, nullptr, 0);
        for (int32_t f = 1; f < 8; f++) {
            if (mode == 2 && !(FactionStatus[1] & (1 << f))) continue;
            StrBuffer[0] = 0;
            crt_strcat(StrBuffer, MFactions[f].formal_name_faction);
            crt_strcat(StrBuffer, "|(");
            crt_strcat(StrBuffer, MFactions[f].desc_name_faction);
            crt_strcat(StrBuffer, ")");
            BasePop_item(pop, StrBuffer, f);
        }
        BasePop_set_active(pop, DefaultPrefs.faction_id);
        thumb_routine(std::max(1, std::min(7, DefaultPrefs.faction_id)));
        int32_t r = BasePop_exec_3(pop, 0, 0);
        pick_close();
        if (r < 0) {
            if (mode == 2) return quit(1);
            goto L_589da1;
        }
        if (!(mode & 2)) DefaultPrefs.faction_id = r;
        pick = r;
        if (BasePop_button_pressed(pop)) {  // OTHER: the other faction file (RELOADFAC)
            if (X_pop_ask("RELOADFAC", 0x50, MFactions[pick].filename, 0, 0) == 0) {
                crt_strcpy(MFactions[pick].filename, ParseStrBuffer[0]);
                crt_strcpy(MFactions[pick].search_key, ParseStrBuffer[0]);
                read_faction(&MFactions[pick], 0);
                load_faction_art(pick);
                compute_faction_modifiers(pick);
            }
            goto L_58a551;
        }
        action = 0;
    } else {
        // PickWin (it stays up under the name popup until the name is given: pick_close).
        pick_close();
        PickWin& w = *(g_pick = new PickWin());
        w.name = "pickwin";
        w.mode = (int32_t)mode;
        w.load();
        sw_init(&w, 5, 5, 0x316, 300, false);
        w.layout();
        w.sel = std::max(1, std::min(7, DefaultPrefs.faction_id));
        DefaultPrefs.faction_id = w.sel;
        w.saved_sel = w.sel;
        sw_show(&w);
        thumb_routine(w.sel);
        sw_run(&w);
        sw_show(&w);
        pick = w.result;
        action = w.action;
        if (pick < 0) {
            pick_close();
            goto L_589da1;
        }
        DefaultPrefs.faction_id = pick;
        if (action == 0x105) {
            pick = game_rand() % 7 + 1;
            pick_close();
        } else if (action == 0x107) {
            g_show_datalinks = !g_show_datalinks;
            pick_close();
            goto L_58a551;
        } else if (action != 0) {
            if (is_random(pick)) {
                pick_close();
                goto L_58a551;
            }
            custom_names(pick);
            pick_close();
            goto L_58a551;
        }
    }
    // The faction's voice (voices\<filename>.mp3 on FacilityVoice) while its leader is named.
    if (GameMorePreferences & 0x4000) {
        std::string voice = std::string("voices\\") + MFactions[pick].filename + ".mp3";
        Wave_unload(&FacilityVoice);
        Wave_load(&FacilityVoice, voice.c_str());
        Wave_play_2(&FacilityVoice);
    }
    // L_58a926: the leader's name (NAME, NAME2 female, NAME3 for a random faction).
    while (!is_random(pick)) {
        StrBuffer[0] = 0;
        crt_strcat(StrBuffer, "NAME");
        if (MFactions[pick].is_leader_female && action != 0x105) crt_strcat(StrBuffer, "2");
        if (action == 0x105) crt_strcat(StrBuffer, "3");
        int32_t r = X_pop_ask(StrBuffer, 0x17, action == 0x105 ? "" : MFactions[pick].name_leader, 0, 0);
        pick_close();
        if (GameMorePreferences & 0x4000) Sound_fade_2(&FacilityVoice, 2000);
        if (r != 0) goto L_58a551;
        crt_strcpy(MFactions[pick].name_leader, ParseStrBuffer[0]);
        if (DialogToggle == 0) break;
        if (action != 0x105) {
            if (!custom_names(pick)) goto L_58a551;
            goto L_58a551;
        }
        MFactions[pick].is_leader_female = !MFactions[pick].is_leader_female;
    }

L_58b34a:  // the rivals
    {
        if (mode == 0) prefs_save(1);
        int32_t choices = 14;
        if (!text_open(AlphaxFileName, "CUSTOMFACTIONS")) {
            text_get();
            while (*text_item()) {
                choices++;
                text_get();
            }
        }
        int32_t aliens = 0, first = 0, second = 0;
        char alien_name[24] = "";
        for (int32_t f = 1; f < 8; f++) {
            if (is_random(f) || !(MFactions[f].rule_flags & RFLAG_ALIEN)) continue;
            if (++aliens == 1) crt_strcpy(alien_name, MFactions[f].filename);
        }
        if (aliens == 1) {
            // One alien faction: the last random slot gets the other one.
            int32_t slot = 0;
            for (int32_t f = 1; f < 8; f++)
                if (is_random(f)) slot = f;
            if (slot) {
                if (text_open(AlphaxFileName, "NEWFACTIONS")) goto L_58a551;
                for (int32_t i = 0; i < 6; i++) text_get();
                char* name = MFactions[slot].filename;
                crt_strcpy(name, text_item());
                if (!crt_strcmp(name, alien_name)) {
                    text_get();
                    crt_strcpy(name, text_item());
                }
                crt_strcpy(MFactions[slot].search_key, name);
                read_faction(&MFactions[slot], 0);
                load_faction_art(slot);
            }
        } else if (aliens == 0 && (game_rand() % 2) != 0) {
            // No alien faction: in half the games both aliens take two random slots.
            int32_t start = game_rand() % 7;
            for (int32_t k = start; k < start + 7; k++) {
                int32_t s = k % 7;
                if (!is_random(s + 1)) continue;
                if (!first) first = s + 1;
                else if (!second) second = s + 1;
            }
            if (!second) first = 0;
        }
        for (int32_t f = 1; f < 8; f++) {
            if (!is_random(f)) continue;
            char* name = MFactions[f].filename;
            if (f == first || f == second) {
                if (text_open(AlphaxFileName, "NEWFACTIONS")) goto L_58a551;
                for (int32_t i = 0; i < (f == first ? 6 : 7); i++) text_get();
                crt_strcpy(name, text_item());
                crt_strcpy(MFactions[f].search_key, name);
                read_faction(&MFactions[f], 0);
                load_faction_art(f);
                continue;
            }
            for (;;) {
                int32_t r;
                do r = game_rand() % choices;
                while (r == 12 || r == 13);
                static const char* kLists[3] = {"FACTIONS", "NEWFACTIONS", "CUSTOMFACTIONS"};
                if (r / 7 < 3 && text_open(AlphaxFileName, kLists[r / 7])) goto L_58a551;
                text_get();
                for (int32_t i = 0; i < r % 7; i++) text_get();
                crt_strcpy(name, text_item());
                crt_strcpy(MFactions[f].search_key, name);
                bool dup = false;
                for (int32_t g = 1; g < 8; g++)
                    if (g != f && !crt_strcmp(name, MFactions[g].filename)) dup = true;
                if (dup) continue;
                read_faction(&MFactions[f], 0);
                load_faction_art(f);
                break;
            }
        }
        load_flags();
        if (ExpansionEnabled && !MultiplayerActive && !PbemActive && (MFactions[pick].rule_flags & RFLAG_ALIEN))
            GameRules = (int32_t)(((uint32_t)GameRules & ~0x200u) | 0x140);
        if (!(mode & 1)) ui_invalidate();
        if (PbemActive) {
            // Hotseat / PBEM (HOTSEATADD0/1, not reconstructed): the picked faction plays.
            players |= 1u << pick;
            FactionStatus[0] = (uint8_t)players;
            CurrentPlayerFaction = pick;
            goto L_58a580;
        }
    }

L_58a56e:  // the human player
    CurrentPlayerFaction = pick;
    FactionStatus[0] = (uint8_t)(1 << pick);
L_58a580:
    {
        // (MultiplayerActive: time_controls_dialog, not reconstructed.)
        AlphaIniPrefs.time_controls = AlphaIniPrefs.time_controls;
        prefs_save(0);
        log_set_state(0);
        if (!(mode & 2)) {
            VehCount = 0;
            if (g_ui_before_world_build) g_ui_before_world_build();
            if (MapGenerate) world_build();
            setup_game(0);
        }
        if (mode == 2) scenario_setup();
        if (MultiplayerActive) log_set_state(1);
        if (ExpansionEnabled) alien_start();
        if (!MultiplayerActive && !PbemActive) {
            if (!(mode & 2)) planetfall(CurrentPlayerFaction);
        } else if (PbemActive) {
            clear_tamper();
            // The PBEM tamper seeds (timeGetTime, GetSystemTime) of PbemState are not set here.
        }
    }
    return quit(0);
}
