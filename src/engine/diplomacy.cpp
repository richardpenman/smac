// Diplomacy (original module around 0x538F30-0x55F930): treaty and agenda bit helpers.
#include "engine/diplomacy.h"

#include <cstddef>

#include "engine/tech.h"
#include "engine/turn.h"
#include "engine/ui_bridge.h"

void GAMEAPI treaty_off(int32_t faction_id, int32_t faction_id_with, uint32_t status) {
    if (status & 7) status |= 0x1100000;
    if (status & 0x10) status |= 0x8040;
    if (status & 0x20) {
        Factions[faction_id].diplo_agenda[faction_id_with] &= 0xFFFFF3FF;
        Factions[faction_id_with].diplo_agenda[faction_id] &= 0xFFFFF3FF;
    }
    Factions[faction_id].diplo_status[faction_id_with] &= ~status;
    Factions[faction_id_with].diplo_status[faction_id] &= ~status;
}

void GAMEAPI agenda_on(int32_t faction_id, int32_t faction_id_with, uint32_t agenda) {
    if (agenda & 0x20) {
        Factions[faction_id].diplo_agenda[faction_id_with] &= 0xFFFFFFBF;
        Factions[faction_id_with].diplo_agenda[faction_id] &= 0xFFFFFFBF;
    }
    Factions[faction_id].diplo_agenda[faction_id_with] |= agenda;
    Factions[faction_id_with].diplo_agenda[faction_id] |= agenda;
}

void GAMEAPI set_treaty(int32_t faction_id, int32_t faction_id_with, uint32_t status, int32_t add) {
    if (!add) {
        Factions[faction_id].diplo_status[faction_id_with] &= ~status;
        return;
    }
    Factions[faction_id].diplo_status[faction_id_with] |= status;
    if (status & 0x40) Factions[faction_id].diplo_merc[faction_id_with] = 50;
}

void GAMEAPI set_agenda(int32_t faction_id, int32_t faction_id_with, uint32_t agenda, int32_t add) {
    if (add)
        Factions[faction_id].diplo_agenda[faction_id_with] |= agenda;
    else
        Factions[faction_id].diplo_agenda[faction_id_with] &= ~agenda;
}

int32_t GAMEAPI great_satan(int32_t faction_id, int32_t is_aggressive) {
    if (!(FactionStatus[0] & (1 << faction_id)) || cs.FactionRankings[7] != faction_id) return 0;
    const Faction& f = Factions[faction_id];
    int32_t min_bases = (CurrentTurn + 25) / 50;
    if (min_bases < 4) min_bases = 4;
    if (f.base_count <= min_bases) return 0;
    int32_t delay;
    if (f.diff_level <= 1 && !(GameRules & RULES_INTENSE_RIVALRY) && !is_aggressive) return 0;
    if (GameRules & RULES_INTENSE_RIVALRY)
        delay = 0;
    else
        delay = (5 - f.diff_level) * 50;
    if (CurrentTurn <= delay + 100) return 0;
    if (climactic_battle() && aah_ooga(faction_id, -1) == faction_id) return 1;
    int32_t mine, other;
    if (GameRules & RULES_INTENSE_RIVALRY) {
        mine = 4;
        other = 5;
    } else if (f.diff_level < 3 && !(GameRules & RULES_VICTORY_CONQUEST) && ObjectiveReqVictory > 1000) {
        mine = 1;
        other = 2;
    } else {
        mine = 2;
        other = 3;
    }
    return FactionRankingsUnk[cs.FactionRankings[7]] * mine >= FactionRankingsUnk[cs.FactionRankings[6]] * other;
}

int32_t GAMEAPI aah_ooga(int32_t faction_id, int32_t faction_id_vs) {
    if (!(GameRules & RULES_VICTORY_DIPLOMATIC)) return 0;
    int32_t total = 0;
    for (int32_t i = 1; i < 8; i++) total += council_votes(i);
    // The faction that could be elected (half of the votes, the governor proposal's tech known
    // to faction_id or reachable); faction_id itself wins ties.
    int32_t result = 0;
    for (int32_t i = 1; i < 8; i++) {
        int32_t votes = council_votes(i);
        if (i == faction_id_vs) continue;
        // BUG(original): with faction_id_vs == -1 this reads the word before diplo_status
        // (last_base_turn).
        const uint32_t* status = reinterpret_cast<const uint32_t*>(&Factions[i]) + offsetof(Faction, diplo_status) / 4;
        if (faction_id_vs && (status[faction_id_vs] & DIPLO_PACT) &&
            (GameRules & RULES_VICTORY_COOPERATIVE))
            continue;
        int32_t tech_id = Proposal[1].preq_tech;
        if (!has_tech(tech_id, faction_id)) {
            if (tech_id < 0) continue;
            if (!has_tech(Tech[tech_id].preq_tech1, faction_id) && !has_tech(Tech[tech_id].preq_tech2, faction_id))
                continue;
        }
        if (votes * 2 >= total && (!result || i == faction_id)) result = i;
    }
    return result;
}

int32_t GAMEAPI climactic_battle() {
    for (int32_t i = 1; i < 8; i++) {
        if ((FactionStatus[0] & (1 << i)) && Factions[i].corner_market_turn > CurrentTurn) return 1;
    }
    if (aah_ooga(0, -1)) return 1;
    if (SecretProjects[FAC_VOICE_OF_PLANET - 70] != -1) {
        for (int32_t i = 1; i < 8; i++) {
            if (!(FactionStatus[0] & (1 << i))) continue;
            if (has_tech(Facility[FAC_PSI_GATE].preq_tech, i)) return 1;
            if (has_tech(Facility[FAC_VOICE_OF_PLANET].preq_tech, i)) return 1;
        }
    }
    return 0;
}

int32_t GAMEAPI reputation(int32_t faction_id, int32_t faction_id_with) {
    int32_t value = Factions[faction_id].integrity_blemishes - Factions[faction_id].diplo_gifts[faction_id_with];
    if (value < 0) return 0;
    if (value > 99) return 99;
    return value;
}

void GAMEAPI agenda_off(int32_t faction_id, int32_t faction_id_with, uint32_t agenda) {
    Factions[faction_id].diplo_agenda[faction_id_with] &= ~agenda;
    Factions[faction_id_with].diplo_agenda[faction_id] &= ~agenda;
}

void GAMEAPI treaty_on(int32_t faction_id, int32_t faction_id_with, uint32_t status) {
    uint32_t& mine = Factions[faction_id].diplo_status[faction_id_with];
    uint32_t& theirs = Factions[faction_id_with].diplo_status[faction_id];
    bool viewer = faction_id == CurrentPlayerFaction || faction_id_with == CurrentPlayerFaction;
    if (status & 7) {
        // A truce, treaty or pact ends the viewer's war music.
        if (viewer && (mine & DIPLO_VENDETTA)) dword_74DA9C &= ~1;
        mine &= 0xFFEE7FAF;
        theirs &= 0xFFEE7FAF;
    }
    if (status & 1) status |= 2;
    if (status & DIPLO_VENDETTA) {
        if (mine & 7) {
            Factions[faction_id_with].diplo_combat_count[faction_id] = 0;
            Factions[faction_id].diplo_combat_count[faction_id_with] = 0;
        }
        status |= 0x80000108;
        mine &= 0xFAEEF378;
        theirs &= 0xFAEEF378;
        if (viewer) {
            dword_74DA9C |= 1;
            ambience(0xCC);
        }
    }
    if (status & 0x17) {
        mine &= 0xFF7FFFFF;
        theirs &= 0xFF7FFFFF;
    }
    mine |= status;
    theirs |= status;
    if ((faction_id == CurrentPlayerFaction || faction_id_with == CurrentPlayerFaction) && (status & 0x1F) &&
        MultiWinVisible)
        GraphicWin_redraw(&MultiWin);
}
