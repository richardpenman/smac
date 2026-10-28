// Console (timers/console modules 0x4CF7C0..0x4E2240, 0x50F460..0x51DF70): the main view's game
// controller. MainConsole (0x9156B0, engine/ui_bridge.h) is a Console: a MapWin (the main map view)
// whose fields from +0x23BD4 hold the human turn's state. The object lives in the memory image, so
// its fields are used in place through the typed view Console (console_win(self).veh); several of
// them are also engine globals (MapWinInVehMode, MapWinVehA, MapWinCursorX, ...).
//
// Reconstructed: the human turn (Console_human_turn, Console_veh_turn, Console_ready...), unit
// selection, the key dispatcher (Console_on_key_click: the main view's keys; menu commands, the
// scenario editor and debug keys are placeholders) and the unit orders Console_veh_key dispatches
// to skip, wait, sentry, hold, build base and terraform. The Console functions the engine calls are
// declared in engine/ui_bridge.h; smacx replaces their headless "console" area with these.
// Functions called here but not reconstructed yet are placeholders (console_placeholders.cpp).
#pragma once
#include <cstdint>

#include "engine/net.h"
#include "engine/ui_bridge.h"
#include "ui/mapwin.h"

// ---- the Console object (MainConsole): its MapWin, then the human turn's fields ----
// A view of the object at its original offsets (GraphicWin virtual base at +0x23D94).
struct Console {
    MapWin map;  // +0x0: the main view (engine/ui_bridge.h)
    uint8_t unk_21a6c[0x22A2C - 0x21A6C];
    uint8_t menu_bar[0x23BD4 - 0x22A2C];  // +0x22A2C: the console's menu bar object (main_menu)
    int32_t faction;  // +0x23BD4: faction whose console this is (the viewer)
    int32_t veh_mode;  // +0x23BD8: MapWinInVehMode: unit mode (1) or view mode (0)
    int32_t veh;  // +0x23BDC: MapWinVehA: selected vehicle (-1: none)
    int32_t next_veh;  // +0x23BE0: MapWinVehB: vehicle to select next (multiplayer)
    int32_t waiting;  // +0x23BE4: waiting for orders for the selected unit
    int32_t turn_active;  // +0x23BE8: the human turn goes on (0: end the turn)
    int32_t any_select;  // +0x23BEC: keep the selection if selectable (not only ready)
    int32_t no_focus;  // +0x23BF0: a go-to order is under way (no recentring)
    int32_t move_set;  // +0x23BF4: the unit was just selected by Console_set_move
    int32_t cursor_shown;  // +0x23BF8: the cursor is drawn
    int32_t cursor_on;  // +0x23BFC
    int32_t redraw;  // +0x23C00: the view needs a full redraw before the cursor
    int32_t unk_23c04;
    int32_t cursor_idx;  // +0x23C08: MapWinCursorIndex: current cursor history slot
    int32_t cursor_head;  // +0x23C0C: newest cursor history slot
    int32_t cursor_x[32];  // +0x23C10: MapWinCursorX
    int32_t cursor_y[32];  // +0x23C90: MapWinCursorY
    int32_t mode;  // +0x23D10: mouse interaction mode (go to, ...; 0: none)
    int32_t mode_range;  // +0x23D14: range of the mode's target (-1: any tile)
    int32_t search_reset;  // +0x23D18: Console_ready searches from the start
    int32_t unk_23d1c;  // +0x23D1C
    int32_t unk_23d20;  // +0x23D20: MapWinField23D20
    int32_t mp_search;  // +0x23D24: multiplayer: units of others' turns searched too
    uint8_t unk_23d28[0x23D94 - 0x23D28];
};
SMAC_LAYOUT(Console, 0x23D94);
static_assert(offsetof(Console, map) == 0x0, "Console layout");
static_assert(offsetof(Console, menu_bar) == 0x22A2C, "Console layout");
static_assert(offsetof(Console, faction) == 0x23BD4, "Console layout");
static_assert(offsetof(Console, veh_mode) == 0x23BD8, "Console layout");
static_assert(offsetof(Console, veh) == 0x23BDC, "Console layout");
static_assert(offsetof(Console, next_veh) == 0x23BE0, "Console layout");
static_assert(offsetof(Console, waiting) == 0x23BE4, "Console layout");
static_assert(offsetof(Console, turn_active) == 0x23BE8, "Console layout");
static_assert(offsetof(Console, any_select) == 0x23BEC, "Console layout");
static_assert(offsetof(Console, no_focus) == 0x23BF0, "Console layout");
static_assert(offsetof(Console, move_set) == 0x23BF4, "Console layout");
static_assert(offsetof(Console, cursor_shown) == 0x23BF8, "Console layout");
static_assert(offsetof(Console, cursor_on) == 0x23BFC, "Console layout");
static_assert(offsetof(Console, redraw) == 0x23C00, "Console layout");
static_assert(offsetof(Console, cursor_idx) == 0x23C08, "Console layout");
static_assert(offsetof(Console, cursor_head) == 0x23C0C, "Console layout");
static_assert(offsetof(Console, cursor_x) == 0x23C10, "Console layout");
static_assert(offsetof(Console, cursor_y) == 0x23C90, "Console layout");
static_assert(offsetof(Console, mode) == 0x23D10, "Console layout");
static_assert(offsetof(Console, mode_range) == 0x23D14, "Console layout");
static_assert(offsetof(Console, search_reset) == 0x23D18, "Console layout");
static_assert(offsetof(Console, mp_search) == 0x23D24, "Console layout");
inline Console& console_win(uint8_t* console) { return *reinterpret_cast<Console*>(console); }

// ---- globals used by the console ----
GLOBAL(int32_t, ConsoleTurnFlag, 0x90D930);  // cleared at the start of a human turn
GLOBAL(int32_t, TutorialTourPending, 0x8CC248);  // TutWin_tour requested
GLOBAL(int32_t, TutorialWinActive, 0x8CC210);  // dword_8CC210: a tutorial window is up
GLOBAL(int32_t, TutorialArrowShown, 0x8CC228);
GLOBAL(int32_t, NetSimultaneous, 0x93E8E0);  // multiplayer: units move simultaneously
GLOBAL(int32_t, NetLocked, 0x93E944);  // multiplayer: input locked
GLOBAL(int32_t, NetBusyCount, 0x93F664);  // nested network waits
GLOBAL(int32_t, MapWinMouseIndex, 0x68A5CC);  // map window under the mouse (-1: none)
GLOBAL(int32_t, ConsoleNotReady, 0x93A944);  // a key arrived before the game was shown
GLOBAL(int32_t, NewBaseX, 0x685478);  // site shown while naming a new base
GLOBAL(int32_t, NewBaseY, 0x68547C);
GLOBAL(uint8_t, ReportWindow, 0x876478);  // a report window (Thinker: ReportWin)
GLOBAL(uint8_t, ReportIfObj, 0x885F38);  // the reports' main interface part (ReportIf)
GLOBAL(uint8_t, MultiWinObj, 0x7FD648);  // the commlink panel (MultiWin)
GLOBAL(int32_t, NetLockHeld, 0x93E940);  // multiplayer: this client holds the unit lock

// ---- reconstructed functions (also: Console_human_turn, Console_focus, Console_ready_search,
// Console_set_move, Console_cursor_next, Console_update_data in engine/ui_bridge.h) ----
// Leaves unit mode for view mode (force: also when already in view mode); redraws the panes.
ADDR_NATIVE(0x511480) void THISCALL Console_set_view(uint8_t* console, int32_t force);
// Moves the selected unit through its turn until it has no orders or moves left; 1 if the
// player was waited for.
ADDR_NATIVE(0x513B90) int32_t THISCALL Console_veh_turn(uint8_t* console);
// Selects the next unit ready to move and centres the view on it.
ADDR_NATIVE(0x510F50) void THISCALL Console_ready(uint8_t* console);
// Key commands: skip (space), wait (w), sentry (l), hold (h, H: 10 turns).
ADDR_NATIVE(0x4D0450) void THISCALL Console_skip(uint8_t* console, int32_t veh_id);
ADDR_NATIVE(0x4D04A0) void THISCALL Console_wait(uint8_t* console, int32_t veh_id);
ADDR_NATIVE(0x4D2220) void THISCALL Console_sentry(uint8_t* console, int32_t veh_id, int32_t turns);
ADDR_NATIVE(0x4D24C0) void THISCALL Console_hold(uint8_t* console, int32_t veh_id, int32_t turns);
// Ctrl+Enter: ends the human turn (asks when units still have orders to give).
ADDR_NATIVE(0x5169F0) void THISCALL Console_end_my_turn(uint8_t* console);
// The selected unit's order keys: ch is the character, key the lower-cased character, vk the key
// with its modifier bits (0x10000 shift, 0x20000 ctrl, 0x40000 alt).
ADDR_NATIVE(0x5171D0) void THISCALL Console_veh_key(uint8_t* console, int32_t ch, int32_t key, int32_t vk);
// Keys acting on the cursor's tile in view mode.
ADDR_NATIVE(0x5166D0) void THISCALL Console_cursor_key(uint8_t* console, int32_t ch, int32_t key, int32_t vk);
// A key pressed in the main view (character ch, key vk with modifier bits). Returns 1.
ADDR_NATIVE(0x5178C0) int32_t THISCALL Console_on_key_click(uint8_t* console, int32_t ch, int32_t vk);
// Console_on_key_click's function keys and menu commands (F1..F12, ctrl+S, the preferences, ...);
// in_base: with the base window open (some keys do nothing).
void console_function_keys(uint8_t* console, int32_t vk, bool in_base);
// Build base (b): founds a base with a colony pod (asks for the name), or joins a base.
ADDR_NATIVE(0x4CF7C0) void THISCALL Console_new_base(uint8_t* console, int32_t veh_id);
// Terraforming orders (f, m, s, r, F, ...): checks the tile and starts terraform_id.
ADDR_NATIVE(0x4D3470) void THISCALL Console_terraform(uint8_t* console, int32_t veh_id, int32_t terraform_id);
// Scenario editor protection: 1 when editing is locked (editor on, Scroll Lock or lock flag).
ADDR_NATIVE(0x4E1F40) int32_t THISCALL Console_edit_lock(uint8_t* console);
// Zoom commands (0x203 in, 0x204 out, 0x205 normal, 0x206..0x208 presets) on map window `index`
// (-1: all; < 0 except -1: the one under the mouse).
ADDR_NATIVE(0x5150D0) void THISCALL pfncProcZoomKey(uint8_t* console, int32_t command, int32_t index);
// Hints for new players (tutorial preference): first base, second base, more bases, vendetta.
ADDR_NATIVE(0x511800) void THISCALL Console_turn_hints(uint8_t* console);
// Multiplayer with simultaneous moves: 1 while it is not this player's turn (net module).
ADDR_NATIVE(0x52DC70) int32_t GAMEAPI not_my_turn();

// ---- unit orders and mouse modes (console_orders.cpp) ----
// Mouse interaction modes (Console::mode, the cursor of CURSOR.PCX with the same index): 1 go to,
// 2 road to, 3 mag tube to, 4 drop, 8 patrol, 10 artillery, 11 bombing run / air patrol target.
ADDR_NATIVE(0x510400) void THISCALL Console_cursor_on(uint8_t* console, int32_t index);
ADDR_NATIVE(0x510490) void THISCALL Console_cursor_on_2(uint8_t* console, int32_t mode, int32_t range);
ADDR_NATIVE(0x510520) void THISCALL Console_cursor_off(uint8_t* console);
// The map wants a target tile: the cursor for (x, y) (out of range: the "no" cursor); 0 if no mode.
ADDR_NATIVE(0x5105C0) int32_t THISCALL Console_selecting(uint8_t* console, int32_t x, int32_t y);
// Ends the mode; returns it (0: none).
ADDR_NATIVE(0x5107E0) int32_t THISCALL Console_selected(uint8_t* console);
ADDR_NATIVE(0x50EF50) void GAMEAPI go_reset();
ADDR_NATIVE(0x4D2F00) void THISCALL Console_activate(uint8_t* console, int32_t x, int32_t y, int32_t select);
// Go-to list (mode: ORDER_MOVE_TO, ROAD_TO, MAGTUBE_TO, 0x80 bombing run; -1 a base's rally point
// (id: the base), -2 find a base, -3 psi gate); group: the units of the stack follow.
ADDR_NATIVE(0x4D61A0) void THISCALL Console_go_to(uint8_t* console, int32_t id, int32_t mode, int32_t group);
ADDR_NATIVE(0x4D6070) void GAMEAPI Console_goto_routine(int32_t base_id);
ADDR_NATIVE(0x4D7DE0) void THISCALL Console_go_home(uint8_t* console, int32_t veh_id);
// Automation (veh state 0x200, order_auto_type mode): 0 full, 1 roads, 2 tubes, 3 farm + solar,
// 4 farm + mine, 5 mines / boreholes..., 6 forests, 7 sensors, 10 bombing target, 11 air
// defence, 12 air patrol.
ADDR_NATIVE(0x4D8A70) void THISCALL Console_automate(uint8_t* console, int32_t veh_id, int32_t mode);
ADDR_NATIVE(0x4D04E0) void THISCALL Console_unload(uint8_t* console, int32_t veh_id);
ADDR_NATIVE(0x4D5D30) void THISCALL Console_oblit(uint8_t* console, int32_t veh_id);
ADDR_NATIVE(0x4D9170) void THISCALL Console_arty(uint8_t* console, int32_t veh_id);
ADDR_NATIVE(0x4D7F60) void THISCALL Console_use_ability(uint8_t* console, int32_t veh_id);
ADDR_NATIVE(0x4D8F40) void THISCALL Console_explore(uint8_t* console, int32_t veh_id);
ADDR_NATIVE(0x4D8FA0) void THISCALL Console_convoy(uint8_t* console, int32_t veh_id);
ADDR_NATIVE(0x4D7EE0) void THISCALL Console_set_patrol(uint8_t* console, int32_t veh_id);
ADDR_NATIVE(0x4D7CF0) void THISCALL Console_designate(uint8_t* console, int32_t veh_id);
ADDR_NATIVE(0x4D7BE0) void THISCALL Console_road_to(uint8_t* console, int32_t veh_id, int32_t order);
ADDR_NATIVE(0x4D26A0) void THISCALL Console_give_veh(uint8_t* console, int32_t veh_id);
ADDR_NATIVE(0x4D52A0) void THISCALL Console_destroy(uint8_t* console, int32_t veh_id);
ADDR_NATIVE(0x4D2AC0) void THISCALL Console_disband(uint8_t* console, int32_t veh_id);
ADDR_NATIVE(0x4D2DE0) void THISCALL Console_disband_2(uint8_t* console, int32_t x, int32_t y);
// The units of the leader's tile (or the marked group) take the leader's go-to orders.
ADDR_NATIVE(0x4D92D0) void THISCALL Console_group(uint8_t* console, int32_t veh_id);
ADDR_NATIVE(0x4D8DC0)
void THISCALL Console_set_road_to(uint8_t* console, int32_t veh_id, int32_t x, int32_t y, int32_t order);
ADDR_NATIVE(0x4D8CD0) void THISCALL Console_set_bombing_run(uint8_t* console, int32_t veh_id, int32_t x, int32_t y);
// Unit pick list of a stack (PICKUNIT...): the chosen unit, -1 cancelled.
ADDR_NATIVE(0x48D0D0) int32_t GAMEAPI veh_pick(int32_t veh_id, const char* label, int32_t cancel);
// A popup with a unit's picture; returns the answer.
ADDR_NATIVE(0x48C360) int32_t GAMEAPI popv(const char* file, const char* label, int32_t veh_id, int32_t flags);
// X_pop_9 of the script file (veh module 0x5BF3F0).
ADDR_NATIVE(0x5BF3F0) int32_t GAMEAPI X_pop_6(const char* label, int32_t flags, int32_t a3);

// Load game (scenario: a scenario) and the game's display reset after it.
ADDR_NATIVE(0x51BBA0) void THISCALL Console_load_proc(uint8_t* console, int32_t scenario, int32_t relinearize);

// ---- the map's mouse code (console_mouse.cpp) ----
// MapWin's handlers (offsets from the MapWin object, the original's are from its GraphicWin base).
ADDR_NATIVE(0x46ED30) void THISCALL MapWin_on_left_down(uint8_t* win, int32_t px, int32_t py);
ADDR_NATIVE(0x46EBA0) void THISCALL MapWin_on_left_click(uint8_t* win, int32_t px, int32_t py);
ADDR_NATIVE(0x46EBE0) void THISCALL MapWin_on_right_click(uint8_t* win, int32_t px, int32_t py);
ADDR_NATIVE(0x46EC10) void THISCALL MapWin_on_right_down(uint8_t* win, int32_t px, int32_t py);
ADDR_NATIVE(0x46F000) void THISCALL MapWin_on_mouse_move(uint8_t* win, int32_t px, int32_t py);
// A click on the map: select / move / give the pending mode's order (right: menu or info).
ADDR_NATIVE(0x46D5D0) void THISCALL MapWin_click(uint8_t* win, int32_t px, int32_t py, int32_t right);
ADDR_NATIVE(0x50F170) void GAMEAPI go_timer();
ADDR_NATIVE(0x5147E0) int32_t THISCALL Console_set_cursor(uint8_t* console, int32_t x, int32_t y);

// The menu bar's commands by command id (console_menu.cpp; Menu_menu_event -> Console_menu_daemon).
ADDR_NATIVE(0x51BC70) void THISCALL Console_menu_proc(uint8_t* console, int32_t id);
// The next position of the cursor history (shift+Backspace).
ADDR_NATIVE(0x510B70) void THISCALL Console_next_cursor(uint8_t* console);
// Landmarks (mapwin_menu.cpp): name / rename the landmark at (x, y) (off the map: the cursor's
// tile; LANDMARK text entry) and erase it (LANDMARKKILL).
ADDR_NATIVE(0x5162A0) void THISCALL Console_name_landmark(uint8_t* console, int32_t x, int32_t y);
ADDR_NATIVE(0x516490) void THISCALL Console_erase_landmark(uint8_t* console, int32_t x, int32_t y);
// Scenario editor: edit / kill the unit (veh -1: the one at the cursor).
ADDR_NATIVE(0x4DDA50) void THISCALL Console_editor_edit_veh(uint8_t* console, int32_t veh_id);
ADDR_NATIVE(0x4DE960) void THISCALL Console_editor_kill_veh(uint8_t* console, int32_t veh_id);

// ---- placeholders: original functions the console calls that are not reconstructed yet
// (console_placeholders.cpp). Each logs its name once and does nothing unless noted. ----
ADDR_NATIVE(0x46AE00) void GAMEAPI draw_cursor();
ADDR_NATIVE(0x46AD60) void THISCALL MapWin_draw_cursor(uint8_t* win);
ADDR_NATIVE(0x458900) void THISCALL InfoWin_change(uint8_t* win, int32_t a1);
ADDR_NATIVE(0x459280) void THISCALL InfoWin_reset(uint8_t* win);
ADDR_NATIVE(0x4B9F90) void THISCALL StatusWin_set_loc(uint8_t* win, int32_t x, int32_t y);
ADDR_NATIVE(0x4C3990) void THISCALL WorldWin_set_world_map(uint8_t* win);
ADDR_NATIVE(0x4C3950) void THISCALL WorldWin_set_detail_map(uint8_t* win);
// detailmap.cpp: the world map pane's detail map (MapWins[1]) is shown in place of the world map.
bool worldwin_detail_shown();
// WorldWin_click's shift click and WorldWin_right_menu's item 4: MapWins[1] moved to (x, y) (the
// main view's centre) and shown in the pane (WorldWin_set_detail_map's steps).
void worldwin_show_detail_at(int32_t x, int32_t y);
// The world map pane into the main interface's canvas: the detail map or WorldWin's map.
void worldpane_draw(Buffer* mi);
ADDR_NATIVE(0x460DD0) void THISCALL pfncMainMenuUpdateVisible(uint8_t* menu, int32_t veh_id);
ADDR_NATIVE(0x4BC6E0) void THISCALL TutWin_tour(uint8_t* win);
ADDR_NATIVE(0x4BDDE0) int32_t THISCALL TutWin_reset(uint8_t* win);
ADDR_NATIVE(0x511BB0) void THISCALL Console_give_hints(uint8_t* console);
ADDR_NATIVE(0x446D10) void GAMEAPI ambience_veh(int32_t veh_id);
ADDR_NATIVE(0x4D95F0) void THISCALL Console_assemble(uint8_t* console);
ADDR_NATIVE(0x515450) void THISCALL Console_survey(uint8_t* console);
ADDR_NATIVE(0x515900) void THISCALL Console_time_controls(uint8_t* console);
// time_controls_dialog (0x589330, console_placeholders.cpp): TIMECONTROLS's six items in `pop`.
class BasePop;
void time_controls_dialog(BasePop* pop);
ADDR_NATIVE(0x517060) void THISCALL Console_use_time_bonus(uint8_t* console);
ADDR_NATIVE(0x515010) void THISCALL Console_set_tech(uint8_t* console, int32_t faction_id);
ADDR_NATIVE(0x5165B0) void THISCALL Console_zoom_to_upkeep_base(uint8_t* console);
ADDR_NATIVE(0x516C50) void THISCALL Console_show_scen_objectives(uint8_t* console);
ADDR_NATIVE(0x4DD1B0) void THISCALL Console_custom_my_faction(uint8_t* console);
ADDR_NATIVE(0x4E2000) void THISCALL Console_corner_the_market(uint8_t* console);
// Resign (shift+Q): REALLYRETIRE, then the score, the rating, the hall of fame, the replay and back
// to the main menu (prefwin.cpp).
ADDR_NATIVE(0x514B30) void THISCALL Console_retire_game(uint8_t* console);
// The scenario editor's game rules (RULES' check boxes; prefwin.cpp).
ADDR_NATIVE(0x4DC230) void THISCALL Console_editor_rules(uint8_t* console);
// ---- the scenario editor (editor.cpp) ----
ADDR_NATIVE(0x4D9970) void THISCALL Console_editor_on(uint8_t* console);
ADDR_NATIVE(0x4E0A50) void GAMEAPI Console_editor_paints(int32_t paint);
ADDR_NATIVE(0x4E0EA0) void GAMEAPI Console_editor_set_brush();
ADDR_NATIVE(0x4E0AD0) void GAMEAPI Console_editor_minor();
ADDR_NATIVE(0x4E0B00) void GAMEAPI Console_editor_terrain();
ADDR_NATIVE(0x4E0F30) void GAMEAPI Console_editor_flat();
ADDR_NATIVE(0x4E0F80) void GAMEAPI Console_editor_polar();
ADDR_NATIVE(0x4E0FA0) void GAMEAPI Console_editor_climate();
ADDR_NATIVE(0x4E14A0) void GAMEAPI Console_editor_rockiness();
ADDR_NATIVE(0x4E13E0) void THISCALL Console_editor_fungus(uint8_t* console);
ADDR_NATIVE(0x4E1290) void GAMEAPI Console_editor_clear();
ADDR_NATIVE(0x4E1300) void GAMEAPI Console_editor_clear_vehicles();
ADDR_NATIVE(0x4E1380) void GAMEAPI Console_editor_clear_terrain();
ADDR_NATIVE(0x4E1190) void GAMEAPI Console_editor_randomize();
ADDR_NATIVE(0x4E1200) void GAMEAPI Console_editor_clear_random();
ADDR_NATIVE(0x4E10C0) void GAMEAPI Console_editor_fast();
ADDR_NATIVE(0x4E0FD0) void GAMEAPI Console_editor_generate();
ADDR_NATIVE(0x4E1F70) void GAMEAPI Console_editor_lock2();
ADDR_NATIVE(0x4E1F20) void GAMEAPI Console_editor_undo();
ADDR_NATIVE(0x4E1F30) void GAMEAPI Console_editor_redo();
ADDR_NATIVE(0x4E09E0) void GAMEAPI Console_editor_save();
ADDR_NATIVE(0x4E0A00) void GAMEAPI Console_editor_load();
ADDR_NATIVE(0x4E0120) void GAMEAPI Console_editor_energy();
ADDR_NATIVE(0x4E0210) void GAMEAPI Console_editor_date();
ADDR_NATIVE(0x4E09B0) void GAMEAPI Console_editor_kill_vehicles();
ADDR_NATIVE(0x4DED00) void GAMEAPI Console_editor_veh();
ADDR_NATIVE(0x4DBD20) void GAMEAPI Console_editor_personality();
ADDR_NATIVE(0x4DBFB0) void GAMEAPI Console_editor_strategy();
ADDR_NATIVE(0x4DBB40) void GAMEAPI Console_editor_reset_faction();
ADDR_NATIVE(0x4DBC40) void THISCALL Console_editor_reset_tech(uint8_t* console);
ADDR_NATIVE(0x4DD6F0) void GAMEAPI Console_editor_diff();
ADDR_NATIVE(0x4E0290) void GAMEAPI Console_editor_reload();
ADDR_NATIVE(0x4E05E0) void GAMEAPI Console_editor_eliminate();
ADDR_NATIVE(0x4DC520) void GAMEAPI Console_editor_scen_rules();
ADDR_NATIVE(0x4DC860) void GAMEAPI Console_editor_scen_victory();
ADDR_NATIVE(0x4DCCC0) void GAMEAPI Console_editor_scen_param();
ADDR_NATIVE(0x4DFAD0) void GAMEAPI Console_editor_tech();
ADDR_NATIVE(0x4DF4F0) void GAMEAPI Console_editor_view();
ADDR_NATIVE(0x4E14D0) void GAMEAPI Console_editor_natural(int32_t place);
ADDR_NATIVE(0x4DB870) void GAMEAPI Console_editor_diplomacy();
ADDR_NATIVE(0x515F70) void THISCALL Console_show_movies(uint8_t* console);
// Console_on_key_click's ctrl+shift+F2 (inline in the original): every technology given / taken.
void editor_all_techs();
// The omniscient view toggled (the y key / the Scenario menu's 0x601): the editor's panel button shown
// or hidden (EditorPanelWin's slots 1 / 2) and the panes redrawn.
void editor_toggle_omniscient();
// Console_on_key_click's editor keys (y, shift / ctrl / ctrl+shift F-keys, the digits); true
// when the key was the editor's.
bool editor_key(uint8_t* console, int32_t key, int32_t ch);
// PrefWin_display (0x48FA00): the preferences dialog on a page (0 preferences, 1 advanced,
// 2 warnings, 3 automation, 4 audio/visual, 5 map display; prefwin.cpp).
void prefwin_display(int32_t page);
// Tutorial popup (popup `label` of file, e.g. TUTOR).
ADDR_NATIVE(0x48C320) int32_t GAMEAPI popt(const char* file, const char* label, int32_t a3, int32_t a4, int32_t a5);
// Text entry popup (label of SCRIPT.txt): the default text in `text`; 0 when confirmed, with the
// entry in ParseStrBuffer[0].
ADDR_NATIVE(0x5BFAB0)
int32_t GAMEAPI X_pop_ask(const char* label, int32_t max_len, const char* text, int32_t a4, int32_t a5);

// MultiWin_on_button_clicked (0x477F00, multiwin.cpp): 0 the chat, 1 the Council, else nothing.
ADDR_NATIVE(0x477F00) void THISCALL MultiWin_on_button_clicked(uint8_t* self, int32_t id);
// Console_chat (0x515770): the CHATASK message to the other players; nothing outside multiplayer.
ADDR_NATIVE(0x515770) void THISCALL Console_chat(uint8_t* console, int32_t faction_id);

// design_workshop (0x583E30, designwin.cpp): the old design workshop's prototype list (unreachable
// in the original: BaseWin_production's 9999 answer); with `pick` the slot to design in opens.
ADDR_NATIVE(0x583E30) int32_t GAMEAPI design_workshop(int32_t faction_id, int32_t pick);

// The auto play timer (win.cpp): Console_on_key_click's ctrl+shift+F11 starts / stops it; its tick
// (auto_play_callback 0x50E890) from smacx's input loops (main_loop: the console's).
void auto_play_toggle();
void auto_play_tick(bool main_loop);

// ---- not original functions ----
// Placeholder of an original function that is not reconstructed: logs the name once (stderr) and
// shows it on the message line.
void console_not_reconstructed(const char* name);
// The same without the message line (UI-only calls made on every key).
void console_not_reconstructed_quiet(const char* name);
// Single-player paths of the net layer (net module, not reconstructed), with the console's
// arguments: what the original NetDaemon functions do when MultiplayerActive is 0 (multiplayer:
// the original function is called; native builds stop there).
int32_t net_lock_veh(int32_t* veh_id, int32_t a2);
void net_unlock_veh();
void net_synch_veh(int32_t veh_id);
int32_t net_order_veh(int32_t veh_id, int32_t dir, int32_t a3);
int32_t net_action(int32_t veh_id, int32_t a2);
// synch_veh / synch_base (NetDaemon_synch: nothing without multiplayer).
void ui_synch_veh(int32_t veh_id);
void ui_synch_base(int32_t base_id);
void net_await_synch();  // NetDaemon_await_synch: nothing without multiplayer
int32_t net_add_global();  // NetDaemon_add_global: 0 without multiplayer
// The mouse cursor over the map windows (Win_set_cursor of CURSOR.PCX sprite `index`; -1: the
// arrow) and the mouse position in screen coordinates (Win_get_mouse_pos).
void ui_set_map_cursor(int32_t index);
void ui_mouse_pos(int32_t* x, int32_t* y);
// Every open map window: index 0 is the main view; others count when active (MapWin::active).
template <typename F>
inline void for_each_mapwin(F&& fn) {
    for (int32_t i = 0; i < 8; i++) {
        uint8_t* w = MapWins[i];
        if (w && (i == 0 || map_win(w).active)) fn(i, w);
    }
}
