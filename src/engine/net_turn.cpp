// The network game's turn (turn module 0x523C60, 0x528EA0..0x52A9B0): net_control_turn runs the
// rounds (control_turn's network counterpart), net_upkeep the upkeep every machine performs in
// lockstep: phases closed by net_upkeep_phase (each player reports 0x2303, the server answers 0x4303
// with the seed of the next phase), the factions' upkeep, the players' own bases synchronised, the
// technologies, the computer factions' turns, the checksums compared (a difference: the server
// resends the block). Then each player moves (simultaneously, or one at a time with GameMoreRules
// 0x10: 0x4309 names the player) and net_end_of_turn waits until every player is done (0x8301; the
// server's 0x4301 starts the next turn). mash_planes crashes the aircraft out of fuel.
// Approximated: the debug log file of Multi Debug (k:\breynolds\log<n>.txt, MULTIDEBUG) is not
// written (the checksums go to the game log as with MultiDebugActive).
#include <cstdio>
#include <cstring>

#include "engine/base.h"
#include "engine/battle.h"
#include "engine/design.h"
#include "engine/globals.h"
#include "engine/log.h"
#include "engine/map.h"
#include "engine/memmap.h"
#include "engine/net.h"
#include "engine/net_misc.h"
#include "engine/parse.h"
#include "engine/prefs.h"
#include "engine/random.h"
#include "engine/save.h"
#include "engine/stack.h"
#include "engine/tech.h"
#include "engine/turn.h"
#include "engine/ui_bridge.h"
#include "engine/veh.h"
#include "engine/ai.h"
#include "engine/clock.h"
#include "engine/crt.h"

namespace {
int32_t& g32(uint32_t addr) { return *reinterpret_cast<int32_t*>(mem::host(addr)); }
int32_t& Sess(int32_t i) { return NetSessionState[i]; }  // 0x93E8C0 + 4i
// NetSessionState: [0] server, [11] (0x93E8EC) turn done mask, [12] upkeep phase done mask,
// [15] (0x93E8FC) in the upkeep phase, [14] (0x93E8F8) end of turn known, [4] (0x93E8D0) phase,
// [5] (0x93E8D4), [6] (0x93E8D8) the game's synch progress, [41] / [42] (0x93E964 / 0x93E968).
int32_t& DoneMask() { return Sess(11); }
int32_t& UpkeepDone() { return Sess(12); }
int32_t& UpkeepIn() { return Sess(15); }
int32_t& EndKnown() { return Sess(14); }
uint8_t* Lock() { return &NetState + 0x14A0; }
int32_t& TurnTimeTotal() { return g32(0x9A64E4); }
void drain() {
    while (NetDaemon_receive(&NetState)) {}
}
}  // namespace

void GAMEAPI set_time_controls() {
    const CTimeControl& tc = TimeControl[GameTimeControl];
    int32_t seconds = tc.turn;
    int32_t players = 0, base_time = 0, unit_time = 0;
    uint8_t humans = FactionStatus[0];
    for (int32_t f = 1; f < 8; f++) {
        int32_t& pills = *reinterpret_cast<int32_t*>(mem::host(0x96C9E0 + 0x2054 + (uint32_t)f * 0x20CC));
        if (ControlTurnC && !ControlTurnMove && tc.refresh && CurrentTurn % tc.refresh == 0)
            pills = pills + 1 >= tc.accumulated ? tc.accumulated : pills + 1;
        if (!(humans & (1 << f))) continue;
        base_time += tc.base * *reinterpret_cast<int32_t*>(mem::host(0x96C9E0 + 0x105C + (uint32_t)f * 0x20CC));
        players++;
        for (int32_t v = 0; v < VehCount; v++) {
            const VEH& veh = Vehs[v];
            uint32_t st = veh.state;
            if (veh.faction_id == f && veh.order != 1 && veh.order != 2 && !(st & 0x200) &&
                (st & 0x1000200) != 0x1000200 && !(st & 0x4000))
                unit_time += tc.unit;
        }
    }
    if (players) {
        int32_t t = (unit_time + base_time) / players + tc.event * Sess(42);
        if (seconds <= t) seconds = t;
    }
    if (CurrentTurn < 6) seconds *= 2;
    TurnTimeTotal() = seconds * 1000;
}

int32_t GAMEAPI net_upkeep_phase(int32_t phase) {
    UpkeepIn() |= 1 << (CurrentPlayerFaction & 31);
    ui_multiwin_draw();
    log_say_values("Upkeep Phase", phase, CurrentTurn, UpkeepDone());
    message_data(0x2303, 0, phase, CurrentTurn, 0, 0);
    uint32_t start = engine_real_time();
    bool shown = phase > 3;
    while ((FactionStatus[0] & UpkeepDone()) != FactionStatus[0]) {
        if (ControlTurnA) return 1;
        if (engine_real_time() - start > 1000) {
            if (!shown) {
                parse_num(0, UpkeepDone());
                parse_num(1, FactionStatus[0]);
                char label[80];
                std::snprintf(label, sizeof(label), "UPKEEP%d", phase);
                NetMsg_pop(&NetMsg, label, 0, 1, nullptr);
                shown = true;
            }
            start = engine_real_time();
        }
        NetDaemon_net_tasks();
    }
    if (ControlTurnA) return 1;
    if (!Sess(0)) {
        log_say_values("Client falling out of upkeep loop", phase, 0, 0);
    } else {
        log_say_values("Server sending upkeep message", phase, 0, 0);
        message_data(0x4303, 0, phase, CurrentTurn, 0, 0);
        UpkeepDone() = 0;
        if (FactionStatus[0]) {
            while ((FactionStatus[0] & UpkeepDone()) != FactionStatus[0]) {
                if (!NetDaemon_receive(&NetState) && (ControlTurnA || !net_link_active())) break;
            }
        }
    }
    NetMsg_close(&NetMsg);
    UpkeepIn() = 0;
    UpkeepDone() = 0;
    return ControlTurnA;
}

void GAMEAPI net_upkeep() {
    ui_net_close_windows();  // (GraphicWin_close)
    NetUpkeepCount = 0;
    NetUpkeepState = 1;
    Sess(13) = 0;  // 0x93E8F4
    for (int32_t b = 0; b < BaseCount; b++) {
        Bases[b].state_flags &= 0xEFFFFFEF;
        if (Bases[b].faction_id == CurrentPlayerFaction) synch_radius(b);
    }
    for (int32_t v = 0; v < VehCount; v++) veh_promote(v);
    if (net_upkeep_phase(NetUpkeepCount)) return;
    NetUpkeepCount++;
    for (int32_t i = 0; i < 8; i++) {
        int32_t f = (CurrentTurn + i) & 7;
        if (FactionStatus[1] & (1 << f)) faction_upkeep(f);
    }
    if (net_upkeep_phase(NetUpkeepCount)) return;
    NetUpkeepCount++;
    message_data(0x2305, 0, Sess(41), 0, 0, 0);
    if (!Sess(13)) {
        log_say_values("Skipping upkeep synch", NetUpkeepCount, 0, 0);
    } else {
        for (int32_t b = 0; b < BaseCount; b++) {
            if (Bases[b].faction_id == CurrentPlayerFaction && (Bases[b].state_flags & 0x10000010)) {
                synch_base(b);
                drain();
                do_all_tasks();
            }
        }
        synch_leader(CurrentPlayerFaction);
        if (!Sess(13)) {
            log_say_values("Skipping upkeep synch", NetUpkeepCount, 0, 0);
        } else {
            drain();
            do_all_tasks();
            if (net_upkeep_phase(NetUpkeepCount)) return;
        }
    }
    NetUpkeepState = 2;
    NetUpkeepCount++;
    int32_t changed = 0;  // 1: another human's technology, 2: the viewer's
    for (int32_t f = 1; f < 8; f++) {
        uint8_t bit = (uint8_t)(1 << f);
        if (!(FactionStatus[1] & bit)) continue;
        uint8_t* fa = mem::host(0x96C9E0 + (uint32_t)f * 0x20CC);
        int32_t& saved = *reinterpret_cast<int32_t*>(fa + 0x3D4);  // technologies earned, not yet applied
        int32_t& research = *reinterpret_cast<int32_t*>(fa + 0x3CC);
        while (saved) {
            log_say("Upkeep Advance", MFactions[f].noun_faction, f, saved, 0);
            saved--;
            tech_advance(f);
            if (FactionStatus[0] & bit) changed |= (f == CurrentPlayerFaction) + 1;
        }
        if (*reinterpret_cast<int32_t*>(fa + 0x3C8) && research < 0) {
            log_say_values("Set Research", f, 0, 0);
            research = tech_selection(f);
            if (FactionStatus[0] & bit) changed |= (f == CurrentPlayerFaction) + 1;
        }
        int32_t& flags = *reinterpret_cast<int32_t*>(fa);
        if (flags & 2) consider_designs(f);
        flags &= ~2;
    }
    if (!changed) {
        log_say_values("Skipping upkeep synch", NetUpkeepCount, 0, 0);
    } else if (net_upkeep_phase(NetUpkeepCount)) {
        return;
    }
    NetUpkeepCount++;
    NetUpkeepState = 0;
    if ((changed & 2) && (GameRules & 0x200)) ui_console_set_ai(CurrentPlayerFaction);
    NetMsg_pop(&NetMsg, "UPKEEP4", 0, 1, nullptr);
    int32_t upgraded = 0;
    for (int32_t u = 0x40; u < 0x200; u++) {
        UNIT& unit = Units[u];
        int32_t f = u / 0x40;
        if ((unit.unit_flags & 1) && !(unit.obsolete_factions & (1 << f)) && (unit.unit_flags & 0x100)) {
            upgraded = 1;
            unit.unit_flags &= 0xFEFF;
            if (f == CurrentPlayerFaction && (GameMorePreferences & 0x100000)) upgrade_prototypes(CurrentPlayerFaction, u);
        }
    }
    bool phase = true;
    if (CouncilNetA < 1) {
        if (!upgraded) {
            log_say_values("Skipping upkeep synch", NetUpkeepCount, 0, 0);
            phase = false;
        }
    } else {
        if (Sess(0)) {
            message_data(0x2600, 0, CouncilNetA, CouncilNetB, CouncilNetC, 0);
            ControlWaitLoop = 1;
            while (!ControlTurnA && ControlWaitLoop) wait_loop();
        }
        CouncilNetA = -1;
        CouncilNetB = -1;
        CouncilNetC = -1;
    }
    if (phase && net_upkeep_phase(NetUpkeepCount)) return;
    NetUpkeepCount++;
    NetMsg_pop(&NetMsg, "UPKEEP5", 0, 1, nullptr);
    for (int32_t f = 0; f < 8; f++) {
        uint8_t bit = (uint8_t)(1 << f);
        if ((FactionStatus[1] & bit) && !(FactionStatus[0] & bit)) {
            log_say_values("Enemy turn", f, 0, 0);
            enemy_turn(f);
        }
    }
    if (net_upkeep_phase(NetUpkeepCount)) return;
    NetUpkeepCount++;
    NetMsg_pop(&NetMsg, "UPKEEP6", 0, 1, nullptr);
    Sess(-1) = 0;  // 0x93E8BC: the event sequence
    for (int32_t i = 0; i < 0x18; i++) DeletionList_clear(&DeleteList, i);
    do_checksums(4);
    if (GameMorePreferences & 2) auto_save();  // (Multi Debug: k:\breynolds\multisaves\ instead)
    message_big_data(0x2307, 0, reinterpret_cast<const int32_t*>(&MasterChecksum), 0x15);
    if (net_upkeep_phase(NetUpkeepCount)) return;
    Sess(6) = -1;
    NetUpkeepCount++;
    bool resynch = false;
    bool tried[0x15] = {};
    const uint32_t* master = MasterChecksum.values;
    for (int32_t f = 1; f < 8; f++) {
        uint8_t bit = (uint8_t)(1 << f);
        if (!(FactionStatus[1] & bit) || !(FactionStatus[0] & bit) || f == CurrentPlayerFaction) continue;
        const uint32_t* remote = reinterpret_cast<const uint32_t*>(&RemoteChecksum) + f * 0x15;
        for (int32_t k = 0; k < 0x15; k++) {
            if (master[k] == remote[k]) continue;
            if (!resynch) NetMsg_pop(&NetMsg, "UPKEEP7", 0, 1, nullptr);
            resynch = true;
            std::fprintf(stderr, "net: checksum %d differs from faction %d's (turn %d)\n", k, f, CurrentTurn);
            if (tried[k] || !Sess(0)) continue;
            tried[k] = true;
            switch (k) {
            case 0:
                log_say_values("Failed GAME checksum - resynch", f, master[0], remote[0]);
                NetDaemon_synch(&NetState, 0, 0, 0, 0, nullptr, 1, 0x2101);
                drain();
                NetDaemon_synch(&NetState, 0x24, 0, 0, 0, nullptr, 1, 0x2101);
                drain();
                break;
            case 1:
                log_say_values("Failed LEADERS checksum - resynch", f, master[1], remote[1]);
                for (int32_t g = 0, u = 0; u < 0x200; g++, u += 0x40) {
                    NetDaemon_synch(&NetState, 6, g, 0, 0, nullptr, 1, 0x2101);
                    drain();
                    NetDaemon_synch(&NetState, 7, g, 0, 0, nullptr, 1, 0x2101);
                    drain();
                    NetDaemon_synch(&NetState, 0xF, u, 0x40, 0, nullptr, 1, 0x2101);
                    drain();
                }
                break;
            case 2:
                log_say_values("Failed MAP checksum - resynch", f, master[2], remote[2]);
                NetDaemon_synch(&NetState, 0x1002, 0, 0, 0, nullptr, 1, 0x2101);
                drain();
                NetDaemon_synch(&NetState, 5, 0, 0, 0, nullptr, 1, 0x2101);
                drain();
                break;
            case 3:
                log_say_values("Failed VEHICLES checksum - resynch", f, master[3], remote[3]);
                NetDaemon_synch(&NetState, 0x25, 0, 0, 0, nullptr, 1, 0x2101);
                drain();
                NetDaemon_synch(&NetState, 0x12, 0, 0, 0, nullptr, 1, 0x2101);
                drain();
                break;
            case 4:
                log_say_values("Failed BASES checksum - resynch", f, master[4], remote[4]);
                NetDaemon_synch(&NetState, 0x26, 0, 0, 0, nullptr, 1, 0x2101);
                drain();
                NetDaemon_synch(&NetState, 0x14, 0, 0, 0, nullptr, 1, 0x2101);
                drain();
                break;
            default:
                log_say_values("Failed MAP section chcksum - resynch", f, master[k], remote[k]);
                NetDaemon_synch(&NetState, 0x30, k - 5, 0, 0, nullptr, 1, 0x2101);
                drain();
                break;
            }
        }
    }
    NetMsg_close(&NetMsg);
    g32(0x93F674) = 0;
    g32(0x93F670) = 0;
    if (resynch) {
        net_upkeep_phase(NetUpkeepCount);
        return;
    }
    log_say_values("Skipping upkeep phase", NetUpkeepCount, 0, 0);
}

void GAMEAPI net_not_my_turn() {
    MapWinField23BE4 = 0;
    MapWinField23BE8 = 1;
    ui_console_set_view();
    ui_multiwin_draw();
    DoneMask() |= 1 << (CurrentPlayerFaction & 31);
    while (!ControlTurnA && !NetNextPlayerTurn && CurrentFaction != CurrentPlayerFaction) {
        MapWinField23BE4 = 1;
        NetDaemon_net_tasks();
        if (Sess(0)) {
            uint8_t bit = (uint8_t)(1 << (CurrentFaction & 31));
            if (CurrentFaction < 1 || !(FactionStatus[0] & bit) || !(FactionStatus[1] & bit)) next_player_turn();
        }
    }
    MapWinField23BE4 = 0;
    MapWinField23BE8 = 0;
}

void GAMEAPI mash_planes() {
    for (int32_t v = VehCount - 1; v >= 0; v--) {
        VEH& veh = Vehs[v];
        const UNIT& unit = Units[veh.unit_id];
        const CChassis& ch = Chassis[unit.chassis_id];
        if (ch.triad != 2 || !ch.range) continue;
        int32_t sp = speed(v, 0);
        int32_t left = sp - veh.moves_spent;
        if (left < 0 || (left <= 999 && sp == veh.moves_spent)) continue;
        MAP* sq = map_tile(veh.x, veh.y);
        if ((sq->items & 1) && (sq->val2 & 0xF) <= 7) continue;
        if (sq->items & 0x40000) continue;
        if (stack_check(v, 6, 0x80, -1, -1)) continue;
        bool seen = false;
        if (veh.faction_id == CurrentPlayerFaction || (veh.visibility & (1 << (CurrentPlayerFaction & 31)))) {
            Console_focus(&MainConsole, veh.x, veh.y, veh.faction_id);
            seen = true;
        }
        veh.state |= 4;
        if (seen) boom(veh.x, veh.y, 0x80);
        auto max_hp = [&]() -> int32_t {
            if (unit.plan == 0xC) return 1;
            int32_t r = unit.reactor_id;
            if (r == 0) r = 1;
            else if (r > 100) r = 100;
            return r * 10;
        };
        if (ch.range == 1 && unit.chassis_id == 6) {
            veh.damage_taken = (uint8_t)(veh.damage_taken + (uint16_t)((int8_t)unit.reactor_id * 3));
        } else {
            int32_t add = max_hp() - veh.damage_taken;
            if (add < 0) add = 0;
            else if (add > 9999) add = 9999;
            veh.damage_taken = (uint8_t)(veh.damage_taken + add);
        }
        int32_t hp = max_hp() - veh.damage_taken;
        if (hp < 0 || (hp < 10000 && hp < 1)) {
            kill(v);
        } else {
            veh.movement_turns = 0;
            draw_tile(veh.x, veh.y, 2);
        }
    }
}

void GAMEAPI net_end_of_turn() {
    if (GameMoreRules & 0x10) {
        if (!Sess(0)) {
            while (!ControlTurnA && !NetNextPlayerTurn) NetDaemon_net_tasks();
        } else {
            message_data(0x4301, 0, 0, 0, 0, 0);
        }
    }
    log_say_values("Entering 'end of turn' loop", DoneMask(), EndKnown(), GameState & 2);
    FX_play(&Sounds, 0x22);
    GameState |= 2;
    MapWinField23BE4 = 0;
    ui_console_set_view();
    if (!Sess(0)) DoneMask() |= 1 << (CurrentPlayerFaction & 31);
    EndKnown() |= 1 << (CurrentPlayerFaction & 31);
    ui_multiwin_draw();
    if (!(GameMoreRules & 0x10)) {
        message_data(0x8301, 0, 0, 0, 0, 0);
        uint32_t start = engine_real_time();
        bool shown = false;
        for (;;) {
            uint32_t humans = FactionStatus[0];
            bool waiting = (humans & (uint32_t)DoneMask()) != humans || (Sess(8) & humans) ||
                           (Sess(0) && (Lock_any_locks(Lock()) || Sess(19)));
            if (!waiting || ControlTurnA || !(GameState & 2)) break;
            if (!shown && engine_real_time() - start > 1000) {
                NetMsg_pop(&NetMsg, "FINISHINGTURN", 0, 0, nullptr);
                shown = true;
            }
            NetDaemon_net_tasks();
        }
        if (shown) NetMsg_close(&NetMsg);
        if (GameState & 2) {
            if (!Sess(0)) {
                log_say_values("Ready to proceed", DoneMask(), FactionStatus[0], 0);
            }
        }
        if (Sess(0)) {
            if (!(GameState & 2)) return;
            if (!ControlTurnA) {
                // The players still executing an event: wait for their acknowledgements.
                for (;;) {
                    int32_t busy = 0;
                    for (int32_t i = 1; i < *reinterpret_cast<int32_t*>(&NetState + 0x768); i++) {
                        uint8_t* pl = Lock() + i * 0x1C;
                        if (PlayerLock_active(pl) && !(pl[0] & 1)) busy++;
                    }
                    if (!busy) break;
                    wait_task();
                    NetDaemon_receive(&NetState);
                }
                message_data(0x4301, 0, 0, 0, 0, 0);
                NetDaemon_receive(&NetState);
            }
        }
        if (!(GameState & 2)) return;
    }
    log_say_values("----- Proceeding to next turn -----", 0, 0, 0);
    UpkeepDone() = 0;
    UpkeepIn() = 0;
    NetUpkeepCount = 0;
    ControlTurnC = 1;
    ui_net_end_of_turn();  // (BaseWin_exit when shown, go_reset)
    while (NetDaemon_receive(&NetState)) {}
    mash_planes();
    do_checksums(0);
    Sess(-1)++;  // 0x93E8BC
}

void GAMEAPI net_control_turn() {
    draw_map(1);
    game_srand((uint32_t)MapRandomSeed);
    ControlTurnC = 1;
    g32(0x93A940) = 1;
    int32_t debug = prefs_get_int("Multi Debug", 0, 0);
    MultiDebugActive = debug && X_pop("MULTIDEBUG", 0) ? 1 : 0;
    log_set_state(MultiDebugActive);
    do_checksums(1);
    if (GameMoreRules & 0x10) NetPlayerTurnDone = 1;
    for (;;) {
        log_say_values("Clearing turn semaphore", DoneMask(), EndKnown(), 0);
        DoneMask() = 0;
        EndKnown() = 0;
        Sess(5) = 1;
        Sess(42) = 0;
        Sess(41) = 0;
        NetNextPlayerTurn = 0;
        ui_net_clear_messages();  // (MessageWin_clear)
        if (!ControlTurnMove) {
            CurrentFaction = 1;
            while (!((FactionStatus[1] & (1 << CurrentFaction)) && (FactionStatus[0] & (1 << CurrentFaction))))
                CurrentFaction++;
            if (g_harness_net_turn) g_harness_net_turn();
            ui_net_turn("upkeep");
            turn_upkeep();
            net_upkeep();
            CurrentFaction = 0;
            if (end_of_game(1)) {
                NetDaemon_hang_up(&NetState);
                net_session_clear();
                MultiplayerActive = 0;
                return;
            }
        }
        set_time_controls();
        NetTurnTimeLeft = TurnTimeTotal();
        GameState &= ~0x800;
        ControlTurnC = 0;
        Sess(9) = 0;  // 0x93E8E4
        if (GameMoreRules & 0x10) {
            if (CurrentTurn == 1 || ControlTurnMove) {
                int32_t x = -1, y = -1;
                for (int32_t b = 0; b < BaseCount; b++) {
                    if (Bases[b].faction_id == CurrentPlayerFaction) {
                        x = Bases[b].x;
                        y = Bases[b].y;
                        break;
                    }
                }
                if (x < 0) {
                    for (int32_t v = 0; v < VehCount; v++) {
                        if (Vehs[v].faction_id == CurrentPlayerFaction) {
                            x = Vehs[v].x;
                            y = Vehs[v].y;
                            break;
                        }
                    }
                }
                if (y >= 0 && y < MapAreaY && x >= 0 && x < MapAreaX) {
                    ui_console_set_view();
                    ui_net_center(x, y);  // (pfncMoveMap, Console_set_cursor)
                }
            }
            if (Sess(0)) {
                if (!ControlTurnMove) next_player_turn();
                else message_data(0x4309, 0, CurrentFaction, 0, 0, 0);
            }
        }
        ControlTurnMove = 0;
        ui_net_turn("moves");
        bool repeat;
        do {
            if (!ControlTurnA) {
                if (!(GameMoreRules & 0x10) || CurrentFaction == CurrentPlayerFaction) {
                    bool play = true;
                    if (!(GameMoreRules & 0x10)) {
                        if (GameState & 0x800) {
                            GameState |= 2;
                            play = false;
                        }
                    } else {
                        NetTurnTimeLeft = TurnTimeTotal();
                        GameState &= ~0x800;
                        NetPlayerTurnDone = 0;
                        DoneMask() &= ~(1 << (CurrentPlayerFaction & 31));
                    }
                    if (play) {
                        Console_human_turn(&MainConsole);
                        if (GameMoreRules & 0x10) {
                            NetPlayerTurnDone = 1;
                            while (!ControlTurnA && Lock_any_locks(Lock())) NetDaemon_net_tasks();
                            message_data(0x8301, 0, 0, 0, 0, 0);
                            while (!ControlTurnA && CurrentFaction == CurrentPlayerFaction && !NetNextPlayerTurn)
                                NetDaemon_net_tasks();
                            if (!NetNextPlayerTurn) GameState &= ~0x800;
                        }
                    }
                } else {
                    net_not_my_turn();
                }
            } else {
                GameState |= 2;
            }
            if (ControlTurnA) break;
            if (!(GameMoreRules & 0x10)) {
                net_end_of_turn();
                if (ControlTurnA) break;
                repeat = (GameMoreRules & 0x10) ? !NetNextPlayerTurn : !(GameState & 2);
            } else {
                repeat = !NetNextPlayerTurn;
            }
        } while (repeat);
        if (GameMoreRules & 0x10) net_end_of_turn();
        if (ControlTurnA) return;
    }
}
