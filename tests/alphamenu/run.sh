#!/bin/bash
# The main view's menus as a player meets them (src/ui/menu.cpp: AlphaMenu, the column of menu
# buttons above the console's MENU button, and its pull-downs; docs/UI_PLAN.md section 5): smacx
# with a window (SDL dummy video, not --hidden) and the animations on (the column's slide, its
# sounds), driven by the mouse and the keys.
#
#   tests/alphamenu/run.sh [OUTDIR]        (SMACX=path/to/smacx, default build/menu-native/smacx)
#
# Checks the log and the screenshots (the column's pixels on the screen, not only its state).
# Exit status 0 when every check passes.
set -u
export SDL_AUDIODRIVER="${SDL_AUDIODRIVER:-dummy}"
export SMACX_ANIMATE=1
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SMACX="$(realpath "${SMACX:-$ROOT/build/menu-native/smacx}")"
OUT="$(realpath -m "${1:-$ROOT/work/alphamenu_test}")"
PY="$ROOT/tools/venv/bin/python"
mkdir -p "$OUT"
FAIL=0
NEW="--new size=0 --human 1 --seed 1,2"
MENU=64x538  # the console's MENU button (console2_A.pcx at the screen's bottom, button 0x3EB)

run() {  # run CASE ARGS... : log in OUT/CASE.log, screenshots in OUT/CASE/
    local name="$1"; shift
    rm -rf "$OUT/$name"
    mkdir -p "$OUT/$name"
    (cd "$ROOT" && SDL_VIDEODRIVER=dummy timeout 600 "$SMACX" --original-bugs --exit-after-keys --outdir "$OUT/$name" "$@" > "$OUT/$name.log" 2>&1)
    echo "$?" > "$OUT/$name.rc"
}
check() {  # check CASE DESCRIPTION REGEX: the log matches (grep -E)
    if grep -qE "$3" "$OUT/$1.log"; then echo "PASS $1: $2"; else echo "FAIL $1: $2 (/$3/)"; FAIL=1; fi
}
order() {  # order CASE DESCRIPTION REGEX1 REGEX2: a line matching REGEX2 follows one matching REGEX1
    if awk -v a="$3" -v b="$4" '$0 ~ a { s = 1 } s && $0 ~ b { f = 1 } END { exit !f }' "$OUT/$1.log"; then
        echo "PASS $1: $2"
    else
        echo "FAIL $1: $2 (/$3/ then /$4/)"; FAIL=1
    fi
}
nocrash() {
    if [ "$(cat "$OUT/$1.rc")" = 0 ] && ! grep -q "is not reconstructed yet\|abort" "$OUT/$1.log"; then
        echo "PASS $1: ran to the end"
    else
        echo "FAIL $1: exit $(cat "$OUT/$1.rc")"; FAIL=1
    fi
}
# pixels CASE DESCRIPTION SHOT_A SHOT_B X0 Y0 X1 Y1 SAME(0/1): the region of two screenshots differs
# (0) / is the same (1).
pixels() {
    local r
    r=$("$PY" - "$OUT/$1/$3" "$OUT/$1/$4" "$5" "$6" "$7" "$8" <<'EOF'
import sys
from PIL import Image
a, b = Image.open(sys.argv[1]).convert('RGB'), Image.open(sys.argv[2]).convert('RGB')
box = tuple(int(v) for v in sys.argv[3:7])
print(1 if a.crop(box).tobytes() == b.crop(box).tobytes() else 0)
EOF
)
    if [ "$r" = "$9" ]; then echo "PASS $1: $2"; else echo "FAIL $1: $2 ($3 / $4 at $5,$6..$7,$8)"; FAIL=1; fi
}
COLUMN="0 343 139 511"  # the column (AlphaMenu_adjust: 139x168, its bottom on the console's top)

# 1. The mouse: the MENU button shows the column (sliding in), the HQ button (the second) opens its
#    pull-down at the column's right, a click on its first row runs Social Engineering (0x105; the
#    column stays), the MENU button hides the column.
run mouse $NEW --keys "ENTER,SHOT:before.bmp,CLICK:$MENU,SHOT:column.bmp,MOVE:70x376,SHOT:hover.bmp,\
CLICK:70x376,SHOT:pulldown.bmp,CLICK:200x390,SCREENSTATE,ESC,SHOT:after.bmp,CLICK:$MENU,SHOT:hidden.bmp"
check mouse "MENU shows the column, sliding, with its sound" "^alphamenu show \(sound 1, slide 1\)"
check mouse "the column above the console" "^menu column shown at 0,343 \(139x168\)"
check mouse "HQ's pull-down at the column's right" "^menu column at 0,343 \(139x168\): HQ at 0,367; pull-down at 140,380"
check mouse "the first row chosen: Social Engineering" "^menu command 105$"
check mouse "the social engineering screen" "^screenstate social"
order mouse "the MENU button again hides the column" "^menu command 105$" "^menu column hidden$"
pixels mouse "the column drawn on the screen" before.bmp column.bmp $COLUMN 0
pixels mouse "the hovered button drawn" column.bmp hover.bmp $COLUMN 0
pixels mouse "the column still shown after the command" column.bmp after.bmp $COLUMN 1
pixels mouse "the column gone after MENU" before.bmp hidden.bmp $COLUMN 1
nocrash mouse

# 2. The keys: Alt+G opens the GAME menu (the column shown), Esc closes the pull-down (the column
#    stays), Alt+M the MAP menu (the fourth button: NETWORK is shown too), Right the next menu
#    (ACTION), Esc, Esc hides the column; Alt alone shows it (Esc hides it); Alt+H is HQ's (before
#    HELP's), Enter runs its first item.
run keys $NEW --keys "ENTER,A-g,SHOT:game.bmp,ESC,SHOT:column.bmp,A-m,RIGHT,ESC,ESC,SHOT:hidden.bmp,\
ALT,SHOT:alt.bmp,ESC,A-h,ENTER,SCREENSTATE,ESC"
check keys "Alt+G: GAME" "^menu column at 0,343 \(139x168\): Game at 0,349; pull-down at 140,362"
check keys "Alt+M: MAP, the fourth button" "^menu column at 0,343 \(139x168\): Map at 0,403"
check keys "Right: ACTION" "^menu column at 0,343 \(139x168\): Action at 0,421"
check keys "Esc closes the pull-down" "^menu pull-down closed$"
order keys "Esc, Esc hides the column" "Action at 0,421" "^menu column hidden$"
check keys "Alt+H: HQ" "^menu column at 0,343 \(139x168\): HQ at 0,367"
check keys "Enter: Social Engineering" "^menu command 105$"
pixels keys "the column after Esc (the pull-down closed)" game.bmp column.bmp 0 343 139 511 0
pixels keys "the column after Esc: no pull-down" hidden.bmp column.bmp 140 343 420 700 1
pixels keys "Alt shows the column" hidden.bmp alt.bmp $COLUMN 0
nocrash keys

exit $FAIL
