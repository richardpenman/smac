// Headless implementation of boom, the combat animation (engine/battle.h). OpenSMAC replaces this area
// (src/ui/sound.cpp: its sounds).
#include "engine/battle.h"

// The combat animation draws and plays sounds only (its result is unused by every caller).
int32_t GAMEAPI boom(int32_t, int32_t, int32_t) { return 0; }
