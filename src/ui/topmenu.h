// The opening screens (UI_PLAN.md phase 9): the main menu (top_menu, game module 0x58E360), its
// sub-menus and the game setup (map_menu, size_of_planet, custom_planet, config_game with the
// faction picker PickWin), the credits (show_credits) and the windows they run in: the desktop
// (TopMenuWin, the opening picture: change_opening / close_opening) and the menu column at the
// bottom right (SetupWin, ui 0x4AD880..0x4AE9A0: SetupWin_do_menu_rightside and its handlers).
//
// The game state changes are the original's: every choice goes through the same globals, the
// preferences (prefs_save) and the engine's setup code (map_init, world_build, setup_game,
// read_faction, alien_start) in the original's order, with the original's game_rand draws (random
// map parameters, random rival factions).
#pragma once
#include <cstdint>

#include "engine/memmap.h"
#include "ui/popup.h"

// ---- game module (0x588F90..0x58EE60) ----
// The map menu (MAPMENU: random map, custom map, the maps of Planet, a map file); quick: QUICK
// START (the last choices, no dialogs). 0: a game was set up; 1: back to the main menu.
ADDR_NATIVE(0x58D6B0) int32_t GAMEAPI map_menu(int32_t quick);
// SELECT SIZE OF PLANET (mode 0: the menu column, 1: the last size without asking, 2: a popup).
// Sets MapSizePlanet, MapAreaX/Y and (mode < 2) map_init / Path_init. 0: done; 1: cancelled.
ADDR_NATIVE(0x58CED0) int32_t GAMEAPI size_of_planet(int32_t mode);
// The custom map's ocean, erosion, native life and cloud cover (setup_win: the menu column with the
// planet pictures, else popups; no_dialogs: the last choices). 0: done; 1: cancelled.
ADDR_NATIVE(0x58C2A0) int32_t GAMEAPI custom_planet(int32_t setup_win, int32_t no_dialogs);
// Difficulty, rules, the factions (PickWin), the leader's name, then the world and the factions
// (world_build, setup_game, alien_start) and PLANETFALL. mode: 0 new game, 1 quick start (the last
// choices), 2 scenario (only the player's faction), 3 new scenario. 0: done; 1: cancelled.
ADDR_NATIVE(0x589D30) int32_t GAMEAPI config_game(uint32_t mode);
// The PLANETFALL message of faction_id (PLANETFALLX / PLANETFALLF / PLANETFALL2 after turn 0).
ADDR_NATIVE(0x589180) void GAMEAPI planetfall(int32_t faction_id);
// The faction picker's text box (BLURB or DATALINKS1/2 of the faction's file with its portrait).
ADDR_NATIVE(0x5898A0) void GAMEAPI thumb_routine(int32_t faction_id);
// The desktop's picture (TopMenuWin; created on first use).
ADDR_NATIVE(0x589A30) int32_t GAMEAPI change_opening(const char* pcx);

// ---- SetupWin (ui 0x4AD880..): the menu column of the opening screens ----
// SetupWin_do_menu_2: the items, caption and initial selection of the started popup `pop`; returns
// the chosen item's id, -1 when cancelled (Esc).
int32_t setupwin_do_menu_2(BasePop* pop);
// SetupWin_do_menu: the section `label` of the main script read directly (#caption, items); returns
// the chosen position, -1 when cancelled.
int32_t setupwin_do_menu(const char* label, int32_t active);

// ---- harness hooks (not original) ----
// Called by config_game right before the world is built (world_build / setup_game): OpenSMAC reseeds
// the generators there (--seed in a game set up from the menu, as the harness's seed command
// before newgame).
extern void (*g_ui_before_world_build)();
// The script's state for the tests (MENUSTATE): the open menu column's caption, items and selection.
void topmenu_log_state();
