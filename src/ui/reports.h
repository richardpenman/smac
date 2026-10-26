// The F-key reports and the monuments / hall of fame windows (reports.cpp, monuwin.cpp; built on
// screenwin.h). report_score, report_intel, monument and hall_of_fame are declared with the
// engine's bridge (engine/ui_bridge.h): smacx replaces the headless "reports" area with them.
#pragma once
#include <cstdint>

#include "engine/memmap.h"

// Opened by Console_on_key_click (F2..F7) and Console_menu_proc.
ADDR_NATIVE(0x4AC9A0) void GAMEAPI report_labs();
ADDR_NATIVE(0x4AC9F0) void GAMEAPI report_energy();
ADDR_NATIVE(0x4ACA40) void GAMEAPI report_ops();
ADDR_NATIVE(0x4ACA90) void GAMEAPI report_projects();
ADDR_NATIVE(0x4ACAE0) void GAMEAPI report_sat();
ADDR_NATIVE(0x4ACB30) void GAMEAPI report_security();

// Not original: the view of base operations (0 production, 1 citizens, 2 garrison).
void reports_set_ops_view(int32_t view);

// MultiWin_hide (multiwin.cpp): the commlink panel closed (another interface mode starts).
void multiwin_hide();
// The commlink panel (multiwin.cpp): F12 and the console's COMMLINK button
// (MainInterface_on_iface_button_clicked 0x3EC).
ADDR_NATIVE(0x479970) void THISCALL MultiWin_show(uint8_t* self);

// The base locator's timer (bl_daemon, 150 ms) while the reports are open (the modal pump).
void reports_tick(uint32_t now_ms);
