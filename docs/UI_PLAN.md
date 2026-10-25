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
* **Bridge switch (engine-side change, not done yet)**: today the engine's `ADDR_NATIVE` UI calls
  resolve to src/headless in every native link, and src/ui uses `namespace native_ui` for the
  bridge names it reconstructs (pfncRedrawMap, pfncMoveMap, load_faction_art). Proposal: move the
  headless implementations into a library (`smacx_ui_headless`) whose functions are weak or are
  selected per function by a generated table, so `smacx` links src/ui implementations first and
  falls back to headless for the rest. Alternative: one function-pointer table in ui_bridge with
  headless defaults that smacx overrides at startup. Either keeps smacx_headless unchanged.
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
| 6 | popups: BasePop on the game's script files (Script.txt, xscript.txt etc.: `#LABEL`, `^` lines, `$STR` parsing via the reconstructed text module), `popp/popb/X_pop*`, NetMsg, choice lists | popups by label vs oracle |
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
  * The oracle's `load` uses `load_daemon`, not `load_game`, so the faction art (base sprites,
    faction colours) stays the one loaded at boot for the default faction set: base sprites and
    label colours of a loaded save are wrong in oracle screenshots. smacx loads the save's art
    (as `load_game` does). A harness change (oracle `load` also calling `load_faction_art(1..7)`)
    would fix the reference.
  * Viewer 0 (observer) shows the landmark word as "items" (visible_items[-1] is the landmarks
    field): condensers and boreholes everywhere. smacx reproduces it; use `viewer N` for
    realistic references.
  * With a real viewer the oracle stops in modal windows (monuments, new tech) after a while;
    only the first frames are usable.
* **Next level**: a hybrid-side screenshot command in oracle.dll (render the main view into its
  Buffer and dump it with the palette, no window grab) would give exact per-pixel references for a
  given state, and an fcompare-like check of single drawing functions (same arguments, compare the
  canvas bytes).
