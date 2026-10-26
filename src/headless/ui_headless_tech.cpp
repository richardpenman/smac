// Headless implementations of the UI-layer functions used by the tech module (engine/ui_bridge.h);
// the research dialog and the new technology window are in ui_headless_techwin.cpp. Native builds
// only: hybrid builds call the original UI.
#ifndef SMACX_HYBRID
#include "engine/ui_bridge.h"

// No windows.
void GAMEAPI desktop_update() {}
void THISCALL DesignWin_shutdown(uint8_t*) {}
#endif
