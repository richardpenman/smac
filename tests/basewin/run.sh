#!/bin/bash
# Scripted base window checks of smacx (the reconstructed BaseWin, src/ui/basewin*.cpp).
#
#   tests/basewin/run.sh [OUTDIR]        (SMACX=path/to/smacx, default build/bw-native/smacx)
#
# Plays scripted keys (smacx --keys, hidden, SDL dummy video) on late saves as a human faction: opens
# a base window (BASE:N, a test aid; the console's Enter on a base calls the same BaseWin_zoom), works
# tiles by clicks on the city map, changes the production (c: the build menu), adds a queue item
# (click on a queue slot), hurries (h: HURRY popup, pay in full), renames (r), and closes (Esc).
# The base's state after each step is logged by BASESTATE ("basestate ..." lines) and compared with
# the expected values; the game saved after the changes is loaded again to check that they stay.
# Screenshots go to OUTDIR/<case>/. Exit status 0 when every check passes.
set -u
# Animated by default (smacx --animate: slides, unit moves, booms, battle reports and movies play
# with their waits, as in an interactive game); TEST_FAST=1 runs unattended (skips them).
if [ "${TEST_FAST:-0}" = 1 ]; then export SMACX_ANIMATE=0; else export SMACX_ANIMATE=1; fi
# No audio device in the tests (sounds still run through the mixer); set SDL_AUDIODRIVER to override.
export SDL_AUDIODRIVER="${SDL_AUDIODRIVER:-dummy}"
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SMACX="$(realpath "${SMACX:-$ROOT/build/bw-native/smacx}")"
OUT="$(realpath -m "${1:-$ROOT/work/basewin_test}")"
mkdir -p "$OUT"
FAIL=0

run() {  # run CASE ARGS... : runs smacx, log in OUT/CASE.log
    local name="$1"; shift
    mkdir -p "$OUT/$name"
    rm -f "$OUT/$name"/*.bmp
    (cd "$ROOT" && SDL_VIDEODRIVER=dummy SMACX_POPUP_SHOTS="$OUT/$name" timeout 600 "$SMACX" --hidden \
        --outdir "$OUT/$name" "$@" > "$OUT/$name.log" 2>&1)
}

field() {  # field CASE N KEY : the value after KEY in the Nth basestate line
    grep "^basestate" "$OUT/$1.log" | sed -n "$2p" | sed "s/.* $3 \([^ ]*\).*/\1/"
}

queue() {  # queue CASE N : the queue of the Nth basestate line
    grep "^basestate" "$OUT/$1.log" | sed -n "$2p" | sed 's/.* queue \(.*\) state .*/\1/'
}

check() {  # check WHAT GOT WANT
    if [ "$2" == "$3" ]; then echo "PASS $1"; else echo "FAIL $1: got '$2' want '$3'"; FAIL=1; fi
}

aborts() {  # aborts CASE : fails when smacx stopped on a function not reconstructed
    if grep -q "is not reconstructed yet\|Aborted\|abort" "$OUT/$1.log"; then
        echo "FAIL $1 (abort: $(grep -m1 'not reconstructed yet\|abort' "$OUT/$1.log"))"
        FAIL=1
    fi
}

# 1. turn_100 as faction 1 (The Flowers Preach, base 30; no popups at the start of the turn).
#    Clicks: the tile right of the base (worker moves there), the citizen (CITIZEN2: too small
#    for specialists), a queue slot (picker: the first item, INSERT, OK).
run late100 --save work/saves_late/turn_100.sav --human 1 --keys \
"BASE:30,BASESTATE,SHOT:open.bmp,\
CLICK:560x200,BASESTATE,\
c,SHOT:pick.bmp,RIGHT,ENTER,BASESTATE,\
h,SHOT:hurry.bmp,DOWN,ENTER,BASESTATE,\
CLICK:180x606,HOME,CLICK:562x515,CLICK:356x515,BASESTATE,\
CLICK:287x648,ENTER,BASESTATE,\
r,BACK,BACK,BACK,BACK,BACK,BACK,BACK,BACK,BACK,BACK,BACK,BACK,BACK,BACK,BACK,BACK,BACK,BACK,BACK,\
B,l,o,o,m,ENTER,BASESTATE,SHOT:done.bmp,\
ESC,BASESTATE,SAVE:after.sav"
aborts late100
check "late100 open" "$(field late100 1 tiles) $(queue late100 1)" "00000801 67"
check "late100 tile click" "$(field late100 2 tiles)" "00000009"
check "late100 production" "$(queue late100 3) $(field late100 3 state)" "64 83001000"
check "late100 hurry" "$(field late100 4 credits) $(field late100 4 minerals)" "38 16/16"
check "late100 queue add" "$(queue late100 5)" "64 67"
check "late100 citizen" "$(field late100 6 tiles)" "00000009"
grep -q "^basestate base 30 Bloom f1" "$OUT/late100.log" && echo "PASS late100 rename" ||
    { echo "FAIL late100 rename"; FAIL=1; }
check "late100 close" "$(grep '^basestate' "$OUT/late100.log" | tail -1)" "basestate closed"

# The saved game keeps the changes.
run reload --save "$OUT/late100/after.sav" --human 1 --keys "BASE:30,BASESTATE,ESC"
aborts reload
check "reload" "$(grep '^basestate' "$OUT/reload.log" | head -1)" \
    "basestate base 30 Bloom f1 pop 1 tiles 00000009 spec 0 minerals 16/16 credits 38 queue 64 67 state 83001000 gov 00DFBE01"

# 2. turn_120 / turn_147 / turn_300 as faction 3 (University Base, base 2): the build menu's next
#    item (the item after the current one in the list: the next grid cell), a hurry (turn_120: a facility, partial payment of 50 credits; turn_147: a unit, in full),
#    the next / previous base. turn_300: one popup first (Enter), then the turn's messages, so only
#    the absence of aborts is checked there. (The runs are reproducible: smacx seeds the generators,
#    --seed default 0,0; with the original's clock seed the turn's events, e.g. FUNGUSGROWS2, came
#    and went between runs and took the scripted keys.)
for t in 120 147 300; do
    pre=""; [ $t == 300 ] && pre="ENTER,"; [ $t == 147 ] && pre="DOWN,ENTER,"  # 147: the Hive calls (COMM: not now)
    pay="DOWN,ENTER"; [ $t == 120 ] && pay="DOWN,ENTER,BACK,BACK,BACK,BACK,5,0,ENTER"
    pick="RIGHT"; [ $t == 120 ] && pick="DOWN,LEFT,LEFT,LEFT,LEFT"  # the grid cell after the current item
    run late$t --save work/saves_late/turn_$t.sav --human 3 --keys \
"${pre}BASE:2,BASESTATE,SHOT:open.bmp,c,SHOT:pick.bmp,$pick,ENTER,BASESTATE,h,SHOT:hurry.bmp,$pay,BASESTATE,\
PGDN,BASESTATE,SHOT:next.bmp,PGUP,BASESTATE,ESC,BASESTATE"
    aborts late$t
    n=$(grep -c "^basestate base" "$OUT/late$t.log")
    echo "late$t: $n base states; $(grep '^basestate' "$OUT/late$t.log" | cut -d' ' -f2-4,13-17 | tr '\n' ';')"
    [ "$n" -ge 4 ] && echo "PASS late$t" || { echo "FAIL late$t"; FAIL=1; }
done
check "late120 production (facility 2)" "$(queue late120 2)" "-2"
check "late120 partial hurry" "$(field late120 3 credits) $(field late120 3 state)" "76 C3004000"
# (the item after the current one in the build menu: the answer the picker logged)
ans=$(grep -o 'prodwin answer: .* item -*[0-9]*' "$OUT/late147.log" | head -1 | sed 's/.* //')
check "late147 production" "$(queue late147 2)" "$ans"
check "late147 hurry" "$(field late147 3 credits) $(field late147 3 minerals)" "99 24/24"
check "late147 next / previous" "$(field late147 4 base) $(field late147 5 base)" "24 2"

exit $FAIL
