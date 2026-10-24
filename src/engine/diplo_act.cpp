#include "engine/diplo_act.h"

#include "engine/clock.h"
#include "engine/ui_bridge.h"

void GAMEAPI bitmask(int32_t bit, int32_t* offset, int32_t* mask) {
    *offset = bit / 8;
    *mask = 1 << (bit & 7);
}

int32_t GAMEAPI bit_count(uint32_t value) {
    int32_t count = 0;
    for (; value; value >>= 1) count += value & 1;
    return count;
}

void GAMEAPI clock_wait(int32_t msec) {
    // Delay with the message pump running. In deterministic mode the oracle's fast clock advances
    // 1000 ms per read: the loop runs once per started second.
    uint32_t start = engine_is_deterministic() ? 0 : engine_real_time();
    uint32_t now = start;
    do {
        do_all_draws();
        do_non_input();
        now = engine_is_deterministic() ? now + 1000 : engine_real_time();
    } while (now - start < (uint32_t)msec);
}
