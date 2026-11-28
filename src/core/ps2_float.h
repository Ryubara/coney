// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cmath>

// Single-precision arithmetic as the PS2's floating-point unit does it: every result rounded toward zero, where an
// IEEE float rounds to nearest. Only where the research shows the last bit deciding something: a class damage of 30
// at 115 % plays as 34, not 35 (docs/research/combat/index.md#damage-table), and a run's velocity and the speed
// measured from it, which decide whether a release skids (docs/research/characters-locomotion.md#run-velocity).
// Each operation is worked out exactly in double (a product or a sum of two floats of like size, and a square root, fit
// in a double's 53 bits) and then cut to a float toward zero; a sum first drops the smaller operand's bits as the PS2's
// adder does.

namespace coney::ps2 {

/// `exact` as a float rounded toward zero.
[[nodiscard]] inline float towardZero(double exact) {
    float rounded = static_cast<float>(exact);
    if (std::fabs(static_cast<double>(rounded)) > std::fabs(exact)) {
        rounded = std::nextafter(rounded, 0.0F);
    }
    return rounded;
}

/// `a × b` and the square root of `a`, rounded toward zero.
[[nodiscard]] inline float mul(float a, float b) { return towardZero(static_cast<double>(a) * static_cast<double>(b)); }
[[nodiscard]] inline float sqrt(float a) { return towardZero(std::sqrt(static_cast<double>(a))); }

/// `a + b` as the PS2's adder makes it: the operand of smaller magnitude is lined up with the larger one keeping one
/// bit below the larger one's last place, the bits under that are dropped, and the sum is rounded toward zero. For
/// operands of one sign that is the exact sum rounded toward zero; a difference can come out a unit higher
/// (docs/research/characters-locomotion.md#run-velocity: the game's sine and cosine match only so).
[[nodiscard]] inline float add(float a, float b) {
    const double larger = std::fabs(a) >= std::fabs(b) ? a : b;
    const double smaller = std::fabs(a) >= std::fabs(b) ? b : a;
    if (larger == 0.0) {
        return a + b;
    }
    // The guard bit is half a unit in the larger operand's last place: 2^(exponent - 24) for a 24-bit significand.
    const double guard = std::ldexp(1.0, std::ilogb(larger) - 24);
    const double kept = std::trunc(smaller / guard) * guard;
    return towardZero(larger + kept);
}

/// `a - b`, rounded toward zero.
[[nodiscard]] inline float sub(float a, float b) { return add(a, -b); }

/// The length of the horizontal vector (`x`, `y`): sqrt(x × x + y × y), each step rounded toward zero.
[[nodiscard]] inline float length(float x, float y) { return ps2::sqrt(add(mul(x, x), mul(y, y))); }

/// The length of the vector (`x`, `y`, `z`): sqrt((x × x + y × y) + z × z), each step rounded toward zero.
[[nodiscard]] inline float length(float x, float y, float z) {
    return ps2::sqrt(add(add(mul(x, x), mul(y, y)), mul(z, z)));
}

} // namespace coney::ps2
