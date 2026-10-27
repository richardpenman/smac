#!/bin/bash
# The cleanup gate (docs/CLEANUP_PLAN.md): every cleanup commit must pass it. Proves that the
# change keeps the game's behaviour identical to the original's.
#
#   src/tools/gate.sh [--quick] [--only STEPS] [--skip STEPS]
#
# Every comparison with the original runs with the original's bugs (--original-bugs, verify.sh;
# the hybrid DLL always has them); the fixes step plays with them fixed.
#
# Steps (in order; STEPS is a comma-separated list):
#   static    src/tools/names.py check (no original address lost, names table current, fcompare
#             entries name declared functions) and src/tools/compat.py check (every BUG(original)
#             site names a declared compat flag, docs/COMPAT.md current)
#   build     native (smacx_headless, smacx, smacx_unit), ASan native, hybrid (i686 mingw)
#   unit      ctest in the native build (smacx_smoke, smacx_unit) and smacx_unit under ASan
#   native    harness/verify.sh native over every harness/scenarios/*.cmd
#   observer  smacx (every UI area replaced) in observer mode (--human 0) against the oracle
#             references: tiny20 small12 std8 late140
#   asan      the ASan smacx_headless on $GATE_ASAN_SCENARIOS (default tiny20 small12 std8 late140
#             council131; --quick: tiny20): no sanitizer report, snapshots match the references
#   fixes     the ASan smacx_headless with the original's bugs fixed (the game's default) on
#             $GATE_FIXES_SCENARIOS (default tiny20 late140 council131 end300; --quick: tiny20):
#             no sanitizer report, the run ends normally (nothing is compared: the game differs)
#   hybrid    harness/verify.sh hybrid "all" on $GATE_HYBRID_SCENARIOS (default tiny20 small12 std8
#             late140; --quick: tiny20). Runs the original under Wine, one process at a time.
#   fcompare  only when GATE_FCOMPARE is set (a patch spec, e.g. "none +vector_dist"): verify.sh
#             fcompare on $GATE_FCOMPARE_SCENARIOS (default tiny20 small12 std8)
#   ui        tests/*/run.sh (scripted smacx checks, animated: slides, moves, booms, movies), each
#             against the native smacx, then again against the ASan smacx (no sanitizer report)
#
# Env: GATE_PREFIX build directory prefix (default build/gate: build/gate-native, -asan, -hybrid);
#      GATE_OUT output directory (default work/gate); GATE_JOBS (default 2).
# The oracle references (work/ref/<scenario>) must exist: the gate never creates them (verify.sh
# would run the original to make a missing one). Exit status 0 when every step passes; the summary
# is also written to $GATE_OUT/summary.txt, each step's log to $GATE_OUT/<step>.log.
set -u
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT" || exit 2
PREFIX="${GATE_PREFIX:-build/gate}"
OUT="$(realpath -m "${GATE_OUT:-$ROOT/work/gate}")"
JOBS="${GATE_JOBS:-2}"
QUICK=0
ONLY=""
SKIP=""
while [ $# -gt 0 ]; do
    case "$1" in
    --quick) QUICK=1 ;;
    --only) ONLY=",$2,"; shift ;;
    --skip) SKIP=",$2,"; shift ;;
    -h|--help) sed -n '2,/^set -u/p' "$0" | sed '$d; s/^# \{0,1\}//'; exit 0 ;;
    *) echo "gate.sh: unknown argument $1"; exit 2 ;;
    esac
    shift
done
if [ $QUICK = 1 ]; then
    ASAN_SCEN="${GATE_ASAN_SCENARIOS:-tiny20}"
    FIXES_SCEN="${GATE_FIXES_SCENARIOS:-tiny20}"
    HYBRID_SCEN="${GATE_HYBRID_SCENARIOS:-tiny20}"
else
    ASAN_SCEN="${GATE_ASAN_SCENARIOS:-tiny20 small12 std8 late140 council131}"
    FIXES_SCEN="${GATE_FIXES_SCENARIOS:-tiny20 late140 council131 end300}"
    HYBRID_SCEN="${GATE_HYBRID_SCENARIOS:-tiny20 small12 std8 late140}"
fi
FC_SCEN="${GATE_FCOMPARE_SCENARIOS:-tiny20 small12 std8}"
OBS_SCEN="tiny20 small12 std8 late140"
ALL_SCEN="$(ls harness/scenarios/*.cmd | xargs -n1 basename | sed 's/\.cmd$//' | tr '\n' ' ')"
NATIVE="$PREFIX-native"
ASAN="$PREFIX-asan"
HYBRID="$PREFIX-hybrid"
rm -rf "$OUT"
mkdir -p "$OUT"
SUMMARY="$OUT/summary.txt"
: > "$SUMMARY"
FAILED=0

want() {  # want STEP: the step is selected
    if [ -n "$ONLY" ]; then [[ "$ONLY" == *",$1,"* ]]; else [[ "$SKIP" != *",$1,"* ]]; fi
}
report() {  # report STEP STATUS DETAIL
    printf "%-9s %-4s %s\n" "$1" "$2" "$3" | tee -a "$SUMMARY"
    [ "$2" = PASS ] || FAILED=1
}
refs_ok() {  # refs_ok SCENARIO...: every oracle reference exists (never created here)
    local s missing=""
    for s in "$@"; do grep -q "exit=0" "work/ref/$s/oracle.log" 2>/dev/null || missing="$missing $s"; done
    [ -z "$missing" ] || { echo "missing oracle references (work/ref):$missing"; return 1; }
}

# ---- static ----------------------------------------------------------------------------------
if want static; then
    if python3 src/tools/names.py check > "$OUT/static.log" 2>&1 && \
        python3 src/tools/compat.py check >> "$OUT/static.log" 2>&1; then
        report static PASS "$(tail -n 2 "$OUT/static.log" | tr '\n' ' ')"
    else
        report static FAIL "see $OUT/static.log"
    fi
fi

# ---- build -----------------------------------------------------------------------------------
build() {  # build DIR LOG CMAKE-ARGS... -- TARGETS...
    local dir="$1" log="$2"; shift 2
    local args=() targets=()
    while [ $# -gt 0 ] && [ "$1" != -- ]; do args+=("$1"); shift; done
    [ $# -gt 0 ] && shift
    targets=("$@")
    { cmake -S . -B "$dir" "${args[@]}" && \
      cmake --build "$dir" -j"$JOBS" ${targets[@]+--target "${targets[@]}"}; } >> "$log" 2>&1
}
if want build; then
    ok=1; what=""
    build "$NATIVE" "$OUT/build.log" -DCMAKE_BUILD_TYPE=RelWithDebInfo || ok=0
    what="native"
    if want asan || want unit || want ui || want fixes; then
        build "$ASAN" "$OUT/build.log" -DCMAKE_BUILD_TYPE=RelWithDebInfo -DSMACX_SANITIZE=ON -- \
            smacx_headless smacx_unit smacx || ok=0
        what="$what asan"
    fi
    if want hybrid || want fcompare; then
        build "$HYBRID" "$OUT/build.log" -G "Unix Makefiles" -DSMACX_HYBRID=ON \
            -DCMAKE_TOOLCHAIN_FILE="$ROOT/cmake/i686-mingw.cmake" || ok=0
        what="$what hybrid"
    fi
    if [ $ok = 1 ]; then report build PASS "$what ($PREFIX-*)"; else report build FAIL "see $OUT/build.log"; fi
fi

# ---- unit ------------------------------------------------------------------------------------
if want unit; then
    ok=1
    ctest --test-dir "$NATIVE" --output-on-failure > "$OUT/unit.log" 2>&1 || ok=0
    if [ -x "$ASAN/smacx_unit" ]; then
        "$ASAN/smacx_unit" >> "$OUT/unit.log" 2>&1 || ok=0
        grep -qE "AddressSanitizer|runtime error" "$OUT/unit.log" && ok=0
    fi
    n=$(grep -o "test cases: *[0-9]*" "$OUT/unit.log" | head -n 1 | tr -s " ")
    if [ $ok = 1 ]; then report unit PASS "ctest + ASan smacx_unit ($n)"; else report unit FAIL "see $OUT/unit.log"; fi
fi

# ---- native ----------------------------------------------------------------------------------
# verify.sh prints "[scenario]" and one "OK"/"DIFF" line per snapshot; counts them per scenario.
count_verify() {  # count_verify LOG: "N scenarios, M snapshots OK" and FAIL lines
    awk '/^\[/ { n++ } / OK$/ { ok++ } /DIFF|MISSING|FAIL|CRASH|not reconstructed|exit=[1-9]/ { bad++ }
         END { printf "%d scenarios, %d snapshots OK, %d problems", n, ok, bad }' "$1"
}
if want native; then
    # shellcheck disable=SC2086
    if refs_ok $ALL_SCEN > "$OUT/native.log" && \
        NATIVE_BUILD="$NATIVE" harness/verify.sh native $ALL_SCEN >> "$OUT/native.log" 2>&1 && \
        ! grep -qE "DIFF|MISSING|FAIL|CRASH|not reconstructed|exit=[1-9]" "$OUT/native.log"; then
        report native PASS "$(count_verify "$OUT/native.log")"
    else
        report native FAIL "$(count_verify "$OUT/native.log"); see $OUT/native.log"
    fi
fi

# ---- observer --------------------------------------------------------------------------------
smacx_args() {  # smacx_args SCENARIO: smacx options for a seed/newgame/load/turns scenario
    awk '$1 == "seed" { printf "--seed %s,%s ", $2, $3 }
         $1 == "newgame" { $1 = ""; printf "--new%s ", $0 }
         $1 == "load" { printf "--save %s ", $2 }
         $1 == "turns" { printf "--turns %s ", $2 }' "harness/scenarios/$1.cmd"
}
if want observer; then
    ok=1; : > "$OUT/observer.log"
    # shellcheck disable=SC2086
    refs_ok $OBS_SCEN >> "$OUT/observer.log" || ok=0
    for s in $OBS_SCEN; do
        [ $ok = 1 ] || break
        d="$OUT/observer/$s"
        mkdir -p "$d"
        echo "[$s]" >> "$OUT/observer.log"
        # shellcheck disable=SC2046
        SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy timeout 1800 "$NATIVE/smacx" --hidden --nosound --human 0 \
            --original-bugs --outdir "$d" --snapshots "$d/snap" $(smacx_args "$s") > "$d/smacx.log" 2>&1
        python3 harness/compare.py "work/ref/$s" "$d/snap" >> "$OUT/observer.log" 2>&1 || ok=0
    done
    grep -qE "DIFF|MISSING" "$OUT/observer.log" && ok=0
    if [ $ok = 1 ]; then report observer PASS "smacx --human 0: $OBS_SCEN ($(grep -c ' OK$' "$OUT/observer.log") snapshots)"
    else report observer FAIL "see $OUT/observer.log"; fi
fi

# ---- asan ------------------------------------------------------------------------------------
if want asan; then
    ok=1; : > "$OUT/asan.log"
    # shellcheck disable=SC2086
    refs_ok $ASAN_SCEN >> "$OUT/asan.log" || ok=0
    for s in $ASAN_SCEN; do
        [ $ok = 1 ] || break
        d="$OUT/asan/$s"
        mkdir -p "$d"
        echo "[$s]" >> "$OUT/asan.log"
        ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=0}" timeout 3600 "$ASAN/smacx_headless" \
            "harness/scenarios/$s.cmd" --outdir "$d" --original-bugs > "$d/headless.log" 2>&1
        if grep -qE "AddressSanitizer|runtime error|LeakSanitizer" "$d/headless.log"; then
            grep -m 5 -E "AddressSanitizer|runtime error" "$d/headless.log" >> "$OUT/asan.log"; ok=0
        fi
        python3 harness/compare.py "work/ref/$s" "$d" >> "$OUT/asan.log" 2>&1 || ok=0
    done
    grep -qE "DIFF|MISSING" "$OUT/asan.log" && ok=0
    if [ $ok = 1 ]; then report asan PASS "$ASAN_SCEN ($(grep -c ' OK$' "$OUT/asan.log") snapshots)"
    else report asan FAIL "see $OUT/asan.log"; fi
fi

# ---- fixes: the game with the original's bugs fixed, under ASan ----------------------------------
if want fixes; then
    ok=1; : > "$OUT/fixes.log"
    for s in $FIXES_SCEN; do
        d="$OUT/fixes/$s"
        mkdir -p "$d"
        ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=0}" timeout 3600 "$ASAN/smacx_headless" \
            "harness/scenarios/$s.cmd" --outdir "$d" > "$d/headless.log" 2>&1
        st=$?
        n=$(ls "$d"/turn_*.state 2>/dev/null | wc -l)
        echo "[$s] exit $st, $n snapshots" >> "$OUT/fixes.log"
        [ $st = 0 ] || ok=0
        if grep -qE "AddressSanitizer|runtime error|LeakSanitizer|compat:" "$d/headless.log"; then
            grep -m 5 -E "AddressSanitizer|runtime error|compat:" "$d/headless.log" >> "$OUT/fixes.log"; ok=0
        fi
    done
    if [ $ok = 1 ]; then report fixes PASS "$FIXES_SCEN"
    else report fixes FAIL "see $OUT/fixes.log"; fi
fi

# ---- hybrid / fcompare (Wine) ----------------------------------------------------------------
if want hybrid; then
    # shellcheck disable=SC2086
    if refs_ok $HYBRID_SCEN > "$OUT/hybrid.log" && \
        HYBRID_BUILD="$HYBRID" harness/verify.sh hybrid all $HYBRID_SCEN >> "$OUT/hybrid.log" 2>&1 && \
        ! grep -qE "DIFF|MISSING|FAIL|CRASH|not reconstructed|exit=[1-9]" "$OUT/hybrid.log"; then
        report hybrid PASS "all patches: $(count_verify "$OUT/hybrid.log")"
    else
        report hybrid FAIL "$(count_verify "$OUT/hybrid.log"); see $OUT/hybrid.log"
    fi
fi
if want fcompare && [ -n "${GATE_FCOMPARE:-}" ]; then
    # shellcheck disable=SC2086
    if HYBRID_BUILD="$HYBRID" harness/verify.sh fcompare "$GATE_FCOMPARE" $FC_SCEN > "$OUT/fcompare.log" 2>&1; then
        report fcompare PASS "\"$GATE_FCOMPARE\" on $FC_SCEN"
    else
        report fcompare FAIL "see $OUT/fcompare.log"
    fi
fi

# ---- ui --------------------------------------------------------------------------------------
if want ui; then
    ok=1; failed=""
    for t in tests/*/run.sh; do
        name="$(basename "$(dirname "$t")")"
        if ! SMACX="$NATIVE/smacx" "$t" "$OUT/ui/$name" > "$OUT/ui_$name.log" 2>&1; then
            ok=0; failed="$failed $name"
        fi
    done
    # The suites again (animated, their default) against the ASan smacx: every check and no
    # sanitizer report in any case's log.
    asan_bad=""
    for t in tests/*/run.sh; do
        name="$(basename "$(dirname "$t")")"
        if ! ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=0}" SMACX="$ASAN/smacx" "$t" "$OUT/ui_asan/$name" \
            > "$OUT/ui_asan_$name.log" 2>&1; then
            ok=0; failed="$failed asan:$name"
        fi
        if grep -lE "AddressSanitizer|runtime error" "$OUT/ui_asan/$name"/*.log >> "$OUT/ui_asan_reports.txt" \
            2>/dev/null; then
            ok=0; asan_bad="$asan_bad $name"
        fi
    done
    pass=$(cat "$OUT"/ui_*.log | grep -c "^PASS")
    fail=$(cat "$OUT"/ui_*.log | grep -c "^FAIL")
    if [ $ok = 1 ]; then report ui PASS "tests/*/run.sh, native and ASan: $pass checks"
    else report ui FAIL "$fail of $((pass + fail)) checks failed in:$failed${asan_bad:+; sanitizer reports in:$asan_bad (see $OUT/ui_asan_reports.txt)} (see $OUT/ui_*.log)"; fi
fi

echo "gate: $([ $FAILED = 0 ] && echo PASS || echo FAIL) (summary: $SUMMARY)"
exit $FAILED
