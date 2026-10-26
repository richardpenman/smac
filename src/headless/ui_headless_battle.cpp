// Headless implementations of the UI-layer functions used by the battle module (engine/ui_bridge.h,
// boom in engine/battle.h). Native builds only: hybrid builds call the original UI.
#ifndef SMACX_HYBRID
#include "engine/battle.h"
#include "engine/game_state.h"
#include "engine/ui_bridge.h"

void THISCALL SubInterface_set_iface_mode(uint8_t*) {}
void THISCALL BattleWin_clear(uint8_t*) {}
void THISCALL BattleWin_pulse_timer(uint8_t*) {}
void THISCALL BattleWin_stop_timer(uint8_t*) {}
void THISCALL StatusWin_draw(uint8_t*, int32_t, int32_t, int32_t, int32_t, int32_t) {}
void THISCALL StatusWin_redraw(uint8_t*) {}
void THISCALL StatusWin_reset(uint8_t*) {}
// As the original: the hint is due once, for a single-player game with tutorial hints (preference
// 0x20) and a non-alien viewer; the hint itself (shown by the caller) is not displayed headless.
int32_t GAMEAPI tut_check2(int32_t bit) {
    if (MultiplayerActive) return 0;
    if (!(GamePreferences & PREF_BSC_TUTORIAL_MSGS)) return 0;
    if (bit & TutorialHintsShown) return 0;
    if (game().mfactions[CurrentPlayerFaction].rule_flags & 0x80) return 0;
    TutorialHintsShown |= bit;
    return 1;
}
#endif
