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

// ---- setup module (setup_game, eliminate_player) ----
// Diplomacy offer slots of every faction pair (DiploStateC[i][j][0..3]) cleared (0x441250).
ADDR_NATIVE(0x441250) void GAMEAPI clear_all_offers();
// Hotseat/PBEM: every diplomatic message list (DiploMessageLists) emptied (0x440F80; the
// original deletes the StringStruct entries; the effect is ui_stringlist_clear on each list).
ADDR_NATIVE(0x440F80) void GAMEAPI clear_hotseat_chat();
// Council notification state cleared: CouncilVoteState and CouncilProposal (0x4243F0).
ADDR_NATIVE(0x4243F0) void GAMEAPI clear_council_notify_2();
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
GLOBAL(int32_t, BlitDestScale, 0x696D18);    // piDestScale
GLOBAL(int32_t, BlitSourceScale, 0x696D1C);  // piSourceScale
// Not original functions: the UI parts of crash_landing (a function that keeps its windows,
// buffers and flic players on its stack). drop: the landing sound (preference 0x400) and the
// 30-frame drop pod animation (alien pod or drop pod with smoke) over the map centred on (x, y),
// with the map window's option bits 0x8C0 cleared; close: releases the animation canvas and fades
// the sound. first_base_name: the FIRSTBASE popup that lets the player rename the first base
// (default_name); the chosen name is left in ParseStrBuffer[0]. Hybrid builds and headless: not
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
// The Planetary Council session (CouncilWin): votes on proposal by faction_id (candidate for the
// governor elections) and applies the result (council_action). UI code that holds the vote logic:
// not reconstructed yet (native builds abort when an AI calls the council).
ADDR(0x428350) void GAMEAPI council(int32_t faction_id, int32_t proposal, int32_t candidate);
// End of game screens: credits, score report, rating (quayle), hall of fame, replay.
ADDR_NATIVE(0x428DF0) void GAMEAPI show_credits();
ADDR_NATIVE(0x4ACB80) void GAMEAPI report_score(int32_t a1);
ADDR_NATIVE(0x496670) void GAMEAPI quayle(int32_t faction_id);
ADDR_NATIVE(0x44AE50) void GAMEAPI hall_of_fame(int32_t a1);
ADDR_NATIVE(0x5ADBF0) void GAMEAPI show_replay();
// Main menu (new game, load, ...). Non-zero: quit.
ADDR_NATIVE(0x58E360) int32_t GAMEAPI top_menu(int32_t a1);
// Multiplayer lobby launch check (command line); non-zero when started from a lobby.
ADDR_NATIVE(0x62D9E0) int32_t THISCALL Net_check_for_lobby(uint8_t* net, char* cmd_line, uint8_t* a2, int32_t a3, int32_t a4);
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
GLOBAL(char[256], ReloadFilePath, 0x9B2078);   // control_game: alternate path for game_reload
GLOBAL(char[256], ReloadFileSource, 0x93AA0C);
// Not original functions: the parts of control_game, control_turn and call_council that work on
// windows, the registry or popup objects on the original's stack.
// DirectPlay registration complete (HKLM DirectPlay application key "Complete" value).
bool ui_directplay_registered();
// Main map window display options (field +0x1DD70) |= bits.
void ui_main_mapwin_set_options(uint32_t bits);
// Hotseat/PBEM: hides the previous player's view and asks the next human (faction_id) to take
// over (SWITCHING popup, password check, optional save and quit). Returns 0 when the player takes
// the turn, 1 when the game was left (ControlTurnA set). Hybrid builds and headless: not shown
// (returns 0).
int32_t ui_hotseat_switch(int32_t faction_id);
// Hotseat/PBEM: shows the new player's view (centred on a base or unit), the battle summary popup
// and the message list. Hybrid builds and headless: not shown.
void ui_hotseat_show(int32_t faction_id);
// call_council's menu of proposals (a Popup object on its stack). Hybrid builds and headless: not
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
ADDR_NATIVE(0x4BE650) void THISCALL TutWin_tut_map(uint8_t* win, const char* label, int32_t x, int32_t y, int32_t veh_id,
                                                  uint8_t* image, int32_t a6, int32_t a7, int32_t a8);
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
// routine). Game state effect: unless flags & 0x4000, the vehicle's rotate_angle becomes dir
// (dir & 7 when dir > 7) in multiplayer games or when flags & 1 is clear. Headless: that effect.
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

// ---- battle module (battle_fight_2, planet_busting, interceptor, promote) ----
// Battle window (BattleWin) and the status/info pane (InfoWin) showing the combatants.
ADDR_NATIVE(0x45D310) void THISCALL SubInterface_set_iface_mode(uint8_t* win);
ADDR_NATIVE(0x422C50) void THISCALL BattleWin_clear(uint8_t* win);
ADDR_NATIVE(0x421B20) void THISCALL BattleWin_pulse_timer(uint8_t* win);
ADDR_NATIVE(0x421B40) void THISCALL BattleWin_stop_timer(uint8_t* win);
ADDR_NATIVE(0x4B8830) void THISCALL StatusWin_draw(uint8_t* win, int32_t veh_id_atk, int32_t veh_id_def, int32_t offense,
                                                   int32_t defense, int32_t flag);
ADDR_NATIVE(0x4B9EA0) void THISCALL StatusWin_redraw(uint8_t* win);
ADDR_NATIVE(0x4B8970) void THISCALL StatusWin_reset(uint8_t* win);
GLOBAL(int32_t, InfoWinBattleShown, 0x8C6B3C);  // InfoWin field: a battle is shown in the pane
// Base window refresh after a multiplayer battle (BaseWin field; non-zero: refresh).
GLOBAL(int32_t, BaseWinMultiplayerRefresh, 0x6E8150);
ADDR_NATIVE(0x415DD0) void THISCALL BaseWin_focus(uint8_t* win);
// Redraws two tiles (attacker and defender) with their surroundings.
ADDR_NATIVE(0x46B040) void GAMEAPI draw_tile_fixup_2(int32_t x1, int32_t y1, int32_t x2, int32_t y2, int32_t a5,
                                                     int32_t a6);
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
// the choice, 0 if cancelled. Hybrid builds and headless: not shown (returns 0); only the viewer's
// own supply crawlers ask.
int32_t ui_supply_menu(int32_t veh_id, int32_t base_id, int32_t items);
// Engine pitch of the moving unit's sound (order_veh; 0 restores it).
ADDR_NATIVE(0x446B10) void THISCALL FX_engine_pitch(uint8_t* sounds, int32_t pitch);
// Not an original function: GetAsyncKeyState(VK_CONTROL) high byte (order_veh: no map centering).
bool ui_ctrl_pressed();
// Not original functions: order_veh's menus (a Popup object on its stack). Artifact entering one of
// the viewer's bases (ARTIFACTMENU: 0 keep, 1 link to the network node, 2 hurry production;
// `items` lines) and a ship's passengers disembarking (DISEMBARKERS: the chosen vehicle + 1, 0
// cancelled). Hybrid builds and headless: not shown (return 0).
int32_t ui_artifact_menu(int32_t veh_id, int32_t base_id, int32_t items);
int32_t ui_disembark_menu(int32_t veh_id);
