// The game() view (engine/game_state.h) is the memory image itself: every member aliases its
// global, so code moved from the globals to game() reads and writes the same bytes.
#include "engine/game_state.h"
#include "unit.h"

TEST_CASE("game(): members alias the globals") {
    GameStateView& g = game();
    CHECK(&g.map.x == &MapAreaX);
    CHECK(&g.map.y == &MapAreaY);
    CHECK(&g.map.half_x == &MapHalfX);
    CHECK(&g.map.tile_count == &MapAreaTiles);
    CHECK(&g.map.sq_root == &MapAreaSqRoot);
    CHECK(&g.map.flat == &MapToggleFlat);
    CHECK(&g.map.tiles == &MapTiles);
    CHECK(&g.factions[0] == &Factions[0]);
    CHECK(&g.mfactions[0] == &MFactions[0]);
    CHECK(&g.bases[0] == &Bases[0]);
    CHECK(&g.base_count == &BaseCount);
    CHECK(&g.vehs[0] == &Vehs[0]);
    CHECK(&g.veh_count == &VehCount);
    CHECK(&g.rules == &Rules);
    CHECK(&g.turn == &CurrentTurn);
    CHECK(sizeof(g.bases) == sizeof(Bases));
    CHECK(sizeof(g.vehs) == sizeof(Vehs));
}

TEST_CASE("game(): writes through the view reach the globals") {
    SaveGlobal<int32_t> count{BaseCount};
    game().base_count = 17;
    CHECK(BaseCount == 17);
    MapDims dims(40, 20, false);
    CHECK(game().map.x == 40);
    CHECK(game().map.half_x == 20);
}
