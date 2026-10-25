// Headless implementations of the UI functions of the diplomacy module (engine/ui_bridge.h).
// Native builds only: hybrid builds call the original UI.
#ifndef SMACX_HYBRID
#include "engine/crt.h"
#include "engine/strings.h"
#include "engine/ui_bridge.h"
#include "platform/win32.h"

// The text effect (HisHerBuffer), as get_his_her: labels 209/210 by the leader's gender.
char* GAMEAPI get_he_she(int32_t faction_id, int32_t mode) {
    crt_strcpy(HisHerBuffer, label_get((MFactions[faction_id].is_leader_female != 0) + 209));
    if (mode == 1) {
        char first[2] = {HisHerBuffer[0], 0};
        win32::CharUpperA(first);
        HisHerBuffer[0] = first[0];
    } else if (mode) {
        win32::CharUpperA(HisHerBuffer);
    }
    return HisHerBuffer;
}

// Only called with a human participant, which an observer game has not: the dialog is not held
// (no state changes), as if the commlink were closed at once.
int32_t GAMEAPI communicate(int32_t, int32_t, int32_t) { return 0; }
#endif
