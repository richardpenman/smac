#include "engine/memmap.h"

#include <cstdio>
#include <cstring>

#include "engine/state.h"

// The memory image, the data image file and savegames hold the original's little-endian values,
// read and written in place. All supported targets (x86, x86-64, arm64) are little-endian.
#if defined(__BYTE_ORDER__) && defined(__ORDER_LITTLE_ENDIAN__)
static_assert(__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__, "little-endian target required");
#elif !defined(_WIN32)
#error "unknown byte order: little-endian target required"
#endif

namespace mem {

alignas(4096) uint8_t g_image[kImageEnd - kImageBase];

namespace {
// The record holding addr, or null (binary search over the sorted records).
const StateRecord* record_of(uint32_t addr) {
    size_t n;
    const StateRecord* r = state_records(&n);
    size_t lo = 0, hi = n;
    while (lo < hi) {
        size_t mid = (lo + hi) / 2;
        if (addr < r[mid].addr) hi = mid;
        else if (addr - r[mid].addr >= r[mid].size) lo = mid + 1;
        else return &r[mid];
    }
    return nullptr;
}

// Globals bound across a record's boundary (filled during static initialisation, reported by
// init). Plain arrays: constant-initialised, usable before any dynamic initialisation.
const char* g_straddling[16];
int g_straddling_count;
}  // namespace

uint8_t* host(uint32_t addr) {
    if (const StateRecord* r = record_of(addr)) return static_cast<uint8_t*>(r->object) + (addr - r->addr);
    return g_image + (addr - kImageBase);
}

uint8_t* bind(uint32_t addr, uint32_t size, const char* name) {
    size_t n;
    const StateRecord* r = state_records(&n);
    for (size_t i = 0; i < n; i++) {
        bool overlaps = addr < r[i].addr + r[i].size && r[i].addr < addr + size;
        bool inside = addr >= r[i].addr && addr + size <= r[i].addr + r[i].size;
        if (overlaps && !inside && g_straddling_count < 16) g_straddling[g_straddling_count++] = name;
    }
    return host(addr);
}

uint32_t address_of(const void* p) {
    const uint8_t* b = static_cast<const uint8_t*>(p);
    if (b >= g_image && b < g_image + sizeof(g_image)) return kImageBase + static_cast<uint32_t>(b - g_image);
    size_t n;
    const StateRecord* r = state_records(&n);
    for (size_t i = 0; i < n; i++) {
        const uint8_t* o = static_cast<const uint8_t*>(r[i].object);
        if (b >= o && b < o + r[i].size) return r[i].addr + static_cast<uint32_t>(b - o);
    }
    return 0;
}

bool init(const char* image_path) {
    for (int i = 0; i < g_straddling_count; i++)
        std::fprintf(stderr, "mem::init: the global %s lies partly in a game-state record (engine/state.h)\n",
                     g_straddling[i]);
    if (g_straddling_count) return false;
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
    // The records start with the original's initial data; their bytes in the image are then
    // filled with a pattern, so a stray read of the image where a record was shows up.
    size_t n;
    const StateRecord* r = state_records(&n);
    for (size_t i = 0; i < n; i++) {
        uint8_t* bytes = g_image + (r[i].addr - kImageBase);
        r[i].load(bytes);
        std::memset(bytes, 0xA5, r[i].size);
    }
    return ok;
}

// Byte ranges that may span several records and the image: one copy per piece.
void read_bytes(uint32_t addr, void* dst, uint32_t n) {
    uint8_t* out = static_cast<uint8_t*>(dst);
    while (n) {
        uint32_t piece = 1;
        if (const StateRecord* r = record_of(addr)) piece = r->addr + r->size - addr;
        else
            while (piece < n && !record_of(addr + piece)) piece++;
        if (piece > n) piece = n;
        std::memcpy(out, host(addr), piece);
        out += piece;
        addr += piece;
        n -= piece;
    }
}
void write_bytes(uint32_t addr, const void* src, uint32_t n) {
    const uint8_t* in = static_cast<const uint8_t*>(src);
    while (n) {
        uint32_t piece = 1;
        if (const StateRecord* r = record_of(addr)) piece = r->addr + r->size - addr;
        else
            while (piece < n && !record_of(addr + piece)) piece++;
        if (piece > n) piece = n;
        std::memcpy(host(addr), in, piece);
        in += piece;
        addr += piece;
        n -= piece;
    }
}

}  // namespace mem
