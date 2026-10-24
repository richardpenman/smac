// UI-layer functions called from engine code.
//
// The original engine calls straight into its UI (popups, map redraws, sound, window classes).
// Those functions belong to the UI reconstruction; until then they are declared here with
// ADDR_NATIVE: hybrid builds always call the original UI, and native builds use the headless
// implementations in src/headless/ui_headless.cpp, which behave like an unattended observer
// (no drawing, popups return their default choice). Keep declarations grouped by original class.
#pragma once
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
// Map window state stored in savegames (save module: game_io, load_daemon, load_map_daemon).
// MapWin fields by byte offset of the original class (UI objects, opaque otherwise).
inline int32_t& mapwin_field(uint8_t* win, uint32_t offset) { return *reinterpret_cast<int32_t*>(win + offset); }
constexpr uint32_t kMapWinActive = 0x1DD74;  // non-zero while the window is in use
GLOBAL(uint8_t, WorldMapWin, 0x8EB48C);      // the world window's map view (MapWin)
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
GLOBAL(uint8_t[8][0x30], DiploTextLists, 0x93A7B0);        // [1..7] used
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

// ---- popups ----
ADDR_NATIVE(0x48C0A0) int32_t GAMEAPI popp(const char* file, const char* label, int32_t flags, const char* pcx,
                                           int32_t flags2);
// Popup from a text file (X_pop_9 without picture); returns the choice (0: first).
ADDR_NATIVE(0x5BF330) int32_t GAMEAPI X_pop_2(const char* label, const char* pcx, int32_t flags);

// ---- message pump ----
// Processes pending window messages (the original keeps the UI responsive during long loops).
ADDR_NATIVE(0x5FCB20) void GAMEAPI do_all_non_input();

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
ADDR_NATIVE(0x5C9860) int32_t THISCALL MultiDebug_init(uint8_t* self, uint8_t* net_state, uint8_t* console,
                                                       uint8_t* param);
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
ADDR_NATIVE(0x601BF0) int32_t THISCALL BasePop_start(uint8_t* pop, const char* file, const char* label, int32_t a3,
                                                     int32_t a4, int32_t a5, int32_t a6);
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
ADDR_NATIVE(0x5900D0) void THISCALL Lock_clear(uint8_t* lock);
ADDR_NATIVE(0x4E25B0) void THISCALL AlphaNet_close(uint8_t* net);
// Multiplayer setup: name of the loaded game and its kind (0x811D1C, 0x811E20).
GLOBAL(char[256], NetGameFileName, 0x811D1C);
GLOBAL(int32_t, NetGameFileKind, 0x811E20);

// ---- interface buttons, faction art ----
// Terrain-improvement buttons of the main interface (BaseButton objects, 0xB4C bytes each).
GLOBAL(uint8_t[19][0xB4C], TerraformButtons, 0x7BB62C);
ADDR_NATIVE(0x607550) void THISCALL BaseButton_set_bubble_text(uint8_t* button, const char* text);
ADDR_NATIVE(0x453710) void GAMEAPI load_faction_art(int32_t faction_id);

// ---- vehicles, spotting (veh/stack modules) ----
GLOBAL(uint8_t, Sounds, 0x749CF8);       // sound effects object (FX)
GLOBAL(uint8_t, NetMsg, 0x805338);       // message box object
GLOBAL(uint8_t, InfoWindow, 0x7AD2A0);   // status/info pane (Thinker's InfoWin)
ADDR_NATIVE(0x446A00) void THISCALL FX_play(uint8_t* sounds, int32_t id);
ADDR_NATIVE(0x446AD0) void THISCALL FX_fade(uint8_t* sounds, int32_t id);
ADDR_NATIVE(0x47A890) int32_t THISCALL NetMsg_pop(uint8_t* msg, const char* label, int32_t delay, int32_t a3,
                                                  const char* pcx);
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
ADDR_NATIVE(0x5BEB70) int32_t GAMEAPI tech_pick(int32_t faction_id, int32_t is_free, int32_t from_faction_id,
                                                const char* label);
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
// Hybrid builds and headless: not displayed (the object exists only inside the original
// function; only reached for the viewer or with GameState 0x80).
void ui_tech_popup_open();
void ui_tech_popup_show(int32_t tech_id, int32_t flags, int32_t faction_id, bool with_help);
void ui_tech_popup_exec();
// Alert popup (shown only to the viewer, with sound): label of SCRIPT.txt.
ADDR_NATIVE(0x48C650) int32_t GAMEAPI popb(const char* label, int32_t flags, int32_t sound_id, const char* pcx,
                                           int32_t a5);
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
// Hybrid builds and headless: not displayed (as ui_tech_popup_*).
void ui_production_popup_open(int32_t width);
void ui_production_popup_blurb(bool is_project);
void ui_production_popup_exec();
// Secret project movies and help (shown to the viewer).
ADDR_NATIVE(0x403BA0) void GAMEAPI amovie_project(int32_t project_id);
ADDR_NATIVE(0x403BE0) void GAMEAPI amovie_project_2(const char* name);
ADDR_NATIVE(0x44C9E0) void GAMEAPI help_project(int32_t project_id);
// Voice clip of a new facility (a Wave object; StrBuffer holds voices\fac<n>.mp3).
GLOBAL(uint8_t, FacilityVoice, 0x945ED0);
ADDR_NATIVE(0x4C6EA0) int32_t THISCALL Wave_unload(uint8_t* wave);
ADDR_NATIVE(0x4C6C20) int32_t THISCALL Wave_load(uint8_t* wave, const char* file);
ADDR_NATIVE(0x4C6920) int32_t THISCALL Wave_play_2(uint8_t* wave);
ADDR_NATIVE(0x4C6600) void THISCALL Sound_fade_2(uint8_t* wave, int32_t ms);
