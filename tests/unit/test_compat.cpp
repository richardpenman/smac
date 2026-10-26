// Compatibility flags (engine/compat.h): every flag defaults to the original behaviour.
#include <cstdint>

#include "engine/compat.h"
#include "unit.h"

TEST_CASE("compat: every flag is on by default") {
    // Compat holds only bools (and the nested StackResidue of bools): one byte each, no padding.
    Compat fresh;
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&fresh);
    for (size_t i = 0; i < sizeof(Compat); i++) CHECK(bytes[i] == 1);
    const uint8_t* live = reinterpret_cast<const uint8_t*>(&g_compat);
    for (size_t i = 0; i < sizeof(Compat); i++) CHECK(live[i] == 1);
}
