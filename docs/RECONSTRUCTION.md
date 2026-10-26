# Reconstruction guide

How to turn original functions of `terranx.exe` into source in `src/`, and how to prove each one
behaves identically. Read this fully before reconstructing anything.

## Fidelity contract

Every reconstructed function must behave **bit-identically** to the original: same results, same
side effects on game memory, same sequence of random number draws. Readability matters, but never
at the cost of behaviour. Original bugs are preserved and marked `// BUG(original): ...`, each with
its compatibility flag next to it (`COMPAT_ORIGINAL(flag);`, declared in `src/engine/compat.h`;
catalogue docs/COMPAT.md, checked by `src/tools/compat.py check`).

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
  Other `timeGetTime()` reads inside engine functions get the harness's fast-forward clock (+1000
  per read, `harness/oracle/fast_clock_sites.h`); where such a value is used as randomness (probe:
  `timeGetTime() & 1` for Algorithmic Enhancement / Hunter-Seeker luck) its canonical value is
  even, so the reconstruction uses 0 (see `probe_clock_odd` in probe.cpp).
* **Short-circuit and loop structure**: keep conditions whose evaluation has side effects (random
  draws, writes) in the original order. Early-exit loops must exit at the same element.
* **Memory copies and packed data**: the original's `memcpy` is a memmove (it copies overlapping
  ranges correctly, and the code relies on it, e.g. `base_kill` shifting `Bases` down). Use
  `crt_memcpy`, never `std::memcpy`, for a copy the original makes. Data the original packs without
  alignment (text index entries, save records) is read and written with `crt_memcpy`, not through
  a cast `int32_t*`: unaligned access is undefined behaviour and traps or miscompiles on some
  targets.
* **Out-of-bounds indexes the original makes** (`Vehs[-1]`, `Tech[-1]`, one past a table) are read
  from the memory image explicitly, not through the typed array: the compiler may assume an
  in-bounds index and drop or reorder the access. (GCC builds add
  `-fno-aggressive-loop-optimizations` as a backstop; clang has no such option.)

* **Out-of-bounds reads into pointer tables.** When such a read lands in a table that native
  builds keep outside the image (`GLOBAL_NATIVE`: it holds string pointers), the image bytes there
  are stale. Read through `rules_orig_byte(addr)` (rules.h). It rebuilds Armor, Tech and Reactor
  in their original 32-bit layout. String pointers become the original's addresses: the
  StringTable pool sits at 0x01FBD3B8 in every oracle run, and native `Strings_put` allocates in
  the same order. Known cases:
  * `arm_val` is passed unit ids by `base_build` (BUG(original)). Ids 14..511 read Tech, including
    the low bytes of name pointers (ids 22, 33, ...; e.g. id 44 reads -62, a "psi" armor). Before
    the fix, native late100 diverged on turn 103, and the result depended on which native
    globals happened to follow `Armor`.
  * `Vehs[-1]` (0x9527F4) is the Reactor table. `Vehs[-1].unit_id` is the high half of
    `Reactor[0].name_short` (0x01FC = 508), and `.faction_id` is `Reactor[0].power` (0). Readers:
    `battle_resolve` (pop loss of an undefended base: native probe131 diverged on turn 129), and
    the `veh_at` result in `ai.cpp`'s attack odds. `veh_init` writes `Vehs[-1].morale` (Reactor[2]
    preq_tech) when the table is full; native does not mirror that.

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
   * Implemented (src/engine/path.cpp): `path_find_core(..., start_zoc)` holds the body; native
     `Path_find` passes 0, `Path_move` passes `SpeedStackResidue != 0`; the hybrid `Path_find` is a
     naked thunk storing `[esp-0x30]` in `PathFindEntryResidue`. fcompare (flag `FC_PATHS`) writes
     0 / 0x5C105A into that slot on alternate calls, so both values are compared.
2. **`enemy_move` (0x56B5B0), local `[ebp-0x48]`** (Ghidra `local_4c`, passenger keep-sentried limit
   in the transport branch, read at 0x574DD7). Canonical value (poison 0 / 0xFFFFFFFF) behaves as
   "limit <= 0". Reconstruct with this variable initialised to 0 at function entry (shared across
   the roles the compiler merged into the slot), marked `BUG(original)`.
   Implemented (src/engine/ai_move.cpp): every local is a field of `EM`, one per frame slot
   (`EM::s48` here), zeroed at entry; the slot is also written on many paths of the call (garrison
   want, loop indexes, a base pointer), which the reconstruction reproduces, so only the paths
   that reach 0x574C5E/0x574DD7 without such a write see the canonical 0.
3. **`enemy_move`, local `[ebp-0x38]`** (distance temp, `EM::s38`): confirmed. 0x574A8B loads it into
   EDI; for sea units that are not transports (plan != 7) the value survives (reloaded from the
   slot) to 0x576828, where a naval escort (group 0x15) with a waypoint order keeps the order
   (non-zero) or drops it (0). It is unwritten in this call only when no earlier code (naval hunt,
   escort loop, ...) stored a distance there: canonical 0 (drop the order). The same slot is read
   uninitialised at 0x56C1C7 (automated former fungus search, human units only: own-base tiles are
   considered only if it is 0). Both use the zeroed `EM`, marked `BUG(original)`.
4. **`wants_to_attack` (0x55BC80), array `[ebp-0x130]`** (7 dwords, Ghidra `auStack_138[1..7]`): the
   loop at 0x55BF6E stores `max(slot, region_total_bases)` per faction without ever initialising the
   slots, then reduces them by a quarter. Its result reaches `enemy_strategy` (region_flags bit 4
   via `local_60`). The residue depends on the depth at which the caller runs: fcompare of
   `time_warp` (whose callee `enemy_strategy` calls it) shows 1-byte `region_flags` differences on
   a few turns although `time_warp` itself matches. `slotlog 55BC80 9 -134 ... -11C` (late100)
   shows what the slots hold: in `enemy_strategy`'s region loop (calls at 0x562D0F/0x562D1E, the
   pair `(f, g)` then `(g, f)`) the second call sees the first call's reduced slots when that call
   reached the scan; the first call of a faction sees residue of earlier frames at that depth:
   zeros, small values, -1, or large numbers (0x688CC4..0xD07018, the same every turn for a given
   faction; stack addresses), which make the "own bases in this region" test true only in the
   headquarters region. No single canonical value exists. Only a residue larger than 4/3 of a
   faction's largest region base count changes the result (0 and -1 behave alike).
   Implemented (src/engine/diplomacy_b.cpp, `wants_to_attack_core(..., slots)`): hybrid builds use a
   shadow thunk (`HYBRID_SHADOW_THUNK`): the body reads the slots from the stack image saved at
   entry (`[entry ESP - 0x138 + 4f]` for faction f) and the original re-runs to leave its frame, so
   hybrid games are exact. Native builds keep the slots in `WantsToAttackResidue[8]`, updated as
   the frame is, so the second call of an `(a, b)` / `(b, a)` pair sees the first call's values.
   Marked `BUG(original)`.
   **Canonical start value.** The residue the first call of an evaluation sees comes from
   unrelated frames, including UI/Win32 ones (late140 turn 142, `spot_all -> encounter`: residue
   of `draw_radius` / `GraphicWin_redraw` / Wine code such as 0x014101C1, 0x7A6D9ADF), which
   headless code cannot reproduce. The oracle therefore poisons the 8 slots before every first or
   single call (harness/README.md, "poisons `wants_to_attack`'s slot array"; sites
   `kWantsToAttackFirstSites`); 0 and -1 behave alike. Reconstructed callers make these calls
   through `wants_to_attack_fresh` (diplomacy.h): native code clears `WantsToAttackResidue`
   first, hybrid code calls the oracle's poisoning thunk through
   `g_harness_wants_to_attack_fresh`, so it fills the slots at the same entry ESP. The second
   call of a pair stays a plain `wants_to_attack` call. Callers: `enemy_strategy` (ai.cpp),
   `encounter` (diplomacy_b.cpp), `tech_val` (tech.cpp), `wants_prop` (turn.cpp, two pairs).
   References from before the poisoning differed from turn 142 in late140 and long300 (old ones:
   work/ref_old_*).
5. **`order_veh` (0x5947C0), `[ebp-0x24]`** ("move shown to the viewer"): the slot also holds the
   target stack offset (`stack_id * 0x34`) and the native-spawn chance; the dir 8 path reaches the
   post-move code without setting it. Only display timing (`clock_wait`), `boom` and viewer
   messages depend on it; the reconstruction uses 0 there (`OrderVeh::visible`, compat flag
   `residue.order_veh_visible`). Also `[ebp-0x90..-0x80]` (ZOCMSG tutorial coordinates) stay
   unset when `quick_zoc` finds nothing (UI only).

6. **`call_council` (0x52C880), local `[ebp-0x14]`** ("aliens alive" flag): written only when
   proposal 1 (Supreme Leader) is examined (Diplomatic Victory on, not yet passed, caller among
   the two largest voters). Otherwise it holds what the prologue left there: the large frame is
   probed by `_chkstk` (0x645550), whose first instruction `push ecx` stores the caller's ECX in
   exactly this slot (the residue of earlier calls, e.g. `corner_market`'s pushed `1`, is
   overwritten; `slotlog` at the function entry sees that residue, not the final value).
   `faction_upkeep` calls with ECX = the human players' mask (`mov cl, FactionStatus[0]` of its
   `is_human` test, upper bytes 0): in observer games 0 (AIs propose anything), with a human
   player non-zero (AIs only call governor elections). Native: `faction_upkeep` passes
   `FactionStatus[0]`; other callers 1. Hybrid: a naked entry thunk stores ECX (turn.cpp).
   Marked `BUG(original)`. (late147: faction 4's Global Trade Pact, turn 148, needs the 0.)

7. **`alien_move` (0x566810), local `[ebp-0x10]`** (score): the movement scoring keeps a score
   marker 0x3039 from a previous tile and is read before any write when the unit is no artillery
   (`if (score != 0x3039) score = 0`). Only an exact residue 0x3039 would matter; the canonical
   values never are. Reconstructed with the local initialised to 0 (ai.cpp).
   `enemy_planet_buster` (0x56A8A0) copies the unset `[ebp-0x54]` into another local on its first
   pass, but that value is overwritten before use (no effect).

Uninitialised heap memory, not stack: `map_init` allocates `MapTiles`/`MapAbstract` without
clearing them and `map_wipe` leaves tile bytes 6 (`unk_1`) and 7 (`owner`) as they are; tiles no
base claims keep the allocator's residue (e.g. scenario rival6, "look first": owner bytes 0x01,
0x03, 0xFF in the original). Native `mem_get` returns zeroed blocks; the oracle clears both new
blocks right after `map_init` allocates them (harness/README.md), so the references match that.

Hybrid tool for side-effect-free functions whose dead stack matters: define the body as
`int32_t GAMEAPI SHADOWABLE(name)(...)` and add `HYBRID_SHADOW_THUNK(...)` (src/engine/hybrid_shadow.h;
example: speed/speed_proto/morale_veh in veh.cpp). In hybrid builds the thunk computes the result
with the reconstruction, then re-runs the original code from the same stack position so the stack
image is the original's. Native builds are unaffected; native code must emulate the read instead
(e.g. `SpeedStackResidue`, set by speed()).

Tools for finding more: oracle commands `steptrace`, `slotlog`, `poison rand`, and
`harness/scripts/uninit_reads.py` (see harness/README.md).

### UI calls from engine code

Engine functions call UI code (popups, `draw_map`, sound, `Console_*`, window classes). Functions
in UI/system modules (`kind` ui/sys in `re/modules.tsv`) are not reconstructed by engine work.
Declare the ones you need in `src/engine/ui_bridge.h` with `ADDR_NATIVE`, grouped by original
class, and add a headless implementation to src/headless that behaves as for an unattended AI
observer: no drawing, popups return the default/first choice (document the value). If a UI
function also changes game state, reconstruct that part faithfully or flag it.

src/headless is the static library `smacx_ui_headless`, one object file per UI area
(`ui_headless_<area>.cpp`; general functions in `ui_headless.cpp`). smacx_headless uses all of it;
smacx (the SDL UI, src/ui) replaces whole areas by defining their functions under the original
names in its own objects, so the linker does not pull those areas' objects (docs/UI_PLAN.md,
"Bridge switch"). Put a new headless function into the file of its area; if smacx already
replaces that area, smacx must define it too (otherwise the link fails with duplicate symbols).
A function declared with `ADDR` (not `ADDR_NATIVE`) in an engine header must be defined in
src/engine: gen_bindings.py counts a definition anywhere under src/, and hybrid builds do not
compile src/ui.

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

**Native checking builds.** Two configure options instrument `smacx_headless` (native only):

```sh
cmake -S . -B build/asan-native -DSMACX_SANITIZE=ON   # AddressSanitizer + UndefinedBehaviorSanitizer
cmake -S . -B build/cov-native -DSMACX_COVERAGE=ON    # gcov coverage
```

* Run any scenarios with the sanitized build (`build/asan-native/smacx_headless
  harness/scenarios/std8.cmd --outdir work/x`). An `AddressSanitizer` or `runtime error` line is a
  bug in the reconstruction (an overlapping `std::memcpy`, an unaligned cast, an out-of-bounds
  native array) unless it is an original out-of-bounds read, which must then go through the memory
  image as described above. Signed overflow is not reported: `-fwrapv` defines it.
* After coverage runs, `src/tools/coverage_report.py build/cov-native --missed` lists, per header,
  the reconstructed functions that native runs never executed. These are verified only by
  fcompare/hybrid runs, if at all; add a scenario when one matters. Counts accumulate across runs;
  delete the `*.gcda` files to reset them. A run that stops at a function not yet reconstructed still
  records its counts.

## Reporting

When you finish a work package, report: functions reconstructed (count and names), verification
results per scenario, functions not exercised by any scenario, deviations or doubts, and new
globals/struct fields added.
