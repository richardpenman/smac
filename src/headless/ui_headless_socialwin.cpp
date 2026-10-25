// Headless social engineering window (engine/ui_bridge.h; smacx: src/ui/socialwin.cpp). Native
// builds only: hybrid builds call the original.
#ifndef SMACX_HYBRID
#include "engine/ui_bridge.h"

// No dialog: the pending social choice stays as it is.
void THISCALL SocialWin_exec(uint8_t*, int32_t) {}
void THISCALL SocialWin_close(uint8_t*) {}
#endif
