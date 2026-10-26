// Unit tests of the engine (doctest, third_party/doctest). Build target smacx_unit; run by ctest
// and src/tools/gate.sh. No game data needed: tests that need engine globals set them in the
// memory image (mem::g_image, zero-initialised) and restore them (tests/unit/unit.h).
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"
