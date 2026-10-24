#include "engine/memmap.h"

#include <cstdio>
#include <cstring>

namespace mem {

#ifndef SMACX_HYBRID
uint8_t g_image[kImageEnd - kImageBase];

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
