// Console_menu_proc (0x51BC70, reached from the menu bar through Menu_menu_event ->
// Console_menu_daemon): the commands of the menu bar's items by command id (menu.cpp), and
// Console_next_cursor (0x510B70). Reconstructed from the asm, with the scenario editor's commands
// (0x600.., 0xA00..: Console_editor_*; other ids do nothing, as the original's switch) and the
// network menu (chat, the commlink panel's button 2, time controls); the network play's own
// paths (hang up, the time bonus in multiplayer) are the online phase's (docs/MULTIPLAYER.md).
#include <algorithm>
#include <cstdio>
#include <cstdlib>

#include "engine/design.h"
#include "engine/diplo_act.h"
#include "engine/globals.h"
#include "engine/score.h"
#include "engine/zoc.h"
#include "engine/strings.h"
#include "ui/basewin.h"
#include "engine/base.h"
#include "engine/game.h"
#include "engine/turn.h"
#include "engine/map.h"
#include "engine/diplomacy.h"
#include "engine/log.h"
#include "engine/util.h"
#include "engine/net.h"
#include "engine/parse.h"
#include "engine/prefs.h"
#include "engine/rules.h"
#include "engine/save.h"
#include "engine/social.h"
#include "engine/tech.h"
#include "engine/ui_bridge.h"
#include "engine/veh.h"
#include "ui/console.h"
#include "ui/console_frame.h"
#include "ui/mapwin.h"
#include "ui/reports.h"
#include "ui/win.h"

GLOBAL(int32_t, TutorialTourKind, 0x8CC228);  // the tour TutWin_tour shows (the Help menu's tours)
GLOBAL(int32_t, ConsoleMenuPending, 0x93A944);  // a menu event came before the game was ready
GLOBAL(int32_t, DesktopRedrawFlag, 0x939ED8);

namespace {

uint8_t* main_win() { return MapWins[0]; }
uint8_t* main_menu(uint8_t* self) { return self + 0x22A2C; }  // the console's menu bar object

// The inlined Console_set_view(1) of the original (view mode, panes redrawn).
void view_mode(uint8_t* self) {
    if (MapWinInVehMode) Console_set_view(self, 1);
}

int32_t veh(uint8_t* self) { return console_win(self).veh; }

void quit_question(bool wimp_test_mp) {
    const char* label = "REALLYQUIT";
    if ((wimp_test_mp ? (MultiplayerActive || PbemActive) : PbemActive) && bit_count(FactionStatus[0]) >= 2)
        label = "REALLYWIMPOUT";
    if (X_pop(label, 0)) {
        if (PbemActive) {
            FactionStatus[0] &= (uint8_t)~(1 << (CurrentPlayerFaction & 31));
            save_game(0);
        }
        if ((GameRules & RULES_IRONMAN) && !PbemActive) auto_save();
        if (MultiplayerActive) console_not_reconstructed("NetDaemon_hang_up");
        ControlTurnA = 1;
        log_say_values("Quit                            ", 0, 0, 0);
    }
}

}  // namespace

// The display toggles shared by the Map menu (Console_menu_proc) and their keys
// (Console_on_key_click): 0x209 flat terrain (ctrl+shift+X), 0x20A / 0x20B the preferences 0x100 /
// 0x200 (ctrl+G / ctrl+shift+G), 0x20C the bases' rally points (ctrl+shift+A), 0x210 the base
// names: names, names and sizes, none (ctrl+N).
void console_toggle_pref(int32_t id) {
    switch (id) {
    case 0x209:
        GameMorePreferences ^= MPREF_MAP_SHOW_FLAT_TERRAIN;
        console_menu_check(2, 0x209, (GameMorePreferences & MPREF_MAP_SHOW_FLAT_TERRAIN) != 0);
        AlphaIniPrefs.more_preferences = GameMorePreferences;
        break;
    case 0x20A:
    case 0x20B: {
        int32_t bit = id == 0x20A ? 0x100 : 0x200;
        GamePreferences ^= bit;
        console_menu_check(2, id, (GamePreferences & bit) != 0);
        AlphaIniPrefs.preferences = GamePreferences;
        break;
    }
    case 0x20C:
        map_win(main_win()).options ^= 0x40000;
        draw_map(1);
        return;
    case 0x210:
        if (!(GameMorePreferences & MPREF_MAP_SHOW_PROD_WITH_BASE_NAMES))
            AlphaIniPrefs.more_preferences =
                GameMorePreferences | ((GameMorePreferences & MPREF_MAP_SHOW_BASE_NAMES) ? 0x1000 : 0x2000);
        else
            AlphaIniPrefs.more_preferences =
                GameMorePreferences & ~(MPREF_MAP_SHOW_PROD_WITH_BASE_NAMES | MPREF_MAP_SHOW_BASE_NAMES);
        GameMorePreferences = AlphaIniPrefs.more_preferences;
        break;
    default: return;
    }
    prefs_save(0);
    draw_map(1);
}

// Console_survey (0x515450): the survey views of the main map (display options 0x623, 0x603,
// 0x402 drawn once each, then the normal view again).
void THISCALL Console_survey(uint8_t* self) {
    static const int32_t kViews[3] = {0x623, 0x603, 0x402};
    uint8_t* w = main_win();
    int32_t step = console_win(self).redraw;
    if (step > 2) {
        console_win(self).redraw = 0;
        pfncRedrawMap(w, 1);
        return;
    }
    console_win(self).redraw = step + 1;
    int32_t saved = map_win(w).options;
    map_win(w).options = kViews[step];
    pfncRedrawMap(w, 1);
    map_win(w).options = saved;
}

// Console_zoom_to_upkeep_base (0x5165B0, '*'): the base window of the player's base nearest to
// the view's centre among those with event flag 0x10.
void THISCALL Console_zoom_to_upkeep_base(uint8_t* self) {
    uint8_t* w = main_win();
    int32_t cx = map_win(w).center_x, cy = map_win(w).center_y, best = 99999, found = -1;
    for (int32_t b = 0; b < BaseCount; b++) {
        if (Bases[b].faction_id != CurrentPlayerFaction || !(Bases[b].event_flags & 0x10)) continue;
        int32_t dx = std::abs(cx - Bases[b].x);
        if (!(MapToggleFlat & 1) && MapHalfX < dx) dx = MapAreaX - dx;
        int32_t dy = std::abs(cy - Bases[b].y);
        int32_t hi = std::max(dx, dy), lo = std::min(dx, dy);
        int32_t d = hi - ((((dx + dy) >> 1) - lo + 1) >> 1);
        if (d <= best) {
            found = b;
            best = d;
        }
    }
    if (found >= 0) BaseWin_zoom(&BaseWin, found, 0);
}

// Console_show_scen_objectives (0x516C50): SCENARIO.txt's OBJECTIVES.
void THISCALL Console_show_scen_objectives(uint8_t* self) {
    int32_t f = CurrentPlayerFaction;
    parse_num(0, ObjectiveReqVictory);
    parse_num(1, ObjectivesSuddenDeathVictory);
    parse_num(2, num_objectives(f, GameRules & RULES_VICTORY_COOPERATIVE));
    parse_num(3, EndingMissionYear);
    PluralDefault = 0;
    GenderDefault = MFactions[f].is_leader_female;
    parse_says(1, MFactions[f].title_leader, -1, -1);
    PluralDefault = 0;
    GenderDefault = MFactions[f].is_leader_female;
    parse_says(2, MFactions[f].name_leader, -1, -1);
    GenderDefault = MFactions[f].noun_gender;
    PluralDefault = MFactions[f].is_noun_plural;
    parse_says(3, MFactions[f].noun_faction, -1, -1);
    StrBuffer[0] = 0;
    say_year(StrBuffer);
    parse_says(4, StrBuffer, -1, -1);
    parse_says(0, StrBuffer, -1, -1);
    X_pop_2("SCENARIO", "OBJECTIVES", 0);
}

// Console_corner_the_market (0x4E2000): the economic victory's purchase (CORNERMARKET).
void THISCALL Console_corner_the_market(uint8_t* self) {
    int32_t f = CurrentPlayerFaction;
    if (!(GameRules & RULES_VICTORY_ECONOMIC) || !has_tech(Rules.tech_preq_economic_victory, f)) return;
    for (int32_t b = 0; b < BaseCount; b++) {
        if (Bases[b].faction_id != f) continue;
        int32_t off, mask;
        bitmask(1, &off, &mask);
        if (!(Bases[b].facilities_built[off] & (uint8_t)mask)) continue;
        Faction& fa = Factions[f];
        int32_t cost = corner_market(f);
        parse_num(0, cost);
        parse_num(1, game_year(Rules.turns_corner_global_energy_market + CurrentTurn));
        if (CurrentTurn < fa.corner_market_turn) {
            parse_num(0, game_year(fa.corner_market_turn));
            NetMsg_pop(&NetMsg, "CORNERALREADY", 5000, 0, nullptr);
            return;
        }
        if (fa.energy_credits < cost) {
            parse_num(1, fa.energy_credits);
            NetMsg_pop(&NetMsg, "CORNERNOTYET", 5000, 0, nullptr);
            return;
        }
        if (!popp(ScriptFileName, "CORNERMARKET", 0, "econwin_sm.pcx", 0)) return;
        if (diplo_lock(0x32)) return;
        fa.corner_market_turn = Rules.turns_corner_global_energy_market + CurrentTurn;
        fa.corner_market_cost = cost;
        fa.energy_credits -= cost;
        synch_leader(f);
        message_data(0x244F, 0, f, fa.corner_market_turn, cost, 0);
        diplo_unlock();
        Console_update_data(self, 0);
        parse_num(0, game_year(fa.corner_market_turn));
        parse_num(1, fa.corner_market_cost);
        popp(ScriptFileName, "CORNERING", 0, "econwin_sm.pcx", 0);
        return;
    }
    NetMsg_pop(&NetMsg, "CORNERNOHQ", 5000, 0, nullptr);
}

// Console_use_time_bonus (0x517060): multiplayer time controls only (message_data 0x2306).
// Console_use_time_bonus (0x517060, ^ / the commlink panel's pills): a bonus pill asked of the
// server (0x2306, relayed as 0x4306: the turn's time extended); SIMULNO when not our move (one
// player at a time), NOTIMEBONUSES without pills, NOBONUSATEND once the time ran out, NOBONUSDONE
// after the turn ended.
void THISCALL Console_use_time_bonus(uint8_t* self) {
    if (ControlTurnC || !GameTimeControl || !MultiplayerActive) return;
    if ((GameMoreRules & 0x10) && CurrentFaction != CurrentPlayerFaction) {
        NetMsg_pop(&NetMsg, "SIMULNO", 5000, 0, nullptr);
        return;
    }
    if (*reinterpret_cast<int32_t*>(mem::host(0x96EA34 + (uint32_t)CurrentPlayerFaction * 0x20CC)) < 1) {
        parse_num(0, TimeControl[GameTimeControl].refresh);
        NetMsg_pop(&NetMsg, "NOTIMEBONUSES", 5000, 0, nullptr);
        return;
    }
    if (GameState & 0x800) {
        NetMsg_pop(&NetMsg, "NOBONUSATEND", 5000, 0, nullptr);
        return;
    }
    if ((GameState & 2) && (NetTurnDoneMask & (1 << (console_win(self).faction & 31)))) {
        NetMsg_pop(&NetMsg, "NOBONUSDONE", 5000, 0, nullptr);
        return;
    }
    message_data(0x2306, 0, 0, 0, 0, 0);
}

void THISCALL Console_next_cursor(uint8_t* self) {
    console_win(self).cursor_shown = 0;
    if (console_win(self).redraw) {
        console_win(self).redraw = 0;
        pfncRedrawMap(main_win(), 1);
    }
    MapWin_draw_cursor(main_win());
    view_mode(self);
    if (console_win(self).cursor_idx != console_win(self).cursor_head) {
        console_win(self).cursor_idx = (console_win(self).cursor_idx + 1) & 0x1F;
        StatusWin_set_loc(&InfoWin, MapWinCursorX[MapWinCursorIndex], MapWinCursorY[MapWinCursorIndex]);
        MapWin_focus(main_win(), MapWinCursorX[MapWinCursorIndex], MapWinCursorY[MapWinCursorIndex]);
    }
}

void THISCALL Console_menu_proc(uint8_t* self, int32_t id) {
    std::fprintf(stderr, "menu command %03X\n", id);
    if (!ControlTurnStarted) {
        ConsoleMenuPending = 1;
        return;
    }
    if (ControlTurnA) return;
    TutWin_reset(&TutWin);
    go_reset();
    console_win(self).mode = 0;
    for (int32_t i = 0; i < 8; i++) {
        uint8_t* w = MapWins[i];
        if (!w || (i && !map_win(w).active)) continue;
        if (map_win(w).cursor || map_win(w).cursor_id != 0x7F00) {
            ui_set_map_cursor(-1);
            map_win(w).cursor_id = 0x7F00;
            map_win(w).cursor = 0;
        }
        uint32_t o = (uint32_t)map_win(w).options;
        if (o & 0xC40000) {
            map_win(w).options = (int32_t)(o & 0xFF3BFFFF);
            pfncRedrawMap(w, 1);
        }
        do_non_input();
    }
    if (MultiplayerActive) {
        uint32_t menu = ((uint32_t)id >> 8) & 0xFF;
        if ((ControlTurnC || NetLocked || NetSessionState[2]) && menu != 0 && menu != 7 && menu != 8) return;
        log_say_hex("Menu Event", id, 0, 0);
    }
    if (MapWinInVehMode) {
        if (Console_edit_lock(self)) {
            Console_set_view(self, 1);
            return;
        }
        if (MapWinInVehMode && veh(self) < 0 && id != 0x5D) return;
    }
    if (id >= 0x900 && id < 0x913) Console_terraform(self, veh(self), id - 0x900);
    switch (id) {
    // Game
    case 0x51:
        GameMorePreferences |= MPREF_ADV_DETAIL_MAIN_MENUS;
        pfncMainMenuUpdateVisible(main_menu(self), veh(self));
        X_pop("MENUDETAIL", 0);
        break;
    case 0x52:
        GameMorePreferences &= ~MPREF_ADV_DETAIL_MAIN_MENUS;
        pfncMainMenuUpdateVisible(main_menu(self), veh(self));
        X_pop("MENUSIMPLE", 0);
        break;
    case 0x53: prefwin_display(0); break;  // Console_set_preferences
    case 0x54: prefwin_display(2); break;  // Console_set_base_preferences
    case 0x55: prefwin_display(1); break;  // Console_set_adv_preferences
    case 0x56: prefwin_display(3); break;  // Console_set_auto_preferences
    case 0x57: prefwin_display(4); break;  // Console_set_audiovisual
    case 0x58: prefwin_display(5); break;  // Console_set_map_display
    case 0x59: save_game(0); break;
    case 0x5A: Console_load_proc(self, 0, 0); break;
    case 0x5B: Console_retire_game(self); break;
    case 0x5C:
        if (MultiplayerActive) break;
        quit_question(false);
        if (ControlTurnA) ControlTurnB = 1;
        break;
    case 0x5D: quit_question(true); break;
    // HQ
    case 0x103: design_new_veh(CurrentPlayerFaction, -1); break;
    case 0x105: social_select(CurrentPlayerFaction); break;
    case 0x106:
        if (GameRules & RULES_BLIND_RESEARCH) {
            Faction& f = Factions[CurrentPlayerFaction];
            int32_t* p = reinterpret_cast<int32_t*>(reinterpret_cast<uint8_t*>(&f) + 0x3E0);
            DialogChoices = p[0] != 0;
            if (p[1]) DialogChoices |= 2;
            if (p[2]) DialogChoices |= 4;
            if (p[3]) DialogChoices |= 8;
            if (X_pop_6("TECHRANDOM", 1, 0) >= 0) {
                int32_t c = (int8_t)DialogChoices;
                p[0] = c & 1;
                p[1] = (c & 2) >> 1;
                p[2] = (c & 4) >> 2;
                p[3] = (c & 8) >> 3;
                synch_ai(CurrentPlayerFaction);
            }
        }
        break;
    case 0x107:
        if (!(GameRules & RULES_BLIND_RESEARCH)) Console_set_tech(self, CurrentPlayerFaction);
        break;
    case 0x108: help_topic(2, 0); break;
    case 0x109: report_labs(); break;
    case 0x10A: report_energy(); break;
    case 0x10B: report_ops(); break;
    case 0x10C: report_projects(); break;
    case 0x10D: report_sat(); break;
    case 0x10E: report_security(); break;
    case 0x10F: report_score(0); break;
    case 0x110: monument(0); break;
    case 0x111: hall_of_fame(0); break;
    case 0x113:
        if (!MultiWinVisible) MultiWin_show(nullptr);
        else multiwin_hide();  // MultiWin_hide
        break;
    case 0x114: Console_corner_the_market(self); break;
    case 0x115: Console_show_scen_objectives(self); break;
    // Map
    case 0x200:
        if (!Console_edit_lock(self)) Console_set_move(self, 0);
        else NetMsg_pop(&NetMsg, "PROTECTED", 5000, 0, nullptr);
        break;
    case 0x201: view_mode(self); break;
    case 0x203:
    case 0x204:
    case 0x205:
    case 0x206:
    case 0x207:
    case 0x208: pfncProcZoomKey(self, id, 0); break;
    case 0x209:
    case 0x20A:
    case 0x20B:
    case 0x20C: console_toggle_pref(id); break;
    case 0x20D:
        DesktopRedrawFlag = 0;
        desktop_init(1);
        break;
    case 0x20F: Console_survey(self); break;
    case 0x210: console_toggle_pref(id); break;
    case 0x211: Console_name_landmark(self, -1, -1); break;
    case 0x212: Console_erase_landmark(self, -1, -1); break;
    case 0x213: Console_go_to(self, -1, -2, 0); break;
    case 0x214: pfncMoveMap(main_win(), MapWinCursorX[MapWinCursorIndex], MapWinCursorY[MapWinCursorIndex], 1); break;
    case 0x215: {  // the patrol routes
        uint32_t on = (uint32_t)map_win(main_win()).options & 0x400000;
        for (int32_t i = 0; i < 8; i++) {
            uint8_t* w = MapWins[i];
            if (!w || (i && !map_win(w).active)) continue;
            if (!on) {
                map_win(w).options |= 0x400000;
                MapWin_draw_base_dest(w, 1);
            } else {
                map_win(w).options &= ~0x400000;
                pfncRedrawMap(w, 1);
            }
        }
        break;
    }
    case 0x216: {  // the previous cursor position
        console_win(self).cursor_shown = 0;
        if (console_win(self).redraw) {
            console_win(self).redraw = 0;
            pfncRedrawMap(main_win(), 1);
        }
        MapWin_draw_cursor(main_win());
        view_mode(self);
        if (--console_win(self).cursor_idx < 0) console_win(self).cursor_idx = 0x1F;
        StatusWin_set_loc(&InfoWin, MapWinCursorX[MapWinCursorIndex], MapWinCursorY[MapWinCursorIndex]);
        MapWin_focus(main_win(), MapWinCursorX[MapWinCursorIndex], MapWinCursorY[MapWinCursorIndex]);
        break;
    }
    case 0x217: Console_next_cursor(self); break;
    // Action
    case 0x300:
    case 0x301: Console_new_base(self, veh(self)); break;
    case 0x302: Console_oblit(self, veh(self)); break;
    case 0x304: {
        int32_t v = veh(self);
        bool air_attack = Chassis[Units[Vehs[v].unit_id].chassis_id].triad == 2 &&
                          Weapon[Units[Vehs[v].unit_id].weapon_id].offense_value != 0;
        Console_automate(self, v, air_attack ? 0xC : 3);
        break;
    }
    case 0x305: Console_arty(self, veh(self)); break;
    case 0x306: Console_automate(self, veh(self), 10); break;
    case 0x307: Console_automate(self, veh(self), 0xB); break;
    case 0x308: Console_convoy(self, veh(self)); break;
    case 0x309:
    case 0x30A: Console_use_ability(self, veh(self)); break;
    case 0x310: Console_go_to(self, veh(self), -3, 0); break;
    case 0x311: Console_destroy(self, veh(self)); break;
    case 0x312:
        if (!MapWinInVehMode)
            Console_disband_2(self, MapWinCursorX[MapWinCursorIndex], MapWinCursorY[MapWinCursorIndex]);
        else Console_disband(self, veh(self));
        break;
    case 0x313:
        if (!Console_edit_lock(self))
            Console_activate(self, MapWinCursorX[MapWinCursorIndex], MapWinCursorY[MapWinCursorIndex], 1);
        else NetMsg_pop(&NetMsg, "PROTECTED", 5000, 0, nullptr);
        break;
    case 0x314: Console_automate(self, veh(self), 0); break;
    case 0x315: Console_hold(self, veh(self), 0); break;
    case 0x316:
    case 0x317: Console_go_home(self, veh(self)); break;
    case 0x318: Console_go_to(self, veh(self), 0x18, 0); break;
    case 0x319: Console_go_to(self, veh(self), 0x18, 1); break;
    case 0x31A: Console_assemble(self); break;
    case 0x31B: Console_explore(self, veh(self)); break;
    case 0x31C: Console_wait(self, veh(self)); break;
    case 0x31D: Console_unload(self, veh(self)); break;
    case 0x31E: Console_upgrade(self, veh(self)); break;
    case 0x31F: Console_give_veh(self, veh(self)); break;
    case 0x320: Console_designate(self, veh(self)); break;
    case 0x321: Console_sentry(self, veh(self), 0); break;
    case 0x322: Console_sentry(self, veh(self), 10); break;
    case 0x323: Console_skip(self, veh(self)); break;
    case 0x324: Console_set_patrol(self, veh(self)); break;
    case 0x600: Console_editor_on(self); break;
    // Help
    case 0x701: help_topic(2, 0); break;
    case 0x702: help_topic(0, 0); break;
    case 0x703: help_topic(1, 0); break;
    case 0x704: help_topic(0xE, 0); break;
    case 0x705: help_topic(4, 0); break;
    case 0x706: help_topic(6, 0); break;
    case 0x707: help_topic(7, 0); break;
    case 0x708: help_topic(5, 0); break;
    case 0x709: help_topic(8, 0); break;
    case 0x70A: help_topic(3, 0); break;
    case 0x70B: help_topic(10, 0); break;
    case 0x70C: help_topic(0xB, 0); break;
    case 0x70D: help_topic(0xC, 0); break;
    case 0x70E: help_topic(0xF, 0); break;
    case 0x720:
        TutorialTourKind = 0;
        TutWin_tour(&TutWin);
        break;
    case 0x721:
        TutorialTourKind = 3;
        TutWin_tour(&TutWin);
        break;
    case 0x722:
        TutorialTourKind = 1;
        TutWin_tour(&TutWin);
        break;
    case 0x723:
        TutorialTourKind = 5;
        TutWin_tour(&TutWin);
        break;
    case 0x724:
        TutorialTourKind = 7;
        TutWin_tour(&TutWin);
        break;
    case 0x725:
        TutorialTourKind = 6;
        TutWin_tour(&TutWin);
        break;
    case 0x750: show_credits(); break;
    case 0x751:
        parse_says(0, "RELEASE 2.0", -1, -1);  // EngineVersion (0x691870), EngineDate (0x691874)
        parse_says(1, "20-Dec-1999", -1, -1);
        popp(ScriptFileName, "VERSIONCONTROLX", 0, "ac_logo_sm.pcx", 0);
        break;
    // Network
    case 0x800: Console_chat(self, 0); break;
    case 0x801: X_pop("VOICEOVERDATA", 0); break;  // (pop_wait as the expiry function)
    case 0x802: MultiWin_on_button_clicked(&MultiWinObj, 2); break;
    case 0x803: Console_custom_my_faction(self); break;
    case 0x804: Console_time_controls(self); break;
    case 0x805: Console_zoom_to_upkeep_base(self); break;
    case 0x806: Console_use_time_bonus(self); break;
    case 0x807:
    case 0x808: Console_end_my_turn(self); break;
    // Terraform: roads / tubes to, the formers' automation
    case 0x9F0: Console_road_to(self, veh(self), 0x1B); break;
    case 0x9F1: Console_road_to(self, veh(self), 0x1C); break;
    case 0x9F2: Console_automate(self, veh(self), 0); break;
    case 0x9F3: Console_automate(self, veh(self), 1); break;
    case 0x9F4: Console_automate(self, veh(self), 2); break;
    case 0x9F5: Console_automate(self, veh(self), 7); break;
    case 0x9F6: Console_automate(self, veh(self), 6); break;
    case 0x9F7: Console_automate(self, veh(self), 3); break;
    case 0x9F8: Console_automate(self, veh(self), 4); break;
    case 0x9F9: Console_automate(self, veh(self), 5); break;
    default: break;
    }
    // The scenario editor's commands (Scenario and Edit Map menus).
    if (!(GameState & STATE_SCENARIO_EDITOR)) return;
    if ((id >= 0x601 && id <= 0x61B) || id >= 0xA00) {
        if (id == 0x601) {  // omniscient view
            editor_toggle_omniscient();
            return;
        }
        if (id == 0x604) {
            tech_advance(CurrentPlayerFaction);
            return;
        }
        if (id == 0x60D) {
            show_replay();
            return;
        }
        if (id == 0x61A) {
            Console_load_proc(self, 1, 0);
            return;
        }
        if (id == 0x61B) {
            save_game(1);
            return;
        }
        if (id == 0x603) {
            Console_editor_edit_veh(self, -1);
            return;
        }
        if (id == 0xA20 || id == 0xA21) {
            MapWin_editor(main_win(), MapWinCursorX[MapWinCursorIndex], MapWinCursorY[MapWinCursorIndex], id - 0xA20);
            return;
        }
        if (id == 0x614) {
            Console_editor_rules(self);
            return;
        }
        switch (id) {  // editor.cpp
        case 0x602: Console_editor_veh(); return;
        case 0x605: Console_editor_tech(); return;
        case 0x606: Console_editor_view(); return;
        case 0x607: Console_editor_diff(); return;
        case 0x60A: Console_editor_eliminate(); return;
        case 0x60B: Console_editor_reload(); return;
        case 0x60E: Console_show_movies(self); return;
        case 0x60F: Console_editor_reset_tech(self); return;
        case 0x610: Console_editor_reset_faction(); return;
        case 0x611: Console_editor_diplomacy(); return;
        case 0x612: Console_editor_personality(); return;
        case 0x613: Console_editor_strategy(); return;
        case 0x615: Console_editor_scen_rules(); return;
        case 0x616: Console_editor_scen_param(); return;
        case 0x617: Console_editor_scen_victory(); return;
        case 0xA17: Console_editor_natural(1); return;
        case 0xA18: Console_editor_natural(0); return;
        case 0x608: Console_editor_energy(); return;
        case 0x609: Console_editor_date(); return;
        case 0x60C: Console_editor_kill_vehicles(); return;
        case 0x618: Console_editor_undo(); return;
        case 0x619: Console_editor_redo(); return;
        case 0xA00: Console_editor_save(); return;
        case 0xA01: Console_editor_load(); return;
        case 0xA10:
        case 0xA11:
        case 0xA12:
        case 0xA13:
        case 0xA14: Console_editor_paints(id - 0xA10); return;
        case 0xA15: Console_editor_terrain(); return;
        case 0xA16: Console_editor_paints(5); return;
        case 0xA19: Console_editor_minor(); return;
        case 0xA24: Console_editor_set_brush(); return;
        case 0xA30: Console_editor_climate(); return;
        case 0xA31: Console_editor_generate(); return;
        case 0xA32: Console_editor_fast(); return;
        case 0xA36: Console_editor_fungus(self); return;
        case 0xA37: Console_editor_rockiness(); return;
        case 0xA40: Console_editor_randomize(); return;
        case 0xA41: Console_editor_clear_random(); return;
        case 0xA48: Console_editor_flat(); return;
        case 0xA50: Console_editor_clear(); return;
        case 0xA51: Console_editor_polar(); return;
        case 0xA52: Console_editor_clear_vehicles(); return;
        case 0xA53: Console_editor_clear_terrain(); return;
        case 0xA54: Console_editor_lock2(); return;
        default: break;
        }
        // Other ids: nothing (the original's switch defaults).
    }
}
