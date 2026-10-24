// Goals, sites, monoliths, supply pods (original module around 0x579770-0x57D1F0).
#include "engine/goals.h"

#include "engine/diplomacy.h"
#include "engine/veh.h"

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
