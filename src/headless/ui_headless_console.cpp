// Headless UI area "console": the Console functions the engine calls (engine/ui_bridge.h). No human
// player is ever interactive headless. Replaced in OpenSMAC by src/ui/console.cpp.
#include "engine/ui_bridge.h"

// Returns at once (also the observer's "turn complete" pause).
int32_t THISCALL Console_human_turn(uint8_t*) { return 0; }
// Centers the map view; returns the original's "view changed" flag (0: nothing to redraw).
int32_t THISCALL Console_focus(uint8_t*, int32_t, int32_t, int32_t) { return 0; }
// No unit of the viewer is ever selected (0: "found one", so callers make no state change).
int32_t THISCALL Console_ready_search(uint8_t*, int32_t) { return 0; }
void THISCALL Console_set_move(uint8_t*, int32_t) {}
void THISCALL Console_cursor_next(uint8_t*, int32_t, int32_t) {}
void THISCALL Console_update_data(uint8_t*, int32_t) {}
