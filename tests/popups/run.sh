#!/bin/bash
# Scripted popup checks of smacx (the reconstructed BasePop / Popup, docs/UI_PLAN.md section 6).
#
#   tests/popups/run.sh [OUTDIR]        (SMACX=path/to/smacx, default build/pp-native/smacx)
#
# Plays scripted keys (smacx --keys, hidden, SDL dummy video) through popups of a new game, popups
# shown by label (POPUP:...), mouse clicks (CLICK:...) and three turns of a late save, and checks the
# answers the popups log ("popup LABEL answer: N"). Screenshots of every popup go to
# OUTDIR/<case>/pop_NNN_LABEL.bmp (SMACX_POPUP_SHOTS). Exit status 0 when every check passes.
set -u
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SMACX="$(realpath "${SMACX:-$ROOT/build/pp-native/smacx}")"
OUT="$(realpath -m "${1:-$ROOT/work/popup_test}")"
mkdir -p "$OUT"
FAIL=0

run() {  # run CASE ARGS... : runs smacx, log in OUT/CASE.log
    local name="$1"; shift
    mkdir -p "$OUT/$name"
    rm -f "$OUT/$name"/*.bmp
    (cd "$ROOT" && SDL_VIDEODRIVER=dummy SMACX_POPUP_SHOTS="$OUT/$name" timeout 600 "$SMACX" --hidden \
        --outdir "$OUT/$name" "$@" > "$OUT/$name.log" 2>&1)
}

expect() {  # expect CASE "LABEL answer: N" ... : the answers, in this order
    local name="$1"; shift
    local got
    got=$(grep -o "popup [A-Z0-9_]* answer: -\?[0-9]*" "$OUT/$name.log" | sed 's/^popup //')
    local want
    want=$(printf '%s\n' "$@")
    if [ "$got" == "$want" ]; then
        echo "PASS $name"
    else
        echo "FAIL $name"
        echo "  want: $(echo $want)"
        echo "  got:  $(echo $got)"
        FAIL=1
    fi
}

# 1. A new game: the crash landing's base name (typed, with a backspace), the end-of-turn and quit
#    questions by keys, then popups by label.
run newgame --new size=0 --human 1 --seed 1,2 --keys \
"N,e,w,SPACE,H,o,p,e,BACK,ENTER,\
C-ENTER,ESC,\
C-ENTER,DOWN,UP,ENTER,\
C-q,ENTER,\
POPUP:SCRIPT/BASEWARNINGS,DOWN,SPACE,DOWN,DOWN,SPACE,ENTER,\
POPUP:SCRIPT/NEWBASE/44,ESC,\
POPUP:SCRIPT/NEWBASE/44,ENTER,\
POPUP:SCRIPT/COUNCILISSUES/40/council_sm.pcx,ESC,\
POPUP:SCRIPT/MENUSIMPLE,ESC,\
POPUP:SCRIPT/REALLYQUIT/0/space_sm.pcx,DOWN,ESC,\
C-ENTER,DOWN,ENTER,\
C-q,ESC"
expect newgame "FIRSTBASE answer: 0" "REALLYOVER answer: 0" "REALLYOVER answer: 0" "REALLYQUIT answer: 0" \
    "BASEWARNINGS answer: 10" "NEWBASE answer: -1" "NEWBASE answer: 0" "COUNCILISSUES answer: -1" \
    "MENUSIMPLE answer: 0" "REALLYQUIT answer: 1" "REALLYOVER answer: 1" "REALLYQUIT answer: 0"

# 2. Mouse: REALLYQUIT, click the second item, then OK (quits the game: answer 1).
run mouse --new size=0 --human 1 --seed 1,2 --keys "ENTER,C-q,CLICK:400x452,CLICK:500x483"
expect mouse "FIRSTBASE answer: 0" "REALLYQUIT answer: 1"

# 3. Late saves played as a human faction: the popups the turns bring are answered with Enter (the
#    first item / OK). turn_100 as faction 1: another faction's secret project (BLURBS + PRODUCE2);
#    turn_200 as faction 2: probe team results (sabotage, incited drones), unit losses, morale.
TURN="C-ENTER,DOWN,ENTER,ENTER,ENTER,ENTER"
run late100 --save work/saves_late/turn_100.sav --human 1 --keys "$TURN,$TURN,$TURN"
run late200 --save work/saves_late/turn_200.sav --human 2 --keys "$TURN,$TURN,$TURN,$TURN,$TURN,$TURN,$TURN,$TURN"
for c in late100 late200; do
    echo "$c popups: $(grep -o 'popup [A-Z0-9_]* \(answer: -\?[0-9]*\|as message\)' "$OUT/$c.log" | sed 's/^popup //' | sort | uniq -c | tr -s ' ' | tr '\n' ';')"
done
grep -q "PRODUCE2 answer: 0" "$OUT/late100.log" && echo "PASS late100" || { echo "FAIL late100"; FAIL=1; }
for l in FACVIRUS1 DRONEINCITE1; do
    grep -q "$l answer: 0" "$OUT/late200.log" || { echo "FAIL late200 ($l)"; FAIL=1; }
done
grep -q "abort\|not reconstructed yet" "$OUT/late200.log" && { echo "FAIL late200 (abort)"; FAIL=1; }
echo "late200 checked"

exit $FAIL
