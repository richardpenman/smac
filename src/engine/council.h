// Planetary Council sessions: the session logic of the council window (CouncilWin, original ui
// module around 0x424410-0x428550): AI votes, the viewer's vote, the tally and the result. The
// council's AI (council_votes, council_get_vote, council_action, call_council) is in turn.h.
#pragma once
#include "engine/globals.h"

// ---- the council window --------------------------------------------------------------------------
// CouncilWin (vtable 0x66AAE8), a UI object in the image. Its session fields (the methods below
// reach them through `self`, which is always CouncilWin): see CouncilSession.
GLOBAL(uint8_t, CouncilWin, 0x6FEC80);
constexpr uint32_t kCouncilWinSession = 0xA1C;  // offset of CouncilSession in CouncilWin
// Session fields of CouncilWin (+0xA1C .. +0xA93).
struct CouncilSession {
    int32_t proposal;
    int32_t candidate;  // elections: the candidate, then the winner (0: none)
    int32_t result;  // 0 not decided, 1 passed, -1 failed / no majority, -2 vetoed by the governor
    int32_t governor;  // GovernorFaction when the session opened
    int32_t unk_a2c[3];
    uint32_t start_time;  // timeGetTime() when the session opened
    int32_t time_limit;  // ms: 60 s per human player (multiplayer)
    int32_t unk_a40;  // 1
    uint32_t voter_mask;  // ~humans (PBEM: 0xFF)
    uint8_t spot[0xC];  // Spot object (UI)
    int32_t votes[8];  // vote per faction: candidate (elections), -1 YEA, -2 NAY, -3 abstain,
    // -5 dead, 0 not voted yet
    int32_t council_votes[8];  // council_votes() per faction
};
SMAC_LAYOUT(CouncilSession, 0x78);
// Non-zero while the council window is open (0x703DE4).
GLOBAL(int32_t, CouncilWinActive, 0x703DE4);

// ---- callees of other modules ------------------------------------------------------------------------
// tech_analysis: engine/diplomacy.h.

// ---- council -------------------------------------------------------------------------------------
// faction_id calls a session of the Planetary Council on proposal (candidate: the governor
// candidate it backs, else faction_id): announces it (CALLSCOUNCIL), lets the window run the
// vote (CouncilWin_session) and tells a newly elected human governor (GOVERNOR). PBEM: a pending
// session (CouncilSessionPending) is resumed instead.
ADDR(0x428350) void GAMEAPI council(int32_t faction_id, int32_t proposal, int32_t candidate);
// FUN_00427230: opens the council window, computes every faction's vote (three passes of
// council_get_vote; the viewer, a human, votes in the window), lets a viewer who cannot win an
// election be offered AI votes (VOTEFORME / VOTEFORMETECH), then runs the window's modal loop
// until it is closed (CouncilWin_vote, CouncilWin_key).
ADDR(0x427230) void THISCALL CouncilWin_session(uint8_t* self, int32_t proposal, int32_t candidate);
// FUN_00424720: faction_id casts vote; when no human has voted, the non-human factions' votes are
// computed again (three passes); then the tally (CouncilWin_tally). Nothing once decided.
ADDR(0x424720) void THISCALL CouncilWin_vote(uint8_t* self, int32_t faction_id, int32_t vote);
// FUN_00424410: counts the votes of factions 1-7. Returns 0 while one of them has not voted.
// apply: sets the result, ProposalCallTurn, clears the factions' vote commitments on the proposal
// and elects the governor (multiplayer: also council_action).
ADDR(0x424410) int32_t THISCALL CouncilWin_tally(uint8_t* self, int32_t apply);
// FUN_00425BA0: key handler of the window. Enter/Escape: for a human viewer who has not decided
// yet, the AI votes are filled in; otherwise the window is closed and a passed proposal applied
// (council_action). Returns 1.
ADDR(0x425BA0) int32_t THISCALL CouncilWin_key(uint8_t* self, int32_t a1, int32_t key);

// ---- window operations (not original functions) ------------------------------------------------
// The parts of the methods above that work on the window itself: fonts, layout, buttons, panels,
// drawing and the modal loop. smacx: the window (src/ui/councilwin.cpp); headless: nothing, except:
// ui_councilwin_modal: the original's modal loop (Win_set_modal) returns once the viewer has voted
// and closed the window. Headless (no human is ever interactive) does what the oracle's
// observer does: the viewer casts the vote the window
// holds for its faction (CouncilWin_vote, unless decided) and presses Enter (CouncilWin_key).
void ui_councilwin_open(uint8_t* win);  // panels and fonts closed, layout, fonts opened
void ui_councilwin_build(uint8_t* win);  // portraits, frame, buttons; panels drawn
int32_t ui_councilwin_show(uint8_t* win);  // hides MultiWin/AlphaMenu (returned), shows it
void ui_councilwin_modal(uint8_t* win);
void ui_councilwin_close(uint8_t* win, int32_t hidden);  // releases its interface, menus back
void ui_councilwin_redraw(uint8_t* win);  // vtable+0xF8
void ui_councilwin_panel_draw(uint8_t* win, int32_t panel);  // panel 0-5 (vtable+8)
void ui_councilwin_button_draw(uint8_t* win);  // the close button (vtable+4)
void ui_councilwin_release(uint8_t* win);  // ends the modal loop (vtable+0xE8)
void ui_councilwin_close_fonts(uint8_t* win);  // panels and fonts closed, everything redrawn
