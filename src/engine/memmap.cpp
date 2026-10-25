#include "engine/memmap.h"

#include <cstdio>
#include <cstring>

// The memory image, the data image file and savegames hold the original's little-endian values,
// read and written in place. All supported targets (x86, x86-64, arm64) are little-endian.
#if defined(__BYTE_ORDER__) && defined(__ORDER_LITTLE_ENDIAN__)
static_assert(__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__, "little-endian target required");
#elif !defined(_WIN32)
#error "unknown byte order: little-endian target required"
#endif

namespace mem {

#ifndef SMACX_HYBRID
alignas(4096) uint8_t g_image[kImageEnd - kImageBase];

bool init(const char* image_path) {
    std::memset(g_image, 0, sizeof(g_image));
    FILE* f = std::fopen(image_path, "rb");
    if (!f) return false;
    uint32_t hdr[2];
    bool ok = true;
    while (std::fread(hdr, sizeof(hdr), 1, f) == 1) {
        if (hdr[0] < kImageBase || hdr[0] + hdr[1] > kImageEnd ||
            std::fread(g_image + (hdr[0] - kImageBase), 1, hdr[1], f) != hdr[1]) {
            ok = false;
            break;
        }
    }
    std::fclose(f);
    return ok;
}
#else
bool init(const char*) { return true; }
#endif

}  // namespace mem
