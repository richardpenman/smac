// Hybrid builds only: "shadow" thunks that make a reconstructed side-effect-free function leave
// exactly the stack image of the original.
//
// The original game reads uninitialised stack memory in a few places (docs/RECONSTRUCTION.md,
// "Known uninitialised reads"), so in hybrid runs a reconstructed function can change the game just
// by leaving different values in dead stack. A shadow thunk:
//   1. saves the 1 KB below its entry stack pointer,
//   2. computes the result with the reconstructed body (name##_impl),
//   3. restores those 1 KB,
//   4. runs the original machine code from the same stack position (entered through a copy of its
//      prologue, so the jump patch at its entry is bypassed), discarding its result,
//   5. rewrites the dead return-address slot with the caller's return address, as the original
//      leaves it, and returns the reconstructed result.
// Only for functions without side effects on game state (the original runs a second time). Values
// are verified separately by fcompare.
#pragma once
#ifdef SMACX_HYBRID
#include <cstdint>

// Mark the reconstructed definition: `int32_t GAMEAPI SHADOWABLE(speed)(int32_t a, int32_t b) {...}`
// defines speed natively and speed_impl_c (the body the thunk calls) in hybrid builds.
#define SHADOWABLE(name) name##_impl_c

#define SHADOW_STR2(x) #x
#define SHADOW_STR(x) SHADOW_STR2(x)

// name: function; nargs: cdecl argument count; resume: original address after the copied
// prologue; prologue: the original prologue instructions (AT&T syntax, position independent).
#define HYBRID_SHADOW_THUNK(ret, name, params, nargs, resume, prologue)                              \
    extern "C" uint32_t name##_shadow_buf[256];                                                       \
    uint32_t name##_shadow_buf[256];                                                                  \
    extern "C" uint32_t name##_shadow_esi, name##_shadow_edi, name##_shadow_res, name##_shadow_ret;   \
    uint32_t name##_shadow_esi, name##_shadow_edi, name##_shadow_res, name##_shadow_ret;              \
    extern "C" __attribute__((naked, used)) void name##_shadow_orig() {                               \
        __asm__(prologue "\n mov $" SHADOW_STR(resume) ", %eax\n jmp *%eax\n");                       \
    }                                                                                                 \
    __attribute__((naked)) ret GAMEAPI name params {                                                  \
        __asm__(                                                                                      \
            "mov %esi, _" #name "_shadow_esi\n mov %edi, _" #name "_shadow_edi\n"                     \
            "lea -1024(%esp), %esi\n mov $_" #name "_shadow_buf, %edi\n mov $256, %ecx\n rep movsl\n" \
            "mov _" #name "_shadow_esi, %esi\n mov _" #name "_shadow_edi, %edi\n"                     \
            ".rept " #nargs "\n pushl (4*" #nargs ")(%esp)\n .endr\n"                                 \
            "call _" #name "_impl_c\n"                               \
            "add $(4*" #nargs "), %esp\n mov %eax, _" #name "_shadow_res\n"                           \
            "mov %esi, _" #name "_shadow_esi\n mov %edi, _" #name "_shadow_edi\n"                     \
            "mov $_" #name "_shadow_buf, %esi\n lea -1024(%esp), %edi\n mov $256, %ecx\n rep movsl\n" \
            "mov _" #name "_shadow_esi, %esi\n mov _" #name "_shadow_edi, %edi\n"                     \
            "pop %eax\n mov %eax, _" #name "_shadow_ret\n"                                            \
            "push $1f\n jmp _" #name "_shadow_orig\n"                                                 \
            "1:\n mov _" #name "_shadow_ret, %ecx\n mov %ecx, -4(%esp)\n"                              \
            "mov _" #name "_shadow_res, %eax\n jmp *%ecx\n");                                         \
    }

#else
#define SHADOWABLE(name) name
#endif
