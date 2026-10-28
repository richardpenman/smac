// The scenario editor's diplomacy editor (Console_editor_diplomacy 0x4DB870 and its window,
// 0x4D9DE0..0x4DB870), reconstructed from the asm: EDITORWHO picks a faction, then a window of
// 0x2F6 x 0x18E (centred, box 9) with the title (label 866 and the faction's noun, upper case,
// font 16 in box 0), the list of the other factions (a ListBox in box 0, the left third), the
// treaty and agenda check boxes of the pair (a CheckBox group in the right half: pact 8, treaty
// 0x10, truce 4, ... and the agenda bits 8 / 0x2000 marked 0x80000000) and four buttons along the
// bottom: 3 atrocities (EDITATROCITY), 2 integrity (EDITINTEGRITY), 1 reset the pair's relations
// (the friction drawn again: game_random), -1 done. The check boxes are applied (EditorDiplo_apply (0x4DA990):
// treaty_on / off, set_treaty, agenda_on / off, set_agenda and the pact's consequences) when
// another faction is selected and when the window closes.
// Approximated: the list box's and the check boxes' look (rows of the window's Arial 14 with the
// popups' check marks, the selection's colour; no scroll bar: the list holds six factions).
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "engine/diplomacy.h"
#include "engine/globals.h"
#include "engine/parse.h"
#include "engine/rules.h"
#include "ui/topmenu.h"
#include "engine/random.h"
#include "engine/save.h"
#include "engine/strings.h"
#include "engine/ui_bridge.h"
#include "ui/basewin.h"
#include "ui/colortables.h"
#include "ui/console.h"
#include "ui/gfx.h"
#include "ui/popup.h"
#include "ui/screenwin.h"
#include "ui/win.h"

namespace {

uint32_t& fac_u(int32_t f, uint32_t off) {
    return *reinterpret_cast<uint32_t*>(reinterpret_cast<uint8_t*>(&Factions[f]) + off);
}
int32_t& fac_i(int32_t f, uint32_t off) {
    return *reinterpret_cast<int32_t*>(reinterpret_cast<uint8_t*>(&Factions[f]) + off);
}
// Faction +0x18 + 4 f: the diplomatic status with f; +0x38: the agenda; +0x58: the friction.
uint32_t& status(int32_t a, int32_t b) { return fac_u(a, 0x18 + 4 * b); }
uint32_t& agenda(int32_t a, int32_t b) { return fac_u(a, 0x38 + 4 * b); }

// The check boxes (EditorDiplo_init (0x4DB080)'s CheckBox_item order): label offset, id (0x80000000: an agenda bit).
const struct {
    uint32_t label;
    uint32_t id;
} kChecks[13] = {{0xDAC, 8},       {0xA18, 0x10},       {0xA0C, 4},         {0xA10, 2},         {0xA14, 1},
                 {0xD90, 0x1000},  {0xD94, 0x2000},     {0xD98, 0x20},      {0xD9C, 0x2000000}, {0xDA0, 0x400},
                 {0xDA4, 0x40000}, {0xDA8, 0x80000008}, {0xDF8, 0x80002000}};

int32_t mem_i(uint32_t a) { return *reinterpret_cast<int32_t*>(mem::host(a)); }

struct DiploEdWin : ScreenWin {
    int32_t faction = 1;  // +0xA1C
    int32_t other = 1;  // +0xA20
    bool state[13] = {};
    std::vector<int32_t> list;  // the ListBox's factions
    Font font{};  // +0xA24: Arial 14
    UiRect list_r{}, check_r{};
    int32_t row_h = 16;

    bool get(uint32_t id) const {
        for (int32_t i = 0; i < 13; i++)
            if (kChecks[i].id == id) return state[i];
        return false;
    }
    void set(uint32_t id, bool on) {
        for (int32_t i = 0; i < 13; i++)
            if (kChecks[i].id == id) state[i] = on;
    }
    // EditorDiplo_load_boxes (0x4DADA0): the check boxes from the pair's status and agenda.
    void load() {
        for (int32_t i = 0; i < 13; i++) {
            uint32_t id = kChecks[i].id;
            state[i] = (id & 0x80000000) ? (agenda(faction, other) & (id & 0x7FFFFFFF)) != 0
                                         : (status(faction, other) & id) != 0;
        }
    }
    // EditorDiplo_apply_box (0x4DA760) / 870 / 7E0 / 900: one box applied with treaty_on / off or
    // set_treaty, agenda_on / off or set_agenda.
    void apply_treaty(uint32_t id, bool with_set) {
        bool on = get(id), has = (status(faction, other) & id) != 0;
        if (on == has) return;
        if (with_set) set_treaty(faction, other, id, on ? 1 : 0);
        else if (on) treaty_on(faction, other, id);
        else treaty_off(faction, other, id);
    }
    void apply_agenda(uint32_t bit, bool with_set) {
        bool on = get(bit | 0x80000000), has = (agenda(faction, other) & bit) != 0;
        if (on == has) return;
        if (with_set) set_agenda(faction, other, bit, on ? 1 : 0);
        else if (on) agenda_on(faction, other, bit);
        else agenda_off(faction, other, bit);
    }
    // EditorDiplo_apply (0x4DA990): the boxes applied, then the pact's and the vendetta's consequences.
    void apply() {
        for (uint32_t id : {8u, 0x10u, 4u, 2u, 1u}) apply_treaty(id, false);
        for (uint32_t id : {0x1000u, 0x2000u}) apply_treaty(id, true);
        for (uint32_t id : {0x20u, 0x2000000u, 0x400u, 0x40000u}) apply_treaty(id, true);
        apply_agenda(8, true);
        apply_agenda(0x2000, false);
        if (status(faction, other) & 0x2000000) {
            treaty_on(faction, other, 1);
            set_treaty(faction, other, 0xC20, 0);
            set_agenda(faction, other, 8, 0);
        }
        if (agenda(faction, other) & 8) treaty_on(faction, other, 0x10);
        if (!(status(faction, other) & 0x20)) set_agenda(faction, other, 0xC00, 0);
        std::fprintf(stderr, "diploedit %d-%d: status %08X agenda %08X\n", faction, other, status(faction, other),
                     agenda(faction, other));
    }
    // EditorDiplo_select (0x4DA740): another faction selected.
    void select(int32_t f) {
        apply();
        other = f;
        load();
    }
    // EditorDiplo_on_button_clicked (0x4D9DE0) button 1: the pair's relations reset, the friction drawn again.
    void reset() {
        treaty_off(faction, other, 0x1F);
        set_treaty(faction, other, 0x2043C20, 0);
        set_treaty(faction, other, 0xDD9BC3C0, 0);
        set_agenda(faction, other, 0x1F89, 0);
        uint32_t st = status(faction, other);
        int32_t* fa = &fac_i(faction, 0x58 + 4 * other);
        int32_t* fb = &fac_i(other, 0x58 + 4 * faction);
        if (st & 0x10) {
            *fa = *fb = 0x14;
        } else if (st & 1) {
            *fa = *fb = 2;
        } else if (st & 0x20) {
            *fa = game_random(0, 10) + 5;
            *fb = game_random(0, 10) + 5;
        } else {
            *fa = game_random(0, 15);
            *fb = game_random(0, 15);
        }
        for (uint32_t off : {0xC4u, 0x12Cu, 0x14Cu, 0x16Cu, 0x18Cu, 0x1ACu}) {
            fac_i(faction, off + 4 * other) = 0;
            fac_i(other, off + 4 * faction) = 0;
        }
        std::fprintf(stderr, "diploedit %d-%d: reset, friction %d / %d\n", faction, other, *fa, *fb);
        load();
    }
    void redraw() override {
        texts.clear();
        const int32_t w = canvas.width, h = canvas.height;
        UiRect all{0, 0, w, h};
        Buffer_set_clip(&canvas, &all);
        Buffer_fill_3(&canvas, &all, 9);
        Buffer_box_sprite(&canvas, &all, box_def(9));
        // The title.
        std::string title = std::string(label_get(0xD88 / 4)) + " ";
        GenderDefault = MFactions[faction].noun_gender;
        PluralDefault = MFactions[faction].is_noun_plural;
        title = sw_upper(title + MFactions[faction].noun_faction);
        UiRect t{4, 4, w - 4, 0x18};
        Buffer_box_sprite(&canvas, &t, box_def(0));
        t = {t.left + 3, t.top + 3, t.right - 3, t.bottom - 3};
        sw_font(&canvas, find_font(0x10, 0), mem_i(0x8C6E2C), mem_i(0x8C6E30));
        sw_text_cent(this, &canvas, title, t);
        // The list of the other factions.
        UiRect lb{4, 0x1F, w / 3 - 4, 0x172};
        Buffer_box_sprite(&canvas, &lb, box_def(0));
        list_r = {lb.left + 3, lb.top + 3, lb.right - 3, lb.bottom - 3};
        spot_init(&spots[0], 0x40);
        sw_font(&canvas, &font, mem_i(0x8C6E2C), mem_i(0x8C6E30));
        row_h = std::max(1, Buffer_text_line_height(&canvas));
        for (size_t i = 0; i < list.size(); i++) {
            int32_t y = list_r.top + (int32_t)i * row_h;
            if (list[i] == other) {
                UiRect sel{list_r.left, y, list_r.right, y + row_h};
                Buffer_fill_3(&canvas, &sel, Palette_get_nearest_palette_index(&GamePalette, 100, 81, 63, 1));
            }
            GenderDefault = MFactions[list[i]].noun_gender;
            PluralDefault = MFactions[list[i]].is_noun_plural;
            sw_text(this, &canvas, MFactions[list[i]].noun_faction, list_r.left + 2, y);
            spot_add(&spots[0], 100 + (int32_t)i, 0, list_r.left, y, list_r.right - list_r.left, row_h);
        }
        // The check boxes (CheckBox: the popups' marks 0x779504 clear / 0x779530 checked; the
        // dialogs' default font, CheckBox_close's: popup_dialog_font; rows of 24 as PrefWin's).
        check_r = {w / 2 + 4, 0x1F, (w / 2) * 2 - 4, 0x172};
        for (int32_t i = 0; i < 13; i++) {
            int32_t y = check_r.top + 4 + i * 24;
            popup_draw_check(&canvas, check_r.left - 2, y - 2, state[i] ? 2 : 1);
            sw_font(&canvas, popup_dialog_font(), mem_i(0x8C6C74), mem_i(0x8C6C78));
            sw_text(this, &canvas, label_get(kChecks[i].label / 4), check_r.left + 0x10, y);
            spot_add(&spots[0], 200 + i, 0, check_r.left, y - 4, check_r.right - check_r.left, 24);
        }
    }
    void on_button(int32_t id) override {
        if (id == -1) {
            sw_close(this);
            return;
        }
        if (id == 1) {
            reset();
        } else if (id == 2) {  // EDITINTEGRITY: the faction's blemishes (+0x124)
            PopupDialogState = 6;
            PluralDefault = MFactions[faction].is_noun_plural;
            GenderDefault = MFactions[faction].noun_gender;
            parse_says(0, MFactions[faction].noun_faction, -1, -1);
            if (X_pop_ask_number("EDITINTEGRITY", fac_i(faction, 0x124), 0) == 0)
                fac_i(faction, 0x124) = ParseNumTable[0];
            PopupDialogState = 0;
            std::fprintf(stderr, "diploedit %d: integrity %d\n", faction, fac_i(faction, 0x124));
        } else if (id == 3) {  // EDITATROCITY: the atrocities (+0x1D4) and major ones (+0x1D8)
            PopupDialogState = 6;
            PluralDefault = MFactions[faction].is_noun_plural;
            GenderDefault = MFactions[faction].noun_gender;
            parse_says(0, MFactions[faction].noun_faction, -1, -1);
            BasePop* p = popup_new();
            Popup_start(p, ScriptFileName, "EDITATROCITY", -1, nullptr, 0x44);
            char num[16];
            std::snprintf(num, sizeof(num), "%d", fac_i(faction, 0x1D4));
            BasePop_edit(p, label_get(0xDB4 / 4), num, 0);
            std::snprintf(num, sizeof(num), "%d", fac_i(faction, 0x1D8));
            BasePop_edit(p, label_get(0xDB8 / 4), num, 0);
            if (BasePop_exec_3(p, 0, 0) == 0) {
                fac_i(faction, 0x1D4) = std::atoi(ParseStrBuffer[0]);
                fac_i(faction, 0x1D8) = std::atoi(ParseStrBuffer[1]);
            }
            popup_delete(p);
            PopupDialogState = 0;
            std::fprintf(stderr, "diploedit %d: atrocities %d major %d\n", faction, fac_i(faction, 0x1D4),
                         fac_i(faction, 0x1D8));
        }
    }
    void on_click(int32_t parent, int32_t index, int32_t, int32_t, int32_t, bool right, bool) override {
        if (parent != 0 || right) return;
        if (index >= 100 && index < 100 + (int32_t)list.size()) {
            if (list[(size_t)(index - 100)] != other) select(list[(size_t)(index - 100)]);
        } else if (index >= 200 && index < 213) {
            state[index - 200] = !state[index - 200];
        }
    }
    // (No keys: the original's window takes only the mouse.)
    void log_state() override {
        std::fprintf(stderr, "diploedit state %d-%d:", faction, other);
        for (int32_t i = 0; i < 13; i++) std::fprintf(stderr, " %X=%d", kChecks[i].id, state[i] ? 1 : 0);
        std::fprintf(stderr, "\n");
    }
};

}  // namespace

// Console_editor_diplomacy (0x4DB870): EDITORWHO (the viewer selected), then the editor window
// (EditorDiplo_init (0x4DB080)).
void GAMEAPI Console_editor_diplomacy() {
    int32_t f;
    {
        BasePop* p = popup_new();
        PopupDialogState = 6;
        Popup_start(p, ScriptFileName, "EDITORWHO", -1, nullptr, 0x40);
        for (int32_t i = 1; i < 8; i++) {
            GenderDefault = MFactions[i].noun_gender;
            PluralDefault = MFactions[i].is_noun_plural;
            BasePop_item(p, MFactions[i].noun_faction, i);
        }
        if (CurrentPlayerFaction >= 0) BasePop_set_active(p, CurrentPlayerFaction);
        f = BasePop_exec_3(p, 0, 0);
        PopupDialogState = 0;
        popup_delete(p);
    }
    if (f < 0) return;
    DiploEdWin w;
    w.name = "diploedit";
    w.faction = f;
    w.other = f;
    Font_ctor(&w.font);
    Font_init(&w.font, "Arial", 0xE, 0);
    for (int32_t i = 1; i < 8; i++) {
        if (i == f) continue;
        w.list.push_back(i);
        if (w.other == f) w.other = i;
    }
    const int32_t ww = 0x2F6, wh = 0x18E;
    sw_init(&w, (ScreenWidth - ww) / 2, (ScreenHeight - wh) / 2, ww, wh, false);
    // The buttons along the bottom: 3, 2, 1, -1 (FlatButtons).
    int32_t l = 4, top = wh - 0x18, bottom = wh - 4;
    int32_t r = (((ww - l) - 2) / 4) - 2 + l;
    static const struct {
        int32_t id;
        uint32_t label;
    } kButtons[4] = {{3, 0xDB0}, {2, 0x378}, {1, 0xD8C}, {-1, 0x194}};
    for (const auto& b : kButtons) {
        sw_button(&w, label_get(b.label / 4), b.id, l, top, r - l, bottom - top, 0, &w.font);
        int32_t d = (r - l) + 2;
        l += d;
        r += d;
    }
    w.load();
    auto_undo();
    std::fprintf(stderr, "diploedit open: faction %d with %d\n", f, w.other);
    sw_run(&w);
    w.apply();
    sw_clear_buttons(&w);
    std::fprintf(stderr, "diploedit closed\n");
}
