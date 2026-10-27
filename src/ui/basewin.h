// Base window (BaseWin, ui module 0x408490..0x421810): the base screen. Opened with Enter on a
// base (Console_on_key_click), by base_upkeep for the human player's bases, from the reports and
// the map's menus; it shows the resource map, the nutrient / mineral / energy rows, the citizens,
// the facilities, the production with its queue, the supported and present units, and gives the
// controls that change the base: worked tiles, specialists, production (the build menu), hurry,
// queue, facilities (scrap), governor, nerve staple, rename.
//
// The object lives in the engine memory image (BaseWin 0x6A7628, a GraphicWin with a SubInterface
// at +0xA14 that receives the main interface's messages), so its integer fields are used in place
// (bwf(offset)). Its canvas (+0x444), the buttons (+0xD7C0, 0xB4C bytes each), the sprites, the
// clickable spots (+0x40EB4, +0x40EC0) and the hit mask (+0x40ECC) are native (BaseWinNative).
// The window draws into two canvases: its own (758 x 490 at 1024 x 768, above the console) and
// the main interface's (MainInfc 0x7AE820, the console at the bottom: base name, citizens, queue,
// production, units). smacx keeps a native main interface canvas for it (basewin_mi_canvas): the
// main interface itself (MainInterface_init / on_redraw) is not reconstructed, only the frame
// art and the pane rectangles the base window uses.
//
// smacx replaces the headless "basewin" area (src/headless/ui_headless_basewin.cpp: the BaseWin
// functions of engine/ui_bridge.h, Win_is_visible and GraphicWin_redraw) with these files.
#pragma once
#include <cstdint>

#include "engine/memmap.h"
#include "engine/ui_bridge.h"
#include "ui/gfx.h"

// ---- BaseWin fields (byte offsets of the original object) ----
constexpr uint32_t kBwClient = 0x474;  // GraphicWin client rect
constexpr uint32_t kBwPane = 0x40AFC;  // lower pane: 0 resources, 1 psych, 2 support (buttons 10, 11, 9)
constexpr uint32_t kBwBase = 0x40B0C;  // base shown
constexpr uint32_t kBwB10 = 0x40B10;
constexpr uint32_t kBwB14 = 0x40B14;
constexpr uint32_t kBwB18 = 0x40B18;
constexpr uint32_t kBwOpen = 0x40B28;  // set while the window is open (BaseWin_zoom's wait loop)
constexpr uint32_t kBwQueueSel = 0x40B2C;  // queue slot picked for the build menu (0: production)
constexpr uint32_t kBwProdAtOpen = 0x40B34;  // production when the window was opened (retooling)
constexpr uint32_t kBwHurried = 0x40B38;  // minerals after the last hurry
constexpr uint32_t kBwDownX = 0x40B44;  // left button press position
constexpr uint32_t kBwDownY = 0x40B48;
constexpr uint32_t kBwDragType = 0x40B4C;  // spot type under the press (-1 none)
constexpr uint32_t kBwDragIndex = 0x40B50;  // spot index under the press
constexpr uint32_t kBwDragState = 0x40B54;  // 0 pressed on a spot, > 0 dragging, -1 none
// Rectangles (left, top, right, bottom) set by BaseWin_init; window coordinates unless noted (MI:
// main interface coordinates).
constexpr uint32_t kBwRectProdMI = 0x40B5C;  // production name
constexpr uint32_t kBwRectProd2MI = 0x40B6C;  // production picture
constexpr uint32_t kBwRectProdPaneMI = 0x40B7C;  // production pane (MainInfc pane at 0xDB0 + 4)
constexpr uint32_t kBwRectMap = 0x40B8C;  // resource map
constexpr uint32_t kBwRectFacTitle = 0x40B9C;
constexpr uint32_t kBwRectFacList = 0x40BAC;
constexpr uint32_t kBwRectFacPane = 0x40BBC;
constexpr uint32_t kBwRectCommerce = 0x40BCC;
constexpr uint32_t kBwRectCommerce2 = 0x40BDC;
constexpr uint32_t kBwRectCommercePane = 0x40BEC;
constexpr uint32_t kBwRectNutrients = 0x40BFC;
constexpr uint32_t kBwRectNutTitle = 0x40C0C;
constexpr uint32_t kBwRectNutPane = 0x40C1C;
constexpr uint32_t kBwRectGarrisonMI = 0x40C2C;  // units in the base (the bottom pane)
constexpr uint32_t kBwRectAlloc = 0x40C3C;  // energy allocation (the lower pane)
constexpr uint32_t kBwRectExpenses = 0x40C4C;  // resource rows
constexpr uint32_t kBwRectPopMI = 0x40C5C;  // citizens row
constexpr uint32_t kBwRectQueueMI = 0x40C6C;  // queue pane
constexpr uint32_t kBwRectQueueListMI = 0x40C7C;
constexpr uint32_t kBwRectQueueList2MI = 0x40C8C;
constexpr uint32_t kBwRectQueueBoxMI = 0x40C9C;
constexpr uint32_t kBwRectNameMI = 0x40CAC;  // base name box
constexpr uint32_t kBwRectExp1 = 0x40CBC;  // resource row rectangles (draw_expenses)
constexpr uint32_t kBwRectMisc = 0x40D1C;  // M.Y., energy, eco-damage
constexpr uint32_t kBwRectOk = 0x40D2C;
constexpr uint32_t kBwRectRename = 0x40D3C;
constexpr uint32_t kBwRectPrevMI = 0x40D4C;
constexpr uint32_t kBwRectNextMI = 0x40D5C;
constexpr uint32_t kBwRectChangeMI = 0x40DEC;
constexpr uint32_t kBwRectHurryMI = 0x40DFC;
constexpr uint32_t kBwRectStapleMI = 0x40E2C;
constexpr uint32_t kBwRectQueueBtnMI = 0x40E6C;
constexpr uint32_t kBwRectEditor = 0x40E7C;
constexpr uint32_t kBwFont = 0x40E8C;  // Font (Arial 14 bold): the buttons' font
constexpr uint32_t kBwSpotsIface = 0x40EB4;  // Spot list of the main interface panes
constexpr uint32_t kBwSpotsWin = 0x40EC0;  // Spot list of the window
constexpr uint32_t kBwMask = 0x40ECC;  // Buffer 100 x 50: the tile diamond (BaseWin_farm_get)
constexpr uint32_t kBwButtons = 0xD7C0;  // BaseButton[22], 0xB4C bytes each
constexpr uint32_t kBwButtonSize = 0xB4C;
constexpr uint32_t kBwButtonStaple2 = 0xBE80;  // a second button 0x10 (ter1wreck.pcx art)

inline int32_t& bwf(uint32_t off) { return *reinterpret_cast<int32_t*>(&BaseWin + off); }
inline UiRect& bw_rect(uint32_t off) { return *reinterpret_cast<UiRect*>(&BaseWin + off); }

// ---- the main interface (MainInfc 0x7AE820) fields the base window uses ----
constexpr uint32_t kMiPos = 0xA1C;  // window rectangle (screen)
constexpr uint32_t kMiPaneQueue = 0xD80;  // pane rectangles (MainInfc coordinates)
constexpr uint32_t kMiPaneRight = 0xD90;
constexpr uint32_t kMiPaneCenter = 0xDA0;
constexpr uint32_t kMiPaneBottom = 0xDB0;
constexpr uint32_t kMiPaneCenter2 = 0xDC0;
constexpr uint32_t kMiPaneBottom2 = 0xDD0;
constexpr uint32_t kMiHeight = 0xD7C;  // console height (0xD5)
inline int32_t& mif(uint32_t off) { return *reinterpret_cast<int32_t*>(&MainInterfaceWin + off); }
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

// ---- original functions (ADDR_NATIVE: hybrid builds call the original UI) ----
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
// Puts item into the queue slot picked (kBwQueueSel); insert: before it.
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
// The canvases (for tests: smacx --keys BASEDUMP:FILE writes them in the oracle's basedump format).
Buffer* basewin_canvas();
Buffer* basewin_mi_canvas();
bool basewin_dump(const char* path);
// The open base's state on stderr ("basestate ..." lines; smacx --keys BASESTATE).
void basewin_log_state();

// ---- the scenario editor's base menu (editor_base.cpp) ----
ADDR_NATIVE(0x41C7A0) void THISCALL BaseWin_base_editor(uint8_t* win);
ADDR_NATIVE(0x41B790) void THISCALL BaseWin_base_editor_fac(uint8_t* win);
ADDR_NATIVE(0x41BBC0) void THISCALL BaseWin_base_editor_proj(uint8_t* win);
ADDR_NATIVE(0x41C240) void THISCALL BaseWin_base_editor_copy(uint8_t* win, int32_t to);
