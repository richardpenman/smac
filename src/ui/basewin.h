// Base window (BaseWin, ui module 0x408490..0x421810): the base screen. Opened with Enter on a
// base (Console_on_key_click), by base_upkeep for the human player's bases, from the reports and
// the map's menus; it shows the resource map, the nutrient / mineral / energy rows, the citizens,
// the facilities, the production with its queue, the supported and present units, and gives the
// controls that change the base: worked tiles, specialists, production (the build menu), hurry,
// queue, facilities (scrap), governor, nerve staple, rename.
//
// The object lives in the engine memory image (BaseWin 0x6A7628, a GraphicWin with a SubInterface
// at +0xA14 that receives the main interface's messages), so its integer fields are used in place
// through the typed view BaseWindow (base_win().base). Its canvas (+0x444), the buttons (+0xD7C0,
// 0xB4C bytes each), the sprites, the clickable spots (+0x40EB4, +0x40EC0) and the hit mask
// (+0x40ECC) are native (BaseWinNative). The window draws into two canvases: its own (758 x 490 at
// 1024 x 768, above the console) and the main interface's (MainInfc 0x7AE820, the console at the
// bottom: base name, citizens, queue, production, units). OpenSMAC keeps a native main interface
// canvas for it (basewin_mi_canvas): the main interface itself (MainInterface_init / on_redraw) is
// not reconstructed, only the frame art and the pane rectangles the base window uses.
//
// OpenSMAC replaces the headless "basewin" area (src/headless/ui_headless_basewin.cpp: the BaseWin
// functions of engine/ui_bridge.h, Win_is_visible and GraphicWin_redraw) with these files.
#pragma once
#include <cstdint>

#include "engine/memmap.h"
#include "engine/ui_bridge.h"
#include "ui/gfx.h"

// ---- the BaseWin object (0x6A7628): a view of its fields at the original's offsets ----
// A GraphicWin (its client rectangle at +0x474) with a SubInterface at +0xA14 and the resource
// map's MapWin at +0x1E674. The parts behind pointers (canvas, buttons, spots, the hit mask, the
// font's handle) are native (BaseWinNative); the Font object at +0x40E8C, the spot lists
// (+0x40EB4, +0x40EC0), the mask (+0x40ECC) and the buttons (+0xD7C0) are reached by offset.
struct BaseWindow {
    uint8_t unk_0[0x474];
    UiRect client;  // +0x474: the GraphicWin's client rectangle
    uint8_t unk_484[0xA18 - 0x484];
    int32_t sub_mode;  // +0xA18: the SubInterface's mode (3)
    uint8_t unk_a1c[0x1A1E4 - 0xA1C];
    int32_t energy_layout;  // +0x1A1E4: the energy pane's alternative layout
    uint8_t unk_1a1e8[0x1E670 - 0x1A1E8];
    int32_t model_turn;  // +0x1E670: the production model's rotation (degrees - 0x1E; in_ECX[0x799C])
    uint8_t map[0x40AF4 - 0x1E674];  // +0x1E674: the resource map (a MapWin, GraphicWin at +0x21A6C)
    int32_t unk_40af4;  // +0x40AF4
    int32_t unk_40af8;  // +0x40AF8
    int32_t pane;  // +0x40AFC: lower pane: 0 resources, 1 psych, 2 support (buttons 10, 11, 9)
    int32_t unk_40b00;  // +0x40B00: -1 at init and open
    int32_t unk_40b04;
    int32_t unk_40b08;
    int32_t base;  // +0x40B0C: base shown
    int32_t fac_scroll;  // +0x40B10: first row of the facilities list
    int32_t sup_scroll;  // +0x40B14: first row of the units supported pane
    int32_t unk_40b18;  // +0x40B18: 1 when the window opens
    int32_t unk_40b1c;  // +0x40B1C: -1 at init
    int32_t sup_bar;  // +0x40B20: the units supported pane scrolls (its bar is shown)
    int32_t unk_40b24;  // +0x40B24: -1 at init
    int32_t open;  // +0x40B28: set while the window is open (BaseWin_zoom's wait loop)
    int32_t queue_sel;  // +0x40B2C: queue slot picked for the build menu (0: production)
    int32_t queue_hilite;  // +0x40B30: queue slot drawn highlighted (0: none)
    int32_t prod_at_open;  // +0x40B34: production when the window was opened (retooling)
    int32_t hurried;  // +0x40B38: minerals after the last hurry
    int32_t unk_40b3c;
    int32_t unk_40b40;  // +0x40B40: 0 at init
    int32_t down_x;  // +0x40B44: left button press position
    int32_t down_y;  // +0x40B48
    int32_t drag_type;  // +0x40B4C: spot type under the press (-1 none)
    int32_t drag_index;  // +0x40B50: spot index under the press
    int32_t drag_state;  // +0x40B54: 0 pressed on a spot, > 0 dragging, -1 none
    int32_t unk_40b58;  // +0x40B58: 0xDF at init
    // Rectangles; MI: in the main interface's canvas, else in the window's.
    UiRect rect_prod_mi;  // +0x40B5C: production name
    UiRect rect_prod2_mi;  // +0x40B6C: production picture
    UiRect rect_prod_pane_mi;  // +0x40B7C: production pane (MainInfc pane at 0xDB0 + 4)
    UiRect rect_map;  // +0x40B8C: resource map
    UiRect rect_fac_title;  // +0x40B9C
    UiRect rect_fac_list;  // +0x40BAC
    UiRect rect_fac_pane;  // +0x40BBC
    UiRect rect_commerce;  // +0x40BCC
    UiRect rect_commerce2;  // +0x40BDC
    UiRect rect_commerce_pane;  // +0x40BEC
    UiRect rect_nutrients;  // +0x40BFC
    UiRect rect_nut_title;  // +0x40C0C
    UiRect rect_nut_pane;  // +0x40C1C
    UiRect rect_garrison_mi;  // +0x40C2C: units in the base (the bottom pane)
    UiRect rect_alloc;  // +0x40C3C: energy allocation (the lower pane)
    UiRect rect_expenses;  // +0x40C4C: resource rows
    UiRect rect_pop_mi;  // +0x40C5C: citizens row
    UiRect rect_queue_mi;  // +0x40C6C: queue pane
    UiRect rect_queue_list_mi;  // +0x40C7C
    UiRect rect_queue_list2_mi;  // +0x40C8C
    UiRect rect_queue_box_mi;  // +0x40C9C
    UiRect rect_name_mi;  // +0x40CAC: base name box
    UiRect rect_exp[6];  // +0x40CBC: resource row rectangles (draw_expenses)
    UiRect rect_misc;  // +0x40D1C: M.Y., energy, eco-damage
    UiRect rect_ok;  // +0x40D2C
    UiRect rect_rename;  // +0x40D3C
    UiRect rect_prev_mi;  // +0x40D4C
    UiRect rect_next_mi;  // +0x40D5C
    UiRect rect_explore;  // +0x40D6C: governor modes (buttons 4..7)
    UiRect rect_discover;  // +0x40D7C
    UiRect rect_build;  // +0x40D8C
    UiRect rect_conquer;  // +0x40D9C
    UiRect rect_governor;  // +0x40DAC: the governor (button 8)
    UiRect rect_support;  // +0x40DBC: the lower pane's buttons (9 support, 10 resources, 11 psych)
    UiRect rect_resources;  // +0x40DCC
    UiRect rect_psych;  // +0x40DDC
    UiRect rect_change_mi;  // +0x40DEC
    UiRect rect_hurry_mi;  // +0x40DFC
    UiRect rect_40e0c[2];  // +0x40E0C
    UiRect rect_staple_mi;  // +0x40E2C
    UiRect rect_40e3c;  // +0x40E3C: button 0x11 (800 x 600 only)
    UiRect rect_gov_advanced;  // +0x40E4C: button 0x12 (the governor's advanced settings)
    UiRect rect_40e5c;  // +0x40E5C: button 0x13
    UiRect rect_queue_btn_mi;  // +0x40E6C
    UiRect rect_editor;  // +0x40E7C
};
SMAC_LAYOUT(BaseWindow, 0x40E8C);
static_assert(offsetof(BaseWindow, client) == 0x474, "BaseWindow layout");
static_assert(offsetof(BaseWindow, sub_mode) == 0xA18, "BaseWindow layout");
static_assert(offsetof(BaseWindow, energy_layout) == 0x1A1E4, "BaseWindow layout");
static_assert(offsetof(BaseWindow, model_turn) == 0x1E670, "BaseWindow layout");
static_assert(offsetof(BaseWindow, map) == 0x1E674, "BaseWindow layout");
static_assert(offsetof(BaseWindow, pane) == 0x40AFC, "BaseWindow layout");
static_assert(offsetof(BaseWindow, base) == 0x40B0C, "BaseWindow layout");
static_assert(offsetof(BaseWindow, fac_scroll) == 0x40B10, "BaseWindow layout");
static_assert(offsetof(BaseWindow, sup_scroll) == 0x40B14, "BaseWindow layout");
static_assert(offsetof(BaseWindow, sup_bar) == 0x40B20, "BaseWindow layout");
static_assert(offsetof(BaseWindow, open) == 0x40B28, "BaseWindow layout");
static_assert(offsetof(BaseWindow, queue_sel) == 0x40B2C, "BaseWindow layout");
static_assert(offsetof(BaseWindow, queue_hilite) == 0x40B30, "BaseWindow layout");
static_assert(offsetof(BaseWindow, prod_at_open) == 0x40B34, "BaseWindow layout");
static_assert(offsetof(BaseWindow, hurried) == 0x40B38, "BaseWindow layout");
static_assert(offsetof(BaseWindow, down_x) == 0x40B44, "BaseWindow layout");
static_assert(offsetof(BaseWindow, down_y) == 0x40B48, "BaseWindow layout");
static_assert(offsetof(BaseWindow, drag_type) == 0x40B4C, "BaseWindow layout");
static_assert(offsetof(BaseWindow, drag_index) == 0x40B50, "BaseWindow layout");
static_assert(offsetof(BaseWindow, drag_state) == 0x40B54, "BaseWindow layout");
static_assert(offsetof(BaseWindow, rect_prod_mi) == 0x40B5C, "BaseWindow layout");
static_assert(offsetof(BaseWindow, rect_prod2_mi) == 0x40B6C, "BaseWindow layout");
static_assert(offsetof(BaseWindow, rect_prod_pane_mi) == 0x40B7C, "BaseWindow layout");
static_assert(offsetof(BaseWindow, rect_map) == 0x40B8C, "BaseWindow layout");
static_assert(offsetof(BaseWindow, rect_fac_title) == 0x40B9C, "BaseWindow layout");
static_assert(offsetof(BaseWindow, rect_fac_list) == 0x40BAC, "BaseWindow layout");
static_assert(offsetof(BaseWindow, rect_fac_pane) == 0x40BBC, "BaseWindow layout");
static_assert(offsetof(BaseWindow, rect_commerce) == 0x40BCC, "BaseWindow layout");
static_assert(offsetof(BaseWindow, rect_commerce2) == 0x40BDC, "BaseWindow layout");
static_assert(offsetof(BaseWindow, rect_commerce_pane) == 0x40BEC, "BaseWindow layout");
static_assert(offsetof(BaseWindow, rect_nutrients) == 0x40BFC, "BaseWindow layout");
static_assert(offsetof(BaseWindow, rect_nut_title) == 0x40C0C, "BaseWindow layout");
static_assert(offsetof(BaseWindow, rect_nut_pane) == 0x40C1C, "BaseWindow layout");
static_assert(offsetof(BaseWindow, rect_garrison_mi) == 0x40C2C, "BaseWindow layout");
static_assert(offsetof(BaseWindow, rect_alloc) == 0x40C3C, "BaseWindow layout");
static_assert(offsetof(BaseWindow, rect_expenses) == 0x40C4C, "BaseWindow layout");
static_assert(offsetof(BaseWindow, rect_pop_mi) == 0x40C5C, "BaseWindow layout");
static_assert(offsetof(BaseWindow, rect_queue_mi) == 0x40C6C, "BaseWindow layout");
static_assert(offsetof(BaseWindow, rect_queue_list_mi) == 0x40C7C, "BaseWindow layout");
static_assert(offsetof(BaseWindow, rect_queue_list2_mi) == 0x40C8C, "BaseWindow layout");
static_assert(offsetof(BaseWindow, rect_queue_box_mi) == 0x40C9C, "BaseWindow layout");
static_assert(offsetof(BaseWindow, rect_name_mi) == 0x40CAC, "BaseWindow layout");
static_assert(offsetof(BaseWindow, rect_exp) == 0x40CBC, "BaseWindow layout");
static_assert(offsetof(BaseWindow, rect_misc) == 0x40D1C, "BaseWindow layout");
static_assert(offsetof(BaseWindow, rect_ok) == 0x40D2C, "BaseWindow layout");
static_assert(offsetof(BaseWindow, rect_rename) == 0x40D3C, "BaseWindow layout");
static_assert(offsetof(BaseWindow, rect_prev_mi) == 0x40D4C, "BaseWindow layout");
static_assert(offsetof(BaseWindow, rect_next_mi) == 0x40D5C, "BaseWindow layout");
static_assert(offsetof(BaseWindow, rect_explore) == 0x40D6C, "BaseWindow layout");
static_assert(offsetof(BaseWindow, rect_discover) == 0x40D7C, "BaseWindow layout");
static_assert(offsetof(BaseWindow, rect_build) == 0x40D8C, "BaseWindow layout");
static_assert(offsetof(BaseWindow, rect_conquer) == 0x40D9C, "BaseWindow layout");
static_assert(offsetof(BaseWindow, rect_governor) == 0x40DAC, "BaseWindow layout");
static_assert(offsetof(BaseWindow, rect_support) == 0x40DBC, "BaseWindow layout");
static_assert(offsetof(BaseWindow, rect_resources) == 0x40DCC, "BaseWindow layout");
static_assert(offsetof(BaseWindow, rect_psych) == 0x40DDC, "BaseWindow layout");
static_assert(offsetof(BaseWindow, rect_change_mi) == 0x40DEC, "BaseWindow layout");
static_assert(offsetof(BaseWindow, rect_hurry_mi) == 0x40DFC, "BaseWindow layout");
static_assert(offsetof(BaseWindow, rect_40e0c) == 0x40E0C, "BaseWindow layout");
static_assert(offsetof(BaseWindow, rect_staple_mi) == 0x40E2C, "BaseWindow layout");
static_assert(offsetof(BaseWindow, rect_40e3c) == 0x40E3C, "BaseWindow layout");
static_assert(offsetof(BaseWindow, rect_gov_advanced) == 0x40E4C, "BaseWindow layout");
static_assert(offsetof(BaseWindow, rect_40e5c) == 0x40E5C, "BaseWindow layout");
static_assert(offsetof(BaseWindow, rect_queue_btn_mi) == 0x40E6C, "BaseWindow layout");
static_assert(offsetof(BaseWindow, rect_editor) == 0x40E7C, "BaseWindow layout");
inline BaseWindow& base_win(uint8_t* win) { return *reinterpret_cast<BaseWindow*>(win); }
inline BaseWindow& base_win() { return base_win(&BaseWin); }

// ---- BaseWin parts reached by offset (objects of other classes, mostly native) ----
constexpr uint32_t kBwFont = 0x40E8C;  // Font (Arial 14 bold): the buttons' font
constexpr uint32_t kBwSpotsIface = 0x40EB4;  // Spot list of the main interface panes
constexpr uint32_t kBwSpotsWin = 0x40EC0;  // Spot list of the window
constexpr uint32_t kBwMask = 0x40ECC;  // Buffer 100 x 50: the tile diamond (BaseWin_farm_get)
constexpr uint32_t kBwButtons = 0xD7C0;  // BaseButton[22], 0xB4C bytes each
constexpr uint32_t kBwButtonSize = 0xB4C;
constexpr uint32_t kBwButtonStaple2 = 0xBE80;  // a second button 0x10 (ter1wreck.pcx art)

// A rectangle of BaseWindow by its offset (the tables of rectangles: tours, expense rows).
inline UiRect& bw_rect(uint32_t off) { return *reinterpret_cast<UiRect*>(&BaseWin + off); }

// ---- the main interface (MainInfc 0x7AE820): a view of its rectangles at the original's offsets ----
// Rectangles in the main interface's canvas (the console at the bottom of the screen) unless noted.
struct MainInterface {
    uint8_t unk_0[0xA1C];
    UiRect pos;  // +0xA1C: the window rectangle (screen)
    UiRect info_buttons[3];  // +0xA2C: the info pane's buttons (1000, 0x3E9, 0x3EA)
    UiRect rect_a5c[2];  // +0xA5C
    UiRect turn_complete;  // +0xA7C: spot 5 ("TURN COMPLETE")
    UiRect globe2;  // +0xA8C: the globe buttons
    UiRect globe1;  // +0xA9C
    UiRect arrow_left;  // +0xAAC: the stack row's scroll arrows
    UiRect rect_abc;  // +0xABC
    UiRect arrow_right;  // +0xACC
    UiRect paint[31];  // +0xADC: the scenario editor's 25 paint and 6 brush buttons (editpane.cpp)
    UiRect tile_pane;  // +0xCCC: StatusWin's panes: the tile
    UiRect unit_pane;  // +0xCDC: the unit (7/12 of the left pane)
    UiRect stack_pane;  // +0xCEC: the stack
    UiRect world_map;  // +0xCFC: the world map pane
    UiRect date;  // +0xD0C: the date box
    UiRect text;  // +0xD1C: the info pane's text
    UiRect rect_d2c;  // +0xD2C
    UiRect battle;  // +0xD3C: the battle report (BattleWin)
    UiRect paint_box;  // +0xD4C: the scenario editor's boxes (editpane.cpp)
    UiRect brush_box;  // +0xD5C
    UiRect text_box;  // +0xD6C
    int32_t height;  // +0xD7C: the console's height (0xD5; MiConsoleHeight)
    UiRect pane_queue;  // +0xD80: the panes (MainInfc coordinates)
    UiRect pane_right;  // +0xD90
    UiRect pane_center;  // +0xDA0
    UiRect pane_bottom;  // +0xDB0
    UiRect pane_center2;  // +0xDC0: the panes while the console is slid down
    UiRect pane_bottom2;  // +0xDD0
};
SMAC_LAYOUT(MainInterface, 0xDE0);
static_assert(offsetof(MainInterface, pos) == 0xA1C, "MainInterface layout");
static_assert(offsetof(MainInterface, info_buttons) == 0xA2C, "MainInterface layout");
static_assert(offsetof(MainInterface, rect_a5c) == 0xA5C, "MainInterface layout");
static_assert(offsetof(MainInterface, turn_complete) == 0xA7C, "MainInterface layout");
static_assert(offsetof(MainInterface, globe2) == 0xA8C, "MainInterface layout");
static_assert(offsetof(MainInterface, globe1) == 0xA9C, "MainInterface layout");
static_assert(offsetof(MainInterface, arrow_left) == 0xAAC, "MainInterface layout");
static_assert(offsetof(MainInterface, rect_abc) == 0xABC, "MainInterface layout");
static_assert(offsetof(MainInterface, arrow_right) == 0xACC, "MainInterface layout");
static_assert(offsetof(MainInterface, paint) == 0xADC, "MainInterface layout");
static_assert(offsetof(MainInterface, tile_pane) == 0xCCC, "MainInterface layout");
static_assert(offsetof(MainInterface, unit_pane) == 0xCDC, "MainInterface layout");
static_assert(offsetof(MainInterface, stack_pane) == 0xCEC, "MainInterface layout");
static_assert(offsetof(MainInterface, world_map) == 0xCFC, "MainInterface layout");
static_assert(offsetof(MainInterface, date) == 0xD0C, "MainInterface layout");
static_assert(offsetof(MainInterface, text) == 0xD1C, "MainInterface layout");
static_assert(offsetof(MainInterface, rect_d2c) == 0xD2C, "MainInterface layout");
static_assert(offsetof(MainInterface, battle) == 0xD3C, "MainInterface layout");
static_assert(offsetof(MainInterface, paint_box) == 0xD4C, "MainInterface layout");
static_assert(offsetof(MainInterface, brush_box) == 0xD5C, "MainInterface layout");
static_assert(offsetof(MainInterface, text_box) == 0xD6C, "MainInterface layout");
static_assert(offsetof(MainInterface, height) == 0xD7C, "MainInterface layout");
static_assert(offsetof(MainInterface, pane_queue) == 0xD80, "MainInterface layout");
static_assert(offsetof(MainInterface, pane_right) == 0xD90, "MainInterface layout");
static_assert(offsetof(MainInterface, pane_center) == 0xDA0, "MainInterface layout");
static_assert(offsetof(MainInterface, pane_bottom) == 0xDB0, "MainInterface layout");
static_assert(offsetof(MainInterface, pane_center2) == 0xDC0, "MainInterface layout");
static_assert(offsetof(MainInterface, pane_bottom2) == 0xDD0, "MainInterface layout");
inline MainInterface& main_interface() { return *reinterpret_cast<MainInterface*>(&MainInterfaceWin); }
// A rectangle of MainInterface by its offset (tables of rectangles).
inline UiRect& mi_rect(uint32_t off) { return *reinterpret_cast<UiRect*>(&MainInterfaceWin + off); }
GLOBAL(int32_t, MiConsoleHeight, 0x7AF59C);  // DAT_007AF59C (MainInfc + 0xD7C)
GLOBAL(int32_t, MiSlideState, 0x7D3930);  // DAT_007D3930: the console slid down (restore_back)

// Text colour pairs of text.pcx (textcolor_init 0x4BA220): {colour, shadow (-1 none)} pairs
// from 0x8C6B5C to 0x8C6E68 (the triples at 0x8C6C24..0x8C6C6C).
GLOBAL(int32_t[195], TextColors, 0x8C6B5C);
inline int32_t text_color(uint32_t addr) { return *reinterpret_cast<int32_t*>(mem::host(addr)); }
// Base names drawn with inverted text (BASENAME.txt #INVERTLIST, 32 bytes each; count 0x6EEE60).
GLOBAL(char[8][32], BaseInvertNames, 0x6A7528);
GLOBAL(int32_t, BaseInvertCount, 0x6EEE60);

// ---- original functions (ADDR_NATIVE) ----
// Also defined here (engine/ui_bridge.h): BaseWin_zoom, BaseWin_on_redraw, BaseWin_close,
// BaseWin_focus, BaseWin_check_base, BaseWin_check_loc, BaseWin_check_veh, BaseWin_base_killed,
// Win_is_visible, GraphicWin_redraw.
ADDR_NATIVE(0x41E8D0) void THISCALL BaseWin_init(uint8_t* self);
ADDR_NATIVE(0x41A4C0) void THISCALL BaseWin_exit(uint8_t* self);
ADDR_NATIVE(0x41AE40) void THISCALL BaseWin_soft_zoom(uint8_t* self, int32_t base_id);
ADDR_NATIVE(0x41A010) void THISCALL BaseWin_prev(uint8_t* self);
ADDR_NATIVE(0x41A160) void THISCALL BaseWin_next(uint8_t* self);
ADDR_NATIVE(0x41A2B0) void THISCALL BaseWin_rename(uint8_t* self);
ADDR_NATIVE(0x416D90) void THISCALL BaseWin_recompute(uint8_t* self, int32_t a1);
// The tile under (x, y) of the resource map: its radius index (0 the base), -1 outside.
ADDR_NATIVE(0x409100) int32_t THISCALL BaseWin_farm_get(uint8_t* self, int32_t x, int32_t y);
// Clicks on the resource map: work / free a tile (or move a worker dragged from another tile).
ADDR_NATIVE(0x40A1D0) void THISCALL BaseWin_farm_click(uint8_t* self, int32_t x, int32_t y, int32_t right);
// Clicks on a citizen: the specialist menu (CITIZEN).
ADDR_NATIVE(0x40A880)
void THISCALL BaseWin_pop_click(uint8_t* self, int32_t index, int32_t x, int32_t y, int32_t right);
// Facilities list: the facility's menu (scrap: SELLFAC).
ADDR_NATIVE(0x40C070)
void THISCALL BaseWin_facilities_click(uint8_t* self, int32_t index, int32_t x, int32_t y, int32_t right);
// Queue pane clicks.
ADDR_NATIVE(0x409200)
void THISCALL BaseWin_queue_click(uint8_t* self, int32_t index, int32_t x, int32_t y, int32_t right);
// Supported / present units: the unit's menu.
ADDR_NATIVE(0x40B140)
void THISCALL BaseWin_garrison_click(uint8_t* self, int32_t index, int32_t x, int32_t y, int32_t right);
// The build menu (queue slot `slot`, 0: the production; `a2`: no network lock).
ADDR_NATIVE(0x417040) void THISCALL BaseWin_production(uint8_t* self, int32_t slot, int32_t a2);
// Puts item into the queue slot picked (BaseWindow::queue_sel); insert: before it.
ADDR_NATIVE(0x416DE0) void THISCALL BaseWin_add_queue(uint8_t* self, int32_t item, int32_t insert);
ADDR_NATIVE(0x416FB0) void THISCALL BaseWin_delete_queue(uint8_t* self, int32_t slot);
// Hurry production (HURRY / HURRYPAY...): the cost formula of the original UI.
ADDR_NATIVE(0x418DF0) void THISCALL BaseWin_hurry(uint8_t* self);
// The governor settings menu.
// a1: do not apply the settings now. Returns 1 when nothing was changed.
ADDR_NATIVE(0x419A90) int32_t THISCALL BaseWin_governor(uint8_t* self, int32_t a1);
// Governor buttons: 0 governor on/off, 0x1000000.. explore / discover / build / conquer.
ADDR_NATIVE(0x415F70) void THISCALL BaseWin_gov_on(uint8_t* self, uint32_t flag);
ADDR_NATIVE(0x415F00) void THISCALL BaseWin_gov_activate(uint8_t* self);
ADDR_NATIVE(0x41B4F0) void THISCALL BaseWin_nerve_staple(uint8_t* self);
ADDR_NATIVE(0x4165D0) void THISCALL BaseWin_click(uint8_t* self, int32_t x, int32_t y, int32_t right, int32_t a4);
ADDR_NATIVE(0x4160F0) void THISCALL BaseWin_iface_click(uint8_t* self, int32_t x, int32_t y, int32_t right, int32_t a4);
ADDR_NATIVE(0x41D500) void THISCALL BaseWin_on_button_clicked(uint8_t* self, int32_t id);
ADDR_NATIVE(0x41E560) void THISCALL BaseWin_update_gov_buttons(uint8_t* self);
// Drawing (the panes; flag 1: present now, 0: with the next update).
ADDR_NATIVE(0x40E360) void THISCALL BaseWin_draw_production(uint8_t* self, int32_t flag);
ADDR_NATIVE(0x412AA0) void THISCALL BaseWin_draw_expenses(uint8_t* self, int32_t flag);
ADDR_NATIVE(0x415080) void THISCALL BaseWin_draw_queue(uint8_t* self, int32_t flag);
ADDR_NATIVE(0x415B20) void THISCALL BaseWin_draw_info(uint8_t* self, int32_t flag);
ADDR_NATIVE(0x415730) void THISCALL BaseWin_draw_misc(uint8_t* self, int32_t flag);
ADDR_NATIVE(0x40F0F0) void THISCALL pfncDrawCityMap(uint8_t* self, int32_t flag);
ADDR_NATIVE(0x414B40) void THISCALL BaseWin_draw_pop(uint8_t* self, int32_t flag);
ADDR_NATIVE(0x40FCC0) void THISCALL BaseWin_draw_facilities(uint8_t* self, int32_t flag);
ADDR_NATIVE(0x410870) void THISCALL BaseWin_draw_commerce(uint8_t* self, int32_t flag);
ADDR_NATIVE(0x411240) void THISCALL BaseWin_draw_nutrients(uint8_t* self, int32_t flag);
ADDR_NATIVE(0x40C850) void THISCALL BaseWin_draw_supported(uint8_t* self, int32_t flag);
ADDR_NATIVE(0x4103B0) void THISCALL BaseWin_draw_garrison(uint8_t* self, int32_t flag);
ADDR_NATIVE(0x411980) void THISCALL BaseWin_draw_energy_alloc(uint8_t* self, int32_t flag);
ADDR_NATIVE(0x408B20) void THISCALL BaseWin_draw_psych(uint8_t* self, int32_t flag);
ADDR_NATIVE(0x408EB0) void THISCALL BaseWin_draw_support(uint8_t* self, int32_t flag);
// The polygon's pixels translated through a colour table (gfx2 polygon module).
ADDR_NATIVE(0x626E10) int32_t GAMEAPI polygon_dest(Buffer* self, const int32_t* pts, int32_t n, const uint8_t* table);
ADDR_NATIVE(0x5882F0) Font* GAMEAPI find_font(int32_t size, int32_t style);
// A number entry popup (pop_ask_number_4 through the alien scripts); the number goes to
// ParseNumTable[0]. Returns 0 when confirmed.
ADDR_NATIVE(0x5BFD10) int32_t GAMEAPI X_pop_ask_number(const char* label, int32_t value, int32_t a3);
// synch_energy (multiplayer: a faction's energy): engine/net.h.

// ---- not original functions ----
// The facility / secret project picture (facs\facNNN.pcx, projs\projNNN.pcx; fac >= 0x46: project).
Sprite* basewin_facility_picture(int32_t fac);
// The keys of the base window (the part of Console_on_key_click 0x517FB8..0x518177 for a visible
// base window, then ctrl+Enter / ctrl+space; the original's other console keys are not passed on).
// key: the lower-cased character; net_busy: multiplayer input not ours. Returns 1.
int32_t basewin_console_key(uint8_t* console, int32_t ch, int32_t vk, int32_t key, int32_t net_busy);
// Draws the open base window over the screen (ui_present): the main interface canvas over the
// window's canvas (index 9 transparent), then the buttons.
void basewin_compose(Buffer* screen);
// Mouse input while the base window is open (screen coordinates); false when it is closed.
bool basewin_mouse(int32_t type, int32_t x, int32_t y, bool double_click, bool right);
// The canvases (for tests: opensmac --keys BASEDUMP:FILE writes them in the oracle's basedump format).
Buffer* basewin_canvas();
Buffer* basewin_mi_canvas();
bool basewin_dump(const char* path);
// The open base's state on stderr ("basestate ..." lines; opensmac --keys BASESTATE).
void basewin_log_state();

// ---- the scenario editor's base menu (editor_base.cpp) ----
ADDR_NATIVE(0x41C7A0) void THISCALL BaseWin_base_editor(uint8_t* win);
ADDR_NATIVE(0x41B790) void THISCALL BaseWin_base_editor_fac(uint8_t* win);
ADDR_NATIVE(0x41BBC0) void THISCALL BaseWin_base_editor_proj(uint8_t* win);
ADDR_NATIVE(0x41C240) void THISCALL BaseWin_base_editor_copy(uint8_t* win, int32_t to);
