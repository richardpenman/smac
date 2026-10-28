// The records of the game state and their serialisation (engine/serialize.h): what savegames,
// map files and the harness's .state snapshots hold, in the original's byte layout.
#pragma once
#include <cstdint>
#include <functional>

#include "engine/serialize.h"
#include "engine/types/all.h"

// serialize(io, record) for every record type the game state holds. The same field list writes
// (Writer) and reads (Reader) the record.
#define SMAC_RECORD(T)                \
    void serialize(Writer& io, T& r); \
    void serialize(Reader& io, T& r);
SMAC_RECORD(GamePrefs)
SMAC_RECORD(Faction)
SMAC_RECORD(Goal)
SMAC_RECORD(MFaction)
SMAC_RECORD(MapHeader)
SMAC_RECORD(Landmark)
SMAC_RECORD(MAP)
SMAC_RECORD(Continent)
SMAC_RECORD(UNIT)
SMAC_RECORD(VEH)
SMAC_RECORD(BASE)
SMAC_RECORD(ReplayEvent)
SMAC_RECORD(MonumentEvent)
SMAC_RECORD(Monument)
SMAC_RECORD(ScenarioRules)
SMAC_RECORD(PbemState)
SMAC_RECORD(CouncilSession)
#undef SMAC_RECORD

// ---- .state snapshots (harness/state_regions.h, harness/README.md "Snapshot files") ----
// Calls put(addr, bytes, size) for every region of harness/state_regions.h, in its order, with the
// region's bytes serialised from the records (addr is the region's original address, or the
// pseudo address of a heap array). Byte-identical to the original's memory in those regions.
using StateRegionSink = std::function<void(uint32_t addr, const uint8_t* bytes, uint32_t size)>;
void state_regions_write(const StateRegionSink& put);
