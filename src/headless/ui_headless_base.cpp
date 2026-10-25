// Headless implementations of the UI-layer functions used by the base module (engine/ui_bridge.h).
// Native builds only: hybrid builds call the original UI.
#ifndef SMACX_HYBRID
#include "engine/ui_bridge.h"

// The base window is never shown.
void THISCALL BaseWin_base_killed(uint8_t*, int32_t) {}
// The message list is empty without a UI.
void ui_messages_base_killed(int32_t) {}
// No sound, no help pages.
void GAMEAPI wave_it(int32_t) {}
void GAMEAPI help_topic(int32_t, int32_t) {}
bool ui_shift_pressed() { return false; }
void THISCALL BaseWin_on_redraw(uint8_t*) {}
void THISCALL BaseWin_zoom(uint8_t*, int32_t, int32_t) {}
// base_production: no movies, help or voice clips (its popups are in the popup area).
void GAMEAPI amovie_project(int32_t) {}
void GAMEAPI amovie_project_2(const char*) {}
void GAMEAPI help_project(int32_t) {}
int32_t THISCALL Wave_unload(uint8_t*) { return 0; }
int32_t THISCALL Wave_load(uint8_t*, const char*) { return 0; }
int32_t THISCALL Wave_play_2(uint8_t*) { return 0; }
void THISCALL Sound_fade_2(uint8_t*, int32_t) {}
#endif
