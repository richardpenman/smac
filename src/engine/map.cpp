#include "engine/map.h"

#include <cstdio>
#include <cstring>

#include "engine/base.h"
#include "engine/compat.h"
#include "engine/crt.h"
#include "engine/diplo_act.h"
#include "engine/game_state.h"
#include "engine/heap.h"
#include "engine/parse.h"
#include "engine/random.h"
#include "engine/score.h"
#include "engine/stack.h"
#include "engine/strings.h"
#include "engine/ui_bridge.h"
#include "engine/util.h"
#include "engine/veh.h"
#include "engine/world.h"

int32_t map_rand_mod(int32_t n) { return n - 1 > 0 ? game_rand() % n : 0; }

// Altitude implied by an altitude detail value (shared by alt_natural, alt_set_both and
// world_shorelines, which inline the same loop).
static int32_t natural_altitude(int32_t detail) {
    for (int32_t alt = 6; alt >= 0; alt--) {
        if (detail - MapSeaLevel >= AltNatural[alt]) return alt;
    }
    return 0;
}

void GAMEAPI map_shutdown() {
    if (game().map.tiles) mem_free(game().map.tiles);
    if (MapAbstract) mem_free(MapAbstract);
    game().map.tiles = nullptr;
    MapAbstract = nullptr;
}

int32_t GAMEAPI map_init() {
    StrBuffer[0] = 0;
    std::strcat(StrBuffer, "maps\\");
    std::strcat(StrBuffer, label_get(0xA90 / 4));
    std::strcat(StrBuffer, ".");
    std::strcpy(MapFilePath, StrBuffer);
    std::strcat(MapFilePath, "MP");
    game().map.half_x = game().map.x / 2;
    game().map.tile_count = game().map.half_x * game().map.y;
    game().map.sq_root = quick_root(game().map.tile_count);
    game().map.tiles = nullptr;
    game().map.tiles = static_cast<MAP*>(mem_get((uint32_t)game().map.tile_count * sizeof(MAP)));
    if (game().map.tiles) {
        MapAbstractX = (game().map.x + 4) / 5;
        MapAbstractY = (game().map.y + 4) / 5;
        MapAbstractCount = ((MapAbstractX + 1) / 2) * MapAbstractY;
        MapAbstract = static_cast<uint8_t*>(mem_get((uint32_t)MapAbstractCount));
        if (MapAbstract) {
            mapwin_terrain_fixup();
            return 0;
        }
        if (!game().map.tiles) {
            game().map.tiles = nullptr;
            MapAbstract = nullptr;
            return 1;
        }
        mem_free(game().map.tiles);
    }
    if (MapAbstract) mem_free(MapAbstract);
    game().map.tiles = nullptr;
    MapAbstract = nullptr;
    return 1;
}

// Tile reset shared by map_wipe and map_resize. Bytes 6 (unk_1) and 7 (owner) are left as they are.
static void wipe_tiles() {
    MapSeaLevelCouncil = 0;
    MapLandmarkCount = 0;
    MapSeaLevel = 0;
    MapRandomSeed = game_random(0, 0x7FFF) + 1;
    MAP* sq = game().map.tiles;
    for (int32_t i = 0; i < game().map.tile_count; i++, sq++) {
        sq->climate = 0x20;
        sq->contour = 0x14;
        sq->val2 = 0xF;
        sq->region = 0;
        sq->visibility = 0;
        sq->items = 0;
        sq->val3 = 0;
        sq->landmarks = 0;
        for (uint32_t& v : sq->visible_items) v = 0;
    }
}

void GAMEAPI map_wipe() { wipe_tiles(); }

// The header block starts at MapAreaX (0x949870) and runs 0xAA4 bytes (to MapFilePath's end);
// it includes the MapTiles/MapAbstract pointer slots, written as they are in the image.
int32_t GAMEAPI map_write(CrtFile* file) {
    if (crt_fwrite(&game().map.x, 0xAA4, 1, file) == 0) return 1;
    if (crt_fwrite(game().map.tiles, (uint32_t)game().map.tile_count * sizeof(MAP), 1, file) == 0) return 1;
    return crt_fwrite(MapAbstract, (uint32_t)MapAbstractCount, 1, file) == 0;
}

int32_t GAMEAPI map_read(CrtFile* file) {
    if (game().map.tiles) mem_free(game().map.tiles);
    if (MapAbstract) mem_free(MapAbstract);
    game().map.tiles = nullptr;
    MapAbstract = nullptr;
    if (crt_fread(&game().map.x, 0xAA4, 1, file) == 0) return 1;
    game().map.tiles = nullptr;
    MapAbstract = nullptr;
    if (map_init() != 0) return 1;
    if (crt_fread(game().map.tiles, (uint32_t)game().map.tile_count * sizeof(MAP), 1, file) == 0) return 1;
    if (crt_fread(MapAbstract, (uint32_t)MapAbstractCount, 1, file) == 0) return 1;
    fixup_landmarks();
    return 0;
}

int32_t GAMEAPI abstract_at(int32_t x, int32_t y) { return MapAbstract[(x >> 1) + (MapAbstractX >> 1) * y]; }

void GAMEAPI abstract_set(int32_t x, int32_t y, int32_t region) {
    MapAbstract[(x >> 1) + (MapAbstractX >> 1) * y] = (uint8_t)region;
}

void GAMEAPI alt_put_detail(int32_t x, int32_t y, int32_t detail) { map_tile(x, y)->contour = (uint8_t)detail; }

// Shows the "base submerged" messages to the viewing faction. Uses popp when the base survives
// (pop restored to 1 is not the case here) and X_pop_2 when it is saved at size 1.
static void base_submerged_popup(int32_t base_id, bool saved) {
    BASE& base = game().bases[base_id];
    int32_t viewer = CurrentPlayerFaction;
    if ((uint8_t)base.faction_id == (uint32_t)viewer) {
        Console_focus(&MainConsole, base.x, base.y, viewer);
        parse_says(0, base.name, -1, -1);
        if (saved) X_pop_2("BASESUBMERGED", "subbase_sm.pcx", 0);
        else popp("SCRIPT", "BASESUBMERGED1", 0, "subbase_sm.pcx", 0);
    } else if ((uint8_t)base.faction_id == (uint32_t)viewer || ((uint8_t)(1 << (viewer & 31)) & base.visibility)) {
        Console_focus(&MainConsole, base.x, base.y, viewer);
        parse_says(0, base.name, -1, -1);
        parse_says(1, game().mfactions[(uint8_t)base.faction_id].adj_name_faction, -1, -1);
        if (saved) X_pop_2("BASESUBMERGED2", "subbase_sm.pcx", 0);
        else popp("SCRIPT", "BASESUBMERGED3", 0, "subbase_sm.pcx", 0);
    }
}

void GAMEAPI alt_set(int32_t x, int32_t y, int32_t altitude) {
    if (CurrentTurn != 0) {
        int32_t old_alt = map_alt(x, y);
        if (altitude != old_alt) delete_landmark(x, y);
        int32_t to_ocean = altitude < 3;
        if ((old_alt < 3) == to_ocean) {
            if (!to_ocean) {
                MAP* sq = map_tile(x, y);
                int32_t rocky = sq->val3 >> 6;
                if (altitude > old_alt) {
                    // Raised land may become rockier.
                    if (rocky < 2 && rocky + 1 <= altitude - 3 && game_rand() % (rocky + 4) == 0) {
                        sq->val3 = (uint8_t)((rocky + 1) << 6) | (sq->val3 & 0x3F);
                        sq->landmarks |= 0x400000;
                        GameDrawState |= 1;
                    }
                } else if (altitude < old_alt) {
                    // Lowered land may become smoother; (rocky + 3) << 6 wraps to rocky - 1 in a byte.
                    if (rocky != 0 && rocky > altitude - 3 && game_rand() % (rocky + 1) != 0) {
                        sq->val3 = (uint8_t)((rocky + 3) << 6) | (sq->val3 & 0x3F);
                        sq->landmarks |= 0x400000;
                        GameDrawState |= 1;
                    }
                }
            }
        } else {
            // The tile crosses the shoreline: drop improvements and refresh every faction's view.
            MAP* sq = map_tile(x, y);
            sq->items &= 0x7E1377A3;
            for (uint32_t& v : sq->visible_items) v = sq->items;
            if (CurrentTurn != 0 && !WorldSkipTerritory) {
                int32_t base_id = base_at(x, y);
                if (base_id >= 0) {
                    if (!to_ocean) goto done;
                    int32_t offset, mask;
                    bitmask(FAC_PRESSURE_DOME, &offset, &mask);
                    BASE& base = game().bases[base_id];
                    if (base.facilities_built[offset] & (uint8_t)mask) goto done;
                    int32_t loss = game_rand() % 3 + 2;
                    int32_t half = base.pop_size / 2;
                    if (half >= loss && loss <= 10) loss = half > 10 ? 10 : half;
                    base.pop_size = (int8_t)(base.pop_size - loss);
                    set_fac(FAC_PRESSURE_DOME, base_id, 1);
                    uint32_t faction = (uint8_t)base.faction_id;
                    game().factions[faction].player_flags |= 0x80000;
                    MapBaseSubmergedCount[faction]++;
                    if (is_objective(base_id)) base.pop_size = base.pop_size < 1 ? 1 : base.pop_size;
                    if (base.pop_size > 0) goto done;
                    if (game().factions[faction].base_count < 2 || (MultiplayerActive && !ControlTurnC)) {
                        // The faction's last base (or a network game outside the control turn) survives.
                        base.pop_size = 1;
                        if (!VehBattleState[0]) base_submerged_popup(base_id, true);
                        goto done;
                    }
                    if (!VehBattleState[0]) base_submerged_popup(base_id, false);
                    base_kill(base_id);
                }
                // Units on the wrong side of the new shoreline drown (air units and units with
                // flag 0x20 excepted).
                for (int32_t veh_id = stack_fix(veh_at(x, y)); veh_id >= 0;) {
                    VEH& veh = game().vehs[veh_id];
                    int32_t next = veh.next_veh_id_stack;
                    int32_t triad = Chassis[Units[veh.unit_id].chassis_id].triad;
                    if (triad != TRIAD_AIR && !(veh.flags & VFLAG_IS_OBJECTIVE) &&
                        (triad == TRIAD_SEA) != (bool)to_ocean) {
                        MapBaseIdClosestSubmergedVeh[veh.faction_id] = base_find_faction(x, y, veh.faction_id);
                        veh_kill(veh_id);
                        if (next > veh_id) next--;
                    }
                    veh_id = next;
                }
            }
        }
    }
done:
    MAP* sq = map_tile(x, y);
    sq->climate = (uint8_t)(sq->climate & 0x1F) | (uint8_t)(altitude << 5);
    GameDrawState |= 4;
}

int32_t GAMEAPI alt_natural(int32_t x, int32_t y) { return natural_altitude(map_tile(x, y)->contour); }

void GAMEAPI alt_set_both(int32_t x, int32_t y, int32_t altitude) {
    alt_set(x, y, altitude);
    if (natural_altitude(map_tile(x, y)->contour) != altitude) {
        int32_t range = AltNatural[altitude + 1] - AltNatural[altitude];
        int32_t offset = range - 1 > 0 ? game_rand() % range : 0;
        map_tile(x, y)->contour = (uint8_t)(AltNatural[altitude] + MapSeaLevel + offset);
    }
}

int32_t GAMEAPI pfncGetElevation(int32_t x, int32_t y) {
    MAP* sq = map_tile(x, y);
    int32_t detail = sq->contour;
    int32_t elev = (detail - AltNaturalLinear[3] - MapSeaLevel) * 50;
    if (detail > AltNaturalLinear[sq->climate >> 5]) elev += (MapSeaLevel * 301 + y * 217 + x * 113) % 50;
    else elev += 10;
    if (elev < -3000) return -3000;
    if (elev > 3500) return 3500;
    return elev;
}

void GAMEAPI climate_set(int32_t x, int32_t y, int32_t rainfall) {
    MAP* sq = map_tile(x, y);
    sq->climate = (uint8_t)((rainfall & 3) << 3) | (sq->climate & 0xE7);
    sq->landmarks |= 0x400000;
    GameDrawState |= 1;
}

void GAMEAPI temp_set(int32_t x, int32_t y, int32_t temperature) {
    MAP* sq = map_tile(x, y);
    sq->climate = (sq->climate & 0xF8) | (temperature & 7);
}

void GAMEAPI owner_set(int32_t x, int32_t y, int32_t faction_id) {
    MAP* sq = map_tile(x, y);
    sq->val2 = (sq->val2 & 0xF0) | (faction_id & 0xF);
}

void GAMEAPI site_set(int32_t x, int32_t y, int32_t site) {
    MAP* sq = map_tile(x, y);
    sq->val2 = (sq->val2 & 0xF) | (uint8_t)(site << 4);
}

void GAMEAPI region_set(int32_t x, int32_t y, int32_t region) { map_tile(x, y)->region = (uint8_t)region; }

void GAMEAPI rocky_set(int32_t x, int32_t y, int32_t rocky) {
    MAP* sq = map_tile(x, y);
    sq->val3 = (sq->val3 & 0x3F) | (uint8_t)(rocky << 6);
    sq->landmarks |= 0x400000;
    GameDrawState |= 1;
}

void GAMEAPI using_set(int32_t x, int32_t y, int32_t faction_id) {
    MAP* sq = map_tile(x, y);
    sq->val3 = (sq->val3 & 0xF8) | (faction_id & 7);
}

void GAMEAPI lock_set(int32_t x, int32_t y, int32_t faction_id) {
    MAP* sq = map_tile(x, y);
    sq->val3 = (uint8_t)((faction_id & 7) << 3) | (sq->val3 & 0xC7);
}

int32_t GAMEAPI lock_map(int32_t x, int32_t y, int32_t faction_id) {
    MAP* sq = map_tile(x, y);
    uint32_t holder = (sq->val3 & 0x38) >> 3;
    if (holder != (uint32_t)faction_id) {
        if (holder != 0) return 1;
        sq->val3 = (uint8_t)((faction_id & 7) << 3) | (sq->val3 & 0xC7);
    }
    return 0;
}

void GAMEAPI unlock_map(int32_t x, int32_t y, int32_t faction_id) {
    MAP* sq = map_tile(x, y);
    if ((uint32_t)((sq->val3 & 0x38) >> 3) == (uint32_t)faction_id) sq->val3 &= 0xC7;
}

void GAMEAPI bit_put(int32_t x, int32_t y, uint32_t items) { map_tile(x, y)->items = items; }

void GAMEAPI bit_set(int32_t x, int32_t y, uint32_t items, int32_t add) {
    MAP* sq = map_tile(x, y);
    if (add) sq->items |= items;
    else sq->items &= ~items;
}

void GAMEAPI bit2_set(int32_t x, int32_t y, uint32_t landmarks, int32_t add) {
    MAP* sq = map_tile(x, y);
    if (add) sq->landmarks |= landmarks;
    else sq->landmarks &= ~landmarks;
}

void GAMEAPI code_set(int32_t x, int32_t y, int32_t code) {
    MAP* sq = map_tile(x, y);
    sq->landmarks = (sq->landmarks & 0xFFFFFF) | ((uint32_t)code << 24);
    GameDrawState |= 4;
}

void GAMEAPI synch_bit(int32_t x, int32_t y, int32_t faction_id) {
    if (faction_id != 0) {
        MAP* sq = map_tile(x, y);
        sq->visible_items[faction_id - 1] = sq->items;
    }
}

int32_t GAMEAPI treaty_user_at(int32_t x, int32_t y, int32_t faction_id) {
    if (faction_id < 1) return -1;
    MAP* sq = map_tile(x, y);
    if ((sq->climate & 0xE0) < 0x60) return -1;
    uint32_t user = sq->val3 & 7;
    if (user == 0 || user == (uint32_t)faction_id) return -1;
    uint32_t status = game().factions[faction_id].diplo_status[user];
    if ((status & DIPLO_PACT) || !(status & DIPLO_TREATY)) return -1;
    return (int32_t)user;
}

int32_t GAMEAPI minerals_at(int32_t x, int32_t y) {
    if (y == 0 || y == game().map.y - 1) return 2;
    int32_t s = (x + y) / 2;
    int32_t a = (x - s) / 2;
    int32_t v = a * 3 + MapRandomSeed + (s / 2) * 2;
    int32_t k = (v - ((x - s) & 1) * 2 - (s & 1)) & 3;
    int32_t alt = map_alt(x, y);
    int32_t d = alt - 3;
    if (d < 0) d = 3 - alt;
    if (alt < 3) d--;
    switch (d) {
    case 0:
        switch (k) {
        case 0: return 1;
        case 2: return (v & 4) ? 2 : 1;
        default: return 0;
        }
    case 1:
        if (k == 0) return 0;
        return k == 3 ? 2 : 1;
    case 2:
        if (k == 0) return 0;
        return k == 1 ? 1 : 2;
    case 3: return k > 1 ? 2 : 1;
    default: return ~k & 1;
    }
}

int32_t GAMEAPI bonus_at(int32_t x, int32_t y) {
    MAP* sq = map_tile(x, y);
    uint32_t items = sq->items;
    int32_t alt = sq->climate >> 5;
    uint32_t forced = items & 0x400;  // BIT_SUPPLY_REMOVE: bonus forced by the editor
    if (!forced) {
        if (MapRandomSeed == 0) return 0;
        if (alt >= 3 && !(GameRules & RULES_NO_UNITY_SCATTERING)) return 0;
    }
    int32_t s = (x + y) >> 1;
    int32_t d = x - s;
    int32_t idx = (s & 3) + (d & 3) * 4;
    if (!forced && idx != ((MapRandomSeed - 5 * (s >> 2) - 3 * (d >> 2)) & 0xF)) return 0;
    if (alt < 2) return 0;
    int32_t bonus = alt < 3 ? idx % 3 + 1 : (idx % 5) & 3;
    if (bonus == 0 || (items & 0x20000000)) {
        if (items & 0x10000) return 3;
        bonus = (items & 0x20000) ? 2 : 1;
    }
    return bonus;
}

int32_t GAMEAPI goody_at(int32_t x, int32_t y) {
    MAP* sq = map_tile(x, y);
    uint32_t items = sq->items;
    if (items & 0x102000) return 0;
    if (GameRules & RULES_NO_UNITY_SCATTERING) return (items & 0xC000000) ? 2 : 0;
    if (items & 0x10000000) return 1;
    if (MapRandomSeed == 0) return 0;
    int32_t s = (x + y) >> 1;
    int32_t d = x - s;
    int32_t idx = (s & 3) + (d & 3) * 4;
    if ((sq->climate & 0xE0) >= 0x60 && idx == ((MapRandomSeed - 5 * (s >> 2) - 3 * (d >> 2)) & 0xF)) return 2;
    return idx == (((d / 4) * GoodyCoefX + MapRandomSeed + (s / 4) * GoodyCoefY + 8) & 0x1F);
}

void GAMEAPI say_loc(char* buffer, int32_t x, int32_t y, int32_t near_base, int32_t coords, int32_t with_near) {
    char num[80];
    int32_t base_id = base_at(x, y);
    if (near_base && base_id < 0) {
        with_near = 0;
        base_id = base_find_filtered(x, y, -1, -1, -1, CurrentPlayerFaction);
        if (base_id >= 0) {
            std::strcat(buffer, label_get(0xF8 / 4));
            std::strcat(buffer, " ");
        }
    }
    if (base_id >= 0) {
        if (with_near) {
            std::strcat(buffer, label_get(0x20 / 4));
            std::strcat(buffer, " ");
        }
        std::strcat(buffer, game().bases[base_id].name);
        if (coords) std::strcat(buffer, " ");
    }
    if (coords == 1 || (coords == 2 && base_id < 0)) {
        std::strcat(buffer, "(");
        crt_itoa(x, num, 10);
        std::strcat(buffer, num);
        std::strcat(buffer, ", ");
        crt_itoa(y, num, 10);
        std::strcat(buffer, num);
        std::strcat(buffer, ")");
        if ((GameState & STATE_SCENARIO_EDITOR) && (GameState & STATE_DEBUG_MODE)) {
            // BUG(original): the region number goes to StrBuffer, not to buffer.
            char* out = g_compat.region_number_strbuffer ? StrBuffer : buffer;
            std::strcat(out, " ");
            crt_itoa(map_tile(x, y)->region, num, 10);
            std::strcat(out, num);
        }
    }
}

// Clears the AI site value within radius 2 of (x, y).
static void clear_sites_radius2(int32_t x, int32_t y) {
    for (int32_t i = 0; i < 21; i++) {
        int32_t x2 = map_wrap_x(x + TableOffsetX[i]);
        int32_t y2 = y + TableOffsetY[i];
        if (map_in_bounds(x2, y2)) map_tile(x2, y2)->val2 &= 0xF;
    }
}

void GAMEAPI site_radius(int32_t x, int32_t y) { clear_sites_radius2(x, y); }

void GAMEAPI fungus_site_radius(int32_t x, int32_t y) {
    map_tile(x, y)->items |= BIT_FUNGUS;
    map_tile(x, y)->items &= 0xFE1FFFAF;
    clear_sites_radius2(x, y);
}

int32_t GAMEAPI find_landmark(int32_t x, int32_t y, int32_t radius_index) {
    for (int32_t i = 0; i < TableRange[radius_index]; i++) {
        int32_t x2 = map_wrap_x(x + TableOffsetX[i]);
        int32_t y2 = y + TableOffsetY[i];
        if (!map_in_bounds(x2, y2)) continue;
        for (int32_t j = 0; j < MapLandmarkCount; j++) {
            if (x2 == Landmarks[j].x && y2 == Landmarks[j].y) return j;
        }
    }
    return -1;
}

int32_t GAMEAPI new_landmark(int32_t x, int32_t y, const char* name) {
    if (MapLandmarkCount > 63) return -1;
    int32_t id = MapLandmarkCount++;
    Landmarks[id].x = x;
    Landmarks[id].y = y;
    std::strcpy(Landmarks[id].name, name);
    return id;
}

int32_t GAMEAPI valid_landmark(int32_t x, int32_t y, int32_t faction_id) {
    int32_t owner = MultiplayerActive == 0 ? whose_territory(faction_id, x, y, nullptr, 0) : map_tile(x, y)->owner;
    if (owner == faction_id) return 1;
    if (owner > 0) return 0;
    int32_t base_id = base_find(x, y);
    if (base_id < 0) return 1;
    return (uint8_t)game().bases[base_id].faction_id == (uint32_t)faction_id;
}

void GAMEAPI kill_landmark(int32_t x, int32_t y) {
    for (int32_t i = 0; i < 9; i++) {
        int32_t x2 = map_wrap_x(x + TableOffsetX[i]);
        int32_t y2 = y + TableOffsetY[i];
        if (!map_in_bounds(x2, y2)) continue;
        for (int32_t j = 0; j < MapLandmarkCount; j++) {
            if (x2 == Landmarks[j].x && y2 == Landmarks[j].y) {
                for (; j < MapLandmarkCount - 1; j++) Landmarks[j] = Landmarks[j + 1];
                MapLandmarkCount--;
                return;
            }
        }
    }
}

static void clear_overlay(uint8_t* overlay, int32_t code) {
    for (int32_t i = 0; i < 5; i++) overlay[LandmarkTileIndex[code * 5 + i]] = 0;
}

void GAMEAPI delete_landmark(int32_t x, int32_t y) {
    MAP* sq = map_tile(x, y);
    uint32_t lm = sq->landmarks;
    if (lm == 0) return;
    sq->landmarks = lm & ~0x80u;
    int32_t code = (int32_t)sq->landmarks >> 24;
    if (code < 0) return;
    if (lm & LM_CRATER) clear_overlay(LandmarkOverlayCrater, code);
    if (lm & LM_VOLCANO) clear_overlay(LandmarkOverlayVolcano, code);
    if (lm & LM_CANYON) clear_overlay(LandmarkOverlayCanyon, code);
    if (lm & LM_MESA) clear_overlay(LandmarkOverlayMesa, code);
    if (lm & LM_RIDGE) clear_overlay(LandmarkOverlayMesa, code);  // the ridge uses the mesa overlay
    // A negative code marks a removed landmark (fixup_landmarks recognizes it).
    sq->landmarks = ((uint32_t)(-1 - code) << 24) | (sq->landmarks & 0xFFFFFF);
    GameDrawState |= 4;
    ui_mapwins_clear_alt(x, y);
}

void GAMEAPI fixup_landmarks() {
    std::memcpy(LandmarkOverlayRidge, LandmarkOverlayRidgeInit, 113);
    std::memcpy(LandmarkOverlayMesa, LandmarkOverlayMesaInit, 113);
    std::memcpy(LandmarkOverlayCanyon, LandmarkOverlayCanyonInit, 113);
    std::memcpy(LandmarkOverlayVolcano, LandmarkOverlayVolcanoInit, 113);
    std::memcpy(LandmarkOverlayCrater, LandmarkOverlayCraterInit, 113);
    int32_t row = 0;
    for (int32_t y = 0; y < game().map.y; y++, row += game().map.half_x) {
        for (int32_t x = y & 1; x < game().map.x; x += 2) {
            MAP* sq = &game().map.tiles[(x >> 1) + row];
            uint32_t lm = sq->landmarks;
            int32_t code = (int32_t)lm >> 24;
            if (lm != 0 && code < 0) {
                code = -1 - code;
                if (lm & LM_VOLCANO) clear_overlay(LandmarkOverlayVolcano, code);
                if (lm & LM_CRATER) clear_overlay(LandmarkOverlayCrater, code);
            }
        }
    }
}

// Walks the tiles in storage order the way set_dirty/clear_dirty do.
static void mark_all_dirty() {
    MAP* sq = game().map.tiles;
    for (int32_t y = 0; y < game().map.y; y++) {
        for (int32_t x = y & 1; x < game().map.x; x += 2, sq++) sq->landmarks |= 0x400000;
    }
}

void GAMEAPI set_dirty() {
    mark_all_dirty();
    GameDrawState |= 4;
}

// BUG(original): sets the redraw flag of every tile instead of clearing it.
void GAMEAPI clear_dirty() {
    if (g_compat.clear_dirty_sets) {
        mark_all_dirty();
        return;
    }
    MAP* sq = game().map.tiles;
    for (int32_t y = 0; y < game().map.y; y++) {
        for (int32_t x = y & 1; x < game().map.x; x += 2, sq++) sq->landmarks &= ~0x400000u;
    }
}

void GAMEAPI map_resize(int32_t width, int32_t height) {
    MapResizeOldX = game().map.x;
    MapResizeOldY = game().map.y;
    if (width >= game().map.x || height >= game().map.y) return;
    MapResizeOldTiles = game().map.tiles;
    game().map.tiles = nullptr;
    game().map.x = width;
    game().map.y = height;
    // BUG(original): the previous MapAbstract array is not freed.
    if (!g_compat.map_resize_abstract_leak && MapAbstract) {
        mem_free(MapAbstract);
        MapAbstract = nullptr;
    }
    map_init();
    wipe_tiles();
    for (int32_t y = 0; y < game().map.y; y++) {
        for (int32_t x = y & 1; x < game().map.x; x += 2) {
            int32_t old_y = (MapResizeOldY * y) / game().map.y;
            int32_t old_x = ((MapResizeOldX * x) / game().map.x) >> 1;
            MAP* src = &MapResizeOldTiles[(MapResizeOldX >> 1) * old_y + old_x];
            MAP* dst = map_tile(x, y);
            dst->contour = src->contour;
            dst->items = src->items;
            dst->landmarks = src->landmarks;
            dst->val3 = src->val3 & 0xC0;
        }
    }
    if (MapResizeOldTiles) mem_free(MapResizeOldTiles);
    world_climate();
}

void GAMEAPI map_scale_set(int32_t extra) { MapScaleExtra = extra < 1 ? 0 : extra; }

int32_t GAMEAPI map_scale(int32_t value) { return (MapScaleExtra + 16) * value / 16; }

int32_t GAMEAPI in_rect(int32_t x, int32_t y, int32_t x1, int32_t y1, int32_t x2, int32_t y2) {
    return x1 <= x && x <= x2 && y1 <= y && y <= y2;
}

int32_t GAMEAPI on_map(int32_t x, int32_t y) { return map_in_bounds(x, y); }
