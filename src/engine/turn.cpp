// Turn processing (original module around 0x51F2E0-0x52DBA0).
#include "engine/turn.h"

#include "engine/base.h"
#include "engine/goals.h"
#include "engine/map.h"
#include "engine/world.h"

void GAMEAPI reset_territory() {
    for (int32_t f = 0; f < 8; f++) {
        Faction& fac = Factions[f];
        for (int32_t r = 0; r < 128; r++) {
            fac.region_good_tiles[r] = 0;
            fac.region_territory_tiles[r] = 0;
            fac.region_visible_tiles[r] = 0;
            fac.region_unk_5[r] = 0;
            fac.region_territory_pods[r] = 0;
            fac.region_unk_6[r] = 0;
        }
    }
    for (Continent& c : Continents) {
        c.pods = 0;
        c.world_site = 0;
    }
    for (int32_t y = 0; y < MapAreaY; y++) {
        for (int32_t x = y & 1; x < MapAreaX; x += 2) {
            MAP* sq = map_tile(x, y);
            uint32_t region = sq->region;
            for (int32_t f = 1; f < 8; f++) {
                uint32_t flags = Factions[f].player_flags;
                bool seen = (flags & PFLAG_STRAT_SEARCH_OBJECTIVES) ? (sq->items & BIT_SUPPLY_REMOVE) != 0
                                                                     : (flags & PFLAG_MAP_REVEALED) || (sq->visibility & (1 << f));
                if (seen) Factions[f].region_visible_tiles[region]++;
            }
            // Owner: the nearest base within the territory distance (half at sea), and at sea any
            // base next to the tile.
            int32_t owner = -1;
            bool ocean = (sq->climate & 0xE0) < 0x60;
            int32_t base_id = base_find_3(x, y, -1, region, -1, -1);
            if (base_id >= 0) {
                int32_t limit = Rules.territory_max_dist_base;
                if (ocean) limit >>= 1;
                if (BaseFindDist < limit) owner = Bases[base_id].faction_id;
            }
            if (ocean) {
                for (int32_t i = 0; i < 9; i++) {
                    int32_t x2 = map_wrap_x(x + TableOffsetX[i]);
                    int32_t y2 = y + TableOffsetY[i];
                    if (!map_in_bounds(x2, y2)) continue;
                    int32_t near_base = base_at(x2, y2);
                    if (near_base >= 0) {
                        owner = Bases[near_base].faction_id;
                        break;
                    }
                }
            }
            if (owner != sq->owner) {
                sq->landmarks |= 0x400000;
                GameDrawState |= 1;
            }
            sq->owner = (int8_t)owner;
            if (owner < 0) owner = 0;
            Faction& fac = Factions[owner];
            fac.region_territory_tiles[region]++;
            if ((sq->climate & 0x18) && (sq->val3 & 0xC0) < 0x80 && !(sq->items & BIT_FUNGUS)) {
                fac.region_good_tiles[region]++;
                if (sq->items & BIT_BASE_RADIUS) fac.region_unk_5[region]++;
            }
            int32_t site = 0;
            if (!(sq->items & BIT_BASE_RADIUS)) {
                site = sq->val2 >> 4;
                if (!site) {
                    site = world_site(x, y, 0);
                    site_set(x, y, site);
                }
            }
            Continent& cont = Continents[region];
            if (site > cont.world_site) cont.world_site = site;
            if (owner && site > fac.region_unk_6[region]) fac.region_unk_6[region] = (uint8_t)site;
            if (!goody_at(x, y)) continue;
            cont.pods++;
            fac.region_territory_pods[region]++;
            if (owner) {
                uint8_t bit = (uint8_t)(1 << owner);
                if ((sq->visibility & bit) || (!(FactionStatus[0] & bit) && DiffLevel > 3)) add_site(owner, 3, 1, x, y);
                continue;
            }
            // Unclaimed pod: a site for factions present on the landmass at medium range of a base.
            for (int32_t f = 1; f < 8; f++) {
                if (!Factions[f].region_total_bases[region]) continue;
                uint8_t bit = (uint8_t)(1 << f);
                if (!(sq->visibility & bit) && ((FactionStatus[0] & bit) || DiffLevel <= 3)) continue;
                if (at_site(f, 3, x, y)) continue;
                if (base_find_3(x, y, f, region, -1, -1) < 0) continue;
                if (BaseFindDist >= Rules.territory_max_dist_base && BaseFindDist <= Rules.territory_max_dist_base * 2)
                    add_site(f, 3, 1, x, y);
            }
        }
    }
}

int32_t GAMEAPI council_votes(int32_t faction_id) {
    if (ExpansionEnabled && (MFactions[faction_id].rule_flags & RFLAG_ALIEN)) return 0;
    int32_t votes = 0;
    for (int32_t i = 0; i < BaseCount; i++) {
        if (Bases[i].faction_id == faction_id) votes += Bases[i].pop_size;
    }
    if (SecretProjects[4] >= 0 && Bases[SecretProjects[4]].faction_id == faction_id) votes += votes / 2;
    if (SecretProjects[26] >= 0 && Bases[SecretProjects[26]].faction_id == faction_id) votes *= 2;
    const MFaction& m = MFactions[faction_id];
    for (int32_t i = 0; i < m.faction_bonus_count; i++) {
        if (m.faction_bonus_id[i] == RULE_VOTES && m.faction_bonus_val1[i] >= 0) votes *= m.faction_bonus_val1[i];
    }
    return votes;
}
