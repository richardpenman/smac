// Goals, sites, monoliths, supply pods (original module around 0x579770-0x57D1F0).
#include "engine/goals.h"

#include <cstdlib>

#include "engine/base.h"
#include "engine/crt.h"
#include "engine/design.h"
#include "engine/diplomacy.h"
#include "engine/map.h"
#include "engine/parse.h"
#include "engine/prefs.h"
#include "engine/rules.h"
#include "engine/tech.h"
#include "engine/ui_bridge.h"
#include "engine/random.h"
#include "engine/veh.h"
#include "engine/world.h"

void GAMEAPI init_goals(int32_t faction_id) {
    Faction& f = Factions[faction_id];
    for (Goal& g : f.goals) {
        g.type = -1;
        g.priority = 0;
        g.y = 0;
        g.x = 0;
        g.base_id = 0;
    }
    for (Goal& g : f.sites) {
        g.type = -1;
        g.priority = 0;
        g.y = 0;
        g.x = 0;
        g.base_id = 0;
    }
}

void GAMEAPI del_site(int32_t faction_id, int32_t type, int32_t x, int32_t y, int32_t distance) {
    Faction& f = Factions[faction_id];
    for (Goal& site : f.sites) {
        if (site.type != type || veh_vector_dist(x, y, site.x, site.y) > distance) continue;
        site.type = -1;
        site.priority = 0;
        // Goals of the same type at the site go too.
        for (Goal& goal : f.goals) {
            if (goal.x == site.x && goal.y == site.y && goal.type == type) goal.type = -1;
        }
    }
}

void GAMEAPI add_goal(int32_t faction_id, int32_t type, int32_t priority, int32_t x, int32_t y, int32_t base_id) {
    if (y < 0 || y >= MapAreaY || x < 0 || x >= MapAreaX) return;
    Faction& f = Factions[faction_id];
    for (Goal& g : f.goals) {
        if (g.x == x && g.y == y && g.type == type) {
            if (g.priority <= priority) g.priority = (int16_t)priority;
            return;
        }
    }
    // Replace a free slot (best), else the lowest-priority goal below the new one.
    int32_t slot = -1, best = 0;
    for (int32_t i = 0; i < 75; i++) {
        const Goal& g = f.goals[i];
        if (g.type >= 0 && g.priority >= priority) continue;
        int32_t value = g.type < 0 ? 1000 : (g.priority > 0 ? 20 - g.priority : g.priority + 100);
        if (value > best) {
            best = value;
            slot = i;
        }
    }
    if (slot < 0) return;
    Goal& g = f.goals[slot];
    g.x = x;
    g.y = y;
    g.type = (int16_t)type;
    g.priority = (int16_t)priority;
    g.base_id = base_id;
}

void GAMEAPI add_site(int32_t faction_id, int32_t type, int32_t priority, int32_t x, int32_t y) {
    if (((x ^ y) & 1) && (GameState & STATE_DEBUG_MODE)) danger("Bad SITE", DangerEmptyText, x, y, type);
    Faction& f = Factions[faction_id];
    for (Goal& s : f.sites) {
        if (s.x == x && s.y == y && s.type == type) {
            if (s.priority <= priority) s.priority = (int16_t)priority;
            return;
        }
    }
    int32_t slot = -1, best = 0;
    for (int32_t i = 0; i < 25; i++) {
        const Goal& s = f.sites[i];
        if (s.type >= 0 && s.priority >= priority) continue;
        int32_t value = s.type < 0 ? 1000 : 20 - s.priority;
        if (value > best) {
            best = value;
            slot = i;
        }
    }
    if (slot < 0) return;
    Goal& s = f.sites[slot];
    s.x = x;
    s.y = y;
    s.type = (int16_t)type;
    s.priority = (int16_t)priority;
    add_goal(faction_id, s.type, s.priority, s.x, s.y, -1);
}

int32_t GAMEAPI at_site(int32_t faction_id, int32_t type, int32_t x, int32_t y) {
    for (const Goal& s : Factions[faction_id].sites) {
        if (s.x == x && s.y == y && s.type == type) return 1;
    }
    return 0;
}

int32_t GAMEAPI rnd(int32_t n) {
    if (n - 1 <= 0) return 0;
    return game_rand() % n;
}

int32_t GAMEAPI x_dist(int32_t x1, int32_t x2) {
    int32_t dx = std::abs(x1 - x2);
    if (!(MapToggleFlat & 1) && dx > MapHalfX) return MapAreaX - dx;
    return dx;
}

int32_t GAMEAPI site_at(int32_t x, int32_t y, int32_t sea) {
    MAP* sq = map_tile(x, y);
    if (sq->items & 0x1000) return 0;
    int32_t site = sq->val2 >> 4;
    if (site == 0) {
        site = world_site(x, y, sea);
        site_set(x, y, site);
    }
    return site;
}

int32_t GAMEAPI is_known(int32_t x, int32_t y, int32_t faction_id) {
    if (!(Factions[faction_id].player_flags & 0x200) &&
        !(map_tile(x, y)->visibility & (uint8_t)(1u << (faction_id & 31))))
        return 0;
    return 1;
}

int32_t GAMEAPI base_who(int32_t x, int32_t y) {
    const MAP* sq = map_tile(x, y);
    if (!(sq->items & 1) || (sq->val2 & 0xF) > 7) return -1;
    return sq->val2 & 0xF;
}

int32_t GAMEAPI anything_at(int32_t x, int32_t y) {
    const MAP* sq = map_tile(x, y);
    if (!(sq->items & 3) || (sq->val2 & 0xF) > 7) return -1;
    return sq->val2 & 0xF;
}

int32_t GAMEAPI veh_top(int32_t veh_id) {
    if (veh_id < 0) return -1;
    while (Vehs[veh_id].prev_veh_id_stack >= 0) veh_id = Vehs[veh_id].prev_veh_id_stack;
    return veh_id;
}

int32_t GAMEAPI veh_moves(int32_t veh_id) {
    int32_t moves = speed(veh_id, 0) - Vehs[veh_id].moves_spent;
    if (moves < 0) return 0;
    if (moves > 999) return 999;
    return moves;
}

int32_t GAMEAPI proto_power(int32_t veh_id) {
    const UNIT& unit = Units[Vehs[veh_id].unit_id];
    if (unit.plan == 12) return 1;
    int32_t reactor = unit.reactor_id;
    if (reactor < 1) reactor = 1;
    else if (reactor > 100) reactor = 100;
    return reactor * 10;
}

int32_t GAMEAPI is_port(int32_t base_id, int32_t is_base_radius) {
    return is_coast(Bases[base_id].x, Bases[base_id].y, is_base_radius);
}

int32_t GAMEAPI at_goal(int32_t faction_id, int32_t type, int32_t x, int32_t y) {
    for (const Goal& g : Factions[faction_id].goals) {
        if (g.x == x && g.y == y && g.type == type) return 1;
    }
    return 0;
}

void GAMEAPI wipe_goals(int32_t faction_id) {
    Faction& f = Factions[faction_id];
    // Goals age: positive priorities turn negative (kept for one more turn), negative ones expire.
    for (Goal& g : f.goals) {
        if (g.priority < 0)
            g.type = -1;
        else
            g.priority = (int16_t)-g.priority;
    }
    for (const Goal& s : f.sites) {
        if (s.type >= 0) add_goal(faction_id, s.type, s.priority, s.x, s.y, -1);
    }
}

void GAMEAPI monolith_sites(int32_t x, int32_t y) {
    for (int32_t faction_id = 1; faction_id < 8; faction_id++) {
        if (map_tile(x, y)->visible_items[faction_id - 1] & 0x2000) add_site(faction_id, 0, 2, x, y);
    }
}

int32_t GAMEAPI valid_tech_leap(int32_t tech_id, int32_t faction_id) {
    const CTech& tech = Tech[tech_id];
    // The prerequisites' prerequisites must be known.
    if (tech.preq_tech1 >= 0) {
        if (!has_tech(Tech[tech.preq_tech1].preq_tech1, faction_id)) return 0;
        if (!has_tech(Tech[tech.preq_tech1].preq_tech2, faction_id)) return 0;
    }
    if (tech.preq_tech2 >= 0) {
        if (!has_tech(Tech[tech.preq_tech2].preq_tech1, faction_id)) return 0;
        if (!has_tech(Tech[tech.preq_tech2].preq_tech2, faction_id)) return 0;
    }
    if (tech.preq_tech1 <= -2 || tech.preq_tech2 <= -2) return 0;
    for (const CReactor& reactor : Reactor) {
        if (reactor.preq_tech == tech_id) return 0;
    }
    for (const CWeapon& weapon : Weapon) {
        if (weapon.preq_tech != tech_id) continue;
        // Weapons: only for weaker factions, and not far ahead of their arsenal.
        if (Factions[faction_id].ranking <= 2 && weapon.offense_value <= weapon_budget(faction_id, 99, 0) + 2)
            return 1;
        return 0;
    }
    return 1;
}
