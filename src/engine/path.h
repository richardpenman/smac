// Path finding (original Path class around 0x59A1B0-0x59D980).
#pragma once
#include "engine/globals.h"

// Allocates the path tables for the current map size (frees previous ones).
ADDR(0x59A220) void THISCALL Path_init(Path* self);
// Builds the abstract (5x5 tile cell) map: region of the first land tile near each cell centre.
ADDR(0x59C200) void THISCALL Path_make_abstract(Path* self);
// Flood-fills the region of (x, y) with `region`, counting its tiles in Continents[region].
ADDR(0x59C520) void THISCALL Path_continent(Path* self, int32_t x, int32_t y, int32_t region);
// Assigns region numbers to every landmass (1-62) and body of water (65-126) and computes
// the per-region sea-coast masks.
ADDR(0x59C790) void THISCALL Path_continents(Path* self);
// Frees the path tables.
ADDR(0x59A2D0) void THISCALL Path_shutdown(Path* self);
// Next step direction (0-7) from (x1, y1) towards (x2, y2), or -1 if unreachable. Costs are flooded
// from the destination into self->mapTable and reused while destination, unit plan, flags and
// faction stay the same. faction_id -1: no faction rules. prev_dir: direction of the previous step
// (turning back costs extra), -1 none. Flags: 1 count steps only, 2 roads and bases only, 4 no
// fungus, 8 at least one move per step, 0x10 fungus costs extra, 0x40 avoid zones of control,
// 0x80 AI: avoid foreign territory, 0x200 territory mode (unit_id: a second faction whose land
// is open; 0x400 closes unowned land, 0x800 opens land of non-hostile factions).
ADDR(0x59A530)
int32_t THISCALL Path_find(Path* self, int32_t x1, int32_t y1, int32_t x2, int32_t y2, int32_t unit_id,
                           int32_t faction_id, int32_t flags, int32_t prev_dir);
// Not an original function: Path_find with the value of its uninitialised start-ZOC local given
// explicitly (BUG(original), compat residue.path_start_zoc; see docs/RECONSTRUCTION.md "Known
// uninitialised reads").
int32_t path_find_core(Path* self, int32_t x1, int32_t y1, int32_t x2, int32_t y2, int32_t unit_id, int32_t faction_id,
                       int32_t flags, int32_t prev_dir, int32_t start_zoc);
// Next step direction of a vehicle towards its first waypoint (Path_find, else the greedy best
// neighbour); -1 if none (the order is then cancelled). faction_id: the faction whose rules apply.
ADDR(0x59BC10) int32_t THISCALL Path_move(Path* self, int32_t veh_id, int32_t faction_id);
// Faction (+1) of visible units exerting a zone of control on (x, y) against faction_id; 0 none.
ADDR(0x59A370) int32_t THISCALL Path_zoc_path(Path* self, int32_t x, int32_t y, int32_t faction_id);
// Best tile for a sensor array of faction_id in the region of (*x, *y) (in its territory, not
// covered by a sensor yet); 0 if found (written back), else 1.
ADDR(0x59CCA0) int32_t THISCALL Path_sensors(Path* self, int32_t faction_id, int32_t* x, int32_t* y);
// Moves region region_from into region_to (tile counts and map).
ADDR(0x59C340) void THISCALL Path_merge(Path* self, int32_t region_from, int32_t region_to);
// Flood-fills the land territory of `owner` connected to (x, y), setting the tiles' unk_1 flag.
ADDR(0x59C3C0) void THISCALL Path_territory(Path* self, int32_t x, int32_t y, int32_t unused, int32_t owner);
// Destructor of the Paths object (same as Path_shutdown).
ADDR(0x59A320) void THISCALL Path_dtor(Path* self);
