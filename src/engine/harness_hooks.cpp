#include "engine/turn.h"

int32_t (*g_harness_turn_upkeep)() = nullptr;
void (*g_harness_net_turn)() = nullptr;
int32_t (*g_harness_faction_upkeep)(int32_t) = nullptr;
int32_t (*g_harness_enemy_turn)(int32_t) = nullptr;
int32_t (*g_harness_human_turn)() = nullptr;
int32_t (*g_harness_observer_pause)() = nullptr;
int32_t (*g_harness_top_menu)() = nullptr;
void (*g_harness_game_end)() = nullptr;
