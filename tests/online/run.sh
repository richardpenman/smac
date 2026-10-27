#!/bin/bash
# Online play (docs/MULTIPLAYER.md, phase 3): two smacx processes on localhost, a host and a client
# (SDL dummy drivers, scripted keys), set up a game through SELECTSERVICE's Internet (TCP/IP) item,
# the TCPHOST / TCPJOIN popups and the MULTIPLAYER SETUP window, then play turns one player at a
# time (GameMoreRules 0x10, the setup's default) with the computer factions' turns in lockstep.
# Each process writes its game state before every turn_upkeep (--snapshots); the two copies are
# compared turn by turn (harness/compare.py) and must agree but for the data the original never
# sends to the players (the monuments, the abstract map, the unused world-generation generator) and
# each machine's own UI state (unit stacking order, a unit's focus bit). The original's desync check
# runs each turn (NetDaemon_game_checksum, net_upkeep: no "checksum differs"). A second game
# provokes a desync (SMACX_NET_DESYNC=TURN: the client changes a unit's morale at that turn): the host finds
# the vehicles' checksum different and resends the units; the next turn's states agree again.
#
#   tests/online/run.sh [OUTDIR]        (SMACX=path/to/smacx, default build/net-native/smacx)
#
# Runs two smacx processes at once: run it alone. Exit status 0 when every check passes.
set -u
if [ "${TEST_FAST:-0}" = 1 ]; then export SMACX_ANIMATE=0; else export SMACX_ANIMATE=1; fi
export SDL_AUDIODRIVER="${SDL_AUDIODRIVER:-dummy}" SDL_VIDEODRIVER=dummy
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
SMACX="$(realpath "${SMACX:-$ROOT/build/net-native/smacx}")"
OUT="$(realpath -m "${1:-$ROOT/work/online_test}")"
rm -rf "$OUT"
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

# game NAME [ENV...]: the host and the client of one game (keys below), logs OUT/NAME-host.log, OUT/NAME-client.log
END="C-ENTER,DOWN,ENTER"  # end of turn (REALLYOVER)
HTURN="WAITFOR:player turn faction 1,ENTER,$END"
CTURN="WAITFOR:player turn faction 2,ENTER,$END"
HOST="DOWN,DOWN,DOWN,DOWN,ENTER,DOWN,ENTER,ENTER,ENTER,WAITFOR:player 2 ready 1,ENTER,WAITFOR:game started,ENTER,\
$HTURN,$HTURN,$HTURN,WAITFOR:player turn faction 1,WAIT:2000"
CLIENT="DOWN,DOWN,DOWN,DOWN,ENTER,DOWN,ENTER,DOWN,ENTER,ENTER,WAITFOR:setup window open,WAIT:500,ENTER,\
WAITFOR:game started,ENTER,$CTURN,$CTURN,$CTURN,WAITFOR:player turn faction 1,WAIT:1000"
game() {
    local name="$1"; shift
    local port=$((29000 + RANDOM % 2000))
    mkdir -p "$OUT/$name-host/snap" "$OUT/$name-client/snap"
    cat "$ROOT/harness/oracle/runtime/Alpha Centauri.Ini" > "$OUT/$name.ini"
    printf 'Difficulty=2\r\nNet Port=%s\r\nNet Address=127.0.0.1\r\nNet Name=Host\r\n' $port >> "$OUT/$name.ini"
    cp "$OUT/$name.ini" "$OUT/$name-client.ini"
    sed -i 's/^Net Name=Host/Net Name=Client/' "$OUT/$name-client.ini"
    (cd "$ROOT" && timeout 600 "$SMACX" --hidden --seed 1001,2001 --ini "$OUT/$name.ini" --outdir "$OUT/$name-host" \
        --snapshots "$OUT/$name-host/snap" --keys "$HOST" > "$OUT/$name-host.log" 2>&1; echo $? > "$OUT/$name-host.rc") &
    local hp=$!
    sleep 2
    (cd "$ROOT" && env "$@" timeout 600 "$SMACX" --hidden --seed 1002,2002 --ini "$OUT/$name-client.ini" \
        --outdir "$OUT/$name-client" --snapshots "$OUT/$name-client/snap" --keys "$CLIENT" \
        > "$OUT/$name-client.log" 2>&1; echo $? > "$OUT/$name-client.rc") &
    local cp=$!
    wait $hp $cp
}
# The states compared: the fields that may differ (see above) dropped; ALLOW adds a pattern.
compare() {  # compare NAME DESCRIPTION [ALLOW]
    python3 "$ROOT/harness/compare.py" "$OUT/$1-host/snap" "$OUT/$1-client/snap" --all --max 100000 \
        > "$OUT/$1-compare.txt" 2>&1
    local allow='Monuments\[|MapAbstractData|next_veh_id_stack|prev_veh_id_stack|Vehs\[[0-9]+\]\.state\+3|^turn_|more$'
    [ -n "${3:-}" ] && allow="$allow|$3"
    local n; n=$(grep -c "^turn_" "$OUT/$1-compare.txt")
    if [ "$n" -ge 3 ] && ! grep -vqE "$allow" "$OUT/$1-compare.txt"; then
        echo "PASS $1: $2 ($n turns)"
    else
        echo "FAIL $1: $2 (see $OUT/$1-compare.txt)"; FAIL=1
    fi
}

# 1. A game of three turns: the setup, the turns, the states equal each turn.
game game
for side in host client; do
    log="game-$side"
    check $log "the setup window" "^net: setup window closed \(start, 2 players\)"
    check $log "the game started" "^net: game started: faction [12], humans 06"
    count $log "the turns' upkeep in lockstep" "^net: turn [0-9]+ upkeep" 3
    count $log "the host's turns" "^net: player turn faction 1" 3
    count $log "the client's turns" "^net: player turn faction 2" 2
    nocheck $log "the checksums agree" "checksum [0-9]+ differs"
    nocheck $log "no crash" "not reconstructed|abort|Assertion"
done
check game-host "the host" "^net: hosting on port"
check game-client "the client" "^net: joined 127.0.0.1"
compare game "host and client states equal each turn"

# 2. A desync provoked at turn 2 on the client: the host resends the units.
game desync SMACX_NET_DESYNC=2
check desync-client "the desync provoked" "^net: desync provoked"
check desync-host "found by the checksums" "checksum 3 differs from faction 2's"
# (The changed unit can make the client's computer factions see other tiles before the host resends
# the units: the tiles' visibility, which the checksums do not cover, may stay different.)
compare desync "the states equal again after the resynch" 'Vehs\[[0-9]+\]\.morale|MapTiles\[[0-9]+\]\.(visibility|visible_items)'
if grep -q "^turn_003" "$OUT/desync-compare.txt" && \
   sed -n '/^turn_003/,$p' "$OUT/desync-compare.txt" | grep -q "morale"; then
    echo "FAIL desync: the changed unit still differs after the resynch"; FAIL=1
else
    echo "PASS desync: the changed unit resent"
fi
exit $FAIL
