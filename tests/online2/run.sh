#!/bin/bash
# Online play, part 2 (docs/MULTIPLAYER.md, phase 3): saving and loading a network game, the type
# of game in the setup window (a saved game, a scenario and its files sent to the players), the
# passwords, a player trying to join a game under way and the network windows over the map. Two smacx processes on localhost (the
# host and a client, SDL dummy drivers, scripted keys) as in tests/online/run.sh; each game's
# states are compared turn by turn (harness/compare.py) and the original's checksums must agree.
#
# 1. A network game saved by the host during its second turn (ctrl+S: any player may save; each
#    machine writes its own copy). The host's copy is loaded in a new session: in the setup window
#    the host picks the type of game (NetWin_pick_map: LOAD GAME, the file box, LOADGAME), the
#    players are seated again (each picks one of the saved game's factions: NetWin_pick_faction,
#    the host's password request 0x0F0E), the host launches it (no new world: NetSetupType 2) and
#    sends it to the client (send_rules / send_game). The loaded game goes on at the saved turn on
#    both machines with equal states.
#
#   tests/online2/run.sh [OUTDIR]        (SMACX=path/to/smacx, default build/net-native/smacx)
#
# Runs two smacx processes at once: run it alone. Exit status 0 when every check passes.
set -u
if [ "${TEST_FAST:-0}" = 1 ]; then export SMACX_ANIMATE=0; else export SMACX_ANIMATE=1; fi
export SDL_AUDIODRIVER="${SDL_AUDIODRIVER:-dummy}" SDL_VIDEODRIVER=dummy
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SMACX="$(realpath "${SMACX:-$ROOT/build/net-native/smacx}")"
OUT="$(realpath -m "${1:-$ROOT/work/online2_test}")"
# ONLY="load ..." runs those sessions alone (their inputs from an earlier run kept in OUTDIR).
ONLY="${ONLY:-}"
want() { [ -z "$ONLY" ] || [[ " $ONLY " == *" $1 "* ]]; }
[ -z "$ONLY" ] && rm -rf "$OUT"
mkdir -p "$OUT"
FAIL=0

check() {  # check LOG DESCRIPTION REGEX
    if grep -aqE "$3" "$OUT/$1.log"; then echo "PASS $1: $2"; else echo "FAIL $1: $2 (/$3/)"; FAIL=1; fi
}
nocheck() {
    if grep -aqE "$3" "$OUT/$1.log"; then echo "FAIL $1: $2 (/$3/ found)"; FAIL=1; else echo "PASS $1: $2"; fi
}
count() {  # count LOG DESCRIPTION REGEX N: at least N lines match
    local n; n=$(grep -acE "$3" "$OUT/$1.log")
    if [ "$n" -ge "$4" ]; then echo "PASS $1: $2 ($n)"; else echo "FAIL $1: $2 ($n < $4)"; FAIL=1; fi
}

END="C-ENTER,DOWN,ENTER"  # end of turn (REALLYOVER)
W="WAITFOR:netmsg SIMULWHOSE,ENTER"  # the other player's turn (a modal NetMsg_pop)
HTURN="WAITFOR:player turn faction 1,ENTER,$END,$W"
CTURN0="WAITFOR:player turn faction 2,ENTER,$END"
CTURN="$W,$CTURN0"
# MULTIPLAYER, Internet (TCP/IP), host / join (the TCPHOST / TCPJOIN popups as the prefs fill them).
HSETUP="DOWN,DOWN,DOWN,DOWN,ENTER,DOWN,ENTER,ENTER,ENTER,WAITFOR:netmsg ADDED,ENTER"
CSETUP="DOWN,DOWN,DOWN,DOWN,ENTER,DOWN,ENTER,DOWN,ENTER,ENTER,WAITFOR:setup window open"
# The setup window's spots (screen coordinates): its button (LAUNCH GAME / MAKE READY), the type of
# game (the session settings' third line), the faction of row 1 (the host) and row 2 (the client),
# row 2's password lock.
READY="CLICK:547x306"
GAMETYPE="CLICK:200x202"
HFACTION="CLICK:715x152"
CFACTION="CLICK:715x174"
CLOCK="CLICK:455x174"

# session NAME HOSTKEYS CLIENTKEYS [HOSTDIR_FROM]: one game; logs OUT/NAME-host.log, OUT/NAME-client.log.
# HOSTDIR_FROM: an earlier session whose saves the host starts with.
session() {
    local name="$1" hk="$2" ck="$3" from="${4:-}"
    local port=$((29000 + RANDOM % 2000))
    mkdir -p "$OUT/$name-host/snap" "$OUT/$name-client/snap"
    [ -n "$from" ] && cp -r "$OUT/$from-host/saves" "$OUT/$name-host/"
    cat "$ROOT/harness/oracle/runtime/Alpha Centauri.Ini" > "$OUT/$name.ini"
    printf 'Difficulty=2\r\nNet Port=%s\r\nNet Address=127.0.0.1\r\nNet Name=Host\r\n%b' $port "${HOSTINI:-}" >> "$OUT/$name.ini"
    grep -av "^Latest " "$OUT/$name.ini" > "$OUT/$name-client.ini"
    sed -i 's/^Net Name=Host/Net Name=Client/' "$OUT/$name-client.ini"
    (cd "$ROOT" && timeout 600 "$SMACX" --hidden --seed 1001,2001 --ini "$OUT/$name.ini" --outdir "$OUT/$name-host" \
        --snapshots "$OUT/$name-host/snap" --keys "$hk" > "$OUT/$name-host.log" 2>&1; echo $? > "$OUT/$name-host.rc") &
    local hp=$!
    sleep 2
    (cd "$ROOT" && timeout 600 "$SMACX" --hidden --seed 1002,2002 --ini "$OUT/$name-client.ini" \
        --outdir "$OUT/$name-client" --snapshots "$OUT/$name-client/snap" --keys "$ck" \
        > "$OUT/$name-client.log" 2>&1; echo $? > "$OUT/$name-client.rc") &
    local cp=$!
    wait $hp $cp
}
compare() {  # compare NAME DESCRIPTION MINTURNS [ALLOW]
    python3 "$ROOT/harness/compare.py" "$OUT/$1-host/snap" "$OUT/$1-client/snap" --all --max 100000 \
        > "$OUT/$1-compare.txt" 2>&1
    local allow='Monuments\[|MapAbstractData|next_veh_id_stack|prev_veh_id_stack|Vehs\[[0-9]+\]\.state\+3|^turn_|more$'
    [ -n "${4:-}" ] && allow="$allow|$4"
    local n; n=$(grep -c "^turn_" "$OUT/$1-compare.txt")
    if [ "$n" -ge "$3" ] && ! grep -vqE "$allow" "$OUT/$1-compare.txt"; then
        echo "PASS $1: $2 ($n turns)"
    else
        echo "FAIL $1: $2 (see $OUT/$1-compare.txt)"; FAIL=1
    fi
}
common() {  # common LOG: no checksum difference, no crash
    nocheck $1 "the checksums agree" "checksum [0-9]+ differs"
    nocheck $1 "no crash" "not reconstructed|abort|Assertion"
}

# 1a. The game saved: the host saves at the start of its second turn (saves\net.SAV). In the setup
# window the client chose its faction (the second of the list) and gave it a password (its row's
# lock: NetWin_pick_pass, ENTERPASSWORDNEW and VERIFYPASSWORD; the faction keeps it in the game).
CPASS="$CFACTION,DOWN,DOWN,ENTER,WAITFOR:player 2 ready 0 faction 1,WAIT:300,$CLOCK,WAIT:500,p,w,ENTER,WAIT:500,p,w,ENTER,\
WAITFOR:net: password set"
HSAVE="WAITFOR:player turn faction 1,ENTER,WAIT:500,C-s,WAIT:300,n,e,t,ENTER,WAIT:800,$END,$W"
want save && session save "$HSETUP,WAITFOR:player 2 ready 1,$READY,WAITFOR:game started,ENTER,$HTURN,$HSAVE,WAITFOR:player turn faction 1,WAIT:1000" \
    "$CSETUP,WAIT:500,$CPASS,$READY,WAITFOR:game started,ENTER,$CTURN,$CTURN,WAITFOR:player turn faction 1,WAIT:500"
if want save; then
    check save-host "the network game saved" "^filebox: saves.net.SAV"
    check save-client "a new password asked (ENTERPASSWORDNEW)" "^popup ENTERPASSWORDNEW shown"
    check save-client "and verified (VERIFYPASSWORD)" "^popup VERIFYPASSWORD shown"
    check save-client "the client's faction has a password" "^net: password set \(on\)"
    for side in host client; do common save-$side; done
    if [ -f "$OUT/save-host/saves/net.SAV" ]; then echo "PASS save-host: saves/net.SAV written"; else
        echo "FAIL save-host: saves/net.SAV written"; FAIL=1; fi
fi

# 1b. The saved game loaded: the host's type of game (the third line of the session settings),
# LOAD GAME (the fourth item), the file box (net), LOADGAME (the window then counts the password no
# player gave yet); its faction (the first one listed); the client waits for the host's choice and
# picks its own (the second listed, its faction of the saved game): a wrong password first
# (ENTERPASSWORDFACTION, BADPASSWORD: the faction not taken), then the right one; it makes ready;
# the host launches. Two rounds then, to turn 3's upkeep (a secret project's movie
# follows in the next one).
HLOAD="WAIT:500,$GAMETYPE,DOWN,DOWN,DOWN,ENTER,WAIT:300,n,e,t,ENTER,WAIT:800,ENTER,\
WAITFOR:net: type of game 2,WAIT:300,SCREENSTATE,$HFACTION,DOWN,ENTER,WAITFOR:player 1 ready 0 faction 1,WAITFOR:player 2 ready 1 faction 2,$READY,\
WAITFOR:game started"
CLOAD="WAITFOR:net: session type 2,WAITFOR:player 1 ready 0 faction 1,WAIT:300,$CFACTION,DOWN,ENTER,WAIT:500,x,y,ENTER,\
WAITFOR:net: bad password,ENTER,WAIT:300,$CFACTION,DOWN,ENTER,WAIT:500,p,w,ENTER,WAITFOR:player 2 ready 0 faction 2,WAIT:300,$READY,WAITFOR:game started"
if want load; then
    rm -rf "$OUT"/load-*; session load "$HSETUP,$HLOAD,WAIT:1000,$HTURN,$HTURN,WAITFOR:turn 3 upkeep,WAIT:2000" \
        "$CSETUP,$CLOAD,$CTURN,$CTURN,WAITFOR:turn 3 upkeep,WAIT:2000" save
    check load-host "the type of game: a saved game" "^net: type of game 2 \(net\)$"
    check load-client "the client told (NetSetupType 2)" "^net: session type 2 \(net\)$"
    check load-host "the client's password not given yet" "^screentext Passwords Unsatisfied: 1$"
    check load-client "its faction's password asked (ENTERPASSWORDFACTION)" "^popup ENTERPASSWORDFACTION shown"
    check load-client "a wrong one refused (BADPASSWORD)" "^popup BADPASSWORD shown"
    for side in host client; do
        log="load-$side"
        check $log "the saved game launched (the two humans re-seated)" "^net: game started: faction [12], humans 06"
        count $log "the turns' upkeep in lockstep" "^net: turn [0-9]+ upkeep" 2
        common $log
    done
    # The loaded game went on from the saved turn: saved in the round after turn 1's upkeep, its first
# upkeep is turn 2's on both machines.
    for side in host client; do
        if grep -aE "^net: turn [0-9]+ upkeep" "$OUT/load-$side.log" | head -1 | grep -q "turn 2 upkeep"; then
            echo "PASS load-$side: the game goes on from the saved turn"
        else echo "FAIL load-$side: the game goes on from the saved turn"; FAIL=1; fi
    done
    compare load "host and client states equal each turn after the load" 2
fi

# 2. A scenario in the setup window (LOAD SCENARIO: a two-player scenario of the game, Dogpile;
# the file box starts in its folder, the prefs' Latest Scenario) and its files sent to the client
# (NetDaemon_send_files: the client has none of them; MPSENDFILE / MPRECEIVEFILE), then a turn
# played (simultaneous moves, the scenario's rule).
SCEN='scenarios\Alpha Centauri Scenarios\MP--2 PlayerA\Dogpile'
if want scen; then
    rm -rf "$OUT"/scen-*
    # The host plays the first faction listed (the scenario's faction 2), the client the second (5).
    # The scenario's turns are simultaneous: the client ends its turn first and waits for the host
    # (FINISHINGTURN, a modal NetMsg_pop answered).
    HT1="WAITFOR:net: turn 1 moves,WAIT:4000,$END"
    CT1="WAITFOR:net: turn 1 moves,WAIT:500,$END,WAITFOR:netmsg FINISHINGTURN,ENTER"
    HOSTINI="Latest Scenario=$SCEN\r\n" session scen \
        "$HSETUP,WAIT:500,$GAMETYPE,DOWN,DOWN,ENTER,WAIT:800,ENTER,WAITFOR:net: type of game 3,WAIT:300,$HFACTION,DOWN,ENTER,\
WAITFOR:player 2 ready 1,$READY,WAITFOR:game started,WAIT:1500,ENTER,$HT1,WAITFOR:turn 1 upkeep,WAIT:2000" \
        "$CSETUP,WAITFOR:net: session type 3,WAITFOR:player 1 ready 0 faction 2,WAIT:300,$CFACTION,DOWN,ENTER,WAITFOR:player 2 ready 0 faction 5,\
WAIT:300,$READY,WAITFOR:game started,WAIT:1500,ENTER,$CT1,WAITFOR:turn 1 upkeep,WAIT:2000"
    check scen-host "the type of game: a scenario" "^net: type of game 3 \(Dogpile\)$"
    check scen-client "the client told (NetSetupType 3)" "^net: session type 3 \(Dogpile\)$"
    check scen-host "the files sent (MPSENDFILE)" "^netmsg MPSENDFILE: .*Dogpile.sc"
    check scen-host "every file asked by the client" "^net: scenario files sent \(3 files, 3 asked\)"
    check scen-client "the scenario's directory" "^net: scenario directory scenarios.Alpha Centauri Scenarios.MP--2 PlayerA"
    check scen-client "the files received (MPRECEIVEFILE)" "^netmsg MPRECEIVEFILE: .*Dogpile.sc"
    for f in Dogpile.sc scenario.txt Units.pcx; do
        if cmp -s "$OUT/scen-client/scenarios/Alpha Centauri Scenarios/MP--2 PlayerA/$f" \
            "$ROOT/game/scenarios/Alpha Centauri Scenarios/MP--2 PlayerA/$f"; then
            echo "PASS scen-client: $f received intact"
        else echo "FAIL scen-client: $f received intact"; FAIL=1; fi
    done
    for side in host client; do
        log="scen-$side"
        check $log "the scenario launched (its humans 2 and 5)" "^net: game started: faction [25], humans 24, map 64x64"
        check $log "the scenario's introduction" "^popup INTRO shown"
        check $log "the first turn's upkeep" "^net: turn 1 upkeep"
        common $log
    done
    # (The scenario's ranking history, FactionTurnMight, stays the host's: send_game sends it only
    # after turn 0, as the original.)
    compare scen "host and client states equal after the first turn" 1 'FactionTurnMight'
fi

# 3. Joining a game under way: the original has no such thing (NetDaemon_on_player_added refuses a
# player once the setup window closed: 0x1F01, NOJOIN). The host launches a game alone; the client
# connects during the host's first turn and is refused.
if want join; then
    rm -rf "$OUT"/join-*
    session join "DOWN,DOWN,DOWN,DOWN,ENTER,DOWN,ENTER,ENTER,ENTER,WAITFOR:setup window open,WAIT:500,$READY,\
WAITFOR:game started,ENTER,WAITFOR:player turn faction 1,ENTER,WAITFOR:net: player refused,WAIT:500,$END,WAITFOR:player turn faction 1,WAIT:500" \
        "WAIT:12000,DOWN,DOWN,DOWN,DOWN,ENTER,DOWN,ENTER,DOWN,ENTER,ENTER,WAITFOR:netmsg NOJOIN,ENTER,WAIT:1000"
    check join-host "the host's game alone" "^net: game started: faction 1, humans 02"
    check join-host "the late player refused (0x1F01)" "^net: player refused \(Client\)"
    check join-client "told it cannot join (NOJOIN)" "^netmsg NOJOIN"
    check join-client "no game for it" "^net: setup window closed \(cancel"
    nocheck join-client "no game for it" "^net: game started"
    count join-host "the host plays on" "^net: player turn faction 1" 2
    common join-host
fi

# 4. The game's network windows over the map: the commlink panel modeless beside the map (F12 shows
# it, F12 hides it: MainInterface_on_iface_button_clicked 0x3EC), the closed panel's time row
# clicked (Console_time_controls: TIMECONTROLS, cancelled) and the NetMsg box's Close button
# clicked (NetMsg_on_button_clicked; the box shown by the test aid NETMSG).
if want panel; then
    rm -rf "$OUT"/panel-*
    HPANEL="WAITFOR:player turn faction 1,ENTER,WAIT:1000,F12,WAIT:500,SCREENSTATE,F12,WAIT:500,CLICK:950x485,WAIT:500,ESC,\
WAIT:300,NETMSG:UPKEEP4,WAIT:300,CLICK:511x72,WAIT:300,$END,$W"
    session panel "$HSETUP,WAITFOR:player 2 ready 1,$READY,WAITFOR:game started,ENTER,$HPANEL,WAITFOR:player turn faction 1,WAIT:500" \
        "$CSETUP,WAIT:500,$READY,WAITFOR:game started,ENTER,$CTURN,WAITFOR:player turn faction 1,WAIT:500"
    check panel-host "the panel shown, modeless" "^multiwin shown \(modeless\)"
    check panel-host "its rows (the time row)" "^screentext No Time Control$"
    check panel-host "hidden again (F12)" "^multiwin hidden"
    check panel-host "the closed panel's time row clicked" "^multiwin time row clicked"
    check panel-host "the time controls (TIMECONTROLS)" "^popup TIMECONTROLS shown"
    # (its text rows are 20 pixels high, Arial Narrow's GDI height as in the original)
    check panel-host "the NetMsg box shown" "^netmsg box UPKEEP4: OK at 511,74"
    check panel-host "its Close button clicked" "^netmsg OK clicked"
    count panel-host "the turns go on" "^net: player turn faction 1" 2
    for side in host client; do common panel-$side; done
fi

exit $FAIL
