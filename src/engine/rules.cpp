// alpha(x).txt rule parsing (see rules.h).
#include "engine/rules.h"

#include "engine/compat.h"
#include "engine/crt.h"
#include "engine/game_state.h"
#include "engine/parse.h"
#include "engine/prefs.h"
#include "engine/proto.h"
#include "engine/random.h"
#include "engine/strings.h"
#include "engine/text.h"
#include "engine/ui_bridge.h"
#include "engine/util.h"

// Removes trailing blanks in place (the name lookups below).
static void trim_trailing(char* name) {
    while (crt_strlen(name) && name[crt_strlen(name) - 1] == ' ') name[crt_strlen(name) - 1] = 0;
}

// Reports an unknown key: file, key and line in parse slots 0-2, then the error popup.
static void bad_key(char* name, const char* label) {
    parse_says(0, TextBuffer.filepath, -1, -1);
    parse_says(1, name, -1, -1);
    parse_says(2, TextBufferGetPtr, -1, -1);
    X_pop(label, 0);
}

int32_t GAMEAPI tech_name(char* name) {
    trim_trailing(name);
    if (!crt_stricmp(name, "None")) return -1;
    if (crt_stricmp(name, "Disable")) {
        for (int32_t i = 0; i < 89; i++)
            if (!crt_stricmp(name, Tech[i].short_name)) return i;
        bad_key(name, "BADTECHKEY");
    }
    return -2;
}

int32_t GAMEAPI chas_name(char* name) {
    trim_trailing(name);
    if (!crt_stricmp(name, "None")) return -1;
    if (!crt_stricmp(name, "Disable")) return -2;
    for (int32_t i = 0; i < 9; i++)
        if (!crt_stricmp(name, Strings_get(&StringTable, Chassis[i].offsv1_name))) return i;
    bad_key(name, "BADCHASKEY");
    return 0;
}

int32_t GAMEAPI weap_name(char* name) {
    trim_trailing(name);
    if (!crt_stricmp(name, "None")) return -1;
    if (!crt_stricmp(name, "Disable")) return -2;
    for (int32_t i = 0; i < 26; i++)
        if (!crt_stricmp(name, Strings_get(&StringTable, Weapon[i].name_short))) return i;
    bad_key(name, "BADWEAPKEY");
    return 0;
}

int32_t GAMEAPI arm_name(char* name) {
    trim_trailing(name);
    if (!crt_stricmp(name, "None")) return -1;
    if (!crt_stricmp(name, "Disable")) return -2;
    for (int32_t i = 0; i < 14; i++)
        if (!crt_stricmp(name, Strings_get(&StringTable, Armor[i].name_short))) return i;
    bad_key(name, "BADARMKEY");
    return 0;
}

int32_t GAMEAPI text_get_number(int32_t min, int32_t max) {
    text_get();
    int32_t value = text_item_number();
    if (value >= min && min <= max) return value > max ? max : value;
    return min;
}

int32_t GAMEAPI tech_item() {
    text_get();
    return tech_name(text_item());
}

// Next line's number clamped with range(value, min, max).
static int32_t rule_range(int32_t min, int32_t max) {
    text_get();
    return range(text_item_number(), min, max);
}

// Next item clamped to [min, max] (inline clamps of the original).
static int32_t clamp_item(int32_t min, int32_t max) {
    int32_t value = text_item_number();
    if (value < min) return min;
    return value > max ? max : value;
}

static int32_t clamp_line(int32_t min, int32_t max) {
    text_get();
    return clamp_item(min, max);
}

int32_t GAMEAPI read_basic_rules() {
    if (text_open(AlphaxFileName, "RULES")) return 1;
    CRules& r = Rules;
    r.move_rate_roads = rule_range(1, 100);
    r.nutrient_intake_req_citizen = rule_range(0, 100);
    r.artillery_dmg_numerator = rule_range(1, 1000);
    r.artillery_dmg_denominator = clamp_item(1, 1000);
    r.artillery_max_rng = rule_range(1, 8);
    r.max_airdrop_rng_wo_orbital_insert = rule_range(1, 500);
    r.nutrient_cost_multi = rule_range(1, 100);
    r.mineral_cost_multi = rule_range(1, 100);
    r.tech_discovery_rate = rule_range(0, 1000);
    r.limit_mineral_inc_for_mine_wo_road = rule_range(0, 100);
    r.nutrient_effect_mine_sq = rule_range(-1, 0);
    r.min_base_size_specialists = rule_range(0, 100);
    r.drones_induced_genejack_factory = rule_range(0, 100);
    r.pop_limit_wo_hab_complex = rule_range(1, 100);
    r.pop_limit_wo_hab_dome = rule_range(1, 100);
    r.extra_cost_prototype_land = rule_range(0, 500);
    r.extra_cost_prototype_sea = rule_range(0, 500);
    r.extra_cost_prototype_air = rule_range(0, 500);
    for (int32_t i = 0; i < 3; i++) {
        r.psi_combat_ratio_atk[i] = rule_range(1, 1000);
        r.psi_combat_ratio_def[i] = clamp_item(1, 1000);
    }
    r.starting_energy_reserve = rule_range(0, 1000);
    r.combat_bonus_intrinsic_base_def = rule_range(-100, 1000);
    r.combat_bonus_atk_road = rule_range(-100, 1000);
    r.combat_bonus_atk_higher_elevation = rule_range(-100, 1000);
    r.combat_penalty_atk_lower_elevation = rule_range(-100, 1000);
    r.combat_mobile_open_ground = rule_range(-100, 1000);
    r.combat_mobile_def_in_rough = rule_range(-100, 1000);
    r.combat_infantry_vs_base = rule_range(-100, 1000);
    r.combat_penalty_atk_airdrop = rule_range(-100, 1000);
    r.combat_bonus_fanatic = rule_range(-100, 1000);
    r.combat_land_vs_sea_artillery = rule_range(-100, 1000);
    r.combat_artillery_bonus_altitude = rule_range(-100, 1000);
    r.combat_bonus_trance_vs_psi = rule_range(-100, 1000);
    r.combat_bonus_empath_song_vs_psi = rule_range(-100, 1000);
    r.combat_penalty_air_supr_vs_ground = rule_range(-100, 1000);
    r.combat_bonus_air_supr_vs_air = rule_range(-100, 1000);
    r.combat_penalty_non_combat_vs_combat = rule_range(-100, 1000);
    r.combat_comm_jammer_vs_mobile = rule_range(-100, 1000);
    r.combat_bonus_vs_ship_port = rule_range(-100, 1000);
    r.combat_aaa_bonus_vs_air = rule_range(-100, 1000);
    r.combat_defend_sensor = rule_range(-100, 1000);
    r.combat_psi_bonus_per_planet = rule_range(-100, 1000);
    r.retool_penalty_prod_change = rule_range(0, 100);
    r.retool_strictness = rule_range(0, 2);
    r.retool_exemption = rule_range(0, 1000);
    r.min_turns_between_councils = rule_range(0, 1000);
    r.minerals_harvesting_forest = rule_range(0, 100);
    r.territory_max_dist_base = rule_range(0, 100);
    r.turns_corner_global_energy_market = rule_range(1, 100);
    r.tech_preq_improv_fungus = tech_item();
    r.tech_preq_ease_fungus_mov = tech_item();
    r.tech_preq_build_road_fungus = tech_item();
    r.tech_preq_allow_2_spec_abil = tech_item();
    r.tech_preq_allow_3_nutrients_sq = tech_item();
    r.tech_preq_allow_3_minerals_sq = tech_item();
    r.tech_preq_allow_3_energy_sq = tech_item();
    r.tech_preq_orb_insert_wo_space = tech_item();
    r.tech_preq_mining_platform_bonus = tech_item();
    r.tech_preq_economic_victory = tech_item();
    r.tgl_probe_steal_tech = clamp_line(-1, 1);
    r.tgl_humans_always_contact_net = clamp_line(-1, 1);
    r.tgl_humans_always_contact_pbem = clamp_line(-1, 1);
    r.max_dmg_percent_arty_base_bunker = clamp_line(10, 100);
    r.max_dmg_percent_arty_open = clamp_line(10, 100);
    r.max_dmg_percent_arty_sea = clamp_line(10, 100);
    r.freq_global_warming_numerator = clamp_line(0, 1000);
    r.freq_global_warming_denominator = clamp_item(1, 1000);
    r.normal_start_year = clamp_line(0, 999999);
    r.normal_end_year_low_three_diff = clamp_line(0, 999999);
    r.normal_end_year_high_three_diff = clamp_line(0, 999999);
    r.tgl_oblit_base_atrocity = clamp_line(-1, 1);
    r.base_size_subspace_gen = clamp_line(1, 999);
    r.subspace_gen_req = clamp_line(1, 999);
    if (text_open(AlphaxFileName, "WORLDBUILDER")) return 1;
    CWorldbuilder& w = WorldBuilder;
    w.land_base = clamp_line(50, 4000);
    w.land_mod = clamp_line(0, 2000);
    w.continent_base = clamp_line(5, 1000);
    w.continent_mod = clamp_line(5, 1000);
    w.hills_base = clamp_line(0, 100);
    w.hills_mod = clamp_line(0, 100);
    w.plateau_base = clamp_line(0, 500);
    w.plateau_mod = clamp_line(0, 500);
    w.rivers_base = clamp_line(0, 100);
    w.rivers_rain_mod = clamp_line(0, 100);
    w.solar_energy = clamp_line(1, 64);
    w.thermal_band = clamp_line(1, 64);
    w.thermal_deviance = clamp_line(1, 64);
    w.global_warming = clamp_line(1, 64);
    w.sea_level_rises = clamp_line(1, 100);
    w.cloudmass_peaks = clamp_line(0, 20);
    w.cloudmass_hills = clamp_line(0, 20);
    w.rainfall_coeff = clamp_line(0, 8);
    w.deep_water = clamp_line(-100, 100);
    w.shelf = clamp_line(-100, 100);
    w.plains = clamp_line(-100, 100);
    w.beach = clamp_line(-100, 100);
    w.hills = clamp_line(0, 100);
    w.peaks = clamp_line(-100, 100);
    w.fungus = clamp_line(0, 5);
    text_get();
    w.cont_size_ratio1 = text_item_number();
    w.cont_size_ratio2 = text_item_number();
    w.cont_size_ratio3 = text_item_number();
    w.cont_size_ratio4 = text_item_number();
    w.cont_size_ratio5 = text_item_number();
    w.islands = clamp_line(1, 500);
    return 0;
}

int32_t GAMEAPI read_tech() {
    if (text_open(AlphaxFileName, "TECHNOLOGY")) return 1;
    for (int32_t i = 0; i < 89; i++) {
        char* short_name = Tech[i].short_name;
        text_get();
        text_item();
        text_item();
        crt_strncpy(short_name, TextBufferItemPtr, 8);
        short_name[7] = 0;
        for (int32_t j = 0; j < i; j++) {
            if (!crt_strcmp(short_name, Tech[j].short_name)) {
                parse_num(0, i);
                parse_num(1, j);
                parse_says(0, short_name, -1, -1);
                parse_says(1, FilefindPaths.last_path, -1, -1);
                parse_says(2, TextBufferGetPtr, -1, -1);
                X_pop("DUPLICATETECH", 0);
            }
        }
    }
    if (text_open(AlphaxFileName, "TECHNOLOGY")) return 1;
    TechValidCount = 0;
    TechCommerceCount = 0;
    for (int32_t i = 0; i < 89; i++) {
        CTech& t = Tech[i];
        text_get();
        t.name = text_item_string();
        text_item();
        t.AI_power = text_item_number();
        t.AI_tech = text_item_number();
        t.AI_wealth = text_item_number();
        t.AI_growth = text_item_number();
        t.preq_tech1 = tech_name(text_item());
        t.preq_tech2 = tech_name(text_item());
        uint32_t flags = (uint32_t)text_item_binary();
        t.flags = (int32_t)flags;
        if (t.preq_tech1 >= -1 && t.preq_tech2 >= -1) {
            TechValidCount++;
            if (flags & 4) TechCommerceCount++;
        }
    }
    return 0;
}

void GAMEAPI clear_faction(MFaction* faction) {
    faction->rule_tech_selected = 0;
    faction->rule_morale = 0;
    faction->rule_research = 0;
    faction->rule_drone = 0;
    faction->rule_talent = 0;
    faction->rule_energy = 0;
    faction->rule_interest = 0;
    faction->rule_population = 0;
    faction->rule_hurry = 100;
    faction->rule_techcost = 100;
    faction->rule_flags = 0;
    faction->faction_bonus_count = 0;
    faction->rule_psi = 0;
    faction->rule_sharetech = 0;
    faction->rule_commerce = 0;
}

void GAMEAPI read_faction2(int32_t faction_id) {
    if (faction_id) {
        read_faction(&game().mfactions[faction_id], 0);
        load_faction_art(faction_id);
    }
}

// strncpy of a name field, always terminated.
static void copy_name(char* dst, const char* src, uint32_t size) {
    crt_strncpy(dst, src, size);
    dst[size - 1] = 0;
}

// Adds a faction bonus (at most 8) with the numeric value of `value`.
static void add_bonus_number(MFaction* f, int32_t id, const char* value) {
    if (f->faction_bonus_count >= 8) return;
    f->faction_bonus_id[f->faction_bonus_count] = id;
    f->faction_bonus_val1[f->faction_bonus_count] = crt_atoi(value);
    f->faction_bonus_count++;
}

// Faction bonus keys: BonusName indices (enum BN_* in types/enums.h).
static bool is_bonus(const char* key, int32_t id) { return !crt_stricmp(key, BonusName[id].key); }

void GAMEAPI read_faction(MFaction* faction, int32_t toggle) {
    MFaction* f = faction;
    f->rule_tech_selected = 0;
    f->rule_morale = 0;
    f->rule_research = 0;
    f->rule_drone = 0;
    f->rule_talent = 0;
    f->rule_energy = 0;
    f->rule_interest = 0;
    f->rule_population = 0;
    f->rule_hurry = 100;
    f->rule_techcost = 100;
    f->rule_flags = 0;
    f->faction_bonus_count = 0;
    f->rule_psi = 0;
    f->rule_sharetech = 0;
    f->rule_commerce = 0;
    if (text_open(f->filename, f->search_key) && text_open(f->filename, f->filename)) {
        parse_says(0, f->search_key, -1, -1);
        parse_says(1, f->filename, -1, -1);
        X_pop("PLAYERFILE", 0);
        return;
    }
    text_get();
    copy_name(f->formal_name_faction, text_item(), 40);
    copy_name(f->desc_name_faction, text_item(), 24);
    copy_name(f->noun_faction, text_item(), 24);
    text_item();
    f->noun_gender = 0;
    if (*TextBufferItemPtr == 'F' || *TextBufferItemPtr == 'f') f->noun_gender = 1;
    if (*TextBufferItemPtr == 'N' || *TextBufferItemPtr == 'n') f->noun_gender = 2;
    int32_t plural = text_item_number() - 1;
    f->is_noun_plural = plural < 0 ? 0 : plural > 1 ? 1 : plural;
    copy_name(f->name_leader, text_item(), 24);
    text_item();
    f->is_leader_female = (*TextBufferItemPtr == 'F' || *TextBufferItemPtr == 'f') ? 1 : 0;
    if (toggle == 2) return;
    f->AI_fight = text_item_number();
    f->AI_power = text_item_number();
    f->AI_tech = text_item_number();
    f->AI_wealth = text_item_number();
    f->AI_growth = text_item_number();

    // faction bonuses: "KEY, value" pairs
    char key[256];
    text_get();
    while (crt_strlen(text_item())) {
        crt_strcpy(key, TextBufferItemPtr);
        char* value = text_item();
        int32_t& n = f->faction_bonus_count;
        if (is_bonus(key, BN_TECH)) {
            if (crt_atoi(value)) {
                f->rule_tech_selected = crt_atoi(value);
            } else if (n < 8) {
                f->faction_bonus_id[n] = 0;
                f->faction_bonus_val1[n] = tech_name(value);
                n++;
            }
        } else if (is_bonus(key, BN_MORALE)) {
            if (*value == '+') value++;
            f->rule_morale = crt_atoi(value);
            if (!f->rule_morale) f->rule_flags |= 0x8000;
        } else if (is_bonus(key, BN_FACILITY)) {
            add_bonus_number(f, 2, value);
        } else if (is_bonus(key, BN_RESEARCH)) {
            f->rule_research = crt_atoi(value);
        } else if (is_bonus(key, BN_DRONE)) {
            f->rule_drone = crt_atoi(value);
        } else if (is_bonus(key, BN_TALENT)) {
            f->rule_talent = crt_atoi(value);
        } else if (is_bonus(key, BN_ENERGY)) {
            f->rule_energy = crt_atoi(value);
        } else if (is_bonus(key, BN_INTEREST)) {
            f->rule_flags |= 0x4000;
            f->rule_interest = crt_atoi(value);
        } else if (is_bonus(key, BN_COMMERCE)) {
            f->rule_commerce = crt_atoi(value);
        } else if (is_bonus(key, BN_POPULATION)) {
            f->rule_population = crt_atoi(value);
        } else if (is_bonus(key, BN_HURRY)) {
            int32_t v = crt_atoi(value);
            f->rule_hurry = v < 1 ? 1 : v > 1000 ? 1000 : v;
        } else if (is_bonus(key, BN_TECHCOST)) {
            f->rule_techcost = crt_atoi(value);
        } else if (is_bonus(key, BN_UNIT)) {
            add_bonus_number(f, 1, value);
        } else if (is_bonus(key, BN_PSI)) {
            f->rule_psi = crt_atoi(value);
        } else if (is_bonus(key, BN_SHARETECH)) {
            f->rule_sharetech = crt_atoi(value);
        } else if (is_bonus(key, BN_TERRAFORM)) {
            f->rule_flags |= 0x2000;
        } else if (is_bonus(key, BN_SOCIAL) || is_bonus(key, BN_ROBUST) || is_bonus(key, BN_IMMUNITY)) {
            int32_t amount = 0;
            for (;; value++) {
                if (*value == '+') amount++;
                else if (*value == '-') amount--;
                else break;
            }
            if (!amount) amount = 1;
            for (int32_t i = 0; i < 11; i++) {
                if (!crt_stricmp(value, SocialParam[i].set1)) {
                    if (n < 8) {
                        if (is_bonus(key, BN_ROBUST))
                            f->faction_bonus_id[n] = 10;
                        else
                            f->faction_bonus_id[n] = is_bonus(key, BN_SOCIAL) ? 3 : 4;
                        f->faction_bonus_val1[n] = i;
                        f->faction_bonus_val2[n] = amount;
                        n++;
                    }
                    break;
                }
            }
        } else if (is_bonus(key, BN_IMPUNITY) || is_bonus(key, BN_PENALTY)) {
            for (int32_t i = 0; i < 4; i++) {
                for (int32_t j = 0; j < 4; j++) {
                    if (!crt_stricmp(value, Strings_get(&StringTable, SocialField[i].soc_name[j])) && n < 8) {
                        f->faction_bonus_id[n] = is_bonus(key, BN_IMPUNITY) ? 5 : 6;
                        f->faction_bonus_val1[n] = i;
                        f->faction_bonus_val2[n] = j;
                        n++;
                    }
                }
            }
        } else if (is_bonus(key, BN_FUNGNUTRIENT)) {
            add_bonus_number(f, 7, value);
        } else if (is_bonus(key, BN_FUNGMINERALS)) {
            add_bonus_number(f, 8, value);
        } else if (is_bonus(key, BN_FUNGENERGY)) {
            add_bonus_number(f, 9, value);
        } else if (is_bonus(key, BN_COMMFREQ)) {
            f->rule_flags |= 0x1000;
        } else if (is_bonus(key, BN_MINDCONTROL)) {
            f->rule_flags |= 0x800;
        } else if (is_bonus(key, BN_FANATIC)) {
            f->rule_flags |= 0x400;
        } else if (is_bonus(key, BN_VOTES)) {
            add_bonus_number(f, 11, value);
        } else if (is_bonus(key, BN_FREEPROTO)) {
            f->rule_flags |= 0x200;
        } else if (is_bonus(key, BN_AQUATIC)) {
            f->rule_flags |= 0x100;
        } else if (is_bonus(key, BN_ALIEN)) {
            f->rule_flags |= 0x80;
        } else if (is_bonus(key, BN_FREEFAC)) {
            add_bonus_number(f, 12, value);
        } else if (is_bonus(key, BN_REVOLT)) {
            add_bonus_number(f, 13, value);
        } else if (is_bonus(key, BN_NODRONE)) {
            add_bonus_number(f, 14, value);
        } else if (is_bonus(key, BN_WORMPOLICE)) {
            f->rule_flags |= 0x40;
        } else if (is_bonus(key, BN_FREEABIL)) {
            add_bonus_number(f, 15, value);
        } else if (is_bonus(key, BN_PROBECOST)) {
            add_bonus_number(f, 16, value);
        } else if (is_bonus(key, BN_DEFENSE)) {
            add_bonus_number(f, 17, value);
        } else if (is_bonus(key, BN_OFFENSE)) {
            add_bonus_number(f, 18, value);
        } else if (is_bonus(key, BN_TECHSHARE)) {
            f->rule_flags |= 0x20;
            f->rule_sharetech = crt_atoi(value);
        } else if (is_bonus(key, BN_TECHSTEAL)) {
            f->rule_flags |= 0x10;
        }
    }

    // social priorities: category, model, effect (priority line, then opposition line)
    for (int32_t k = 0; k < 2; k++) {
        int32_t* category = (k == 0) ? &f->soc_priority_category : &f->soc_opposition_category;
        int32_t* model = (k == 0) ? &f->soc_priority_model : &f->soc_opposition_model;
        int32_t* effect = (k == 0) ? &f->soc_priority_effect : &f->soc_opposition_effect;
        *category = -1;
        *model = 0;
        *effect = -1;
        text_get();
        StrBuffer[0] = 0;
        crt_strcat(StrBuffer, text_item());
        for (int32_t i = 0; i < 4; i++) {
            const char* name = Strings_get(&StringTable, SocialField[i].field_name);
            int32_t cmp = GameLanguage == 0 ? crt_stricmp(StrBuffer, name) : crt_strnicmp(StrBuffer, name, 4);
            if (!cmp) {
                *category = i;
                break;
            }
        }
        StrBuffer[0] = 0;
        crt_strcat(StrBuffer, text_item());
        if (*category >= 0) {
            for (int32_t j = 0; j < 4; j++) {
                const char* name = Strings_get(&StringTable, SocialField[*category].soc_name[j]);
                int32_t cmp = GameLanguage == 0 ? crt_stricmp(StrBuffer, name) : crt_strnicmp(StrBuffer, name, 4);
                if (!cmp) *model = j;
            }
        }
        StrBuffer[0] = 0;
        crt_strcat(StrBuffer, text_item());
        for (int32_t i = 0; i < 11; i++) {
            if (!crt_stricmp(StrBuffer, SocialParam[i].set1)) {
                COMPAT_ORIGINAL(social_effect_not_stored);
                *effect = -1;  // BUG(original): the matching effect index is not stored
                break;
            }
        }
    }
    text_get();
    copy_name(f->adj_name_faction, text_item(), 128);
    text_get();
    copy_name(f->assistant_name, text_item(), 24);
    copy_name(f->scientist_name, text_item(), 24);
    copy_name(f->assistant_city, text_item(), 24);
    text_get();
    copy_name(f->title_leader, text_item(), 24);
    copy_name(f->adj_leader, text_item(), 128);
    copy_name(f->adj_insult_leader, text_item(), 128);
    copy_name(f->adj_faction, text_item(), 128);
    copy_name(f->adj_insult_faction, text_item(), 128);
    text_get();
    copy_name(f->insult_leader, text_item(), 24);
}

int32_t GAMEAPI read_factions() {
    if (text_open(AlphaxFileName, "BONUSNAMES")) return 1;
    text_get();
    for (int32_t i = 0, column = 0; i < 41; i++) {
        crt_strcpy(BonusName[i].key, text_item());
        if (++column > 7) {
            column = 0;
            text_get();
        }
    }
    if (text_open(AlphaxFileName, ExpansionEnabled ? "NEWFACTIONS" : "FACTIONS")) return 1;
    for (int32_t i = 1; i < 8; i++) {
        text_get();
        crt_strcpy(game().mfactions[i].filename, text_item());
        crt_strcpy(game().mfactions[i].search_key, text_item());
    }
    if (ExpansionEnabled) prefs_fac_load();
    int32_t choices = 14;  // 7 original + 7 expansion factions, then the custom ones
    if (!text_open(AlphaxFileName, "CUSTOMFACTIONS")) {
        text_get();
        while (*text_item()) {
            choices++;
            text_get();
        }
    }
    for (int32_t i = 1; i < 8; i++) {
        char* filename = game().mfactions[i].filename;
        if (crt_strcmp(filename, "JENN282")) continue;
        int32_t pick;
        do {
            pick = game_rand() % choices;
            int32_t list = pick / 7;
            if (list == 0) {
                if (text_open(AlphaxFileName, "FACTIONS")) return 1;
            } else if (list == 1) {
                if (text_open(AlphaxFileName, "NEWFACTIONS")) return 1;
            } else if (list == 2) {
                if (text_open(AlphaxFileName, "CUSTOMFACTIONS")) return 1;
            }
            pick %= 7;
            text_get();
            for (int32_t n = pick; n > 0; n--) text_get();
            crt_strcpy(filename, text_item());
            crt_strcpy(game().mfactions[i].search_key, filename);
            for (int32_t j = 1; j < 8; j++) {
                if (i != j && !crt_strcmp(filename, game().mfactions[j].filename)) {
                    pick = -1;
                    break;
                }
            }
            if (pick != -1) {
                read_faction(&game().mfactions[i], 0);
                load_faction_art(i);
            }
        } while (pick == -1);
    }
    for (int32_t i = 1; i < 8; i++) {
        read_faction(&game().mfactions[i], 0);
        load_faction_art(i);
    }
    return 0;
}

void GAMEAPI noun_item(int32_t* gender, int32_t* plural) {
    char* item = text_item();
    *gender = 0;
    *plural = 0;
    if (*item == 'M' || *item == 'm') *gender = 0;
    if (*item == 'F' || *item == 'f') *gender = 1;
    if (*item == 'N' || *item == 'n') *gender = 2;
    if (item[1] == '1') *plural = 0;
    if (item[1] == '2') *plural = 1;
}

int32_t GAMEAPI read_units() {
    if (text_open(AlphaxFileName, "UNITS")) return 1;
    text_get();
    int32_t count = text_item_number();
    if (count < 0)
        count = 0;
    else if (count > 64)
        count = 64;
    for (int32_t i = 0; i < count; i++) {
        UNIT& u = Units[i];
        text_get();
        crt_strcpy(u.name, text_item());
        int32_t chassis_id = chas_name(text_item());
        int32_t weapon_id = weap_name(text_item());
        int32_t armor_id = arm_name(text_item());
        int32_t plan = text_item_number();
        int32_t cost = text_item_number();
        int32_t carry = text_item_number();
        u.preq_tech = (int16_t)tech_name(text_item());
        int32_t icon = text_item_number();
        uint32_t abilities = (uint32_t)text_item_binary();
        int32_t reactor_id = i == 16 ? 1 : i == 17 ? 2 : i == 18 ? 3 : 1;
        make_proto(i, chassis_id, weapon_id, armor_id, abilities, reactor_id);
        if (plan >= 0) u.plan = (int8_t)plan;
        if (cost) u.cost = (uint8_t)cost;
        if (carry) u.carry_capacity = (uint8_t)carry;
        u.icon_offset = (int8_t)icon;
    }
    return 0;
}

int32_t GAMEAPI read_rules(int32_t all_rules) {
    char format[256];
    Strings_init(&StringTable, 0xC320);
    if (labels_init()) return 1;
    text_clear_index();
    text_make_index(ScriptFileName);
    text_make_index(AlphaxFileName);
    if (read_tech()) return 1;
    if (read_basic_rules()) return 1;

    if (text_open(AlphaxFileName, "TERRAIN")) return 1;
    for (int32_t i = 0; i < 20; i++) {
        CTerraform& t = Terraform[i];
        text_get();
        t.name = text_item_string();
        t.preq_tech = tech_name(text_item());
        t.name_sea = text_item_string();
        t.preq_tech_sea = tech_name(text_item());
        t.rate = text_item_number();
        crt_strcpy(format, text_item());
        // order names: the format with $STR0 = the land / sea improvement name
        parse_say(0, t.name, -1, -1);
        StrBuffer[0] = 0;
        parse_string(format, StrBuffer);
        Order[4 + i].order = Strings_put(&StringTable, StrBuffer);
        parse_say(0, t.name_sea, -1, -1);
        StrBuffer[0] = 0;
        parse_string(format, StrBuffer);
        Order[4 + i].order_sea = Strings_put(&StringTable, StrBuffer);
        Order[4 + i].letter = text_item_string();
        t.shortcuts = text_item_string();
    }

    if (text_open(AlphaxFileName, "RESOURCEINFO")) return 1;
    ResValue* res = &ResInfo.ocean_sq;
    for (int32_t i = 0; i < 9; i++) {
        text_get();
        text_item();
        res[i].nutrient = text_item_number();
        res[i].mineral = text_item_number();
        res[i].energy = text_item_number();
        res[i].unused = text_item_number();
    }

    if (text_open(AlphaxFileName, "TIMECONTROLS")) return 1;
    for (int32_t i = 0; i < 6; i++) {
        CTimeControl& t = TimeControl[i];
        text_get();
        t.name = text_item_string();
        t.turn = text_item_number();
        t.base = text_item_number();
        t.unit = text_item_number();
        t.event = text_item_number();
        t.extra = text_item_number();
        t.refresh = text_item_number();
        t.accumulated = text_item_number();
    }

    if (text_open(AlphaxFileName, "CHASSIS")) return 1;
    for (int32_t i = 0; i < 9; i++) {
        CChassis& c = Chassis[i];
        text_get();
        c.offsv1_name = text_item_string();
        noun_item(&c.offsv1_gender, &c.offsv1_is_plural);
        c.offsv2_name = text_item_string();
        noun_item(&c.offsv2_gender, &c.offsv2_is_plural);
        c.defsv1_name = text_item_string();
        noun_item(&c.defsv1_gender, &c.defsv1_is_plural);
        c.defsv2_name = text_item_string();
        noun_item(&c.defsv2_gender, &c.defsv2_is_plural);
        c.speed = (uint8_t)text_item_number();
        c.triad = (uint8_t)text_item_number();
        c.range = (uint8_t)text_item_number();
        c.missile = (uint8_t)text_item_number();
        c.cargo = (uint8_t)text_item_number();
        c.cost = (uint8_t)text_item_number();
        c.preq_tech = (int16_t)tech_name(text_item());
        c.offsv_name_lrg = text_item_string();
        noun_item(&c.offsv_gender_lrg, &c.offsv_is_plural_lrg);
        c.defsv_name_lrg = text_item_string();
        noun_item(&c.defsv_gender_lrg, &c.defsv_is_plural_lrg);
    }

    if (text_open(AlphaxFileName, "REACTORS")) return 1;
    for (int32_t i = 0; i < 4; i++) {
        text_get();
        Reactor[i].name = text_item_string();
        Reactor[i].name_short = text_item_string();
        text_item();  // power: not stored
        Reactor[i].preq_tech = (int16_t)tech_name(text_item());
    }

    if (text_open(AlphaxFileName, "WEAPONS")) return 1;
    for (int32_t i = 0; i < 26; i++) {
        CWeapon& w = Weapon[i];
        text_get();
        w.name = text_item_string();
        w.name_short = text_item_string();
        w.offense_value = (int8_t)text_item_number();
        w.mode = (uint8_t)text_item_number();
        w.cost = (uint8_t)text_item_number();
        w.icon = (int8_t)text_item_number();
        w.preq_tech = (int16_t)tech_name(text_item());
    }

    if (text_open(AlphaxFileName, "DEFENSES")) return 1;
    for (int32_t i = 0; i < 14; i++) {
        CArmor& a = Armor[i];
        text_get();
        a.name = text_item_string();
        a.name_short = text_item_string();
        a.defense_value = (int8_t)text_item_number();
        a.mode = (uint8_t)text_item_number();
        a.cost = (uint8_t)text_item_number();
        a.preq_tech = (int16_t)tech_name(text_item());
    }

    if (text_open(AlphaxFileName, "ABILITIES")) return 1;
    for (int32_t i = 0; i < 29; i++) {
        CAbility& a = Ability[i];
        text_get();
        a.name = text_item_string();
        a.cost = text_item_number();
        a.preq_tech = (int16_t)tech_name(text_item());
        a.abbreviation = text_item_string();
        a.flags = text_item_binary();
        a.description = text_item_string();
    }

    if (text_open(AlphaxFileName, "MORALE")) return 1;
    for (int32_t i = 0; i < 7; i++) {
        text_get();
        Morale[i].name = text_item_string();
        Morale[i].name_lifecycle = text_item_string();
    }

    if (text_open(AlphaxFileName, "DEFENSEMODES")) return 1;
    for (int32_t i = 0; i < 3; i++) {
        text_get();
        DefenseMode[i].name = text_item_string();
        DefenseMode[i].hyphened = text_item_string();
        DefenseMode[i].abbrev = text_item_string();
        DefenseMode[i].letter = text_item_string();
    }

    if (text_open(AlphaxFileName, "OFFENSEMODES")) return 1;
    for (int32_t i = 0; i < 3; i++) {
        text_get();
        OffenseMode[i].name = text_item_string();
        OffenseMode[i].hyphened = text_item_string();
        OffenseMode[i].abbrev = text_item_string();
        OffenseMode[i].letter = text_item_string();
    }

    if (all_rules) {
        for (int32_t i = 0; i < 512; i++) {
            Units[i].name[0] = 0;
            Units[i].unit_flags = 0;
            Units[i].group_id = 0;
        }
        if (read_units()) return 1;
    }

    if (text_open(AlphaxFileName, "FACILITIES")) return 1;
    for (int32_t i = 1; i < 134; i++) {
        CFacility& f = Facility[i];
        text_get();
        f.name = text_item_string();
        f.cost = text_item_number();
        f.maint = text_item_number();
        f.preq_tech = tech_name(text_item());
        f.free_tech = tech_name(text_item());
        f.effect = text_item_string();
        f.free_tech = -2;  // the "free" tech column is read but never used
        if (i >= 70) {    // secret projects: AI priorities
            f.AI_fight = text_item_number();
            f.AI_power = text_item_number();
            f.AI_tech = text_item_number();
            f.AI_wealth = text_item_number();
            f.AI_growth = text_item_number();
        }
    }

    if (text_open(AlphaxFileName, "ORDERS")) return 1;
    for (int32_t i = 0; i < 30; i++) {
        if (i < 4 || i >= 24) {  // 4-23 are the terraforming orders (#TERRAIN)
            text_get();
            Order[i].order = Order[i].order_sea = text_item_string();
            Order[i].letter = text_item_string();
        }
    }

    if (text_open(AlphaxFileName, "COMPASS")) return 1;
    for (int32_t i = 0; i < 8; i++) {
        text_get();
        Compass[i] = text_item_string();
    }

    if (text_open(AlphaxFileName, "PLANS")) return 1;
    for (int32_t i = 0; i < 15; i++) {
        text_get();
        PlansShortName[i] = text_item_string();
        PlansFullName[i] = text_item_string();
    }

    if (text_open(AlphaxFileName, "TRIAD")) return 1;
    for (int32_t i = 0; i < 3; i++) {
        text_get();
        TriadName[i] = text_item_string();
    }

    if (text_open(AlphaxFileName, "RESOURCES")) return 1;
    for (int32_t i = 0; i < 4; i++) {
        text_get();
        ResName[i].name_singular = text_item_string();
        ResName[i].name_plural = text_item_string();
    }

    if (text_open(AlphaxFileName, "ENERGY")) return 1;
    for (int32_t i = 0; i < 3; i++) {
        text_get();
        Energy[i].abbrev = text_item_string();
        Energy[i].name = text_item_string();
    }

    if (text_open(AlphaxFileName, "CITIZENS")) return 1;
    for (int32_t i = 0; i < 10; i++) {
        CCitizen& c = Citizen[i];
        text_get();
        c.singular_name = text_item_string();
        c.plural_name = text_item_string();
        if (i < 7) {  // specialists
            c.preq_tech = tech_name(text_item());
            c.obsol_tech = tech_name(text_item());
            c.econ_bonus = text_item_number();
            c.psych_bonus = text_item_number();
            c.labs_bonus = text_item_number();
        }
    }

    if (text_open(AlphaxFileName, "SOCIO")) return 1;
    text_get();
    for (int32_t i = 0; i < 11; i++) crt_strcpy(SocialParam[i].set1, text_item());
    text_get();
    for (int32_t i = 0; i < 11; i++) crt_strcpy(SocialParam[i].set2, text_item());
    text_get();
    for (int32_t i = 0; i < 4; i++) SocialField[i].field_name = text_item_string();
    for (int32_t i = 0; i < 4; i++) {
        for (int32_t j = 0; j < 4; j++) {
            CSocialField& s = SocialField[i];
            text_get();
            s.soc_name[j] = text_item_string();
            s.soc_preq_tech[j] = tech_name(text_item());
            int32_t* effect = s.soc_effect[j].values;
            for (int32_t k = 0; k < 11; k++) effect[k] = 0;
            while (crt_strlen(text_item())) {
                // "++ECONOMY", "-PLANET": each sign adds or subtracts one
                const char* p = TextBufferItemPtr;
                int32_t amount = 0;
                for (;; p++) {
                    if (*p == '+') amount++;
                    else if (*p == '-') amount--;
                    else break;
                }
                for (int32_t k = 0; k < 11; k++)
                    if (!crt_stricmp(p, SocialParam[k].set1)) effect[k] += amount;
            }
        }
    }

    if (text_open(AlphaxFileName, "DIFF")) return 1;
    for (int32_t i = 0; i < 6; i++) {
        text_get();
        Difficulty[i] = text_item_string();
    }

    if (all_rules && read_factions()) return 1;

    if (text_open(AlphaxFileName, "MANDATE")) return 1;
    for (int32_t i = 0; i < 4; i++) {
        text_get();
        Mandate[i].name = text_item_string();
        Mandate[i].name_caps = text_item_string();
    }

    if (text_open(AlphaxFileName, "MOOD")) return 1;
    for (int32_t i = 0; i < 9; i++) {
        text_get();
        Mood[i] = text_item_string();
    }

    if (text_open(AlphaxFileName, "REPUTE")) return 1;
    for (int32_t i = 0; i < 8; i++) {
        text_get();
        Repute[i] = text_item_string();
    }

    if (text_open(AlphaxFileName, "MIGHT")) return 1;
    for (int32_t i = 0; i < 7; i++) {
        text_get();
        Might[i].adj_start = text_item_string();
        Might[i].adj = text_item_string();
    }

    if (text_open(AlphaxFileName, "PROPOSALS")) return 1;
    for (int32_t i = 0; i < 11; i++) {
        text_get();
        Proposal[i].name = text_item_string();
        Proposal[i].preq_tech = tech_name(text_item());
        Proposal[i].description = text_item_string();
    }

    if (text_open(AlphaxFileName, "NATURAL")) return 1;
    for (int32_t i = 0; i < 16; i++) {
        text_get();
        Natural[i].name = text_item_string();
        Natural[i].name_short = text_item_string();
    }

    // bubble help of the terraforming buttons: the k-th terraform type with a map bit
    for (int32_t k = 0; k < 15; k++) {
        int32_t id = k;
        for (int32_t j = 0; j < 20; j++)
            if (!Terraform[j].bit && id >= j) id++;
        BaseButton_set_bubble_text(TerraformButtons[k], Strings_get(&StringTable, Terraform[id].name));
    }
    BaseButton_set_bubble_text(TerraformButtons[15], Strings_get(&StringTable, Natural[2].name_short));
    BaseButton_set_bubble_text(TerraformButtons[16], Strings_get(&StringTable, Natural[6].name_short));
    BaseButton_set_bubble_text(TerraformButtons[17], Strings_get(&StringTable, Natural[3].name_short));
    BaseButton_set_bubble_text(TerraformButtons[18], Strings_get(&StringTable, Natural[10].name_short));
    return 0;
}

#ifndef SMACX_HYBRID
// Initial map-bit columns of Terraform (0x691878, .data): {bit, bit_incompatible}.
static const uint32_t kTerraformBits[20][2] = {
    {0x8000, 0x200000},       {0x80000, 0x200000},   {0x10, 0x81E00050},      {0x40, 0x81E00050},
    {0x200000, 0x1E88070},    {0x4, 0x0},            {0x8, 0x0},              {0x800, 0x40000},
    {0x40000, 0x800},         {0x80000000, 0x1C00050}, {0x0, 0x20},           {0x20, 0x288050},
    {0x400000, 0x81E00050},   {0x800000, 0x81E00050}, {0x1000000, 0x81E08050}, {0x0, 0x0},
    {0x0, 0x0},               {0x0, 0x0},            {0x0, 0x0},              {0x2000, 0x30000400},
};

void rules_static_init() {
    AlphaxFileName = "ALPHAX";
    ScriptFileName = "SCRIPT";
    for (int32_t i = 0; i < 20; i++) {
        Terraform[i].bit = kTerraformBits[i][0];
        Terraform[i].bit_incompatible = kTerraformBits[i][1];
    }
}
#endif

// ---- original layout of the pointer tables (see rules.h) ----

uint32_t rules_orig_string_addr(const char* str) {
    constexpr uint32_t kOrigStringPoolBase = 0x01FBD3B8;  // StringTable.heap.base in the oracle
    const char* base = reinterpret_cast<const char*>(StringTable.heap.base);
    if (!str || !base || str < base || str >= base + StringTable.heap.base_size) return 0;
    return kOrigStringPoolBase + (uint32_t)(str - base);
}

uint8_t rules_orig_byte(uint32_t addr) {
#ifdef SMACX_HYBRID
    return *mem::host(addr);
#else
    uint8_t rec[0x2C] = {};
    auto put32 = [&](uint32_t off, uint32_t v) { crt_memcpy(rec + off, &v, 4); };
    auto put16 = [&](uint32_t off, uint16_t v) { crt_memcpy(rec + off, &v, 2); };
    if (addr >= 0x94F278 && addr < 0x94F278 + 14 * 0x10) {
        const CArmor& a = Armor[(addr - 0x94F278) / 0x10];
        put32(0, rules_orig_string_addr(a.name));
        put32(4, rules_orig_string_addr(a.name_short));
        rec[8] = (uint8_t)a.defense_value;
        rec[9] = a.mode;
        rec[10] = a.cost;
        rec[11] = (uint8_t)a.padding1;
        put16(12, (uint16_t)a.preq_tech);
        put16(14, (uint16_t)a.padding2);
        return rec[(addr - 0x94F278) % 0x10];
    }
    if (addr >= 0x9527F8 && addr < 0x9527F8 + 4 * 0xC) {
        // Read as Vehs[-1] (0x9527F4) where a vehicle id is -1.
        const CReactor& r = Reactor[(addr - 0x9527F8) / 0xC];
        put32(0, rules_orig_string_addr(r.name));
        put32(4, rules_orig_string_addr(r.name_short));
        put16(8, (uint16_t)r.preq_tech);
        put16(10, (uint16_t)r.power);
        return rec[(addr - 0x9527F8) % 0xC];
    }
    if (addr >= 0x94F358 && addr < 0x94F358 + 89 * 0x2C) {
        const CTech& t = Tech[(addr - 0x94F358) / 0x2C];
        put32(0, (uint32_t)t.flags);
        put32(4, rules_orig_string_addr(t.name));
        crt_memcpy(rec + 8, t.short_name, 8);
        put32(16, (uint32_t)t.padding);
        put32(20, (uint32_t)t.AI_growth);
        put32(24, (uint32_t)t.AI_tech);
        put32(28, (uint32_t)t.AI_wealth);
        put32(32, (uint32_t)t.AI_power);
        put32(36, (uint32_t)t.preq_tech1);
        put32(40, (uint32_t)t.preq_tech2);
        return rec[(addr - 0x94F358) % 0x2C];
    }
    return *mem::host(addr);
#endif
}
