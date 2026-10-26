#!/bin/bash
# Scripted checks of the functions reconstructed by the placeholder sweep (UI_PLAN.md "Remaining
# work"): the map's right-click menu (pulldown.cpp, mapwin_menu.cpp), landmarks, the info pane and
# the status panes' clicks (infowin.cpp, maininfc.cpp), ...
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

# 4. The base window (late save, faction 1, base 30): the production picker's grid (its items, a
#    right click's menu: Obsolete on the Recon Rover hides it), the support map pane, the units
#    present pane's menu (BaseWin_garrison_click at the click: Activate unit 113 closes the window).
if [ -f "$ROOT/work/saves_late/turn_100.sav" ]; then
    run basewin --save work/saves_late/turn_100.sav --human 1 --keys \
"BASE:30,c,SHOT:pick.bmp,ESC,c,RCLICK:240x80,DOWN,DOWN,DOWN,ENTER,ESC,CLICK:511x308,SHOT:support.bmp,\
CLICK:340x724,DOWN,DOWN,ENTER,BASESTATE,UNITS"
    check basewin "picker items" "prodwin items: 67 64 .* -69"
    check basewin "picker menu" "pulldown item 1: Obsolete"
    check basewin "obsolete: the list without 67" "prodwin items: 64 "
    check basewin "garrison menu at the click" "pulldown shown: 6 items at 341,615"
    check basewin "activated: the window closes" "basestate closed"
    check basewin "activated: order 0" "unit 113 f1 Scout Rover \(3,43\) order 0 moves 0 state 04000000"
    nocrash basewin
fi

# 5. Console_menu_proc: the Map menu's items without a shortcut through the menu bar (Alt+M, the
#    menu's accelerators): the right-click menu's Action... sub-menu (the Action menu's pull-down)
#    with an order; the base names toggle (ctrl+N) and its menu id 0x210.
run menuproc $NEW --keys "ENTER,S-RCLICK:530x200,UP,UP,UP,UP,UP,UP,UP,ENTER,DOWN,ENTER,UNITS,C-n,SHOT:names.bmp"
check menuproc "a sub-menu item chosen" "menu item [0-9A-F]+ "
check menuproc "run by Console_menu_proc" "^menu command [0-9A-F]+$"
nocrash menuproc
D13=$(printf 'DOWN,%.0s' $(seq 1 13))
run survey $NEW --keys "ENTER,A-m,${D13}ENTER,SHOT:survey1.bmp,A-m,${D13}ENTER,SHOT:survey2.bmp"
check survey "Map menu: Terrain Survey (0x20F)" "^menu command 20F$"
nocrash survey

# 6. The main interface's panes (maininfc.cpp: StatusWin_draw_status / draw_active, WorldWin, the
#    date box) and the map cursor in view mode (v), over a few moves of the cursor.
run panes $NEW --keys "ENTER,SHOT:unit.bmp,v,KP6,KP6,SHOT:cursor.bmp,S-BACK,BACK,UNITS"
nocrash panes

# 7. The info pane (infowin.cpp: InfoWin_setup_text, the typing timer, clicks, the right-click menu
#    of the displays, the M / I buttons) and the status panes' clicks (maininfc.cpp:
#    StatusWin_on_left_click / on_right_click, the hover of a stack unit, StatusWin_arrow_*).
#    INFOTICK:N runs N steps of the 50 ms timer. The first display (turn 1: the year, the
#    allocation) is typed into the first column; a click on the pane finishes the typing (the
#    second click the empty column's), the next click shows the next display in the other column; the menu's first item sets the display 0
#    again; the tile pane's click toggles the resource numbers; the stack row's unit under the mouse
#    is shown (hover); M shows the message list (mode 1000), I the info again (0x3E9).
run infopane $NEW --keys "ENTER,INFOTICK:60,INFOSTATE,CLICK:400x620,INFOSTATE,CLICK:400x620,CLICK:400x620,INFOSTATE,\
RCLICK:400x620,DOWN,ENTER,INFOSTATE,CLICK:190x600,MOVE:240x730,INFOSTATE,SHOT:info.bmp,RCLICK:170x730,ESC,\
MOVE:600x300,CLICK:745x596,INFOSTATE,CLICK:745x700,INFOSTATE"
check infopane "first display typing" "infostate mode 1001 pane 0 state0 0 progress 9/75"
check infopane "the year line" "info 0.0: Mission Year: \{2101\}"
check infopane "the allocation line" "info 0.2: Econ: \{50%  \(\+1/turn\)\}"
check infopane "click: typing finished" "pane 0 state0 0 progress 76/75"
check infopane "second click: the other column" "pane 1 state0 0 progress 76/75 state1 0 progress 76/75"
check infopane "the displays' menu" "pulldown item 20: Help"
check infopane "menu: display 0 typed again (both columns from the start)" "pane 1 state0 0 progress 0/75 state1 0 progress 0/75"
check infopane "tile pane: numbers, hover of the stack unit" "status numbers -1 veh -1 base -1 hover 4/-1"
check infopane "stack row's base menu" "pulldown item 52:  Hurry"
check infopane "M: the message list" "infostate mode 1000 .* hover -1/-1"
check infopane "I: the info pane" "infostate mode 1001 .* hover -1/-1"
nocrash infopane

# 8. The animations (boom.cpp, scoot.cpp, battlewin.cpp, slide.cpp): a turn of the late save with
#    the computer players' attacks and moves in view, drawn (SMACX_ANIM=1: the frames without
#    waiting) and not (the unattended default): the combat explosions (16 frames), the units'
#    turns and slides, the battle report in the console; the two games end identical (the saves
#    compared byte by byte). The base window opens with its arms (slide_show).
if [ -f "$ROOT/work/saves_late/turn_200.sav" ]; then
    TURN="C-ENTER,DOWN,ENTER,ENTER,ENTER,ENTER"
    run anim_off --save work/saves_late/turn_200.sav --human 1 --keys "${TURN},SAVE:end.sav"
    SMACX_ANIM=1 SMACX_BATTLE_SHOT=battle.bmp run anim_on --save work/saves_late/turn_200.sav --human 1 --keys "${TURN},SAVE:end.sav,BASE:0,SHOT:base.bmp"
    check anim_on "explosions drawn" "boom anim: flags 0 frames 16"
    check anim_on "units turned and slid" "scoot anim: veh [0-9]+ dir [0-7] turn [0-9]+ slide 6"
    check anim_on "battle report" "battle report: [0-9]+ \\(.*\\) vs [0-9]+ \\(.*\\) strength"
    if cmp -s "$OUT/anim_off/end.sav" "$OUT/anim_on/end.sav"; then echo "PASS anim: same game with and without the frames"; else echo "FAIL anim: the saves differ"; FAIL=1; fi
    nocrash anim_on
    nocrash anim_off
fi

# 9. The tutorial (tutwin.cpp; SMACX_PREFS_SET=0x20: the tutorial hints preference). LANDFALL's
#    TOUR button starts the first turn's tour (TutWin_tour kind 0: TOUR1 .. TOURDATEINFO,
#    TOUROVER, each with Continue); in a second game the base window's BASECONTROL and the unit
#    hints of Console_give_hints (the foil on a fungus sea: FUNGUSSEA).
K=$(printf 'ENTER,%.0s' $(seq 1 16))
SMACX_PREFS_SET=0x20 run tour $NEW --keys "ENTER,CLICK:135x735,INFOSTATE,${K}UNITS"
check tour "LANDFALL (a hint with the TOUR button)" "popup LANDFALL shown"
check tour "the tour's first window" "tutwin TOUR1: kind 2"
check tour "the ocean near the cursor" "tutwin TOUROCEAN: kind 2"
check tour "the interface's windows" "tutwin TOURDATEINFO: kind 2"
check tour "the end of the tour" "tutwin TOUROVER: kind 0"
nocrash tour
SMACX_PREFS_SET=0x20 run hints $NEW --keys "ENTER,ENTER,INFOSTATE,KP6,ENTER,KP6,ENTER,KP3,ENTER,KP2,ENTER,KP2,ENTER,UNITS"
check hints "the base window's hint" "tutwin BASECONTROL: kind 0"
check hints "a unit hint (Console_give_hints)" "tutwin FUNGUSSEA: kind 0"
nocheck hints "no placeholder" "not reconstructed: Console_give_hints"
nocrash hints

# 10. Console_assemble (shift+J): the ASSEMBLEGROUP dialog with the "Vehicle Groups" preference's
#     kinds (default 7: near the cursor, not sentried / held, not in a base); the units chosen join
#     the group (state bit 0x8000000).
run assemble $NEW --keys "ENTER,S-j,ENTER,UNITS"
check assemble "the dialog" "popup ASSEMBLEGROUP shown"
check assemble "the scout in the group" "unit 4 f1 Scout Patrol .* state 0C000000"
nocheck assemble "no placeholder" "not reconstructed: Console_assemble"
nocrash assemble

exit $FAIL
