// Headless implementations of the base window (BaseWin) functions the engine calls
// (engine/ui_bridge.h), and of the window visibility / redraw requests they depend on. Native
// builds only: hybrid builds call the original UI. smacx replaces this area (src/ui/basewin*.cpp).
#ifndef SMACX_HYBRID
#include "engine/ui_bridge.h"

// No window is ever visible: the base window is never shown, nothing is recomputed or redrawn.
int32_t THISCALL Win_is_visible(uint8_t*) { return 0; }
void THISCALL GraphicWin_redraw(uint8_t*) {}
void THISCALL BaseWin_close(uint8_t*) {}
void THISCALL BaseWin_zoom(uint8_t*, int32_t, int32_t) {}
void THISCALL BaseWin_on_redraw(uint8_t*) {}
void THISCALL BaseWin_focus(uint8_t*) {}
void THISCALL BaseWin_check_base(uint8_t*, int32_t) {}
void THISCALL BaseWin_check_loc(uint8_t*, int32_t, int32_t, int32_t) {}
void THISCALL BaseWin_check_veh(uint8_t*, int32_t) {}
void THISCALL BaseWin_base_killed(uint8_t*, int32_t) {}
#endif
