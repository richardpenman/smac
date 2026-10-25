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
* **Units**: voxel models (`*.cvr`, load_voxels, ~"Caviar") rendered at runtime; native life from
  Units.pcx; flags from flags.pcx.

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
| 5 | BaseWin (base screen) | screenshots per base |
| 6 (done, section 6) | popups: BasePop on the game's script files (Script.txt, xscript.txt etc.: `#LABEL`, `^` lines, `$STR` parsing via the reconstructed text module), `popp/popb/X_pop*`, NetMsg, choice lists | popups by label vs oracle |
| 7 | design workshop (DesignWin), social engineering, tech pick/NewTechWin, datalinks (help) | |
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
  landmarks, unit sprites, flags, health bars and order letters match pixel for pixel; the 3D unit
  models (chassis 0..8: voxel models drawn by vehdraw_draw through the gfx2 software renderer) are
  not reconstructed yet and are the remaining differences.

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
* **Placeholders** (src/ui/console_placeholders.cpp, win.cpp): the console panel is
  text (turn, energy, selected unit, message line) instead of MainInterface/StatusWin/InfoWin; the
  cursor and the selected unit are diamonds; popups are reconstructed (section 6); orders that
  open other code (go to, automate, unload, artillery, abilities, convoy, patrol, road to,
  designate, destroy, disband, upgrade dialog), the base window, reports, menus, preferences and
  the scenario editor log "not reconstructed" and do nothing. Map redraws are full redraws at
  presentation time. Multiplayer paths stop at the first network call.
* **Testing**: `--keys` scripts input (characters, `C-`/`S-` modifiers, KP1..KP9, named keys, and
  the commands `SHOT[:file]`, `SAVE:file` (also writes `file.rng`), `UNITS`, `QUIT`);
  `--snapshots DIR` writes the harness's turn_NNN.state/.rng/.sav before every turn_upkeep and
  `--rng FILE` restores a snapshot's generators after `--save`. Checks done:
  * observer mode (`--human 0`) with every smacx area replaced matches the oracle references
    (harness/compare.py): tiny20, small12, std8 (new games), late140 (load);
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
  (multiplayer), NewTechWin and the other full-screen windows (they are not popups).
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

