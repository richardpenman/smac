#!/bin/bash
# A long scripted session (manual test, not one of the tests/*/run.sh suites): a new game played
# for about 45 turns with animations on (unit moves, go-to drags, the base window and production
# picker, menus, reports F2..F7 and F12, the map's right-click menu, info pane clicks, view mode,
# zoom, waits, end of turn), meant for the ASan build, to catch memory errors that only show in a
# long interactive game.
#
#   tests/long/session.sh [OUTDIR] [SEED...]    (OPENSMAC=path/to/opensmac, default build/bug-asan/opensmac)
#
# Each seed (default 1,2 5,9 11,3) is one game; it passes when OpenSMAC ends normally at the end of
# its keys and its log has no sanitizer report. long.keys was generated once (random actions,
# python random.seed(7)) and is kept as is so that runs are comparable. About 5-10 minutes per
# seed under ASan.
set -u
export SDL_AUDIODRIVER="${SDL_AUDIODRIVER:-dummy}" SDL_VIDEODRIVER=dummy OPENSMAC_ANIMATE=1
export ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=0}"
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OPENSMAC="$(realpath "${OPENSMAC:-$ROOT/build/bug-asan/opensmac}")"
OUT="$(realpath -m "${1:-$ROOT/work/long_test}")"
shift || true
SEEDS="${*:-1,2 5,9 11,3}"
FAIL=0
for seed in $SEEDS; do
    d="$OUT/seed_${seed/,/_}"
    rm -rf "$d"
    mkdir -p "$d"
    (cd "$ROOT" && timeout 3600 "$OPENSMAC" --hidden --outdir "$d" --new size=0 --human 1 --seed "$seed" \
        --keys "$(cat "$ROOT/tests/long/long.keys")" > "$d.log" 2>&1)
    rc=$?
    if [ $rc = 0 ] && grep -q "scripted keys done" "$d.log" && ! grep -qE "AddressSanitizer|runtime error" "$d.log"; then
        echo "PASS seed $seed: $(grep -o 'scripted keys done (turn [0-9]*)' "$d.log")"
    else
        echo "FAIL seed $seed: exit $rc ($(grep -m1 -E 'SUMMARY|runtime error' "$d.log"))"
        FAIL=1
    fi
done
exit $FAIL
