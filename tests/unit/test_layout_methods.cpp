// Member functions of the layout structs (Veh, Base, Faction): the original's free functions
// forward to them, so both reach the same state in the memory image.
#include "engine/base.h"
#include "engine/battle.h"
#include "engine/diplomacy.h"
#include "engine/game_state.h"
#include "engine/veh.h"
#include "unit.h"

TEST_CASE("Veh::id is the index in game().vehs") {
    CHECK(game().vehs[0].id() == 0);
    CHECK(game().vehs[37].id() == 37);
    CHECK(game().vehs[2048].id() == 2048);  // veh_fake's scratch slot
}

TEST_CASE("Veh::sleep and sleep(): sentry on board without a transport") {
    SaveGlobal<VEH> saved{game().vehs[5]};
    game().vehs[5].order = ORDER_NONE;
    game().vehs[5].waypoint_x[0] = 12;
    game().vehs[5].waypoint_y[0] = 7;
    sleep(5);
    CHECK(game().vehs[5].order == ORDER_SENTRY_BOARD);
    CHECK(game().vehs[5].waypoint_x[0] == -1);
    CHECK(game().vehs[5].waypoint_y[0] == 0);
}

TEST_CASE("Veh::wake: a unit on alert that has not moved gets its moves back") {
    SaveGlobal<VEH> saved{game().vehs[9]};
    VEH& v = game().vehs[9];
    v.order = ORDER_SENTRY_BOARD;
    v.state = VSTATE_ON_ALERT | VSTATE_EXPLORE | 0x100;
    v.order_auto_type = ORDERA_ON_ALERT;
    v.moves_spent = 3;
    CHECK(veh_wake(9) == 9);
    CHECK(v.order == ORDER_NONE);
    CHECK(v.moves_spent == 0);
    CHECK(v.state == (0x100u & 0xF4FFBDFFu));  // alert and explore cleared
    v.state = VSTATE_ON_ALERT | VSTATE_HAS_MOVED;
    v.moves_spent = 3;
    v.wake();
    CHECK(v.moves_spent == 3);  // it has moved: its moves stay spent
}

TEST_CASE("Base::pop_goal: the size the AI aims at, within the habitation limits") {
    SaveGlobal<BASE> base{game().bases[3]};
    SaveGlobal<MFaction> mfaction{game().mfactions[2]};
    SaveGlobal<CRules> rules{Rules};
    SaveGlobal<int32_t[64]> projects{SecretProjects};
    BASE& b = game().bases[3];
    std::memset(b.facilities_built, 0, sizeof(b.facilities_built));
    b.faction_id = 2;
    game().mfactions[2].rule_population = 0;
    Rules.pop_limit_wo_hab_complex = 7;
    Rules.pop_limit_wo_hab_dome = 14;
    SecretProjects[FAC_ASCETIC_VIRTUES - 70] = -1;
    b.pop_size = 3;
    CHECK(b.id() == 3);
    CHECK(pop_goal(3) == 7);  // (36 - 3) / 6 + 3 = 8, limited to 7 without a Hab Complex
    CHECK(pop_goal_fac(3) == 0);
    b.pop_size = 7;
    CHECK(pop_goal_fac(3) == FAC_HAB_COMPLEX);
    set_fac(FAC_HAB_COMPLEX, 3, 1);
    CHECK(b.pop_goal() == 11);  // (36 - 7) / 6 + 7
    b.pop_size = 14;
    CHECK(b.pop_goal_fac() == FAC_HABITATION_DOME);
    CHECK(b.pop_goal() == 14);
}

TEST_CASE("Faction: treaties and agendas on both sides, reputation") {
    SaveGlobal<Faction> f1{game().factions[1]}, f2{game().factions[2]};
    SaveGlobal<int32_t> viewer{CurrentPlayerFaction};
    CurrentPlayerFaction = 7;  // neither side: no music, no window redraw
    Faction& a = game().factions[1];
    Faction& b = game().factions[2];
    CHECK(a.id() == 1);
    a.diplo_status[2] = 0;
    b.diplo_status[1] = 0;
    treaty_on(1, 2, DIPLO_PACT);
    CHECK(has_treaty(1, 2, DIPLO_PACT));
    CHECK(b.has_treaty(1, DIPLO_PACT));
    CHECK(a.has_treaty(2, 2));  // a pact also sets the treaty bit
    a.treaty_off(2, DIPLO_PACT);
    CHECK(!a.has_treaty(2, DIPLO_PACT));
    CHECK(!b.has_treaty(1, DIPLO_PACT));
    set_treaty(1, 2, 0x40, 1);  // one side only
    CHECK(a.has_treaty(2, 0x40));
    CHECK(!b.has_treaty(1, 0x40));
    CHECK(a.diplo_merc[2] == 50);
    a.diplo_agenda[2] = 0;
    b.diplo_agenda[1] = 0;
    agenda_on(1, 2, 0x100);
    CHECK(has_agenda(2, 1, 0x100));
    a.agenda_off(2, 0x100);
    CHECK(!b.has_agenda(1, 0x100));
    a.integrity_blemishes = 5;
    a.diplo_gifts[2] = 2;
    CHECK(reputation(1, 2) == 3);
    a.diplo_gifts[2] = 9;
    CHECK(a.reputation(2) == 0);
}
