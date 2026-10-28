// UI-layer functions called from engine code.
//
// The original engine calls straight into its UI (popups, map redraws, sound, window classes).
// Those functions belong to the UI reconstruction; until then they are declared here with
// ADDR_NATIVE: native builds use the headless implementations in src/headless (the static library
// smacx_ui_headless, one object file per UI area), which behave like an unattended observer (no
// drawing, popups return their default choice). smacx replaces whole areas with its SDL UI (src/ui)
// by defining their functions in its own objects (src/headless/ui_headless_mapview.cpp explains the
// mechanism). Keep declarations grouped by original class.
#pragma once
#include <cstddef>

#include "engine/globals.h"

// ---- map windows (MapWin, WorldWin) ----
// Open map windows (ppMain); the first is the main view. UI objects: opaque to the engine.
GLOBAL_NATIVE(uint8_t* [8], MapWins, 0x7D3C3C);
ADDR_NATIVE(0x46AEF0) void GAMEAPI draw_radius(int32_t x, int32_t y, int32_t radius, int32_t mode);
ADDR_NATIVE(0x46AF40) void GAMEAPI draw_tile(int32_t x, int32_t y, int32_t mode);
ADDR_NATIVE(0x46B190) void GAMEAPI draw_map(int32_t mode);
ADDR_NATIVE(0x46FB90) void THISCALL MapWin_clear_alt(uint8_t* win, int32_t x, int32_t y, int32_t mode);
ADDR_NATIVE(0x46FD90) void THISCALL MapWin_clear_terrain(uint8_t* win);
ADDR_NATIVE(0x471240) void GAMEAPI mapwin_terrain_fixup();
GLOBAL(uint8_t, WorldWindow, 0x8E9F60);  // WorldWin object (UI)
ADDR_NATIVE(0x4C45E0) void THISCALL WorldWin_clear_terrain(uint8_t* win);
// Not original functions: loops the engine runs over the open map windows.
// MapWin_clear_terrain on every active map window (world_build, world_climate).
void ui_mapwins_clear_terrain();
// Marks every active map window for a full redraw and clears the altitude cache at (x, y)
// (delete_landmark).
void ui_mapwins_clear_alt(int32_t x, int32_t y);
// MapWin (vtable 0x669D48 .. ; ui 0x4626E0..0x4712D0): a map view, a UI object. The main view is
// MainConsole (a Console, ui/console.h); the others are MapWins[1..7] and WorldMapWin. The objects
// live in the memory image (or, for the windows smacx owns, in native buffers of the same
// layout), so this is a view of their integer fields at the original's offsets; the GraphicWin
// virtual base follows at +0x21A6C (a Console's at +0x23D94). Savegames hold six of the fields
// (game_io). What the original keeps behind pointers (the canvas, the per-tile terrain cache at
// +4) is native (ui/mapwin.h MapWinNative).
struct MapWin {
    int32_t vbtable;  // +0x0: the virtual base table (not used natively)
    int32_t tile_cache;  // +0x4: per-tile terrain cache pointer (native: MapWinNative)
    int32_t unk_8;  // +0x8: set to 99999 after a fixed-zoom terrain draw (MapWin_gen_terrain_poly)
    uint8_t unk_c[0x1DD6C - 0xC];
    int32_t elev_scale;  // +0x1DD6C: corner height step: 25 (8 with "flat terrain")
    int32_t options;  // +0x1DD70: display option bits (MapWinOptions for the main view)
    int32_t active;  // +0x1DD74: non-zero while the window is in use
    int32_t index;  // +0x1DD78: index in MapWins (0: main view, 1: world map)
    int32_t right_count;  // +0x1DD7C
    int32_t tracking;  // +0x1DD80: mouse-over tracking active
    int32_t group_drag;  // +0x1DD84: the drag moves the stack (Console_group)
    int32_t ignore_click;  // +0x1DD88: the next click is ignored (set by go_reset)
    int32_t edit_paint;  // +0x1DD8C: scenario editor: a drag paints (1 left, 2 right; 4: no redraw)
    int32_t edit_x;  // +0x1DD90: scenario editor: the tile last painted
    int32_t edit_y;  // +0x1DD94
    int32_t zoom;  // +0x1DD98: zoom step (0: normal; < 0: smaller)
    int32_t center_x;  // +0x1DD9C
    int32_t center_y;  // +0x1DDA0
    int32_t origin_x;  // +0x1DDA4: tile at the top-left of the view
    int32_t origin_y;  // +0x1DDA8
    int32_t pixel_x;  // +0x1DDAC: pixel offset of the view (map narrower than the window)
    int32_t pixel_y;  // +0x1DDB0
    int32_t tile_w;  // +0x1DDB4: 8q
    int32_t tile_h;  // +0x1DDB8: 4q
    int32_t half_w;  // +0x1DDBC: 4q
    int32_t half_h;  // +0x1DDC0: 2q
    int32_t quarter_w;  // +0x1DDC4: 2q
    int32_t q;  // +0x1DDC8: the zoom unit
    int32_t cols_odd;  // +0x1DDCC: tiles per odd row
    int32_t rows_above;  // +0x1DDD0
    int32_t cols_even;  // +0x1DDD4: tiles per even row
    int32_t rows_below;  // +0x1DDD8
    int32_t unit_lift;  // +0x1DDDC: unit/base height above the terrain (option 2)
    int32_t unit_lift2;  // +0x1DDE0
    int32_t dest_x[5];  // +0x1DDE4: the go-to drag's path preview: points of its segments
    int32_t dest_y[5];  // +0x1DDF8
    int32_t drag_x;  // +0x1DE0C: tile under the drag (-1: none)
    int32_t drag_y;  // +0x1DE10
    int32_t dest_unit;  // +0x1DE14: the preview's unit (MapWin_dest_line)
    int32_t dest_triad;  // +0x1DE18
    int32_t dest_patrol;  // +0x1DE1C
    int32_t dest_count;  // +0x1DE20: segments drawn
    int32_t drag_base;  // +0x1DE24: base whose rally point is dragged
    uint8_t unk_1de28[0x1EEC0 - 0x1DE28];
    int32_t last_zoom;  // +0x1EEC0: the zoom the label fonts were made for
    int32_t last_elev;  // +0x1EEC4: the elevation step the unit lift was computed for
    uint8_t fonts[3][0x2C];  // +0x1EEC8: Font objects of the map labels (ui/gfx.h Font)
    int32_t cursor;  // +0x1EF4C: cursor sprite address (0: arrow)
    int32_t cursor_id;  // +0x1EF50: standard cursor id (0x7F00: IDC_ARROW)
    uint8_t unk_1ef54[0x21A44 - 0x1EF54];
    int32_t scrolling;  // +0x21A44: incremental scroll redraw in progress
    int32_t scroll_cols[4];  // +0x21A48: the columns a scrolled redraw draws
    int32_t scroll_rows[4];  // +0x21A58: the rows
    int32_t unk_21a68;  // +0x21A68: PlanWin: shown (ui/planwin.cpp)
};
SMAC_LAYOUT(MapWin, 0x21A6C);
static_assert(offsetof(MapWin, vbtable) == 0x0, "MapWin layout");
static_assert(offsetof(MapWin, tile_cache) == 0x4, "MapWin layout");
static_assert(offsetof(MapWin, elev_scale) == 0x1DD6C, "MapWin layout");
static_assert(offsetof(MapWin, options) == 0x1DD70, "MapWin layout");
static_assert(offsetof(MapWin, active) == 0x1DD74, "MapWin layout");
static_assert(offsetof(MapWin, index) == 0x1DD78, "MapWin layout");
static_assert(offsetof(MapWin, right_count) == 0x1DD7C, "MapWin layout");
static_assert(offsetof(MapWin, tracking) == 0x1DD80, "MapWin layout");
static_assert(offsetof(MapWin, group_drag) == 0x1DD84, "MapWin layout");
static_assert(offsetof(MapWin, ignore_click) == 0x1DD88, "MapWin layout");
static_assert(offsetof(MapWin, edit_paint) == 0x1DD8C, "MapWin layout");
static_assert(offsetof(MapWin, edit_x) == 0x1DD90, "MapWin layout");
static_assert(offsetof(MapWin, edit_y) == 0x1DD94, "MapWin layout");
static_assert(offsetof(MapWin, zoom) == 0x1DD98, "MapWin layout");
static_assert(offsetof(MapWin, center_x) == 0x1DD9C, "MapWin layout");
static_assert(offsetof(MapWin, center_y) == 0x1DDA0, "MapWin layout");
static_assert(offsetof(MapWin, origin_x) == 0x1DDA4, "MapWin layout");
static_assert(offsetof(MapWin, origin_y) == 0x1DDA8, "MapWin layout");
static_assert(offsetof(MapWin, pixel_x) == 0x1DDAC, "MapWin layout");
static_assert(offsetof(MapWin, pixel_y) == 0x1DDB0, "MapWin layout");
static_assert(offsetof(MapWin, tile_w) == 0x1DDB4, "MapWin layout");
static_assert(offsetof(MapWin, tile_h) == 0x1DDB8, "MapWin layout");
static_assert(offsetof(MapWin, half_w) == 0x1DDBC, "MapWin layout");
static_assert(offsetof(MapWin, half_h) == 0x1DDC0, "MapWin layout");
static_assert(offsetof(MapWin, quarter_w) == 0x1DDC4, "MapWin layout");
static_assert(offsetof(MapWin, q) == 0x1DDC8, "MapWin layout");
static_assert(offsetof(MapWin, cols_odd) == 0x1DDCC, "MapWin layout");
static_assert(offsetof(MapWin, rows_above) == 0x1DDD0, "MapWin layout");
static_assert(offsetof(MapWin, cols_even) == 0x1DDD4, "MapWin layout");
static_assert(offsetof(MapWin, rows_below) == 0x1DDD8, "MapWin layout");
static_assert(offsetof(MapWin, unit_lift) == 0x1DDDC, "MapWin layout");
static_assert(offsetof(MapWin, unit_lift2) == 0x1DDE0, "MapWin layout");
static_assert(offsetof(MapWin, dest_x) == 0x1DDE4, "MapWin layout");
static_assert(offsetof(MapWin, dest_y) == 0x1DDF8, "MapWin layout");
static_assert(offsetof(MapWin, drag_x) == 0x1DE0C, "MapWin layout");
static_assert(offsetof(MapWin, drag_y) == 0x1DE10, "MapWin layout");
static_assert(offsetof(MapWin, dest_unit) == 0x1DE14, "MapWin layout");
static_assert(offsetof(MapWin, dest_triad) == 0x1DE18, "MapWin layout");
static_assert(offsetof(MapWin, dest_patrol) == 0x1DE1C, "MapWin layout");
static_assert(offsetof(MapWin, dest_count) == 0x1DE20, "MapWin layout");
static_assert(offsetof(MapWin, drag_base) == 0x1DE24, "MapWin layout");
static_assert(offsetof(MapWin, last_zoom) == 0x1EEC0, "MapWin layout");
static_assert(offsetof(MapWin, last_elev) == 0x1EEC4, "MapWin layout");
static_assert(offsetof(MapWin, fonts) == 0x1EEC8, "MapWin layout");
static_assert(offsetof(MapWin, cursor) == 0x1EF4C, "MapWin layout");
static_assert(offsetof(MapWin, cursor_id) == 0x1EF50, "MapWin layout");
static_assert(offsetof(MapWin, scrolling) == 0x21A44, "MapWin layout");
static_assert(offsetof(MapWin, scroll_cols) == 0x21A48, "MapWin layout");
static_assert(offsetof(MapWin, scroll_rows) == 0x21A58, "MapWin layout");
// The MapWin fields of a map window object (any MapWin or Console).
inline MapWin& map_win(uint8_t* win) { return *reinterpret_cast<MapWin*>(win); }
GLOBAL(uint8_t, WorldMapWin, 0x8EB48C);  // the world window's map view (MapWin)
// Fields of the main view (MainConsole's MapWin; CurrentPlayerFaction is its owner field).
GLOBAL(int32_t, MapWinInVehMode, 0x939288);
GLOBAL(int32_t, MapWinField23BE4, 0x939294);
GLOBAL(int32_t, MapWinField23BE8, 0x939298);
GLOBAL(int32_t, MapWinCursorIndex, 0x9392B8);
GLOBAL(int32_t[32], MapWinCursorX, 0x9392C0);
GLOBAL(int32_t[32], MapWinCursorY, 0x939340);
GLOBAL(int32_t, MapWinField23D20, 0x9393D0);
// Releases the window's buffers and closes it.
ADDR_NATIVE(0x470F70) void THISCALL MapWin_close(uint8_t* win);
// Scenario editor panel (window object at 0x7B0CB8): shown (vtable slot 1, argument 0) or
// hidden (slot 2), as load_daemon and the editor toggle do. Not an original function.
GLOBAL(uint8_t, EditorPanelWin, 0x7B0CB8);
void ui_editor_panel_show(bool show);

// ---- PBEM diplomatic message lists (StringStruct objects, a UI class) ----
// Each list object is 0x30 bytes: +0x10 entry count, +0x14 position; the rest are heap links.
GLOBAL(uint8_t[8][0x30], DiploTextLists, 0x93A7B0);  // [1..7] used
GLOBAL(uint8_t[8][8][0x30], DiploMessageLists, 0x737CD8);  // [i][j] used for 1 <= i < j <= 7
// Not original functions: the list operations game_data performs inline.
// Removes every entry and resets the position (and the flags word at +0x18).
void ui_stringlist_clear(uint8_t* list);
inline int32_t ui_stringlist_count(uint8_t* list) { return *reinterpret_cast<int32_t*>(list + 0x10); }
// Moves the cursor to entry `index` (StringStruct_seek_pos) and returns its id and text; with no
// entries, id 0 and nullptr.
const char* ui_stringlist_entry(uint8_t* list, int32_t index, int32_t* id);
// Appends an entry (StringStruct_add with the source text set to `text`).
void ui_stringlist_add(uint8_t* list, int32_t id, char* text);

// ---- Console (main game UI controller) ----
GLOBAL(uint8_t, MainConsole, 0x9156B0);  // Console object (UI)
ADDR_NATIVE(0x5108A0) int32_t THISCALL Console_focus(uint8_t* console, int32_t x, int32_t y, int32_t faction_id);
// Unit upgrade command (enemy_strategy, Console_menu_proc, Console_veh_key): AI factions buy the
// first eligible prototype if they can afford it (reconstructed in ai.cpp); human factions get
// the dialog (Console_upgrade_human). Returns 1 if the unit was upgraded.
ADDR(0x4D06C0) int32_t THISCALL Console_upgrade(uint8_t* console, int32_t veh_id);
// Next unit of the viewer ready to move (a: start veh id, -1: from the beginning); -1 if none.
ADDR_NATIVE(0x510CE0) int32_t THISCALL Console_ready_search(uint8_t* console, int32_t a);
// Puts the console into unit move mode (pact_withdraw, after moving the selected unit).
ADDR_NATIVE(0x511520) void THISCALL Console_set_move(uint8_t* console, int32_t a);
// Not an original function: pact_ends' dialog for the viewer ("ENDPACT1", and "ENDPACT2" when
// units were withdrawn), a Popup object on the original's stack. Viewer only: no-op in headless
// builds (observer games never reach it).
void ui_pact_ends_dialog(int32_t withdrawn);
// Not an original function: Console_upgrade for a unit of a human faction (the upgrade dialog).
// Headless: no human players, returns 0 (cancelled).
int32_t THISCALL Console_upgrade_human(uint8_t* console, int32_t veh_id);

// ---- popups ----
ADDR_NATIVE(0x48C0A0)
int32_t GAMEAPI popp(const char* file, const char* label, int32_t flags, const char* pcx, int32_t flags2);
// Popup from a text file (X_pop_9 without picture); returns the choice (0: first).
ADDR_NATIVE(0x5BF330) int32_t GAMEAPI X_pop_2(const char* label, const char* pcx, int32_t flags);

// ---- message pump ----
// Processes pending window messages (the original keeps the UI responsive during long loops).
ADDR_NATIVE(0x5FCB20) void GAMEAPI do_all_non_input();
// One pass of the message pump (wait_loop: the human turn and multiplayer waits).
ADDR_NATIVE(0x5FC700) void GAMEAPI wait_task();

// ---- boot: window/sprite/font setup (system_init, system_close, game_init, game_reload) ----
// UI objects (opaque to the engine).
GLOBAL(uint8_t, MainInterfaceWin, 0x7AE820);
GLOBAL(uint8_t, BattleWin, 0x6EEED8);
GLOBAL(uint8_t, MultiDebugWin, 0x9B22F0);
GLOBAL(uint8_t, NetState, 0x93CD90);
GLOBAL(uint8_t, MultiDebugParam, 0x6F107C);  // unk_6F107C
GLOBAL(uint8_t, MessageWin, 0x7F67F8);
GLOBAL(uint8_t, DatalinkWin, 0x703EA0);
GLOBAL(uint8_t, DesignWin, 0x71F2B0);
GLOBAL(uint8_t, BaseWin, 0x6A7628);
GLOBAL(uint8_t, InfoWin, 0x8C5568);
GLOBAL(uint8_t, WorldWin, 0x8E9F60);
GLOBAL(uint8_t, MultiWin, 0x7FD648);
GLOBAL(int32_t, MultiWinVisible, 0x7FE06C);  // MultiWin field: the multiplayer status window is shown
GLOBAL(int32_t, MapWinOptions, 0x933420);  // display option bits of the map windows
GLOBAL_NATIVE(uint8_t*, TopMenuWin, 0x945824);
GLOBAL_NATIVE(uint8_t*, ConsoleVBase, 0x9B8DD0);  // MainConsole adjusted to its virtual base
// Sprite pointers set by game_init (popup frame sprites, menu icons).
GLOBAL(uint8_t, PopupSpriteA, 0x75AE4C);
GLOBAL(uint8_t, PopupSpriteB, 0x75AE78);
GLOBAL(uint8_t, MenuIconSprites, 0x75AE20);
GLOBAL_NATIVE(uint8_t*, PopupSpriteAPtr, 0x9B7B00);
GLOBAL_NATIVE(uint8_t*, PopupSpriteBPtr, 0x9B7B04);
GLOBAL_NATIVE(uint8_t*, MenuIconSpritesPtr, 0x9B7B0C);
ADDR_NATIVE(0x4BA220) void GAMEAPI textcolor_init();
ADDR_NATIVE(0x455E80) void GAMEAPI load_fixed_sprites();
ADDR_NATIVE(0x455EB0) void GAMEAPI load_sprites();
// Fonts and popup settings; also reads FastUnitAnim/SmoothUnitAnim into UnitAnimMode.
ADDR_NATIVE(0x588AA0) void GAMEAPI config_popups();
GLOBAL(int32_t, UnitAnimMode, 0x94581C);  // 1 fast unit animation, -1 smooth, 0 normal
ADDR_NATIVE(0x4626E0) uint8_t* THISCALL MapWin_ctor(uint8_t* self, int32_t flag);
ADDR_NATIVE(0x459680) int32_t THISCALL MainInterface_init(uint8_t* self, int32_t flag);
ADDR_NATIVE(0x4219E0) int32_t THISCALL BattleWin_init(uint8_t* self);
ADDR_NATIVE(0x51D890) int32_t THISCALL Console_init(uint8_t* self, int32_t flag);
ADDR_NATIVE(0x51D7D0) void THISCALL Console_close(uint8_t* self);
ADDR_NATIVE(0x5C9860)
int32_t THISCALL MultiDebug_init(uint8_t* self, uint8_t* net_state, uint8_t* console, uint8_t* param);
ADDR_NATIVE(0x5C98E0) void THISCALL MultiDebug_close(uint8_t* self);
ADDR_NATIVE(0x471D50) int32_t THISCALL MessageWin_init(uint8_t* self);
ADDR_NATIVE(0x429300) int32_t THISCALL Datalink_init(uint8_t* self);
ADDR_NATIVE(0x4595C0) void GAMEAPI bung_momma();
ADDR_NATIVE(0x4C5D90) void GAMEAPI release_sound();
ADDR_NATIVE(0x43C1A0) void THISCALL DesignWin_close(uint8_t* self);
ADDR_NATIVE(0x408710) void THISCALL BaseWin_close(uint8_t* self);
ADDR_NATIVE(0x4710E0) void GAMEAPI mapwin_system_shutdown();
ADDR_NATIVE(0x4B9F80) void THISCALL StatusWin_close(uint8_t* self);
ADDR_NATIVE(0x5D4E40) void THISCALL GraphicWin_close(uint8_t* self);
ADDR_NATIVE(0x5EDCD0) void THISCALL Win_hide(uint8_t* self);
ADDR_NATIVE(0x58EE60) int32_t GAMEAPI desktop_init(int32_t flag);
ADDR_NATIVE(0x50F3D0) void GAMEAPI start_timers();
ADDR_NATIVE(0x50F440) void GAMEAPI stop_timers();
// Not original functions: C++ object operations the original performs inline.
// MainConsole adjusted to its virtual base class (vbtable lookup).
uint8_t* ui_console_vbase();
// Virtual scalar-deleting destructor: obj->vtable[0](1); with_vbase adjusts to the virtual base.
void ui_delete_object(uint8_t* obj, bool with_vbase);

// ---- popups (BasePop) ----
// Popup factory installed by WinMain (Popup_alloc 0x404FB0; BasePop_basepop_alloc by default).
typedef uint8_t*(GAMEAPI* BasePopAllocFn)();
GLOBAL_NATIVE(BasePopAllocFn, BasePopAlloc, 0x696ECC);
// Shows popup `label` of text file `file`. Returns 0 (choice in BasePop_choice) or an error code.
ADDR_NATIVE(0x601BF0)
int32_t THISCALL
    BasePop_start(uint8_t* pop, const char* file, const char* label, int32_t a3, int32_t a4, int32_t a5, int32_t a6);
ADDR_NATIVE(0x6018A0) int32_t GAMEAPI BasePop_set_def_ok_text(const char* text);
ADDR_NATIVE(0x601910) int32_t GAMEAPI BasePop_set_def_cancel_text(const char* text);
// Error popup of SCRIPT.txt (veh module).
ADDR_NATIVE(0x5BF310) int32_t GAMEAPI X_pop(const char* label, int32_t flags);
// ---- design workshop (DesignWin) ----
// Opens the design workshop on DesignWin for a faction (negative: as called by tech_achieved) and
// unit (-2: new design). Headless: nothing is designed, returns 0.
ADDR_NATIVE(0x43E9A0) int32_t THISCALL DesignWin_exec(uint8_t* win, int32_t faction_id, int32_t unit_id);
// Not original functions: popup object field (selected choice) and deletion.
int32_t BasePop_choice(uint8_t* pop);
inline void BasePop_delete(uint8_t* pop) { ui_delete_object(pop, false); }

// ---- file dialogs (save module: save_game, load_game, load_map, save_map) ----
// FileDialog: the Windows common file dialog wrapper (video module); the chosen path is at +0.
// FileBox: the in-game file dialog (kFileBoxSize bytes) with its file list (gfx2) at
// kFileBoxList. The "run" functions return the chosen path, or nullptr when cancelled.
constexpr uint32_t kFileDialogSize = 0x420;
constexpr uint32_t kFileBoxSize = 0x6058;
constexpr uint32_t kFileBoxList = 0xA14;
ADDR_NATIVE(0x634BE0) void THISCALL FileDialog_ctor(uint8_t* self);
ADDR_NATIVE(0x634C20) void THISCALL FileDialog_dtor(uint8_t* self);
ADDR_NATIVE(0x634C30) char* THISCALL FileDialog_save(uint8_t* self);
ADDR_NATIVE(0x634CF0) char* THISCALL FileDialog_open(uint8_t* self);
ADDR_NATIVE(0x634DB0) int32_t THISCALL FileDialog_add_filter(uint8_t* self, const char* pattern, const char* text);
ADDR_NATIVE(0x634E80) int32_t THISCALL FileDialog_set_ext(uint8_t* self, const char* ext);
ADDR_NATIVE(0x634ED0) int32_t THISCALL FileDialog_set_name(uint8_t* self, const char* name);
ADDR_NATIVE(0x407350) void THISCALL FileBox_ctor(uint8_t* self);
ADDR_NATIVE(0x407410) void THISCALL FileBox_init(uint8_t* self);
ADDR_NATIVE(0x407910) void THISCALL FileBox_close(uint8_t* self);
ADDR_NATIVE(0x407920) char* THISCALL FileBox_save(uint8_t* self);
ADDR_NATIVE(0x407A80) char* THISCALL FileBox_load(uint8_t* self);
ADDR_NATIVE(0x6141C0) int32_t THISCALL FileList_set_path(uint8_t* list, const char* path);
ADDR_NATIVE(0x614140) int32_t THISCALL FileList_add_ext(uint8_t* list, const char* ext);
ADDR_NATIVE(0x613900) void THISCALL FileList_close(uint8_t* list);
// Not an original function: the FileBox destructor, which the original inlines at each return.
void ui_filebox_destroy(uint8_t* box);
// pfncWinIsVisible (0x5F7E90) on the main console (its virtual base).
ADDR_NATIVE(0x5F7E90) int32_t THISCALL Win_is_visible(uint8_t* win);
ADDR_NATIVE(0x5FCBB0) void GAMEAPI do_all_draws();
// Multiple-choice popup of SCRIPT.txt with a picture; returns the choice (< 0: cancelled).
ADDR_NATIVE(0x5BF7D0) int32_t GAMEAPI X_pops_11(const char* label, int32_t flags, const char* pcx, int32_t a4);
// Faction portraits (picture file names, indexed by faction).
GLOBAL_NATIVE(const char* [8], FactionPortraits, 0x6846D8);
ADDR_NATIVE(0x453F70) void GAMEAPI load_flags();
// Network UI objects closed when an iron man multiplayer game is saved.
// Multiplayer setup: name of the loaded game and its kind (0x811D1C, 0x811E20).
GLOBAL(char[256], NetGameFileName, 0x811D1C);
GLOBAL(int32_t, NetGameFileKind, 0x811E20);

// ---- interface buttons, faction art ----
// Terrain-improvement buttons of the main interface (BaseButton objects, 0xB4C bytes each).
GLOBAL(uint8_t[19][0xB4C], TerraformButtons, 0x7BB62C);
ADDR_NATIVE(0x607550) void THISCALL BaseButton_set_bubble_text(uint8_t* button, const char* text);
ADDR_NATIVE(0x453710) void GAMEAPI load_faction_art(int32_t faction_id);

// ---- vehicles, spotting (veh/stack modules) ----
GLOBAL(uint8_t, Sounds, 0x749CF8);  // sound effects object (FX)
GLOBAL(uint8_t, NetMsg, 0x805338);  // message box object
GLOBAL(uint8_t, InfoWindow, 0x7AD2A0);  // status/info pane (Thinker's InfoWin)
ADDR_NATIVE(0x446A00) void THISCALL FX_play(uint8_t* sounds, int32_t id);
ADDR_NATIVE(0x446AD0) void THISCALL FX_fade(uint8_t* sounds, int32_t id);
ADDR_NATIVE(0x47A890)
int32_t THISCALL NetMsg_pop(uint8_t* msg, const char* label, int32_t delay, int32_t a3, const char* pcx);
ADDR_NATIVE(0x458910) void THISCALL InfoWin_on_redraw(uint8_t* win);
ADDR_NATIVE(0x5D5A70) void THISCALL GraphicWin_redraw(uint8_t* win);
// Base window: recomputes and redraws the shown base (base_compute(1)) when it is visible and
// concerned (BaseWin, a base id / a location within distance a3 of the shown base).
ADDR_NATIVE(0x41A7A0) void THISCALL BaseWin_check_base(uint8_t* win, int32_t base_id);
ADDR_NATIVE(0x41A7E0) void THISCALL BaseWin_check_loc(uint8_t* win, int32_t x, int32_t y, int32_t dist);
// Vehicle ids held by the map and status windows; veh_kill renumbers them.
GLOBAL(int32_t, MapWinVehA, 0x93928C);
GLOBAL(int32_t, MapWinVehB, 0x939290);
GLOBAL(int32_t, StatusWinVehA, 0x8C6B2C);
GLOBAL(int32_t, StatusWinVehB, 0x8C6B34);
GLOBAL_NATIVE(int32_t*, NetVehIDPtr, 0x93E908);  // multiplayer: vehicle id variable being edited

// ---- base module (base_kill, base_production, ...) ----
// Base window: adjusts its shown base id after base_id was removed (closes it when it showed it).
ADDR_NATIVE(0x41A620) void THISCALL BaseWin_base_killed(uint8_t* win, int32_t base_id);
// Not an original function: base_kill's update of the message list (ListBox at 0x7F685C): entries
// of the killed base (id base_id | 0x40000000) are deleted, later bases' entries renumbered, then
// ListBox_update_changes.
void ui_messages_base_killed(int32_t base_id);
// Plays a voice/sound effect by id (UI sound module).
ADDR_NATIVE(0x4455F0) void GAMEAPI wave_it(int32_t id);
// Help (datalinks) popup on a topic: kind 11 is a facility/project.
ADDR_NATIVE(0x44CB60) void GAMEAPI help_topic(int32_t kind, int32_t id);

// ---- social module (social_select) and tamper records (net_misc) ----
// Scenario editor: asks which faction to act for (returns faction_id or -1 when cancelled).
ADDR_NATIVE(0x4D9AD0) int32_t THISCALL Console_editor_who(uint8_t* console, int32_t faction_id, int32_t flag);
GLOBAL(uint8_t, SocialWin, 0x8A6270);  // social engineering window
// Social engineering dialog (0x4B2740): lets the player edit the pending choice of faction_id.
ADDR_NATIVE(0x4B2740) void THISCALL SocialWin_exec(uint8_t* win, int32_t faction_id);
// Releases the social engineering dialog's controls (0x4B26D0).
ADDR_NATIVE(0x4B26D0) void THISCALL SocialWin_close(uint8_t* win);
// Hotseat/PBEM tamper records in the registry (HKLM DirectPlay key): read into / write from
// TamperIndex and TamperRecords.
ADDR_NATIVE(0x51E000) void GAMEAPI read_tamper();
ADDR_NATIVE(0x51E090) void GAMEAPI set_tamper();

// ---- tech module (tech_selection, tech_research, tech_achieved) ----
// Research choice dialog (TechWin on the stack; 0x5BEB70 runs FUN_00486510). AI factions (and
// unattended viewers) choose without a dialog: tech_ai, or the best tech_val among the listed
// techs. Headless: the AI choice for every faction.
ADDR_NATIVE(0x5BEB70)
int32_t GAMEAPI tech_pick(int32_t faction_id, int32_t is_free, int32_t from_faction_id, const char* label);
ADDR_NATIVE(0x47A7D0) void THISCALL NetMsg_close(uint8_t* msg);
// Shows the monuments window after a new monument (UI only).
ADDR_NATIVE(0x476A50) void GAMEAPI monument(int32_t is_new);
// Ambient sound/music cue (UI sound module).
ADDR_NATIVE(0x446B40) int32_t GAMEAPI ambience(int32_t id);
// Empty in the original.
ADDR_NATIVE(0x58EE50) void GAMEAPI desktop_update();
ADDR_NATIVE(0x43E960) void THISCALL DesignWin_shutdown(uint8_t* win);
GLOBAL(uint8_t, NewTechWin, 0x811E40);  // "new technology" window (UI)
ADDR_NATIVE(0x483F90) int32_t THISCALL NewTechWin_exec(uint8_t* win, int32_t tech_id, int32_t from);
// Not original functions: the announcement popup of tech_achieved, a BasePop object on its stack.
// open: blurb "#TECH<n>" of BLURBSX (in StrBuffer) with separator lines; show: tech icon, popup
// `label` (StrBuffer) of the main script with the given flags, the tech's help text for the
// viewer, and the sound effect; exec: shows it (non-viewer factions in the omniscient view).
// Headless: not displayed (the object exists only inside the original
// function; only reached for the viewer or with GameState 0x80).
void ui_tech_popup_open();
void ui_tech_popup_show(int32_t tech_id, int32_t flags, int32_t faction_id, bool with_help);
void ui_tech_popup_exec();
// Alert popup (shown only to the viewer, with sound): label of SCRIPT.txt.
ADDR_NATIVE(0x48C650)
int32_t GAMEAPI popb(const char* label, int32_t flags, int32_t sound_id, const char* pcx, int32_t a5);
// Refreshes the main console's status displays.
ADDR_NATIVE(0x514880) void THISCALL Console_update_data(uint8_t* console, int32_t a1);
// Centers a map window on (x, y).
ADDR_NATIVE(0x46B1F0) void THISCALL pfncMoveMap(uint8_t* win, int32_t x, int32_t y, int32_t a3);
// Popup with a picture: POP2(label, pcx, base_id or -1).
ADDR_NATIVE(0x405140) int32_t GAMEAPI POP2(const char* label, const char* pcx, int32_t a3);
// Not an original function: GetAsyncKeyState(VK_SHIFT) high byte (base_ecology skips a delay).
bool ui_shift_pressed();
// Base window redraw / zoom to a base (base_upkeep).
ADDR_NATIVE(0x41E790) void THISCALL BaseWin_on_redraw(uint8_t* win);
ADDR_NATIVE(0x41A920) void THISCALL BaseWin_zoom(uint8_t* win, int32_t base_id, int32_t a2);
// Redraws the tiles around (x, y).
ADDR_NATIVE(0x46B140) void GAMEAPI draw_tiles(int32_t x, int32_t y, int32_t mode);
// Not original functions: base_production's announcement popup, a Popup object on its stack.
// open: BasePop_init with the given width; blurb: popup BLURBS / StrBuffer with separator lines
// (is_project: the first line only below 800 px width, then the PRODUCE2 text); exec: shows it.
// Headless: not displayed (as ui_tech_popup_*).
void ui_production_popup_open(int32_t width);
void ui_production_popup_blurb(bool is_project);
void ui_production_popup_exec();
// Secret project movies and help (shown to the viewer).
ADDR_NATIVE(0x403BA0) void GAMEAPI amovie_project(int32_t project_id);
ADDR_NATIVE(0x403BE0) void GAMEAPI amovie_project_name(const char* name);
ADDR_NATIVE(0x44C9E0) void GAMEAPI help_project(int32_t project_id);
// Voice clip of a new facility (a Wave object; StrBuffer holds voices\fac<n>.mp3).
GLOBAL(uint8_t, FacilityVoice, 0x945ED0);
ADDR_NATIVE(0x4C6EA0) int32_t THISCALL Wave_unload(uint8_t* wave);
ADDR_NATIVE(0x4C6C20) int32_t THISCALL Wave_load(uint8_t* wave, const char* file);
ADDR_NATIVE(0x4C6920) int32_t THISCALL Wave_play_2(uint8_t* wave);
ADDR_NATIVE(0x4C6600) void THISCALL Sound_fade_2(uint8_t* wave, int32_t ms);

// ---- setup module (setup_game, eliminate_player) ----
// Diplomacy offer slots of every faction pair (DiploStateC[i][j][0..3]) cleared (0x441250).
ADDR_NATIVE(0x441250) void GAMEAPI clear_all_offers();
// Hotseat/PBEM: every diplomatic message list (DiploMessageLists) emptied (0x440F80; the
// original deletes the StringStruct entries; the effect is ui_stringlist_clear on each list).
ADDR_NATIVE(0x440F80) void GAMEAPI clear_hotseat_chat();
// Council notification state cleared: CouncilVoteState and CouncilProposal (0x4243F0).
ADDR_NATIVE(0x4243F0) void GAMEAPI clear_council_notify_all();
// Records "faction_id eliminated killed_id" in faction_id's monuments (first free entry of the
// six killed-faction events) and shows the monuments window to a non-alien viewer.
ADDR_NATIVE(0x477A60) void GAMEAPI mon_killed_faction(int32_t faction_id, int32_t killed_id);
// "his"/"her" (labels 207/208, by the leader's gender) of faction_id in HisHerBuffer; mode 1
// capitalises the first letter, other non-zero modes the whole word. Returns HisHerBuffer.
GLOBAL(char[32], HisHerBuffer, 0x945838);
ADDR_NATIVE(0x58FBC0) char* GAMEAPI get_his_her(int32_t faction_id, int32_t mode);
// Discards pending keyboard and mouse input.
ADDR_NATIVE(0x5FD120) void GAMEAPI flush_input();
// Blitter scale factors (video module), reset to 1 by crash_landing.
GLOBAL(int32_t, BlitDestScale, 0x696D18);  // piDestScale
GLOBAL(int32_t, BlitSourceScale, 0x696D1C);  // piSourceScale
// Not original functions: the UI parts of crash_landing (a function that keeps its windows,
// buffers and flic players on its stack). drop: the landing sound (preference 0x400) and the
// 30-frame drop pod animation (alien pod or drop pod with smoke) over the map centred on (x, y),
// with the map window's option bits 0x8C0 cleared; close: releases the animation canvas and fades
// the sound. first_base_name: the FIRSTBASE popup that lets the player rename the first base
// (default_name); the chosen name is left in ParseStrBuffer[0]. Headless: not
// displayed (the default name is kept); crash_landing only runs for a human viewer.
void ui_crash_landing_drop(int32_t x, int32_t y, bool alien);
void ui_crash_landing_close();
void ui_first_base_name(const char* default_name);

// ---- turn module (turn_upkeep, control_turn, control_game, council, end_of_game) ----
// Story interlude (text and movie of event id) shown to the viewer once (GameInterludeState bit);
// a2 is the image address of a text for $STR, mode the popup style. Records the event in
// GameInterludeState even when the viewer's "no interludes" preference (0x40000) skips the display.
// Headless: the state effects only.
ADDR_NATIVE(0x5230E0) void GAMEAPI interlude(int32_t id, int32_t a2, int32_t mode, int32_t a4);
// Main map window caption (the turn/year line).
ADDR_NATIVE(0x46FB10) void THISCALL MapWin_main_caption(uint8_t* win);
// Centers the map window on (x, y) if needed; non-zero if the view changed (redrawn).
ADDR_NATIVE(0x46B310) int32_t THISCALL MapWin_focus(uint8_t* win, int32_t x, int32_t y);
// One pass of the message pump (clock_wait).
ADDR_NATIVE(0x5FCA30) int32_t GAMEAPI do_non_input();
// Maximum allocation steps (of 10) to economy/labs/psych: 10 in this version.
ADDR_NATIVE(0x445440) int32_t GAMEAPI energy_limit(int32_t faction_id);
// The human player's interactive turn (also the observer's "turn complete" pause after a round).
// Headless: returns at once.
ADDR_NATIVE(0x514050) int32_t THISCALL Console_human_turn(uint8_t* console);
// Clears the message list window (MessageWin).
ADDR_NATIVE(0x4717A0) void THISCALL MessageWin_clear(uint8_t* win);
// Hotseat council notification of faction_id done: CouncilProposal and CouncilVoteState cleared.
ADDR_NATIVE(0x4243D0) void GAMEAPI clear_council_notify(int32_t faction_id);
// Hotseat/PBEM diplomacy between two human players (DiploWin). Headless: nothing.
ADDR_NATIVE(0x444D90) void GAMEAPI diplo(int32_t faction_id, int32_t faction_id_with);
// End of game screens: credits, score report, rating (quayle), hall of fame, replay.
ADDR_NATIVE(0x428DF0) void GAMEAPI show_credits();
ADDR_NATIVE(0x4ACB80) void GAMEAPI report_score(int32_t a1);
ADDR_NATIVE(0x496670) void GAMEAPI quayle(int32_t faction_id);
ADDR_NATIVE(0x44AE50) void GAMEAPI hall_of_fame(int32_t a1);
ADDR_NATIVE(0x5ADBF0) void GAMEAPI show_replay();
// Main menu (new game, load, ...). Non-zero: quit.
ADDR_NATIVE(0x58E360) int32_t GAMEAPI top_menu(int32_t a1);
// Multiplayer lobby launch check (command line); non-zero when started from a lobby.
ADDR_NATIVE(0x62D9E0)
int32_t THISCALL Net_check_for_lobby(uint8_t* net, char* cmd_line, uint8_t* a2, int32_t a3, int32_t a4);
// Sound effects initialisation (FX object Sounds).
ADDR_NATIVE(0x445CD0) void THISCALL FX_init(uint8_t* sounds);
ADDR_NATIVE(0x4C6B10) int32_t THISCALL Wave_is_playing(uint8_t* wave);
// Game desktop (main windows) shown / closed around a game; multiplayer setup; opening screens.
ADDR_NATIVE(0x58EFF0) void GAMEAPI desktop_close();
ADDR_NATIVE(0x589B20) void GAMEAPI close_opening();
ADDR_NATIVE(0x58DB30) int32_t GAMEAPI multiplayer_init(int32_t a1);
GLOBAL(uint8_t, DesktopWin, 0x937118);  // background GraphicWin behind the game windows
ADDR_NATIVE(0x5D5250) void THISCALL GraphicWin_fill(uint8_t* win, int32_t color);
ADDR_NATIVE(0x5ED9D0) void THISCALL Win_show(uint8_t* win, int32_t a1);
GLOBAL(char[256], ReloadFilePath, 0x9B2078);  // control_game: alternate path for game_reload
GLOBAL(char[256], ReloadFileSource, 0x93AA0C);
// Not original functions: the parts of control_game, control_turn and call_council that work on
// windows, the registry or popup objects on the original's stack.
// DirectPlay registration complete (HKLM DirectPlay application key "Complete" value).
bool ui_directplay_registered();
// Main map window display options (field +0x1DD70) |= bits.
void ui_main_mapwin_set_options(uint32_t bits);
// Hotseat/PBEM: hides the previous player's view and asks the next human (faction_id) to take
// over (SWITCHING popup, password check, optional save and quit). password_tries is control_turn's
// count of wrong passwords (kept for the whole control_turn, as the original's local). Returns 0
// when the player takes the turn, 1 when the game was left (ControlTurnA set). Headless:
// not shown (returns 0).
int32_t ui_hotseat_switch(int32_t faction_id, int32_t* password_tries);
// Hotseat/PBEM: shows the new player's view (centred on a base or unit), the battle summary popup
// and the message list. Headless: not shown.
void ui_hotseat_show(int32_t faction_id);
// call_council's menu of proposals (a Popup object on its stack). Headless: not
// shown; exec returns -1 (cancelled); only human callers show it.
void ui_council_menu_open(int32_t faction_id);
void ui_council_menu_add(const char* text, int32_t proposal);
int32_t ui_council_menu_exec();
void ui_council_menu_close();

// ---- action module (action_terraform, action_airdrop, action_tectonic, ...) ----
// Tutorial window (TutWin) and the terrain pictures its terraforming hints show (UI objects).
GLOBAL(uint8_t, TutWin, 0x8C6E68);
GLOBAL(uint8_t, ImgMapFarm, 0x79204C);
GLOBAL(uint8_t, ImgMapSoilEnr, 0x79A794);
GLOBAL(uint8_t, ImgMapMine, 0x776A50);
GLOBAL(uint8_t, ImgMapSolar, 0x7991F8);
// Tutorial hint `label` at (x, y) (veh_id or -1) with an optional picture. Headless: nothing.
ADDR_NATIVE(0x4BE650)
void THISCALL TutWin_tut_map(uint8_t* win, const char* label, int32_t x, int32_t y, int32_t veh_id, uint8_t* image,
                             int32_t a6, int32_t a7, int32_t a8);
// Base window: redraws when it shows veh_id's location.
ADDR_NATIVE(0x41A8B0) void THISCALL BaseWin_check_veh(uint8_t* win, int32_t veh_id);
// Sound effect id at a map location.
ADDR_NATIVE(0x446960) void THISCALL FX_play_loc(uint8_t* sounds, int32_t id, int32_t x, int32_t y);
// Moves the map cursor away from (x, y) (unit airdropped while selected).
ADDR_NATIVE(0x5109B0) void THISCALL Console_cursor_next(uint8_t* console, int32_t x, int32_t y);
// Report window (ReportWin; shown report kind at +8, 6: military) redraw.
GLOBAL(uint8_t, ReportWin, 0x885F38);
GLOBAL(int32_t, ReportWinKind, 0x885F40);
ADDR_NATIVE(0x4A3FC0) void THISCALL ReportIf_on_redraw(uint8_t* win);
// Main view field 0x23BF0 (non-zero: animations off / skip the airdrop pause).
GLOBAL(int32_t, MapWinField23BF0, 0x9392A0);
// Animates veh_id sliding from (x, y) in direction dir (diplomacy module 0x55A150, an animation
// routine). Game state effect: the vehicle's rotate_angle becomes dir (dir % 8 when dir > 7), set
// by vehdraw_turn_start (directly with flags 0x4000). Headless: that effect.
ADDR_NATIVE(0x55A150) void GAMEAPI veh_scoot(int32_t veh_id, int32_t x, int32_t y, int32_t dir, int32_t flags);

// ---- path / move / goals (path finding debug redraw, unit movement) ----
// Redraws a map window (Path_find, with the path-debug display option 0x4000 of the main view).
ADDR_NATIVE(0x46A550) void THISCALL pfncRedrawMap(uint8_t* win, int32_t flag);
// Stops a sound effect of the FX object (monolith).
ADDR_NATIVE(0x446A90) void THISCALL FX_stop(uint8_t* sounds, int32_t id);
// Appends the morale name of veh_id to StrBuffer (say_morale). Headless: appends nothing (the text
// only feeds messages).
ADDR_NATIVE(0x4B43C0) void GAMEAPI say_morale_2(int32_t veh_id, int32_t a2);
// Redraws the tiles around (x, y) on every open map window (radius by has_radar; viewer `a4`).
ADDR_NATIVE(0x46AF90) void GAMEAPI draw_tile_fixup(int32_t x, int32_t y, int32_t has_radar, int32_t a4);

// ---- battle module (battle_resolve, planet_busting, interceptor, promote) ----
// Battle window (BattleWin) and the status/info pane (InfoWin) showing the combatants.
ADDR_NATIVE(0x45D310) void THISCALL SubInterface_set_iface_mode(uint8_t* win);
ADDR_NATIVE(0x422C50) void THISCALL BattleWin_clear(uint8_t* win);
ADDR_NATIVE(0x421B20) void THISCALL BattleWin_pulse_timer(uint8_t* win);
ADDR_NATIVE(0x421B40) void THISCALL BattleWin_stop_timer(uint8_t* win);
ADDR_NATIVE(0x4B8830)
void THISCALL StatusWin_draw(uint8_t* win, int32_t veh_id_atk, int32_t veh_id_def, int32_t offense, int32_t defense,
                             int32_t flag);
ADDR_NATIVE(0x4B9EA0) void THISCALL StatusWin_redraw(uint8_t* win);
ADDR_NATIVE(0x4B8970) void THISCALL StatusWin_reset(uint8_t* win);
GLOBAL(int32_t, InfoWinBattleShown, 0x8C6B3C);  // InfoWin field: a battle is shown in the pane
// Base window refresh after a multiplayer battle (BaseWin field; non-zero: refresh).
GLOBAL(int32_t, BaseWinMultiplayerRefresh, 0x6E8150);
ADDR_NATIVE(0x415DD0) void THISCALL BaseWin_focus(uint8_t* win);
// Redraws two tiles (attacker and defender) with their surroundings.
ADDR_NATIVE(0x46B040)
void GAMEAPI draw_tile_fixup_pair(int32_t x1, int32_t y1, int32_t x2, int32_t y2, int32_t a5, int32_t a6);
// Popup of SCRIPT.txt shown to faction_id with the map centred on (x, y). Headless: 0.
ADDR_NATIVE(0x4055A0) int32_t GAMEAPI POP3(const char* label, int32_t a2, int32_t faction_id, int32_t x, int32_t y);
// Tutorial hint `bit` due (once per game, recorded in TutorialHintsShown) for a single-player
// viewer with tutorial hints on. Game state: TutorialHintsShown |= bit when it returns 1.
GLOBAL(int32_t, TutorialHintsShown, 0x9A64B0);
ADDR_NATIVE(0x4BA650) int32_t GAMEAPI tut_check2(int32_t bit);
// Not an original function: planet_busting waits while the sound object at 0x74B5DC plays
// (vtable slot 0x5C, do_task loop). Headless: returns at once.
void ui_battle_wait_sound();
// Not an original function: supply_options' menu (a Popup object on its stack with the items of
// SUPPLYMENU: 0 cancel, 1 help build, 2.. convoy resources; `items` lines are listed). Returns
// the choice, 0 if cancelled. Headless: not shown (returns 0); only the viewer's
// own supply crawlers ask.
int32_t ui_supply_menu(int32_t veh_id, int32_t base_id, int32_t items);
// Engine pitch of the moving unit's sound (order_veh; 0 restores it).
ADDR_NATIVE(0x446B10) void THISCALL FX_engine_pitch(uint8_t* sounds, int32_t pitch);
// Not an original function: GetAsyncKeyState(VK_CONTROL) high byte (order_veh: no map centering).
bool ui_ctrl_pressed();
// Not original functions: order_veh's menus (a Popup object on its stack). Artifact entering one of
// the viewer's bases (ARTIFACTMENU: 0 keep, 1 link to the network node, 2 hurry production;
// `items` lines) and a ship's passengers disembarking (DISEMBARKERS: the chosen vehicle + 1, 0
// cancelled). Headless: not shown (return 0).
int32_t ui_artifact_menu(int32_t veh_id, int32_t base_id, int32_t items);
int32_t ui_disembark_menu(int32_t veh_id);

// ---- probe module (probe teams) ----
// Popup of SCRIPT.txt with a picture (faction portrait); returns the choice.
ADDR_NATIVE(0x5BF5D0) int32_t GAMEAPI X_pops(const char* label, const char* pcx, int32_t a3);
// Shows the intelligence report on faction_id (ReportWin). Headless: nothing.
ADDR_NATIVE(0x4ACBD0) void GAMEAPI report_intel(int32_t faction_id);
// "him"/"her" of faction_id's leader in HisHerBuffer (mode as get_his_her). Returns HisHerBuffer.
ADDR_NATIVE(0x58FC50) char* GAMEAPI get_him_her(int32_t faction_id, int32_t mode);

// ---- diplomacy module, first half (diplomacy.cpp) ----
// The diplomacy dialog window (DiploPop object) and its info pane rectangle.
GLOBAL(uint8_t, DiploPopWin, 0x733990);
GLOBAL(uint8_t, DiploPopInfoRect, 0x735A84);
// Redraws the info pane (portrait, mood caption) of the open diplomacy dialog. Headless: nothing.
ADDR_NATIVE(0x43FA90) void THISCALL DiploPop_draw_info(uint8_t* pop);
// Copies a rectangle of a window to the screen. Headless: nothing.
ADDR_NATIVE(0x5D5930) void THISCALL GraphicWin_soft_update_3(uint8_t* win, uint8_t* rect);
// Opens the diplomacy dialog with faction_id_with's portrait. Headless: nothing.
ADDR_NATIVE(0x43F2B0) void THISCALL DiploPop_start(uint8_t* pop, int32_t faction_id_with);
// Sets the status/info pane's display state. Headless: nothing.
ADDR_NATIVE(0x458F10) void THISCALL InfoWin_set_state(uint8_t* win, int32_t state, int32_t a2);
// Not original functions: the diplomacy menus, Popup objects on the original functions' stacks
// (wants_to_speak COMM, make_gift OFFERENERGY, call_off_vendetta CALLOFF, propose_attack
// MAYBEWARENERGY / MAYBEWARPRICE, buy_council_vote BUYVOTEMENU, tech_trade TRADETECH). The engine
// parses the item texts (script lookups into TextBuffer / StrBuffer, as the original). start runs
// Popup_start on the script file's section label with the given flags (a second start appends
// to the same menu); item adds an entry with its value; exec shows the menu with
// faction_id_with's portrait and returns the chosen value (the meaning is per caller); close
// discards it. Headless: nothing is shown, exec returns 0 (BasePop_exec_3's no-display value,
// which the oracle forces).
void ui_diplo_menu_start(const char* label, int32_t flags);
void ui_diplo_menu_item(const char* text, int32_t value);
int32_t ui_diplo_menu_exec(int32_t faction_id_with);
void ui_diplo_menu_close();
// The human side of the diplomacy dialog (UI, called from communicate and the diplomacy panel;
// never by the engine): the DIPLOMACY menu (returns the chosen topic), the proposal menus that
// dispatch to the reconstructed propose_* / tech_trade / energy_trade / make_gift / base_swap /
// threaten / buy_council_vote, the BATTLEPLANS map dialog (agrees on a common target: trade_maps,
// the partner's base_id_attack_target and target location) and a click on a faction's commlink
// button. Headless: nothing happens (0).
ADDR_NATIVE(0x54C560) int32_t GAMEAPI diplomacy_menu(int32_t faction_id, int32_t faction_id_with);
ADDR_NATIVE(0x54F420) void GAMEAPI make_a_proposal(int32_t faction_id, int32_t faction_id_with, int32_t a3);
ADDR_NATIVE(0x54DCF0) int32_t GAMEAPI proposal_menu(int32_t faction_id, int32_t faction_id_with);
ADDR_NATIVE(0x54B1C0) void GAMEAPI battle_plans(int32_t faction_id, int32_t faction_id_with);
ADDR_NATIVE(0x558C60) void GAMEAPI commlink_attempt(int32_t faction_id_with);
// Closes the diplomacy dialog's interface mode. Headless: nothing.
ADDR_NATIVE(0x43F5C0) void THISCALL DiploPop_stop(uint8_t* pop);
// Checks before a human opens a commlink or acts on the diplomacy panel: faction_id_with | 0x10
// marks a faction not met or eradicated (message, refused), Ctrl/Shift with debug mode opens the
// debug dialog, multiplayer locks and open channels refuse. 0: go ahead. Headless: the single
// player path without keys (0, or 1 with the message for faction_id_with | 0x10).
ADDR_NATIVE(0x5589E0) int32_t GAMEAPI commlink_attempter(int32_t faction_id, int32_t faction_id_with);
// InfoWindow field cleared after a technology changes hands in the dialog (status pane, UI).
GLOBAL(int32_t, InfoWindowTechReset, 0x7AD330);
// Colours of the four mandates (reports), at [0], [2], [4], [6] (palette values).
GLOBAL(int32_t[8], MandateColors, 0x8C6DE4);

// ---- diplomacy module, second half (diplomacy_b.cpp) ----
// "he"/"she" of faction_id's leader in HisHerBuffer (labels 209/210; mode as get_his_her).
ADDR_NATIVE(0x58FCE0) char* GAMEAPI get_he_she(int32_t faction_id, int32_t mode);
// The diplomacy dialog (commlink) between faction_id and faction_id_with, one of them human
// (callers: comm_check, enemy_diplomacy, control_turn, commlink_attempt, the multiplayer wait
// loop). 34 KB of dialog code with the menus diplomacy_menu, make_a_proposal, proposal_menu,
// battle_plans, call_off_vendetta and base_swap (0x54B140-0x5589E0), which change treaties and
// trade bases, energy and technology as the human chooses. All-AI games never call it.
// Headless: nothing happens (returns 0, as when the human closes the commlink at once).
ADDR_NATIVE(0x54FFD0) int32_t GAMEAPI communicate(int32_t faction_id, int32_t faction_id_with, int32_t a3);
// Not original functions: the probe menus the original builds on a Popup object on its stack,
// shown only to a human player (VehAttackFlags bit 0). Headless: not shown,
// each returns its "cancelled" value (documented per function).
// Action menu (PROBEMENU items whose bit is set in `items`): choice, -1 cancelled.
int32_t ui_probe_menu(int32_t faction_id_tgt, uint32_t items, bool alien);
// Sabotage target menu (VIRUS: facilities of base_id, 98 planet buster, 0 production, 99 abort):
// choice, -1 cancelled.
int32_t ui_probe_sabotage_menu(int32_t base_id, bool planet_buster, const char* production);
// Two-part confirmation menus (THOUGHTCONTROL/THOUGHTMENU, SUBVERT/SUBVERTMENU; show_menu false:
// the cost is not affordable): choice, 0 cancelled.
int32_t ui_probe_confirm_menu(const char* label, const char* menu, bool show_menu);
// FRAMEMENU: faction to frame (0 none), offered among factions known to faction_id.
int32_t ui_probe_frame_menu(int32_t faction_id, int32_t faction_id_tgt);
// FREEWHO: captured leader to free among faction_id_tgt's killed-faction monuments, -1 cancelled.
int32_t ui_probe_free_menu(int32_t faction_id_tgt, bool alien);
// Excuse popups (EXCUSE, PACTEXCUSE, NETEXCUSE) after a probe was caught (busted_action > 0:
// the "BUSTED<n>" header): choice, 0 declined.
int32_t ui_probe_excuse(const char* label, int32_t busted_action, bool alien);
// Popup object (0x5410 bytes) constructed on the stack of order_veh/supply_options for their menus;
// while it exists, the UI refers to it (MainInfc). Headless: no-ops.
ADDR_NATIVE(0x4048A0) void THISCALL Popup_ctor(uint8_t* popup);
ADDR_NATIVE(0x406C00) void THISCALL Popup_dtor(uint8_t* popup);
