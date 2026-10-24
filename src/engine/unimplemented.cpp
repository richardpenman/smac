#include "engine/unimplemented.h"

#include <cstdio>
#include <cstdlib>

#ifdef SMACX_COVERAGE
extern "C" void __gcov_dump(void);
#endif

void smac_unimplemented(const char* name, uint32_t addr) {
    std::fprintf(stderr, "smacx: original function %s (0x%06X) is not reconstructed yet\n", name, addr);
    std::fflush(stderr);
#ifdef SMACX_COVERAGE
    __gcov_dump();  // abort() skips the exit-time write of the coverage counts
#endif
    std::abort();
}
