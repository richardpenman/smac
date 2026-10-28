// The base window's build menu and queue / unit panes (basewin.h): BaseWin_production,
// BaseWin_queue_click, BaseWin_garrison_click.
//
// BaseWin_production follows the original (0x417040) after the item is chosen: the queue counters
// (Faction units_queue), the retool penalty popup (RETOOLPENALTYn[A]), the queue insertion modes,
// the production change bookkeeping (state/governor flags, log, wave, draw_radius, waking the
// sentried formers / colony pods at the base), the redraw and the "Ascent" interlude, with the
// picker (prodwin.cpp: ProdWin_exec (0x494700)) and its modes (help, workshop, reset, queue delete / insert /
// replace; the picker stays open while a queue slot is edited).
#include <cstdio>
#include <cstring>
#include "engine/action.h"
#include "engine/base.h"
#include "engine/crt.h"
#include "engine/globals.h"
#include "engine/log.h"
#include "engine/net.h"
#include "engine/parse.h"
#include "engine/proto.h"
#include "engine/rules.h"
#include "engine/strings.h"
#include "engine/tech.h"
#include "engine/turn.h"
#include "engine/ui_bridge.h"
#include "engine/veh.h"
#include "ui/basewin_internal.h"
#include "ui/pulldown.h"
#include "ui/reports.h"
#include "ui/console.h"
#include "engine/veh.h"
#include "engine/map.h"
#include "engine/base.h"
#include "ui/console.h"
#include "ui/popup.h"
#include "ui/win.h"

namespace {

void queue_count(int32_t faction, int32_t delta) {
    BASE& b = cur_base();
    for (int32_t i = 0; i <= b.queue_size; i++)
        if (b.queue_items[i] >= 0) Factions[faction].units_queue[b.queue_items[i]] += (uint8_t)delta;
}

void refresh_base(uint8_t* self) {
    int32_t was = cur_base().worked_tiles;
    set_base(base_win().base);
    base_compute(0);
    if (MultiplayerActive && !ControlTurnC && was != Bases[base_win().base].worked_tiles &&
        !bw_lock_base(CurrentBaseID))
        bw_unlock_base(CurrentBaseID);
}

}  // namespace

void THISCALL BaseWin_production(uint8_t* self, int32_t slot, int32_t no_lock) {
    set_base(base_win().base);
    base_compute(0);
    if (Bases[base_win().base].faction_id != CurrentPlayerFaction && !(GameState & STATE_OMNISCIENT_VIEW)) return;
    BASE& b = cur_base();
    int32_t saved_size = b.queue_size, saved[10];
    for (int32_t i = 0; i < 10; i++) saved[i] = b.queue_items[i];
    base_win().prod_at_open = b.production_id_last;
    int32_t faction = b.faction_id;
    if (slot == 0) b.state_flags &= ~0x800000;
    if (!no_lock && bw_lock_base(CurrentBaseID)) return;
    queue_count(faction, -1);
    for (;;) {
        refresh_base(self);
        // ProdWin_exec (0x494700): the picker (BaseWin's member at +0xA1C) for the selected queue slot.
        int32_t mode = 0;
        int32_t item = prodwin_pick(faction, base_win().queue_sel, &mode);
        if (slot != 0 && item == -9999) {
            for (int32_t i = 0; i < 10; i++) cur_base().queue_items[i] = saved[i];
            cur_base().queue_size = saved_size;
            BaseWin_draw_production(self, 0);
        }
        refresh_base(self);
        FX_play(&Sounds, 0xF);
        BASE& cb = cur_base();
        if (item < -0x270E || (GameState & 0x800)) {
            base_win().queue_sel = 0;
            BaseWin_draw_queue(self, 1);
            if (!no_lock) bw_unlock_base(CurrentBaseID);
            queue_count(faction, 1);
            prodwin_hide();
            return;
        }
        // The picker's modes (+0xB194).
        bool next = false;
        switch (mode) {
        case 1:  // HELP: the facility's / project's datalink entry, the unit's design
            next = true;
            if (item < 0) {
                help_topic(-item < 0x46 ? 10 : 11, -item);
            } else if (item != 9999) {
                if (item < 0x200) DesignWin_exec(&DesignWin, (uint8_t)cb.faction_id, item);
                else help_topic(item - 0x200 < 0x46 ? 10 : 11, item - 0x200);
            }
            break;
        case 2:  // WORKSHOP (the selected unit's design) / 3 a new design; then the list again
        case 3:
            DesignWin_exec(&DesignWin, (uint8_t)cb.faction_id, mode == 2 && item >= 0 ? item : -1);
            refresh_base(self);
            prodwin_rebuild();
            next = true;
            break;
        case 4:  // DELETE (queue) / RESET (production: the governor chooses)
        case 6:
            if (slot == 0) {
                base_reset(CurrentBaseID, 0);
                if (!no_lock) bw_unlock_base(CurrentBaseID);
                BaseWin_on_redraw(self);
                GraphicWin_redraw(self);
                queue_count(faction, 1);
                prodwin_hide();
                return;
            }
            if (cb.queue_size < base_win().queue_sel) {
                base_win().queue_sel = std::max(base_win().queue_sel - 1, 1);
                BaseWin_draw_queue(self, 1);
            } else {
                for (int32_t i = base_win().queue_sel; i < 9; i++) cb.queue_items[i] = cb.queue_items[i + 1];
                cb.queue_size--;
                if (cb.queue_size < base_win().queue_sel) base_win().queue_sel = std::max(cb.queue_size, 1);
                BaseWin_draw_queue(self, 1);
                BaseWin_draw_queue(self, 1);
            }
            break;
        case 5:  // INSERT
            if (slot != 0) BaseWin_add_queue(self, item, 1);
            break;
        case 7:  // REPLACE
            if (slot != 0) BaseWin_add_queue(self, item, 0);
            break;
        default: break;
        }
        if (next) continue;
        if (item == 9999) {
            // The old design workshop (design_workshop: the picker never returns 9999, so this is
            // unreachable as in the original), then the list again.
            design_workshop(cb.faction_id, 1);
            refresh_base(self);
            prodwin_rebuild();
            continue;
        }
        if (item > 0x1FF) item = 0x200 - item;
        bool retool = slot == 0 && item == cb.queue_items[0];
        if (!retool) {
            cb.state_flags |= 0x80000000;
            if (cb.governor_flags & 0x40000000) cb.governor_flags &= 0x3FFFFFFF;
            retool = slot == 0;
        }
        if (retool && cb.minerals_accumulated_2 != 0) {
            int32_t open = base_win().prod_at_open;
            if (base_making(item, CurrentBaseID) == base_making(open, CurrentBaseID) ||
                Factions[faction].diff_level < 2 || Rules.retool_penalty_prod_change == 0 ||
                cb.minerals_accumulated_2 <= Rules.retool_exemption || open == -0x45) {
                cb.minerals_accumulated = cb.minerals_accumulated_2;
            } else if (base_making(cb.queue_items[0], CurrentBaseID) == base_making(open, CurrentBaseID)) {
                int32_t q0 = cb.queue_items[0];
                if (q0 < 0) parse_say(0, Facility[-q0].name, -1, -1);
                else parse_says(0, Units[q0].name, -1, -1);
                if (item < 0) parse_say(1, Facility[-item].name, -1, -1);
                else parse_says(1, Units[item].name, -1, -1);
                int32_t kept = (cb.minerals_accumulated_2 - Rules.retool_exemption) *
                                   (100 - Rules.retool_penalty_prod_change) / 100 +
                               Rules.retool_exemption;
                parse_num(0, Rules.retool_penalty_prod_change);
                parse_num(1, Rules.retool_exemption);
                parse_num(2, cb.minerals_accumulated - kept);
                char label[32], num[12];
                StrBuffer[0] = 0;
                crt_strcat(StrBuffer, "RETOOLPENALTY");
                snprintf(num, sizeof num, "%d", Rules.retool_strictness);
                crt_strcat(StrBuffer, num);
                if (Rules.retool_exemption) crt_strcat(StrBuffer, "A");
                snprintf(label, sizeof label, "%s", StrBuffer);
                BasePop* pop = popup_new();
                Popup_start(pop, ScriptFileName, label, -1, nullptr, 0);
                if (cb.queue_size == 0) BasePop_item(pop, label_get(0xB0C / 4), 2);
                BasePop_item(pop, label_get(0xB10 / 4), 3);
                int32_t r = BasePop_exec_3(pop, 0, 0);
                popup_delete(pop);
                switch (r) {
                case 0:
                    if (!no_lock) bw_unlock_base(CurrentBaseID);
                    queue_count(faction, 1);
                    return;
                case 1: cb.minerals_accumulated = kept; break;
                case 2: {
                    for (int32_t j = 9; j > 1; j--) cb.queue_items[j] = cb.queue_items[j - 1];
                    int32_t n = cb.queue_size + 1;
                    cb.queue_size = n < 0 ? 0 : n > 9 ? 9 : n;
                    slot = 1;
                    cb.queue_items[1] = item;
                    break;
                }
                case 3:
                    if (cb.queue_size < 9) cb.queue_size++;
                    slot = cb.queue_size;
                    cb.queue_items[slot] = item;
                    break;
                }
            }
        }
        log_say_values("BASE: changed production", CurrentBaseID, slot, cb.queue_items[slot]);
        if (slot == 0) {
            cb.queue_items[0] = item;
            if (cb.queue_items[0] < -0x45) wave_it(0x24);
            draw_radius(cb.x, cb.y, 2, 2);
            int32_t it = cb.queue_items[0];
            if (it < -0x45 || (it > 0x40 && !(Units[it].unit_flags & 4))) {
                for (int32_t v = veh_at(cb.x, cb.y); v >= 0; v = Vehs[v].next_veh_id_stack) {
                    VEH& vh = Vehs[v];
                    int8_t plan = Units[vh.unit_id].plan;
                    if (vh.faction_id == (uint8_t)cb.faction_id && (plan == 12 || plan == 10)) {
                        vh.order = 0;
                        vh.state &= ~0x200u;
                        if (MultiplayerActive) synch_veh(v);
                    }
                }
            }
        }
        // The picker stays open while a queue is edited: back to it.
        if (prodwin_shown()) continue;
        if (!no_lock) bw_unlock_base(CurrentBaseID);
        queue_count(faction, 1);
        // (Multiplayer: message_data for a project started or dropped, not reconstructed.)
        base_win().queue_sel = 0;
        base_compute(1);
        BaseWin_on_redraw(self);
        GraphicWin_redraw(self);
        draw_radius(cur_base().x, cur_base().y, 1, 2);
        if (MapWinCursorX[MapWinCursorIndex] == cur_base().x && MapWinCursorY[MapWinCursorIndex] == cur_base().y)
            Console_update_data(&MainConsole, 0);
        if (slot == 0 && cur_base().queue_items[0] == -0x65) {
            interlude(10, 0, 1, 0);
            GameInterludeState |= 0x800;
        }
        return;
    }
}

// The queue pane (iface spot 8, index = slot + 2): left click picks the item for that slot; a
// right click removes it.
void THISCALL BaseWin_queue_click(uint8_t* self, int32_t index, int32_t, int32_t, int32_t right) {
    int32_t s = index - 2;
    BASE& b = cur_base();
    if (Bases[base_win().base].faction_id != CurrentPlayerFaction && !(GameState & STATE_OMNISCIENT_VIEW)) return;
    if (s < 1) s = 1;
    if (s > b.queue_size + 1) s = b.queue_size + 1;
    if (s > 9) return;
    if (right) {
        if (s <= b.queue_size) BaseWin_delete_queue(self, s);
        return;
    }
    base_win().queue_sel = s;
    BaseWin_draw_queue(self, 1);
    BaseWin_production(self, s, 0);
}

// BaseWin_garrison_click (0x40B140): a click on a unit of the units present (spot 6) or supported
// (spot 7) panes. With the "quick select" preference (0x40000) a left click activates the unit
// and closes the window; otherwise its menu (PullDown): activate (sentry / held / patrolling:
// clear the order), select, sentry, hold, disband, change home base / show the home base, return
// to the home base, centre the map on it. Multiplayer locks: not reconstructed (none in
// single-player games).
void THISCALL BaseWin_garrison_click(uint8_t* self, int32_t veh_id, int32_t x, int32_t y, int32_t right) {
    if (Vehs[veh_id].faction_id != CurrentPlayerFaction ||
        (MultiplayerActive && (NetSessionState[0x1F] || NetLocked || ControlTurnC)))
        return;
    // Selects the unit, closes the window (or the report) and recentres the view.
    auto select = [&](bool clear) {
        MapWinVehA = veh_id;
        if (clear) {
            Vehs[veh_id].order = 0;
            Vehs[veh_id].state &= 0xFCFFBDFFu;
            ui_synch_veh(veh_id);
            net_await_synch();
        }
        Console_set_move(&MainConsole, 1);
        console_win(&MainConsole).any_select = 1;
        if (Win_is_visible(&BaseWin)) bw_close_window(self);
        else if (Win_is_visible(&ReportWindow)) ReportIf_done(&ReportIfObj);
    };
    if (!right && (GameMorePreferences & MPREF_ADV_CLICK_VEH_CANCELS_ORDERS)) {
        if (!veh_selectable(veh_id) || net_lock_veh(&veh_id, 0)) return;
        select(true);
        net_unlock_veh();
        return;
    }
    PullDown pd;
    PullDown_init(&pd);
    VEH& v = Vehs[veh_id];
    uint32_t st = v.state;
    if (v.order || (st & 0x4200) || (st & 0x1000200) == 0x1000200 ||
        ((st & 0x2000200) == 0x2000200 && v.patrol_current_point < v.waypoint_count))
        PullDown_add_item(&pd, 1, label_get(0xB6C / 4));
    PullDown_add_item(&pd, 2, label_get(0x958 / 4));
    if (GameState & STATE_SCENARIO_EDITOR) PullDown_add_item(&pd, 0x5A, label_get(0xDFC / 4));
    PullDown_add_separator(&pd, 99);
    PullDown_add_item(&pd, 10, label_get(0xB70 / 4));
    PullDown_add_item(&pd, 0xB, label_get(0xB74 / 4));
    PullDown_add_item(&pd, 0xC, label_get(0xB78 / 4));
    int32_t home = v.home_base_id;
    if (home >= 0 && home != CurrentBaseID) {
        PullDown_add_separator(&pd, 99);
        if (Bases[CurrentBaseID].faction_id == v.faction_id) PullDown_add_item(&pd, 0x14, label_get(0xB7C / 4));
        PullDown_add_item(&pd, 0x15, label_get(0xB80 / 4));
    }
    if (home == CurrentBaseID && (v.x != Bases[CurrentBaseID].x || v.y != Bases[CurrentBaseID].y)) {
        if (map_tile(v.x, v.y)->region == map_tile(Bases[CurrentBaseID].x, Bases[CurrentBaseID].y)->region ||
            Chassis[Units[v.unit_id].chassis_id].triad != 0) {
            PullDown_add_separator(&pd, 99);
            PullDown_add_item(&pd, 0x1E, label_get(0xB84 / 4));
        }
        PullDown_add_item(&pd, 0x1F, label_get(0x910 / 4));
    }
    int32_t shown = veh_id;
    // Win_client_to_screen_2: the panes' coordinates are the main interface's.
    const UiRect& mp = main_interface().pos;
    PullDown_show(&pd, x + mp.left, y + mp.top);
    if (GameState & 0x800) return;
    int32_t sel = PullDown_get_selected(&pd);
    int32_t id = sel < 0x40 ? pulldown_item_id(&pd, sel) : -1;
    if (veh_id != shown || net_lock_veh(&veh_id, 0)) return;
    VEH& u = Vehs[veh_id];
    if (u.faction_id != CurrentPlayerFaction) {
        net_unlock_veh();
        return;
    }
    auto jail = [&]() {
        if (!veh_jail(veh_id)) return false;
        X_pop("JAIL", 0);
        return true;
    };
    switch (id) {
    case 1:
        if (jail()) break;
        u.order = 0;
        ui_synch_veh(veh_id);
        net_await_synch();
        break;
    case 2:
        if (jail()) break;
        select(true);
        net_unlock_veh();
        return;
    case 10:
        if (jail()) break;
        sleep(veh_id);
        ui_synch_veh(veh_id);
        net_await_synch();
        break;
    case 0xB:
        if (jail()) break;
        u.order = 2;
        ui_synch_veh(veh_id);
        net_await_synch();
        break;
    case 0xC:
        net_unlock_veh();
        Console_disband(&MainConsole, veh_id);
        if (Win_is_visible(&BaseWin)) {
            bw_redraw();
            GraphicWin_redraw(&MainInterfaceWin);
        }
        return;
    case 0x14:
        if (Bases[base_win().base].faction_id == u.faction_id) {
            u.home_base_id = (int16_t)base_win().base;
            ui_synch_veh(veh_id);
            net_await_synch();
        }
        break;
    case 0x15:
        net_unlock_veh();
        BaseWin_zoom(&BaseWin, u.home_base_id, 0);
        return;
    case 0x1E:
        if (jail()) break;
        u.state &= 0xFCFFBDFFu;
        u.order = 0x18;
        u.waypoint_x[0] = Bases[u.home_base_id].x;
        u.waypoint_y[0] = Bases[u.home_base_id].y;
        ui_synch_veh(veh_id);
        net_await_synch();
        break;
    case 0x1F:
        net_unlock_veh();
        Console_set_view(&MainConsole, 0);
        pfncMoveMap(&MainConsole, u.x, u.y, 1);
        Console_set_cursor(&MainConsole, u.x, u.y);
        if (Win_is_visible(&BaseWin)) bw_close_window(self);
        else if (Win_is_visible(&ReportWindow)) ReportIf_done(&ReportIfObj);
        return;
    case 0x5A: Console_editor_edit_veh(&MainConsole, veh_id); break;
    default: break;
    }
    net_unlock_veh();
    if (Win_is_visible(&BaseWin)) {
        set_base(BaseUpkeepDrawID);
        base_compute(1);
        bw_redraw();
        GraphicWin_redraw(&MainInterfaceWin);
        BaseWin_draw_garrison(self, 1);
    } else if (Win_is_visible(&ReportWindow)) {
        GraphicWin_redraw(&ReportWindow);
    }
}
