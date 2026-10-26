// Pure helpers of the original's util module (engine/util.h). Each is checked against the original
// by fcompare or used on every harness game path; the expected values here are derived by hand
// from the original's code (re/show NAME --asm), so they also document its edge cases.
#include "engine/util.h"

#include <climits>

#include "unit.h"

TEST_CASE("quick_root: integer square root rounded down") {
    CHECK(quick_root(0) == 0);
    CHECK(quick_root(1) == 1);
    CHECK(quick_root(2) == 1);
    CHECK(quick_root(3) == 1);
    CHECK(quick_root(4) == 2);
    CHECK(quick_root(99) == 9);
    CHECK(quick_root(100) == 10);
    CHECK(quick_root(1 << 30) == 32768);
    CHECK(quick_root(INT32_MAX) == 46340);
    CHECK(quick_root(-5) == -5);  // values below 2 are returned unchanged
    for (int32_t v = 0; v < 200000; v++) {
        int64_t r = quick_root(v);
        REQUIRE(r * r <= v);
        REQUIRE((r + 1) * (r + 1) > v);
    }
}

TEST_CASE("btoi / htoi / stoi: number parsing of the text files") {
    CHECK(btoi("1011") == 11);
    CHECK(btoi("102") == 2);  // stops at the first other character
    CHECK(btoi("") == 0);
    CHECK(htoi("1aF") == 0x1AF);
    CHECK(htoi("ff,") == 0xFF);
    CHECK(htoi("g") == 0);
    CHECK(stoi("42") == 42);
    CHECK(stoi("-7") == -7);
    CHECK(stoi("0") == 0);
    CHECK(stoi("012") == 12);  // a leading 0 is skipped, not octal
    CHECK(stoi("0x1F") == 31);
    CHECK(stoi("0X1f") == 31);
    CHECK(stoi("0b101") == 5);
    CHECK(stoi("0d42") == 42);
    CHECK(stoi("  9") == 9);  // atoi skips leading blanks
}

TEST_CASE("fixed_div: 16.16 fixed-point quotient, truncated toward zero") {
    CHECK(fixed_div(1, 2) == 0x8000);
    CHECK(fixed_div(1, 3) == 21845);
    CHECK(fixed_div(-3, 2) == -98304);
    CHECK(fixed_div(5, 1) == 5 << 16);
}

TEST_CASE("bit_count_signed: position of the highest set bit") {
    CHECK(bit_count_signed(0) == 0);
    CHECK(bit_count_signed(1) == 1);
    CHECK(bit_count_signed(255) == 8);
    CHECK(bit_count_signed(256) == 9);
    CHECK(bit_count_signed(INT32_MAX) == 31);
    // Negative values never terminate in the original (arithmetic shift): not called with them.
}

TEST_CASE("range: clamp, min wins when the bounds are inverted") {
    CHECK(range(5, 0, 3) == 3);
    CHECK(range(-1, 0, 3) == 0);
    CHECK(range(2, 0, 3) == 2);
    CHECK(range(2, 5, 1) == 5);
    CHECK(range(9, 5, 1) == 5);
}

TEST_CASE("swap / swap_2: xor swap") {
    int32_t a = 3, b = -9;
    swap(&a, &b);
    CHECK(a == -9);
    CHECK(b == 3);
    uint8_t c = 1, d = 250;
    swap_2(&c, &d);
    CHECK(c == 250);
    CHECK(d == 1);
    // As in the original, swapping an object with itself zeroes it.
    swap(&a, &a);
    CHECK(a == 0);
}

TEST_CASE("findnum / memrchr") {
    const char* s = "$STR12";
    CHECK(findnum(s) == s + 4);
    CHECK(findnum("none") == nullptr);
    const char* t = "a,b,c";
    CHECK(memrchr(t, t + 4, ',') == t + 3);
    CHECK(memrchr(t, t + 4, 'a') == nullptr);  // searches (start, end]: *start is never examined
}
