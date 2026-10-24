// Tamper detection, checksums and network wait loops (original module around 0x51E000-0x51F2E0).
//
// Not reconstructed here (network/UI): clear_all_player_messages (0x51E3C0), 0x51E480 and
// 0x51E530 (per-faction message lists of heap objects), help_spock/check_spock/wait_loop/wait_2
// (0x51F0C0-0x51F2C0, network waits). read_tamper/set_tamper (registry) are in ui_bridge.h.
#pragma once
#include "engine/globals.h"

// Hotseat/PBEM tamper records: (key_a, key_b, turn, faction, count, key_c), 50 entries in a ring.
GLOBAL(int32_t[50][6], TamperRecords, 0x93A060);
GLOBAL(int32_t, TamperIndex, 0x93A9F8);  // next record to write
GLOBAL(int32_t, TamperKeyA, 0x93A9AC);   // savegame identity (0: no checks)
GLOBAL(int32_t, TamperKeyB, 0x93A9B0);
GLOBAL(int32_t, TamperKeyC, 0x93A9B4);
GLOBAL(int32_t[8], TamperFaction, 0x93A9B8);  // per faction: player who reloaded the turn
GLOBAL(int32_t[8], TamperCount, 0x93A9D8);    // per faction: number of reloads

// Hotseat/PBEM: detects a savegame loaded twice for the same turn (registry records).
ADDR(0x51E110) void GAMEAPI check_tamper();
// Clears the tamper keys and the per-faction reload notes.
ADDR(0x51E390) void GAMEAPI clear_tamper();
// 0x51E6F0: logs "<value> <label>" with the value in hex (debug log).
ADDR(0x51E6F0) void GAMEAPI say_checksum(const char* label, int32_t value);
// Multiplayer debugging: logs the game checksums (stage 0 end of turn, 1 game start, 2/3 before/
// after world regeneration, 4 beginning of turn: also stored in MasterChecksum). Only with
// MultiDebugActive, except stage 4.
ADDR(0x51E760) void GAMEAPI do_checksums(int32_t stage);
