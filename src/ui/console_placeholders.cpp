// Placeholders of original UI functions the console calls that are not reconstructed yet (declared
// in console.h). None of them changes game state in the original except where noted; each does
// nothing here (the first call is logged) or only marks the screen for presentation.
#include "ui/console.h"
#include "ui/console_frame.h"
#include "ui/win.h"

// The cursor and the panes are drawn by the placeholder screen (win.cpp) at presentation.
void GAMEAPI draw_cursor() { ui_invalidate(); }
void THISCALL MapWin_draw_cursor(uint8_t*) { ui_invalidate(); }
void THISCALL InfoWin_change(uint8_t*, int32_t) { ui_invalidate(); }
void THISCALL InfoWin_reset(uint8_t*) {}
void THISCALL StatusWin_set_loc(uint8_t*, int32_t, int32_t) { ui_invalidate(); }
void THISCALL WorldWin_set_world_map(uint8_t*) {}
void THISCALL pfncMainMenuUpdateVisible(uint8_t*, int32_t) {}
void THISCALL TutWin_tour(uint8_t*) { console_not_reconstructed("TutWin_tour"); }
// No tutorial window is up (0).
int32_t THISCALL TutWin_reset(uint8_t*) { return 0; }
// Tutorial hints for the selected unit (game state: tut_check bits; only with tutorial hints on).
void THISCALL Console_give_hints(uint8_t*) { console_not_reconstructed("Console_give_hints"); }
void THISCALL Console_assemble(uint8_t*) { console_not_reconstructed("Console_assemble"); }
void THISCALL Console_time_controls(uint8_t*) { console_not_reconstructed("Console_time_controls"); }
void THISCALL Console_custom_my_faction(uint8_t*) { console_not_reconstructed("Console_custom_my_faction"); }
void THISCALL Console_editor_edit_veh(uint8_t*, int32_t) { console_not_reconstructed("Console_editor_edit_veh"); }
void THISCALL Console_editor_kill_veh(uint8_t*, int32_t) { console_not_reconstructed("Console_editor_kill_veh"); }
// Scenario editor brush (ui module, not reconstructed).
void THISCALL MapWin_editor(uint8_t*, int32_t, int32_t, int32_t) { console_not_reconstructed("MapWin_editor"); }
