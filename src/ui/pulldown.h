// PullDown (gfx 0x5F8800..0x5FA790): the pop-up list of commands the original shows for a right
// click on the map (MapWin_right_menu) and under the menu bar (AlphaMenu's pull-downs).
//
// The object lives in native memory; its fields keep the original meaning (offsets noted). The
// look is the game's (popups_normal / AlphaMenu_init): font 0x9457EC (Arial 16, 12 at 800x600),
// margin 10 (0x696E04), border 1 (0x696E1C), the item rows painted by AlphaMenu_fill_func (box
// sprite 27 normal, 29 selected, clipped to the row), the text colours of text.pcx (0x8C6BF4..).
// Reconstructed: add_item / add_separator / disable / enable / hide / show / check, compute_length,
// on_redraw, the modal show with the mouse (rows by position, a press outside closes, a release on
// a row chooses; the right button acts as the left) and keys (arrows / keypad 8 2 skip separators
// and hidden rows, Enter, Esc, the accelerator letters).
// Not reconstructed: the check mark sprite of checked items (flag 8; no caller in the main view).
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "ui/gfx.h"

struct PullDownItem {  // +0xA18 + i * 0x14
    std::string text;       // +0x00 (the label without its '&')
    std::string shortcut;   // +0x04 (after '|'; drawn right-aligned)
    int32_t id = -1;        // +0x08
    uint32_t flags = 0;     // +0x0C: 1 shown, 2 disabled, 4 separator, 8 checked
    int32_t accel = -1;     // +0x10: index of the accelerator character in text (-1 none)
};

struct PullDown {
    std::vector<PullDownItem> items;  // +0xF20 count (at most 64)
    int32_t max_width = 0;            // +0xF24
    int32_t selected = -1;            // +0xF28
    int32_t lines = 0;                // +0xF2C: rows (items and separators)
    bool dirty = true;                // +0xF34: size to compute again
    bool menu = false;                // +0xF30 / +0xF30: a pull-down of the menu bar (+0xC4 parent)
    // Window (native): position and size on the screen, canvas.
    int32_t x = 0, y = 0, w = 0, h = 0;
    Buffer canvas;
    bool shown = false;
};

// PullDown_init (0x5F8920): empty list.
void PullDown_init(PullDown* self, bool menu = false);
// PullDown_add_item (0x5F89D0): "text|shortcut", '&' before the accelerator. 0 ok, 1 full or the
// id is taken, 3 no text / id -1.
int32_t PullDown_add_item(PullDown* self, int32_t id, const char* text);
int32_t PullDown_add_separator(PullDown* self, int32_t id);  // 0x5F8BE0
int32_t PullDown_hide_item(PullDown* self, int32_t id);      // 0x5F8CB0
int32_t PullDown_show_item(PullDown* self, int32_t id);      // 0x5F8D20
int32_t PullDown_disable_item(PullDown* self, int32_t id);   // 0x5F8D90
int32_t PullDown_enable_item(PullDown* self, int32_t id);    // 0x5F8DF0
int32_t PullDown_check_item(PullDown* self, int32_t id);     // 0x5F9040
// PullDown_show (0x5F9170): shows the list at (x, y) (screen; a free pull-down opens at x+1, y+1
// and flips left / up at the screen's edges) and runs the modal loop. Returns the selected index
// (-1: none).
int32_t PullDown_show(PullDown* self, int32_t x, int32_t y);
// PullDown_get_selected (0x5F9F40): the selected index, -1 when none or disabled.
int32_t PullDown_get_selected(const PullDown* self);
// The id of the item at `index` (the caller's table lookup), -1 when out of range.
int32_t pulldown_item_id(const PullDown* self, int32_t index);

// ---- not original functions ----
// Composes the open pull-downs over the screen (win.cpp, ui_present).
void pulldown_draw_windows(Buffer* screen);
// The menu column's pull-down: placed at (x, y) (kept on the screen; PullDown_show's sizes),
// drawn over the screen with its selection, and its row at y (window coordinates; -1 none).
void pulldown_place(PullDown* self, int32_t x, int32_t y);
void pulldown_render(PullDown* self, Buffer* screen);
int32_t pulldown_row_at(const PullDown* self, int32_t y);
