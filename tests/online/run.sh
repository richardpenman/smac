#!/bin/bash
# Online play (docs/MULTIPLAYER.md, phase 3): two smacx processes on localhost, a host and a client
# (SDL dummy drivers, scripted keys), set up a game through SELECTSERVICE's Internet (TCP/IP) item,
# the TCPHOST / TCPJOIN popups and the MULTIPLAYER SETUP window, then play turns one player at a
# time (GameMoreRules 0x10, the setup's default) with the computer factions' turns in lockstep.
# Each process writes its game state before every turn_upkeep (--snapshots); the two copies are
# compared turn by turn (harness/compare.py) and must agree but for the data the original never
# sends to the players (the monuments, the abstract map, the unused world-generation generator) and
# each machine's own UI state (unit stacking order, a unit's focus bit). The original's desync check
# runs each turn (NetDaemon_game_checksum, net_upkeep: no "checksum differs"). The host moves a unit
# through the locks. A second game
# provokes a desync (SMACX_NET_DESYNC=TURN: the client changes a unit's morale at that turn): the host finds
# the vehicles' checksum different and resends the units; the next turn's units and bases agree.
# A third game has the two humans make a deal in the DiploWin (a treaty and energy credits), a
# fourth sets time controls and lets turns run out, a fifth has the host's settings picked in the
# setup window applied on the client, a sixth has the client drop out of the game.
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

# The setup window's button (LAUNCH GAME for the host, MAKE READY for a player).
READY="CLICK:547x306"
# game NAME [ENV...]: the host and the client of one game (keys below), logs OUT/NAME-host.log, OUT/NAME-client.log
END="C-ENTER,DOWN,ENTER"  # end of turn (REALLYOVER)
# NetMsg_pop's modal messages (a3 0) wait for their OK (ENTER) and hold the network meanwhile: the
# other player's turn (SIMULWHOSE) after each turn of ours.
W="WAITFOR:netmsg SIMULWHOSE,ENTER"
HTURN="WAITFOR:player turn faction 1,ENTER,$END,$W"
CTURN0="WAITFOR:player turn faction 2,ENTER,$END"  # (the host's turn's SIMULWHOSE already answered)
CTURN="$W,$CTURN0"
# The host's first turn moves a unit twice (KP2, KP4: NetDaemon_order_veh's lock, MOVE_VEH 0x2401
# relayed as 0x4401 and executed by both).
HMOVE="WAITFOR:player turn faction 1,ENTER,WAIT:500,KP2,WAITFOR:move veh 0 dir 3,KP4,WAITFOR:move veh 0 dir 5,$END,$W"
# Its second turn sends a chat line to everyone (ctrl+C: Console_chat, CHATASK, message_chat 0x4E00).
HCHAT="WAITFOR:player turn faction 1,ENTER,WAIT:500,C-c,h,i,ENTER,WAITFOR:net: chat,$END,$W"
HOST="DOWN,DOWN,DOWN,DOWN,ENTER,DOWN,ENTER,ENTER,ENTER,WAITFOR:netmsg ADDED,ENTER,WAITFOR:player 2 ready 1,$READY,WAITFOR:game started,ENTER,\
$HMOVE,$HCHAT,$HTURN,WAITFOR:player turn faction 1,WAIT:2000"
# The client's first turn renames its leader (Console_custom_my_faction, "+": CUSTOMNAMES, a Z added
# to the leader's name): NetDaemon_synch 7 sends the faction's names to the host, so the states
# compared below agree.
CCUSTOM="$W,WAITFOR:player turn faction 2,ENTER,WAIT:500,+,Z,ENTER,WAIT:500,$END"
CLIENT="DOWN,DOWN,DOWN,DOWN,ENTER,DOWN,ENTER,DOWN,ENTER,ENTER,WAITFOR:setup window open,WAIT:500,$READY,\
WAITFOR:game started,ENTER,$CCUSTOM,$CTURN,$CTURN,WAITFOR:player turn faction 1,WAIT:1000"
game() {
    local name="$1"; shift
    local port=$((29000 + RANDOM % 2000))
    mkdir -p "$OUT/$name-host/snap" "$OUT/$name-client/snap"
    cat "$ROOT/harness/oracle/runtime/Alpha Centauri.Ini" > "$OUT/$name.ini"
    printf 'Difficulty=2\r\nNet Port=%s\r\nNet Address=127.0.0.1\r\nNet Name=Host\r\n' $port >> "$OUT/$name.ini"
    cp "$OUT/$name.ini" "$OUT/$name-client.ini"
    sed -i 's/^Net Name=Host/Net Name=Client/' "$OUT/$name-client.ini"
    # The host plays with the original's bugs (the ini's key); the client, with the default (the
    # fixes), takes the host's setting.
    printf 'Original Bugs=1\r\n' >> "$OUT/$name.ini"
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
    check $log "the host's unit moved (lock, MOVE_VEH)" "^net: move veh 0 dir 3 \\(faction 1\\)"
    check $log "and moved again" "^net: move veh 0 dir 5 \\(faction 1\\)"
    check $log "the chat line" "^net: chat .*: hi$"
    nocheck $log "the checksums agree" "checksum [0-9]+ differs"
    nocheck $log "no crash" "not reconstructed|abort|Assertion"
done
# NetMsg_pop: the modal messages (a3 0: the other player's turn, SIMULWHOSE) and the NetMsg box
# shown without waiting (the upkeep's phases), hidden by NetMsg_close or its timer.
check game-client "a modal NetMsg_pop (SIMULWHOSE)" "^popup SIMULWHOSE shown"
check game-client "the NetMsg box (UPKEEP5)" "^netmsg UPKEEP5: Upkeep Phase"
nocheck game-client "not a modal popup" "^popup UPKEEP5 shown"
check game-host "the host" "^net: hosting on port"
check game-client "the client" "^net: joined 127.0.0.1"
check game-client "the leader renamed (CUSTOMNAMES)" "^custom faction 2: [^,]*Z, "
check game-client "the host's setting: the original bugs" "^net: the host plays with the original bugs"
compare game "host and client states equal each turn"

# 2. A desync provoked at turn 2 on the client: the host resends the units.
game desync SMACX_NET_DESYNC=2
check desync-client "the desync provoked" "^net: desync provoked"
check desync-host "found by the checksums" "checksum 3 differs from faction 2's"
# After the resynch the units and bases agree again. (The rest may keep a trace of the desync, as
# in the original: the host resends only the blocks whose checksum failed, here the units; data the
# checksums do not cover -- a faction's military strength, the tiles' visibility -- can stay
# different.)
python3 "$ROOT/harness/compare.py" "$OUT/desync-host/snap" "$OUT/desync-client/snap" --all --max 100000 \
    > "$OUT/desync-compare.txt" 2>&1
if grep -q "^turn_003" "$OUT/desync-compare.txt" && ! sed -n '/^turn_003/,$p' "$OUT/desync-compare.txt" |
    grep -E '^ +(Vehs|Bases)\[' | grep -vqE 'next_veh_id_stack|prev_veh_id_stack|\.state\+3'; then
    echo "PASS desync: the units and bases equal again after the resynch"
else
    echo "FAIL desync: the units and bases equal again after the resynch (see $OUT/desync-compare.txt)"; FAIL=1
fi
# 3. Diplomacy between the two humans (the DiploWin over the network): the host calls the client
# from the commlink panel (F12: commlink_attempt's 0x1501, DIPLOCOMM), the client answers COMMDIPLO
# (0x1502) and both DiploWins open; the host chats, offers a treaty and 5 energy credits
# (0x1506) and agrees (0x1509), the client sees the offers and agrees: MAYBE / YES DEAL
# (0x150A / 0x150B), the host (the lower faction) executes the deal (net_treaty_on, net_energy)
# and tells the client (0x150D). The treaty and the credits must agree on both machines.
HDIPLO="WAITFOR:player turn faction 1,ENTER,WAIT:500,F12,WAIT:300,SCREENSTATE,SHOT:commlink.bmp,CLICK:950x348,WAITFOR:netmsg DIPLOCOMM,ENTER,WAITFOR:diplo 1 2,h,i,ENTER,\
CLICK:790x467,CLICK:400x467,5,ENTER,CLICK:330x519,WAITFOR:diplo: deal done between 1 and 2,WAIT:300,DIPLOSTATE,ESC,$END,$W"
CDIPLO="$W,WAITFOR:COMMDIPLO from faction 1,ENTER,WAITFOR:diplo 2 1,WAITFOR:diplo chat: ,\
WAITFOR:diplo: faction 1 state 1,CLICK:330x519,WAITFOR:diplo: deal done by faction 1,WAITFOR:diplo: closed by faction 1,WAITFOR:netmsg DIPLOCLOSE,ENTER,DIPLOSTATE"
HOST="DOWN,DOWN,DOWN,DOWN,ENTER,DOWN,ENTER,ENTER,ENTER,WAITFOR:netmsg ADDED,ENTER,WAITFOR:player 2 ready 1,$READY,WAITFOR:game started,ENTER,\
$HDIPLO,$HTURN,$HTURN,WAITFOR:player turn faction 1,WAIT:2000"
CLIENT="DOWN,DOWN,DOWN,DOWN,ENTER,DOWN,ENTER,DOWN,ENTER,ENTER,WAITFOR:setup window open,WAIT:500,$READY,\
WAITFOR:game started,ENTER,$CDIPLO,$CTURN0,$CTURN,$CTURN,WAITFOR:player turn faction 1,WAIT:1000"
game diplo
for side in host client; do
    log="diplo-$side"
    check $log "the DiploWin opened" "^diplo (1 2|2 1)$"
    check $log "the chat line" "^diplo chat: .*:  hi$"
    check $log "the deal done" "^diplo: deal done (between 1 and 2|by faction 1)$"
    check $log "the treaty between the two" "^diplo [12] status [0-9A-F]{7}[2367ABEF] "
    nocheck $log "the checksums agree" "checksum [0-9]+ differs"
    nocheck $log "no crash" "not reconstructed|abort|Assertion"
done
# The commlink panel's network rows (MultiWin_draw / MultiWin_draw_time / MultiWin_draw_player):
# the time row, the other human with the chat flag and "(+)" (moves after the current player).
check diplo-host "the panel's time row" "^screentext No Time Control$"
check diplo-host "the client's row" "^screentext 1\. Yang$"
check diplo-host "the client's state (moves after us)" "^screentext \(\+\)$"
check diplo-client "the call answered (COMMDIPLO)" "^net: COMMDIPLO from faction 1: 0"
check diplo-host "the treaty and credits offered" "^diplo offer: side 0 type 1 value 5"
check diplo-client "the offers seen" "^diplo offer: side 1 type 3 value 0"
check diplo-host "the host gave 5 credits" "^diplostate f1 credits 95 "
check diplo-client "the client got them" "^diplostate f2 credits 105 "
check diplo-client "the host's credits as the client sees them" "^diplo 1 status .* credits 95 "
compare diplo "host and client states equal each turn"

# 4. Time controls: at its first turn the host opens Console_time_controls (T: TIMECONTROLS), picks
# CUSTOM and sets 5 seconds a turn (CUSTOMTIME; no time per base, unit or event; 10 seconds a bonus
# pill, a pill each turn, at most 2), sent to the client (NetDaemon_synch 0x85, 0x4C02). The first
# turns count double: 10 seconds. The host's turn and then the client's run out (turn_timer:
# GameState 0x800 ends the turn); at its second turn the host uses a bonus pill (S-6, ^: 0x2306 / 0x4306).
HTIMER="WAITFOR:player turn faction 1,ENTER,WAIT:500,T,DOWN,DOWN,DOWN,DOWN,DOWN,ENTER,\
0,DOWN,0,DOWN,0,DOWN,5,DOWN,1,0,DOWN,1,DOWN,2,ENTER,WAITFOR:netmsg WECHANGEDTIME,ENTER,WAITFOR:turn time out,$W"
HBONUS="WAITFOR:player turn faction 1,ENTER,WAIT:500,S-6,WAITFOR:netmsg WEBONUSED,ENTER,WAIT:1500,$END,$W"
CTIMER="$W,WAITFOR:netmsg HECHANGEDTIME,ENTER,WAITFOR:player turn faction 2,ENTER,WAITFOR:netmsg TIMEWARNING,ENTER,\
WAITFOR:turn time out,$W,WAITFOR:netmsg HEBONUSED,ENTER"
HOST="DOWN,DOWN,DOWN,DOWN,ENTER,DOWN,ENTER,ENTER,ENTER,WAITFOR:netmsg ADDED,ENTER,WAITFOR:player 2 ready 1,$READY,WAITFOR:game started,ENTER,\
$HTIMER,$HBONUS,$HTURN,WAITFOR:player turn faction 1,WAIT:2000"
CLIENT="DOWN,DOWN,DOWN,DOWN,ENTER,DOWN,ENTER,DOWN,ENTER,ENTER,WAITFOR:setup window open,WAIT:500,$READY,\
WAITFOR:game started,ENTER,$CTIMER,$CTURN0,$CTURN,WAITFOR:player turn faction 1,WAIT:1000"
game timer
for side in host client; do
    log="timer-$side"
    check $log "the custom time controls set" "^netmsg (WE|HE)CHANGEDTIME[01]?: .*Custom"
    check $log "a turn ran out" "^net: turn time out"
    count $log "the turns' upkeep in lockstep" "^net: turn [0-9]+ upkeep" 3
    check $log "the bonus pill used" "^netmsg (WE|HE)BONUSED"
    nocheck $log "the checksums agree" "checksum [0-9]+ differs"
    nocheck $log "no crash" "not reconstructed|abort|Assertion"
done
check timer-host "the host's CUSTOMTIME answered" "^popup CUSTOMTIME answer: [0-9]"
check timer-host "the custom values" "^time controls custom: base 0 unit 0 event 0 turn 5 extra 10 refresh 1 max 2"
# (The original sends the table to the server (0x2101, relayed as 0x4101) and 0x4C02 to everyone at
# once: the client applies 0x4C02 before the new table, so its turn's total time at that turn's
# snapshot (GamePrefs.GameTimeControl[1], 0x9A64E4) is the old table's until the next turn.)
compare timer "host and client states equal each turn" 'GamePrefs\.GameTimeControl\[1\]'


# 5. The setup window (NetWin): the host picks the planet's size (Tiny), the time controls (Loose)
# and a rule (Look First) in the session settings; each choice is sent (0x2F02 -> the session
# packet to everyone) and the game the client starts with has them.
HLOBBY="WAIT:500,CLICK:200x218,UP,UP,ENTER,WAITFOR:setting 4 chosen 1,CLICK:180x170,DOWN,DOWN,DOWN,DOWN,ENTER,\
WAITFOR:setting 3 chosen 4,CLICK:200x648,WAITFOR:rule 6 toggled,SCREENSTATE,d,o,n,e,ENTER"
HOST="DOWN,DOWN,DOWN,DOWN,ENTER,DOWN,ENTER,ENTER,ENTER,WAITFOR:netmsg ADDED,ENTER,$HLOBBY,WAITFOR:player 2 ready 1,$READY,\
WAITFOR:game started,ENTER,$HTURN,WAITFOR:player turn faction 1,WAIT:1000"
CLIENT="DOWN,DOWN,DOWN,DOWN,ENTER,DOWN,ENTER,DOWN,ENTER,ENTER,WAITFOR:setup window open,WAITFOR:net: chat Host: done,WAITFOR:session settings,WAITFOR:session settings,WAITFOR:session settings,\
SCREENSTATE,$READY,WAITFOR:game started,ENTER,$CTURN,WAITFOR:player turn faction 1,WAIT:1000"
game lobby
check lobby-host "the planet's size picked" "^net: setting 4 chosen 1$"
check lobby-host "the time controls picked" "^net: setting 3 chosen 4$"
check lobby-host "a rule toggled" "^net: rule 6 toggled$"
for side in host client; do
    log="lobby-$side"
    check $log "the planet's size in the window" "^screentext .*: Tiny planet"
    check $log "the time controls in the window" "^screentext .*: Loose"
    check $log "the rule in the window" "^screentext \\[X\\] Look First"
    check $log "the game with the host's settings" "^net: game started: .*map 48x48, rules [0-9A-F]*[13579BDF][0-9A-F]{1}, time control 4"
    nocheck $log "the checksums agree" "checksum [0-9]+ differs"
    nocheck $log "no crash" "not reconstructed|abort|Assertion"
done

# 6. A player dropping: the client plays its first turn and quits. The host is told (DROPPED,
# NetDaemon_on_player_deleted), the client's faction is no longer human (FactionStatus) and the
# host plays on alone: two more turns, the computer playing the client's faction.
HOST="DOWN,DOWN,DOWN,DOWN,ENTER,DOWN,ENTER,ENTER,ENTER,WAITFOR:netmsg ADDED,ENTER,WAITFOR:player 2 ready 1,$READY,\
WAITFOR:game started,ENTER,$HTURN,WAITFOR:netmsg DROPPED,ENTER,WAITFOR:player turn faction 1,ENTER,$END,\
WAITFOR:player turn faction 1,ENTER,$END,WAITFOR:player turn faction 1,WAIT:1000"
CLIENT="DOWN,DOWN,DOWN,DOWN,ENTER,DOWN,ENTER,DOWN,ENTER,ENTER,WAITFOR:setup window open,WAIT:500,$READY,\
WAITFOR:game started,ENTER,$CTURN"
game drop
check drop-host "the client dropped (DROPPED)" "^netmsg DROPPED: "
check drop-host "the client's faction left to the computer" "^net: player 2 left$"
count drop-host "the host's turns after the drop" "^net: player turn faction 1" 4
count drop-host "the turns' upkeep goes on" "^net: turn [0-9]+ upkeep" 4
nocheck drop-host "no crash" "not reconstructed|abort|Assertion"
if sed -n '/^net: player 2 left/,$p' "$OUT/drop-host.log" | grep -aq "^net: player turn faction 2"; then
    echo "FAIL drop-host: the dropped faction no longer has a player's turn"; FAIL=1
else
    echo "PASS drop-host: the dropped faction no longer has a player's turn"
fi

exit $FAIL
