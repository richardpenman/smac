// Headless UI area "pump": the message pump the engine runs inside long loops (engine/ui_bridge.h).
// There is no window. Replaced in smacx by src/ui/win.cpp.
// Native builds only: hybrid builds always call the original UI.
#ifndef SMACX_HYBRID
#include "engine/ui_bridge.h"

void GAMEAPI do_all_non_input() {}
void GAMEAPI wait_task() {}
int32_t GAMEAPI do_non_input() { return 0; }
// Discards pending keyboard and mouse input.
void GAMEAPI flush_input() {}
#endif
