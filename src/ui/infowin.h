// InfoWin (ui 0x4562F0..0x459500): the info pane in the middle of the console, two columns of
// text typed out by a timer (the year and the energy allocation, commerce, the diplomacy partner,
// the faction's standing, the ranking graphs, the records, the editor's brush), its right-click
// menu of the displays; the message list (MessageWin 0x7F67F8, the M button) shown in its place.
//
// The object of the original is InfoWindow (0x7AD2A0, engine/ui_bridge.h); its fields are used in
// place (the text lines, their positions and colours, the states, the typing progress, the font
// at +0x58). It draws into the main interface's canvas (basewin_mi_canvas), whose info pane keeps
// what was drawn between the console's redraws (console_frame.cpp).
#pragma once
#include <cstdint>

#include "engine/memmap.h"
#include "ui/gfx.h"

ADDR_NATIVE(0x4592A0) void THISCALL InfoWin_init(uint8_t* self);
ADDR_NATIVE(0x458DC0) int32_t GAMEAPI InfoWin_state_valid(int32_t state);
ADDR_NATIVE(0x456300) void THISCALL InfoWin_setup_text(uint8_t* self);
ADDR_NATIVE(0x458420) void THISCALL InfoWin_draw_text(uint8_t* self, int32_t pane);
ADDR_NATIVE(0x4588B0) void THISCALL InfoWin_redraw(uint8_t* self);  // InfoWin_UNK2
ADDR_NATIVE(0x458FF0) void THISCALL InfoWin_timer_change(uint8_t* self, int32_t a1);
ADDR_NATIVE(0x459150) void THISCALL InfoWin_timer_proc(uint8_t* self, int32_t a1);
ADDR_NATIVE(0x459260) void GAMEAPI InfoWin_timer_daemon(int32_t a1);
ADDR_NATIVE(0x458930) void THISCALL InfoWin_on_left_click(uint8_t* self, int32_t x, int32_t y);
ADDR_NATIVE(0x4589C0) void THISCALL InfoWin_right_menu(uint8_t* self, int32_t x, int32_t y);
ADDR_NATIVE(0x458D40) void THISCALL InfoWin_on_right_click(uint8_t* self, int32_t x, int32_t y);

// MessageWin (0x4713A0..): the message list of the info pane's M mode.
ADDR_NATIVE(0x471530) void THISCALL MessageWin_add_message(uint8_t* self, const char* text, uint32_t id);
ADDR_NATIVE(0x4713B0) void THISCALL MessageWin_add_base(uint8_t* self, uint32_t base_id, const char* text);
ADDR_NATIVE(0x4718C0) void THISCALL MessageWin_on_selected(uint8_t* self, uint32_t id);
// MainInterface_set_message (0x45C0F0): the message line (StringBox) and the message list.
ADDR_NATIVE(0x45C0F0) void THISCALL MainInterface_set_message(uint8_t* self, const char* text);

// ---- not original functions ----
// The main interface's pane mode (+0x1E8FC: 1000 messages, 0x3E9 info, 0x3EA editor).
int32_t mi_mode();
// ButtonGroup_set of the mode buttons and the main interface's redraw of the new mode
// (MainInterface_on_iface_group_clicked -> MainInterface_on_redraw).
void mi_set_mode(int32_t mode);
// Lazy MainInterface_init part of the info pane (InfoWin_init, MessageWin_init, the M / I / X
// buttons); console_frame.cpp calls it with the pane geometry set.
void infowin_setup();
// The info pane into the main interface's canvas at a console redraw: what InfoWin drew (or the
// message list) and the mode buttons.
void infowin_compose(Buffer* mi);
// Mouse input on the info pane and its buttons (main interface coordinates); true when taken.
bool infowin_mouse(int32_t type, int32_t x, int32_t y);
// The timer (every 50 ms, InfoWin_init's pfncStartTimer) from the pump.
void infowin_tick();
// EditorPanelWin's show / hide: the 0x3EA mode button (the scenario editor's pane).
void infowin_editor_button(bool show);
// editpane.cpp: the editor's pane (mode 0x3EA) composed into the main interface's canvas, its
// clicks (main interface coordinates), ButtonGroup_set of its groups (2 paints, 3 brushes), and
// its state on stderr (EDITSTATE).
void editpane_compose(Buffer* mi);
bool editpane_mouse(int32_t type, int32_t x, int32_t y);
void editpane_group_set(int32_t group, int32_t id);
void editpane_log_state();
ADDR_NATIVE(0x45B550) void THISCALL MainInterface_draw_edit_pane(uint8_t* self);
// Test aid (scripted INFOSTATE): the panes' states and texts on stderr.
void infowin_log_state();
