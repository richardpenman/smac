// The tutorial windows (TutWin 0x8C6E68, ui 0x4BA6B0..0x4BEA20) and the human turn's unit hints
// (Console_give_hints, declared in ui/console.h). See tutwin.cpp.
#pragma once
#include <cstdint>

#include "engine/memmap.h"
#include "ui/gfx.h"

ADDR_NATIVE(0x4BDFE0)
int32_t THISCALL TutWin_tut_win(uint8_t* self, uint8_t* parent, const char* label, int32_t px, int32_t py,
                                const Sprite* sprite, uint32_t flags, int32_t px2, int32_t py2);
ADDR_NATIVE(0x4BDEA0) void THISCALL TutWin_draw_arrow(uint8_t* self);
ADDR_NATIVE(0x4BA870) void THISCALL TutWin_do_base(uint8_t* self, const UiRect* r, const char* label, uint32_t flags);
ADDR_NATIVE(0x4BC5F0) void THISCALL TutWin_base_rect(const UiRect* r, int32_t* x, int32_t* y);

// ---- not original functions ----
// TutWin_tut_map (0x4BE650; engine/ui_bridge.h) with its answer (TutWin_tut_win's).
int32_t tut_map(uint8_t* self, const char* label, int32_t x, int32_t y, int32_t veh_id, const Sprite* sprite,
                uint32_t flags, int32_t x2, int32_t y2);
// The window's pointer lines over the screen (ui_present, before the popups).
void tutwin_draw_lines(Buffer* screen);
// TutWin_on_button_clicked (0x4BDCC0) with the button's answer.
void tutwin_button(int32_t button);
// The base window's client point to the screen (Win_client_to_screen_2 of BaseWin).
void basewin_client_to_screen(int32_t* x, int32_t* y);
// The social engineering window for the tour (socialwin.cpp): open, closed after the tour's
// display, and the screen centre of its rectangle `which` (see socialwin_point).
bool socialwin_open();
void socialwin_tour_close();
void socialwin_point(int32_t which, int32_t* x, int32_t* y);
// The design workshop for the tour (designwin.cpp): open, and the screen centre of its rectangle
// `which` (see designwin_point); DesignWin_exec(faction, -2) shows it and runs
// tutwin_tour7_steps (tutwin.cpp: TutWin_tour7's windows).
bool designwin_open();
void designwin_point(int32_t which, int32_t* x, int32_t* y);
void tutwin_tour7_steps();
