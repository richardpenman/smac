// Headless implementations of the UI-layer functions used by the path, move and goals modules
// (engine/ui_bridge.h). Native builds only: hybrid builds call the original UI.
#ifndef SMACX_HYBRID
#include "engine/ui_bridge.h"

void THISCALL pfncRedrawMap(uint8_t*, int32_t) {}
void THISCALL FX_stop(uint8_t*, int32_t) {}
void GAMEAPI say_morale_2(int32_t, int32_t) {}
int32_t ui_supply_menu(int32_t, int32_t, int32_t) { return 0; }
int32_t ui_artifact_menu(int32_t, int32_t, int32_t) { return 0; }
int32_t ui_disembark_menu(int32_t) { return 0; }
bool ui_ctrl_pressed() { return false; }
void THISCALL FX_engine_pitch(uint8_t*, int32_t) {}

#endif
