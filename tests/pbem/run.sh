#!/bin/bash
# PBEM (docs/MULTIPLAYER.md, phase 2): a two-human hotseat/email game passed back and forth by its
# save file between two "machines" (two outdirs, each with its own saves folder and tamper records,
# tamper.reg: the original's registry values), over several turns, with passwords, SAVE AND EXIT at
# each handover, a wrong password on load, a save loaded twice (HOTNOTAMPER) and the other player
# told (HOTTAMPER2), and the passworded save refused as a single-player load (PASSWORDEDLOADATTEMPT).
#
#   tests/pbem/run.sh [OUTDIR]        (SMACX=path/to/smacx, default build/net-native/smacx)
#
# Each step runs smacx with scripted keys (hidden, SDL dummy video) and checks its log. Exit status
# 0 when every check passes.
set -u
# Animated by default (smacx --animate); TEST_FAST=1 runs unattended (skips the animations).
if [ "${TEST_FAST:-0}" = 1 ]; then export SMACX_ANIMATE=0; else export SMACX_ANIMATE=1; fi
export SDL_AUDIODRIVER="${SDL_AUDIODRIVER:-dummy}"
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SMACX="$(realpath "${SMACX:-$ROOT/build/net-native/smacx}")"
OUT="$(realpath -m "${1:-$ROOT/work/pbem_test}")"
rm -rf "$OUT"
mkdir -p "$OUT/a/saves" "$OUT/b/saves"
cat "$ROOT/harness/oracle/runtime/Alpha Centauri.Ini" > "$OUT/pbem.ini"
echo "Difficulty=2" >> "$OUT/pbem.ini"
# Machine A sets the game up with the original's bugs (the ini's key, as a player sets it); every
# later step's ini has the default (the fixes): the game keeps the setting its save carries.
cp "$OUT/pbem.ini" "$OUT/pbem_ob.ini"
echo "Original Bugs=1" >> "$OUT/pbem_ob.ini"
FAIL=0

run() {  # run STEP MACHINE ARGS... : the machine's outdir (a or b), log in OUT/STEP.log
    local name="$1" dir="$OUT/$2"; shift 2
    (cd "$ROOT" && SDL_VIDEODRIVER=dummy timeout 900 "$SMACX" --hidden --ini "${INI:-$OUT/pbem.ini}" --outdir "$dir" \
        "$@" > "$OUT/$name.log" 2>&1)
    echo "$?" > "$OUT/$name.rc"
}
mail() {  # mail FROM TO: the save sent by e-mail (copied from one machine's saves to the other's)
    if [ -f "$OUT/$1/saves/pb.SAV" ]; then
        cp "$OUT/$1/saves/pb.SAV" "$OUT/$2/saves/pb.SAV"; echo "PASS mail $1 -> $2: pb.SAV"
    else
        echo "FAIL mail $1 -> $2: no pb.SAV"; FAIL=1
    fi
}
check() {  # check STEP DESCRIPTION REGEX: the log matches (grep -E)
    if grep -aqE "$3" "$OUT/$1.log"; then echo "PASS $1: $2"; else echo "FAIL $1: $2 (/$3/)"; FAIL=1; fi
}
nocheck() {  # nocheck STEP DESCRIPTION REGEX: the log does not match
    if grep -aqE "$3" "$OUT/$1.log"; then echo "FAIL $1: $2 (/$3/ found)"; FAIL=1; else echo "PASS $1: $2"; fi
}
nocrash() {
    if [ "$(cat "$OUT/$1.rc")" = 0 ] && ! grep -aq "not reconstructed\|abort" "$OUT/$1.log"; then
        echo "PASS $1: ran to the end"
    else
        echo "FAIL $1: exit $(cat "$OUT/$1.rc")"; FAIL=1
    fi
}

END="C-ENTER,DOWN,ENTER"           # end of turn (REALLYOVER: yes)
SAVEEXIT="CLICK:344x478,p,b,ENTER"  # SWITCHING's SAVE AND EXIT, the file box: saves\pb.SAV
LOAD="DOWN,DOWN,DOWN,DOWN,ENTER,ENTER,DOWN,DOWN,ENTER,p,b,ENTER"  # MULTIPLAYER, hotseat/email, LOAD

# 1. Machine A: the game set up (University and Pirates, as tests/hotseat); University's turn 1 with
#    a new password (ab); the Pirates' handover: SAVE AND EXIT. Mailed to B.
SETUP="DOWN,DOWN,DOWN,DOWN,ENTER,MENUSTATE,ENTER,MENUSTATE,ENTER,DOWN,ENTER,UP,UP,ENTER,ENTER,ENTER,ENTER,ENTER,DOWN,ENTER,\
ENTER,ENTER,ENTER,DOWN,ENTER,DOWN,ENTER,ENTER,ENTER"
INI="$OUT/pbem_ob.ini" run a1 a --seed 1001,2001 --keys "$SETUP,a,b,ENTER,a,b,ENTER,$END,$SAVEEXIT"
check a1 "University's password set" "^hotseat: faction 1 password set"
check a1 "Pirates' handover" "^hotseat: switching to faction 2 \\(no password\\)"
check a1 "saved and exited" "^filebox: saves.pb.SAV"
check a1 "the PBEM save count" "^pbem: saved and exited, save count 1$"
nocrash a1
mail a b

# 2. Machine B: loaded from the HOTSEAT menu (LOADGAME); the Pirates' handover with a new password
#    (cd); their turn 1; University's handover (SWITCHING2): SAVE AND EXIT. Mailed to A.
run b1 b --keys "$LOAD,ENTER,c,d,ENTER,c,d,ENTER,FACTIONSTATE,$END,$SAVEEXIT"
check b1 "loaded" "^filebox: saves.pb.SAV"
check b1 "the save's setting: the original bugs" "^smacx: this game plays with the original bugs \(the save's setting\)"
check b1 "first load of this save: a new tamper record" "^pbem: tamper record new \(turn 1 faction 2 save 1 loads 1\)"
nocheck b1 "no tampering told" "^popup HOTNOTAMPER shown"
check b1 "Pirates' password set" "^hotseat: faction 2 password set"
check b1 "Pirates' turn 1" "^hotseat: faction 2 takes the turn \(turn 1\)"
check b1 "University's handover: its password" "^hotseat: switching to faction 1 \(password\)"
check b1 "saved and exited" "^pbem: saved and exited, save count 2$"
nocrash b1
mail b a

# 3. Machine A: University's SWITCHING2: a wrong password, then the right one; its turn 2; the
#    Pirates' handover: SAVE AND EXIT. Mailed to B.
run a2 a --keys "$LOAD,ENTER,z,z,ENTER,a,b,ENTER,ENTER,FACTIONSTATE,$END,$SAVEEXIT"
check a2 "University's password asked" "^hotseat: switching to faction 1 \(password\)"
check a2 "a wrong password" "^hotseat: wrong password \(1\)"
check a2 "University's turn 2" "^hotseat: faction 1 takes the turn \(turn 2\)"
check a2 "saved and exited" "^pbem: saved and exited, save count 3$"
nocrash a2
mail a b

# 4. Machine B: the Pirates load the save and play, then quit without saving ...
run b2 b --keys "$LOAD,ENTER,c,d,ENTER,ENTER,FACTIONSTATE,QUIT"
check b2 "a new tamper record" "^pbem: tamper record new \(turn 2 faction 2 save 3 loads 1\)"
check b2 "Pirates' turn 2" "^hotseat: faction 2 takes the turn \(turn 2\)"
nocrash b2
# 5. ... and load the same save again: HOTNOTAMPER (the other players will be told); the turn is
#    played; University's handover: SAVE AND EXIT. Mailed to A.
run b3 b --keys "$LOAD,ENTER,ENTER,c,d,ENTER,ENTER,$END,$SAVEEXIT"
check b3 "the save loaded again: the record found" "^pbem: tamper record again \(turn 2 faction 2 save 3 loads 2\)"
check b3 "HOTNOTAMPER" "^popup HOTNOTAMPER shown"
check b3 "Pirates' turn 2" "^hotseat: faction 2 takes the turn \(turn 2\)"
check b3 "saved and exited" "^pbem: saved and exited, save count 4$"
nocrash b3
mail b a

# 6. Machine A: University's turn 3: told of the Pirates' extra load (HOTTAMPER2).
run a3 a --keys "$LOAD,ENTER,a,b,ENTER,ENTER,FACTIONSTATE,QUIT"
check a3 "University's turn 3" "^hotseat: faction 1 takes the turn \(turn 3\)"
check a3 "still the save's setting" "^smacx: this game plays with the original bugs \(the save's setting\)"
check a3 "the tampering told: HOTTAMPER2" "^popup HOTTAMPER2 shown"
nocrash a3

# 7. The passworded save loaded as a single-player game (LOAD GAME): refused.
run sp a --keys "DOWN,DOWN,DOWN,ENTER,p,b,ENTER,ENTER,QUIT"
check sp "loaded" "^filebox: saves.pb.SAV"
check sp "PASSWORDEDLOADATTEMPT" "^popup PASSWORDEDLOADATTEMPT shown"
nocheck sp "no turn played" "^hotseat: faction . takes the turn"
nocrash sp

exit $FAIL
