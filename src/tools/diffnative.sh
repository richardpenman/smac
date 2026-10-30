#!/bin/bash
# Native against a frozen baseline (docs/CLEANUP_PLAN.md section 2, increment 18): plays the same
# games with a baseline build of opensmac_headless and with the build under test, and compares their
# snapshots (harness/compare.py: every turn's state, generators, game_end). No Wine, so any number
# of new seeds and saved games can be played, beyond the 29 references of work/ref.
#
#   src/tools/diffnative.sh [options]
#
#   --baseline REV|DIR  the baseline: a directory holding opensmac_headless (smacx_headless before
#                       the rename to OpenSMAC), or a git revision built (that target only,
#                       -j$JOBS) into build/baseline-<commit> on first use
#                       (default: the tag hybrid-final)
#   --build DIR         the build under test (default build/native)
#   --seeds N           N new games (default 6), each with its own seeds, map size (0-2),
#                       difficulty and random leaders; --first K starts at game K (default 1), so
#                       --first 7 --seeds 6 plays six other games
#   --turns T           turns per new game and per saved game (default 30)
#   --save FILE         also a game loaded from FILE, T turns with seeds of its own (repeatable)
#   --scenarios LIST    also these harness scenarios, as harness/scenarios/*.cmd ("all": every one;
#                       `$veh` in a scenario reaches the baseline as the ids the new build's
#                       veh_init calls returned, as older baselines do not parse it)
#   --fixes             play with the original's bugs fixed (the game's default) instead of
#                       --original-bugs; the default compares the original's behaviour, as every
#                       comparison against the original or an original-behaviour baseline must
#   --keep              keep the snapshots (<output>/<game>/{base,new}; a differing game's are
#                       always kept)
#
# Runs one game at a time. Exit status 0 when every game matches, 1 otherwise; the summary is
# written to work/diffnative/summary.txt. Env: JOBS (default 2) for the baseline build;
# DIFFNATIVE_OUT (default work/diffnative) the output directory (emptied first).
set -u
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT" || exit 2
BASELINE="hybrid-final"
BUILD="build/native"
SEEDS=6
FIRST=1
TURNS=30
SAVES=()
SCENARIOS=""
BUGS="--original-bugs"
KEEP=0
JOBS="${JOBS:-2}"
while [ $# -gt 0 ]; do
    case "$1" in
    --baseline) BASELINE="$2"; shift ;;
    --build) BUILD="$2"; shift ;;
    --seeds) SEEDS="$2"; shift ;;
    --first) FIRST="$2"; shift ;;
    --turns) TURNS="$2"; shift ;;
    --save) SAVES+=("$(realpath "$2")"); shift ;;
    --scenarios) SCENARIOS="$2"; shift ;;
    --fixes) BUGS="" ;;
    --keep) KEEP=1 ;;
    -h|--help) sed -n '2,/^set -u/p' "$0" | sed '$d; s/^# \{0,1\}//'; exit 0 ;;
    *) echo "diffnative.sh: unknown argument $1"; exit 2 ;;
    esac
    shift
done
[ "$SCENARIOS" = all ] && SCENARIOS="$(ls harness/scenarios/*.cmd | xargs -n1 basename | sed 's/\.cmd$//' | tr '\n' ' ')"

# ---- the two builds ----
# The headless binary in a build directory: opensmac_headless, or smacx_headless in a build of a
# revision before the rename to OpenSMAC (the tag hybrid-final is one).
headless_in() {  # headless_in DIR
    if [ -x "$1/opensmac_headless" ]; then echo "$1/opensmac_headless"; else echo "$1/smacx_headless"; fi
}
NEW="$(realpath "$BUILD")/opensmac_headless"
[ -x "$NEW" ] || { echo "diffnative.sh: no $NEW (build it first)"; exit 2; }
if [ -x "$(headless_in "$BASELINE")" ]; then
    BASE="$(headless_in "$(realpath "$BASELINE")")"
else
    COMMIT="$(git rev-parse --verify --quiet "$BASELINE^{commit}")" || {
        echo "diffnative.sh: $BASELINE is neither a build directory nor a git revision"
        [ "$BASELINE" = hybrid-final ] && echo "  (the tag hybrid-final marks the hybrid freeze; give --baseline REV or DIR)"
        exit 2
    }
    BDIR="$ROOT/build/baseline-${COMMIT:0:12}"
    BASE="$(headless_in "$BDIR")"
    if [ ! -x "$BASE" ]; then
        SRC="$ROOT/work/baseline-src-${COMMIT:0:12}"
        echo "building the baseline ${COMMIT:0:12} ($BASELINE) into $BDIR ..."
        rm -rf "$SRC"
        git worktree add --detach "$SRC" "$COMMIT" > /dev/null 2>&1 || { echo "diffnative.sh: git worktree add failed"; exit 2; }
        target=opensmac_headless
        grep -q "opensmac_headless" "$SRC/CMakeLists.txt" || target=smacx_headless  # before the rename
        if ! { cmake -S "$SRC" -B "$BDIR" -DCMAKE_BUILD_TYPE=RelWithDebInfo &&
               cmake --build "$BDIR" -j"$JOBS" --target "$target"; } > "$ROOT/work/baseline-${COMMIT:0:12}.log" 2>&1; then
            echo "diffnative.sh: the baseline build failed (work/baseline-${COMMIT:0:12}.log)"
            git worktree remove --force "$SRC"; exit 2
        fi
        git worktree remove --force "$SRC"
        BASE="$(headless_in "$BDIR")"
    fi
fi
[ "$BASE" -ef "$NEW" ] && echo "diffnative.sh: warning: the baseline and the build under test are the same binary"

# ---- the games ----
OUT="$(realpath -m "${DIFFNATIVE_OUT:-$ROOT/work/diffnative}")"
rm -rf "$OUT"
mkdir -p "$OUT/cmd"
GAMES=()
for ((k = FIRST; k < FIRST + SEEDS; k++)); do  # new games: seeds and settings derived from k
    g="seed$k"
    printf "seed %d %d\nnewgame size=%d diff=%d random=%d\nturns %d\n" $((7000 + k * 7)) $((9000 + k * 13)) \
        $((k % 3)) $((k % 6)) $((k % 4)) "$TURNS" > "$OUT/cmd/$g.cmd"
    GAMES+=("$g")
done
i=0
for f in "${SAVES[@]+"${SAVES[@]}"}"; do
    i=$((i + 1)); g="save$i"
    printf "load %s\nseed %d %d\nturns %d\n" "$f" $((5000 + i * 11)) $((6000 + i * 17)) "$TURNS" > "$OUT/cmd/$g.cmd"
    GAMES+=("$g")
done
for s in $SCENARIOS; do
    cp "harness/scenarios/$s.cmd" "$OUT/cmd/$s.cmd"
    GAMES+=("$s")
done
[ ${#GAMES[@]} -gt 0 ] || { echo "diffnative.sh: no games"; exit 2; }

SUMMARY="$OUT/summary.txt"
FAIL=0
for g in "${GAMES[@]}"; do
    d="$OUT/$g"
    mkdir -p "$d/base" "$d/new"
    "$NEW" "$OUT/cmd/$g.cmd" --outdir "$d/new" $BUGS > "$d/new/headless.log" 2>&1; sn=$?
    cmd="$OUT/cmd/$g.cmd"
    if grep -q '\$veh' "$cmd"; then
        # `$veh` (the id the last `call veh_init` returned) is newer than older baselines' parser:
        # the baseline gets the ids the new build's veh_init calls returned, in order.
        awk 'NR == FNR { if ($0 ~ /^call veh_init .* -> /) ids[++n] = $NF; next }
             /call veh_init / { last = ids[++k] }
             { gsub(/\$veh/, last); print }' "$d/new/oracle.log" "$cmd" > "$d/base.cmd"
        cmd="$d/base.cmd"
    fi
    "$BASE" "$cmd" --outdir "$d/base" $BUGS > "$d/base/headless.log" 2>&1; sb=$?
    python3 harness/compare.py "$d/base" "$d/new" > "$d/compare.log" 2>&1; sc=$?
    n=$(grep -c " OK$" "$d/compare.log")
    nb=$(ls "$d/base" | grep -c "\.state$"); nn=$(ls "$d/new" | grep -c "\.state$")
    [ "$nb" = "$nn" ] || echo "MISSING: $nb snapshots in the baseline's game, $nn in the new one" >> "$d/compare.log"
    if [ $sc = 0 ] && [ $sb = $sn ] && ! grep -qE "DIFF|MISSING" "$d/compare.log" && [ "$n" -gt 0 ]; then
        echo "$g: OK ($n snapshots, exit $sn)" | tee -a "$SUMMARY"
        [ $KEEP = 1 ] || rm -rf "$d"
    else  # kept for inspection
        echo "$g: DIFF (exit $sb / $sn): $(grep -m 1 -E "DIFF|MISSING" "$d/compare.log" | cut -c1-160)" | tee -a "$SUMMARY"
        FAIL=1
    fi
done
echo "diffnative: $([ $FAIL = 0 ] && echo PASS || echo FAIL) (${#GAMES[@]} games, baseline $BASE, ${BUGS:-with fixes}; $SUMMARY)" | tee -a "$SUMMARY"
exit $FAIL
