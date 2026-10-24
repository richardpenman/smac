#include "engine/battle.h"

#include <cstring>

#include "engine/diplo_act.h"
#include "engine/map.h"
#include "engine/tech.h"

void GAMEAPI clear() { StrBuffer[0] = 0; }

void GAMEAPI says(const char* text) { std::strcat(StrBuffer, text); }

int32_t GAMEAPI alt_at(int32_t x, int32_t y) { return map_alt(x, y); }

int32_t GAMEAPI alt_detail_at(int32_t x, int32_t y) { return map_tile(x, y)->contour; }

uint32_t GAMEAPI bit_at(int32_t x, int32_t y) { return map_tile(x, y)->items; }

int32_t GAMEAPI is_ocean(int32_t x, int32_t y) { return map_is_ocean(x, y); }

int32_t GAMEAPI region_at(int32_t x, int32_t y) { return map_tile(x, y)->region; }

int32_t GAMEAPI veh_who(int32_t x, int32_t y) {
    MAP* sq = map_tile(x, y);
    if (!(sq->items & 2)) return -1;
    uint32_t faction = sq->val2 & 0xF;
    return faction > 7 ? -1 : (int32_t)faction;
}

int32_t GAMEAPI has_fac_built(int32_t facility_id) {
    int32_t base_id = CurrentBaseID;
    if (facility_id > 0x40) return 0;
    int32_t offset, mask;
    bitmask(facility_id, &offset, &mask);
    return (Bases[base_id].facilities_built[offset] & (uint8_t)mask) != 0;
}

int32_t GAMEAPI base_project(int32_t project_id) { return SecretProjects[project_id]; }

uint32_t GAMEAPI has_treaty(int32_t faction_id, int32_t faction_id_with, uint32_t treaty) {
    return Factions[faction_id].diplo_status[faction_id_with] & treaty;
}

int32_t GAMEAPI drop_range(int32_t faction_id) {
    if (!has_tech(Rules.tech_preq_orb_insert_wo_space, faction_id)) {
        int32_t base_id = SecretProjects[27]  /* The Space Elevator */;
        if (base_id < 0 || (uint8_t)Bases[base_id].faction_id != (uint32_t)faction_id)
            return Rules.max_airdrop_rng_wo_orbital_insert;
    }
    return MapAreaX > MapAreaY ? MapAreaX : MapAreaY;
}
