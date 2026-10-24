#pragma once
#include <cstdint>
// Hybrid build: table of reconstructed functions that can be jump-patched over the originals.
struct HybridPatch {
    uint32_t addr;
    void* func;
    const char* name;
};
extern const HybridPatch kHybridPatches[];  // terminated by {0, nullptr, nullptr}
