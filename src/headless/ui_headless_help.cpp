// Headless implementations of the help (datalinks) entry points the engine calls
// (engine/ui_bridge.h).
#include "engine/ui_bridge.h"

// No help pages.
void GAMEAPI help_topic(int32_t, int32_t) {}
void GAMEAPI help_project(int32_t) {}
