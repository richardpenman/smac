// The main interface around the main view (MainInterface, ui 0x459680..0x45F1F0): the console
// frame of console2_A.pcx with its buttons (menu, commlink, end of turn / turn complete, the
// info/message toggles, the world map buttons), the status panes (StatusWin: the unit, the
// tile, the unit stack), the info pane (InfoWin) and the world map (WorldWin); and the hooks of
// the window layer (win.cpp) into the console's mouse code (console_mouse.cpp) and the menus
// (menu.cpp).
#pragma once
#include <string>
#include <cstdint>

#include "engine/memmap.h"
#include "ui/gfx.h"

// ---- window layer hooks (not original functions) ----
// Draws the interface frame and its panes over the screen (below the popups).
void console_frame_draw(Buffer* screen);
// The point is on the frame (not on a transparent part of console2_A.pcx).
bool console_frame_hit(int32_t x, int32_t y);
// Mouse input on the frame (UiInput types).
void console_frame_mouse(int32_t type, int32_t x, int32_t y);
// Main screen mouse input: menus, frame, map (console_mouse.cpp).
void console_ui_mouse(int32_t type, int32_t x, int32_t y);
// Scripted drag from (x1, y1) to (x2, y2) (the go-to drag of a held button).
void console_ui_drag(int32_t x1, int32_t y1, int32_t x2, int32_t y2, const std::string& shot = std::string());
// Timers of the console (go-to drag): called by the pump.
void console_ui_tick();
// The mouse cursor sprite (CURSOR.PCX) at the mouse position.
void console_draw_cursor(Buffer* screen);
// The tile under a go-to drag (-1: none), marked on the map.
void ui_set_drag_target(int32_t x, int32_t y);
// Menus: mouse input (true when taken) and keys (Alt, Alt+letter, the open menu's keys; true when
// taken) and drawing (menu.cpp).
bool console_menu_mouse(int32_t type, int32_t x, int32_t y);
bool console_menu_key(int32_t key);
void console_menu_toggle();  // the MENU button (MainInterface_on_iface_button_clicked 0x3EB)
void console_menu_draw(Buffer* screen);
// The pull-down of menu `index` (Menu_id_to_index: 3 Action, 9 Terraform, ...) shown at the screen
// position (x, y) as MapWin_right_menu shows it; the chosen command runs (menu.cpp).
void console_menu_popup(int32_t index, int32_t x, int32_t y);
// Menu_check_menu_item / Menu_uncheck_menu_item (0x5FB760 / 0x5FB7C0) of the console's menu bar.
void console_menu_check(int32_t index, int32_t id, bool on);
// The Map menu's display toggles (console_menu.cpp), also bound to keys.
void console_toggle_pref(int32_t id);
// Height of the main view (the map window): the screen minus the console's lower part (168
// pixels, the oracle's view at 1024 x 768); on a screen wider than the console (1024) the whole
// height, so the map fills the screen beside the console (console_frame_draw).
int32_t console_view_height(int32_t screen_width, int32_t screen_height);

// The main interface's panes drawn by the original's code (maininfc.cpp): the pane rectangles of
// MainInterface_init, the world map (WorldWin_on_redraw), the date box (MainInterface_set_date)
// and "TURN COMPLETE" (MainInterface_redraw_complete), into the main interface's canvas.
void maininfc_geometry();
void worldwin_draw();
// The same into another main interface canvas (a screen's: the reports' WorldWin_show_all).
void worldwin_draw_to(Buffer* mi);
// WorldWin_tile_to_pixel (0x4C44E0): the pane coordinates of a tile; 1 when off the map.
int32_t worldwin_tile_to_pixel(int32_t x, int32_t y, int32_t* px, int32_t* py);
void maininfc_set_date();
void maininfc_redraw_complete();
// blink2_timer (100 ms): TURN COMPLETE's animation (turn.flc) a frame on while the turn's end waits.
void maininfc_blink2();

// ---- original functions ----
// Hides the pull-down menu bar (AlphaMenu at 0x9380DC).
void AlphaMenu_hide_main();
ADDR_NATIVE(0x4B4A20) void THISCALL StatusWin_set_title(uint8_t* win, const char* title);
// The unit menu of a right click on the map (tile x, y; mouse px, py).
ADDR_NATIVE(0x46C0D0) void THISCALL MapWin_right_menu(uint8_t* win, int32_t x, int32_t y, int32_t px, int32_t py);
// Scenario editor brush on a tile (right: the second brush).
ADDR_NATIVE(0x46B510) void THISCALL MapWin_editor(uint8_t* win, int32_t x, int32_t y, int32_t right);
