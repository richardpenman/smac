#!/bin/bash
# Scripted checks of the functions reconstructed by the placeholder sweep (UI_PLAN.md "Remaining
# work"): the map's right-click menu (pulldown.cpp, mapwin_menu.cpp), landmarks, ...
#
#   tests/placeholders/run.sh [OUTDIR]        (SMACX=path/to/smacx, default build/ph-native/smacx)
#
# Each case runs smacx with scripted keys (hidden, SDL dummy video) and checks its log. Exit status
# 0 when every check passes.
set -u
export SDL_AUDIODRIVER="${SDL_AUDIODRIVER:-dummy}"
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SMACX="$(realpath "${SMACX:-$ROOT/build/ph-native/smacx}")"
OUT="$(realpath -m "${1:-$ROOT/work/placeholders_test}")"
mkdir -p "$OUT"
FAIL=0
NEW="--new size=0 --human 1 --seed 1,2"

run() {  # run CASE ARGS... : log in OUT/CASE.log
    local name="$1"; shift
    mkdir -p "$OUT/$name"
    (cd "$ROOT" && SDL_VIDEODRIVER=dummy timeout 600 "$SMACX" --hidden --outdir "$OUT/$name" "$@" > "$OUT/$name.log" 2>&1)
    echo "$?" > "$OUT/$name.rc"
}
check() {  # check CASE DESCRIPTION REGEX: the log matches (grep -E)
    if grep -qE "$3" "$OUT/$1.log"; then echo "PASS $1: $2"; else echo "FAIL $1: $2 (/$3/)"; FAIL=1; fi
}
nocheck() {  # nocheck CASE DESCRIPTION REGEX: the log does not match
    if grep -qE "$3" "$OUT/$1.log"; then echo "FAIL $1: $2 (/$3/ found)"; FAIL=1; else echo "PASS $1: $2"; fi
}
nocrash() {
    if [ "$(cat "$OUT/$1.rc")" = 0 ] && ! grep -q "is not reconstructed yet\|abort" "$OUT/$1.log"; then
        echo "PASS $1: ran to the end"
    else
        echo "FAIL $1: exit $(cat "$OUT/$1.rc")"; FAIL=1
    fi
}

# 1. The right-click menu (MapWin_right_menu; shift + right click without the "right click menu"
#    preference) on the first base's tile: its items; Esc closes it with nothing chosen. Then on a
#    tile beside the scout: "Move unit to here" (id 1) by the arrows gives the go-to order (24).
run rmenu $NEW --keys "ENTER,S-RCLICK:530x200,SHOT:rmenu.bmp,ESC,S-RCLICK:600x260,DOWN,DOWN,DOWN,DOWN,DOWN,DOWN,DOWN,DOWN,DOWN,DOWN,ENTER,UNITS"
check rmenu "menu shown at the click + 1" "pulldown shown: 21 items at 531,201"
check rmenu "base items" "pulldown item 51:  Change production"
check rmenu "hurry with the production" "pulldown item 52:  Hurry Formers"
check rmenu "unit items" "pulldown item 1: Move unit to here"
check rmenu "Esc: nothing chosen" "pulldown answer: -1 \(id -1\)"
check rmenu "move here chosen" "pulldown answer: [0-9]+ \(id 1\)"
check rmenu "go-to order 24" "unit 4 f1 Scout Patrol \([0-9]+,[0-9]+\) order 24"
nocrash rmenu

# 2. Landmarks: shift+N names one (LANDMARK text entry), shift+N again renames it, ctrl+shift+N
#    erases it (LANDMARKKILL).
run landmark $NEW --keys "ENTER,S-n,M,y,SPACE,H,i,l,l,ENTER,SHOT:lm.bmp,S-n,ENTER,C-S-n,ENTER"
check landmark "name entry" "popup LANDMARK shown"
check landmark "erase question" "popup LANDMARKKILL shown"
nocheck landmark "no placeholder" "not reconstructed: Console_name_landmark"
nocrash landmark

# 3. The go-to drag's path preview (MapWin_on_mouse_move -> MapWin_dest_line on DesktopWin): the
#    text with the path's length; the screenshot is taken with the button still held.
run drag $NEW --keys "ENTER,DRAG:530x200:700x330:drag.bmp,UNITS"
check drag "drag text with the distance" "goto drag: Move to \(38, 12\): Dist = 4"
check drag "go-to order 24" "unit 4 f1 Scout Patrol \([0-9]+,[0-9]+\) order 24"
nocrash drag

exit $FAIL
