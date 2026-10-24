// New game setup (original module around 0x5AE120-0x5B4210).
#include "engine/setup.h"

void GAMEAPI clear_scenario() {
    ObjectiveReqVictory = 9999;
    ObjectivesSuddenDeathVictory = 9999;
    ObjectiveAchievePts = 0;
    VictoryAchieveBonusPts = 0;
    StartingMissionYear = Rules.normal_start_year;
    if (DiffLevel < 3) {
        EndingMissionYear = Rules.normal_end_year_low_three_diff;
        return;
    }
    EndingMissionYear = Rules.normal_end_year_high_three_diff;
}

void GAMEAPI compute_faction_modifiers(int32_t faction_id) {
    int32_t* modifiers = &Factions[faction_id].SE_economy_base;
    for (int32_t i = 0; i < 11; i++) modifiers[i] = 0;
    MFaction& m = MFactions[faction_id];
    for (int32_t i = 0; i < m.faction_bonus_count; i++)
        if (m.faction_bonus_id[i] == RULE_SOCIAL) modifiers[m.faction_bonus_val1[i]] += m.faction_bonus_val2[i];
}
