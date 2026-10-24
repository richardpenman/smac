#include "engine/design.h"

#include <cstring>

#include "engine/ai.h"
#include "engine/base.h"
#include "engine/battle.h"
#include "engine/crt.h"
#include "engine/diplo_act.h"
#include "engine/diplomacy.h"
#include "engine/map.h"
#include "engine/net.h"
#include "engine/parse.h"
#include "engine/proto.h"
#include "engine/rules.h"
#include "engine/stack.h"
#include "engine/strings.h"
#include "engine/tech.h"
#include "engine/ui_bridge.h"
#include "engine/veh.h"

namespace {
// Unit flags (UNIT.unit_flags).
constexpr uint32_t kUnitActive = 0x1;
constexpr uint32_t kUnitCustomName = 0x2;
constexpr uint32_t kUnitPrototyped = 0x4;

inline int32_t weap_off(int32_t weapon_id) { return Weapon[weapon_id].offense_value; }
inline int32_t arm_def(int32_t armor_id) { return Armor[armor_id].defense_value; }
inline char* S(const char* str) { return Strings_get(&StringTable, str); }
inline uint8_t faction_bit(int32_t faction_id) { return (uint8_t)(1u << (faction_id & 31)); }
inline bool is_human(int32_t faction_id) { return (FactionStatus[0] & faction_bit(faction_id)) != 0; }
// Unit slot of the i-th (0..127) entry of a faction's design list: basic units, then its prototypes.
inline int32_t proto_slot(int32_t i, int32_t faction_id) { return i < 64 ? i : faction_id * 64 + i % 64; }
// Units[unit_id].name without a range check (the original reads Units[-1] in name_proto).
inline const char* unit_name_raw(int32_t unit_id) {
    return reinterpret_cast<const char*>(&Units[0]) + unit_id * (int32_t)sizeof(UNIT);
}
// Reactor short name (reactor_id 1-4).
inline char* reactor_name(int32_t reactor_id) { return S(Reactor[reactor_id - 1].name_short); }
inline int32_t abil_preq(int32_t ability_id) { return Ability[ability_id].preq_tech; }
// The faction may combine two special abilities.
inline bool two_abils(int32_t faction_id) { return has_tech(Rules.tech_preq_allow_2_spec_abil, faction_id) != 0; }
}  // namespace

int32_t GAMEAPI transport_val(int32_t chassis_id, uint32_t ability_flags, int32_t reactor_id) {
    int32_t cargo = Chassis[chassis_id].cargo;
    if (Chassis[chassis_id].triad == 1) cargo *= reactor_id;  // sea
    if (ability_flags & 0x800000) cargo /= 2;
    if (ability_flags & 0x80000) cargo = (cargo * 3 + 1) / 2;
    return cargo;
}

int32_t GAMEAPI arm_strat(int32_t armor_id, int32_t faction_id) {
    if (!ExpansionEnabled && armor_id > 9) return 1;
    if (arm_def(armor_id) < 0)
        return psi_factor(Factions[faction_id].enemy_best_weapon_value * Rules.psi_combat_ratio_def[0] /
                              Rules.psi_combat_ratio_atk[0],
                          faction_id, 0, 0);
    return arm_def(armor_id);
}

int32_t GAMEAPI weap_strat(int32_t weapon_id, int32_t faction_id) {
    if (!ExpansionEnabled && (weapon_id == 12 || weapon_id == 13 || weapon_id == 14)) return 1;
    if (weap_off(weapon_id) < 0)
        return psi_factor(Factions[faction_id].enemy_best_armor_value * Rules.psi_combat_ratio_atk[0] /
                              Rules.psi_combat_ratio_def[0],
                          faction_id, 1, 0);
    return weap_off(weapon_id);
}

int32_t GAMEAPI weap_val(int32_t unit_id, int32_t faction_id) {
    return weap_strat(Units[unit_id].weapon_id, faction_id);
}

int32_t GAMEAPI arm_val(int32_t armor_id, int32_t faction_id) {
    if (faction_id < 0) return arm_def(armor_id) * 2;
    if (!ExpansionEnabled && armor_id > 9) return 2;
    if (arm_def(armor_id) < 0)
        return psi_factor(Factions[faction_id].enemy_best_weapon_value * Rules.psi_combat_ratio_def[0] /
                              Rules.psi_combat_ratio_atk[0],
                          faction_id, 0, 0) *
               2;
    return arm_def(armor_id) * 2;
}

int32_t GAMEAPI armor_val(int32_t unit_id, int32_t faction_id) {
    return arm_val(Units[unit_id].armor_id, faction_id);
}

// ---- stat strings ----

void GAMEAPI say_offense(char* buffer, int32_t unit_id) {
    char num[80];
    if (has_abil(unit_id, 0x20)) std::strcat(buffer, "<");
    if (can_arty(unit_id, 1)) std::strcat(buffer, "(");
    crt_itoa(offense_proto(unit_id, -1, 0), num, 10);
    std::strcat(buffer, num);
    if (has_abil(unit_id, 0x10)) std::strcat(buffer, "^");
    if (has_abil(unit_id, 8)) std::strcat(buffer, "~");
    if (has_abil(unit_id, 0x100000)) std::strcat(buffer, "x");
    if (has_abil(unit_id, 0x800)) std::strcat(buffer, "e");
    if (has_abil(unit_id, 0x20000)) std::strcat(buffer, "!");
    if (can_arty(unit_id, 1)) std::strcat(buffer, ")");
    if (has_abil(unit_id, 0x20)) std::strcat(buffer, ">");
    if (Units[unit_id].weapon_id == 12 || Units[unit_id].weapon_id == 13) std::strcat(buffer, "r");
}

void GAMEAPI say_defense(char* buffer, int32_t unit_id) {
    char num[80];
    if (has_abil(unit_id, 0x100)) std::strcat(buffer, "<");
    crt_itoa(armor_proto(unit_id, -1, 0), num, 10);
    std::strcat(buffer, num);
    if (has_abil(unit_id, 0x200)) std::strcat(buffer, "+");
    if (has_abil(unit_id, 0x40000)) std::strcat(buffer, "t");
    if (has_abil(unit_id, 0x100)) std::strcat(buffer, ">");
    if (Units[unit_id].armor_id == 10 || Units[unit_id].armor_id == 12) std::strcat(buffer, "p");
    if (Units[unit_id].armor_id == 11 || Units[unit_id].armor_id == 13) std::strcat(buffer, "r");
}

void GAMEAPI say_stats_3(char* buffer, int32_t unit_id) {
    char num[80];
    const UNIT& unit = Units[unit_id];
    if (weap_off(unit.weapon_id) < 0)
        std::strcat(buffer, "?");
    else if (weap_off(unit.weapon_id) < 99)
        say_offense(buffer, unit_id);
    else
        std::strcat(buffer, "*");
    std::strcat(buffer, "-");
    if (arm_def(unit.armor_id) < 0)
        std::strcat(buffer, "?");
    else
        say_defense(buffer, unit_id);
    std::strcat(buffer, "-");
    crt_itoa(speed_proto(unit_id) / Rules.move_rate_roads, num, 10);
    std::strcat(buffer, num);
    if (unit.reactor_id > 1) {
        std::strcat(buffer, "*");
        crt_itoa(unit.reactor_id, num, 10);
        std::strcat(buffer, num);
    }
}

void GAMEAPI say_stats_2(char* buffer, int32_t unit_id) {
    char num[80];
    const UNIT& unit = Units[unit_id];
    if (weap_off(unit.weapon_id) < 0)
        std::strcat(buffer, label_get(196));  // psi
    else if (weap_off(unit.weapon_id) < 99)
        say_offense(buffer, unit_id);
    else
        std::strcat(buffer, "*");
    std::strcat(buffer, "-");
    if (arm_def(unit.armor_id) < 0)
        std::strcat(buffer, label_get(196));
    else
        say_defense(buffer, unit_id);
    std::strcat(buffer, "-");
    crt_itoa(speed_proto(unit_id) / Rules.move_rate_roads, num, 10);
    std::strcat(buffer, num);
    if (Chassis[unit.chassis_id].triad == 1) {
        std::strcat(buffer, " ");
        std::strcat(buffer, label_get(163));
    }
    if (Chassis[unit.chassis_id].triad == 2) {
        std::strcat(buffer, " ");
        std::strcat(buffer, label_get(162));
    }
    if (unit.reactor_id > 1) {
        std::strcat(buffer, "*");
        crt_itoa(unit.reactor_id, num, 10);
        std::strcat(buffer, num);
    }
}

void GAMEAPI say_stats(char* buffer, int32_t unit_id, const char* separator) {
    char num[80];
    const UNIT& unit = Units[unit_id];
    uint8_t plan = (uint8_t)unit.plan;
    uint8_t mode = Weapon[unit.weapon_id].mode;
    int32_t triad = Chassis[unit.chassis_id].triad;
    bool full_name = false;
    if (plan == 3 && triad == 0 && weap_off(unit.weapon_id) == 1 && arm_def(unit.armor_id) == 1 &&
        unit.ability_flags == 0) {
        std::strcat(buffer, S(PlansFullName[3]));
        std::strcat(buffer, ", ");
    } else if (mode < 3) {
        const char* name;
        if (plan == 2 && (weap_off(unit.weapon_id) < 0 || arm_def(unit.armor_id) < weap_off(unit.weapon_id)))
            name = TextLabels[312];
        else
            name = PlansShortName[plan];
        std::strcat(buffer, S(name));
        std::strcat(buffer, ", ");
    }
    bool separator_done = false;
    if (weap_off(unit.weapon_id) < 0) {
        std::strcat(buffer, label_get(196));
        separator_done = true;
    } else if (mode < 3) {
        say_offense(buffer, unit_id);
        separator_done = true;
    } else if (arm_def(unit.armor_id) == 1 && unit.ability_flags == 0 &&
               (Chassis[unit.chassis_id].speed == 1 || (mode == 7 && unit.chassis_id == 3))) {
        std::strcat(buffer, S(PlansFullName[mode]));
        if (plan == 7) {
            std::strcat(buffer, "(");
            crt_itoa(unit.carry_capacity, num, 10);
            std::strcat(buffer, num);
            std::strcat(buffer, ")");
        }
        if (triad != 0) std::strcat(buffer, ", ");
        full_name = true;
    } else {
        std::strcat(buffer, S(PlansShortName[mode]));
        if (plan == 7) {
            std::strcat(buffer, "(");
            crt_itoa(unit.carry_capacity, num, 10);
            std::strcat(buffer, num);
            std::strcat(buffer, ")");
        }
        std::strcat(buffer, ", ");
    }
    if (separator_done) std::strcat(buffer, separator ? separator : "/");
    if (!full_name) {
        if (arm_def(unit.armor_id) < 0)
            std::strcat(buffer, label_get(196));
        else
            say_defense(buffer, unit_id);
        std::strcat(buffer, separator ? separator : "/");
        crt_itoa(speed_proto(unit_id) / Rules.move_rate_roads, num, 10);
        std::strcat(buffer, num);
    }
    if (triad == 1) {
        std::strcat(buffer, " ");
        std::strcat(buffer, label_get(163));
    }
    if (triad == 2) {
        std::strcat(buffer, " ");
        std::strcat(buffer, label_get(162));
    }
    if (unit.reactor_id > 1) {
        std::strcat(buffer, " ");
        std::strcat(buffer, "(");
        std::strcat(buffer, "*");
        crt_itoa(unit.reactor_id, num, 10);
        std::strcat(buffer, num);
        std::strcat(buffer, ")");
    }
}

// ---- savegame headers ----

void GAMEAPI header_check(char* buffer, CrtFile* file) {
    int32_t n = 0;
    char c = (char)crt_fgetc(file);
    *buffer = c;
    while (c) {
        buffer++;
        n++;
        if (n > 0xFF) break;  // BUG(original): 256 characters without NUL leave buffer unterminated
        c = (char)crt_fgetc(file);
        *buffer = c;
    }
    crt_fgetc(file);  // 0x1A
}

void GAMEAPI header_write(const char* header, CrtFile* file) {
    char c;
    do {
        c = *header++;
        crt_fputc(c, file);
    } while (c);
    crt_fputc(0x1A, file);
}

// ---- bunglist ----

void GAMEAPI bung_clear(uint8_t* entry) {
    for (int32_t k = 0; k < 8; k++) entry[k] = 0xFF;
}

void GAMEAPI clear_bunglist(int32_t faction_id) {
    for (int32_t i = 0; i < 64; i++) {
        uint8_t* entry = BungList[faction_id][i];
        for (int32_t k = 0; k < 8; k++) entry[k] = 0xFF;
    }
}

void GAMEAPI bung_add(int32_t faction_id, int32_t unit_id) {
    int32_t index = Factions[faction_id].bunglist_next;
    Factions[faction_id].bunglist_next = (index + 1) % 64;
    // The stored index is not range checked.
    uint8_t* entry = &BungList[0][0][0] + (faction_id * 64 + index) * 8;
    const UNIT& unit = Units[unit_id];
    std::memcpy(entry, &unit.ability_flags, 4);
    entry[4] = unit.chassis_id;
    entry[5] = unit.weapon_id;
    entry[6] = unit.armor_id;
    entry[7] = unit.reactor_id;
}

int32_t GAMEAPI is_bunged(int32_t faction_id, int32_t chassis_id, int32_t weapon_id, int32_t armor_id,
                          uint32_t ability_flags, int32_t reactor_id) {
    if (!is_human(faction_id)) return 0;
    for (int32_t i = 0; i < 64; i++) {
        const uint8_t* entry = BungList[faction_id][i];
        uint32_t flags;
        std::memcpy(&flags, entry, 4);
        if (entry[4] == (uint32_t)chassis_id && entry[5] == (uint32_t)weapon_id && entry[6] == (uint32_t)armor_id &&
            flags == ability_flags && entry[7] == (uint32_t)reactor_id)
            return 1;
    }
    return 0;
}

// ---- naming ----

namespace {
// name_proto: makes name differ from the faction's other designs: bumps a trailing digit 2-8 of an
// existing numeric suffix, or appends " <suffix>2". Sets *stop when the digit cannot be bumped.
void name_bump(char* name, int32_t* stop) {
    char* found = nullptr;
    do {
        found = std::strstr(found ? found + 1 : name, label_get(919));
        if (!found) goto append;
    } while (std::strstr(found + 1, label_get(919)));
    if (std::strlen(found) > std::strlen(label_get(919)) + 1) goto append;
    {
        size_t len = std::strlen(name);
        int8_t c = (int8_t)name[len - 1];
        if (c < '2' || c >= '9')
            *stop = 1;
        else
            name[len - 1] = (char)(c + 1);
    }
    return;
append:
    char num[80];
    std::strcat(name, " ");
    std::strcat(name, label_get(919));
    crt_itoa(2, num, 10);
    std::strcat(name, num);
}
}  // namespace

void GAMEAPI name_proto(char* name, int32_t unit_id, int32_t faction_id, int32_t chassis_id, int32_t weapon_id,
                        int32_t armor_id, uint32_t ability_flags, int32_t reactor_id) {
    // Two candidate names: `name` and the variant `alt`; the variant is chosen by a parity bit.
    char alt_buf[258];
    char* alt = alt_buf + 1;  // the original writes alt[strlen(name) - 1] (see below)
    const CChassis& ch = Chassis[chassis_id];
    int32_t off = weap_off(weapon_id);
    int32_t def = arm_def(armor_id);
    uint8_t mode = Weapon[weapon_id].mode;
    name[0] = 0;
    // first ability abbreviation as a prefix
    for (int32_t i = 0; i < 29; i++) {
        if (!(ability_flags & (1u << i))) continue;
        if (!std::strlen(S(Ability[i].abbreviation))) continue;
        if (i == 5 && ch.triad == 2) continue;  // air superiority on an air chassis
        if (i == 15) continue;
        if (i == 3 && ch.speed == 1 && off >= def && !(ability_flags & 0x8000)) continue;
        std::strcat(name, S(Ability[i].abbreviation));
        std::strcat(name, " ");
        break;
    }
    std::strcpy(alt, name);
    int32_t large = 0;    // use the "large" chassis names
    int32_t not_sea = 0;
    if (mode < 3) {
        int32_t threshold;
        int32_t value;
        if (off < def) {
            threshold = 6;
            value = off;
        } else {
            threshold = 3;
            value = def;
        }
        if (ch.triad == 1) threshold++;
        if (ch.triad == 2) threshold += (uint32_t)threshold / 2;
        large = threshold < value;
        not_sea = ch.triad != 1;
        if (!std::strlen(S(ch.offsv_name_lrg)) || !std::strlen(S(ch.defsv_name_lrg))) large = 0;
    }
    int32_t special = 0;
    int32_t parity = ((reactor_id - 1) ^ faction_id) & 1;
    bool police = Units[unit_id].group_id == 10 && !is_human(faction_id) && ch.speed > 1;
    if (police || (off <= ch.speed && def <= ch.speed && off >= 0 && ch.triad == 0)) {
        if (mode < 3) {
            if (!has_abil(unit_id, 0x400000)) {
                int32_t label = off < 2 ? 146 : 182;
                std::strcat(name, label_get(label));
                std::strcat(alt, label_get(label));
            } else {
                if (std::strlen(name)) name[std::strlen(name) - 1] = 0;
                // BUG(original): truncates alt at the (already shortened) length of name
                if (std::strlen(alt)) alt[std::strlen(name) - 1] = 0;
            }
            special = 1;
            parity = ch.speed > 1;
        }
    }
    // 1: a better reactor than every other design of the same plan and chassis
    int32_t reactor_rank = 0;
    if (!special) {
        for (int32_t i = 0; i < 128; i++) {
            int32_t u = proto_slot(i, faction_id);
            if ((Units[u].unit_flags & kUnitActive) && Units[u].plan == Units[unit_id].plan &&
                Units[u].chassis_id == Units[unit_id].chassis_id) {
                if (Units[unit_id].reactor_id <= Units[u].reactor_id) {
                    reactor_rank = 0;
                    break;
                }
                reactor_rank = 1;
            }
        }
    }
    const char* alt_part;
    if (off < def && mode < 3 && off >= 0 && !(ability_flags & 0x8000)) {
        // defensive unit: "<armor> <defensive chassis name>"
        if (!special) {
            parity ^= (uint8_t)def & 1;
            const char* part = reactor_rank ? Reactor[reactor_id - 1].name_short : Armor[armor_id].name_short;
            std::strcat(name, S(part));
            std::strcat(alt, S(part));
        }
        std::strcat(name, " ");
        std::strcat(alt, " ");
        std::strcat(name, S(ch.defsv1_name));
        if (!special || !(unit_id & 1))
            alt_part = (!large || not_sea) ? ch.defsv2_name : ch.defsv_name_lrg;
        else
            alt_part = (!large || !not_sea) ? ch.defsv1_name : ch.defsv_name_lrg;
        std::strcat(alt, S(alt_part));
    } else if (ch.triad != 2 || !(ability_flags & 0x20) || mode > 2 || off < 0) {
        if (ch.missile) {
            if (off > 98) {
                std::strcat(name, reactor_name(reactor_id));
                std::strcat(alt, reactor_name(reactor_id));
                std::strcat(name, " ");
                std::strcat(alt, " ");
                std::strcat(name, S(Weapon[weapon_id].name_short));
                std::strcat(alt, S(Weapon[weapon_id].name_short));
                goto dedupe;
            }
            if (weapon_id == 24 || weapon_id == 25) {
                std::strcat(name, S(Weapon[weapon_id].name_short));
                std::strcat(alt, S(Weapon[weapon_id].name_short));
                std::strcat(name, " ");
                std::strcat(alt, " ");
                std::strcat(name, reactor_name(reactor_id));
                std::strcat(alt, reactor_name(reactor_id));
                std::strcat(name, " ");
                std::strcat(alt, " ");
                std::strcat(name, S(ch.offsv1_name));
                std::strcat(alt, S(ch.offsv1_name));
                goto dedupe;
            }
        }
        if (mode > 2 && chassis_id != 0) {
            if (reactor_id > 1) {
                std::strcat(name, reactor_name(reactor_id));
                std::strcat(alt, reactor_name(reactor_id));
                std::strcat(name, " ");
                std::strcat(alt, " ");
            }
            if (!((mode == 7 && chassis_id == 3) || (mode == 9 && ch.triad == 0 && std::strchr(name, ' ')))) {
                if ((mode == 8 || mode == 9) && chassis_id == 3) {
                    std::strcat(name, label_get(163));
                    std::strcat(alt, label_get(163));
                } else {
                    std::strcat(name, S(ch.offsv1_name));
                    std::strcat(alt, S(ch.offsv2_name));
                }
                std::strcat(name, " ");
                std::strcat(alt, " ");
            }
        }
        if (!special) {
            parity ^= (uint8_t)off & 1;
            std::strcat(name, S(Weapon[weapon_id].name_short));
            std::strcat(alt, S(Weapon[weapon_id].name_short));
        }
        if (mode > 2) goto dedupe;
        if (ability_flags & 0x8000) {
            std::strcat(name, " ");
            std::strcat(name, label_get(543));
            std::strcat(alt, " ");
            std::strcat(alt, label_get(544));
        } else if ((ability_flags & 8) && ch.speed == 1 && off > def) {
            std::strcat(name, " ");
            std::strcat(name, label_get(614));
            std::strcat(alt, " ");
            std::strcat(alt, label_get(615));
        } else {
            std::strcat(name, " ");
            if (!reactor_rank)
                std::strcat(name, S((!large || !not_sea) ? ch.offsv1_name : ch.offsv_name_lrg));
            else
                std::strcat(name, reactor_name(reactor_id));
            std::strcat(alt, " ");
            if (reactor_rank)
                std::strcat(alt, reactor_name(reactor_id));
            else
                std::strcat(alt, S((!large || not_sea) ? ch.offsv2_name : ch.offsv_name_lrg));
        }
    } else {
        // air unit with air superiority
        if (!special) {
            const char* part = reactor_rank ? Reactor[reactor_id - 1].name_short : Weapon[weapon_id].name_short;
            std::strcat(name, S(part));
            std::strcat(alt, S(part));
        }
        std::strcat(name, " ");
        std::strcat(alt, " ");
        std::strcat(name, S(ch.defsv1_name));
        if (!special || !(unit_id & 1))
            alt_part = (!large || not_sea) ? ch.defsv2_name : ch.defsv_name_lrg;
        else
            alt_part = (!large || !not_sea) ? ch.defsv1_name : ch.defsv_name_lrg;
        std::strcat(alt, S(alt_part));
    }
dedupe : {
    int32_t same = std::strcmp(name, alt) == 0;
    int32_t stop = 0;
    for (int32_t i = 0; i < 512; i++) {
        if (i == unit_id) continue;
        if (i / 64 != 0 && i / 64 != faction_id) continue;
        const UNIT& other = Units[i];
        if (other.chassis_id == (uint32_t)chassis_id && other.weapon_id == (uint32_t)weapon_id &&
            other.armor_id == (uint32_t)armor_id && other.reactor_id == (uint32_t)reactor_id &&
            other.ability_flags == ability_flags)
            continue;
        if (!stop && !std::strcmp(name, Units[i].name)) {
            if (!same) parity = 1;
            name_bump(name, &stop);
            i = -1;
        }
        // BUG(original): after a rename above, this compares with Units[-1] (memory before the table)
        if (!same && !std::strcmp(alt, unit_name_raw(i))) {
            parity = 0;
            name_bump(alt, &stop);
            i = -1;
        }
    }
    if (parity) std::strcpy(name, alt);
}
}

// ---- component selection ----

int32_t GAMEAPI best_reactor(int32_t faction_id) {
    for (int32_t r = 3; r >= 0; r--)
        if (has_tech(Reactor[r].preq_tech, faction_id)) return r + 1;
    return 1;
}

int32_t GAMEAPI pick_chassis(int32_t faction_id, int32_t triad, int32_t speed) {
    int32_t best_score = 0;
    int32_t best = -1;
    for (int32_t i = 0; i < 9; i++) {
        const CChassis& ch = Chassis[i];
        if (!has_tech(ch.preq_tech, faction_id)) continue;
        if (triad >= 0 && ch.triad != triad) continue;
        if (ch.missile && speed != -1) continue;
        int32_t score = 1;
        if (speed > 0) {
            if (ch.speed < speed) continue;
            if (ch.speed == speed) score = 2;
        } else if (triad != 2) {
            score = ch.speed + 1;
        } else if (speed == -1) {
            if (!ch.missile) continue;
            score = ch.speed + 1;
        } else {
            if (speed == -2 && ch.range != 1) continue;
            score = ch.speed + 1;
        }
        if (score > best_score) {
            best_score = score;
            best = i;
        }
    }
    return best;
}

int32_t GAMEAPI weapon_budget(int32_t faction_id, int32_t cost, int32_t combat_only) {
    int32_t best_value = -1;
    int32_t best = 0;
    for (int32_t i = 0; i < 26; i++) {
        const CWeapon& w = Weapon[i];
        if (!has_tech(w.preq_tech, faction_id)) continue;
        if (cost < 0) {
            if ((uint32_t)w.mode == (uint32_t)-cost) return i;
        } else if ((!combat_only || w.mode < 2) && w.cost <= cost && w.mode < 3 && w.offense_value < 99 && i != 23 &&
                   best_value <= w.offense_value * 2) {
            best_value = w.offense_value * 2;
            best = i;
        }
    }
    return best;
}

int32_t GAMEAPI armor_budget(int32_t faction_id, int32_t cost) {
    int32_t best_value = -1;
    int32_t best = 0;
    for (int32_t i = 0; i < 14; i++) {
        const CArmor& a = Armor[i];
        if (!has_tech(a.preq_tech, faction_id) || a.cost > cost || a.defense_value < 0) continue;
        int32_t value = (faction_id < 0 || ExpansionEnabled || i <= 9) ? a.defense_value : 1;
        if (best_value <= value * 2) {
            best_value = value * 2;
            best = i;
        }
    }
    return best;
}

// ---- prototypes ----

void GAMEAPI retire_proto(int32_t unit_id, int32_t faction_id) {
    bung_add(faction_id, unit_id);
    // replacement: an active design of the same group, preferably not obsolete
    int32_t best = -1;
    int32_t best_score = -1;
    for (int32_t j = 0; j < 64; j++) {
        int32_t u = faction_id * 64 + j;
        if (u != unit_id && (Units[u].unit_flags & kUnitActive) && Units[u].group_id == Units[unit_id].group_id) {
            int32_t score = 2 - ((Units[u].obsolete_factions & faction_bit(faction_id)) != 0);
            if (best_score < score) {
                best_score = score;
                best = u;
            }
        }
    }
    Faction& f = Factions[faction_id];
    for (int32_t v = VehCount - 1; v >= 0; v--) {
        if (v >= VehCount || Vehs[v].unit_id != unit_id) continue;
        VEH& veh = Vehs[v];
        if (!is_human(faction_id)) {
            if (best >= 0) {
                bool unseen = !(veh.visibility & FactionStatus[0]);
                if (!unseen) {
                    MAP* tile = map_tile(veh.x, veh.y);
                    uint32_t owner = tile->val2 & 0xF;
                    unseen = (tile->items & 1) && owner < 8 && owner != 0;
                }
                if (unseen && veh.faction_id == (uint32_t)faction_id) {
                    veh.unit_id = (int16_t)best;
                    f.units_active[unit_id]--;
                    f.units_active[best]++;
                    continue;
                }
            }
            int32_t base_id = base_at(veh.x, veh.y);
            if (base_id >= 0) {
                if (stack_check(v, 1, -1, -1, -1) == 1) {
                    veh.unit_id = 2;
                    f.units_active[unit_id]--;
                    f.units_active[2]++;
                    continue;
                }
                BASE& base = Bases[base_id];
                int32_t minerals = base.minerals_accumulated + (Units[veh.unit_id].cost >> 1);
                base.minerals_accumulated = minerals;
                base.minerals_accumulated_2 = minerals;
                base.production_id_last = base.queue_items[0];
            }
        }
        kill(v);
    }
    if (unit_id >= 64) {
        for (int32_t b = 0; b < BaseCount; b++) {
            BASE& base = Bases[b];
            if (base.queue_size < 0) continue;
            int32_t j = 0;
            do {
                if (base.queue_items[j] == unit_id) {
                    if (j == 0) {
                        base.queue_items[0] = -69;  // Stockpile Energy
                    } else {
                        for (int32_t k = j; k < base.queue_size; k++) base.queue_items[k] = base.queue_items[k + 1];
                        base.queue_size--;
                    }
                }
                j++;
            } while (j <= base.queue_size);
        }
        Units[unit_id].unit_flags &= ~kUnitActive;
    }
}

namespace {
// prune_protos: the faction's psi-aware armor comparison value (arm_val inlined).
inline int32_t prune_arm(int32_t armor_id, int32_t faction_id) { return arm_val(armor_id, faction_id); }

// prune_protos (human players): status text "name (stats abilities)" of a design.
void prune_say_design(int32_t unit_id) {
    StrBuffer[0] = 0;
    std::strcat(StrBuffer, Units[unit_id].name);
    std::strcat(StrBuffer, " ");
    std::strcat(StrBuffer, "(");
    say_stats_2(StrBuffer, unit_id);
    if (Units[unit_id].ability_flags) {
        std::strcat(StrBuffer, " ");
        for (int32_t i = 0; i < 29; i++)
            if (Units[unit_id].ability_flags & (1u << (i & 31))) std::strcat(StrBuffer, S(Ability[i].abbreviation));
    }
    std::strcat(StrBuffer, ")");
}
}  // namespace

void GAMEAPI prune_protos(int32_t faction_id, int32_t unit_id, int32_t net_synch) {
    uint8_t mask = faction_bit(faction_id);
    // Faction designs not yet built whose weapon, armor and chassis are all matched by known designs
    // become "prototyped" (0x100: no prototype cost).
    if (unit_id < 64 || (Units[unit_id].unit_flags & kUnitPrototyped)) {
        for (int32_t j = 0; j < 64; j++) {
            UNIT& u = Units[faction_id * 64 + j];
            if (!(u.unit_flags & kUnitActive) || (u.unit_flags & kUnitPrototyped)) continue;
            bool weapon = false, armor = false, chassis = false;
            for (int32_t i = 0; i < 128; i++) {
                int32_t v_id = proto_slot(i, faction_id);
                const UNIT& v = Units[v_id];
                if (!(v.unit_flags & kUnitActive)) continue;
                if (v_id < 64 ? !has_tech(v.preq_tech, faction_id) : !(v.unit_flags & kUnitPrototyped)) continue;
                if (v.weapon_id == u.weapon_id) weapon = true;
                if (v.armor_id == u.armor_id) armor = true;
                if (v.chassis_id == u.chassis_id) chassis = true;
                if (weap_off(u.weapon_id) > 0 && weap_off(u.weapon_id) <= weap_off(v.weapon_id)) weapon = true;
                if (arm_def(u.armor_id) > 0 && arm_def(u.armor_id) <= arm_def(v.armor_id)) armor = true;
                if (Chassis[u.chassis_id].triad == Chassis[v.chassis_id].triad &&
                    Chassis[u.chassis_id].speed <= Chassis[v.chassis_id].speed)
                    chassis = true;
            }
            if (weapon && armor && chassis) u.unit_flags |= 0x104;
        }
    }
    if (is_human(faction_id)) {
        if (!MultiplayerActive && !(GameMorePreferences & 0x100000)) return;
        // Human players with automatic obsolescence: designs dominated by another become obsolete;
        // the player may replace them in the build queues.
        for (int32_t i = 0; i < 128; i++) {
            int32_t u_id = i % 64;
            if (i < 64) {
                if (!has_tech(Units[u_id].preq_tech, faction_id)) continue;
            } else {
                u_id += faction_id * 64;
            }
            const UNIT& u = Units[u_id];
            if ((u.obsolete_factions & mask) || !(u.unit_flags & kUnitActive)) continue;
            for (int32_t k = 0; k < 128; k++) {
                int32_t v_id = k % 64 + (k >= 64 ? faction_id * 64 : 0);
                if (k == i) continue;
                UNIT& v = Units[v_id];
                uint32_t vflags = v.unit_flags;
                if (!(vflags & kUnitActive) || (v.obsolete_factions & mask)) continue;
                if (v_id < 64 && (weap_off(v.weapon_id) < 0 || v_id == 15)) continue;
                if (vflags & 0x200) continue;
                if (!(u_id < 64 || (u.unit_flags & kUnitPrototyped) || (v_id >= 64 && !(vflags & kUnitPrototyped))))
                    continue;
                uint32_t v_flag8 = (vflags & 8) >> 3;
                if (v_flag8 && v.reactor_id >= u.reactor_id) continue;
                if (weap_off(v.weapon_id) < 1 ? v.weapon_id != u.weapon_id
                                              : weap_off(v.weapon_id) > weap_off(u.weapon_id))
                    continue;
                if (arm_def(v.armor_id) < 1 ? v.armor_id != u.armor_id
                                            : prune_arm(v.armor_id, faction_id) * 2 >
                                                  prune_arm(u.armor_id, faction_id) * 2)
                    continue;
                bool same_roles = v.plan == 3 || ((weap_off(v.weapon_id) > 1) == (weap_off(u.weapon_id) > 1) &&
                                                  (arm_def(v.armor_id) > 1) == (arm_def(u.armor_id) > 1));
                // BUG(original): compares the range with unit_id's chassis instead of u's
                if (!(same_roles && v.reactor_id <= u.reactor_id && v.carry_capacity <= u.carry_capacity &&
                      Chassis[v.chassis_id].range == Chassis[Units[unit_id].chassis_id].range))
                    continue;
                if (!(Chassis[v.chassis_id].triad == Chassis[u.chassis_id].triad &&
                      Chassis[v.chassis_id].speed <= Chassis[u.chassis_id].speed))
                    continue;
                uint32_t va = v.ability_flags, ua = u.ability_flags;
                const Faction& fac = Factions[faction_id];
                bool abil_ok =
                    va == ua ||
                    (((ua ^ va) & 0xFFFBD5FC) == 0 &&
                     (v.reactor_id < u.reactor_id || (ua & va) == va ||
                      (u_id >= 64 && fac.units_active[v_id] == 0 && fac.units_queue[v_id] == 0)));
                if (!(abil_ok && v.plan == u.plan)) continue;
                if (!(v_flag8 == 0 || (v.group_id == u.group_id && (ua & va) == va))) continue;
                if (!(u.cost <= v.cost || v.reactor_id < u.reactor_id || (v_id >= 64 && !(v.unit_flags & 8)))) continue;
                if (!(v.weapon_id != u.weapon_id || v.armor_id != u.armor_id || v.chassis_id != u.chassis_id ||
                      v.reactor_id != u.reactor_id || (v_id >= 64 && u_id >= 64)))
                    continue;
                if (v_id == unit_id) {
                    v.unit_flags |= 8;
                    if (net_synch) synch_obs(v_id);
                    continue;
                }
                v.obsolete_factions |= mask;
                if (net_synch) synch_obs(v_id);
                bool declined = false;  // bVar5
                if (v_id >= 64 || has_tech(v.preq_tech, faction_id)) {
                    bool queued = false;
                    for (int32_t b = 0; b < BaseCount && !queued; b++) {
                        if ((uint8_t)Bases[b].faction_id != faction_id) continue;
                        for (int32_t q = 0; q <= Bases[b].queue_size; q++)
                            if (Bases[b].queue_items[q] == v_id) {
                                queued = true;
                                break;
                            }
                    }
                    bool replaced = false;  // bVar7
                    bool next_k = false;
                    for (int32_t b = 0; b < BaseCount; b++) {
                        if ((uint8_t)Bases[b].faction_id != faction_id) continue;
                        bool ask;  // reached the question (LAB_0058038f)
                        if (!MultiplayerActive && faction_id == CurrentPlayerFaction) {
                            if (!queued) continue;
                            if (replaced) {
                                if (net_synch && NetDaemon_lock_base(b, 0, -1, -1) != 0) continue;
                                if (MultiplayerActive) {
                                    ask = false;
                                } else {
                                    ask = true;
                                }
                            } else {
                                ask = true;
                            }
                        } else {
                            if (!queued || (net_synch && NetDaemon_lock_base(b, 0, -1, -1) != 0)) continue;
                            ask = !MultiplayerActive;
                        }
                        if (ask && faction_id == CurrentPlayerFaction && !replaced) {
                            prune_say_design(v_id);
                            parse_says(0, StrBuffer, -1, -1);
                            prune_say_design(u_id);
                            parse_says(1, StrBuffer, -1, -1);
                            int32_t choice;
                            bool check = false;
                            if ((v.unit_flags & 0x200) || declined) {
                                choice = 1;
                                if (!declined) check = true;
                            } else {
                                choice = X_pop("ASKPRODUPGRADE", 0);
                                if (choice < 2)
                                    check = true;
                                else
                                    declined = true;
                            }
                            if (check && ((v.unit_flags & 0x200) || choice == 1)) {
                                v.obsolete_factions &= ~mask;
                                if (v.unit_flags & 8) v.unit_flags |= 0x200;
                                v.unit_flags |= 8;
                                next_k = true;
                                break;
                            }
                        }
                        replaced = true;
                        BASE& base = Bases[b];
                        for (int32_t q = 0; q <= base.queue_size; q++)
                            if (base.queue_items[q] == v_id) base.queue_items[q] = u_id;
                        if (MultiplayerActive && net_synch) NetDaemon_unlock_base(b);
                    }
                    if (next_k) continue;
                    if (BaseCount > 0 && replaced && MultiplayerActive && faction_id == CurrentPlayerFaction) {
                        prune_say_design(v_id);
                        parse_says(0, StrBuffer, -1, -1);
                        prune_say_design(u_id);
                        parse_says(1, StrBuffer, -1, -1);
                        if (MFactions[faction_id].rule_flags & 0x80)
                            popp(ScriptFileName, "PRODUPGRADE", 0, "alunit_sm.pcx", 0);
                        else
                            popp(ScriptFileName, "PRODUPGRADE", 0, "unit_sm.pcx", 0);
                    }
                }
            }
        }
        return;
    }
    // Computer players: designs dominated by a better one of the same group become obsolete.
    for (int32_t j = 0; j < 64; j++) {
        int32_t u_id = faction_id * 64 + j;
        const UNIT& u = Units[u_id];
        if (!(u.unit_flags & kUnitActive) || (u.obsolete_factions & mask)) continue;
        bool check_basic;
        if (u_id < 64) {
            if (!has_tech(u.preq_tech, faction_id)) continue;
            check_basic = true;
        } else {
            check_basic = (u.unit_flags & kUnitPrototyped) != 0;
        }
        bool prototyped = check_basic;  // bVar5
        if (check_basic) {
            for (int32_t v_id = 0; v_id < 64; v_id++) {
                UNIT& v = Units[v_id];
                if (!(v.unit_flags & kUnitActive) || v_id == u_id || weap_off(v.weapon_id) < 0 || v_id == 15 ||
                    v.group_id != u.group_id)
                    continue;
                if (Chassis[v.chassis_id].triad != Chassis[u.chassis_id].triad || v.plan != u.plan) continue;
                bool obsolete = false;
                if (v.reactor_id < u.reactor_id && weap_off(v.weapon_id) >= 0) {
                    obsolete = true;
                } else if (Chassis[v.chassis_id].range == Chassis[u.chassis_id].range &&
                           (weap_off(v.weapon_id) < 1 ? v.weapon_id == u.weapon_id
                                                      : weap_off(v.weapon_id) <= weap_off(u.weapon_id)) &&
                           v.carry_capacity <= u.carry_capacity &&
                           (arm_def(v.armor_id) < 1 ? v.armor_id == u.armor_id
                                                    : prune_arm(v.armor_id, faction_id) * 2 <=
                                                          prune_arm(u.armor_id, faction_id) * 2) &&
                           Chassis[v.chassis_id].speed <= Chassis[u.chassis_id].speed &&
                           bit_count(v.ability_flags & u.ability_flags) >= bit_count(v.ability_flags) &&
                           ((v.ability_flags ^ u.ability_flags) & 0x408418) == 0) {
                    obsolete = true;
                }
                if (obsolete) {
                    v.obsolete_factions |= mask;
                    if (net_synch) synch_obs(v_id);
                }
            }
        }
        for (int32_t k = 0; k < 64; k++) {
            int32_t v_id = faction_id * 64 + k;
            UNIT& v = Units[v_id];
            if (!(v.unit_flags & kUnitActive) || v_id == u_id || v.group_id != u.group_id) continue;
            if (Chassis[v.chassis_id].triad != Chassis[u.chassis_id].triad || v.plan != u.plan) continue;
            if (!prototyped && (v.unit_flags & kUnitPrototyped)) continue;
            bool obsolete = false;
            if (v.reactor_id < u.reactor_id) {
                obsolete = true;
            } else if (v.carry_capacity <= u.carry_capacity &&
                       Chassis[v.chassis_id].range == Chassis[u.chassis_id].range &&
                       (weap_off(v.weapon_id) < 1 ? v.weapon_id == u.weapon_id
                                                  : weap_off(v.weapon_id) <= weap_off(u.weapon_id)) &&
                       (arm_def(v.armor_id) < 1 ? v.armor_id == u.armor_id
                                                : prune_arm(v.armor_id, faction_id) * 2 <=
                                                      prune_arm(u.armor_id, faction_id) * 2) &&
                       Chassis[v.chassis_id].speed <= Chassis[u.chassis_id].speed) {
                if (!((v.unit_flags & kUnitPrototyped) &&
                      bit_count(u.ability_flags & v.ability_flags) < bit_count(v.ability_flags)) &&
                    ((u.ability_flags ^ v.ability_flags) & 0x408418) == 0)
                    obsolete = true;
            }
            if (obsolete) {
                v.obsolete_factions |= mask;
                if (net_synch) synch_obs(v_id);
            }
        }
    }
}

int32_t GAMEAPI propose_proto(int32_t faction_id, int32_t chassis_id, int32_t weapon_id, int32_t armor_id,
                              uint32_t ability_flags, int32_t reactor_id, int32_t plan, const char* name) {
    uint8_t mask = faction_bit(faction_id);
    Faction& fac = Factions[faction_id];
    if (!DesignSuggestMode) {
        if (is_bunged(faction_id, chassis_id, weapon_id, armor_id, ability_flags, reactor_id)) return -1;
    } else if (DesignSuggestPlan >= 0 && DesignSuggestPlan != plan) {
        return -1;
    }
    if (Chassis[chassis_id].speed == 1 && arm_def(armor_id) == 1 && weap_off(weapon_id) == 1 && !DesignSuggestMode)
        return 2;
    int32_t off_value = weap_strat(weapon_id, faction_id);
    int32_t def_value = arm_strat(armor_id, faction_id);
    int32_t best = -1;
    int32_t best_score = 0;
    for (int32_t i = 0; i < 128; i++) {
        int32_t u_id = proto_slot(i, faction_id);
        UNIT& u = Units[u_id];
        int32_t score;
        int32_t slot = u_id;
        if (u_id >= 64 && (u.unit_flags & kUnitActive) && (u.obsolete_factions & mask) &&
            !fac.units_active[u_id] && !fac.units_queue[u_id]) {
            // obsolete and unused: replaceable
            score = plan != (uint8_t)u.group_id ? 0x1000 : 0x2000;
        } else if (!(u.unit_flags & kUnitActive)) {
            if (u_id < 64) continue;
            score = 0x800;  // empty slot
        } else {
            if (u_id < 64 && !has_tech(u.preq_tech, faction_id)) continue;
            if (u.chassis_id == (uint32_t)chassis_id) {
                if (u.weapon_id == (uint32_t)weapon_id && u.armor_id == (uint32_t)armor_id &&
                    u.ability_flags == ability_flags && u.reactor_id == (uint32_t)reactor_id && !DesignSuggestMode &&
                    is_human(faction_id))
                    return -1;
                if (u.weapon_id == (uint32_t)weapon_id && u.armor_id == (uint32_t)armor_id &&
                    ((u.ability_flags ^ ability_flags) & 0x44AE1B) == 0 &&
                    ((u.ability_flags & ability_flags) == u.ability_flags || !is_human(faction_id)) &&
                    u.reactor_id == (uint32_t)reactor_id) {
                    // an equivalent design exists
                    if (plan == -2) u.unit_flags |= kUnitPrototyped;
                    if (DesignSuggestMode && DesignSuggestMode < 3) {
                        DesignSuggestAbilities = ability_flags;
                        DesignSuggestWeapon = weapon_id;
                        DesignSuggestArmor = armor_id;
                        DesignSuggestChassis = chassis_id;
                        DesignSuggestReactor = reactor_id;
                        DesignSuggestMode = 2;
                    }
                    u.obsolete_factions &= ~mask;
                    return u_id;
                }
            }
            if (u_id < 64) continue;
            if ((u.obsolete_factions & mask) && !fac.units_active[u_id]) {
                score = 0x400;
            } else {
                if (u.chassis_id == (uint32_t)chassis_id && u.reactor_id == (uint32_t)reactor_id &&
                    ((u.ability_flags ^ ability_flags) & 0x48841A) == 0) {
                    int32_t u_off = weap_strat(u.weapon_id, faction_id);
                    int32_t u_def = arm_strat(u.armor_id, faction_id);
                    int32_t top = u_off > u_def ? u_off : u_def;
                    int32_t top2 = top + (u_off == u_def);
                    if (off_value < top) {
                        int32_t d_off = off_value - u_off;
                        if (d_off < 0) d_off = u_off - off_value;
                        if (d_off < 2 && def_value < top2) {
                            int32_t d_def = def_value - u_def;
                            if (d_def < 0) d_def = u_def - def_value;
                            // a similar design that is better at its role exists
                            if (d_def < 2 && !(mask & FactionStatus[0]) &&
                                std::strcmp(MFactions[faction_id].filename, "UNIV") != 0)
                                return u_id;
                        }
                    }
                }
                if ((uint8_t)u.group_id != plan && (fac.units_active[u_id] || fac.units_queue[u_id])) continue;
                int32_t base_score = fac.units_active[u_id] ? 0x40 : 0x240;
                if (u.obsolete_factions & mask) base_score |= 0x100;
                if (!fac.units_queue[u_id]) base_score |= 0x80;
                uint32_t penalty = (uint32_t)u.cost * fac.units_active[u_id] + fac.units_queue[u_id];
                if (penalty > 63) penalty = 63;
                score = base_score - (int32_t)penalty;
            }
        }
        if (best_score < score) {
            best_score = score;
            best = slot;
        }
    }
    if (best < 0) return -1;
    if (DesignSuggestMode) {
        if (DesignSuggestMode < 3) {
            DesignSuggestWeapon = weapon_id;
            DesignSuggestArmor = armor_id;
            DesignSuggestChassis = chassis_id;
            DesignSuggestAbilities = ability_flags;
            DesignSuggestReactor = reactor_id;
            DesignSuggestPlan = plan;
            DesignSuggestMode = 3;
        }
        return -1;
    }
    UNIT& unit = Units[best];
    bool was_free = !(unit.unit_flags & kUnitActive);
    if ((unit.unit_flags & kUnitActive) && !MultiplayerActive) retire_proto(best, faction_id);
    make_proto(best, chassis_id, weapon_id, armor_id, ability_flags, reactor_id);
    if (plan == -2) unit.unit_flags |= kUnitPrototyped;
    if (DiffLevel < 2) unit.unit_flags |= kUnitPrototyped;
    if (was_free) unit.unit_flags |= 0x40;
    unit.unit_flags |= 0x80;
    if (plan >= 0) {
        unit.group_id = (int8_t)plan;
        if (plan == 10) unit.plan = 3;
    }
    if (!name) {
        char buf[256];
        name_proto(buf, best, faction_id, chassis_id, weapon_id, armor_id, ability_flags, reactor_id);
        strncpy_term(unit.name, buf, 32);
        unit.unit_flags &= ~kUnitCustomName;
    } else {
        std::strcpy(unit.name, name);
        for (int32_t i = 0; i < 512; i++) {
            if (i != best && (i / 64 == 0 || i / 64 == faction_id) && !std::strcmp(unit.name, Units[i].name)) {
                std::strcat(unit.name, "*");
                i = -1;
            }
        }
    }
    prune_protos(faction_id, best, 0);
    // replace obsolete designs of the same group in the faction's build queues
    for (int32_t b = 0; b < BaseCount; b++) {
        BASE& base = Bases[b];
        if ((uint8_t)base.faction_id != faction_id) continue;
        if (base.queue_size < 0) continue;
        int32_t q = 0;
        do {
            int32_t item = base.queue_items[q];
            if (item >= 0 && (Units[item].obsolete_factions & mask) && Units[item].group_id == unit.group_id)
                base.queue_items[q] = best;
            q++;
        } while (q <= base.queue_size);
    }
    if (dword_68F0F0 < 0) dword_68F0F0 = best;
    return best;
}

int32_t GAMEAPI abil_index(uint32_t ability_flag) {
    int32_t i = 0;
    for (int32_t f = (int32_t)ability_flag; !(f & 1); f >>= 1) i++;
    return i;
}

void GAMEAPI add_abil(uint32_t* ability_flags, uint32_t ability_flag, int32_t faction_id) {
    if (*ability_flags) return;
    int32_t index = abil_index(ability_flag);
    if (!has_tech(abil_preq(index), faction_id)) return;
    const MFaction& m = MFactions[faction_id];
    for (int32_t k = 0; k < m.faction_bonus_count; k++) {
        // the faction gets it for free
        if (m.faction_bonus_id[k] == 15 && has_tech(abil_preq(m.faction_bonus_val1[k]), faction_id) &&
            index == m.faction_bonus_val1[k])
            return;
    }
    *ability_flags |= 1u << (index & 31);
}

void GAMEAPI consider_designs(int32_t faction_id) {
    const int32_t f = faction_id;
    Faction& fac = Factions[f];
    const MFaction& mf = MFactions[f];
    const bool human = is_human(f);
    dword_68F0F0 = -1;
    // hostility (war_score): vendettas and atrocity victims
    int32_t war_score = 0;
    for (int32_t i = 1; i < 8; i++) {
        if (i == f) continue;
        uint32_t status = fac.diplo_status[i];
        if (!(status & 0x40000)) {
            if (status & 0x20) {
                if (is_human(i) &&
                    (fac.ranking < Factions[i].ranking || fac.diplo_combat_count[i] < Factions[i].diplo_combat_count[f]))
                    war_score += (GameRules >> 6) & 1;
                war_score += (Factions[i].integrity_blemishes - 1) / 4;
            }
        } else {
            war_score += (Factions[i].atrocities + 2) / 3;
        }
    }
    enemy_capabilities(f);
    const int32_t reactor = best_reactor(f);
    const int32_t rpow = 2 << (reactor & 31);
    // weapon / armor upgrades for existing designs
    int32_t new_weapon = -1;  // local_18
    int32_t new_armor = -1;   // local_8
    for (int32_t w = 0; w < 26; w++) {
        int32_t off = weap_off(w);
        if (!has_tech(Weapon[w].preq_tech, f) || off <= 0 || off >= 99 || w == 23) continue;
        if (new_weapon >= 0 && weap_off(new_weapon) > off) continue;
        for (int32_t i = 0; i < 128; i++) {
            int32_t u = i % 64 + (i >= 64 ? f * 64 : 0);
            const UNIT& unit = Units[u];
            if ((unit.unit_flags & kUnitActive) && !(unit.obsolete_factions & faction_bit(f)) &&
                (u >= 64 || has_tech(unit.preq_tech, f)) && !Chassis[unit.chassis_id].missile) {
                if (off <= weap_off(unit.weapon_id)) break;
                new_weapon = w;
            }
        }
    }
    int32_t scratch = 0;  // local_14: loop counter, reused below (the original reads it later)
    for (scratch = 0; scratch < 14; scratch++) {
        int32_t def = arm_def(scratch);
        if (!has_tech(Armor[scratch].preq_tech, f) || def <= 1) continue;
        if (new_armor >= 0 && arm_def(new_armor) > def) continue;
        for (int32_t i = 0; i < 128; i++) {
            int32_t u = i % 64 + (i >= 64 ? f * 64 : 0);
            const UNIT& unit = Units[u];
            if ((unit.unit_flags & kUnitActive) && !(unit.obsolete_factions & faction_bit(f)) &&
                (u >= 64 || has_tech(unit.preq_tech, f))) {
                if (def <= arm_def(unit.armor_id)) break;
                new_armor = scratch;
            }
        }
    }
    if (new_weapon >= 0 || new_armor >= 0) {
        for (int32_t j = 0; j < 64; j++) {
            const UNIT& unit = Units[f * 64 + j];
            if (!(unit.unit_flags & kUnitActive) || (unit.obsolete_factions & faction_bit(f)) ||
                Chassis[unit.chassis_id].missile)
                continue;
            int32_t chassis = unit.chassis_id;
            int32_t weapon = new_weapon;
            int32_t armor = new_armor;
            switch ((uint8_t)unit.plan) {
            case 0:
            case 4:
                if (new_weapon < 0) continue;
                armor = unit.armor_id;
                break;
            case 1:
                if (new_weapon < 0) {
                    scratch = 2 - (reactor != 1);
                    weapon = unit.weapon_id;
                    int32_t off = weap_off(weapon);
                    int32_t lim = (int32_t)(scratch * rpow * 4) / off;
                    if (lim < 2 || off < 2)
                        off = 2;
                    else if (lim <= off)
                        off = lim;
                    if (off < arm_def(new_armor)) continue;
                } else {
                    armor = unit.armor_id;
                    if (new_armor >= 0) {
                        scratch = 2 - (reactor != 1);
                        int32_t off = weap_off(new_weapon);
                        int32_t lim = (int32_t)(scratch * rpow * 4) / off;
                        if (lim < 2 || off < 2)
                            lim = 2;
                        else if (off < lim)
                            lim = off;
                        if (arm_def(new_armor) <= lim) armor = new_armor;
                    }
                }
                break;
            case 2:
                if (new_armor < 0) continue;
                weapon = unit.weapon_id;
                break;
            case 6: {
                armor = unit.armor_id;
                weapon = unit.weapon_id;
                int32_t margin = reactor * reactor > 4 ? reactor * reactor : 5;
                if (new_weapon >= 0) {
                    if (proto_cost(chassis, new_weapon, armor, unit.ability_flags, reactor) <=
                        fac.AI_power + fac.AI_growth + margin)
                        weapon = new_weapon;
                }
                bool upgrade_armor = false;
                if (new_armor >= 0) {
                    if (proto_cost(chassis, weapon, new_armor, unit.ability_flags, reactor) <=
                        fac.AI_power + fac.AI_growth + margin)
                        upgrade_armor = true;
                }
                if (upgrade_armor)
                    armor = new_armor;
                else if (armor != new_armor && weapon != new_weapon)
                    continue;
                break;
            }
            default:
                continue;
            }
            propose_proto(f, chassis, weapon, armor, unit.ability_flags, reactor, (uint8_t)unit.group_id, nullptr);
        }
    }
    // threats from human players: 1 psi (or Xenoempathy Dome), 2 Hunter-Seeker-like project
    uint32_t threats = 0;  // local_1c
    if (mf.rule_psi < 1 && !human) {
        for (int32_t i = 1; i < 8; i++) {
            if (!is_human(i)) continue;
            if (!great_satan(i, 0)) {
                uint32_t war = (fac.diff_level < 4 && !(GameRules & 0x40)) ? 0 : 0x10;
                if (!(fac.diplo_status[i] & (war | 0x20))) continue;
            }
            if (MFactions[i].rule_psi > 0 ||
                (SecretProjects[19] >= 0 && (uint8_t)Bases[SecretProjects[19]].faction_id == i))
                threats |= 1;
            if (MFactions[i].rule_psi > 0 ||
                (SecretProjects[9] >= 0 && (uint8_t)Bases[SecretProjects[9]].faction_id == i))
                threats |= 2;
        }
    }
    uint32_t abil;
    // ---- defensive infantry (plan 2), trance (0x12), police (0x14) ----
    int32_t chassis = pick_chassis(f, 0, 1);
    if (chassis >= 0) {
        int32_t armor = armor_budget(f, 99);
        abil = 0;
        add_abil(&abil, 0x200, f);
        if (fac.AI_fight < 0 || (fac.SE_morale_pending < 0 && !human) || mf.rule_morale < 0) add_abil(&abil, 0x4000, f);
        if (!human && (fac.enemy_best_weapon_value * 2 <= fac.enemy_best_psi_land_offense || (threats & 1)))
            add_abil(&abil, 0x40000, f);
        if (has_tech(abil_preq(18), f) && (abil == 0 || two_abils(f)) && !human &&
            (threats != 0 || (fac.clean_minerals_modifier > 3 && fac.SE_morale_pending < 1)))
            abil |= 0x40000;
        int32_t weapon = weapon_budget(f, 1, 0);
        for (int32_t cost = 2;; cost++) {
            int32_t next = weapon_budget(f, cost, 0);
            if (next == weapon) break;
            if (proto_cost(chassis, weapon, armor, abil, reactor) < proto_cost(chassis, next, armor, abil, reactor))
                break;
            weapon = next;
        }
        if (arm_def(armor) > 1 || DesignSuggestMode) propose_proto(f, chassis, weapon, armor, abil, reactor, 2, nullptr);
        if (has_tech(abil_preq(18), f) && !(abil & 0x40000) && !human) {
            bool trance = !(fac.clean_minerals_modifier < 4 || fac.SE_morale_pending > 0);
            if (trance || threats) {
                abil = 0x40000;
                propose_proto(f, chassis, weapon, armor, 0x40000, reactor, 0x12, nullptr);
            }
        }
        if (has_tech(abil_preq(8), f) && !human && (fac.unk_66 != 0 || fac.unk_68 != 0)) {
            abil = 0x100;
            propose_proto(f, chassis, weapon, armor, 0x100, reactor, 0x14, nullptr);
        }
    }
    // ---- offensive infantry/rovers (plan 3, 0xb, 0xc) ----
    chassis = pick_chassis(f, 0, 1);
    if (chassis >= 0) {
        int32_t div = rpow * 2;
        int32_t budget = 1;
        abil = 0;
        if (2 / div <= fac.AI_power + 5) {
            int32_t k = 2;
            do {
                budget++;
                k++;
            } while ((k + 1) / 2 * k / div <= fac.AI_power + 5);
        }
        int32_t weapon = weapon_budget(f, human ? 99 : budget, 0);
        int32_t lim_off = weap_off(weapon);
        int32_t lim = (int32_t)(scratch * rpow * 4) / lim_off;
        if (lim < 2 || lim_off < 2)
            lim_off = 2;
        else if (lim <= lim_off)
            lim_off = lim;
        if (lim_off <= budget / 2) lim_off = budget / 2;
        if (lim_off < 1)
            lim_off = 1;
        else if (lim_off > 99)
            lim_off = 99;
        int32_t armor = armor_budget(f, lim_off);
        scratch = armor;
        int32_t off_cost = weap_off(weapon);
        int32_t def = arm_def(armor);
        for (int32_t x = (off_cost + 1) * def; x / div <= fac.AI_power + 5; x += def) off_cost++;
        weapon = weapon_budget(f, off_cost, 0);
        if (!human && fac.unk_105 * 2 < fac.unk_66 && fac.unk_66 > 1 && fac.unk_106 < fac.unk_66)
            add_abil(&abil, 0x100, f);
        if (ProposalPassCount[9] < ProposalPassCount[8] && fac.AI_fight + fac.AI_power > 0 && !human)
            add_abil(&abil, 0x100000, f);
        bool deep_radar = true;
        if (!(war_score == 0 || war_score <= 1 - fac.AI_fight || fac.AI_fight < 0)) {
            if (human)
                deep_radar = false;
            else
                add_abil(&abil, 0x100000, f);
        }
        if (deep_radar && !human &&
            ((fac.best_armor_value * 3 / 2 <= fac.enemy_best_psi_land_defense &&
              weap_off(weapon) / 2 < fac.enemy_best_psi_land_defense) ||
             threats != 0))
            add_abil(&abil, 0x800, f);
        if (fac.base_count > 7 && fac.ranking > 5 && (fac.AI_wealth != 0 || fac.AI_tech != 0))
            add_abil(&abil, 0x10000, f);
        if (mf.rule_morale < 0) add_abil(&abil, 0x4000, f);
        bool check_morale;
        if (arm_def(armor) < 2) {
            check_morale = abil == 0;
        } else if (abil == 0) {
            if (fac.AI_tech > 0 && fac.enemy_best_land_speed > 1 &&
                fac.best_armor_value * 2 <= fac.enemy_best_weapon_value) {
                add_abil(&abil, 0x200, f);
                check_morale = abil == 0;
            } else {
                check_morale = true;
            }
        } else {
            check_morale = false;
        }
        if (check_morale && (fac.AI_fight < 0 || (fac.SE_morale_pending < 0 && !human))) add_abil(&abil, 0x4000, f);
        if (has_tech(abil_preq(10), f) && (abil == 0 || two_abils(f))) abil |= 0x400;
        if ((weap_off(weapon) > 1 && arm_def(armor) > 1) || weap_off(weapon) > 2 || DesignSuggestMode) {
            propose_proto(f, chassis, weapon, armor, abil, reactor, 3, nullptr);
            abil &= ~0x400u;
            if (!two_abils(f)) abil = 0;
            if (has_tech(abil_preq(4), f)) propose_proto(f, chassis, weapon, armor, abil | 0x10, reactor, 0xB, nullptr);
            if (has_tech(abil_preq(3), f)) propose_proto(f, chassis, weapon, armor, abil | 8, reactor, 0xC, nullptr);
        }
    }
    // ---- garrison with comm jammer-like ability 15 (plan 1) ----
    {
        int32_t weapon = weapon_budget(f, 99, 0);
        int32_t ch;
        if (has_tech(abil_preq(15), f) && (ch = pick_chassis(f, 0, 1)) >= 0) {
            abil = 0;
            int32_t armor = armor_budget(f, 1);
            int32_t armor2 = 0;  // local_24
            for (int32_t round = 0; round < 100; round++) {
                bool faster = false;
                int32_t alt;
                if (Chassis[ch].speed == 1 && (alt = pick_chassis(f, 0, 2)) >= 0 && alt != ch) {
                    int32_t cost_alt = proto_cost(alt, weapon, armor, abil, reactor);
                    bool take;
                    if (cost_alt > reactor + 5)
                        take = proto_cost(ch, weapon, armor, abil, reactor) >= proto_cost(alt, weapon, armor, abil, reactor);
                    else
                        take = (int32_t)(proto_cost(ch, weapon, armor, abil, reactor) + 1 + (f & 1)) >=
                               proto_cost(alt, weapon, armor, abil, reactor);
                    if (take) {
                        faster = true;
                        ch = alt;
                    }
                }
                armor2 = armor_budget(f, arm_def(armor) + 1);
                if (arm_def(armor) < arm_def(armor2) &&
                    proto_cost(ch, weapon, armor, abil, reactor) >= proto_cost(ch, weapon, armor2, abil, reactor)) {
                    armor = armor2;
                } else if (!faster) {
                    break;
                }
            }
            if (!has_tech(abil_preq(15), f) || two_abils(f)) {
                add_abil(&abil, 0x20000, f);
                add_abil(&abil, 0x400, f);
                add_abil(&abil, 2, f);
                // (the original also computes an unused proto_cost here)
            }
            if (has_tech(abil_preq(15), f)) abil |= 0x8000;
            if (!(weap_off(weapon) < arm_def(armor) * 3 && !has_tech(abil_preq(15), f) && !DesignSuggestMode))
                propose_proto(f, ch, weapon, armor, abil, reactor, 1, nullptr);
        }
    }
    // ---- fast attackers (plan 4) ----
    int32_t ch4 = pick_chassis(f, 0, 3);
    if (ch4 >= 0 || (ch4 = pick_chassis(f, 0, 2)) >= 0) {
        abil = 0;
        int32_t armor = armor_budget(f, 1);
        int32_t budget = 0;
        int32_t k = 2;
        do {
            k += 2;
            budget++;
        } while (k / rpow <= fac.AI_power + reactor * 2 + 3 + fac.AI_fight);
        if (human) budget = 99;
        int32_t weapon = weapon_budget(f, budget, 0);
        for (int32_t round = 0;;) {
            int32_t def = arm_def(armor);
            if (weap_off(weapon) - 1 <= def) break;
            def++;
            int32_t cost = weap_off(weapon) / 2;
            if (cost <= def) cost = def;
            int32_t better = armor_budget(f, cost);
            if (arm_def(better) <= arm_def(armor) ||
                proto_cost(ch4, weapon, better, abil, reactor) > fac.AI_power + fac.AI_fight + 3 + reactor)
                break;
            armor = better;
            round++;
            if (round > 99) break;
        }
        if (war_score != 0 && 3 - fac.AI_fight < war_score && fac.AI_fight >= 0 && !human) add_abil(&abil, 0x100000, f);
        bool radar = true;
        if (ProposalPassCount[9] < ProposalPassCount[8] && fac.AI_power + fac.AI_fight > 0) {
            if (!human)
                add_abil(&abil, 0x100000, f);
            else
                radar = false;
        }
        if (radar && !human && (threats & 2)) add_abil(&abil, 0x800, f);
        if (mf.rule_morale < 0 || (fac.AI_fight > 0 && fac.SE_industry_pending >= 0) ||
            (fac.SE_morale_pending < 0 && !human))
            add_abil(&abil, 0x4000, f);
        if (has_tech(abil_preq(10), f) && (abil == 0 || two_abils(f))) abil |= 0x400;
        propose_proto(f, ch4, weapon, armor, abil, reactor, 4, nullptr);
    }
    // ---- probe / scout (plan 10, 19 with trance) ----
    abil = 0;
    int32_t ch10 = pick_chassis(f, 0, 3);
    bool skip10 = false;
    if (ch10 < 0 && (ch10 = pick_chassis(f, 0, 2)) < 0) {
        ch10 = pick_chassis(f, 0, 1);
        add_abil(&abil, 0x400, f);
        if (ch10 < 0) skip10 = true;
    }
    if (!skip10) {
        int32_t w2 = weapon_budget(f, 2, 0);
        if (weap_off(w2) < 2 && !DesignSuggestMode && (!has_tech(abil_preq(11), f) || human)) skip10 = true;
    }
    if (!skip10) {
        int32_t weapon = weapon_budget(f, 1, 0);
        int32_t armor = armor_budget(f, 1);
        if (!has_tech(abil_preq(11), f) || human) {
            if (abil == 0) {
                add_abil(&abil, 2, f);
                if (abil == 0) add_abil(&abil, 4, f);
            }
            if (has_tech(abil_preq(10), f)) {
                if (!two_abils(f))
                    abil = 0x400;
                else
                    abil |= 0x400;
            }
        } else {
            abil |= 0x800;
            if (two_abils(f)) {
                if (!has_tech(abil_preq(10), f)) {
                    if (has_tech(abil_preq(14), f)) abil |= 0x4000;
                } else {
                    abil |= 0x400;
                }
            }
        }
        propose_proto(f, ch10, weapon, armor, abil, reactor, (abil & 0x40000) ? 19 : 10, nullptr);
    }
    // ---- naval combat (plan 6, 0x15) ----
    int32_t sea = pick_chassis(f, 1, 0);
    if (sea >= 0) {
        abil = 0;
        if (fac.unk_106 < fac.unk_66 && fac.unk_105 < fac.unk_66 && !human) add_abil(&abil, 0x100, f);
        add_abil(&abil, 2, f);
        add_abil(&abil, 0x100, f);
        if (threats & 1) {
            if (has_tech(abil_preq(18), f) && (abil == 0 || two_abils(f))) abil |= 0x40000;
        }
        if (has_tech(abil_preq(8), f) && !(abil & 0x100) && bit_count(abil) < 2 && (abil == 0 || two_abils(f)))
            abil |= 0x100;
        if (has_tech(abil_preq(1), f) && !(abil & 2) && bit_count(abil) < 2 && (abil == 0 || two_abils(f)))
            abil |= 2;
        int32_t weapon = weapon_budget(f, 1, 0);
        int32_t armor = armor_budget(f, 1);
        for (int32_t round = 0; round < 100; round++) {
            bool upgraded = false;
            int32_t next = weapon;
            for (int32_t k = 1; k < 4 && next == weapon; k++) next = weapon_budget(f, weap_off(weapon) + k, 0);
            if (next != weapon) {
                int32_t margin = reactor * 2 - reactor / 2;
                if (margin < 5) margin = 5;
                if (proto_cost(sea, next, armor, 0, reactor) <= fac.AI_power + fac.AI_growth + margin) {
                    upgraded = true;
                    weapon = next;
                }
            }
            int32_t next_armor = armor;
            for (int32_t k = 1; k < 4 && next_armor == armor; k++) next_armor = armor_budget(f, arm_def(armor) + k);
            int32_t result = armor;
            if (next_armor != armor && arm_def(next_armor) > arm_def(armor)) {
                int32_t margin = reactor * 2 - reactor / 2;
                if (margin < 5) margin = 5;
                if (proto_cost(sea, weapon, next_armor, abil, reactor) <= fac.AI_power + fac.AI_growth + margin) {
                    armor = next_armor;
                    continue;
                }
            }
            (void)result;
            if (!upgraded) break;
        }
        if (human) weapon = weapon_budget(f, 99, 0);
        if (weap_off(weapon) > 0) propose_proto(f, sea, weapon, armor, abil, reactor, 6, nullptr);
        if (!human) {
            int32_t weapon2 = weapon_budget(f, 1, 0);
            int32_t armor2 = armor_budget(f, 99);
            abil = 0;
            add_abil(&abil, 0x100, f);
            if (has_tech(abil_preq(1), f) && two_abils(f)) abil |= 2;
            if (arm_def(armor2) > 1) propose_proto(f, sea, weapon2, armor2, abil, reactor, 0x15, nullptr);
        }
    }
    // ---- naval transport (7), sea former (0xF), sea probe (0x10) ----
    int32_t sea2 = pick_chassis(f, 1, 0);
    if (sea2 >= 0) {
        abil = 0;
        int32_t armor = armor_budget(f, 1);
        int32_t weapon = weapon_budget(f, -7, 0);
        if (fac.unk_106 < fac.unk_66 && fac.unk_105 < fac.unk_66) armor = armor_budget(f, 99);
        propose_proto(f, sea2, weapon, armor, abil, reactor, 7, nullptr);
        abil = 0;
        if (sea2 != 3 && has_tech(Chassis[3].preq_tech, f)) sea2 = 3;
        armor = armor_budget(f, 1);
        weapon = weapon_budget(f, -9, 0);
        add_abil(&abil, 1, f);
        add_abil(&abil, 0x2000, f);
        if (abil == 1 && has_tech(abil_preq(13), f) && two_abils(f)) abil |= 0x2000;
        if (weapon >= 0) propose_proto(f, sea2, weapon, armor, abil, reactor, 0xF, nullptr);
        abil = 0;
        weapon = weapon_budget(f, -8, 0);
        if (weapon >= 0) propose_proto(f, sea2, weapon, armor, abil, reactor, 0x10, nullptr);
    }
    // ---- air (8, 9) and short-range air (0x11) ----
    for (int32_t pass = 0; pass < 2; pass++) {
        int32_t air = pick_chassis(f, 2, pass ? -2 : 0);
        if (air < 0) continue;
        int32_t weapon = weapon_budget(f, 99, 0);
        int32_t armor = armor_budget(f, 1);
        abil = 0;
        if (fac.ranking > 5 && (fac.AI_wealth != 0 || fac.AI_tech != 0) && (fac.AI_power == 0 || fac.AI_fight < 1))
            add_abil(&abil, 0x10000, f);
        if (threats & 2) add_abil(&abil, 0x800, f);
        if (war_score != 0 && (pass ? 4 : 5) - fac.AI_fight < war_score && fac.AI_fight >= 0 && !human)
            add_abil(&abil, 0x100000, f);
        if (ProposalPassCount[9] < ProposalPassCount[8] && fac.AI_fight + fac.AI_power > 0 && !human)
            add_abil(&abil, 0x100000, f);
        if (has_tech(abil_preq(1), f) && (abil == 0 || two_abils(f))) abil |= 2;
        if (pass) {
            propose_proto(f, air, weapon, armor, abil, reactor, 0x11, nullptr);
            continue;
        }
        propose_proto(f, air, weapon, armor, abil, reactor, 8, nullptr);
        weapon = weapon_budget(f, 99, 0);
        abil = 0x20;
        if (!has_tech(abil_preq(1), f) || !two_abils(f)) {
            if ((threats & 1) && has_tech(abil_preq(18), f) && two_abils(f)) abil |= 0x40000;
        } else {
            abil |= 2;
        }
        propose_proto(f, air, weapon, armor, abil, reactor, 9, nullptr);
    }
    // ---- missiles (0xD, 0x16, 0x17, 0xE) ----
    int32_t missile = pick_chassis(f, 2, -1);
    if (missile >= 0) {
        int32_t armor = armor_budget(f, 1);
        if (has_tech(Weapon[23].preq_tech, f)) propose_proto(f, missile, 23, armor, 0, reactor, 0xD, nullptr);
        if (has_tech(Weapon[24].preq_tech, f)) propose_proto(f, missile, 24, armor, 0, reactor, 0x16, nullptr);
        if (has_tech(Weapon[25].preq_tech, f)) propose_proto(f, missile, 25, armor, 0, reactor, 0x17, nullptr);
        for (int32_t w = 0; w < 26; w++) {
            if (has_tech(Weapon[w].preq_tech, f) && weap_off(w) > 98) {
                propose_proto(f, missile, w, armor, 0, reactor, 0xE, nullptr);
                break;
            }
        }
    }
    // ---- terraformers (weapon 18, plan -1) ----
    abil = 0;
    if (reactor < 2 && !has_tech(abil_preq(0), f)) {
        int32_t ch;
        if (has_tech(abil_preq(13), f) && (ch = pick_chassis(f, 0, 1)) >= 0) {
            int32_t armor = armor_budget(f, 1);
            propose_proto(f, ch, 18, armor, 0x2000, reactor, -1, nullptr);
        }
    } else {
        int32_t ch = pick_chassis(f, 0, 2);
        if (ch >= 0) {
            int32_t armor = armor_budget(f, 1);
            if (has_tech(abil_preq(0), f)) abil |= 1;
            if (has_tech(abil_preq(13), f) && (abil == 0 || two_abils(f))) abil |= 0x2000;
            propose_proto(f, ch, 18, armor, abil, reactor, -1, nullptr);
        }
    }
}

int32_t GAMEAPI design_new_veh(int32_t faction_id, int32_t unit_id) {
    return DesignWin_exec(&DesignWin, faction_id, unit_id);
}

int32_t GAMEAPI abil_cond(int32_t ability_id, int32_t chassis_id, int32_t weapon_id, int32_t armor_id) {
    const CChassis& ch = Chassis[chassis_id];
    if (ch.missile) return 0;
    uint32_t flags = (uint32_t)Ability[ability_id].flags;
    if (ch.triad == 0) {
        if (!(flags & 1)) return 0;
    } else if (ch.triad == 1) {
        if (!(flags & 2)) return 0;
    } else if (ch.triad == 2) {
        if (!(flags & 4)) return 0;
    }
    uint8_t mode = Weapon[weapon_id].mode;
    if (weap_off(weapon_id) == 0) {
        if (mode == 9) {
            if (!(flags & 0x10)) return 0;
        } else if (!(flags & 0x20)) {
            return 0;
        }
    } else if (!(flags & 8)) {
        return 0;
    }
    if (mode == 11) {
        if (flags & 0x40) return 0;
    } else if (flags & 0x800) {
        return 0;
    }
    if (!ExpansionEnabled) {
        uint32_t bit = 1u << (ability_id & 31);
        if (bit == 0x1000000 || bit == 0x2000000 || bit == 0x4000000 || bit == 0x8000000 || bit == 0x10000000) return 0;
    }
    if ((weap_off(weapon_id) < 0 || arm_def(armor_id) < 0) && (flags & 0x80)) return 0;
    if ((flags & 0x100) && mode != 7) return 0;
    if ((flags & 0x200) && ch.speed > 1) return 0;
    return 1;
}

void GAMEAPI strncpy_term(char* dst, const char* src, uint32_t n) {
    std::strncpy(dst, src, n);
    dst[n - 1] = 0;
}
