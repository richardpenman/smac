# SMAC/X reconstruction

Goal: reconstruct the source of *Sid Meier's Alpha Centauri: Alien Crossfire*
as portable C++ that builds and runs natively,
and prove behavioural equivalence against the original binary with a differential harness.

## Layout

| Path | What |
| --- | --- |
| `game/` | Installed game data (assets, `terranx.exe`). Read-only input. |
| `ref/` | Public reverse-engineering references: Thinker (MIT), OpenSMACX (GPL-3). Used for names/layouts. |
| `tools/` | Downloaded tooling: Ghidra, llvm-mingw (i686 Windows cross compiler), Python venv. |
| `re/` | Analysis: Ghidra project, symbol map, exported decompilation (`re/out/functions.jsonl`). |
| `src/` | The reconstructed game. |
| `harness/` | Differential harness: `oracle/` (DLL injected into the original), comparison tools. |
| `work/` | Scratch run directories, Wine prefix. Disposable. |

## Approach

1. **Inventory**: Ghidra decompiles every function; names come from Thinker/OpenSMACX symbol maps.
2. **Layout-faithful state**: the reconstruction keeps the original 32-bit struct layouts for all
   game state (factions, bases, vehicles, map, rules), with `static_assert`s on sizes/offsets. This
   keeps savegames byte-compatible and lets the harness compare state directly.
3. **Determinism**: the game uses MSVC `rand()` (214013/2531011 LCG) and its own LCG
   (1664525/1013904223). Both are reimplemented bit-exactly; the harness pins the seeds that the
   original derives from the clock.
4. **Differential harness**: the original runs under Wine + Xvfb with `oracle.dll` injected. The DLL
   pins seeds, sets up a game non-interactively, runs all-AI turns and writes a save + RNG state after
   every turn. The reconstruction runs the same scenario headless and writes the same artifacts.
   `harness/compare` diffs them field by field and reports the first divergence.

## Tooling (all local, no system installs)

| Tool | Location | Notes |
| --- | --- | --- |
| Ghidra 12.1.4 | `tools/ghidra_12.1.4_PUBLIC` | Project in `re/ghidra_proj`; scripts in `re/ghidra_scripts` (`ApplySymbols`, `ExportAll`). |
| llvm-mingw 20260922 | `tools/llvm-mingw-*` | i686 Windows cross compiler for `oracle.dll`/`hybrid.dll` (`cmake/i686-mingw.cmake`). |
| Python venv | `tools/venv` | pefile, capstone, pillow, numpy, mss. |
| SDL2 2.30.11 + SDL2\_ttf 2.24.0 | `tools/sdl2` | Built from source (`tools/SDL2-*`); X11/ALSA loaded dynamically. Headers for the build came from Debian `-dev` packages unpacked into `tools/sysroot`. |
| Wine prefix | `work/wineprefix` | Private; crash debugger disabled. |

## Builds

```sh
cmake -S . -B build/native && cmake --build build/native -j2          # engine + smacx_headless (+ smacx with SDL2)
cmake -S . -B build/hybrid -DSMACX_HYBRID=ON -DCMAKE_TOOLCHAIN_FILE=cmake/i686-mingw.cmake
cmake --build build/hybrid -j2                                         # oracle.dll, hybrid.dll, oracle_launch.exe
```

`smacx` (native game with the SDL2 UI, first slice: the map of a save; docs/UI_PLAN.md) is built
when SDL2 and SDL2\_ttf are found (tools/sdl2 first): `build/native/smacx --save FILE.sav`.

Reconstruction work follows `docs/RECONSTRUCTION.md`; the harness is described in `harness/README.md`.

## Status

Work in progress. There is no playable build yet: the UI has not been reconstructed, and the
native engine (`smacx_headless`) still aborts at the first original function that is not
reconstructed. Reconstructed so far: boot, text/rules/preferences, map and world generation,
vehicles/stacks/prototypes/zones of control, save/load; bases and technology/social/unit design are
in progress. Progress per function: `build/*/generated/status.tsv`.

## Getting started

Not in the repository (copyrighted or regenerable): the game, the installers, tools, reference
projects and generated analysis. To recreate them:

1. Install the game files into `game/` (GOG Alpha Centauri Planetary Pack; `game/terranx.exe` must be
   v2.0, md5 `cc659f53cb6fa0eaaddccd0a54176647`).
2. Data image for native builds:
   `python3 src/tools/gen_data_image.py game/terranx.exe src/data/data_image.bin`
3. Reference projects (names/layouts): `git clone https://github.com/induktio/thinker ref/thinker`
   and `git clone https://github.com/b-casey/OpenSMACX ref/OpenSMACX`.
4. Analysis (needed for reconstruction work, not for building): download Ghidra into `tools/`, then
   `python3 re/scripts/extract_symbols.py`, run `analyzeHeadless re/ghidra_proj terranx -import
   game/terranx.exe -scriptPath re/ghidra_scripts -preScript ApplySymbols.java re/out/symbols.tsv
   -postScript ExportAll.java re/out/functions.jsonl`, then `python3 re/scripts/split_funcs.py` and
   `objdump -d -M intel --start-address=0x401000 --stop-address=0x664000 game/terranx.exe >
   re/out/terranx.asm`.
5. The differential harness additionally needs Wine, Xvfb and llvm-mingw (Linux).
