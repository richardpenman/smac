// The engine's popups built on Popup objects on the original functions' stacks (engine/ui_bridge.h,
// the ui_*_menu / ui_*_popup functions): smacx shows them with the reconstructed Popup (popup.h).
// The headless "popup" area holds their unattended defaults; the diplomacy menus replace the
// engine's ui_bridge_diplomacy.cpp in smacx (all four functions, so that object is not linked).
#include <cstdio>
#include <cstring>
#include <string>

#include "engine/base.h"
#include "engine/crt.h"
#include "engine/diplo_act.h"
#include "engine/battle.h"
#include "engine/globals.h"
#include "engine/map.h"
#include "engine/probe.h"
#include "engine/parse.h"
#include "engine/rules.h"
#include "engine/strings.h"
#include "engine/tech.h"
#include "engine/text.h"
#include "engine/ui_bridge.h"
#include "engine/veh.h"
#include "ui/popup.h"

namespace {

// The Popup object of a stack frame that spans several engine calls (open ... exec).
BasePop* g_production = nullptr;
BasePop* g_tech = nullptr;
BasePop* g_council = nullptr;
BasePop* g_diplo = nullptr;

void reset(BasePop*& pop) {
    popup_delete(pop);
    pop = popup_new();
}

int32_t popup_start(BasePop* pop, const char* file, const char* label, uint32_t flags) {
    return BasePop_start(pop, file, label, -1, nullptr, flags, nullptr);
}

// X_text_open's line, expanded (parse_string into StrBuffer, as the originals do).
const char* expand(const char* line) {
    StrBuffer[0] = 0;
    parse_string(line, StrBuffer);
    return StrBuffer;
}

}  // namespace

// ---- Popup objects constructed on a caller's stack (order_veh, supply_options, Console) ----
void THISCALL Popup_ctor(uint8_t*) {}
void THISCALL Popup_dtor(uint8_t*) {}

// ---- pact_ends (0x53C640): ENDPACT1, with ENDPACT2 appended when units were withdrawn ----
void ui_pact_ends_dialog(int32_t withdrawn) {
    BasePop* pop = popup_new();
    uint32_t flags = MultiplayerActive ? 0 : 0x100000;
    popup_start(pop, ScriptFileName, "ENDPACT1", flags);
    if (withdrawn) popup_start(pop, ScriptFileName, "ENDPACT2", flags | 0x80);
    BasePop_exec_3(pop, 0, 0);
    popup_delete(pop);
}

// ---- tech_achieved (0x5BAF70): the announcement of another faction's tech ----
void ui_tech_popup_open() {
    reset(g_tech);
    BasePop_set_width(g_tech, 500);
    popup_start(g_tech, "BLURBSX", StrBuffer, 0x80);
    BasePop_string(g_tech, "^");
    BasePop_string(g_tech, "^------");
    BasePop_string(g_tech, "^");
}
// The tech icon (SpriteBox_sprite of the tech's sprite at half size) and the viewer's help text
// (help_tech_info_2) are not shown: the tech icons and the help module are not reconstructed.
void ui_tech_popup_show(int32_t, int32_t flags, int32_t, bool) {
    if (!(flags & 0x80)) reset(g_tech);
    popup_start(g_tech, ScriptFileName, StrBuffer, (uint32_t)flags);
}
void ui_tech_popup_exec() {
    if (!g_tech) return;
    BasePop_exec_3(g_tech, 0, 0);
    popup_delete(g_tech);
    g_tech = nullptr;
}

// ---- base_production (0x4F2A70): the blurb of a facility / another faction's secret project ----
void ui_production_popup_open(int32_t width) {
    reset(g_production);
    BasePop_set_width(g_production, width);
}
void ui_production_popup_blurb(bool is_project) {
    if (!g_production) ui_production_popup_open(500);
    popup_start(g_production, "BLURBS", StrBuffer, 0x80);
    if (!is_project || ScreenWidth != 800) BasePop_string(g_production, "^");
    BasePop_string(g_production, "^------");
    BasePop_string(g_production, "^");
    if (is_project) popup_start(g_production, ScriptFileName, "PRODUCE2", 0x80);
}
void ui_production_popup_exec() {
    if (!g_production) return;
    BasePop_set_picture(g_production, "secproj_sm.pcx");
    BasePop_exec_3(g_production, 0, 0);
    popup_delete(g_production);
    g_production = nullptr;
}

// ---- call_council (0x52C880): the proposals menu (COUNCILISSUES, with Cancel) ----
void ui_council_menu_open(int32_t) {
    reset(g_council);
    popup_start(g_council, ScriptFileName, "COUNCILISSUES", 0x40);
    BasePop_set_picture(g_council, "council_sm.pcx");
}
void ui_council_menu_add(const char* text, int32_t proposal) {
    if (g_council) BasePop_item(g_council, text, proposal);
}
int32_t ui_council_menu_exec() { return g_council ? BasePop_exec_3(g_council, 0, 0) : -1; }
void ui_council_menu_close() {
    popup_delete(g_council);
    g_council = nullptr;
}

// ---- supply_options (0x593A00): SUPPLYUNIT with the SUPPLYMENU lines that apply ----
int32_t ui_supply_menu(int32_t veh_id, int32_t base_id, int32_t) {
    BasePop* pop = popup_new();
    int32_t faction_id = Vehs[veh_id].faction_id;
    popup_start(pop, ScriptFileName, "SUPPLYUNIT", 0);
    BasePop_set_picture(pop, (MFactions[faction_id].rule_flags & 0x80) ? "al_col_sm.pcx" : "colfoun_sm.pcx");
    int32_t result = 0;
    if (!X_text_open(ScriptFileName, "SUPPLYMENU")) {
        const BASE& base = Bases[base_id];
        int32_t items = 0;
        BasePop_item(pop, expand(text_get()), 0);  // cancel
        items++;
        const char* line = text_get();
        int32_t item = base.queue_items[0];
        if (item <= -70) {  // help build a secret project
            parse_say(0, Facility[-item].name, -1, -1);
            BasePop_item(pop, expand(line), 1);
            items++;
        }
        line = text_get();
        item = base.queue_items[0];
        if (item >= 64 && !(Units[item].unit_flags & 4)) {  // help build a prototype
            parse_says(0, Units[item].name, -1, -1);
            BasePop_item(pop, expand(line), 1);
            items++;
        }
        if (convoy(veh_id, base_id)) {
            for (int32_t i = 2; i <= 4; i++) {
                BasePop_item(pop, expand(text_get()), i);
                items++;
            }
        }
        if (items > 1) result = BasePop_exec_3(pop, 0, 0);
    }
    popup_delete(pop);
    return result;
}

// ---- order_veh (0x594020): an artifact entering a base of its owner (ARTIFACT / ARTIFACTMENU) ----
int32_t ui_artifact_menu(int32_t veh_id, int32_t base_id, int32_t) {
    BasePop* pop = popup_new();
    const BASE& base = Bases[base_id];
    parse_says(0, base.name, -1, -1);
    popup_start(pop, ScriptFileName, "ARTIFACT", 0);
    BasePop_set_picture(pop, "art_dis_sm.pcx");
    int32_t faction_id = Vehs[veh_id].faction_id;
    int32_t item = base.queue_items[0];
    int32_t cost = item < 0 ? cost_factor(faction_id, 1, -1) * Facility[-item].cost
                            : veh_cost(item, base_id, nullptr) * cost_factor(faction_id, 1, -1);
    parse_num(0, base.minerals_accumulated);
    parse_num(1, cost);
    int32_t result = 0;
    if (!X_text_open(ScriptFileName, "ARTIFACTMENU")) {
        int32_t items = 1;
        BasePop_item(pop, expand(text_get()), 0);  // keep the artifact
        const char* line = text_get();
        int32_t offset, mask;
        bitmask(8, &offset, &mask);  // network node: link the artifact
        if ((base.facilities_built[offset] & (uint8_t)mask) && SecretProjects[20] != base_id &&
            !(base.state_flags & 0x800)) {
            BasePop_item(pop, expand(line), 1);
            items++;
        }
        line = text_get();
        if (SecretProjects[20] == base_id) {  // the Planetary Datalinks... (project 20)
            BasePop_item(pop, expand(line), 1);
            items++;
        }
        line = text_get();
        item = base.queue_items[0];
        if (item <= -70) {  // hurry a secret project
            parse_say(1, Facility[-item].name, -1, -1);
            BasePop_item(pop, expand(line), 2);
            items++;
        }
        line = text_get();
        item = base.queue_items[0];
        if (item >= 64 && !(Units[item].unit_flags & 4)) {  // hurry a prototype
            parse_says(2, Units[item].name, -1, -1);
            BasePop_item(pop, expand(line), 2);
            items++;
        }
        if (items > 1) result = BasePop_exec_3(pop, 0, 0);
    }
    popup_delete(pop);
    return result;
}

// ---- order_veh: a ship moving to land, DISEMBARK with a DISEMBARKERS line per passenger that
// can move (its id: the vehicle + 1) ----
int32_t ui_disembark_menu(int32_t veh_id) {
    BasePop* pop = popup_new();
    popup_start(pop, ScriptFileName, "DISEMBARK", 0);
    int32_t result = 0;
    if (!X_text_open(ScriptFileName, "DISEMBARKERS")) {
        std::string line = text_get() ? TextBufferGetPtr : "";
        int32_t top = veh_id;
        while (Vehs[top].prev_veh_id_stack >= 0) top = Vehs[top].prev_veh_id_stack;
        int32_t items = 0;
        for (int32_t v = top; v >= 0; v = Vehs[v].next_veh_id_stack) {
            if (Vehs[v].order != 1 || Vehs[v].waypoint_x[0] != veh_id) continue;
            if (speed(v, 0) - Vehs[v].moves_spent <= 0) continue;
            parse_says(1, Units[Vehs[v].unit_id].name, -1, -1);
            BasePop_item(pop, expand(line.c_str()), v + 1);
            items++;
        }
        if (items) result = BasePop_exec_3(pop, 0, 0);
    }
    popup_delete(pop);
    return result;
}

// ---- diplomacy menus (wants_to_speak, make_gift, call_off_vendetta, propose_attack,
// buy_council_vote, tech_trade): the original shows them inside the diplomacy dialog (DiploPop,
// with the other leader's portrait); here they are popups with the portrait as their picture ----
void ui_diplo_menu_start(const char* label, int32_t flags) {
    if (!g_diplo || !(flags & 0x80)) reset(g_diplo);
    popup_start(g_diplo, ScriptFileName, label, (uint32_t)flags);
}
void ui_diplo_menu_item(const char* text, int32_t value) {
    if (g_diplo) BasePop_item(g_diplo, text, value);
}
int32_t ui_diplo_menu_exec(int32_t faction_id_with) {
    if (!g_diplo) return 0;
    if (faction_id_with > 0 && faction_id_with < 8) BasePop_set_picture(g_diplo, FactionPortraits[faction_id_with]);
    return BasePop_exec_3(g_diplo, 0, 0);
}
void ui_diplo_menu_close() {
    popup_delete(g_diplo);
    g_diplo = nullptr;
}

// ---- parse_it (0x48C060): a script line, expanded into StrBuffer ----
void GAMEAPI parse_it(const char* file, const char* label) {
    StrBuffer[0] = 0;
    if (!X_text_open(file, label)) {
        text_get();
        parse_string(TextBufferGetPtr, StrBuffer);
    }
}

// ---- probe (0x59F110): the probe team menus, shown to a human player; the original builds them
// on one Popup object on probe()'s stack. The engine (probe.cpp) has set the parse slots. ----

// PROBE (with Cancel) and the PROBEMENU lines whose bit is set in `items` (the line's index is the
// answer; the lines are listed as they are, without $-markers).
int32_t ui_probe_menu(int32_t, uint32_t items, bool alien) {
    BasePop* pop = popup_new();
    popup_start(pop, ScriptFileName, "PROBE", 0x40);
    BasePop_set_picture(pop, alien ? "al_infil_sm.pcx" : "infil_sm.pcx");
    int32_t result = -1;
    if (!X_text_open(ScriptFileName, "PROBEMENU")) {
        for (int32_t i = 0; i < 9; i++) {
            if (i == 8 && (!ExpansionEnabled || MultiplayerActive)) break;
            const char* line = text_get();
            if (!line) break;
            if (items & (1u << i)) BasePop_item(pop, line, i);
        }
        result = BasePop_exec_3(pop, 0, 0);
    }
    popup_delete(pop);
    return result;
}

// VIRUS: the planet buster (98), the base's facilities (their ids; the Aquafarm-class facility 26
// only on land), the production (0) and ABORTSTRING (99).
int32_t ui_probe_sabotage_menu(int32_t base_id, bool planet_buster, const char* production) {
    BasePop* pop = popup_new();
    popup_start(pop, ScriptFileName, "VIRUS", 0);
    if (planet_buster) BasePop_item(pop, label_get(0x404), 98);
    const BASE& base = Bases[base_id];
    for (int32_t fac = 1; fac < 0x46; fac++) {
        if (fac == 1) continue;
        if (fac == 0x1A) {
            if ((map_tile(base.x, base.y)->climate & 0xE0) < 0x60) continue;
        } else if (fac >= 0x41) {
            continue;
        }
        int32_t offset, mask;
        bitmask(fac, &offset, &mask);
        if (base.facilities_built[offset] & (uint8_t)mask) BasePop_item(pop, Facility[fac].name, fac);
    }
    StrBuffer[0] = 0;
    crt_strcat(StrBuffer, production ? production : "");
    crt_strcat(StrBuffer, " ");
    crt_strcat(StrBuffer, "(");
    crt_strcat(StrBuffer, label_get(0xF9));
    crt_strcat(StrBuffer, ")");
    BasePop_item(pop, StrBuffer, 0);
    parse_it(ScriptFileName, "ABORTSTRING");
    BasePop_item(pop, StrBuffer, 99);
    int32_t result = BasePop_exec_3(pop, 0, 0);
    popup_delete(pop);
    return result;
}

// SUBVERT + SUBVERTMENU / THOUGHTCONTROL + THOUGHTMENU (the menu only when affordable).
int32_t ui_probe_confirm_menu(const char* label, const char* menu, bool show_menu) {
    BasePop* pop = popup_new();
    popup_start(pop, ScriptFileName, label, 0);
    if (show_menu) popup_start(pop, ScriptFileName, menu, 0x80);
    int32_t result = BasePop_exec_3(pop, 0, 0);
    popup_delete(pop);
    return result;
}

// FRAME / FRAMEMENU: "nobody" (0), then each other living faction faction_id has a commlink with.
int32_t ui_probe_frame_menu(int32_t faction_id, int32_t faction_id_tgt) {
    BasePop* pop = popup_new();
    popup_start(pop, ScriptFileName, "FRAME", 0);
    int32_t result = 0;
    if (!X_text_open(ScriptFileName, "FRAMEMENU")) {
        BasePop_item(pop, expand(text_get()), 0);
        std::string line = text_get() ? TextBufferGetPtr : "";
        for (int32_t k = 1; k < 8; k++) {
            if (k == faction_id || k == faction_id_tgt) continue;
            if (!(Factions[faction_id].diplo_status[k] & 8) || !(FactionStatus[1] & (1 << k))) continue;
            parse_set(MFactions[k].is_leader_female, 0);
            parse_says(0, MFactions[k].title_leader, -1, -1);
            parse_says(1, get_name(k), -1, -1);
            clear();
            parse_string(line.c_str(), StrBuffer);
            BasePop_item(pop, StrBuffer, k);
        }
        result = BasePop_exec_3(pop, 0, 0);
    }
    popup_delete(pop);
    return result;
}

// FREEWHO (with Cancel): the leaders of factions faction_id_tgt eliminated (its monuments 15..20).
int32_t ui_probe_free_menu(int32_t faction_id_tgt, bool alien) {
    BasePop* pop = popup_new();
    popup_start(pop, ScriptFileName, "FREEWHO", 0x40);
    BasePop_set_picture(pop, alien ? "al_infil_sm.pcx" : "infil_sm.pcx");
    for (int32_t i = 0; i < 6; i++) {
        const MonumentEvent& e = Monuments[faction_id_tgt].events[15 + i];
        if (!e.achieved) continue;
        int32_t k = e.param2;
        clear();
        says(get_title(k));
        space();
        says(get_name(k));
        space();
        say_label(0xB4);
        space();
        says(get_noun(k));
        BasePop_item(pop, StrBuffer, k);
    }
    int32_t result = BasePop_exec_3(pop, 0, 0);
    popup_delete(pop);
    return result;
}

// EXCUSE / PACTEXCUSE / NETEXCUSE, after the BUSTED<n> header the engine put in StrBuffer.
int32_t ui_probe_excuse(const char* label, int32_t busted_action, bool alien) {
    BasePop* pop = popup_new();
    if (busted_action) popup_start(pop, ScriptFileName, StrBuffer, 0);
    BasePop_set_picture(pop, alien ? "al_cap_sm.pcx" : "capture_sm.pcx");
    popup_start(pop, ScriptFileName, label, busted_action ? 0x80 : 0);
    int32_t result = BasePop_exec_3(pop, 0, 0);
    popup_delete(pop);
    return result;
}
