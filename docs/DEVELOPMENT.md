# Development

Details for working on the reconstruction. The README covers playing and building.

## First run of OpenSMAC

1. **Finding the game.** OpenSMAC uses the folder it remembered (`opensmac.cfg` in the per-user
   directory), else looks in the standard install locations: on Windows GOG's registry entries,
   `C:\GOG Games`, `Program Files`, GOG Galaxy and Steam folders; on macOS GOG's Mac app (a Wineskin
   wrapper: `*.app/Contents/Resources/drive_c/...`) in `/Applications` and `~/Applications`,
   `~/GOG Games`, CrossOver bottles; on Linux `~/GOG Games`, `~/Games` (Lutris, Heroic), Wine prefixes
   (`$WINEPREFIX`, `~/.wine`), Heroic and Bottles prefixes, Steam/Proton. A folder qualifies when it
   has `terranx.exe` v2.0 and the game's data files; another version of `terranx.exe` is reported
   with its md5. If none is found, OpenSMAC asks: a message box, then the platform's folder picker
   (Windows: the shell's folder dialog; macOS: `osascript` choose folder; Linux: `zenity` or
   `kdialog` when installed), else a path typed on the terminal. The answer is checked and
   remembered. Without a display or a terminal it exits with status 3 and a message;
   `OPENSMAC_GAMEDIR=DIR` gives the answer in advance, `--gamedir DIR` skips the search.
2. **The data image.** OpenSMAC builds the engine's initial data image from the player's
   `terranx.exe` (the C++ port of `src/tools/gen_data_image.py`, byte-identical output) and
   caches it as `data_image.bin` in the per-user directory (`--image` overrides).
3. **Per-user files.** The ini (`Alpha Centauri.Ini`, seeded from the default ini shipped next to
   the executable), saved games (the game's `saves` folder), the Hall of Fame and screenshots go to
   the per-user directory: `%APPDATA%\OpenSMAC` (Windows), `~/Library/Application Support/OpenSMAC`
   (macOS), `~/.local/share/opensmac` (Linux). The game folder is only read. A copy from before
   the rename to OpenSMAC (v0.2.x, called smacx) used `smacx` there: when the new directory is
   missing or empty, the old one is moved to it (copied when a rename fails) and its `smacx.cfg`
   renamed to `opensmac.cfg` (src/platform/userdir.h).

With `--outdir DIR` (the tests and the harness) none of this happens: the defaults are the
repository's (`game/`, `src/data/data_image.bin`, `harness/oracle/runtime/Alpha Centauri.Ini`),
everything is written below `DIR` (saved games in `DIR/saves`), and the ini is copied from `--ini` at every start, so a
stale ini from an earlier run cannot change a test run.

## Making a release

Push a tag: `git tag v1.2.3 && git push origin v1.2.3`. `.github/workflows/release.yml` builds
the three packages (Linux on Ubuntu 22.04, the SDL2 libraries built from source; macOS arm64 on
`macos-14`, an `.app` with the SDL2 dylibs in `Contents/Frameworks`, ad-hoc signed; Windows
cross-compiled with llvm-mingw against the official SDL2 mingw packages) and publishes them as a
GitHub Release (a tag with a `-`, e.g. `v1.2.3-rc1`, makes a pre-release). The repository is
private, so Actions minutes are billed: one release is roughly 65 billed minutes (macOS counts
10x). Every push to `main` also publishes a pre-release `build-N-SHA` (N the workflow's run
number) with the commit's packages, its tag made on the commit; pushes changing only `docs/` or
`*.md` files are skipped. Running the workflow by hand on a branch builds the packages as
workflow artifacts without a release. `packaging/package.sh` assembles a package and refuses any file outside its
allow-list, so no game data or anything derived from `terranx.exe` can ship.

## Layout

| Path | What |
| --- | --- |
| `game/` | Installed game data (assets, `terranx.exe`). Read-only input. |
| `ref/` | Public reverse-engineering references: Thinker (MIT), OpenSMACX (GPL-3). Used for names/layouts. |
| `tools/` | Downloaded tooling: Ghidra, llvm-mingw (Windows cross compiler), Python venv. |
| `re/` | Analysis: Ghidra project, symbol map, exported decompilation (`re/out/functions.jsonl`). |
| `src/` | The reconstructed game. |
| `tests/` | Portability smoke test (`opensmac_smoke`, no game data needed). |
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
| llvm-mingw 20260922 | `tools/llvm-mingw-*` | Windows cross compiler: i686 for `oracle.dll`/`hybrid.dll` (`cmake/i686-mingw.cmake`), x86-64 native builds (`cmake/x86_64-mingw.cmake`). |
| Python venv | `tools/venv` | pefile, capstone, pillow, numpy, mss. |
| SDL2 2.30.11 + SDL2\_ttf 2.24.0 | `tools/sdl2` | Built from source (`tools/SDL2-*`); X11/ALSA loaded dynamically. Headers for the build came from Debian `-dev` packages unpacked into `tools/sysroot`. |
| Wine prefix | `work/wineprefix` | Private; crash debugger disabled; the game's fonts installed by `harness/wine_env.sh`. |

## Building with CMake directly

The native build needs CMake 3.20+, Python 3 (it generates the original-function bindings) and a
C++17 compiler with `<filesystem>`. Game data is only needed at run time: `opensmac` finds the game
itself; `opensmac_headless` reads `game/` and `src/data/data_image.bin` (see "Getting started").
`opensmac_smoke` (`ctest`) is a portability self-test that needs no game data; `opensmac_unit` (`ctest`,
doctest) holds the engine's unit tests. ctest's `golden_*` tests replay the original's recorded
results of 197 functions (`tests/golden`, harness/README.md "Golden records") and need the game's
data (skipped without it); `names_table` checks that `re/original_names.tsv` matches the `ADDR`
declarations (`src/tools/names.py update` after adding or renaming one). Cleanup work (docs/CLEANUP_PLAN.md) is gated by
`src/tools/gate.sh`.

The oracle used by the comparison harness (Linux): `oracle.dll`, injected into the original
`terranx.exe`, and its launcher, which record the references (`work/ref`):

```sh
cmake -S . -B build/native -DCMAKE_BUILD_TYPE=RelWithDebInfo && cmake --build build/native -j8
cmake -S . -B build/hybrid -DOPENSMAC_HYBRID=ON -DCMAKE_TOOLCHAIN_FILE=cmake/i686-mingw.cmake
cmake --build build/hybrid -j8                                         # oracle.dll, oracle_launch.exe
```

### The hybrid build after the freeze

`hybrid.dll` (the reconstruction jump-patched into `terranx.exe`: `harness/verify.sh hybrid` and
`fcompare`, and the recording of golden records, `harness/golden.sh record`) needs the game state
at the original's addresses. Since cleanup step 20 native builds keep it in native objects
(src/engine/state.h), so main builds only the oracle; the hybrid is built from the freeze commit,
tagged `hybrid-final` (docs/CLEANUP_PLAN.md section 2), in a worktree of its own (the freeze
predates the rename to OpenSMAC, so its option is still `SMACX_HYBRID`):

```sh
git worktree add work/hybrid-final hybrid-final
cmake -S work/hybrid-final -B build/hybrid-final -DSMACX_HYBRID=ON -DCMAKE_TOOLCHAIN_FILE=$PWD/cmake/i686-mingw.cmake
cmake --build build/hybrid-final -j2
HYBRID_BUILD=build/hybrid-final harness/verify.sh hybrid all tiny20
```

It checks the functions as they were at the freeze. Changes made since are proven natively:
`src/tools/diffnative.sh` plays new seeds, saves and the 29 scenarios with the build under test
and with the freeze build (`--baseline hybrid-final`), and the golden records replay the
original's results of 197 functions.

On Windows with MSYS2, add `-G Ninja -DCMAKE_EXE_LINKER_FLAGS=-static` to the configure step.

Cross-compiling from Linux with the local llvm-mingw: `-DCMAKE_TOOLCHAIN_FILE=cmake/x86_64-mingw.cmake`
(64-bit) or `cmake/i686-mingw.cmake` without `OPENSMAC_HYBRID` (32-bit native, runs under 32-bit Wine).
The 64-bit `opensmac.exe` (the release's) runs under a 64-bit Wine: with only Debian's `wine32`
installed, `apt-get download wine64 libwine:amd64 libz-mingw-w64` and `dpkg -x` them into a folder
of `work/`, point its `usr/lib/wine/wineserver` script at the `wineserver64` beside it (the script
names `/usr/lib/wine`), and copy the x86-64 `zlib1.dll` into the prefix's `system32`; then
`WINEARCH=win64 WINEPREFIX=... .../usr/lib/wine/wine64 opensmac.exe` (a game folder in the prefix's
`C:\GOG Games` is found at the first start). The cross-built `opensmac_unit.exe` runs the same way.
Run `opensmac_headless` from the repository root (or pass `--gamedir`, `--image`, `--ini`).

## Compilers

The reconstruction keeps the original's MSVC/x86 semantics (signed overflow wraps, no type-based
aliasing, signed `char`, no fused multiply-add, out-of-bounds table reads kept as in the original);
`CMakeLists.txt` sets the flags (`-fwrapv -fno-strict-aliasing -fsigned-char -ffp-contract=off`,
GCC also `-fno-aggressive-loop-optimizations`). Targets must be little-endian (x86, x86-64, arm64).

| Compiler | Targets | Status |
| --- | --- | --- |
| GCC 9+ (tested 14) | Linux x86-64 | reference build; matches the original on all harness scenarios |
| Clang 10+ (tested 23) | Linux x86-64 | matches (tiny20, small12, std8, late140, council131) |
| llvm-mingw / MSYS2 clang (libc++) | Windows x86-64, i686 | builds; the i686 build matches under Wine (same five scenarios) |
| Apple clang | macOS arm64 / x86-64 | built and smoke-tested in CI |
| MSVC (cl.exe) | Windows x86-64 | experimental (CI, allowed to fail); not verified against the original |

CI (`.github/workflows/build.yml`) builds `opensmac_headless` and runs `opensmac_smoke` on all of these.

## The scripted UI suites

Everything compared with the original runs with the original's bugs (`--original-bugs`,
engine/compat.h, docs/COMPAT.md): `harness/verify.sh` passes it to `opensmac_headless`, the gate's
observer and ASan steps to OpenSMAC and `opensmac_headless`, and hybrid.dll always has them (patched
functions inside terranx.exe must behave as the original's). The game itself plays with the bugs
fixed; the gate's `fixes` step plays scenarios that way under ASan (no comparison).

`tests/*/run.sh` drive OpenSMAC with scripted keys (SDL dummy video and audio, fixed seeds). Their
expectations come from games played with the original's rules, so every suite passes
`--original-bugs`, except the multiplayer ones, which check that the setting travels: tests/hotseat
and tests/pbem set it up with `Original Bugs=1` in the first game's ini and check that the later
runs (ini default) play with the save's setting; tests/online's host has it in its ini and the
client takes the host's; tests/online2 plays with the fixes on both machines (the copies must
agree in that mode too). `tests/long/session.sh` (manual) plays with the fixes. They run
**animated** by default (`OPENSMAC_ANIMATE=1`, i.e. `opensmac --animate`): the window slides, the menu
column's slide, unit moves, combat booms, battle reports and movies play with their real waits, as
in an interactive game, and the run still quits when its keys end. `TEST_FAST=1` runs them
unattended instead (those animations skipped or drawn without waiting). `WAIT:MS` in a script lets
real time pass with no input (timers and animations run meanwhile). Timings (the twelve suites
one after another, before tests/hotseat): animated about 220 s, fast about 145 s; the slowest suite is placeholders.
`src/tools/gate.sh`'s `ui` step runs them with the native OpenSMAC and again with the ASan build.

`tests/long/session.sh [OUTDIR] [SEED...]` is a manual test, not one of the suites (it is not a
`run.sh`, so neither the suites' loops nor the gate pick it up): a new game played for about 45
turns from `tests/long/long.keys` (moves, go-to drags, the base window, menus, reports, combat,
end of turn) with animations on, by default against the ASan build (`build/bug-asan/opensmac`; any
OpenSMAC with `OPENSMAC=`). Each seed is one game; a sanitizer report or an abnormal end fails it.
About 5-10 minutes per seed under ASan.

## Recreating what is not in the repository

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
