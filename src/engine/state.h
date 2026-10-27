// The game state's records as native objects (docs/CLEANUP_PLAN.md section 3, phase B).
//
// Everything a savegame and a .state snapshot hold (harness/state_regions.h) lives here, in
// ordinary C++ objects, not in the memory image at the original's addresses. Native builds bind
// the globals of those addresses (globals.h: Factions, Bases, VehCount, MapAreaX, ...) to these
// objects (mem::host resolves an original address inside a record to the record), so the engine
// and the UI keep their names. The image only seeds them with the original's initial data
// (mem::init) and still holds the rest: rule tables, the scratch globals of the engine, the UI.
//
// A record's original address stays its identity: the savegame block and the snapshot region it
// is written to (engine/state_io.h), and the target of the original's address arithmetic (pointers
// kept as 32-bit addresses such as CurrentBasePtr, reads past the end of a table, the network
// blocks). That arithmetic needs the record's type to keep the original's layout, which the
// SMAC_LAYOUT assertions pin; a record whose type changes must first lose its address users.
//
// Hybrid builds (32-bit, inside terranx.exe) have no such objects: their globals are the
// original's memory.
//
// Not here although saved: CouncilWinState and CouncilWinVotes (0x6FF69C, 0x6FF6D4) are fields of
// the council window object (CouncilWin + 0xA1C, engine/council.h), a UI object of the image that
// the council code reaches through its `self` pointer; they move with the UI windows.
#pragma once
#include <cstdint>
#include <vector>

#include "engine/types/all.h"

#ifndef SMACX_HYBRID
namespace state {
// By original address (defined in state.cpp). Each is its own object, so a read past one record's
// end cannot silently land in the next (ASan sees it; the original's reads past an end go through
// mem::read_bytes).
extern int32_t council_session_pending;  // 0x703DE8 CouncilSessionPending
extern int32_t council_proposal[8];  // 0x703DEC CouncilProposal
extern int32_t council_vote_state[8];  // 0x703E0C CouncilVoteState
extern int32_t diplo_state_c[8][8][36];  // 0x7388D8 DiploStateC
extern int32_t faction_combat_win[8];  // 0x93A96C FactionCombatWin
extern int32_t faction_combat_loss[8];  // 0x93A98C FactionCombatLoss
extern PbemState pbem;  // 0x93A9AC PbemGameID .. PbemTamperReloads
extern int32_t diplo_state_b[8][8];  // 0x93F698 DiploStateB
extern int32_t faction_rankings_unk[8];  // 0x945DD8 FactionRankingsUnk
extern int32_t tectonic_detonation_count[8];  // 0x946138 TectonicDetonationCount
extern int32_t replay_event_size;  // 0x9469F8 ReplayEventSize (bytes)
extern MFaction mfactions[8];  // 0x946A50 MFactions
extern MapHeader map_header;  // 0x949870 MapAreaX .. MapFilePath
extern ScenarioRules scenario_rules;  // 0x94B4C0 ObjectiveReqVictory ..
extern uint8_t bung_list[8][64][8];  // 0x94B558 BungList
extern Monument monuments[8];  // 0x94CA08 Monuments
extern ReplayEvent replay_events[1024];  // 0x950324 ReplayEvents
extern VEH vehs[2049];  // 0x952828 Vehs ([2048] + veh_fake's scratch slot)
extern Faction factions[8];  // 0x96C9E0 Factions
extern BASE bases[512];  // 0x97D040 Bases
extern GamePrefs prefs;  // 0x9A6490 cs: GamePreferences .. ClimateLevel
extern int16_t faction_turn_might[1000][8];  // 0x9A68AC FactionTurnMight
extern Continent continents[128];  // 0x9AA730 Continents
extern UNIT units[512];  // 0x9AB868 Units
extern char game_alt_path[256];  // 0x9B2078 GameAltPath
extern char scenario_file_name[256];  // 0x9B2178 ScenarioFileName
}  // namespace state
#endif

// One record: its original address and size, the native object, and how it is read from the
// original's bytes (the initial data image).
struct StateRecord {
    const char* name;
    uint32_t addr;
    uint32_t size;
    void* object;
    void (*load)(const uint8_t* bytes);
};
// The records, sorted by address (none in hybrid builds).
const StateRecord* state_records(size_t* count);

// A copy of the memory image and of every record (golden replays put the state back after each
// call they make).
std::vector<uint8_t> state_capture();
bool state_equals(const std::vector<uint8_t>& copy);
void state_restore(const std::vector<uint8_t>& copy);
