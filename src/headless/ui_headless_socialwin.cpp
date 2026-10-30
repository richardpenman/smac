// Headless social engineering window (engine/ui_bridge.h; OpenSMAC: src/ui/socialwin.cpp).
#include "engine/ui_bridge.h"

// No dialog: the pending social choice stays as it is.
void THISCALL SocialWin_exec(uint8_t*, int32_t) {}
void THISCALL SocialWin_close(uint8_t*) {}
