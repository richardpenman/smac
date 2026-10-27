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
# Animated by default (smacx --animate: slides, unit moves, booms, battle reports and movies play
# with their waits, as in an interactive game); TEST_FAST=1 runs unattended (skips them).
if [ "${TEST_FAST:-0}" = 1 ]; then export SMACX_ANIMATE=0; else export SMACX_ANIMATE=1; fi
# No audio device in the tests (sounds still run through the mixer); set SDL_AUDIODRIVER to override.
export SDL_AUDIODRIVER="${SDL_AUDIODRIVER:-dummy}"
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
#    gift", COUNTER "ALL of my research data" (make_a_proposal -> tech_trade: GAVETECH1; the
#    commlink panel closed by the dialog, MultiWin_hide): the
#    Spartans get the technologies, their friction drops. Saved and loaded again.
HIVE="ENTER,ENTER,ENTER,ENTER,ENTER,ENTER,ENTER,ENTER"
SPARTANS="F12,CLICK:950x382,ENTER,ENTER,ENTER,ENTER,ENTER,ENTER"
run giftech147 --save work/saves_late/turn_147.sav --human 3 --keys \
"$HIVE,$SPARTANS,DIPLOSTATE,DOWN,ENTER,SHOT:proposal.bmp,DOWN,ENTER,SHOT:counter.bmp,DOWN,DOWN,ENTER,ENTER,DIPLOSTATE,\
SHOT:menu.bmp,ENTER,SAVE:after.sav"
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
"$HIVE,$SPARTANS,DIPLOSTATE,DOWN,ENTER,DOWN,ENTER,DOWN,DOWN,DOWN,ENTER,SHOT:offer.bmp,DOWN,ENTER,ENTER,DIPLOSTATE,ENTER,\
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
has council300 "the window slides in (slide_show)" "^slide show 812x395 at "
has council300 "and out (slide_hide)" "^slide hide 812x395 at "
check "council300 open" "$(grep -a '^councilstate' "$OUT/council300.log" | sed -n 1p | cut -d" " -f2-17)" \
    "proposal 0 candidate 6 result 0 governor 3 votes -5 -5 -3 -5 5 0 7"
check "council300 voted" "$(grep -a '^councilstate' "$OUT/council300.log" | sed -n 2p | cut -d" " -f2-17)" \
    "proposal 0 candidate 5 result 1 governor 5 votes -5 -5 -3 -5 5 5 7"
check "council300 closed" "$(grep -a '^screenstate' "$OUT/council300.log" | sed -n 3p)" "screenstate commlink"
check "council300 governor" "$(dl council300 1 0 | sed 's/.*governor/governor/')" "governor 5"
run council300r --save "$OUT/council300/after.sav" --human 6 --keys "DIPLOSTATE"
check "council300 reload governor" "$(dl council300r 1 0 | sed 's/.*governor/governor/')" "governor 5"

# 5. DiploPop's interface buttons during the Hive's call (turn_147, faction 3), clicked while its
#    popup is open (DiploPop_set_modal): PROFILE (report_intel on the Hive), DATALINKS (the Hive's
#    datalink entry, category 15); the right pane starts with the Hive's headquarters
#    (DiploPop_on_status sets HQINFO: DiploPop_draw_hq in place of the world map), a globe button
#    shows the map (HQINFO cleared), HQINFO the headquarters again.
run diplopop147 --save work/saves_late/turn_147.sav --human 3 --keys \
"ENTER,CLICK:630x728,SCREENSTATE,ESC,CLICK:390x728,SCREENSTATE,ESC,SHOT:hq0.bmp,CLICK:930x740,SHOT:map.bmp,CLICK:840x740,SHOT:hq.bmp"
nocrash diplopop147
has diplopop147 "PROFILE" "^diplopop button 1"
has diplopop147 "the Hive's intelligence report" "^screentext \\[HIVE INTELLIGENCE FILES\\]"
has diplopop147 "DATALINKS" "^diplopop button 0"
has diplopop147 "the faction's datalink" "^datalink cat 15 index 2 "
has diplopop147 "HQINFO" "^diplopop button 2"
has diplopop147 "a globe button clears it" "^diplopop globe 1006: hq 0"

# 6. DiploPop's own window: shown and slid up when the dialog starts (700 x 0x1C2 above the
#    console), the dialog's popups (flag 0x100000) drawn into it at its origin and size
#    (BasePop_set_def_external_window), hidden when the dialog ends; its globe buttons: 0x3EF the
#    detail map (MapWins[1]), 0x3EE the world map. In the main interface the globe buttons switch
#    the pane too (WorldWin_set_detail_map / _set_world_map).
run diplowin147 --save work/saves_late/turn_147.sav --human 3 --keys \
"ENTER,SHOT:intro.bmp,CLICK:985x740,SHOT:detail.bmp,CLICK:930x740,ENTER,ENTER,ENTER,ENTER,ENTER,ENTER,ENTER,CLICK:985x740,SHOT:main_detail.bmp,CLICK:930x740"
nocrash diplowin147
has diplowin147 "the window shown" "^diplopop window shown at 178,105 700x450"
has diplowin147 "the greeting in it" "^popup INTRO0 shown: 700x450 at 178,105"
has diplowin147 "the detail map" "^diplopop globe 1007: hq 0"
has diplowin147 "the world map again" "^diplopop globe 1006: hq 0"
has diplowin147 "the window hidden at the end" "^diplopop window hidden"
has diplowin147 "the main interface's detail map" "^worldwin detail map"
if [ "$(grep -ac '^worldwin detail map' "$OUT/diplowin147.log")" = 2 ]; then echo "PASS diplowin147: the main interface's globe"; else echo "FAIL diplowin147: the main interface's globe"; FAIL=1; fi

# 7. The base trade menus' PlanWin preview (proposal_menu): the Spartans asked for one of their
#    bases (PROPOSAL "You must cede control of one of your bases", PROPOSEBASE): PlanWin over the
#    world map pane (above the HQ pane) zoomed on the first base with its point "route"; the
#    commlink panel closed when the dialog started (MultiWin_hide); hidden again after the menu.
run basetrade147 --save work/saves_late/turn_147.sav --human 3 --keys \
"$HIVE,$SPARTANS,DOWN,ENTER,DOWN,DOWN,DOWN,DOWN,DOWN,DOWN,DOWN,ENTER,PLANSTATE,SHOT:base.bmp,ESC,PLANSTATE"
nocrash basetrade147
has basetrade147 "the preview opened" "^planwin open "
has basetrade147 "the first base's point" "^planwin shown 1 zoom -13 centre 24,24 lines 1 \\(22,20\\)-\\(22,20\\) c0"
has basetrade147 "the menu in DiploPop's window" "^popup PROPOSEBASE shown: 700x450 at 178,105"
has basetrade147 "hidden after the menu" "^planwin shown 0 "

exit $FAIL
