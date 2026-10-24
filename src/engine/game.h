// Game setup and top-level flow (original module game, 0x5882F0-0x590510).
#pragma once
#include "engine/globals.h"

GLOBAL(int32_t, GameNotStarted, 0x68F21C);  // pfGameNotStarted (= GameHalted)
GLOBAL(int32_t, dword_9469FC, 0x9469FC);   // cleared by WinMain

// New game, Alien Crossfire: unless the viewer plays an alien or the Cult of Planet, removes the
// computer-controlled aliens and Cult (bases, units, alive bit).
ADDR(0x588F90) void GAMEAPI alien_start();
// Creates the save directories, loads labels, the interface (windows, sprites, fonts). Returns 0
// on success.
ADDR(0x58F040) int32_t GAMEAPI system_init();
ADDR(0x58F250) void GAMEAPI system_close();
// Reseeds the random generators from the clock, optionally reloads the text tables (init_text),
// loads the sprites and the rules (read_rules(init_rules)). Returns 0 on success.
ADDR(0x58F2F0) int32_t GAMEAPI game_init(int32_t init_text, int32_t init_rules);
ADDR(0x58F430) void GAMEAPI game_close();
// game_init after closing the game windows (close_windows) and the text tables. Returns 0 on
// success.
ADDR(0x58F450) int32_t GAMEAPI game_reload(int32_t close_windows, int32_t init_rules);

// Native builds: run the original's static initializers the engine depends on (CRT startup)
// and the state WinMain (0x45F950) sets before control_game. ini_path: preferences file.
void engine_static_init(const char* ini_path);
