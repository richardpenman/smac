// Headless implementations of the setup-module UI functions declared in engine/ui_bridge.h.
// Native builds only: hybrid builds always call the original UI. These functions change game
// data (diplomacy and council state, monuments, a text buffer); that part is reproduced exactly.
#ifndef SMACX_HYBRID
#include "engine/crt.h"
#include "engine/strings.h"
#include "engine/ui_bridge.h"
#include "engine/zoc.h"
#include "platform/win32.h"

void GAMEAPI clear_all_offers() {
    for (int32_t i = 0; i < 8; i++) {
        for (int32_t j = 0; j < 8; j++) {
            for (int32_t k = 0; k < 4; k++) {
                DiploStateC[i][j][k] = 0;
                DiploStateC[j][i][k] = 0;
            }
        }
    }
}

void GAMEAPI clear_hotseat_chat() {
    for (int32_t i = 0; i < 8; i++) {
        for (int32_t j = 0; j < 8; j++) ui_stringlist_clear(DiploMessageLists[i][j]);
    }
}

void GAMEAPI clear_council_notify_2() {
    for (int32_t i = 0; i < 8; i++) CouncilVoteState[i] = 0;
    for (int32_t i = 0; i < 8; i++) CouncilProposal[i] = 0;
}

void GAMEAPI mon_killed_faction(int32_t faction_id, int32_t killed_id) {
    int32_t slot = -1;
    int32_t year = game_year(CurrentTurn);
    Monument& m = Monuments[faction_id];
    for (int32_t i = 0; i < 6; i++) {
        if (m.events[15 + i].achieved == 0 && slot == -1) slot = 15 + i;
    }
    if (slot == -1) return;
    MonumentEvent& e = m.events[slot];
    e.year = year;
    e.param2 = killed_id;
    e.prev_count = 1;
    e.achieved = 1;
    e.faction_id = faction_id;
    m.latest_event_id = slot;
    if (!(MFactions[faction_id].rule_flags & 0x80) && faction_id == CurrentPlayerFaction) monument(1);
}

char* GAMEAPI get_his_her(int32_t faction_id, int32_t mode) {
    crt_strcpy(HisHerBuffer, label_get((MFactions[faction_id].is_leader_female != 0) + 207));
    if (mode == 1) {
        char first[2] = {HisHerBuffer[0], 0};
        win32::CharUpperA(first);
        HisHerBuffer[0] = first[0];
    } else if (mode) {
        win32::CharUpperA(HisHerBuffer);
    }
    return HisHerBuffer;
}
#endif

#ifndef SMACX_HYBRID
// ---- crash_landing: no animation; the first base keeps its name ----
void GAMEAPI flush_input() {}
void ui_crash_landing_drop(int32_t, int32_t, bool) {}
void ui_crash_landing_close() {}
void ui_first_base_name(const char* default_name) { crt_strcpy(ParseStrBuffer[0], default_name); }
#endif
