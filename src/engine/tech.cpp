#include "engine/tech.h"

#include "engine/base.h"
#include "engine/base_misc.h"
#include "engine/crt.h"
#include "engine/design.h"
#include "engine/diplo_act.h"
#include "engine/diplomacy.h"
#include "engine/log.h"
#include "engine/net.h"
#include "engine/parse.h"
#include "engine/map.h"
#include "engine/random.h"
#include "engine/rules.h"
#include "engine/social.h"
#include "engine/stack.h"
#include "engine/turn.h"
#include "engine/strings.h"
#include "engine/text.h"
#include "engine/ui_bridge.h"
#include "engine/zoc.h"
#include "platform/win32.h"

namespace {
inline bool is_human(int32_t faction_id) { return (FactionStatus[0] & (uint8_t)(1u << (faction_id & 31))) != 0; }
// Larger depth of the two prerequisites of a tech (the inlined tech_recurse pair).
int32_t preq_depth(int32_t tech_id) {
    int32_t a = tech_recurse(Tech[tech_id].preq_tech1, 1);
    int32_t b = tech_recurse(Tech[tech_id].preq_tech2, 1);
    return a > b ? a : b;
}
// tech_id is target_id or a prerequisite of it (the inlined pattern of tech_val).
bool leads_to(int32_t tech_id, int32_t target_id, int32_t depth) {
    if (tech_id < 0 || target_id < 0) return false;
    if (tech_id == target_id) return true;
    if (tech_is_preq(tech_id, Tech[target_id].preq_tech1, depth)) return true;
    return tech_is_preq(tech_id, Tech[target_id].preq_tech2, depth) != 0;
}
// Owner of a secret project (-1 if not built), as a faction id compared like the original (byte).
inline bool project_owned_by(int32_t project, uint32_t faction_id) {
    return SecretProjects[project] >= 0 && (uint8_t)Bases[SecretProjects[project]].faction_id == faction_id;
}
// Facility built at the base or among its first queue_count queue entries (inlined in
// facility_avail).
bool built_or_queued(int32_t base_id, int32_t facility_id, int32_t queue_count) {
    int32_t offset, mask;
    bitmask(facility_id, &offset, &mask);
    if (Bases[base_id].facilities_built[offset] & (uint8_t)mask) return true;
    for (int32_t i = 0; i < queue_count; i++)
        if (Bases[base_id].queue_items[i] == -facility_id) return true;
    return false;
}
}  // namespace

// ---- names ----------------------------------------------------------------------------------------

void GAMEAPI say_tech_2(char* buffer, int32_t tech_id, int32_t with_category) {
    if (tech_id < -1) {
        crt_strcat(buffer, label_get(310));
        return;
    }
    if (tech_id < 0) {
        crt_strcat(buffer, label_get(25));
        return;
    }
    if (tech_id == 9999) {
        crt_strcat(buffer, label_get(306));
        return;
    }
    if (tech_id < 89) {
        crt_strcat(buffer, Strings_get(&StringTable, Tech[tech_id].name));
        if (with_category) {
            crt_strcat(buffer, " ");
            crt_strcat(buffer, "(");
            crt_strcat(buffer, label_get(629 + tech_category(tech_id)));
            int32_t depth1 = tech_recurse(Tech[tech_id].preq_tech1, 1);
            int32_t depth = tech_recurse(Tech[tech_id].preq_tech2, 1);
            if (depth1 > depth) depth = depth1;
            char num[80];
            crt_itoa(depth, num, 10);
            crt_strcat(buffer, num);
            crt_strcat(buffer, ")");
        }
        return;
    }
    if (tech_id < 97) {
        int32_t faction_id = tech_id - 89;
        if (GameLanguage) {
            crt_strcat(buffer, label_get(487));
            crt_strcat(buffer, " ");
            crt_strcat(buffer, "(");
            GenderDefault = MFactions[faction_id].noun_gender;
            PluralDefault = MFactions[faction_id].is_noun_plural;
            crt_strcat(buffer, MFactions[faction_id].noun_faction);
            crt_strcat(buffer, ")");
            return;
        }
        crt_strcat(buffer, MFactions[faction_id].adj_name_faction);
        crt_strcat(buffer, " ");
        crt_strcat(buffer, label_get(487));
        return;
    }
    crt_strcat(buffer, Units[tech_id - 97].name);
    crt_strcat(buffer, " ");
    crt_strcat(buffer, label_get(185));
}

char* GAMEAPI tech_name_2(int32_t tech_id, int32_t with_category) {
    TechNameBuffer[0] = 0;
    say_tech_2(TechNameBuffer, tech_id, with_category);
    return TechNameBuffer;
}

int32_t GAMEAPI has_tech(int32_t tech_id, int32_t faction_id) {
    if (faction_id <= 0) return 0;
    if (tech_id == -1) return 1;
    if (tech_id < 0 || tech_id >= 88) return 0;  // BUG(original): the last tech (88) never counts
    if (Tech[tech_id].preq_tech1 < -1) return 0;
    if (Tech[tech_id].preq_tech2 < -1 && Tech[tech_id].preq_tech1 != -1) return 0;
    return (TechOwners[tech_id] & (uint8_t)(1u << (faction_id & 31))) != 0;
}

int32_t GAMEAPI tech_recurse(int32_t tech_id, int32_t level) {
    if (tech_id < 0 || tech_id >= 89) return level;
    int32_t a = tech_recurse(Tech[tech_id].preq_tech1, level + 1);
    int32_t b = tech_recurse(Tech[tech_id].preq_tech2, level + 1);
    return a > b ? a : b;
}

int32_t GAMEAPI tech_category(int32_t tech_id) {
    const CTech& t = Tech[tech_id];
    int32_t best = t.AI_growth, category = 0;
    if (t.AI_tech > best) {
        best = t.AI_tech;
        category = 1;
    }
    if (t.AI_wealth > best) {
        best = t.AI_wealth;
        category = 2;
    }
    if (t.AI_power > best) category = 3;
    return category;
}

int32_t GAMEAPI redundant(int32_t facility_id, int32_t faction_id) {
    int32_t project;
    switch (facility_id) {
    case 28: project = 10; break;
    case 4: project = 5; break;
    case 27: project = 1; break;
    case 30: project = 17; break;
    case 21: project = 28; break;
    case 29: project = 35; break;
    case 7: project = 36; break;
    default: return 0;
    }
    return project_owned_by(project, (uint32_t)faction_id);
}

// ---- availability ---------------------------------------------------------------------------------

int32_t GAMEAPI facility_avail(int32_t facility_id, int32_t faction_id, int32_t base_id, int32_t queue_count) {
    if (facility_id == 0) return 0;
    if (facility_id == 10) {
        if (DiffLevel < 2) return 0;
    } else {
        if (facility_id >= 70 && (GameRules & 0x80000000)) return 0;
        if (facility_id == 102) {
            if (SecretProjects[31] == -1) return 0;
            return (GameRules & 0x800) != 0;
        }
    }
    int32_t preq = Facility[facility_id].preq_tech;
    if (faction_id < 1) return 0;
    if (preq != -1 && !has_tech(preq, faction_id)) return 0;
    if (!ExpansionEnabled && facility_id >= 103 && facility_id <= 106) return 0;
    StrBuffer[0] = 0;
    crt_strcat(StrBuffer, MFactions[faction_id].filename);
    win32::CharUpperA(StrBuffer);
    if (!crt_strcmp(StrBuffer, "CARETAKE") && (facility_id == 101 || facility_id == 102)) return 0;
    if (facility_id >= 70) return SecretProjects[facility_id - 70] == -1;
    if (base_id < 0) return 1;
    if (facility_id < 65 && built_or_queued(base_id, facility_id, queue_count)) return 0;
    if (facility_id == 28) {
        if (project_owned_by(10, (uint32_t)faction_id)) return 0;
        if (!is_coast(Bases[base_id].x, Bases[base_id].y, 0)) return 0;
    } else {
        int32_t project = -1;
        switch (facility_id) {
        case 4: project = 5; break;
        case 27: project = 1; break;
        case 30: project = 17; break;
        case 21: project = 28; break;
        case 29: project = 35; break;
        case 7: project = 36; break;
        case 36:
        case 37:
        case 38:
            if (!ExpansionEnabled) return 0;
            if (!is_coast(Bases[base_id].x, Bases[base_id].y, 0)) return 0;
            break;
        }
        if (project >= 0 && has_project(project, faction_id)) return 0;
    }
    if (!ExpansionEnabled &&
        (facility_id == 41 || facility_id == 34 || facility_id == 35 || facility_id == 39 || facility_id == 40))
        return 0;
    if (!(MFactions[faction_id].rule_flags & 0x80) && facility_id == 40) return 0;
    switch (facility_id) {
    case 3:
        return !built_or_queued(base_id, 26, queue_count);
    case 5:
        if (built_or_queued(base_id, 4, queue_count)) return 1;
        return project_owned_by(5, (uint32_t)faction_id);
    case 11:
        if (!built_or_queued(base_id, 6, queue_count)) return 0;
        return !project_owned_by(6, (uint32_t)faction_id);
    case 14:
        return built_or_queued(base_id, 13, queue_count);
    case 16:
        return built_or_queued(base_id, 15, queue_count);
    case 18:
        return built_or_queued(base_id, 17, queue_count);
    case 10:
        return !(MFactions[faction_id].rule_flags & 0x200);
    case 23:
        return !built_or_queued(base_id, 12, queue_count);
    case 25:
        return built_or_queued(base_id, 24, queue_count);
    }
    if ((facility_id > 64 && facility_id < 69) || facility_id == 41) {
        if (!built_or_queued(base_id, 29, queue_count) && !has_project(35, faction_id) &&
            !has_project(27, faction_id))
            return 0;
    }
    if (facility_id == 20) {
        if (has_fac(19, base_id, queue_count)) return 1;
        return has_fac(22, base_id, queue_count) != 0;
    }
    if (facility_id == 21) return has_fac(19, base_id, queue_count) != 0;
    if (facility_id == 32) return has_fac(31, base_id, queue_count) != 0;
    return 1;
}

int32_t GAMEAPI veh_avail(int32_t unit_id, int32_t faction_id, int32_t base_id) {
    const UNIT& u = Units[unit_id];
    if (!(u.unit_flags & 1)) return 0;
    uint8_t bit = (uint8_t)(1u << (faction_id & 31));
    if (bit & u.obsolete_factions) return 0;
    if (unit_id < 64 && (faction_id <= 0 || !has_tech(u.preq_tech, faction_id))) return 0;
    if (u.plan == 8 && (GameRules & 0x400000)) return 0;
    if (base_id >= 0 && Chassis[u.chassis_id].triad == 1 && !is_coast(Bases[base_id].x, Bases[base_id].y, 0))
        return 0;
    if (!ExpansionEnabled) {
        if (u.armor_id > 9) return 0;
        uint8_t w = u.weapon_id;
        if (w == 12 || w == 13 || w == 14 || w == 24 || w == 25) return 0;
        if (u.ability_flags & 0x1F000000) return 0;
        if (unit_id >= 14 && unit_id <= 20) return 0;
    }
    if (unit_id < 64) return 1;
    return unit_id / 64 == faction_id;
}

int32_t GAMEAPI terrain_avail(int32_t terraform_id, int32_t is_sea, int32_t faction_id) {
    int32_t preq = is_sea ? Terraform[terraform_id].preq_tech_sea : Terraform[terraform_id].preq_tech;
    if (preq < -1) return 0;
    if ((terraform_id == 16 || terraform_id == 17) && (GameRules & 0x800000)) return 0;
    if (terraform_id > 11 && terraform_id < 19 && project_owned_by(2, (uint32_t)faction_id)) return 1;
    if (faction_id > 0) return has_tech(preq, faction_id);
    return 0;
}

int32_t GAMEAPI tech_avail(int32_t tech_id, int32_t faction_id) {
    if (faction_id > 0 && (tech_id == -1 || has_tech(tech_id, faction_id))) return 0;
    if (tech_id >= 89) return 0;
    if (!ExpansionEnabled && tech_id >= 78 && tech_id <= 86) return 0;
    int32_t preq1 = Tech[tech_id].preq_tech1;
    if (preq1 < -1) return 0;
    int32_t preq2 = Tech[tech_id].preq_tech2;
    if (preq2 < -1 || faction_id <= 0) return 0;
    if (!has_tech(preq1, faction_id)) return 0;
    return has_tech(preq2, faction_id);
}

void GAMEAPI tech_secret(int32_t tech_id, int32_t faction_id) {
    parse_say(0, Tech[tech_id].name, -1, -1);
    parse_says(1, MFactions[faction_id].adj_name_faction, -1, -1);
    StrBuffer[0] = 0;
    crt_strcat(StrBuffer, "THESECRET");
    char num[80];
    crt_itoa(faction_id != CurrentPlayerFaction, num, 10);
    crt_strcat(StrBuffer, num);
    X_pop(StrBuffer, 0);
}

void GAMEAPI tech_effects(int32_t faction_id) {
    Faction& f = Factions[faction_id];
    MFaction& m = MFactions[faction_id];
    f.tech_commerce_bonus = m.rule_commerce;
    f.tech_fungus_nutrient = 0;
    f.tech_fungus_mineral = 0;
    f.tech_fungus_energy = 0;
    f.tech_fungus_unused = 0;
    for (int32_t i = 0; i < 89; i++) {
        if (!has_tech(i, faction_id)) continue;
        int32_t flags = Tech[i].flags;
        if (flags & 0x40) f.tech_fungus_energy++;
        if (flags & 0x80) f.tech_fungus_mineral++;
        if (flags & 0x100) f.tech_fungus_nutrient++;
        if (flags & 4) f.tech_commerce_bonus++;
    }
    for (int32_t i = 0; i < m.faction_bonus_count; i++) {
        int32_t id = m.faction_bonus_id[i];
        if (id == 7) f.tech_fungus_nutrient += m.faction_bonus_val1[i];
        if (id == 8) f.tech_fungus_mineral += m.faction_bonus_val1[i];
        if (id == 9) f.tech_fungus_energy += m.faction_bonus_val1[i];
    }
    int32_t* fungus = &f.tech_fungus_nutrient;
    for (int32_t i = 0; i < 4; i++)
        if (fungus[i] < 1) fungus[i] = 0;
    if (f.SE_economy_pending > 2) {
        int32_t bonus = f.tech_commerce_bonus;
        f.tech_commerce_bonus = bonus + 1;
        if (f.SE_economy_pending > 3) f.tech_commerce_bonus = bonus + 2;
        if (f.SE_economy_pending > 4) f.tech_commerce_bonus++;
    }
}

// ---- discovery -------------------------------------------------------------------------------------

void GAMEAPI tech_achieved(int32_t faction_id, int32_t tech_id, int32_t from, int32_t is_shared) {
    // (The original constructs its announcement popup, a BasePop, on the stack here.)
    if (tech_id < 0) return;
    MFaction& m = MFactions[faction_id];
    log_say("Gaining a TECH", m.name_leader, faction_id, tech_id, from);
    uint8_t bit = (uint8_t)(1u << (faction_id & 31));
    if (FactionStatus[0] & bit) ambience(0xC9);
    if (tech_id >= 9999) {
        // maps of faction `from`
        uint32_t from_bit = 1u << (from & 31);
        Factions[faction_id].traded_maps |= Factions[from].traded_maps | from_bit;
        for (int32_t i = 0; i < MapAreaTiles; i++) {
            MAP* sq = &MapTiles[i];
            if ((uint8_t)from_bit & sq->visibility) {
                sq->visibility |= bit;
                if (faction_id && from)
                    sq->visible_items[faction_id - 1] |= sq->visible_items[from - 1] & sq->items;
            }
        }
        for (int32_t i = 0; i < BaseCount; i++)
            if ((Bases[i].visibility & (uint8_t)from_bit) || (uint8_t)Bases[i].faction_id == (uint32_t)from)
                spot_base(i, faction_id);
        if (faction_id == CurrentPlayerFaction) {
            draw_map(1);
            GameDrawState |= 4;
            GraphicWin_redraw(&WorldWindow);
        }
        return;
    }
    if (tech_id >= 89 && tech_id < 97) {  // commlink frequency
        treaty_on(faction_id, tech_id - 89, 8);
        return;
    }
    if (tech_id >= 97) {  // a unit design
        const UNIT& u = Units[tech_id - 97];
        int32_t unit_id = propose_proto(faction_id, u.chassis_id, u.weapon_id, u.armor_id, u.ability_flags,
                                        u.reactor_id, -2, u.name);
        if (unit_id >= 0) {
            if (MultiplayerActive && (bit & FactionStatus[0])) {
                Units[unit_id].unit_flags |= 0x100;
            } else {
                prune_protos(faction_id, unit_id, 0);
                upgrade_any_prototypes(faction_id);
            }
        }
        return;
    }
    Faction& f = Factions[faction_id];
    if (f.tech_research_id == tech_id) f.tech_research_id = -1;
    f.tech_trade_source[tech_id] = (int8_t)(from <= 0 ? 0 : from);
    bool first = !TechOwners[tech_id];
    TechOwners[tech_id] |= bit;
    f.tech_achieved++;
    if (!CurrentTurn || SkipTechScreenA) return;
    for (int32_t k = 0; k < 2; k++) {
        f.tech_ranking++;
        if (f.unk_27) f.unk_27--;
    }
    if (tech_id == 88) f.tech_count_transcendent++;
    // reactor upgrades of the existing designs
    int32_t upgraded = 0;
    for (int32_t reactor = 2; reactor <= 4; reactor++) {
        if (tech_id != Reactor[reactor - 1].preq_tech) continue;
        for (int32_t i = 0; i < 64; i++) {
            int32_t unit_id = faction_id * 64 + i;
            const UNIT& u = Units[unit_id];
            if (!(u.unit_flags & 1) || (bit & u.obsolete_factions) || u.reactor_id >= reactor) continue;
            // (The original scans the faction's designs here without using the result.)
            int32_t plan = (u.unit_flags & 4) ? -2 : -1;
            int32_t new_id = propose_proto(faction_id, u.chassis_id, u.weapon_id, u.armor_id, u.ability_flags,
                                           reactor, plan, nullptr);
            if (new_id != unit_id) {
                if (MultiplayerActive && (bit & FactionStatus[0])) {
                    Units[new_id].unit_flags |= 0x100;
                } else {
                    prune_protos(faction_id, new_id, 0);
                    upgrade_any_prototypes(faction_id);
                }
            }
            upgraded = reactor;
        }
    }
    if (upgraded && faction_id == CurrentPlayerFaction && !MultiplayerActive) {
        parse_say(0, Reactor[upgraded - 1].name, -1, -1);
        X_pop("REACTORUPGRADE", 0);
    }
    dword_68F0F0 = -1;
    if (MultiplayerActive) {
        f.player_flags |= 2;
    } else if (!(FactionStatus[0] & bit) || (GamePreferences & 0x4000)) {
        for (int32_t i = 0; i < 64; i++) {
            uint8_t* flags = reinterpret_cast<uint8_t*>(&Units[faction_id * 64 + i].unit_flags);
            *flags &= 0x3F;
        }
        consider_designs(faction_id);
        if (faction_id == CurrentPlayerFaction) {
            for (int32_t i = 0; i < 512; i++)
                if ((Units[i].unit_flags & 1) && tech_id == Units[i].preq_tech && dword_68F0F0 < 0) dword_68F0F0 = i;
        }
    }
    dword_7591A4 = -1;
    dword_7591A8 = -1;
    // announcement
    if (faction_id == CurrentPlayerFaction || (GameState & 0x80)) {
        StrBuffer[0] = 0;
        say_tech_2(StrBuffer, tech_id, 0);
        parse_says(0, StrBuffer, -1, -1);
        GenderDefault = m.noun_gender;
        PluralDefault = m.is_noun_plural;
        parse_says(1, m.noun_faction, -1, -1);
        if (from > 0) {
            GenderDefault = MFactions[from].noun_gender;
            PluralDefault = MFactions[from].is_noun_plural;
            parse_says(2, MFactions[from].noun_faction, -1, -1);
        } else if (from < 0) {
            if (from == -1)
                parse_say(2, Facility[81].name, -1, -1);
            else
                parse_says(2, label_get(215), -1, -1);
        }
        int32_t flags = 0;
        if (faction_id == CurrentPlayerFaction) {
            StrBuffer[0] = 0;
            crt_strcat(StrBuffer, "TECH");
            char num[80];
            crt_itoa(tech_id, num, 10);
            crt_strcat(StrBuffer, num);
            ui_tech_popup_open();
            flags = 0x80;
        }
        StrBuffer[0] = 0;
        crt_strcat(StrBuffer, faction_id == CurrentPlayerFaction ? "WE" : "THEY");
        crt_strcat(StrBuffer, from ? "ACQUIRE" : "DEVELOP");
        if (is_shared) {
            StrBuffer[0] = 0;
            crt_strcat(StrBuffer, "WEGETSHARETECH");
        }
        ui_tech_popup_show(tech_id, flags, faction_id, faction_id == CurrentPlayerFaction);
        FX_play(&Sounds, 0x33);
        if (faction_id == CurrentPlayerFaction) {
            if (MultiplayerActive && (DiploWinState || dword_7492CC || ControlWaitLoop)) {
                parse_say(0, Tech[tech_id].name, -1, -1);
                NetMsg_pop(&NetMsg, "TECHOBTAINED", 5000, 0, nullptr);
            } else {
                NewTechWin_exec(&NewTechWin, tech_id, from);
            }
        } else if (!MultiplayerActive) {
            ui_tech_popup_exec();
        }
    }
    // free facilities and abilities of the faction
    bool free_facility = false;
    for (int32_t i = 0; i < m.faction_bonus_count; i++) {
        if (m.faction_bonus_id[i] != 12 || tech_id != Facility[m.faction_bonus_val1[i]].preq_tech) continue;
        free_facility = true;
        for (int32_t b = 0; b < BaseCount; b++)
            if ((uint8_t)Bases[b].faction_id == (uint32_t)faction_id) set_fac(m.faction_bonus_val1[i], b, 1);
        if (faction_id == CurrentPlayerFaction) {
            parse_say(0, Tech[tech_id].name, -1, -1);
            parse_say(1, Facility[m.faction_bonus_val1[i]].name, -1, -1);
            NetMsg_pop(&NetMsg, "FREEFACTECH", -5000, 0, nullptr);
        }
    }
    // (NetDaemon_synch returns at once in single player games.)
    if (free_facility && MultiplayerActive) NetDaemon_synch(&NetState, 0x14, 0, 0, 0, 0, 1, 0x2101);
    for (int32_t i = 0; i < m.faction_bonus_count; i++) {
        if (m.faction_bonus_id[i] == 15 && tech_id == Ability[m.faction_bonus_val1[i]].preq_tech &&
            faction_id == CurrentPlayerFaction) {
            parse_say(0, Tech[tech_id].name, -1, -1);
            parse_say(1, Ability[m.faction_bonus_val1[i]].name, -1, -1);
            NetMsg_pop(&NetMsg, "FREEABILTECH", -5000, 0, nullptr);
        }
    }
    if (Tech[tech_id].flags & 1) mon_secrets_of_tech(faction_id);
    if (faction_id == CurrentPlayerFaction) {
        if (tech_id == TutorialTreeTech && !MultiplayerActive) {
            parse_say(0, Tech[tech_id].name, -1, -1);
            X_pop_2("TUTOR", "TREETIME", 0);
        }
        if ((GamePreferences & 0x4000) && dword_68F0F0 >= 0 && !MultiplayerActive) {
            // offer to look at the new designs
            int32_t ask = 0;
            dword_90EA3C = 0;
            dword_73396C = 1;
            for (int32_t i = 0; i < 512; i++) {
                if (!(Units[i].unit_flags & 1) || tech_id != Units[i].preq_tech ||
                    Weapon[Units[i].weapon_id].offense_value < 0 || i == 15)
                    continue;
                if (!ask) {
                    if (!X_pop("ASKSEEDESIGN", 0)) {
                        ask = -1;
                        continue;
                    }
                    ask = 1;
                } else if (ask < 0) {
                    continue;
                }
                design_new_veh(-faction_id, i);
            }
            for (int32_t i = 0; i < 64; i++) {
                int32_t unit_id = faction_id * 64 + i;
                uint8_t* flags = reinterpret_cast<uint8_t*>(&Units[unit_id].unit_flags);
                if (!(*flags & 0x80) || (bit & Units[unit_id].obsolete_factions)) continue;
                if (!ask) {
                    if (!X_pop("ASKSEEDESIGN", 0)) {
                        ask = -1;
                    } else {
                        ask = 1;
                        design_new_veh(-faction_id, unit_id);
                    }
                } else if (ask > 0) {
                    design_new_veh(-faction_id, unit_id);
                }
                *flags &= 0x7F;
            }
            DesignWin_shutdown(&DesignWin);
            for (int32_t i = 0; i < 64; i++) {
                int32_t unit_id = faction_id * 64 + i;
                if (!(bit & Units[unit_id].obsolete_factions) && (Units[unit_id].unit_flags & 0x100) &&
                    (!MultiplayerActive || !(FactionStatus[0] & bit)))
                    upgrade_prototypes(faction_id, unit_id);
            }
        }
        if (faction_id == CurrentPlayerFaction && f.diff_level > 1 && !MultiplayerActive) {
            // newly available social models
            for (int32_t category = 0; category < 4; category++) {
                for (int32_t model = 0; model < 4; model++) {
                    if (tech_id != SocialField[category].soc_preq_tech[model] ||
                        !society_avail(category, model, faction_id))
                        continue;
                    int32_t choice;
                    do {
                        parse_say(0, Tech[tech_id].name, -1, -1);
                        parse_say(1, SocialField[category].soc_name[model], -1, -1);
                        StrBuffer[0] = 0;
                        crt_strcat(StrBuffer, "SOCIETY");
                        choice = X_pop_2("TUTOR", StrBuffer, 0);
                        if (choice) social_select(faction_id);
                    } while (choice == 2);
                }
            }
        }
    }
    if ((Tech[tech_id].flags & 8) && !(f.player_flags & 0x200)) {
        // reveals the map
        for (int32_t i = 0; i < MapAreaTiles; i++) {
            MAP* sq = &MapTiles[i];
            sq->visibility |= bit;
            if (faction_id) sq->visible_items[faction_id - 1] = sq->items;
        }
        for (int32_t i = 0; i < BaseCount; i++) {
            Bases[i].visibility |= bit;
            Bases[i].factions_pop_size_intel[faction_id] = Bases[i].pop_size;
        }
        f.player_flags |= 0x200;
        if (CurrentTurn && faction_id == CurrentPlayerFaction) {
            parse_say(0, Tech[tech_id].name, -1, -1);
            popp(ScriptFileName, "SEEMAP2", 0, "native_sm.pcx", 0);
            draw_map(1);
            set_dirty();
            GraphicWin_redraw(&WorldWindow);
        }
    }
    tech_effects(faction_id);
    if (faction_id == CurrentPlayerFaction) {
        if (tech_id == Facility[9].preq_tech) interlude(1, 0, 1, 0);
        if (tech_id == 47) interlude(2, 0, 1, 0);
        if (SecretProjects[31] != -1 && faction_id > 0 && has_tech(34, faction_id) && has_tech(57, faction_id)) {
            interlude(15, 0, 1, 0);
            interlude(16, 0, 0, 0);
        }
        if (tech_id == 4 && (m.rule_flags & 0x80)) {
            for (int32_t i = 1; i < 8; i++)
                if (faction_id != i && !(MFactions[i].rule_flags & 0x80) && (f.diplo_status[i] & 8))
                    interlude(0x24, 0, 1, 0);
        }
    }
    if (faction_id <= 0) goto secret;
    if (SecretProjects[11] >= 0) {
        // the project shares techs known to three factions with its owner
        int32_t owner = (uint8_t)Bases[SecretProjects[11]].faction_id;
        if (owner > 0 && has_tech(tech_id, faction_id) && !has_tech(tech_id, owner)) {
            int32_t known = 0;
            for (int32_t i = 1; i < 8; i++)
                if (has_tech(tech_id, i)) known++;
            if (known >= 3) tech_achieved(owner, tech_id, -1, 0);
        }
    }
    if (has_tech(tech_id, faction_id)) {
        // factions sharing the techs of their neighbours
        for (int32_t i = 1; i < 8; i++) {
            int32_t needed = MFactions[i].rule_sharetech;
            if (needed <= 0 || has_tech(tech_id, i)) continue;
            int32_t count = 0;
            for (int32_t j = 1; j < 8; j++) {
                if (!has_tech(tech_id, j)) continue;
                uint32_t status = Factions[i].diplo_status[j];
                if (!ExpansionEnabled || !(MFactions[i].rule_flags & 0x20) || (status & 0x1000) || (status & 1) ||
                    project_owned_by(4, (uint32_t)i) ||
                    (i == GovernorFaction && !(MFactions[j].rule_flags & 0x80)))
                    count++;
            }
            if (count >= needed) tech_achieved(i, tech_id, -2, 1);
        }
    }
secret:
    if (CurrentTurn && first && (Tech[tech_id].flags & 1)) {
        f.player_flags |= 0x40;
        parse_say(0, Tech[tech_id].name, -1, -1);
        parse_says(1, m.adj_name_faction, -1, -1);
        StrBuffer[0] = 0;
        crt_strcat(StrBuffer, "THESECRET");
        char num[80];
        crt_itoa(faction_id != CurrentPlayerFaction, num, 10);
        crt_strcat(StrBuffer, num);
        X_pop(StrBuffer, 0);
        log_say("Learning a SECRET", m.name_leader, faction_id, tech_id, TechOwners[tech_id]);
    }
    desktop_update();
}

// ---- values ---------------------------------------------------------------------------------------

int32_t GAMEAPI tech_is_preq(int32_t preq_id, int32_t tech_id, int32_t depth) {
    if (preq_id < 0 || tech_id < 0) return 0;
    if (preq_id == tech_id) return 1;
    if (!depth) return 0;
    if (tech_is_preq(preq_id, Tech[tech_id].preq_tech1, depth - 1)) return 1;
    return tech_is_preq(preq_id, Tech[tech_id].preq_tech2, depth - 1) != 0;
}

int32_t GAMEAPI tech_val(int32_t tech_id, int32_t faction_id, int32_t is_simple) {
    if (tech_id == 9999) return 2;
    Faction& f = Factions[faction_id];
    int32_t vendettas = 0;
    for (int32_t i = 1; i < 8; i++)
        if (i != faction_id && (f.diplo_status[i] & 0x10)) vendettas++;
    int32_t weight = is_simple ? 1 : ((GameRules & 0x200) ? 4 : 2);
    if (tech_id >= 89 && tech_id < 97) {
        // a faction's knowledge (commlink): more valuable when relations are hostile
        int32_t other = tech_id - 89;
        int32_t value = 1;
        if (!wants_to_attack(faction_id, other, 0)) value = 2;
        if (!wants_to_attack(other, faction_id, 0)) value++;
        return value / (f.AI_fight + 2) * value;
    }
    if (tech_id >= 97) {
        const UNIT& u = Units[tech_id - 97];
        int32_t off = Weapon[u.weapon_id].offense_value;
        off = off < 1 ? 1 : (off > 2 ? 2 : off);
        int32_t def = Armor[u.armor_id].defense_value;
        def = def < 1 ? 1 : (def > 2 ? 2 : def);
        int32_t spd = Chassis[u.chassis_id].speed;
        spd = spd < 1 ? 1 : (spd > 2 ? 2 : spd);
        return u.reactor_id + spd + def - 2 + off;
    }
    const CTech& t = Tech[tech_id];
    int32_t value = (f.AI_power * weight + 1) * t.AI_power + (f.AI_tech * weight + 1) * t.AI_tech +
                    (f.AI_wealth * weight + 1) * t.AI_wealth + (f.AI_growth * weight + 1) * t.AI_growth;
    if ((!t.AI_power || (!f.AI_power && !vendettas)) && (!t.AI_tech || !f.AI_tech) &&
        (!t.AI_wealth || !f.AI_wealth) && (!t.AI_growth || (!f.AI_growth && f.base_count >= 4)))
        value = (value + 1) / 2;
    bool human = is_human(faction_id);
    if (!human && !has_tech(tech_id, faction_id)) {
        if (is_simple) {
            if (bit_count(TechOwners[tech_id]) > 1) value += 2 - bit_count(TechOwners[tech_id]) * 2;
            int32_t max_depth = 1;
            for (int32_t i = 0; i < 89; i++) {
                if (!has_tech(i, faction_id)) continue;
                if (preq_depth(i) >= max_depth) max_depth = preq_depth(i);
            }
            int32_t depth = tech_id < 0 ? 0 : preq_depth(tech_id);
            if (depth < max_depth) value = (depth + 1) * value / (max_depth + 1);
            if (value < 1) value = 1;
            return value;
        }
    } else if (is_simple) {
        return value;
    }
    int32_t base_count = f.base_count;
    if (base_count) {
        for (int32_t region = 1; region < 64; region++) {
            if ((region & 0x3F) == 0x3F || !(region & 0x3F)) continue;
            uint8_t plan = f.region_base_plan[region];
            int32_t bases = f.region_total_bases[region];
            int32_t human_div = (human ? 2 : 1) * base_count;
            if (plan == 7 && vendettas && !human) {
                value += bases * t.AI_power / base_count;
            } else if (plan == 2) {
                value += bases * t.AI_power * 4 / human_div;
            } else if (plan == 0) {
                int32_t mult = f.best_weapon_value >= f.enemy_best_weapon_value ? 2 : 4;
                value += mult * bases * t.AI_power / human_div;
            } else {
                for (int32_t i = 1; i < 8; i++) {
                    uint32_t status = f.diplo_status[i];
                    if (i != faction_id && Factions[i].region_total_bases[region] && f.region_total_bases[region] &&
                        (status & 8) && (!(status & 3) || (status & 0x20)))
                        value += f.region_total_bases[region] * t.AI_power / human_div;
                }
            }
        }
    }
    if (has_tech(tech_id, faction_id)) return value;
    if (climactic_battle() && leads_to(tech_id, Facility[102].preq_tech, 1)) value <<= 2;
    if (f.SE_planet_base > 0 && f.AI_growth && tech_id >= 0) {
        if (tech_id == 47 || tech_is_preq(tech_id, Tech[47].preq_tech1, 9998) ||
            tech_is_preq(tech_id, Tech[47].preq_tech2, 9998))
            value *= 3;
        if (tech_id == 61 || tech_is_preq(tech_id, Tech[61].preq_tech1, 9998) ||
            tech_is_preq(tech_id, Tech[61].preq_tech2, 9998))
            value *= 2;
        if (tech_id == 64 || tech_is_preq(tech_id, Tech[64].preq_tech1, 2) ||
            tech_is_preq(tech_id, Tech[64].preq_tech2, 2))
            value *= 2;
    }
    if (f.SE_probe_base < 1 && tech_id >= 0 && Facility[85].preq_tech >= 0) {
        int32_t target = Facility[85].preq_tech;
        int32_t depth = (f.AI_tech != 0) + 1;
        if (tech_id == target || tech_is_preq(tech_id, Tech[target].preq_tech1, depth) ||
            tech_is_preq(tech_id, Tech[target].preq_tech2, depth)) {
            if (!f.AI_power) value *= 2;
            if (f.AI_tech) value *= 2;
        }
    }
    if (f.AI_growth && tech_id >= 0 &&
        (tech_id == 35 || tech_is_preq(tech_id, Tech[35].preq_tech1, 1) ||
         tech_is_preq(tech_id, Tech[35].preq_tech2, 1)))
        value *= 2;
    if ((f.AI_wealth || !MapCloudCover) && tech_id >= 0 &&
        (tech_id == 59 || tech_is_preq(tech_id, Tech[59].preq_tech1, 9998) ||
         tech_is_preq(tech_id, Tech[59].preq_tech2, 9998)))
        value *= 2;
    if ((t.flags & 1) && !TechOwners[tech_id] && !(GameRules & 0x200)) value = (f.AI_power + 1) * value * 2;
    bool psi_doubled;
    if (MFactions[faction_id].rule_psi < 1) {
        int32_t reactor = Reactor[1].preq_tech;
        if (tech_id == reactor) value *= 2;
        if (tech_id == Reactor[2].preq_tech) value *= 2;
        if (leads_to(tech_id, reactor, 9998)) value++;
        psi_doubled = leads_to(tech_id, Reactor[1].preq_tech, 0) && !(GameRules & 0x200);
    } else {
        psi_doubled = leads_to(tech_id, Facility[89].preq_tech, 9998);
    }
    if (psi_doubled) value *= 2;
    int32_t bases = base_count < 1 ? 1 : (base_count > 9999 ? 9999 : base_count);
    int32_t per_base = f.unk_47 / bases;
    if (f.AI_power < per_base &&
        (leads_to(tech_id, Facility[14].preq_tech, 9998) || leads_to(tech_id, Facility[13].preq_tech, 9998) ||
         leads_to(tech_id, Facility[31].preq_tech, 9998) || leads_to(tech_id, Facility[32].preq_tech, 9998)))
        value += per_base;
    if (MFactions[faction_id].rule_population > 0) {
        if (leads_to(tech_id, Facility[24].preq_tech, 9998))
            value *= 2;
        else if (leads_to(tech_id, Facility[25].preq_tech, 9998) && CurrentTurn > 250)
            value = value * 3 / 2;
    }
    if (f.AI_power) {
        for (int32_t i = 0; i < 26; i++) {
            if (!Weapon[i].offense_value) continue;
            int32_t preq = Weapon[i].preq_tech;
            if (tech_id == preq)
                value *= human + 3;
            else if (leads_to(tech_id, preq, 0))
                value *= human + 2;
        }
    }
    if (f.AI_tech || !f.AI_power) {
        for (int32_t i = 0; i < 89; i++) {
            if (!has_tech(i, faction_id) && (Tech[i].flags & 1) && !TechOwners[i] && leads_to(tech_id, i, 0))
                value *= 3;
        }
    }
    int32_t unit_preq = Units[1].preq_tech;
    if (leads_to(tech_id, unit_preq, 9998) && !has_tech(Units[1].preq_tech, faction_id)) {
        value *= 2;
        if (human) value *= 2;
    }
    int32_t chassis_preq = Chassis[3].preq_tech;
    if (!leads_to(tech_id, chassis_preq, 9998) || has_tech(Chassis[3].preq_tech, faction_id)) return value;
    bool contested = false;
    for (int32_t region = 1; region < 64; region++) {
        if (!f.region_total_bases[region]) continue;
        if (!contested) {
            for (int32_t i = 1; i < 8; i++) {
                if (i != faction_id && !Factions[i].region_total_bases[region]) {
                    contested = true;
                    break;
                }
            }
            if (!contested) continue;
        }
        if (f.region_visible_tiles[region] >= Continents[region].tile_count) {
            value *= 3;
            if (human) value *= 2;
            break;
        }
    }
    if (contested) value = value * 2 + 4;
    return value;
}

int32_t GAMEAPI tech_ai(int32_t faction_id) {
    int32_t best_value = -999, best = -1;
    bool human = is_human(faction_id);
    for (int32_t i = 0; i < 89; i++) {
        if (!tech_avail(i, faction_id)) continue;
        int32_t value = tech_val(i, faction_id, 0);
        if (GameRules & 0x200) {  // blind research: favour the shallow techs
            if (human && (Factions[faction_id].AI_growth || Factions[faction_id].AI_wealth) &&
                i == Units[1].preq_tech)
                return i;
            value = (value << 8) / preq_depth(i);
        }
        int32_t roll;
        if (!human)
            roll = value < 1 ? 0 : game_rand() % (value + 1);
        else
            roll = game_random(0, value + 1);
        if (roll > best_value) {
            best = i;
            best_value = roll;
        }
    }
    return best;
}

int32_t GAMEAPI tech_mil(int32_t tech_id) { return tech_id < 89 ? Tech[tech_id].AI_power : 0; }
int32_t GAMEAPI tech_tech(int32_t tech_id) { return tech_id < 89 ? Tech[tech_id].AI_tech : 1; }
int32_t GAMEAPI tech_infra(int32_t tech_id) { return tech_id < 89 ? Tech[tech_id].AI_wealth : 1; }
int32_t GAMEAPI tech_colonize(int32_t tech_id) { return tech_id < 89 ? Tech[tech_id].AI_growth : 1; }

int32_t GAMEAPI wants_prototype(int32_t unit_id, int32_t faction_id) {
    const UNIT& u = Units[unit_id];
    if (!(u.unit_flags & 1) || !(u.unit_flags & 4)) return 0;
    for (int32_t i = 0; i < 64; i++) {
        int32_t other_id = faction_id * 64 + i;
        const UNIT& o = Units[other_id];
        if (!(o.unit_flags & 1) || !(o.unit_flags & 4) || u.plan != o.plan ||
            Chassis[u.chassis_id].triad != Chassis[o.chassis_id].triad)
            continue;
        // the existing prototype's weapon is at least as good
        if (Weapon[o.weapon_id].mode < 3) {
            int8_t off = Weapon[o.weapon_id].offense_value;
            if (off < 0) {
                if (off != Weapon[u.weapon_id].offense_value) continue;
            } else if (Weapon[u.weapon_id].offense_value > off) {
                continue;
            }
        } else if (Weapon[o.weapon_id].mode != Weapon[u.weapon_id].mode) {
            continue;
        }
        // its armor too
        if (Armor[o.armor_id].defense_value < 1) {
            if (o.armor_id != u.armor_id) continue;
        } else if (armor_val(other_id, faction_id) < armor_val(unit_id, faction_id)) {
            continue;
        }
        // and its speed
        if (Chassis[u.chassis_id].speed <= Chassis[o.chassis_id].speed) return 0;
    }
    return 1;
}

// ---- research -------------------------------------------------------------------------------------

int32_t GAMEAPI tech_selection(int32_t faction_id) {
    Faction& f = Factions[faction_id];
    if (MultiplayerActive && is_human(faction_id) && faction_id != CurrentPlayerFaction) {
        bool shown = false;
        while (!ControlTurnA && f.tech_research_id < 0) {
            NetDaemon_net_tasks();
            if (!shown) {
                shown = true;
                PluralDefault = 0;
                GenderDefault = MFactions[faction_id].is_leader_female;
                parse_says(0, MFactions[faction_id].title_leader, -1, -1);
                GenderDefault = MFactions[faction_id].is_leader_female;
                PluralDefault = 0;
                parse_says(1, MFactions[faction_id].name_leader, -1, -1);
                NetMsg_pop(&NetMsg, "PICKINGTECH", 0, 0, nullptr);
            }
        }
        NetMsg_close(&NetMsg);
        return f.tech_research_id;
    }
    int32_t tech_id = tech_pick(faction_id, 0, -1, nullptr);
    f.tech_research_id = tech_id;
    if (MultiplayerActive && is_human(faction_id)) {
        synch_researching(faction_id);
        f.tech_research_id = -1;
        while (!ControlTurnA && f.tech_research_id < 0) NetDaemon_net_tasks();
    }
    return tech_id;
}

int32_t GAMEAPI tech_advance(int32_t faction_id) {
    Faction& f = Factions[faction_id];
    log_say("Earning a Tech", MFactions[faction_id].name_leader, f.tech_research_id, 0, 0);
    if (MultiplayerActive && NetUpkeepState == 1) {
        log_say("Saving Earned Tech for Later", MFactions[faction_id].name_leader, faction_id, 0, 0);
    } else if (!PbemActive || !is_human(faction_id) || faction_id == CurrentPlayerFaction) {
        int32_t tech_id = f.tech_research_id;
        if (tech_id < 0 && (tech_id = tech_selection(faction_id)) < 0) return tech_id;
        f.tech_research_id = -1;
        tech_achieved(faction_id, tech_id, 0, 0);
        int32_t base_id = CurrentBaseID;
        if (!is_human(faction_id)) {
            bases_reset(-1, faction_id, 0);
            if (CurrentTurn && base_id >= 0 && base_id < BaseCount) {
                set_base(CurrentBaseID);
                base_compute(0);
            }
        }
        if ((MultiplayerActive || faction_id == CurrentPlayerFaction) && !SkipTechScreenA && !SkipTechScreenB &&
            f.tech_research_id < 0)
            f.tech_research_id = tech_selection(faction_id);
        return tech_id;
    }
    f.earned_techs_saved++;
    return -1;
}

int32_t GAMEAPI tech_rate(int32_t faction_id) {
    Faction& f = Factions[faction_id];
    if (f.tech_cost >= 0) return f.tech_cost;
    if (!Rules.tech_discovery_rate) return 999999999;
    int32_t level = f.earned_techs_saved * 2 - f.unk_27 + f.tech_ranking;
    level = level < 2 ? 2 : (level > 9999 ? 9999 : level);
    int32_t leader = 0;
    for (int32_t i = 1; i < 8; i++) {
        int32_t v = Factions[i].tech_ranking + Factions[i].earned_techs_saved * 2;
        if (v >= leader) leader = v;
    }
    int32_t techs = level / 2;
    int32_t leader_techs = leader / 2;
    bool human = (FactionStatus[0] & (1u << (faction_id & 31))) != 0;
    int32_t diff = human ? f.diff_level : DiffLevel;
    diff += diff < 3;
    int32_t diff_factor = human ? 3 : DiffLevel;
    int32_t cost = human ? diff * 4 + 8 : 29 - diff * 3;
    int32_t lo = 12 - techs, hi = techs + 12;
    if (cost < lo || hi < lo)
        cost = lo;
    else if (cost > hi)
        cost = hi;
    uint32_t scale = (uint32_t)(GameRules & 0x20) | 0x40;
    int32_t turn_techs = CurrentTurn / (int32_t)(scale >> 3);
    int32_t lag = (int32_t)((scale >> 5) * (uint32_t)cost) >> 1;
    int32_t behind = techs - turn_techs;
    if (behind < 0 || lag < 0)
        lag = 0;
    else if (behind <= lag)
        lag = behind;
    cost += lag;
    int32_t cap = cost * diff_factor / 10 + 1;
    int32_t catch_up = (leader_techs - diff_factor - techs + 7) / (8 - diff_factor);
    if (catch_up < 0 || cap < 0)
        catch_up = 0;
    else if (catch_up > cap)
        catch_up = cap;
    cost -= catch_up;
    int32_t research = f.SE_research_base > 0 ? 1 : (f.SE_research_base >= 0) - 1;
    techs -= research;
    int32_t n = techs < 1 ? 1 : (techs > 99999 ? 99999 : techs);
    int32_t result = n * cost;
    if (Rules.tech_discovery_rate != 100) result = result * 100 / Rules.tech_discovery_rate;
    if (MFactions[faction_id].rule_techcost != 100) result = MFactions[faction_id].rule_techcost * result / 100;
    result = MapAreaSqRoot * result / 56;
    if (GameRules & 0x20) result += result / 2;
    if (result < 1) return 1;
    if (result > 99999999) return 99999999;
    return result;
}

void GAMEAPI tech_research(int32_t faction_id, int32_t labs) {
    if (labs < 1) return;
    if (!is_human(faction_id) && climactic_battle()) {
        if (!(faction_id > 0 && has_tech(Facility[33].preq_tech, faction_id) &&
              has_tech(Facility[102].preq_tech, faction_id)))
            labs *= 2;
    }
    Faction& f = Factions[faction_id];
    f.tech_accumulated += labs;
    if (MultiplayerActive || f.tech_research_id >= 0) {
        int32_t cost = tech_rate(faction_id);
        if (f.tech_research_id > 88) f.tech_accumulated += labs;
        if (cost <= f.tech_accumulated) {
            log_say("Researched Advance", MFactions[faction_id].name_leader, faction_id, 0, 0);
            int32_t tech_id = tech_advance(faction_id);
            if (tech_id >= 0) mon_tech_discovered(faction_id, tech_id);
            f.tech_accumulated = 0;
            f.tech_cost = -1;
            f.tech_cost = tech_rate(faction_id);
        }
        if (f.player_flags & 0x40) {
            log_say("Secret Advance", MFactions[faction_id].name_leader, faction_id, 0, 0);
            f.player_flags &= ~0x40u;
            int32_t tech_id = tech_advance(faction_id);
            if (tech_id >= 0) mon_tech_discovered(faction_id, tech_id);
        }
    }
    if (f.tech_accumulated >= 0 && f.tech_research_id < 0 && NetUpkeepState != 1 &&
        (!is_human(faction_id) || faction_id == CurrentPlayerFaction || !PbemActive))
        f.tech_research_id = tech_selection(faction_id);
}

int32_t GAMEAPI X_text_open(const char* filename, const char* section) {
    if (crt_strcmp(filename, "SCRIPT")) return text_open(filename, section);
    if ((MFactions[CurrentPlayerFaction].rule_flags & 0x80) && ExpansionEnabled &&
        !text_open("alienIscript", section))
        return 0;
    return text_open(filename, section);
}

void GAMEAPI mon_tech_discovered(int32_t faction_id, int32_t tech_id) {
    int32_t year = game_year(CurrentTurn);
    MonumentEvent& e = Monuments[faction_id].events[MON_TECH_DISCOVERED];
    if ((MFactions[faction_id].rule_flags & 0x80) || e.achieved) return;
    e.prev_count = 0;
    for (int32_t i = 0; i < 8; i++)
        if (i != faction_id && Monuments[i].events[MON_TECH_DISCOVERED].achieved) e.prev_count++;
    int32_t who = faction_id;
    if (!e.prev_count) {
        e.prev_count = 1;
    } else {
        e.prev_count = 0;
        for (int32_t i = 0; i < 8; i++) {
            const MonumentEvent& o = Monuments[i].events[MON_TECH_DISCOVERED];
            if (i != faction_id && o.achieved && o.prev_count) who = i;
        }
    }
    e.achieved = 1;
    e.year = year;
    e.faction_id = who;
    e.param2 = tech_id;
    Monuments[faction_id].latest_event_id = MON_TECH_DISCOVERED;
    if (faction_id == CurrentPlayerFaction) monument(1);
}

void GAMEAPI mon_secrets_of_tech(int32_t faction_id) {
    int32_t year = game_year(CurrentTurn);
    MonumentEvent& e = Monuments[faction_id].events[MON_SECRETS_OF_TECH];
    if (MFactions[faction_id].rule_flags & 0x80) return;
    if (!e.achieved) {
        e.prev_count = 0;
        for (int32_t i = 0; i < 8; i++)
            if (i != faction_id && Monuments[i].events[MON_SECRETS_OF_TECH].achieved) e.prev_count++;
        int32_t who = faction_id;
        if (!e.prev_count) {
            e.prev_count = 1;
        } else {
            e.prev_count = 0;
            for (int32_t i = 0; i < 8; i++) {
                const MonumentEvent& o = Monuments[i].events[MON_SECRETS_OF_TECH];
                if (i != faction_id && o.achieved && o.prev_count) who = i;
            }
        }
        e.faction_id = who;
        e.year = year;
        e.achieved = 1;
        e.param2 = -1;
        e.param1 = -1;
    } else if (e.param2 == -1) {
        e.param2 = year;
    } else if (e.param1 == -1) {
        e.param1 = year;
    }
    Monuments[faction_id].latest_event_id = MON_SECRETS_OF_TECH;
    if (faction_id == CurrentPlayerFaction) monument(1);
}
