// Console: the human player's turn and the main view's keys (see console.h).
//
// Every function follows the asm of the original; the object is MainConsole (THISCALL `self`).
// Calls to window code that is not reconstructed yet (status/info panes, menus, cursor drawing,
// sounds, most orders) go to placeholders (console_placeholders.cpp) or the bridge's headless
// versions; their game-state effects, where they have any, are noted at the call.
#include "ui/statuswin.h"
#include "ui/console.h"
#include "ui/infowin.h"
#include "ui/sound.h"
#include "ui/mapwin.h"
#include "ui/help.h"
#include "ui/techwin.h"
#include "ui/reports.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "engine/action.h"
#include "engine/ai.h"
#include "engine/base.h"
#include "engine/compat.h"
#include "engine/crt.h"
#include "engine/design.h"
#include "engine/diplo_act.h"
#include "engine/diplomacy.h"
#include "engine/goody.h"
#include "engine/log.h"
#include "engine/map.h"
#include "engine/move.h"
#include "engine/net.h"
#include "engine/parse.h"
#include "engine/prefs.h"
#include "engine/path.h"
#include "engine/probe.h"
#include "engine/random.h"
#include "engine/rules.h"
#include "engine/save.h"
#include "engine/social.h"
#include "engine/stack.h"
#include "engine/tech.h"
#include "engine/turn.h"
#include "engine/veh.h"
#include "engine/zoc.h"
#include "platform/ui_platform.h"
#include "ui/basewin.h"
#include "ui/console_frame.h"
#include "ui/win.h"

namespace {

uint8_t* main_view() { return MapWins[0]; }  // ppMain
uint8_t* main_menu(uint8_t* self) { return self + 0x22A2C; }  // the console's menu bar object

// Inlined in several places of the original: the cursor is hidden before it moves, after a
// pending full redraw.
void hide_cursor(uint8_t* self) {
    mw(self, kConCursorShown) = 0;
    if (mw(self, kConRedraw)) {
        mw(self, kConRedraw) = 0;
        pfncRedrawMap(main_view(), 1);
    }
    MapWin_draw_cursor(main_view());
}

// The status displays after a selection (inlined sequence of the original).
void update_panes(uint8_t* self) {
    InfoWin_change(&InfoWindow, 0);
    StatusWin_redraw(&InfoWin);  // the status window object (0x8C5568)
    MapWin_main_caption(main_view());
}

bool tile_has_own_base(const MAP* sq) { return (sq->items & 1) && (sq->val2 & 0xF) < 8; }

int32_t triad_of(int32_t veh_id) { return Chassis[Units[Vehs[veh_id].unit_id].chassis_id].triad; }

}  // namespace

// Console_set_view (0x511480), also inlined by the original in Console_human_turn and others:
// leaves unit mode for view mode (cursor shown, panes redrawn).
void THISCALL Console_set_view(uint8_t* self, int32_t force) {
    if (!MapWinInVehMode && !force) return;
    ambience(0x1E);
    mw(self, kConVehMode) = 0;
    mw(self, kConCursorShown) = 1;
    mw(self, kConCursorOn) = 1;
    if (mw(self, kConRedraw)) {
        mw(self, kConRedraw) = 0;
        pfncRedrawMap(main_view(), 1);
    }
    draw_cursor();
    update_panes(self);
    pfncMainMenuUpdateVisible(main_menu(self), mw(self, kConVeh));
}

// ---- net layer, single-player paths --------------------------------------------------------------

int32_t net_lock_veh(int32_t* veh_id, int32_t a2) {
    if (!MultiplayerActive) return 0;  // NetDaemon_lock_veh (0x531020)
    return NetDaemon_lock_veh(&NetState, veh_id, a2, -1, -1, 0);
}
void net_unlock_veh() {
    if (MultiplayerActive) {
        NetDaemon_unlock_veh(&NetState);
        return;
    }
    // NetDaemon_unlock_veh (0x5310F0) without multiplayer: the lock fields are cleared.
    mapwin_field(&NetState, 0x1B78) = 0;
    mapwin_field(&NetState, 0x1BC4) = 0;
}
void net_synch_veh(int32_t veh_id) {
    if (!MultiplayerActive) return;  // synch_veh -> NetDaemon_synch, NetDaemon_await_synch: nothing
    synch_veh(veh_id);
    NetDaemon_await_synch(&NetState);
}
int32_t net_order_veh(int32_t veh_id, int32_t dir, int32_t a3) {
    if (!MultiplayerActive) return order_veh(veh_id, dir, 3);  // NetDaemon_order_veh (0x531780)
    return NetDaemon_order_veh(&NetState, veh_id, dir, a3);
}
int32_t net_action(int32_t veh_id, int32_t a2) {
    if (!MultiplayerActive) return action(veh_id);  // NetDaemon_action (0x532570)
    return NetDaemon_action(&NetState, veh_id, a2);
}

void ui_synch_veh(int32_t veh_id) {
    if (MultiplayerActive) synch_veh(veh_id);
}
void ui_synch_base(int32_t base_id) {
    if (MultiplayerActive) synch_base(base_id);
}
void net_await_synch() {
    if (MultiplayerActive) NetDaemon_await_synch(&NetState);
}
int32_t net_add_global() { return MultiplayerActive ? NetDaemon_add_global(&NetState) : 0; }

int32_t GAMEAPI not_my_turn() {
    if (!MultiplayerActive) return 0;
    if (!(GameMoreRules & 0x10)) return 0;
    return CurrentFaction != CurrentPlayerFaction;
}

int32_t THISCALL Console_edit_lock(uint8_t*) {
    if (!(GameState & STATE_SCENARIO_EDITOR)) return 0;
    if (!(ui_key_state(0x91) & 1) && !(GameState & STATE_EDITOR_ONLY_MODE)) return 0;  // GetKeyState(VK_SCROLL) toggle
    return 1;
}

// ---- cursor and selection -------------------------------------------------------------------------

void THISCALL Console_cursor_next(uint8_t* self, int32_t x, int32_t y) {
    if ((x ^ y) & 1) {
        if (GameState & STATE_DEBUG_MODE) danger("Bad Cursor Move", "", x, y, 0);
        if (x & 1) x--;
        if (y & 1) x++;
    }
    StatusWin_set_loc(&InfoWin, x, y);
    if (x != MapWinCursorX[MapWinCursorIndex] || y != MapWinCursorY[MapWinCursorIndex]) {
        int32_t i = (mw(self, kConCursorHead) + 1) & 0x1F;  // the original's signed modulo of a positive value
        mw(self, kConCursorHead) = i;
        mw(self, kConCursorIdx) = i;
        mw(self, kConCursorX + i * 4) = x;
        mw(self, kConCursorY + mw(self, kConCursorHead) * 4) = y;
    }
}

int32_t THISCALL Console_focus(uint8_t* self, int32_t x, int32_t y, int32_t faction_id) {
    int32_t changed = 0;
    uint32_t bits = faction_id < 0 ? 0x60000000u : (faction_id != CurrentPlayerFaction ? 0x40000000u : 0x20000000u);
    for (int32_t i = 0; i < 8; i++) {
        uint8_t* w = MapWins[i];
        if (!w || (i && !mw(w, kMwActive))) continue;
        bool follow;  // the main view follows the cursor
        if (!(bits & (uint32_t)mw(w, kMwOptions))) {
            if (i || !ControlTurnC) continue;
            follow = true;
        } else {
            follow = i == 0;
        }
        if (follow) {
            Console_cursor_next(&MainConsole, x, y);
            if (mw(self, kConRedraw)) {
                mw(self, kConRedraw) = 0;
                if (MapWin_focus(main_view(), x, y)) changed = 1;
                else pfncRedrawMap(main_view(), 1);
                continue;
            }
        }
        if (MapWin_focus(MapWins[i], x, y) && i == 0) changed = 1;
    }
    if (changed) flush_input();
    return changed;
}

int32_t THISCALL Console_ready_search(uint8_t* self, int32_t veh_id) {
    int32_t best = 9999, found = -1;
    int32_t cx, cy;
    if (veh_id < 0 || Vehs[veh_id].faction_id != CurrentPlayerFaction) {
        veh_id = -1;
        cx = MapWinCursorX[MapWinCursorIndex];
        cy = MapWinCursorY[MapWinCursorIndex];
    } else {
        cx = Vehs[veh_id].x;
        cy = Vehs[veh_id].y;
    }
    int32_t pass = MultiplayerActive ? -1 : 0;
    for (; pass <= 2; pass++) {
        if (MultiplayerActive && pass == 0) {
            int32_t next = mw(self, kConNextVeh);
            if (next >= 0 && next < VehCount && Vehs[next].faction_id == CurrentPlayerFaction && veh_ready(next)) {
                found = mw(self, kConNextVeh);
                mw(self, kConNextVeh) = -1;
                break;
            }
        }
        for (int32_t i = 0; i < VehCount; i++) {
            if (!veh_ready(i)) continue;
            uint32_t state = Vehs[i].state;
            // The first multiplayer pass looks at units with orders or automation only.
            if (pass < 0 && !(state & 0x200) && (state & 0x1000200) != 0x1000200 && !(state & 0x4000) && !Vehs[i].order)
                continue;
            if (Vehs[i].state & 2) continue;  // waiting (w)
            int32_t dx = std::abs(cx - Vehs[i].x);
            if (!(MapToggleFlat & 1) && MapHalfX < dx) dx = MapAreaX - dx;
            int32_t dy = std::abs(cy - Vehs[i].y);
            int32_t ax = std::abs(dx), ay = std::abs(dy);
            int32_t hi = ax <= ay ? ay : ax;
            int32_t lo = ay <= ax ? ay : ax;
            int32_t dist = hi - ((((ax + ay) >> 1) - lo + 1) >> 1);
            if (dist < best || (dist == best && i == mw(self, kConVeh))) {
                best = dist;
                found = i;
            }
        }
        if (found >= 0) break;
        if (pass >= 0) {
            // Nothing ready: the units told to wait come back.
            for (int32_t i = 0; i < VehCount; i++) {
                if (pass != 0 || i != mw(self, kConVeh) || veh_id < 0) Vehs[i].state &= ~2u;
            }
        }
    }
    if (MultiplayerActive && pass < 0) {
        mw(self, kConMpSearch) = 1;
        return found;
    }
    mw(self, kConMpSearch) = 0;
    return found;
}

void THISCALL Console_ready(uint8_t* self) {
    mw(self, kConCursorShown) = 0;
    draw_cursor();
    int32_t veh_id = Console_ready_search(self, mw(self, kConSearchReset) ? -1 : mw(self, kConVeh));
    mw(self, kConVeh) = veh_id;
    mw(self, kConSearchReset) = 0;
    if (veh_id < 0) return;
    if (!MapWinInVehMode) return;
    if (!Vehs[veh_id].order || (int8_t)Vehs[veh_id].order > 0x17) {
        int32_t y = Vehs[veh_id].y, x = Vehs[veh_id].x;
        hide_cursor(self);
        Console_cursor_next(self, x, y);
        update_panes(self);
        veh_id = mw(self, kConVeh);
        uint32_t state = Vehs[veh_id].state;
        if (!(GamePreferences & PREF_ADV_NO_CENTER_VEH_ORDERS) ||
            (!Vehs[veh_id].order && !(state & 0x200) && (state & 0x1000200) != 0x1000200)) {
            if (!Console_focus(self, Vehs[veh_id].x, Vehs[veh_id].y, Vehs[veh_id].faction_id)) {
                // Recentre unless the unit is at least 3 tiles inside the view (and high
                // terrain does not hide it at the top).
                y = Vehs[mw(self, kConVeh)].y;
                x = Vehs[mw(self, kConVeh)].x;
                int32_t oy = mw(self, kMwOriginY), ox = mw(self, kMwOriginX);
                bool inside = false;
                uint8_t climate = 0;
                bool off_map = y < 0 || MapAreaY <= y || x < 0 || MapAreaX <= x;
                if (!off_map) climate = map_tile(x, y)->climate;
                if ((off_map || (climate & 0xE0) < 0x61 || oy + 3 <= (y - (climate >> 5)) + 3) && oy + 3 <= y &&
                    y < mw(self, kMwRowsBelow) + mw(self, kMwRowsAbove) - 3 + oy) {
                    int32_t wx = x;
                    int32_t right = mw(self, kMwColsEven) + mw(self, kMwColsOdd) - 3 + ox;
                    if (!(MapToggleFlat & 1)) {
                        if (x < ox + 3) wx = x + MapAreaX;
                        if (right <= wx) wx -= MapAreaX;
                    }
                    inside = ox + 3 <= wx && wx < right;
                }
                if (!inside) pfncMoveMap(main_view(), x, y, 1);
            }
        }
        pfncMainMenuUpdateVisible(main_menu(self), mw(self, kConVeh));
        flush_input();
        Vehs[mw(self, kConVeh)].iter_count = 0;
    }
    veh_id = mw(self, kConVeh);
    int32_t base_id = base_at(Vehs[veh_id].x, Vehs[veh_id].y);
    veh_id = mw(self, kConVeh);
    VEH& v = Vehs[veh_id];
    if (!v.order && !(v.state & 0x200) && (v.state & 0x1000200) != 0x1000200 && !(v.state & 0x4000) && base_id >= 0) {
        // A unit in a base with a rally point for its triad goes there.
        int32_t triad = Chassis[Units[v.unit_id].chassis_id].triad;
        int16_t target = (&Bases[base_id].autoforward_land_base_id)[triad];
        if (target >= 0 && target != base_id) {
            v.order = ORDER_MOVE_TO;
            v.waypoint_x[0] = Bases[target].x;
            v.waypoint_y[0] = Bases[target].y;
        }
    }
    veh_id = mw(self, kConVeh);
    VEH& u = Vehs[veh_id];
    if (u.faction_id == CurrentPlayerFaction && (u.flags & 0x1000) && !u.order && !(u.state & 0x200)) {
        // First selection of a new unit: its sound.
        u.flags &= ~0x1000;
        const UNIT& unit = Units[u.unit_id];
        int32_t sound = -1;
        switch (Chassis[unit.chassis_id].triad) {
        case 0:
            if (!unit.chassis_id && Weapon[unit.weapon_id].offense_value) sound = 0x40;
            else sound = Weapon[unit.weapon_id].offense_value ? 0x41 : 0x42;
            break;
        case 1: sound = Weapon[unit.weapon_id].offense_value ? 0x45 : 0x46; break;
        case 2:
            if (!Chassis[unit.chassis_id].range) sound = 0x43;
            else sound = Chassis[unit.chassis_id].range == 1 ? 0x44 : 0x47;
            break;
        default: break;
        }
        if (sound >= 0) FX_play_loc(&Sounds, sound, u.x, u.y);
    }
    ambience_veh(mw(self, kConVeh));
    flush_input();
}

void THISCALL Console_set_move(uint8_t* self, int32_t force) {
    bool shown = false;
    if (MultiplayerActive) {
        if (ControlTurnC || NetLocked) return;
        int32_t busy;
        if (!(GameMoreRules & 0x10)) {
            busy = NetSimultaneous;
            if (NetTurnDoneMask & (1 << (mw(self, kConFaction) & 31))) return;
        } else {
            busy = not_my_turn();
        }
        if (busy) return;
    }
    if (!MapWinInVehMode || force) {
        mw(self, kConVehMode) = 0;
        mw(self, kConCursorShown) = 0;
        mw(self, kConCursorOn) = 1;
        shown = true;
        if (mw(self, kConRedraw)) {
            mw(self, kConRedraw) = 0;
            pfncRedrawMap(main_view(), 1);
        }
        draw_cursor();
        mw(self, kConVehMode) = 1;
        mw(self, kConWaiting) = 0;
    }
    if (!veh_selectable(mw(self, kConVeh))) {
        Console_ready(self);
        shown = false;
    } else {
        ambience_veh(mw(self, kConVeh));
    }
    if (mw(self, kConVeh) >= 0 && veh_selectable(mw(self, kConVeh))) {
        if (!Vehs[mw(self, kConVeh)].moves_spent && !MultiplayerActive) spot_all(mw(self, kConVeh), 0);
        int32_t y = Vehs[mw(self, kConVeh)].y, x = Vehs[mw(self, kConVeh)].x;
        hide_cursor(self);
        Console_cursor_next(self, x, y);
        if (shown) {
            update_panes(self);
            Console_focus(self, MapWinCursorX[MapWinCursorIndex], MapWinCursorY[MapWinCursorIndex],
                          Vehs[mw(self, kConVeh)].faction_id);
            pfncMainMenuUpdateVisible(main_menu(self), mw(self, kConVeh));
        }
        mw(self, kConMoveSet) = 1;
        return;
    }
    mw(self, kConWaiting) = 0;
    if (MapWinInVehMode) Console_set_view(self, 1);  // inlined
    StatusWin_set_loc(&InfoWin, MapWinCursorX[MapWinCursorIndex], MapWinCursorY[MapWinCursorIndex]);
    Console_focus(self, MapWinCursorX[MapWinCursorIndex], MapWinCursorY[MapWinCursorIndex], mw(self, kConFaction));
}

void THISCALL Console_update_data(uint8_t*, int32_t a1) {
    InfoWin_change(&InfoWindow, a1);
    StatusWin_redraw(&InfoWin);
    MapWin_main_caption(main_view());
}

// ---- the human turn ---------------------------------------------------------------------------------

int32_t THISCALL Console_veh_turn(uint8_t* self) {
    mw(self, kConWaiting) = 1;
    int32_t waited = 0;  // [ebp-0x8]
    bool looped = false;  // [ebp-0x10]
    int32_t auto_moves = 0;  // [ebp-0x4]: moves of an automated unit / order steps this turn
    if ((GamePreferences & PREF_BSC_TUTORIAL_MSGS) && !Vehs[mw(self, kConVeh)].order) Console_give_hints(self);
    int32_t& veh = mw(self, kConVeh);
    int32_t ready = veh_ready(veh);
    for (;;) {
        // Leave when the unit changed state, the player is done or the game ends.
        bool stop;
        int32_t sel = veh_selectable(veh);
        if ((!sel || veh_ready(veh) != ready || (!ready && Vehs[veh].order)) && MapWinInVehMode) stop = true;
        else
            stop = (!mw(self, kConWaiting) && mw(self, kConNoFocus) != 2) || !mw(self, kConTurnActive) ||
                   ControlTurnA || end_of_game(0) || (MultiplayerActive && (GameState & 0x800));
        if (stop) break;
        looped = true;
        if (mw(self, kConNoFocus) == 2) {
            uint8_t order = Vehs[veh].order;
            mw(self, kConNoFocus) = 0;
            mw(self, kConWaiting) = order == ORDER_MOVE_TO;
        }
        if (NetSimultaneous) {
            if (!MapWinInVehMode) {
                waited = 1;
                wait_loop();
            } else {
                Console_set_view(self, 1);  // inlined
            }
            continue;
        }
        if (mw(self, kConMpSearch)) {
            uint32_t state = Vehs[veh].state;
            if (!(state & 0x200) && (state & 0x1000200) != 0x1000200 && !(state & 0x4000) && !Vehs[veh].order) {
                mw(self, kConMpSearch) = 0;
                veh = -1;
                break;
            }
        }
        if (MapWinInVehMode) {
            if (!veh_ready(veh)) {
                if (Vehs[veh].order || (Vehs[veh].state & 0x4200)) {
                    veh = -1;
                    break;
                }
            } else if (!Win_is_visible(&BaseWin) && !Win_is_visible(&ReportWindow)) {
                int32_t v = veh;
                if (Vehs[v].state & 0x4200) {
                    // Automated unit: the AI moves it (at most 20 steps).
                    if (MultiplayerActive) {
                        console_not_reconstructed("NetDaemon_automated_lock");
                        break;
                    }
                    NetBusyCount++;
                    auto_moves++;
                    if (auto_moves < 0x15) {
                        if (enemy_veh(veh)) {
                            if (MultiplayerActive) {
                                net_unlock_veh();
                                veh = -1;
                            }
                            NetBusyCount--;
                            break;
                        }
                        NetBusyCount--;
                    } else {
                        veh_skip(veh);
                        if (MultiplayerActive) {
                            net_synch_veh(veh);
                            net_unlock_veh();
                        }
                        NetBusyCount--;
                    }
                    continue;
                }
                uint8_t order = Vehs[v].order;
                if (!order) {
                    auto_moves = 0;
                } else {
                    // A unit with orders acts; it gives up after 100 steps (20 for a go-to).
                    auto_moves++;
                    if ((order != ORDER_MOVE_TO ? 100 - 80 : 100) < auto_moves) {
                        if (order == ORDER_MOVE_TO) Vehs[v].order = 0;
                        else veh_skip(v);
                        if (MultiplayerActive) {
                            net_synch_veh(veh);
                            net_unlock_veh();
                        }
                        continue;
                    }
                }
                if (net_action(v, 0)) continue;
                Vehs[veh].state |= 0x4000000;
            }
        }
        waited = 1;
        wait_loop();
        if (TutorialTourPending) {
            TutorialTourPending = 0;
            TutWin_tour(&TutWin);
        }
    }
    mw(self, kConWaiting) = 0;
    if (!looped) veh = -1;
    return waited;
}

int32_t THISCALL Console_human_turn(uint8_t* self) {
    int32_t waited = 0;  // [ebp-0x8] in part: units were waited for (ENDOFTURN vs ENDOFTURN1)
    int32_t veh_count = -1;  // [ebp-0x8]: VehCount when the selection was last checked
    ConsoleTurnFlag = 0;
    WorldWin_set_world_map(&WorldWin);
    GameState &= ~0x400;
    ControlTurnStarted = 1;
    mw(self, kCon23D1C) = 0;
    for (int32_t i = 0; i < VehCount; i++) Vehs[i].state &= ~0x8000000u;
    if (!ControlTurnMove) {
        mw(self, kConVeh) = -1;
        Console_set_move(self, 0);
    } else if (!MapWinInVehMode) {
        Console_set_view(self, 1);  // inlined
        pfncMainMenuUpdateVisible(main_menu(self), mw(self, kConVeh));
    } else {
        Console_set_move(self, 1);
    }
    pfncMainMenuUpdateVisible(main_menu(self), mw(self, kConVeh));
    StatusWin_reset(&InfoWin);
    InfoWin_reset(&InfoWindow);
    mw(self, kConTurnActive) = 1;
    mw(self, kConMpSearch) = 0;
    if (ControlTurnMove) {
        int32_t v = mw(self, kConVeh);
        if ((v < 0 || !veh_ready(v) || (Vehs[v].state & 0x4200) || Vehs[v].order) && MapWinInVehMode)
            Console_set_view(self, 1);  // inlined
    }
    if (MultiplayerActive) FX_play(&Sounds, 0x21);
    ControlTurnMove = 0;
    for (;;) {
        if ((GameState & STATE_VICTORY_CONQUER) && !(GameState & STATE_GAME_DONE) &&
            (GameRules & RULES_VICTORY_CONQUEST)) {
            // Conquest victory reached: announced once.
            MFaction& m = MFactions[CurrentPlayerFaction];
            PluralDefault = 0;
            GenderDefault = m.is_leader_female;
            parse_says(0, m.title_leader, -1, -1);
            PluralDefault = 0;
            GenderDefault = m.is_leader_female;
            parse_says(1, m.name_leader, -1, -1);
            GenderDefault = m.noun_gender;
            PluralDefault = m.is_noun_plural;
            parse_says(2, m.noun_faction, -1, -1);
            X_pop("CONQUERPLANET", 0);
            GameState |= STATE_GAME_DONE;
        }
        int32_t v = mw(self, kConVeh);
        bool keep = false;  // the selected unit can go on
        if (v >= 0 && VehCount == veh_count) keep = (mw(self, kConAnySelect) ? veh_selectable(v) : veh_ready(v)) != 0;
        if (!keep) {
            Console_ready(self);
        } else if (MapWinInVehMode && !mw(self, kConMoveSet)) {
            int32_t y = Vehs[mw(self, kConVeh)].y, x = Vehs[mw(self, kConVeh)].x;
            hide_cursor(self);
            Console_cursor_next(self, x, y);
            if (!mw(self, kConNoFocus) || Vehs[mw(self, kConVeh)].order != ORDER_MOVE_TO) {
                VEH& u = Vehs[mw(self, kConVeh)];
                Console_focus(self, u.x, u.y, u.faction_id);
                update_panes(self);
                pfncMainMenuUpdateVisible(main_menu(self), mw(self, kConVeh));
            }
        }
        veh_count = VehCount;
        mw(self, kConMoveSet) = 0;
        mw(self, kConNoFocus) = 0;
        mw(self, kConAnySelect) = 0;
        if (mw(self, kConVeh) < 0) {
            // No unit needs orders: the turn can end.
            GameState |= 2;
            if (Win_is_visible(&BaseWin) || Win_is_visible(&ReportWindow)) GameState |= 4;
            mw(self, kConTurnActive) = 1;
            if (!Console_edit_lock(self)) {
                if (!(GamePreferences & PREF_BSC_PAUSE_END_TURN) && !MultiplayerActive && !PbemActive && waited &&
                    !(GameState & 4)) {
                    mw(self, kConTurnActive) = 0;  // automatic end of turn
                } else if (!(GamePreferences & PREF_BSC_PAUSE_END_TURN) || MultiplayerActive) {
                    parse_num(0, game_year(CurrentTurn));
                    NetMsg_pop(&NetMsg, waited ? "ENDOFTURN" : "ENDOFTURN1", 5000, 0, nullptr);
                }
            }
            if (!mw(self, kConTurnActive)) break;
            Console_set_view(self, 1);  // inlined
            Console_focus(self, MapWinCursorX[MapWinCursorIndex], MapWinCursorY[MapWinCursorIndex],
                          mw(self, kConFaction));
        }
        Console_turn_hints(self);
        if (mw(self, kConVeh) < 0) {
            GameState &= ~4;
            sound_turn_complete();  // the "turn complete" voice (FX slot 57) unless it is playing
            ambience(0xCB);
            mw(self, kConWaiting) = 1;
            int32_t count = VehCount;
            while (!MapWinInVehMode && mw(self, kConWaiting) && mw(self, kConTurnActive) && !ControlTurnA &&
                   !end_of_game(0) && count == VehCount && (!MultiplayerActive || !(GameState & 0x800))) {
                wait_loop();
                bool tour = TutorialTourPending != 0;
                TutorialTourPending = 0;
                if (tour) TutWin_tour(&TutWin);
            }
            mw(self, kConWaiting) = 0;
        } else {
            GameState &= ~6;
            waited |= Console_veh_turn(self);
        }
        if (ControlTurnA) return 0;
        if (GameState & 4) waited = 0;
        if (mw(self, kConCursorShown)) draw_cursor();
        if (!mw(self, kConTurnActive) || ControlTurnA || (MultiplayerActive && (GameState & 0x800)) || end_of_game(0))
            break;
    }
    ambience(0x1E);
    flush_input();
    mw(self, kConTurnActive) = 0;
    go_reset();
    return 0;
}

// ---- unit orders ------------------------------------------------------------------------------------

void THISCALL Console_skip(uint8_t* self, int32_t veh_id) {
    if (veh_ready(veh_id) && !not_my_turn()) {
        net_order_veh(veh_id, -1, 0);
        return;
    }
    mw(self, kConWaiting) = 0;
}

void THISCALL Console_wait(uint8_t* self, int32_t veh_id) {
    mw(self, kConSearchReset) = 0;
    Vehs[veh_id].state |= 2;
    Console_ready(self);
    mw(self, kConWaiting) = 0;
}

void THISCALL Console_sentry(uint8_t* self, int32_t veh_id, int32_t turns) {
    MAP* sq = map_tile(Vehs[veh_id].x, Vehs[veh_id].y);
    // Air units sentry only in a base, an airbase or a carrier.
    if (triad_of(veh_id) == 2 && !tile_has_own_base(sq) && !(sq->items & 0x40000) &&
        !stack_check(veh_id, 6, 0x80, -1, -1))
        return;
    if (triad_of(veh_id) == 0) {
        sq = map_tile(Vehs[veh_id].x, Vehs[veh_id].y);
        if ((!(sq->items & 1) || (sq->val2 & 0xF) > 7) && !(sq->items & 0x40000) && veh_id >= 0) {
            // A land unit next to an air transport in its stack is asked whether to board it.
            int32_t v = veh_id;
            for (int32_t p = Vehs[v].prev_veh_id_stack; p >= 0; p = Vehs[p].prev_veh_id_stack) v = p;
            for (; v >= 0; v = Vehs[v].next_veh_id_stack) {
                if (triad_of(v) == 2 && Units[Vehs[v].unit_id].plan == 7) {
                    int32_t r = (MFactions[CurrentPlayerFaction].rule_flags & 0x80)
                                    ? popp(ScriptFileName, "AIRLOAD", 0, "alair_sm.pcx", 0)
                                    : popp(ScriptFileName, "AIRLOAD", 0, "air_sm.pcx", 0);
                    if (!r) return;
                    break;
                }
            }
        }
    }
    if (net_lock_veh(&veh_id, 0)) return;
    sleep(veh_id);
    if (turns) Vehs[veh_id].waypoint_y[0] = (int16_t)turns;
    net_synch_veh(veh_id);
    net_unlock_veh();
    mw(self, kConWaiting) = 0;
    draw_tile(Vehs[veh_id].x, Vehs[veh_id].y, 1);
}

void THISCALL Console_hold(uint8_t*, int32_t veh_id, int32_t turns) {
    if (net_lock_veh(&veh_id, 0)) return;
    int32_t y = Vehs[veh_id].y, x = Vehs[veh_id].x;
    MAP* sq = map_tile(x, y);
    int32_t triad = triad_of(veh_id);
    bool in_base = (sq->items & 1) && (sq->val2 & 0xF) <= 7;
    // Not at sea for land units, not in the air for aircraft with a range (outside bases).
    if (((sq->climate & 0xE0) < 0x60 && triad == 0 && !in_base) ||
        (triad == 2 && Chassis[Units[Vehs[veh_id].unit_id].chassis_id].range && !in_base && !(sq->items & 0x40000))) {
        net_unlock_veh();
        return;
    }
    Vehs[veh_id].waypoint_y[0] = (int16_t)turns;
    Vehs[veh_id].order = ORDER_HOLD;
    net_synch_veh(veh_id);
    net_unlock_veh();
    BaseWin_check_veh(&BaseWin, veh_id);
    draw_tile(x, y, 1);
    if (Units[Vehs[veh_id].unit_id].plan == 12) {
        // A unit held in its own base ends its turn.
        sq = map_tile(x, y);
        int32_t owner = (sq->items & 1) ? (sq->val2 & 0xF) : -1;
        if (owner > 7) owner = -1;
        if (owner == Vehs[veh_id].faction_id) net_order_veh(veh_id, -1, 0);
    }
}

void THISCALL Console_new_base(uint8_t* self, int32_t veh_id) {
    int32_t x = Vehs[veh_id].x;
    int32_t y = Vehs[veh_id].y;
    int32_t faction_id = Vehs[veh_id].faction_id;
    if (Units[Vehs[veh_id].unit_id].plan != 8) {
        NetMsg_pop(&NetMsg, "NEEDCOLONISTS", 5000, 0, nullptr);
        return;
    }
    MAP* sq = map_tile(x, y);
    uint8_t alt = sq->climate & 0xE0;
    if ((alt < 0x60) != (triad_of(veh_id) == 1)) {
        NetMsg_pop(&NetMsg, "NOTINTRIAD", 5000, 0, nullptr);
        return;
    }
    if ((sq->items & 0x20) && alt > 0x3F) {
        NetMsg_pop(&NetMsg, "FUNGUSDANGUS", 5000, 0, nullptr);
        return;
    }
    if (sq->items & 0x2000) {
        NetMsg_pop(&NetMsg, "NOTONMONOLITH", 5000, 0, nullptr);
        return;
    }
    int32_t base_id = base_at(x, y);
    if ((map_tile(x, y)->val3 & 0xC0) > 0x40 && (map_tile(x, y)->climate & 0xE0) > 0x5F && base_id < 0) {
        NetMsg_pop(&NetMsg, "NOTONROCKY", 5000, 0, nullptr);
        return;
    }
    if (not_my_turn()) {
        NetMsg_pop(&NetMsg, "SIMULNO", 5000, 0, nullptr);
        return;
    }
    if (!veh_ready(veh_id)) {
        NetMsg_pop(&NetMsg, "UNITMOVED", 5000, 0, nullptr);
        return;
    }
    if (base_id < 0) {
        // Not next to another base.
        for (int32_t i = 0; i < 8; i++) {
            int32_t nx = x + NearOffsetX[i];
            if (!(MapToggleFlat & 1)) {
                if (nx < 0) nx += MapAreaX;
                else if (MapAreaX <= nx) nx -= MapAreaX;
            }
            int32_t ny = y + NearOffsetY[i];
            if (ny >= 0 && ny < MapAreaY && nx >= 0 && nx < MapAreaX && tile_has_own_base(map_tile(nx, ny))) {
                NetMsg_pop(&NetMsg, "BASETOONEAR", 5000, 0, nullptr);
                return;
            }
        }
        // Another faction's territory: treaties forbid it, a truce asks, otherwise the owner may
        // object (THISLANDISMYLAND).
        int32_t owner = whose_territory(faction_id, x, y, nullptr, 0);
        if (Factions[faction_id].base_count && owner >= 0 && owner != faction_id &&
            !(Factions[faction_id].diplo_status[owner] & 0x10)) {
            MFaction& me = MFactions[faction_id];
            MFaction& them = MFactions[owner];
            PluralDefault = 0;
            GenderDefault = me.is_leader_female;
            parse_says(0, me.title_leader, -1, -1);
            parse_says(1, get_pact_hood(faction_id, owner), -1, -1);
            GenderDefault = them.noun_gender;
            PluralDefault = them.is_noun_plural;
            parse_says(2, them.noun_faction, -1, -1);
            parse_says(3, them.adj_name_faction, -1, -1);
            GenderDefault = them.is_leader_female;
            PluralDefault = 0;
            parse_says(5, them.title_leader, -1, -1);
            GenderDefault = them.is_leader_female;
            PluralDefault = 0;
            parse_says(6, them.name_leader, -1, -1);
            bool land = (map_tile(x, y)->climate & 0xE0) > 0x5F;
            uint32_t status = Factions[faction_id].diplo_status[owner];
            if (status & 1) {
                NetMsg_pop(&NetMsg, land ? "TERRPACT" : "TERRPACT2", 5000, 0, nullptr);
                return;
            }
            const char* label;
            if (!(status & 2)) {
                if (!MultiplayerActive) {
                    DiploCaptionOff = 8;
                    wants_to_speak(faction_id, owner);
                    parse_says(0, them.adj_name_faction, -1, -1);
                    GenderDefault = me.is_leader_female;
                    PluralDefault = 0;
                    parse_says(1, me.title_leader, -1, -1);
                    GenderDefault = me.is_leader_female;
                    PluralDefault = 0;
                    parse_says(2, me.name_leader, -1, -1);
                    parse_says(3, me.adj_insult_faction, -1, -1);
                    int32_t r = X_pops(land ? "THISLANDISMYLAND" : "THISLANDISMYLAND2", FactionPortraits[owner], 0);
                    if (!r) {
                        diplomacy_ends(faction_id, owner);
                        return;
                    }
                    GenderDefault = me.is_leader_female;
                    StrBuffer[0] = 0;
                    PluralDefault = 0;
                    parse_says(0, me.title_leader, -1, -1);
                    GenderDefault = me.is_leader_female;
                    PluralDefault = 0;
                    parse_says(1, me.name_leader, -1, -1);
                    parse_says(2, me.adj_insult_faction, -1, -1);
                    parse_says(3, me.adj_leader, -1, -1);
                    parse_says(4, get_phrase(owner, 0xD), -1, -1);
                    parse_says(5, get_noun_phrase(faction_id, 10), -1, -1);
                    crt_strcat(StrBuffer, "BULLY");
                    char num[80];
                    crt_itoa(game_random(0, 7), num, 10);
                    crt_strcat(StrBuffer, num);
                    X_pops(StrBuffer, FactionPortraits[owner], 0);
                    diplomacy_ends(faction_id, owner);
                    goto bureaucracy;
                }
                label = land ? "TERRTRUCE" : "TERRTRUCE2";
            } else {
                label = land ? "TERRTREATY" : "TERRTREATY2";
            }
            if (!X_pop(label, 0)) return;
        }
    bureaucracy:
        // A warning when the next base will add a drone.
        int32_t drones = 0, limit = 0;
        psych_check(faction_id, &drones, &limit);
        if (Factions[faction_id].base_count && Factions[faction_id].base_count % limit == 0) {
            parse_num(0, limit);
            const char* label = Factions[faction_id].base_count != limit ? "BUREAUCRACY2" : "BUREAUCRACY";
            if (popp(ScriptFileName, label, 0x40, "bureau_sm.pcx", 0) < 0) return;
        }
    }
    char name[256];
    name[0] = 0;
    name_base(faction_id, name, 0, (map_tile(x, y)->climate & 0xE0) < 0x60);
    bool redraw;
    if (base_id < 0) {
        NewBaseX = x;
        NewBaseY = y;
        if (GamePreferences & PREF_MAP_SHOW_BASE_GRID) draw_map(1);
        int32_t r = X_pop_ask("NEWBASE", 0x17, name, 0, 0);
        NewBaseY = -99;
        NewBaseX = -99;
        if (r) {
            redraw = !(GamePreferences & PREF_MAP_SHOW_BASE_GRID) ? false : true;
            if (redraw) draw_map(1);
            return;
        }
        crt_strcpy(name, ParseStrBuffer[0]);
    }
    ambience(0xD1);
    {
        const char* error;
        if (not_my_turn()) {
            error = "SIMULNO";
        } else if (net_lock_veh(&veh_id, 4)) {
            if (GamePreferences & PREF_MAP_SHOW_BASE_GRID) draw_map(1);
            return;
        } else if (Units[Vehs[veh_id].unit_id].plan != 8) {
            net_unlock_veh();
            error = "NEEDCOLONISTS";
        } else {
            sq = map_tile(x, y);
            alt = sq->climate & 0xE0;
            if ((alt < 0x60) != (triad_of(veh_id) == 1)) {
                net_unlock_veh();
                error = "NOTINTRIAD";
            } else if ((sq->items & 0x20) && alt >= 0x40) {
                net_unlock_veh();
                error = "FUNGUSDANGUS";
            } else if (sq->items & 0x2000) {
                net_unlock_veh();
                error = "NOTONMONOLITH";
            } else if (!veh_ready(veh_id)) {
                net_unlock_veh();
                error = "UNITMOVED";
            } else {
                int32_t join = base_at(x, y);
                if (join >= 0) {
                    // Join the base.
                    Bases[join].pop_size++;
                    if (MultiplayerActive) synch_base(join);
                    if (!MultiplayerActive) kill(veh_id);
                    else message_veh(0x2407, veh_id, 0, 0);
                    BaseWin_check_base(&BaseWin, join);
                    if (MultiplayerActive) NetDaemon_await_exec(&NetState, 1);
                    return;
                }
                bool near = false;
                for (int32_t i = 0; i < 8 && !near; i++) {
                    int32_t nx = x + NearOffsetX[i];
                    if (!(MapToggleFlat & 1)) {
                        if (nx < 0) nx += MapAreaX;
                        else if (MapAreaX <= nx) nx -= MapAreaX;
                    }
                    int32_t ny = y + NearOffsetY[i];
                    near = ny >= 0 && ny < MapAreaY && nx >= 0 && nx < MapAreaX && tile_has_own_base(map_tile(nx, ny));
                }
                if (near) {
                    net_unlock_veh();
                    error = "BASETOONEAR";
                } else {
                    int32_t new_id;
                    if (!MultiplayerActive) {
                        log_say_values("Manually building base", 0, 0, 0);
                        new_id = action_build(veh_id, name);
                        if (new_id < 0) return;
                    } else {
                        console_not_reconstructed("Console_new_base (multiplayer)");
                        return;
                    }
                    if (Console_ready_search(self, -1) < 0) GameState |= 4;
                    parse_says(0, Bases[new_id].name, -1, -1);
                    parse_num(0, CurrentMissionYear);
                    bool land = (map_tile(x, y)->climate & 0xE0) > 0x5F;
                    if (MFactions[faction_id].rule_flags & 0x80)
                        popp(ScriptFileName, "BASEFOUNDED", 0, land ? "al_col_sm.pcx" : "seabas_sm.pcx", 0);
                    else popp(ScriptFileName, "BASEFOUNDED", 0, land ? "colfoun_sm.pcx" : "seabas_sm.pcx", 0);
                    BaseWin_zoom(&BaseWin, new_id, 1);
                    Console_update_data(self, 1);
                    return;
                }
            }
        }
        NetMsg_pop(&NetMsg, error, 5000, 0, nullptr);
    }
    if (GamePreferences & PREF_MAP_SHOW_BASE_GRID) draw_map(1);
}

namespace {
// Messages of Console_terraform about a technology still missing (NOTUNTILTECH, NOTYETFUNGUS).
void say_missing_tech(int32_t tech_id) {
    StrBuffer[0] = 0;
    say_tech_2(StrBuffer, tech_id, 0);
    parse_says(0, StrBuffer, -1, -1);
}
int32_t terraform_preq(int32_t type, int32_t is_sea) {
    return is_sea ? Terraform[type].preq_tech_sea : Terraform[type].preq_tech;
}
const char* terraform_name(int32_t type, int32_t is_sea) {
    return is_sea ? Terraform[type].name_sea : Terraform[type].name;
}
// Refund of a paid raise/lower when the order is not given after all.
void terraform_refund(uint8_t* self, int32_t faction_id, int32_t cost) {
    if (!cost) return;
    Factions[faction_id].energy_credits += cost;  // synch_energy: multiplayer only
    Console_update_data(self, 0);
}
}  // namespace

void THISCALL Console_terraform(uint8_t* self, int32_t veh_id, int32_t type) {
    // The original constructs a Popup on its stack for the raise/lower cost question.
    alignas(8) static uint8_t popup[0x5410];
    struct StackPopup {
        StackPopup() { Popup_ctor(popup); }
        ~StackPopup() { Popup_dtor(popup); }
    } stack_popup;
    if (Units[Vehs[veh_id].unit_id].plan != 9) {
        NetMsg_pop(&NetMsg, "NEEDTERRAFORMERS", 5000, 0, nullptr);
        return;
    }
    const int32_t y = Vehs[veh_id].y, x = Vehs[veh_id].x;
    MAP* sq = map_tile(x, y);
    uint8_t alt = sq->climate & 0xE0;
    const int32_t is_sea = alt < 0x60;
    int32_t triad = triad_of(veh_id);
    if (triad != 2 && is_sea != (triad == 1)) {
        NetMsg_pop(&NetMsg, "TERRASEA", 5000, 0, nullptr);
        return;
    }
    if (type == FORMER_ROAD) {
        if ((sq->items & 4) && terraform_preq(FORMER_MAGTUBE, is_sea) >= -1) type = FORMER_MAGTUBE;
    } else if (type == FORMER_FARM) {
        if ((sq->items & 0x20) && alt >= 0x40) type = FORMER_REMOVE_FUNGUS;
        else if ((sq->items & 0x8000) && alt >= 0x60 && terraform_preq(FORMER_SOIL_ENR, is_sea) >= -1)
            type = FORMER_SOIL_ENR;
    }
    parse_say(6, is_sea ? Order[type + 4].order_sea : Order[type + 4].order, -1, -1);
    if (terraform_preq(type, is_sea) < -1) {
        const char* label = terraform_preq(type, !is_sea) < -1 ? "NOTATALL" : (is_sea ? "NOTATSEA" : "NOTONLAND");
        NetMsg_pop(&NetMsg, label, 5000, 0, nullptr);
        return;
    }
    if (type == FORMER_SOIL_ENR) {
        if (!(map_tile(x, y)->items & 0x8000)) return;
    } else if (type == FORMER_MAGTUBE && !(map_tile(x, y)->items & 4)) {
        return;
    }
    if (!terrain_avail(type, is_sea, mw(self, kConFaction)) &&
        !(type == FORMER_REMOVE_FUNGUS && has_abil(Vehs[veh_id].unit_id, 0x2000))) {
        say_missing_tech(terraform_preq(type, is_sea));
        const char* label = type == FORMER_MAGTUBE ? "HASROAD" : (type == FORMER_SOIL_ENR ? "HASFARM" : "NOTUNTILTECH");
        NetMsg_pop(&NetMsg, label, 5000, 0, nullptr);
        return;
    }
    sq = map_tile(x, y);
    if ((sq->items & 1) && (sq->val2 & 0xF) < 8) {
        NetMsg_pop(&NetMsg, "NOTBASE", 5000, 0, nullptr);
        return;
    }
    uint32_t bit = Terraform[type].bit;
    if (bit && (sq->items & bit) == bit) {
        parse_say(0, terraform_name(type, is_sea), -1, -1);
        NetMsg_pop(&NetMsg, "ALREADYBUILT", 5000, 0, nullptr);
        return;
    }
    if ((sq->items & 0x2000) && bit && type != FORMER_ROAD && type != FORMER_MAGTUBE && type != FORMER_PLANT_FUNGUS) {
        NetMsg_pop(&NetMsg, "NOTONMONOLITH", 5000, 0, nullptr);
        return;
    }
    if (is_sea && (sq->climate & 0xE0) < 0x40 && type != FORMER_RAISE_LAND && type != FORMER_LOWER_LAND) {
        if (!(MFactions[mw(self, kConFaction)].rule_flags & 0x100)) {
            NetMsg_pop(&NetMsg, "LOWTIDAL", 5000, 0, nullptr);
            return;
        }
        if (!has_tech(TECH_EcoEng2, mw(self, kConFaction))) {
            NetMsg_pop(&NetMsg, "NEEDADVECOENG", 5000, 0, nullptr);
            return;
        }
    }
    sq = map_tile(x, y);
    if ((sq->landmarks & 0x80000002) == 2 && ((bit & 0x208000) || !(sq->landmarks & 0xFF000000))) {
        StrBuffer[0] = 0;
        crt_strcat(StrBuffer, "NOTONVOLCANO");
        if (!(bit & 0x208000)) crt_strcat(StrBuffer, "1");
        NetMsg_pop(&NetMsg, StrBuffer, 5000, 0, nullptr);
        return;
    }
    if (!is_sea && (sq->val3 & 0xC0) > 0x40 && (bit & 0x208000)) {
        NetMsg_pop(&NetMsg, "NOTINROCKY", 5000, 0, nullptr);
        return;
    }
    if ((sq->items & 0x20) && (sq->climate & 0xE0) >= 0x40) {
        int32_t tech = -2;
        if (bit & 4) tech = Rules.tech_preq_build_road_fungus;
        else if (bit) tech = Rules.tech_preq_improv_fungus;
        if (tech != -2 && !has_tech(tech, mw(self, kConFaction))) {
            if (tech < 0) {
                NetMsg_pop(&NetMsg, "NOTINFUNGUS", 5000, 0, nullptr);
            } else {
                say_missing_tech(tech);
                NetMsg_pop(&NetMsg, "NOTYETFUNGUS", 5000, 0, nullptr);
            }
            return;
        }
    }
    const int32_t faction_id = mw(self, kConFaction);
    switch (type) {
    case FORMER_SOLAR:
        if (!energy_yield(faction_id, -1, x, y, 1)) {
            NetMsg_pop(&NetMsg, "LOWSOLAR", 5000, 0, nullptr);
            return;
        }
        break;
    case FORMER_THERMAL_BORE: {
        uint32_t own_alt = map_tile(x, y)->climate >> 5;
        for (int32_t i = 0; i < 8; i++) {
            int32_t nx = x + NearOffsetX[i];
            if (!(MapToggleFlat & 1)) {
                if (nx < 0) nx += MapAreaX;
                else if (MapAreaX <= nx) nx -= MapAreaX;
            }
            int32_t ny = y + NearOffsetY[i];
            if (ny < 0 || ny >= MapAreaY || nx < 0 || nx >= MapAreaX) continue;
            MAP* n = map_tile(nx, ny);
            if ((n->climate & 0xE0) < 0x60) continue;
            if (n->items & 0x1000000) {
                X_pop("TWOBOREHOLES", 0);
                return;
            }
            for (int32_t v = veh_at(nx, ny); v >= 0; v = Vehs[v].next_veh_id_stack) {
                if (Vehs[v].faction_id == (uint32_t)mw(self, kConFaction) && Vehs[v].order == ORDER_THERMAL_BOREHOLE) {
                    X_pop("TWOBOREHOLES", 0);
                    return;
                }
            }
            if ((uint32_t)(map_tile(nx, ny)->climate >> 5) < own_alt) {
                X_pop("NOBOREHOLE", 0);
                return;
            }
        }
        break;
    }
    case FORMER_AQUIFER:
        for (int32_t i = 0; i < 8; i++) {
            int32_t nx = x + NearOffsetX[i];
            if (!(MapToggleFlat & 1)) {
                if (nx < 0) nx += MapAreaX;
                else if (MapAreaX <= nx) nx -= MapAreaX;
            }
            int32_t ny = y + NearOffsetY[i];
            if (ny < 0 || ny >= MapAreaY || nx < 0 || nx >= MapAreaX) continue;
            MAP* n = map_tile(nx, ny);
            if ((n->climate & 0xE0) >= 0x60 && (n->items & 0x80)) {
                NetMsg_pop(&NetMsg, "NOAQUIFER", 5000, 0, nullptr);
                return;
            }
        }
        break;
    case FORMER_LEVEL_TERRAIN:
        if (!(map_tile(x, y)->val3 & 0xC0)) {
            X_pop("TOOFLAT", 0);
            return;
        }
        break;
    case FORMER_RAISE_LAND:
    case FORMER_LOWER_LAND: {
        if (GameRules & RULES_SCN_NO_TERRAFORMING) {
            NetMsg_pop(&NetMsg, "NOTINSCENARIO", 5000, 0, nullptr);
            return;
        }
        uint32_t a = map_tile(x, y)->climate & 0xE0;
        if (type == FORMER_RAISE_LAND) {
            if (a < 0x60 ? a >= 0x40 : a >= 0xC0) {
                NetMsg_pop(&NetMsg, a < 0x60 ? "SEATOOHIGH" : "TOOHIGH", 5000, 0, nullptr);
                return;
            }
        } else if (a >= 0x60) {
            if (a == 0x60) {
                NetMsg_pop(&NetMsg, "TOOLOW", 5000, 0, nullptr);
                return;
            }
        } else if (!a) {
            COMPAT_ORIGINAL(terraform_sea_too_low);
            NetMsg_pop(&NetMsg, "SEATOOLOW", 5000, 0, nullptr);  // BUG(original): the order is given anyway
        }
        log_say_values("Want to terraform", faction_id, 0, 0);
        int32_t cost = 0;
        if (!stack_check(veh_id, 9, type + 4, -1, -1)) {
            cost = terraform_cost(x, y, faction_id);
            if (cost) {
                parse_num(0, cost);
                parse_num(1, Factions[faction_id].energy_credits);
                StrBuffer[0] = 0;
                crt_strcat(StrBuffer, "TERRACOST");
                crt_strcat(StrBuffer, type == FORMER_LOWER_LAND ? "DOWN" : "UP");
                // Popup_start(TERRACOST..), Popup_start(TERRAPAY) when affordable, BasePop_exec_3.
                bool affordable = Factions[faction_id].energy_credits >= cost;
                if (affordable) log_say_values("Offering option to pay", 0, 0, 0);
                if (!ui_script_popup(ScriptFileName, StrBuffer, affordable ? "TERRAPAY" : nullptr)) {
                    log_say_values("Didn't or couldn't pay; returning", 0, 0, 0);
                    return;
                }
                log_say_values("Took option to pay", 0, 0, 0);
                Factions[faction_id].energy_credits -= cost;  // synch_energy: multiplayer only
                Console_update_data(self, 0);
            }
        }
        // Breaking treaties with the owners of the affected tiles.
        int32_t count = 1;
        int32_t level = (int32_t)(map_tile(x, y)->climate >> 5);
        if (type == FORMER_RAISE_LAND) {
            int32_t r = level + 1;
            if (r > 6) count = TableRange[6 - 3];
            else if (r > 3) count = TableRange[r - 3];
        } else {
            int32_t r = level - 1;
            if (r < 0) r = 0;
            if (r < 3) count = TableRange[3 - r + 1];
        }
        for (int32_t i = 0; i < count; i++) {
            int32_t nx = x + TableOffsetX[i];
            if (!(MapToggleFlat & 1)) {
                if (nx < 0) nx += MapAreaX;
                else if (MapAreaX <= nx) nx -= MapAreaX;
            }
            int32_t ny = y + TableOffsetY[i];
            if (ny < 0 || ny >= MapAreaY || nx < 0 || nx >= MapAreaX) continue;
            int32_t owner = whose_territory(faction_id, nx, ny, nullptr, 0);
            if (owner < 0 || owner == faction_id || !(Factions[faction_id].diplo_status[owner] & 7)) continue;
            if (net_lock_veh(&veh_id, 2)) {
                terraform_refund(self, faction_id, cost);
                return;
            }
            if (break_treaty(faction_id, owner, 7)) {
                terraform_refund(self, faction_id, cost);
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
        if (tut_check(0x800)) popt("TUTOR", "TERRAFORM", -1, 0, 0);
        break;
    }
    default: break;
    }
    if (type != FORMER_REMOVE_FUNGUS && type != FORMER_RAISE_LAND && type != FORMER_LOWER_LAND) {
        sq = map_tile(x, y);
        uint32_t a = sq->climate & 0xE0;
        uint32_t conflict = Terraform[type].bit_incompatible & sq->items;
        if (a >= 0x40 && conflict) {
            // An improvement this one replaces: confirm.
            for (int32_t i = 0; i < 20; i++) {
                if (Terraform[i].bit & conflict) {
                    parse_say(0, terraform_name(i, a < 0x60), -1, -1);
                    if (!X_pop("MIMIMI", 0)) return;
                    break;
                }
            }
        }
    }
    if (NetLockHeld) log_say_values("I think I've already got my lock", 0, 0, 0);
    if (!NetLockHeld && net_lock_veh(&veh_id, 0)) return;
    Vehs[veh_id].order = (uint8_t)(type + 4);
    net_synch_veh(veh_id);
    log_say_values("Issuing Vehicle Orders", veh_id, type + 4, 0);
    if (MultiplayerActive) {
        int32_t left = speed(veh_id, 0) - Vehs[veh_id].moves_spent;
        if (left >= 0 && (left > 999 || left)) net_action(veh_id, 1);
        else net_unlock_veh();
    } else {
        net_unlock_veh();
    }
    log_say_values("Finished issuing orders", veh_id, type + 4, 0);
    mw(self, kConWaiting) = 0;
}

void THISCALL Console_end_my_turn(uint8_t* self) {
    BaseWinMultiplayerRefresh = 0;
    mapwin_field(&InfoWindow, 0xA8) = 0;
    TutWin_reset(&TutWin);
    StatusWin_reset(&InfoWin);
    FX_play(&Sounds, 0xF);
    if (ControlTurnC) return;
    if (Console_edit_lock(self)) {
        NetMsg_pop(&NetMsg, "PROTECTED", 5000, 0, nullptr);
        return;
    }
    if (MultiplayerActive) {
        if ((GameMoreRules & 0x10) && CurrentFaction != CurrentPlayerFaction) {  // not our turn
            const MFaction& m = MFactions[CurrentFaction];
            PluralDefault = 0;
            GenderDefault = m.is_leader_female;
            parse_says(0, m.title_leader, -1, -1);
            PluralDefault = 0;
            GenderDefault = m.is_leader_female;
            parse_says(1, m.name_leader, -1, -1);
            GenderDefault = m.noun_gender;
            PluralDefault = m.is_noun_plural;
            parse_says(2, m.noun_faction, -1, -1);
            const char* name = Net_get_player_name(&NetState, AlphaNet_who_2_pid(&NetState, CurrentFaction));
            parse_says(3, name ? name : "", -1, -1);
            NetMsg_pop(&NetMsg, "SIMULWHOSE", 5000, 0, nullptr);
            return;
        }
        if ((GameState & 2) && !(GameMoreRules & 0x10) && (NetTurnDoneMask & (1 << (CurrentPlayerFaction & 31))) &&
            NetTurnDoneMask != 0xFF) {  // the end of turn withdrawn
            NetMsg_pop(&NetMsg, "ATTEMPTTOCANCEL", 5000, 0, nullptr);
            message_data(0x2302, 0, CurrentTurn, 0, 0, 0);
            return;
        }
    }
    if (!mw(self, kConTurnActive)) return;
    if (!(GameState & 2)) {
        Console_set_move(self, 1);
        int32_t v = mw(self, kConVeh);
        if (MapWinInVehMode && v >= 0 && (veh_ready(v) || Console_ready_search(self, v) >= 0) &&
            !X_pop("REALLYOVER", 0))
            return;
    }
    GameState |= 2;
    mw(self, kConWaiting) = 0;
    mw(self, kConTurnActive) = 0;
    mw(self, kConNextVeh) = mw(self, kConVeh);
}

void THISCALL Console_veh_key(uint8_t* self, int32_t ch, int32_t key, int32_t vk) {
    if (MultiplayerActive && key != 'a') {
        if (ControlTurnC || NetLocked || (NetTurnDoneMask & (1 << (CurrentPlayerFaction & 31)))) return;
    }
    int32_t& veh = mw(self, kConVeh);
    switch (ch) {
    case 'B': Console_terraform(self, veh, FORMER_THERMAL_BORE); break;
    case 'E': Console_terraform(self, veh, FORMER_ECH_MIRROR); break;
    case 'F': Console_terraform(self, veh, FORMER_FOREST); break;
    case 'G': Console_go_home(self, veh); break;
    case 'H': Console_hold(self, veh, 10); break;
    case 'I': Console_go_to(self, veh, -3, 0); break;
    case 'L': Console_automate(self, veh, 0xB); break;
    case 'P':
        // Base destinations display (option 0x400000 of every map window).
        for_each_mapwin([&](int32_t, uint8_t* w) {
            if (!(mw(main_view(), kMwOptions) & 0x400000)) {
                mw(w, kMwOptions) |= 0x400000;
                MapWin_draw_base_dest(w, 1);
            } else {
                mw(w, kMwOptions) &= ~0x400000;
                pfncRedrawMap(w, 1);
            }
        });
        break;
    case 'U': Console_unload(self, veh); break;
    case 'a':
        if (!NetLocked) Console_activate(self, Vehs[veh].x, Vehs[veh].y, 1);
        break;
    case 'b': {
        const UNIT& u = Units[Vehs[veh].unit_id];
        int32_t triad = Chassis[u.chassis_id].triad;
        bool armed = Weapon[u.weapon_id].offense_value != 0;
        if (triad == 2 && u.plan != 8 && armed) Console_automate(self, veh, 10);
        else if (!armed || triad == 2) {
            if (u.plan == 9) Console_terraform(self, veh, FORMER_THERMAL_BORE);
            else Console_new_base(self, veh);
        } else {
            Console_oblit(self, veh);
        }
        break;
    }
    case 'f':
        if (can_arty(Vehs[veh].unit_id, 1)) Console_arty(self, veh);
        else Console_terraform(self, veh, FORMER_FARM);
        break;
    case 'g': Console_go_to(self, veh, ORDER_MOVE_TO, 0); break;
    case 'h': Console_hold(self, veh, 0); break;
    case 'i': Console_use_ability(self, veh); break;
    case 'l': Console_sentry(self, veh, 0); break;
    case 'n': Console_terraform(self, veh, FORMER_CONDENSER); break;
    case 'o':
        if (Units[Vehs[veh].unit_id].plan != 10) Console_terraform(self, veh, FORMER_SENSOR);
        else Console_convoy(self, veh);
        break;
    case 'p': Console_set_patrol(self, veh); break;
    case 'q': Console_terraform(self, veh, FORMER_AQUIFER); break;
    case 'r': Console_terraform(self, veh, FORMER_ROAD); break;
    default: break;
    }
    switch (key) {
    case ' ': Console_skip(self, veh); break;
    case '.': Console_terraform(self, veh, FORMER_AIRBASE); break;
    case '/': Console_explore(self, veh); break;
    case '[': Console_terraform(self, veh, FORMER_LOWER_LAND); break;
    case ']': Console_terraform(self, veh, FORMER_RAISE_LAND); break;
    case '_': Console_terraform(self, veh, FORMER_LEVEL_TERRAIN); break;
    case 'd': Console_destroy(self, veh); break;
    case 'k': Console_terraform(self, veh, FORMER_BUNKER); break;
    case 'm': Console_terraform(self, veh, FORMER_MINE); break;
    case 's': Console_terraform(self, veh, FORMER_SOLAR); break;
    default: break;
    }
    switch (vk) {
    case 0x30046: Console_automate(self, veh, 6); break;  // ctrl+shift+F
    case 0x20052: Console_road_to(self, veh, ORDER_ROAD_TO); break;  // ctrl+R
    case 0x20044: Console_designate(self, veh); break;  // ctrl+D
    case 0x20046: Console_terraform(self, veh, FORMER_PLANT_FUNGUS); break;  // ctrl+F
    case 0x20048: Console_go_home(self, veh); break;  // ctrl+H
    case 0x20054: Console_road_to(self, veh, ORDER_MAGTUBE_TO); break;  // ctrl+T
    case 0x20055: Console_upgrade(self, veh); break;  // ctrl+U
    case 0x30041: {  // ctrl+shift+A
        const UNIT& u = Units[Vehs[veh].unit_id];
        if (Chassis[u.chassis_id].triad == 2 && Weapon[u.weapon_id].offense_value) Console_automate(self, veh, 0xC);
        else Console_automate(self, veh, 3);
        break;
    }
    case 0x3004D: Console_automate(self, veh, 5); break;
    case 0x3004F: Console_automate(self, veh, 7); break;
    case 0x30052: Console_automate(self, veh, 1); break;
    case 0x30053: Console_automate(self, veh, 4); break;
    case 0x30054: Console_automate(self, veh, 2); break;
    case 0x30055: Console_give_veh(self, veh); break;
    default: break;
    }
}

void THISCALL Console_cursor_key(uint8_t* self, int32_t, int32_t key, int32_t) {
    int32_t cx = MapWinCursorX[MapWinCursorIndex], cy = MapWinCursorY[MapWinCursorIndex];
    switch (key) {
    case ' ': {
        int32_t base_id = base_at(cx, cy);
        if (base_id >= 0) {
            int32_t owner = Bases[base_id].faction_id;
            if ((GameState & STATE_OMNISCIENT_VIEW) || owner == CurrentPlayerFaction ||
                ((Bases[base_id].visibility & (1 << (CurrentPlayerFaction & 31))) && spying(owner))) {
                BaseWin_zoom(&BaseWin, base_id, 0);
                return;
            }
        }
    }
        [[fallthrough]];
    case 'a':
        if (!NetLocked) Console_activate(self, cx, cy, 1);
        break;
    case 'b': {
        int32_t base_id = base_find_faction(cx, cy, CurrentPlayerFaction);
        if (base_id >= 0) BaseWin_zoom(&BaseWin, base_id, 0);
        break;
    }
    case 'g':
        if (!ControlTurnC && !NetLocked) {
            int32_t base_id = base_at(cx, cy);
            if (base_id >= 0) Console_go_to(self, base_id, -1, 0);
        }
        break;
    case 'p':
        for_each_mapwin([&](int32_t, uint8_t* w) {
            if (!(mw(main_view(), kMwOptions) & 0x400000)) {
                mw(w, kMwOptions) |= 0x400000;
                MapWin_draw_base_dest(w, 1);
            } else {
                mw(w, kMwOptions) &= ~0x400000;
                pfncRedrawMap(w, 1);
            }
        });
        break;
    default: break;
    }
}

void THISCALL pfncProcZoomKey(uint8_t*, int32_t command, int32_t index) {
    if (index < 0) {
        // The mouse over the world map pane: the main view zooms, centred on the tile there; else
        // the first map window of a reduced zoom under the mouse, centred on the tile under it;
        // else every window zooms (around its centre). The tile must be known to the viewer.
        int32_t mx = 0, my = 0, tx = -1, ty = -1;
        ui_mouse_pos(&mx, &my);
        auto seen = [&](int32_t x, int32_t y) {
            return y >= 0 && y < MapAreaY && x >= 0 && x < MapAreaX &&
                   ((Factions[CurrentPlayerFaction].player_flags & 0x200) ||
                    (map_tile(x, y)->visibility & (1 << CurrentPlayerFaction)) || (GameState & STATE_OMNISCIENT_VIEW));
        };
        if (worldwin_screen_tile(mx, my, &tx, &ty) == 0 && seen(tx, ty)) {
            mw(main_view(), kMwCenterX) = tx;
            mw(main_view(), kMwCenterX + 4) = ty;
            index = 0;
        } else {
            for_each_mapwin([&](int32_t i, uint8_t* w) {
                if (index >= 0 || mw(w, kMwZoom) >= 0) return;
                int32_t cw = mw(w, kGwWidth), ch = -mw(w, kGwWidth + 4);
                if (mx < 0 || my < 0 || mx >= cw || my >= ch) return;
                if (pfncPtToTile(w, mx, my, &tx, &ty) != 0 || !seen(tx, ty)) return;
                mw(w, kMwCenterX) = tx;
                mw(w, kMwCenterX + 4) = ty;
                index = i;
            });
        }
    }
    for_each_mapwin([&](int32_t i, uint8_t* w) {
        if (index >= 0 && i != index) return;
        int32_t& zoom = mw(w, kMwZoom);
        switch (command) {
        case 0x203:
            if (zoom < 0x10) zoom += 2;
            break;
        case 0x204:
            if (-0xE < zoom) zoom -= 2;
            break;
        case 0x205: zoom = 0; break;
        case 0x206: zoom = -6; break;
        case 0x207: zoom = 0x10; break;
        case 0x208: zoom = -0xE; break;
        default: break;
        }
        pfncRedrawMap(w, 1);
    });
    GraphicWin_redraw(&WorldWin);
}

void THISCALL Console_turn_hints(uint8_t* self) {
    int32_t f = CurrentPlayerFaction;
    if (!(GamePreferences & PREF_BSC_TUTORIAL_MSGS) || MultiplayerActive || TutorialWinActive) return;
    int32_t cx = MapWinCursorX[MapWinCursorIndex], cy = MapWinCursorY[MapWinCursorIndex];
    if (CurrentTurn == 1 && tut_check(1)) {
        int32_t base_id = base_find_faction(cx, cy, f);
        if (base_id >= 0) {
            parse_says(0, Bases[base_id].name, -1, -1);
            if (mw(self, kConVeh) >= 0) {
                parse_says(1, Units[Vehs[mw(self, kConVeh)].unit_id].name, -1, -1);
                TutorialArrowShown = 0;
                TutWin_tut_map(&TutWin, "LANDFALL", MapWinCursorX[MapWinCursorIndex], MapWinCursorY[MapWinCursorIndex],
                               -1, nullptr, 0x100, -1, -1);
            }
        }
    }
    Faction& fa = Factions[f];
    if (CurrentTurn < 0x14 || fa.base_count != 1 || fa.pop_total < 2 || fa.units_active[0] || fa.units_queue[0] ||
        !tut_check(0x100000)) {
        if (CurrentTurn - fa.last_base_turn > 0x17 && !fa.units_active[0] && fa.base_count < 6 && fa.base_count > 1) {
            int32_t base_id = base_find_faction(cx, cy, f);
            if (base_id >= 0) {
                TutWin_tut_map(&TutWin, "BUILDMORE", Bases[base_id].x, Bases[base_id].y, -2 - base_id, nullptr, 0, -1,
                               -1);
                fa.last_base_turn = CurrentTurn;
            }
        }
    } else {
        int32_t base_id = base_find_faction(cx, cy, f);
        if (base_id >= 0) {
            Console_focus(self, Bases[base_id].x, Bases[base_id].y, f);
            parse_says(0, Bases[base_id].name, -1, -1);
            TutWin_tut_map(&TutWin, "SECONDBASE", Bases[base_id].x, Bases[base_id].y, -2 - base_id, nullptr, 0, -1, -1);
            fa.last_base_turn = CurrentTurn;
        }
    }
    int32_t other = 1;
    for (; other < 8; other++) {
        uint32_t status = fa.diplo_status[other];
        if (f != other && (status & 8) && (status & 0x10)) break;
    }
    if (other > 7) return;
    if (tut_check(0x80)) {
        MFaction& m = MFactions[other];
        PluralDefault = 0;
        GenderDefault = m.is_leader_female;
        parse_says(0, m.title_leader, -1, -1);
        GenderDefault = m.is_leader_female;
        PluralDefault = 0;
        parse_says(1, m.name_leader, -1, -1);
        GenderDefault = m.noun_gender;
        PluralDefault = m.is_noun_plural;
        parse_says(2, m.noun_faction, -1, -1);
        X_pop_2("TUTOR", "VENDETTA", 0);
    }
}

// ---- the key dispatcher -----------------------------------------------------------------------------

namespace {
GLOBAL(uint8_t[6], DebugKeyLetters, 0x939FE0);  // ctrl+shift+P/I/R/A/T/E: letters typed (debug)
GLOBAL(int32_t, AutoPlayTimerOn, 0x939E8C);  // ctrl+shift+Z: computer plays for the human
GLOBAL(uint8_t, AlphaMenu, 0x9380DC);  // the pull-down menu bar
GLOBAL(int32_t, NetMsgShownA, 0x80A6BC);  // message boxes on screen
GLOBAL(int32_t, NetMsgShownB, 0x805304);
GLOBAL(int32_t, InterfaceMessageShown, 0x7CD2E8);
GLOBAL(int32_t, InterfaceButtonState, 0x7CD11C);
GLOBAL(int32_t, DebugKeyCount, 0x68A5C8);  // '~' presses (debug mode)
GLOBAL(int32_t, NetPendingInput, 0x93E8C8);
GLOBAL(int32_t, ScenarioVictoryA, 0x94B4C0);  // scenario victory conditions (9000: none)
GLOBAL(int32_t, ScenarioVictoryB, 0x94B4C4);

bool in_map(int32_t x, int32_t y) { return y >= 0 && y < MapAreaY && x >= 0 && x < MapAreaX; }

// The quit question (Esc, ctrl+Q): REALLYQUIT, or REALLYWIMPOUT with other humans in the game.
bool confirm_quit() {
    const char* label =
        (MultiplayerActive || PbemActive) && bit_count(FactionStatus[0]) >= 2 ? "REALLYWIMPOUT" : "REALLYQUIT";
    if (!X_pop(label, 0)) return false;
    if (PbemActive) {
        FactionStatus[0] &= (uint8_t)~(1 << (CurrentPlayerFaction & 31));
        save_game(0);
    }
    if ((GameRules & RULES_IRONMAN) && !PbemActive) auto_save();
    if (MultiplayerActive) console_not_reconstructed("Console_on_key_click: multiplayer hang up");
    ControlTurnA = 1;
    log_say_values("Quit ------------------------------", 0, 0, 0);
    return true;
}
}  // namespace

// Console_on_key_click's function keys and menu commands (from 0x51A53E). With the base window open
// (its flag [ebp-0x28]) the same switch runs after the base window's keys, except Backspace,
// shift+Backspace, shift+N, ctrl+shift+N, ctrl+B and the zoom keys ctrl+X / ctrl+Z (each checks the
// flag and returns); the movement keys after it are then skipped (0x51B078).
void console_function_keys(uint8_t* self, int32_t vk, bool in_base) {
    switch (vk) {
    case 0x20043: Console_chat(self, 0); break;
    case 0x77: report_score(0); break;  // F8
    case 8: {  // Backspace: back in the cursor history
        if (in_base) break;
        hide_cursor(self);
        if (MapWinInVehMode) Console_set_view(self, 1);  // inlined
        if (--mw(self, kConCursorIdx) < 0) mw(self, kConCursorIdx) = 0x1F;
        StatusWin_set_loc(&InfoWin, MapWinCursorX[MapWinCursorIndex], MapWinCursorY[MapWinCursorIndex]);
        MapWin_focus(main_view(), MapWinCursorX[MapWinCursorIndex], MapWinCursorY[MapWinCursorIndex]);
        break;
    }
    case 0xD:
    case 0x1000D: {  // Enter: end of turn, or the base at the cursor
        if (!(GameState & 2) || Console_edit_lock(self)) {
            int32_t base_id = base_at(MapWinCursorX[MapWinCursorIndex], MapWinCursorY[MapWinCursorIndex]);
            if (base_id >= 0) {
                int32_t owner = Bases[base_id].faction_id;
                bool known = owner == CurrentPlayerFaction ||
                             (Bases[base_id].visibility & (1 << (CurrentPlayerFaction & 31))) ||
                             (GameState & STATE_OMNISCIENT_VIEW);
                if (known && ((GameState & STATE_OMNISCIENT_VIEW) || owner == CurrentPlayerFaction || spying(owner)))
                    BaseWin_zoom(&BaseWin, base_id, 0);
            }
        } else {
            mw(self, kConWaiting) = 0;
            mw(self, kConTurnActive) = 0;
        }
        break;
    }
    case 0x13:  // Pause
        // The pause asked or lifted (0x4C00 / 0x4C01, NetState +0x1B50 the players pausing); not
        // with one player at a time (SIMULNOT).
        if (MultiplayerActive) {
            int32_t paused = *reinterpret_cast<int32_t*>(&NetState + 0x1B50);
            if (!(paused & (1 << (CurrentPlayerFaction & 31))) && (GameMoreRules & 0x10)) {
                NetMsg_pop(&NetMsg, "SIMULNOT", 5000, 0, nullptr);
                ui_multiwin_draw();
                break;
            }
            message_data((paused & (1 << (CurrentPlayerFaction & 31))) ? 0x4C01 : 0x4C00, 0, 0, 0, 0, 0);
            ui_multiwin_draw();
        }
        break;
    case 0x1B:  // Esc
        if (InfcList[InfcUsed] != 5) confirm_quit();
        else ReportIf_done(&ReportIfObj);
        break;
    case 0x70: Datalink_exec(&DatalinkWin, 0xE, 0); break;  // F1
    case 0x71: report_labs(); break;  // F2
    case 0x72: report_energy(); break;  // F3
    case 0x73: report_ops(); break;  // F4
    case 0x74: report_projects(); break;  // F5
    case 0x75: report_sat(); break;  // F6
    case 0x76: report_security(); break;  // F7
    case 0x78: monument(0); break;  // F9
    case 0x79: hall_of_fame(0); break;  // F10
    case 0x7A:  // F11: menu detail
        GameMorePreferences ^= MPREF_ADV_DETAIL_MAIN_MENUS;
        pfncMainMenuUpdateVisible(main_menu(self), mw(self, kConVeh));
        X_pop((GameMorePreferences & MPREF_ADV_DETAIL_MAIN_MENUS) ? "MENUDETAIL" : "MENUSIMPLE", 0);
        break;
    case 0x7B: MultiWin_show(nullptr); break;  // F12: MainInterface_on_iface_button_clicked(0x3EC)
    case 0x10008: if (!in_base) Console_next_cursor(self); break;  // shift+Backspace
    case 0x1004E: if (!in_base) Console_name_landmark(self, -1, -1); break;  // shift+N
    case 0x3004E: if (!in_base) Console_erase_landmark(self, -1, -1); break;  // ctrl+shift+N
    case 0x10051: Console_retire_game(self); break;  // shift+Q: Resign
    case 0x20042: if (!in_base) Console_go_to(self, -1, -2, 0); break;  // ctrl+B
    case 0x20051: confirm_quit(); break;  // ctrl+Q
    case 0x30051:  // ctrl+shift+Q: quit to the main menu
        if (confirm_quit() && ControlTurnA) ControlTurnB = 1;
        break;
    case 0x20053: save_game(0); break;  // ctrl+S
    case 0x2004C:  // ctrl+L
        if (!MultiplayerActive || (GameState & STATE_DEBUG_MODE)) Console_load_proc(self, 0, 0);
        break;
    case 0x20058: if (!in_base) pfncProcZoomKey(self, 0x208, 0); break;  // ctrl+X
    case 0x2005A: if (!in_base) pfncProcZoomKey(self, 0x207, 0); break;  // ctrl+Z
    case 0x20050: prefwin_display(0); break;  // ctrl+P: Console_set_preferences
    case 0x2004F: prefwin_display(1); break;  // ctrl+O: Console_set_adv_preferences
    case 0x20057: prefwin_display(2); break;  // ctrl+W: Console_set_base_preferences
    case 0x20041: prefwin_display(3); break;  // ctrl+A: Console_set_auto_preferences
    case 0x20049: prefwin_display(4); break;  // ctrl+I: Console_set_audiovisual
    case 0x2004D: prefwin_display(5); break;  // ctrl+M: Console_set_map_display
    case 0x20047: console_toggle_pref(0x20A); break;  // ctrl+G
    case 0x30047: console_toggle_pref(0x20B); break;  // ctrl+shift+G
    case 0x2004E: console_toggle_pref(0x210); break;  // ctrl+N: base names
    case 0x30041: console_toggle_pref(0x20C); break;  // ctrl+shift+A: rally points
    case 0x30058: console_toggle_pref(0x209); break;  // ctrl+shift+X: flat terrain
    case 0x30057:  // ctrl+shift+W
        GameMorePreferences ^= MPREF_MAP_SHOW_FOG_WAR;
        draw_map(1);
        prefs_save(0);
        break;
    case 0x2004B: Console_editor_on(self); break;  // ctrl+K: scenario editor
    case 0x3004C: Console_load_proc(self, 0, 0); break;  // ctrl+shift+L
    case 0x20073:  // ctrl+F4: version
        parse_says(0, "RELEASE 2.0", -1, -1);
        parse_says(1, "20-Dec-1999", -1, -1);
        popp(ScriptFileName, "VERSIONCONTROLX", 0, "ac_logo_sm.pcx", 0);
        break;
    case 0x30073: show_credits(); break;  // ctrl+shift+F4
    default: break;
    }
}

int32_t THISCALL Console_on_key_click(uint8_t* self, int32_t ch, int32_t vk) {
    if (ControlTurnA || (vk & 0xFFFCFFFF) == 0x12) return 1;  // Alt (menus)
    switch (vk) {
    case 0x30041: DebugKeyLetters[3] = 'a'; break;
    case 0x30045: DebugKeyLetters[5] = 'e'; break;
    case 0x30049: DebugKeyLetters[1] = 'i'; break;
    case 0x30050: DebugKeyLetters[0] = 'p'; break;
    case 0x30052: DebugKeyLetters[2] = 'r'; break;
    case 0x30054: DebugKeyLetters[4] = 't'; break;
    case 0x3007A: auto_play_toggle(); break;  // ctrl+shift+F11: the auto play timer
    default: break;
    }
    AlphaMenu_hide_main();
    if (!ControlTurnStarted) {
        ConsoleNotReady = 1;
        return 1;
    }
    if (TutWin_reset(&TutWin) && (vk == 0xD || vk == 0x1B)) return 1;
    // Enter or space dismiss the message boxes first.
    bool dismiss = false;
    if (vk == 0x91) MainInterface_draw_edit_pane(&MainInterfaceWin);
    if (vk == 0xD || ch == ' ') {
        if (NetMsgShownA || NetMsgShownB || InterfaceMessageShown ||
            (InterfaceButtonState == 1000 && !Win_is_visible(&BaseWin)))
            dismiss = true;
    }
    if (!dismiss && vk == 0xD && (Win_is_visible(&AlphaMenu) || MultiWinVisible)) dismiss = true;
    if (dismiss) {
        // ButtonGroup_button_click(0x3E9) on the main interface's mode buttons (+0x1E878): the
        // message list (M) gives way to the info pane (I); nothing when it already shows.
        if (!Win_is_visible(&BaseWin)) mi_set_mode(0x3E9);
        AlphaMenu_hide_main();
        if (MultiWinVisible) multiwin_hide();  // MultiWin_hide
        // (InterfaceMessageShown: MainInterface_clear_message, an empty function in the original.)
        if (NetMsgShownA || NetMsgShownB) {
            NetMsg_close(&NetMsg);
            NetMsg_close(&NetMsg);
            return 1;
        }
    }
    InfoWin_reset(&InfoWindow);
    StatusWin_reset(&InfoWin);
    // The scenario editor's keys (editor.cpp); the others as usual.
    if ((GameState & STATE_SCENARIO_EDITOR) && editor_key(self, vk, ch)) return 1;
    if (vk >= 0x60 && vk <= 0x69) ch = 0;  // keypad digits
    if (vk == 0xD && (GameState & STATE_OMNISCIENT_VIEW) && (mw(main_view(), kMwOptions) & 0x4000)) {
        // Path display (omniscient debug view): the go-to unit at the cursor moves.
        int32_t v = veh_at(MapWinCursorX[MapWinCursorIndex], MapWinCursorY[MapWinCursorIndex]);
        for (; v >= 0; v = Vehs[v].next_veh_id_stack) {
            if (Vehs[v].order == ORDER_MOVE_TO) {
                Path_move(&Paths, v, Vehs[v].faction_id);
                break;
            }
        }
        draw_map(1);
        return 1;
    }
    int32_t key = std::tolower(ch);  // [ebp-0x24]
    int32_t net_busy;  // [ebp-0x2c]: multiplayer, input not ours now
    if (!MultiplayerActive || (!ControlTurnC && !NetLocked && !NetPendingInput)) {
        net_busy = 0;
    } else {
        net_busy = 1;
        if (ControlTurnC) return 1;
    }
    int32_t mode = mw(self, kConMode);
    if (mode == 1 && MapWinMouseIndex >= 0 && mw(self, kConVeh) >= 0 && MapWinInVehMode) {
        // Keys of the go-to drag: j toggles the group move, g / space add a waypoint (patrol
        // or group waypoints), other keys end the patrol and the unit's orders.
        uint8_t* w = MapWins[MapWinMouseIndex];
        VEH& v = Vehs[mw(self, kConVeh)];
        int32_t mx, my;
        if (key == 'j') {
            mw(w, 0x1DD84) = !mw(w, 0x1DD84);
            ui_mouse_pos(&mx, &my);
            MapWin_on_mouse_move(w, mx, my);
            return 1;
        }
        if (key == 'g' || key == ' ') {
            if ((v.state & 0x1000200) == 0x1000200) {
                v.waypoint_count = v.waypoint_count < 3 ? v.waypoint_count + 1 : 0;
            } else if (!(v.state & 0x2000000)) {
                v.state |= 0x2000000;
                v.patrol_current_point = 0;
                v.waypoint_count = 1;
            } else if (++v.waypoint_count >= 4) {
                v.state &= 0xFDFFFFFF;
                v.waypoint_count = 0;
            }
            ui_mouse_pos(&mx, &my);
            mw(w, 0x1DE10) = -1;
            mw(w, 0x1DE0C) = -1;
            MapWin_on_mouse_move(w, mx, my);
            return 1;
        }
        v.state &= 0xFDFFFDFF;
        v.order = 0;
    }
    if (mode == 8 && mw(self, kMwTracking) && mw(self, kConVeh) >= 0 && MapWinInVehMode && (key == ' ' || key == 'g')) {
        VEH& v = Vehs[mw(self, kConVeh)];
        v.waypoint_count = v.waypoint_count < 3 ? v.waypoint_count + 1 : 0;
        int32_t mx, my;
        ui_mouse_pos(&mx, &my);
        mw(main_view(), 0x1DE0C) = -1;
        MapWin_on_mouse_move(main_view(), mx, my);
        return 1;
    }
    for_each_mapwin([&](int32_t, uint8_t* w) {
        if ((mw(w, kMwOptions) & 0x400000) && (key == 'g' || key == 'p')) vk = ch = key = 0;
    });
    if (mode && (key == 'g' || key == 'p')) vk = ch = key = 0;
    go_reset();
    mw(self, kConMode) = 0;
    for_each_mapwin([&](int32_t, uint8_t* w) {
        if (mw(w, 0x1EF4C) || mw(w, 0x1EF50) != 0x7F00) {  // Win_set_cursor_3(IDC_ARROW)
            mw(w, 0x1EF50) = 0x7F00;
            mw(w, 0x1EF4C) = 0;
        }
        if (mw(w, kMwOptions) & 0xC40000) {
            mw(w, kMwOptions) &= 0xFF3BFFFF;
            pfncRedrawMap(w, 1);
        }
        do_non_input();
    });
    if (vk == 0xD && InfcList[InfcUsed] == 5) {
        ReportIf_done(&ReportIfObj);
        if (Win_is_visible(&BaseWin)) BaseWin_exit(&BaseWin);
        return 1;
    }
    if (Win_is_visible(&BaseWin)) {
        // The base window's keys (production, next/previous base, ...): ui/basewin.cpp.
        return basewin_console_key(self, ch, vk, key, net_busy);
    }
    if (InterfaceButtonState == 1000) mi_set_mode(0x3E9);  // ButtonGroup_button_click(0x3E9), as above
    if (vk == 0x2000D) {  // ctrl+Enter
        Console_end_my_turn(self);
        return 1;
    }
    if (vk == 0x20020) {  // ctrl+space
        Console_use_time_bonus(self);
        return 1;
    }
    // Character commands.
    switch (ch) {
    case 'z':
        pfncProcZoomKey(self, 0x203, 0);
        flush_input();
        return 1;
    case 'x':
        pfncProcZoomKey(self, 0x204, 0);
        flush_input();
        return 1;
    case 'Z':
        pfncProcZoomKey(self, 0x205, 0);
        flush_input();
        return 1;
    case 'X':
        pfncProcZoomKey(self, 0x206, 0);
        flush_input();
        return 1;
    case 't': Console_survey(self); return 1;
    case 'e':
        if (!net_busy) social_select(mw(self, kConFaction));
        return 1;
    case 'u':
        if (!net_busy) design_new_veh(mw(self, kConFaction), -1);
        return 1;
    case 'c': pfncMoveMap(main_view(), MapWinCursorX[MapWinCursorIndex], MapWinCursorY[MapWinCursorIndex], 1); return 1;
    case 'D':
        if (net_busy) return 1;
        if (MapWinInVehMode && mw(self, kConVeh) >= 0) Console_disband(self, mw(self, kConVeh));
        else Console_disband_2(self, MapWinCursorX[MapWinCursorIndex], MapWinCursorY[MapWinCursorIndex]);
        return 1;
    case ',':
        if (!net_busy) Console_corner_the_market(self);
        return 1;
    case '=':
        if ((GameState & STATE_IS_SCENARIO) || ScenarioVictoryA < 9000 || ScenarioVictoryB < 9000)
            Console_show_scen_objectives(self);
        return 1;
    case '+': Console_custom_my_faction(self); return 1;
    default: break;
    }
    if (Console_edit_lock(self) && (ch == 'v' || ch == 'a')) NetMsg_pop(&NetMsg, "PROTECTED", 5000, 0, nullptr);
    if (!Console_edit_lock(self)) {
        switch (ch) {
        case 'A':
            if (!net_busy) Console_automate(self, mw(self, kConVeh), 0);
            break;
        case 'P':
            if (MultiplayerActive) MultiWin_on_button_clicked(&MultiWinObj, 2);  // (id 2: nothing)
            break;
        case 'T': Console_time_controls(self); break;
        case '^': Console_use_time_bonus(self); break;
        case 'J': Console_assemble(self); break;
        case 'j': Console_go_to(self, mw(self, kConVeh), ORDER_MOVE_TO, 1); break;
        case 'R':
            if (GameRules & RULES_BLIND_RESEARCH) Console_set_ai(self, mw(self, kConFaction));
            else Console_set_tech(self, mw(self, kConFaction));
            break;
        case 'v':
            if (net_busy) break;
            if (!MapWinInVehMode) {
                Console_set_move(self, 0);
            } else {
                Console_set_view(self, 1);  // inlined
                Console_focus(self, MapWinCursorX[MapWinCursorIndex], MapWinCursorY[MapWinCursorIndex],
                              mw(self, kConFaction));
            }
            break;
        default: break;
        }
    }
    if (key == ':') {
        if (DebugKeyCount == -1) DebugKeyCount = 0;
    } else if (key == '~') {
        bool toggle = (GameState & (STATE_SCENARIO_CHEATED_FLAG | STATE_SCENARIO_EDITOR)) != 0;
        if (!toggle) {
            if (DebugKeyCount == -1 ? !MultiplayerActive : DebugKeyCount < 4) DebugKeyCount++;
            toggle = DebugKeyCount >= (MultiplayerActive ? 4 : 0);
        }
        if (toggle) {
            // Debug mode on/off.
            GameState ^= STATE_DEBUG_MODE;
            if (MultiplayerActive && (GameState & STATE_DEBUG_MODE) && !(GameState & STATE_SCENARIO_CHEATED_FLAG))
                GameState |= (STATE_SCENARIO_CHEATED_FLAG | STATE_OMNISCIENT_VIEW);
            set_dirty();
            draw_map(1);
            GraphicWin_redraw(&WorldWin);
            update_panes(self);
        }
    }
    if (!Console_edit_lock(self)) {
        if (key == '*') Console_zoom_to_upkeep_base(self);
        if (vk >= 0x31 && vk <= 0x36) {
            // Digits: commlink to the n-th other faction.
            int32_t other = vk - 0x30;
            if (CurrentPlayerFaction <= other) other = vk - 0x2F;
            if ((Factions[CurrentPlayerFaction].diplo_status[other] & 8) || (GameState & STATE_OMNISCIENT_VIEW) ||
                (SecretProjects[FAC_EMPATH_GUILD - SP_ID_First] >= 0 &&
                 Bases[SecretProjects[FAC_EMPATH_GUILD - SP_ID_First]].faction_id == CurrentPlayerFaction) ||
                ((FactionStatus[0] & (1 << (other & 31))) && auto_contact()))
                commlink_attempt(other);
        }
    }
    if (key == 'w' || vk == 9) {  // wait / Tab
        if (!net_busy && !Console_edit_lock(self)) {
            if (!MapWinInVehMode) Console_set_move(self, 0);
            else Console_wait(self, mw(self, kConVeh));
        } else if (Console_edit_lock(self)) {
            NetMsg_pop(&NetMsg, "PROTECTED", 5000, 0, nullptr);
        }
    }
    console_function_keys(self, vk, false);
    // Movement: keypad, arrows, Home/End/PgUp/PgDn (ctrl: a move order; shift: scroll the view).
    int32_t ctrl = vk & 0x20000;
    uint32_t k = ctrl ? (vk & 0xFFFCFFFF) : vk;
    int32_t dir = -1;
    switch (k) {
    case 0x21:
    case 0x69: dir = 0; break;  // PgUp, keypad 9: north-east
    case 0x27:
    case 0x66: dir = 1; break;  // right, keypad 6
    case 0x22:
    case 0x63: dir = 2; break;  // PgDn, keypad 3
    case 0x28:
    case 0x62: dir = 3; break;  // down, keypad 2
    case 0x23:
    case 0x61: dir = 4; break;  // End, keypad 1
    case 0x25:
    case 0x64: dir = 5; break;  // left, keypad 4
    case 0x24:
    case 0x67: dir = 6; break;  // Home, keypad 7
    case 0x26:
    case 0x68: dir = 7; break;  // up, keypad 8
    default: break;
    }
    if (dir >= 0) {
        if (ctrl && (vk & 0x10000)) {
            if (dir == 5) StatusWin_arrow_left(&InfoWin);
            else if (dir == 1) StatusWin_arrow_right(&InfoWin);
            return 1;
        }
        if (MapWinInVehMode) {
            if (!Console_edit_lock(self)) {
                int32_t v = mw(self, kConVeh);
                if (!net_busy && v >= 0) {
                    if (!ctrl) {
                        if (not_my_turn()) {
                            NetMsg_pop(&NetMsg, "SIMULNO", 5000, 0, nullptr);
                        } else if (!veh_ready(v)) {
                            NetMsg_pop(&NetMsg, "UNITMOVED", 5000, 0, nullptr);
                            mw(self, kConWaiting) = 0;
                        } else {
                            net_order_veh(v, dir, 0);
                        }
                    } else {
                        // ctrl: a one-step move order.
                        Vehs[v].order = ORDER_MOVE;
                        Vehs[v].waypoint_x[0] = (int16_t)dir;
                        Vehs[v].waypoint_x[1] = Vehs[v].x;
                        Vehs[v].waypoint_y[1] = Vehs[v].y;
                    }
                }
                return 1;
            }
            Console_set_view(self, 1);  // inlined
        }
        // View mode: the cursor moves.
        int32_t nx = NearOffsetX[dir] + MapWinCursorX[MapWinCursorIndex];
        if (!(MapToggleFlat & 1)) {
            if (nx < 0) nx += MapAreaX;
            else if (MapAreaX <= nx) nx -= MapAreaX;
        }
        int32_t ny = NearOffsetY[dir] + MapWinCursorY[MapWinCursorIndex];
        if (in_map(nx, ny)) {
            hide_cursor(self);
            Console_cursor_next(self, nx, ny);
            StatusWin_redraw(&InfoWin);
            mw(self, kConCursorShown) = 1;
            mw(self, kConCursorOn) = 1;
            MapWin_draw_cursor(main_view());
            MapWin_focus(main_view(), nx, ny);
        }
        return 1;
    }
    uint8_t* w = main_view();
    switch (k) {
    case 0x10026:
    case 0x10068:
    case 0x10027:
    case 0x10066:
    case 0x10028:
    case 0x10062:
    case 0x10025:
    case 0x10064: {
        // shift+direction: scroll the main view by a third of its size.
        int32_t cx = mw(w, kMwCenterX), cy = mw(w, kMwCenterY);
        int32_t x = cx, y = cy;
        if (k == 0x10026 || k == 0x10068) {
            int32_t step = (mw(w, kMwRowsAbove) + 2) / 3, room = mw(w, kMwOriginY);
            if (step < 0 || room < 0) step = 0;
            else if (room < step) step = room;
            y = cy - step;
        } else if (k == 0x10028 || k == 0x10062) {
            int32_t step = (mw(w, kMwRowsAbove) + 2) / 3;
            int32_t room = MapAreaY - mw(w, kMwRowsBelow) * 2 - mw(w, kMwOriginY) + 1;
            if (step < 0 || room < 0) room = 0;
            else if (step <= room) room = step;
            y = cy + room;
        } else {
            int32_t step = (mw(w, kMwColsOdd) + 2) / 3;
            x = (k == 0x10027 || k == 0x10066) ? cx + step : cx - step;
            if (!(MapToggleFlat & 1)) {
                if (x < 0) x += MapAreaX;
                else if (MapAreaX <= x) x -= MapAreaX;
            }
        }
        if (x != cx || y != cy) pfncMoveMap(w, x, y, 1);
        break;
    }
    default: break;
    }
    if (MapWinInVehMode) {
        if (!Console_edit_lock(self)) {
            if (MapWinVehA >= 0) Console_veh_key(self, ch, key, vk);
            return 1;
        }
        Console_set_view(self, 1);  // inlined
    }
    Console_cursor_key(self, ch, key, vk);
    return 1;
}
