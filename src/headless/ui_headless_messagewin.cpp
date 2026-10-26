// Headless implementation of MessageWin_clear, the message list of the info pane (engine/ui_bridge.h).
// Native builds only: hybrid builds call the original UI. smacx replaces this area
// (src/ui/infowin.cpp). The list is display only.
#ifndef SMACX_HYBRID
#include "engine/ui_bridge.h"

void THISCALL MessageWin_clear(uint8_t*) {}
#endif
