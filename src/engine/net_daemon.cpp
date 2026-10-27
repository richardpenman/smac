// The multiplayer network layer's entry points (NetDaemon, the synch_* / message_* senders,
// check_spock), as far as a single player game reaches them: each tests what the original tests
// (MultiplayerActive 0x93F660, or the session flag 0x93E8C0) and returns as the original does
// without a network game. The network paths themselves (locks, channels, message traffic) are not
// reconstructed: reaching one aborts with net_unimplemented. The purely multiplayer functions
// (net_control_turn, DeletionList_add_deletion, NetDaemon_game_checksum, NetDaemon_receive,
// NetDaemon_net_tasks) are only called under MultiplayerActive and keep their generated stubs.
#include <cstdio>
#include <cstdlib>

#include "engine/action.h"
#include "engine/globals.h"
#include "engine/log.h"
#include "engine/memmap.h"
#include "engine/move.h"
#include "engine/net.h"
#include "engine/net_misc.h"
#include "engine/probe.h"
#include "engine/ui_bridge.h"
#include "engine/unimplemented.h"

namespace {
[[noreturn]] void net_unimplemented(const char* name, uint32_t addr) {
    std::fprintf(stderr, "smacx: multiplayer path of %s (0x%06X) is not reconstructed\n", name, addr);
    smac_unimplemented(name, addr);
}
int32_t& fld(uint8_t* net, uint32_t off) { return *reinterpret_cast<int32_t*>(net + off); }
}  // namespace

// ---- NetDaemon_synch (0x532E00) and its senders: nothing without a network game ----

void THISCALL NetDaemon_synch(uint8_t*, int32_t, int32_t, int32_t, int32_t, int32_t, int32_t, int32_t) {
    if (!MultiplayerActive) return;
    net_unimplemented("NetDaemon_synch", 0x532E00);
}

void GAMEAPI synch_veh(int32_t veh_id) { NetDaemon_synch(&NetState, 0x11, veh_id, 0, 0, 0, 1, 0x2101); }
void GAMEAPI synch_base(int32_t base_id) { NetDaemon_synch(&NetState, 0x13, base_id, 0, 0, 0, 1, 0x2101); }
void GAMEAPI synch_energy(int32_t faction_id) { NetDaemon_synch(&NetState, 0xB, faction_id, 0, 0, 0, 1, 0x2101); }
void GAMEAPI synch_researching(int32_t faction_id) {
    NetDaemon_synch(&NetState, 10, faction_id, 0, 0, 0, 1, 0x2101);
}
void GAMEAPI synch_leader(int32_t faction_id) { NetDaemon_synch(&NetState, 6, faction_id, 0, 0, 0, 1, 0x2101); }
void GAMEAPI synch_diplo(int32_t faction_id, int32_t faction_id_with) {
    NetDaemon_synch(&NetState, 0x16, faction_id, faction_id_with, 0, 0, 1, 0x2101);
}

// message_veh / message_base / message_data (0x592E10 / 0x592E70 / 0x592EE0): a message (its type,
// the sender, timeGetTime, the session's turn 0x93E8BC, the arguments) to NetDaemon_send_message,
// which sends nothing without a network game.
void GAMEAPI message_veh(int32_t, int32_t, int32_t, int32_t) {
    if (!MultiplayerActive) return;
    net_unimplemented("message_veh", 0x592E10);
}
void GAMEAPI message_base(int32_t, int32_t, const char*, int32_t) {
    if (!MultiplayerActive) return;
    net_unimplemented("message_base", 0x592E70);
}
void GAMEAPI message_data(int32_t, int32_t, int32_t, int32_t, int32_t, int32_t) {
    if (!MultiplayerActive) return;
    net_unimplemented("message_data", 0x592EE0);
}

// check_spock (0x51F1B0): with the session's lock state (0x93E8C0, 0x93F670) outside the upkeep and
// the computer players' turns, help_spock when no lock is pending.
void GAMEAPI check_spock() {
    if (NetSessionState[0] && *reinterpret_cast<int32_t*>(mem::host(0x93F670)) && !ControlUpkeepA && !ControlTurnC)
        net_unimplemented("check_spock", 0x51F1B0);
}

// ---- NetDaemon (NetState 0x93CD90): locks, channels and remote execution ----

void THISCALL NetDaemon_hang_up(uint8_t* net) {
    if (fld(net, 0x1B3C) || !MultiplayerActive) return;
    net_unimplemented("NetDaemon_hang_up", 0x52E050);
}

int32_t THISCALL NetDaemon_open_channel(uint8_t*, int32_t, int32_t) {
    if (!MultiplayerActive) return 0;
    net_unimplemented("NetDaemon_open_channel", 0x5304A0);
}

void THISCALL NetDaemon_close_channel(uint8_t*) {
    if (!MultiplayerActive) return;
    net_unimplemented("NetDaemon_close_channel", 0x530600);
}

int32_t THISCALL NetDaemon_add_lock(uint8_t*, int32_t, int32_t, int32_t) {
    if (!MultiplayerActive) return 0;
    net_unimplemented("NetDaemon_add_lock", 0x530920);
}

int32_t THISCALL NetDaemon_add_global(uint8_t*) {
    if (!MultiplayerActive) return 0;
    net_unimplemented("NetDaemon_add_global", 0x530AA0);
}

void THISCALL NetDaemon_unlock(uint8_t*) {
    if (!MultiplayerActive) return;
    net_unimplemented("NetDaemon_unlock", 0x530D00);
}

// NetDaemon_lock_base (0x530D50): logged ("LockBase"), 0 without a network game.
int32_t THISCALL NetDaemon_lock_base(uint8_t*, int32_t, int32_t, int32_t, int32_t) {
    log_say_values("LockBase", ControlUpkeepA, ControlTurnC, CurrentBaseID);
    if (!MultiplayerActive) return 0;
    net_unimplemented("NetDaemon_lock_base", 0x530D50);
}

// NetDaemon_unlock_base (0x530E50): logged ("UnlockBase"); the synch and the release only in a
// network game with the session's base lock (0x93E93C).
void THISCALL NetDaemon_unlock_base(uint8_t*, int32_t base_id) {
    log_say_values("UnlockBase", NetSessionState[0x1F], base_id, 0);
    if (!MultiplayerActive || !NetSessionState[0x1F]) return;
    net_unimplemented("NetDaemon_unlock_base", 0x530E50);
}

int32_t THISCALL NetDaemon_lock_veh(uint8_t*, int32_t*, int32_t, int32_t, int32_t, int32_t) {
    if (!MultiplayerActive) return 0;
    net_unimplemented("NetDaemon_lock_veh", 0x531020);
}

// NetDaemon_unlock_veh (0x5310F0): the release message in a network game; always the locked unit
// pointer (+0x1B78) and +0x1BC4 cleared.
void THISCALL NetDaemon_unlock_veh(uint8_t* net) {
    if (MultiplayerActive) net_unimplemented("NetDaemon_unlock_veh", 0x5310F0);
    fld(net, 0x1B78) = 0;
    fld(net, 0x1BC4) = 0;
}

void THISCALL NetDaemon_await_exec(uint8_t*, int32_t) {
    if (!MultiplayerActive) return;
    net_unimplemented("NetDaemon_await_exec", 0x531300);
}

void THISCALL NetDaemon_await_synch(uint8_t*) {
    if (!MultiplayerActive) return;
    net_unimplemented("NetDaemon_await_synch", 0x531480);
}

void THISCALL NetDaemon_await_diplo(uint8_t*, int32_t) {
    if (!MultiplayerActive) return;
    net_unimplemented("NetDaemon_await_diplo", 0x5315C0);
}

// NetDaemon_maybe_lock (0x531700): 0 when a2 is set; 1 (MOVEPENDING after a second try) while a
// lock is held (+0x1BAC: only in a network game); else NetDaemon_lock_veh with a3 | 4.
int32_t THISCALL NetDaemon_maybe_lock(uint8_t* net, int32_t* veh_id, int32_t a2, int32_t a3, int32_t a4, int32_t a5,
                                      int32_t a6) {
    if (a2) return 0;
    if (fld(net, 0x1BAC)) net_unimplemented("NetDaemon_maybe_lock", 0x531700);
    fld(net, 0x1BCC) = 0;
    return NetDaemon_lock_veh(net, veh_id, a3 | 4, a4, a5, a6);
}

// NetDaemon_order_veh (0x531780): order_veh(veh_id, dir, 3) without a network game.
int32_t THISCALL NetDaemon_order_veh(uint8_t*, int32_t veh_id, int32_t dir, int32_t) {
    if (!MultiplayerActive) return order_veh(veh_id, dir, 3);
    net_unimplemented("NetDaemon_order_veh", 0x531780);
}

// NetDaemon_action (0x532570): action(veh_id) without a network game.
int32_t THISCALL NetDaemon_action(uint8_t*, int32_t veh_id, int32_t) {
    if (!MultiplayerActive) return action(veh_id);
    net_unimplemented("NetDaemon_action", 0x532570);
}
