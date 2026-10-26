// StatusWin (ui 0x4B4990..0x4BA1A0): the console's left panes, the selected unit (or the units of
// the tile under the cursor) and the tile under the cursor, with the unit stack row below; and the
// unit text helpers of the ui module it uses (say_orders).
//
// The StatusWin object of the original is InfoWin (0x8C5568, engine/ui_bridge.h); its integer
// fields are used in place. Drawing goes into the main interface's canvas (console_frame.cpp).
#pragma once
#include <cstdint>

#include "engine/memmap.h"
#include "ui/gfx.h"

// Appends the description of a unit's orders (automation, go-to targets, convoy, patrol
// waypoints) to buffer.
ADDR_NATIVE(0x4B43E0) void GAMEAPI say_orders(char* buffer, int32_t veh_id);
// say_orders into StrBuffer.
ADDR_NATIVE(0x4B4970) void GAMEAPI say_orders_2(int32_t veh_id);

// The panes' mouse input (maininfc.cpp).
ADDR_NATIVE(0x4B8E40) void THISCALL StatusWin_on_left_click(uint8_t* self, int32_t x, int32_t y);
ADDR_NATIVE(0x4B8990) void THISCALL StatusWin_on_left_double_click(uint8_t* self, int32_t x, int32_t y);
ADDR_NATIVE(0x4B9010) void THISCALL StatusWin_on_right_click(uint8_t* self, int32_t x, int32_t y, int32_t a3);
ADDR_NATIVE(0x4B89C0) void THISCALL StatusWin_arrow_left(uint8_t* self);
ADDR_NATIVE(0x4B8BD0) void THISCALL StatusWin_arrow_right(uint8_t* self);

// The battle report (battlewin.cpp).
ADDR_NATIVE(0x421B50) void THISCALL BattleWin_battle_report(uint8_t* win, int32_t veh_atk, int32_t veh_def, int32_t offense,
                                                           int32_t defense, uint32_t flags);
ADDR_NATIVE(0x422CC0) void THISCALL BattleWin_on_status(uint8_t* win, int32_t on);
ADDR_NATIVE(0x45D380) void THISCALL SubInterface_release_iface_mode(uint8_t* win);

// ---- not original functions ----
// say_morale (0x4B3FD0) into out.
void statuswin_say_morale(char* out, int32_t veh_id, int32_t faction_id);
// battlewin.cpp: the report is shown (interface mode 10); its canvas into the main interface's;
// the 5 s timer from the pump; slide_show / slide_hide's console part (MiSlideState) and its
// picture over the console art.
bool battlewin_shown();
void battlewin_compose(Buffer* mi);
void battlewin_tick();
void mi_slide(bool down);
void mi_slide_draw(Buffer* mi);
// The stack row's scroll arrows (buttons 0x3F0 / 0x3F2) into the main interface's canvas.
void statuswin_compose(Buffer* mi);
// Mouse input on the status panes, the stack arrows, the world map pane and spot 5 (main interface
// coordinates x, y; screen sx, sy); true when taken.
bool statuswin_mouse(int32_t type, int32_t x, int32_t y, int32_t sx, int32_t sy);
// The unit pane of the unit / base under the mouse (the hover kept over the console's redraws).
void statuswin_hover();
// WorldWin_pixel_to_tile (0x4C43F0) in the pane's coordinates; 1 when off the map.
int32_t worldwin_pixel_to_tile(int32_t px, int32_t py, int32_t* tx, int32_t* ty);
// The world map pane's tile under a screen point (0: found).
int32_t worldwin_screen_tile(int32_t sx, int32_t sy, int32_t* tx, int32_t* ty);
