#include "engine/path.h"

#include <cstdlib>
#include <cstring>

#include "engine/compat.h"
#include "engine/diplo_act.h"
#include "engine/game_state.h"
#include "engine/heap.h"
#include "engine/base.h"
#include "engine/map.h"
#include "engine/move.h"
#include "engine/tech.h"
#include "engine/veh.h"
#include "engine/zoc.h"
#include "engine/ui_bridge.h"

void THISCALL Path_init(Path* self) {
    if (self->mapTable) mem_free(self->mapTable);
    if (self->xTable) mem_free(self->xTable);
    if (self->yTable) mem_free(self->yTable);
    self->mapTable = nullptr;
    self->yTable = nullptr;
    self->xTable = nullptr;
    self->xDst = -1;
    self->yDst = -1;
    self->mapTable = static_cast<int32_t*>(mem_get((uint32_t)game().map.tile_count * 4));
    self->xTable = static_cast<int16_t*>(mem_get((uint32_t)game().map.tile_count * 2));
    self->yTable = static_cast<int16_t*>(mem_get((uint32_t)game().map.tile_count * 2));
    std::memset(self->mapTable, 0, (size_t)game().map.tile_count * 4);
}

void THISCALL Path_make_abstract(Path* self) {
    for (int32_t ay = 0; ay < MapAbstractY; ay++) {
        for (int32_t ax = ay & 1; ax < MapAbstractX; ax += 2) {
            int32_t region = 0;
            for (int32_t i = 0; i < 9; i++) {
                int32_t x2 = map_wrap_x(ax * 5 + TableOffsetX[i]);
                int32_t y2 = ay * 5 + TableOffsetY[i];
                if (map_in_bounds(x2, y2) && !map_is_ocean(x2, y2)) {
                    region = map_tile(x2, y2)->region;
                    break;
                }
            }
            abstract_set(ax, ay, region);
        }
    }
}

void THISCALL Path_continent(Path* self, int32_t x, int32_t y, int32_t region) {
    Continent& cont = Continents[region];
    cont.tile_count = 0;
    int32_t fresh = 0;
    self->index2 = 0;
    self->index1 = 0;
    self->xTable[0] = (int16_t)x;
    self->yTable[self->index1] = (int16_t)y;
    self->index1++;
    region_set(x, y, region);
    bool ocean = map_is_ocean(x, y);
    do {
        int32_t cx = self->xTable[self->index2];
        int32_t cy = self->yTable[self->index2];
        self->index2++;
        cont.tile_count++;
        for (int32_t i = 0; i < 8; i++) {
            int32_t x2 = map_wrap_x(cx + NearOffsetX[i]);
            int32_t y2 = cy + NearOffsetY[i];
            // Tiles reached from the polar rows are not expanded.
            if (!map_in_bounds(x2, y2) || cy == 0 || cy == game().map.y - 1) continue;
            MAP* sq = map_tile(x2, y2);
            if (((sq->climate & 0xE0) < 0x60) != ocean || sq->region != 0) continue;
            if (ocean && (sq->landmarks & LM_FRESH) && (sq->climate & 0xE0) < 0x60) fresh++;
            region_set(x2, y2, region);
            self->xTable[self->index1] = (int16_t)x2;
            self->yTable[self->index1] = (int16_t)y2;
            self->index1++;
        }
    } while (self->index2 != 0 && self->index1 != self->index2);
    if (fresh) {
        // The fresh-water flag sticks only if most of the sea is fresh water.
        int32_t tiles = cont.tile_count;
        for (int32_t ty = 0; ty < game().map.y; ty++) {
            for (int32_t tx = ty & 1; tx < game().map.x; tx += 2) {
                if (map_tile(tx, ty)->region == (uint32_t)region) bit2_set(tx, ty, LM_FRESH, (tiles * 3) / 4 <= fresh);
            }
        }
    }
    do_all_non_input();
}

// Moves every tile of region `from` to region `to`.
static void retag_region(uint32_t from, uint32_t to) {
    for (int32_t i = 0; i < game().map.tile_count; i++) {
        if (game().map.tiles[i].region == from) game().map.tiles[i].region = (uint8_t)to;
    }
}

void THISCALL Path_continents(Path* self) {
    int32_t land_count = 0;
    int32_t sea_count = 0x40;
    for (int32_t i = 0; i < game().map.tile_count; i++) game().map.tiles[i].region = 0;
    for (int32_t y = 1; y < game().map.y - 1; y++) {
        for (int32_t x = y & 1; x < game().map.x; x += 2) {
            if (map_tile(x, y)->region != 0) continue;
            int32_t first, last, region;
            if (map_is_ocean(x, y)) {
                first = 0x40;
                sea_count++;
                last = 0x7E;
                region = sea_count < 0x40 ? 0x40 : (sea_count > 0x7E ? 0x7E : sea_count);
            } else {
                first = 0;
                land_count++;
                last = 0x3E;
                region = land_count < 0 ? 0 : (land_count > 0x3E ? 0x3E : land_count);
            }
            Path_continent(self, x, y, region);
            if (region != last) continue;
            // Out of region numbers: merge the smallest region into the overflow region
            // (last + 1) and reuse its number.
            int32_t smallest = -1;
            int32_t smallest_count = Continents[region].tile_count;
            for (int32_t r = first; r < last; r++) {
                if (Continents[r].tile_count < smallest_count) {
                    smallest = r;
                    smallest_count = Continents[r].tile_count;
                }
            }
            if (smallest >= 0) {
                Continents[last + 1].tile_count += Continents[smallest].tile_count;
                Continents[smallest].tile_count = 0;
                retag_region(smallest, last + 1);
                Continents[smallest].tile_count += Continents[region].tile_count;
                Continents[region].tile_count = 0;
                retag_region(region, smallest);
            } else {
                Continents[last + 1].tile_count += Continents[region].tile_count;
                Continents[region].tile_count = 0;
                retag_region(region, last + 1);
            }
        }
    }
    // Polar rows: regions 0x3F (land) and 0x7F (water).
    for (int32_t x = 0; x < game().map.x; x += 2) {
        int32_t region = map_is_ocean(x, 0) ? 0x7F : 0x3F;
        region_set(x, 0, region);
        Continents[region].tile_count++;
    }
    for (int32_t x = (game().map.y - 1) & 1, y = game().map.y - 1; x < game().map.x; x += 2) {
        int32_t region = map_is_ocean(x, y) ? 0x7F : 0x3F;
        region_set(x, y, region);
        Continents[region].tile_count++;
    }
    int32_t largest = 0, total = 0;
    for (int32_t r = 1; r < 63; r++) {
        total += Continents[r].tile_count;
        if (largest < Continents[r].tile_count) largest = Continents[r].tile_count;
    }
    if (largest < (total * 4) / 5) GameState &= ~0x100;
    else GameState |= 0x100;  // one dominant landmass
    for (int32_t r = 0; r < 64; r++) std::memset(Continents[r].sea_coasts, 0, 8);
    for (int32_t y = 0; y < game().map.y; y++) {
        for (int32_t x = y & 1; x < game().map.x; x += 2) {
            uint32_t region = map_tile(x, y)->region;
            if (region >= 0x40) continue;
            for (int32_t i = 0; i < 8; i++) {
                int32_t x2 = map_wrap_x(x + NearOffsetX[i]);
                int32_t y2 = y + NearOffsetY[i];
                if (!map_in_bounds(x2, y2)) continue;
                uint32_t sea = map_tile(x2, y2)->region;
                if (sea < 0x40) continue;
                int32_t offset, mask;
                bitmask((int32_t)sea - 0x40, &offset, &mask);
                Continents[region].sea_coasts[offset] |= (uint8_t)mask;
                i += 2 - (i & 1);  // skip the next one or two neighbours
            }
        }
    }
    Path_make_abstract(self);
    do_all_non_input();
}

void THISCALL Path_shutdown(Path* self) {
    if (self->mapTable) mem_free(self->mapTable);
    if (self->xTable) mem_free(self->xTable);
    if (self->yTable) mem_free(self->yTable);
    self->mapTable = nullptr;
    self->yTable = nullptr;
    self->xTable = nullptr;
}

void THISCALL Path_dtor(Path* self) { Path_shutdown(self); }

void THISCALL Path_merge(Path* self, int32_t region_from, int32_t region_to) {
    Continents[region_to].tile_count += Continents[region_from].tile_count;
    Continents[region_from].tile_count = 0;
    retag_region((uint32_t)region_from, (uint32_t)region_to);
}

void THISCALL Path_territory(Path* self, int32_t x, int32_t y, int32_t unused, int32_t owner) {
    if (map_is_ocean(x, y)) return;
    self->index2 = 0;
    self->index1 = 0;
    self->xTable[0] = (int16_t)x;
    self->yTable[self->index1] = (int16_t)y;
    self->index1++;
    do {
        int32_t cx = self->xTable[self->index2];
        int32_t cy = self->yTable[self->index2];
        self->index2++;
        for (int32_t i = 0; i < 8; i++) {
            int32_t x2 = map_wrap_x(cx + NearOffsetX[i]);
            int32_t y2 = cy + NearOffsetY[i];
            // Tiles reached from the polar rows are not expanded.
            if (!map_in_bounds(x2, y2) || cy == 0 || cy == game().map.y - 1) continue;
            MAP* sq = map_tile(x2, y2);
            if ((sq->climate & 0xE0) < 0x60 || sq->unk_1 != 0 || sq->owner != owner) continue;
            sq->unk_1 = 1;
            self->xTable[self->index1] = (int16_t)x2;
            self->yTable[self->index1] = (int16_t)y2;
            self->index1++;
        }
    } while (self->index2 != 0 && self->index1 != self->index2);
    do_all_non_input();
}

int32_t THISCALL Path_zoc_path(Path* self, int32_t x, int32_t y, int32_t faction_id) {
    const MAP* sq = map_tile(x, y);
    // Bases are never in a zone of control.
    if ((sq->items & BIT_BASE_IN_TILE) && (sq->val2 & 0xF) < 8) return 0;
    bool ocean = (sq->climate & 0xE0) < 0x60;
    for (int32_t i = 0; i < 8; i++) {
        int32_t x2 = map_wrap_x(x + NearOffsetX[i]);
        int32_t y2 = y + NearOffsetY[i];
        if (!map_in_bounds(x2, y2)) continue;
        const MAP* nb = map_tile(x2, y2);
        if (!(nb->items & BIT_VEH_IN_TILE)) continue;
        int32_t owner = nb->val2 & 0xF;
        if (owner >= 8 || owner == faction_id || ((nb->climate & 0xE0) < 0x60) != ocean) continue;
        if (game().factions[faction_id].diplo_status[owner] & (DIPLO_PACT | DIPLO_VENDETTA))
            continue;  // pact or vendetta: ignored
        uint8_t bit = (uint8_t)(1u << (faction_id & 31));
        if (!(FactionStatus[0] & bit)) return owner + 1;
        // Human players only know about units they can see.
        for (int32_t veh_id = veh_at(x2, y2); veh_id >= 0; veh_id = game().vehs[veh_id].next_veh_id_stack) {
            const VEH& veh = game().vehs[veh_id];
            if ((veh.faction_id == (uint32_t)faction_id && (veh.flags & (VFLAG_LURKER | VFLAG_INVISIBLE)) != 0x440) ||
                (veh.visibility & bit))
                return owner + 1;
        }
    }
    return 0;
}

namespace {
// Unit table entry; Path_find reads it without checking unit_id.
const UNIT& unit_raw(int32_t unit_id) { return (&Units[0])[unit_id]; }
// Factions entry; callers pass faction_id -1 ("any") and the original still reads diplomacy flags.
Faction& faction_raw(int32_t faction_id) { return (&game().factions[0])[faction_id]; }
inline int32_t path_index(int32_t x, int32_t y) { return (game().map.x >> 1) * y + (x >> 1); }
inline uint32_t owner_bit(const MAP* sq) { return 1u << ((uint8_t)sq->owner & 31); }
// Distance test of the ZOC rule: true when (x, y) is within two steps of (x2, y2).
bool path_near(int32_t x, int32_t y, int32_t x2, int32_t y2) {
    int32_t dx = std::abs(x - x2);
    if (!(game().map.flat & 1) && dx > game().map.half_x) dx = game().map.x - dx;
    int32_t dy = std::abs(y - y2);
    return ((dx + dy) & ~1) <= 2;
}

// State of one Path_find call (the original's locals).
struct PathSearch {
    Path* self;
    int32_t x1, y1, x2, y2, unit_id, faction_id;
    uint32_t flags;
    int32_t prev_dir;
    const UNIT* unit;
    bool sea;  // sea unit: ocean tiles are passable
    int32_t road_speed;  // the unit moves no faster than one road move (hex_cost ignore_terrain)
    uint32_t ai_flag;  // flags & 0x80: avoid foreign territory and enemy surroundings
    uint32_t own_mask = 0, src_mask = 0, dst_mask = 0;
    uint32_t treaty_mask = 0, truce_mask = 0, war_mask = 0;
    int32_t dst_near = 0;  // enemy territory at (1) or next to (2) the destination
    int32_t cur_near = 0;  // same for the tile being expanded
    int32_t zoc = 0;  // the tile being expanded is in a zone of control (Path_zoc_path)

    bool plan_watched() const { return unit->plan == 9 || unit->plan == 8; }
    // Enemy (vendetta) territory at the centre (1) or on a diagonal-free neighbour (2) of (x, y).
    int32_t near_enemy(int32_t x, int32_t y) const {
        for (int32_t i = 0; i < 9; i++) {
            int32_t x2 = map_wrap_x(x + TableOffsetX[i]);
            int32_t y2 = y + TableOffsetY[i];
            if (map_in_bounds(x2, y2) && (i == 0 || (i & 1)) && (war_mask & owner_bit(map_tile(x2, y2))))
                return (i != 0) + 1;
        }
        return 0;
    }
    // Extra cost of entering (x, y) (sq) for an AI unit (flags & 0x80). *dir: the direction loop
    // counter, which the original reuses as the counter of the sea artillery scan.
    int32_t ai_cost(int32_t x, int32_t y, const MAP*& sq, int32_t cost, int32_t* dir) const;
};

int32_t PathSearch::ai_cost(int32_t x, int32_t y, const MAP*& sq, int32_t cost, int32_t* dir) const {
    if (war_mask && plan_watched()) {
        for (int32_t i = 0; i < 9; i++) {
            int32_t x2 = map_wrap_x(x + TableOffsetX[i]);
            int32_t y2 = y + TableOffsetY[i];
            if (!map_in_bounds(x2, y2) || !(war_mask & owner_bit(map_tile(x2, y2)))) continue;
            if (i == 0) {
                if (dst_near != 1) {
                    cost += 2500;
                    break;
                }
            } else if (i & 1) {
                if (dst_near == 0 && cur_near != 1) {
                    cost += 500;
                    break;
                }
            } else {
                if (cur_near == 0 && dst_near == 0) cost += 100;
                break;
            }
        }
    }
    uint32_t fbit = 1u << (faction_id & 31);
    uint32_t human = FactionStatus[0] & fbit;
    sq = map_tile(x, y);
    if (!human || (faction_raw(faction_id).player_flags & PFLAG_MAP_REVEALED) || (sq->visibility & (uint8_t)fbit)) {
        // Foreign territory.
        uint32_t obit = owner_bit(sq);
        if (!(own_mask & obit)) {
            int32_t extra = 0;
            if (treaty_mask & obit) extra = SunspotDuration <= 0 ? 1000 : 2500;
            else if (truce_mask & obit) extra = human ? 1 : 100;
            if (extra) {
                if (src_mask & obit) extra = 1;
                if (dst_mask & obit) extra = 1;
                for (int32_t i = 0; i < 21; i++) {
                    int32_t x2 = map_wrap_x(x + TableOffsetX[i]);
                    int32_t y2 = y + TableOffsetY[i];
                    if (!map_in_bounds(x2, y2)) continue;
                    const MAP* t = map_tile(x2, y2);
                    if (!(t->items & BIT_BASE_IN_TILE)) continue;
                    int32_t base_owner = t->val2 & 0xF;
                    if (base_owner < 8 && base_owner > 0) extra *= 2;  // near a (faction) base
                }
            }
            cost += extra;
        }
    }
    // Sea units in open water: enemy artillery in range. Unreachable: Path_find clears 0x80 for
    // sea units.
    if (Chassis[unit->chassis_id].triad == 1 && (sq->climate & 0xE0) < 0x60 &&
        !((sq->items & BIT_BASE_IN_TILE) && (sq->val2 & 0xF) < 8)) {
        // BUG(original): the scan reuses the direction loop counter (set to 25), so no further
        // neighbour of the expanded tile is examined.
        COMPAT_ORIGINAL(path_scan_dir_counter);
        *dir = 25;
        for (int32_t i = 1; i < 25; i++) {
            int32_t x2 = map_wrap_x(x + TableOffsetX[i]);
            int32_t y2 = y + TableOffsetY[i];
            if (!map_in_bounds(x2, y2)) continue;
            for (int32_t veh_id = veh_at(x2, y2); veh_id >= 0; veh_id = game().vehs[veh_id].next_veh_id_stack) {
                const VEH& veh = game().vehs[veh_id];
                if (veh.faction_id != (uint32_t)faction_id &&
                    (faction_raw(faction_id).diplo_status[veh.faction_id] & DIPLO_VENDETTA) && can_arty(veh.unit_id, 1))
                    cost += 100;
            }
        }
    }
    return cost;
}
}  // namespace

// Path_find with the value of its uninitialised "expanded tile is in a ZOC" local made explicit
// (start_zoc): it is read, unset, while the destination's neighbours are expanded (see
// docs/RECONSTRUCTION.md, "Known uninitialised reads"; only the zero/non-zero distinction matters).
int32_t path_find_core(Path* self, int32_t x1, int32_t y1, int32_t x2, int32_t y2, int32_t unit_id, int32_t faction_id,
                       int32_t flags, int32_t prev_dir, int32_t start_zoc) {
    PathSearch s;
    s.self = self;
    s.x1 = x1;
    s.y1 = y1;
    s.x2 = x2;
    s.y2 = y2;
    s.faction_id = faction_id;
    s.prev_dir = prev_dir;
    int32_t best_cost = 2000000000;
    int32_t other_faction = 0;  // flags & 0x200: unit_id holds a second faction whose land is open
    if (flags & 0x200) {
        other_faction = unit_id;
        unit_id = 0;
    }
    s.unit_id = unit_id;
    s.unit = &unit_raw(unit_id);
    s.sea = Chassis[s.unit->chassis_id].triad == 1;
    s.road_speed = speed_proto(unit_id) == Rules.move_rate_roads;
    if (s.sea) flags &= ~0xC0;
    if ((flags & 0x40) && faction_id > 0 && unit_id >= 0 && has_abil(unit_id, 4)) flags &= ~0x40;
    s.flags = (uint32_t)flags;
    s.ai_flag = flags & 0x80;
    if (s.ai_flag && faction_id > 0) {
        const MAP* dst = map_tile(x2, y2);
        s.src_mask = owner_bit(map_tile(x1, y1));
        s.dst_mask = owner_bit(dst);
        for (int32_t i = 1; i < 8; i++) {
            if (i == faction_id) continue;
            uint32_t d = game().factions[faction_id].diplo_status[i];
            if ((d & 2) && !(d & 0xC00)) {
                if (!(d & 1)) s.treaty_mask |= 1u << i;
            } else if ((d & 4) && !(d & 0xC00)) {
                s.truce_mask |= 1u << i;
            } else if ((d & 0x10) && game().factions[i].region_total_bases[dst->region]) {
                s.war_mask |= 1u << i;
            }
        }
        s.own_mask = 1u << (faction_id & 31);
        s.dst_near = 0;
        if (s.plan_watched()) s.dst_near = s.near_enemy(x2, y2);
    }
    const int32_t mr = Rules.move_rate_roads;
    s.zoc = start_zoc;
    bool cached = false;
    if (x2 == self->xDst && y2 == self->yDst && s.unit->plan == unit_raw(self->unit_id).plan &&
        flags == self->field_20 && faction_id == self->faction_id2) {
        cached = self->mapTable[path_index(x1, y1)] != 0;
    } else {
        std::memset(self->mapTable, 0, (size_t)game().map.tile_count * 4);
        self->field_20 = flags;
        self->xDst = x2;
        self->yDst = y2;
        self->unit_id = unit_id;
        self->faction_id2 = faction_id;
    }
    if (!cached) {
        // Cost flood from the destination until the start is reached.
        self->index2 = 0;
        self->index1 = 0;
        self->xTable[0] = (int16_t)x2;
        self->yTable[self->index1] = (int16_t)y2;
        self->index1++;
        self->mapTable[path_index(x2, y2)] = 1;
        do {
            int32_t i = self->index2;
            int32_t cx = self->xTable[i];
            int32_t cy = self->yTable[i];
            self->index2 = i + 1;
            if (i + 1 == game().map.tile_count) self->index2 = 0;
            if (MultiplayerActive && !(self->index2 & 0x3F)) do_all_non_input();
            int32_t cost = self->mapTable[path_index(cx, cy)];
            if (cost > best_cost) continue;
            if (cx == x1 && cy == y1) {
                best_cost = cost;
                continue;
            }
            if ((flags & 0x40) && faction_id > 0 && cost > 1)
                s.zoc = map_is_ocean(cx, cy) ? 0 : Path_zoc_path(self, cx, cy, faction_id);
            if (s.ai_flag && faction_id > 0 && s.war_mask && s.plan_watched()) s.cur_near = s.near_enemy(cx, cy);
            for (int32_t dir = 0; dir < 8; dir++) {
                int32_t nx = map_wrap_x(cx + NearOffsetX[dir]);
                int32_t ny = cy + NearOffsetY[dir];
                if (!map_in_bounds(nx, ny)) continue;
                const MAP* sq = map_tile(nx, ny);
                bool passable =
                    ((sq->climate & 0xE0) < 0x60) == s.sea || (nx == x1 && ny == y1) || (nx == x2 && ny == y2);
                if (!passable && s.sea && (sq->items & BIT_BASE_IN_TILE) && (sq->val2 & 0xF) < 8) {
                    int32_t owner = sq->val2 & 0xF;  // ships enter friendly bases
                    passable = owner == faction_id || (faction_raw(faction_id).diplo_status[owner] & DIPLO_PACT);
                }
                if (!passable) continue;
                if (s.unit->plan == 12) {
                    // Supply crawlers keep away from pods and unexplored tiles.
                    if (goody_at(nx, ny)) continue;
                    if (faction_id >= 0) {
                        bool unknown = false;
                        for (int32_t k = 0; k < 8 && !unknown; k++) {
                            int32_t mx = map_wrap_x(nx + NearOffsetX[k]);
                            int32_t my = ny + NearOffsetY[k];
                            unknown = map_in_bounds(mx, my) &&
                                      !(game().factions[faction_id].player_flags & PFLAG_MAP_REVEALED) &&
                                      !(map_tile(mx, my)->visibility & (uint8_t)(1u << (faction_id & 31)));
                        }
                        if (unknown) continue;
                    }
                }
                if (flags & 2) {  // roads and bases only
                    uint8_t items = (uint8_t)sq->items;
                    if (!(items & 4) && (!(items & 1) || (sq->val2 & 0xF) >= 8)) continue;
                }
                if ((flags & 4) && (sq->items & BIT_FUNGUS) && (sq->climate & 0xE0) >= 0x40) continue;  // no fungus
                int32_t ncost;
                if (flags & 1) {
                    ncost = cost + 1;
                } else {
                    int32_t hc = hex_cost(unit_id, faction_id, nx, ny, cx, cy, s.road_speed);
                    if ((flags & 8) && hc <= mr - 1) hc = mr - 1;
                    ncost = hc * 16 + 8 + cost;
                    if ((map_tile(cx, cy)->climate & 0xE0) < (sq->climate & 0xE0)) ncost--;
                    if (goody_at(nx, ny)) ncost += mr;
                }
                if ((flags & 0x10) && (sq->items & BIT_FUNGUS) && (sq->climate & 0xE0) >= 0x40) ncost += mr * 6;
                if (faction_id >= 0) {
                    if (!(flags & 0x200)) {
                        int32_t occupant = -1;  // faction of units on the tile
                        if (sq->items & BIT_VEH_IN_TILE) {
                            occupant = sq->val2 & 0xF;
                            if (occupant >= 8) {
                                occupant = -1;
                            } else if (occupant != faction_id &&
                                       !(faction_raw(faction_id).diplo_status[occupant] & DIPLO_PACT)) {
                                continue;
                            }
                        }
                        if ((flags & 0x40) && faction_id > 0 && occupant < 0) {
                            // BUG(original): for the destination's neighbours s.zoc is the
                            // uninitialised start value.
                            COMPAT_ORIGINAL(residue.path_start_zoc);
                            if ((s.zoc || !path_near(nx, ny, x2, y2)) && Path_zoc_path(self, nx, ny, faction_id)) {
                                ncost += 1000;
                                if (s.zoc) ncost += 2000;
                            }
                        }
                        if (s.ai_flag && faction_id > 0 && occupant < 0) ncost = s.ai_cost(nx, ny, sq, ncost, &dir);
                    } else {
                        int32_t owner = sq->owner;
                        if (faction_id != owner && other_faction != owner) {
                            if (owner < 0) {
                                if (flags & 0x400) continue;
                            } else {
                                if (!(flags & 0x800)) continue;
                                uint32_t d = faction_raw(faction_id).diplo_status[owner];
                                if ((d & 0x100000) && !(d & 0x11)) continue;
                                if (!(faction_raw(owner).diplo_status[faction_id] & (DIPLO_PACT | DIPLO_VENDETTA)) &&
                                    (FactionStatus[0] & (uint8_t)(1u << (owner & 31))))
                                    continue;
                            }
                        }
                    }
                }
                if (ncost < cost + 1) ncost = cost + 1;
                int32_t& slot = self->mapTable[path_index(nx, ny)];
                if (slot == 0 || ncost < slot) {
                    slot = ncost;
                    int32_t k = self->index2, n = self->index1;
                    while (k < n && !(self->xTable[k] == nx && self->yTable[k] == ny)) k++;
                    if (k == n && n < game().map.tile_count) {
                        self->xTable[n] = (int16_t)nx;
                        self->yTable[self->index1] = (int16_t)ny;
                        self->index1++;
                    }
                }
            }
        } while (self->index2 != 0 && self->index1 != self->index2);
        if (mapwin_field(MapWins[0], 0x1DD70) & 0x4000) pfncRedrawMap(MapWins[0], 1);
        if (best_cost == 2000000000) return -1;
    }
    // Pick the cheapest step from the start.
    int32_t best_dir = -1;
    int32_t best_value = 2000000000;
    if ((flags & 0x40) && faction_id > 0) s.zoc = map_is_ocean(x1, y1) ? 0 : Path_zoc_path(self, x1, y1, faction_id);
    if (s.ai_flag && faction_id > 0 && s.war_mask && s.plan_watched()) s.cur_near = s.near_enemy(x1, y1);
    for (int32_t dir = 0; dir < 8; dir++) {
        int32_t nx = map_wrap_x(x1 + NearOffsetX[dir]);
        int32_t ny = y1 + NearOffsetY[dir];
        if (!map_in_bounds(nx, ny)) continue;
        if (nx == x2 && ny == y2) return dir;
        int32_t cost = self->mapTable[path_index(nx, ny)];
        if (!cost) continue;
        const MAP* sq = map_tile(nx, ny);
        if (!(flags & 1)) {
            int32_t hc = hex_cost(unit_id, faction_id, x1, y1, nx, ny, s.road_speed);
            if ((flags & 8) && hc <= mr - 1) hc = mr - 1;
            cost += hc * 16;
            if (!s.sea && (map_tile(x1, y1)->climate & 0xE0) <= (sq->climate & 0xE0)) {
                cost--;
                if (cost < 0) cost = 0;
                else if (cost > 999999) cost = 999999;
            }
            if (goody_at(nx, ny)) cost += mr;
        }
        if (faction_id >= 0 && !(flags & 0x200)) {
            int32_t occupant = -1;
            if (sq->items & BIT_VEH_IN_TILE) {
                occupant = sq->val2 & 0xF;
                if (occupant >= 8) {
                    occupant = -1;
                } else if (occupant != faction_id && !(faction_raw(faction_id).diplo_status[occupant] & DIPLO_PACT)) {
                    continue;
                }
            }
            if ((flags & 0x40) && faction_id > 0) {
                if ((s.zoc || !path_near(nx, ny, x2, y2)) && occupant < 0 && Path_zoc_path(self, nx, ny, faction_id)) {
                    cost += 1000;
                    if (s.zoc) cost += 2000;
                }
            }
            if (s.ai_flag && faction_id > 0) cost = s.ai_cost(nx, ny, sq, cost, &dir);
        }
        if (prev_dir >= 0 && prev_dir < 8 && dir == (prev_dir ^ 4)) cost += mr * 20;  // no turning back
        int32_t value = (dir & 1) + cost * 2;
        if (value <= best_value) {
            best_dir = dir;
            best_value = value;
        }
    }
    do_all_non_input();
    return best_dir;
}

#ifdef SMACX_HYBRID
// Hybrid: original callers leave the uninitialised local's value on the stack; the entry thunk
// captures the dword Path_find's frame would read ([ebp-0x2c] = [entry esp - 0x30]).
extern "C" int32_t PathFindEntryResidue;
int32_t PathFindEntryResidue = 0;
static int32_t THISCALL path_find_entry(Path* self, int32_t x1, int32_t y1, int32_t x2, int32_t y2, int32_t unit_id,
                                        int32_t faction_id, int32_t flags, int32_t prev_dir) {
    return path_find_core(self, x1, y1, x2, y2, unit_id, faction_id, flags, prev_dir, PathFindEntryResidue);
}
extern "C" void* path_find_entry_ptr;
void* path_find_entry_ptr = reinterpret_cast<void*>(&path_find_entry);
__attribute__((naked)) int32_t THISCALL Path_find(Path* self, int32_t x1, int32_t y1, int32_t x2, int32_t y2,
                                                  int32_t unit_id, int32_t faction_id, int32_t flags,
                                                  int32_t prev_dir) {
    __asm__("mov -0x30(%esp), %eax\n mov %eax, _PathFindEntryResidue\n jmp *_path_find_entry_ptr\n");
}
#else
// Native: only Path_move reaches the uninitialised read (it calls path_find_core with the residue
// speed() leaves); other callers see the canonical zero.
int32_t THISCALL Path_find(Path* self, int32_t x1, int32_t y1, int32_t x2, int32_t y2, int32_t unit_id,
                           int32_t faction_id, int32_t flags, int32_t prev_dir) {
    return path_find_core(self, x1, y1, x2, y2, unit_id, faction_id, flags, prev_dir, 0);
}
#endif

int32_t THISCALL Path_move(Path* self, int32_t veh_id, int32_t faction_id) {
    VEH& veh = game().vehs[veh_id];
    uint32_t flags = 0xC0;
    uint32_t veh_faction = veh.faction_id;
    if (veh_faction != (uint32_t)self->faction_id1) {
        self->xDst = -1;
        self->yDst = -1;
        self->faction_id1 = (int32_t)veh_faction;
    }
    int32_t x = veh.x, y = veh.y;
    int32_t tx = veh.waypoint_x[0], ty = veh.waypoint_y[0];
    if (ty < 0 || ty >= game().map.y || tx < 0 || tx >= game().map.x) return -1;
    if (x == tx && y == ty) return -1;
    uint8_t fbit = (uint8_t)(1u << (veh_faction & 31));
    if (!(FactionStatus[0] & fbit) && (veh.state & 0x60000) == 0x60000) flags = 0xE0;
    if ((veh.state & 0x1000200) == 0x1000200) {
        flags &= ~0x40u;
    } else {
        int32_t moves = speed(veh_id, 0) - veh.moves_spent;
        moves = moves < 0 ? 0 : moves > 999 ? 999 : moves;
        if (moves <= Rules.move_rate_roads) {
            flags |= 0x100;  // last move
            moves = speed(veh_id, 0) - veh.moves_spent;
            moves = moves < 0 ? 0 : moves > 999 ? 999 : moves;
            if (moves <= (veh.unit_id != 8) + 1) flags |= 0x1000;
        }
    }
    const UNIT& unit = Units[veh.unit_id];
    if (Weapon[unit.weapon_id].offense_value == 0 &&
        (SecretProjects[FAC_XENOEMPATHY_DOME - SP_ID_First] < 0 ||
         (uint8_t)game().bases[SecretProjects[FAC_XENOEMPATHY_DOME - SP_ID_First]].faction_id != veh_faction))
        flags |= 0x10;  // non-combat units avoid fungus (unless Xenoempathy Dome)
    int32_t triad = Chassis[unit.chassis_id].triad;
    if (triad != 2) {
        int32_t dx = std::abs(x - tx);
        if (!(game().map.flat & 1) && dx > game().map.half_x) dx = game().map.x - dx;
        if ((dx + std::abs(y - ty)) & ~1) {
            int32_t prev_dir = (veh.state & 0x10000) ? (int32_t)veh.rotate_angle : -1;
            // BUG(original): Path_find's start-ZOC flag is the dead stack value speed() leaves.
            COMPAT_ORIGINAL(residue.path_start_zoc);
            int32_t dir = path_find_core(self, x, y, tx, ty, veh.unit_id, faction_id, (int32_t)flags, prev_dir,
                                         SpeedStackResidue != 0);
            if (dir >= 0) return dir;
            if ((veh.state & 0x1000200) == 0x1000200) {
                veh.order = ORDER_NONE;
                veh.state &= 0xFEFFFDFF;
                return -1;
            }
        }
    }
    // Greedy step towards the waypoint.
    int32_t zoc_here = zoc_move(x, y, (int32_t)veh_faction);
    int32_t best = 99999;
    int32_t best_dir = -1;
    for (int32_t dir = 0; dir < 8; dir++) {
        int32_t nx = map_wrap_x(x + NearOffsetX[dir]);
        int32_t ny = y + NearOffsetY[dir];
        if (!map_in_bounds(nx, ny)) continue;
        int32_t dx = std::abs(nx - tx);
        if (!(game().map.flat & 1) && dx > game().map.half_x) dx = game().map.x - dx;
        int32_t dy = std::abs(ny - ty);
        int32_t dist = (dx + dy) >> 1;
        if (dist == 0) return dir;
        const MAP* sq = map_tile(nx, ny);
        if ((sq->items & (BIT_BASE_IN_TILE | BIT_VEH_IN_TILE)) && (sq->val2 & 0xF) < 8) {
            uint32_t owner = sq->val2 & 0xF;
            if (owner != veh_faction && !(game().factions[veh_faction].diplo_status[owner] & DIPLO_PACT)) continue;
        }
        int32_t value;
        if (triad == 2) {
            if (sq->items & BIT_AIRBASE) continue;
            int32_t a = std::abs(dx), b = std::abs(dy);
            int32_t big = a > b ? a : b;
            int32_t small = a < b ? a : b;
            value = dist * 32 - ((((a + b) >> 1) - small + 1) >> 1) + big;
        } else {
            if (((sq->climate & 0xE0) < 0x60) != (triad == 1)) continue;
            if (zoc_here && zoc_move(nx, ny, (int32_t)veh_faction)) {
                // BUG(original): tests the tile at index x >> 1 (row 0) for a native base.
                COMPAT_ORIGINAL(native_base_row_0);
                const MAP* t = &game().map.tiles[nx >> 1];
                if (!(t->items & BIT_BASE_IN_TILE) || (t->val2 & 0xF) >= 8 || (t->val2 & 0xF) != 0) continue;
            }
            value = hex_cost(veh.unit_id, (int32_t)veh_faction, x, y, nx, ny, 0) + dist * 32;
        }
        if (value < best) {
            best = value;
            best_dir = dir;
        }
    }
    if (best_dir < 0) {
        veh.order = ORDER_NONE;
        if (!(FactionStatus[0] & fbit)) veh_skip(veh_id);
    }
    if ((veh.state & 0x10000) && best_dir == ((int32_t)veh.rotate_angle ^ 4) &&
        Chassis[Units[veh.unit_id].chassis_id].triad != 2) {
        veh.state &= ~0x10000u;
        veh.order = ORDER_NONE;
        if (!(FactionStatus[0] & fbit)) veh_skip(veh_id);
        return -1;
    }
    return best_dir;
}

int32_t THISCALL Path_sensors(Path* self, int32_t faction_id, int32_t* x, int32_t* y) {
    int32_t result = 1;
    std::memset(self->mapTable, 0, (size_t)game().map.tile_count * 4);
    self->xDst = -1;
    self->yDst = -1;
    int32_t x0 = *x, y0 = *y;
    int32_t best = 9999;
    uint8_t region = map_tile(x0, y0)->region;
    for (int32_t ty = 0; ty < game().map.y; ty++) {
        for (int32_t tx = ty & 1; tx < game().map.x; tx += 2) {
            MAP* sq = map_tile(tx, ty);
            if (sq->region != region) continue;
            if (whose_territory(faction_id, tx, ty, nullptr, 0) != faction_id) continue;
            if ((sq->items & BIT_BASE_IN_TILE) && (sq->val2 & 0xF) < 8) continue;
            if ((sq->items & BIT_VEH_IN_TILE) && (sq->val2 & 0xF) < 8) {
                int32_t owner = sq->val2 & 0xF;
                if (owner != faction_id && !(game().factions[faction_id].diplo_status[owner] & DIPLO_PACT)) continue;
            }
            int32_t zoc = zoc_veh(tx, ty, faction_id);
            if (zoc == 1) continue;
            // BUG(original): zoc may be 8 (diplo_status[8] is the field after the array).
            COMPAT_ORIGINAL(out_of_range_reads);
            if (zoc != 0 && (diplo_status_raw(faction_id, zoc) & 0x10)) continue;
            uint32_t items = sq->items;
            if ((items & 0x50) || (items & 0x1402000)) continue;
            if (bonus_at(tx, ty)) continue;
            bool rough = (sq->val3 & 0xC0) > 0x40 || (sq->climate & 0x18) == 0;
            if (!rough && (!(sq->items & BIT_FUNGUS) || (sq->climate & 0xE0) < 0x40)) continue;
            if ((sq->items & BIT_FUNGUS) && (sq->climate & 0xE0) >= 0x40 &&
                !has_tech(Rules.tech_preq_improv_fungus, faction_id))
                continue;
            // Sensor coverage within radius 2 (adjacent: covered).
            bool covered_adjacent = false, covered = false;
            for (int32_t i = 0; i < 25; i++) {
                int32_t x2 = map_wrap_x(tx + TableOffsetX[i]);
                int32_t y2 = ty + TableOffsetY[i];
                if (!map_in_bounds(x2, y2)) continue;
                if ((is_sensor(x2, y2) && whose_territory(faction_id, x2, y2, nullptr, 0) == faction_id) ||
                    self->mapTable[path_index(x2, y2)] != 0) {
                    if (i < 9) covered_adjacent = true;
                    covered = true;
                }
            }
            if (covered_adjacent) continue;
            int32_t score = veh_vector_dist(x0, y0, tx, ty);
            bool near_base = false, base_uncovered = false;
            for (int32_t i = 0; i < 25; i++) {
                int32_t bx = map_wrap_x(tx + TableOffsetX[i]);
                int32_t by = ty + TableOffsetY[i];
                if (!map_in_bounds(bx, by)) continue;
                const MAP* b = map_tile(bx, by);
                int32_t owner = ((b->items & BIT_BASE_IN_TILE) && (b->val2 & 0xF) < 8) ? (b->val2 & 0xF) : -1;
                if (owner != faction_id) continue;
                near_base = true;
                bool base_covered = false;
                for (int32_t k = 0; k < 25 && !base_covered; k++) {
                    int32_t x2 = map_wrap_x(bx + TableOffsetX[k]);
                    int32_t y2 = by + TableOffsetY[k];
                    if (!map_in_bounds(x2, y2)) continue;
                    base_covered = (is_sensor(x2, y2) || self->mapTable[path_index(x2, y2)] != 0) &&
                                   whose_territory(faction_id, x2, y2, nullptr, 0) == faction_id;
                }
                if (!base_covered) base_uncovered = true;
            }
            if (!near_base) score += 400;
            if (!base_uncovered) score += 800;
            if (covered && !base_uncovered) {
                score += 200;
            } else {
                self->mapTable[path_index(tx, ty)] = score + 1;
            }
            if (score < best) {
                *x = tx;
                result = 0;
                *y = ty;
                best = score;
            }
        }
    }
    do_all_non_input();
    return result;
}
