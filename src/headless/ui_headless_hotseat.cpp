// Headless implementations of the hotseat / PBEM UI functions (engine/ui_bridge.h): the next
// human player's handover and the diplomacy between two humans (smacx: src/ui/hotseat.cpp,
// src/ui/diplo_hotseat.cpp).
#include "engine/ui_bridge.h"

void GAMEAPI diplo(int32_t, int32_t) {}
int32_t ui_hotseat_switch(int32_t, int32_t*) { return 0; }
void ui_hotseat_show(int32_t) {}
