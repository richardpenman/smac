// The network layer's entry points in a single player game (engine/net_daemon.cpp): each returns as
// the original does without a network game, none reaches an unreconstructed multiplayer path
// (synch_veh once aborted smacx when an artifact was linked to a network node).
#include "engine/action.h"
#include "engine/net.h"
#include "engine/net_misc.h"
#include "engine/probe.h"
#include "engine/ui_bridge.h"
#include "unit.h"

TEST_CASE("net: single player paths return without a network game") {
    SaveGlobal<int32_t> mp{MultiplayerActive};
    SaveGlobal<int32_t[44]> session{NetSessionState};
    MultiplayerActive = 0;
    NetSessionState[0] = 0;
    NetSessionState[0x1F] = 0;
    synch_veh(1);
    synch_base(2);
    synch_energy(1);
    synch_researching(1);
    synch_leader(1);
    synch_diplo(1, 2);
    message_veh(0x2011, 3, 0, 0);
    message_base(0x2412, 3, "Base", 0);
    message_data(0x2411, 0, 1, 2, 3, 0);
    check_spock();
    NetDaemon_hang_up(&NetState);
    CHECK(NetDaemon_open_channel(&NetState, 1, 2) == 0);
    NetDaemon_close_channel(&NetState);
    CHECK(NetDaemon_add_lock(&NetState, 0, 1, 1) == 0);
    CHECK(NetDaemon_add_global(&NetState) == 0);
    NetDaemon_unlock(&NetState);
    CHECK(NetDaemon_lock_base(&NetState, 0, 0, -1, -1) == 0);
    NetDaemon_unlock_base(&NetState, 0);
    int32_t veh = 3;
    CHECK(NetDaemon_lock_veh(&NetState, &veh, 0, -1, -1, 0) == 0);
    CHECK(NetDaemon_maybe_lock(&NetState, &veh, 1, 0, 0, 0, 4) == 0);
    CHECK(NetDaemon_maybe_lock(&NetState, &veh, 0, 0, 0, 0, 4) == 0);
    NetDaemon_await_exec(&NetState, 0);
    NetDaemon_await_synch(&NetState);
    NetDaemon_await_diplo(&NetState, 1);
    // NetDaemon_unlock_veh clears the locked unit (+0x1B78) and +0x1BC4 in any game.
    int32_t* locked = reinterpret_cast<int32_t*>(&NetState + 0x1B78);
    int32_t* flag = reinterpret_cast<int32_t*>(&NetState + 0x1BC4);
    *locked = 5;
    *flag = 1;
    NetDaemon_unlock_veh(&NetState);
    CHECK(*locked == 0);
    CHECK(*flag == 0);
}
