#include "engine/clock.h"

#include <chrono>

static bool g_deterministic = false;
static uint32_t g_logic_clock = 0;

uint32_t engine_real_time() {
    using namespace std::chrono;
    return (uint32_t)duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

uint32_t engine_logic_time() {
    if (!g_deterministic) return engine_real_time();
    g_logic_clock += 7919;
    return g_logic_clock;
}

void engine_set_deterministic(bool on) { g_deterministic = on; }
uint32_t engine_logic_clock_state() { return g_logic_clock; }
void engine_set_logic_clock_state(uint32_t value) { g_logic_clock = value; }
