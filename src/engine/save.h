// Saving and loading (original module save, 0x5A66E0-0x5ABFF0).
//
// Savegame layout (version 12, written by save_daemon / game_data):
//   "TERRAN\0" 0x1A, int32 0x56, the legacy preferences block (0x31C bytes, see game_data),
//   GamePrefs (0x398), [encrypted: GamePrefs, DiploPlrList, NetPlayerListSeq, TimeControl],
//   CurrentPlayerFaction, Factions, MFactions, FactionRankingsUnk, the map (map_write),
//   FactionTurnMight block, ReplayEventSize + ReplayEvents, Continents, Units, BungList, Vehs,
//   Bases, [scenario: ScenarioFileName], GameAltPath, scenario objectives, Monuments,
//   TectonicDetonationCount, main view state, map window state (game_io) of the 8 map windows and
//   the world window, [PBEM: diplomacy/council state and message lists], canvas size, and the
//   save_daemon time stamp. With GameState & 0x10000000 the blocks after GamePrefs (except the
//   map, the message lists and the canvas size) are encrypted (encrypt_write).
#pragma once
#include "engine/crt.h"
#include "engine/globals.h"

// Savegame version written by save_daemon (GamePrefs.SaveFileVersion).
constexpr int32_t kSaveFileVersion = 12;

// ---- globals (save module and blocks only the save module names) ----
GLOBAL(int32_t, NetPlayerListSeq, 0x93D4F8);  // multiplayer copy of NetDiploSeq
GLOBAL(int32_t[8][103], NetPlayerList, 0x93D4FC);  // multiplayer copy of DiploPlrList
GLOBAL(int32_t, NetDiploSeq, 0x93F798);  // dword_93F798
GLOBAL(char[256], ScenarioFileName, 0x9B2178);  // unk_9B2178 (scenarios only)
GLOBAL(char[256], GameAltPath, 0x9B2078);  // unk_9B2078: alternate data directory
// Zero-initialised 4-byte strings (empty) used as defaults: unk_945D80 (GameAltPath of version 0
// savegames), 0x945D84 (scenario name), 0x945D88 (last save name).
GLOBAL(char[4], EmptyPathA, 0x945D80);
GLOBAL(char[4], EmptyPathB, 0x945D84);
GLOBAL(char[4], EmptyPathC, 0x945D88);
GLOBAL(int32_t, GameAltPathChanged, 0x9B2074);  // dword_9B2074 (load_game)
GLOBAL(int32_t, UndoIndex, 0x690D7C);  // scenario editor undo slot (1-9)
GLOBAL(uint8_t[0x3E80], FactionTurnMightBlock, 0x9A68AC);  // FactionTurnMight and following data
GLOBAL(int32_t[4], CouncilWinState, 0x6FF69C);
GLOBAL(int32_t[8], CouncilWinVotesA, 0x6FF6D4);
GLOBAL(int32_t[8], CouncilWinVotesB, 0x6FF6F4);

// ---- savegame I/O primitives ----
// Writes count items of size bytes, obfuscated: byte pairs are swapped and xored with two running
// keys (0x81, 0x82, ... and 0x7F, 0x7E, ...) unless the byte or the result is 0. Odd sizes are
// padded to an even length. Returns fwrite's result (0 on failure).
ADDR(0x5A66E0) int32_t GAMEAPI encrypt_write(const void* buf, uint32_t size, uint32_t count, CrtFile* file);
// Reads data written by encrypt_write. Returns 1, or 0 on failure.
ADDR(0x5A67B0) int32_t GAMEAPI encrypt_read(void* buf, uint32_t size, uint32_t count, CrtFile* file);
// Reads/writes the saved view state of a map window (6 fields). Returns 1 on failure.
ADDR(0x5A6890) int32_t GAMEAPI game_io(uint8_t* win, CrtFile* file);
// Reads (write == 0) or writes the game state; also converts older savegame versions.
// Returns 0 on success, 1 on failure (SaveFileStatus = 1: game in progress on another seat).
ADDR(0x5A6AC0) int32_t GAMEAPI game_data(CrtFile* file, int32_t write);
// Reads/writes a map file (map only), or the map of a savegame (is_save != 0). Returns 0 on
// success, 1 on failure.
ADDR(0x5A9280) int32_t GAMEAPI map_data(CrtFile* file, int32_t write, int32_t is_save);

// ---- savegames ----
// Writes a savegame (".SAV" appended when the name has no extension). Returns 0 on success,
// 1 cannot create, 2 header failed, 3 game data failed (the partial file is deleted).
ADDR(0x5A94F0) int32_t GAMEAPI save_daemon(const char* filename);
// Sets PFLAG_MAP_REVEALED for factions with satellites or a map-revealing tech.
ADDR(0x5A96D0) void GAMEAPI see_map_check();
// Loads a savegame (relinearize: recompute the altitude thresholds). Returns 0 on success,
// 1 cannot open, 2 not a savegame, 3 too old, 4 read failure, 5 the game is in progress on
// another seat (hotseat/PBEM).
ADDR(0x5A9760) int32_t GAMEAPI load_daemon(const char* filename, int32_t relinearize);
// Writes a map file (".MP" appended when the name has no extension). Same codes as save_daemon.
ADDR(0x5A9B20) int32_t GAMEAPI save_map_daemon(const char* filename);
// Loads the map of a map file or of a savegame (".SAV" extension). Same codes as load_daemon
// (1-4).
ADDR(0x5A9C10) int32_t GAMEAPI load_map_daemon(const char* filename);
// Replaces each stand-alone group of exactly 4 digits in name with the current game year.
ADDR(0x5A9DB0) void GAMEAPI yearmotize(char* name);
// Deletes the first autosave file.
ADDR(0x5ABD10) void GAMEAPI kill_auto_save();
// Writes the autosave (rotating the autosave files every turn, and every 10 turns).
ADDR(0x5ABD20) void GAMEAPI auto_save();
// Scenario editor undo/redo (direction < 0 undo, > 0 redo): loads the undo slot.
ADDR(0x5ABE40) void GAMEAPI load_undo(int32_t direction);
// Deletes the scenario editor undo files.
ADDR(0x5ABEC0) void GAMEAPI wipe_undo();
// Scenario editor: rotates the undo files and saves the current state as undo slot 1.
ADDR(0x5ABF20) void GAMEAPI auto_undo();

// ---- file dialogs (UI shells around the daemons: hybrid builds always call the original; the
// native reconstruction runs the dialogs through engine/ui_bridge.h, headless: cancelled) ----
// "Save game" (scenario != 0: save scenario) dialog and save. Returns 0 when saved, 1 otherwise.
ADDR_NATIVE(0x5A9EB0) int32_t GAMEAPI save_game(int32_t scenario);
// "Load game" (scenario != 0: load scenario) dialog and load_daemon(path, relinearize). Returns
// load_daemon's result, 1 when cancelled, 5 when the game is in progress on another seat.
ADDR_NATIVE(0x5AAAB0) int32_t GAMEAPI load_game(int32_t scenario, int32_t relinearize);
// "Load map" dialog and load_map_daemon. Returns its result, 1 when cancelled.
ADDR_NATIVE(0x5AB600) int32_t GAMEAPI load_map();
// Scenario editor "save map" dialog and save_map_daemon.
ADDR_NATIVE(0x5ABB90) void GAMEAPI save_map();

// ---- callees of other modules that only the save module uses ----
// Resets every monument record (ui module, 0x476B30; engine state only).
ADDR(0x476B30) void GAMEAPI clear_monuments();
