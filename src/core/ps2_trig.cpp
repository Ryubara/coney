// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/ps2_trig.h"

#include "core/ps2_float.h"

#include <bit>
#include <cmath>
#include <cstdint>

// The game's sine and cosine are fdlibm's float versions from its C runtime (middleware, so no `@orig` tag: core code
// needs none), run on the EE's floating-point unit (docs/research/maths.md#trig): a polynomial kernel on [-π/4, π/4]
// after reducing the angle by π/2. Each operation below is one of the original's, in its order, with the PS2's rounding
// (core/ps2_float.h), so the results are the original's to the bit
// (docs/research/characters-locomotion.md#run-velocity).

namespace coney::ps2 {

namespace {

// A float from its raw word: the kernels' constants are written as the original loads them.
constexpr float word(std::uint32_t bits) { return std::bit_cast<float>(bits); }

// The magnitude's raw word, which the original compares against thresholds as an integer.
std::uint32_t magnitudeBits(float x) { return std::bit_cast<std::uint32_t>(x) & 0x7fffffffU; }

// Below 2^-27 the kernels return their argument (the sine) or 1 (the cosine) unchanged.
constexpr std::uint32_t kTiny = 0x32000000U;
// π/4: below it no reduction is needed.
constexpr std::uint32_t kQuarterPi = 0x3f490fd9U;
// 3π/4: below it one subtraction of π/2 reduces the angle.
constexpr std::uint32_t kThreeQuarterPi = 0x4016cbe4U;

// The sine of x + y (y the tail of a reduced angle, ignored without `tail`) for |x| up to π/4.
// The C runtime's Math_KernelSinf (0x004bdf20).
float kernelSin(float x, float y, bool tail) {
    if (magnitudeBits(x) < kTiny) {
        return x;
    }
    const float z = mul(x, x);
    const float v = mul(z, x);
    // r = S2 + z(S3 + z(S4 + z(S5 + z S6))), from the innermost term out.
    float r = mul(z, word(0x2f2ec9d3U));
    r = add(r, word(0xb2d72f34U));
    r = mul(z, r);
    r = add(r, word(0x3638ef1bU));
    r = mul(z, r);
    r = add(r, word(0xb9500d01U));
    r = mul(z, r);
    r = add(r, word(0x3c088889U));
    const float s1 = word(0xbe2aaaabU); // -1/6
    if (!tail) {
        return add(x, mul(v, add(mul(z, r), s1)));
    }
    // x - ((z (y/2 - v r) - y) - v S1)
    const float vr = mul(v, r);
    const float vs1 = mul(v, s1);
    const float inner = mul(z, sub(mul(y, 0.5F), vr));
    return sub(x, sub(sub(inner, y), vs1));
}

// The cosine of x + y for |x| up to π/4.
// The C runtime's Math_KernelCosf (0x004bd478).
float kernelCos(float x, float y) {
    const std::uint32_t bits = magnitudeBits(x);
    if (bits < kTiny) {
        return 1.0F;
    }
    const float z = mul(x, x);
    // r = z (C1 + z(C2 + z(C3 + z(C4 + z(C5 + z C6))))), from the innermost term out.
    float r = mul(z, word(0xad47d74eU));
    r = add(r, word(0x310f74f6U));
    r = mul(z, r);
    r = add(r, word(0xb493f27cU));
    r = mul(z, r);
    r = add(r, word(0x37d00d01U));
    r = mul(z, r);
    r = add(r, word(0xbab60b61U));
    r = mul(z, r);
    r = add(r, word(0x3d2aaaabU));
    r = mul(z, r);
    const float zr = mul(z, r);
    const float xy = mul(x, y);
    const float halfZ = mul(z, 0.5F);
    if (bits < 0x3e99999aU) {
        // Under 0.3: 1 - (z/2 - (z r - x y)).
        return sub(1.0F, sub(halfZ, sub(zr, xy)));
    }
    // Above it a quarter of x (0.28125 from 0.78125 up) is split off first to keep the precision.
    const float qx = bits > 0x3f480000U ? 0.28125F : word(bits - 0x01000000U);
    const float rest = sub(zr, xy);
    const float hz = sub(halfZ, qx);
    const float a = sub(1.0F, qx);
    return sub(a, sub(hz, rest));
}

// An angle under 3π/4 (and at least π/4) reduced by π/2 into a head and a tail; returns the quadrant, 1 or -1. π/2 is
// taken in three parts, the third used only near π/2 itself, where the first two cancel most of the angle.
// The C runtime's Math_ReduceByHalfPif (0x004bcf60), its first step only.
int reduceByHalfPi(float x, float& head, float& tail) {
    const float pio2Head = word(0x3fc90f80U);       // π/2's first 17 bits
    const float pio2Second = word(0x37354400U);     // its next part
    const float pio2SecondTail = word(0x2e85a308U); // what is left after the second part
    const float pio2Tail = word(0x37354443U);       // what is left after the first part
    const bool nearHalfPi = (magnitudeBits(x) & 0xfffffff0U) == 0x3fc90fd0U;
    if (std::bit_cast<std::int32_t>(x) > 0) {
        float t = sub(x, pio2Head);
        float c = pio2Tail;
        if (nearHalfPi) {
            t = sub(t, pio2Second);
            c = pio2SecondTail;
        }
        head = sub(t, c);
        tail = sub(sub(t, head), c);
        return 1;
    }
    float t = add(x, pio2Head);
    float c = pio2Tail;
    if (nearHalfPi) {
        t = add(t, pio2Second);
        c = pio2SecondTail;
    }
    head = add(t, c);
    tail = add(sub(t, head), c);
    return -1;
}

// The sine (`cosine` false) or cosine of x through the reduction: the quadrant picks the kernel and the sign.
float reduced(float x, bool cosine) {
    float head = 0.0F;
    float tail = 0.0F;
    // Quadrant 1 for a positive angle, 3 (that is -1) for a negative one; the cosine is the sine a quadrant on.
    const int quadrant = (reduceByHalfPi(x, head, tail) + (cosine ? 1 : 0)) & 3;
    switch (quadrant) {
    case 0:
        return kernelSin(head, tail, true);
    case 1:
        return kernelCos(head, tail);
    case 2:
        return -kernelSin(head, tail, true);
    default:
        return -kernelCos(head, tail);
    }
}

} // namespace

// The C runtime's Math_Sinf (0x004b8c40).
float sinf(float x) {
    const std::uint32_t bits = magnitudeBits(x);
    if (bits < kQuarterPi) {
        return kernelSin(x, 0.0F, false);
    }
    if (bits < kThreeQuarterPi) {
        return reduced(x, false);
    }
    // **Coney stand-in**: the original reduces larger angles in more steps (and a NaN or infinity gives x - x); no
    // caller ported so far passes one (a half heading is under π/2 and a bit), so IEEE's sine stands in.
    return towardZero(std::sin(static_cast<double>(x)));
}

// The C runtime's Math_Cosf (0x004b8a70).
float cosf(float x) {
    const std::uint32_t bits = magnitudeBits(x);
    if (bits < kQuarterPi) {
        return kernelCos(x, 0.0F);
    }
    if (bits < kThreeQuarterPi) {
        return reduced(x, true);
    }
    // **Coney stand-in**: as for the sine.
    return towardZero(std::cos(static_cast<double>(x)));
}

} // namespace coney::ps2
