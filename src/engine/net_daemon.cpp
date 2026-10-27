// The NetDaemon (original module 0x52DBA0..0x538F30) and the message / synch senders of the map
// module (0x592E10..0x593510): the network game's message protocol, kept as the original's (message
// types, payloads, routing, locks, the synch blocks). Only the transport below it (Net_*,
// DirectPlay in the original) is smacx's (engine/net_link.cpp, platform/net_tcp).
//
// The NetDaemon object NetState (0x93CD90) begins with the Net / AlphaNet object (+0x760 the own
// player id, +0x764 the host's, +0x768 the number of players, the player slots 1..7 of 0x19C bytes
// from +0x76C: the name, +0x20 the pid, +0x24 the faction), then the Lock object (+0x14A0, NetLock,
// engine/net_lock.cpp) and the daemon's state (+0x1B2C.., NetSessionState):
//   +0x1B2C the event sequence number (the host numbers the events it relays; turn counter),
//   +0x1B30 this machine is the server (host), +0x1B34 setup data received, +0x1B38 a base lock
//   pending, +0x1B3C hung up, +0x1B40 the phase (1 the setup window, 2 the game), +0x1B44, +0x1B48 /
//   +0x1B4C the game's / the rules' synch progress, +0x1B50 the players who paused, +0x1B54,
//   +0x1B5C the factions done with their turn, +0x1B60 the factions done with the upkeep phase,
//   +0x1B64 an upkeep base lock, +0x1B68 the factions whose end of turn is known, +0x1B6C the
//   factions in the upkeep phase, +0x1B70 the answer to a lock request (1 granted, 2 refused, 3
//   refused: global, 4 refused: upkeep), +0x1B78 the locked unit (a pointer to its id), +0x1B7C /
//   +0x1B80[8] the diplomatic channels, +0x1BA4 the channel answer, +0x1BA8 an awaited diplomatic
//   event, +0x1BAC a lock held, +0x1BB0 an execution awaited, +0x1BB4 an automated lock, +0x1BB8 a
//   synch acknowledgement awaited, +0x1BBC the senders of a synch in progress, +0x1BC0 / +0x1BC4 /
//   +0x1BC8 / +0x1BCC the lock's retries, +0x1BD0.
// Messages: a 16-byte header (the type in the low word of +0, +4 the sender's faction, +8 the time
// (the host's timeGetTime when it relays the event: the random seed of its execution), +0xC the
// sequence number) and their arguments from +0x10. The type's bits route it (NetDaemon_send_message):
// 0x4000 to every player (timed), 0x8000 to the host, 0x2000 to the server of the moment, else to
// the faction given; the host relays the 0x2000 / 0x8000 events it does not answer itself as 0x4000
// (NetDaemon_process_message's default), so that every machine executes them in the same order
// with the same seed.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "engine/action.h"
#include "engine/base.h"
#include "engine/battle.h"
#include "engine/clock.h"
#include "engine/path.h"
#include "engine/crt.h"
#include "engine/diplo_act.h"
#include "engine/strings.h"
#include "engine/diplomacy.h"
#include "engine/game_state.h"
#include "engine/globals.h"
#include "engine/log.h"
#include "engine/map.h"
#include "engine/memmap.h"
#include "engine/move.h"
#include "engine/net.h"
#include "engine/net_misc.h"
#include "engine/parse.h"
#include "engine/prefs.h"
#include "engine/probe.h"
#include "engine/random.h"
#include "engine/social.h"
#include "engine/stack.h"
#include "engine/turn.h"
#include "engine/ui_bridge.h"
#include "engine/veh.h"
#include "platform/win32.h"

namespace {
int32_t& fld(uint8_t* net, uint32_t off) { return *reinterpret_cast<int32_t*>(net + off); }
int32_t& g32(uint32_t addr) { return *reinterpret_cast<int32_t*>(mem::host(addr)); }
uint8_t& g8(uint32_t addr) { return *mem::host(addr); }
int32_t& NetBusy() { return g32(0x93F664); }
int32_t& NetDropped() { return g32(0x93F65C); }
int32_t& NetLockRetryDelay() { return g32(0x93F668); }
int32_t& NetLockRetryStart() { return g32(0x93F66C); }
int32_t& SpockWanted() { return g32(0x93F670); }
int32_t& SpockBusy() { return g32(0x93F674); }

uint8_t* Lock() { return &NetState + 0x14A0; }

uint32_t now_ms() { return engine_real_time(); }

// The original's idle wait of a network request: the tasks, one message, the global lock, spock;
// after `popup_ms` a NetMsg box (`label`) until the condition clears.
template <class Cond>
void await(uint8_t* net, const char* label, uint32_t popup_ms, Cond waiting) {
    int32_t saved = VehAttackFlags;
    NetBusy()++;
    uint32_t start = now_ms();
    bool shown = false;
    while (waiting() && !ControlTurnA) {
        if (!shown && now_ms() - start > popup_ms) {
            shown = true;
            NetMsg_pop(&NetMsg, label, 0, 2, nullptr);
        }
        wait_task();
        int32_t from = 0, size = 0;
        if (uint8_t* m = Net_get(net, &from, &size)) NetDaemon_process_message(net, m, size, from);
        Lock_check_global(Lock());
        check_spock();
    }
    if (shown) NetMsg_close(&NetMsg);
    NetBusy()--;
    VehAttackFlags = saved;
}

// The client's release of its lock (inlined in many places of the original).
void release_lock(uint8_t* net) {
    if (!MultiplayerActive) return;
    log_say_values("Client releasing lock", CurrentPlayerFaction, 0, 0);
    message_data(0x2212, 0, 0, 0, 0, 0);
    fld(net, 0x1BB0) = 0;
    fld(net, 0x1BAC) = 0;
    fld(net, 0x1BCC) = 0;
    fld(net, 0x1BC8) = 0;
}
void release_veh(uint8_t* net) {
    release_lock(net);
    fld(net, 0x1B78) = 0;
    fld(net, 0x1BC4) = 0;
}
// The synch of a unit after an interrupted move and its acknowledgement (inlined).
void synch_veh_ack(uint8_t* net, int32_t veh_id) {
    Vehs[veh_id].order = 0;
    Vehs[veh_id].state &= 0xFCFFBDFF;
    synch_veh(veh_id);
    if (!MultiplayerActive) return;
    fld(net, 0x1BB8) = 1;
    log_say_values("Awaiting acknowledgement of SYNCH", CurrentPlayerFaction, 0, 0);
    NetTurnTimeLeft += NetDaemon_wait(net, "AWAITEXEC", &fld(net, 0x1BB8), 1, 2000);
    if (!fld(net, 0x1BB8)) log_say_values("Received SYNCH acknowledgement", CurrentPlayerFaction, 0, 0);
    fld(net, 0x1BB8) = 0;
}
void drain(uint8_t* net) {
    while (NetDaemon_receive(net)) {}
}
}  // namespace

// ---- AlphaNet: the player slots ----

void THISCALL AlphaNet_close(uint8_t* net) {
    for (int32_t i = 0; i < 8; i++) fld(net, 0x78C + i * 0x19C) = 0;
    fld(net, 0x768) = 0;
    Net_close(net);
}

int32_t THISCALL AlphaNet_pid_2_idx(uint8_t* net, int32_t pid) {
    for (int32_t i = 1; i < 8; i++) {
        if (fld(net, 0x78C + i * 0x19C) == pid) return i;
    }
    return 0;
}

int32_t THISCALL AlphaNet_pid_2_who(uint8_t* net, int32_t pid) {
    for (int32_t i = 1; i < 8; i++) {
        if (fld(net, 0x78C + i * 0x19C) == pid) return (int8_t)net[0x790 + i * 0x19C];
    }
    return 0;
}

int32_t THISCALL AlphaNet_who_2_pid(uint8_t* net, int32_t faction_id) {
    for (int32_t i = 1; i < 8; i++) {
        if ((int8_t)net[0x790 + i * 0x19C] == faction_id) return fld(net, 0x78C + i * 0x19C);
    }
    return 0;
}

int32_t THISCALL AlphaNet_who_2_idx(uint8_t* net, int32_t faction_id) {
    for (int32_t i = 1; i < 8; i++) {
        if ((int8_t)net[0x790 + i * 0x19C] == faction_id) return i;
    }
    return 0;
}

// ---- the server of the moment and the next player (one-at-a-time turns) ----

int32_t GAMEAPI current_server() {
    if ((GameMoreRules & 0x10) && !ControlTurnC) return CurrentFaction == CurrentPlayerFaction;
    return NetSessionState[0];
}

int32_t GAMEAPI next_player_turn() {
    if (!(GameMoreRules & 0x10) || !NetSessionState[0]) return 0;
    for (int32_t f = CurrentFaction + 1; f < 8; f++) {
        uint8_t bit = (uint8_t)(1 << f);
        if ((FactionStatus[1] & bit) && (FactionStatus[0] & bit)) {
            message_data(0x4309, 0, f, 0, 0, 0);
            return 0;
        }
    }
    NetNextPlayerTurn = 1;
    return 1;
}

// ---- the daemon's set up and end ----

int32_t THISCALL NetDaemon_init(uint8_t* net, int32_t lobby) {
    static const uint32_t kCleared[] = {0x1B30, 0x1B34, 0x1B38, 0x1B3C, 0x1B60, 0x1B5C, 0x1B6C, 0x1B68, 0x1B7C,
                                        0x1BA8, 0x1BA0, 0x1BA4, 0x1BCC, 0x1BC8, 0x1B50, 0x1B54, 0x1B58, 0x1BC4,
                                        0x1BC0, 0x1BB0, 0x1BAC, 0x1BB4, 0x1B40, 0x1B44, 0x1BD8, 0x1BD4, 0x1B48,
                                        0x1B4C};
    for (uint32_t off : kCleared) fld(net, off) = 0;
    fld(net, 0x1BD0) = 0xFF;
    fld(net, 0x1B78) = 0;
    for (int32_t i = 0; i < 8; i++) fld(net, 0x1B80 + i * 4) = 0;
    Lock_clear(Lock());
    AlphaNet_close(net);
    fld(net, 0x1B3C) = 0;
    fld(net, 0x1B38) = 0;
    return AlphaNet_setup(net, lobby) != 0;
}

void THISCALL NetDaemon_hang_up(uint8_t* net) {
    if (fld(net, 0x1B3C) || !MultiplayerActive) return;
    fld(net, 0x1B3C) = 1;
    bool send_done = false;
    if (GameMoreRules & 0x10) {
        if (!ControlTurnC) send_done = CurrentFaction == CurrentPlayerFaction;
        else if (!NetSessionState[0]) {
            NetMsg_close(&NetMsg);
            return;
        } else {
            send_done = true;
        }
    }
    if (send_done) message_data(0x8301, 0, 0, 0, 0, 0);
    if (NetSessionState[0]) {
        PlayerLock_clear(Lock() + CurrentPlayerFaction * 0x1C);
        if (fld(net, 0x1580) == CurrentPlayerFaction) {
            fld(net, 0x1584) = 0;
            fld(net, 0x1580) = 0;
        }
        fld(net, 0x1B7C) &= ~fld(net, 0x1B80 + CurrentPlayerFaction * 4);
        message_data(0x4B00, 0, 0, 0, 0, 0);
    }
    NetMsg_close(&NetMsg);
}

// The session state cleared when a network game ends (inlined after hang_up in the original).
void net_session_clear() {
    static const uint8_t kCleared[] = {0, 1, 2, 3, 12, 11, 15, 14, 19, 30, 28, 29, 39, 38,
                                       8, 9, 10, 37, 36, 32, 31, 33, 4, 5, 42, 41, 6, 7};
    for (uint8_t i : kCleared) NetSessionState[i] = 0;
    NetSessionState[40] = 0xFF;
    NetSessionState[18] = 0;
    for (int32_t i = 20; i < 28; i++) NetSessionState[i] = 0;
    Lock_clear(Lock());
    AlphaNet_close(&NetState);
}

// ---- players joining and leaving ----

void THISCALL NetDaemon_on_player_added(uint8_t* net, const char* name, int32_t pid) {
    int32_t server = fld(net, 0x760) == fld(net, 0x764);
    fld(net, 0x1B30) = server;
    if (fld(net, 0x768) < 7 && fld(net, 0x1B40) < 2) {
        int32_t n = ++fld(net, 0x768);
        fld(net, 0x78C + n * 0x19C) = pid;
        const char* pname = Net_get_player_name(net, pid);
        crt_strcpy(reinterpret_cast<char*>(net + 0x76C + n * 0x19C), pname ? pname : "");
        uint8_t* cfg = mem::host(0x90DB98 + n * 0x17C);
        cfg[2] = mem::host(0x90E8E0)[2];
        cfg[0] = (uint8_t)n;
        net[0x790 + n * 0x19C] = (uint8_t)n;
        bool self_known = false;
        for (int32_t i = 1; i < n; i++) {
            if (fld(net, 0x78C + i * 0x19C) == fld(net, 0x760)) self_known = true;
        }
        if (n > 1 && self_known) {
            parse_says(0, name, -1, -1);
            NetMsg_pop(&NetMsg, "ADDED", 3000, 0, nullptr);
        }
        if (fld(net, 0x1B30)) {
            *reinterpret_cast<int32_t*>(cfg + 0x178) = 0;
            cfg[0] = 0xFF;
            cfg[1] = 0;
            cfg[2] = 0;
            cfg[3] = 0xFF;
            cfg[4] = 2;
            cfg[3] = 0xFF;
            cfg[0] = (uint8_t)n;
            cfg[2] = mem::host(0x90E8E0)[2];
            cfg[1] = 0;
            send_all_player_packets(-1);
            send_session_packet(n, -1);
            send_session2_packet(n, -1);
            send_faction_data(n);
        }
        char note[120];
        std::snprintf(note, sizeof(note), "net: player %d joined (%s)", n, name ? name : "");
        ui_net_note_text(note);
        if (ui_netwin_visible()) ui_netwin_redraw();
    } else if (server) {
        uint8_t msg[0x20] = {};
        *reinterpret_cast<uint16_t*>(msg) = 0x1F01;  // the session is full / under way
        Net_send(net, msg, 0x20, pid, 1);
    }
}

void THISCALL NetDaemon_on_player_deleted(uint8_t* net, int32_t, int32_t pid) {
    bool server_lost = pid == fld(net, 0x764);
    parse_says(0, "", -1, -1);
    if (server_lost) NetSessionState[43] = 1;  // 0x93E96C
    int32_t idx = 1;
    int32_t paused = 0;
    int32_t f = 0;
    for (; idx <= fld(net, 0x768); idx++) {
        if (fld(net, 0x78C + idx * 0x19C) != pid) continue;
        if (fld(net, 0x1B50)) {
            paused = fld(net, 0x1B50) &= ~(1 << idx);
            if (!paused) FX_play(&Sounds, 0x15);
        }
        parse_says(0, reinterpret_cast<char*>(net + 0x76C + idx * 0x19C), -1, -1);
        f = (int8_t)net[0x790 + idx * 0x19C];
        if (fld(net, 0x1B40) == 2) FactionStatus[0] &= (uint8_t)~(1 << (f & 31));
        fld(net, 0x1BBC) &= ~(1 << (f & 31));
        if ((GameMoreRules & 0x10) && NetSessionState[0] && CurrentFaction == f) {
            for (int32_t n = CurrentFaction + 1; n < 8; n++) {
                uint8_t bit = (uint8_t)(1 << n);
                if ((FactionStatus[1] & bit) && (FactionStatus[0] & bit)) {
                    message_data(0x4309, 0, n, 0, 0, 0);
                    break;
                }
            }
        }
        break;
    }
    if (idx > fld(net, 0x768)) return;  // (not a player of the session)
    PlayerLock_clear(Lock() + f * 0x1C);
    fld(net, 0x1B7C) &= ~fld(net, 0x1B80 + f * 4);
    if (fld(net, 0x1580) == f) {
        fld(net, 0x1580) = 0;
        fld(net, 0x1584) = 0;
    }
    bool server_role = ((GameMoreRules & 0x10) && !ControlTurnC && CurrentFaction == CurrentPlayerFaction) ||
                       ((!(GameMoreRules & 0x10) || ControlTurnC) && NetSessionState[0]);
    if (server_role && fld(net, 0x1B40) == 2) {
        // The players still waiting for the dropped player's acknowledgement: their execution done.
        for (int32_t n = 1; n < 8; n++) {
            uint8_t bit = (uint8_t)(1 << n);
            uint8_t* pl = Lock() + n * 0x1C;
            if (!(FactionStatus[1] & bit) || !(FactionStatus[0] & bit) || n == f) continue;
            if (!PlayerLock_active(pl) || (pl[0] & 1)) continue;
            pl[0] |= (uint8_t)(1 << idx);
            if ((pl[0] & FactionStatus[0]) == FactionStatus[0]) {
                log_say_values("SPECIAL Sending EXEC DONE to", n, 0, 0);
                message_data(0x1206, n, 0, 0, 0, 0);
                pl[0] |= 1;
            }
        }
    }
    log_say(reinterpret_cast<char*>(ParseStrBuffer[0]), "Dropped ---------------------------", pid, idx, 0);
    uint8_t* slot = net + 0x76C + idx * 0x19C;
    slot[0x24] = 0xFF;
    slot[0x25] = 0;
    slot[0x26] = 0;
    slot[0x27] = 0xFF;
    slot[0x28] = 2;
    if (fld(net, 0x1B40) < 2) mem::host(0x90DB98 + idx * 0x17C)[0] = 0xFF;
    crt_strcpy(reinterpret_cast<char*>(mem::host(0x90DB9D + idx * 0x17C)), label_get(0xFE4 / 4));
    crt_strcpy(reinterpret_cast<char*>(mem::host(0x90DBDD + idx * 0x17C)), label_get(0xFE4 / 4));
    for (int32_t i = idx; i <= fld(net, 0x768) - 1; i++) {
        std::memcpy(net + 0x76C + i * 0x19C, net + 0x76C + (i + 1) * 0x19C, 0x19C);
        std::memcpy(mem::host(0x90DB98 + i * 0x17C), mem::host(0x90DB98 + (i + 1) * 0x17C), 0x17C);
        mem::host(0x90DB98 + i * 0x17C)[0] = (uint8_t)i;
    }
    fld(net, 0x768)--;
    fld(net, 0x1B30) = fld(net, 0x760) == fld(net, 0x764);
    if (fld(net, 0x1B40) == 2) {
        bool my_turn = (GameMoreRules & 0x10) && !ControlTurnC && CurrentFaction == CurrentPlayerFaction;
        bool host_role = !((GameMoreRules & 0x10) && !ControlTurnC) && NetSessionState[0];
        if (!my_turn && !host_role) {
            if (!(GameMoreRules & 0x10) && (NetTurnDoneMask & (1 << (CurrentPlayerFaction & 31))))
                message_data(0x8301, 0, 0, 0, 0, 0);
            if (ControlTurnC) message_data(0x2303, 0, NetUpkeepCount, CurrentTurn, 0, 0);
        }
    }
    NetMsg_pop(&NetMsg, server_lost ? (ui_netwin_visible() ? "SERVERLOSTINSETUP" : "SERVERLOST") : "DROPPED", 3000,
               2, nullptr);
    if (server_lost) {
        if (ui_netwin_visible()) ui_netwin_cancel();
        fld(net, 0x1BB0) = 0;
        fld(net, 0x1BB8) = 0;
        fld(net, 0x1BA8) = 0;
        fld(net, 0x1B70) = 2;
        NetDropped() = 1;
        fld(net, 0x1588) = 1;
    }
    ui_netwin_redraw();
    fld(net, 0x1B38) = 0;
    char note[80];
    std::snprintf(note, sizeof(note), "net: player %d left%s", pid, server_lost ? " (the host)" : "");
    ui_net_note_text(note);
}

// ---- sending and receiving ----

void THISCALL NetDaemon_send_message(uint8_t* net, void* msg, int32_t size, int32_t to) {
    if (!MultiplayerActive) return;
    uint16_t type = *static_cast<uint16_t*>(msg);
    int32_t* m = static_cast<int32_t*>(msg);
    if (type & 0x4000) {
        m[2] = (int32_t)now_ms();
        Net_send(net, msg, size, 0, 1);
        return;
    }
    if (type & 0x8000) {
        if (NetSessionState[4] != 1) to = fld(net, 0x764);
        Net_send(net, msg, size, to, 1);
        return;
    }
    if (type & 0xA000) {
        if ((GameMoreRules & 0x10) && !ControlTurnC && CurrentFaction > 0 && NetSessionState[4] > 1) {
            Net_send(net, msg, size, AlphaNet_who_2_pid(net, CurrentFaction), 1);
            return;
        }
        if (NetSessionState[4] != 1) to = fld(net, 0x764);
        Net_send(net, msg, size, to, 1);
        return;
    }
    if (NetSessionState[4] != 1) to = AlphaNet_who_2_pid(net, to);
    Net_send(net, msg, size, to, 1);
}

int32_t THISCALL NetDaemon_receive(uint8_t* net) {
    int32_t from = 0, size = 0;
    uint8_t* m = Net_get(net, &from, &size);
    if (!m) return 0;
    NetDaemon_process_message(net, m, size, from);
    return 1;
}

void GAMEAPI NetDaemon_net_tasks() {
    wait_task();
    int32_t from = 0, size = 0;
    if (uint8_t* m = Net_get(&NetState, &from, &size)) NetDaemon_process_message(&NetState, m, size, from);
    Lock_check_global(Lock());
    check_spock();
}

int32_t THISCALL NetDaemon_wait(uint8_t* net, const char* label, int32_t* flag, int32_t value, uint32_t popup_ms) {
    await(net, label, popup_ms, [&] { return *flag == value; });
    return 0;
}

// ---- the message senders (map module) ----

namespace {
void header(int32_t* m, int32_t type) {
    m[0] = type & 0xFFFF;
    m[1] = CurrentPlayerFaction;
    m[2] = (int32_t)now_ms();
    m[3] = NetSessionState[-1];  // 0x93E8BC: the sequence number
}
void send(int32_t* m, int32_t size, int32_t to) {
    NetBusy()++;
    NetDaemon_send_message(&NetState, m, size, to);
    NetBusy()--;
}
}  // namespace

void GAMEAPI message_veh(int32_t type, int32_t veh_id, int32_t a3, int32_t to) {
    int32_t m[6];
    header(m, type);
    m[4] = veh_id;
    m[5] = a3;
    send(m, 0x18, to);
}

void GAMEAPI message_base(int32_t type, int32_t veh_id, const char* name, int32_t to) {
    int32_t m[0x40 / 4] = {};
    header(m, type);
    m[4] = veh_id;
    crt_strncpy(reinterpret_cast<char*>(&m[5]), name, 0x18);
    reinterpret_cast<char*>(m)[0x2F] = 0;
    send(m, 0x30, to);
}

void GAMEAPI message_data(int32_t type, int32_t to, int32_t a3, int32_t a4, int32_t a5, int32_t a6) {
    int32_t m[8];
    header(m, type);
    m[4] = a3;
    m[5] = a4;
    m[6] = a5;
    m[7] = a6;
    send(m, 0x20, to);
}

void GAMEAPI message_big_data(int32_t type, int32_t to, const int32_t* data, int32_t count) {
    int32_t m[0x90 / 4];
    header(m, type);
    int32_t n = 0;
    for (; n < count && n < 0x20; n++) m[4 + n] = data[n];
    for (; n < 0x20; n++) m[4 + n] = 0;
    send(m, 0x90, to);
}

void GAMEAPI message_lock(int32_t type, int32_t a2, int32_t a3, int32_t a4, int32_t a5, int32_t a6, int32_t a7,
                          int32_t to) {
    int32_t m[10];
    header(m, type);
    m[4] = a2;
    m[5] = a3;
    m[6] = a4;
    m[7] = a6;
    m[8] = a7;
    m[9] = a5;
    send(m, 0x28, to);
}

void GAMEAPI message_landmark(int32_t type, int32_t x, int32_t y, const char* name, int32_t to) {
    int32_t m[0x40 / 4] = {};
    header(m, type);
    m[4] = x;
    m[5] = y;
    if (name) crt_strncpy(reinterpret_cast<char*>(&m[8]), name, 0x1F);
    send(m, 0x40, to);
}

void GAMEAPI message_chat(int32_t type, int32_t to, const char* text) {
    int32_t m[0x110 / 4];
    header(m, type);
    crt_strncpy(reinterpret_cast<char*>(&m[4]), text, 0x100);
    reinterpret_cast<char*>(m)[0x10F] = 0;
    send(m, 0x110, to);
}

void GAMEAPI synch_veh(int32_t veh_id) { NetDaemon_synch(&NetState, 0x11, veh_id, 0, 0, nullptr, 1, 0x2101); }
void GAMEAPI synch_base(int32_t base_id) { NetDaemon_synch(&NetState, 0x13, base_id, 0, 0, nullptr, 1, 0x2101); }
void GAMEAPI synch_energy(int32_t f) { NetDaemon_synch(&NetState, 0xB, f, 0, 0, nullptr, 1, 0x2101); }
void GAMEAPI synch_researching(int32_t f) { NetDaemon_synch(&NetState, 10, f, 0, 0, nullptr, 1, 0x2101); }
void GAMEAPI synch_leader(int32_t f) { NetDaemon_synch(&NetState, 6, f, 0, 0, nullptr, 1, 0x2101); }
void GAMEAPI synch_template(int32_t f) { NetDaemon_synch(&NetState, 0x18, f, 0, 0, nullptr, 1, 0x2101); }
void GAMEAPI synch_radius(int32_t base_id) { NetDaemon_synch(&NetState, 0x23, base_id, 0, 0, nullptr, 1, 0x2101); }
void GAMEAPI synch_diplo(int32_t f, int32_t f_with) {
    NetDaemon_synch(&NetState, 0x16, f, f_with, 0, nullptr, 1, 0x2101);
}

// ---- spock: the host resends the units and bases when a client's state was found changed ----

void GAMEAPI help_spock() {
    if (NetSessionState[0]) {
        NetDaemon_synch(&NetState, 0x25, 0, 0, 0, nullptr, 1, 0x2101);
        drain(&NetState);
        NetDaemon_synch(&NetState, 0x12, 0, 0, 0, nullptr, 1, 0x2101);
        drain(&NetState);
        NetDaemon_synch(&NetState, 0x26, 0, 0, 0, nullptr, 1, 0x2101);
        drain(&NetState);
        NetDaemon_synch(&NetState, 0x14, 0, 0, 0, nullptr, 1, 0x2101);
        drain(&NetState);
    }
    SpockWanted() = 0;
    SpockBusy() = 0;
}

void GAMEAPI check_spock() {
    if (NetSessionState[0] && SpockWanted() && !ControlUpkeepA && !ControlTurnC) {
        SpockBusy() = 1;
        if (!Lock_any_locks(Lock())) help_spock();
    }
}

void GAMEAPI wait_2() {
    if (MultiplayerActive) NetDaemon_net_tasks();
    else wait_task();
}

// ---- locks ----

int32_t THISCALL NetDaemon_lock(uint8_t* net, uint32_t flags1, int32_t x1, int32_t y1, uint32_t flags2, int32_t x2,
                                int32_t y2) {
    if (!MultiplayerActive) return 0;
    NetDropped() = 0;
    if (fld(net, 0x1BAC)) {
        if (fld(net, 0x1BC4)) return 0;  // (the original returns its last value)
        NetMsg_pop(&NetMsg, "MOVEPENDING", 0, 2, nullptr);
        return 1;
    }
    fld(net, 0x1BAC) = 1;
    fld(net, 0x1B70) = 0;
    fld(net, 0x1588) = 0;
    log_say_values("Client requesting lock", CurrentPlayerFaction, x1, y1);
    message_lock(0x2210, (int32_t)flags1, x1, y1, (int32_t)flags2, x2, y2, 0);
    await(net, "AWAITLOCK", 0x640, [&] { return fld(net, 0x1B70) == 0; });
    if (fld(net, 0x1B70) == 1) {
        fld(net, 0x1BC8) = 0;
        if (flags1 & 2) await(net, "AWAITGLOBALLOCK", 800, [&] { return fld(net, 0x1588) == 0; });
        if (!NetDropped()) return 0;
        fld(net, 0x1B70) = 2;
    }
    if (!fld(net, 0x1BC4) && (++fld(net, 0x1BC8) > 0 || fld(net, 0x1B70) > 2)) {
        NetMsg_pop(&NetMsg, fld(net, 0x1B70) == 2 ? "NOLOCK" : "NOLOCKGLOBAL", 5000, 2, nullptr);
        fld(net, 0x1BC8) = 0;
    }
    fld(net, 0x1BAC) = 0;
    return 1;
}

int32_t THISCALL NetDaemon_add_lock(uint8_t* net, int32_t flags, int32_t x, int32_t y) {
    if (!MultiplayerActive) return 0;
    if (!fld(net, 0x1BAC)) return 1;
    log_say_values("Client requesting ADD LOCK", CurrentPlayerFaction, 0, 0);
    fld(net, 0x1B70) = 0;
    message_lock(0x2215, flags, x, y, 0, -1, -1, 0);
    await(net, "AWAITLOCK", 0x640, [&] { return fld(net, 0x1B70) == 0; });
    if (fld(net, 0x1B70) != 1) NetMsg_pop(&NetMsg, "NOLOCK", 5000, 2, nullptr);
    return fld(net, 0x1B70) != 1;
}

int32_t THISCALL NetDaemon_add_global(uint8_t* net) {
    if (!MultiplayerActive) return 0;
    NetDropped() = 0;
    fld(net, 0x1B70) = 0;
    fld(net, 0x1588) = 0;
    message_data(0x2211, 0, 0, 0, 0, 0);
    await(net, "AWAITGLOBALLOCK", 0x640, [&] { return fld(net, 0x1B70) == 0; });
    if (fld(net, 0x1B70) == 1) {
        await(net, "AWAITGLOBALLOCK", 800, [&] { return fld(net, 0x1588) == 0; });
        if (!NetDropped()) {
            fld(net, 0x1BC8) = 0;
            return 0;
        }
        fld(net, 0x1B70) = 2;
    }
    if (!fld(net, 0x1BC4) && (++fld(net, 0x1BC8) > 0 || fld(net, 0x1B70) > 2)) {
        NetMsg_pop(&NetMsg, fld(net, 0x1B70) == 2 ? "NOLOCK" : "NOLOCKGLOBAL", 5000, 2, nullptr);
        fld(net, 0x1BC8) = 0;
    }
    fld(net, 0x1BAC) = 0;
    return 1;
}

void THISCALL NetDaemon_unlock(uint8_t* net) { release_lock(net); }

int32_t THISCALL NetDaemon_lock_base(uint8_t* net, int32_t base_id, int32_t flags, int32_t a3, int32_t a4) {
    log_say_values("LockBase", ControlUpkeepA, ControlTurnC, CurrentBaseID);
    if (!MultiplayerActive) return 0;
    if (ControlUpkeepA) {
        Bases[base_id].state_flags |= 0x10000000;
        message_data(0x4304, 0, 0, 0, 0, 0);
        return 0;
    }
    if (ControlTurnC || fld(net, 0x1B38)) return 1;
    log_say_values("LockBase Needs Lock", base_id, Bases[base_id].x, Bases[base_id].y);
    return NetDaemon_lock(net, (uint32_t)flags | 4, Bases[base_id].x, Bases[base_id].y, 0, a3, a4);
}

void THISCALL NetDaemon_unlock_base(uint8_t* net, int32_t base_id) {
    log_say_values("UnlockBase", NetSessionState[0x1F], base_id, 0);
    if (!MultiplayerActive || !NetSessionState[0x1F]) return;
    log_say_values("UnlockBase doing synch & unlock", 0, 0, 0);
    synch_base(base_id);
    if (!MultiplayerActive) return;
    fld(net, 0x1BB8) = 1;
    log_say_values("Awaiting acknowledgement of SYNCH", CurrentPlayerFaction, 0, 0);
    await(net, "AWAITEXEC", 2000, [&] { return fld(net, 0x1BB8) == 1; });
    if (!fld(net, 0x1BB8)) log_say_values("Received SYNCH acknowledgement", CurrentPlayerFaction, 0, 0);
    fld(net, 0x1BB8) = 0;
    release_lock(net);
}

int32_t THISCALL NetDaemon_lock_veh(uint8_t* net, int32_t* veh_id, int32_t flags1, int32_t x2, int32_t y2,
                                    int32_t flags2) {
    if (!MultiplayerActive) return 0;
    if ((NetTurnDoneMask & (1 << (CurrentPlayerFaction & 31))) && !(GameMoreRules & 0x10)) {
        NetMsg_pop(&NetMsg, "ALREADYDONE", 0, 2, nullptr);
        return 1;
    }
    fld(net, 0x1B78) = (int32_t)(intptr_t)veh_id;  // (a pointer in the original; only tested)
    if (!NetDaemon_lock(net, (uint32_t)flags1, Vehs[*veh_id].x, Vehs[*veh_id].y, (uint32_t)flags2, x2, y2) &&
        *veh_id >= 0) {
        fld(net, 0x1BB0) = 1;
        fld(net, 0x1BC0) = 0;
        return 0;
    }
    fld(net, 0x1B78) = 0;
    return 1;
}

void THISCALL NetDaemon_unlock_veh(uint8_t* net) { release_veh(net); }

int32_t THISCALL NetDaemon_automated_lock(uint8_t* net, int32_t* veh_id, int32_t flags) {
    if (!MultiplayerActive) return 0;
    log_say_values("Getting auto lock", *veh_id, Vehs[*veh_id].order, 0);
    NetBusy()++;
    uint32_t start = now_ms();
    fld(net, 0x1BB4) = 1;
    bool shown = false;
    int32_t result = 0;
    while (!ControlTurnA) {
        result = NetDaemon_lock_veh(net, veh_id, flags, -1, -1, 0);
        if (!result) break;
        if (!shown && now_ms() - start > 500) {
            shown = true;
            NetMsg_pop(&NetMsg, "AWAITORDERS", 0, 2, nullptr);
        } else {
            log_say_values(" Auto lock delay ", *veh_id, 0, 0);
            while (now_ms() - start < 0xFA && !ControlTurnA) {
                wait_task();
                int32_t from = 0, size = 0;
                if (uint8_t* m = Net_get(net, &from, &size)) NetDaemon_process_message(net, m, size, from);
                Lock_check_global(Lock());
                check_spock();
            }
            start = now_ms();
        }
        check_spock();
    }
    if (shown) NetMsg_close(&NetMsg);
    log_say_values("Got auto lock", *veh_id, Vehs[*veh_id].order, 0);
    fld(net, 0x1BB4) = 0;
    NetBusy()--;
    return result;
}

void THISCALL NetDaemon_await_exec(uint8_t* net, int32_t release) {
    if (!MultiplayerActive) return;
    log_say_values("Client waiting for acknowledgement", CurrentPlayerFaction, 0, 0);
    await(net, "AWAITEXEC", 2000, [&] { return fld(net, 0x1BB0) == 1; });
    log_say_values("Client received acknowledgement", CurrentPlayerFaction, 0, 0);
    if (release) release_veh(net);
    fld(net, 0x1BB0) = 0;
}

void THISCALL NetDaemon_await_synch(uint8_t* net) {
    if (!MultiplayerActive) return;
    fld(net, 0x1BB8) = 1;
    log_say_values("Awaiting acknowledgement of SYNCH", CurrentPlayerFaction, 0, 0);
    await(net, "AWAITEXEC", 2000, [&] { return fld(net, 0x1BB8) == 1; });
    if (!fld(net, 0x1BB8)) log_say_values("Received SYNCH acknowledgement", CurrentPlayerFaction, 0, 0);
    fld(net, 0x1BB8) = 0;
}

void THISCALL NetDaemon_await_diplo(uint8_t* net, int32_t type) {
    if (!MultiplayerActive) return;
    fld(net, 0x1BA8) = type;
    log_say_values("Awaiting DIPLO event", CurrentPlayerFaction, 0, 0);
    await(net, "AWAITDIPLO", 2000, [&] { return fld(net, 0x1BA8) == type; });
    if (!fld(net, 0x1BA8)) log_say_values("Received DIPLO acknowledgement", CurrentPlayerFaction, 0, 0);
    fld(net, 0x1BA8) = 0;
}

int32_t THISCALL NetDaemon_maybe_lock(uint8_t* net, int32_t* veh_id, int32_t a2, int32_t a3, int32_t a4, int32_t a5,
                                      int32_t a6) {
    if (a2) return 0;
    if (fld(net, 0x1BAC)) {
        if (fld(net, 0x1BC4)) return 0;
        if (++fld(net, 0x1BCC) >= 2) NetMsg_pop(&NetMsg, "MOVEPENDING", 0, 2, nullptr);
        return 1;
    }
    fld(net, 0x1BCC) = 0;
    return NetDaemon_lock_veh(net, veh_id, a3 | 4, a4, a5, a6);
}

// ---- remote execution: moves and actions ----

int32_t THISCALL NetDaemon_order_veh(uint8_t* net, int32_t veh_id, int32_t dir, int32_t locked) {
    if (!MultiplayerActive) return order_veh(veh_id, dir, 3);
    if (!locked) {
        if (fld(net, 0x1BAC)) {
            if (fld(net, 0x1BC4)) return 0;
            if (++fld(net, 0x1BCC) >= 2) NetMsg_pop(&NetMsg, "MOVEPENDING", 0, 2, nullptr);
            return 0;
        }
        fld(net, 0x1BCC) = 0;
        if (NetDaemon_lock_veh(net, &veh_id, 4, -1, -1, 0)) return 0;
    }
    int32_t f = Vehs[veh_id].faction_id;
    int32_t x = Vehs[veh_id].x + NearOffsetX[dir];
    if (!(MapToggleFlat & 1)) {
        if (x < 0) x += MapAreaX;
        else if (x >= MapAreaX) x -= MapAreaX;
    }
    int32_t y = Vehs[veh_id].y + NearOffsetY[dir];
    int32_t base_id = base_at(x, y);
    bool global = false;  // the move needs the global lock (an attack on a base, a planet buster)
    Vehs[veh_id].probe_action = 0;
    if (base_id >= 0) {
        int32_t owner = Bases[base_id].faction_id;
        MAP* sq = map_tile(x, y);
        if (owner != f && !(Factions[f].diplo_status[owner] & 1) &&
            (!(sq->items & 2) || (sq->val2 & 0xF) > 7 || Bases[base_id].pop_size < 2))
            global = true;
    }
    if (planet_buster(veh_id)) {
        int32_t at = stack_fix(veh_at(x, y));
        if (at >= 0 && Vehs[at].faction_id != f && !(Factions[f].diplo_status[Vehs[at].faction_id] & 1))
            global = true;
    }
    auto abort = [&]() {
        if (fld(net, 0x1BC4)) synch_veh_ack(net, veh_id);
        release_veh(net);
    };
    if (global && NetDaemon_add_global(net)) {
        abort();
        return 0;
    }
    if (!order_veh(veh_id, dir, 1)) {
        release_veh(net);
        return 0;
    }
    int32_t left = speed(veh_id, 0) - Vehs[veh_id].moves_spent;
    if (left < 0 || (left <= 999 && left == 0)) {
        release_veh(net);
        return 0;
    }
    bool send_now = false;
    if (Units[Vehs[veh_id].unit_id].plan == 0xB) {  // a probe team
        if (base_id < 0) {
            send_now = true;
        } else if (f != Bases[base_id].faction_id && !stack_check(veh_at(x, y), 2, 0xB, -1, -1) && !global) {
            if (NetDaemon_add_global(net)) {
                abort();
                NetMsg_pop(&NetMsg, "GLOBALFAILED", 5000, 0, nullptr);
                return 0;
            }
            send_now = true;
        }
    }
    if (!send_now && !(base_id < 0 || Bases[base_id].pop_size > 1 || !(Vehs[veh_id].state & 0x80000) || global ||
                       !NetDaemon_add_global(net))) {
        abort();
        return 0;
    }
    log_say_values("Sending MOVE_VEH message", veh_id, dir, 0);
    message_veh(0x2401, veh_id, dir, 0);
    if (!locked && MultiplayerActive) {
        log_say_values("Client waiting for acknowledgement", CurrentPlayerFaction, 0, 0);
        NetTurnTimeLeft += NetDaemon_wait(net, "AWAITEXEC", &fld(net, 0x1BB0), 1, 2000);
        log_say_values("Client received acknowledgement", CurrentPlayerFaction, 0, 0);
        release_veh(net);
        fld(net, 0x1BB0) = 0;
    }
    // Enemy units able to move next to the destination: a short pause for their reactions.
    bool pause = false;
    for (int32_t i = 0; i < 0x15; i++) {
        int32_t tx = TableOffsetX[i] + x;
        if (!(MapToggleFlat & 1)) {
            if (tx < 0) tx += MapAreaX;
            else if (tx >= MapAreaX) tx -= MapAreaX;
        }
        int32_t ty = TableOffsetY[i] + y;
        if (ty < 0 || ty >= MapAreaY || tx < 0 || tx >= MapAreaX) continue;
        MAP* sq = map_tile(tx, ty);
        if (!(sq->items & 3)) continue;
        int32_t owner = sq->val2 & 0xF;
        if (owner >= 8 || owner == CurrentPlayerFaction) continue;
        if (!(Factions[owner].diplo_status[CurrentPlayerFaction] & 0x10)) continue;
        if (!(FactionStatus[0] & (1 << owner))) continue;
        for (int32_t v = veh_at(tx, ty); v >= 0; v = Vehs[v].next_veh_id_stack) {
            if (Vehs[v].faction_id != owner) continue;
            int32_t sp = speed(v, 0);
            int32_t left = sp - Vehs[v].moves_spent;
            if (left >= 0 && (left > 999 || sp != Vehs[v].moves_spent)) pause = true;
        }
    }
    if (pause) {
        uint32_t start = now_ms();
        do {
            do_all_non_input();
            int32_t from = 0, size = 0;
            if (uint8_t* m = Net_get(net, &from, &size)) NetDaemon_process_message(net, m, size, from);
            Lock_check_global(Lock());
        } while (!ControlTurnA && now_ms() - start < 500);
    }
    return 1;
}

int32_t THISCALL NetDaemon_action(uint8_t* net, int32_t veh_id, int32_t locked) {
    if (!MultiplayerActive) return action(veh_id);
    int32_t order = Vehs[veh_id].order;
    int32_t kind, lock_flags;
    if (order >= 4 && order <= 0x17) {
        kind = 0;
        lock_flags = (order == 0x14 || order == 0x15) ? 0xC : 0;
    } else if (order == 0x18 || order == 0x58) {
        kind = 1;
        lock_flags = 4;
    } else if (order == 0x1B || order == 0x1C) {
        kind = 2;
        lock_flags = 4;
    } else {
        if (order) log_say_values("Falling out of action loop", veh_id, order, 0);
        if (locked) release_veh(net);
        return 0;
    }
    fld(net, 0x1BC4) = 1;
    if (!locked && NetDaemon_automated_lock(net, &veh_id, lock_flags)) return 0;
    if (kind == 0) {
        log_say_values("Net Action: Sending ACTION message", veh_id, Vehs[veh_id].order, 0);
        message_veh(0x2404, veh_id, 0, 0);
        log_say_values("Net Action: Awaiting EXEC", 0, 0, 0);
        if (MultiplayerActive) {
            log_say_values("Client waiting for acknowledgement", CurrentPlayerFaction, 0, 0);
            await(net, "AWAITEXEC", 2000, [&] { return fld(net, 0x1BB0) == 1; });
            log_say_values("Client received acknowledgement", CurrentPlayerFaction, 0, 0);
            release_veh(net);
            fld(net, 0x1BB0) = 0;
        }
        log_say_values("Net Action: Finished awaiting", 0, 0, 0);
    } else if (kind == 1) {
        log_say_values("Net Action: Go to", veh_id, 0, 0);
        action_go_to(veh_id);
    } else {
        log_say_values("Net Action: Road", 0, 0, 0);
        action_road_to(veh_id);
    }
    log_say_values("Falling out of Net Action", 0, 0, 0);
    return 1;
}

void THISCALL NetDaemon_end_of_event(uint8_t* net, int32_t faction_id, int32_t seq) {
    if (!fld(net, 0x1B30)) fld(net, 0x1B2C) = seq;
    if (faction_id == CurrentPlayerFaction) {
        if (fld(net, 0x1BC0)) {
            log_say_values("Remaining Locked2", 0, 0, 0);
            fld(net, 0x1BC0) = 0;
            message_data(0x2214, 0, 0, 0, 0, 0);
            return;
        }
        fld(net, 0x1BB0) = 0;
    }
}

// ---- the game sent to the players ----

void THISCALL NetDaemon_send_game(uint8_t* net) {
    static const int32_t kFirst[] = {0, 2, 4, 5};
    for (int32_t t : kFirst) {
        NetDaemon_synch(net, t, 0, 0, 0, nullptr, 1, 0x2101);
        drain(net);
    }
    for (int32_t f = 0, units = 0; units < 0x200; f++, units += 0x40) {
        NetDaemon_synch(net, 6, f, 0, 0, nullptr, 1, 0x2101);
        drain(net);
        NetDaemon_synch(net, 7, f, 0, 0, nullptr, 1, 0x2101);
        drain(net);
        NetDaemon_synch(net, 0xF, units, 0x40, 0, nullptr, 1, 0x2101);
        drain(net);
    }
    static const int32_t kNext[] = {0x29, 0x12, 0x14, 0x19};
    for (int32_t t : kNext) {
        NetDaemon_synch(net, t, 0, 0, 0, nullptr, 1, 0x2101);
        drain(net);
    }
    if (CurrentTurn) {
        NetDaemon_synch(net, 0x20, 0, 0, 0, nullptr, 1, 0x2101);
        drain(net);
    }
    NetDaemon_synch(net, 0x21, 0, 0, 0, nullptr, 1, 0x2101);
    drain(net);
    if (ReplayEventSize) {
        NetDaemon_synch(net, 0x22, 0, 0, 0, nullptr, 1, 0x2101);
        drain(net);
    }
    NetDaemon_synch(net, 0x24, 0, 0, 0, nullptr, 1, 0x2101);
    drain(net);
    NetDaemon_synch(net, 0x15, 0, 0, 0, nullptr, 1, 0x2101);
    drain(net);
}

void THISCALL NetDaemon_send_rules(uint8_t* net) {
    for (int32_t t = 0x80; t <= 0x8D; t++) {
        NetDaemon_synch(net, t, 0, 0, 0, nullptr, 1, 0x2101);
        drain(net);
    }
}

// ---- the deletion list: unit ids renumbered by deletions a message's sender did not know of ----
// DeleteList (0x93E31C, NetState +0x158C): 24 entries of 0x3C bytes: +0 the factions that
// acknowledged it (an entry is free once every human did), +4 the event's sequence number, +8 the
// number of deleted units, +0xA their ids (int16); +0x5A0 the current sequence number (0x93E8BC).

namespace {
bool entry_free(const uint8_t* e) { return (e[0] & FactionStatus[0]) == FactionStatus[0]; }
int32_t& seq_of(uint8_t* e) { return *reinterpret_cast<int32_t*>(e + 4); }
int16_t& count_of(uint8_t* e) { return *reinterpret_cast<int16_t*>(e + 8); }
int16_t* ids_of(uint8_t* e) { return reinterpret_cast<int16_t*>(e + 0xA); }
}  // namespace

void THISCALL DeletionList_ctor(uint8_t* list) {
    *reinterpret_cast<int32_t*>(list + 0x5A0) = 0;
    for (int32_t i = 0; i < 0x18; i++) {
        list[i * 0x3C] = 0xFF;
        count_of(list + i * 0x3C) = 0;
    }
}

void THISCALL DeletionList_clear(uint8_t* list, int32_t i) {
    list[i * 0x3C] = 0xFF;
    count_of(list + i * 0x3C) = 0;
}

int32_t THISCALL DeletionList_UNK1(uint8_t* list) {
    for (int32_t i = 0; i < 0x18; i++) {
        if (entry_free(list + i * 0x3C)) return i;
    }
    return 0;
}

int32_t THISCALL DeletionList_UNK2(uint8_t* list, int32_t seq) {
    for (int32_t i = 0; i < 0x18; i++) {
        uint8_t* e = list + i * 0x3C;
        if (!entry_free(e) && seq_of(e) == seq) return i;
    }
    return -1;
}

void THISCALL DeletionList_ack(uint8_t* list, int32_t seq, int32_t faction_id) {
    int32_t i = DeletionList_UNK2(list, seq);
    if (i < 0) return;
    uint8_t* e = list + i * 0x3C;
    e[0] |= (uint8_t)(1 << (faction_id & 7));
    if ((e[0] & FactionStatus[0]) == FactionStatus[0]) e[0] = 0xFF;
}

int32_t THISCALL DeletionList_convert(uint8_t* list, int32_t veh_id, int32_t seq) {
    int32_t original = veh_id;
    int32_t cur = *reinterpret_cast<int32_t*>(list + 0x5A0);
    for (int32_t s = seq + 1; s <= cur; s++) {
        int32_t i = DeletionList_UNK2(list, s);
        if (i < 0) continue;
        uint8_t* e = list + i * 0x3C;
        for (int32_t k = 0; k < count_of(e); k++) {
            int32_t id = ids_of(e)[k];
            if (id == veh_id) veh_id = -1;
            else if (id < veh_id) veh_id--;
        }
    }
    if (original != veh_id) log_say_values("DelConverted index", original, veh_id, 0);
    return veh_id;
}

int32_t THISCALL DeletionList_add_deletion(uint8_t* list, int32_t veh_id) {
    log_say_values("Deleting vehicle", veh_id, Vehs[veh_id].x, Vehs[veh_id].y);
    int32_t cur = *reinterpret_cast<int32_t*>(list + 0x5A0);
    int32_t i = DeletionList_UNK2(list, cur);
    if (i < 0) {
        i = DeletionList_UNK1(list);
        uint8_t* e = list + i * 0x3C;
        e[0] = 0xFF;
        count_of(e) = 0;
        seq_of(e) = cur;
        e[0] = 0;
    }
    uint8_t* e = list + i * 0x3C;
    ids_of(e)[count_of(e)] = (int16_t)veh_id;
    return ++count_of(e) - 1;
}

int32_t THISCALL NetDaemon_fixup_message(uint8_t* net, uint8_t* msg) {
    uint16_t type = *reinterpret_cast<uint16_t*>(msg);
    int32_t seq = *reinterpret_cast<int32_t*>(msg + 0xC);
    if (type == 0x2101) {
        if (*reinterpret_cast<uint16_t*>(msg + 0x10) != 0x11) return 0;
        int32_t& v = *reinterpret_cast<int32_t*>(msg + 0x14);
        v = DeletionList_convert(&DeleteList, v, seq);
        if (msg[0x35] != 1) return 0;
        int16_t& next = *reinterpret_cast<int16_t*>(msg + 0x38);
        if (next < 0) return 0;
        next = (int16_t)DeletionList_convert(&DeleteList, next, seq);
        if (next == -1) msg[0x35] = 0;
        return 0;
    }
    if ((type >= 0x2401 && type <= 0x2406) || type == 0x2408 || type == 0x2413) {
        int32_t& v = *reinterpret_cast<int32_t*>(msg + 0x10);
        v = DeletionList_convert(&DeleteList, v, seq);
        return v < 0;
    }
    return 0;
}

// ---- NetDaemon_synch (0x532E00): a block of the game state, sent (send != 0) in chunks of 0x800
// bytes or applied from a received chunk (msg) ----

namespace {
// The rules tables (send_rules, synch 0x80..0x8D) are native structures in smacx (their names are
// pointers): they travel in the original's 32-bit layout, pointers as zero, and the receiver copies
// the fields the original copies from its staging buffer (0x93AD8C).
struct RuleField {
    uint32_t orig;    // offset in the original's element
    uint32_t native;  // offset in smacx's element
    uint32_t size;
};
struct RuleTable {
    int32_t type;
    uint8_t* base;
    uint32_t native_size;
    uint32_t orig_size;
    int32_t count;
    std::vector<RuleField> fields;  // the fields copied on receipt (and sent)
};
#define RF(T, field, orig) RuleField{orig, (uint32_t)offsetof(T, field), (uint32_t)sizeof(T::field)}
const std::vector<RuleTable>& rule_tables() {
    static std::vector<RuleTable> t;
    if (!t.empty()) return t;
    t.push_back({0x80, reinterpret_cast<uint8_t*>(Tech), sizeof(CTech), 0x2C, 89,
                 {RF(CTech, flags, 0), RF(CTech, AI_growth, 0x14), RF(CTech, AI_tech, 0x18), RF(CTech, AI_wealth, 0x1C),
                  RF(CTech, AI_power, 0x20), RF(CTech, preq_tech1, 0x24), RF(CTech, preq_tech2, 0x28)}});
    t.push_back({0x83, reinterpret_cast<uint8_t*>(Terraform), sizeof(CTerraform), 0x20, 20,
                 {RF(CTerraform, preq_tech, 8), RF(CTerraform, preq_tech_sea, 0xC), RF(CTerraform, bit, 0x10),
                  RF(CTerraform, bit_incompatible, 0x14), RF(CTerraform, rate, 0x18)}});
    t.push_back({0x85, reinterpret_cast<uint8_t*>(TimeControl), sizeof(CTimeControl), 0x20, 6,
                 {RF(CTimeControl, turn, 4), RF(CTimeControl, base, 8), RF(CTimeControl, unit, 0xC),
                  RF(CTimeControl, event, 0x10), RF(CTimeControl, extra, 0x14), RF(CTimeControl, refresh, 0x18),
                  RF(CTimeControl, accumulated, 0x1C)}});
    t.push_back({0x86, reinterpret_cast<uint8_t*>(Chassis), sizeof(CChassis), 0x90, 9,
                 {RuleField{0x48, (uint32_t)offsetof(CChassis, speed), 6},
                  RF(CChassis, preq_tech, 0x8E)}});
    t.push_back({0x87, reinterpret_cast<uint8_t*>(Reactor), sizeof(CReactor), 0xC, 4, {RF(CReactor, preq_tech, 8)}});
    t.push_back({0x88, reinterpret_cast<uint8_t*>(Weapon), sizeof(CWeapon), 0x10, 26,
                 {RuleField{8, (uint32_t)offsetof(CWeapon, offense_value), 4}, RF(CWeapon, preq_tech, 0xC)}});
    t.push_back({0x89, reinterpret_cast<uint8_t*>(Armor), sizeof(CArmor), 0x10, 14,
                 {RuleField{8, (uint32_t)offsetof(CArmor, defense_value), 3}, RF(CArmor, preq_tech, 0xC)}});
    t.push_back({0x8A, reinterpret_cast<uint8_t*>(Ability), sizeof(CAbility), 0x1C, 29,
                 {RF(CAbility, cost, 0xC), RF(CAbility, unk_1, 0x10), RF(CAbility, flags, 0x14),
                  RF(CAbility, preq_tech, 0x18)}});
    t.push_back({0x8B, reinterpret_cast<uint8_t*>(Facility), sizeof(CFacility), 0x30, 134,
                 {RuleField{8, (uint32_t)offsetof(CFacility, pad), 0x28}}});
    t.push_back({0x8C, reinterpret_cast<uint8_t*>(Citizen), sizeof(CCitizen), 0x1C, 10,
                 {RF(CCitizen, preq_tech, 8), RF(CCitizen, obsol_tech, 0xC), RF(CCitizen, econ_bonus, 0x10),
                  RF(CCitizen, psych_bonus, 0x14), RF(CCitizen, labs_bonus, 0x18)}});
    t.push_back({0x8D, reinterpret_cast<uint8_t*>(SocialField), sizeof(CSocialField), 0xD4, 4,
                 {RF(CSocialField, soc_preq_tech, 4), RF(CSocialField, soc_effect, 0x24)}});
    return t;
}
#undef RF
const RuleTable* rule_table(int32_t type) {
    for (const RuleTable& r : rule_tables()) {
        if (r.type == type) return &r;
    }
    return nullptr;
}
std::vector<uint8_t> pack_rules(const RuleTable& r) {
    std::vector<uint8_t> out((size_t)r.orig_size * r.count, 0);
    for (int32_t i = 0; i < r.count; i++) {
        for (const RuleField& f : r.fields)
            std::memcpy(&out[(size_t)i * r.orig_size + f.orig], r.base + (size_t)i * r.native_size + f.native, f.size);
    }
    return out;
}
void unpack_rules(const RuleTable& r, const uint8_t* staging) {
    for (int32_t i = 0; i < r.count; i++) {
        for (const RuleField& f : r.fields)
            std::memcpy(r.base + (size_t)i * r.native_size + f.native, staging + (size_t)i * r.orig_size + f.orig, f.size);
    }
}

uint8_t* map_bytes() { return reinterpret_cast<uint8_t*>(game().map.tiles); }
}  // namespace

void GAMEAPI synch_display(int32_t type, int32_t chunk, int32_t chunks, int32_t a4) {
    const char* label = nullptr;
    switch (type) {
    case 0: label = "SYNCHINGGAME2"; break;
    case 4: label = "SYNCHINGWORLD2"; break;
    case 6: label = "SYNCHINGPLAYERS2"; break;
    case 0x12: label = "SYNCHINGVEHICLES2"; break;
    case 0x14: label = "SYNCHINGBASES2"; break;
    case 0x30: label = "SYNCHINGWORLD3"; break;
    default: return;
    }
    parse_num(0, chunk);
    parse_num(1, chunks);
    if (type == 6 || type == 0x30) parse_num(2, a4);
    NetMsg_pop(&NetMsg, label, 0, 1, nullptr);
    do_all_draws();
}

void THISCALL NetDaemon_synch(uint8_t* net, int32_t type, int32_t a2, int32_t a3, int32_t a4, int32_t* msg,
                              int32_t send, int32_t msg_type) {
    if (!MultiplayerActive) return;
    type = (int16_t)type;
    const int32_t id = a2;  // the faction, unit, base or section
    auto progress = [&](uint32_t off, int32_t from, int32_t to, const char* label) {
        if (!send && fld(net, off) == from) {
            if (label) NetMsg_pop(&NetMsg, label, 0, 1, nullptr);
            else NetMsg_close(&NetMsg);
            do_all_draws();
            fld(net, off) = to;
        }
    };
    int32_t size = 8;
    int32_t section_rows = 0;
    switch (type) {
    case 0:
        if (!send && fld(net, 0x1B48) >= 0) {
            NetMsg_pop(&NetMsg, "SYNCHINGGAME", 0, 1, nullptr);
            do_all_draws();
            fld(net, 0x1B48) = 1;
        }
        size = 0x398;
        break;
    case 1:
    case 0x10: size = 1; break;
    case 2:
    case 0x1002: size = 0xA9C; break;
    case 4:
        progress(0x1B48, 1, 2, "SYNCHINGWORLD");
        size = MapAreaTiles * 0x2C;
        break;
    case 5:
        progress(0x1B48, 2, 3, "SYNCHINGREGIONS");
        size = 0xE00;
        break;
    case 6:
        progress(0x1B48, 3, 4, "SYNCHINGPLAYERS");
        size = 0x20CC;
        break;
    case 7: size = 0x59C; break;
    case 8:
    case 0xD:
    case 0x23: size = 0x14; break;
    case 9:
    case 0xC: size = 8; break;
    case 0xE: size = 0x34; break;
    case 0xF: size = a3 * 0x34; break;
    case 0x11:
        log_say_values(send ? "Sent SYNCH VEH" : "Received SYNCH VEH", id, Vehs[id].x, Vehs[id].y);
        size = 0x30;
        break;
    case 0x12:
        progress(0x1B48, 4, 5, "SYNCHINGUNITS");
        if (!VehCount) return;
        size = VehCount * 0x34;
        break;
    case 0x13:
        log_say(send ? "Sending SYNCH BASE" : "Received SYNCH BASE", Bases[id].name, id, 0, 0);
        size = 0x134;
        break;
    case 0x14:
        if (!BaseCount) return;
        size = BaseCount * 0x134;
        break;
    case 0x15:
        if (!send) {
            NetMsg_close(&NetMsg);
            do_all_draws();
            fld(net, 0x1B48) = 8;
        }
        size = 4;
        break;
    case 10:
    case 0xB:
    case 0x17:
    case 0x25:
    case 0x26: size = 4; break;
    case 0x16: size = 0x48; break;
    case 0x18: size = 0x224; break;
    case 0x19:
        progress(0x1B48, 5, 6, "SYNCHINGPRESTIGE");
        size = 0x20;
        break;
    case 0x20: size = CurrentTurn << 4; break;
    case 0x21:
        size = 4;
        progress(0x1B48, 6, 7, "SYNCHINGREPLAY");
        break;
    case 0x22: size = ReplayEventSize; break;
    case 0x24: size = 0x98; break;
    case 0x27: size = 0x104; break;
    case 0x28: size = 0; break;
    case 0x29: size = 0x1000; break;
    case 0x30: {
        section_rows = MapAreaY / 16;
        int32_t rows = id == 0xF ? MapAreaY - section_rows * 15 : section_rows;
        size = (MapAreaX >> 1) * rows * 0x2C;
        break;
    }
    case 0x80:
        if (!send && fld(net, 0x1B4C) >= 0) {
            NetMsg_pop(&NetMsg, "RULESYNCHTECH", 0, 1, nullptr);
            do_all_draws();
            fld(net, 0x1B4C) = 1;
        }
        size = 0xF4C;
        break;
    case 0x81: size = 0x13C; break;
    case 0x82: size = 0x7C; break;
    case 0x83:
        progress(0x1B4C, 1, 2, "RULESYNCHTERR");
        size = 0x280;
        break;
    case 0x84: size = 0x90; break;
    case 0x85: size = 0xC0; break;
    case 0x86:
        progress(0x1B4C, 2, 3, "RULESYNCHVEH");
        size = 0x510;
        break;
    case 0x87: size = 0x30; break;
    case 0x88: size = 0x1A0; break;
    case 0x89: size = 0xE0; break;
    case 0x8A: size = 0x32C; break;
    case 0x8B:
        progress(0x1B4C, 3, 4, "RULESYNCHFAC");
        size = 0x1920;
        break;
    case 0x8C: size = 0x118; break;
    case 0x8D:
        progress(0x1B4C, 4, 5, nullptr);
        size = 0x350;
        break;
    default: size = a2; break;
    }
    // The block's source (sending) or destination (receiving).
    uint8_t* block = nullptr;
    std::vector<uint8_t> packed;
    int32_t diplo[0x12] = {};
    const int32_t g = a3;
    uint8_t* fa = mem::host(0x96C9E0 + (uint32_t)id * 0x20CC);
    switch (type) {
    case 0: block = mem::host(0x9A6490); break;
    case 1: block = mem::host(0x9A64E8); break;
    case 2:
    case 0x1002: block = mem::host(0x949870); break;
    case 4: block = map_bytes(); break;
    case 5: block = mem::host(0x9AA730); break;
    case 6:
    case 0x17: block = fa; break;
    case 7: block = mem::host(0x946A50 + (uint32_t)id * 0x59C); break;
    case 8: block = fa + 0x3DC; break;
    case 9: block = fa + 0x3C8; break;
    case 10: block = fa + 0x3CC; break;
    case 0xB: block = fa + 0x220; break;
    case 0xC: block = fa + 0x324; break;
    case 0xD: block = fa + 0x228; break;
    case 0xE:
    case 0xF: block = mem::host(0x9AB868 + (uint32_t)id * 0x34); break;
    case 0x10: block = mem::host(0x9AB894 + (uint32_t)id * 0x34); break;
    case 0x11: block = reinterpret_cast<uint8_t*>(&Vehs[id]); break;
    case 0x12: block = mem::host(0x952828); break;
    case 0x13: block = reinterpret_cast<uint8_t*>(&Bases[id]); break;
    case 0x14: block = mem::host(0x97D040); break;
    case 0x15: block = mem::host(0x9B2070); break;
    case 0x16: {
        uint8_t* ff = mem::host(0x96C9E0 + (uint32_t)id * 0x20CC);
        uint8_t* fg = mem::host(0x96C9E0 + (uint32_t)g * 0x20CC);
        auto d = [](uint8_t* p, uint32_t off) -> int32_t& { return *reinterpret_cast<int32_t*>(p + off); };
        if (send) {
            diplo[0] = (int8_t)ff[0xB8 + g];
            diplo[1] = (int8_t)fg[0xB8 + id];
            diplo[2] = d(ff, 0x58 + 4 * g);
            diplo[3] = d(fg, 0x58 + 4 * id);
            diplo[4] = d(ff, 0x128);
            diplo[5] = d(fg, 0x128);
            diplo[6] = d(ff, 0x12C + 4 * g);
            diplo[7] = d(fg, 0x12C + 4 * id);
            diplo[8] = d(ff, 0x14C + 4 * g);
            diplo[9] = d(fg, 0x14C + 4 * id);
            diplo[10] = d(ff, 0x78 + 4 * g);
            diplo[11] = d(fg, 0x78 + 4 * id);
            diplo[12] = d(ff, 0x18 + 4 * g);
            diplo[13] = d(fg, 0x18 + 4 * id);
            diplo[14] = d(ff, 0x38 + 4 * g);
            diplo[15] = d(fg, 0x38 + 4 * id);
            diplo[16] = d(ff, 0x98 + 4 * g);
            diplo[17] = d(fg, 0x98 + 4 * id);
        }
        block = reinterpret_cast<uint8_t*>(diplo);
        size = 0x48;
        break;
    }
    case 0x18: block = fa + 0x46C; break;
    case 0x19: block = mem::host(0x945DD8); break;
    case 0x20: block = mem::host(0x9A68AC); break;
    case 0x21: block = mem::host(0x9469F8); break;
    case 0x22: block = mem::host(0x950324); break;
    case 0x23: block = reinterpret_cast<uint8_t*>(&Bases[id]) + 0x78; break;
    case 0x24: block = mem::host(0x94B4C0); break;
    case 0x25: block = mem::host(0x9A64C8); break;
    case 0x26: block = mem::host(0x9A64CC); break;
    case 0x27: block = mem::host(0x93AC88); break;
    case 0x28: block = reinterpret_cast<uint8_t*>(StrBuffer); break;
    case 0x29: block = mem::host(0x94B558); break;
    case 0x30: block = map_bytes() + (size_t)(MapAreaX >> 1) * (section_rows * id) * 0x2C; break;
    case 0x81: block = mem::host(0x949730); break;
    case 0x82: block = mem::host(0x9502A8); break;
    case 0x84: block = mem::host(0x945F50); break;
    default:
        if (const RuleTable* r = rule_table(type)) {
            if (send) {
                packed = pack_rules(*r);
                block = packed.data();
            } else {
                block = mem::host(0x93AD8C);  // the staging buffer
            }
        }
        break;
    }
    int32_t chunks = (size + 0x7FF) / 0x800;
    int32_t rest = size % 0x800;
    for (int32_t chunk = 0; chunk < chunks; chunk++) {
        int32_t index = send ? chunk : *reinterpret_cast<int16_t*>(reinterpret_cast<uint8_t*>(msg) + 0x12);
        int32_t part = (index == chunks - 1 && rest) ? rest : 0x800;
        int32_t offset = index * 0x800;
        if (!send) {
            if (index == chunks - 1) fld(net, 0x1BBC) &= ~(1 << (msg[1] & 31));
            synch_display(type, index + 1, chunks, id);
            const uint8_t* data = reinterpret_cast<uint8_t*>(msg) + 0x24;
            bool copy = true;
            if ((type == 0x11 || type == 0x13) && chunks == 1 && part) {
                const int16_t* pos = reinterpret_cast<const int16_t*>(data);
                const int16_t* mine = reinterpret_cast<const int16_t*>(block + offset);
                if (pos[0] != mine[0] || pos[1] != mine[1]) {
                    copy = false;
                    log_say(type == 0x11 ? "Synch changed vehicle" : "Synch changed base", ParseStrBuffer[0], id,
                            mine[0], mine[1]);
                    bool server_role = ((GameMoreRules & 0x10) && !ControlTurnC && CurrentFaction == CurrentPlayerFaction) ||
                                       ((!(GameMoreRules & 0x10) || ControlTurnC) && NetSessionState[0]);
                    if (server_role) SpockWanted() = 1;
                    else message_data(0x2308, 0, 0, 0, 0, 0);
                }
            }
            if (copy && part && block) std::memcpy(block + offset, data, (size_t)part);
            if (index == chunks - 1) {
                if (type == 0x12) rebuild_vehicle_bits();
                else if (type == 0x14) rebuild_base_bits();
                else if (const RuleTable* r = rule_table(type)) unpack_rules(*r, mem::host(0x93AD8C));
            }
            break;  // one received chunk per call
        }
        std::vector<uint8_t> m((size_t)part + 0x24, 0);
        *reinterpret_cast<uint16_t*>(&m[0]) = (uint16_t)msg_type;
        *reinterpret_cast<uint16_t*>(&m[0x10]) = (uint16_t)type;
        *reinterpret_cast<int32_t*>(&m[4]) = CurrentPlayerFaction;
        *reinterpret_cast<int32_t*>(&m[0xC]) = fld(net, 0x1B2C);
        *reinterpret_cast<int32_t*>(&m[0x14]) = id;
        *reinterpret_cast<int32_t*>(&m[0x18]) = a3;
        *reinterpret_cast<int32_t*>(&m[0x1C]) = a4;
        *reinterpret_cast<uint16_t*>(&m[0x12]) = (uint16_t)chunk;
        if (part && block) std::memcpy(&m[0x24], block + offset, (size_t)part);
        NetDaemon_send_message(net, m.data(), part + 0x24, 0);
    }
    if (chunks == 0 && send) {  // an empty block (0x28): the header alone
        uint8_t m[0x24] = {};
        *reinterpret_cast<uint16_t*>(&m[0]) = (uint16_t)msg_type;
        *reinterpret_cast<uint16_t*>(&m[0x10]) = (uint16_t)type;
        *reinterpret_cast<int32_t*>(&m[4]) = CurrentPlayerFaction;
        *reinterpret_cast<int32_t*>(&m[0xC]) = fld(net, 0x1B2C);
        *reinterpret_cast<int32_t*>(&m[0x14]) = id;
        NetDaemon_send_message(net, m, 0x24, 0);
    }
    if (send) return;
    if (type == 0) {
        prefs_use();
    } else if (type == 2) {
        if (!fld(net, 0x1B30)) {
            map_shutdown();
            map_init();
            Path_init(&Paths);
        }
    } else if (type == 0x13) {
        BaseWin_check_base(&BaseWin, id);
    } else if (type == 0x11) {
        BaseWin_check_veh(&BaseWin, id);
    } else if (type == 0x16) {
        uint8_t* ff = mem::host(0x96C9E0 + (uint32_t)id * 0x20CC);
        uint8_t* fg = mem::host(0x96C9E0 + (uint32_t)g * 0x20CC);
        auto d = [](uint8_t* p, uint32_t off) -> int32_t& { return *reinterpret_cast<int32_t*>(p + off); };
        ff[0xB8 + g] = (uint8_t)diplo[0];
        fg[0xB8 + id] = (uint8_t)diplo[1];
        d(ff, 0x58 + 4 * g) = diplo[2];
        d(fg, 0x58 + 4 * id) = diplo[3];
        d(ff, 0x128) = diplo[4];
        d(fg, 0x128) = diplo[5];
        d(ff, 0x12C + 4 * g) = diplo[6];
        d(fg, 0x12C + 4 * id) = diplo[7];
        d(ff, 0x14C + 4 * g) = diplo[8];
        d(fg, 0x14C + 4 * id) = diplo[9];
        d(ff, 0x78 + 4 * g) = diplo[10];
        d(fg, 0x78 + 4 * id) = diplo[11];
        d(ff, 0x18 + 4 * g) = diplo[12];
        d(fg, 0x18 + 4 * id) = diplo[13];
        d(ff, 0x38 + 4 * g) = diplo[14];
        d(fg, 0x38 + 4 * id) = diplo[15];
        d(ff, 0x98 + 4 * g) = diplo[16];
        d(fg, 0x98 + 4 * id) = diplo[17];
    } else if (type == 0xD) {
        social_set(id);
    }
}

// ---- the diplomatic channels: one pair of factions at a time per player ----

int32_t THISCALL NetDaemon_open_channel(uint8_t* net, int32_t faction_id, int32_t faction_id_with) {
    if (!MultiplayerActive) return 0;
    if (fld(net, 0x1BA0)) return 1;
    log_say_values("Client requesting Diplo Channel", CurrentPlayerFaction, 0, 0);
    fld(net, 0x1BA0) = 1;
    fld(net, 0x1BA4) = 0;
    message_data(0x8240, 0, faction_id, faction_id_with, 0, 0);
    await(net, "AWAITCHANNEL", 0x640, [&] { return fld(net, 0x1BA4) == 0; });
    if (fld(net, 0x1BA4) > 1) {
        fld(net, 0x1BA0) = 0;
        return 1;
    }
    return 0;
}

void THISCALL NetDaemon_close_channel(uint8_t* net) {
    if (!MultiplayerActive) return;
    message_data(0x8241, 0, 0, 0, 0, 0);
    fld(net, 0x1BA0) = 0;
}

// ---- NetDaemon_game_checksum (0x52F8E0): the game state's checksums (net_upkeep compares the first
// 21 with the other players'; the others only with MultiDebugActive) ----

void THISCALL NetDaemon_game_checksum(uint8_t*, FullChecksum* out) {
    auto ck = [](const void* p, int32_t n, uint8_t seed) -> uint8_t {
        return (uint8_t)checksum(static_cast<const uint8_t*>(p), n, seed);
    };
    auto at = [](uint32_t addr) { return mem::host(addr); };
    uint8_t* tiles = map_bytes();
    uint32_t* o = out->values;
    uint8_t s = ck(at(0x9A649C), 4, 0);
    s = ck(at(0x9A64C8), 0x10, s);
    s = ck(at(0x9A64E8), 2, s);
    s = ck(at(0x9A6514), 0x100, s);
    for (int32_t t = 0; t < 0x59; t++) s ^= (uint8_t)(bit_count(TechOwners[t]) * (t + 1));
    *o++ = s;
    // The factions 1..7 (esi: Factions[f] + 0x3CC; the +0x43C read reaches the next faction).
    s = 0;
    for (uint32_t esi = 0x96EE78; esi < 0x97D40C; esi += 0x20CC) {
        s = ck(at(esi - 4), 4, s);
        s = ck(at(esi), 4, s);
        s = ck(at(esi + 0x98), 4, s);
        s = ck(at(esi - 0x1AC), 4, s);
        s = ck(at(esi - 0x1A4), 0x10, s);
        s = ck(at(esi + 0x43C), 0x10, s);
        uint32_t a[8], b[8];
        for (int32_t i = 0; i < 8; i++) {
            a[i] = *reinterpret_cast<uint32_t*>(at(esi - 0x3B4 + 4 * i)) & 0xB1D3C7F;
            b[i] = *reinterpret_cast<uint32_t*>(at(esi - 0x394 + 4 * i)) & 0x1E68;
        }
        s = ck(a, 0x20, s);
        s = ck(b, 0x20, s);
        s = ck(at(esi - 0x2A8), 4, s);
    }
    *o++ = s;
    s = 0;
    for (int32_t i = 0; i < MapAreaTiles; i++) {
        uint8_t* t = tiles + (size_t)i * 0x2C;
        s = ck(t, 1, s);
        s = ck(t + 3, 1, s);
        s = ck(t + 8, 4, s);
        s = ck(t + 1, 1, s);
        s = ck(t + 7, 1, s);
        s = ck(t + 0xC, 1, s);
    }
    *o++ = s;
    s = 0;
    auto veh_fields = [&](uint8_t* v, uint8_t seed, bool debug) -> uint8_t {
        if (!debug) {
            seed = ck(v, 2, seed);
            seed = ck(v + 2, 2, seed);
            seed = ck(v + 0xA, 2, seed);
            seed = ck(v + 0xE, 1, seed);
            seed = ck(v + 0x10, 1, seed);
            seed = ck(v + 0x24, 1, seed);
            seed = ck(v + 0x28, 1, seed);
            seed = ck(v + 0x2E, 2, seed);
        } else {
            seed = ck(v, 2, seed);
            seed = ck(v + 2, 2, seed);
            seed = ck(v + 0xA, 3, seed);
            seed = ck(v + 0x24, 1, seed);
            seed = ck(v + 0x28, 1, seed);
            seed = ck(v + 0x2E, 2, seed);
        }
        return seed;
    };
    for (int32_t p = 0; p < 8; p++) {
        for (int32_t v = 0; v < VehCount; v++) {
            uint8_t* veh = at(0x952828 + (uint32_t)v * 0x34);
            if (veh[0xE] == p) s = veh_fields(veh, s, false);
        }
    }
    *o++ = s;
    auto base_fields = [&](uint8_t seed) -> uint8_t {
        for (int32_t b = 0; b < BaseCount; b++) {
            uint8_t* esi = at(0x97D07C + (uint32_t)b * 0x134);
            seed = ck(esi - 0x3C, 7, seed);
            seed = ck(esi, 4, seed);
            seed = ck(esi + 4, 4, seed);
            seed = ck(esi + 0x10, 4, seed);
            seed = ck(esi + 0x14, 4, seed);
            seed = ck(esi + 0x3C, 4, seed);
            seed = ck(esi + 0x48, 8, seed);
            seed = ck(esi + 0x50, 9, seed);
        }
        return seed;
    };
    *o++ = base_fields(0);
    int32_t rows = MapAreaY / 16;
    int32_t rest = MapAreaY - rows * 16;
    for (int32_t sec = 0; sec < 16; sec++) {
        int32_t start = sec * rows;
        int32_t n = rows;
        if (sec == 15 && rest) n += rest;
        s = 0;
        int32_t count = (MapAreaX >> 1) * n;
        uint8_t* t = tiles + (size_t)(MapHalfX * start) * 0x2C;
        for (int32_t i = 0; n && i < count; i++, t += 0x2C) {
            s = ck(t, 1, s);
            s = ck(t + 3, 1, s);
            s = ck(t + 8, 4, s);
            s = ck(t + 1, 1, s);
            s = ck(t + 7, 1, s);
        }
        *o++ = s;
    }
    if (!MultiDebugActive) return;
    s = ck(at(0x9A649C), 4, 0);
    s = ck(at(0x9A64C8), 0x10, s);
    *o++ = ck(at(0x9A64E8), 2, s);
    *o++ = ck(at(0x9A6514), 0x100, 0);
    s = 0;
    for (int32_t t = 0; t < 0x59; t++) s ^= (uint8_t)(bit_count(TechOwners[t]) * (t + 1));
    *o++ = s;
    for (uint32_t esi = 0x96EE78; esi < 0x97D40C; esi += 0x20CC) {
        s = ck(at(esi - 4), 4, 0);
        s = ck(at(esi), 4, s);
        s = ck(at(esi + 0x98), 4, s);
        s = ck(at(esi - 0x1AC), 4, s);
        s = ck(at(esi - 0x1A4), 0x10, s);
        s = ck(at(esi + 0x43C), 0x10, s);
        static const int32_t kBlocks[] = {-0x3B4, -0x394, -0x374, -0x314, -0x334, -0x354, -0x280};
        for (int32_t off : kBlocks) s = ck(at(esi + off), 0x20, s);
        *o++ = ck(at(esi - 0x2A8), 4, s);
    }
    static const int32_t kTileFields[][2] = {{0, 1}, {3, 1}, {8, 4}, {1, 1}, {7, 1}, {0xC, 1}};
    for (const auto& f : kTileFields) {
        s = 0;
        for (int32_t i = 0; i < MapAreaTiles; i++) s = ck(tiles + (size_t)i * 0x2C + f[0], f[1], s);
        *o++ = s;
    }
    *o++ = ck(at(0x94A2A0), 0x1C, 0);
    for (int32_t p = 0; p < 8; p++) {
        s = 0;
        for (int32_t v = 0; v < VehCount; v++) {
            uint8_t* veh = at(0x952828 + (uint32_t)v * 0x34);
            if (veh[0xE] == p) s = veh_fields(veh, s, true);
        }
        *o++ = s;
    }
    *o++ = base_fields(0);
}

// ---- the setup window's packets (UI module 0x47B020..0x47B360) ----

namespace {
void send_to_idx(void* msg, int32_t size, int32_t idx, uint16_t type_all, uint16_t type_host, uint16_t type_one) {
    uint8_t* net = &NetState;
    uint16_t& t = *static_cast<uint16_t*>(msg);
    if (idx < 0) {
        t = type_all;
        Net_send(net, msg, size, 0, 1);
    } else if (idx == 0) {
        t = type_host;
        Net_send(net, msg, size, fld(net, 0x764), 1);
    } else {
        t = type_one;
        Net_send(net, msg, size, fld(net, 0x78C + idx * 0x19C), 1);
    }
}
}  // namespace

void GAMEAPI send_faction_data(int32_t idx) {
    uint8_t* net = &NetState;
    if (fld(net, 0x760) != fld(net, 0x764)) return;
    const uint8_t* data = ui_netwin_faction_blob();
    if (!data) return;
    int32_t count = data[1];
    int32_t size = count * 399 + 0x1A4;
    std::vector<uint8_t> m((size_t)size, 0);
    *reinterpret_cast<uint16_t*>(&m[0]) = 0xF0D;
    *reinterpret_cast<int32_t*>(&m[4]) = -1;
    std::memcpy(&m[0x10], data, (size_t)size - 0x10);
    Net_send(net, m.data(), size, idx < 0 ? 0 : fld(net, 0x78C + idx * 0x19C), 1);
}

void GAMEAPI send_all_player_packets(int32_t idx) {
    std::vector<uint8_t> m(0xA74, 0);
    // (the original prepares an empty player table on its stack, then sends the real one)
    if (idx == -1 && !NetSessionState[0]) return;
    *reinterpret_cast<int32_t*>(&m[4]) = -1;
    std::memcpy(&m[0x10], mem::host(0x90DD14), 0xA64);
    if (idx < 0) {
        *reinterpret_cast<uint16_t*>(&m[0]) = 0x4F04;
        Net_send(&NetState, m.data(), 0xA74, 0, 1);
    } else {
        *reinterpret_cast<uint16_t*>(&m[0]) = 0x1F04;
        Net_send(&NetState, m.data(), 0xA74, fld(&NetState, 0x78C + idx * 0x19C), 1);
    }
}

void GAMEAPI send_session_packet(int32_t, int32_t idx) {
    uint8_t m[0x28] = {};
    std::memcpy(m + 0x10, mem::host(0x90E8E0), 0x18);
    send_to_idx(m, 0x28, idx, 0x4F02, 0x2F02, 0x1F02);
}

void GAMEAPI send_session2_packet(int32_t, int32_t idx) {
    uint8_t m[0x178] = {};
    std::memcpy(m + 0x10, mem::host(0x90E778), 0x168);
    send_to_idx(m, 0x178, idx, 0x4F03, 0x2F03, 0x1F03);
}
