// NetDaemon_process_message (0x534400): one received message handled. The original's handlers are
// kept by message type: the setup window's (0x0Fxx: player and session packets, faction data, the
// join refused), the locks (0x2210..0x2216 at the server, 0x1201..0x1208 its answers), the synch
// blocks (0x2101 to the server, 0x4101 / 0x1101 applied), the turn's control (0x2301 / 0x8301 done,
// 0x2302 withdrawn, 0x2303 upkeep phase done, 0x4301 next turn, 0x4303 next upkeep phase, 0x4309 the
// next player, 0x1301..0x1303 the server's notes, 0x4304..0x4307), the events executed by every
// machine with the server's seed (0x44xx: moves, actions, diplomacy; 0x46xx the council; 0x4Dxx
// projects; 0x4Exx landmarks and chat), pause and time controls (0x4Cxx), the server change (0x4B00),
// the diplomatic channels (0x8240 / 0x8241 / 0x1240, 0x15xx). A 0x2000 / 0x8000 event without a
// handler at the server is relayed to every player as 0x4000 with the server's time and sequence
// number (the deletion list renumbering its unit ids first, NetDaemon_fixup_message).
// The DiploWin between two humans: 0x1501 the call (COMMDIPLO), 0x1502..0x1505 its answers and the
// channel closed, 0x1506..0x150F the offers, states and the deal (the window's side in
// ui/diplo_hotseat.cpp through ui_diplo_net).
// Not reconstructed: the scenario file transfer (0x0F05..0x0F0C: NetDaemon_send_files).
#include <cstdio>
#include <cstring>

#include "engine/action.h"
#include "engine/base.h"
#include "engine/battle.h"
#include "engine/clock.h"
#include "engine/council.h"
#include "engine/rules.h"
#include "engine/strings.h"
#include "engine/crt.h"
#include "engine/design.h"
#include "engine/diplo_act.h"
#include "engine/diplomacy.h"
#include "engine/globals.h"
#include "engine/log.h"
#include "engine/map.h"
#include "engine/memmap.h"
#include "engine/move.h"
#include "engine/net.h"
#include "engine/net_misc.h"
#include "engine/parse.h"
#include "engine/probe.h"
#include "engine/random.h"
#include "engine/tech.h"
#include "engine/turn.h"
#include "engine/ui_bridge.h"
#include "engine/veh.h"
#include "engine/zoc.h"

namespace {
int32_t& fld(uint8_t* net, uint32_t off) { return *reinterpret_cast<int32_t*>(net + off); }
int32_t& g32(uint32_t addr) { return *reinterpret_cast<int32_t*>(mem::host(addr)); }
uint8_t* Lock() { return &NetState + 0x14A0; }
int32_t& SpockWanted() { return g32(0x93F670); }
int32_t& SpockBusy() { return g32(0x93F674); }
int32_t& DiploPartner() { return g32(0x73B790); }
int32_t& DiploRefused() { return g32(0x7FFF74); }

void say_leader(int32_t f) {
    PluralDefault = 0;
    GenderDefault = MFactions[f].is_leader_female;
    parse_says(0, MFactions[f].title_leader, -1, -1);
    PluralDefault = 0;
    GenderDefault = MFactions[f].is_leader_female;
    parse_says(1, MFactions[f].name_leader, -1, -1);
    PluralDefault = MFactions[f].is_noun_plural;
    GenderDefault = MFactions[f].noun_gender;
    parse_says(2, MFactions[f].noun_faction, -1, -1);
}

// $TITLE0 $NAME1 of faction f (the channel's answers).
void say_leader2(int32_t f) {
    PluralDefault = 0;
    GenderDefault = MFactions[f].is_leader_female;
    parse_says(0, MFactions[f].title_leader, -1, -1);
    PluralDefault = 0;
    GenderDefault = MFactions[f].is_leader_female;
    parse_says(1, MFactions[f].name_leader, -1, -1);
}

// The player of faction f named in $STR3 (the AlphaNet slot's name).
void say_player(uint8_t* net, int32_t f) {
    for (int32_t i = 1; i <= fld(net, 0x768); i++) {
        if ((int8_t)net[0x790 + i * 0x19C] == f) parse_says(3, reinterpret_cast<char*>(net + 0x76C + i * 0x19C), -1, -1);
    }
}

// The SLOWPOKE note: with three or more players, the last one still playing is named.
void slowpoke(uint8_t* net, const char* label) {
    uint8_t humans = FactionStatus[0];
    if (bit_count(humans & ~fld(net, 0x1B68)) != 1 || bit_count(humans) <= 2) return;
    for (int32_t f = 1; f < 8; f++) {
        uint8_t bit = (uint8_t)(1 << f);
        if (!(FactionStatus[1] & bit) || !(humans & bit) || (fld(net, 0x1B68) & bit)) continue;
        say_leader(f);
        say_player(net, f);
        NetMsg_pop(&NetMsg, label, 5000, 0, nullptr);
        return;
    }
}

// The server tells every other player a faction's note (0x1301 / 0x1303).
void tell_players(uint8_t* net, int32_t type, int32_t sender, int32_t skip) {
    for (int32_t i = 1; i <= fld(net, 0x768); i++) {
        int32_t f = (int8_t)net[0x790 + i * 0x19C];
        if (f != CurrentPlayerFaction && f != skip) message_data(type, f, sender, 1, CurrentTurn, 0);
    }
}

bool server_role() {
    return ((GameMoreRules & 0x10) && !ControlTurnC && CurrentFaction == CurrentPlayerFaction) ||
           ((!(GameMoreRules & 0x10) || ControlTurnC) && NetSessionState[0]);
}
}  // namespace

void THISCALL NetDaemon_process_message(uint8_t* net, uint8_t* msg, int32_t size, int32_t from_pid) {
    int32_t* m = reinterpret_cast<int32_t*>(msg);
    uint16_t type = *reinterpret_cast<uint16_t*>(msg);
    const int32_t sender = AlphaNet_pid_2_who(net, from_pid);  // the sending player's faction
    const int32_t who = m[1];                                    // the faction named in the header
    const int32_t seq = m[3];
    if (net_trace_enabled())
        std::fprintf(stderr, "net: message %04X from %d (faction %d) args %d %d %d %d\n", type, from_pid, who, m[4],
                     m[5], m[6], m[7]);
    // A diplomatic message while not in a channel with its sender: the channel is closed.
    if ((type & 0xF00) == 0x500 && type != 0x1501 && (!NetCommlinkActive || DiploPartner() != who)) {
        if (type == 0x1505) return;
        message_data(0x1505, who, 0, 0, 0, 0);
    }
    auto end_of_event = [&](int32_t ev_sender) { NetDaemon_end_of_event(net, ev_sender, seq); };
    switch (type) {
    // ---- the setup window ----
    case 0x1F00:
    case 0x4F00:
        fld(net, 0x1B34) = 1;
        // fallthrough
    case 0x2F00: {
        uint8_t* slot = net + who * 0x19C;
        slot[0x790] = msg[0x18];
        slot[0x792] = msg[0x14];
        slot[0x791] = msg[0x10];
        if (type & 0x2000) {
            *reinterpret_cast<uint16_t*>(msg) = (uint16_t)((type & 0xDFFF) | 0x4000);
            NetDaemon_send_message(net, msg, size, 0);
        }
        break;
    }
    case 0x1F01:  // the session refused us: full or under way
        NetMsg_pop(&NetMsg, "NOJOIN", 5000, 2, nullptr);
        fld(net, 0x1B70) = 2;
        ui_netwin_cancel();
        break;
    case 0x1F02:
    case 0x2F02:
    case 0x4F02:
        std::memcpy(mem::host(0x90E8E0), msg + 0x10, 0x18);
        if (type & 0x2000) {
            *reinterpret_cast<uint16_t*>(msg) = (uint16_t)((type & 0xDFFF) | 0x4000);
            NetDaemon_send_message(net, msg, size, 0);
        } else {
            ui_netwin_redraw();
        }
        break;
    case 0x1F03:
    case 0x2F03:
    case 0x4F03:
        std::memcpy(mem::host(0x90E778), msg + 0x10, 0x168);
        if (type & 0x2000) {
            *reinterpret_cast<uint16_t*>(msg) = (uint16_t)((type & 0xDFFF) | 0x4000);
            NetDaemon_send_message(net, msg, size, 0);
        } else {
            ui_netwin_redraw();
        }
        break;
    case 0x1F04:
    case 0x4F04:
        fld(net, 0x1B34) = 1;
        // fallthrough
    case 0x2F04: {
        int32_t idx = who;
        if (!(type & 0x2000)) {
            if (idx == -1) std::memcpy(mem::host(0x90DD14), msg + 0x10, 0xA64);
            else std::memcpy(mem::host(0x90DB98 + idx * 0x17C), msg + 0x10, 0x17C);
            for (int32_t i = 1; i <= fld(net, 0x768); i++) {
                const uint8_t* c = mem::host(0x90DB98 + i * 0x17C);
                char note[96];
                std::snprintf(note, sizeof(note), "net: player %d ready %d faction %d", i, c[1], (int8_t)c[3]);
                ui_net_note_text(note);
            }
            ui_netwin_redraw();
            break;
        }
        // A player's choice at the server: checked (a faction taken stays with its first owner),
        // then relayed to everyone.
        *reinterpret_cast<uint16_t*>(msg) = (uint16_t)((type & 0xDFFF) | 0x4000);
        if (idx != -1) {
            uint8_t* cfg = mem::host(0x90DB98 + idx * 0x17C);
            int32_t faction = (int8_t)msg[0x13];
            int32_t old = (int8_t)cfg[3];
            if (NetSetupType == 2 || NetSetupType == 3) {  // a scenario: a faction taken stays taken
                if (faction != -1) {
                    int32_t lock = *reinterpret_cast<const int32_t*>(ui_netwin_faction_blob() + faction * 400 + 4);
                    if (lock && lock != m[0x62]) return;  // (its password)
                    for (int32_t i = 1; i <= fld(net, 0x768); i++) {
                        uint8_t* other = mem::host(0x90DB98 + i * 0x17C);
                        if (i != idx && other[3] != 0xFF && (int8_t)other[3] == faction) return;
                    }
                }
                cfg[3] = msg[0x13];
                *reinterpret_cast<int32_t*>(cfg + 0x178) = m[0x62];
            }
            if ((int8_t)msg[0x13] != (int8_t)cfg[3] || old != (int8_t)cfg[3]) ui_netwin_fill_player(msg + 0x10, faction);
        }
        NetDaemon_send_message(net, msg, size, 0);
        break;
    }
    case 0x0F0A: net[0x794 + who * 0x19C] = 1; break;
    case 0x0F0C: net[0x794 + who * 0x19C] = 2; break;
    case 0x0F0D: ui_netwin_faction_data(msg); break;
    case 0x0F0E:
        if (ui_netwin_visible()) ui_netwin_pick_pass(1);
        break;

    // ---- locks (at the server) ----
    case 0x2210:
        log_say_values("From Client: LOCK", sender, m[4], m[5]);
        if ((!SpockBusy() && !NetPlayerTurnDone) || sender == CurrentPlayerFaction) {
            if (!ControlTurnC) {
                if (!Lock_lock(Lock(), sender, (uint32_t)m[4], m[5], m[6], (uint32_t)m[9], m[7], m[8])) {
                    log_say_values(" (Sending) Lock OK ", 0, 0, 0);
                    if ((m[4] & 2) && Lock_check_global_2(Lock(), sender)) {
                        message_data(0x2216, sender, 0, 0, 0, 0);
                        break;
                    }
                    message_data(0x1201, sender, 0, 0, 0, 0);
                } else {
                    log_say_values(" (Sending) Lock Failed ", 0, 0, 0);
                    message_data((fld(net, 0x1580) != 0) + 0x1202, sender, 0, 0, 0, 0);
                }
                break;
            }
            log_say_values(" (Sending) Lock failed -- UPKEEP ", 0, 0, 0);
        } else {
            log_say_values(" (Sending) Lock failed -- REJECT ", 0, 0, 0);
        }
        message_data(0x1203, sender, 0, 0, 0, 0);
        break;
    case 0x2211:
        log_say_values("From Client: LOCK GLOBAL", sender, 0, 0);
        if (!Lock_global_lock(Lock(), sender)) {
            message_data(0x1201, sender, 0, 0, 0, 0);
            Lock_check_global(Lock());
        } else {
            message_data((fld(net, 0x1580) != 0) + 0x1202, sender, 0, 0, 0, 0);
        }
        break;
    case 0x2212:
        log_say_values("From Client: Ready to unlock", sender, 0, 0);
        Lock_unlock(Lock(), sender);
        break;
    case 0x2213: {  // an event executed by a player: its deletion entry and the locker's acknowledgement
        log_say_values("From Client: ACKNOWLEDGE", sender, m[4], 0);
        int32_t f = m[4];
        DeletionList_ack(&DeleteList, seq, sender);
        uint8_t* pl = Lock() + f * 0x1C;
        if (PlayerLock_active(pl) && !(pl[0] & 1)) {
            pl[0] |= (uint8_t)(1 << (sender & 31));
            if ((pl[0] & FactionStatus[0]) == FactionStatus[0]) {
                log_say_values("Sending EXEC DONE to", f, 0, 0);
                message_data(0x1206, f, 0, 0, 0, 0);
                pl[0] |= 1;
            }
        }
        break;
    }
    case 0x2214:
        log_say_values("From Client: RELOCK", sender, 0, 0);
        Lock()[sender * 0x1C] = 0;
        break;
    case 0x2215:
        if (!SpockBusy() && !NetPlayerTurnDone) {
            log_say_values("From Client: ADD LOCK", sender, m[4], m[5]);
            Lock_add_lock(Lock(), sender, (uint32_t)m[4], m[5], m[6]);
            log_say_values(" (Sending) Add OK ", 0, 0, 0);
            message_data(0x1207, sender, 0, 0, 0, 0);
        } else {
            log_say_values(" (Sending) Lock failed -- REJECT ", 0, 0, 0);
            message_data(0x1208, sender, 0, 0, 0, 0);
        }
        break;
    case 0x2216:
        log_say_values("From Server: LOCK OK GLOBAL", 0, 0, 0);
        fld(net, 0x1B70) = 1;
        g32(0x93F668) = 0;
        g32(0x93F66C) = 0;
        fld(net, 0x1588) = 1;
        break;
    case 0x1201:
    case 0x1207:
        log_say_values(type == 0x1201 ? "From Server: LOCK OK" : "From Server: ADD LOCK OK", 0, 0, 0);
        fld(net, 0x1B70) = 1;
        if (type == 0x1201) {
            g32(0x93F668) = 0;
            g32(0x93F66C) = 0;
        }
        break;
    case 0x1202:
    case 0x1203:
    case 0x1204: {
        log_say_values("From Server: LOCK FAILED", type, 0, 0);
        fld(net, 0x1B70) = type - 0x1200;
        uint32_t now = engine_real_time();
        if (!g32(0x93F66C)) g32(0x93F66C) = (int32_t)now;
        g32(0x93F668) = (now - (uint32_t)g32(0x93F66C) > 5000) ? 5000 : 1000;
        break;
    }
    case 0x1205:
        log_say_values("From Server: GLOBAL LOCK READY", 0, 0, 0);
        fld(net, 0x1588) = 1;
        break;
    case 0x1206:
        log_say_values("From Server: EXEC DONE", fld(net, 0x1BC0), 0, 0);
        if (fld(net, 0x1BC0)) {
            log_say_values("Remaining locked", 0, 0, 0);
            fld(net, 0x1BC0) = 0;
            message_data(0x2214, 0, 0, 0, 0, 0);
        } else {
            fld(net, 0x1BB0) = 0;
        }
        break;
    case 0x1208:
        log_say_values("From Server: ADD LOCK FAIL", 0, 0, 0);
        fld(net, 0x1B70) = 2;
        break;

    // ---- synch blocks ----
    case 0x1101:
    case 0x4101: {
        int32_t pending = fld(net, 0x1BBC);
        fld(net, 0x1BBC) = pending | (1 << (who & 31));
        NetDaemon_synch(net, *reinterpret_cast<int16_t*>(msg + 0x10), m[5], m[6], m[7], m, 0, 0x2101);
        if (!pending) {
            while (fld(net, 0x1BBC)) {
                int32_t from = 0, sz = 0;
                if (uint8_t* next = Net_get(net, &from, &sz)) NetDaemon_process_message(net, next, sz, from);
                else if (ControlTurnA || !net_link_active()) break;
            }
            if (who == CurrentPlayerFaction) fld(net, 0x1BB8) = 0;
        }
        break;
    }

    // ---- the turn's control ----
    case 0x2301:
    case 0x8301: {
        uint32_t bit = 1u << (sender & 31);
        fld(net, 0x1B5C) |= bit;
        fld(net, 0x1B68) |= bit;
        if (!(GameMoreRules & 0x10)) tell_players(net, 0x1301, sender, -1);
        ui_multiwin_draw();
        log_say_values("From Client: DONE TURN", sender, fld(net, 0x1B5C), 0);
        if (!(GameMoreRules & 0x10)) {
            slowpoke(net, "SLOWPOKE");
            break;
        }
        if (!NetSessionState[0]) break;
        for (int32_t f = CurrentFaction + 1; f < 8; f++) {
            uint8_t b = (uint8_t)(1 << f);
            if ((FactionStatus[1] & b) && (FactionStatus[0] & b)) {
                message_data(0x4309, 0, f, 0, 0, 0);
                return;
            }
        }
        NetNextPlayerTurn = 1;
        break;
    }
    case 0x1301:
        log_say_values("From Server: TURN INFO", m[4], m[5], m[6]);
        if (!(GameMoreRules & 0x10) && !NetSessionState[0] && m[6] == CurrentTurn) {
            if (!m[5]) {
                fld(net, 0x1B68) &= ~(1 << (m[4] & 31));
            } else {
                fld(net, 0x1B68) |= 1 << (m[4] & 31);
                slowpoke(net, "SLOWPOKE");
            }
        }
        ui_multiwin_draw();
        break;
    case 0x2302:
        log_say_values("From Client: WITHDRAW END OF TURN", sender, 0, 0);
        if (NetTurnDoneMask != 0xFF && (NetTurnDoneMask & (1 << (sender & 31))) && m[4] == CurrentTurn) {
            NetTurnDoneMask &= ~(1 << (sender & 31));
            message_data(0x1302, sender, CurrentTurn, 0, 0, 0);
            for (int32_t i = 1; i <= fld(net, 0x768); i++) {
                int32_t f = (int8_t)net[0x790 + i * 0x19C];
                if (f != sender && f != CurrentPlayerFaction) message_data(0x1301, f, sender, 0, CurrentTurn, 0);
            }
        }
        ui_multiwin_draw();
        break;
    case 0x1302: {
        log_say_values("From Server: WITHDRAW APPROVED", 0, 0, 0);
        uint32_t bit = 1u << (CurrentPlayerFaction & 31);
        if ((GameState & 2) && NetTurnDoneMask != 0xFF && ((bit & (uint32_t)NetTurnDoneMask) || NetSessionState[0]) &&
            m[4] == CurrentTurn) {
            NetTurnDoneMask &= ~bit;
            fld(net, 0x1B68) &= ~bit;
            GameState &= ~2;
        }
        ui_multiwin_draw();
        break;
    }
    case 0x2303: {
        if (m[4] != NetUpkeepCount)
            log_say_values(" Upkeep acknowledgement for wrong phase! ", m[4], NetUpkeepCount, 0);
        else if (m[5] != CurrentTurn)
            log_say_values(" Upkeep ack for wrong turn! ", m[5], CurrentTurn, 0);
        uint32_t bit = 1u << (sender & 31);
        fld(net, 0x1B6C) |= bit;
        if (!(bit & (uint32_t)fld(net, 0x1B60))) {
            fld(net, 0x1B60) |= bit;
            for (int32_t i = 1; i <= fld(net, 0x768); i++) {
                int32_t f = (int8_t)net[0x790 + i * 0x19C];
                if (f != CurrentPlayerFaction) message_data(0x1303, f, sender, 1, CurrentTurn, 0);
            }
            ui_multiwin_draw();
        }
        log_say_values("From Client: DONE UPKEEP", sender, m[4], fld(net, 0x1B60));
        break;
    }
    case 0x1303:
        log_say_values("From Server: UPKEEP INFO", m[4], m[5], m[6]);
        if (!NetSessionState[0]) {
            fld(net, 0x1B6C) |= 1 << (m[4] & 31);
            ui_multiwin_draw();
        }
        break;
    case 0x2308:
        log_say_values("From Client: HELP ME SPOCK", sender, 0, 0);
        SpockWanted() = 1;
        break;
    case 0x4301:
        game_srand((uint32_t)m[2]);
        fld(net, 0x1B68) = 0xFF;
        fld(net, 0x1B5C) = 0xFF;
        ui_multiwin_draw();
        log_say_values("From Server: NEXT TURN", CurrentTurn, fld(net, 0x1B5C), 0);
        g32(0x93F668) = 0;
        g32(0x93F66C) = 0;
        if (GameMoreRules & 0x10) {
            GameState |= 0x800;
            NetNextPlayerTurn = 1;
        }
        break;
    case 0x4303:
        log_say_values("From Server: NEXT UPKEEP", m[4], m[5], 0);
        if (m[4] != NetUpkeepCount)
            log_say_values(" Upkeep instruction for wrong phase! ", m[4], NetUpkeepCount, 0);
        else if (m[5] != CurrentTurn)
            log_say_values(" Upkeep instruction for wrong turn! ", m[5], CurrentTurn, 0);
        game_srand((uint32_t)m[2]);
        fld(net, 0x1B6C) = 0xFF;
        fld(net, 0x1B60) = 0xFF;
        ui_multiwin_draw();
        g32(0x93F668) = 0;
        g32(0x93F66C) = 0;
        break;
    case 0x4304:
        log_say_values("From Server: UPKEEP BASE", sender, 0, 0);
        fld(net, 0x1B64) = 1;
        break;
    case 0x4305:
        log_say_values("From Server: MSG EVENTS", sender, m[4], 0);
        if (NetSessionState[42] <= m[4]) NetSessionState[42] = m[4];  // 0x93E968
        break;
    case 0x4306: {
        log_say_values("From Server: MSG TIME BONUS", who, m[4], 0);
        int32_t f = who;
        int32_t& pills = *reinterpret_cast<int32_t*>(mem::host(0x96EA34 + (uint32_t)f * 0x20CC));
        if (ControlTurnC || pills < 1) break;
        pills--;
        int32_t extra = TimeControl[GameTimeControl].extra * 1000;
        NetTurnTimeLeft += extra;
        g32(0x9A64E4) += extra;
        ui_multiwin_draw();
        const char* name = Net_get_player_name(net, from_pid);
        parse_says(0, name ? name : "", -1, -1);
        parse_num(0, TimeControl[GameTimeControl].extra);
        NetMsg_pop(&NetMsg, f == CurrentPlayerFaction ? "WEBONUSED" : "HEBONUSED", 5000, 0, nullptr);
        break;
    }
    case 0x4307:
        for (int32_t i = 0; i < 0x15; i++) reinterpret_cast<int32_t*>(&RemoteChecksum)[who * 0x15 + i] = m[4 + i];
        break;
    case 0x4309: {
        log_say_values("From Server: NEXT PLAYER TURN", m[4], 0, 0);
        CurrentFaction = m[4];
        {
            char note[64];
            std::snprintf(note, sizeof(note), "net: player turn faction %d", CurrentFaction);
            ui_net_note_text(note);
        }
        say_leader(CurrentFaction);
        const char* name = Net_get_player_name(net, AlphaNet_who_2_pid(net, CurrentFaction));
        parse_says(3, name ? name : "", -1, -1);
        if (CurrentFaction == CurrentPlayerFaction) {
            NetPlayerTurnDone = 0;
            char year[80];
            year[0] = 0;
            say_year(year);
            parse_says(4, year, -1, -1);
            X_pop("SIMULYOU", 0);
        } else {
            NetMsg_pop(&NetMsg, "SIMULWHOSE", 5000, 0, nullptr);
        }
        break;
    }

    // ---- the events every machine executes (the server's seed) ----
    case 0x4401: {
        int32_t v = m[4];
        log_say_values("From Server: MOVE VEH", who, v, m[5]);
        {
            char note[96];
            std::snprintf(note, sizeof(note), "net: move veh %d dir %d (faction %d)", v, m[5], who);
            ui_net_note_text(note);
        }
        game_srand((uint32_t)m[2]);
        if (Vehs[v].faction_id == who) {
            order_veh(v, m[5], 2);
        } else if (!NetSessionState[0]) {
            message_data(0x2308, 0, 0, 0, 0, 0);
            order_veh(v, m[5], 2);
        } else {
            SpockWanted() = 1;
        }
        log_say_values("Acknowledging MOVE VEH", who, 0, 0);
        end_of_event(who);
        break;
    }
    case 0x4402:
        log_say_values("From Server: ARTY", who, m[4], m[5]);
        game_srand((uint32_t)m[2]);
        VehAttackFlags = 2;
        battle_fight(m[4], m[5], 1, 1, nullptr);
        end_of_event(who);
        break;
    case 0x4403:
        game_srand((uint32_t)m[2]);
        action_airdrop(m[4], m[5], m[6], 2);
        log_say_values("Acknowledging AIRDROP", who, 0, 0);
        end_of_event(who);
        break;
    case 0x4404:
        log_say_values("From Server: ACTION", who, m[4], Vehs[m[4]].order);
        game_srand((uint32_t)m[2]);
        action(m[4]);
        end_of_event(who);
        break;
    case 0x4405:
        game_srand((uint32_t)m[2]);
        action_terraform(m[4], m[5] - 4, 1);
        Vehs[m[4]].order = (uint8_t)m[6];
        end_of_event(who);
        break;
    case 0x4406:
        game_srand((uint32_t)m[2]);
        action_destroy(m[4], m[5], -1, -1);
        end_of_event(who);
        break;
    case 0x4407:
        game_srand((uint32_t)m[2]);
        if (!m[5]) kill(m[4]);
        else action_destruct(m[4]);
        end_of_event(who);
        break;
    case 0x4408:
        log_say("From Server: BUILD BASE", reinterpret_cast<char*>(&m[5]), m[4], who, 0);
        {
            char note[96];
            std::snprintf(note, sizeof(note), "net: build base %s (veh %d, faction %d)", reinterpret_cast<char*>(&m[5]), m[4], who);
            ui_net_note_text(note);
        }
        game_srand((uint32_t)m[2]);
        action_build(m[4], reinterpret_cast<char*>(&m[5]));
        end_of_event(who);
        break;
    case 0x4409:
        game_srand((uint32_t)m[2]);
        action_oblit(m[4], m[5]);
        end_of_event(who);
        break;
    case 0x4410:
        action_gate(m[4], m[5]);
        end_of_event(who);
        break;
    case 0x4411:
        game_srand((uint32_t)m[2]);
        do_upgrade(m[4], m[5], m[6]);
        break;
    case 0x4412:
        game_srand((uint32_t)m[2]);
        action_sat_attack(m[4], m[5], m[6], m[7]);
        end_of_event(who);
        break;
    case 0x4413:
        game_srand((uint32_t)m[2]);
        action_destroy(m[4], m[5], m[6], -1);
        end_of_event(who);
        break;
    case 0x4414:
        game_srand((uint32_t)m[2]);
        action_staple(m[4]);
        end_of_event(who);
        break;
    case 0x4415:
        game_srand((uint32_t)m[2]);
        retire_proto(m[4], m[5]);
        end_of_event(who);
        break;
    case 0x4416:
        game_srand((uint32_t)m[2]);
        action_give(m[4], m[5]);
        end_of_event(who);
        break;
    case 0x4417:
        game_srand((uint32_t)m[2]);
        action_tectonic(m[4], m[5], m[6]);
        end_of_event(who);
        break;
    case 0x4418:
        game_srand((uint32_t)m[2]);
        action_fungal(m[4], m[5], m[6]);
        end_of_event(who);
        break;
    case 0x4440:
        game_srand((uint32_t)m[2]);
        double_cross(m[4], m[5], m[6]);
        end_of_event(who);
        break;
    case 0x4441:
        log_say_hex("Received TREATY ON", m[4], m[5], m[6]);
        treaty_on(m[4], m[5], (uint32_t)m[6]);
        break;
    case 0x4442: treaty_off(m[4], m[5], (uint32_t)m[6]); break;
    case 0x4443: set_treaty(m[4], m[5], (uint32_t)m[6], m[7]); break;
    case 0x4444: agenda_on(m[4], m[5], (uint32_t)m[6]); break;
    case 0x4445: agenda_off(m[4], m[5], (uint32_t)m[6]); break;
    case 0x4446: set_agenda(m[4], m[5], (uint32_t)m[6], m[7]); break;
    case 0x4447:  // energy: m[5] to faction m[4], m[7] to faction m[6]
        if (m[4]) *reinterpret_cast<int32_t*>(mem::host(0x96CC00 + (uint32_t)m[4] * 0x20CC)) += m[5];
        if (m[6]) *reinterpret_cast<int32_t*>(mem::host(0x96CC00 + (uint32_t)m[6] * 0x20CC)) += m[7];
        break;
    case 0x4448: trade_maps(m[4], m[5]); break;
    case 0x4449:
        game_srand((uint32_t)m[2]);
        give_a_base(m[4], m[5]);
        break;
    case 0x444A:
        game_srand((uint32_t)m[2]);
        do_withdrawal(m[4], m[5]);
        break;
    case 0x444B:
    case 0x444C:
        game_srand((uint32_t)m[2]);
        tech_achieved(m[4], m[5], m[6], 0);
        if (type == 0x444C && CurrentPlayerFaction == m[6]) {
            parse_says(0, MFactions[m[4]].noun_faction, -1, -1);
            StrBuffer[0] = 0;
            say_tech(m[5], 0);
            NetMsg_pop(&NetMsg, StrBuffer, 0, 1, nullptr);
        }
        break;
    case 0x444D:
        game_srand((uint32_t)m[2]);
        pact_ends(m[4], m[5]);
        break;
    case 0x444E:
        *reinterpret_cast<int32_t*>(mem::host(0x96CAA4 + (uint32_t)m[4] * 0x20CC + (uint32_t)m[5] * 4)) = m[6];
        *reinterpret_cast<int32_t*>(mem::host(0x96CAC4 + (uint32_t)m[4] * 0x20CC + (uint32_t)m[5] * 4)) = m[7];
        break;
    case 0x444F:
        if (m[4] != CurrentPlayerFaction) {
            say_leader(m[4]);
            parse_say(3, MFactions[m[4]].adj_name_faction, -1, -1);
            parse_num(0, game_year(m[5]));
            popp(ScriptFileName, "CORNERWARNING", 0, "econwin_sm.pcx", 0);
        }
        break;
    case 0x4600:
        log_say_values("Received CONVENE COUNCIL", m[4], m[5], m[6]);
        game_srand((uint32_t)m[2]);
        council(m[4], m[5], m[6]);
        break;

    // ---- chat, landmarks, projects ----
    case 0x1E00:
    case 0x4E00: {
        log_say("Received CHAT", reinterpret_cast<char*>(&m[4]), who, 0, 0);
        if (DiploRefused() & (1 << (who & 31))) break;
        if (fld(net, 0x1B40) == 1) {
            int32_t idx = AlphaNet_pid_2_idx(net, from_pid);
            if (idx >= 0) ui_netwin_chat(idx, reinterpret_cast<char*>(&m[4]));
        } else if (!NetCommlinkActive) {
            StrBuffer[0] = 0;
            if (fld(net, 0x1B40) < 2) {
                const char* name = Net_get_player_name(net, from_pid);
                crt_strcat(StrBuffer, name ? name : "");
            } else {
                PluralDefault = 0;
                GenderDefault = MFactions[who].is_leader_female;
                crt_strcat(StrBuffer, MFactions[who].title_leader);
                crt_strcat(StrBuffer, " ");
                PluralDefault = 0;
                GenderDefault = MFactions[who].is_leader_female;
                crt_strcat(StrBuffer, MFactions[who].name_leader);
            }
            crt_strcat(StrBuffer, ": ");
            crt_strcat(StrBuffer, reinterpret_cast<char*>(&m[4]));
            ui_main_chat(StrBuffer, who);
        } else {
            ui_diplo_chat(reinterpret_cast<char*>(&m[4]), who);
        }
        break;
    }
    case 0x4E01:
        log_say("Received LANDMARK", reinterpret_cast<char*>(&m[8]), m[4], m[5], 0);
        new_landmark(m[4], m[5], reinterpret_cast<char*>(&m[8]));
        if (who == CurrentPlayerFaction) draw_map(1);
        break;
    case 0x4E02:
        kill_landmark(m[4], m[5]);
        if (who == CurrentPlayerFaction) draw_map(1);
        break;
    case 0x4E03: {
        int32_t i = find_landmark(m[4], m[5], 1);
        if (i >= 0) crt_strcpy(Landmarks[i].name, reinterpret_cast<char*>(&m[8]));
        if (who == CurrentPlayerFaction) draw_map(1);
        break;
    }

    // ---- pause, time controls, server change ----
    case 0x4C00: {
        if (!fld(net, 0x1B50)) {
            fld(net, 0x1B54) = (int32_t)engine_real_time() - NetTurnTimeLeft;
            FX_play(&Sounds, 0x15);
        }
        fld(net, 0x1B50) |= 1 << (sender & 31);
        const char* name = Net_get_player_name(net, from_pid);
        parse_says(0, name ? name : "", -1, -1);
        NetMsg_pop(&NetMsg, "PAUSED", 0, 2, nullptr);
        break;
    }
    case 0x4C01: {
        fld(net, 0x1B50) &= ~(1 << (sender & 31));
        if (!fld(net, 0x1B50)) FX_play(&Sounds, 0x15);
        const char* name = Net_get_player_name(net, from_pid);
        parse_says(0, name ? name : "", -1, -1);
        NetMsg_pop(&NetMsg, "UNPAUSED", 0, 2, nullptr);
        break;
    }
    case 0x4C02: {
        int32_t left = g32(0x9A64E4) - NetTurnTimeLeft;
        GameTimeControl = m[4];
        set_time_controls();
        NetTurnTimeLeft = g32(0x9A64E4) - left;
        ui_multiwin_draw();
        const char* name = Net_get_player_name(net, from_pid);
        parse_says(0, name ? name : "", -1, -1);
        parse_say(1, TimeControl[GameTimeControl].name, -1, -1);
        NetMsg_pop(&NetMsg, sender == CurrentPlayerFaction ? "WECHANGEDTIME" : "HECHANGEDTIME", 5000, 2, nullptr);
        break;
    }
    case 0x4B00:
        log_say_values("Received SERVER CHANGE message", 0, 0, 0);
        NetMsg_pop(&NetMsg, "CHANGINGSERVERS", 0, 2, nullptr);
        break;

    // ---- diplomatic channels ----
    case 0x8240: {
        uint32_t pair = (1u << (m[5] & 31)) | (1u << (m[4] & 31));
        int32_t ok = (pair & (uint32_t)fld(net, 0x1B7C)) == 0;
        if (ok) {
            fld(net, 0x1B80 + sender * 4) = (int32_t)pair;
            fld(net, 0x1B7C) |= (int32_t)pair;
        }
        log_say_values("Received OPEN CHANNEL request", sender, m[4], m[5]);
        message_data(0x1240, sender, ok, 0, 0, 0);
        break;
    }
    case 0x8241:
        fld(net, 0x1B7C) &= ~fld(net, 0x1B80 + sender * 4);
        log_say_values("Received CLOSE CHANNEL", sender, fld(net, 0x1B80 + sender * 4), fld(net, 0x1B7C));
        fld(net, 0x1B80 + sender * 4) = 0;
        break;
    case 0x1240:
        fld(net, 0x1BA4) = m[4] ? 1 : 2;
        log_say_values(m[4] ? "Received OPEN CHANNEL SUCCESS" : "Received OPEN CHANNEL FAILED", 0, 0, 0);
        break;
    case 0x1505:
        log_say_values("Received CLOSE CHAN", who, 0, 0);
        if (NetCommlinkActive) {
            ui_diplo_closed(who);  // (Win_release_modal: the DiploWin closed)
            NetCommlinkActive = 0;
            if (GameLanguage == 0) {
                say_leader2(who);
                NetMsg_pop(&NetMsg, "DIPLOCLOSE", 5000, 0, nullptr);
            }
        }
        break;

    // ---- the DiploWin between two humans (commlink_attempt's 0x1501, FUN_00443F10) ----
    case 0x1501: {  // OPEN CHAN: another player calls us
        log_say_values("Received OPEN CHAN", who, 0, 0);
        uint32_t bit = 1u << (who & 31);
        if ((uint32_t)DiploRefused() & bit) {
            message_data(0x1503, who, 0, 0, 0, 0);
            break;
        }
        if (fld(net, 0x1BAC) == 0) {
            if (NetCommlinkActive) {
                // We were calling them too: the channel opens at once.
                if (who == DiploPartner() && !ui_diplo_net(0, 0, 0)) {
                    message_data(0x1502, who, 0, 0, 0, 0);
                    NetCommlinkActive = 1;
                    diplo(CurrentPlayerFaction, who);
                    break;
                }
            } else if (!DiploWinState) {
                if (PopWaitActive) {  // in a popup: busy
                    message_data(0x1504, who, 1, 0, 0, 0);
                    break;
                }
                int32_t r;
                char note0[64];
                std::snprintf(note0, sizeof(note0), "net: COMMDIPLO from faction %d", who);
                ui_net_note_text(note0);
                for (;;) {  // COMMDIPLO: the communications officer (4: the leader's profile, asked again)
                    say_leader(who);
                    parse_num(0, game_random(0, 10) + 1);
                    parse_says(3, get_him_her(who, 0), -1, -1);
                    r = X_pops("COMMDIPLO", FactionPortraits[who], 0x539920);
                    if ((GameState & 0x800) || r != 4) break;
                    report_intel(who);
                }
                char note[64];
                std::snprintf(note, sizeof(note), "net: COMMDIPLO from faction %d: %d", who, r);
                ui_net_note_text(note);
                if (!(GameState & 0x800) && r == 0) {
                    message_data(0x1502, who, 0, 0, 0, 0);
                    NetCommlinkActive = 1;
                    diplo(CurrentPlayerFaction, who);
                    break;
                }
                if (!(GameState & 0x800) && r == 3) DiploRefused() |= (int32_t)bit;
                message_data(0x1503, who, r == 2, 0, 0, 0);
                break;
            }
        }
        message_data(0x1504, who, 0, 0, 0, 0);
        break;
    }
    case 0x1502:  // OPEN ACKNOWLEDGE: the player we called answers
        log_say_values("Received OPEN ACKNOWLEDGE", who, 0, 0);
        if (!NetCommlinkActive || who == DiploPartner()) {
            if (!ui_diplo_net(0, 0, 0)) {
                message_data(0x2441, 0, CurrentPlayerFaction, who, 8, 0);
                diplo(CurrentPlayerFaction, who);
            }
        } else {
            message_data(0x1505, who, 0, 0, 0, 0);
        }
        break;
    case 0x1503:  // OPEN REFUSE
    case 0x1504:  // OPEN BUSY
        NetCommlinkActive = 0;
        log_say_values(type == 0x1503 ? "Received OPEN REFUSE" : "Received OPEN BUSY", who, 0, 0);
        say_leader2(who);
        if (m[4] == 0 || m[4] == 1) {
            static const char* const labels[2][2] = {{"DIPLOREFUSE", "DIPLOREFUSE1"}, {"DIPLOBUSY", "DIPLOBUSY1"}};
            NetMsg_pop(&NetMsg, labels[type == 0x1504][m[4]], 5000, 0, nullptr);
        }
        break;
    case 0x1506:  // OPEN OFFER: their offer added (FUN_00441490(1, type, value))
        log_say_values("Received OPEN OFFER", who, m[4], m[5]);
        ui_diplo_net(type, m[4], m[5]);
        break;
    case 0x1507:  // KILL OFFER: theirs removed (FUN_004413F0(1, index))
        log_say_values("Received KILL OFFER", who, m[4], 0);
        ui_diplo_net(type, m[4], 0);
        break;
    case 0x1508:  // CLEAR OFFER
        log_say_values("Received CLEAR OFFER", who, 0, 0);
        ui_diplo_net(type, 0, 0);
        break;
    case 0x1509:  // CHANNEL STATUS: their state (1 agreed, 2 rejected), FUN_004426F0
        log_say_values("Received CHANNEL STATUS", who, m[4], 0);
        ui_diplo_net(type, m[4], 0);
        break;
    case 0x150A:  // MAYBE DEAL: both agreed on their side; ours answers
        log_say_values("Received MAYBE DEAL", who, 0, 0);
        if (ui_diplo_net(1, 0, 0) == 1 && ui_diplo_net(2, 0, 0) == 1)
            message_data(0x150B, who, 0, 0, 0, 0);
        else
            message_data(0x150C, who, ui_diplo_net(1, 0, 0), ui_diplo_net(2, 0, 0), 0, 0);
        break;
    case 0x150B:  // YES DEAL: FUN_00442740 (the lower faction executes it)
        log_say_values("Received YES DEAL", who, 0, 0);
        ui_diplo_net(type, 0, 0);
        break;
    case 0x150C:  // NO DEAL: the states as they see them
        log_say_values("Received NO DEAL", who, 0, 0);
        ui_diplo_net(type, m[4], m[5]);
        break;
    case 0x150D:  // DONE DEAL: FUN_004429C0
        log_say_values("Received DONE DEAL", who, 0, 0);
        ui_diplo_net(type, 0, 0);
        break;
    case 0x150E:
        log_say_values("Received DEMAND WITHDRAWAL", who, 0, 0);
        break;
    case 0x150F:  // a human told of a vendetta others agreed against them
        log_say_values("Received VENDETTA WARNING", who, 0, 0);
        say_leader(who);
        NetMsg_pop(&NetMsg, "VENDETTAWARNING", 5000, 0, nullptr);
        break;

    default:
        // A request to the server without a handler: relayed to everyone as a timed event.
        if (type & 0xA000) {
            m[2] = (int32_t)net_stamp();
            if (!NetDaemon_fixup_message(net, msg)) {
                *reinterpret_cast<uint16_t*>(msg) = (uint16_t)((type & 0x1FFF) | 0x4000);
                m[3] = ++fld(net, 0x1B2C);
                NetDaemon_send_message(net, msg, size, 0);
            }
        } else if (net_trace_enabled()) {
            std::fprintf(stderr, "net: message %04X not handled\n", type);
        }
        break;
    }
    // An awaited diplomatic event done (NetDaemon_await_diplo).
    uint32_t awaited = (uint32_t)fld(net, 0x1BA8);
    if (awaited && (type & 0x4000) && type == (awaited | 0x4000) && who == CurrentPlayerFaction) fld(net, 0x1BA8) = 0;
    (void)size;
    (void)server_role;
}
