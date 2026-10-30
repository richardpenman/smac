// The diplomacy menus built on Popup objects on the original functions' stacks
// (engine/ui_bridge.h, ui_diplo_menu_start ...), shown only to a human player. Headless: nothing
// (OpenSMAC defines them in src/ui/popup_menus.cpp). exec returns
// what the oracle's popups return (BasePop_exec_3's no-display path: 0).
#include "engine/ui_bridge.h"

void ui_diplo_menu_start(const char*, int32_t) {}
void ui_diplo_menu_close() {}
void ui_diplo_menu_item(const char*, int32_t) {}
int32_t ui_diplo_menu_exec(int32_t) { return 0; }
