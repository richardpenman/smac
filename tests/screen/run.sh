#!/bin/bash
# Scripted checks of smacx at screen sizes other than 1024 x 768 (docs/UI_PLAN.md section 14): the
# main interface centred at the bottom as MainInterface_init places it, the map beside it (the
# fix of compat wide_console_strips; black under --original-bugs, as the original), clicks on the
# console's buttons, a popup's rows and the base window landing where the windows are drawn, and
# the opening picture stretched over the screen (change_opening); then the display settings'
# computation (smacx_unit's display tests) and, when Xvfb and tools/venv are there, an interactive
# start on a 3840 x 2160 display (the ini's auto size and scale: 1920 x 1080 shown at 2).
#
#   tests/screen/run.sh [OUTDIR]        (SMACX=path/to/smacx, default build/g-native/smacx)
#
# Sizes: 1024x768 (the reference: every other size is compared with it), 1280x1024, 1366x768,
# 1920x1080, 2560x1440 (SIZES="..." to choose). The windows anchored at the bottom centre (the
# console, popups, the base window) move by ((W - 1024) / 2, H - 768) from their 1024 x 768
# places, so the scripted clicks are the 1024 x 768 ones moved by that. Exit status 0 when every
# check passes.
set -u
# Animated by default (smacx --animate: slides, unit moves, booms, battle reports and movies play
# with their waits, as in an interactive game); TEST_FAST=1 runs unattended (skips them).
if [ "${TEST_FAST:-0}" = 1 ]; then export SMACX_ANIMATE=0; else export SMACX_ANIMATE=1; fi
# No audio device in the tests (sounds still run through the mixer); set SDL_AUDIODRIVER to override.
export SDL_AUDIODRIVER="${SDL_AUDIODRIVER:-dummy}"
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SMACX="$(realpath "${SMACX:-$ROOT/build/g-native/smacx}")"
OUT="$(realpath -m "${1:-$ROOT/work/screen_test}")"
SIZES="${SIZES:-1024x768 1280x1024 1366x768 1920x1080 2560x1440}"
LAYOUT="$ROOT/tests/screen/layout.py"
mkdir -p "$OUT"
FAIL=0

run() {  # run CASE SIZE ARGS... : smacx at SIZE, log in OUT/CASE.log
    local name="$1" size="$2"; shift 2
    rm -rf "$OUT/$name"
    mkdir -p "$OUT/$name"
    (cd "$ROOT" && SDL_VIDEODRIVER=dummy timeout 600 "$SMACX" --hidden --size "$size" --outdir "$OUT/$name" "$@" \
        > "$OUT/$name.log" 2>&1)
    echo "$?" > "$OUT/$name.rc"
}
check() {  # check WHAT GOT WANT
    if [ "$2" == "$3" ]; then echo "PASS $1"; else echo "FAIL $1: got '$2' want '$3'"; FAIL=1; fi
}
has() {  # has CASE WHAT REGEX : the log matches
    if grep -aqE "$3" "$OUT/$1.log"; then echo "PASS $1: $2"; else echo "FAIL $1: $2 (/$3/)"; FAIL=1; fi
}
above() {  # above WHAT VALUE MIN : VALUE >= MIN (decimals)
    if python3 -c "import sys; sys.exit(0 if float('$2') >= float('$3') else 1)"; then
        echo "PASS $1 ($2)"
    else
        echo "FAIL $1: $2 below $3"; FAIL=1
    fi
}
nocrash() {
    if ! grep -aq "is not reconstructed yet\|Aborted\|abort" "$OUT/$1.log"; then
        echo "PASS $1: ran to the end"
    else
        echo "FAIL $1: $(grep -am1 'not reconstructed yet\|abort' "$OUT/$1.log")"
        FAIL=1
    fi
}
at() {  # at X Y : the 1024 x 768 position X,Y moved to the current size (bottom centre anchored)
    echo "$((X + $1))x$((Y + $2))"
}

# 1. The main screen on turn_147 as faction 3 (its first popup answered): the screen; the MENU
#    button (the menu column at the console's left); the info pane's M and I buttons (the message
#    list, the info pane); the base window opened, its OK clicked (closed); the quit question's
#    "Yes" row clicked (answer 1: the game ends). The same with --original-bugs for the strips.
for size in $SIZES; do
    W=${size%x*}
    H=${size#*x}
    X=$(((W - 1024) / 2))
    Y=$((H - 768))
    keys="DOWN,ENTER,SHOT:map.bmp,CLICK:$(at 64 538),SHOT:menu.bmp,ESC,CLICK:$(at 745 596),INFOSTATE,\
CLICK:$(at 745 663),INFOSTATE,BASE:0,BASESTATE,SHOT:base.bmp,CLICK:$(at 650 524),BASESTATE,C-q,SHOT:popup.bmp,\
CLICK:$(at 439 449)"
    run "main$size" "$size" --save work/saves_late/turn_147.sav --human 3 --keys "$keys"
    run "orig$size" "$size" --original-bugs --save work/saves_late/turn_147.sav --human 3 --keys "DOWN,ENTER,SHOT:map.bmp"
    c="main$size"
    nocrash "$c"
    has "$c" "MENU clicked: the menu column at the console" "^menu column shown at $X,"
    has "$c" "M clicked: the message list" "^infostate mode 1000"
    has "$c" "I clicked: the info pane" "^infostate mode 1001"
    has "$c" "base window opened" "^basestate base 0 "
    has "$c" "base window's OK clicked: closed" "^basestate closed"
    # (Popup_show: centred over the console, ((W - width + 32) / 2), its bottom on the console's top.)
    pw=$(grep -a "^popup REALLYQUIT shown" "$OUT/$c.log" | sed 's/.*shown: \([0-9]*\)x\([0-9]*\) at.*/\1 \2/')
    set -- $pw 0 0
    has "$c" "quit question placed above the console" "^popup REALLYQUIT shown: ${1}x$2 at $(((W - $1 + 32) / 2)),$((H - 213 - $2))"
    has "$c" "its Yes row clicked" "^popup REALLYQUIT answer: 1"
    if [ "$size" != 1024x768 ]; then
        r=$(python3 "$LAYOUT" console "$OUT/$c/map.bmp" "$OUT/main1024x768/map.bmp" | awk '{print $3}')
        above "$c: the console centred at the bottom (as at 1024 x 768)" "$r" 0.97
    fi
    if [ "$W" -gt 1024 ]; then
        # (The map's own black, beyond its edges and where unexplored, keeps its grid lines.)
        n=$(python3 "$LAYOUT" strips "$OUT/$c/map.bmp" | awk '{print $5 / $7}')
        above "$c: the map beside the console (nonblack share of the strips)" "$n" 0.005
        n=$(python3 "$LAYOUT" strips "$OUT/orig$size/map.bmp" | awk '{print $5}')
        check "orig$size: black strips beside the console under --original-bugs" "$n" 0
    fi
done

# 2. The opening screens: the opening picture stretched over the screen (change_opening's
#    Buffer_copy_3); compared left of the menu column with the 1024 x 768 picture stretched.
for size in $SIZES; do
    W=${size%x*}
    run "menu$size" "$size" --original-bugs --keys "MENUSTATE,SHOT:menu.bmp,DOWN,DOWN,DOWN,DOWN,DOWN,DOWN,ENTER"
    has "menu$size" "the main menu" '^menustate caption="" selected=0 \[START GAME\]'
    if [ "$size" != 1024x768 ] && [ -f "$OUT/menu1024x768/menu.bmp" ]; then
        # (Left of both screens' menu columns: 600 of the 1024 x 768 picture's columns.)
        r=$(python3 "$LAYOUT" stretch "$OUT/menu$size/menu.bmp" "$OUT/menu1024x768/menu.bmp" $((600 * W / 1024)) | awk '{print $3}')
        above "menu$size: the opening picture stretched" "$r" 0.99
    fi
done

# 3. The auto size and scale (platform/display.h): smacx_unit's display tests, when built.
UNIT="$(dirname "$SMACX")/smacx_unit"
if [ -x "$UNIT" ]; then
    if "$UNIT" -tc="display*" > "$OUT/unit.log" 2>&1; then echo "PASS unit: display tests"
    else echo "FAIL unit: display tests (see $OUT/unit.log)"; FAIL=1; fi
else
    echo "SKIP unit: no $UNIT"
fi

# 4. Interactive play on a 3840 x 2160 display (Xvfb; no --hidden, no keys: the ini's display
#    settings, here the defaults): 1920 x 1080 shown at 2 in fullscreen, every logical pixel a 2 x 2
#    block; a click of the display at twice a popup row's position answers it (the mouse mapped to
#    the frame); Alt+Enter leaves fullscreen and saves Fullscreen=0. (--seed: the same game at every
#    run; an interactive game is seeded from the clock, and its first popup varies.)
PY="$ROOT/tools/venv/bin/python"
if command -v Xvfb > /dev/null && [ -x "$PY" ]; then
    c=xvfb4k
    rm -rf "$OUT/$c"
    mkdir -p "$OUT/$c"
    disp=":$((500 + RANDOM % 400))"
    Xvfb "$disp" -screen 0 3840x2160x24 > /dev/null 2>&1 &
    xpid=$!
    sleep 1
    (cd "$ROOT" && DISPLAY=$disp exec timeout 120 "$SMACX" --outdir "$OUT/$c" --seed 0,0 --save work/saves_late/turn_147.sav --human 3 \
        > "$OUT/$c.log" 2>&1) &
    spid=$!
    for i in $(seq 60); do  # the call shown (at most a minute), then a moment for its first frames
        grep -aq "^popup COMM shown" "$OUT/$c.log" 2> /dev/null && break
        sleep 1
    done
    sleep 3
    # The computer player's call (COMM, 888 x 343 at 532,524 on 1920 x 1080; its second row, "No",
    # at 960,756, its OK at 960,810) clicked at twice their places; then Alt+Enter.
    DISPLAY=$disp "$PY" "$ROOT/tests/screen/xinput.py" "$OUT/$c" "shot screen" "click 1920 1512" "click 1920 1620" \
        "wait 2" "key A+Return" "wait 2" > "$OUT/$c.xinput.log" 2>&1
    kill $spid 2> /dev/null; sleep 1; kill -9 $spid 2> /dev/null
    kill $xpid 2> /dev/null
    has $c "auto: 1920 x 1080 at 2, fullscreen" "^display: 1920x1080 at 2 \(fullscreen, 3840x2160"
    n=$(python3 "$LAYOUT" scale2 "$OUT/$c/screen.png" | awk '{print $3 / $5}')
    above "$c: every logical pixel a 2 x 2 block" "$n" 0.999
    check "$c: the popup's row and OK clicked at twice their places" \
        "$(grep -a "^popup COMM answer\|^quit requested" "$OUT/$c.log" | head -1)" "popup COMM answer: 1 (button 0)"
    check "$c: Alt+Enter saved Fullscreen=0" "$(grep -a '^Fullscreen=' "$OUT/$c/Alpha Centauri.Ini" | tr -d '\r')" "Fullscreen=0"
else
    echo "SKIP xvfb4k: no Xvfb or tools/venv"
fi

exit $FAIL
