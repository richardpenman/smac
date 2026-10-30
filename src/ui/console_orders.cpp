// Console: the unit orders that Console_veh_key, Console_on_key_click, the menus and the map's
// mouse code give (go to, go home, automate, unload, artillery, abilities, explore, convoy, patrol,
// road to, designate, destroy, disband, activate, give, ...), and the mouse interaction modes they
// start (Console_cursor_on_2: the map waits for a target tile, MapWin_click gives the order).
//
// Every function follows the asm of the original (address in its declaration, console.h); the
// object is MainConsole (THISCALL `self`). The net layer's single-player paths are the
// net_* helpers of console.cpp. The go-to list's path preview is PlanWin (planwin.cpp); drawing
// left out (destination lines of group moves) is noted where it is.
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "engine/action.h"
#include "engine/ai.h"
#include "engine/base.h"
#include "engine/battle.h"
#include "engine/crt.h"
#include "engine/diplo_act.h"
#include "engine/log.h"
#include "engine/map.h"
#include "engine/move.h"
#include "engine/parse.h"
#include "engine/prefs.h"
#include "engine/rules.h"
#include "engine/save.h"
#include "engine/score.h"
#include "engine/setup.h"
#include "engine/stack.h"
#include "engine/strings.h"
#include "engine/tech.h"
#include "engine/util.h"
#include "engine/veh.h"
#include "engine/game.h"
#include "ui/basewin.h"
#include "ui/console.h"
#include "ui/mapwin.h"
#include "ui/vehdraw.h"
#include "ui/popup.h"
#include "ui/planwin.h"
#include "ui/sprites.h"
#include "ui/statuswin.h"
#include "ui/win.h"

namespace {

GLOBAL(int32_t, GotoToBase, 0x688348);  // the go-to list routes a unit (1) or finds a base (0)
GLOBAL(int32_t, GotoLastBase, 0x68834C);  // base chosen last in a go-to list (-1: none)
GLOBAL(int32_t, GotoFromX, 0x915680);  // the go-to list's route start (PlanWin)
GLOBAL(int32_t, GotoFromY, 0x915650);

uint8_t* main_view() { return MapWins[0]; }
inline VEH& veh(int32_t id) { return Vehs[id]; }
inline const UNIT& unit_of(int32_t veh_id) { return Units[Vehs[veh_id].unit_id]; }
inline int32_t triad_of(int32_t veh_id) { return Chassis[unit_of(veh_id).chassis_id].triad; }
inline MAP* tile_of(int32_t veh_id) { return map_tile(Vehs[veh_id].x, Vehs[veh_id].y); }
// The tile holds a base of any faction (the original's `(items & 1) && 0 <= owner < 8`).
inline bool tile_has_base(const MAP* sq) { return (sq->items & 1) && (int32_t)(sq->val2 & 0xF) < 8; }
inline void pop_msg(const char* label) { NetMsg_pop(&NetMsg, label, 5000, 0, nullptr); }

// Horizontal distance with the map's wrap (the inlined abs / piTilesPerRow fold).
int32_t wrap_dx(int32_t dx) {
    int32_t d = std::abs(dx);
    if (!(MapToggleFlat & 1) && MapHalfX < d) d = MapAreaX - d;
    return d;
}

// The Popup object the original constructs on its stack (Popup_ctor ... BasePop_dtor).
struct StackPop {
    BasePop* p = popup_new();
    ~StackPop() { popup_delete(p); }
};

// Win_set_cursor of every open map window to cursor `index` of CURSOR.PCX (the window field
// +0x1EF4C remembers the cursor sprite: 0x93AA70 + index * 0x2C), then do_non_input.
void set_map_cursor(int32_t index) {
    uint32_t sprite = 0x93AA70u + (uint32_t)index * 0x2C;
    for_each_mapwin([&](int32_t, uint8_t* w) {
        if ((uint32_t)map_win(w).cursor != sprite) {
            ui_set_map_cursor(index);
            map_win(w).cursor = (int32_t)sprite;
        }
    });
    do_non_input();
}

// The arrow cursor back on every map window (Win_set_cursor_3(IDC_ARROW)) and the target
// highlights of the modes (options 0xC40000) off.
void arrow_cursors() {
    for_each_mapwin([&](int32_t, uint8_t* w) {
        if (map_win(w).cursor || map_win(w).cursor_id != 0x7F00) {
            ui_set_map_cursor(-1);
            map_win(w).cursor_id = 0x7F00;
            map_win(w).cursor = 0;
        }
        if (map_win(w).options & 0xC40000) {
            map_win(w).options &= 0xFF3BFFFF;
            pfncRedrawMap(w, 1);
        }
        do_non_input();
    });
}

}  // namespace

// ---- mouse interaction modes ----------------------------------------------------------------------

void THISCALL Console_cursor_on(uint8_t*, int32_t index) { set_map_cursor(index); }

void THISCALL Console_cursor_on_2(uint8_t* self, int32_t mode, int32_t range) {
    console_win(self).mode_range = range;
    console_win(self).mode = mode;
    set_map_cursor(mode);
}

void THISCALL Console_cursor_off(uint8_t* self) {
    console_win(self).mode = 0;
    arrow_cursors();
}

int32_t THISCALL Console_selected(uint8_t* self) {
    int32_t mode = console_win(self).mode;
    if (mode) {
        console_win(self).mode = 0;
        arrow_cursors();
    }
    return mode;
}

int32_t THISCALL Console_selecting(uint8_t* self, int32_t x, int32_t y) {
    int32_t mode = console_win(self).mode;
    if (mode == 1) return 1;
    if (!mode) return 0;
    if (console_win(self).mode_range >= 0) {
        // Out of range: the "no" cursor (index 3).
        int32_t v = console_win(self).veh;
        int32_t dx = wrap_dx(x - Vehs[v].x);
        int32_t dy = std::abs(y - Vehs[v].y);
        if (console_win(self).mode_range < (dy + dx) >> 1) {
            set_map_cursor(3);
            return 1;
        }
    }
    if (console_win(self).mode == 8 && !valid_patrol(console_win(self).veh, x, y)) {
        set_map_cursor(3);
        return 1;
    }
    set_map_cursor(console_win(self).mode);
    return 1;
}

// ---- simple orders ---------------------------------------------------------------------------------

void THISCALL Console_go_home(uint8_t* self, int32_t veh_id) {
    if (net_lock_veh(&veh_id, 0)) return;
    int32_t base_id = base_at(veh(veh_id).x, veh(veh_id).y);
    if (base_id < 0) {
        action_home(veh_id, 0);
        ui_synch_veh(veh_id);
        net_await_synch();
    } else if (Bases[base_id].faction_id == (int8_t)veh(veh_id).faction_id) {
        // In a base of its own: that base becomes its home.
        int32_t old_home = veh(veh_id).home_base_id;
        veh(veh_id).home_base_id = (int16_t)base_id;
        ui_synch_veh(veh_id);
        net_await_synch();
        net_unlock_veh();
        if (old_home >= 0) BaseWin_check_base(&BaseWin, old_home);
        BaseWin_check_base(&BaseWin, base_id);
        Console_update_data(self, 0);
        return;
    }
    net_unlock_veh();
}

void THISCALL Console_automate(uint8_t*, int32_t veh_id, int32_t mode) {
    const UNIT& u = unit_of(veh_id);
    if (u.plan == PLAN_TERRAFORM) {
        if (mode == 2 || mode == 1) {
            // Road / tube networks: land formers on land only.
            if (Chassis[u.chassis_id].triad) return;
            if ((tile_of(veh_id)->climate & 0xE0) < 0x60) return;
        }
        if (mode == 2) {
            if (!has_tech(Terraform[FORMER_MAGTUBE].preq_tech, veh(veh_id).faction_id)) return;
            goto lock;
        }
        if ((mode == 5 || mode == 4) && tile_has_base(tile_of(veh_id))) return;
    }
    if (mode == 10) {
        // Air patrol / bombing run: armed aircraft pick a target with the mouse.
        if (Chassis[u.chassis_id].triad != TRIAD_AIR) return;
        if (!Weapon[u.weapon_id].offense_value) return;
        veh(veh_id).order = 0;
        Console_cursor_on_2(&MainConsole, 0xB, -1);
        return;
    }
lock:
    if (net_lock_veh(&veh_id, 0)) return;
    VEH& v = veh(veh_id);
    if (mode == 0xB) {
        // Air defence: returns to the current tile (or home base for aircraft with a range).
        v.waypoint_x[1] = v.x;
        v.waypoint_y[1] = v.y;
        if (Chassis[unit_of(veh_id).chassis_id].range) {
            action_home(veh_id, 0);
            if (v.order == ORDER_MOVE_TO) {
                v.waypoint_x[1] = v.waypoint_x[0];
                v.waypoint_y[1] = v.waypoint_y[0];
            }
            v.order = 0;
        }
    }
    v.order_auto_type = (uint8_t)mode;
    v.order = 0;
    v.state = (v.state & 0xFCFFFFFF) | 0x200;
    v.waypoint_count = 0;
    ui_synch_veh(veh_id);
    net_await_synch();
    net_unlock_veh();
}

void THISCALL Console_explore(uint8_t*, int32_t veh_id) {
    if (net_lock_veh(&veh_id, 0)) return;
    veh(veh_id).order = 0;
    veh(veh_id).state |= 0x4000;
    ui_synch_veh(veh_id);
    net_await_synch();
    net_unlock_veh();
}

void THISCALL Console_set_patrol(uint8_t* self, int32_t veh_id) {
    veh(veh_id).waypoint_count = 0;
    if (console_win(self).mode == 8) {
        Console_cursor_off(self);
        return;
    }
    for_each_mapwin([&](int32_t, uint8_t* w) {
        map_win(w).options |= 0x400000;
        MapWin_draw_base_dest(w, 1);
    });
    Console_cursor_on_2(self, 8, -1);
}

void THISCALL Console_designate(uint8_t*, int32_t veh_id) {
    if (!Weapon[unit_of(veh_id).weapon_id].offense_value) return;
    if (net_lock_veh(&veh_id, 0)) return;
    veh(veh_id).flags ^= 0x80;
    ui_synch_veh(veh_id);
    net_unlock_veh();
    parse_says(0, Units[veh(veh_id).unit_id].name, -1, -1);
    pop_msg((veh(veh_id).flags & 0x80) ? "DESIGNATED" : "UNDESIGNATED");
}

void THISCALL Console_road_to(uint8_t* self, int32_t veh_id, int32_t order) {
    const UNIT& u = unit_of(veh_id);
    if (u.plan != PLAN_TERRAFORM) {
        pop_msg("NEEDTERRAFORMERS");
        return;
    }
    if (Chassis[u.chassis_id].triad) {
        pop_msg("NOTINTRIAD");
        return;
    }
    if ((tile_of(veh_id)->climate & 0xE0) < 0x60) {
        pop_msg("NOTINOCEAN");
        return;
    }
    if (order == ORDER_MAGTUBE_TO && !has_tech(Terraform[FORMER_MAGTUBE].preq_tech, veh(veh_id).faction_id)) return;
    Console_cursor_on_2(self, (order == ORDER_MAGTUBE_TO) + 2, -1);
}

void THISCALL Console_unload(uint8_t* self, int32_t veh_id) {
    uint32_t found = 0;  // bit 0: a passenger, bit 1: one that can move now
    if (net_lock_veh(&veh_id, 0)) return;
    if (!veh_cargo(veh_id) && !has_abil(veh(veh_id).unit_id, 0x80)) {
        net_unlock_veh();
        return;
    }
    // Carriers (0x80: air transport) unload aircraft, others land units.
    int32_t triad = has_abil(veh(veh_id).unit_id, 0x80) ? 2 : 0;
    if (veh_id < 0) {
        veh_id = -1;
    } else {
        while (veh(veh_id).prev_veh_id_stack >= 0) veh_id = veh(veh_id).prev_veh_id_stack;
    }
    for (; veh_id >= 0; veh_id = veh(veh_id).next_veh_id_stack) {
        if (veh(veh_id).order == ORDER_SENTRY_BOARD) {
            veh(veh_id).order = 0;
            ui_synch_veh(veh_id);
        }
        if (triad_of(veh_id) == triad) {
            found |= 1;
            if (veh_unmoved(veh_id) && !veh(veh_id).order) {
                console_win(self).veh = veh_id;
                found |= 2;
            }
        }
    }
    net_unlock_veh();
    if (!found) {
        pop_msg("UNLOADNO");
        return;
    }
    if (!(found & 2)) {
        pop_msg("UNLOADNEXT");
        return;
    }
    console_win(self).waiting = 0;
    Console_set_move(self, 1);
}

void THISCALL Console_oblit(uint8_t*, int32_t veh_id) {
    int32_t faction_id = veh(veh_id).faction_id;
    int32_t x = veh(veh_id).x, y = veh(veh_id).y;
    if (not_my_turn()) {
        pop_msg("SIMULNO");
        return;
    }
    if (!veh_ready(veh_id)) {
        pop_msg("UNITMOVED");
        return;
    }
    int32_t base_id = base_at(x, y);
    if (base_id < 0) {
        pop_msg("OBLITNOBASE");
        return;
    }
    if ((uint8_t)Bases[base_id].faction_id != faction_id) {
        pop_msg("OBLITNOTYOURS");
        return;
    }
    if (is_objective(base_id)) {
        pop_msg("OBLITNOTOBJECTIVE");
        return;
    }
    if (net_lock_veh(&veh_id, 4)) return;
    base_id = base_at(x, y);
    if (base_id < 0 || (uint8_t)Bases[base_id].faction_id != faction_id) return;
    parse_says(0, Bases[base_id].name, -1, -1);
    // OBLIT counts as an atrocity (Rules), OBLITOK does not.
    const char* label;
    const char* pcx;
    if (MFactions[faction_id].rule_flags & 0x80) {
        label = Rules.tgl_oblit_base_atrocity ? "OBLIT" : "OBLITOK";
        pcx = "al_col_sm.pcx";
    } else {
        label = Rules.tgl_oblit_base_atrocity ? "OBLIT" : "OBLITOK";
        pcx = "colfoun_sm.pcx";
    }
    if (!popp(ScriptFileName, label, 0, pcx, 0)) {
        net_unlock_veh();
        return;
    }
    if (net_add_global()) return;
    if (MultiplayerActive) {
        message_veh(0x2409, veh_id, base_id, 0);
        NetDaemon_await_exec(&NetState, 1);
        return;
    }
    action_oblit(veh_id, base_id);
}

void THISCALL Console_arty(uint8_t* self, int32_t veh_id) {
    if (!can_arty(veh(veh_id).unit_id, 1)) return;
    if (not_my_turn()) {
        pop_msg("SIMULNO");
        return;
    }
    if (!veh_ready(veh_id)) {
        pop_msg("UNITMOVED");
        return;
    }
    MAP* sq = tile_of(veh_id);
    if ((sq->climate & 0xE0) < 0x60 && triad_of(veh_id) != TRIAD_SEA && !tile_has_base(sq)) {
        pop_msg("SEAARTY");
        return;
    }
    Console_cursor_on_2(self, 10, Rules.artillery_max_rng);
    // The window under the mouse updates its cursor for the mode (vtable +0x44: on_mouse_move).
    int32_t mx, my;
    ui_mouse_pos(&mx, &my);
    MapWin_on_mouse_move(main_view(), mx, my);
}

void THISCALL Console_convoy(uint8_t* self, int32_t veh_id) {
    if (net_lock_veh(&veh_id, 0)) return;
    int32_t base_id = base_at(veh(veh_id).x, veh(veh_id).y);
    if (base_id >= 0) {
        VehAttackFlags = 3;
        supply_options(veh_id, base_id);
        net_unlock_veh();
        return;
    }
    int32_t home = veh(veh_id).home_base_id;
    if (home >= 0) {
        if (!can_convoy(veh(veh_id).x, veh(veh_id).y)) {
            pop_msg("CANTCONVOY");
            net_unlock_veh();
            return;
        }
        // What the tile would yield to the home base, by resource (nutrients, minerals, energy).
        for (int32_t i = 0; i < 3; i++) {
            int32_t n =
                resource_yield(i, console_win(self).faction, veh(veh_id).home_base_id, veh(veh_id).x, veh(veh_id).y);
            parse_num(i, n);
            parse_say(i, n == 1 ? ResName[i].name_singular : ResName[i].name_plural, -1, -1);
        }
        parse_says(3, Bases[home].name, -1, -1);
        int32_t r = X_pop_6("SUPPLYEXTRACT", 0x40, 0);
        if (r >= 0) {
            veh(veh_id).order = ORDER_CONVOY;
            veh(veh_id).order_auto_type = (uint8_t)r;
            ui_synch_veh(veh_id);
            net_await_synch();
        }
    }
    net_unlock_veh();
}

// ---- unit pick list and the unit popup (ui module) ----

int32_t GAMEAPI veh_pick(int32_t veh_id, const char* label, int32_t cancel) {
    // Popup_start(label, flags 0x800 | (cancel ? 0x40 : 0)): one item per unit of the stack
    // (morale and name; the original shows the unit's picture in a sprite box per item).
    StackPop pop;
    BasePop_start(pop.p, ScriptFileName, label, -1, nullptr, 0x800u | (cancel ? 0x40u : 0u), nullptr);
    int32_t count = 0;
    if (veh_id >= 0) {
        while (veh(veh_id).prev_veh_id_stack >= 0) veh_id = veh(veh_id).prev_veh_id_stack;
        for (; veh_id >= 0; veh_id = veh(veh_id).next_veh_id_stack) {
            StrBuffer[0] = 0;
            say_morale_2(veh_id, 0);
            crt_strcat(StrBuffer, " ");
            crt_strcat(StrBuffer, Units[veh(veh_id).unit_id].name);
            BasePop_item(pop.p, StrBuffer, veh_id);
            count++;
        }
    }
    if (!count) return -1;
    PopupDialogState = 6;
    int32_t r = BasePop_exec_3(pop.p, 0, 0);
    PopupDialogState = 0;
    return r;
}

int32_t GAMEAPI popv(const char* file, const char* label, int32_t veh_id, int32_t flags) {
    // A popup with the unit's picture (sprite box of veh_id; not drawn here).
    StackPop pop;
    BasePop_start(pop.p, file, label, -1, nullptr, (uint32_t)flags, nullptr);
    (void)veh_id;
    int32_t r = BasePop_exec_3(pop.p, 0, 0);
    DialogToggle = BasePop_button_pressed(pop.p);
    return r;
}

// ---- activate, disband, give ------------------------------------------------------------------------

void THISCALL Console_activate(uint8_t* self, int32_t x, int32_t y, int32_t select) {
    log_say_values("Console ACTIVATE", x, y, 0);
    int32_t v;
    if (MultiplayerActive) {
        if (NetLockHeld || NetLocked) return;
        if (!(NetTurnDoneMask & (1 << (CurrentPlayerFaction & 31))) || (GameMoreRules & 0x10)) {
            if (ControlTurnC) goto mp_busy;
        }
        console_not_reconstructed("Console_activate (multiplayer)");
        return;
    mp_busy:
        v = veh_at(x, y);
        if (stack_check(v, 1, -1, -1, -1) != 1) v = veh_pick(v, "PICKUNIT", 1);
        if (v >= 0) {
            console_win(self).veh = v;
            console_win(self).next_veh = v;
        }
        pop_msg((NetTurnDoneMask & (1 << (CurrentPlayerFaction & 31))) && veh_ready(v) ? "ALREADYDONE" : "FIRSTMOVE");
        return;
    }
    if (MapWinInVehMode && select) {
        v = console_win(self).veh;
        console_win(self).waiting = 0;
        if (stack_check(v, 1, CurrentPlayerFaction, -1, -1) == 1 && veh_selectable(v)) {
            console_win(self).any_select = 1;
            return;
        }
        goto pick;
    }
    v = veh_at(x, y);
    if (v < 0 || !stack_check(v, 1, CurrentPlayerFaction, -1, -1)) {
        console_win(self).waiting = 0;
        return;
    }
    if (stack_check(v, 1, -1, -1, -1) != 1) goto pick;
    if (veh_selectable(v) || veh(v).order || (veh(v).state & 0x4200)) {
        log_say_values("VEH ORDERS", v, 0, 0);
        if ((int8_t)veh(v).order > 2 && !(GameMorePreferences & MPREF_ADV_CLICK_VEH_CANCELS_ORDERS)) {
            // The unit has orders: asked whether to cancel them (2: and do not ask again).
            parse_says(0, Units[veh(v).unit_id].name, -1, -1);
            StrBuffer[0] = 0;
            say_orders_2(v);
            parse_says(1, StrBuffer, -1, -1);
            StrBuffer[0] = 0;
            crt_strcat(StrBuffer, "VEHORDERS");
            if (veh(v).order >= ORDER_FARM && veh(v).order < ORDER_MOVE_TO) {
                parse_num(0, action_terraform(v, veh(v).order - 4, 0) + 1);
                crt_strcat(StrBuffer, "2");
            }
            int32_t r = X_pop(StrBuffer, 0);
            if (!r) return;
            if (r == 2) {
                GameMorePreferences |= MPREF_ADV_CLICK_VEH_CANCELS_ORDERS;
                prefs_save(0);
            }
        }
        goto activate;
    }
    goto pick_v;
pick:
    v = console_win(self).veh;  // [ebp+0xc] at L4d329e: the unit tested above
pick_v:
    v = veh_pick(v, "PICKUNIT", 1);
    if (v < 0) return;
activate:
    if (veh(v).faction_id != (uint32_t)console_win(self).faction) return;
    log_say_values("Activate AWAKEN", v, veh(v).x, veh(v).y);
    if (veh_jail(v)) {
        pop_msg("INJAIL");
        return;
    }
    if (net_lock_veh(&v, 0)) return;
    log_say_values("Activate AWAKENING", v, veh(v).x, veh(v).y);
    veh_wake(v);
    if (!veh_selectable(v)) {
        draw_tile(veh(v).x, veh(v).y, 1);
        if (!MapWinInVehMode && !(GameState & 2)) {
            if (popv(ScriptFileName, "ALREADYMOVED2", v, 0)) Console_set_move(self, 0);
        } else {
            popv(ScriptFileName, "ALREADYMOVED", v, 0);
        }
    } else {
        console_win(self).waiting = 0;
    }
    console_win(self).veh = v;
    ui_synch_veh(v);
    net_await_synch();
    net_unlock_veh();
    Console_set_move(self, 1);
    console_win(self).any_select = 1;
}

void THISCALL Console_disband(uint8_t* self, int32_t veh_id) {
    if ((veh(veh_id).flags & 0x20) && !(GameState & STATE_SCENARIO_EDITOR)) {
        pop_msg("DISBANDOBJ");  // a scenario objective unit
        return;
    }
    if (net_lock_veh(&veh_id, 4)) return;
    if (veh(veh_id).faction_id != (uint32_t)console_win(self).faction &&
        (MultiplayerActive || !(GameState & STATE_OMNISCIENT_VIEW))) {
        net_unlock_veh();
        return;
    }
    console_win(self).waiting = 0;
    int32_t base_id = base_at(veh(veh_id).x, veh(veh_id).y);
    parse_says(0, Units[veh(veh_id).unit_id].name, -1, -1);
    StrBuffer[0] = 0;
    crt_strcat(StrBuffer, "DISBAND");
    if (base_id < 0) crt_strcat(StrBuffer, "2");
    const char* pcx;
    int32_t triad = triad_of(veh_id);
    if (MFactions[console_win(self).faction].rule_flags & 0x80)
        pcx = triad == TRIAD_AIR ? "alair_sm.pcx" : (triad == TRIAD_SEA ? "navun_sm.pcx" : "alunit_sm.pcx");
    else pcx = triad == TRIAD_AIR ? "air_sm.pcx" : (triad == TRIAD_SEA ? "navun_sm.pcx" : "unit_sm.pcx");
    int32_t r = popp(ScriptFileName, StrBuffer, 0, pcx, 0);
    if (!r) {
        net_unlock_veh();
        return;
    }
    if (base_id >= 0) {
        // Disbanded in a base: half its cost goes to production (not native life, not when it
        // self-destructs: answer 2).
        int16_t unit_id = veh(veh_id).unit_id;
        bool native = unit_id < 0x40 && (Weapon[Units[unit_id].weapon_id].offense_value < 0 || unit_id == 0xF);
        if (!native && r != 2) {
            BASE& b = Bases[base_id];
            int32_t cost = cost_factor(veh(veh_id).faction_id, 1, -1) * Units[veh(veh_id).unit_id].cost;
            b.minerals_accumulated += cost / 2;
            b.minerals_accumulated_2 = b.minerals_accumulated;
            b.production_id_last = b.queue_items[0];
            ui_synch_base(base_id);
            BaseWin_check_base(&BaseWin, base_id);
        }
    }
    if (MultiplayerActive) {
        message_veh(0x2407, veh_id, r, 0);
        NetDaemon_await_exec(&NetState, 1);
        return;
    }
    if (r == 2) action_destruct(veh_id);
    else kill(veh_id);
}

void THISCALL Console_disband_2(uint8_t* self, int32_t x, int32_t y) {
    int32_t v = veh_at(x, y);
    if (v >= 0) {
        Console_disband(self, v);
        return;
    }
    // Scenario editor: the base at the cursor is removed.
    if (!(GameState & STATE_SCENARIO_EDITOR) || MultiplayerActive) return;
    auto_undo();
    int32_t base_id = base_at(x, y);
    if (base_id < 0) return;
    parse_says(0, Bases[base_id].name, -1, -1);
    int32_t r = (MFactions[Bases[base_id].faction_id].rule_flags & 0x80)
                    ? popp(ScriptFileName, "DISBAND", 0, "al_col_sm.pcx", 0)
                    : popp(ScriptFileName, "DISBAND", 0, "colfoun_sm.pcx", 0);
    if (!r) return;
    int32_t faction_id = (uint8_t)Bases[base_id].faction_id;
    base_kill(base_id);
    draw_map(1);
    StatusWin_redraw(&InfoWin);
    eliminate_player(faction_id, 0);
}

void THISCALL Console_give_veh(uint8_t* self, int32_t veh_id) {
    if (net_lock_veh(&veh_id, 4)) return;
    if (veh(veh_id).faction_id != (uint32_t)CurrentPlayerFaction) {
        net_unlock_veh();
        return;
    }
    int32_t x = veh(veh_id).x, y = veh(veh_id).y;
    int32_t to = whose_territory(CurrentPlayerFaction, x, y, nullptr, 0);
    int32_t me = console_win(self).faction;
    auto is_native = [](int32_t unit_id) {
        return unit_id < 0x40 && (Weapon[Units[unit_id].weapon_id].offense_value < 0 || unit_id == 0xF);
    };
    if (to == me || to < 0 || !(Factions[me].diplo_status[to] & 8) || (Factions[me].diplo_status[to] & 0x10)) {
        // Not in the territory of a faction we have contact with and no vendetta: only native
        // life can be released (to nobody).
        if (!is_native(veh(veh_id).unit_id)) {
            net_unlock_veh();
            pop_msg("CANTGIVE");
            return;
        }
        to = 0;
    }
    if (triad_of(veh_id) == TRIAD_AIR) {
        MAP* sq = map_tile(x, y);
        if (!tile_has_base(sq) && !(sq->items & 0x40000)) {
            net_unlock_veh();
            pop_msg("CANTGIVEAIR");
            return;
        }
    }
    MFaction& m = MFactions[to];
    GenderDefault = m.noun_gender;
    PluralDefault = m.is_noun_plural;
    parse_says(0, m.noun_faction, -1, -1);
    parse_says(1, Units[veh(veh_id).unit_id].name, -1, -1);
    int32_t r;
    if (is_native(veh(veh_id).unit_id)) {
        if (to) {
            r = popp(ScriptFileName, "CANGIVE2", 0, "native_sm.pcx", 0);
            if (!r) {
                net_unlock_veh();
                return;
            }
            if (r == 2) to = 0;  // released into the wild
        } else {
            r = popp(ScriptFileName, "CANGIVE1", 0, "native_sm.pcx", 0);
        }
    } else {
        int32_t triad = triad_of(veh_id);
        const char* pcx;
        if (MFactions[console_win(self).faction].rule_flags & 0x80)
            pcx = triad == TRIAD_AIR ? "alair_sm.pcx" : (triad == TRIAD_SEA ? "navun_sm.pcx" : "alunit_sm.pcx");
        else pcx = triad == TRIAD_AIR ? "air_sm.pcx" : (triad == TRIAD_SEA ? "navun_sm.pcx" : "unit_sm.pcx");
        r = popp(ScriptFileName, "CANGIVE", 0, pcx, 0);
    }
    if (!r) {
        net_unlock_veh();
        return;
    }
    if (MultiplayerActive) {
        message_veh(0x2416, veh_id, to, 0);
        NetDaemon_await_exec(&NetState, 1);
        return;
    }
    action_give(veh_id, to);
}

// ---- abilities and pillage ------------------------------------------------------------------------

void THISCALL Console_use_ability(uint8_t*, int32_t veh_id) {
    StackPop pop;
    int32_t x = veh(veh_id).x, y = veh(veh_id).y;
    int32_t faction_id = veh(veh_id).faction_id;
    if (not_my_turn()) {
        pop_msg("SIMULNO");
        return;
    }
    const UNIT& u = unit_of(veh_id);
    uint32_t abil = u.ability_flags & 0x10;  // drop pods
    bool missile = Chassis[u.chassis_id].missile && (u.plan == PLAN_TECTONIC_MISSILE || u.plan == PLAN_FUNGAL_MISSILE);
    if (!missile) {
        if (!abil) {
            pop_msg("NOABILITY");
            return;
        }
        if (veh(veh_id).moves_spent || !veh_ready(veh_id)) {
            pop_msg("UNITMOVED");
            return;
        }
        if (bit_count(abil) > 1) {
            // Several usable abilities: asked which (never with the drop pod bit alone).
            BasePop_start(pop.p, ScriptFileName, "ABILITY", -1, nullptr, 0x40, nullptr);
            for (int32_t i = 0; i < 29; i++) {
                uint32_t bit = 1u << i;
                if (has_abil(veh(veh_id).unit_id, bit)) BasePop_item(pop.p, Ability[i].name, (int32_t)bit);
            }
            int32_t r = BasePop_exec_3(pop.p, 0, 0);
            if (r < 0) return;
            abil = (uint32_t)r;
        }
        if (abil & 0x10) {
            MAP* sq = map_tile(x, y);
            if (!tile_has_base(sq) && !(sq->items & 0x40000)) {
                X_pop("DROPBASE", 0);
                return;
            }
            if (veh(veh_id).state & 0x20) {
                X_pop("DROPALREADY", 0);
                return;
            }
            if (veh(veh_id).moves_spent) {
                X_pop("DROPMOVED", 0);
                return;
            }
        }
        if (abil == 0x10) Console_cursor_on_2(&MainConsole, 4, drop_range(faction_id));
        return;
    }
    // Tectonic / fungal missiles detonate where they are (not over a base).
    MAP* sq = map_tile(x, y);
    if (tile_has_base(sq)) {
        pop_msg("MISSILEBASE");
        return;
    }
    int32_t count;  // tiles of the blast (TableRange by radius)
    if (u.plan == PLAN_TECTONIC_MISSILE) {
        if ((sq->climate & 0xE0) < 0x60) {
            pop_msg("TECTONICOCEAN");
            return;
        }
        int32_t r = (sq->climate >> 5) + 1;
        count = 1;
        // [0x66F8B8 + r * 4] = TableRange[r - 3]: radius 1..3 by altitude.
        if (r > 6) count = TableRange[3];
        else if (r > 3) count = TableRange[r - 3];
    } else {
        count = 9;
    }
    for (int32_t i = 0; i < count; i++) {
        int32_t tx = TableOffsetX[i] + x;
        if (!(MapToggleFlat & 1)) {
            if (tx < 0) tx += MapAreaX;
            else if (MapAreaX <= tx) tx -= MapAreaX;
        }
        int32_t ty = TableOffsetY[i] + y;
        if (ty < 0 || ty >= MapAreaY || tx < 0 || tx >= MapAreaX) continue;
        int32_t owner = whose_territory(faction_id, tx, ty, nullptr, 0);
        if (owner < 0 || owner == faction_id || !(Factions[faction_id].diplo_status[owner] & 7)) continue;
        // A blast in the territory of a pact / treaty / truce partner breaks the treaty.
        if (net_lock_veh(&veh_id, 2)) return;
        if (break_treaty(faction_id, owner, 7)) {
            net_unlock_veh();
            return;
        }
        if (MultiplayerActive) {
            message_data(0x2440, 0, faction_id, owner, -1, 0);
            NetDaemon_await_exec(&NetState, 1);
        } else {
            double_cross(faction_id, owner, -1);
        }
    }
    if (net_lock_veh(&veh_id, 2)) return;
    if (unit_of(veh_id).plan == PLAN_TECTONIC_MISSILE) {
        if (MultiplayerActive) {
            message_veh(0x2417, veh_id, x, y);
            NetDaemon_await_exec(&NetState, 1);
        } else {
            action_tectonic(veh_id, x, y);
        }
    } else if (unit_of(veh_id).plan == PLAN_FUNGAL_MISSILE) {
        if (MultiplayerActive) {
            message_veh(0x2418, veh_id, x, y);
            NetDaemon_await_exec(&NetState, 1);
        } else {
            action_fungal(veh_id, x, y);
        }
    }
}

void THISCALL Console_destroy(uint8_t* self, int32_t veh_id) {
    StackPop pop;
    if (not_my_turn()) {
        pop_msg("SIMULNO");
        return;
    }
    if (!veh_ready(veh_id)) {
        pop_msg("UNITMOVED");
        return;
    }
    if (net_lock_veh(&veh_id, 0)) return;
    if (not_my_turn()) {
        pop_msg("SIMULNO");
        net_unlock_veh();
        return;
    }
    if (!veh_ready(veh_id)) {
        pop_msg("UNITMOVED");
        net_unlock_veh();
        return;
    }
    int32_t faction_id = veh(veh_id).faction_id;
    int32_t x = veh(veh_id).x, y = veh(veh_id).y;
    MAP* sq = map_tile(x, y);
    int32_t is_sea = (sq->climate & 0xE0) < 0x60;
    const UNIT& u = unit_of(veh_id);
    if (!Weapon[u.weapon_id].offense_value && Weapon[u.weapon_id].mode != 9) {
        pop_msg("ONLYCOMBAT");
        net_unlock_veh();
        return;
    }
    if (tile_has_base(sq)) {
        net_unlock_veh();
        pop_msg("NOTBASE");
        return;
    }
    int32_t triad = Chassis[u.chassis_id].triad;
    if (triad != TRIAD_AIR && is_sea != (triad == TRIAD_SEA)) {
        net_unlock_veh();
        pop_msg("NOTINTRIAD");
        return;
    }
    uint32_t items = sq->items;
    BasePop_start(pop.p, ScriptFileName, triad == TRIAD_AIR ? "DESTROYBOMB" : "DESTROY", -1, nullptr, 0x40, nullptr);
    int32_t count = 0;
    for (int32_t i = 0; i < 19; i++) {
        // Roads under a mag tube, farms under a soil enricher and the aquifer are not listed.
        if (i == FORMER_ROAD) {
            if (items & 8) continue;
        } else if (i == FORMER_FARM) {
            if (items & 0x80000) continue;
        } else if (i == 11) {
            continue;
        }
        uint32_t bit = Terraform[i].bit;
        if (bit && (items & bit) && i != 10) {
            BasePop_item(pop.p, is_sea ? Terraform[i].name_sea : Terraform[i].name, (int32_t)bit);
            count++;
        }
    }
    if (!count) {
        net_unlock_veh();
        pop_msg("NOTHERE");
        return;
    }
    int32_t choice;
    if (count >= 2) {
        choice = BasePop_exec_3(pop.p, 0, 0);
        if (choice == -1) {
            net_unlock_veh();
            return;
        }
    } else {
        choice = 0;  // the only improvement: everything
    }
    int32_t owner = whose_territory(faction_id, x, y, nullptr, 0);
    if (owner >= 0 && owner != faction_id) {
        if (break_treaty(faction_id, owner, 7)) {
            net_unlock_veh();
            return;
        }
        if (!(Factions[faction_id].diplo_status[owner] & 0x10)) {
            if (MultiplayerActive) {
                message_data(0x2440, 0, faction_id, owner, -1, 0);
                NetDaemon_await_exec(&NetState, 0);
                message_data(0x2214, 0, 0, 0, 0, 0);
            } else {
                double_cross(faction_id, owner, -1);
            }
        }
    }
    console_win(self).waiting = 0;
    if (MultiplayerActive) {
        message_veh(0x2406, veh_id, choice, 0);
        NetDaemon_await_exec(&NetState, 1);
    } else {
        action_destroy(veh_id, choice, -1, -1);
    }
}

// ---- group moves and mouse-mode targets ----------------------------------------------------------------

void THISCALL Console_group(uint8_t* self, int32_t veh_id) {
    VEH& lead = veh(veh_id);
    int32_t x = lead.x, y = lead.y;
    int32_t faction_id = lead.faction_id;
    int32_t region = map_tile(lead.waypoint_x[0], lead.waypoint_y[0])->region;
    lead.state |= 0x8000000;
    for (int32_t i = 0; i < VehCount; i++) {
        if (i == veh_id) continue;
        VEH& v = veh(i);
        if (v.faction_id != (uint32_t)faction_id) continue;
        // The marked group (after a first group order), else the units of the leader's tile.
        if (console_win(self).unk_23d1c) {
            if (!(v.state & 0x8000000)) continue;
        } else if (v.x != x || v.y != y) {
            continue;
        }
        int32_t r = map_tile(v.x, v.y)->region;
        if (r != region) {
            int32_t triad = triad_of(i);
            if (triad != TRIAD_AIR) {
                if ((r < 0x40) != (region < 0x40)) continue;
                if (triad == TRIAD_LAND) continue;
                if (triad == TRIAD_SEA) {
                    int32_t to = base_at(lead.waypoint_x[0], lead.waypoint_y[0]);
                    int32_t from = base_at(v.x, v.y);
                    if (to >= 0 && from >= 0 && !port_to_port(to, from)) continue;
                }
            }
        }
        v.state &= 0xFCFFBDFF;
        v.order = lead.order;
        v.waypoint_count = lead.waypoint_count;
        v.patrol_current_point = lead.patrol_current_point;
        for (int32_t k = 0; k < 4; k++) {
            v.waypoint_x[k] = lead.waypoint_x[k];
            v.waypoint_y[k] = lead.waypoint_y[k];
        }
        v.state |= (lead.state & 0x2000200) | 0x8000000;
    }
    // The destination lines of the group (MapWin_dest_line per unit, option +0x1DD84) are drawn
    // by the map window; here the map is redrawn.
    console_win(self).unk_23d1c = 1;
    draw_map(1);
}

void THISCALL Console_set_road_to(uint8_t*, int32_t veh_id, int32_t x, int32_t y, int32_t order) {
    if (y < 0 || y >= MapAreaY || x < 0 || x >= MapAreaX) return;
    int32_t dx = wrap_dx(x - veh(veh_id).x);
    if (((std::abs(y - veh(veh_id).y) + dx) & ~1) < 2) return;  // next to the unit
    MAP* sq = map_tile(x, y);
    if ((sq->climate & 0xE0) < 0x60) {
        pop_msg("NOTINOCEAN");
        return;
    }
    if (sq->region != tile_of(veh_id)->region) return;
    if (net_lock_veh(&veh_id, 0)) return;
    veh(veh_id).order = (uint8_t)order;
    veh(veh_id).waypoint_x[0] = (int16_t)x;
    veh(veh_id).waypoint_y[0] = (int16_t)y;
    ui_synch_veh(veh_id);
    net_await_synch();
    net_unlock_veh();
}

void THISCALL Console_set_bombing_run(uint8_t*, int32_t veh_id, int32_t x, int32_t y) {
    int32_t base_id = base_at(x, y);
    if (base_id < 0 || !(Factions[veh(veh_id).faction_id].diplo_status[(uint8_t)Bases[base_id].faction_id] & 0x10)) {
        pop_msg("NOBOMB");
        return;
    }
    if (net_lock_veh(&veh_id, 0)) return;
    VEH& v = veh(veh_id);
    v.state = (v.state & 0xFCFFFFFF) | 0x200;
    v.order_auto_type = 0xA;
    v.waypoint_x[1] = (int16_t)x;
    v.waypoint_y[1] = (int16_t)y;
    ui_synch_veh(veh_id);
    net_await_synch();
    net_unlock_veh();
}

// ---- go to ------------------------------------------------------------------------------------------

// Console_goto_routine (0x4D6070, the go-to list's selection callback): PlanWin shows the route
// from the unit (GotoFromX / Y) to the highlighted base, or for find-base the base itself.
void GAMEAPI Console_goto_routine(int32_t base_id) {
    planwin_clear_lines();
    if (base_id < 0 || BaseCount <= base_id) {
        planwin_redraw_back();
    } else if (!GotoToBase) {
        const BASE& b = Bases[base_id];
        planwin_zoom_tile(b.x, b.y, 0);
        planwin_add_line(b.x, b.y, b.x, b.y, FactionColorFill[CurrentPlayerFaction]);
    } else {
        const BASE& b = Bases[base_id];
        planwin_zoom_route(GotoFromX, GotoFromY, b.x, b.y, 0);
        planwin_add_line(GotoFromX, GotoFromY, b.x, b.y, FactionColorFill[CurrentPlayerFaction]);
    }
    planwin_draw();
}

void THISCALL Console_go_to(uint8_t* self, int32_t id, int32_t mode, int32_t group) {
    // id: the unit (a base for mode -1); mode: ORDER_MOVE_TO / ROAD_TO / MAGTUBE_TO, 0x80 bombing
    // run, -1 the base's rally point of a triad, -2 find a base (centre the view), -3 psi gate.
    StackPop pop;
    int32_t saved_options = map_win(main_view()).options;  // [ebp-0x58]
    int32_t sort = prefs_get_int("Base Sort", 0, 0);  // [ebp-0x34]
    int32_t artifact = 0;  // [ebp-0x40]: carries an artifact
    int32_t faction_id, x, y;  // [ebp-0x28], [ebp-0x20], [ebp-0x1c]
    int32_t triad = -1;  // [ebp-0x30]
    int32_t range = 0, home_x = 0, home_y = 0;  // [ebp-0x18], [ebp-0x44], [ebp-0x48]
    int32_t base_id = -1;  // the rally point's base
    bool base_locked = false;
    GotoToBase = 1;
    auto restore_view = [&]() {
        if (map_win(main_view()).options != saved_options) {
            map_win(main_view()).options = saved_options;
            draw_map(1);
        }
    };
    auto finish = [&]() {  // L4d7b12
        if (mode == -2) {
            restore_view();
            return;
        }
        if (mode == -1) {
            NetDaemon_unlock_base(&NetState, CurrentBaseID);
            restore_view();
            return;
        }
        net_unlock_veh();
        restore_view();
    };
    if (mode == -1) {
        base_id = id;
        BASE& b = Bases[base_id];
        faction_id = (uint8_t)b.faction_id;
        x = b.x;
        y = b.y;
        set_base(base_id);
        if (NetDaemon_lock_base(&NetState, base_id, 0, -1, -1)) return;
        base_locked = true;
        if (!(saved_options & 0x40000)) {
            // The rally points of every base are shown while choosing.
            map_win(main_view()).options &= ~0x40;
            map_win(main_view()).options |= 0x40000;
            draw_map(1);
        }
        parse_says(0, b.name, -1, -1);
        BasePop_start(pop.p, ScriptFileName, "DESTTRIAD", -1, nullptr, 0x40, nullptr);
        for (int32_t i = 0; i < 3; i++) {
            StrBuffer[0] = 0;
            crt_strcat(StrBuffer, TriadName[i]);
            crt_strcat(StrBuffer, " ");
            crt_strcat(StrBuffer, "(");
            say_base(StrBuffer, (&b.autoforward_land_base_id)[i]);
            crt_strcat(StrBuffer, ")");
            BasePop_item(pop.p, StrBuffer, i);
        }
        triad = BasePop_exec_3(pop.p, 0, 0);
        if (triad < 0) {
            NetDaemon_unlock_base(&NetState, CurrentBaseID);
            restore_view();
            return;
        }
        parse_say(0, TriadName[triad], -1, -1);
        parse_says(1, b.name, -1, -1);
    } else if (mode == -2) {
        faction_id = CurrentPlayerFaction;
        triad = -1;
        x = MapWinCursorX[MapWinCursorIndex];
        y = MapWinCursorY[MapWinCursorIndex];
        GotoToBase = 0;
    } else {
        if (id < 0) {
            if (mode == -3) return;
            // No unit: a stand-in at the cursor (veh_fake).
            id = veh_fake(0, CurrentPlayerFaction);
            veh(id).x = (int16_t)MapWinCursorX[MapWinCursorIndex];
            veh(id).y = (int16_t)MapWinCursorY[MapWinCursorIndex];
            veh(id).state = 0;
        }
        if (net_lock_veh(&id, 0)) return;
        faction_id = veh(id).faction_id;
        x = veh(id).x;
        y = veh(id).y;
        triad = triad_of(id);
        parse_says(0, Units[veh(id).unit_id].name, -1, -1);
        if (unit_of(id).plan == PLAN_ARTIFACT) artifact = 1;
        if (veh_cargo(id) && stack_check(id, 2, 0xC, -1, -1)) artifact = 1;
        if (mode == -3) {
            // Psi gate: from a base with a gate not used this turn.
            int32_t from = base_at(x, y);
            const char* error = nullptr;
            if (from < 0) {
                error = "GATENOBASE";
            } else {
                int32_t offset, mask;
                bitmask(0x21, &offset, &mask);
                if (!(Bases[from].facilities_built[offset] & (uint8_t)mask)) {
                    error = "GATENOGATE";
                } else if (Bases[from].state_flags & 0x20000000) {
                    parse_says(0, Bases[from].name, -1, -1);
                    error = "GATEUSED";
                }
            }
            if (error) {
                pop_msg(error);
                net_unlock_veh();
                restore_view();
                return;
            }
        } else if (mode == 0x80) {
            // Bombing run: the targets within half the aircraft's range of its base.
            range = Chassis[unit_of(id).chassis_id].range == 1 ? speed(id, 0) / 2 : speed(id, 0);
            if (tile_has_base(map_tile(x, y))) {
                home_x = x;
                home_y = y;
            } else {
                action_home(id, 0);
                if (veh(id).order != ORDER_MOVE_TO) {
                    net_unlock_veh();
                    return;
                }
                home_x = veh(id).waypoint_x[0];
                home_y = veh(id).waypoint_y[0];
                veh(id).order = 0;
            }
        }
    }
    MAP* here = map_tile(x, y);
    int32_t region = here->region;  // [ebp-0x50]
    int32_t is_sea = (here->climate & 0xE0) < 0x60;  // [ebp-0x60]
    std::vector<int32_t> order(BaseCount > 0 ? BaseCount : 0), key(BaseCount > 0 ? BaseCount : 0);
    for (;;) {
        StrBuffer[0] = 0;
        switch (mode) {
        case ORDER_MOVE_TO: crt_strcat(StrBuffer, group ? "GROUPTO" : "GOTO"); break;
        case ORDER_ROAD_TO: crt_strcat(StrBuffer, "ROADTO"); break;
        case ORDER_MAGTUBE_TO: crt_strcat(StrBuffer, "TUBETO"); break;
        case 0x80: crt_strcat(StrBuffer, "BOMBTO"); break;
        case -1: crt_strcat(StrBuffer, "DESTTO"); break;
        case -2: crt_strcat(StrBuffer, "FINDTO"); break;
        case -3: crt_strcat(StrBuffer, "GATETO"); break;
        default: break;
        }
        BasePop_start(pop.p, ScriptFileName, StrBuffer, -1, nullptr, 0x42, nullptr);
        // Sort buttons: by name, by size, by distance; "zoom to base" when finding a base.
        BasePop_button(pop.p, label_get(451));
        BasePop_button(pop.p, label_get(452));
        BasePop_button(pop.p, label_get(453));
        BasePop_set_loc(pop.p, -10, -10);
        if (mode == -2) BasePop_button(pop.p, label_get(454));
        int32_t items = 0;  // [ebp-0x38]
        int32_t active = -1;  // [ebp-0x4a44]
        if (mode == -1) {
            // "(none)": no rally point.
            StrBuffer[0] = 0;
            crt_strcat(StrBuffer, "(");
            crt_strcat(StrBuffer, label_get(251));
            crt_strcat(StrBuffer, ")");
            BasePop_item(pop.p, StrBuffer, 9999);
            items = 1;
        }
        for (int32_t i = 0; i < BaseCount; i++) {
            order[i] = i;
            if (sort == 1) {
                key[i] = Bases[i].pop_size;
            } else if (sort == 2) {
                int32_t dx = wrap_dx(x - Bases[i].x);
                key[i] = -((std::abs(y - Bases[i].y) + dx) >> 1);
            }
        }
        if (sort) {
            for (bool swapped = true; swapped;) {  // bubble sort, largest key first
                swapped = false;
                for (int32_t i = 0; i < BaseCount - 1; i++) {
                    if (key[i] < key[i + 1]) {
                        swapped = true;
                        swap(&key[i], &key[i + 1]);
                        swap(&order[i], &order[i + 1]);
                    }
                }
            }
        } else {
            for (bool swapped = true; swapped;) {  // by name
                swapped = false;
                for (int32_t i = 0; i < BaseCount - 1; i++) {
                    if (crt_stricmp(Bases[order[i]].name, Bases[order[i + 1]].name) > 0) {
                        swapped = true;
                        swap(&order[i], &order[i + 1]);
                    }
                }
            }
        }
        for (int32_t n = 0; n < BaseCount; n++) {
            int32_t bid = order[n];
            BASE& b = Bases[bid];
            int32_t owner = (uint8_t)b.faction_id;
            bool listed = false;
            if (owner == faction_id) {
                if (mode == 0x80) continue;
            } else if (mode == 0x80) {
                if (!(Factions[faction_id].diplo_status[owner] & 0x10)) continue;  // vendetta only
                goto reach;
            } else {
                // Others' bases: pact partners only.
                if (mode < 0 || !(Factions[faction_id].diplo_status[owner] & 1)) continue;
            }
            if (mode == -3) {
                int32_t offset, mask;
                bitmask(0x21, &offset, &mask);
                if (!(b.facilities_built[offset] & (uint8_t)mask)) continue;
                listed = true;
            }
        reach:
            if (!listed) {
                // Reachable: same region (sea units: a port of the region, air units: in range).
                int32_t bx = b.x, by = b.y;
                int32_t bregion = map_tile(bx, by)->region;
                if (triad == TRIAD_SEA) {
                    if (is_sea) {
                        if (!base_on_sea(bid, region)) continue;
                    } else {
                        int32_t from = base_at(x, y);
                        if (bid >= 0 && !port_to_port(from, bid)) continue;
                    }
                } else if (triad == TRIAD_LAND) {
                    if (bregion != region) continue;
                } else if (triad == TRIAD_AIR) {
                    if (mode == 0x80) {
                        int32_t d = (std::abs(home_y - by) + wrap_dx(home_x - bx)) >> 1;
                        if (d > range) continue;
                    } else if (mode >= 0 && Chassis[unit_of(id).chassis_id].range) {
                        int32_t dx = wrap_dx(x - bx);
                        int32_t left = speed(id, 0) - veh(id).moves_spent;
                        if (left < 0) left = 0;
                        else if (left > 999) left = 999;
                        int32_t d = (std::abs(y - by) + dx) >> 1;
                        int32_t reach_tiles =
                            (speed(id, 0) * (Chassis[unit_of(id).chassis_id].range - veh(id).movement_turns - 1) +
                             left) /
                            Rules.move_rate_roads;
                        if (d > reach_tiles) continue;
                    }
                }
            }
            // Known bases only.
            if (owner != faction_id && !(b.visibility & (1 << (faction_id & 31))) &&
                !(GameState & STATE_OMNISCIENT_VIEW))
                continue;
            StrBuffer[0] = 0;
            crt_strcat(StrBuffer, b.name);
            crt_strcat(StrBuffer, "|");  // the rest is right-aligned
            if (owner != faction_id) {
                crt_strcat(StrBuffer, " ");
                crt_strcat(StrBuffer, "(");
                GenderDefault = MFactions[owner].noun_gender;
                PluralDefault = MFactions[owner].is_noun_plural;
                crt_strcat(StrBuffer, MFactions[owner].noun_faction);
                crt_strcat(StrBuffer, ")");
            }
            if (artifact && owner == faction_id) {
                // Where an artifact can go: the Planetary Datalinks or a network node.
                const char* what = nullptr;
                if (SecretProjects[FAC_UNIVERSAL_TRANSLATOR - SP_ID_First] == bid) {
                    what = Facility[0].name;  // DAT_009A5C48 (the facility name the original reads)
                } else {
                    int32_t offset, mask;
                    bitmask(8, &offset, &mask);
                    if ((b.facilities_built[offset] & (uint8_t)mask) && !(b.state_flags & 0x400)) what = label_get(305);
                }
                if (what) {
                    crt_strcat(StrBuffer, " ");
                    crt_strcat(StrBuffer, "(");
                    crt_strcat(StrBuffer, what);
                    crt_strcat(StrBuffer, ")");
                }
            }
            if (mode == -3) {
                crt_strcat(StrBuffer, " ");
                crt_strcat(StrBuffer, label_get((b.state_flags & 0x20000000) ? 530 : 529));
            } else {
                crt_strcat(StrBuffer, " ");
                crt_strcat(StrBuffer, label_get(250));
                crt_strcat(StrBuffer, ": ");
                int32_t dx = wrap_dx(x - b.x);
                char num[80];
                crt_itoa((std::abs(y - b.y) + dx) >> 1, num, 10);
                crt_strcat(StrBuffer, num);
                if (sort == 1) {
                    crt_strcat(StrBuffer, ", ");
                    crt_strcat(StrBuffer, label_get(579));
                    crt_strcat(StrBuffer, ": ");
                    char size[80];
                    crt_itoa(b.pop_size, size, 10);
                    crt_strcat(StrBuffer, size);
                }
            }
            BasePop_item(pop.p, StrBuffer, bid);
            items++;
            // The initial selection: the last base chosen, the rally point, the home base.
            if (mode == -2) {
                if (bid == GotoLastBase) active = bid;
            } else if (mode == -1) {
                if (bid == (&Bases[base_id].autoforward_land_base_id)[triad]) active = bid;
            } else if (GotoLastBase < 0) {
                if (bid == veh(id).home_base_id) active = bid;
            } else if (bid == GotoLastBase) {
                active = bid;
            }
        }
        if (!items) {
            if (mode == -3) pop_msg("GATENODEST");
            finish();
            return;
        }
        BasePop_set_active(pop.p, active);
        // PlanWin (PlanWin_show (0x48BC60)) in the world map pane, from the route's start (DAT_00915680 /
        // DAT_00915650); the list's selection callback (Popup +0x3280) redraws it.
        GotoFromX = x;
        GotoFromY = y;
        planwin_open();
        BasePop_set_select_callback(pop.p, Console_goto_routine);
        Console_goto_routine(BasePop_selected_id(pop.p));  // Dialog_get_selected_id
        int32_t chosen = BasePop_exec_3(pop.p, 0, 0);
        planwin_close();  // PlanWin_hide (0x48BC50)
        if (chosen < 0) {
            finish();
            return;
        }
        GotoLastBase = chosen;
        int32_t button = BasePop_button_pressed(pop.p);
        if (button >= 1 && button <= 3) {
            sort = button - 1;
            prefs_put_int("Base Sort", (uint32_t)sort, 0);
            continue;
        }
        BASE& t = Bases[chosen < BaseCount && chosen >= 0 ? chosen : 0];
        if (mode == -2) {
            Console_cursor_next(&MainConsole, t.x, t.y);
            pfncMoveMap(main_view(), t.x, t.y, 1);
            if (button == 4 && (uint8_t)t.faction_id == (uint32_t)CurrentPlayerFaction)
                BaseWin_zoom(&BaseWin, chosen, 0);
            restore_view();
            return;
        }
        if (mode == -1) {
            if (chosen == 9999) chosen = -1;
            (&Bases[base_id].autoforward_land_base_id)[triad] = (int16_t)chosen;
            draw_map(1);
            NetDaemon_unlock_base(&NetState, CurrentBaseID);
            restore_view();
            return;
        }
        if (mode == -3) {
            if (t.x != veh(id).x || t.y != veh(id).y) {
                log_say_values("Psi Gate teleport", id, chosen, 0);
                if (!NetDaemon_add_lock(&NetState, 0, t.x, t.y)) {
                    if (MultiplayerActive) {
                        message_veh(0x2410, id, chosen, 0);
                        NetDaemon_await_exec(&NetState, 1);
                    } else {
                        action_gate(id, chosen);
                    }
                }
            }
            net_unlock_veh();
            restore_view();
            return;
        }
        log_say_values("Setting special veh orders", id, mode, 0);
        VEH& v = veh(id);
        if (mode == 0x80) {
            v.state = (v.state & 0xFCFFFFFF) | 0x200;
            v.order_auto_type = 0xA;
            v.waypoint_x[1] = t.x;
            v.waypoint_y[1] = t.y;
        } else {
            v.order = (uint8_t)mode;
            v.waypoint_x[0] = t.x;
            v.waypoint_y[0] = t.y;
            if (mode == ORDER_MOVE_TO) {
                v.state &= 0xFFFEFFFF;
                if (group) Console_group(self, id);
            }
        }
        ui_synch_veh(id);
        net_await_synch();
        finish();
        (void)base_locked;
        return;
    }
}

// ---- load ---------------------------------------------------------------------------------------------

void THISCALL Console_load_proc(uint8_t* self, int32_t scenario, int32_t relinearize) {
    BaseWin_exit(&BaseWin);
    if (MultiplayerActive && !(GameState & STATE_DEBUG_MODE)) return;
    if (PbemActive && !(GameState & STATE_DEBUG_MODE)) return;
    if (load_game(scenario, relinearize)) return;
    if (GameAltPathChanged) {
        if (!game_reload(1, 0)) goto shown;
        crt_exit(3);
    }
    // desktop_init: the windows are reset for the loaded game (OpenSMAC: the faction art, flags and
    // the main view, as OpenSMAC's start of a game).
    desktop_init(0);
    for (int32_t f = 1; f < 8; f++) load_faction_art(f);
    load_flags();
    MapWin_clear(main_view(), 0);
    draw_map(1);
shown:
    if (scenario) {
        GameState |= (STATE_SCENARIO_CHEATED_FLAG | STATE_SCENARIO_EDITOR | STATE_OMNISCIENT_VIEW);
        draw_map(1);
    }
    pfncMainMenuUpdateVisible(self + 0x22A2C, console_win(self).veh);
}

// ---- Console_assemble (0x4D95F0) ---------------------------------------------------------------

// Console_clear_group (0x50F650): no unit in the group.
static void console_clear_group(uint8_t* self) {
    console_win(self).unk_23d1c = 0;
    for (int32_t i = 0; i < VehCount; i++) Vehs[i].state &= ~0x8000000u;
}

// Console_assemble (0x4D95F0): the ASSEMBLEGROUP dialog (its check boxes kept as the "Vehicle
// Groups" preference): the faction's units of the chosen kinds join the group (bit 0x8000000);
// the dialog's first button sends the last of them to the cursor (go-to, the group follows it),
// the second wakes them all.
void THISCALL Console_assemble(uint8_t* self) {
    uint32_t prefs = (uint32_t)prefs_get_int("Vehicle Groups", 7, 0);
    for (int32_t i = 0; i < 0xC; i++) {
        uint32_t bit = 1u << i;
        if (prefs & bit) DialogChoices |= (int32_t)bit;
        else DialogChoices &= ~(int32_t)bit;
    }
    if (X_pop_6("ASSEMBLEGROUP", 0x41, 0) < 0) return;
    console_clear_group(self);
    uint32_t kinds = 0;
    for (int32_t i = 0; i < 0xC; i++)
        if (DialogChoices & (1 << i)) kinds |= 1u << i;
    console_win(self).unk_23d1c = 1;
    int32_t last = -1;
    int32_t cx = MapWinCursorX[MapWinCursorIndex], cy = MapWinCursorY[MapWinCursorIndex];
    for (int32_t i = 0; i < VehCount; i++) {
        VEH& v = Vehs[i];
        if ((uint32_t)v.faction_id != (uint32_t)console_win(self).faction) continue;
        if (kinds & 1) {  // near the cursor
            int32_t dx = std::abs(v.x - cx);
            if (!(MapToggleFlat & 1) && MapHalfX < dx) dx = MapAreaX - dx;
            int32_t dy = std::abs(v.y - cy);
            if (4 < (int32_t)((uint32_t)(dy + dx) & 0xFFFFFFFEu)) continue;
        }
        const UNIT& u = Units[v.unit_id];
        int32_t triad = Chassis[u.chassis_id].triad;
        int32_t plan = u.plan;
        if ((kinds & 2) && (v.order == 1 || v.order == 2)) continue;
        if ((kinds & 4) && ((v.state & 0x1000200) == 0x1000200 || (v.state & 0x4200))) continue;
        if ((kinds & 8) && triad == 0) continue;
        if ((kinds & 0x10) && triad == 1) continue;
        if ((kinds & 0x20) && triad == 2) continue;
        if ((kinds & 0x40) && (plan == 0 || plan == 1 || plan == 5 || plan == 6)) continue;
        if ((kinds & 0x80) && (plan == 2 || plan == 4)) continue;
        if ((kinds & 0x100) && plan == 9) continue;
        if ((kinds & 0x200) && plan == 0xB) continue;
        if ((kinds & 0x400) && (plan == 8 || plan == 10 || plan == 7 || plan == 0xC)) continue;
        v.state |= 0x8000000;
        last = i;
    }
    if (DialogToggle == 1 && last >= 0) {
        VEH& v = Vehs[last];
        v.order = 0x18;
        v.waypoint_x[0] = (int16_t)cx;
        v.waypoint_y[0] = (int16_t)cy;
        v.state &= 0xFCFFBDFF;
        Console_group(self, last);
    }
    if (DialogToggle == 2) {
        for (int32_t i = 0; i < VehCount; i++) {
            VEH& v = Vehs[i];
            if ((uint32_t)v.faction_id == (uint32_t)console_win(self).faction && (v.state & 0x8000000)) {
                v.order = 0;
                v.state &= 0xFCFFBDFF;
            }
        }
    }
    draw_map(1);
    prefs_put_int("Vehicle Groups", kinds, 0);
}
