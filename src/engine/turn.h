// Turn processing (original module around 0x51F2E0-0x52DBA0): random events, native life, turn
// and faction upkeep, the main game loops and the Planetary Council's AI. The multiplayer turn
// loop (net_*) is not reconstructed (single player only).
#pragma once
#include "engine/globals.h"

// ---- harness hooks (not in the original) ----------------------------------------------------------
// When set, control_turn / control_game call the hook instead of the original callee; the hook must
// make the call itself. The differential harness uses them to snapshot state before each
// turn_upkeep, poison the stack before engine entry points, stop after N turns, skip the
// observer's end-of-round pause and run its script in place of the main menu.
extern int32_t (*g_harness_turn_upkeep)();                // control_turn -> turn_upkeep
extern int32_t (*g_harness_faction_upkeep)(int32_t);      // control_turn -> faction_upkeep
extern int32_t (*g_harness_enemy_turn)(int32_t);          // control_turn -> enemy_turn
extern int32_t (*g_harness_human_turn)();                 // control_turn -> Console_human_turn (human)
extern int32_t (*g_harness_observer_pause)();             // control_turn -> Console_human_turn (round end)
extern int32_t (*g_harness_top_menu)();                   // control_game -> top_menu

// ---- globals -----------------------------------------------------------------------------------------
GLOBAL(int32_t[8], CouncilVoteFor, 0x6FF6D4);        // council: the candidate each faction voted for
GLOBAL(int32_t[8], HotseatTamperFaction, 0x93A9B8);  // hotseat: faction caught tampering (told next turn)
GLOBAL(int32_t[8], HotseatTamperCount, 0x93A9D8);
GLOBAL(char[4], ControlTurnPlayerText, 0x93AA04);   // hotseat: player name text ($STR0 of SWITCHING)
GLOBAL(uint8_t, LobbyParam, 0x689218);              // Net_check_for_lobby's second argument
// Image address of MFactions (texts passed to interlude by address).
constexpr uint32_t kMFactionsAddr = 0x946A50;

// ---- callees of other modules (declared here; reconstructed in later packages) ---------------------
// Probe module: energy needed to corner the Global Energy Market (sum of mind_control costs of
// the foreign bases, at least 1000).
ADDR(0x59EE50) int32_t GAMEAPI corner_market(int32_t faction_id);
// Multiplayer: waits for network messages (council sessions).
ADDR(0x51F200) void GAMEAPI wait_loop();

// ---- random events and Planet ---------------------------------------------------------------------
// Random events of the turn (is_extra: an additional event, without the turn's housekeeping and
// without reseeding the generator).
ADDR(0x51F2E0) void GAMEAPI random_events(int32_t is_extra);
// Forests and kelp spread; native life spawns (Isles of the Deep, Sealurks, mind worms).
ADDR(0x522180) void GAMEAPI alien_fauna();
// Fungal towers spread fungus and refresh their morale.
ADDR(0x522F00) void GAMEAPI do_fungal_towers();
// The turn-75 interlude and the Ascent announcement to the viewer (turn_upkeep's version is inline).
ADDR(0x523B50) void GAMEAPI ascent();
// One step of a pending sea level change (turn_upkeep's version is inline).
ADDR(0x524210) void GAMEAPI climactic_changes();
// Number of faction_id's working subspace generators (in bases of at least the required size);
// out (optional) receives the number in smaller bases. 0 for non-alien factions.
ADDR(0x5247B0) int32_t GAMEAPI generators(int32_t faction_id, int32_t* out);
// Victory checks (check_only == 0: only report a game already over) and the end of game screens.
// Returns non-zero when the game is over (ControlTurnA set).
ADDR(0x524870) int32_t GAMEAPI end_of_game(int32_t check);
ADDR(0x523DD0) void GAMEAPI reset_territory();

// ---- turn and faction upkeep ----------------------------------------------------------------------
ADDR(0x5258C0) void GAMEAPI turn_upkeep();
ADDR(0x526030) void GAMEAPI repair_phase(int32_t faction_id);
ADDR(0x5267B0) void GAMEAPI allocate_energy(int32_t faction_id);
ADDR(0x526E70) void GAMEAPI production_phase(int32_t faction_id);
ADDR(0x527290) void GAMEAPI faction_upkeep(int32_t faction_id);
// The single player game loop: turn upkeep, then each faction's upkeep and turn, until the game
// ends or is left.
ADDR(0x5275B0) void GAMEAPI control_turn();
// Multiplayer game loop (not reconstructed).
ADDR(0x52A2E0) void GAMEAPI net_control_turn();
// The program's main loop: boot, main menu, game, restart.
ADDR(0x52AA30) void GAMEAPI control_game();

// ---- Planetary Council ------------------------------------------------------------------------------
// Council votes of faction_id: its population, +50% with the Empath Guild, doubled with the
// Clinical Immortality, times its VOTES faction bonus (0 for aliens).
ADDR(0x52AD30) int32_t GAMEAPI council_votes(int32_t faction_id);
// Whether faction_id can be a governor candidate (at most one faction has more votes).
ADDR(0x52AE20) int32_t GAMEAPI eligible(int32_t faction_id);
// How much faction_id (AI) wants proposal passed; for the governor election (0) and the Supreme
// Leader (1) the candidate. Positive: for, negative: against.
ADDR(0x52AEB0) int32_t GAMEAPI wants_prop(int32_t faction_id, int32_t proposal, int32_t candidate);
// faction_id's vote: for proposal 0 the candidate it elects; otherwise a bribed vote, -1 yes,
// -2 no or the abstention by turn.
ADDR(0x52BE60) int32_t GAMEAPI council_get_vote(int32_t faction_id, int32_t proposal, int32_t candidate);
// Applies a passed proposal (winner: the elected Supreme Leader).
ADDR(0x52BF60) void GAMEAPI council_action(int32_t proposal, int32_t winner);
// Whether faction_id may call the council (show_reason: messages to a human viewer).
ADDR(0x52C670) int32_t GAMEAPI can_call_council(int32_t faction_id, int32_t show_reason);
// faction_id calls the council: a human chooses the proposal, an AI the one it wants most.
ADDR(0x52C880) void GAMEAPI call_council(int32_t faction_id);
