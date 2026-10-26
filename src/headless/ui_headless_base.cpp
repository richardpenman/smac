// Headless implementations of the UI-layer functions used by the base module (engine/ui_bridge.h).
// Native builds only: hybrid builds call the original UI.
#ifndef SMACX_HYBRID
#include "engine/ui_bridge.h"

// The message list is empty without a UI.
void ui_messages_base_killed(int32_t) {}
bool ui_shift_pressed() { return false; }
#endif
