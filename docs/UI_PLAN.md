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
  (TTF rasterised without antialiasing, GDI metrics), audio later (SDL audio for WAV/MP3 voices),
  movies later (FLC/WVE decoders on top of the frame API).
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
  | `filebox` | FileDialog_*, FileBox_*, FileList_set_path/add_ext/close, ui_filebox_destroy (the dialogs of save_game / load_game) | console_filebox.cpp |
  | `socialwin` | SocialWin_exec, SocialWin_close | socialwin.cpp |
  | `reports` | report_score, report_intel, monument, hall_of_fame | reports.cpp, monuwin.cpp |
  | `techwin` | tech_pick (the research dialog), NewTechWin_exec | techwin.cpp |
  | `design` | DesignWin_exec | designwin.cpp |
  | `help` | help_topic, help_project (Datalink_exec) | help.cpp, datalink.cpp |

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
| 5 (mostly done: src/ui/basewin*.cpp, tests/basewin/) | BaseWin (base screen): layout and art, panes, city map, clicks, build menu (text popup stand-in for the picker grid), queue, hurry, governor, specialists, rename, recycling; headless stubs in src/headless/ui_headless_basewin.cpp | `basedump` canvases vs oracle (tests/basewin/compare.sh), scripted keys (tests/basewin/run.sh) |
| 6 (done, section 6) | popups: BasePop on the game's script files (Script.txt, xscript.txt etc.: `#LABEL`, `^` lines, `$STR` parsing via the reconstructed text module), `popp/popb/X_pop*`, NetMsg, choice lists | popups by label vs oracle |
| 7 (done, section 9) | design workshop (DesignWin), tech pick (research window, Console_set_tech / set_ai), NewTechWin, datalinks (help); social engineering was done with the reports (section 8) | tests/design/run.sh, tests/datalinks/run.sh; tests/design/compare.sh vs the original |
| 8 | diplomacy (DiploWin, commlink), council window, reports (ReportWin/ReportIf), monuments | |
| 9 | main menu, game setup (top_menu, setup dialogs), file dialogs (FileBox), preferences | new game from the menu |
| 10 | sound (FX, voices, music), movies (FLC/WVE: openings, interludes, secret projects) | |

Each phase replaces the corresponding headless stubs in smacx; `smacx_headless` and the hybrid
builds stay unchanged.

## 4. Verification against the original

* **Oracle screenshots**: `work/ui/shot_oracle.sh <script> <outdir>` runs terranx.exe under
  Wine/Xvfb (1024x768) with oracle.dll and grabs every changed frame (`grab.py`) while the script
  runs (`load SAVE`, optional `viewer N`, `turns 1`). The first map frame after load is the
  desktop_init redraw: same save, same view centre (the map cursor), no bases/units yet
  (`dword_93A940` is still 0); later frames show the turn.
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
  say_orders (statuswin.cpp). Not reconstructed: MapWin_right_menu, MapWin_editor, the path
  preview (MapWin_dest_line, Console_goto_routine's PlanWin), sounds.
* **Main interface** (console_frame.cpp; approximated, see its header): console2_A.pcx frame,
  pane backgrounds, menu / commlink buttons, "TURN COMPLETE", the unit, tile, stack, info and
  date panes and a simple world map, drawn with the original's art, fonts and colours at the
  measured positions, not by StatusWin_draw_* / InfoWin_setup_text / WorldWin_on_redraw.
* **Menus** (menu.cpp; approximated): the bar and pull-downs of menu.txt with the original's
  command ids and init-hidden items (Alt, Alt+letter, arrows, Enter, mouse); a command sends its
  shortcut key to Console_on_key_click (Console_menu_proc is not reconstructed).
* **Save / load**: ctrl+S / ctrl+L (and the Game menu) run the engine's save_game / load_game with
  a simple file box (console_filebox.cpp): a name field, a list of saves; original naming.
* **Placeholders** (src/ui/console_placeholders.cpp): the map cursor and selected unit are
  diamonds; preferences, landmarks, the scenario
  editor, the unit menu of a right click, time controls, survey, assemble, tutorial hints log
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
* **Not reconstructed**: sounds (`#wave` etc.), the main interface's frame arms and the slide
  animation (slide_show), DiploPop (diplomacy menus are shown as plain popups with the portrait),
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
  Set to EXPLORE.. / off); security nexus with the unit type filter; labs (Console_set_tech on the
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
  the BLURBSX quote, OK; a link opens the datalinks, then the window again. help_tech_info and
  tech_heck (helpx.txt TECHHECK) are in src/ui/help.cpp, with the help_* entry points. Not
  reconstructed: the voice clip, the two picture buttons' function, scroll bars.
* **Design workshop** (src/ui/designwin.cpp): DesignWin_exec and its methods with the original's
  game state changes (setup_veh, design_it, check_abil, suggest_abil, the select_* rules, the
  buttons APPLY / DONE / RENAME / OBSOLETE / UPGRADE / RETIRE / CANCEL / presets, the keys, the
  prototype list; Units[63] used as the preview's fake unit, as the original), through the
  engine (make_proto, name_proto, prune_protos, retire_proto, consider_designs, upgrade_prototype,
  synch_proto / synch_obs). The component pickers (FUN_00433F70) are grids of names and values
  (the original renders each component's model). The design is drawn with the voxel renderer
  (vehdraw_construct_chassis / weapon / armor / reactor / abil; vehdraw_construct_reactor 0x4C3800
  is now a function of its own). Reached by u (design_new_veh), the menu, tech_achieved's
  ASKSEEDESIGN and the base window's build menu (a last item standing for the picker's DESIGN
  mode). Multiplayer paths (diplo_lock, message_data) are not reconstructed.
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

