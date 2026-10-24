// Headless implementations of the UI/system functions used by the social and net_misc modules
// (engine/ui_bridge.h). Native builds only: hybrid builds call the original.
#ifndef SMACX_HYBRID
#include "engine/ui_bridge.h"

// No scenario editor: the faction itself.
int32_t THISCALL Console_editor_who(uint8_t*, int32_t faction_id, int32_t) { return faction_id; }
// No dialog: the pending social choice stays as it is.
void THISCALL SocialWin_exec(uint8_t*, int32_t) {}
void THISCALL SocialWin_close(uint8_t*) {}
// No registry: nothing is read (records unchanged) or written.
void GAMEAPI read_tamper() {}
void GAMEAPI set_tamper() {}
#endif
