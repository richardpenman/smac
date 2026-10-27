// Placeholders of original UI functions the console calls that are not reconstructed yet (declared
// in console.h). None of them changes game state in the original except where noted; each does
// nothing here (the first call is logged) or only marks the screen for presentation.
#include "ui/console.h"
#include "ui/console_frame.h"
#include "ui/win.h"
#include "engine/ui_bridge.h"
#include "engine/globals.h"
#include "engine/memmap.h"
#include "engine/log.h"
#include "engine/net.h"
#include "engine/parse.h"
#include "engine/rules.h"
#include "engine/strings.h"
#include "ui/popup.h"

#include <cctype>
#include <cstdlib>
#include <cstdio>
#include <string>

// The cursor (MapWin_draw_radius's Texture_draw_trans_3 of the cursor texture over the cursor
// tile) is composed over the map view at presentation (win.cpp) from the tile's corners of the
// last redraw; partial redraws are not reconstructed: the selected unit's blink and a moved cursor
// redraw the view.
void GAMEAPI draw_cursor() {
    if (MapWinInVehMode) ui_invalidate_map();
    else ui_invalidate();
}
void THISCALL MapWin_draw_cursor(uint8_t*) { ui_invalidate_map(); }
// time_controls_dialog (0x589330): the TIMECONTROLS popup's six items, "NAME|(TURN sec/turn ...
// BASE sec/base + UNIT sec/unit)" (the first, no time controls: its label only).
void time_controls_dialog(BasePop* pop) {
    Popup_start(pop, ScriptFileName, "TIMECONTROLS", -1, nullptr, 0x40);
    auto lab = [](uint32_t off) { return std::string(label_get(off / 4)); };
    for (int32_t i = 0; i < 6; i++) {
        const CTimeControl& tc = TimeControl[i];
        std::string t = std::string(tc.name ? tc.name : "") + "|(";
        if (i == 0) {
            t += lab(0x474);
        } else {
            t += std::to_string(tc.turn) + " " + lab(0x468) + " " + lab(0x1094) + " " + std::to_string(tc.base) +
                 " " + lab(0x46C) + " + " + std::to_string(tc.unit) + " " + lab(0x470);
        }
        t += ")";
        BasePop_item(pop, t.c_str(), i);
    }
}

// Console_time_controls (0x515900, T / the commlink panel's time): outside multiplayer nothing;
// a client: HOSTTIME; the host: TIMECONTROLS (the current one selected); CUSTOM (5) asks its seven
// values (CUSTOMTIME, each 0..9999) and sends the table (NetDaemon_synch 0x85); the choice goes to
// everyone (0x4C02: set_time_controls, the turn's time left kept, HECHANGEDTIME / WECHANGEDTIME).
void THISCALL Console_time_controls(uint8_t*) {
    if (!MultiplayerActive) return;
    if (!NetSessionState[0]) {
        NetMsg_pop(&NetMsg, "HOSTTIME", 5000, 0, nullptr);
        return;
    }
    BasePop* pop = popup_new();
    time_controls_dialog(pop);
    BasePop_set_active(pop, GameTimeControl);
    int32_t r = BasePop_exec_3(pop, 0, 0);
    popup_delete(pop);
    if (r < 0) return;
    if (r == 5) {
        CTimeControl& tc = TimeControl[5];
        int32_t* fields[7] = {&tc.base, &tc.unit, &tc.event, &tc.turn, &tc.extra, &tc.refresh, &tc.accumulated};
        for (int32_t i = 0; i < 7; i++) parse_says(i, std::to_string(*fields[i]).c_str(), -1, -1);
        BasePop* ask = popup_new();
        int32_t a = -1;
        if (!Popup_start(ask, ScriptFileName, "CUSTOMTIME", -1, nullptr, 0x44)) a = BasePop_exec_3(ask, 0, 0);
        popup_delete(ask);
        if (a >= 0) {
            for (int32_t i = 0; i < 7; i++) {
                int32_t v = std::atoi(ParseStrBuffer[i]);
                *fields[i] = v < 0 ? 0 : (v > 9999 ? 9999 : v);
            }
            std::fprintf(stderr, "time controls custom: base %d unit %d event %d turn %d extra %d refresh %d max %d\n",
                         tc.base, tc.unit, tc.event, tc.turn, tc.extra, tc.refresh, tc.accumulated);
            NetDaemon_synch(&NetState, 0x85, 0, 0, 0, nullptr, 1, 0x2101);
        }
    }
    message_data(0x4C02, 0, r, 0, 0, 0);
}

// Console_chat (0x515770, ctrl+C / the Network menu): outside multiplayer nothing. During the
// upkeep not yet reached: FINISHUPKEEP. Else CHATASK (the text, 255 characters): a text starting
// with '!' goes to every player (0x4E00, without the '!'); with some players excluded by the chat
// mask (NetSessionState[40], 0x93E960) to each player still listed, or to `to` (a faction; < 1:
// the diplomatic partner during a commlink) as 0x1E00; else to everyone (0x4E00).
void THISCALL Console_chat(uint8_t*, int32_t to) {
    if (!MultiplayerActive) return;
    if (ControlTurnC && !(NetSessionState[15] & (1 << (CurrentPlayerFaction & 31))) && !NetSessionState[8]) {
        NetMsg_pop(&NetMsg, "FINISHUPKEEP", 1000, 0, nullptr);
        return;
    }
    if (X_pop_ask("CHATASK", 0xFF, reinterpret_cast<const char*>(mem::host(0x939FE8)), 0, 0)) return;
    if (ParseStrBuffer[0][0] == '!') {
        message_chat(0x4E00, 0, ParseStrBuffer[0] + 1);
        log_say("Sent CHAT ALL(1)", ParseStrBuffer[0], 0, 0, 0);
        return;
    }
    uint8_t humans = FactionStatus[0];
    int32_t mask = NetSessionState[40];
    if (NetSessionState[4] > 1 && (humans & mask) != humans) {
        for (int32_t f = 1; f < 8; f++) {
            uint32_t bit = 1u << f;
            if (!(humans & bit)) continue;
            int32_t target = to;
            if (to < 1) {
                target = *reinterpret_cast<int32_t*>(mem::host(0x73B790));  // the diplomatic partner
                if (!NetCommlinkActive) {
                    if (!ControlWaitLoop && !(mask & bit)) continue;
                    target = f;
                }
            }
            if (f != target) continue;
            message_chat(0x1E00, f, ParseStrBuffer[0]);
            log_say("Sent CHAT", ParseStrBuffer[0], f, 0, 0);
        }
        return;
    }
    message_chat(0x4E00, 0, ParseStrBuffer[0]);
    log_say("Sent CHAT ALL(2)", ParseStrBuffer[0], 0, 0, 0);
}

// Console_custom_my_faction (0x4DD1B0, ctrl+shift+F / the Game menu): the CUSTOMNAMES popup of the
// viewer's faction (in the scenario editor, EDITORWHO first picks the faction): the leader's
// name, the faction's formal name, noun and adjective, the leader's title and adjective, and the
// gender (male / female labels 0xB88 / 0xB8C); OK copies the fields back (female when the gender
// text starts as label 0xB8C, compared case-insensitively on its first letter).
// (NetDaemon_synch of the names: multiplayer, out of scope.)
void THISCALL Console_custom_my_faction(uint8_t*) {
    int32_t f = CurrentPlayerFaction;
    if ((GameState & STATE_SCENARIO_EDITOR) && (GameState & STATE_OMNISCIENT_VIEW)) {
        BasePop* who = popup_new();
        PopupDialogState = 6;
        Popup_start(who, ScriptFileName, "EDITORWHO", -1, nullptr, 0x40);
        for (int32_t i = 1; i < 8; i++) {
            PluralDefault = MFactions[i].is_noun_plural;
            GenderDefault = MFactions[i].noun_gender;
            BasePop_item(who, MFactions[i].noun_faction, i);
        }
        f = BasePop_exec_3(who, 0, 0);
        PopupDialogState = 0;
        popup_delete(who);
        if (f < 0) return;
    }
    MFaction& m = MFactions[f];
    std::string gender = label_get(m.is_leader_female ? 0xB8C / 4 : 0xB88 / 4);
    BasePop* pop = popup_new();
    Popup_start(pop, ScriptFileName, "CUSTOMNAMES", -1, nullptr, 0x44);
    BasePop_edit(pop, label_get(0x2B8 / 4), m.name_leader, 0x17);
    BasePop_edit(pop, label_get(0x2BC / 4), m.formal_name_faction, 0x27);
    BasePop_edit(pop, label_get(0x2C0 / 4), m.noun_faction, 0x17);
    BasePop_edit(pop, label_get(0x2C4 / 4), m.adj_name_faction, 0x17);
    BasePop_edit(pop, label_get(0x78C / 4), m.title_leader, 0x17);
    BasePop_edit(pop, label_get(0x790 / 4), m.adj_leader, 0x17);
    BasePop_edit(pop, label_get(0x794 / 4), gender.c_str(), 0x17);
    int32_t r = BasePop_exec_3(pop, 0, 0);
    popup_delete(pop);
    if (r < 0) return;
    crt_strcpy(m.name_leader, ParseStrBuffer[0]);
    crt_strcpy(m.formal_name_faction, ParseStrBuffer[1]);
    crt_strcpy(m.noun_faction, ParseStrBuffer[2]);
    crt_strcpy(m.adj_name_faction, ParseStrBuffer[3]);
    crt_strcpy(m.title_leader, ParseStrBuffer[4]);
    crt_strcpy(m.adj_leader, ParseStrBuffer[5]);
    const char* fem = label_get(0xB8C / 4);
    m.is_leader_female = std::toupper((uint8_t)ParseStrBuffer[6][0]) == std::toupper((uint8_t)fem[0]);
    std::fprintf(stderr, "custom faction %d: %s %s, %s / %s / %s (%s)\n", f, m.title_leader, m.name_leader,
                 m.formal_name_faction, m.noun_faction, m.adj_name_faction, m.is_leader_female ? "female" : "male");
}
