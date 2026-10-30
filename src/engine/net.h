// NetDaemon and synchronisation (original module around 0x52DBA0-0x538F30).
#pragma once
#include "engine/globals.h"

GLOBAL(uint8_t, DeleteList, 0x93E31C);  // DeletionList object (multiplayer)
// Multiplayer: records a deleted vehicle.
ADDR(0x52DE70) int32_t THISCALL DeletionList_add_deletion(uint8_t* list, int32_t veh_id);
// Recomputes the vehicle bits and owners of every tile from the vehicle table.
ADDR(0x532A90) void GAMEAPI rebuild_vehicle_bits();
// Recomputes the base bits and owners of every tile from the base table.
ADDR(0x532B70) void GAMEAPI rebuild_base_bits();
// Ends the network session (NetDaemon object NetState).
ADDR(0x52E050) void THISCALL NetDaemon_hang_up(uint8_t* net);
// Multiplayer: locks a base for editing (0 on success) / releases it.
ADDR(0x530D50) int32_t THISCALL NetDaemon_lock_base(uint8_t* net, int32_t base_id, int32_t a2, int32_t a3, int32_t a4);
ADDR(0x530E50) void THISCALL NetDaemon_unlock_base(uint8_t* net, int32_t base_id);
// Multiplayer session state cleared when an iron man game is saved (0x93E8C0-0x93E96C).
GLOBAL(int32_t[44], NetSessionState, 0x93E8C0);
// The sequence number of the session's events (the dword before NetSessionState; carried in every
// message header and reset with the session).
GLOBAL(int32_t, NetEventSequence, 0x93E8BC);
GLOBAL(uint8_t, NetLock, 0x93E230);  // Lock object (multiplayer)
// Multiplayer: sends faction_id's social choice to the other players.
ADDR(0x5933C0) void GAMEAPI synch_soc(int32_t faction_id);
// Multiplayer: sends faction_id's energy allocation to the other players.
ADDR(0x593390) void GAMEAPI synch_alloc(int32_t faction_id);
// Multiplayer: sends a base to the other players (NetDaemon_synch 0x13).
ADDR(0x593250) void GAMEAPI synch_base(int32_t base_id);
// Fills a FullChecksum of the game state (NetDaemon object NetState).
ADDR(0x52F8E0) void THISCALL NetDaemon_game_checksum(uint8_t* net, FullChecksum* out);
// Multiplayer: sends faction_id's research choice (synch_research, type 9) / blind research
// priorities (synch_ai, type 8) to the other players.
ADDR(0x593340) void GAMEAPI synch_research(int32_t faction_id);
ADDR(0x593310) void GAMEAPI synch_ai(int32_t faction_id);
// Multiplayer: sends prototype unit_id (type 0xE).
ADDR(0x5933F0) void GAMEAPI synch_proto(int32_t unit_id);
// Multiplayer: sends faction_id's research goal to the other players.
ADDR(0x5932B0) void GAMEAPI synch_researching(int32_t faction_id);
// Multiplayer: sends a faction's energy (NetDaemon_synch 0xB).
ADDR(0x593280) void GAMEAPI synch_energy(int32_t faction_id);
// Processes pending network messages (multiplayer wait loops).
ADDR(0x530360) void GAMEAPI NetDaemon_net_tasks();
// Multiplayer: asks the player of faction_id_with to open a diplomatic channel; 0 when opened.
ADDR(0x5304A0) int32_t THISCALL NetDaemon_open_channel(uint8_t* net, int32_t faction_id, int32_t faction_id_with);
// Multiplayer: per faction 1..7, a diplomatic channel the faction asked for (wait_loop opens it).
GLOBAL(int32_t[7], NetChannelRequests, 0x93F67C);
GLOBAL(int32_t, NetTurnDoneMask, 0x93E8EC);  // multiplayer: factions that ended their turn
// Multiplayer: sends a synchronisation message (does nothing in single player).
// type: the block (0 the game's settings, 2 the map's, 4 the map, 6 a faction, 0x11 a unit, 0x13 a
// base, 0x16 two factions' diplomacy, 0x80..0x8D the rules, ...); a2..a4 its arguments; msg the
// received message (send 0) or nullptr (send 1); msg_type the message's type (0x2101).
ADDR(0x532E00)
void THISCALL NetDaemon_synch(uint8_t* net, int32_t type, int32_t a2, int32_t a3, int32_t a4, int32_t* msg,
                              int32_t send, int32_t msg_type);
// Multiplayer: sends a data message (type, a2..a5) to the other players.
ADDR(0x592EE0) void GAMEAPI message_data(int32_t type, int32_t a2, int32_t a3, int32_t a4, int32_t a5, int32_t a6);
// Multiplayer: sends a chat line (type 0x1E00 / 0x4E00, the faction it goes to, the text).
ADDR(0x593190) void GAMEAPI message_chat(int32_t type, int32_t faction_id, const char* text);
// Multiplayer: sends a base-building message (type, veh_id, base name, a4) to the other players.
ADDR(0x592E70) void GAMEAPI message_base(int32_t type, int32_t veh_id, const char* name, int32_t a4);
// Multiplayer: locks the tile (x, y) for a move; 0 if the lock was obtained (always 0 in single player).
ADDR(0x530920) int32_t THISCALL NetDaemon_add_lock(uint8_t* net, int32_t a1, int32_t x, int32_t y);
GLOBAL(char[32], NetBaseName, 0x93FC30);  // base name buffer sent by enemy_move's BUILD BASE message
// Multiplayer: handles one received message; non-zero while messages remain.
ADDR(0x530320) int32_t THISCALL NetDaemon_receive(uint8_t* net);
// Multiplayer: performs veh_id's current order through the network daemon (1: unit done).
ADDR(0x532570) int32_t THISCALL NetDaemon_action(uint8_t* net, int32_t veh_id, int32_t a2);
// Multiplayer: takes the global (diplomatic) lock, processing messages while waiting; non-zero
// while it is not obtained (0 at once in single player) / releases it (nothing in single player).
ADDR(0x530AA0) int32_t THISCALL NetDaemon_add_global(uint8_t* net);
ADDR(0x530D00) void THISCALL NetDaemon_unlock(uint8_t* net);
// Multiplayer: closes the diplomacy channel (nothing in single player).
ADDR(0x530600) void THISCALL NetDaemon_close_channel(uint8_t* net);
// Multiplayer: sends the diplomatic state of two factions (NetDaemon_synch 0x16).
ADDR(0x593450) void GAMEAPI synch_diplo(int32_t faction_id, int32_t faction_id_with);
// Multiplayer: sends faction_id's leader state (council promises) to the other players.
ADDR(0x5932E0) void GAMEAPI synch_leader(int32_t faction_id);

// ---- the transport (Net_*, netlib 0x62D6A0..): DirectPlay in the original, OpenSMAC's TCP transport
// (platform/net_tcp) in native builds (engine/net_link.cpp). The NetDaemon object NetState begins
// with the Net object: +0x760 the own player id (pid), +0x764 the host's, and the AlphaNet player
// slots (1..7, 0x19C bytes each from +0x76C: the name, +0x20 the pid, +0x24 the faction).
// Net_send: msg (len bytes) to the player `to` (0: every other player; the own pid: to itself).
ADDR_NATIVE(0x630080) int32_t THISCALL Net_send(uint8_t* net, const void* msg, int32_t len, int32_t to, int32_t flags);
// Net_get: the next received message (its buffer, valid until the next Net_get), its sender and
// size; nullptr when none is waiting.
ADDR_NATIVE(0x630A00) uint8_t* THISCALL Net_get(uint8_t* net, int32_t* from, int32_t* size);
ADDR_NATIVE(0x631A20) const char* THISCALL Net_get_player_name(uint8_t* net, int32_t pid);
ADDR_NATIVE(0x62E010) void THISCALL Net_close(uint8_t* net);
// Net_handle_sys_msg: the session's events: a player joined (NetDaemon_on_player_added), a player
// left or the host was lost (NetDaemon_on_player_deleted). Called by the idle tasks.
ADDR_NATIVE(0x632DB0) void THISCALL Net_handle_sys_msg(uint8_t* net);
ADDR(0x52E160) void THISCALL NetDaemon_on_player_added(uint8_t* net, const char* name, int32_t pid);
ADDR(0x52E370) void THISCALL NetDaemon_on_player_deleted(uint8_t* net, int32_t a1, int32_t pid);
// OpenSMAC (not original): opens the TCP session as its host (port) or joins one (address, port);
// 0 on success, else a message is in *error. Net_close ends it.
int32_t net_link_host(uint8_t* net, uint16_t port, const char* name, char* error, uint32_t error_size);
int32_t net_link_join(uint8_t* net, const char* address, uint16_t port, const char* name, char* error,
                      uint32_t error_size);
// OpenSMAC: the transport's sockets pumped (non-blocking); called from the idle tasks.
void net_link_pump();
bool net_link_active();

// ---- the multiplayer locks (engine/net_lock.cpp; the Lock object NetLock, PlayerLock, SquareLock) ----
ADDR(0x58FD70) void THISCALL SquareLock_clear(uint8_t* sq);
ADDR(0x58FD90) void THISCALL SquareLock_unlock(uint8_t* sq, int32_t faction_id);
ADDR(0x58FE80) int32_t THISCALL SquareLock_lock(uint8_t* sq, int32_t faction_id, uint32_t flags, int32_t x, int32_t y);
ADDR(0x58FF70) void THISCALL PlayerLock_clear(uint8_t* pl);
ADDR(0x58FFA0) int32_t THISCALL PlayerLock_active(uint8_t* pl);
ADDR(0x58FFC0) void THISCALL PlayerLock_unlock(uint8_t* pl, int32_t faction_id);
ADDR(0x590000)
int32_t THISCALL PlayerLock_lock(uint8_t* pl, int32_t faction_id, uint32_t flags1, int32_t x1, int32_t y1,
                                 uint32_t flags2, int32_t x2, int32_t y2);
ADDR(0x5900A0) void THISCALL PlayerLock_add_lock(uint8_t* pl, int32_t faction_id, uint32_t flags, int32_t x, int32_t y);
ADDR(0x5900D0) void THISCALL Lock_clear(uint8_t* lock);
ADDR(0x590140) void THISCALL Lock_reset_map(uint8_t* lock);
ADDR(0x590170) void THISCALL Lock_unlock(uint8_t* lock, int32_t faction_id);
ADDR(0x5901D0) void THISCALL Lock_check_global(uint8_t* lock);
ADDR(0x590240) int32_t THISCALL Lock_check_global_2(uint8_t* lock, int32_t faction_id);
ADDR(0x5902C0) int32_t THISCALL Lock_global_lock(uint8_t* lock, int32_t faction_id);
ADDR(0x590300)
int32_t THISCALL Lock_lock(uint8_t* lock, int32_t faction_id, uint32_t flags1, int32_t x1, int32_t y1, uint32_t flags2,
                           int32_t x2, int32_t y2);
ADDR(0x590470) void THISCALL Lock_add_lock(uint8_t* lock, int32_t faction_id, uint32_t flags, int32_t x, int32_t y);
ADDR(0x5904A0) int32_t THISCALL Lock_any_locks(uint8_t* lock);
// The server of the moment (0x52DBA0): with one-at-a-time turns (GameMoreRules 0x10) outside the
// upkeep, the player whose turn it is; else the session's host (NetSessionState[0]).
ADDR(0x52DBA0) int32_t GAMEAPI current_server();
ADDR(0x4E25B0) void THISCALL AlphaNet_close(uint8_t* net);
ADDR(0x4E25E0) int32_t THISCALL AlphaNet_pid_2_idx(uint8_t* net, int32_t pid);
ADDR(0x4E2610) int32_t THISCALL AlphaNet_pid_2_who(uint8_t* net, int32_t pid);
ADDR(0x4E2660) int32_t THISCALL AlphaNet_who_2_pid(uint8_t* net, int32_t faction_id);
ADDR(0x4E26B0) int32_t THISCALL AlphaNet_who_2_idx(uint8_t* net, int32_t faction_id);

// ---- the daemon (engine/net_daemon.cpp, net_process.cpp) ----
ADDR(0x52DF30) int32_t THISCALL NetDaemon_init(uint8_t* net, int32_t lobby);
ADDR(0x52E900) void THISCALL NetDaemon_send_game(uint8_t* net);
ADDR(0x52F520) void THISCALL NetDaemon_send_rules(uint8_t* net);
ADDR(0x5303B0)
int32_t THISCALL NetDaemon_wait(uint8_t* net, const char* label, int32_t* flag, int32_t value, uint32_t popup_ms);
ADDR(0x530630)
int32_t THISCALL NetDaemon_lock(uint8_t* net, uint32_t flags1, int32_t x1, int32_t y1, uint32_t flags2, int32_t x2,
                                int32_t y2);
ADDR(0x531150) int32_t THISCALL NetDaemon_automated_lock(uint8_t* net, int32_t* veh_id, int32_t flags);
ADDR(0x5328C0) void THISCALL NetDaemon_end_of_event(uint8_t* net, int32_t faction_id, int32_t seq);
ADDR(0x532940) void THISCALL NetDaemon_send_message(uint8_t* net, void* msg, int32_t size, int32_t to);
ADDR(0x534330) int32_t THISCALL NetDaemon_fixup_message(uint8_t* net, uint8_t* msg);
ADDR(0x534400) void THISCALL NetDaemon_process_message(uint8_t* net, uint8_t* msg, int32_t size, int32_t from_pid);
ADDR(0x532C30) void GAMEAPI synch_display(int32_t type, int32_t chunk, int32_t chunks, int32_t a4);
ADDR(0x52DCA0) void THISCALL DeletionList_ctor(uint8_t* list);
ADDR(0x52DCD0) void THISCALL DeletionList_clear(uint8_t* list, int32_t i);
ADDR(0x52DCF0) int32_t THISCALL DeletionList_UNK1(uint8_t* list);
ADDR(0x52DD10) int32_t THISCALL DeletionList_UNK2(uint8_t* list, int32_t seq);
ADDR(0x52DD50) void THISCALL DeletionList_ack(uint8_t* list, int32_t seq, int32_t faction_id);
ADDR(0x52DDC0) int32_t THISCALL DeletionList_convert(uint8_t* list, int32_t veh_id, int32_t seq);
ADDR(0x52DBF0) int32_t GAMEAPI next_player_turn();
ADDR(0x592FF0) void GAMEAPI message_big_data(int32_t type, int32_t to, const int32_t* data, int32_t count);
ADDR(0x593090)
void GAMEAPI message_lock(int32_t type, int32_t a2, int32_t a3, int32_t a4, int32_t a5, int32_t a6, int32_t a7,
                          int32_t to);
ADDR(0x593110) void GAMEAPI message_landmark(int32_t type, int32_t x, int32_t y, const char* name, int32_t to);
ADDR(0x5934B0) void GAMEAPI synch_template(int32_t faction_id);
ADDR(0x5934E0) void GAMEAPI synch_radius(int32_t base_id);
ADDR(0x51F0C0) void GAMEAPI help_spock();
ADDR(0x51F2C0) void GAMEAPI wait_2();
// NetDaemon_send_files (0x52ED40, engine/net_files.cpp): a scenario's files sent to the players
// (multiplayer_init, NetSetupType 3); non-zero when cancelled (Esc).
ADDR_NATIVE(0x52ED40) int32_t THISCALL NetDaemon_send_files(uint8_t* net);
// process_message's file transfer messages (0x0F05..0x0F0C, 0x0F0F; engine/net_files.cpp).
void net_files_message(uint8_t* net, uint8_t* msg, int32_t size, int32_t from);
// The session state cleared when a network game ends (inlined after NetDaemon_hang_up).
void net_session_clear();
// The setup window's packets (UI module 0x47B020..0x47B360, protocol only): the faction data (0x0F0D),
// every player's settings (0x0F04), the session's settings (0x0F02 NetConfigValue, 0x0F03
// NetSetupType's block) to a player index (< 0: everyone).
ADDR(0x47B020) void GAMEAPI send_faction_data(int32_t idx);
// OpenSMAC: the host's original-bugs setting to every player (0x0F10; net_process applies it).
void net_send_original_bugs();
ADDR(0x47B0F0) void GAMEAPI send_all_player_packets(int32_t idx);
ADDR(0x47B260) void GAMEAPI send_session_packet(int32_t a1, int32_t idx);
ADDR(0x47B2E0) void GAMEAPI send_session2_packet(int32_t a1, int32_t idx);
// AlphaNet_setup (0x4E31E0, UI): the service list, host or join; 0 when a session was set up.
ADDR_NATIVE(0x4E31E0) int32_t THISCALL AlphaNet_setup(uint8_t* net, int32_t lobby);

// ---- the network game's windows (UI: NetWin, MultiWin, the chat lines; headless: nothing) ----
bool ui_netwin_visible();
void ui_netwin_redraw();
void ui_netwin_cancel();                                   // NetWin_on_button_clicked(-2)
void ui_netwin_fill_player(uint8_t* player, int32_t faction);  // NetWin_fill_player
void ui_netwin_faction_data(const uint8_t* msg);           // NetWin_setup_faction_data(msg)
void ui_netwin_pick_pass(int32_t a1);                      // NetWin_pick_pass
void ui_netwin_chat(int32_t idx, const char* text);        // NetWin_new_chat
void ui_main_chat(const char* text, int32_t faction_id);   // MainInterface_chat
void ui_diplo_chat(const char* text, int32_t faction_id);  // the DiploWin's chat line (FUN_004412F0)
// The DiploWin's side of the diplomatic messages (ui/diplo_hotseat.cpp): op 0x1506 their offer added
// (a type, b value), 0x1507 removed (a its index), 0x1508 cleared, 0x1509 their state (a),
// 0x150B the deal done (FUN_00442740), 0x150C the states (a theirs, b ours), 0x150D done
// (FUN_004429C0); queries: 0 the window shown, 1 our state, 2 theirs.
int32_t ui_diplo_net(int32_t op, int32_t a, int32_t b);
void ui_diplo_closed(int32_t faction_id);                  // DIPLOCLOSE: the channel closed
void ui_multiwin_draw();                                   // MultiWin_draw(0)
const uint8_t* ui_netwin_faction_blob();                   // the NetWin's faction data (+0x772C)
bool net_trace_enabled();
const char* ui_user_dir();                                 // OpenSMAC: the per-user directory (received files)
bool ui_escape_pressed();                                  // GetAsyncKeyState(VK_ESCAPE) high bit
void ui_net_close_windows();                               // net_upkeep: GraphicWin_close
void ui_console_set_ai(int32_t faction_id);                // Console_set_ai
void ui_console_set_view();                                // Console_set_view(MainConsole, 0)
void ui_net_end_of_turn();                                 // BaseWin_exit (when shown), go_reset
void ui_net_center(int32_t x, int32_t y);                  // pfncMoveMap + Console_set_cursor
void ui_net_clear_messages();                              // MessageWin_clear
void ui_net_turn(const char* stage);                       // OpenSMAC: a trace line for the tests
void ui_net_note_text(const char* text);                   // OpenSMAC: a note (stderr; WAITFOR in tests)                                  // OPENSMAC_NETTRACE: every message logged

// ---- the network turn (engine/net_turn.cpp) ----
ADDR(0x523C60) void GAMEAPI set_time_controls();
ADDR(0x528EA0) int32_t GAMEAPI net_upkeep_phase(int32_t phase);
ADDR(0x5290A0) void GAMEAPI net_upkeep();
ADDR(0x529E70) void GAMEAPI net_not_my_turn();
ADDR(0x529C00) void GAMEAPI mash_planes();
ADDR(0x529F30) void GAMEAPI net_end_of_turn();
// The pending tasks run once (UI: timers, the session's events, redraws; headless: nothing).
ADDR_NATIVE(0x5FC8F0) void GAMEAPI do_all_tasks();
// The time stamp the server gives a relayed event (its seed): timeGetTime, or OpenSMAC's deterministic counter.
uint32_t net_stamp();
