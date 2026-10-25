// The research choice (tech_pick's dialog, FUN_00486510 "TechWin"), the new technology window
// (NewTechWin) and Console_set_tech / Console_set_ai (techwin.cpp).
#pragma once
#include <cstdint>

#include "engine/memmap.h"
#include "ui/gfx.h"

// The technology pictures (load_misc 0x454D10: techs\techNNN.pcx at 0x759E30 [89], full size
// 215 x 215; loaded when first drawn).
Sprite* tech_picture(int32_t tech_id);
// Blind research priorities (TECHRANDOM: the faction's AI_fight / growth / tech / wealth).
ADDR_NATIVE(0x514F50) void THISCALL Console_set_ai(uint8_t* console, int32_t faction_id);
// Test aid (smacx --keys TECHSTATE): the viewer's research ("techstate f F research T
// accumulated A cost C") and the research window's choices when it is open.
void techwin_log_state();
