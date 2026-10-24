// Engine wall-clock reads.
//
// A few engine routines (base_terraform, drone_riot, alien_fauna, probe, save_daemon,
// world_climate, world_borehole) use timeGetTime() as a source of randomness. In deterministic mode
// (used by the differential harness) these read a counter that advances by 7919 per read instead;
// the oracle patches the original binary to use the same counter.
#pragma once
#include <cstdint>

// Value used where the original called timeGetTime() for gameplay purposes.
uint32_t engine_logic_time();
// Real milliseconds clock, for delays/animation only.
uint32_t engine_real_time();

void engine_set_deterministic(bool on);
uint32_t engine_logic_clock_state();
void engine_set_logic_clock_state(uint32_t value);
