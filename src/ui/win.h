// Window layer of smacx: the screen, the message pump and keyboard input, placeholder panes; the
// popups (popup.h) are drawn over the screen.
//
// The original builds its screen from Win/GraphicWin objects (gfx module) and dispatches Windows
// messages to them; that layer is not reconstructed yet (UI_PLAN.md phase 4). Here the screen is
// the main map view plus a placeholder console panel, the message pump reads platform events, and
// keys reach the console as the original's Win_on_key / Win_key_down_event / WM_CHAR handling
// delivers them: Console_on_key_click(character, virtual key | modifier bits).
//
// Original functions (ADDR_NATIVE, smacx's replacement of the headless "pump" and "popup" areas):
// wait_task, do_all_non_input, do_non_input, flush_input, and the popups (win_popup.cpp,
// popup_menus.cpp: the reconstructed BasePop / Popup of popup.h).
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "engine/memmap.h"
#include "ui/gfx.h"

// wait_task (engine/ui_bridge.h): the window is presented if it changed and one pending input
// event is dispatched (it waits briefly when there is none, where the original spins).

// ---- not original functions ----
// The screen: the main map view (MainConsole) over the top of the window, the console panel below.
void ui_screen_init(int32_t width, int32_t height);
Buffer* ui_screen_canvas();
// The map view must be redrawn (draw_map, draw_tile, ...) / the screen must be presented again.
void ui_invalidate_map();
void ui_invalidate();
// The help bubble (Win_set_bubble_text): `text` (lines split at '^') above the screen point
// (x, y); cleared by the next mouse move over the window (Win_clear_bubble_text).
void ui_bubble_set(const char* text, int32_t x, int32_t y);
// The same for a rectangle (BaseButton_timer_callback: the button's): above it, centred on it.
void ui_bubble_set_rect(const char* text, const UiRect& at);
void ui_bubble_clear();
// A full redraw of the main view is pending (the next ui_present does it).
bool ui_map_dirty();
// Redraws what is invalid and presents the screen.
void ui_present();
bool ui_screenshot(const std::string& path);
// Desktop mode (the opening screens before a game: top menu, setup dialogs): the screen shows the
// windows over black, without the map view and the console (topmenu.cpp).
void ui_set_desktop(bool on);
bool ui_desktop();
// Palette layers: under GDI each of the original's windows shows through its own DIB colour table
// (the opening pictures, the menu items' animation, the faction portraits). Layer n (1..3) is shown
// through `palette` (null: off); ui_layer_mark marks the pixels of `src` placed at (x0, y0) that
// are not `key` as layer `layer` (0: the game palette) for the frame being composed.
void ui_set_layer_palette(int32_t layer, const Palette* palette);
void ui_layer_mark(int32_t x0, int32_t y0, const Buffer* src, uint8_t layer, int32_t key = 9);
// DesktopWin's canvas (0x93755C): the screen-size overlay of the go-to drag (path and turns),
// transparent (9) where nothing is drawn; shown over the map while the drag runs.
Buffer* ui_desktop_canvas();
void ui_desktop_show(bool on);
// Message line of the console panel (the latest message box or placeholder note).
void ui_set_message(const std::string& text);
// GetKeyState for a virtual key: bit 15 held, bit 0 toggled (Scroll Lock etc. are never toggled).
int32_t ui_key_state(int32_t vk);

// Scripted input (smacx --keys): consumed before real input. A key is a virtual key with modifier
// bits (0x10000 shift, 0x20000 ctrl, 0x40000 alt); commands are run by the pump.
struct ScriptedKey {
    int32_t vk = 0;
    std::string command;  // "shot FILE", "save FILE", "quit"
};
void ui_script_keys(const std::vector<ScriptedKey>& keys);
// SMACX_TRACE=1: logs the random generators and the logic clock (determinism checks).
void ui_trace(const char* what);
// Called when the scripted keys run out with nothing left to do (no display): the game quits.
extern bool g_ui_exit_when_script_done;
extern std::string g_ui_outdir;
extern int32_t g_ui_shots;

// Dispatches a key (virtual key with modifier bits) as Win_on_key / WM_CHAR deliver it.
void ui_dispatch_key(int32_t key);
// Next key for a modal placeholder (popups): scripted or real; 0 when the input ended.
int32_t ui_wait_key();
// Next input of a modal window (popups, popup.cpp): a key (virtual key with the modifier bits), or
// a left button press / release / mouse move at screen coordinates (scripted: "CLICK:XxY", a
// press and a release). False when the input ended (scripted keys done, window closed).
struct UiInput {
    enum Type { KEY, LDOWN, LUP, MOVE, RDOWN, RUP } type = KEY;
    int32_t key = 0, x = 0, y = 0;
    bool double_click = false;
};
bool ui_wait_input(UiInput* in);
// Test aid (scripted "POPUP:FILE/LABEL[/FLAGS[/PCX]]", win_popup.cpp): shows that popup.
void ui_popup_command(const std::string& spec);

// Console_terraform's popup (win_popup.cpp): section `label` of `file` with section
// `choices_label` appended (Popup_start flag 0x80) when given. Returns BasePop_exec_3's answer.
int32_t ui_script_popup(const char* file, const char* label, const char* choices_label);
