// Heap allocation wrappers (mem_get 0x5D4510 in the gfx module, mem_free 0x644EF2 in the CRT).
// Hybrid builds must allocate from the original's heap, so both always call the original there.
#pragma once
#include "engine/memmap.h"
#include "engine/types/all.h"

// Allocates size bytes; the original aborts the program when memory runs out.
ADDR_NATIVE(0x5D4510) void* GAMEAPI mem_get(uint32_t size);
ADDR_NATIVE(0x644EF2) void GAMEAPI mem_free(void* ptr);

// Growable block allocator (gfx module, 0x5D4560-0x5D4850). Blocks are carved from one buffer
// that grows with realloc in 1 KB steps (previously returned pointers are not updated).
struct Heap {
    uint8_t flags;  // bit 0: fixed size, Heap_get fails instead of growing
    uint8_t pad[3];
    uint8_t* base;
    uint8_t* next;  // next free byte
    int32_t base_size;
    int32_t free_size;
};
SMAC_POINTER_STRUCT(Heap);
SMAC_LAYOUT(Heap, 0x14);

// Constructor (0x5D4560): all fields zero.
ADDR(0x5D4560) Heap* THISCALL Heap_ctor(Heap* self);
// Frees the buffer and resets all fields (0x5D4580, a copy of Heap_shutdown).
ADDR(0x5D4580) void THISCALL Heap_free(Heap* self);
ADDR(0x5D45B0) void THISCALL Heap_shutdown(Heap* self);
// Shrinks the buffer to the used size (the argument is unused).
ADDR(0x5D45E0) void THISCALL Heap_squeeze(Heap* self, int32_t unused);
// Allocates a buffer of size bytes. Returns 0 on success, 1 if out of memory.
ADDR(0x5D4620) int32_t THISCALL Heap_init(Heap* self, int32_t size);
// Returns size bytes from the heap, growing it if needed; aborts the program when realloc fails.
ADDR(0x5D4680) void* THISCALL Heap_get(Heap* self, int32_t size);
