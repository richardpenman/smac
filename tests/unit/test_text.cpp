// Text substitution (engine/parse.h): the "$" markers of the game's text files, filled from the
// parse slots. parse_string runs on every message of every harness game (snapshots compare the
// texts the game keeps); the expectations are worked out from the original's code.
#include "engine/parse.h"

#include <string>

#include "unit.h"

namespace {
std::string expand(const char* src) {
    char dst[512] = {};
    REQUIRE(parse_string(src, dst) == 0);
    return dst;
}

struct ParseState {
    SaveGlobal<int32_t[10]> nums{ParseNumTable};
    SaveGlobal<char[10][256]> strs{ParseStrBuffer};
    SaveGlobal<int32_t[10]> genders{ParseStrGender};
    SaveGlobal<int32_t[10]> plurals{ParseStrPlurality};
    SaveGlobal<int32_t> language{GameLanguage};
    ParseState() { GameLanguage = 0; }
};
}  // namespace

TEST_CASE("parse_string: numbers and strings") {
    ParseState state;
    CHECK(parse_num(0, 42) == 0);
    CHECK(parse_num(1, -7) == 0);
    CHECK(parse_num(10, 1) == 3);  // bad slot
    CHECK(expand("a $NUM0 b") == "a 42 b");
    CHECK(expand("$NUMBER1 credits") == "-7 credits");
    CHECK(expand("$HEX0") == "42");  // decimal, despite the name
    CHECK(parse_says(0, "Gaia's Stepdaughters", 1, 1) == 0);
    CHECK(parse_says(10, "x", 0, 0) == 3);
    CHECK(parse_says(1, nullptr, 0, 0) == 3);
    CHECK(expand("Hello $STR0!") == "Hello Gaia's Stepdaughters!");
    CHECK(expand("$TITLE0 and $NAME0") == "Gaia's Stepdaughters and Gaia's Stepdaughters");
    CHECK(expand("no markers") == "no markers");
    CHECK(expand("100$$ sure") == "100$ sure");
}

TEST_CASE("parse_string: gender and plural alternatives") {
    ParseState state;
    // One colon: singular / plural.
    CHECK(expand("$<M:unit:units>") == "unit");
    parse_num(0, 1);
    CHECK(expand("$<#0:unit:units>") == "unit");
    parse_num(0, 3);
    CHECK(expand("$<#0:unit:units>") == "units");
    // Two colons: masculine / feminine / neuter.
    CHECK(expand("$<M:he:she:it>") == "he");
    CHECK(expand("$<F:he:she:it>") == "she");
    CHECK(expand("$<N:he:she:it>") == "it");
    // A slot digit takes gender and plurality from the string slot.
    parse_says(2, "Sisters", 1, 1);
    CHECK(expand("The $STR2 $<2:is:are>") == "The Sisters are");
    parse_says(3, "Lady Deirdre", 1, 0);
    CHECK(expand("$<3:his:her:its>") == "her");
}

TEST_CASE("parse_string: errors") {
    ParseState state;
    char dst[64];
    CHECK(parse_string(nullptr, dst) == 3);
    CHECK(parse_string("x", nullptr) == 3);
    CHECK(parse_string("$NUMx", dst) == 14);  // 'x' - '0' > 9
}
