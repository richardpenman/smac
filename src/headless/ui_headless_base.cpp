// Headless implementations of the UI-layer functions used by the base module (engine/ui_bridge.h).
#include "engine/ui_bridge.h"

// The message list is empty without a UI.
void ui_messages_base_killed(int32_t) {}
bool ui_shift_pressed() { return false; }
