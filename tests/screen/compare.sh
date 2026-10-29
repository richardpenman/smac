#!/bin/bash
# The original and smacx side by side at screen sizes above 1024 x 768 (tests/screen/run.sh's
# layouts): the opening screen, the main screen's console and the strips beside it, a popup.
#
#   tests/screen/compare.sh OUTDIR [SIZES]        (default SIZES "1280x1024 1920x1080")
#
# The original runs under Wine on an Xvfb screen of the size (tests/popups/orig_drive.py,
# ORIG_SCREEN: without DirectDraw it takes the desktop's size): orig_load.steps (the opening screen,
# then turn_120.sav loaded through LOAD GAME), orig_quit.steps (QUICK START, the quit question).
# smacx plays the same with --original-bugs (the original's black strips beside the console):
# turn_120.sav as observer, a new game's quit question. Their maps and factions differ (the
# original's quick start picks its own faction; the loaded save shows the Hive's turn), so the
# compared regions are the opening picture and menu, the console's top band (the frame and its
# buttons, the map showing through the frame's gaps), the strips beside the console and the popup.
# Writes OUTDIR/SIZE/cmp_<name>.png (original above, smacx in the middle, differing pixels below)
# and prints the matching fractions. Needs Wine, Xvfb and tools/venv.
set -u
export SDL_AUDIODRIVER="${SDL_AUDIODRIVER:-dummy}"
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUT="$(realpath -m "$1")"
SIZES="${2:-1280x1024 1920x1080}"
PY="$ROOT/tools/venv/bin/python"
SMACX="$(realpath "${SMACX:-$ROOT/build/g-native/smacx}")"
for size in $SIZES; do
    W=${size%x*}
    H=${size#*x}
    X=$(((W - 1024) / 2))
    D="$OUT/$size"
    mkdir -p "$D/orig" "$D/smacx"
    if [ ! -f "$D/orig/map.png" ]; then
        ORIG_SCREEN=$size ORIG_SAVE="$ROOT/work/saves_late/turn_120.sav" "$PY" "$ROOT/tests/popups/orig_drive.py" \
            "$ROOT/tests/screen/orig_load.steps" "$D/orig" > "$D/orig_load.log" 2>&1
    fi
    if [ ! -f "$D/orig/quit.png" ]; then
        ORIG_SCREEN=$size "$PY" "$ROOT/tests/popups/orig_drive.py" "$ROOT/tests/screen/orig_quit.steps" "$D/orig" \
            > "$D/orig_quit.log" 2>&1
    fi
    sx() {  # sx NAME ARGS...
        local name="$1"; shift
        (cd "$ROOT" && SDL_VIDEODRIVER=dummy timeout 300 "$SMACX" --original-bugs --hidden --size "$size" \
            --outdir "$D/smacx/$name" "$@" > "$D/smacx/$name.log" 2>&1)
    }
    sx menu --keys "WAIT:2000,SHOT:menu.bmp"
    sx map --save work/saves_late/turn_120.sav --keys "SHOT:map.bmp"
    sx quit --new size=0 --human 1 --seed 1,2 --keys "ENTER,C-q,SHOT:quit.bmp"
    pop=$(grep -a "^popup REALLYQUIT shown" "$D/smacx/quit.log" | sed 's/.*shown: \([0-9]*\)x\([0-9]*\) at \([0-9]*\),\([0-9]*\).*/\1 \2 \3 \4/')
    set -- $pop 0 0 0 0
    for p in "menu menu 0 0 $W $H" "map console $X $((H - 257)) $((X + 1024)) $((H - 213))" \
             "map strip_left 0 $((H - 213)) $X $H" "map strip_right $((X + 1024)) $((H - 213)) $W $H" \
             "quit quit $3 $4 $(($3 + $1)) $(($4 + $2))"; do
        set -- $p
        [ "$4" -lt "$6" ] || continue
        printf '%-10s %-12s ' "$size" "$2"
        "$PY" "$ROOT/tests/popups/cmp.py" "$D/orig/$1.png" "$D/smacx/$1/$1.bmp" "$D/cmp_$2.png" $3 $4 $5 $6 2> /dev/null
        set -- $pop 0 0 0 0
    done
done
