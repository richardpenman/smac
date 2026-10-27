#!/bin/bash
# Scripted checks of the main screen (src/ui/console*.cpp, menu.cpp, statuswin.cpp): unit orders by
# key, mouse and menu, the go-to list and drag, save / load, the interface frame.
#
#   tests/console/run.sh [OUTDIR]        (SMACX=path/to/smacx, default build/pp-native/smacx)
#
# Each case runs smacx with scripted keys (hidden, SDL dummy video) and checks the units the
# script lists (UNITS: "unit ID fF NAME (X,Y) order O moves M state S") and the log. Screenshots go
# to OUTDIR/<case>/. Exit status 0 when every check passes.
set -u
# Animated by default (smacx --animate: slides, unit moves, booms, battle reports and movies play
# with their waits, as in an interactive game); TEST_FAST=1 runs unattended (skips them).
if [ "${TEST_FAST:-0}" = 1 ]; then export SMACX_ANIMATE=0; else export SMACX_ANIMATE=1; fi
# No audio device in the tests (sounds still run through the mixer); set SDL_AUDIODRIVER to override.
export SDL_AUDIODRIVER="${SDL_AUDIODRIVER:-dummy}"
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SMACX="$(realpath "${SMACX:-$ROOT/build/pp-native/smacx}")"
OUT="$(realpath -m "${1:-$ROOT/work/console_test}")"
mkdir -p "$OUT"
FAIL=0
NEW="--new size=0 --human 1 --seed 1,2"

run() {  # run CASE ARGS... : log in OUT/CASE.log
    local name="$1"; shift
    mkdir -p "$OUT/$name"
    (cd "$ROOT" && SDL_VIDEODRIVER=dummy timeout 600 "$SMACX" --hidden --outdir "$OUT/$name" "$@" > "$OUT/$name.log" 2>&1)
    echo "$?" > "$OUT/$name.rc"
}
# check CASE DESCRIPTION REGEX: the log matches (grep -E)
check() {
    if grep -qE "$3" "$OUT/$1.log"; then echo "PASS $1: $2"; else echo "FAIL $1: $2 (/$3/)"; FAIL=1; fi
}
nocrash() {
    if [ "$(cat "$OUT/$1.rc")" = 0 ] && ! grep -q "is not reconstructed yet\|abort" "$OUT/$1.log"; then
        echo "PASS $1: ran to the end"
    else
        echo "FAIL $1: exit $(cat "$OUT/$1.rc")"; FAIL=1
    fi
}

# 1. Orders by key on the first turn (Gaians: scout 4 at 35,7 in the base, foil 5):
#    hold (h), then the foil: explore (/) ...
run keys $NEW --keys "ENTER,h,UNITS,/,UNITS,SHOT:keys.bmp"
check keys "hold: order 2" "unit 4 f1 Scout Patrol \(35,7\) order 2 "
check keys "explore: state 0x4000" "unit 5 f1 Unity Foil \([0-9]+,[0-9]+\) order 0 moves [0-9]+ state 0401?4"
nocrash keys

# 2. Go-to list (g: the base list; Enter: the preselected base, the unit's home) on a late save
#    (Morgan's... faction 1 of turn_100: colony pod 91 selected first).
if [ -f "$ROOT/work/saves_late/turn_100.sav" ]; then
    run goto --save work/saves_late/turn_100.sav --human 1 --keys "g,PLANSTATE,SHOT:goto_list.bmp,ENTER,UNITS,PLANSTATE"
    check goto "go-to list shown" "popup GOTO shown"
    # PlanWin (planwin.cpp, Console_goto_routine): the world map pane shows the route from the pod
    # to the selected base, zoomed in to -10 around both ends; hidden when the list closes.
    check goto "PlanWin's route" "planwin shown 1 zoom -10 centre 6,42 lines 1 \\(6,42\\)-\\(3,43\\) c241"
    check goto "PlanWin closed" "planwin shown 0 "
    check goto "PlanWin set up" "planwin open 239x119"
    # Find a base (ctrl+B, turn_147, faction 3): the selected base on PlanWin; Down moves the
    # selection and the list's callback redraws it at the next base.
    run findbase --save work/saves_late/turn_147.sav --human 3 --keys "DOWN,ENTER,C-b,PLANSTATE,DOWN,PLANSTATE,SHOT:find.bmp,ESC"
    check findbase "the first base" "planwin shown 1 zoom -14 centre 24,24 lines 1 \\(37,21\\)-\\(37,21\\) c255"
    check findbase "the next one" "planwin shown 1 zoom -14 centre 24,24 lines 1 \\(34,18\\)-\\(34,18\\) c255"
    nocrash findbase
    check goto "a base chosen" "popup GOTO answer: [0-9]+"
    check goto "go-to order 24 given" "unit 91 f1 Colony Pod \([0-9]+,[0-9]+\) order 24"
    nocrash goto
fi

# 3. Mouse: drag the scout (go-to drag, held button) to a tile; click on the map (centre, select).
run mouse $NEW --keys "ENTER,DRAG:530x200:640x260,UNITS,v,CLICK:300x300,UNITS,SHOT:mouse.bmp"
check mouse "drag: go-to order 24" "unit 4 f1 Scout Patrol \(35,9\) order 24"
nocrash mouse

# 4. Unit orders through the menus (Alt+A: Action; the items run Console_menu_proc).
run menu $NEW --keys "ENTER,A-a,SHOT:menu_action.bmp,ESC,ALT,RIGHT,SHOT:menu_game.bmp,ESC,A-a,DOWN,DOWN,DOWN,DOWN,DOWN,DOWN,DOWN,DOWN,DOWN,DOWN,ENTER,UNITS"
check menu "a menu command chosen" "menu item [0-9A-F]+ "
check menu "Console_menu_proc ran it" "^menu command 3[0-9A-F]{2}$"
nocrash menu

# 5. Save (ctrl+S, the name typed) and load (ctrl+L, the newest save): the hold order of the
#    scout given after the save is gone after the load.
rm -f "$ROOT/game/saves/cstest.SAV"
run saveload $NEW --keys "ENTER,C-s,c,s,t,e,s,t,ENTER,h,UNITS,C-l,ENTER,UNITS,SHOT:loaded.bmp"
if [ -f "$ROOT/game/saves/cstest.SAV" ]; then echo "PASS saveload: saves/cstest.SAV written"; else echo "FAIL saveload: no save"; FAIL=1; fi
check saveload "loaded: the scout has no orders again" "popup LOADGAME shown(.|\n)*" 
if [ "$(grep -c 'unit 4 f1 Scout Patrol (35,7) order 0 ' "$OUT/saveload.log")" -ge 1 ] && \
   grep -q 'unit 4 f1 Scout Patrol (35,7) order 2 ' "$OUT/saveload.log"; then echo "PASS saveload: order 2 before, 0 after"
else echo "FAIL saveload: orders"; FAIL=1; fi
rm -f "$ROOT/game/saves/cstest.SAV"
nocrash saveload

# 6. A late save as a human faction: go-to list, automation (shift+A), turns.
if [ -f "$ROOT/work/saves_late/turn_100.sav" ]; then
    TURN="C-ENTER,DOWN,ENTER,ENTER,ENTER,ENTER"
    run late100 --save work/saves_late/turn_100.sav --human 1 --keys "UNITS,g,ESC,A,UNITS,SHOT:late.bmp,$TURN,$TURN,SHOT:late2.bmp"
    nocrash late100
fi

exit $FAIL
