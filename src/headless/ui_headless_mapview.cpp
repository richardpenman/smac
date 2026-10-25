// Headless UI area "map view": the MapWin drawing entry points the engine calls (engine/ui_bridge.h).
// Nothing is drawn.
//
// How UI areas are replaced: src/headless is the static library smacx_ui_headless, the default
// implementation of every UI bridge function, with one object file per area (ui_headless_<area>.cpp).
// smacx_headless uses all of it. smacx defines the functions of the areas its SDL UI implements
// (src/ui) in its own objects, so the linker never pulls those areas' objects from the library;
// hybrid builds call the original UI and use neither. To replace an area, implement every function
// of its file in src/ui (a partial replacement is a duplicate-symbol link error); to replace a single
// function, first move it into an area file of its own here. Areas replaced by smacx: mapview,
// console, art, pump, popup.
// Native builds only: hybrid builds always call the original UI.
#ifndef SMACX_HYBRID
#include "engine/ui_bridge.h"

void GAMEAPI draw_radius(int32_t, int32_t, int32_t, int32_t) {}
void GAMEAPI draw_tile(int32_t, int32_t, int32_t) {}
void GAMEAPI draw_tiles(int32_t, int32_t, int32_t) {}
void GAMEAPI draw_tile_fixup(int32_t, int32_t, int32_t, int32_t) {}
void GAMEAPI draw_tile_fixup_2(int32_t, int32_t, int32_t, int32_t, int32_t, int32_t) {}
void GAMEAPI draw_map(int32_t) {}
void THISCALL pfncRedrawMap(uint8_t*, int32_t) {}
void THISCALL pfncMoveMap(uint8_t*, int32_t, int32_t, int32_t) {}
// Returns the original's "view changed" flag (0: nothing redrawn).
int32_t THISCALL MapWin_focus(uint8_t*, int32_t, int32_t) { return 0; }
void THISCALL MapWin_main_caption(uint8_t*) {}
void THISCALL MapWin_clear_alt(uint8_t*, int32_t, int32_t, int32_t) {}
void THISCALL MapWin_clear_terrain(uint8_t*) {}
void ui_mapwins_clear_terrain() {}
void ui_mapwins_clear_alt(int32_t, int32_t) {}
#endif
