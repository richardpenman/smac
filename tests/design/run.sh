#!/bin/bash
# Scripted checks of the research choice (tech_pick's window, Console_set_tech, Console_set_ai),
# the new technology window (NewTechWin) and the design workshop (DesignWin) (src/ui/techwin.cpp,
# designwin.cpp).
#
#   tests/design/run.sh [OUTDIR]        (SMACX=path/to/smacx, default build/p7-native/smacx)
#
# Plays scripted keys (smacx --keys, hidden, SDL dummy video) on late saves and on a new game
# without blind research (rules=0x180e: the late saves and the default rules research blind, where
# the window is not shown) as a human faction. TECHSTATE logs the viewer's research ("techstate fF
# research T accumulated A cost C ai FIGHT GROWTH TECH WEALTH rules R"), SCREENSTATE the open
# window ("techwin ... list ...", "newtechwin ...", "designstate ..."), PROTOSTATE the faction's
# prototypes; games saved after a change are loaded again to check that it stays. Screenshots go to
# OUTDIR/<case>/. Exit status 0 when every check passes.
set -u
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SMACX="$(realpath "${SMACX:-$ROOT/build/p7-native/smacx}")"
OUT="$(realpath -m "${1:-$ROOT/work/design_test}")"
mkdir -p "$OUT"
FAIL=0

run() {  # run CASE ARGS... : runs smacx, log in OUT/CASE.log
    local name="$1"; shift
    mkdir -p "$OUT/$name"
    rm -f "$OUT/$name"/*.bmp
    (cd "$ROOT" && SDL_VIDEODRIVER=dummy timeout 600 "$SMACX" --hidden --outdir "$OUT/$name" "$@" > "$OUT/$name.log" 2>&1)
    echo "$?" > "$OUT/$name.rc"
}
line() {  # line CASE PREFIX N : the Nth log line starting with PREFIX
    grep -a "^$2" "$OUT/$1.log" | sed -n "$3p"
}
check() {  # check WHAT GOT WANT
    if [ "$2" == "$3" ]; then echo "PASS $1"; else echo "FAIL $1: got '$2' want '$3'"; FAIL=1; fi
}
has() {  # has CASE WHAT REGEX : the log matches
    if grep -aqE "$3" "$OUT/$1.log"; then echo "PASS $1: $2"; else echo "FAIL $1: $2 (/$3/)"; FAIL=1; fi
}
nocrash() {
    if [ "$(cat "$OUT/$1.rc")" = 0 ] && ! grep -aq "is not reconstructed yet\|Aborted\|abort" "$OUT/$1.log"; then
        echo "PASS $1: ran to the end"
    else
        echo "FAIL $1: exit $(cat "$OUT/$1.rc") $(grep -am1 'not reconstructed yet\|abort' "$OUT/$1.log")"
        FAIL=1
    fi
}
ts() {  # ts CASE N : the Nth techstate line without the rules
    line "$1" techstate "$2" | sed 's/ rules.*//'
}
NEW="--new size=0 rules=0x180e --human 1"
TURN="C-ENTER,DOWN,ENTER"  # end the turn (REALLYOVER: Yes)

# 1. Blind research (turn_100, faction 1): shift+R asks for the research priorities (TECHRANDOM);
#    space toggles the first check box (CONQUER... EXPLORE), Enter keeps the choice.
run blind100 --save work/saves_late/turn_100.sav --human 1 --keys "TECHSTATE,R,SHOT:priorities.bmp,SPACE,ENTER,TECHSTATE,\
SAVE:after.sav"
nocrash blind100
check "blind100 start" "$(ts blind100 1)" "techstate f1 research 58 accumulated 8 cost 28 ai -1 1 0 0"
has blind100 "priorities popup" "^popup TECHRANDOM answer: 2"
check "blind100 priorities" "$(ts blind100 2)" "techstate f1 research 58 accumulated 8 cost 28 ai 0 1 0 0"
run blind100r --save "$OUT/blind100/after.sav" --human 1 --keys "TECHSTATE"
check "blind100 reload" "$(ts blind100r 1)" "techstate f1 research 58 accumulated 8 cost 28 ai 0 1 0 0"

# 2. The research window (new game, no blind research): shift+R lists the seven available techs in
#    a 4 x 2 grid; Right, Enter picks the second (Progenitor Psych). Two turns later TECHCHANGE:
#    "Never mind" keeps it, "Change" opens the window: the letter b selects Biogenetics (half the
#    points are lost); a click on the fifth panel picks Industrial Base.
run choice --new size=0 rules=0x180e --human 1 --keys "ENTER,R,SHOT:techwin.bmp,SCREENSTATE,RIGHT,ENTER,TECHSTATE,\
$TURN,$TURN,TECHSTATE,R,ENTER,TECHSTATE,R,DOWN,ENTER,b,SCREENSTATE,ENTER,TECHSTATE,R,DOWN,ENTER,CLICK:88x499,TECHSTATE,\
SAVE:after.sav"
nocrash choice
check "choice list" "$(line choice techwin 1)" "techwin faction 1 count 7 cols 4 rows 2 panel 236 344 sel 0 list 5 78 0 2 1 4 3"
has choice "caption" "^screentext SELECT RESEARCH GOAL"
has choice "panel text" "^screentext panel \{Recycling Tanks \(Bonus Resources\)\}"
check "choice picked" "$(ts choice 1)" "techstate f1 research 78 accumulated 0 cost -1 ai -1 1 0 0"
check "choice two turns" "$(ts choice 2)" "techstate f1 research 78 accumulated 2 cost -1 ai -1 1 0 0"
check "choice never mind" "$(ts choice 3)" "techstate f1 research 78 accumulated 2 cost -1 ai -1 1 0 0"
check "choice letter b" "$(line choice techwin 2 | sed 's/.* sel \([0-9]*\) .*/\1/')" "2"
check "choice changed (half the points)" "$(ts choice 4)" "techstate f1 research 0 accumulated 1 cost -1 ai -1 1 0 0"
check "choice click" "$(ts choice 5)" "techstate f1 research 1 accumulated 0 cost -1 ai -1 1 0 0"
run choicer --save "$OUT/choice/after.sav" --human 1 --keys "TECHSTATE"
check "choice reload" "$(ts choicer 1)" "techstate f1 research 1 accumulated 0 cost -1 ai -1 1 0 0"

# 3. The new technology window: Doctrine: Mobility is discovered on turn 8 (NewTechWin with its
#    sections and links), Enter closes it; ASKSEEDESIGN (No), then the next research choice.
T7="$TURN,$TURN,$TURN,$TURN,$TURN,$TURN,$TURN"
run newtech --new size=0 rules=0x180e --human 1 --keys "ENTER,R,ENTER,$T7,SHOT:newtech.bmp,SCREENSTATE,ENTER,ENTER,\
SCREENSTATE,ENTER,TECHSTATE"
nocrash newtech
has newtech "window" "^newtechwin tech 5 from 0"
has newtech "title" "^screentext OUR RESEARCHERS HAVE MADE A BREAKTHROUGH!"
has newtech "facility link" "^screentext section 0 \\\$LINK<Command Center=100027>"
has newtech "tech links" "^screentext section 1 \\\$LINK<Doctrine: Loyalty=140043>"
has newtech "chassis link" "^screentext section 2 \\\$LINK<Speeder=40001> \(2\)"
has newtech "design question" "^popup ASKSEEDESIGN answer: 0"
check "newtech next choice" "$(line newtech techwin 1 | cut -d' ' -f1-4)" "techwin faction 1 count"
check "newtech research" "$(ts newtech 1)" "techstate f1 research 36 accumulated 7 cost -1 ai -1 1 0 0"

exit $FAIL
