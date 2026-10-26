// Unit movement (original module around 0x593510-0x59A1B0).
#include "engine/move.h"

#include <cstddef>
#include <cstdlib>
#include <cstring>

#include "engine/action.h"
#include "engine/base.h"
#include "engine/diplo_act.h"
#include "engine/game_state.h"
#include "engine/map.h"
#include "engine/net.h"
#include "engine/parse.h"
#include "engine/ui_bridge.h"
#include "engine/veh.h"

namespace {
// Unit table entry; the original indexes it without checking unit_id (-1 reads the bytes before).
const UNIT& unit_raw(int32_t unit_id) { return (&Units[0])[unit_id]; }
int32_t unit_raw_triad(int32_t unit_id) { return Chassis[unit_raw(unit_id).chassis_id].triad; }
// Factions[faction_id].SE_planet without a range check: hex_cost is called with faction -1
// (Path_move for some units); Factions[-1] lies in the vehicle table (plain data in the image).
int32_t se_planet_raw(int32_t faction_id) {
    int32_t v;
    std::memcpy(&v, reinterpret_cast<const uint8_t*>(&game().factions[0]) + faction_id * (int32_t)sizeof(Faction) +
                        offsetof(Faction, SE_planet), 4);
    return v;
}
// Whether faction_id owns the Xenoempathy Dome (secret project 8).
bool owns_xenoempathy(int32_t faction_id) {
    return SecretProjects[8] >= 0 && (uint8_t)game().bases[SecretProjects[8]].faction_id == (uint32_t)faction_id;
}
}  // namespace

int32_t GAMEAPI hex_cost(int32_t unit_id, int32_t faction_id, int32_t x1, int32_t y1, int32_t x2, int32_t y2,
                         int32_t ignore_terrain) {
    const MAP* from = map_tile(x1, y1);
    const MAP* to = map_tile(x2, y2);
    uint32_t from_items = from->items;
    uint32_t to_items = to->items;
    int32_t to_alt = to->climate & 0xE0;
    if (to_alt < 0x60) {
        // Sea fungus on the shelf slows ships (unit 9: Sealurk).
        if ((to_items & 0x20) && to_alt == 0x40 && unit_raw_triad(unit_id) == 1 && unit_id != 9 &&
            !owns_xenoempathy(faction_id))
            return Rules.move_rate_roads * 3;
        return Rules.move_rate_roads;
    }
    if ((from->climate & 0xE0) < 0x60) return Rules.move_rate_roads;  // landing
    if (unit_id >= 0 && unit_raw_triad(unit_id) != 0) return Rules.move_rate_roads;
    // Mag tubes / roads (natives use neither).
    if ((from_items & 9) && (to_items & 9) && faction_id != 0) return 0;
    if ((from_items & 5) || ((from_items & 0x20) && faction_id > 0 && owns_xenoempathy(faction_id))) {
        if ((to_items & 5) && faction_id != 0) return 1;
    }
    if (faction_id >= 0 &&
        (owns_xenoempathy(faction_id) || faction_id == 0 || unit_id == 8 || unit_id == 15) && (to_items & 0x20))
        return 1;  // fungus for natives (Mind Worms, Spore Launchers) and Xenoempathy Dome owners
    // Along a river.
    if ((from_items & 0x80) && (to_items & 0x80)) {
        int32_t dx = std::abs(x1 - x2);
        if (!(game().map.flat & 1) && dx > game().map.half_x) dx = game().map.x - dx;
        if (dx == 1) {
            int32_t dy = y1 - y2;
            if (dy < 0) dy = y2 - y1;
            if (dy == 1 && faction_id != 0) return 1;
        }
    }
    const UNIT& unit = unit_raw(unit_id);
    if (unit.chassis_id == 2 || has_abil(unit_id, 0x400)) return Rules.move_rate_roads;  // hovertank, antigrav
    int32_t cost = Rules.move_rate_roads;
    if ((to->val3 & 0xC0) > 0x40 && !ignore_terrain) cost += Rules.move_rate_roads;  // rocky
    if ((to_items & 0x200000) && !ignore_terrain) cost += Rules.move_rate_roads;     // forest
    if (faction_id != 0 && (to_items & 0x20)) {
        if (unit_id < 64 && Weapon[unit.weapon_id].offense_value < 0) return cost;
        if (unit.plan != 9 && unit.plan != 12 && se_planet_raw(faction_id) <= 0)
            return cost + Rules.move_rate_roads * 2;
        if (cost <= speed_proto(unit_id)) return speed_proto(unit_id);
    }
    return cost;
}

void GAMEAPI quick_zoc(int32_t x, int32_t y, int32_t faction_id, int32_t x2, int32_t y2, int32_t* x_out,
                       int32_t* y_out) {
    int32_t best = -1;
    bool ocean = map_is_ocean(x, y);
    for (int32_t i = 0; i < 8; i++) {
        int32_t nx = map_wrap_x(x + NearOffsetX[i]);
        int32_t ny = y + NearOffsetY[i];
        if (!map_in_bounds(nx, ny)) continue;
        const MAP* sq = map_tile(nx, ny);
        if (!(sq->items & 2)) continue;
        int32_t owner = sq->val2 & 0xF;
        if (owner >= 8 || owner == faction_id || map_is_ocean(nx, ny) != ocean) continue;
        if (game().factions[faction_id].diplo_status[owner] & 1) continue;
        int32_t dist = veh_vector_dist(nx, ny, x2, y2);
        if (dist >= best) {
            best = dist;
            *x_out = nx;
            *y_out = ny;
        }
    }
}

int32_t GAMEAPI supply_options(int32_t veh_id, int32_t base_id) {
    // The original's Popup object on its stack (constructed first, destroyed on return).
    struct StackPopup {
        alignas(8) uint8_t obj[0x5394];
        StackPopup() { Popup_ctor(obj); }
        ~StackPopup() { Popup_dtor(obj); }
    } popup;
    VEH& veh = game().vehs[veh_id];
    int32_t faction_id = veh.faction_id;
    // Only human players are asked.
    if (!(FactionStatus[0] & (uint8_t)(1u << (faction_id & 31)))) return 0;
    int8_t order = (int8_t)veh.order;
    if (order == 0x1B || order == 0x1C || order == 0x19 || (veh.state & 0x4000)) return 0;
    if (order == 0x18) {
        // Moving somewhere else: only ask when the destination is next to this base.
        int32_t dx = std::abs(veh.x - veh.waypoint_x[0]);
        if (!(game().map.flat & 1) && dx > game().map.half_x) dx = game().map.x - dx;
        if (((std::abs(veh.y - veh.waypoint_y[0]) + dx) & ~1) > 2) return 0;
    }
    BASE& base = game().bases[base_id];
    uint32_t owner = (uint8_t)base.faction_id;
    if ((uint32_t)faction_id != owner && !(game().factions[faction_id].diplo_status[owner] & 1)) return 0;
    parse_says(1, Units[veh.unit_id].name, -1, -1);
    parse_says(2, base.name, -1, -1);
    if (veh.home_base_id >= 0) parse_says(3, game().bases[veh.home_base_id].name, -1, -1);
    int32_t item = base.queue_items[0];
    int32_t cost;
    if (item < 0)
        cost = cost_factor(faction_id, 1, -1) * Facility[-item].cost;
    else
        cost = veh_cost(item, base_id, nullptr) * cost_factor(faction_id, 1, -1);
    parse_num(0, base.minerals_accumulated);
    parse_num(1, cost);
    // Menu items (the SUPPLYMENU texts): cancel, help build a secret project or a prototype,
    // convoy (three resources).
    int32_t items = 1;
    item = base.queue_items[0];
    if (item <= -70) {
        cost = cost_factor(faction_id, 1, -1) * Facility[-item].cost;
        parse_say(0, Facility[-item].name, -1, -1);
        items = 2;
    }
    item = base.queue_items[0];
    if (item >= 0) {
        cost = veh_cost(item, base_id, nullptr) * cost_factor(faction_id, 1, -1);
        if (item >= 64 && !(Units[item].unit_flags & 4)) {
            parse_says(0, Units[item].name, -1, -1);
            items++;
        }
    }
    if (convoy(veh_id, base_id)) items += 3;
    if (items <= 1) return 0;
    int32_t choice;
    if (faction_id == CurrentPlayerFaction && (VehAttackFlags & 1)) {
        choice = ui_supply_menu(veh_id, base_id, items);
        if (!choice) return 0;
        veh.probe_action = (uint8_t)choice;
    } else {
        choice = veh.probe_action;  // the choice made when the order was given
    }
    if ((VehAttackFlags & 1) && !(VehAttackFlags & 2)) return 1;
    if (choice != 1) {
        veh.order = 3;  // convoy
        veh.order_auto_type = (uint8_t)(choice - 2);
        return 0;
    }
    // Help build: the crawler's cost goes into the production.
    base.minerals_accumulated += cost_factor(faction_id, 1, -1) * Units[veh.unit_id].cost;
    base.minerals_accumulated_2 = base.minerals_accumulated;
    base.production_id_last = base.queue_items[0];
    synch_base(base_id);
    int32_t left = cost - base.minerals_accumulated;
    left = left < 0 ? 0 : left > 9999 ? 9999 : left;
    if (faction_id == CurrentPlayerFaction) {
        parse_num(0, left);
        NetMsg_pop(&NetMsg, "SUPPLYASSIGN", 5000, 0, nullptr);
    }
    if (MultiplayerActive) {
        message_veh(0x2407, veh_id, 1, 0);
        NetDaemon_await_exec(&NetState, 1);
    } else {
        kill(veh_id);
    }
    return 1;
}
