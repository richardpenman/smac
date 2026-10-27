// The original's reads outside a table (compat out_of_range_reads, engine/compat.h). With the
// original's bugs these read what the original's memory holds there: the memory image, or the
// original's rule tables through rules_orig_byte (Vehs[-1] lies in the Reactor table, whose
// names are pointers in the original). With the fix an index outside the table gives the
// caller's value of "none".
#pragma once
#include <cstddef>
#include <cstdint>

#include "engine/compat.h"
#include "engine/game_state.h"
#include "engine/globals.h"
#include "engine/rules.h"
#include "engine/table_at.h"

// Address of Vehs in the original's memory (0x952828), records of 0x34 bytes.
inline uint32_t compat_veh_addr(int32_t veh_id, uint32_t offset) { return 0x952828 + (uint32_t)veh_id * 0x34 + offset; }

// Vehs[veh_id].unit_id; outside the table: the original's memory, or none (fixed).
inline int32_t compat_veh_unit(int32_t veh_id, int32_t none = -1) {
    if ((uint32_t)veh_id < 2049) return game().vehs[veh_id].unit_id;
    if (!g_compat.out_of_range_reads) return none;
    return rules_orig_i16(compat_veh_addr(veh_id, offsetof(VEH, unit_id)));
}

// Vehs[veh_id].faction_id; outside the table: the original's memory, or none (fixed).
inline uint32_t compat_veh_faction(int32_t veh_id, uint32_t none) {
    if ((uint32_t)veh_id < 2049) return game().vehs[veh_id].faction_id;
    if (!g_compat.out_of_range_reads) return none;
    return rules_orig_byte(compat_veh_addr(veh_id, offsetof(VEH, faction_id)));
}

// Vehs[veh_id].x (i 0) or .y (i 1); outside the table: the original's memory, or none (fixed).
inline int32_t compat_veh_pos(int32_t veh_id, int32_t i, int32_t none) {
    if ((uint32_t)veh_id < 2049) return i ? game().vehs[veh_id].y : game().vehs[veh_id].x;
    if (!g_compat.out_of_range_reads) return none;
    return rules_orig_i16(compat_veh_addr(veh_id, i ? 2 : 0));
}

// Bases[base_id].faction_id (any index: Bases[-1] is the end of Factions[7] in the memory image),
// or none for an index outside the table (fixed).
inline uint32_t compat_base_faction(int32_t base_id, uint32_t none) {
    if (!g_compat.out_of_range_reads && (base_id < 0 || base_id >= 512)) return none;
    return table_bytes(game().bases, kBasesAddr, base_id)[offsetof(BASE, faction_id)];
}

// TechOwners[tech_id]; ids past the 89 entries (-1, prototype ids 97 + unit) read the memory
// image, or own nothing (fixed).
inline uint8_t compat_tech_owners(int32_t tech_id) {
    if ((uint32_t)tech_id < 89) return TechOwners[tech_id];
    if (!g_compat.out_of_range_reads) return 0;
    return rules_orig_byte(0x9A6670 + (uint32_t)tech_id);
}
