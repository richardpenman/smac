// Diplomacy (original module around 0x538F30-0x55F930).
#pragma once
#include "engine/globals.h"

// First contact / encounter between two factions at (x, y).
ADDR(0x55E370) void GAMEAPI encounter(int32_t faction_id, int32_t faction_id_with, int32_t x, int32_t y,
                                      int32_t is_monolith, int32_t is_native);
// Whether faction_id is the "great satan" (overwhelming leader); is_aggressive selects the rule.
ADDR(0x539C00) int32_t GAMEAPI great_satan(int32_t faction_id, int32_t is_aggressive);
// Whether faction_id (seen from faction_id_vs) is close to a diplomatic/council victory.
ADDR(0x539D40) int32_t GAMEAPI aah_ooga(int32_t faction_id, int32_t faction_id_vs);
// Whether a human faction is close to victory (the AI then unites against it).
ADDR(0x539E40) int32_t GAMEAPI climactic_battle();
// Whether faction_id wants to attack faction_id_target (odds_override: extra aggression).
ADDR(0x55BC80) int32_t GAMEAPI wants_to_attack(int32_t faction_id, int32_t faction_id_target, int32_t odds_override);
// Whether the game is near its end for faction_id (a faction close to victory).
ADDR(0x539EF0) int32_t GAMEAPI at_climax(int32_t faction_id);

// ---- debug/checksum helpers at the start of the module (0x538F30-0x539160; net_misc.cpp) ----
// Debug alert: logs label/text/values and shows the DANGER popup with them.
ADDR(0x538F30) void GAMEAPI danger(const char* label, const char* text, int32_t v1, int32_t v2, int32_t v3);
// XOR of all bytes of a file (0 if it cannot be opened or is empty).
ADDR(0x538FE0) int32_t GAMEAPI checksum_file(const char* filename);
// XOR of size bytes into seed. Only the low byte is the result; the upper bytes of the return
// value are the register residue of the original (see net_misc.cpp).
ADDR(0x539090) int32_t GAMEAPI checksum(const uint8_t* data, int32_t size, int32_t seed);
// Password check value: XOR of the upper-cased characters + 1 (0 for an empty password).
ADDR(0x5390C0) int32_t GAMEAPI checksum_password(const char* password);
// Sets diplomatic status bits of faction_id toward faction_id_with (and the reverse where mutual).
ADDR(0x55B870) void GAMEAPI treaty_on(int32_t faction_id, int32_t faction_id_with, uint32_t status);
// Clears diplomatic status bits between two factions in both directions (with dependent bits:
// any of 7 also clears 0x1100000, 0x10 also clears 0x8040; 0x20 clears agenda bits 0xC00).
ADDR(0x55B760) void GAMEAPI treaty_off(int32_t faction_id, int32_t faction_id_with, uint32_t status);
// Sets agenda bits between two factions in both directions (0x20 first clears agenda bit 0x40).
ADDR(0x55BA80) void GAMEAPI agenda_on(int32_t faction_id, int32_t faction_id_with, uint32_t agenda);
// Sets (add != 0) or clears diplomatic status bits of faction_id toward faction_id_with; setting
// 0x40 (vendetta) also sets diplo_merc toward faction_id_with to 50.
ADDR(0x55BB30) void GAMEAPI set_treaty(int32_t faction_id, int32_t faction_id_with, uint32_t status, int32_t add);
// Sets (add != 0) or clears agenda bits of faction_id toward faction_id_with.
ADDR(0x55BBA0) void GAMEAPI set_agenda(int32_t faction_id, int32_t faction_id_with, uint32_t agenda, int32_t add);
// Reputation penalty of faction_id toward faction_id_with: integrity blemishes minus the gifts
// made to faction_id_with, clamped to 0..99.
ADDR(0x53A100) int32_t GAMEAPI reputation(int32_t faction_id, int32_t faction_id_with);
// Clears agenda bits between two factions in both directions.
ADDR(0x55B820) void GAMEAPI agenda_off(int32_t faction_id, int32_t faction_id_with, uint32_t agenda);
// Ends a pact between two factions (units withdrawn from each other's bases, messages).
ADDR(0x53C650) void GAMEAPI pact_ends(int32_t faction_id, int32_t faction_id_with);
// Moves faction_id's units in or in the territory of faction_id_with's bases to the nearest own
// base they can reach. Returns the number of units moved.
ADDR(0x53C370) int32_t GAMEAPI pact_withdraw(int32_t faction_id, int32_t faction_id_with);
// communicate (0x54FFD0), the diplomacy dialog with a human player: engine/ui_bridge.h.
// Multiplayer: takes / releases the global diplomatic lock. diplo_lock returns non-zero when the
// player gave up waiting.
ADDR(0x539820) int32_t GAMEAPI diplo_lock(int32_t timeout);
ADDR(0x5398C0) void GAMEAPI diplo_unlock();
// Major atrocity (planet buster) by faction_id against faction_id_vs (-1 none): sanctions, the
// council's and every faction's reactions.
ADDR(0x55F450) void GAMEAPI major_atrocity(int32_t faction_id, int32_t faction_id_vs);
// Whether faction_id's and faction_id_with's areas in region connect (path search).
ADDR(0x53A780) int32_t GAMEAPI contiguous(int32_t faction_id, int32_t faction_id_with, int32_t region, int32_t a4);
// Units of faction_id_with in faction_id's land territory (outside regions where both fight a
// common enemy with bases there). Counts 1 per unit (+1 on an improved tile); flags bit 0: units
// near the claiming base (or any, with friction/a revenge or betrayal mood) are warned (flags |= 3)
// or, if already warned, count 1 more (+2 more with a treaty); bit 1: also with agenda bit 0x200.
// Marks the units (state 0x400), sets faction_id's player flag 0x10000 when any are found.
// base_id_out: the claiming base of the last one; count_out: warned units already flagged.
ADDR(0x55EB80) int32_t GAMEAPI territory(int32_t faction_id, int32_t faction_id_with, int32_t flags,
                                         int32_t* base_id_out, int32_t* count_out);
// AI factions with a treaty trade technology.
ADDR(0x55D430) void GAMEAPI enemies_trade_tech(int32_t faction_id, int32_t faction_id_with);

// ---- second half of the module (0x54B140-0x55F930; diplomacy_b.cpp) ----
// Native emulation state of wants_to_attack's uninitialised slot array (known uninitialised read
// #4, docs/RECONSTRUCTION.md): [f] is faction f's slot. Not original memory.
extern int32_t WantsToAttackResidue[8];
// wants_to_attack at a call that starts an evaluation (a single call, or the first call of an
// (a, b) / (b, a) pair): the oracle poisons the slot array before these calls (harness/README.md),
// so the slots start canonical (0). Native: clears WantsToAttackResidue. Hybrid: calls the
// oracle's poisoning thunk through g_harness_wants_to_attack_fresh (it fills the slots below the
// entry ESP and enters 0x55BC80). Not an original function.
int32_t wants_to_attack_fresh(int32_t faction_id, int32_t faction_id_target, int32_t odds_override);
extern int32_t(GAMEAPI* g_harness_wants_to_attack_fresh)(int32_t faction_id, int32_t faction_id_target,
                                                         int32_t odds_override);
// Whether faction_id is the overwhelming human leader (top ranked, more than max(4, (turn+25)/50)
// bases, difficulty above 1, intense rivalry or is_aggressive).
ADDR(0x539B70) int32_t GAMEAPI great_beelzebub(int32_t faction_id, int32_t is_aggressive);
// Both factions share their maps (visibility and seen improvements; traded_maps bits).
ADDR(0x543A20) void GAMEAPI trade_maps(int32_t faction_id, int32_t faction_id_with);
// Value of base_id to faction_id when faction_id_with offers or demands it (-1: none or not
// reachable): energy, size, nearby bases, facilities, projects, garrison; is_threat halves it
// where both have bases, is_gift multiplies by 4.
ADDR(0x54CB50) int32_t GAMEAPI value_of_base(int32_t base_id, int32_t faction_id, int32_t faction_id_with,
                                             int32_t is_threat, int32_t is_gift);
// Hands base_id over to faction_id: nearby units of the old owner move to its nearest base or
// change sides, production is reset, the base's supported units of the old owner are killed.
ADDR(0x54CF40) void GAMEAPI give_a_base(int32_t base_id, int32_t faction_id);
// Text helpers of the diplomacy dialogs (faction strings; get_insult_leader sets the parse
// gender defaults).
ADDR(0x5590E0) const char* GAMEAPI get_adjective_leader(int32_t faction_id);
ADDR(0x559100) const char* GAMEAPI get_adjective_insult_leader(int32_t faction_id);
ADDR(0x559120) const char* GAMEAPI get_adjective_faction(int32_t faction_id);
ADDR(0x559140) const char* GAMEAPI get_adjective_insult_faction(int32_t faction_id);
ADDR(0x559160) const char* GAMEAPI get_insult_leader(int32_t faction_id);
// Whether region is no real region (0 or 0x3F in the low 6 bits).
ADDR(0x5591C0) int32_t GAMEAPI bad_reg(int32_t region);
// Agenda bits of faction_id toward faction_id_with.
ADDR(0x5591E0) uint32_t GAMEAPI has_agenda(int32_t faction_id, int32_t faction_id_with, uint32_t agenda);
// value rounded down to a multiple of step (FUN_00559210).
ADDR(0x559210) int32_t GAMEAPI round_down(int32_t value, int32_t step);
// First contact of a human faction: the commlink dialog (communicate) or, in hotseat/PBEM and
// multiplayer, the pending-contact flags and NETMET messages; sets truce + informal truce.
ADDR(0x55C430) void GAMEAPI comm_check(int32_t faction_id, int32_t faction_id_with);
// Viewer messages (NetMsg): a pact partner attacks / ends the pact / signs a truce or treaty.
ADDR(0x55C730) void GAMEAPI pact_attacks(int32_t faction_id, int32_t faction_id_tgt);
ADDR(0x55CCA0) void GAMEAPI pact_unpact(int32_t faction_id, int32_t faction_id_with);
ADDR(0x55D040) void GAMEAPI pact_truce(int32_t faction_id, int32_t faction_id_with);
ADDR(0x55D190) void GAMEAPI pact_treaty(int32_t faction_id, int32_t faction_id_with);
// AI factions met on the map go to war (vendetta, map exchange, revenge moods may turn into
// grudges).
ADDR(0x55C840) void GAMEAPI enemies_war(int32_t faction_id, int32_t faction_id_with);
// A pact between two AI factions ends (messages to the viewer).
ADDR(0x55CE20) void GAMEAPI enemies_unpact(int32_t faction_id, int32_t faction_id_with);
// After a pact against faction_id_tgt: faction_id joins the war (or wants to talk to it).
ADDR(0x55D2A0) void GAMEAPI enemies_team_up(int32_t faction_id, int32_t faction_id_with, int32_t faction_id_tgt);
// Two AI factions that do not want war improve relations: truce, treaty, or a pact against a
// common enemy (else an existing pact ends). trade_tech: first trade technologies.
ADDR(0x55D760) void GAMEAPI enemies_treaty(int32_t faction_id, int32_t faction_id_with, int32_t trade_tech);

// ---- first half of the module (0x538F30-0x54B140; diplomacy.cpp) ----
// In these functions faction_id is usually the player who talks (often human) and
// faction_id_with the AI faction answering (the "rival" of the Diplo* globals, globals.h).

// Whether human players always contact each other (multiplayer / hotseat-PBEM rules options).
ADDR(0x539160) int32_t GAMEAPI auto_contact();
// Multiplayer-aware variants of agenda, energy, loan, map, pact, withdrawal, base and double
// cross changes: single player makes the change directly; multiplayer sends the message (and,
// with wait, waits until it is acknowledged). Treaty and technology variants: engine/probe.h.
ADDR(0x539310) void GAMEAPI net_agenda_on(int32_t faction_id, int32_t faction_id_with, uint32_t agenda, int32_t wait);
ADDR(0x539380) void GAMEAPI net_agenda_off(int32_t faction_id, int32_t faction_id_with, uint32_t agenda, int32_t wait);
ADDR(0x5393F0) void GAMEAPI net_set_agenda(int32_t faction_id, int32_t faction_id_with, uint32_t agenda, int32_t add,
                                           int32_t wait);
// Adds energy to faction_id (amount) and faction_id_with (amount_with); faction 0 is skipped.
ADDR(0x539460) void GAMEAPI net_energy(int32_t faction_id, int32_t amount, int32_t faction_id_with,
                                       int32_t amount_with, int32_t wait);
// Sets the loan faction_id owes faction_id_with (balance, payment per turn).
ADDR(0x539510) void GAMEAPI net_loan(int32_t faction_id, int32_t faction_id_with, int32_t balance, int32_t payment);
ADDR(0x539580) void GAMEAPI net_maps(int32_t faction_id, int32_t faction_id_with, int32_t wait);
ADDR(0x539660) void GAMEAPI net_pact_ends(int32_t faction_id, int32_t faction_id_with, int32_t wait);
// do_withdrawal; multiplayer: 1.
ADDR(0x5396D0) int32_t GAMEAPI net_withdrawal(int32_t faction_id, int32_t faction_id_with, int32_t wait);
// give_a_base(base_id, faction_id).
ADDR(0x539740) void GAMEAPI net_cede_base(int32_t base_id, int32_t faction_id, int32_t wait);
ADDR(0x5397B0) void GAMEAPI net_double_cross(int32_t faction_id, int32_t faction_id_with, int32_t a3, int32_t wait);
// X_pops_11 wait callbacks (passed by address): multiplayer: run the wait loop once; 0 when the
// popup must close (turn ended; pop_expire also when the game is over), else 1.
ADDR(0x5398E0) int32_t GAMEAPI pop_wait();
GLOBAL(int32_t, PopWaitActive, 0x93FAC4);  // set while pop_wait/pop_expire run the wait loop
ADDR(0x539920) int32_t GAMEAPI pop_expire();
// Mood of faction_id_with toward faction_id (from DiploFriction, set by diplomacy_check) as the
// caption of the diplomacy dialog (parse slot 7); redraws the dialog's info pane when it is open.
// Clamps DiploFriction to 15 for a vendetta with friction <= 14.
ADDR(0x5399A0) void GAMEAPI diplomacy_caption(int32_t faction_id, int32_t faction_id_with);
// Mood index (Mood[], 0 best .. 8 worst) of a friction value.
ADDR(0x53A090) int32_t GAMEAPI get_mood(int32_t friction);
// How many proposals faction_id_with listens to from faction_id (vendetta 1, pact 6 or 500 when
// surrendered, else from treaty and DiploFriction).
ADDR(0x53A150) int32_t GAMEAPI get_patience(int32_t faction_id, int32_t faction_id_with);
// Value of an amount of energy in a trade (diminishing per 50 / 100 credits).
ADDR(0x53A1C0) int32_t GAMEAPI energy_value(int32_t energy);
// faction_id_with (if it wants to speak) mentions its prototype DiploMentionUnitID: with a pact
// both exchange all their prototypes (with the "PROTO"+"RIVAL" popup's consent otherwise).
ADDR(0x53A230) void GAMEAPI mention_prototypes(int32_t faction_id, int32_t faction_id_with);
// DiploBestUnitID: faction_id_with's best prototype that faction_id has no equal of;
// DiploMentionUnitID: the one it would mention (not yet mentioned/traded, not a probe/transport).
ADDR(0x53A4A0) void GAMEAPI scan_prototypes(int32_t faction_id, int32_t faction_id_with);
// Evaluates faction_id_with's attitude toward faction_id into the Diplo* globals (friction,
// hostility, fear, tribute demand, attack chances, enemies and allies, betrayal). is_full: also
// the dialog extras (spoke turn, factions to mention, prototypes, betrayal). Uses
// wants_to_attack at a first-call site (wants_to_attack_fresh).
ADDR(0x53A980) void GAMEAPI diplomacy_check(int32_t faction_id, int32_t faction_id_with, int32_t is_full);
// Technologies faction_id and faction_id_with could trade: DiploTradeWantTech1-4 (faction_id
// has, faction_id_with wants), DiploTradeOfferTech1/2 (faction_id_with has; projects in
// production halve their value: DiploTradeOfferProject1/2, DiploTradeTechProject).
ADDR(0x53E800) void GAMEAPI tech_analysis(int32_t faction_id, int32_t faction_id_with);

// faction_id_with (AI) asks faction_id (human) to talk: the COMM popup (or URGENTCOMM when it
// has planet busters not yet announced) until the human accepts, declines or ends the caption;
// on acceptance the dialog opens with introduce. Returns DiploCaptionMode (non-zero: talking).
// Humans and aliens need Xenoempathy/Centauri Psi first.
ADDR(0x53DCE0) int32_t GAMEAPI wants_to_speak(int32_t faction_id, int32_t faction_id_with);
// faction_id_with's opening words (INTRONEW/INTROMAD/INTRO popups, random variants); a first
// warning about faction_id's planet busters sets status 0x4000.
ADDR(0x53D360) void GAMEAPI introduce(int32_t faction_id, int32_t faction_id_with);
// Treaties agreed in the dialog: faction_id_with signs a pact (maps exchanged, prototypes
// mentioned), a treaty (units in each other's territory are asked to leave) or a truce (show:
// the MAKETRUCE message) with faction_id. Reputation of both rises.
ADDR(0x53CAF0) void GAMEAPI pact_of_brotherhood(int32_t faction_id, int32_t faction_id_with);
ADDR(0x53CE10) void GAMEAPI make_treaty(int32_t faction_id, int32_t faction_id_with);
ADDR(0x53D160) void GAMEAPI pledge_truce(int32_t faction_id, int32_t faction_id_with, int32_t show);
// Closes the diplomacy dialog state (DiploWinState, caption flags, map focus).
ADDR(0x53E470) void GAMEAPI diplomacy_ends(int32_t faction_id, int32_t faction_id_with);
GLOBAL(int32_t, DiploTalkActive, 0x93FAB8);  // cleared by diplomacy_ends
GLOBAL(int32_t, DiploFocusX, 0x68D43C);      // map location shown during the dialog (-1 none)
GLOBAL(int32_t, DiploFocusY, 0x68D440);
// faction_id (human) demands that faction_id_with withdraws its units from its territory
// (commlink checks first; sunspots: only a message about the answer).
ADDR(0x53E4C0) void GAMEAPI demands_withdrawal(int32_t faction_id, int32_t faction_id_with);
// faction_id (human) renounces its pact with faction_id_with: blemish, pact ends.
ADDR(0x53E5C0) void GAMEAPI renounce_pact(int32_t faction_id, int32_t faction_id_with);
// faction_id_with's units leave faction_id's land territory (to an own base, transports to an
// adjacent own tile or ship). Returns the number of moves (agenda 2 and status 0x100000 set).
ADDR(0x548F80) int32_t GAMEAPI do_withdrawal(int32_t faction_id, int32_t faction_id_with);
// The ally of faction_id whose base faction_id_with's units in faction_id's territory are heading
// for (so it refuses to withdraw them), 0 if none.
ADDR(0x548DD0) int32_t GAMEAPI dont_withdrawal(int32_t faction_id, int32_t faction_id_with);
// The answer of faction_id_with to faction_id's demand to withdraw: 0 units withdrawn (or none to
// withdraw), 1 refused, 2 refused with a vendetta or broken treaty.
ADDR(0x549270) int32_t GAMEAPI demand_withdrawal(int32_t faction_id, int32_t faction_id_with);
// Base of a common enemy that faction_id_with suggests attacking together with faction_id (the
// current target if it is one), -1 none.
ADDR(0x54ACC0) int32_t GAMEAPI suggest_plan(int32_t faction_id, int32_t faction_id_with);
// faction_id's base closest to base_id (same region preferred; the faction's target location
// counts as distance 0), 0 if none.
ADDR(0x54AFA0) int32_t GAMEAPI attack_from(int32_t base_id, int32_t faction_id);

// faction_id (human) buys DiploTradeOfferTech1 from faction_id_with (mode 5: its best prototype
// DiploBestUnitID; mode 13: a commlink to DiploProposalFaction): the BUY... popup with the price
// (eager: 1.5 times; offer 1: no purchase; offer 5 or mode 5: offered even when unaffordable).
// Returns 1 if an offer was made.
ADDR(0x5401A0) int32_t GAMEAPI buy_tech(int32_t faction_id, int32_t faction_id_with, int32_t offer, int32_t eager,
                                        int32_t mode);
// faction_id (human) and faction_id_with exchange technologies (or maps, 9999): the AI's refusals
// (hate, fear, its lead, projects), gifts of surrendered or strong pact partners, else the
// TRADETECH offer; offer 2/4: the human proposes the trade; mode 5: prototypes, 13: a commlink
// frequency for a technology; counter: repeat while trades succeed. Returns 1 if a trade was made.
ADDR(0x541100) int32_t GAMEAPI tech_trade(int32_t faction_id, int32_t faction_id_with, int32_t counter, int32_t offer,
                                          int32_t mode);
// faction_id (human) asks faction_id_with for energy (by DiploCounterProposal): a gift from a rich
// pact partner, technology sold for energy, or a loan. Returns 1 if energy changed hands.
ADDR(0x540630) int32_t GAMEAPI energy_trade(int32_t faction_id, int32_t faction_id_with);
// faction_id (human) proposes to exchange maps with faction_id_with (accepted with a pact or low
// friction: DiploMapMsg set).
ADDR(0x545D60) void GAMEAPI propose_trade_maps(int32_t faction_id, int32_t faction_id_with);

// The human (faction_id) proposes a treaty / a pact to faction_id_with, possibly with a counter
// offer (DiploCounterProposal): its acceptance, a requested bribe (technology or energy) or help
// against an enemy, then make_treaty / pact_of_brotherhood. Return 0 when a counter proposal
// (DiploCounterProposal 1 or 3) was not taken up, else 1.
ADDR(0x544E40) int32_t GAMEAPI propose_treaty(int32_t faction_id, int32_t faction_id_with);
ADDR(0x543BC0) int32_t GAMEAPI propose_pact(int32_t faction_id, int32_t faction_id_with);
// The human (faction_id) threatens faction_id_with to obtain DiploCurrentProposal (energy,
// technology, a commlink, a base, war on DiploProposalFaction): given in fear, ignored, or answered
// with a vendetta / the end of the pact.
ADDR(0x549930) void GAMEAPI threaten(int32_t faction_id, int32_t faction_id_with);
// The human (faction_id) asks for base DiploAskBaseSwapID (for energy, technology or base
// DiploBidBaseSwapID). Returns 0 if a base changed hands.
ADDR(0x54D410) int32_t GAMEAPI base_swap(int32_t faction_id, int32_t faction_id_with);
// The human (faction_id) gives faction_id_with technology (DiploCounterProposal 4: one, 7: all),
// energy (5: amount from the OFFERENERGY menu) or base DiploBidBaseSwapID (8): friction falls,
// large gifts end revenge moods and count against integrity blemishes.
ADDR(0x547C10) void GAMEAPI make_gift(int32_t faction_id, int32_t faction_id_with);
// The human (faction_id) asks faction_id_with for its council vote on proposal (0: governor
// election of candidate; candidate -1/-2/-3: YEA/NAY/ABSTAIN): its price in energy or
// technology (BUYVOTEMENU); the promise is kept in faction_id_with's unk_29/unk_30 and cast at
// once when the council is in session.
ADDR(0x53EB50) void GAMEAPI buy_council_vote(int32_t faction_id, int32_t faction_id_with, int32_t proposal,
                                             int32_t candidate);
// The human (faction_id) asks faction_id_with to declare war on DiploProposalFaction: refused,
// done at once by a pact partner, or for technology / energy; the target may learn who incited
// it. Returns 0 when a counter proposal (DiploCounterProposal 3) was not taken up, else 1.
ADDR(0x545E80) int32_t GAMEAPI propose_attack(int32_t faction_id, int32_t faction_id_with);
// The human (faction_id) asks faction_id_with to end its vendetta with a faction of the CALLOFF
// menu.
ADDR(0x54BEC0) void GAMEAPI call_off_vendetta(int32_t faction_id, int32_t faction_id_with);

// ---- callees of other modules reconstructed here ----
// ui module (0x445130): faction_id's energy budget for the reports (FactionCurrent* globals,
// EnergyBaseMaint, EnergyFacilityCount); with recompute, base_compute of each base first.
// diplomacy_check reads FactionCurrentNetIncome.
ADDR(0x445130) void GAMEAPI energy_compute(int32_t faction_id, int32_t recompute);
GLOBAL(int32_t[512], EnergyBaseMaint, 0x7492E0);    // per base: facility maintenance
GLOBAL(int32_t, FactionCurrentLabs, 0x749AE0);      // labs total (then research points per turn)
GLOBAL(int32_t[70], EnergyFacilityCount, 0x749AEC); // facilities 0-64 built in the faction's bases
// game module text helpers (0x58F700-0x58FAF0): "$<M1:text>" gender/number markup of
// faction_id's noun around text appended to buffer; leader file phrases (#<filename> of
// faction_id: line 9 + n; get_noun_phrase sets the gender defaults from its 2nd/3rd items);
// pact brother/sister label(s) (get_pacts_2: of two factions).
ADDR(0x58F700) void GAMEAPI say_fac_special(char* buffer, const char* text, int32_t faction_id);
ADDR(0x58F810) const char* GAMEAPI get_phrase(int32_t faction_id, int32_t n);
ADDR(0x58F8B0) const char* GAMEAPI get_noun_phrase(int32_t faction_id, int32_t n);
ADDR(0x58F9D0) const char* GAMEAPI get_pact(int32_t faction_id);
ADDR(0x58FA20) const char* GAMEAPI get_pacts(int32_t faction_id);
ADDR(0x58FA70) const char* GAMEAPI get_pacts_2(int32_t faction_id, int32_t faction_id_with);
