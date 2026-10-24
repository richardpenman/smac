// Prototypes (see proto.h).
#include "engine/proto.h"

#include "engine/design.h"
#include "engine/map.h"
#include "engine/social.h"
#include "engine/tech.h"
#include "engine/veh.h"

int32_t GAMEAPI proto_cost(int32_t chassis_id, int32_t weapon_id, int32_t armor_id, uint32_t ability_flags,
                           int32_t reactor_id) {
    const int32_t weapon_cost0 = Weapon[weapon_id].cost;
    int32_t weapon_cost = weapon_cost0;
    const uint8_t armor_cost0 = Armor[armor_id].cost;
    int32_t armor_cost = armor_cost0;
    int32_t chassis_cost = Chassis[chassis_id].cost;
    int32_t ability_cost = 0;
    if (ability_flags) {
        for (int32_t i = 0; i < 29; i++) {
            if (!(ability_flags & (1u << i))) continue;
            if (ability_cost) ability_cost++;  // each further ability costs one more
            int32_t cost = Ability[i].cost;
            if (cost >= 0) {
                ability_cost += cost;
                continue;
            }
            switch (cost) {
            case -7:
                ability_cost += chassis_cost + armor_cost - 2;
                break;
            case -6:
                ability_cost += chassis_cost + weapon_cost - 2;
                break;
            case -5:
                ability_cost += armor_cost + weapon_cost - 2;
                break;
            case -4:
                ability_cost += chassis_cost - 1;
                break;
            case -3:
                ability_cost += armor_cost - 1;
                break;
            case -2:
                ability_cost += weapon_cost - 1;
                break;
            case -1: {
                int32_t ratio = weapon_cost / armor_cost;
                ability_cost += ratio < 0 ? 0 : ratio > 2 ? 2 : ratio;
                break;
            }
            default:
                break;
            }
        }
    }
    const uint8_t triad = Chassis[chassis_id].triad;
    if (triad == 1) {  // sea
        armor_cost /= 2;
        chassis_cost += reactor_id;
    } else if (triad == 2) {  // air
        chassis_cost += reactor_id * 2;
        if (armor_cost > 1) armor_cost = armor_cost * reactor_id * 2;
    } else {
        for (int32_t i = 0; i < 29; i++)
            if ((ability_flags & (1u << i)) && (Ability[i].flags & 0x400)) ability_cost++;
    }
    if (Chassis[chassis_id].missile && Weapon[weapon_id].offense_value >= 99) return weapon_cost0;
    if (armor_cost / 2 + 1 > weapon_cost) weapon_cost = armor_cost / 2 + 1;
    int32_t scale = 2 << (reactor_id & 31);
    int32_t cost = (scale / 2 + (chassis_cost + armor_cost) * weapon_cost) / scale;
    if (chassis_cost == 1) cost = cost / 2 + 1;
    if (Weapon[weapon_id].cost > 1 && armor_cost0 > 1) cost++;
    if (triad == 0 && Weapon[weapon_id].cost > 1 && armor_cost0 > 1 && chassis_cost > 1) cost++;
    if (triad == 1) {
        if (Weapon[weapon_id].mode != 11) cost = (cost + 1) / 2;
    } else if (triad == 2) {
        cost /= Weapon[weapon_id].mode > 2 ? 2 : 4;
    }
    int32_t minimum = reactor_id * 2 - reactor_id / 2;
    if (cost <= minimum) cost = minimum;
    if (weapon_cost == 1 && armor_cost == 1 && chassis_cost == 1 && reactor_id == 1) cost = 1;
    return ((ability_cost + 4) * cost + 2) >> 2;
}

void GAMEAPI make_proto(int32_t unit_id, int32_t chassis_id, int32_t weapon_id, int32_t armor_id,
                        uint32_t ability_flags, int32_t reactor_id) {
    int32_t better_weapon = 0, better_armor = 0, better_chassis = 0, prototyped = 0;
    if (unit_id >= 64) {
        const int32_t faction_id = unit_id / 64;
        int32_t id = 0;
        for (int32_t i = 0; i < 128; i++) {
            // basic units 0-63, then the faction's own slots
            id = i % 64;
            if (id != i) id += unit_id - unit_id % 64;
            UNIT& u = Units[id];
            if (!(u.unit_flags & 1)) continue;
            if (id < 64) {
                if (!has_tech(u.preq_tech, faction_id)) continue;
            } else if (!(u.unit_flags & 4)) {
                continue;
            }
            if (weapon_id == u.weapon_id) better_weapon = 1;
            if (armor_id == u.armor_id) better_armor = 1;
            if (chassis_id == u.chassis_id) better_chassis = 1;
            int8_t offense = Weapon[weapon_id].offense_value;
            if (offense > 0 && Weapon[u.weapon_id].offense_value >= offense) better_weapon = 1;
            int8_t defense = Armor[armor_id].defense_value;
            if (defense > 0 && Armor[u.armor_id].defense_value >= defense) better_armor = 1;
            if (Chassis[chassis_id].triad == Chassis[u.chassis_id].triad &&
                Chassis[u.chassis_id].speed >= Chassis[chassis_id].speed)
                better_chassis = 1;
        }
        if (better_weapon && better_armor && better_chassis) {
            prototyped = 1;
            // BUG(original): tests the last unit of the loop, not the matching one
            if (id >= 64 && (Units[id].unit_flags & 0x10)) prototyped = 3;
        }
    }
    UNIT& u = Units[unit_id];
    u.reactor_id = (uint8_t)reactor_id;
    u.chassis_id = (uint8_t)chassis_id;
    u.weapon_id = (uint8_t)weapon_id;
    u.armor_id = (uint8_t)armor_id;
    u.ability_flags = ability_flags;
    u.cost = (uint8_t)proto_cost(chassis_id, weapon_id, armor_id, ability_flags, reactor_id);
    u.carry_capacity = Weapon[weapon_id].mode == 7 ? (uint8_t)transport_val(chassis_id, ability_flags, reactor_id) : 0;
    if (Chassis[chassis_id].missile) {
        if (Weapon[weapon_id].offense_value >= 99) {
            u.plan = 5;
            u.group_id = 14;
        } else if (weapon_id == 24) {
            u.plan = 13;
            u.group_id = 22;
        } else if (weapon_id == 25) {
            u.plan = 14;
            u.group_id = 23;
        } else {
            u.plan = 0;
            u.group_id = 13;
        }
    } else if (Weapon[weapon_id].mode >= 7) {
        u.plan = (int8_t)Weapon[weapon_id].mode;
        u.group_id = (int8_t)(Weapon[weapon_id].mode + 32);
    } else if (Chassis[u.chassis_id].triad == 1) {
        u.plan = 6;
        u.group_id = 6;
    } else if (Chassis[u.chassis_id].triad == 2) {
        if (has_abil(unit_id, 0x20)) {
            u.plan = 4;
            u.group_id = 9;
        } else {
            u.plan = 0;
            u.group_id = 8;
        }
    } else {
        int8_t offense = Weapon[u.weapon_id].offense_value;
        int8_t defense = Armor[u.armor_id].defense_value;
        uint8_t speed = Chassis[u.chassis_id].speed;
        u.plan = 0;
        if (defense > 1) u.plan = 2;
        if (offense >= defense && u.plan == 2) u.plan = 1;
        if (speed > 1 && offense > defense) u.plan = 0;
        if (ability_flags & 0x8018) u.plan = 0;
        u.group_id = 3;
        if (defense > offense) u.group_id = 2;
        if ((int32_t)offense > defense * 2) u.group_id = 1;
        if (speed >= 2) u.group_id = 4;
        if (speed >= 3) u.group_id = 5;
        if (offense == 1 && defense == 1) {
            u.group_id = 10;
            if (speed > 1) u.plan = 3;
            if (has_abil(unit_id, 0x400000)) u.plan = 2;
        }
    }
    u.unit_flags = 1;
    if (prototyped) {
        u.unit_flags = 0x105;
        if (prototyped & 2) u.unit_flags = 0x115;
    }
    u.obsolete_factions = 0;
    if (unit_id < 64) {
        u.combat_factions = 0xFF;
    } else {
        u.combat_factions = (uint8_t)(1 << ((unit_id / 64) & 31));
    }
    u.icon_offset = -1;
}

void GAMEAPI veh_put(int32_t veh_id, int32_t x, int32_t y) { veh_drop(veh_lift(veh_id), x, y); }

int32_t GAMEAPI veh_health(int32_t veh_id) {
    UNIT& u = Units[Vehs[veh_id].unit_id];
    int32_t hp;
    if (u.plan == 12) {
        hp = 1;
    } else {
        int32_t reactor = u.reactor_id < 1 ? 1 : u.reactor_id > 100 ? 100 : u.reactor_id;
        hp = reactor * 10;
    }
    hp -= Vehs[veh_id].damage_taken;
    return hp < 0 ? 0 : hp > 9999 ? 9999 : hp;
}

int32_t GAMEAPI base_cost(int32_t unit_id) {
    UNIT& u = Units[unit_id];
    return proto_cost(u.chassis_id, u.weapon_id, u.armor_id, 0, u.reactor_id);
}

namespace {
// Unit id of entry i of the sort lists: basic units 0-63, then the faction's 64 slots.
int32_t proto_list_id(int32_t i, int32_t faction_id) { return (i < 64 ? 0 : faction_id << 6) + (i & 63); }
}  // namespace

void GAMEAPI proto_sort(int32_t faction_id) {
    int32_t values[128];
    ProtoSortCount = 0;
    for (int32_t i = 0; i < 128; i++) {
        int32_t unit_id = proto_list_id(i, faction_id);
        ProtoSortList[i] = unit_id;
        UNIT& u = Units[unit_id];
        int32_t value;
        if (!(u.unit_flags & 1) || (unit_id < 64 && !has_tech(u.preq_tech, faction_id))) {
            if (unit_id < 64) {
                value = 999999999;
            } else {
                value = 888888888;
                ProtoSortCount++;
            }
        } else {
            uint8_t plan = (uint8_t)u.plan;
            uint8_t triad = Chassis[u.chassis_id].triad;
            int32_t weapon = weap_val(unit_id, faction_id);
            uint8_t speed = Chassis[u.chassis_id].speed;
            int32_t armor = arm_val(u.armor_id, faction_id);
            value = (triad + plan * 16) * 0x40000 + (32 - weapon) * 0x200 + (8 - speed) * 0x40 + (8 - armor);
            ProtoSortCount++;
        }
        values[i] = value;
    }
    sort(128, ProtoSortList, values);
}

void GAMEAPI proto_sort_2(int32_t faction_id) {
    int32_t values[128];
    ProtoSortCount = 0;
    for (int32_t i = 0; i < 128; i++) {
        int32_t unit_id = proto_list_id(i, faction_id);
        ProtoSortList[i] = unit_id;
        UNIT& u = Units[unit_id];
        bool available = (u.unit_flags & 1) &&
                         (unit_id >= 64 || (Weapon[u.weapon_id].offense_value >= 0 && unit_id != 15 &&
                                            has_tech(u.preq_tech, faction_id)));
        int32_t value;
        if (!available) {
            if (unit_id < 64) {
                value = 999999999;
            } else {
                value = unit_id + 0x400;
                ProtoSortCount++;
            }
        } else if (!(u.obsolete_factions & (uint8_t)(1 << (faction_id & 31)))) {
            value = unit_id;
            ProtoSortCount++;
        } else if (unit_id < 64) {
            value = unit_id + 0x2000;
            ProtoSortCount++;
        } else {
            value = unit_id + (Factions[faction_id].units_active[unit_id] ? -0x800 : 0) + 0x1000;
            ProtoSortCount++;
        }
        values[i] = value;
    }
    sort(128, ProtoSortList, values);
}

namespace {
int32_t sign(int32_t v) { return v > 0 ? 1 : v < 0 ? -1 : 0; }
// Horizontal offset from x1 to x2, wrapped to the shorter way round.
int32_t wrap_delta(int32_t x1, int32_t x2) {
    int32_t dx = x2 - x1;
    if (dx < -(MapAreaX / 2)) dx += MapAreaX;
    if (MapAreaX / 2 < dx) dx -= MapAreaX;
    return dx;
}
}  // namespace

int32_t GAMEAPI compass_index(int32_t dx, int32_t dy) {
    int32_t sx = sign(dx), sy = sign(dy);
    for (int32_t i = 0; i <= 8; i++) {
        if (sx == sign(NearOffsetX[i]) && sy == sign(NearOffsetY[i])) return i;
    }
    return -1;
}

int32_t GAMEAPI radius_move(int32_t dx, int32_t dy, int32_t count) {
    for (int32_t i = 0; i < count; i++) {
        if (dx == TableOffsetX[i] && dy == TableOffsetY[i]) return i;
    }
    return -1;
}

int32_t GAMEAPI radius_move_2(int32_t x1, int32_t y1, int32_t x2, int32_t y2, int32_t count) {
    int32_t dx = wrap_delta(x1, x2);
    for (int32_t i = 0; i < count; i++) {
        if (dx == TableOffsetX[i] && y2 - y1 == TableOffsetY[i]) return i;
    }
    return -1;
}

int32_t GAMEAPI compass_move(int32_t x1, int32_t y1, int32_t x2, int32_t y2) {
    return compass_index(wrap_delta(x1, x2), y2 - y1);
}
