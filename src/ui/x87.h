// x87 floating point as the original runs it (FPU control word 0x27F: 53-bit precision, round to
// nearest), for code translated from the original's x87 instructions.
//
// * With 53-bit precision control, FADD/FSUB/FMUL/FDIV/FSQRT round their result to a double
//   mantissa, so a register value of the original is a `double` here and those operations are
//   plain double arithmetic (the build uses -ffp-contract=off: no fused multiply-add). Values the
//   original stores as float (fst/fstp DWORD) are `float`; floats and int32 load exactly.
// * The transcendental instructions (FSIN, FCOS, FPATAN) are not affected by precision control:
//   their result has a 64-bit mantissa (`ext`). An operation that takes such a value rounds the
//   exact result to 53 bits once (x87_mul etc.); a store to float rounds it to 24 bits once.
//   On x86 hosts these run the x87 instructions themselves (the same results as the original on
//   the same CPU); elsewhere `ext` is double and the libm functions are used (may differ in the
//   last bit).
// * FISTP rounds to nearest even (the control word's rounding mode): x87_fistp. The CRT's __ftol
//   (0x6458C8) truncates: x87_ftol.
#pragma once
#include <cmath>
#include <cstdint>

#if (defined(__x86_64__) || defined(__i386__)) && (defined(__GNUC__) || defined(__clang__))
#define SMACX_X87_NATIVE 1
using ext = long double;
// The x87 control word for the duration of a scope: 0x27F (the original's: 53-bit precision,
// round to nearest, exceptions masked). Native x86-64 code runs with 0x37F (64-bit precision).
inline ext x87_fsin(ext x) {
    ext r;
    __asm__("fsin" : "=t"(r) : "0"(x));
    return r;
}
inline ext x87_fcos(ext x) {
    ext r;
    __asm__("fcos" : "=t"(r) : "0"(x));
    return r;
}
// FPATAN: atan(y / x) with the quadrant of (x, y).
inline ext x87_fpatan(ext y, ext x) {
    ext r;
    __asm__("fpatan" : "=t"(r) : "0"(x), "u"(y) : "st(1)");
    return r;
}
// a op b with the result rounded once to 53 bits (precision control 0x27F).
inline double x87_mul(ext a, ext b) {
    ext r;
    uint16_t old = 0, cw = 0x27F;
    __asm__ volatile("fnstcw %1\n\tfldcw %2\n\tfmulp\n\tfldcw %1"
                     : "=t"(r), "=m"(old)
                     : "m"(cw), "0"(a), "u"(b)
                     : "st(1)");
    return (double)r;
}
inline double x87_add(ext a, ext b) {
    ext r;
    uint16_t old = 0, cw = 0x27F;
    __asm__ volatile("fnstcw %1\n\tfldcw %2\n\tfaddp\n\tfldcw %1"
                     : "=t"(r), "=m"(old)
                     : "m"(cw), "0"(a), "u"(b)
                     : "st(1)");
    return (double)r;
}
#else
#define SMACX_X87_NATIVE 0
using ext = double;
inline ext x87_fsin(ext x) { return std::sin(x); }
inline ext x87_fcos(ext x) { return std::cos(x); }
inline ext x87_fpatan(ext y, ext x) { return std::atan2(y, x); }
inline double x87_mul(ext a, ext b) { return a * b; }
inline double x87_add(ext a, ext b) { return a + b; }
#endif
inline double x87_sub(ext a, ext b) { return x87_add(a, -b); }

// FISTP (round to nearest even), 32-bit.
inline int32_t x87_fistp(double v) { return (int32_t)std::nearbyint(v); }
// __ftol (0x6458C8): FISTP QWORD with chop rounding; the low 32 bits.
inline int32_t x87_ftol(double v) { return (int32_t)(uint32_t)(uint64_t)(int64_t)v; }

// The CRT functions (MSVC 6, called with the control word 0x27F; results in st(0)).
// atan (0x646664): FPATAN(x, 1).
inline ext crt_atan(double x) { return x87_fpatan((ext)x, (ext)1.0); }
// acos (_CIacos 0x647AB0) for |x| < 1: FPATAN(sqrt((1 + x) * (1 - x)), x).
inline ext crt_acos(double x) { return x87_fpatan((ext)std::sqrt((1.0 + x) * (1.0 - x)), (ext)x); }
// _hypot (0x650231): m * sqrt((x/m)^2 + (y/m)^2), m = max(|x|, |y|), in double steps.
inline double crt_hypot(double x, double y) {
    if (x < 0.0) x = -x;
    if (y < 0.0) y = -y;
    double m = y > x ? y : x;
    if (m == 0.0) return 0.0;
    x = x / m;
    y = y / m;
    double r = std::sqrt(x * x + y * y);
    int e1 = 0, e2 = 0;
    double f1 = std::frexp(r, &e1), f2 = std::frexp(m, &e2);
    return std::ldexp(f1 * f2, e1 + e2);
}
// floor (0x647851): exact.
inline double crt_floor(double x) { return std::floor(x); }
