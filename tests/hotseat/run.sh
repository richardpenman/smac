#!/bin/bash
# Hotseat (docs/MULTIPLAYER.md, phase 1): a two-human game set up from the menus, played for
# several turns with the handovers, a treaty and energy traded between the humans in the DiploWin,
# SAVE AND EXIT, and the save reloaded from the HOTSEAT menu.
#
#   tests/hotseat/run.sh [OUTDIR]        (SMACX=path/to/smacx, default build/mp-native/smacx)
#
# Each case runs smacx with scripted keys (hidden, SDL dummy video) and checks its log. Exit status
# 0 when every check passes.
set -u
# Animated by default (smacx --animate); TEST_FAST=1 runs unattended (skips the animations).
if [ "${TEST_FAST:-0}" = 1 ]; then export SMACX_ANIMATE=0; else export SMACX_ANIMATE=1; fi
export SDL_AUDIODRIVER="${SDL_AUDIODRIVER:-dummy}"
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SMACX="$(realpath "${SMACX:-$ROOT/build/mp-native/smacx}")"
OUT="$(realpath -m "${1:-$ROOT/work/hotseat_test}")"
rm -rf "$OUT"
mkdir -p "$OUT"
# The menus' ini: the harness's with Difficulty=2 (as tests/menu).
cat "$ROOT/harness/oracle/runtime/Alpha Centauri.Ini" > "$OUT/hotseat.ini"
echo "Difficulty=2" >> "$OUT/hotseat.ini"
# The first game is played with the original's bugs (the ini's key, as a player sets it): its save
# carries the setting, and the reload (whose ini has the default, the fixes) plays with the save's.
cp "$OUT/hotseat.ini" "$OUT/hotseat_ob.ini"
echo "Original Bugs=1" >> "$OUT/hotseat_ob.ini"
FAIL=0

run() {  # run CASE ARGS... : log in OUT/CASE.log
    local name="$1"; shift
    mkdir -p "$OUT/$name"
    (cd "$ROOT" && SDL_VIDEODRIVER=dummy timeout 900 "$SMACX" --hidden --ini "${INI:-$OUT/hotseat.ini}" --outdir "$OUT/$name" \
        "$@" > "$OUT/$name.log" 2>&1)
    echo "$?" > "$OUT/$name.rc"
}
check() {  # check CASE DESCRIPTION REGEX: the log matches (grep -E)
    if grep -aqE "$3" "$OUT/$1.log"; then echo "PASS $1: $2"; else echo "FAIL $1: $2 (/$3/)"; FAIL=1; fi
}
nocheck() {  # nocheck CASE DESCRIPTION REGEX: the log does not match
    if grep -aqE "$3" "$OUT/$1.log"; then echo "FAIL $1: $2 (/$3/ found)"; FAIL=1; else echo "PASS $1: $2"; fi
}
nocrash() {
    if [ "$(cat "$OUT/$1.rc")" = 0 ] && ! grep -aq "not reconstructed\|abort" "$OUT/$1.log"; then
        echo "PASS $1: ran to the end"
    else
        echo "FAIL $1: exit $(cat "$OUT/$1.rc")"; FAIL=1
    fi
}

END="C-ENTER,DOWN,ENTER"  # end of turn (REALLYOVER: yes)

# 1. MULTIPLAYER -> SELECTSERVICE's hotseat/email item -> HOTSEAT: New Multiplayer Game, a tiny
#    planet, the first faction (University), HOTSEATADD1 "Yes", the second (Pirates), "No".
#    Turn 1, University: SWITCHING with a new password (VERIFYPASSWORD); the commlink panel (F12)
#    calls the Pirates: the DiploWin (diplo): MAKE TREATY, GIVE ENERGY (DIPLOENERGY: 5), ACCEPT
#    OFFER, a chat line, closed; end of turn. Pirates: SWITCHING; the offers wait for them
#    (DiploStateB): the DiploWin opens with the University's offers, ACCEPT OFFER: the deal is done
#    (the treaty, 5 credits); end of turn. Turn 2, University: SWITCHING2, a wrong password, then
#    the right one; COMMCOMPLETE; end of turn. Pirates: SWITCHING's SAVE AND EXIT (saves\hs.SAV).
SETUP="DOWN,DOWN,DOWN,DOWN,ENTER,MENUSTATE,ENTER,MENUSTATE,ENTER,DOWN,ENTER,UP,UP,ENTER,ENTER,ENTER,ENTER,ENTER,DOWN,ENTER,\
ENTER,ENTER,ENTER,DOWN,ENTER,DOWN,ENTER,ENTER,SHOT:players.bmp,ENTER"
T1="SHOT:switching.bmp,a,b,ENTER,a,b,ENTER,F12,CLICK:950x366,CLICK:790x467,CLICK:400x467,5,ENTER,CLICK:330x519,h,i,ENTER,\
SHOT:diplo1.bmp,SCREENSTATE,ESC,ESC,$END"
T2="ENTER,SHOT:diplo2.bmp,CLICK:330x519,SCREENSTATE,ESC,DIPLOSTATE,FACTIONSTATE,$END"
T3="z,z,ENTER,a,b,ENTER,ENTER,ENTER,FACTIONSTATE,$END,CLICK:344x478,h,s,ENTER"
INI="$OUT/hotseat_ob.ini" run game --seed 1001,2001 --keys "$SETUP,$T1,$T2,$T3"
check game "the service list" "^popup SELECTSERVICE answer: 9999"
check game "the HOTSEAT menu" 'menustate caption="HOTSEAT/EMAIL GAME" selected=0 \[NEW MULTIPLAYER GAME\]'
check game "another human player" "^hotseat players 02: add"
check game "two humans" "^hotseat players 06: proceed"
check game "University's handover" "^hotseat: switching to faction 1 \\(no password\\)"
check game "a password set" "^hotseat: faction 1 password set"
check game "the commlink to a human: the DiploWin" "^diplo 1 2"
check game "a treaty offered" "^diplo offer: side 0 type 3 value 0"
check game "energy offered" "^diplo offer: side 0 type 1 value 5"
check game "University agrees" "^diplo: faction 1 agrees"
check game "the chat" "^diplo chat: AKI ZETA-5:  hi"
check game "the offers kept" "^diplostate win 1-2 state 1 0 offers \\[3:0 1:5\\] \\[\\]"
check game "Pirates' handover" "^hotseat: faction 2 takes the turn \\(turn 1\\)"
check game "the DiploWin at the Pirates' turn" "^diplo 2 1"
check game "the deal done" "^diplo: deal done between 2 and 1"
check game "the treaty" "^diplo 1 status 00000002"
check game "the energy" "^factionstate f2 credits 16 "
check game "a wrong password" "^hotseat: wrong password \\(1\\)"
check game "University's turn 2" "^hotseat: faction 1 takes the turn \\(turn 2\\)"
check game "COMMCOMPLETE" "^popup COMMCOMPLETE shown"
check game "saved" "^filebox: saves.hs.SAV"
nocrash game
if [ -f "$OUT/game/saves/hs.SAV" ]; then echo "PASS game: the save written below the outdir"; \
else echo "FAIL game: the save written below the outdir"; FAIL=1; fi

# 2. The save reloaded: MULTIPLAYER -> hotseat -> LOAD MULTIPLAYER GAME (the file box: hs): the
#    Pirates' handover again (the game was saved at it; TECHRANDOM), the treaty kept; University's
#    SWITCHING2 with its password: its turn 3.
mkdir -p "$OUT/reload/saves"
cp "$OUT/game/saves/hs.SAV" "$OUT/reload/saves/" 2>/dev/null
run reload --keys "DOWN,DOWN,DOWN,DOWN,ENTER,ENTER,DOWN,DOWN,ENTER,h,s,ENTER,ENTER,ENTER,ENTER,DIPLOSTATE,$END,\
a,b,ENTER,FACTIONSTATE,SHOT:reload.bmp,QUIT"
check reload "loaded" "^filebox: saves.hs.SAV"
check reload "the save's setting: the original bugs" "^smacx: this game plays with the original bugs \\(the save's setting\\)"
check reload "Pirates' handover" "^hotseat: switching to faction 2 \\(no password\\)"
check reload "the treaty kept" "^diplo 1 status 00000002"
check reload "University's password asked" "^hotseat: switching to faction 1 \\(password\\)"
nocheck reload "the right password" "^hotseat: wrong password"
check reload "University's turn 3" "^hotseat: faction 1 takes the turn \\(turn 3\\)"
check reload "University's credits" "^factionstate f1 credits 8 "
nocrash reload

exit $FAIL
