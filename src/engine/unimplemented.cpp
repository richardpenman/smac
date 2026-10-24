#include "engine/unimplemented.h"

#include <cstdio>
#include <cstdlib>

void smac_unimplemented(const char* name, uint32_t addr) {
    std::fprintf(stderr, "smacx: original function %s (0x%06X) is not reconstructed yet\n", name, addr);
    std::fflush(stderr);
    std::abort();
}
