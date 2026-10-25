// Headless reports and monuments (engine/ui_bridge.h; smacx: src/ui/reports.cpp, monuwin.cpp).
// Native builds only: hybrid builds call the original UI.
#ifndef SMACX_HYBRID
#include "engine/ui_bridge.h"

// No windows: nothing is shown.
void GAMEAPI report_intel(int32_t) {}
void GAMEAPI report_score(int32_t) {}
void GAMEAPI hall_of_fame(int32_t) {}
void GAMEAPI monument(int32_t) {}
#endif
