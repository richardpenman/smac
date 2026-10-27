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
# Animated by default (smacx --animate: slides, unit moves, booms, battle reports and movies play
# with their waits, as in an interactive game); TEST_FAST=1 runs unattended (skips them).
if [ "${TEST_FAST:-0}" = 1 ]; then export SMACX_ANIMATE=0; else export SMACX_ANIMATE=1; fi
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
# (Terrain Survey is the 13th shown item of the Map menu: pfncMainMenuUpdateVisible shows one of
# the view / unit mode items 0x200 / 0x201.)
D13=$(printf 'DOWN,%.0s' $(seq 1 12))
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

# 11. The labs report's tree (ReportWin_draw_labs, labs.cpp; ReportIf_draw_labs): blind research's
#     top box, the progress bar and the breakthrough lines; Biogenetics at the centre with its two
#     successors; the mouse over GENE SPLICING highlights it; a click moves the tree there (its
#     prerequisites and successors); the BASES tab lists the bases. In the datalinks (F1 on
#     turn_147) the technology entry is the same tree, its buttons open the entries.
run labs $NEW --keys "ENTER,F2,SCREENSTATE,MOVE:733x316,SCREENSTATE,CLICK:733x316,SCREENSTATE,CLICK:180x585,SCREENSTATE,ESC"
check labs "blind research's top" "^report labs research \\[LABORATORY SUMMARY\\]"
check labs "the progress bar" "^report labs bar 0 [0-9]+ of 638"
check labs "the breakthrough line" "^report labs discov Breakthroughs every .*Next breakthrough anticipated"
check labs "the tree at Biogenetics" "^report labs tree 0 preq leads 48 49"
check labs "what it allows" "^report labs allows Base Facilities: Recycling Tanks, The Human Genome Project"
check labs "the technologies list" "^report labs list 6 Centauri Ecology\\|\\[E 1\\]"
check labs "the progress per category" "^report labs progress 0 1/22"
check labs "the summary" "^screentext TECH COST: [0-9]+    TECH ACCUMULATED: 0    TECH PER TURN: "
check labs "the mouse over a successor" "^screentext labs hover 892"
check labs "the tree moved" "^report labs tree 49 preq 0 44 leads 38 50 60"
check labs "the bases list" "^report labs list 1024 .*\\|\\+[0-9]+ \\([0-9.]+\\)"
nocrash labs
run labsdl --save work/saves_late/turn_147.sav --human 3 --keys "DOWN,ENTER,F1,SCREENSTATE,CLICK:797x237,SCREENSTATE,ESC"
check labsdl "the entry's tree" "^screentext labs tree 0 preq leads 48 49"
check labsdl "links in the centre box" "^screentext labs allows Base Facilities: \\\$LINK<Recycling Tanks=100003>"
check labsdl "a successor's button opens its entry" "^datalink cat 14 index 49 "
check labsdl "its tree" "^screentext labs tree 49 preq 0 44 leads"
nocrash labsdl

# 12. The energy report's two columns (ReportWin_draw_energy, ReportIf_draw_energy; turn_147,
#     faction 3): the summary, the first base's budget, a click on one of its facilities shows that
#     facility in every base; the main interface's BASE EXPENDITURES and MAINTENANCE COSTS lists.
#     The scroll bars (scroll.cpp): the labs report's technology list scrolled by its down button.
run energy --save work/saves_late/turn_147.sav --human 3 --keys "DOWN,ENTER,F3,SCREENSTATE,CLICK:560x80,SCREENSTATE,F2,SCREENSTATE,CLICK:232x748,SCREENSTATE,ESC"
check energy "the summary" "^report energy summary 195 income 18 maint 8 loan 0 net 10"
check energy "the base's budget" "^screentext \\[UNIVERSITY BASE BUDGET\\]"
check energy "its facilities" "^report energy budget 3 Recycling Tanks\\|\\{0\\}"
check energy "its total, selected" "^report energy budget 99999999 \\* \\{TOTAL COST: \\|0\\}"
check energy "a facility in every base" "^screentext \\[RECYCLING TANKS COST\\]"
check energy "the bases with it" "^report energy budget 1029 U.N. Headquarters"
check energy "base expenditures" "^report energy bases 1026 \\* University Base\\|\\+3/-0 = \\+3/year"
check energy "maintenance costs" "^report energy facilities 6 4 Recreation Commons\\|-4/year"
check energy "the list scrolled a line (its ninth row shown)" "^report labs list 36 "
nocheck energy "not a page" "^report labs list 13 "
nocrash energy

# 13. The map view of turn_300 as faction 3 sees it, against the original (the oracle's mapdump of
#     the same save and view: orig_t300_view.smxmap is its header, the pixels' MD5 below): the
#     bases' art is their founder's (base_draw: faction_id_former), the drone riot icon, and
#     faction 5's black population boxes (its search key is FUNGBOY, its art spartans.pcx: the
#     original draws them so too).
if [ -f "$ROOT/work/saves_late/turn_300.sav" ]; then
    run map300 --save work/saves_late/turn_300.sav --human 0 --viewer 3 --nosound --turns 0 \
        --dump "$OUT/map300.smxmap" --like "$ROOT/tests/placeholders/orig_t300_view.smxmap"
    md5=$("$ROOT/tools/venv/bin/python" -c "import hashlib,sys; print(hashlib.md5(open(sys.argv[1],'rb').read()[808:]).hexdigest())" "$OUT/map300.smxmap" 2>/dev/null)
    echo "map300 md5 $md5" >> "$OUT/map300.log"
    check map300 "the original's pixels" "map300 md5 f895ca60f35e21d0cf931ff4029227ef"
    nocrash map300
fi

# 14. The Help menu's tours (Console_menu_proc 0x720..0x725 -> TutWin_tour): the terraforming
#     lessons (kind 3: the rainfall, altitude, rockiness and forest windows at the scored tiles),
#     the base window's (TutWin_tour1, the window opened on the first base), social
#     engineering's (tour5, the window shown for it), the design workshop's (tour7) and the drone
#     riots' (tour6); Enter continues each window.
D() { printf 'DOWN,%.0s' $(seq 1 $1); }
E=$(printf 'ENTER,%.0s' $(seq 1 45))
for t in 15 16 17 18 19; do
    run tour$t $NEW --keys "ENTER,A-h,RIGHT,RIGHT,RIGHT,RIGHT,$(D $t)ENTER,${E}UNITS"
done
check tour15 "the terraforming lessons" "tutwin TOURRAIN: kind 2"
check tour15 "altitude" "tutwin TOURALT: kind 2"
check tour15 "the lessons' end" "tutwin TOURFORMDONE: kind 2"
check tour16 "the base window's tour" "tutwin TOUR1GOVERNOR: kind 2"
check tour16 "its end" "tutwin TOUR1OK: kind 2"
check tour17 "social engineering's tour" "tutwin SOCTOURMAIN: kind 2"
check tour17 "its end" "tutwin SOCTOUROK: kind 2"
check tour18 "the design workshop's tour" "tutwin DESTOURCHAS: kind 2"
check tour18 "its end" "tutwin DESTOURCANCEL: kind 2"
check tour19 "the drone riots' tour" "tutwin DRONETOUR1: kind 2"
check tour19 "its end" "tutwin TOUR1PSYCHBUTTON: kind 2"
for t in 15 16 17 18 19; do
    nocheck tour$t "no placeholder" "not reconstructed: TutWin_tour"
    nocrash tour$t
done

# 15. Console_custom_my_faction ("+"): the CUSTOMNAMES fields of the viewer's faction; the
#     leader's name edited (Deirdre -> Bob) and OK.
run custom $NEW --keys "ENTER,+,BACK,BACK,BACK,BACK,BACK,BACK,BACK,BACK,BACK,BACK,BACK,BACK,B,o,b,ENTER"
check custom "the dialog" "popup CUSTOMNAMES shown"
check custom "the name kept" "custom faction 1: Lady Bob, Gaia's Stepdaughters / Gaians / Gaian \\(female\\)"
nocheck custom "no placeholder" "not reconstructed: Console_custom_my_faction"
nocrash custom

# 16. The base window's help bubbles (BaseWin_click / BaseWin_iface_click with the right button:
#     Win_set_bubble_text over the click): the nutrients box, the resource map, the facilities
#     list.
run bubbles $NEW --keys "ENTER,BASE:0,RCLICK:240x150,RCLICK:500x200,RCLICK:760x200,ESC"
check bubbles "the nutrients' bubble" "^bubble: NUTRIENTS: "
check bubbles "the resource map's" "^bubble: RESOURCES: "
check bubbles "the facilities'" "^bubble: FACILITIES: "
nocrash bubbles

# 17. The map view's partial redraws and overlays (mapwin_radius.cpp) on turn_300 as faction 3 sees
#     it, against the original (the oracle's mapdump with options= / radius=, same view; the
#     pixels' MD5 below; orig_t300_r4.smxmap is its header): MapWin_draw_radius on a canvas
#     cleared to 0 (radius 0, 2, and 1 across the map's wrap), the AI goals (options 0x2000:
#     MapWin_gen_overlays), the keypad legend and the diplomatic table (0x20000 MapWin_draw_legend,
#     0x100000 MapWin_draw_wars; their MD5s are smacx's: the original's differs in 10 pixels of the
#     legend's bold digits, the font rasteriser's); and two scrolled redraws.
if [ -f "$ROOT/work/saves_late/turn_300.sav" ]; then
    mapcase() {  # mapcase NAME MD5 ARGS...
        local name=$1 want=$2; shift 2
        run $name --save work/saves_late/turn_300.sav --human 0 --viewer 3 --nosound --turns 0 \
            --dump "$OUT/$name.smxmap" --like "$ROOT/tests/placeholders/orig_t300_r4.smxmap" "$@"
        local md5=$("$ROOT/tools/venv/bin/python" -c "import hashlib,sys; print(hashlib.md5(open(sys.argv[1],'rb').read()[808:]).hexdigest())" "$OUT/$name.smxmap" 2>/dev/null)
        echo "$name md5 $md5" >> "$OUT/$name.log"
        check $name "the original's pixels" "$name md5 $want"
        nocrash $name
    }
    mapcase map_base eca8a12b4ebf3931a16f1a69b0f5ef09
    mapcase map_r0 83d3c1b4cf47c9b9652285f0b28eaebf --dump-radius 3,15,0
    mapcase map_r2 a774f8162217db2fe7ade169baa9ff6f --dump-radius 4,16,2
    mapcase map_r1e ee008b079a3459cc0f83707454bf8511 --dump-radius 40,2,1
    mapcase map_goals 1df5c340869907f34a3598a600d76145 --dump-options 2000
    mapcase map_legend b4433f7bd947debba97a24d0795292b4 --dump-options 120000
    mapcase map_legend_r a57f4a5d64a402e94e7abf13bafc4883 --dump-radius 1,13,1 --dump-options 22000
    # Scrolled redraws (pfncRedrawMap with the scroll flag: the old canvas shifted, the uncovered
    # columns and rows drawn): from 60,9 the original's picture is its full redraw's; from 7,17
    # it differs from it (the oracle's mapdump scrollfrom=). The unit at the left edge turns by
    # the camera yaw of its own tilt, not the last unit drawn's (vehdraw_draw: compute_camera
    # before the rotation).
    mapcase map_scroll eca8a12b4ebf3931a16f1a69b0f5ef09 --scroll-from 60,9
    mapcase map_scroll2 df47e3c947c6a1a0082d4d0b57c8fe80 --scroll-from 7,17
fi

# 18. Palette cycling (load_cycling, Palette_init_cycle / start_cycle, Palette_timer_callback): the
#     shipped system.txt has no cycles; a game directory whose #CYCLING lists three gets them, and
#     each step of a cycle's timer moves its colours down one entry (PALTICK:N runs N steps).
mkdir -p "$OUT/cyclegame"
for f in "$ROOT"/game/*; do ln -sfn "$f" "$OUT/cyclegame/"; done
rm -f "$OUT/cyclegame/system.txt"
sed '/^#CYCLING/{n;s/^0/3/}' "$ROOT/game/system.txt" > "$OUT/cyclegame/system.txt"
run cycle $NEW --gamedir "$OUT/cyclegame" --keys "ENTER,PALTICK:0,PALTICK:1,PALTICK:4"
check cycle "the three cycles" "^palette cycle 2: entries 241..245 every 300 ms"
if "$ROOT/tools/venv/bin/python" - "$OUT/cycle.log" <<'PY'
import sys
l = [x.split(':', 2)[2].split() for x in open(sys.argv[1]) if x.startswith('palette cycle 0:')]
sys.exit(0 if len(l) == 3 and l[1] == l[0][1:] + l[0][:1] and l[2] == l[0] else 1)
PY
then echo "PASS cycle: a step rotates the colours, five steps go round"; else echo "FAIL cycle: the rotation"; FAIL=1; fi
run nocycle $NEW --keys "ENTER,PALTICK:1"
nocheck nocycle "no cycles in the shipped system.txt" "^palette cycle"
nocrash cycle

# 19. scroll_show / scroll_hide (slide.cpp): a list popup with more items than rows (a test
#     script, #listbox 4, in the game directory of case 18) gets the console's right arm and the
#     scroll rail at its right edge, the scroll bar on the rail as tall as the rail allows
#     (Popup_on_listbox_scroll_create: 255 - 10); closing it hides them. Base operations of
#     turn_300 (faction 3, 15 bases) get the report window's scroll bar on its rail: its down
#     button scrolls a row, its track a page; secret projects too, the score report hides it.
printf '#LISTTEST\r\n#xs 440\r\n#caption Scroll test\r\n#listbox 4\r\n"Pick one:"\r\n\r\nOne\r\nTwo\r\nThree\r\nFour\r\nFive\r\nSix\r\nSeven\r\nEight\r\n\r\n' \
    > "$OUT/cyclegame/listtest.txt"
run listrail $NEW --gamedir "$OUT/cyclegame" --keys "ENTER,POPUP:listtest/LISTTEST/2,SHOT:list.bmp,DOWN,ENTER"
check listrail "the rail arrives" "^scroll show 757x300 at 149,255: rail 255"
check listrail "and leaves" "^scroll hide 757x300 at 149,255"
check listrail "the list's choice" "^popup LISTTEST answer: 1 "
nocrash listrail
if [ -f "$ROOT/work/saves_late/turn_300.sav" ]; then
    run opsrail --save work/saves_late/turn_300.sav --human 3 --keys \
        "ESC,ESC,ESC,ESC,ESC,ESC,ESC,F4,CLICK:861x495,SCREENSTATE,CLICK:861x300,SCREENSTATE,SHOT:ops.bmp,F5,SCREENSTATE,F8,ESC"
    check opsrail "the window's scroll bar" "^scroll show 728x553 at 164,3"
    check opsrail "its down button: a row" "^report page scroll 1 of 5"
    check opsrail "its track: a page" "^report page scroll 5 of 5"
    check opsrail "secret projects" "^report page scroll 0 of 7"
    check opsrail "the score report hides it" "^scroll hide 728x553 at 164,3"
    nocrash opsrail
fi

# 20. The menus' look (AlphaMenu, menu.cpp, from the asm: no screenshot of the original's could be
#     taken): a column of nine menu2.pcx buttons (0x8B x 0x12 under a 6 high top) above the
#     console, the shown menus from the top (HELP at the bottom); the open menu's PullDown at its
#     right (PullDown_on_redraw with AlphaMenu's fill functions); AlphaMenu_show when it opens.
run menucol $NEW --keys "ENTER,A-m,DOWN,DOWN,SHOT:menu.bmp,ESC,A-h,ESC"
check menucol "the column, MAP its third button" "^menu column at 0,343 \\(139x168\\): Map at 0,385"
check menucol "HQ the second" "^menu column at 0,343 \\(139x168\\): HQ at 0,367"
check menucol "AlphaMenu_show when the column opens (the slide skipped unattended)" "^alphamenu show \\(sound [01], slide 0\\)"
nocrash menucol

# 21. The scenario editor (editor.cpp): ctrl+K (SCENEDITOR yes) switches it on; '1' paints the
#     elevation, the Edit Map menu's Place At Cursor (0xA20) raises the cursor's tile
#     (MapWin_editor; world_raise_alt lifts the foil's sea tile beside it: the foil is lost, as the
#     engine does); '7' the brush (radius 1), '5' unity pods placed then removed (0xA21);
#     ctrl+shift+F1 edits the scout's flags (EDITVEH: its second box, flag 0x80); shift+F1 creates a unit (EDITORUNIT: the first, a colony pod) at the cursor; ctrl+F6 kills the
#     cursor's stack; ctrl+K switches the editor off.
D12=$(printf 'DOWN,%.0s' $(seq 1 12))
run editor $NEW --keys "ENTER,C-k,DOWN,ENTER,ENTER,1,A-e,${D12}ENTER,7,DOWN,DOWN,ENTER,5,A-e,${D12}ENTER,A-e,DOWN,${D12}ENTER,\
C-S-F1,DOWN,SPACE,ENTER,S-F1,ENTER,UNITS,C-F6,UNITS,C-k,ENTER"
check editor "on" "^scenario editor on \\(GameState 000000E0\\)"
check editor "elevation raised at the cursor" "^editor paint 0 at 35,7: change 1"
check editor "the brush" "^editor paint 0 brush 2"
check editor "a unity pod placed" "^editor paint 4 at 35,7: change 1"
check editor "and removed" "^editor paint 4 at 35,7 \\(remove\\): change 1"
check editor "the unit's flags edited" "^editor: unit 4 flags 0080 "
check editor "a unit created" "^editor: unit 19 \\(Colony Pod\\) of faction 1 at 35,7"
if [ "$(grep -c '^unit 19 f1 Colony Pod' "$OUT/editor.log")" = 1 ]; then echo "PASS editor: the stack killed"; else echo "FAIL editor: the stack killed"; FAIL=1; fi
check editor "off" "^scenario editor off"
nocrash editor

# 22. The rest of the scenario editor (editor.cpp, editpane.cpp, editor_diplo.cpp, editor_base.cpp):
#     the interface pane (the 0x3EA button shown with the omniscient view, the paint and brush
#     buttons, '2' presses Rockiness's), the unit's DATA (EDITVEH's button 2: EDITVEH2's first field),
#     the difficulty, the personality and strategy (AIEDITOR / AIEDITOR2's check boxes), the
#     scenario's parameters, rules and victory conditions, a natural landmark placed and removed,
#     the factions reset, the diplomacy editor's window (the pact box clicked, another faction
#     selected: the boxes applied; INTEGRITY; OK), the view (ALL: EDITORCONTROLALL), the y key (the omniscient view), the base
#     window's editor menu (the population, a secret project made unbuilt).
run editor2 $NEW --keys "ENTER,C-k,DOWN,ENTER,ENTER,EDITSTATE,2,EDITSTATE,C-S-F1,CLICK:332x472,BACK,5,ENTER,\
CMD:0x607,DOWN,ENTER,CMD:0x612,ENTER,SPACE,ENTER,CMD:0x613,ENTER,DOWN,SPACE,ENTER,CMD:0x616,ENTER,CMD:0x615,SPACE,ENTER,\
CMD:0x617,ENTER,CMD:0xA17,ENTER,CMD:0xA18,ENTER,CMD:0x610,DOWN,ENTER,\
S-F9,ENTER,CLICK:560x324,SCREENSTATE,CLICK:200x243,CLICK:420x569,ENTER,CLICK:794x569,\
CMD:0x606,DOWN,DOWN,DOWN,DOWN,DOWN,DOWN,DOWN,ENTER,ENTER,y,EDITSTATE,y,EDITSTATE,BASE:0,e,DOWN,ENTER,BASESTATE,\
e,DOWN,DOWN,DOWN,DOWN,DOWN,DOWN,DOWN,DOWN,DOWN,ENTER,DOWN,DOWN,ENTER,ENTER"
check editor2 "the pane shown with its buttons" "^editpane shown 1 mode 1002 paint 0 \\(button 1011\\) brush 0 \\(button 1036\\)"
check editor2 "the paint buttons' bubbles" "^editpane button 3FA at 313,110 43x18: Soil Enricher"
check editor2 "the brush buttons" "^editpane button 411 at 522,132 24x24"
check editor2 "a paint key presses its button" "^editpane shown 1 mode 1002 paint 1 \\(button 1012\\)"
check editor2 "the unit's data" "^editor: unit 4 data damage 5 moves 0 lurking 0 turns 0"
check editor2 "difficulty" "^editor: difficulty 4"
check editor2 "personality" "^editor: faction 1 personality 0 0 0 0 fight -1"
check editor2 "strategy" "^editor: faction 1 strategy flags 08000000 ext 00000000"
check editor2 "parameters" "^editor: parameters 9999 9999 0 0 2100 2500 -1"
check editor2 "rules" "^editor: rules 00201A0E more 00000000"
check editor2 "victory" "^editor: victory rules 00201A0E state 000000E0"
check editor2 "a crater placed" "^editor: natural 0 placed at 35,7"
check editor2 "and removed" "^editor: natural 0 removed at 35,7"
check editor2 "factions reset" "^editor: factions reset"
check editor2 "diplomacy: the pact box" "^diploedit state 1-2: 8=0 10=0 4=0 2=0 1=1 "
check editor2 "diplomacy: applied on selecting another faction" "^diploedit 1-2: status 00000003 agenda 00000000"
check editor2 "diplomacy: integrity" "^diploedit 1: integrity 0"
check editor2 "diplomacy: closed" "^diploedit closed"
check editor2 "view: all factions" "^editor: view all \\(humans 02\\)"
check editor2 "y: the pane's button hidden" "^editpane shown 0 mode 1002"
check editor2 "base editor: population" "^basestate base 0 .* f1 pop 2 "
check editor2 "base editor: a project made unbuilt" "^base editor: project 2 at -1"
nocheck editor2 "no placeholder" "not reconstructed"
nocrash editor2

# 23. The new technology window's picture buttons (NewTechWin_exec, FUN_00483EB0): the quote and
#     the long description (sprites of interface.pcx, load_box_sprites 0x822610), the quote's
#     selected; a click on the second shows techlongs.txt's TECHn in the quote's place, the first
#     the BLURBSX quote again. (Doctrine: Mobility is discovered on turn 8, as tests/design's; a
#     supply pod's data shows the same window.)
T="C-ENTER,DOWN,ENTER"
run newtech --new size=0 rules=0x180e --human 1 --keys "ENTER,R,ENTER,$T,$T,$T,$T,$T,$T,$T,SCREENSTATE,\
CLICK:755x619,SCREENSTATE,SHOT:desc.bmp,CLICK:724x619,SCREENSTATE,ENTER,ENTER"
check newtech "the quote button's picture, selected" "^newtechwin button 1 sprite 28x34 set 1"
check newtech "the description button's picture" "^newtechwin button 2 sprite 28x34 set 0"
check newtech "the quote" "^screentext blurb quote"
check newtech "the description shown" "^screentext blurb description"
check newtech "techlongs.txt's text" "^screentext blurb The early colonists quickly learned the importance of"
check newtech "the description button selected" "^newtechwin button 2 sprite 28x34 set 1"
if [ "$(grep -c '^screentext blurb quote' "$OUT/newtech.log")" = 2 ]; then echo "PASS newtech: the quote again"; else echo "FAIL newtech: the quote again"; FAIL=1; fi
nocrash newtech

# 24. Re-entrant presents (a stack overflow seen on macOS): a unit with a go-to order shown in the
#     status pane (view mode, the cursor on it) draws its path (StatusWin_draw_active ->
#     MapWin_dest_line -> Path_find), whose message pump (do_all_non_input) presented the screen
#     again from inside the present, without end once 50 ms had passed since the last one.
run gotoredraw $NEW --keys "ENTER,DRAG:530x200:700x330,v,KP2,KP1,WAIT:200,SHOT:goto.bmp,WAIT:200,UNITS"
check gotoredraw "the go-to order kept" "unit 4 f1 Scout Patrol \(35,9\) order 24"
nocrash gotoredraw

exit $FAIL
