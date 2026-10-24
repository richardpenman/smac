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
// Next step direction (0-7) from (x1, y1) towards (x2, y2), or -1 (Paths object).
ADDR(0x59A530) int32_t THISCALL Path_find(Path* self, int32_t x1, int32_t y1, int32_t x2, int32_t y2, int32_t unit_id,
                                          int32_t faction_id, int32_t flags, int32_t a9);
// Nearest tile without sensor coverage of faction_id to (*x, *y); 0 if found (written back).
ADDR(0x59CCA0) int32_t THISCALL Path_sensors(Path* self, int32_t faction_id, int32_t* x, int32_t* y);
