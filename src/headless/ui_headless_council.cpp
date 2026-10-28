// Headless implementations of the council window's operations (engine/council.h).
#include "engine/council.h"

void ui_councilwin_open(uint8_t*) {}
void ui_councilwin_build(uint8_t*) {}
int32_t ui_councilwin_show(uint8_t*) { return 0; }
// No human is ever interactive headless: the viewer acts as the oracle's observer does (see
// harness/README.md, "Planetary Council sessions"). It casts the vote the window holds for its
// faction (for faction 0 the tally ignores it; an AI viewer keeps its own AI vote) and closes the
// window with Enter, which applies a passed proposal.
void ui_councilwin_modal(uint8_t* win) {
    const CouncilSession& s = CouncilWinSession;
    int32_t viewer = CurrentPlayerFaction;
    if (!s.result) CouncilWin_vote(win, viewer, s.votes[viewer]);
    CouncilWin_key(win, 0, 0x0D);
}
void ui_councilwin_close(uint8_t*, int32_t) {}
void ui_councilwin_redraw(uint8_t*) {}
void ui_councilwin_panel_draw(uint8_t*, int32_t) {}
void ui_councilwin_button_draw(uint8_t*) {}
void ui_councilwin_release(uint8_t*) {}
void ui_councilwin_close_fonts(uint8_t*) {}
