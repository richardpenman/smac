# Reconstruction guide

How to turn original functions of `terranx.exe` into source in `src/`, and how to prove each one
behaves identically. Read this fully before reconstructing anything.

## Fidelity contract

Every reconstructed function must behave **bit-identically** to the original: same results, same
side effects on game memory, same sequence of random number draws. Readability matters, but never
at the cost of behaviour. Original bugs are preserved and marked `// BUG(original): ...`.

## Where things are

| What | Where |
| --- | --- |
| Function index (addr, name, size, module, callers/callees counts, file) | `re/out/index.tsv` |
| Decompiled C per function (Ghidra) | `re/out/funcs/<module>/<addr>_<name>.c` |
| Full disassembly | `re/out/terranx.asm` |
| Quick lookup | `re/show <name|hexaddr> [--asm|--both]` |
| Module address ranges | `re/modules.tsv` |
| Known names of functions/globals | `re/out/symbols.tsv` |
| MSVC C runtime functions (FUN_0064xxxx → strcat, sprintf, _stricmp, …) | `re/crt_names.tsv` — call the libc equivalent; ignore `_chkstk`, `__ehvec_*`, `_lock`; `_ftol` is an `(int)` cast. Keep sprintf buffers at least the original size; `toupper`/`tolower` must behave as in the C locale; the original `rename` fails if the target exists |
| Struct layouts | `src/engine/types/*.h` |
| Engine globals (name → original address) | `src/engine/globals.h` |
| Reference implementations | `ref/thinker/src` (MIT, may borrow with attribution), `ref/OpenSMACX/src` (GPL-3: read for understanding only, do **not** copy code) |

Thinker is a *mod*: many of its functions deliberately change game behaviour. Use it to learn what
fields and functions mean, never as ground truth. The decompilation and disassembly are the truth.

Ghidra's decompilation is a guide, not gospel. Check the disassembly whenever signedness, shifts,
division, char/byte extension, or argument order matters (`re/show NAME --both`). Example:
Ghidra prints `game_random`'s scaling as a signed `>>`, but the code uses `shr` (unsigned).

## Code layout and conventions

* Module `X` (names from `re/modules.tsv`) lives in `src/engine/X.h` / `src/engine/X.cpp`.
* Declare every reconstructed original function in its module header with its address:

  ```cpp
  ADDR(0x5C86E0) void GAMEAPI world_build();
  ADDR(0x59A220) void THISCALL Path_init(Path* self);   // C++ method of the original: object first
  ```

  The signature must be ABI-identical to the original (cdecl, all parameters 32-bit). Use
  `int32_t`/`uint32_t` and pointers; never `long`, never 64-bit types in signatures.
* Name functions as in `re/out/index.tsv`. Give `FUN_xxxxxx` functions a descriptive name and
  mention the address in the doc comment. Keep one short doc comment per function (purpose, return
  value meaning); no decompiler noise, no `iVar3`-style names.
* `tools/gen_bindings.py` (run by the build) picks up `ADDR(...)` declarations: implemented ones
  become hybrid patches, missing ones get stubs (native: abort with the function's name; hybrid:
  forward to the original). `build/*/generated/status.tsv` lists the state of every declaration.
* To call an original function that is not reconstructed yet, just declare it (with `ADDR`) in the
  header of the module it belongs to. Hybrid builds will call the original; native builds abort
  when it is reached, which is how we find what to do next.
* `ADDR_NATIVE` is for functions whose state lives outside the memory image (CRT `rand()`), and for
  UI functions (below). Hybrid builds always call the original for these.

### Globals

* Use the names in `src/engine/globals.h` (or a module header). Every global is
  `GLOBAL(type, Name, 0xADDR);`. Before adding one, grep for its address — it may already exist under
  another name. Two names for one address are harmless.
* Add new globals to the module header that owns them (or `globals.h` if shared), with the precise
  type: arrays sized correctly, structs where the memory is a struct.
* Globals whose type contains pointers (rule tables with `char*` names, heap arrays like `MapTiles`)
  must use `GLOBAL_NATIVE`: native 64-bit builds cannot store pointers in the 32-bit memory image.
  Never read a pointer out of the image in native code.
* Never use raw address arithmetic in reconstructed code. Convert `*(short*)(&DAT_0097d042 + i*0x134)`
  into `Bases[i].y` using the struct layouts. If a field is missing or misnamed in
  `src/engine/types`, fix the struct (keeping the layout and its `SMAC_LAYOUT` assert intact) and
  grep for other uses.
* Initial values of plain-data globals come from the original data image automatically. Data
  containing pointers (e.g. tables of string pointers) must be re-created in source.

### Semantics to preserve exactly

* **Random draws**: same calls, same order, same arguments. C++ does not specify argument evaluation
  order: `f(game_rand(), game_rand())` must be split into statements matching the disassembly.
* **Integers**: the build uses `-fwrapv` (wrapping signed overflow like MSVC). Match signed vs.
  unsigned division, modulo, shifts (`sar`/`shr`, `idiv`/`div`), and sign/zero extension
  (`movsx`/`movzx`). Plain `char` is unsigned on ARM: use `int8_t`/`uint8_t` for anything
  arithmetic.
* **Floating point**: the original uses x87 (80-bit intermediates). Use `double` and keep the
  operation order; mark such code `// FP:` so divergences are easy to locate.
* **Wall clock**: where the original reads `timeGetTime()` as a randomness source (base_terraform,
  drone_riot, alien_fauna, probe, save_daemon, world_climate, world_borehole) call
  `engine_logic_time()` (`engine/clock.h`); for delays/animation use `engine_real_time()`.
* **Short-circuit and loop structure**: keep conditions whose evaluation has side effects (random
  draws, writes) in the original order. Early-exit loops must exit at the same element.

### Known uninitialised reads in the original (must be emulated)

The original reads stack memory it never wrote in these places. The oracle makes them canonical
(stack poisoning, see harness/README.md); the reconstruction emulates the canonical value.

1. **`Path_find` (0x59A530), local `[ebp-0x2c]`** ("tile being expanded is in enemy ZOC"): not
   computed for the start tile (cost 1), read at 0x59AEBE/0x59AF26 when expanding the start tile's
   neighbours with flags & 0x40, faction > 0, empty neighbour. Only `Path_move` (call 0x59BE66) hits
   this; the slot holds what `speed()` (called just before) left behind: has_abil's saved EDI
   (= `unit_id*0x34`), overwritten by `morale_veh`'s callees (morale_alien / has_tech loop: a stack
   address; bitmask: 0x5C105A). Canonical value is 0 only for unit 0 when `morale_veh` makes no call.
   * Native: `speed()` sets an emulation global `SpeedStackResidue` (1 if morale_veh made a call:
     faction==0, or plan==11 probe with any Tech flag 2, or (!(unit<64 && psi weapon) &&
     home_base_id>=0); else `unit_id*0x34` unless unit_id==19 → unchanged). `Path_move` passes
     `SpeedStackResidue != 0` as the initial ZOC flag into `Path_find`. Mark `BUG(original)`.
   * Hybrid: a reconstructed `Path_find` needs a naked entry thunk capturing `*(uint32_t*)(esp-0x30)`;
     reconstructed `speed`/`speed_proto`/`morale_veh` need a thunk writing the residue to
     `[entry_esp-0x48]` (the existing `has_abil` thunk works because it pushes EDI as the original).
2. **`enemy_move` (0x56B5B0), local `[ebp-0x48]`** (Ghidra `local_4c`, passenger keep-sentried limit
   in the transport branch, read at 0x574DD7). Canonical value (poison 0 / 0xFFFFFFFF) behaves as
   "limit <= 0". Reconstruct with this variable initialised to 0 at function entry (shared across
   the roles the compiler merged into the slot), marked `BUG(original)`.
3. Suspect, unproven: `enemy_move` 0x574A8B loads `[ebp-0x38]` into EDI before writing it; check the
   `esi != 7` path to 0x576828 when reconstructing.

Tools for finding more: oracle commands `steptrace`, `slotlog`, `poison rand`, and
`harness/scripts/uninit_reads.py` (see harness/README.md).

### UI calls from engine code

Engine functions call UI code (popups, `draw_map`, sound, `Console_*`, window classes). Functions
in UI/system modules (`kind` ui/sys in `re/modules.tsv`) are not reconstructed by engine work.
Declare the ones you need in `src/engine/ui_bridge.h` with `ADDR_NATIVE`, grouped by original
class, and add a headless implementation to `src/headless/ui_headless.cpp` that behaves as for an
unattended AI observer: no drawing, popups return the default/first choice (document the value).
If a UI function also changes game state, reconstruct that part faithfully or flag it.

## Verification

Build in your own build directories so parallel work does not collide:

```sh
cmake -S . -B build/$ME-native && cmake --build build/$ME-native -j2
cmake -S . -B build/$ME-hybrid -DSMACX_HYBRID=ON -DCMAKE_TOOLCHAIN_FILE=cmake/i686-mingw.cmake
cmake --build build/$ME-hybrid -j2
```

Both must build without errors (native proves the code is portable; hybrid is what we test).

**Hybrid verification** runs the original game with reconstructed functions jump-patched over the
originals and compares every game-state field per turn against cached oracle runs:

```sh
HYBRID_BUILD=build/$ME-hybrid harness/verify.sh hybrid                 # all reconstructed functions
HYBRID_BUILD=build/$ME-hybrid harness/verify.sh hybrid "none +world_build +world_rivers"
HYBRID_BUILD=build/$ME-hybrid harness/verify.sh hybrid "all -alt_set" tiny20
```

Each scenario must report `OK` for every turn. On `DIFF`, the output names the first divergent
fields (e.g. `MapTiles[812].climate`, `rng {'crt': ...}`); bisect with patch specs to find the
function responsible. Snapshots are taken before each `turn_upkeep`; `turn_000` covers world
generation and game setup. Scenarios live in `harness/scenarios/` (add more for code paths that the
defaults do not reach; e.g. `load` a late-game save from `work/ref/long300/`).

A function that is never called in the scenarios is not verified by them. Say so in your report
and, when practical, add a scenario that reaches it.

**Function comparison** is mandatory for every work package: add an argument-table entry for each
reconstructed function to `harness/oracle/fcompare_table.h` and run

```sh
HYBRID_BUILD=build/$ME-hybrid harness/verify.sh fcompare "none +fn1 +fn2 ..." tiny20 small12 std8
```

(plus late-game scenarios such as `late100`/`late120`, which reach more code). Every scenario must
report `PASS`. It calls original and reconstruction side by side on the live state of every turn
and compares results and memory effects, so it also covers functions that the game paths of the
scenarios rarely reach, and it is not disturbed by the stack-content effects that can make whole
hybrid games diverge (see harness/README.md). Report the per-function call counts of functions that
were never called (empty domains). Details: harness/README.md, "Function comparison".

**Native verification** (`harness/verify.sh native`) runs `smacx_headless` against the same
references once the native code path is complete.

## Reporting

When you finish a work package, report: functions reconstructed (count and names), verification
results per scenario, functions not exercised by any scenario, deviations or doubts, and new
globals/struct fields added.
