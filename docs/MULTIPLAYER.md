# Multiplayer

Plan for multiplayer in smacx: hotseat, then play-by-email (PBEM), then online play between smacx
copies over TCP. Single-player behaviour does not change: every path below is taken only with
`PbemActive` or `MultiplayerActive` set, and the differential harness (`harness/verify.sh`) and
the scripted suites keep passing at each step. User decisions: smacx to smacx only (no DirectPlay
compatibility with the original); the original's bugs are replicated (COMPAT flags on).

## How the original is structured

**Entry.** `top_menu` (0x58E360) item MULTIPLAYER calls `multiplayer_init(0)` (0x58DB30). It calls
`NetDaemon_init` -> `AlphaNet_setup` (0x4E31E0) -> `AlphaNet_pick_service`: the SELECTSERVICE popup
(jackal.txt) lists the DirectPlay service providers and one more item, label 0x1128 (the
hotseat/email game), id 9999, which sets `PbemActive`. With `PbemActive` the net layer is closed
and `multiplayer_init` runs `top_menu(1)`: the HOTSEAT menu (New Multiplayer Game, Multiplayer
Scenario, Load Multiplayer Game). A network service instead asks NETCONNECT_JOIN_OR_CREATE
(`AlphaNet_create` / `AlphaNet_join`: the DirectPlay session), then `NetWin_exec` (the MULTIPLAYER
SETUP window: players, factions, rules, the planet, time controls) and the host sends the rules,
the game and the files to the others (`NetDaemon_send_rules / send_game / send_files`).

**Hotseat setup** (`config_game` 0x589D30 with `PbemActive`). After each faction is picked:
HOTSEATADD0 (the human factions so far) + HOTSEATADD1 (add another human player? No / Yes); a
taken faction gives FACTIONTAKEN. `FactionStatus[0]` gets the human factions, the first is the
viewer; `clear_tamper` and the PBEM tamper seeds (`timeGetTime + MapRandomSeed`, the system date)
are set; no planetfall movie. Passwords are not asked here: they are set at the first handover.

**Turns.** `control_game` runs `control_turn` (hotseat / PBEM / single player) or
`net_control_turn` (network). In `control_turn` a human faction other than the viewer (always with
`PbemActive`) takes over the screen: the windows are hidden, the screen is cleared and the
SWITCHING popup is shown (NEXT PLAYER TURN, the leader, the mission year; "Set Password or Press
ENTER" — SWITCHING2 "Enter Password" once the faction has one; the SAVE AND EXIT button saves
with `GameMoreRules` 0x40 and quits). A typed password is checked with `checksum_password`
(three wrong tries: save and quit); a first password is confirmed with VERIFYPASSWORD and stored
in the faction (+0x88 of Faction, `dword 0x96EA68`). Then the view is centred on the player's first
base (or unit), and what happened since their last turn is shown: tampering (HOTTAMPER2), saved
technologies (`tech_advance`), diplomacy requested by other humans (`diplo`), AI contacts
(`communicate`), the council's pending session or result (COUNCILHOT*).

**Diplomacy between humans.** Between two human players the commlink does not open the AI dialog:
`commlink_attempt` / `commlink_attempter` call `diplo` (0x444D90 -> FUN_00443F10), the DiploWin:
a window over the main interface with each side's offers (technologies, energy, maps, treaties and
pacts, bases, loans), the chat lines (StringBox and EditBox) and the buttons (ids 0xDC4..0xDF7:
propose, accept, reject, ...). In hotseat / PBEM the request is stored per pair of factions
(the offer tables at 0x7388D8.. and `DiploStateB / DiploStateC`) and the other player sees it at
the start of their turn (`control_turn`, above); in network play the offers travel as messages
(`message_data`, `NetDaemon_await_diplo`, `net_treaty_on / off`, `net_energy`, `net_tech`,
`net_maps`, `net_pact_ends`, `net_cede_base`, ...). `clear_hotseat_chat` clears the stored chat.

**PBEM.** The same game as hotseat; after SAVE AND EXIT the save is mailed by hand. `save_game`
counts saves (`PbemSaveCount`), `set_tamper / check_tamper / read_tamper / clear_tamper` detect a
player reloading the save to retry a turn (HOTTAMPER, HOTTAMPER2, HOTNOTAMPER); loading a
password-protected save as single player is refused (PASSWORDEDLOADATTEMPT); loading asks the
faction's password (ENTERPASSWORD / BADPASSWORD).

**Network.** `NetDaemon` (0x52DF30..0x538D10) sits on `AlphaNet` / `Net` (DirectPlay, 0x62D6A0..):
`Net_send` / `Net_get` move packets; `NetDaemon_send_message` routes a message by its type bits
(0x4000 timed, 0x8000 to the host, 0xA000 to the current faction in simultaneous play, else to a
faction); `NetDaemon_process_message` (17.6 KB) handles about 70 message types (0x0F05.., 0x1201..,
0x1505.., 0x2212.., 0x4304.., 0x4405.., 0x4441..). State changes are replicated by `synch_*`
(`synch_veh`, `synch_base`, `synch_proto`, `synch_obs`, `synch_bit`, `synch_diplo`, ... ->
`NetDaemon_synch`) and events by `message_*` (`message_data`, `message_veh`, `message_base`,
`message_chat`, `message_landmark`, `message_lock`, ...). Exclusive access uses locks
(`NetDaemon_lock / lock_veh / lock_base / add_global / await_*`). `net_control_turn` (0x52A2E0)
runs the round: `turn_upkeep` + `net_upkeep` (the host), `net_upkeep_phase`, each player's moves
(simultaneous or in turn, `GameMoreRules` 0x10), `net_end_of_turn`, `net_not_my_turn`, checksums
(`do_checksums`), time controls (`turn_timer`, `set_time_controls`). `wait_loop` (0x51F200) is the
idle loop of every modal wait: without multiplayer `wait_task`, with it `NetDaemon_net_tasks` and
the diplomatic channels other humans opened (`NetDaemon_open_channel` -> `communicate`).
MultiWin (the commlink panel) shows the players' time and lock state, its chat button
(`Console_chat` -> CHATASK -> `message_chat`).

## Phases

**1. Hotseat** (this phase):
* the SELECTSERVICE popup with the hotseat/email item (no network service yet) ->
  `multiplayer_init`'s PBEM path -> `top_menu(1)`;
* `config_game`'s HOTSEATADD0 / HOTSEATADD1 / FACTIONTAKEN loop, the tamper seeds;
* `control_turn`'s handover: the screen cleared, SWITCHING / SWITCHING2, the passwords
  (`checksum_password`, VERIFYPASSWORD, three tries), SAVE AND EXIT, the view centred
  (`ui_hotseat_switch` / `ui_hotseat_show` in src/ui);
* `diplo` and the DiploWin (FUN_00443F10 and its helpers 0x440F80..0x445080) for two humans;
* the PbemActive paths already reconstructed in the engine (control_turn, hotseat_take_turn,
  council, comm_check, save / load) checked in play.

**2. PBEM**: SAVE AND EXIT and reloading (ENTERPASSWORD, BADPASSWORD, PASSWORDEDLOADATTEMPT),
the tamper checks (`set_tamper`, `check_tamper`, `read_tamper`; HOTTAMPER / HOTNOTAMPER), the
council's hotseat results across saves. Mostly already in the engine's save / load code.

**3. Online (TCP between smacx copies)**: `Net_*` (DirectPlay) is replaced by a small TCP transport
under the original `NetDaemon` message protocol, which stays as the original's (same message
types and payloads, same routing in `NetDaemon_send_message`, same `process_message`):
* the host listens on a port, clients connect (NETCONNECT_JOIN_OR_CREATE: host or join; join asks
  the host's address); the SELECTSERVICE list shows "Internet (TCP/IP, smacx)" beside the
  hotseat/email item;
* a `Net` replacement with the original's entry points (`Net_send(msg, len, to, guaranteed)`,
  `Net_get`, `Net_enum_players`, `Net_drop_player`, `Net_handle_sys_msg` for joins / drops),
  length-prefixed frames on one TCP stream per client, the host relaying between clients as
  DirectPlay's server did; player ids are the original's (`AlphaNet_pid_2_who`, `who_2_pid`);
* then `NetWin_exec` (the setup window), `NetDaemon_send_rules / send_game / send_files`, the
  locks, `net_control_turn` and the message handlers, one group at a time;
* no voice (`Net_start_voice`), no lobby (`Net_check_for_lobby`), no DirectPlay registry check.

## Testing

* **Every phase**: `NATIVE_BUILD=build/mp-native harness/verify.sh native tiny20 council131` and
  all `tests/*/run.sh` (animated) pass: single player unchanged.
* **Hotseat**: `tests/hotseat/run.sh` (animated, scripted keys): a 2-3 human game through the
  menus (MULTIPLAYER -> the hotseat item -> New Multiplayer Game -> the factions, HOTSEATADD),
  several turns with the handovers (SWITCHING, a password set and checked, a wrong one), a treaty
  between two humans through the DiploWin, SAVE AND EXIT and the save reloaded; the log checked
  (popups shown, `FactionStatus`, the viewer per turn, the treaty in DIPLOSTATE).
* **PBEM**: the same game saved and exited at each handover and reloaded by the next player;
  tampering provoked (a save reloaded twice).
* **Online**: two smacx processes on localhost (one host, one client, `--keys` scripts each, run
  one after the other's start; the machine allows only one smacx at a time for the suites, so
  this test runs alone); the messages logged on both sides and the game states compared at each
  turn's end (`do_checksums` must agree). Unit tests for the frame codec and the message routing.

## State

**Phase 1 (hotseat): done.**
* src/ui/topmenu.cpp: `multiplayer_init` (SELECTSERVICE with the hotseat/email item, the HOTSEAT
  menu through `top_menu(1)`).
* src/ui/gamesetup.cpp: `config_game`'s FACTIONTAKEN, HOTSEATADD0 / MULTISTRING / HOTSEATADD1, the
  first human as the viewer, the tamper seeds (`PbemGameID`, `PbemGameDate`, `PbemSaveCount`).
* src/ui/hotseat.cpp: the handover (`ui_hotseat_switch`: the screen cleared, SWITCHING /
  SWITCHING2, passwords with VERIFYPASSWORD and three tries, SAVE AND EXIT, the view centred;
  `ui_hotseat_show`: WONLOST, the timers). The engine's `control_turn` and `hotseat_take_turn`
  were already reconstructed.
* src/ui/diplo_hotseat.cpp: `diplo` and the DiploWin.
* src/app/smacx.cpp, src/platform/win32.cpp: with `--outdir` the saves go to `DIR/saves`.
* Approximated: the players' own message lists between turns (sub_51E530), the desktop's box art
  behind SWITCHING, the DiploWin's StringBox / EditBox look.
* Tested by tests/hotseat/run.sh (two humans; a third works the same way through HOTSEATADD1).

**Phase 2 (PBEM): done.** The original has no separate e-mail mode: a PBEM game is the hotseat game
saved at a handover (SWITCHING's SAVE AND EXIT, or any save: with `PbemActive` `save_game` ends the
game) and loaded by the next player from the HOTSEAT menu's LOAD MULTIPLAYER GAME. There is no
e-mail instructions popup and no special save name (HOTTAMPER, ENTERPASSWORD, NOHOTSEAT,
HOTSEATADD2, PASSWORDREQUIRED and LOADSECURITY are in the script but unused by terranx.exe).
* Already in the engine: `save_daemon` (PbemSaveCount, GameMoreRules 0x20, the tamper seeds and
  per-faction reload notes saved), `load_daemon` (PbemActive set again for a game with 0x20 or two
  humans, `check_tamper`: HOTNOTAMPER and the other players' notes), `load_game`'s refusal of a
  passworded game outside multiplayer (PASSWORDEDLOADATTEMPT), `hotseat_take_turn`'s HOTTAMPER2.
  The password asked on load is the handover's SWITCHING2 (phase 1).
* src/ui/hotseat.cpp: `read_tamper` / `set_tamper` (0x51E000 / 0x51E090). The original keeps the
  50 tamper records in the registry (HKLM ...\DirectPlay\Applications\Sid Meier's Alpha
  Centauri, values MVM and MVB), i.e. per machine; smacx keeps the same two values in
  `tamper.reg` in its per-user directory (the `--outdir` in tests). The stubs in src/ui/editor.cpp
  were removed.
* Tested by tests/pbem/run.sh: two "machines" (two outdirs, each with its saves and its
  tamper.reg) pass `pb.SAV` back and forth over three turns: passwords set at the first handovers,
  a wrong one on load, SAVE AND EXIT at each handover, a save loaded twice (HOTNOTAMPER; the
  record's load count 2), the other player told at their next turn (HOTTAMPER2), and the save
  refused as a single-player load (PASSWORDEDLOADATTEMPT).

**Phase 3 (online, TCP between smacx copies): in progress.**
* Group 1, the transport (done): src/platform/net_tcp.{h,cpp} (frames: a 16-byte header -- size,
  kind, from, to -- then the payload; the host relays; pids: the host 1, the clients 2, 3, ... in
  join order; join / leave events; non-blocking sockets, POSIX and Winsock), unit-tested by
  tests/unit/test_net_tcp.cpp (framing at any split, oversized frames, routing, the player list, a
  host and two clients on localhost). src/engine/net_link.cpp gives the original's Net_send /
  Net_get / Net_get_player_name / Net_close / Net_handle_sys_msg over it (a message to everyone
  also reaches its sender, as with Net_init's session flags; DirectPlay's player list sent to a
  joining player becomes its NetDaemon_on_player_added calls). Hybrid builds keep DirectPlay.
* Group 2, the setup (done): src/ui/netwin.cpp: multiplayer_init's network path, AlphaNet_setup
  (SELECTSERVICE: the hotseat/email item, then "Internet (TCP/IP)"; NETCONNECT_JOIN_OR_CREATE;
  smacx's TCPHOST / TCPJOIN popups (smacxnet.txt) for the name, address and port, kept in the
  prefs Net Name / Net Address / Net Port), NetWin_config_players, NetWin_setup_faction_data,
  NetWin_fill_player, NetWin_on_button_clicked (ready / start, NOTALLREADY), NetWin_prepare_game
  (Random_reseed(1000)); the setup packets (0x0F02/03/04/0D) in the engine. Approximated: the
  MULTIPLAYER SETUP window is a plain list (F the faction, C a chat line, Enter ready / start, Esc
  cancel); the host's map and rule settings stay the defaults (a random map, NetSetupType 0); no
  scenario file transfer (NetDaemon_send_files, 0x0F05..0x0F0C).
* Group 3, turns, synch and locks (done): src/engine/net_daemon.cpp (routing, message_* /
  synch_*, NetDaemon_synch's blocks -- the rule tables travel in the original's 32-bit layout --,
  send_rules / send_game, the locks' requests, order_veh / action / await_*, the deletion list,
  game_checksum, the channels), net_process.cpp (process_message), net_lock.cpp (Lock, PlayerLock,
  SquareLock), net_turn.cpp (net_control_turn, net_upkeep and its phases, net_end_of_turn,
  net_not_my_turn, mash_planes, set_time_controls). A unit's move: NetDaemon_lock (0x2210) at the
  server, MOVE_VEH (0x2401) relayed as 0x4401 and executed by every machine with the server's
  stamp as seed. smacx stamps relayed events with a counter of its own under its deterministic
  clock (net_stamp; the original: timeGetTime), so the same keys give the same online game.
  NetMsg_pop (done, src/ui/win_popup.cpp): with a3 0 (or a3 > 1 and no interface window) a
  modal popup that holds the network until its OK (as the original: SIMULWHOSE, DIPLOCOMM,
  DIPLOCLOSE, ADDED, the time controls' messages, ...); else the NetMsg box (top centre, 8 pixels
  down, PopupDialogState 6, its OK button label 0x36C) shown without waiting, hidden by the next
  one, NetMsg_close or its timer (NetMsg_timer_daemon after a positive delay). Each network
  message is also a note ("netmsg LABEL: text") for the tests. Approximated: the box's OK button
  cannot be clicked (it closes by its timer or NetMsg_close); single player keeps showing every
  NetMsg_pop modal as before.
* Group 4 (in part): the chat (Console_chat: CHATASK, message_chat 0x4E00 / 0x1E00, shown on the
  message line; the setup window's chat), the time-control and time-bonus messages (0x4C02,
  0x4306), pause (0x4C00/01). Not done: the scenario file transfer.
* Time controls (done): Console_time_controls (T, the panel's time row: HOSTTIME for a client;
  time_controls_dialog's TIMECONTROLS with the current one selected; CUSTOM asks CUSTOMTIME's seven
  values, 0..9999, and sends the table (NetDaemon_synch 0x85); 0x4C02 to everyone, its handler
  with WECHANGEDTIME / HECHANGEDTIME0 / HECHANGEDTIME1 and the table's values), Console_use_time_bonus
  (^, the panel's pills: SIMULNO, NOTIMEBONUSES, NOBONUSATEND, NOBONUSDONE, 0x2306), the pause key
  (0x4C00 / 0x4C01, SIMULNOT with one player at a time), turn_timer (every 500 ms of real time,
  from wait_task and the modal waits) and MultiWin_draw_time's countdown: not while paused or in
  a network wait (blinking then), TIMEWARNING and a tick in the last fifteen seconds, the time run
  out: GameState 0x800 (the turn ends, net_control_turn) and the alarm. EditGroup popups now move
  between their fields with the arrows / Tab (the original dialog's focus). As the original, the
  custom table travels through the server (0x2101 -> 0x4101) while 0x4C02 goes to everyone at
  once, so a client applies 0x4C02 with the old table (its HECHANGEDTIME1 shows the old values
  and that turn's total time is the old one).
* The commlink panel's network rows (done, src/ui/multiwin.cpp): MultiWin_draw's network path (the
  top art, the time row, the bonus pills' row "^", the chat button under the Council button),
  MultiWin_draw_player's chat flag (multi.pcx's three flags: none, refused (0x7FFF74), left out
  of the chat (0x93E960); a click cycles them as MultiWin_on_left_click) and the players' states
  (PAUSED, (*), (+), (-), the upkeep's count, (?) / (@) the server's locks, the treaty's short
  name), the time and pills spots (Console_time_controls, Console_use_time_bonus). Approximated:
  the original's panel is modeless beside the map; closed, it keeps its time and pills rows on
  the screen: here that closed panel is drawn over the screen but not clickable (T and ^).
* The DiploWin between online players (done): commlink_attempt's network path (another human:
  0x1501 and DIPLOCOMM; a computer faction: NetDaemon_open_channel / NOCHANNEL, communicate, the
  commlink treaty through 0x2441), commlink_attempter's ALREADYDONE / DIPLOALREADY / input-lock
  refusals; process_message's 0x1501 (refused callers: 0x1503; busy: 0x1504; the officer's
  COMMDIPLO with its channel number (game_random), the leader's profile (report_intel), "call back
  later" and "no further messages" (0x7FFF74)), 0x1502 (the caller's commlink treaty 0x2441 and
  its DiploWin), 0x1503 / 0x1504 (DIPLOREFUSE(1) / DIPLOBUSY(1)), 0x1505 (the window released,
  DIPLOCLOSE), 0x1506..0x1509 (their offer added / removed / cleared, their state;
  FUN_004426F0's MAYBE DEAL), 0x150A..0x150D (MAYBE / YES / NO / DONE DEAL: both machines ask,
  the lower faction executes FUN_00442740 -- net_tech, net_energy, net_treaty_on,
  net_double_cross --, the other is told), 0x150E, 0x150F (VENDETTAWARNING); the chat lines
  (FUN_004412F0). src/ui/diplo_hotseat.cpp holds the window's side (ui_diplo_net); the modal
  windows and popups now run their callback while they wait (Win_set_modal / BasePop_exec_3:
  pop_wait / pop_expire -> wait_loop in a network game; src/ui/win.cpp ui_set_modal_callback), so
  a player in the DiploWin or a popup keeps receiving messages, and a message can close the window
  (Win_release_modal). The commlink panel, modal here, runs wait_loop while it is open (the
  original's panel is modeless beside the running main loop).
* What the original never sends to the players (so their copies differ there too): the
  monuments, the abstract map (only world_build's Path_continents computes it), the
  world-generation generator; each machine's unit stacking order and focus bit are its own.
  After a resynch only the failed blocks are resent: data the checksums do not cover (a faction's
  military strength, the tiles' visibility) can keep a trace of the desync.
* tests/online/run.sh (run it alone: two processes): the setup, three turns one player at a time
  with the computer factions in lockstep, a unit moved twice through the locks, a chat line, the
  states compared each turn and no checksum difference; then a desync provoked on the client
  (SMACX_NET_DESYNC) found by the host's checksums and its units resent; a third game has the
  two humans trade in the DiploWin (the call answered, a chat line, a treaty and 5 credits
  offered, both agree, the deal done by the host) with the treaty and credits checked on both;
  a fourth game sets custom time controls (5 s a turn, doubled the first turns), lets the host's
  and the client's turns run out and uses a bonus pill.
