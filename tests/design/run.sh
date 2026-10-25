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
# turn_147 as faction 3 and turn_300 as faction 6 start with a computer player's call (communicate):
# DOWN,ENTER answers its COMM popup "not now".

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

# 4. The design workshop (turn_147, faction 3: University). Window at (216, 168), 623 x 387; the
#    component boxes (weapon at 320,320), the picker's grid (Resonance Laser at 513,407), the
#    buttons RENAME (403,525) and APPLY (296,499), the INFANTRY preset (842,631), the prototype
#    list in the bottom pane (second box at 339,729).
#    a) u opens a new design in the first empty slot (208: Chaos Foil from consider_designs); the
#       weapon picker gives Resonance Laser; Enter (DONE) makes the prototype (R-Laser Foil) and
#       prune_protos makes AAA Gatling Foil obsolete; the save keeps them.
run design147 --save work/saves_late/turn_147.sav --human 3 --keys "DOWN,ENTER,u,SHOT:design.bmp,SCREENSTATE,CLICK:320x320,\
SHOT:picker.bmp,SCREENSTATE,CLICK:513x407,SCREENSTATE,ENTER,PROTOSTATE,SAVE:after.sav"
nocrash design147
ds() { line "$1" designstate "$2" | sed 's/ stack.*//'; }
pr() { grep -a "^proto $2 " "$OUT/$1.log" | sed -n "${3:-1}p"; }
check "design147 new design" "$(ds design147 1)" \
    "designstate f3 unit 208 chassis 3 weapon 5 armor 11 reactor 2 abil 0x2 slots 1 -1 name '' cost 40 proto -1"
has design147 "title" "^screentext DESIGN WORKSHOP"
has design147 "slot text" "^screentext DESIGNING NEW UNIT \(SLOT #17\)"
has design147 "picker lists the weapons" "^picker kind 1 sel 5 items 0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21 23 24 25"
has design147 "unavailable weapon" "^screentext item 6 Fusion Laser \(unavailable\)"
check "design147 weapon chosen" "$(ds design147 2)" \
    "designstate f3 unit 208 chassis 3 weapon 12 armor 11 reactor 2 abil 0x2 slots 1 -1 name '' cost 40 proto -1"
has design147 "name" "^screentext name R-LASER FOIL"
check "design147 prototype" "$(pr design147 208)" \
    "proto 208 name 'R-Laser Foil' chassis 3 weapon 12 armor 11 reactor 2 abil 0x2 flags 0x10d obsolete 0 cost 5"
check "design147 pruned" "$(pr design147 215)" \
    "proto 215 name 'AAA Gatling Foil' chassis 3 weapon 3 armor 11 reactor 2 abil 0x100 flags 0x5 obsolete 1 cost 4"
run design147r --save "$OUT/design147/after.sav" --human 3 --keys "PROTOSTATE"
check "design147 reload" "$(pr design147r 208)" \
    "proto 208 name 'R-Laser Foil' chassis 3 weapon 12 armor 11 reactor 2 abil 0x2 flags 0x10d obsolete 0 cost 5"

#    b) the INFANTRY preset (consider_designs) gives the existing Chaos Infantry's design; RENAME
#       (DESIGNNAME: "Test"), APPLY reuses prototype 204 with the custom name (flag 2).
run preset147 --save work/saves_late/turn_147.sav --human 3 --keys "DOWN,ENTER,u,CLICK:842x631,SCREENSTATE,CLICK:403x525,\
T,e,s,t,ENTER,SCREENSTATE,CLICK:296x499,SCREENSTATE,ESC,PROTOSTATE,SAVE:after.sav"
nocrash preset147
check "preset147 infantry" "$(ds preset147 1)" \
    "designstate f3 unit 208 chassis 0 weapon 5 armor 11 reactor 2 abil 0 slots -1 -1 name '' cost 40 proto -1"
has preset147 "rename popup" "^popup DESIGNNAME answer: 0"
check "preset147 renamed" "$(ds preset147 2)" \
    "designstate f3 unit 208 chassis 0 weapon 5 armor 11 reactor 2 abil 0 slots -1 -1 name 'Test' cost 40 proto -1"
check "preset147 applied (existing design)" "$(line preset147 designstate 3 | sed 's/.* result //')" "1"
check "preset147 prototype" "$(pr preset147 204)" \
    "proto 204 name 'Test' chassis 0 weapon 5 armor 11 reactor 2 abil 0 flags 0x7 obsolete 0 cost 5"
run preset147r --save "$OUT/preset147/after.sav" --human 3 --keys "PROTOSTATE"
check "preset147 reload" "$(pr preset147r 204)" \
    "proto 204 name 'Test' chassis 0 weapon 5 armor 11 reactor 2 abil 0 flags 0x7 obsolete 0 cost 5"

#    c) a click on Chaos Squad in the list, o: obsolete (the next design is shown); five Left and a
#       click on Sea Colony Pod, U: the only upgrade (Fusion Sea Colony Pod, YESUPGRADE: no units).
run obsolete147 --save work/saves_late/turn_147.sav --human 3 --keys "DOWN,ENTER,u,CLICK:339x729,SCREENSTATE,o,SCREENSTATE,\
LEFT,LEFT,LEFT,LEFT,LEFT,CLICK:480x729,SCREENSTATE,U,ENTER,SCREENSTATE,ESC,PROTOSTATE"
nocrash obsolete147
check "obsolete147 selected" "$(line obsolete147 designstate 1 | cut -d' ' -f2-4)" "f3 unit 214"
check "obsolete147 next" "$(line obsolete147 designstate 2 | cut -d' ' -f2-4)" "f3 unit 215"
check "obsolete147 obsolete" "$(pr obsolete147 214 | sed 's/.* obsolete //')" "1 cost 3"
check "obsolete147 colony pod" "$(line obsolete147 designstate 3 | cut -d' ' -f2-4)" "f3 unit 200"
has obsolete147 "upgrade done" "^popup YESUPGRADE answer: 0"
check "obsolete147 upgraded to" "$(line obsolete147 designstate 4 | cut -d' ' -f2-4)" "f3 unit 197"

# 5. The other late saves: the workshop opens and Esc leaves the prototypes unchanged (turn_100
#    faction 1, turn_300 faction 6 after its popups and base window).
run design100 --save work/saves_late/turn_100.sav --human 1 --keys "PROTOSTATE,u,SHOT:design.bmp,SCREENSTATE,ESC,\
SCREENSTATE,PROTOSTATE"
nocrash design100
has design100 "opened" "^designstate f1 unit "
check "design100 closed" "$(grep -a '^screenstate' "$OUT/design100.log" | tail -1)" "screenstate none"
check "design100 unchanged" "$(grep -a '^proto ' "$OUT/design100.log" | sort | uniq -u | wc -l)" "0"
run design300 --save work/saves_late/turn_300.sav --human 6 --keys "DOWN,ENTER,ENTER,ENTER,ENTER,ESC,PROTOSTATE,u,SCREENSTATE,ESC,\
PROTOSTATE"
nocrash design300
has design300 "opened" "^designstate f6 unit "
check "design300 unchanged" "$(grep -a '^proto ' "$OUT/design300.log" | sort | uniq -u | wc -l)" "0"

# 6. From the base window (turn_147, University Base): the build menu's last item opens the design
#    workshop, Esc returns to the build menu.
D40=$(printf 'DOWN,%.0s' $(seq 1 40))
run basedesign147 --save work/saves_late/turn_147.sav --human 3 --keys "DOWN,ENTER,BASE:2,c,${D40}ENTER,SCREENSTATE,ESC,ESC,ESC"
nocrash basedesign147
has basedesign147 "design item" "^popup  answer: 32767"
has basedesign147 "workshop open" "^designstate f3 unit 208 "
check "basedesign147 build menu again" "$(grep -a '^popup  answer' "$OUT/basedesign147.log" | sed -n 2p)" "popup  answer: -1 (button 0)"

exit $FAIL
