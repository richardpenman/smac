// Headless UI area "topmenu": the opening screens (engine/ui_bridge.h): the main menu, the credits,
// the multiplayer setup and the opening picture's window.
// Replaced in smacx by src/ui/topmenu.cpp (and gamesetup.cpp).
// Native builds only: hybrid builds always call the original UI.
#ifndef SMACX_HYBRID
#include "engine/ui_bridge.h"

// The script runner of the headless app takes the place of the main menu (see g_harness_top_menu);
// without it the game quits.
int32_t GAMEAPI top_menu(int32_t) { return 1; }
void GAMEAPI show_credits() {}
void GAMEAPI close_opening() {}
int32_t GAMEAPI multiplayer_init(int32_t) { return 1; }
#endif
