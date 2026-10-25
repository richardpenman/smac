// Headless implementations of the UI-layer functions used by the turn module (engine/ui_bridge.h).
// Native builds only: hybrid builds call the original UI.
#ifndef SMACX_HYBRID
#include "engine/ui_bridge.h"

#include "engine/crt.h"
#include "engine/game.h"
#include "engine/turn.h"
#include "platform/win32.h"

// Only the state effects of the original: the interlude is recorded in GameInterludeState (as the
// viewer's "no interludes" preference does); its text and movie are not shown.
void GAMEAPI interlude(int32_t id, int32_t, int32_t, int32_t) {
    if (MultiplayerActive || DiploWinState) return;
    bool alien_viewer = (MFactions[CurrentPlayerFaction].rule_flags & 0x80) != 0;
    if (id < 0x20) {
        if ((GameInterludeState & (1 << id)) || alien_viewer) return;
    } else {
        if ((GameInterludeState & (1 << ((id - 0x20) & 31))) || !alien_viewer) return;
    }
    if ((GameState & 0x10) || (GameRules & 0x200000)) return;
    StrBuffer[0] = 0;
    crt_strcat(StrBuffer, MFactions[CurrentPlayerFaction].filename);
    win32::CharUpperA(StrBuffer);
    if (!crt_strcmp(StrBuffer, "FUNGBOY")) return;
    if (id < 0x20) GameInterludeState |= 1 << id;
    if (id == 6) GameInterludeState |= 0x12;
    if (GamePreferences & 0x40000) return;
    // Shown interlude (texts not built headless): alien interludes record their bit now.
    if (id >= 0x20) {
        id -= 0x20;
        if (id == 3) GameInterludeState |= 0x10;
        GameInterludeState |= 1 << (id & 31);
    }
}
int32_t GAMEAPI energy_limit(int32_t) { return 10; }
void THISCALL MessageWin_clear(uint8_t*) {}
void GAMEAPI clear_council_notify(int32_t faction_id) {
    CouncilProposal[faction_id] = 0;
    CouncilVoteState[faction_id] = 0;
}
void GAMEAPI diplo(int32_t, int32_t) {}
void GAMEAPI show_credits() {}
void GAMEAPI quayle(int32_t) {}
// The end of the game for the harness (g_harness_game_end: final snapshot, exit), as the oracle
// does at the original's show_replay.
void GAMEAPI show_replay() {
    if (g_harness_game_end) g_harness_game_end();
}
// The script runner of the headless app takes the place of the main menu (see g_harness_top_menu);
// without it the game quits.
int32_t GAMEAPI top_menu(int32_t) { return 1; }
int32_t THISCALL Net_check_for_lobby(uint8_t*, char*, uint8_t*, int32_t, int32_t) { return 0; }
void THISCALL FX_init(uint8_t*) {}
int32_t THISCALL Wave_is_playing(uint8_t*) { return 0; }
void GAMEAPI desktop_close() {}
void GAMEAPI close_opening() {}
int32_t GAMEAPI multiplayer_init(int32_t) { return 1; }
void THISCALL GraphicWin_fill(uint8_t*, int32_t) {}
void THISCALL Win_show(uint8_t*, int32_t) {}
bool ui_directplay_registered() { return false; }
void ui_main_mapwin_set_options(uint32_t) {}
int32_t ui_hotseat_switch(int32_t) { return 0; }
void ui_hotseat_show(int32_t) {}
#endif
