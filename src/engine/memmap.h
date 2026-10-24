// Engine memory image and global bindings.
//
// Every global of the original engine is declared with its original address:
//
//   GLOBAL(int32_t, CurrentTurn, 0x9A64D4);
//   GLOBAL(Faction[8], Factions, 0x96C9E0);
//
// Native builds back the original data segment (0x682000-0x9CA000) with mem::g_image, initialised
// from the original .data contents, so plain-data globals keep their exact layout and aliasing.
// Hybrid builds (SMACX_HYBRID, a 32-bit DLL injected into terranx.exe) bind the same names to the
// original process memory.
//
// Globals whose type holds pointers cannot live in the image on 64-bit targets; declare them with
// GLOBAL_NATIVE, which gives them ordinary storage in native builds.
#pragma once
#include <cstdint>
#include <type_traits>

namespace mem {
constexpr uint32_t kImageBase = 0x682000;
constexpr uint32_t kImageEnd = 0x9CA000;
#ifdef SMACX_HYBRID
inline uint8_t* host(uint32_t addr) { return reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(addr)); }
#else
extern uint8_t g_image[kImageEnd - kImageBase];
inline uint8_t* host(uint32_t addr) { return g_image + (addr - kImageBase); }
#endif
// Load the initial data image (native builds). Returns false if the image is missing.
bool init(const char* image_path);
}  // namespace mem

template <class T> T& smac_bind(uint32_t addr) { return *reinterpret_cast<T*>(mem::host(addr)); }
template <class T> using smac_type_t = T;  // lets array types appear in declarator position

#define GLOBAL(type, name, addr) inline std::add_lvalue_reference_t<type> name = smac_bind<type>(addr)
#ifdef SMACX_HYBRID
#define GLOBAL_NATIVE(type, name, addr) GLOBAL(type, name, addr)
#else
#define GLOBAL_NATIVE(type, name, addr) inline smac_type_t<type> name{}
#endif

// Marks a function declaration with the address of the original function it reconstructs.
// tools/gen_bindings.py reads these markers to build the hybrid patch table and the stubs for
// functions that are declared but not yet reconstructed.
#define ADDR(a)
// Like ADDR, but hybrid builds always call the original (state lives outside the memory image,
// e.g. the CRT rand() seed). The native definition must be guarded with #ifndef SMACX_HYBRID.
#define ADDR_NATIVE(a)
// Calling convention of original free functions (cdecl). Only meaningful on 32-bit x86.
// Methods of the original C++ classes are reconstructed as free functions taking the object
// pointer first, declared THISCALL so hybrid builds match the original convention.
#if defined(SMACX_HYBRID)
#define GAMEAPI __cdecl
#define THISCALL __thiscall
#else
#define GAMEAPI
#define THISCALL
#endif
