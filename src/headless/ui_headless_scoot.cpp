// Headless implementation of veh_scoot, the unit's sliding animation (engine/ui_bridge.h). smacx
// replaces this area (src/ui/sound.cpp).
#include "engine/game_state.h"
#include "engine/ui_bridge.h"

// The sliding animation is not shown; its game-state effect is the vehicle's facing, which the
// original always sets (vehdraw_turn_start, or directly with flags 0x4000).
void GAMEAPI veh_scoot(int32_t veh_id, int32_t, int32_t, int32_t dir, int32_t) {
    if (dir > 7) dir %= 8;
    game().vehs[veh_id].rotate_angle = (int8_t)dir;
}
