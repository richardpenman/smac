#include "engine/heap.h"

#ifndef SMACX_HYBRID
#include <cstdio>
#include <cstdlib>

// Native: zeroed blocks. The original's blocks come from a fresh Windows heap and read as zero
// where the game never writes them (e.g. the last row of MapAbstract), which savegames and the
// state comparison see.
void* GAMEAPI mem_get(uint32_t size) {
    void* ptr = std::calloc(size ? size : 1, 1);
    if (!ptr) {
        std::fprintf(stderr, "FATAL ERROR: out of memory\n");
        std::exit(4);
    }
    return ptr;
}

void GAMEAPI mem_free(void* ptr) { std::free(ptr); }
#endif

// ---- Heap (gfx module) ----
#include "engine/compat.h"
#include "engine/crt.h"
#include "engine/globals.h"
#include "engine/strings.h"
#include "platform/win32.h"

#include <unordered_map>

// The fixed heap (compat heap_realloc_moves): a growable heap is allocated with room to grow in
// place (kHeapReserve more bytes than asked), so the blocks it handed out stay valid. The original's
// sizes are kept (base_size, free_size): Strings_get's range test sees the same heap. Capacities of
// such heaps, by heap.
namespace {
constexpr int32_t kHeapReserve = 0x100000;
std::unordered_map<const Heap*, int32_t>& heap_capacity() {
    static std::unordered_map<const Heap*, int32_t> capacity;
    return capacity;
}
}  // namespace

Heap* THISCALL Heap_ctor(Heap* self) {
    self->flags = 0;
    self->next = nullptr;
    self->base = nullptr;
    self->free_size = 0;
    self->base_size = 0;
    return self;
}

void THISCALL Heap_free(Heap* self) {
    heap_capacity().erase(self);
    if (self->base) {
        mem_free(self->base);
        self->base = nullptr;
    }
    self->flags = 0;
    self->next = nullptr;
    self->base = nullptr;
    self->free_size = 0;
    self->base_size = 0;
}

void THISCALL Heap_shutdown(Heap* self) { Heap_free(self); }

void THISCALL Heap_squeeze(Heap* self, int32_t unused) {
    // BUG(original): the shrunk buffer may move, as Heap_get's (compat heap_realloc_moves). Fixed:
    // the buffer stays where it is; only the sizes shrink.
    if (!g_compat.heap_realloc_moves && heap_capacity().count(self)) {
        self->base_size -= self->free_size;
        self->free_size = 0;
        return;
    }
    uint8_t* p = (uint8_t*)crt_realloc(self->base, (uint32_t)(self->base_size - self->free_size));
    if (p) {
        self->base = p;
        int32_t free_size = self->free_size;
        self->free_size = 0;
        self->base_size -= free_size;
    }
}

int32_t THISCALL Heap_init(Heap* self, int32_t size) {
    if (self->base) {
        mem_free(self->base);
        self->flags = 0;
        self->next = nullptr;
        self->base = nullptr;
        self->free_size = 0;
        self->base_size = 0;
    }
    heap_capacity().erase(self);
    if (!g_compat.heap_realloc_moves && !(self->flags & 1)) {
        self->base = (uint8_t*)mem_get((uint32_t)(size + kHeapReserve));
        if (self->base) heap_capacity()[self] = size + kHeapReserve;
    } else {
        self->base = (uint8_t*)mem_get((uint32_t)size);
    }
    if (!self->base) return 1;
    self->free_size = size;
    self->base_size = size;
    self->next = self->base;
    return 0;
}

void* THISCALL Heap_get(Heap* self, int32_t size) {
    while (self->free_size < size) {
        if (self->flags & 1) return nullptr;
        // BUG(original): blocks handed out earlier keep pointing into the old buffer if realloc
        // moves it.
        auto cap = heap_capacity().find(self);
        if (!g_compat.heap_realloc_moves && cap != heap_capacity().end() && self->base_size + 0x400 <= cap->second) {
            self->base_size += 0x400;  // fixed: grown in place, within the reserved room
            self->free_size += 0x400;
            continue;
        }
        uint8_t* p = (uint8_t*)crt_realloc(self->base, (uint32_t)(self->base_size + 0x400));
        if (cap != heap_capacity().end()) heap_capacity().erase(cap);  // (past the reserve: as the original)
        if (!p) {
            char num[80];
            StrBuffer[0] = 0;
            crt_strcat(StrBuffer, "Aborting due to a heap shortage!");
            add_lf(StrBuffer);
            crt_strcat(StrBuffer, "Base size: ");
            crt_itoa(self->base_size, num, 10);
            crt_strcat(StrBuffer, num);
            add_lf(StrBuffer);
            crt_strcat(StrBuffer, "Free size: ");
            crt_itoa(self->free_size, num, 10);
            crt_strcat(StrBuffer, num);
            add_lf(StrBuffer);
            crt_strcat(StrBuffer, "Requested size: ");
            crt_itoa(size, num, 10);
            crt_strcat(StrBuffer, num);
            win32::MessageBoxA(StrBuffer, "Heap Notice!!", 0);
            crt_exit(3);
        }
        self->base = p;
        self->base_size += 0x400;
        self->free_size += 0x400;
    }
    uint8_t* block = self->next;
    self->free_size -= size;
    self->next = block + size;
    return block;
}
