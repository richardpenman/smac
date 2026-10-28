// Headless implementations of the UI functions of the diplomacy module (engine/ui_bridge.h).
#include "engine/crt.h"
#include "engine/game_state.h"
#include "engine/parse.h"
#include "engine/strings.h"
#include "engine/ui_bridge.h"
#include "platform/win32.h"

// The text effect (HisHerBuffer), as get_his_her: labels 209/210 by the leader's gender.
char* GAMEAPI get_he_she(int32_t faction_id, int32_t mode) {
    crt_strcpy(HisHerBuffer, label_get((game().mfactions[faction_id].is_leader_female != 0) + 209));
    if (mode == 1) {
        char first[2] = {HisHerBuffer[0], 0};
        win32::CharUpperA(first);
        HisHerBuffer[0] = first[0];
    } else if (mode) {
        win32::CharUpperA(HisHerBuffer);
    }
    return HisHerBuffer;
}

// ---- first half of the module (diplomacy.cpp) ----
void THISCALL DiploPop_draw_info(uint8_t*) {}
void THISCALL GraphicWin_soft_update_3(uint8_t*, uint8_t*) {}
void THISCALL DiploPop_start(uint8_t*, int32_t) {}
void THISCALL DiploPop_stop(uint8_t*) {}
void THISCALL InfoWin_set_state(uint8_t*, int32_t, int32_t) {}

// The single player path without modifier keys: faction_id_with | 0x10 (not met / eradicated)
// shows the message and refuses; otherwise go ahead (no multiplayer locks or channels).
int32_t GAMEAPI commlink_attempter(int32_t, int32_t faction_id_with) {
    DiploSecondFaction = faction_id_with;
    if (!(faction_id_with & 0x10)) return 0;
    int32_t f = faction_id_with & ~0x10;
    const MFaction& m = game().mfactions[f];
    PluralDefault = 0;
    GenderDefault = m.is_leader_female;
    parse_says(0, m.title_leader, -1, -1);
    GenderDefault = m.is_leader_female;
    PluralDefault = 0;
    parse_says(1, m.name_leader, -1, -1);
    GenderDefault = m.noun_gender;
    PluralDefault = m.is_noun_plural;
    parse_says(2, m.noun_faction, -1, -1);
    NetMsg_pop(&NetMsg, (FactionStatus[1] & (1 << f)) ? "NOTMETYET" : "ERADICATED", 5000, 0, nullptr);
    return 1;
}

// The human's menus and the battle plans dialog: nothing chosen.
int32_t GAMEAPI diplomacy_menu(int32_t, int32_t) { return 0; }
void GAMEAPI make_a_proposal(int32_t, int32_t, int32_t) {}
int32_t GAMEAPI proposal_menu(int32_t, int32_t) { return 0; }
void GAMEAPI battle_plans(int32_t, int32_t) {}
void GAMEAPI commlink_attempt(int32_t) {}

// Only called with a human participant, which an observer game has not: the dialog is not held
// (no state changes), as if the commlink were closed at once.
int32_t GAMEAPI communicate(int32_t, int32_t, int32_t) { return 0; }
