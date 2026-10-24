// Headless implementations of the UI-layer functions used by the design module (engine/ui_bridge.h).
// Native builds only: hybrid builds call the original UI.
#ifndef SMACX_HYBRID
#include "engine/ui_bridge.h"

// The design workshop is never shown: nothing is designed.
int32_t THISCALL DesignWin_exec(uint8_t*, int32_t, int32_t) { return 0; }
#endif
