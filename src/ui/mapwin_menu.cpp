// The map's right-click menu (MapWin_right_menu, 0x46C0D0) and the landmark commands it offers
// (Console_name_landmark 0x5162A0, Console_erase_landmark 0x516490). Reconstructed from the asm:
// the items and their conditions, the disabled items when it is not the player's turn, the
// commands (the selection is ignored when the tile's base / unit or the active unit changed while
// the menu was open). Multiplayer: the network locks of the original (NetDaemon) are not
// reconstructed; with MultiplayerActive the commands that need them do nothing.
#include <cstdio>
#include <cstring>
#include <string>

#include "engine/base.h"
#include "engine/globals.h"
#include "engine/map.h"
#include "engine/parse.h"
#include "engine/stack.h"
#include "engine/strings.h"
#include "engine/ui_bridge.h"
#include "engine/veh.h"
#include "engine/action.h"
#include "engine/probe.h"
#include "ui/basewin.h"
#include "ui/basewin_internal.h"
#include "ui/console.h"
#include "ui/console_frame.h"
#include "ui/help.h"
#include "ui/mapwin.h"
#include "ui/pulldown.h"
#include "ui/win.h"

namespace {

char g_str[512];  // StrBuffer use of the original ("  " + label)

const char* indented(int32_t label_off, const char* tail = nullptr) {
    std::snprintf(g_str, sizeof(g_str), " %s%s%s", label_get(label_off / 4), tail ? " " : "", tail ? tail : "");
    return g_str;
}

int32_t map_dist(int32_t dx, int32_t dy) {  // the x distance wraps on round maps
    int32_t ax = dx < 0 ? -dx : dx;
    if (!(MapToggleFlat & 1) && ax > MapHalfX) ax = MapAreaX - ax;
    int32_t ay = dy < 0 ? -dy : dy;
    return ax + ay;
}

bool is_air(int32_t veh_id) { return Chassis[Units[Vehs[veh_id].unit_id].chassis_id].triad == 2; }

}  // namespace

void THISCALL MapWin_right_menu(uint8_t* win, int32_t x, int32_t y, int32_t px, int32_t py) {
    PullDown pd;
    // Win_client_to_screen_2: the map views are at the screen's origin in smacx.
    int32_t base_id = base_at(x, y);
    int32_t veh_id = veh_at(x, y);
    int32_t active = MapWinVehA;
    PullDown_init(&pd);
    if (map_win(win).index == 1) {
        // The world map window's menu.
        PullDown_add_item(&pd, 800, label_get(0xC04 / 4));
        PullDown_add_separator(&pd, 99);
        PullDown_add_item(&pd, 0x323, label_get(0xC68 / 4));
        PullDown_add_item(&pd, 0x324, label_get(0xC6C / 4));
        PullDown_add_item(&pd, 0x325, label_get(0xC70 / 4));
        PullDown_add_separator(&pd, 99);
        PullDown_add_item(&pd, 0x321, label_get(0x910 / 4));
        PullDown_add_item(&pd, 0x322, label_get(0x914 / 4));
        PullDown_show(&pd, px, py);
        if (GameState & 0x800) return;
        int32_t sel = PullDown_get_selected(&pd);
        int32_t id = sel < 0x40 ? pulldown_item_id(&pd, sel) : -1;
        if (base_id != base_at(x, y) || veh_id != veh_at(x, y) || active != MapWinVehA) return;
        int32_t zoom = map_win(win).zoom;
        switch (id) {
        case 800:  // WorldWin_set_center / set_world_map, button 0x3EE (the world map pane)
            ui_invalidate();
            break;
        case 0x321:
        case 0x322: pfncMoveMap(MapWins[0], x, y, 1); break;
        case 0x323:
            if (zoom < 0) map_win(win).zoom = zoom + 2, ui_invalidate_map();
            break;
        case 0x324:
            if (zoom >= -0xD) map_win(win).zoom = zoom - 2, ui_invalidate_map();
            break;
        case 0x325:
            if (zoom != -8) map_win(win).zoom = -8, ui_invalidate_map();
            break;
        default: break;
        }
        return;
    }
    if (MapWinInVehMode && is_air(active)) PullDown_add_item(&pd, 0x6E, label_get(0xB18 / 4));
    PullDown_add_item(&pd, 2, label_get(0x8D4 / 4));
    if ((GameState & STATE_SCENARIO_EDITOR) && veh_id >= 0) {
        PullDown_add_item(&pd, 0x29A, label_get(0xDFC / 4));
        PullDown_add_item(&pd, 0x29B, label_get(0x104C / 4));
    }
    if (base_id >= 0 && veh_id >= 0 && stack_check(veh_id, 10, CurrentPlayerFaction, -1, -1) &&
        !Console_edit_lock(&MainConsole)) {
        int32_t n = stack_check(veh_id, 1, -1, -1, -1);
        PullDown_add_item(&pd, 6, label_get((n == 1 ? 0x8E0 : 0x8E4) / 4));
    }
    PullDown_add_separator(&pd, 99);
    if (base_id >= 0) {
        int32_t owner = Bases[base_id].faction_id;
        if ((GameState & STATE_OMNISCIENT_VIEW) || owner == CurrentPlayerFaction || spying(owner) ||
            (Factions[CurrentPlayerFaction].diplo_status[owner] & 1))
            PullDown_add_item(&pd, 5, label_get(0x894 / 4));
        if (owner == CurrentPlayerFaction || (GameState & STATE_OMNISCIENT_VIEW)) {
            PullDown_add_item(&pd, 0x33, indented(0x898));
            int32_t item = Bases[base_id].queue_items[0];
            const char* prod = item < 0 ? Strings_get(&StringTable, Facility[-item].name) : Units[item].name;
            std::snprintf(g_str, sizeof(g_str), " %s %s", label_get(0x89C / 4), prod);
            PullDown_add_item(&pd, 0x34, g_str);
            PullDown_add_item(&pd, 0x35, indented(0x8A0));
            PullDown_add_item(&pd, 0x36, indented(0x8A4));
            PullDown_add_item(&pd, 0x37, indented(0x8A8));
            PullDown_add_item(&pd, 0x38, indented(0x8AC));
            if (GameMorePreferences & MPREF_ADV_DETAIL_RIGHT_CLICK_MENUS) PullDown_add_item(&pd, 0x39, indented(0x8B0));
            PullDown_add_separator(&pd, 99);
        }
    }
    if (MapWinInVehMode) {
        PullDown_add_item(&pd, 1, label_get(0x8B4 / 4));
        const VEH& v = Vehs[active];
        if (x != v.x || y != v.y) {
            if (GameMorePreferences & MPREF_ADV_DETAIL_RIGHT_CLICK_MENUS) {
                PullDown_add_item(&pd, 0x69, label_get(0x8B8 / 4));
                if (is_air(active) && Weapon[Units[v.unit_id].weapon_id].offense_value != 0)
                    PullDown_add_item(&pd, 0x6A, label_get(0x8BC / 4));
            }
            if (has_abil(v.unit_id, 0x10)) PullDown_add_item(&pd, 0x67, label_get(0x8C0 / 4));
            if (can_arty(v.unit_id, 1) && map_dist(x - v.x, y - v.y) >> 1 <= Rules.artillery_max_rng)
                PullDown_add_item(&pd, 0x68, label_get(0x8C8 / 4));
        }
        if ((map_dist(x - v.x, y - v.y) & ~1) < 3) {
            PullDown_add_item(&pd, 0x65, label_get(0x8CC / 4));
            if (Units[v.unit_id].plan == 9) PullDown_add_item(&pd, 0x66, label_get(0x8D0 / 4));
        }
        PullDown_add_item(&pd, 199, label_get(0xE1C / 4));
        PullDown_add_item(&pd, 200, label_get(0x1098 / 4));
        PullDown_add_separator(&pd, 99);
    }
    if (MapWinInVehMode) PullDown_add_item(&pd, 4, label_get(0x8D8 / 4));
    else if (!Console_edit_lock(&MainConsole)) PullDown_add_item(&pd, 3, label_get(0x8DC / 4));
    if (base_id < 0 && veh_id >= 0 && stack_check(veh_id, 10, CurrentPlayerFaction, -1, -1) &&
        !Console_edit_lock(&MainConsole)) {
        int32_t n = stack_check(veh_id, 1, -1, -1, -1);
        PullDown_add_item(&pd, 6, label_get((n == 1 ? 0x8E0 : 0x8E4) / 4));
    }
    PullDown_add_item(&pd, 8, label_get(0x8E8 / 4));
    if (GameMorePreferences & MPREF_ADV_DETAIL_RIGHT_CLICK_MENUS) {
        PullDown_add_separator(&pd, 99);
        if (find_landmark(x, y, 1) < 0) {
            PullDown_add_item(&pd, 9, label_get(0x8F4 / 4));
        } else {
            PullDown_add_item(&pd, 9, label_get(0x8EC / 4));
            PullDown_add_item(&pd, 0xB, label_get(0x8F0 / 4));
        }
    }
    PullDown_add_separator(&pd, 99);
    PullDown_add_item(&pd, 7, label_get(0x8F8 / 4));
    PullDown_add_item(&pd, 10, label_get(0x8FC / 4));
    if (ControlTurnC) {
        for (int32_t id : {1, 3, 4, 5, 8, 9, 0xB}) PullDown_disable_item(&pd, id);
        for (int32_t id = 0x33; id < 0x3A; id++) PullDown_disable_item(&pd, id);
        for (int32_t id = 100; id < 0x6F; id++) PullDown_disable_item(&pd, id);
    }
    PullDown_show(&pd, px, py);
    if (GameState & 0x800) return;
    int32_t sel = PullDown_get_selected(&pd);
    int32_t id = sel < 0x40 ? pulldown_item_id(&pd, sel) : -1;
    if (base_id != base_at(x, y) || veh_id != veh_at(x, y) || active != MapWinVehA) return;
    // Multiplayer: not the player's turn / input pending (bVar2) or the turn ended (uVar8).
    bool locked = false;
    if (MultiplayerActive) {
        locked =
            ControlTurnC || NetLocked || NetInputPending || ((1u << CurrentPlayerFaction) & (uint32_t)NetTurnDoneMask);
        base_id = base_at(x, y);
        veh_id = veh_at(x, y);
        active = MapWinVehA;
    }
    int32_t mx, my;
    ui_mouse_pos(&mx, &my);
    switch (id) {
    case 0x33:
        if (!locked) {
            BaseWin_soft_zoom(&BaseWin, base_id);
            BaseWin_production(&BaseWin, 0, 0);
        }
        break;
    case 1:  // move the unit here: go-to order
        if (!locked && !net_lock_veh(&active, 0)) {
            VEH& v = Vehs[active];
            v.order = 0x18;
            v.waypoint_x[0] = (int16_t)x;
            v.waypoint_y[0] = (int16_t)y;
            v.state &= ~0x10000u;
            ui_synch_veh(active);
            net_unlock_veh();
        }
        break;
    case 2:
        Console_set_view(&MainConsole, 0);
        Console_set_cursor(&MainConsole, x, y);
        pfncMoveMap(win, x, y, 1);
        break;
    case 3:
        if (!locked) {
            Console_set_move(&MainConsole, 0);
            if (active < 0) {  // the unit active when the menu opened
                GameState |= 2;
                MapWinField23BE4 = 0;
            }
        }
        break;
    case 4: pfncMoveMap(&MainConsole, Vehs[active].x, Vehs[active].y, 1); break;
    case 5: BaseWin_zoom(&BaseWin, base_id, 0); break;
    case 6:
        if (veh_id >= 0 && stack_check(veh_id, 10, CurrentPlayerFaction, -1, -1))
            Console_activate(&MainConsole, x, y, 0);
        break;
    case 7:
        StatusWin_set_loc(&InfoWin, x, y);
        StatusWin_redraw(&InfoWin);
        InfoWin_set_state(&InfoWindow, -1, 0);
        break;
    case 8:
        if (!locked) {
            bool end = (GameState & 2) != 0;
            if (!end) {
                Console_set_move(&MainConsole, 1);
                end = !MapWinInVehMode || MapWinVehA < 0 || X_pop("REALLYOVER", 0);
            }
            if (end) {
                GameState |= 2;
                MapWinField23BE4 = 0;
                MapWinField23BE8 = 0;
            }
        }
        break;
    case 9:
        if (!locked) Console_name_landmark(&MainConsole, x, y);
        break;
    case 10: help_any(); break;
    case 0xB:
        if (!locked) Console_erase_landmark(&MainConsole, x, y);
        break;
    case 0x65:  // Action...: the Action menu's pull-down at the mouse
        console_menu_popup(3, mx, my);
        break;
    case 0x66:  // Terraform...
        console_menu_popup(9, mx, my);
        break;
    case 0x34:
        if (!locked) {
            BaseWin_soft_zoom(&BaseWin, base_id);
            BaseWin_hurry(&BaseWin);
        }
        break;
    case 0x35:
    case 0x36:
    case 0x37:
    case 0x38:
        if (!locked) {
            BaseWin_soft_zoom(&BaseWin, base_id);
            BaseWin_gov_on(&BaseWin, 0x1000000u << (id - 0x35));
        }
        break;
    case 0x39:
        if (!locked) Console_go_to(&MainConsole, base_id, -1, 0);
        break;
    case 0x6E:
        if (!locked) Console_go_home(&MainConsole, active);
        break;
    case 0x67:
        if (!locked) action_airdrop(active, x, y, 3);  // NetDaemon_veh_drop without multiplayer
        break;
    case 0x68:
        if (!locked) action_arty(active, x, y);
        break;
    case 0x69:
        if (!locked) action_patrol(active, x, y);
        break;
    case 0x6A:
        if (!locked) Console_set_bombing_run(&MainConsole, active, x, y);
        break;
    case 0x29A:
        if (!locked) Console_editor_edit_veh(&MainConsole, veh_id);
        break;
    case 199:
        if (!locked) Console_disband(&MainConsole, active);
        break;
    case 200:
        if (!locked) Console_destroy(&MainConsole, active);
        break;
    case 0x29B:
        if (!locked) Console_editor_kill_veh(&MainConsole, veh_id);
        break;
    default: break;
    }
}

// Console_name_landmark (0x5162A0): names the landmark at (x, y) (the cursor when off the map), or
// renames the one there.
void THISCALL Console_name_landmark(uint8_t* console, int32_t x, int32_t y) {
    if (y < 0 || y >= MapAreaY || x < 0 || x >= MapAreaX) {
        x = MapWinCursorX[MapWinCursorIndex];
        y = MapWinCursorY[MapWinCursorIndex];
    }
    int32_t lm = find_landmark(x, y, 1);
    if (!(GameState & STATE_OMNISCIENT_VIEW)) {
        int32_t lx = x, ly = y;
        if (lm >= 0) lx = Landmarks[lm].x, ly = Landmarks[lm].y;
        if (!valid_landmark(lx, ly, CurrentPlayerFaction)) {
            NetMsg_pop(&NetMsg, "LANDMARKNO", 5000, 0, nullptr);
            return;
        }
    }
    if (lm < 0) {
        if (X_pop_ask("LANDMARK", 0x1F, reinterpret_cast<const char*>(mem::host(0x939FEC)), 0, 0)) return;
        purge_spaces(ParseStrBuffer[0]);
        if (!std::strlen(ParseStrBuffer[0])) return;
        if (MultiplayerActive) {
            console_not_reconstructed("message_landmark");
            return;
        }
        if (new_landmark(x, y, ParseStrBuffer[0]) < 0) {
            parse_num(0, 0x40);
            NetMsg_pop(&NetMsg, "LANDMARKTOOMANY", 5000, 0, nullptr);
        }
        draw_map(1);
    } else {
        if (X_pop_ask("LANDMARK", 0x1F, Landmarks[lm].name, 0, 0)) return;
        purge_spaces(ParseStrBuffer[0]);
        if (!std::strlen(ParseStrBuffer[0])) return;
        if (MultiplayerActive) {
            console_not_reconstructed("message_landmark");
            return;
        }
        std::strncpy(Landmarks[lm].name, ParseStrBuffer[0], sizeof(Landmarks[lm].name) - 1);
        Landmarks[lm].name[sizeof(Landmarks[lm].name) - 1] = 0;
        draw_map(1);
    }
}

// Console_erase_landmark (0x516490).
void THISCALL Console_erase_landmark(uint8_t* console, int32_t x, int32_t y) {
    if (y < 0 || y >= MapAreaY || x < 0 || x >= MapAreaX) {
        x = MapWinCursorX[MapWinCursorIndex];
        y = MapWinCursorY[MapWinCursorIndex];
    }
    int32_t lm = find_landmark(x, y, 1);
    if (lm < 0) {
        NetMsg_pop(&NetMsg, "LANDMARKNONE", 5000, 0, nullptr);
        return;
    }
    if (!(GameState & STATE_OMNISCIENT_VIEW) && !valid_landmark(x, y, CurrentPlayerFaction)) {
        NetMsg_pop(&NetMsg, "LANDMARKNO", 5000, 0, nullptr);
        return;
    }
    parse_says(0, Landmarks[lm].name, -1, -1);
    if (!X_pop("LANDMARKKILL", 0)) return;
    if (MultiplayerActive) {
        console_not_reconstructed("message_data (landmark)");
        return;
    }
    kill_landmark(Landmarks[lm].x, Landmarks[lm].y);
    draw_map(1);
}
