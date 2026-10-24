// Computer player strategy and unit movement (original module around 0x55F930-0x579770).
#include "engine/ai.h"

#include "engine/design.h"
#include "engine/tech.h"

void GAMEAPI enemy_capabilities(int32_t faction_id) {
    Faction& f = Factions[faction_id];
    f.best_psi_land_offense = veh_avail(BSC_MIND_WORMS, faction_id, -1) ? weap_strat(WPN_PSI_ATTACK, faction_id) : 0;
    f.best_weapon_value = 1;
    for (int32_t i = 0; i < 26; i++) {
        if (!has_tech(Weapon[i].preq_tech, faction_id) || Weapon[i].offense_value >= 99) continue;
        int32_t value = weap_strat(i, faction_id);
        if (Weapon[i].offense_value < 0 && value >= f.best_psi_land_offense) f.best_psi_land_offense = value;
        if (value >= f.best_weapon_value) f.best_weapon_value = value;
    }
    f.best_psi_land_defense = veh_avail(BSC_MIND_WORMS, faction_id, -1) ? arm_strat(ARM_PSI_DEFENSE, faction_id) : 0;
    f.best_armor_value = 1;
    for (int32_t i = 0; i < 14; i++) {
        if (!has_tech(Armor[i].preq_tech, faction_id)) continue;
        int32_t value = arm_strat(i, faction_id);
        if (Armor[i].defense_value < 0 && value >= f.best_psi_land_defense) f.best_psi_land_defense = value;
        if (value >= f.best_armor_value) f.best_armor_value = value;
    }
    f.best_land_speed = 1;
    for (int32_t i = 0; i < 9; i++) {
        if (has_tech(Chassis[i].preq_tech, faction_id) && Chassis[i].triad == TRIAD_LAND &&
            (int32_t)Chassis[i].speed >= f.best_land_speed)
            f.best_land_speed = Chassis[i].speed;
    }
    f.enemy_best_land_speed = 0;
    f.enemy_best_armor_value = 0;
    f.enemy_best_weapon_value = 0;
    f.enemy_best_psi_land_defense = 0;
    f.enemy_best_psi_land_offense = 0;
    // Enemy values from the most hostile non-pact factions known: at war, then without a treaty,
    // then any with contact, finally any.
    for (int32_t pass = 0; pass < 4; pass++) {
        if (f.enemy_best_weapon_value != 0) return;
        for (int32_t i = 1; i < 8; i++) {
            if (i == faction_id) continue;
            uint32_t status = f.diplo_status[i];
            if (status & DIPLO_PACT) continue;
            if (pass == 0 && !(status & DIPLO_VENDETTA)) continue;
            if (pass <= 1 && (status & DIPLO_TREATY)) continue;
            if (pass <= 2 && !(status & DIPLO_COMMLINK)) continue;
            const Faction& e = Factions[i];
            if (e.best_weapon_value >= f.enemy_best_weapon_value) f.enemy_best_weapon_value = e.best_weapon_value;
            if (e.best_armor_value >= f.enemy_best_armor_value) f.enemy_best_armor_value = e.best_armor_value;
            if (e.best_land_speed >= f.enemy_best_land_speed) f.enemy_best_land_speed = e.best_land_speed;
            if (e.best_psi_land_offense >= f.enemy_best_psi_land_offense)
                f.enemy_best_psi_land_offense = e.best_psi_land_offense;
            if (e.best_psi_land_defense >= f.enemy_best_psi_land_defense)
                f.enemy_best_psi_land_defense = e.best_psi_land_defense;
        }
    }
}
