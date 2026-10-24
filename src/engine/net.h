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
// Multiplayer: sends a base to the other players (NetDaemon_synch 0x13).
ADDR(0x593250) void GAMEAPI synch_base(int32_t base_id);
// Fills a FullChecksum of the game state (NetDaemon object NetState).
ADDR(0x52F8E0) void THISCALL NetDaemon_game_checksum(uint8_t* net, FullChecksum* out);
// Multiplayer: sends faction_id's research goal to the other players.
ADDR(0x5932B0) void GAMEAPI synch_researching(int32_t faction_id);
// Processes pending network messages (multiplayer wait loops).
ADDR(0x530360) void GAMEAPI NetDaemon_net_tasks();
// Multiplayer: sends a synchronisation message (does nothing in single player).
ADDR(0x532E00) void THISCALL NetDaemon_synch(uint8_t* net, int32_t type, int32_t a2, int32_t a3, int32_t a4,
                                            int32_t a5, int32_t a6, int32_t a7);
// Multiplayer: sends a data message (type, a2..a5) to the other players.
ADDR(0x592EE0) void GAMEAPI message_data(int32_t type, int32_t a2, int32_t a3, int32_t a4, int32_t a5, int32_t a6);
// Multiplayer: handles one received message; non-zero while messages remain.
ADDR(0x530320) int32_t THISCALL NetDaemon_receive(uint8_t* net);
