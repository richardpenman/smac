// Placeholders of original UI functions the console calls that are not reconstructed yet (declared
// in console.h). None of them changes game state in the original except where noted; each does
// nothing here (the first call is logged) or only marks the screen for presentation.
#include "ui/console.h"
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
// Unit sounds (the original also remembers the unit, 0x683BF4, for its sound loop).
void GAMEAPI ambience_veh(int32_t) {}
// Ends mouse go-to and drag modes (none is started without the mouse code).
void GAMEAPI go_reset() {}
void THISCALL MapWin_draw_base_dest(uint8_t*, int32_t) { ui_invalidate_map(); }
void THISCALL Console_activate(uint8_t*, int32_t, int32_t, int32_t) { console_not_reconstructed("Console_activate"); }
void THISCALL Console_go_to(uint8_t*, int32_t, int32_t, int32_t) { console_not_reconstructed("Console_go_to"); }
void THISCALL Console_go_home(uint8_t*, int32_t) { console_not_reconstructed("Console_go_home"); }
void THISCALL Console_automate(uint8_t*, int32_t, int32_t) { console_not_reconstructed("Console_automate"); }
void THISCALL Console_unload(uint8_t*, int32_t) { console_not_reconstructed("Console_unload"); }
void THISCALL Console_oblit(uint8_t*, int32_t) { console_not_reconstructed("Console_oblit"); }
void THISCALL Console_arty(uint8_t*, int32_t) { console_not_reconstructed("Console_arty"); }
void THISCALL Console_use_ability(uint8_t*, int32_t) { console_not_reconstructed("Console_use_ability"); }
void THISCALL Console_explore(uint8_t*, int32_t) { console_not_reconstructed("Console_explore"); }
void THISCALL Console_convoy(uint8_t*, int32_t) { console_not_reconstructed("Console_convoy"); }
void THISCALL Console_set_patrol(uint8_t*, int32_t) { console_not_reconstructed("Console_set_patrol"); }
void THISCALL Console_designate(uint8_t*, int32_t) { console_not_reconstructed("Console_designate"); }
void THISCALL Console_road_to(uint8_t*, int32_t, int32_t) { console_not_reconstructed("Console_road_to"); }
void THISCALL Console_give_veh(uint8_t*, int32_t) { console_not_reconstructed("Console_give_veh"); }
void THISCALL Console_destroy(uint8_t*, int32_t) { console_not_reconstructed("Console_destroy"); }
void THISCALL Console_disband(uint8_t*, int32_t) { console_not_reconstructed("Console_disband"); }
void THISCALL Console_disband_2(uint8_t*, int32_t, int32_t) { console_not_reconstructed("Console_disband_2"); }
void THISCALL Console_assemble(uint8_t*) { console_not_reconstructed("Console_assemble"); }
void THISCALL Console_survey(uint8_t*) { console_not_reconstructed("Console_survey"); }
void THISCALL Console_time_controls(uint8_t*) { console_not_reconstructed("Console_time_controls"); }
void THISCALL Console_use_time_bonus(uint8_t*) { console_not_reconstructed("Console_use_time_bonus"); }
void THISCALL Console_set_tech(uint8_t*, int32_t) { console_not_reconstructed("Console_set_tech"); }
void THISCALL Console_zoom_to_upkeep_base(uint8_t*) { console_not_reconstructed("Console_zoom_to_upkeep_base"); }
void THISCALL Console_show_scen_objectives(uint8_t*) { console_not_reconstructed("Console_show_scen_objectives"); }
void THISCALL Console_custom_my_faction(uint8_t*) { console_not_reconstructed("Console_custom_my_faction"); }
void THISCALL Console_corner_the_market(uint8_t*) { console_not_reconstructed("Console_corner_the_market"); }
