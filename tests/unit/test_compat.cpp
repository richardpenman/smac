// The original's bugs (engine/compat.h): the two settings, and the fixed paths of single bugs.
#include <cstdint>
#include <cstring>

#include "engine/compat.h"
#include "unit.h"

namespace {
bool same(const Compat& a, const Compat& b) { return std::memcmp(&a, &b, sizeof(Compat)) == 0; }
}  // namespace

TEST_CASE("compat: the game starts with the fixes") {
    CHECK(same(g_compat, Compat{}));
    CHECK(!g_original_bugs);
}

TEST_CASE("compat: the original bugs turn every flag on, the fixes restore the defaults") {
    SaveGlobal<Compat> keep(g_compat);
    SaveGlobal<bool> keep_setting(g_original_bugs);
    compat_set_original_bugs(true);
    CHECK(g_original_bugs);
    // Compat holds only bools (and the nested StackResidue of bools): one byte each, no padding.
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&g_compat);
    for (size_t i = 0; i < sizeof(Compat); i++) CHECK(bytes[i] == 1);
    compat_set_original_bugs(false);
    CHECK(!g_original_bugs);
    CHECK(same(g_compat, Compat{}));
}
