// Planetary Council sessions (see council.h).
#include "engine/council.h"

#include "engine/clock.h"
#include "engine/crt.h"
#include "engine/diplo_act.h"
#include "engine/diplomacy.h"
#include "engine/net.h"
#include "engine/parse.h"
#include "engine/probe.h"
#include "engine/rules.h"
#include "engine/tech.h"
#include "engine/turn.h"
#include "engine/ui_bridge.h"

namespace {
inline bool is_human(int32_t faction_id) { return (FactionStatus[0] & (uint8_t)(1u << (faction_id & 31))) != 0; }
inline bool is_alive(int32_t faction_id) { return (FactionStatus[1] & (uint8_t)(1u << (faction_id & 31))) != 0; }
inline bool is_alien(int32_t faction_id) { return (MFactions[faction_id].rule_flags & RFLAG_ALIEN) != 0; }
inline CouncilSession& session(uint8_t* self) { return *reinterpret_cast<CouncilSession*>(self + kCouncilWinSession); }

// parse_says of faction_id's leader title (n) and name (n + 1).
void say_leader(int32_t n, int32_t faction_id) {
    PluralDefault = 0;
    GenderDefault = MFactions[faction_id].is_leader_female;
    parse_says(n, MFactions[faction_id].title_leader, -1, -1);
    GenderDefault = MFactions[faction_id].is_leader_female;
    PluralDefault = 0;
    parse_says(n + 1, MFactions[faction_id].name_leader, -1, -1);
}
}  // namespace

void GAMEAPI council(int32_t faction_id, int32_t proposal, int32_t candidate) {
    int32_t governor = GovernorFaction;
    // A session makes everyone talk: commlinks between all non-alien factions.
    for (int32_t i = 1; i < 8; i++) {
        for (int32_t j = 1; j < i; j++) {
            if (!is_alien(j) && !is_alien(i)) treaty_on(i, j, DIPLO_COMMLINK);
        }
    }
    if (PbemActive && CouncilSessionPending) {
        // PBEM: the session called in an earlier turn resumes.
        faction_id = CouncilSessionPending;
        proposal = session(&CouncilWin).proposal;
        candidate = session(&CouncilWin).candidate;
    } else {
        // The caller's last call: for proposals other than elections, `candidate` is the caller
        // (call_council).
        if (proposal != 0) Factions[candidate].council_call_turn = CurrentTurn;
        ProposalCallTurn[proposal] = CurrentTurn;
    }
    if (faction_id != CurrentPlayerFaction || !is_human(faction_id)) {
        say_leader(0, faction_id);
        parse_say(2, Proposal[proposal].name, -1, -1);
        popp(ScriptFileName, "CALLSCOUNCIL", 0, "council_sm.pcx", 0);
    }
    GameState |= STATE_COUNCIL_HAS_CONVENED;
    CouncilWin_session(&CouncilWin, proposal, candidate);
    if (proposal == 0 && GovernorFaction == CurrentPlayerFaction && governor != CurrentPlayerFaction &&
        !MultiplayerActive && (!PbemActive || !CouncilSessionPending) &&
        !(Factions[CurrentPlayerFaction].player_flags & PFLAG_BEEN_ELECTED_GOVERNOR)) {
        Factions[CurrentPlayerFaction].player_flags |= PFLAG_BEEN_ELECTED_GOVERNOR;
        popp(ScriptFileName, "GOVERNOR", 0, "council_sm.pcx", 0);
    }
}

void THISCALL CouncilWin_session(uint8_t* self, int32_t proposal, int32_t candidate) {
    CouncilSession& s = session(self);
    ui_councilwin_open(self);
    s.proposal = proposal;
    s.candidate = candidate;
    s.governor = GovernorFaction;
    s.voter_mask = PbemActive ? 0xFF : ~(uint32_t)FactionStatus[0];
    CouncilWinActive = 1;
    s.start_time = engine_real_time();
    int32_t humans = bit_count(FactionStatus[0]);
    s.unk_a40 = 1;
    s.time_limit = humans * 60000;
    if (!PbemActive || !CouncilSessionPending) {
        s.result = 0;
        for (int32_t i = 0; i < 8; i++) s.council_votes[i] = council_votes(i);
        // Humans vote in the window (in single player only the viewer; the others are asked
        // like AIs).
        for (int32_t pass = 3; pass; pass--) {
            for (int32_t i = 0; i < 8; i++) {
                int32_t vote;
                if (!is_human(i) || (i != CurrentPlayerFaction && !MultiplayerActive && !PbemActive))
                    vote = council_get_vote(i, s.proposal, candidate);
                else
                    vote = 0;
                s.votes[i] = vote;
                if (!is_alive(i)) s.votes[i] = -5;
            }
        }
        if (PbemActive) CouncilSessionPending = CurrentPlayerFaction;
    }
    ui_councilwin_build(self);
    ControlWaitLoop = 1;
    if (MultiplayerActive) message_data(0x2602, 0, CurrentPlayerFaction, 0, 0, 0);
    int32_t hidden = ui_councilwin_show(self);
    // Elections: a viewer who cannot win (not among the two largest voters) and plays may be
    // offered the votes of an AI faction that could swing the election, for energy credits
    // (VOTEFORME) or technologies (VOTEFORMETECH). An observer (faction 0) never is: with no
    // votes of its own it never swings an election.
    if (!MultiplayerActive && s.proposal == 0 && !eligible(CurrentPlayerFaction) &&
        Factions[CurrentPlayerFaction].diff_level) {
        int32_t viewer = CurrentPlayerFaction;
        int32_t backing[7];  // votes for candidate c + 1 among the living non-alien factions
        uint8_t alive = FactionStatus[1];
        for (int32_t c = 1; c < 8; c++) {
            backing[c - 1] = 0;
            if (is_alien(c) || !(alive & (1 << c))) continue;
            for (int32_t i = 1; i < 8; i++) {
                if (!is_alien(i) && (alive & (1 << i)) && s.votes[i] == c) backing[c - 1] += s.council_votes[i];
            }
        }
        bool traded = false;
        for (int32_t f = 1; f < 8; f++) {
            if (is_alien(f) || !is_alive(f) || !eligible(f)) continue;
            int32_t rival = 0;  // the strongest other candidate's votes
            for (int32_t c = 1; c < 8; c++) {
                if (!is_alien(c) && c != f && rival <= backing[c - 1]) rival = backing[c - 1];
            }
            int32_t mine = s.council_votes[viewer];
            if (mine + backing[f - 1] <= rival || mine + rival <= backing[f - 1]) continue;
            diplomacy_check(viewer, f, 0);
            if (DiploFriction >= 15 || DiploHostility || traded) continue;
            // Pact, vendetta, revenge, betrayal, atrocities.
            if (Factions[f].diplo_status[viewer] & 0x40C31) continue;
            int32_t price = CurrentTurn + Factions[f].base_count * 5;
            int32_t limit = Factions[f].energy_credits * 3 / 4;
            if (limit < price) price = limit;
            int32_t cost = price / 25 * 25;
            if (cost && Factions[f].energy_credits >= cost) {
                parse_num(0, cost);
                diplomacy_caption(viewer, f);
                say_leader(0, viewer);
                say_leader(2, f);
                clock_wait(500);
                if (X_pops_11("VOTEFORME", 0, FactionPortraits[f], 0)) {
                    Factions[f].energy_credits -= cost;
                    Factions[viewer].energy_credits += cost;
                    Console_update_data(&MainConsole, 0);
                    CouncilWin_vote(self, viewer, f);
                }
                break;
            }
            tech_analysis(viewer, f);
            if (DiploTradeOfferTech1 < 0 || DiploTradeOfferTech2 < 0) continue;
            StrBuffer[0] = 0;
            say_tech_2(StrBuffer, DiploTradeOfferTech1, 0);
            parse_says(4, StrBuffer, -1, -1);
            StrBuffer[0] = 0;
            say_tech_2(StrBuffer, DiploTradeOfferTech2, 0);
            parse_says(5, StrBuffer, -1, -1);
            diplomacy_caption(viewer, f);
            say_leader(0, viewer);
            say_leader(2, f);
            clock_wait(500);
            if (X_pops_11("VOTEFORMETECH", 0, FactionPortraits[f], 0)) {
                traded = true;
                tech_achieved(viewer, DiploTradeOfferTech1, f, 0);
                tech_achieved(viewer, DiploTradeOfferTech2, f, 0);
                CouncilWin_vote(self, viewer, f);
            }
        }
    }
    ui_councilwin_modal(self);
    ui_councilwin_close(self, hidden);
}

void THISCALL CouncilWin_vote(uint8_t* self, int32_t faction_id, int32_t vote) {
    CouncilSession& s = session(self);
    if (s.result) return;
    s.votes[faction_id] = vote;
    bool humans_pending = true;
    for (int32_t i = 0; i < 8; i++) {
        if (is_human(i) && s.votes[i]) humans_pending = false;
    }
    if (humans_pending) {
        for (int32_t pass = 3; pass; pass--) {
            for (int32_t i = 0; i < 8; i++) {
                if (is_alive(i) && !is_human(i)) s.votes[i] = council_get_vote(i, s.proposal, s.candidate);
            }
        }
    }
    ui_councilwin_redraw(self);
    if (!MultiplayerActive) {
        CouncilWin_tally(self, 1);
    } else if (NetSessionState[0]) {
        if (CouncilWin_tally(self, 0)) message_data(0x2603, 0, 0, 0, 0, 0);
    }
    if (PbemActive && faction_id == CurrentPlayerFaction) {
        for (int32_t i = 0; i < 6; i++) ui_councilwin_panel_draw(self, i);
        ui_councilwin_button_draw(self);
    }
}

int32_t THISCALL CouncilWin_tally(uint8_t* self, int32_t apply) {
    CouncilSession& s = session(self);
    int32_t for_candidate[8] = {};
    int32_t yea = 0, nay = 0, total = 0;
    bool veto = false;
    for (int32_t i = 1; i < 8; i++) {
        if (is_alien(i)) continue;
        int32_t vote = s.votes[i];
        if (!vote) return 0;
        if (vote < -2 || !is_alive(i)) continue;
        // Elections and Supreme Leader: by council votes; other proposals: one per faction.
        total += s.proposal > 1 ? 1 : s.council_votes[i];
        if (vote == -1) {
            yea += s.proposal > 1 ? 1 : s.council_votes[i];
        } else {
            nay += s.proposal > 1 ? 1 : s.council_votes[i];
            if (i == GovernorFaction && s.proposal > 1) veto = true;
        }
        if (vote > 0) for_candidate[vote] += s.council_votes[i];
    }
    // The governor's veto does not stop a proposal everybody else supports.
    if (veto && yea == total - 1) veto = false;
    if (!apply) return 1;
    if (s.proposal == 0) {
        // The most votes wins; ties keep the governor, otherwise nobody is elected. Candidates are
        // examined from (CurrentTurn % 8).
        int32_t best = 0;
        for (int32_t i = 0; i < 8; i++) {
            int32_t c = (CurrentTurn + i) % 8;
            if (!c || for_candidate[c] < best) continue;
            if (for_candidate[c] != best) {
                best = for_candidate[c];
                s.candidate = c;
            } else if (GovernorFaction == c) {
                s.candidate = c;
            } else if (GovernorFaction != s.candidate) {
                s.candidate = 0;
            }
        }
        s.result = s.candidate ? 1 : -1;
    } else if (yea > nay && (s.proposal != 1 || total * 3 / 4 <= yea)) {
        // Supreme Leader needs three quarters of the votes.
        s.result = veto ? -2 : 1;
    } else {
        s.result = -1;
    }
    int32_t viewer = CurrentPlayerFaction;
    CouncilSessionPending = 0;
    if (PbemActive) {
        // The other human players are told the result in their turns.
        uint8_t humans = FactionStatus[0];
        for (int32_t i = 1; i < 8; i++) {
            if ((humans & (1 << i)) && i != viewer) {
                CouncilProposal[i] = s.proposal;
                CouncilVoteState[i] = s.result;
            }
        }
    }
    ui_councilwin_redraw(self);
    ProposalCallTurn[s.proposal] = CurrentTurn;
    for (int32_t i = 1; i < 8; i++) {
        Factions[i].unk_29[s.proposal] = 0;
        Factions[i].unk_30[s.proposal] = 0;
    }
    for (int32_t i = 0; i < 6; i++) {
        ui_councilwin_panel_draw(self, i);
        ui_councilwin_button_draw(self);
    }
    if (s.result == 1) {
        if (s.proposal == 0) {
            GovernorFaction = s.candidate;
            ui_councilwin_redraw(self);
        }
        if (MultiplayerActive) council_action(s.proposal, s.candidate);
    }
    return 1;
}

int32_t THISCALL CouncilWin_key(uint8_t* self, int32_t, int32_t key) {
    CouncilSession& s = session(self);
    if (key != 0x1B && key != 0x0D) return 1;
    int32_t viewer = CurrentPlayerFaction;
    if (is_human(viewer) &&
        (!(GameState & STATE_SCENARIO_EDITOR) || !(GameState & STATE_DEBUG_MODE) || PbemActive) && !s.result) {
        if (!PbemActive || !CouncilSessionPending || !s.votes[viewer]) {
            // The human has not decided yet: fill in the votes of the AI factions.
            for (int32_t i = 0; i < 8; i++) {
                if (!s.votes[i] && !is_human(i)) s.votes[i] = council_get_vote(i, s.proposal, s.candidate);
            }
            return 1;
        }
        // PBEM: the viewer voted; the session continues in the other players' turns.
        ui_councilwin_release(self);
        ControlWaitLoop = 0;
        CouncilWinActive = 0;
        ui_councilwin_close_fonts(self);
        return 1;
    }
    ui_councilwin_release(self);
    ControlWaitLoop = 0;
    CouncilWinActive = 0;
    ui_councilwin_close_fonts(self);
    if (!MultiplayerActive && s.result == 1) council_action(s.proposal, s.candidate);
    return 1;
}

int32_t (*g_harness_council_modal)(uint8_t* win) = nullptr;
