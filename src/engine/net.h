// NetDaemon and synchronisation (original module around 0x52DBA0-0x538F30).
#pragma once
#include "engine/globals.h"

GLOBAL(uint8_t, DeleteList, 0x93E31C);  // DeletionList object (multiplayer)
// Multiplayer: records a deleted vehicle.
ADDR(0x52DE70) void THISCALL DeletionList_add_deletion(uint8_t* list, int32_t veh_id);
// Recomputes the vehicle bits and owners of every tile from the vehicle table.
ADDR(0x532A90) void GAMEAPI rebuild_vehicle_bits();
// Recomputes the base bits and owners of every tile from the base table.
ADDR(0x532B70) void GAMEAPI rebuild_base_bits();
// Ends the network session (NetDaemon object NetState).
ADDR(0x52E050) void THISCALL NetDaemon_hang_up(uint8_t* net);
// Multiplayer: locks a base for editing (0 on success) / releases it.
ADDR(0x530D50) int32_t GAMEAPI NetDaemon_lock_base(int32_t base_id, int32_t a2, int32_t a3, int32_t a4);
ADDR(0x530E50) void GAMEAPI NetDaemon_unlock_base(int32_t base_id);
// Multiplayer session state cleared when an iron man game is saved (0x93E8C0-0x93E96C).
GLOBAL(int32_t[44], NetSessionState, 0x93E8C0);
GLOBAL(uint8_t, NetLock, 0x93E230);  // Lock object (multiplayer)
// Multiplayer: sends faction_id's social choice to the other players.
ADDR(0x5933C0) void GAMEAPI synch_soc(int32_t faction_id);
// Multiplayer: sends faction_id's energy allocation to the other players.
ADDR(0x593390) void GAMEAPI synch_alloc(int32_t faction_id);
// Multiplayer: sends a base to the other players (NetDaemon_synch 0x13).
ADDR(0x593250) void GAMEAPI synch_base(int32_t base_id);
// Fills a FullChecksum of the game state (NetDaemon object NetState).
ADDR(0x52F8E0) void THISCALL NetDaemon_game_checksum(uint8_t* net, FullChecksum* out);
// Multiplayer: sends faction_id's research goal to the other players.
ADDR(0x5932B0) void GAMEAPI synch_researching(int32_t faction_id);
// Processes pending network messages (multiplayer wait loops).
ADDR(0x530360) void GAMEAPI NetDaemon_net_tasks();
// Multiplayer: asks the player of faction_id_with to open a diplomatic channel; 0 when opened.
ADDR(0x5304A0) int32_t THISCALL NetDaemon_open_channel(uint8_t* net, int32_t faction_id, int32_t faction_id_with);
// Multiplayer: per faction 1..7, a diplomatic channel the faction asked for (wait_loop opens it).
GLOBAL(int32_t[7], NetChannelRequests, 0x93F67C);
GLOBAL(int32_t, NetTurnDoneMask, 0x93E8EC);  // multiplayer: factions that ended their turn
// Multiplayer: sends a synchronisation message (does nothing in single player).
ADDR(0x532E00) void THISCALL NetDaemon_synch(uint8_t* net, int32_t type, int32_t a2, int32_t a3, int32_t a4,
                                            int32_t a5, int32_t a6, int32_t a7);
// Multiplayer: sends a data message (type, a2..a5) to the other players.
ADDR(0x592EE0) void GAMEAPI message_data(int32_t type, int32_t a2, int32_t a3, int32_t a4, int32_t a5, int32_t a6);
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
