// Headless implementations of the UI-layer functions used by the probe module (engine/ui_bridge.h).
// Native builds only: hybrid builds call the original UI.
#ifndef SMACX_HYBRID
#include "engine/game_state.h"
#include "engine/strings.h"
#include "engine/ui_bridge.h"

#include <cstring>

// The text effect (HisHerBuffer), as get_his_her: labels 199/200 by the leader's gender.
char* GAMEAPI get_him_her(int32_t faction_id, int32_t mode) {
    std::strcpy(HisHerBuffer, label_get(199 + (game().mfactions[faction_id].is_leader_female != 0)));
    if (mode == 1) {
        if (HisHerBuffer[0] >= 'a' && HisHerBuffer[0] <= 'z') HisHerBuffer[0] = (char)(HisHerBuffer[0] - 32);
    } else if (mode) {
        for (char* s = HisHerBuffer; *s; s++)
            if (*s >= 'a' && *s <= 'z') *s = (char)(*s - 32);
    }
    return HisHerBuffer;
}
#endif
