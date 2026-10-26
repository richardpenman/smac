// Script-driven popups of the original UI: BasePop (gfx2 0x600860..0x6051A0, the generic dialog
// built from a section of a game text file) and its subclass Popup (ui 0x4048A0..0x406C00, the
// game's look: box-sprite frame, caption bar, item highlights), with the item group widgets they
// use (Dialog: RadioButton, CheckBox, ListBox, EditGroup) and FlatButton push buttons.
//
// Script sections (Script.txt etc.):
//   #LABEL
//   #xs 320 / #ys / #x / #y / #col / #caption TEXT / #checkbox N / #editbox / #listbox N /
//   #active N / #text / #itemlist / #button TEXT / #extra N / #wave .. #pan / #sprite FILE
//   text lines ("^" new line, "^^" centred, "^" alone an empty line, $-markers expanded)
//   (blank line)
//   item lines (choices; "|" splits a right-aligned part)
//
// The objects are native (not in the memory image): each BasePop holds the original's fields under
// their meaning with the original offsets noted; the drawing is done into the popup's own canvas
// (its GraphicWin Buffer), which the screen composites over the main view (popup_draw_windows).
//
// Exec runs the modal loop (Win_set_modal) on smacx's input: keys as Win_key_down_event /
// Win_key_click_event deliver them (Enter: the OK button, Esc: the cancel button, arrows: the
// item group), mouse clicks on items and buttons. The return value is the original's: the
// selected item id (check boxes: their state bits), -1 when cancelled with a Cancel button.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "engine/memmap.h"
#include "ui/font_text.h"
#include "ui/gfx.h"

// ---- box sprites (interface.pcx frames; load_box_sprites) ----
// A frame of 4 corner sprites, 4 tiled edge sprites and a tiled centre canvas (0x28 bytes each,
// 51 of them at 0x78D528).
struct BoxDef {
    Sprite* corner[4];  // +0x00: top left, top right, bottom left, bottom right
    Sprite* edge[4];    // +0x10: top, bottom, left, right (tiled)
    Buffer* center;     // +0x20: tiled over the inside; also painted over corner/edge pixels of `key`
    int32_t key;        // +0x24
};
BoxDef* box_def(int32_t index);  // 0x78D528 + index * 0x28
BoxDef* box_def_large();         // 0x6846B0: the larger frame (interface windows)
ADDR_NATIVE(0x453A70) void GAMEAPI load_box_sprites();
ADDR_NATIVE(0x5E2DD3) void THISCALL Buffer_box_sprite(Buffer* self, const UiRect* rect, const BoxDef* box);
// Tiles a sprite / canvas over (x, y, w, h) with the pattern origin at (x - sx % w, y - sy % h).
ADDR_NATIVE(0x5DA9F0) int32_t THISCALL Buffer_tile_2(Buffer* self, Sprite* tile, int32_t sx, int32_t sy, int32_t x,
                                                    int32_t y, int32_t w, int32_t h);
ADDR_NATIVE(0x5DA860) int32_t THISCALL Buffer_tile(Buffer* self, Buffer* tile, int32_t sx, int32_t sy, int32_t x,
                                                  int32_t y, int32_t w, int32_t h);
// As Buffer_tile, only over pixels of colour `mask` (Buffer_copy_mask).
ADDR_NATIVE(0x5DA6C0) int32_t THISCALL Buffer_tile_mask(Buffer* self, Buffer* tile, int32_t sx, int32_t sy, int32_t x,
                                                       int32_t y, int32_t w, int32_t h, int32_t mask);

// ---- interface art and colours used by popups (load_interface, textcolor_init, config_popups) ----
// Loads interface.pcx's popup pieces (button bars, check box marks, box sprites), the text
// colours of text.pcx and iface.pcx and the popup fonts, once.
void popup_art_init();
// iface.pcx's colours of load_interface (0x68A5A0 + 4 i).
int32_t g_iface_color(int32_t i);

// ---- BasePop / Popup ----
class BasePop;

// Popup factory of the game (Popup_alloc 0x404FB0): a Popup with the current defaults.
BasePop* popup_new();
void popup_delete(BasePop* pop);

// BasePop methods (the engine-facing ones take the popup object as `this`).
// Reads section `label` of `file` into the popup (flags: bit 0x80 appends to the current
// contents). Returns 0, 1 (section not found), 3 (no label).
int32_t BasePop_start(BasePop* self, const char* file, const char* label, int32_t active, const char* edit_default,
                      uint32_t flags, uint8_t* window);
// Adds a text line ("^"/"^^" prefixes as in scripts) / an item with its id / a push button.
int32_t BasePop_string(BasePop* self, const char* text);
int32_t BasePop_item(BasePop* self, const char* text, int32_t id);
int32_t BasePop_button(BasePop* self, const char* text);
void BasePop_set_loc(BasePop* self, int32_t x, int32_t y);
void BasePop_set_width(BasePop* self, int32_t width);
void BasePop_set_max_rows(BasePop* self, int32_t rows);
// Shows the popup and waits for the answer (no_display: prepare only, return 0).
int32_t BasePop_exec_3(BasePop* self, int32_t no_display, int32_t a2);
// The picture shown left of the text (a PCX file; null for none).
void BasePop_set_picture(BasePop* self, const char* pcx);
// The picture from a sprite (TutWin's unit / tile / base pictures).
void BasePop_set_picture_sprite(BasePop* self, const Sprite* s);
// BasePop_set_ok_text / set_cancel_text: the OK and Cancel buttons' labels.
void BasePop_set_ok_text(BasePop* self, const char* text);
void BasePop_set_cancel_text(BasePop* self, const char* text);
// The window's size after BasePop_exec_3 (also with no_display: the dimensions are computed).
void BasePop_size(BasePop* self, int32_t* w, int32_t* h);
// The pressed #button (0: OK / an item) and the check box state bits after exec.
int32_t BasePop_button_pressed(BasePop* self);
int32_t BasePop_check_state(BasePop* self);
void BasePop_set_check_state(BasePop* self, int32_t bits);
// Text of edit field i after exec (EditGroup_get_text).
const char* BasePop_edit_text(BasePop* self, int32_t i);
// Number of items; item i's text and id; the initial selection (+0xA44).
int32_t BasePop_item_count(BasePop* self);
const char* BasePop_item_text(BasePop* self, int32_t i);
int32_t BasePop_item_id(BasePop* self, int32_t i);
int32_t BasePop_active(BasePop* self);
// An edit field of an edit group (flag 4) labelled `label`, its text `text` (EditGroup_edit).
void BasePop_edit(BasePop* self, const char* label, const char* text, int32_t max_len);
// A popup shown without running it (BasePop_exec_3 of a modeless popup: the PickWin's faction
// text, thumb_routine) at its #x / #y position, until BasePop_hide (or popup_delete).
void BasePop_show(BasePop* self);
void BasePop_hide(BasePop* self);
// A check box mark of interface.pcx (state 0 clear, 1 under the mouse, 2 checked) at (x, y).
void popup_draw_check(Buffer* b, int32_t x, int32_t y, int32_t state);
// Not original functions: the popup's mode (+0x30AC, #extra: 1 message line, 2 base message,
// 0x3EA base message and dialog) and its text lines joined by spaces (Popup_on_exec, POP2).
void BasePop_set_mode(BasePop* self, int32_t mode);
// The initial selection (+0xA44) set after Popup_start (Console_go_to): the item with this id.
void BasePop_set_active(BasePop* self, int32_t id);
// The list's selection callback (Popup +0x3280, e.g. Console_goto_routine): called with the item's
// id when the selection changes while the popup is open.
void BasePop_set_select_callback(BasePop* self, void (*fn)(int32_t));
// Dialog_get_selected_id before the popup is shown: the active item's id, else the first item's.
int32_t BasePop_selected_id(BasePop* self);
// Popup_start (0x406380, win_popup.cpp): BasePop_start with the expansion's alien scripts tried
// first.
int32_t Popup_start(BasePop* pop, const char* file, const char* label, int32_t active, const char* edit,
                    uint32_t flags);
std::string BasePop_text(BasePop* self);
// The #caption of the section(s) read (null for none).
const char* BasePop_caption(BasePop* self);

// ---- the pop module (0x627100..) and the veh module's wrappers (0x5BF310..) ----
// pops_18: start `label` of `file` (initial selection `active`, edit default `edit`), the picture
// `pcx` at scale scale_d/scale_s, check boxes from DialogChoices; exec; DialogChoices and
// DialogToggle (the pressed #button) updated. Returns the answer, -1 if the section is missing.
ADDR_NATIVE(0x6276A0) int32_t GAMEAPI pops_18(const char* file, const char* label, int32_t active, const char* edit,
                                             uint32_t flags, const char* pcx, int32_t scale_d, int32_t scale_s,
                                             int32_t a9);
ADDR_NATIVE(0x6272C0) int32_t GAMEAPI pop_9(const char* file, const char* label, int32_t active, const char* edit,
                                           uint32_t flags, int32_t a6);
// Edit field popup (flags | 0x44: edit group with Cancel); the texts go to the parse slots.
ADDR_NATIVE(0x627910) int32_t GAMEAPI pop_ask_6(const char* file, const char* label, int32_t active, const char* edit,
                                               int32_t a5, uint32_t flags);
// As pop_9 / pops_18 / pop_ask_6 with the alien scripts tried first (Crossfire).
ADDR_NATIVE(0x5BF480) int32_t GAMEAPI X_pop_9(const char* file, const char* label, int32_t active, const char* edit,
                                             uint32_t flags, int32_t a6);
ADDR_NATIVE(0x5BF930) int32_t GAMEAPI X_pops_18(const char* file, const char* label, int32_t active,
                                               const char* edit, uint32_t flags, const char* pcx, int32_t scale_d,
                                               int32_t scale_s, int32_t a9);
ADDR_NATIVE(0x5BFBC0) int32_t GAMEAPI X_pop_ask_6(const char* file, const char* label, int32_t active,
                                                 const char* edit, int32_t a5, uint32_t flags);
// Popup styles: PopupDialogState 0 (normal) / 2 (tutorial).
ADDR_NATIVE(0x588460) void GAMEAPI popups_normal();
ADDR_NATIVE(0x5888E0) void GAMEAPI popups_tutorial();
// popt (tutorial-style popup) is declared in ui/console.h.
// The first line of section `label` of `file`, expanded into StrBuffer (popup_menus.cpp).
ADDR_NATIVE(0x48C060) void GAMEAPI parse_it(const char* file, const char* label);

// ---- the open popup windows, drawn over the screen (ui_present) ----
void popup_draw_windows(Buffer* screen);
// Mouse click on the screen while a popup is modal (handled by the popup loop).
