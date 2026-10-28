// Headless implementations of the UI-layer functions used by the action module (engine/ui_bridge.h).
#include "engine/ui_bridge.h"

// Tutorial hints, base window refresh, cursor and report window: nothing to show.
void THISCALL TutWin_tut_map(uint8_t*, const char*, int32_t, int32_t, int32_t, uint8_t*, int32_t, int32_t, int32_t) {}
void THISCALL ReportIf_on_redraw(uint8_t*) {}
