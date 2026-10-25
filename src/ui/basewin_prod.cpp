// The base window's build menu and queue / unit panes (basewin.h): BaseWin_production,
// BaseWin_queue_click, BaseWin_garrison_click.
//
// BaseWin_production follows the original (0x417040) after the item is chosen: the queue counters
// (Faction units_queue), the retool penalty popup (RETOOLPENALTYn[A]), the queue insertion modes,
// the production change bookkeeping (state/governor flags, log, wave, draw_radius, waking the
// sentried formers / colony pods at the base), the redraw and the "Ascent" interlude. The original
// picker (FUN_00494700, a full-screen grid of item pictures built by FUN_00492fa0) is replaced by a
// text popup listing the same items in the same order (units in proto_sort order filtered by
// veh_avail, then facilities 1..0x85 filtered by facility_avail) with the same return codes
// (-9999 cancel, unit id, 0x200 + facility id).
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
#include "ui/console.h"
#include "ui/popup.h"
#include "ui/win.h"

namespace {

// Minerals left to build item (FUN_00492fa0's cost column, with the retool loss shown when the
// item belongs to another category than the production at open).
int32_t pick_rest(int32_t item, int32_t prod_at_open, int32_t slot) {
    BASE& b = cur_base();
    const Faction& f = Factions[b.faction_id];
    int32_t cf = cost_factor(b.faction_id, 1, -1);
    if (item >= 0) {
        int32_t has_proto = 0;
        int32_t cost = cf * veh_cost(item, CurrentBaseID, &has_proto);
        if (slot != 0) return cost;
        int32_t acc = b.minerals_accumulated_2;
        if (base_making(item, CurrentBaseID) != base_making(prod_at_open, CurrentBaseID) && f.diff_level >= 2 &&
            acc > Rules.retool_exemption)
            acc = (acc - Rules.retool_exemption) * (100 - Rules.retool_penalty_prod_change) / 100 +
                  Rules.retool_exemption;
        return cost - acc;
    }
    int32_t cost = cf * Facility[-item].cost;
    if (slot != 0) return cost;
    if (base_making(item, CurrentBaseID) == base_making(prod_at_open, CurrentBaseID) || f.diff_level < 2)
        return cost - b.minerals_accumulated;
    return cost - b.minerals_accumulated / 2;
}

void pick_item(BasePop* pop, int32_t item, int32_t code, int32_t prod_at_open, int32_t slot) {
    BASE& b = cur_base();
    char buf[64];
    StrBuffer[0] = 0;
    crt_strcat(StrBuffer, item >= 0 ? Units[item].name : Facility[-item].name);
    int32_t rest = pick_rest(item, prod_at_open, slot);
    if (rest < 0) rest = 0;
    int32_t surplus = b.mineral_surplus;
    if (surplus > 0) snprintf(buf, sizeof buf, "  (%d, %d)", rest, (rest + surplus - 1) / surplus);
    else snprintf(buf, sizeof buf, "  (%d)", rest);
    if (item != -0x45) crt_strcat(StrBuffer, buf);
    BasePop_item(pop, StrBuffer, code);
}

// FUN_00494700 stand-in: the choice for queue slot `slot` (0 = current production).
int32_t production_pick(int32_t faction, int32_t slot, int32_t prod_at_open) {
    BasePop* pop = popup_new();
    Popup_start(pop, nullptr, nullptr, -1, nullptr, 0x40);
    BasePop_string(pop, cur_base().name);
    BasePop_set_max_rows(pop, 20);  // columns of 20 (the original picker is a grid)
    int32_t current = cur_base().queue_items[slot], active = -9999;
    proto_sort(faction);
    for (int32_t i = 0; i < 128; i++) {
        int32_t u = ProtoSortList[i];
        if (!veh_avail(u, faction, CurrentBaseID)) continue;
        if (u == current) active = u;
        pick_item(pop, u, u, prod_at_open, slot);
    }
    for (int32_t fac = 1; fac <= 0x85; fac++) {
        if (!facility_avail(fac, faction, CurrentBaseID, slot)) continue;
        if (-fac == current) active = 0x200 + fac;
        pick_item(pop, -fac, 0x200 + fac, prod_at_open, slot);
    }
    if (active != -9999) BasePop_set_active(pop, active);
    int32_t r = BasePop_exec_3(pop, 0, 0);
    popup_delete(pop);
    return r < 0 ? -9999 : r;
}

void queue_count(int32_t faction, int32_t delta) {
    BASE& b = cur_base();
    for (int32_t i = 0; i <= b.queue_size; i++)
        if (b.queue_items[i] >= 0) Factions[faction].units_queue[b.queue_items[i]] += (uint8_t)delta;
}

void refresh_base(uint8_t* self) {
    int32_t was = cur_base().worked_tiles;
    set_base(bwf(kBwBase));
    base_compute(0);
    if (MultiplayerActive && !ControlTurnC && was != Bases[bwf(kBwBase)].worked_tiles &&
        !bw_lock_base(CurrentBaseID))
        bw_unlock_base(CurrentBaseID);
}

}  // namespace

void THISCALL BaseWin_production(uint8_t* self, int32_t slot, int32_t no_lock) {
    set_base(bwf(kBwBase));
    base_compute(0);
    if (Bases[bwf(kBwBase)].faction_id != CurrentPlayerFaction && !(GameState & 0x80)) return;
    BASE& b = cur_base();
    int32_t saved_size = b.queue_size, saved[10];
    for (int32_t i = 0; i < 10; i++) saved[i] = b.queue_items[i];
    bwf(kBwProdAtOpen) = b.production_id_last;
    int32_t faction = b.faction_id;
    if (slot == 0) b.state_flags &= ~0x800000;
    if (!no_lock && bw_lock_base(CurrentBaseID)) return;
    queue_count(faction, -1);
    for (;;) {
        refresh_base(self);
        int32_t item = production_pick(faction, slot, bwf(kBwProdAtOpen));
        if (slot != 0 && item == -9999) {
            for (int32_t i = 0; i < 10; i++) cur_base().queue_items[i] = saved[i];
            cur_base().queue_size = saved_size;
            BaseWin_draw_production(self, 0);
        }
        refresh_base(self);
        FX_play(&Sounds, 0xF);
        BASE& cb = cur_base();
        if (item < -0x270E || (GameState & 0x800)) {
            bwf(kBwQueueSel) = 0;
            BaseWin_draw_queue(self, 1);
            if (!no_lock) bw_unlock_base(CurrentBaseID);
            queue_count(faction, 1);
            return;
        }
        // (The picker's design / help / queue-edit modes, self+0xB194, are not offered by the
        // stand-in picker: mode 0, a plain choice.)
        if (item == 9999) break;
        if (item > 0x1FF) item = 0x200 - item;
        bool retool = slot == 0 && item == cb.queue_items[0];
        if (!retool) {
            cb.state_flags |= 0x80000000;
            if (cb.governor_flags & 0x40000000) cb.governor_flags &= 0x3FFFFFFF;
            retool = slot == 0;
        }
        if (retool && cb.minerals_accumulated_2 != 0) {
            int32_t open = bwf(kBwProdAtOpen);
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
        if (slot != 0 && !retool) BaseWin_add_queue(self, item, 0);  // the picker's queue mode (7)
        log_say_2("BASE: changed production", CurrentBaseID, slot, cb.queue_items[slot]);
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
        // The original loops back to the picker while it stays open (queue editing); the stand-in
        // closes after each choice.
        if (!no_lock) bw_unlock_base(CurrentBaseID);
        queue_count(faction, 1);
        // (Multiplayer: message_data for a project started or dropped, not reconstructed.)
        bwf(kBwQueueSel) = 0;
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
    // 9999: the design workshop, then the picker again (not offered by the stand-in).
    if (!no_lock) bw_unlock_base(CurrentBaseID);
    queue_count(faction, 1);
}

// The queue pane (iface spot 8, index = slot + 2): left click picks the item for that slot; a
// right click removes it.
void THISCALL BaseWin_queue_click(uint8_t* self, int32_t index, int32_t, int32_t, int32_t right) {
    int32_t s = index - 2;
    BASE& b = cur_base();
    if (Bases[bwf(kBwBase)].faction_id != CurrentPlayerFaction && !(GameState & 0x80)) return;
    if (s < 1) s = 1;
    if (s > b.queue_size + 1) s = b.queue_size + 1;
    if (s > 9) return;
    if (right) {
        if (s <= b.queue_size) BaseWin_delete_queue(self, s);
        return;
    }
    bwf(kBwQueueSel) = s;
    BaseWin_draw_queue(self, 1);
    BaseWin_production(self, s, 0);
}

// The units present pane (iface spot 6): the unit's popup menu is the console's (not reconstructed
// here); a click centres the map on the base.
void THISCALL BaseWin_garrison_click(uint8_t* self, int32_t, int32_t, int32_t, int32_t) {
    console_not_reconstructed("BaseWin_garrison_click");
}
