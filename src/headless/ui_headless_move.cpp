// Headless implementations of the UI-layer functions used by the path, move and goals modules
// (engine/ui_bridge.h). Native builds only: hybrid builds call the original UI.
#ifndef SMACX_HYBRID
#include "engine/ui_bridge.h"

void THISCALL FX_stop(uint8_t*, int32_t) {}
void GAMEAPI say_morale_2(int32_t, int32_t) {}
bool ui_ctrl_pressed() { return false; }
void THISCALL FX_engine_pitch(uint8_t*, int32_t) {}

#endif
