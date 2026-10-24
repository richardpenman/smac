// Headless implementations of the UI-layer functions used by the action module (engine/ui_bridge.h).
// Native builds only: hybrid builds call the original UI.
#ifndef SMACX_HYBRID
#include "engine/ui_bridge.h"

// Tutorial hints, base window refresh, sounds, cursor and report window: nothing to show.
void THISCALL TutWin_tut_map(uint8_t*, const char*, int32_t, int32_t, int32_t, uint8_t*, int32_t, int32_t, int32_t) {}
void THISCALL BaseWin_check_veh(uint8_t*, int32_t) {}
void THISCALL FX_play_loc(uint8_t*, int32_t, int32_t, int32_t) {}
void THISCALL Console_cursor_next(uint8_t*, int32_t, int32_t) {}
void THISCALL ReportIf_on_redraw(uint8_t*) {}
void GAMEAPI draw_tile_fixup(int32_t, int32_t, int32_t, int32_t) {}

// The sliding animation is not shown; its game-state effect is the vehicle's facing.
void GAMEAPI veh_scoot(int32_t veh_id, int32_t, int32_t, int32_t dir, int32_t flags) {
    if (dir > 7) dir %= 8;
    if (flags & 0x4000) return;
    if (MultiplayerActive || !(flags & 1)) Vehs[veh_id].rotate_angle = (int8_t)dir;
}
#endif
