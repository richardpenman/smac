#pragma once
// Layout assertions. Structs holding pointers only match the original layout on 32-bit targets
// (hybrid build); on 64-bit native builds they are ordinary C++ types with native storage.
#include <type_traits>
#if UINTPTR_MAX == 0xFFFFFFFFu
#define SMAC_IS_32BIT 1
#else
#define SMAC_IS_32BIT 0
#endif
template <class T> struct smac_has_pointers : std::false_type {};
#define SMAC_POINTER_STRUCT(T) template <> struct smac_has_pointers<T> : std::true_type {}
#define SMAC_LAYOUT(T, N) static_assert(!(SMAC_IS_32BIT || !smac_has_pointers<T>::value) || sizeof(T) == (N), #T " layout")
#define SMAC_LAYOUT_N(T, K, N) static_assert(!(SMAC_IS_32BIT || !smac_has_pointers<T>::value) || sizeof(T) * (K) == (N), #T " layout")
