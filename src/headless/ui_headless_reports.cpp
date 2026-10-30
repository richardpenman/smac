// Headless reports and monuments (engine/ui_bridge.h; OpenSMAC: src/ui/reports.cpp, monuwin.cpp).
#include "engine/ui_bridge.h"

// No windows: nothing is shown.
void GAMEAPI report_intel(int32_t) {}
void GAMEAPI report_score(int32_t) {}
void GAMEAPI hall_of_fame(int32_t) {}
void GAMEAPI monument(int32_t) {}
