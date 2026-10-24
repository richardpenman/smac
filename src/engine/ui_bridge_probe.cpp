// The probe menus (engine/ui_bridge.h, ui_probe_*): Popup objects on the original probe()'s stack,
// shown only to a human player. Hybrid builds and headless: not shown, the cancelled value.
#include "engine/ui_bridge.h"

int32_t ui_probe_menu(int32_t, uint32_t, bool) { return -1; }
int32_t ui_probe_sabotage_menu(int32_t, bool, const char*) { return -1; }
int32_t ui_probe_confirm_menu(const char*, const char*, bool) { return 0; }
int32_t ui_probe_frame_menu(int32_t, int32_t) { return 0; }
int32_t ui_probe_free_menu(int32_t, bool) { return -1; }
int32_t ui_probe_excuse(const char*, int32_t, bool) { return 0; }
