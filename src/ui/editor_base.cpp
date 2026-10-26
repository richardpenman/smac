// BaseWin_base_editor (0x41C7A0) and its dialogs (BaseWin_base_editor_fac 0x41B790, _proj
// 0x41BBC0, _copy 0x41C240), reconstructed from the asm: the base window's editor menu (the 'e' key
// and button 0x15 in the scenario editor) at the mouse: the population up / down, the facilities
// (EDITORFAC: one toggled at a time), the secret projects (PICKAPROJ, then PICKAPLACE: unbuilt,
// destroyed or a base), copy the facilities from a base or to bases (EDITORBASECOPY / ...TO: all
// bases, the faction's), clear them, build every available one, the base's flag 0x8000
// (event_flags), the minerals and nutrients stored, the owner and the former owner (CHANGEOWNER,
// CHANGEFORMER, CHANGECONQUEST), kill the base (REALLYKILLBASE).
#include <cstdio>
#include <cstring>
#include <string>

#include "engine/base.h"
#include "engine/globals.h"
#include "engine/map.h"
#include "engine/parse.h"
#include "engine/rules.h"
#include "engine/strings.h"
#include "engine/tech.h"
#include "engine/turn.h"
#include "engine/ui_bridge.h"
#include "ui/basewin.h"
#include "ui/basewin_internal.h"
#include "ui/console.h"
#include "ui/popup.h"
#include "ui/pulldown.h"
#include "ui/win.h"

namespace {

BASE& cb() { return Bases[CurrentBaseID]; }
uint8_t* cb_raw() { return reinterpret_cast<uint8_t*>(&Bases[CurrentBaseID]); }
uint8_t* base_raw(int32_t b) { return reinterpret_cast<uint8_t*>(&Bases[b]); }
int32_t& raw_i(uint8_t* p, uint32_t off) { return *reinterpret_cast<int32_t*>(p + off); }

struct Pop {
    BasePop* p = popup_new();
    ~Pop() { popup_delete(p); }
};

void redraw(uint8_t* win) { BaseWin_on_redraw(win); }

// The bases of the current base's faction, then the others': "name|faction" with their ids.
void add_bases(BasePop* p) {
    for (int32_t pass = 0; pass < 2; pass++)
        for (int32_t b = 0; b < BaseCount; b++) {
            bool same = Bases[b].faction_id == cb().faction_id;
            if (pass == 0 ? !same : same) continue;
            StrBuffer[0] = 0;
            std::strcat(StrBuffer, Bases[b].name);
            std::strcat(StrBuffer, "|");
            PluralDefault = MFactions[Bases[b].faction_id].is_noun_plural;
            GenderDefault = MFactions[Bases[b].faction_id].noun_gender;
            std::strcat(StrBuffer, MFactions[Bases[b].faction_id].noun_faction);
            BasePop_item(p, StrBuffer, b);
        }
}

}  // namespace

// BaseWin_base_editor_fac (0x41B790): EDITORFAC, the facilities (built / available / not); a
// choice builds or removes it, until Cancel.
void THISCALL BaseWin_base_editor_fac(uint8_t* win) {
    int32_t last = -1;
    for (;;) {
        Pop pop;
        PopupDialogState = 6;
        parse_says(0, cb().name, -1, -1);
        Popup_start(pop.p, ScriptFileName, "EDITORFAC", -1, nullptr, 0x42);
        BasePop_set_cancel_text(pop.p, label_get(0x438 / 4));
        BasePop_set_ok_text(pop.p, label_get(0xFC0 / 4));
        for (int32_t f = 1; f < 0x41; f++) {
            StrBuffer[0] = 0;
            std::strcat(StrBuffer, Facility[f].name);
            std::strcat(StrBuffer, "|");
            int32_t lab;
            if (has_fac(f, CurrentBaseID, 0)) {
                lab = 0xD50;
            } else {
                int32_t avail = facility_avail(f, cb().faction_id, CurrentBaseID, 0);
                StrBuffer[0] = 0;
                std::strcat(StrBuffer, Facility[f].name);
                std::strcat(StrBuffer, "|");
                lab = avail ? 0x3DC : 0x4D8;
            }
            std::strcat(StrBuffer, label_get(lab / 4));
            BasePop_item(pop.p, StrBuffer, f);
        }
        if (last >= 0) BasePop_set_active(pop.p, last);
        int32_t r = BasePop_exec_3(pop.p, 0, 0);
        PopupDialogState = 0;
        if (r < 0) return;
        const int32_t base = *reinterpret_cast<int32_t*>(win + kBwBase);
        bool built = r < 0x41 && (base_raw(base)[0x8C + r / 8] & (1 << (r & 7)));
        set_fac(r, base, built ? 0 : 1);
        std::fprintf(stderr, "base editor: base %d facility %d %s\n", base, r, built ? "removed" : "built");
        last = r;
        redraw(win);
    }
}

// BaseWin_base_editor_proj (0x41BBC0): PICKAPROJ (each project's state: not built, available /
// not, its base), then PICKAPLACE: unbuilt (-1), destroyed (-2) or a base.
void THISCALL BaseWin_base_editor_proj(uint8_t* win) {
    Pop pop;
    PopupDialogState = 6;
    Popup_start(pop.p, ScriptFileName, "PICKAPROJ", -1, nullptr, 0x42);
    for (int32_t j = 0; j < 64; j++) {
        StrBuffer[0] = 0;
        std::strcat(StrBuffer, Facility[70 + j].name);
        std::strcat(StrBuffer, "|");
        int32_t sp = SecretProjects[j];
        if (sp == -2) std::strcat(StrBuffer, label_get(0xD54 / 4));
        else if (sp == -1)
            std::strcat(StrBuffer,
                        label_get(has_tech(Facility[70 + j].preq_tech, cb().faction_id) ? 0x3DC / 4 : 0x4D8 / 4));
        else std::strcat(StrBuffer, Bases[sp].name);
        BasePop_item(pop.p, StrBuffer, j);
    }
    const int32_t proj = BasePop_exec_3(pop.p, 0, 0);
    PopupDialogState = 0;
    if (proj < 0) return;
    parse_says(0, Facility[70 + proj].name, -1, -1);
    Pop place;
    PopupDialogState = 6;
    Popup_start(place.p, ScriptFileName, "PICKAPLACE", -1, nullptr, 0x42);
    BasePop_item(place.p, label_get(0xD64 / 4), 9999);
    BasePop_item(place.p, label_get(0xD68 / 4), 9998);
    add_bases(place.p);
    int32_t r = BasePop_exec_3(place.p, 0, 0);
    PopupDialogState = 0;
    if (r < 0) return;
    if (r > 8999) r -= 10000;
    SecretProjects[proj] = r;
    std::fprintf(stderr, "base editor: project %d at %d\n", proj, r);
    redraw(win);
}

// BaseWin_base_editor_copy (0x41C240): the facilities copied from a base (EDITORBASECOPY), or to a
// base, every base (9999) or the faction's (9998) (EDITORBASECOPYTO).
void THISCALL BaseWin_base_editor_copy(uint8_t* win, int32_t to) {
    Pop pop;
    parse_says(0, cb().name, -1, -1);
    PopupDialogState = 6;
    Popup_start(pop.p, ScriptFileName, to ? "EDITORBASECOPYTO" : "EDITORBASECOPY", -1, nullptr, 0x42);
    if (to) {
        StrBuffer[0] = 0;
        std::strcat(StrBuffer, label_get(0xE64 / 4));
        BasePop_item(pop.p, StrBuffer, 9998);
        StrBuffer[0] = 0;
        std::strcat(StrBuffer, label_get(0xE68 / 4));
        BasePop_item(pop.p, StrBuffer, 9999);
    }
    add_bases(pop.p);
    const int32_t r = BasePop_exec_3(pop.p, 0, 0);
    PopupDialogState = 0;
    if (r < 0) return;
    for (int32_t i = 0; i < 9; i++) {
        uint8_t c = cb_raw()[0x8C + i];
        if (!to) {
            cb_raw()[0x8C + i] = base_raw(r)[0x8C + i];
        } else if (r < 9998) {
            base_raw(r)[0x8C + i] = c;
        } else {
            for (int32_t b = 0; b < BaseCount; b++)
                if (b != CurrentBaseID && (r != 9998 || Bases[b].faction_id == cb().faction_id))
                    base_raw(b)[0x8C + i] = c;
        }
    }
    std::fprintf(stderr, "base editor: facilities copied %s %d\n", to ? "to" : "from", r);
    redraw(win);
}

// BaseWin_base_editor (0x41C7A0): the menu at the mouse.
void THISCALL BaseWin_base_editor(uint8_t* win) {
    PullDown pd;
    int32_t mx = 0, my = 0;
    ui_mouse_pos(&mx, &my);
    PullDown_init(&pd);
    PullDown_add_item(&pd, 1, label_get(0xD40 / 4));
    PullDown_add_item(&pd, 2, label_get(0xD44 / 4));
    PullDown_add_separator(&pd, 0x63);
    PullDown_add_item(&pd, 5, label_get(0xD58 / 4));
    PullDown_add_item(&pd, 10, label_get(0xE60 / 4));
    PullDown_add_item(&pd, 6, label_get(0xD5C / 4));
    PullDown_add_item(&pd, 7, label_get(0xD60 / 4));
    PullDown_add_separator(&pd, 0x63);
    PullDown_add_item(&pd, 8, label_get((cb().event_flags & 0x8000) ? 0xE04 / 4 : 0xE00 / 4));
    PullDown_add_separator(&pd, 0x63);
    PullDown_add_item(&pd, 3, label_get(0xD48 / 4));
    PullDown_add_item(&pd, 4, label_get(0xD4C / 4));
    PullDown_add_separator(&pd, 0x63);
    PullDown_add_item(&pd, 11, label_get(0xF58 / 4));
    PullDown_add_item(&pd, 12, label_get(0xF5C / 4));
    PullDown_add_separator(&pd, 0x63);
    PullDown_add_item(&pd, 13, label_get(0x103C / 4));
    PullDown_add_item(&pd, 14, label_get(0x1040 / 4));
    PullDown_add_item(&pd, 9, label_get(0xE18 / 4));
    PullDown_show(&pd, mx, my);
    int32_t sel = PullDown_get_selected(&pd);
    if (sel > 0x3F || sel < 0) return;
    const int32_t cmd = pulldown_item_id(&pd, sel);
    if (cmd < 0) return;
    std::fprintf(stderr, "base editor: base %d command %d\n", CurrentBaseID, cmd);
    uint8_t* b = cb_raw();
    switch (cmd) {
    case 1:
    case 2:
        if (cmd == 1 ? (int8_t)b[6] < 99 : (int8_t)b[6] > 1) b[6] = (uint8_t)(b[6] + (cmd == 1 ? 1 : -1));
        if (Win_is_visible(win)) {
            set_base(*reinterpret_cast<int32_t*>(win + kBwBase));
            base_compute(1);
            redraw(win);
            GraphicWin_redraw(win);
        }
        break;
    case 3: BaseWin_base_editor_fac(win); break;
    case 4: BaseWin_base_editor_proj(win); break;
    case 5: BaseWin_base_editor_copy(win, 0); break;
    case 10: BaseWin_base_editor_copy(win, 1); break;
    case 6:
        for (int32_t i = 0; i < 9; i++) b[0x8C + i] = 0;
        redraw(win);
        break;
    case 7:
        for (int32_t n = 0; n < 10; n++)
            for (int32_t f = 1; f < 0x41; f++)
                if (facility_avail(f, b[4], CurrentBaseID, 0)) set_fac(f, CurrentBaseID, 1);
        redraw(win);
        break;
    case 8:
        cb().event_flags ^= 0x8000;
        redraw(win);
        break;
    case 9: {
        parse_says(0, cb().name, -1, -1);
        if (!X_pop("REALLYKILLBASE", 0)) break;
        bool shown = Win_is_visible(win);
        base_kill(CurrentBaseID);
        if (shown && Win_is_visible(win)) bw_close_window(win);
        draw_map(1);
        break;
    }
    case 11:
        if (X_pop_ask_number("EDITBASEMINERALS", raw_i(b, 0x40), 0)) break;
        raw_i(b, 0x9C) = std::max(0, ParseNumTable[0]);
        raw_i(b, 0x40) = raw_i(b, 0x9C);
        raw_i(b, 0x44) = raw_i(b, 0x50);
        redraw(win);
        break;
    case 12:
        if (X_pop_ask_number("EDITBASENUTRIENT", raw_i(b, 0x3C), 0)) break;
        raw_i(b, 0x3C) = std::max(0, ParseNumTable[0]);
        redraw(win);
        break;
    case 13: {
        Pop pop;
        Popup_start(pop.p, ScriptFileName, "CHANGEOWNER", -1, nullptr, 0);
        for (int32_t f = 1; f < 8; f++) {
            GenderDefault = MFactions[f].noun_gender;
            PluralDefault = MFactions[f].is_noun_plural;
            BasePop_item(pop.p, MFactions[f].noun_faction, f);
        }
        int32_t r = BasePop_exec_3(pop.p, 0, 0);
        if (r <= 0) break;
        owner_set(cb().x, cb().y, r);
        b[5] = (uint8_t)r;
        b[4] = (uint8_t)r;
        raw_i(b, 0x4C) = 0;
        raw_i(b, 0x44) = 0;
        raw_i(b, 0x50) = 0;
        reset_territory();
        base_compute(1);
        redraw(win);
        draw_map(1);
        Console_update_data(&MainConsole, 0);
        break;
    }
    case 14: {
        // (The original's items are empty: StrBuffer is emptied and parse_says fills $FACTION0.)
        Pop pop;
        Popup_start(pop.p, ScriptFileName, "CHANGEFORMER", -1, nullptr, 0);
        for (int32_t f = 1; f < 8; f++) {
            GenderDefault = MFactions[f].noun_gender;
            PluralDefault = MFactions[f].is_noun_plural;
            StrBuffer[0] = 0;
            parse_says(0, MFactions[f].noun_faction, -1, -1);
            BasePop_item(pop.p, StrBuffer, f);
        }
        BasePop_set_active(pop.p, b[5]);
        int32_t r = BasePop_exec_3(pop.p, 0, 0);
        if (r <= 0) break;
        if (b[5] == r) {
            b[7] = 0;
        } else {
            if (!b[7]) b[7] = 0x14;
            if (X_pop_ask_number("CHANGECONQUEST", b[7], 0)) break;
            b[7] = (uint8_t)ParseNumTable[0];
        }
        b[5] = (uint8_t)r;
        base_compute(1);
        redraw(win);
        draw_map(1);
        Console_update_data(&MainConsole, 0);
        break;
    }
    default: break;
    }
}
