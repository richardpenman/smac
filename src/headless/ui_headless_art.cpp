// Headless UI area "art": faction art and unit flags (load_faction_art, load_flags,
// engine/ui_bridge.h); nothing is loaded.
// Replaced in smacx by src/ui/sprites.cpp.
// Native builds only: hybrid builds always call the original UI.
#ifndef SMACX_HYBRID
#include "engine/ui_bridge.h"

void GAMEAPI load_faction_art(int32_t) {}
void GAMEAPI load_flags() {}
#endif
