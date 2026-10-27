// The game-state records are native objects (engine/state.h); the globals of their addresses and
// the original's address arithmetic (mem::host, mem::read_bytes) reach them.
#include <cstring>

#include "engine/game_state.h"
#include "engine/state.h"
#include "engine/table_at.h"
#include "unit.h"

TEST_CASE("state: the globals of a record are the native object") {
    CHECK(&Factions[0] == &state::factions[0]);
    CHECK(&Bases[0] == &state::bases[0]);
    CHECK(&Vehs[0] == &state::vehs[0]);
    CHECK(&Units[0] == &state::units[0]);
    CHECK(&cs == &state::prefs);
    CHECK(&VehCount == &state::prefs.VehCount);  // aliases inside a record
    CHECK(&CurrentTurn == &state::prefs.CurrentTurn);
    CHECK(&MapAreaX == &state::map_header.area_x);
    CHECK(&Landmarks[0] == &state::map_header.landmarks[0]);
    CHECK(&ObjectiveReqVictory == &state::scenario_rules.req_victory);
    CHECK(&PbemTamperReloads == &state::pbem.tamper_reloads[0]);
    CHECK(reinterpret_cast<uint8_t*>(&ReplayEvents[0]) == reinterpret_cast<uint8_t*>(&state::replay_events[0]));
    // Not in the image any more.
    const uint8_t* b = reinterpret_cast<const uint8_t*>(&Bases[0]);
    CHECK((b < mem::g_image || b >= mem::g_image + sizeof(mem::g_image)));
}

TEST_CASE("state: original addresses resolve to the records and back") {
    CHECK(mem::host(0x97D040 + 3 * 0x134) == reinterpret_cast<uint8_t*>(&Bases[3]));
    CHECK(mem::host(0x9A64C8) == reinterpret_cast<uint8_t*>(&VehCount));
    CHECK(mem::host(0x9A6490 - 4) == mem::g_image + (0x9A6490 - 4 - mem::kImageBase));  // the image
    CHECK(mem::address_of(&Bases[3].name) == 0x97D040 + 3 * 0x134 + offsetof(BASE, name));
    CHECK(mem::address_of(&Factions[2]) == 0x96C9E0 + 2 * 0x20CC);
    CHECK(mem::address_of(mem::g_image + 0x100) == mem::kImageBase + 0x100);
    int local = 0;
    CHECK(mem::address_of(&local) == 0);
}

TEST_CASE("state: reads past a table's end land where the original's do") {
    // Bases[-1] is the end of Factions[7]; Factions[-1] lies in the vehicle table.
    CHECK(&table_at(game().bases, kBasesAddr, -1) ==
          reinterpret_cast<BASE*>(reinterpret_cast<uint8_t*>(&Factions[8]) - sizeof(BASE)));
    CHECK(table_bytes(game().factions, kFactionsAddr, -1) ==
          reinterpret_cast<uint8_t*>(&Vehs[0]) + (0x96C9E0 - 0x20CC - 0x952828));
    CHECK(&table_at(game().bases, kBasesAddr, 5) == &Bases[5]);

    // A range across two records and the image between them, in one call.
    SaveGlobal<Faction[8]> f{Factions};
    SaveGlobal<BASE[512]> bases{Bases};
    std::memset(reinterpret_cast<uint8_t*>(&Factions[8]) - 4, 0x11, 4);
    std::memset(&Bases[0], 0x22, 4);
    uint8_t got[8];
    mem::read_bytes(0x97D040 - 4, got, 8);
    const uint8_t expect[8] = {0x11, 0x11, 0x11, 0x11, 0x22, 0x22, 0x22, 0x22};
    CHECK(std::memcmp(got, expect, 8) == 0);
    const uint8_t put[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    mem::write_bytes(0x97D040 - 4, put, 8);
    CHECK(reinterpret_cast<uint8_t*>(&Factions[8])[-1] == 4);
    CHECK(reinterpret_cast<uint8_t*>(&Bases[0])[0] == 5);
}

TEST_CASE("state: a capture puts every record back") {
    SaveGlobal<int32_t> turn{CurrentTurn};
    CurrentTurn = 5;
    std::vector<uint8_t> copy = state_capture();
    CHECK(state_equals(copy));
    CurrentTurn = 6;
    CHECK_FALSE(state_equals(copy));
    state_restore(copy);
    CHECK(CurrentTurn == 5);
}
