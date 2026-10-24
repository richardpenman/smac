#include "engine/diplo_act.h"

void GAMEAPI bitmask(int32_t bit, int32_t* offset, int32_t* mask) {
    *offset = bit / 8;
    *mask = 1 << (bit & 7);
}

int32_t GAMEAPI bit_count(uint32_t value) {
    int32_t count = 0;
    for (; value; value >>= 1) count += value & 1;
    return count;
}
