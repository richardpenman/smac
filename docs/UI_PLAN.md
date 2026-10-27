# UI reconstruction plan

The engine (src/engine) is reconstructed and runs natively; the user interface still goes through
the bridge (engine/ui_bridge*.h, `ADDR_NATIVE`, headless stubs in src/headless). This plan covers
reconstructing the original UI on SDL2 so the game is playable on Linux, macOS and Windows and
looks and behaves like terranx.exe.

## 1. The original UI

### Size by module (re/out/index.tsv)

| Module | Range | Code | Contents |
| --- | --- | --- | --- |
| ui | 0x401000-0x4C9420 | ~770 KB | popups, BaseWin, MapWin, DesignWin, DiploWin, reports, interface, FX/sound, vehdraw |
| console, timers | 0x4CF7C0-0x4E39D0, 0x50E890-0x51E000 | ~140 KB | Console (main controller: keys, menus, human turn), AlphaNet |
| game | 0x5882F0-0x590510 | 33 KB | setup dialogs, top menu, game init |
| gfx | 0x5D4510-0x5FD4C0 | ~170 KB | Heap, GraphicWin, Buffer, Sprite, Win, Menu/PullDown, task loop |
| gfx2 | 0x5FE2A0-0x625750 | ~160 KB | Palette, filefind, BasePop, Scroll, buttons, ListBox, EditBox, Time, Font, Texture |
| pop, util, log | 0x626040-0x62D3A0 | ~30 KB | pop wrappers, Filemap, StringBox, polygon |
| video | 0x634BB0-0x644EF2 | ~60 KB | env, DirectDraw/GDI video, FileDialog, movies |
| diplomacy (part) | 0x538F30.. | | veh_draw, base_draw (misfiled UI), diplomacy dialogs |

### Class hierarchy (C++ with virtual bases; vtables in .rdata)

| Class | Base | Role |
| --- | --- | --- |
| Win (0x5EB3D0) | | window: rect, parent/children, z-order, modal state, message handlers via vtable (on_redraw +0x?, on_key, on_mouse...) |
| GraphicWin (0x5D4CF0) | Win | window with an 8-bit canvas: Buffer at +0x444, client rect +0x474, size +0x4C4 / -height +0x4C8 |
| MapWin (0x4626E0) | GraphicWin (virtual, +0x21A6C) | isometric map view; 0x22480 bytes; fields 0x1DD6C..0x1DE00 (zoom, centre, origin, tile metrics), terrain cache at +4 |
| Console (0x50F460) | MapWin, GraphicWin (+0x23D94) | the main view and game controller (MainConsole 0x9156B0 = MapWins[0]) |
| WorldWin, StatusWin, InfoWin, MessageWin | GraphicWin | mini map, unit/tile info, message list |
| BaseWin, DesignWin, SocialWin, DiploWin, CouncilWin, Datalink, ReportWin | GraphicWin | full-screen dialogs |
| BasePop / Popup (0x600860) | GraphicWin | script-driven popups (`popp`, `X_pop*`, `pops_*`) |
| Widgets | Win | BaseButton, FlatButton, PushButton, ImageButton, Scroll, ListBox, RadioButton, CheckBox, EditBox, PullDown, Menu, SpriteBox, StringBox |

### Graphics layer

* **Buffer** (0x5D7210): 8-bit DIB section (top-down: height stored negated) with a 256-entry colour
  table, clip rect (+0x20), stride (+0x4A8), up to 4 fonts and per-font text colour/shadow
  (+0x52C..+0x56C). Drawing: fill, box, lines, `Buffer_copy_*`, text (`Buffer_write_*`, `{}`/`[]`
  font switches, `$LINK<>` hyperlinks, `~` colour runs).
* **Palette**: one 8-bit game palette (0x94C590): Windows static colours 0..9/246..255, PALETTE.PCX
  in 10..245. Every art file is loaded with `Buffer_load_pcx(file, pal, 10, 236)`: pixels +10, so raw
  index 255 becomes the transparent key 9. Colour tables (shadow, tints) are derived with
  `Palette_create_table` (HSV) and cached in `*.tmp` files. Palette cycling (water, glow) runs on a
  timer and consumes the game LCG (oracle redirects it).
* **Sprite** (0x2C): bits, size, offsets, key at +8; drawn with transparency, a shadow table for
  pixel 0, and integer scaling (`piDestScale/piSourceScale`, Bresenham tables).
* **Texture** (0x70): 56x56 terrain cells, drawn as textured gouraud polygons
  (`pfncImageCopytoCanvas2`, `Texture_draw_*`): light is added to the texel index (terrain art uses
  colour ramps), dithered rows (`color 0x41`) for tiles not currently seen.
* **Fonts**: GDI `CreateFontIndirect` ("Arial", pixel heights) and `TextOut` with baseline
  alignment on the DIB; no antialiasing.
* **Units**: voxel models (`*.cvr`, load_voxels, "Caviar") rendered at runtime (section 7); native
  life from Units.pcx; flags from flags.pcx.

### Message pump and modality

* `do_all_non_input` / `do_non_input` (0x5FCB20/0x5FCA30) dispatch pending window messages; the
  engine calls them inside long loops (enemy_veh pump, clock_wait).
* Modal loops: `Win_set_modal` (0x5EE190) runs the pump until `Win_release_modal`; popups,
  BaseWin, council etc. are modal. `Console_human_turn` (0x514050) is the human turn's loop.
* Timers: `Time` objects (WinMM multimedia timers) drive animation and palette cycling.

### How the engine calls the UI

The bridge (src/engine/ui_bridge.h, ~190 declarations, grouped by class) is the complete list:
map redraws (`draw_map`, `draw_tile`, `pfncRedrawMap`, `MapWin_focus`, `pfncMoveMap`), Console
(`Console_human_turn`, `Console_focus`, `Console_upgrade`), popups (`popp`, `popb`, `X_pop*`,
`BasePop_start`, `NetMsg_pop`), dialogs (`DesignWin_exec`, `SocialWin_exec`, `tech_pick`,
`NewTechWin_exec`, `diplo`, council), sound (`FX_play`, `wave_it`, `ambience`, Wave_*), movies,
boot (`system_init` parts), file dialogs, end screens. Hybrid builds call the original for all of
them; native builds bind them to src/headless (unattended observer behaviour).

## 2. Architecture of the reconstruction

```
src/engine        reconstructed game logic (unchanged by UI work)
src/ui            reconstructed UI classes, original names/addresses (ADDR_NATIVE), portable C++
src/platform      ui_platform.h: what the UI needs from the OS (no SDL types)
src/platform/sdl  SDL2 + SDL2_ttf backend (smacx only; excluded from the engine library)
src/app/smacx.cpp the native game executable
```

* **Platform layer** replaces DirectDraw/GDI/user32/WinMM: present an 8-bit frame with its palette
  (SDL texture, scaled), input as Windows messages (VK codes, client coordinates), clock, fonts
  (TTF rasterised without antialiasing, GDI metrics), audio (a mixer of sample voices and streams
  on SDL audio) and true-colour frames for movies (section 12).
* **Original classes above it**: Buffer, Palette, Font, Sprite, Texture, then Win/GraphicWin and
  widgets, then the game windows. Same algorithms as the asm (scaling tables, texture stepping,
  text layout) so screenshots match; per-function fidelity as in docs/RECONSTRUCTION.md, with
  bit-exact drawing as the goal for every function marked done.
* **Objects in the memory image**: window objects that the engine sees (MainConsole, BaseWin, ...)
  stay at their original addresses; their integer fields are used in place (`mw(win, offset)`).
  Pointer-holding parts (canvas, caches, fonts' handles are ints) live in native side structures.
* **Bridge switch (UI areas)**: the engine calls the UI through the bridge (`ADDR_NATIVE`
  declarations). Hybrid builds bind them to the original (gen_bindings.py forwarders). Native builds
  link `smacx_ui_headless`, a static library built from src/headless with one object file per UI
  area (`ui_headless_<area>.cpp`); it is the default implementation and smacx_headless uses all of
  it. smacx defines the functions of the areas its SDL UI implements in its own objects (src/ui),
  so the linker never pulls those areas' objects from the library; an area replaced only in part
  is a duplicate-symbol link error. Areas replaced by smacx today:

  | Area (src/headless) | Functions | smacx (src/ui) |
  | --- | --- | --- |
  | `mapview` | draw_map, draw_tile(s), draw_radius, draw_tile_fixup(_2), pfncRedrawMap, pfncMoveMap, MapWin_focus, MapWin_main_caption, MapWin_clear_alt/terrain, ui_mapwins_* | mapwin.cpp, win_mapview.cpp |
  | `console` | Console_human_turn, Console_focus, Console_ready_search, Console_set_move, Console_cursor_next, Console_update_data | console.cpp |
  | `art` | load_faction_art, load_flags | sprites.cpp, vehdraw.cpp |
  | `pump` | wait_task, do_all_non_input, do_non_input, flush_input | win.cpp |
  | `popup` | popp, X_pop, X_pop_2, X_pops, X_pops_11, popb, POP2, POP3, NetMsg_pop, NetMsg_close, Popup_ctor/dtor, ui_first_base_name, ui_crash_landing_*, ui_pact_ends_dialog, ui_tech_popup_*, ui_production_popup_*, ui_council_menu_*, ui_supply_menu, ui_artifact_menu, ui_disembark_menu | win_popup.cpp, popup_menus.cpp (popup.cpp, font_text.cpp) |
  | `filebox` | FileDialog_*, FileBox_*, FileList_set_path/add_ext/close, ui_filebox_destroy (the dialogs of save_game / load_game) | filebox.cpp |
  | `topmenu` | top_menu, show_credits, multiplayer_init, close_opening (the opening screens) | topmenu.cpp, gamesetup.cpp |
  | `socialwin` | SocialWin_exec, SocialWin_close | socialwin.cpp |
  | `reports` | report_score, report_intel, monument, hall_of_fame | reports.cpp, monuwin.cpp |
  | `techwin` | tech_pick (the research dialog), NewTechWin_exec | techwin.cpp |
  | `design` | DesignWin_exec | designwin.cpp |
  | `help` | help_topic, help_project (Datalink_exec) | help.cpp, datalink.cpp |
  | `diplomacy` | communicate, diplomacy_menu, proposal_menu, make_a_proposal, battle_plans, commlink_attempt, commlink_attempter, DiploPop_start/stop/draw_info, InfoWin_set_state, GraphicWin_soft_update_3, get_he_she | diplo_comm.cpp, diplowin.cpp |
  | `council` | ui_councilwin_* (the council window's operations, engine/council.h) | councilwin.cpp |
  | `messagewin` | MessageWin_clear (the info pane's message list) | infowin.cpp |
  | `battle` | SubInterface_set_iface_mode, BattleWin_clear / pulse_timer / stop_timer, StatusWin_draw / redraw / reset, tut_check2 | battlewin.cpp, maininfc.cpp |
  | `boom` | boom (the combat animation) | boom.cpp |
  | `scoot` | veh_scoot (the move animation) | scoot.cpp |
  | `move` | say_morale_2, ui_ctrl_pressed | battlewin.cpp |
  | `action` | TutWin_tut_map, ReportIf_on_redraw | tutwin.cpp |

  To replace another function: move its headless definition into an area file of its own (or
  into the area it belongs to, if smacx implements the whole area), then define it in src/ui
  under its original name. Functions only the UI calls are declared `ADDR_NATIVE` in src/ui
  headers: gen_bindings.py gives them hybrid forwarders (never used: src/ui is not part of the
  hybrid build) and, while they have no definition, aborting native stubs. A function declared
  with `ADDR` in an engine header must be defined in the engine (src/engine), never only in
  src/ui (hybrid builds would patch it with code they do not link).
  The probe and diplomacy menus (`ui_probe_*`, `ui_diplo_menu_*`) have their unattended defaults
  in engine objects (engine/ui_bridge_probe.cpp, ui_bridge_diplomacy.cpp, shared with the hybrid
  build); smacx defines every function of each of those objects (popup_menus.cpp), so the linker
  does not pull them.
* **WinMain state**: `ScreenWidth/ScreenHeight` (GetSystemMetrics) are not set natively; smacx sets
  them from the window size. Engine-visible, so harness native runs should set the oracle's value
  (1024x768) too.

## 3. Order of work

| Phase | Content | Verification |
| --- | --- | --- |
| 1 (this slice) | platform layer; Buffer/Palette/Font/Sprite/Texture subset; MapWin geometry (zoom, tile<->point, corner heights, lighting); terrain base textures, water, rocky, fungus, forest, improvements, grid; bases (base_draw), base names; placeholder units; turn stepping (AI) | oracle screenshots vs smacx for saves (section 4) |
| 2 | complete `MapWin_gen_terrain_poly`: coasts/beaches (Texture_draw_coast), rivers, roads/mag tubes, landmarks textures and sprites, territory borders, resources/pods/monolith, jungle; bit-exact Texture_draw_* stepping; `pfncPtToTile`; scrolled redraws (`pfncRedrawMap` partial copy) | pixel diff per tile class |
| 3 | units: load_vehicles, flags, Units.pcx natives, voxel renderer (cvr) or pre-rendered sprite cache; veh_draw with stacks, health, animation (vehdraw_*) | unit close-ups vs oracle |
| 4 | Win/GraphicWin/z-order/message pump/modal loops on the platform events; MainInterface (console frame), StatusWin, InfoWin, WorldWin (mini map), MessageWin; Console key handling and `Console_human_turn`: a human player can move units and end turns | scripted key sequences, state compare with oracle (same keys) |
| 5 (mostly done: src/ui/basewin*.cpp, tests/basewin/) | BaseWin (base screen): layout and art, panes, city map, clicks, build menu (the picker grid, prodwin.cpp), queue, hurry, governor, units present / supported menus, support map, specialists, rename, recycling; headless stubs in src/headless/ui_headless_basewin.cpp | `basedump` canvases vs oracle (tests/basewin/compare.sh), scripted keys (tests/basewin/run.sh) |
| 6 (done, section 6) | popups: BasePop on the game's script files (Script.txt, xscript.txt etc.: `#LABEL`, `^` lines, `$STR` parsing via the reconstructed text module), `popp/popb/X_pop*`, NetMsg, choice lists | popups by label vs oracle |
| 7 (done, section 9) | design workshop (DesignWin), tech pick (research window, Console_set_tech / set_ai), NewTechWin, datalinks (help); social engineering was done with the reports (section 8) | tests/design/run.sh, tests/datalinks/run.sh; tests/design/compare.sh vs the original |
| 8 (done, section 10; reports and monuments in section 8) | diplomacy (communicate and the human's menus, DiploPop), council window (CouncilWin); hotseat/PBEM `diplo` (DiploWin) not reconstructed | tests/diplo/run.sh |
| 9 (done, section 11) | main menu, game setup (top_menu, setup dialogs), file dialogs (FileBox), preferences (PrefWin), Resign | tests/menu/run.sh (a new game from the menu equals --new); tests/menu/compare.sh vs the original |
| 10 (done, section 12) | sound (FX, voices, music), movies (WVE: opening, faction, secret project, defeat and victory movies), the menu column's slide, monument.flc | tests/sound/run.sh; tests/sound/compare.sh vs the original (oracle `logsound`) |
| 11 (placeholder sweep, section 13) | right-click menu (PullDown), landmarks, go-to path preview and destination lines, the build picker grid, garrison menu, support map, Console_menu_proc, the status and world panes, the map cursor; the remaining work table | tests/placeholders/run.sh; tests/placeholders/compare.sh vs the original |

Each phase replaces the corresponding headless stubs in smacx; `smacx_headless` and the hybrid
builds stay unchanged.

## 4. Verification against the original

* **Oracle screenshots**: `work/ui/shot_oracle.sh <script> <outdir>` runs terranx.exe under
  Wine/Xvfb (1024x768) with oracle.dll and grabs every changed frame (`grab.py`) while the script
  runs (`load SAVE`, optional `viewer N`, `turns 1`). The first map frame after load is the
  desktop_init redraw: same save, same view centre (the map cursor), no bases/units yet
  (`ControlTurnStarted` is still 0); later frames show the turn.
* **Native screenshots**: `smacx --save SAVE [--viewer N] [--center X,Y] --hidden --shot out.bmp`
  with `SDL_VIDEODRIVER=dummy` renders offscreen; `--turns N` first plays N turns.
* **Compare**: `work/ui/compare.py` (stacked image), `work/ui/score.py` (fraction of pixels within
  a colour tolerance; the console area is excluded).
* **Caveats found in this slice**:
  * The oracle's `load` uses `load_daemon`, not `load_game`; it now also calls
    `load_faction_art(1..7)` and `load_flags` (as `load_game` does), so references made with it
    show the save's base sprites, faction colours and flags.
  * Viewer 0 (observer) shows the landmark word as "items" (visible_items[-1] is the landmarks
    field): condensers and boreholes everywhere. smacx reproduces it; use `viewer N` for
    realistic references.
  * With a real viewer the oracle stops in modal windows (monuments, new tech) after a while;
    only the first frames are usable.
* **Exact map references**: the oracle's `mapdump` command (harness/README.md, "Map view
  dumps") dumps the main view's canvas after a full or scrolled redraw for a given viewer, zoom
  and centre; `smacx --like REF --dump OUT [--scroll-from X,Y]` draws the same view and
  `work/ui/mapcmp.py` scores it. Terrain, coasts, rivers, roads, borders, bases, base names,
  landmarks, unit sprites, flags, health bars, order letters and the 3D unit models (section 7)
  match pixel for pixel.

## 5. The human turn (phase 4, first part)

smacx plays a faction as a human: `smacx --new [newgame keys] --human N` (default faction 1) or
`smacx --save FILE --human N`. The engine runs `control_game` / `control_turn` as in the original;
the human faction's turns run the reconstructed `Console_human_turn` (src/ui/console.cpp), the
computer players move with the engine.

* **Reconstructed (asm-faithful, original names and `ADDR_NATIVE` addresses)**: Console_human_turn,
  Console_veh_turn, Console_ready, Console_ready_search, Console_set_move, Console_set_view,
  Console_focus, Console_cursor_next, Console_update_data, Console_end_my_turn, Console_on_key_click
  (the main view's keys: characters, movement, Enter/ctrl+Enter, Esc, w/Tab, zoom, debug '~', digits),
  Console_veh_key (every unit order key), Console_cursor_key, Console_skip, Console_wait,
  Console_sentry, Console_hold, Console_new_base, Console_terraform, Console_edit_lock,
  Console_turn_hints, pfncProcZoomKey, MapWin_focus, not_my_turn; wait_loop (net_misc, in the
  engine). Keys reach Console_on_key_click as the original's Win_on_key / WM_CHAR handling delivers
  them (character, virtual key | 0x10000 shift | 0x20000 ctrl), so the original key bindings apply.
* **Unit orders and mouse** (src/ui/console_orders.cpp, console_mouse.cpp; asm-faithful):
  Console_go_to (go-to / road-to / tube-to / bombing / rally point / find base / psi gate lists
  with the sort buttons), go_home, automate (all modes), unload, oblit, arty, use_ability (drop
  pods, tectonic / fungal missiles), explore, convoy, set_patrol, designate, road_to, give_veh,
  destroy (pillage), disband, disband_2, activate, group, set_road_to, set_bombing_run, veh_pick,
  popv, Console_load_proc; the mouse modes (Console_cursor_on(_2), cursor_off, selecting,
  selected; CURSOR.PCX cursors drawn in software), MapWin_on_left_down / left_click /
  right_click / right_down / mouse_move, MapWin_click (select, centre, open bases, the modes'
  targets, rally-point drag), the go-to drag (go_timer, go_reset) and its keys (g, space, j).
  say_orders (statuswin.cpp). The map's right-click menu (MapWin_right_menu, mapwin_menu.cpp) on
  the reconstructed PullDown (pulldown.cpp: items, separators, disabled rows, AlphaMenu's row
  fill, keys and mouse), with its Action... / Terraform... sub-menus (the menu bar's pull-downs)
  and the landmark commands (Console_name_landmark, Console_erase_landmark; shift+N,
  ctrl+shift+N). The go-to drag's path preview (mapwin_dest.cpp: MapWin_dest_line,
  MapWin_direct_dest_line on DesktopWin's overlay, the "Move to <tile>: Dist = n" text of
  MapWin_on_mouse_move) and the map's destination lines (MapWin_draw_base_dest: display options
  rally points / patrols / go-to orders, dashed Buffer_line). Not reconstructed: MapWin_editor,
  the go-to list's map preview (Console_goto_routine's PlanWin, a second MapWin at 0x834D70).
  Sounds: section 12.
* **Main interface** (console_frame.cpp, maininfc.cpp): console2_A.pcx frame, pane backgrounds,
  menu / commlink buttons and, drawn by the original's code into the main interface's canvas: the
  status panes (StatusWin_draw_status: the tile with its terrain picture and words or resource
  numbers, coordinates; StatusWin_draw_active: the unit or base; the stack row), the world map
  (WorldWin_on_redraw: the whole map by its own MapWin at zoom -14, the bases, the view's box),
  the date box (MainInterface_set_date), "TURN COMPLETE" (MainInterface_redraw_complete), the globe
  buttons; the pane rectangles of MainInterface_init. The map cursor is MapWin_draw_radius's cursor
  texture over the cursor tile (Texture_draw_trans_3), blinking with blink_timer (the selected unit
  blinks too). Approximated: the info pane's text (InfoWin_setup_text / InfoWin_draw_text with its
  typewriter timer: the year and the allocation only). Not reconstructed: turn.flc, the info pane's
  M / I buttons, the stack row's scroll arrows, the panes' clicks (StatusWin_on_left_click /
  on_right_click), blink_timer's scrolling at the screen's edge.
* **Menus** (menu.cpp; the look approximated): the bar and pull-downs of menu.txt with the
  original's command ids and init-hidden items (Alt, Alt+letter, arrows, Enter, mouse); a command
  runs Console_menu_proc (console_menu.cpp, reconstructed: every Game / HQ / Map / Action /
  Terraform / Help / Network command, the display toggles with their check marks and keys,
  Console_next_cursor, Console_survey, Console_zoom_to_upkeep_base, Console_show_scen_objectives,
  Console_corner_the_market). The scenario editor's commands and the multiplayer ones log.
* **Save / load**: ctrl+S / ctrl+L (and the Game menu) run the engine's save_game / load_game with
  a simple file box (console_filebox.cpp): a name field, a list of saves; original naming.
* **Placeholders** (src/ui/console_placeholders.cpp): the scenario editor, time controls,
  assemble, tutorial hints log
  "not reconstructed". Multiplayer paths stop at the first network call.
* **Testing**: `--keys` scripts input (characters, `C-`/`S-`/`A-` modifiers, `ALT`, `CLICK:XxY`,
  `RCLICK:XxY`, `MOVE:XxY`, `DRAG:X1xY1:X2xY2` (a held left button: the go-to drag), KP1..KP9, named keys, and
  the commands `SHOT[:file]`, `SAVE:file` (also writes `file.rng`), `UNITS`, `QUIT`);
  `--snapshots DIR` writes the harness's turn_NNN.state/.rng/.sav before every turn_upkeep and
  `--rng FILE` restores a snapshot's generators after `--save`.
  **Determinism**: a save plus scripted keys must play the same way every run. The original is not
  reproducible here by itself: game_init seeds the CRT generator (`srand`) and the game LCG from
  timeGetTime(), and load_daemon reseeds `srand` only under GameRules 0x400, so after a load the
  engine's `rand()` calls (e.g. the human faction's turn_upkeep: fungus growth, events) depend on
  the boot time. smacx therefore pins the generators as the harness's `seed` command does for
  the oracle: `--seed CRT,GAME` defaults to 0,0 for scripted or hidden runs (--keys, --hidden; interactive play seeds from the clock) (before the load or the new game; the logic clock
  starts at 0); `--seed clock` seeds from the clock as the original. This was the flakiness of
  tests/basewin (late120 / late147: the turn's FUNGUSGROWS2 popup took the scripted keys in some
  runs, turn_300's popups came in a different order). The engine's timeGetTime() randomness reads
  use the logic clock (engine/clock.h, smacx runs the engine in deterministic mode). Scripted
  input never depends on real time: wait_task / ui_wait_key / ui_wait_input take a scripted key
  before pumping real input, and the go-to drag timer (console_ui_tick) is checked only when the
  script is empty. The remaining real-time reads are cosmetic: presentation pacing, the double
  click of real mouse input, the citizens' faces of the base window (the original's
  `Random_reseed(timeGetTime())` there is UI code, which the oracle's hook leaves on real time),
  so screenshots of those faces may differ between runs. `SMACX_TRACE=1` logs the generators and
  the logic clock at every scripted key and popup (compare the logs of two runs).
  Checks done:
  * observer mode (`--human 0`) with every smacx area replaced matches the oracle references
    (harness/compare.py): tiny20, small12, std8 (new games), late140 (load);
  * tests/console/run.sh: orders by key, go-to list and drag, menus, save / load, late saves;
  * a human game played with scripted keys for several turns, saved at the start of a human turn
    (`SAVE:`) and continued from that save with the same keys, gives identical snapshots (state
    and generators) on the following turns; the only difference is FactionTurnMight of the loaded
    turn, which the original's load path (`ControlTurnMove`: `rankings(1)` instead of
    `turn_upkeep`) writes earlier.
  * Not compared with the oracle: the oracle plays observer games only. Driving the original with
    the same keys needs an oracle command that makes a faction human, skips its crash-landing
    dialogs and posts WM_KEYDOWN/WM_CHAR messages to the game window (or calls
    Console_on_key_click) at the human turn's wait_loop; the native placeholders (popups answered
    with the first choice, base names) would then have to answer as the original's dialogs do.

## 6. Popups (phase 6)

The script-driven popups are reconstructed (src/ui/popup.{h,cpp}, font_text.{h,cpp},
win_popup.cpp, popup_menus.cpp); every engine popup call opens the original's dialog in smacx.

* **BasePop / Popup** (one native class, popup.cpp; the original's field offsets are noted):
  `BasePop_start` reads a script section (`#xs #ys #x #y #col #caption #checkbox #editbox #listbox
  #active #text #itemlist #button #extra #sprite`, `^`/`^^` lines, `$` markers through
  `parse_string`, the item lines after the blank line; flag 0x80 appends a section); the layout is
  BasePop_calculate_dimensions with Popup_calculate_dimensions' look (margins, the 300-pixel
  minimum in the main interface's frame, picture width, button bar); drawing is
  Popup_on_draw_background (box sprites of interface.pcx: frame 9, caption bars 3/6, text frame 0,
  outer frame 3), BasePop_on_redraw (picture at -5/-6 as sprite_draw_func, Buffer_write_strings),
  the item group (RadioButton / CheckBox geometry, Popup_on_dialog_item_back_draw: item boxes 27..29
  and the text.pcx colours per state, check marks of interface.pcx), EditGroup (label, edit field
  with the default text selected) and FlatButton bars (interface.pcx rows 6..8). Position: in the
  main interface's frame as Popup_show (x = (width of screen - width + 32) / 2, bottom on the
  console), else `#x/#y` (0x1000 centred, negative from the right/bottom).
* **Input** (BasePop_exec_3's modal loop on smacx's pump, `ui_wait_input`): Enter = OK
  (Win_key_click_event -> BasePop_on_button_clicked(-1)), Esc = cancel (-2: -1 with a Cancel
  button, else the selected item, as the original), arrows/keypad move the selection
  (RadioButton_on_key_down), space toggles a check box, typing edits the field; mouse: click
  selects an item / toggles a check box, double click answers, buttons on release. Return values
  are the original's: the item id, check box bits, -1 for Cancel; DialogChoices / DialogToggle and
  the edit texts in the parse slots as pops_18 / pop_ask_6 leave them.
* **Text** (font_text.cpp): the multi-font writer ({bold}, [italic], $LINK<>), line breaking
  (Font_find_line_break_l / Buffer_find_line_break_l) and the flying writer that flows the text
  lines into paragraphs (Buffer_write_strings and its measuring twin), as the gfx module does.
  Glyphs come from platform::font_render (the one place where GDI is approximated).
* **Not reconstructed**: the main interface's frame arms and the slide
  animation (slide_show), DiploPop's own popup look (the diplomacy dialog's popups are the plain popups, with the leader
  portrait: section 10),
  the help texts and tech icons of the tech popup, scroll bars of list boxes, hyperlink spots,
  `~` colour runs, sprite boxes (popups with several pictures), NetMsg's timed message window
  (multiplayer), the full-screen windows (they are not popups: sections 8 and 9).
* **Observer mode** (no human player): popups take BasePop_exec_3's no-display path, as the
  oracle forces it, so observer games play as the oracle's (`smacx_headless` is unchanged).
* **Testing**: `--keys` takes `CLICK:XxY` (a left click) and `POPUP:FILE/LABEL[/FLAGS[/PCX]]` (shows
  that popup; FILE `SCRIPT` is the main script, `tutor` shows in the tutorial style);
  `SMACX_POPUP_SHOTS=DIR` writes a screenshot of every popup when it opens. tests/popups/run.sh
  checks the answers of scripted popups (new game: base name, end of turn, quit, check boxes, edit
  field with Cancel, council menu; mouse clicks; three turns of late saves as a human faction).
  tests/popups/compare.sh drives the plain original (terranx.exe without the oracle, Wine + Xvfb,
  XTEST keys: tests/popups/orig_drive.py, orig_quickstart.steps) through QUICK START to its first
  popups and stacks them with smacx's (tests/popups/cmp.py). Differences found: the text glyphs
  (the original runs "Arial Narrow" under Wine, which substitutes another face with 20-pixel
  lines; smacx uses the game's arialn*.ttf, 19-pixel lines), hence line breaks and a one or two
  pixel shift of what follows the text (which also moves the scan-line phase of the box
  textures), and the main interface's arms beside the frame (slide_show, not reconstructed).


## 7. Unit models (voxel renderer)

The 3D unit models (chassis 0..8) are reconstructed: src/ui/voxel.{h,cpp} (the .cvr loader, model
handles, the unit scene, the vector/matrix helpers), src/ui/voxel_render.cpp (the renderer and its
span drawers), src/ui/vehdraw.cpp (load_voxels, vehdraw_draw, compute_camera, the construct_*
functions, the unit colour tables), src/ui/x87.h (floating point helpers).

* **Models**: `load_voxels` (0x451030) loads 133 .cvr files into 12-byte handles at fixed
  addresses of the original (CaviarHandle; native: `caviar_handle(addr)`); four temporary loads
  give part offsets (AVGMTGUN, adroper, AVHT-Gun/LightsReposition: frame 1 - frame 0 of object 0,
  stored at 0x7AC1B0, 0x79A618, 0x798C30, 0x789B48). The .cvr reader (0x6381F0..0x6391F0) is
  chunked; the game's files use no mesh patches (0x04050000), size chunks or segmented streams.
* **Scene** (0x8CC828): `vehdraw_construct_vehicle` and the chassis/weapon/abil/armor functions
  add model objects (frame, position, matrix, mesh, shading table: the model's own, a faction
  table at 0x79A7EC or an armour table at 0x792664). Faction and armour tables are the shading
  table of VBP.CVR recoloured (colortables_init / _faction: system.txt VEHICLECOLORING,
  ARMORCOLORING; the faction PCX pixel (0x1B3, 0x2E9)); armour 5 adds a translucent shield drawn
  through the blend tables at 0x6EEF7C (ARMORCOLORING_TRANS).
* **Camera**: the map's view tilted with the terrain (the four facet normals of the tile's corner
  heights, table 0x8CDD10 built by vehdraw_compute_facet_normals), turned to the unit's direction,
  scaled by the zoom; the shadow is the model projected along the light onto z = 0.
* **Renderer** (gfx2 0x617230, video 0x6396A0..0x6404F0): per object a level of detail, a light
  table chosen from the light direction in voxel space (24 latitudes x 89 longitudes of static
  tables at 0x699100), then the voxel stream: each 3-byte record steps to a neighbour voxel (26
  directions in 8.8 fixed point) and splats 1 or 2x2 pixels into a 256x256 canvas with a 16-bit
  depth buffer, the shadow into a second canvas. Scene_render blits the shadow (through the shadow
  table) and the model to the map; Scene_clear clears the drawn rectangle. The _SELFMOD span
  drawers patch their own addresses; they are plain loops here.
* **Floating point**: the control word is taken as 0x27F (53-bit precision, round to nearest: the
  Windows / MSVC default, which the CRT math functions check for and force); the test views do
  not distinguish it from 64-bit precision (below). With 53-bit precision every FADD/FSUB/FMUL/
  FDIV/FSQRT result is a correctly rounded double, so register values are `double` and the code
  repeats the original's operation order and its float stores (fst/fstp DWORD) exactly. FSIN,
  FCOS and FPATAN ignore the precision control: their 64-bit results (mat3_from_euler, the CRT
  atan/acos) are computed with the x87 instructions themselves on x86 hosts (inline asm,
  `long double`), and an operation on such a value is rounded once to 53 bits (`x87_mul/x87_add`
  run fmul/fadd under control word 0x27F); other hosts use libm (may differ in the last bit).
  Conversions: `__ftol` truncates (`x87_ftol`), FISTP rounds to nearest even (`x87_fistp`);
  `round_away` is the original's x*256 +- 0.5 then __ftol. `_hypot` (0x650231) is reproduced step
  by step (crt_hypot). A test build with 64-bit intermediates gave the same pixels on the test
  views.
* **Not reconstructed**: segmented voxel streams (node flag 4, part flag 1; unused by the game's
  models), Scene_render's flag 4 (bounding box centring, 0x63A6F0) and the double-size mode
  (scene flag 0x20), the turn/move animations (vehdraw_turn*, vehdraw_scoot*), the battle and
  explosion frames (XI.PCX / XF.PCX, skipped in load_vehicles). The design workshop's copter
  animation step (pfncWinIsVisible of the workshop window) is always off.
* **Verification**: work/vx (cmpall.sh, cmpdir.sh, classify.py: diff pixels inside / outside
  what the models drew). All 34 map views of work/mv and 36 new zoom 8/12/16 views of turns 100
  and 147 (several viewers) have no model pixel that differs from the oracle; the remaining
  differences are base art (Morgan bases and leader portrait overlays), terrain texels and base name
  glyphs, present before. A scenario with 21 extra prototypes (all chassis, armours 1..13
  including the armour 5 shield, abilities, reactors 1..4, natives 9 and 10) matched except one
  invalid design (missile payload 0x17 on a cruiser on one tilted tile: ~120 pixels where its
  missile model meets the hull; the span calls, light tables and depth-clear rectangles were
  checked identical to the oracle's with slotlog, cause not found).


## 8. Reports and HQ screens

The keys are the original's (Console_on_key_click): `e` social engineering, F2 labs, F3 energy,
F4 base operations, F5 secret projects, F6 satellites, F7 security nexus, F8 score, F9 monuments,
F10 hall of fame, F11 menu detail, F12 the commlink panel (MainInterface_on_iface_button_clicked
0x3EC). F1 opens the datalinks (section 9), as does the reports' DATALINKS button.

* **Frame** (src/ui/screenwin.{h,cpp}, not an original class): a modal full-screen window with its
  canvas, a copy of the main interface's canvas (the console art and pane rectangles of MainInfc),
  FlatButtons (BaseButton of basewin_internal.h; interface.pcx bars 0x77A5C8, now also extracted
  for the base window) and spots, run on ui_wait_input (keys, left / right / double clicks); a base
  window opened from a screen gets the input until it closes.
* **Social engineering** (socialwin.cpp: SocialWin_exec 0x4B2740 and the unnamed methods
  0x4AE9E0..0x4B26D0): the game state changes are the original's (model choice with social_calc /
  energy_compute, effect buttons, the allocation's +/- with locks and rebalancing, Reset, OK with
  social_upheaval: UPHEAVALTOOMUCH / UPHEAVAL, net_energy; Cancel). synch_soc / synch_alloc are in
  the engine (net.cpp). The models' effects are text instead of socwin.pcx icons.
* **Reports** (reports.cpp: report_* 0x4AC9A0.., ReportWin / ReportIf): the report kinds and keys
  of ReportWin_on_key_click; base operations with the eight sort keys (ReportIf_merge_sort), the
  three views, selection, double click (BaseWin_zoom), the governor (picture click, GOVERNOR tab:
  Set to EXPLORE.. / ADVANCED SETTINGS (BaseWin_governor's dialog, then the faction's settings for
  every selected base) / off); security nexus with the unit type filter; labs (Console_set_tech on the
  research line); energy; projects; satellites; score (compute_score, #SCORING); the intelligence
  file (report_intel). Layout at the original's rectangles; the labs tree, the energy budget's two
  columns, the base locator map, the satellite FLC and scroll bars are simplified or missing.
* **Monuments / hall of fame** (monuwin.cpp): MonuWin's slots, icons (monicons.pcx), texts
  (monument.txt with MonuWin_on_redraw's parse slots), pictures (monbigN.pcx) and the recorded
  latest monument; the background FLC is not decoded. FameWin: frame.pcx / hof.pcx, the faction's
  top five of hallfame.jhh (read and written in --outdir).
* **Commlink panel** (multiwin.cpp: MultiWin): contacts, treaties, votes, commlink_attempt, the
  right click's menu (report_intel, renounce_pact, demands_withdrawal, call_council), the Council
  button (call_council). Modal here (the original keeps it beside the map).
* **Testing**: tests/reports/run.sh (SCREENSTATE / FACTIONSTATE dumps, saves reloaded);
  tests/reports/compare.sh drives the plain original (orig_reports.steps, Difficulty=2) through
  QUICK START and the same keys and scores each screen against smacx's.


## 9. Research, new technologies, design workshop and datalinks (phase 7)

* **Research choice** (src/ui/techwin.cpp): tech_pick (0x5BEB70) runs the original's dialog
  FUN_00486510: blind research (GameRules 0x200) asks the viewer for the research priorities
  (Console_set_ai: TECHRANDOM, X_pop_6) on the original's turns, then tech_ai; otherwise the
  candidates are listed by category with the original's filters, two game_random draws per listed
  tech (display weights the original discards: they advance the game generator) and tech_val; a
  computer player takes the best, a human chooses in the window: a grid of panels (columns by the
  number of choices, panel size from the screen) with the name, category and level, the picture in
  the category colour (techs\techNNN.pcx), the TECHSHORTS quote and what the tech leads to, as
  FUN_00484C60 lays them out; keys / letters / mouse / right click (datalinks) as FUN_00486170,
  FUN_00485FC0, FUN_004860B0, FUN_00486110. Console_set_tech: TECHCHANGE, half the points lost on a
  change, synch_research. shift+R runs Console_set_ai under blind research.
  smacx_headless's tech_pick (src/headless/ui_headless_techwin.cpp, unchanged) does not make the
  game_random draws; smacx's does, as the original (they only matter for free / stolen picks of
  computer players and for the human's dialog).
* **New technology window** (techwin.cpp: NewTechWin_exec 0x483F90, FUN_00483950): the 550 x 600
  window with its title (breakthrough / acquired), the tech's name and picture, help_tech_info's
  four sections (base facilities, technologies, unit advances, miscellaneous) with their links,
  the BLURBSX quote, OK; a link opens the datalinks, then the window again. The two picture
  buttons beside the quote (interface.pcx sprites of load_box_sprites, 0x822610; a ButtonGroup
  with the quote's selected) switch the box between the BLURBSX quote and techlongs.txt's long
  description (FUN_00483EB0); the quote's frame spans the window's width under them. help_tech_info and
  tech_heck (helpx.txt TECHHECK) are in src/ui/help.cpp, with the help_* entry points. Not
  reconstructed: scroll bars. The voice clip: section 12. (tests/placeholders/run.sh case 23.)
* **Design workshop** (src/ui/designwin.cpp): DesignWin_exec and its methods with the original's
  game state changes (setup_veh, design_it, check_abil, suggest_abil, the select_* rules, the
  buttons APPLY / DONE / RENAME / OBSOLETE / UPGRADE / RETIRE / CANCEL / presets, the keys, the
  prototype list; Units[63] used as the preview's fake unit, as the original), through the
  engine (make_proto, name_proto, prune_protos, retire_proto, consider_designs, upgrade_prototype,
  synch_proto / synch_obs). The component pickers (FUN_00433F70) are grids of names and values
  (the original renders each component's model). The design is drawn with the voxel renderer
  (vehdraw_construct_chassis / weapon / armor / reactor / abil; vehdraw_construct_reactor 0x4C3800
  is now a function of its own). Reached by u (design_new_veh), the menu, tech_achieved's
  ASKSEEDESIGN and the base window's build menu (the picker's WORKSHOP button and HELP on a unit). Multiplayer paths (diplo_lock, message_data) are not reconstructed.
* **Datalinks** (src/ui/datalink.cpp): Datalink_exec with the 16 categories (lists, ids
  category * 10000 + entry, sorting), the entries from the helpx.txt / conceptsx.txt / BLURBSX /
  faction file sections the original uses, hyperlinks ($LINK<text=id>: the text writer now
  registers their rectangles, font_text.h buffer_link_at), the 20-entry history (BACK), EXIT,
  Enter / Esc, list keys; entry points: F1 (techs), the reports' DATALINKS button (per report
  kind, as ReportWin_on_button_clicked), help_topic / help_project / help_any / help_tech, links
  of NewTechWin, right clicks in the research window. Not reconstructed: the labs tree inside a
  tech entry, scroll bars.
* **Testing**: tests/design/run.sh (blind research priorities on turn_100; the research window,
  TECHCHANGE and the letter / click choices on a new game without blind research, rules=0x180e,
  since the late saves research blind; the new technology window on turn 8 of that game; the
  workshop on turn_147: a new prototype through the weapon picker and DONE, a preset with RENAME
  and APPLY, OBSOLETE, UPGRADE, saves reloaded; turn_100 / turn_300 open and cancel; the base
  window's build menu) and tests/datalinks/run.sh (browsing, links, BACK, categories, the labs
  report's DATALINKS, faction profiles); test tokens TECHSTATE and PROTOSTATE (src/app/smacx.cpp).
  tests/design/compare.sh drives the plain original through QUICK START (orig_design.steps) to the
  design workshop (u), the datalinks (F1) and the research priorities (shift+R) and scores them
  against smacx's: 0.866, 0.846 and 0.767 of the pixels within tolerance (a random faction and
  map in the original; differences: the component boxes' headers and models, the original's DATA
  column, list fonts, the labs tree of a tech entry, popup text glyphs as in section 6). The
  research window and the new technology window are not reached by the original's QUICK START
  (blind research) and are not compared.



## 10. Diplomacy and the Planetary Council (phase 8)

* **communicate** (0x54FFD0, src/ui/diplo_comm.cpp): the 34 KB dialog between the human
  (faction_id) and a computer player (faction_id_with), reconstructed from the disassembly with the
  original's control flow (labels L_<address>, the stack slots it reuses): the computer player's
  topics (withdrawal demands GETOUT / GOAWAY / INOURTERRITORY2, pleas and requests for help PLEA /
  PROPOSE / DEMANDATTACK, introductions and betrayals INTRODUCEENEMY / BETRAYFRIEND, map and
  commlink trades ENEMYMAP / METFRIEND / BOTHFRIEND / ANYNEWS, RANSOMDEMAND, tribute DEMANDBRIBE
  (WEASELOUT / HALFBRIBE), technology demands DEMANDTECH with the energy / technology counter-offer
  menu, ASKFORLOAN, social priorities, BREAKPACT, COLLABORATOR, BULLY, VENDETTA, SWEARAPACT /
  GIMMEHELP, RANSOMTREATY, truces (WANTTOTRUCE, FACTIONTRUCE of the faction's own file through
  X_pops_13, TECHTRUCE / ENERGYTRUCE, surrender TRUCEPLEASE with the diplomatic victory check),
  treaties (FACTIONTREATY / OFFERTREATY / ENERGYTREATY), greetings, atrocities, the allies' war
  reports, TEAMTECH, TERRITORY, QUISLING), then the human's DIPLO menu until the patience runs out
  (TIMESUP), and the end (agendas, patience, diplomacy_ends, RENEWTRUCE / ENDTRUCE). Every change
  goes through the engine (treaty_on/off, pact_of_brotherhood, make_treaty, pledge_truce,
  double_cross, net_energy / net_tech / net_maps / net_loan / net_cede_base, ...). The
  multiplayer paths are the engine's net_* functions (the plain change in single player).
  Original bugs kept (marked BUG(original)): INTRODUCEENEMY's alien text chosen by
  DiploHelpFaction; PROPOSEATTACK's check of diplo_status[8]; REPORTWINNING's region test.
* **The human's menus** (src/ui/diplowin.cpp): diplomacy_menu (DIPLO / DIPLOMENU), proposal_menu
  (PROPOSAL, PROPOSEATTACK / PROPOSECOMMLINK / PROPOSEBASE, COUNTER, COUNTERBASE), make_a_proposal
  (propose_pact / propose_treaty / threaten / propose_attack / base_swap / tech_trade /
  energy_trade / make_gift, TRADEMAPS, the loan repaid, COUNCILMATTER / COUNCILVOTE /
  COUNCILVOTEGOV with buy_council_vote), battle_plans (BATTLEPLANS / BATTLEPLAN, the target's
  acceptance: base_id_attack_target, the old target dropped), commlink_attempt (F12's commlink
  panel; SUNSPOTSNO, ALIENHATRED2, the psych technologies, PBEM `diplo`, communicate, then the
  commlink and contact bits) and commlink_attempter (single player). The menus are popups of the
  script sections (ui_diplo_menu_*).
* **DiploPop** (diplowin.cpp): while the commlink is open the main interface's left pane shows the
  bioscan animation (dip_<a|f|m>_<mood>.flc at 0x1A, 0x5A) and, in DiploPop_init's rectangle, the
  leader's animation (<faction>.flc) under the leader's name, REPUTATION and MOOD
  (DiploPop_draw_portrait). The FLC files are decoded natively (colour, BRUN, COPY, BLACK, SS2,
  LC chunks), their colours mapped to the game palette. They animate in interactive play only
  (scripted and hidden runs show the first frames). DiploPop_start runs diplomacy_check as the
  original. The popups' leader picture is now the faction art's sprite 28 (FactionPortraits,
  load_faction_art), so every popup that passes FactionPortraits shows it.
* **The council window** (src/ui/councilwin.cpp, the engine's council.cpp for the session): the
  window (0x32C wide over the main interface, as the original's layout), the seven faction columns
  (portrait, leader, council votes, GOVERNOR, the vote cast), the title and the result (MEASURE
  PASSES / FAILS, EXECUTIVE VETO, the elected governor), the buttons on the main interface: YEA /
  NAY (VETO for the governor) / ABSTAIN for proposals, VOTE (GOVVOTE: abstain or a candidate) for
  elections, OK once decided (FUN_00424870's ids 1-5); Enter / Esc go to CouncilWin_key. A viewer
  that is not human (observer mode) gets the unattended behaviour of the headless window.
* **Not reconstructed (placeholders)**: hotseat/PBEM `diplo` (0x444D90 -> FUN_00443F10 and the
  DiploWin message window, ~20 KB; still headless), DiploPop_draw_info / draw_hq (the info and HQ
  panes stay empty), DiploPop's popup style (PopupDialogState 6 while the interface mode is on),
  the PlanWin map previews of the base swap and battle plan menus (battle_routine), the commlink
  debug dialog, multiplayer channels and the council's multiplayer timer. The council window's
  panel art is approximated (box sprites, faction art sprite 26).
* **Testing**: tests/diplo/run.sh (DIPLOSTATE and the council window's councilstate): turn_147 as
  faction 3, the Hive's call (a pact and a vendetta accepted); the human's call to the Spartans
  with a gift of all technologies and a gift of 75 credits; turn_300 as faction 6, a council
  election with the human's vote (governor 3 -> 5); every change saved and loaded again. On
  turn_147 (faction 3) and turn_300 (faction 6) a computer player now calls at the start of the
  turn, as in the original (the headless communicate did nothing), so the other tests answer its
  COMM popup "not now" first (DOWN,ENTER).
* **Not compared with the original**: QUICK START has no contacts, so the original's commlink and
  council cannot be reached there. The late saves are observer games and have no human player.


## 11. The opening screens, file dialogs and preferences (phase 9)

`smacx` without `--new` / `--save` runs the original's main menu: control_game calls top_menu as the
original does (smacx's main_menu only adds the seeding and the human player for its harness parts).

* **Desktop and menu column** (src/ui/topmenu.cpp): the desktop (TopMenuWin, change_opening /
  close_opening) shows xopeninga.pcx (xopeningb.pcx at 800 x 600) through the picture's own palette.
  Under GDI each of the original's windows has its own DIB colour table, so the screen now has
  palette layers (win.h ui_set_layer_palette / ui_layer_mark, platform set_layer_palettes): the
  desktop and the menu column are layer 1, the credits layer 2, popups and the game's windows the
  game palette. The menu column (SetupWin_do_menu_rightside / SetupWin_draw_item and its key and
  mouse handlers) stacks the frames of openitem.flc (frame 5 at rest, indices of the desktop's
  colour table, 9 transparent, 0 the redtint table's colour of the desktop pixel under it: Win +0xA0,
  RedTintTable now built by colortables_init) at the bottom right, the caption first and 20 pixels
  apart, texts in Arial 18 bold upper case cut at "(", colours 0xCD / 0xCA / 0x42. The custom
  planet's items put their pictures (S?L?C?.pcx, MOON1..3.PCX) on the desktop when highlighted.
  FLC decoding moved to src/ui/flic.{h,cpp} (diplowin.cpp uses it), with raw indices on request.
* **top_menu** (0x58E360): TOPMENU (START GAME, QUICK START, SCENARIO, LOAD GAME, MULTIPLAYER,
  VIEW CREDITS, EXIT GAME; DefaultPrefs.top_menu saved), SCENARIOMENU (PLAY SCENARIO: a scenario
  file, config_game(2), the INTRO; CREATE SCENARIO: size, config_game(3), setup_game(1) and the
  editor state 0x80000E0; LOAD MAP FILE; EDIT SCENARIO), LOAD GAME (load_game with the FileBox),
  MULTIPLAYER (multiplayer_init: a placeholder message, no network play), VIEW CREDITS
  (show_credits: credits2.pcx and the CREDITS section scrolling in a 400 x 400 window, closed by a
  key or a click). There is no SMAC / SMACX choice in terranx.exe: the launcher picks terran.exe or
  terranx.exe (ExpansionEnabled is set by WinMain).
* **The game setup**: map_menu (MAPMENU; QUICK START reuses the last choices; the random map's two
  game_rand draws for ocean and erosion), size_of_planet (WORLDSIZE of alphax.txt, Custom Size with
  CUSTOMMAP and VERYLARGEMAP; map_init, Path_init), custom_planet (the custom map asks ocean, erosion,
  native life and clouds in this order; the orbit is left 0, as SMACX does), config_game
  (src/ui/gamesetup.cpp, reconstructed from the disassembly with the original's jumps: DIFFICULTY,
  USERULES / RULES with the DialogChoices bit order, the faction picker, NAME / NAME2 / NAME3, the
  CUSTOMNAMES dialog (CHANGE NAME, or the name popup's button), the random rivals of JENN282 slots
  with the alien pair rule and its draws, load_flags, the alien player's rules, the human player,
  world_build, setup_game(0), alien_start, planetfall). clear_all_player_messages is done in
  top_menu (the combat counts and the players' message lists).
* **PickWin** (0x4887A0..): the 790 x 300 window of PickWin_init's rectangles: title, the three
  pickfac.pcx buttons (the original factions, the new factions, all random; one column of
  pickfac.pcx per button, its rows the states), PLAY SELECTED / PLAY RANDOM FACTION, the seven
  slots (formal name | (description), RANDOM) with their arrow menus (every faction's noun and
  RANDOM: read_faction2), the line to the selected slot, INFO / QUOTE, CHANGE NAME, OK, CANCEL;
  under it thumb_routine's text box (the faction's BLURB or DATALINKS1/2 with the leader's portrait,
  700 pixels wide while ThumbRoutineActive is set). The picker stays up under the name popup, as the
  original's.
* **FileBox** (src/ui/filebox.cpp, replacing the simple box of console_filebox.cpp): FileBox_init's
  layout at the menu and in a game, the list of FileList (directories, "..", the files of the
  dialog's extensions, jackal.pcx icons, rows of 24 pixels, box 29 for the selection), the name
  field, OK / CANCEL, FILEWIN_LOAD / FILEWIN_SAVEEXISTS / FILEWIN_ILLEGALCHAR of jackal.txt.
  Directories are browsed relative to the game directory (the original changes the process's
  current directory and answers absolute paths); a typed absolute directory is entered.
* **Preferences** (src/ui/prefwin.cpp): PrefWin with its six pages (BASICPREFS, ADVANCEDPREFS,
  BASEWARNINGS, AUTOMATIONPREFS, AUDIOVISUALPREFS, MAPPREFS; PrefWin_init's check box ids,
  PrefWin_get / set_preferences' bits), Restore Defaults, OK (PrefWin_destroy: AlphaIniPrefs,
  prefs_use, prefs_save(0) into Alpha Centauri.Ini) and Cancel; ctrl+P / O / W / A / I / M open their
  pages. Resign (shift+Q, Console_retire_game: REALLYRETIRE, report_score, quayle, hall_of_fame,
  show_replay, back to the main menu) and the editor's custom rules (shift+F10,
  Console_editor_rules) are reconstructed; Start New Game (ctrl+shift+Q) returns to the main menu.
* **Not reconstructed (placeholders)**: multiplayer_init (DirectPlay / hotseat / PBEM setup, time
  controls, HOTSEATADD), the PBEM tamper seeds, the gamma correction dialog, the Windows common file
  dialog (WindowsFileBox), the scenario editor itself. The menu column's slide, its sound, the menu
  music, the opening movie and the audio page's volume buttons and sliders: section 12.
* **Testing**: tests/menu/run.sh: the main menu and its sub-menus (MENUSTATE, SCREENSTATE); a new
  game set up through every dialog compared with the --new game (`size=0 ocean=1 erosion=1 orbit=0
  clouds=1 natives=1 diff=3`, the Crossfire factions, human 3): turn_000 equal by
  harness/compare.py; custom rules (RULES, diff 1) against `--new rules=0x120E`; a save loaded through
  the FileBox; a preference flipped and written to the ini. tests/menu/compare.sh drives the plain
  original through the same screens (orig_menu.steps) and scores them (fraction of pixels within
  tolerance): the main menu, its highlighted second item, the scenario menu, the map menu, the
  planet's size, ocean, erosion, native life, clouds, the difficulty and the rules 0.999 to 1.000
  (pixel exact but for a few text pixels); the load FileBox 0.983; the faction picker 0.909 (list
  and portrait text glyphs: the original runs Arial Narrow under Wine's substitute, see section 6);
  the name popup over the picker 0.937; PLANETFALL 0.738 (the same text, one line less with the
  game's arialn.ttf, so the box is lower); the preferences 0.869 (and its check boxes reflect
  different preferences); the save FileBox in a game 0.847 (the original's base window behind it).
  The credits (animated) and the multiplayer setup (a placeholder) are not scored.

## 12. Sound, music and movies (phase 10)

The original plays its sounds through soundx.dll (DirectSound, and the ACM MP3 codec for the
voices) and its movies through EA's player library (video module). smacx replaces both: the platform
layer has an audio mixer (src/platform/sdl/audio_sdl.cpp: sample voices with gain, pan, pitch, loop
and fade-out, and generator streams, on one SDL audio device; `--nosound` opens none,
SDL_AUDIODRIVER chooses it, the tests use `dummy`) and true-colour frames
(`video_present_rgb`). Nothing in the sound layer touches game state: its objects live in native
side structures keyed by the original's addresses (the image's FX object 0x749CF8..0x74C5F0 and the
other Wave objects are never written; the fcompare ignore list names them), it reads the real clock,
never the logic clock or a game generator, and a call's choices that depend on a sound playing
(wave_it, ambience_veh) only decide what is heard. The headless areas `sound` (FX, voices, Wave_*,
ambience, release_sound, desktop_init), `movie` (amovie_project*), `boom` and `scoot` (veh_scoot)
are replaced by smacx (src/ui/sound.cpp, music.cpp, movie.cpp).

* **Sound effects** (src/ui/sound.cpp, from the disassembly): FX_init's table (the file, flags and
  sound group of each slot; slot = offset / 0x6C: 0 psi attack, 1 energy1, 2/3 projectiles, 4 laser
  (pitch 1100), 56..96 mind worms, planet buster, CPU turn complete / new data, psi blow, boom, the
  units' engines by chassis and the weapons; the slots FX_init never loads, e.g. 6, 0x0F, 0x16, are
  silent in the original too), the 45 CPU voices (0x74C5F0, loaded at each play), ok.wav,
  amenu2.wav, scoot.wav, passover.wav, menu up / down.wav with their volumes and pitches. FX_play /
  FX_play_loc (preference GamePreferences 0x400; slots 0..2 duck the music, the psi attack's pitch
  rises by 100 cents while it plays; FX_play_loc ignores the location, as the original), FX_stop,
  FX_fade, FX_engine_pitch, wave_it (one voice at a time: not while FacilityVoice or the last
  voice plays), the popups' `#wave N` / `#vol V` (popup_wave_callback: 0x19 becomes 0x25 for a
  secret project, 0x2B plays one time in three, 0x10 adds FX 0x38), ambience (ids < 200: the one
  current ambient effect, faded when replaced; 200..0xD5: the music's events; preference 0x800),
  ambience_veh (the selected unit's sound by plan, chassis, triad, weapon and native unit, alien
  factions' variants), boom's sounds (the missile's launch, the explosion once per map window
  showing the tile, then the shooter's weapon or the psi attack; the animation is not drawn),
  veh_scoot's scoot.wav, the base window's fx\menu2.wav / ambience 0xCD and 0xCE / menu down.wav,
  the design workshop's menu up.wav and ambience 0xD3 / 0xD4, ok_callback / passover_callback /
  input_callback (every key and click goes to the music), release_sound.
* **Groups and preferences**: sound group 0 (voices) and 1 (effects) with their volumes and
  switches, the master volume. The audio page of PrefWin (prefwin.cpp) has the original's Main /
  Voice / SFX / Music On|Off buttons (ids 0x55..0x58, labels 1080..1087,
  PrefWin_change_volume_button_state: the master off clears 0x400 / 0x800 / More 0x4000 at once,
  Music on loads the music) and the four 0..127 sliders (PrefWin_on_scrolling: master, group 0,
  group 1, music; applied at once), Main / Voice / SFX / Music Volume written by PrefWin_destroy;
  desktop_init applies the saved volumes (PrefWin_init's sliders).
* **Voices** (mp3.cpp, mp3_tables.h): a minimal MPEG audio layer III decoder (MPEG-1/2/2.5, mono,
  stereo and mid/side; no intensity stereo, which the game's files do not use). Its Huffman,
  scale factor band and window tables were taken once from the system's libmp3lame / libmpg123
  (standard data; the build has no dependency); its output equals Wine's mpg123 (ACM) within
  +-2 LSB on the voice files. NewTechWin plays voices\techN.mp3 at the voices group's volume
  (More 0x4000, faded after the window with More 0x20000), config_game the faction's
  voices\NAME.mp3 while its leader is named, base_production (engine) voices\facN.mp3. The
  original's datalinks play no voice (VOICEOVERDATA is a text popup).
* **Music** (music.cpp): load_music's choice of the set and class by the faction's search key
  (gaians, peace: gset1; hive, morgan: mset1; univ: uset2; spartans: sset1; believers: bset1 (the
  Believers' key is "believers"); others: aset1), and the six music classes' methods (vtable
  +0x12C..+0x164, 0x447340..0x448280) with their statics: which groups play on which event and
  input, with which pitch, the default set's three phases (counted in user inputs). The groups
  are played by soundx.dll's ambient engine, which is not reconstructed: music.cpp approximates it
  with the .amb files' data (prgm programs, kmap key zones of fx\*.wav samples with their smpl
  unity notes and sustain loops, glbl groups: type 4 loops a random track of its range, type 1
  plays one and then its next group; the MIDI tracks' notes, programs, volume and pan) mixed as a
  stream. The opening menu's music is fx\opening menu.wav (looped, faded on leaving).
* **Movies** (wve.cpp, movie.cpp): the GOG data's movies are EA WVE files (SCHl / SCCl / pIQT /
  SCDl / SCEl chunks): TQI video (MPEG-1 intra macroblocks, AAN-scaled dequantisation, EA's IDCT,
  15 frames per second; tables of ISO 11172-2 taken from libavcodec once) and EA ADPCM stereo sound
  at 22050 Hz, decoded natively. amovie_project (movlist.txt's #PROJECTS) and amovie_project_name
  (movies\NAME.wve; the CD and .avi fallbacks find nothing in the GOG data) play them full window
  with their sound while the other sounds are off; Enter, Esc, space or a click end them. The
  engine calls them for the opening (openingx), the factions' planetfall movies, secret projects,
  theyTranscend, defeat (losealien / loseman / losewoman) and victory (close_ct / close_us).
  Unattended runs decode only SMACX_MOVIE_FRAMES frames (default none).
* **Animations**: the menu column slides in and out (frames 0-5 / 6-11 of openitem.flc, 10 ms
  each, not in unattended runs); the monuments' background is monument.flc's frame of the
  number of monuments (MonuWin_init).
* **Not reconstructed (placeholders)**: soundx.dll's ambient engine (approximated as above), its
  DirectSound volume curve (linear here) and 3D positioning; AlphaMenu_show / hide's amenu2.wav (the
  menu bar does not slide), the design workshop's button sounds (FX 0x2B etc., silent slots), the
  combat animation itself, Popup_show's slide sound, the interlude windows (text, no movie in the
  original), Console_show_movies (its SHOWMOVIES section is missing from the GOG scripts), the
  8BitMovieSound option.
* **Testing**: `SMACX_SOUNDLOG=1` logs every call ("sound: T<turn> FX_play 15") before the
  preferences or the device are looked at, so logs are the same with or without a device. The
  oracle's `logsound` command (harness/oracle/oracle.cpp) hooks the same functions in terranx.exe
  (thunks keeping ECX for the thiscall ones) and logs "SOUND T<turn> name args". tests/sound/run.sh:
  the 20-turn tiny20 observer game makes the original's 257 calls in order (orig_tiny20.log; the
  original repeats an explosion for a second map window showing the tile, which `soundcmp.py
  --one-window` counts once; snapshots must be written as the oracle does, as save_game reads the
  logic clock); scripted human actions make the triggers the original's code makes (startup, unit
  selection, base window, main menu); the audio page; an MP3 voice decoded and the mixer's output
  not silent (SDL disk driver); 30 s of music rendered (MUSICRENDER); a movie's frames decoded
  (MOVIE, SMACX_MOVIE_FRAMES; frame 100 a picture). tests/sound/compare.sh runs the oracle again
  and compares (--update refreshes the reference). Human-input sounds cannot be logged in the
  original under the oracle (it plays observer games only); their triggers come from the
  disassembly.

## 13. Remaining work

**Deliberate differences from the original.** A player's first start (no `--outdir`) seeds the
per-user `Alpha Centauri.Ini` with "Sliding windows" (PREF_AV_SLIDING_WINDOWS, 0x8000000) off:
gamedata.cpp `seed_ini` writes the original's default preferences (0xBBE1DD96 and the tutorial bit
0x20) without it, under "Prefs Format=12" (tests/placeholders case 26). Preferences turn it back on;
an existing ini keeps its setting; the tests and the harness (`--outdir`, the harness ini) keep the
original's default.

The placeholder sweep (phase 11): the placeholders, "not reconstructed" parts and approximations of
the native UI, with their state after the sweep. Size: S (under ~1 KB of the original's code, an
hour), M (a few KB, a day), L (more, several days). Checks of the items done:
tests/placeholders/run.sh (and the suites named), side by side with the original:
tests/placeholders/compare.sh (orig_rmenu.steps; orig_basewin.steps by eye).

| Item | Original functions | State | Size |
|---|---|---|---|
| Map right-click menu | MapWin_right_menu, PullDown (add_item .. show, on_redraw), AlphaMenu fill functions | done (mapwin_menu.cpp, pulldown.cpp) | — |
| Landmarks | Console_name_landmark, Console_erase_landmark (shift+N, ctrl+shift+N) | done | — |
| Go-to drag path and distance | MapWin_dest_line, MapWin_direct_dest_line, DesktopWin overlay, MapWin_on_mouse_move's text | done (mapwin_dest.cpp) | — |
| Destination lines on the map | MapWin_draw_base_dest, Buffer_line (dashed) | done | — |
| Go-to list's map preview | Console_goto_routine's PlanWin (a second MapWin, FUN_0048AF30.., PlanWin_blink) | done (planwin.cpp): PlanWin in the world map pane while a go-to / find-base list is open (FUN_0048BC60 / BC50), its own MapWin and background (FUN_0048B3F0, options 0x84010002), the zooms around a tile or a route (FUN_0048B8E0, FUN_0048B630), the lines (FUN_0048AE40 / AE90), the redraw (FUN_0048AF30: bases, stand-in units at the ends, dashed lines in the faction's colour), PlanWin_blink and line_timer's moving dashes while the list is open; the list's selection callback (Popup +0x3280: Console_goto_routine, battle_routine); battle_plans' preview (FUN_0048B450: centred between the targets and their attacking bases) (tests/console case 2); the base trade menus' previews (proposal_menu's PROPOSEBASE / COUNTERBASE: PlanWin zoomed on the first base with its point route, the list's selection callback the original's battle_routine (FUN_0054DC90, a base preview, is unused); PlanWin drawn over DiploPop's HQ pane; tests/diplo case 7). battle_plans' preview is not exercised by the test saves (no pact) | — |
| Build menu picture grid | FUN_00492FA0, FUN_00494700, FUN_004927C0, FUN_00493EA0, FUN_00494280, FUN_004945D0; BaseWin_production's modes | done (prodwin.cpp) | — |
| Units present / supported menu | BaseWin_garrison_click | done | — |
| Support map pane | BaseWin_draw_support | done | — |
| Governor advanced settings (ops report) | ReportIf_on_iface_button_clicked 0xCF4, BaseWin_governor | done | — |
| City map cursor texture | pfncDrawCityMap's Texture_draw_trans_3 | done | — |
| Menu commands | Console_menu_proc, Console_next_cursor, the display toggles and their keys | done (console_menu.cpp) | — |
| Menu items without a function | Console_survey, Console_zoom_to_upkeep_base, Console_show_scen_objectives, Console_corner_the_market | done | — |
| Menu bar look and item visibility | Menu_on_redraw, PullDown_pulldown_update, pfncMainMenuUpdateVisible | done: item visibility (menu.cpp: pfncMainMenuUpdateVisible's shown / hidden / disabled items, the terraforming orders renamed with their turns); the look reconstructed from the asm, not compared (no screenshot of the original's menus could be taken: its XTEST Alt does not open them): the main view's menus are AlphaMenu's (the Menu subclass overriding Menu_on_redraw: AlphaMenu_init / compute / on_redraw / adjust), a column of menu2.pcx buttons (menu.pcx at 800) above the console, nine slots in MainMenu_menu_init's order, the shown menus from the top and HELP from the bottom, titles in capitals in text.pcx's colours, the open button's and the hovered button's states; the open menu's pull-down is the reconstructed PullDown (PullDown_on_redraw with AlphaMenu_fill_func / backfill_func) at the column's right (tests/placeholders case 20). AlphaMenu_show: fx\\amenu2.wav and the column's slide in (six frames of its picture rising in its rectangle from its bottom less the top piece by a fifth of its height; skipped in the unattended runs, not compared). Approximated: the pull-down's position (AlphaMenu_on_adjust_pulldown_pos read as the column's right, level with the button) | — |
| Status panes | StatusWin_draw_status, StatusWin_draw_active, say_morale, StatusWin_set_loc / redraw / reset | done (maininfc.cpp) | — |
| World map pane | WorldWin_on_redraw, MapWin_gen_bases_only, WorldWin_draw_box, WorldWin_tile_to_pixel | done (nearest-pixel stretch for maps larger than the pane) | — |
| Date box, TURN COMPLETE, globe buttons | MainInterface_set_date, MainInterface_redraw_complete, MainInterface_init's geometry | done (turn.flc not decoded) | — |
| Map cursor and blinking | MapWin_draw_radius's cursor texture, Texture_draw_trans_3, blink_timer | done (no edge scrolling) | — |
| Info pane | InfoWin_setup_text, InfoWin_draw_text, InfoWin timer / set_state, the M / I / X buttons, InfoWin_right_menu, MessageWin (the M mode's list), MainInterface_set_message | done (infowin.cpp; the message list's ListBox look approximated; control_turn's faction boxes in the pane during the computer players' turns not drawn) | — |
| Clicks on the interface panes | StatusWin_on_left_click / on_right_click, StatusWin_arrow_left / right, WorldWin_click / right_menu, the stack scroll arrows | done (maininfc.cpp; the world map window of a shift click / the menu's item 4 not reconstructed: the view is centred) | — |
| Labs report and the Datalinks tech tree | ReportWin_draw_labs (9.9 KB), ReportWin_labs_line, ReportWin_on_button_passover, ReportWin_on_button_clicked 0x379..0x37E, ReportIf_draw_labs (5.2 KB), labs_write | done (labs.cpp: one function for both, the report's research box, progress bar and breakthrough lines, the tree's buttons, lines, arrows and centre box with what the technology allows (links in the Datalinks), the highlight under the mouse; reports.cpp: the TECHNOLOGY / BASES list, the summary, the progress per category. Approximated: the list box's look, the faction logo of the centre pane) | — |
| Energy report's two columns | ReportWin_draw_energy, ReportWin_on_selected, ReportIf_draw_energy, ReportIf_on_iface_selected | done (reports.cpp: the summary with sanctions, FACTENERGY and the loans, the right column's budget of a base or of a facility in every base (a click switches), BASE EXPENDITURES and MAINTENANCE COSTS. Approximated: the subject is reset when the report opens or switches, not at every full redraw) | — |
| Base locator map, report pictures, satellite FLC, hover help | ReportWin_draw_ops / sat, ReportIf_* | done (reports.cpp): the base locator (WorldWin_show_all: the world map pane in base operations and secret projects; ReportIf_bl_anim's dot0..3.pcx animation, 150 ms steps, after a single base is selected or a project clicked), the production pictures of base operations (facility / project pictures in their mandate's colour, the units' models), the satellites report's two views (0xD3C / 0xD3D) with ReportWin_draw_sat's animation (satellite.flc, sixteen spring orbits, ReportWin_sat_anim every 4000 ms), the report buttons' help bubbles (a right click: BaseButton_set_bubble_text; generic for the screens' buttons) (tests/reports cases 5c, 5d); the citizens view's pictures (ReportWin_draw_ops 0x8B4: the small faces and specialists right to left with its step, the UI generator's looks; compared with the original: the same), the satellites' table (ReportWin_draw_sat: every faction, the five columns' colours, the ODPs left / all; compared: the same) and the main interface's panes (ReportIf_draw_sat: SAT0 / SAT1; the ORBITAL ATTACK VIEW's target and kind buttons), the orbital attack (ATTACK 0xD3E: sat_attack's checks and popups, NOKILLERS / KILLERSDEPLOYED / WHICHGSP, break_treaty; ReportWin_start_attack: a satellite fewer and XI.PCX's explosion frames every 50 ms, then action_sat_attack), sat_edit (a right click on a cell in the editor's omniscient view) (tests/reports case 5e) | — |
| Scroll bars | Scroll (ctor, init, on_redraw, compute_thumb_rect, the buttons and the track), scroll_normal / scroll_small, load_icons' ICONS.PCX sprites | done (scroll.cpp: vertical bars with their buttons and thumb; the reports' list boxes, the Datalinks list, the build picker, the base window's facilities and units supported panes, the list popups, the file box; a button moves a line, the track a page. Approximated: colours 7 / 247 / 248 / 255 drawn black as the original shows them (smacx's palette has Windows static colours there), the buttons' pressed state, dragging the thumb, the popups' bar height) | — |
| Combat animation | boom, BattleWin_* (battle report in the status pane), XI.PCX / XF.PCX frames | done (boom.cpp: shots, explosions, the planet buster; battlewin.cpp: the report, interface mode 10, its 5 s timer. Approximated: a destroyed unit's tile drawn again by a full redraw, the buster's scaling; native life's pictures in the report not drawn) | — |
| Move / turn animations | vehdraw_turn*, veh_scoot (sliding) | done (scoot.cpp, vehdraw.cpp. Approximated: the saved background (MapWin_compute_clip), the slide's terrain tilt through the tiles' mean) | — |
| Interface arms, slide sounds | slide_show, slide_hide, scroll_show / scroll_hide | slide_show / slide_hide done for the base window, the console's slide (battle), the screens (screenwin.cpp's `slide`: the datalinks, reports, design workshop, social engineering, preferences) and the council window (FUN_00426FA0); scroll_show / scroll_hide done (slide.cpp: MainInterface_init's rail art of console_x2_A.pcx, the outer right arm rising beside the window and the rail sliding in, 10 ms frames, the rail and the arm then in the window's canvas; scroll_hide the reverse): the list popups (Popup_on_listbox_scroll_create: the scroll bar on the rail with its height formula) and the window's scroll bar of base operations, secret projects and the security nexus (ReportIf_report's Scroll 0x882584, its clicks; tests/placeholders case 19). DiploPop's window and its slide (diplowin.cpp: DiploPop_init's 700 x 0x1C2 window above the console with box 9, slide_show flag 2 with the console's lid when the dialog starts, slide_hide at its end; the dialog's popups, flag 0x100000, drawn into it: BasePop_set_def_external_window, Popup_calculate_dimensions' / Popup_on_draw_background's DiploPop branches in popup.cpp: the window's origin and size, DiploPop's text colours, the frame, caption and text boxes, the buttons 0x1E above its bottom; tests/diplo case 6, compared with the original's diplomacy dialog: the same frame, caption, picture, text and OK button). Approximated: the slides' frames not compared (the interface animations are off in the unattended runs); the external popups' item group is not given the window's height (the original's 450 rows fall under the buttons, unseen) | — |
| Tutorial windows and hints | TutWin_tour, TutWin_tour1 / 5 / 6 / 7, TutWin_tut_win, Console_give_hints, TutWin_draw_arrow, the SOCWIN / DESWIN hints | done (tutwin.cpp: every unit and base window hint, the TUTOR windows with their pointer lines and buttons, the first turn's tour (kind 0), the terraforming lessons (kind 3: the tiles scored as the original, TOURRAIN .. TOURFORMDONE), the base window's (1), social engineering's (5), drone riots' (6) and design workshop's (7) tours with their windows opened or shown for them (the Help menu's tours). Approximated: the pictures are drawn into a picture, not the sprite box callbacks) | — |
| Time controls | Console_time_controls, Console_use_time_bonus (multiplayer), time_controls_dialog | done for single player (Console_time_controls does nothing outside multiplayer); the dialog, HOSTTIME and the time bonus are multiplayer's (out of scope) | — |
| Assemble | Console_assemble | done (console_orders.cpp) | — |
| Gamma dialog | FUN_005C8E60 / FUN_005C9520 / FUN_005C95E0 (PrefWin button 6) | done (prefwin.cpp GammaWin: the ramp in its own palette layer, the value, the scroll bar, OK / CANCEL, the preference and the palette reloaded; tests/menu case 6. Approximated: the scroll bar's thumb is not dragged) | — |
| Custom faction | Console_custom_my_faction | done (console_placeholders.cpp: EDITORWHO in the editor, CUSTOMNAMES, the names and gender kept; the multiplayer synchronisation out of scope) | — |
| Scenario / map editor | MapWin_editor, Console_editor_* (Scenario and Edit Map menus, keys), BaseWin_base_editor, Console_editor_edit_veh / kill_veh | done (from the asm): editor.cpp: Console_editor_on (ctrl+K, SCENEDITOR; the pane's button and mode), MapWin_editor (every paint: elevation, rockiness, river sources, special resources, unity pods, cover / uncover, the terrain enhancements of 0x691888, the minor landmarks; the brushes and the one-tile brush's search outwards; the drag; the redraws), Console_editor_paints / set_brush / minor / terrain, the map commands (flat, polar caps, climate, rockiness, fungus, randomize, clear random, clear, clear units, clear enhancements, fast and slow generation, protection lock, save / load map, undo / redo), Console_editor_natural (NATPLACE: world_crater .. world_fossil at the cursor; NATUNPLACE: the landmark's tiles around the cursor, the fresh water sea's region), energy, date, Console_editor_veh (EDITORUNIT: All, Player, Morale), Console_editor_kill_veh, kill the stack, Console_editor_edit_veh (EDITVEH's flags, MORALE with native life's lifecycle, DATA: EDITVEH2 / EDITVEH2A, PLAYER, HOME), Console_editor_who, _tech (EDITORTECH sorted, the Player button), _view (EDITORWHO with ALL: EDITORCONTROLALL / EDITORCONTROL), _diff, _eliminate, _reload (RELOADFAC, read_faction2), _reset_faction / _reset_tech, _personality / _strategy (AIEDITOR / AIEDITOR2), _scen_rules / _scen_param / _scen_victory (SCENRULES, SCENPARAM, SCENVICTORY, FACOBJ), ctrl+shift+F2 (every technology), Console_show_movies (SHOWMOVIES), the omniscient view's windows (the editor's y key / 0x601: EditorPanelWin, the info pane's 0x3EA button, shown or hidden; the world map, info and status panes and the caption redrawn), the menus' commands and the editor's keys; editpane.cpp: the editor's interface pane (MainInterface_init's 25 paint and 6 brush sprite buttons of interface.pcx in their ButtonGroups with their bubbles, MainInterface_on_redraw's boxes, MainInterface_draw_edit_pane's text: map file, paint, brush, size, rainfall, the lock; MainInterface_on_iface_group_clicked); editor_diplo.cpp: Console_editor_diplomacy's window (FUN_004DB080: the faction list, the 13 treaty / agenda check boxes applied by FUN_004DA990, Atrocity / Integrity / Clear (the friction redrawn) / OK); editor_base.cpp: BaseWin_base_editor's menu ('e', button 0x15) with _fac, _proj, _copy, the population, the facilities, the objective flag, minerals, nutrients, owner, former owner, delete (tests/placeholders cases 21, 22). popup.cpp with them: the dialogs' 12 rows (Dialog_close's DAT_006970A4; list boxes scroll, longer radio and check box lists take columns) and BasePop_start's edit fields' defaults ($STRn, the edit default for the first). Compared with the original (tests/placeholders/orig_editor.steps through tests/popups/orig_drive.py): the interface pane and the diplomacy window match but for the map and the factions. Approximated: the diplomacy window's list box and check boxes are drawn by editor_diplo.cpp after the original's screenshot (ListBox / CheckBox rows, fonts, the selection's colour; no scroll bar: six factions), the pane's "{...}" colour (the info pane's second text colour: the global text state the original leaves) | — |
| Base window details | help bubbles (right clicks on panes: BaseWin_click / BaseWin_iface_click, Win_set_bubble_text, pfncPaintMain's bubble), the panes' scroll arrows, the production pane's 3D model | done (basewin_cmd.cpp: the panes' bubbles, drawn by win.cpp (find_font(12), black on a white frame), cleared by a mouse move; the right button's clicks reach the window; the units present pane's stack arrows (MainInfc 0x3F0 / 0x3F2) with their clicks; basewin_draw.cpp: the production's voxel model with BaseWin_init's camera. Approximated: the production pane's bubble (the original passes an uninitialised text)) | — |
| Diplomacy panes | DiploPop_draw_info, DiploPop_draw_hq, DiploPop's popup look | done: DiploPop_draw_info (diplowin.cpp: the two faction boxes); DiploPop's buttons DATALINKS (the faction's datalink entry), PROFILE (report_intel) and HQINFO (DiploPop +0x4208, set by DiploPop_on_status when the mode starts: DiploPop_draw_hq's pane, the HQ base's name and the faction's picture 30, in place of the world map; a globe button shows the map again), clickable while the dialog's popups are open (DiploPop_set_modal; tests/diplo case 5). The dialog's popups are drawn in DiploPop's window (see Interface arms); the globe buttons (DiploPop_on_iface_group_clicked; in the main interface MainInterface_on_iface_group_clicked): 0x3EF the detail map (detailmap.cpp: WorldWin_set_detail_map / _set_world_map, MapWins[1] over the world map pane at zoom -8; tests/diplo case 6, compared with the original's). Approximated: the detail map takes the main view's display options and is centred on the cursor at each redraw | — |
| Monuments / hall of fame | the new entry (FUN_0044A660), decorations, background FLC | done (monuwin.cpp): the new entry at the end of the game (FUN_0044A660 / FUN_0044AC10: the slot by rating, the lower entries and their pictures moved down, the entry's fields and "best of" marks, the world map pane written as hof_<f>_<n>.pcx by Buffer_write_pcx, in --outdir as hallfame.jhh) and the hall of fame's WORLD MAP picture (FUN_00448D90); the monuments' background (monument.flc advanced to the number of monuments, as MonuWin_init) and MonuWin_init's decorations: the viewer's emblem above the centre, the victory picture or an eliminated leader in the centre, the eliminated leaders' portraits with the pillars for their number (tests/reports case 5b). The hall of fame's other panels (FUN_00448400 / FUN_00448D90, monuwin.cpp: the panel at (0x77, 0x50), the six categories' buttons pressed for the analysed entry's marks, the factions' emblems (their PCX's 0x2BD sprites, the selected one pressed), the faction's logo under WORLD MAP, SCORE ANALYSIS (the marks, how the game was won, the score lines), the banner of the first mark, the column titles, TOP 5 SCORES with the analysed entry in colour 0x9B, CLOSE with boxes 45..47; compared with the original's empty hall: the same but the factions). The eliminations' pillars and portraits are not exercised by the test saves (no faction eliminated another) | — |
| Map view | partial and scrolled redraws (MapWin_gen_radius), MapWin_draw_wars / legend / overlays, pfncProcZoomKey's mouse position, palette cycling | done. base_draw: the base's art by its founder (faction_id_former) and the drone riot icon (pixel-exact against the oracle's mapdump of turn_300, tests/placeholders case 13); pfncProcZoomKey's mouse position (console.cpp); mapwin_radius.cpp: MapWin_compute_clip, MapWin_gen_radius, MapWin_draw_vehicles_2, MapWin_draw_radius / draw_tile and the engine's draw_tile, draw_tiles, draw_radius, draw_tile_fixup(_2) (the changed tiles redrawn at once on every open map window, as the original; boom's MapWin_draw_tile), MapWin_gen_overlays (AI goals, option 0x2000), MapWin_draw_legend (0x20000), MapWin_draw_wars (0x100000) in pfncDrawMap and the partial redraws: pixel-exact against the oracle's mapdump options= / radius= (case 17; the legend's bold digits differ in 10 pixels, the font rasteriser's); scrolled redraws (pfncRedrawMap's scroll path) pixel-exact against the oracle's mapdump scrollfrom= (turn_300 scrolled to 1,13 from 60,9 and from 7,17, case 17; the edge unit's difference was smacx's: vehdraw_draw / vehdraw_scoot took the camera yaw of the last unit drawn, the original computes the unit's camera before its rotation; the original has no dependency on its terrain cache there: its full and scrolled pictures from 60,9 are the same); palette cycling (gfx.cpp: Palette_init_cycle / start_cycle / timer_callback, the nearest-colour searches skip the cycles' entries; the shipped system.txt has no #CYCLING entries, so the original animates no colours: case 18 checks a game directory with three). Approximated: the cursor texture MapWin_draw_radius draws again (drawn over the view at each presentation), do_all_draws after a partial redraw (the next frame) | — |
| Music | soundx.dll's ambient engine | approximated (music.cpp); not made more faithful: soundx.dll was decompiled (Ghidra, 1482 functions) and its .amb loader found (FUN_10003A30: chnl / trak / glbl / prgm / kmap chunks, then the MIDI file; glbl entries of 0x38 bytes kept in a list sorted by their second field, FUN_1001E820), but its sequencer is a large C++ engine driven through vtables (the tick FUN_1001B920, 16 channel slots of 0x428 bytes each with 128 random values pre-drawn in a [min, max] range, the patterns' own two random tables, FUN_1001DDF0), and every random table is reseeded from timeGetTime (FUN_1001C300 / C420 / C750 / CB60 / E040): the original's choices cannot be reproduced, and how the slots' values map to track, timing and pitch choices could not be settled without reconstructing most of the engine | L |
| Out of scope | multiplayer: DirectPlay, NetDaemon, PBEM / hotseat setup and DiploWin (`diplo`), NetMsg's timed window, MultiWin's chat, multiplayer_init | documented placeholders | — |
