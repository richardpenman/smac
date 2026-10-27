// The diplomacy dialog's interface mode (DiploPop, ui 0x43F0F0..0x440EF0) and the human side of
// the diplomacy dialog (diplomacy module 0x54B1C0..0x558FC0): see diplowin.cpp and diplo_comm.cpp.
#pragma once
#include <cstdint>

#include "ui/gfx.h"

// Draws the diplomacy interface mode over the console's left pane while a commlink is open
// (DiploPop_on_redraw: the bioscan and portrait animations, the leader's name, attitude and
// mood). Returns true when the mode is active (the console then leaves its unit panes empty).
bool diplowin_draw_console(Buffer* screen, int32_t ox, int32_t oy);
// Animations follow real time only when enabled (interactive play); scripted and hidden runs
// show the first frames so that screenshots are reproducible.
void diplowin_set_animate(bool on);
// DiploPop's interface buttons (DATALINKS, PROFILE, HQINFO) and the globe buttons during the
// diplomacy mode: mouse input at screen (x, y) outside the popups; true when it was theirs.
bool diplowin_iface_mouse(int32_t type, int32_t x, int32_t y);
// DiploPop's HQ pane and buttons over the main interface's canvas (after its date box).
void diplowin_draw_iface(Buffer* mi);
// BaseButton_set of HQINFO (DiploPop +0x4208).
void diplowin_set_hq(int32_t on);
// HQINFO is set: the right pane shows the HQ instead of the world map (WorldWin hidden).
bool diplowin_hq_shown();
// The faction on the commlink (-1: none).
int32_t diplowin_faction();
// DiploPop's own window (the dialog's popups are drawn into it) onto the screen, under the popups.
void diplowin_draw_window(Buffer* screen);
// When DiploPop's window was shown (ui_window_seq; 0xFFFFFFFF when hidden): the windows shown
// after it (a new technology, a report from its PROFILE button) are drawn over it.
uint32_t diplowin_window_seq();
// The diplomacy menu's initial selection (COUNCILVOTEGOV: the human's own entry; popup_menus.cpp).
void ui_diplo_menu_set_active(int32_t value);
// The menu's selection callback (Popup +0x3280: battle_routine).
void ui_diplo_menu_set_callback(void (*fn)(int32_t));
// Test aid (DIPLOSTATE): "diplostate fV credits C techs N bases B" and, for each other living
// faction, "diplo I status S agenda A friction F patience P loan BAL/PAY credits C bases B" on stderr.
void diplowin_log_state();
