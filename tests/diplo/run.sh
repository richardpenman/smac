#!/bin/bash
# Scripted checks of the diplomacy dialog (communicate, the human's menus, DiploPop) and the
# Planetary Council window (src/ui/diplo_comm.cpp, diplowin.cpp, councilwin.cpp).
#
#   tests/diplo/run.sh [OUTDIR]        (SMACX=path/to/smacx, default build/p8-native/smacx)
#
# Plays scripted keys (smacx --keys, hidden, SDL dummy video) on late saves as a human faction.
# DIPLOSTATE logs the viewer ("diplostate fV credits C techs N bases B governor G") and every
# other living faction ("diplo I status S agenda A friction F patience P loan BAL/PAY credits C
# bases B techs N"); the council window's SCREENSTATE adds "councilstate proposal P candidate C
# result R governor G votes v1..v7 power p1..p7". Games saved after a change are loaded again to
# check that the change stays. Screenshots go to OUTDIR/<case>/. Exit status 0 when every check
# passes.
set -u
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SMACX="$(realpath "${SMACX:-$ROOT/build/p8-native/smacx}")"
OUT="$(realpath -m "${1:-$ROOT/work/diplo_test}")"
mkdir -p "$OUT"
FAIL=0

run() {  # run CASE ARGS... : runs smacx, log in OUT/CASE.log
    local name="$1"; shift
    mkdir -p "$OUT/$name"
    rm -f "$OUT/$name"/*.bmp
    (cd "$ROOT" && SDL_VIDEODRIVER=dummy timeout 600 "$SMACX" --hidden --outdir "$OUT/$name" "$@" > "$OUT/$name.log" 2>&1)
    echo "$?" > "$OUT/$name.rc"
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
dl() {  # dl CASE N I : faction I's line of the Nth DIPLOSTATE (I = 0: the viewer's line)
    if [ "$3" = 0 ]; then
        grep -a "^diplostate" "$OUT/$1.log" | sed -n "$2p"
    else
        awk -v n="$2" -v i="$3" '/^diplostate/{k++} k==n && $1=="diplo" && $2==i' "$OUT/$1.log" | head -1
    fi
}
popups() {  # popups CASE : the popups answered, in order
    grep -a "^popup .* answer:" "$OUT/$1.log" | awk '{print $2 "=" $4}' | tr '\n' ' ' | sed 's/ $//'
}

# 1. turn_147 as faction 3 (University): the Hive calls (communicate from the computer player's
#    turn): talk (COMM), its greeting, its plea against the Believers accepted ("A cunning plan!":
#    pact_of_brotherhood with the Hive, vendetta with the Believers), the pact's map exchange,
#    social priorities, then the DIPLO menu closed. Saved and loaded again.
run aicall147 --save work/saves_late/turn_147.sav --human 3 --keys \
"DIPLOSTATE,ENTER,SHOT:intro.bmp,ENTER,SHOT:propose.bmp,DOWN,ENTER,ENTER,ENTER,ENTER,SHOT:menu.bmp,ENTER,DIPLOSTATE,SAVE:after.sav"
nocrash aicall147
check "aicall147 dialog" "$(popups aicall147)" \
    "COMM=0 INTRO0=0 PROPOSE=1 MAKEPACT=0 HERESTHEMAP=0 SOCIAL0CAT2BAD1=0 DIPLO=0"
check "aicall147 Hive before" "$(dl aicall147 1 2 | cut -d' ' -f1-4)" "diplo 2 status 4800000A"
check "aicall147 Hive pact" "$(dl aicall147 2 2 | cut -d' ' -f1-8)" "diplo 2 status C900000B agenda 00000200 friction 15"
check "aicall147 Believers vendetta" "$(dl aicall147 2 6 | cut -d' ' -f1-4)" "diplo 6 status C8000058"
run aicall147r --save "$OUT/aicall147/after.sav" --human 3 --keys "DIPLOSTATE"
check "aicall147 reload Hive" "$(dl aicall147r 1 2 | cut -d' ' -f1-4)" "diplo 2 status C900000B"
check "aicall147 reload Believers" "$(dl aicall147r 1 6 | cut -d' ' -f1-4)" "diplo 6 status C8000058"

# 2. The human calls the Spartans (F12, their row of the commlink panel: commlink_attempt) after
#    the Hive's call (default answers): their topics, then DIPLO "make a proposal", PROPOSAL "a
#    gift", COUNTER "ALL of my research data" (make_a_proposal -> tech_trade: GAVETECH1): the
#    Spartans get the technologies, their friction drops. Saved and loaded again.
HIVE="ENTER,ENTER,ENTER,ENTER,ENTER,ENTER,ENTER,ENTER"
SPARTANS="F12,CLICK:950x382,ENTER,ENTER,ENTER,ENTER,ENTER,ENTER"
run giftech147 --save work/saves_late/turn_147.sav --human 3 --keys \
"$HIVE,$SPARTANS,DIPLOSTATE,DOWN,ENTER,SHOT:proposal.bmp,DOWN,ENTER,SHOT:counter.bmp,DOWN,DOWN,ENTER,ENTER,DIPLOSTATE,\
SHOT:menu.bmp,ENTER,ESC,SAVE:after.sav"
nocrash giftech147
has giftech147 "commlink to the Spartans" "^multiwin action 1 faction 5"
check "giftech147 menus" "$(popups giftech147 | sed 's/.*DIPLO=0 //')" \
    "INTRO1=0 DEMANDATTACK1b=0 TRADETECH3=0 DEMANDTECH9=0 MILDTREATY=0 SOCIAL2CAT2BAD1=0 DIPLO=2 PROPOSAL=1 COUNTER1=7 GAVETECH1=0 DIPLO=0"
check "giftech147 Spartans before" "$(dl giftech147 1 5 | cut -d" " -f7-8,17-18)" "friction 2 techs 21"
check "giftech147 Spartans after" "$(dl giftech147 2 5 | cut -d" " -f7-8,17-18)" "friction 0 techs 33"
run giftech147r --save "$OUT/giftech147/after.sav" --human 3 --keys "DIPLOSTATE"
check "giftech147 reload" "$(dl giftech147r 1 5 | cut -d' ' -f17-18)" "techs 33"

# 3. The same call, COUNTER "a modest sum of energy credits" (make_gift: OFFERENERGY, the second
#    amount, 75): the credits change hands.
run giftenergy147 --save work/saves_late/turn_147.sav --human 3 --keys \
"$HIVE,$SPARTANS,DIPLOSTATE,DOWN,ENTER,DOWN,ENTER,DOWN,DOWN,DOWN,ENTER,SHOT:offer.bmp,DOWN,ENTER,ENTER,DIPLOSTATE,ENTER,ESC,\
SAVE:after.sav"
nocrash giftenergy147
check "giftenergy147 menus" "$(popups giftenergy147 | sed 's/.*PROPOSAL=1 //')" "COUNTER1=5 OFFERENERGY=75 GAVEENERGY=0 DIPLO=0"
check "giftenergy147 viewer" "$(dl giftenergy147 1 0 | cut -d" " -f2-4) -> $(dl giftenergy147 2 0 | cut -d" " -f2-4)" \
    "f3 credits 195 -> f3 credits 120"
check "giftenergy147 Spartans" "$(dl giftenergy147 1 5 | cut -d' ' -f13-14) -> $(dl giftenergy147 2 5 | cut -d' ' -f13-14)" \
    "credits 34 -> credits 109"
run giftenergy147r --save "$OUT/giftenergy147/after.sav" --human 3 --keys "DIPLOSTATE"
check "giftenergy147 reload" "$(dl giftenergy147r 1 0 | cut -d" " -f2-4) $(dl giftenergy147r 1 5 | cut -d' ' -f13-14)" \
    "f3 credits 120 credits 109"

# 4. turn_300 as faction 6 (the Believers): the Peacekeepers' call first, then the council (the
#    commlink panel's Council button, COUNCILISSUES: the election), the council window: VOTE,
#    GOVVOTE for Santiago, the result (the Spartans elected: GovernorFaction 3 -> 5), OK closes
#    the window. Saved and loaded again.
run council300 --save work/saves_late/turn_300.sav --human 6 --keys \
"ENTER,ENTER,ENTER,ENTER,ENTER,ENTER,ENTER,ENTER,ESC,F12,CLICK:950x478,ENTER,SHOT:window.bmp,SCREENSTATE,CLICK:512x666,\
SHOT:govvote.bmp,DOWN,ENTER,SHOT:result.bmp,SCREENSTATE,ENTER,SCREENSTATE,ESC,DIPLOSTATE,SAVE:after.sav"
nocrash council300
has council300 "council window" "^council window proposal 0"
check "council300 open" "$(grep -a '^councilstate' "$OUT/council300.log" | sed -n 1p | cut -d" " -f2-17)" \
    "proposal 0 candidate 6 result 0 governor 3 votes -5 -5 -3 -5 5 0 7"
check "council300 voted" "$(grep -a '^councilstate' "$OUT/council300.log" | sed -n 2p | cut -d" " -f2-17)" \
    "proposal 0 candidate 5 result 1 governor 5 votes -5 -5 -3 -5 5 5 7"
check "council300 closed" "$(grep -a '^screenstate' "$OUT/council300.log" | sed -n 3p)" "screenstate commlink"
check "council300 governor" "$(dl council300 1 0 | sed 's/.*governor/governor/')" "governor 5"
run council300r --save "$OUT/council300/after.sav" --human 6 --keys "DIPLOSTATE"
check "council300 reload governor" "$(dl council300r 1 0 | sed 's/.*governor/governor/')" "governor 5"

exit $FAIL
