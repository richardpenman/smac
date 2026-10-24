#include "engine/util.h"

int32_t GAMEAPI quick_root(int32_t value) {
    if (value < 2) return value;
    int32_t next = value >> 1, root;
    do {
        root = next;
        next = (value / root + root) >> 1;
    } while (next < root);
    return root;
}

#include "engine/crt.h"

int32_t GAMEAPI btoi(const char* str) {
    int32_t value = 0;
    for (; *str == '0' || *str == '1'; str++) value = value * 2 + ((int8_t)*str - '0');
    return value;
}

int32_t GAMEAPI htoi(const char* str) {
    int32_t value = 0;
    while (crt_isxdigit((int8_t)*str)) {
        value <<= 4;
        if (crt_isdigit((int8_t)*str))
            value += (int8_t)*str - '0';
        else
            value += crt_toupper((int8_t)*str) - 0x37;
        str++;
    }
    return value;
}

int32_t GAMEAPI stoi(const char* str) {
    if (*str == '0') {
        str++;
        switch (*str) {
        case 'B':
        case 'b':
            return btoi(str + 1);
        case 'D':
        case 'd':
            str++;
            break;
        case 'X':
        case 'x':
            return htoi(str + 1);
        default:
            break;
        }
    }
    return crt_atoi(str);
}

void GAMEAPI swap(int32_t* a, int32_t* b) {
    *a ^= *b;
    *b ^= *a;
    *a ^= *b;
}

void GAMEAPI swap_2(uint8_t* a, uint8_t* b) {
    *a ^= *b;
    *b ^= *a;
    *a ^= *b;
}

int32_t GAMEAPI bit_count_signed(int32_t value) {
    int32_t bits = 0;
    for (; value; value >>= 1) bits++;
    return bits;
}

int32_t GAMEAPI fixed_div(int32_t a, int32_t b) {
    // idiv of the sign-extended 64-bit value (a << 16) by b (quotient truncated to 32 bits).
    return (int32_t)(((int64_t)a * 65536) / b);
}

char* GAMEAPI memrchr(const char* start, const char* end, int32_t ch) {
    if (!start || !end) return nullptr;
    for (intptr_t n = end - start; n; n--, end--)
        if (*end == (char)ch) return (char*)end;
    return nullptr;
}

char* GAMEAPI findnum(const char* str) {
    if (!str) return nullptr;
    for (;; str++) {
        int8_t c = (int8_t)*str;
        if (!c) return nullptr;
        if (c >= '0' && c <= '9') return (char*)str;
    }
}

int32_t GAMEAPI range(int32_t value, int32_t min, int32_t max) {
    if (value < min || max < min) return min;
    return value > max ? max : value;
}
