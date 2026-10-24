#include "engine/heap.h"

#ifndef SMACX_HYBRID
#include <cstdio>
#include <cstdlib>

void* GAMEAPI mem_get(uint32_t size) {
    void* ptr = std::malloc(size ? size : 1);
    if (!ptr) {
        std::fprintf(stderr, "FATAL ERROR: out of memory\n");
        std::exit(4);
    }
    return ptr;
}

void GAMEAPI mem_free(void* ptr) { std::free(ptr); }
#endif

// ---- Heap (gfx module) ----
#include "engine/crt.h"
#include "engine/globals.h"
#include "engine/strings.h"
#include "platform/win32.h"

Heap* THISCALL Heap_ctor(Heap* self) {
    self->flags = 0;
    self->next = nullptr;
    self->base = nullptr;
    self->free_size = 0;
    self->base_size = 0;
    return self;
}

void THISCALL Heap_free(Heap* self) {
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
    self->base = (uint8_t*)mem_get((uint32_t)size);
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
        uint8_t* p = (uint8_t*)crt_realloc(self->base, (uint32_t)(self->base_size + 0x400));
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
