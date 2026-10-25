// Headless implementations of the UI-layer functions used by the tech module (engine/ui_bridge.h).
// Native builds only: hybrid builds call the original UI.
#ifndef SMACX_HYBRID
#include "engine/tech.h"
#include "engine/ui_bridge.h"

// The research dialog (FUN_00486510) as it behaves for AI factions, which never see it:
// - a regular pick (is_free 0, no source faction) on turn 0, for an AI faction or under blind
//   research is tech_ai's random choice;
// - otherwise the dialog lists the candidate techs by category (explore, discover, build,
//   conquer): with a source faction, those it knows and we do not whose prerequisites we partly
//   know; else the available ones (tech_avail), of which a regular pick skips every third (by
//   tech id, tech_ranking and faction) once one is listed, except among the first seven techs
//   while tech_ranking is 0. An AI faction takes the
//   listed tech with the highest tech_val (the last of equal ones); -1 if none is listed.
// Human factions would get the dialog; headless they choose like an AI. The dialog's own
// random draws use the UI generator (they only order the display).
int32_t GAMEAPI tech_pick(int32_t faction_id, int32_t is_free, int32_t from_faction_id, const char*) {
    bool blind = (GameRules & 0x200) && from_faction_id <= 0;
    bool human = (FactionStatus[0] & (1u << (faction_id & 31))) != 0;
    if (!is_free && (!CurrentTurn || !human || blind) && from_faction_id < 0) return tech_ai(faction_id);
    int32_t listed = 0, best = -1, best_value = -1;
    int32_t ranking = Factions[faction_id].tech_ranking;
    for (int32_t category = 0; category < 4; category++) {
        for (int32_t t = 0; t < 89; t++) {
            if (from_faction_id >= 0) {
                if (has_tech(t, faction_id) || !has_tech(t, from_faction_id)) continue;
                if (!has_tech(Tech[t].preq_tech1, faction_id) && !has_tech(Tech[t].preq_tech2, faction_id)) continue;
            } else {
                if (!tech_avail(t, faction_id)) continue;
                if (listed && !is_free && (t - ranking + faction_id) % 3 == 0 && (t >= 7 || ranking)) continue;
            }
            if (tech_category(t) != category) continue;
            listed++;
            int32_t value = tech_val(t, faction_id, 0);
            if (value >= best_value) {
                best_value = value;
                best = t;
            }
        }
    }
    return listed ? best : -1;
}

void GAMEAPI monument(int32_t) {}
#endif
#ifndef SMACX_HYBRID
// No sound, no windows.
int32_t GAMEAPI ambience(int32_t) { return 0; }
void GAMEAPI desktop_update() {}
void THISCALL DesignWin_shutdown(uint8_t*) {}
int32_t THISCALL NewTechWin_exec(uint8_t*, int32_t, int32_t) { return 0; }
#endif
