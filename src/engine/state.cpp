// The game state's records as native objects (engine/state.h).
#include "engine/state.h"

#include <cstring>

#include "engine/memmap.h"
#include "engine/state_io.h"

namespace state {
CouncilSession council_session;
int32_t council_session_pending;
int32_t council_proposal[8];
int32_t council_vote_state[8];
int32_t diplo_state_c[8][8][36];
int32_t faction_combat_win[8];
int32_t faction_combat_loss[8];
PbemState pbem;
int32_t diplo_state_b[8][8];
int32_t faction_rankings_unk[8];
int32_t tectonic_detonation_count[8];
int32_t replay_event_size;
MFaction mfactions[8];
MapHeader map_header;
ScenarioRules scenario_rules;
uint8_t bung_list[8][64][8];
Monument monuments[8];
ReplayEvent replay_events[1024];
VEH vehs[2049];
Faction factions[8];
BASE bases[512];
GamePrefs prefs;
int16_t faction_turn_might[1000][8];
Continent continents[128];
UNIT units[512];
char game_alt_path[256];
char scenario_file_name[256];
}  // namespace state

namespace {

// Reads a record from the original's bytes (its serialisation, engine/state_io.h).
template <class T, T& V>
void load_record(const uint8_t* bytes) {
    Reader r(bytes, sizeof(T));
    r(V);
}

#define RECORD(name, addr, object) \
    StateRecord { name, addr, sizeof(object), &(object), &load_record<decltype(object), object> }

// Sorted by address; constant-initialised, so the globals' bindings (mem::host, during static
// initialisation) can use it in any order.
constexpr StateRecord kRecords[] = {
    RECORD("CouncilWinSession", 0x6FF69C, state::council_session),
    RECORD("CouncilSessionPending", 0x703DE8, state::council_session_pending),
    RECORD("CouncilProposal", 0x703DEC, state::council_proposal),
    RECORD("CouncilVoteState", 0x703E0C, state::council_vote_state),
    RECORD("DiploStateC", 0x7388D8, state::diplo_state_c),
    RECORD("FactionCombatWin", 0x93A96C, state::faction_combat_win),
    RECORD("FactionCombatLoss", 0x93A98C, state::faction_combat_loss),
    RECORD("PbemState", 0x93A9AC, state::pbem),
    RECORD("DiploStateB", 0x93F698, state::diplo_state_b),
    RECORD("FactionRankingsUnk", 0x945DD8, state::faction_rankings_unk),
    RECORD("TectonicDetonationCount", 0x946138, state::tectonic_detonation_count),
    RECORD("ReplayEventSize", 0x9469F8, state::replay_event_size),
    RECORD("MFactions", 0x946A50, state::mfactions),
    RECORD("MapHeader", 0x949870, state::map_header),
    RECORD("ScenarioRules", 0x94B4C0, state::scenario_rules),
    RECORD("BungList", 0x94B558, state::bung_list),
    RECORD("Monuments", 0x94CA08, state::monuments),
    RECORD("ReplayEvents", 0x950324, state::replay_events),
    RECORD("Vehs", 0x952828, state::vehs),
    RECORD("Factions", 0x96C9E0, state::factions),
    RECORD("Bases", 0x97D040, state::bases),
    RECORD("GamePrefs", 0x9A6490, state::prefs),
    RECORD("FactionTurnMight", 0x9A68AC, state::faction_turn_might),
    RECORD("Continents", 0x9AA730, state::continents),
    RECORD("Units", 0x9AB868, state::units),
    RECORD("GameAltPath", 0x9B2078, state::game_alt_path),
    RECORD("ScenarioFileName", 0x9B2178, state::scenario_file_name),
};
#undef RECORD

constexpr bool sorted_and_disjoint() {
    for (size_t i = 1; i < sizeof(kRecords) / sizeof(kRecords[0]); i++)
        if (kRecords[i].addr < kRecords[i - 1].addr + kRecords[i - 1].size) return false;
    return true;
}
static_assert(sorted_and_disjoint(), "kRecords: sorted by address, no overlaps");

}  // namespace

const StateRecord* state_records(size_t* count) {
    *count = sizeof(kRecords) / sizeof(kRecords[0]);
    return kRecords;
}

std::vector<uint8_t> state_capture() {
    std::vector<uint8_t> copy(mem::g_image, mem::g_image + sizeof(mem::g_image));
    for (const StateRecord& r : kRecords) {
        const uint8_t* p = static_cast<const uint8_t*>(r.object);
        copy.insert(copy.end(), p, p + r.size);
    }
    return copy;
}

bool state_equals(const std::vector<uint8_t>& copy) {
    if (std::memcmp(copy.data(), mem::g_image, sizeof(mem::g_image))) return false;
    size_t at = sizeof(mem::g_image);
    for (const StateRecord& r : kRecords) {
        if (std::memcmp(copy.data() + at, r.object, r.size)) return false;
        at += r.size;
    }
    return true;
}

void state_restore(const std::vector<uint8_t>& copy) {
    std::memcpy(mem::g_image, copy.data(), sizeof(mem::g_image));
    size_t at = sizeof(mem::g_image);
    for (const StateRecord& r : kRecords) {
        std::memcpy(r.object, copy.data() + at, r.size);
        at += r.size;
    }
}
