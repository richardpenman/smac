// Diplomacy (original module around 0x538F30-0x55F930): treaty and agenda bit helpers.
#include "engine/diplomacy.h"

#include <cstddef>

#include "engine/action.h"
#include "engine/base.h"
#include "engine/map.h"
#include "engine/parse.h"
#include "engine/probe.h"
#include "engine/tech.h"
#include "engine/turn.h"
#include "engine/ui_bridge.h"
#include "engine/veh.h"

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

// ---- pact ends ------------------------------------------------------------------------------------

int32_t GAMEAPI pact_withdraw(int32_t faction_id, int32_t faction_id_with) {
    int32_t moved = 0;
    for (int32_t veh_id = 0; veh_id < VehCount; veh_id++) {
        VEH& veh = Vehs[veh_id];
        if (veh.faction_id != (uint32_t)faction_id) continue;
        int32_t near = base_find(veh.x, veh.y);
        if (near < 0) continue;
        if ((uint8_t)Bases[near].faction_id != (uint32_t)faction_id_with &&
            whose_territory(faction_id, veh.x, veh.y, nullptr, 0) != faction_id_with) {
            continue;
        }
        int32_t best = -1, best_dist = 9999;
        for (int32_t i = 0; i < BaseCount; i++) {
            const BASE& base = Bases[i];
            if ((uint8_t)base.faction_id != (uint32_t)faction_id) continue;
            if (veh_triad(veh_id) == TRIAD_SEA) {
                // BUG(original): is_coast is given the location of the base whose id is this
                // base's x coordinate, and this base's y coordinate as the radius flag.
                const BASE& odd = Bases[base.x];
                if (!is_coast(odd.x, odd.y, base.y)) continue;
            }
            if (veh_triad(veh_id) == TRIAD_SEA && map_is_ocean(veh.x, veh.y) &&
                !base_on_sea(i, map_tile(veh.x, veh.y)->region)) {
                continue;
            }
            int32_t dist = veh_vector_dist(base.x, base.y, veh.x, veh.y);
            if (dist < best_dist) {
                best_dist = dist;
                best = i;
            }
        }
        if (best < 0) continue;
        veh_drop(veh_lift(veh_id), Bases[best].x, Bases[best].y);
        veh.order = 0;
        veh.state &= 0xFCFFBDFF;
        if (veh_triad(veh_id) == TRIAD_AIR) veh.movement_turns = 0;
        moved++;
        if (MapWinInVehMode && veh_id == MapWinVehA) Console_set_move(&MainConsole, 1);
    }
    return moved;
}

void GAMEAPI pact_ends(int32_t faction_id, int32_t faction_id_with) {
    DiploSecondFaction = faction_id_with;
    treaty_off(faction_id, faction_id_with, DIPLO_PACT);
    int32_t withdrawn = pact_withdraw(faction_id, faction_id_with);
    int32_t withdrawn_with = pact_withdraw(faction_id_with, faction_id);
    draw_map(1);
    int32_t count;
    if (faction_id == CurrentPlayerFaction || faction_id_with == CurrentPlayerFaction) {
        int32_t other = faction_id == CurrentPlayerFaction ? faction_id_with : faction_id;
        GenderDefault = MFactions[other].noun_gender;
        PluralDefault = MFactions[other].is_noun_plural;
        parse_says(0, MFactions[other].noun_faction, -1, -1);
        parse_says(1, MFactions[other].adj_name_faction, -1, -1);
        count = faction_id == CurrentPlayerFaction ? withdrawn : withdrawn_with;
    } else {
        const Faction& viewer = Factions[CurrentPlayerFaction];
        if (spying(faction_id) || spying(faction_id_with) || (GameState & 0x80) ||
            (viewer.diplo_status[faction_id] & DIPLO_PACT) || (viewer.diplo_status[faction_id_with] & DIPLO_PACT)) {
            GenderDefault = MFactions[faction_id].noun_gender;
            PluralDefault = MFactions[faction_id].is_noun_plural;
            parse_says(0, MFactions[faction_id].noun_faction, -1, -1);
            GenderDefault = MFactions[faction_id_with].noun_gender;
            PluralDefault = MFactions[faction_id_with].is_noun_plural;
            parse_says(1, MFactions[faction_id_with].noun_faction, -1, -1);
            parse_says(2, get_pact_hood(faction_id, faction_id_with), -1, -1);
            X_pop("ENDPACT3", 0);
        }
        return;
    }
    parse_num(0, count);
    parse_says(2, get_pact_hood(faction_id, faction_id_with), -1, -1);
    ui_pact_ends_dialog(count);
}
