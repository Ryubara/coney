// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cmath>

// Single-precision arithmetic as the PS2's floating-point unit does it: every result rounded toward zero, where an
// IEEE float rounds to nearest. Only where the research shows the last bit deciding something: a class damage of 30
// at 115 % plays as 34, not 35 (docs/research/combat.md#damage-table), and a steady run's measured speed comes out
// below the run speed it was set to far more often than above it (docs/research/characters.md#run-stop).
// Each operation is worked out exactly in double (a product or a sum of two floats of like size, and a square root,
// fit in a double's 53 bits) and then cut to a float toward zero.

namespace coney::ps2 {

/// `exact` as a float rounded toward zero.
[[nodiscard]] inline float towardZero(double exact) {
    float rounded = static_cast<float>(exact);
    if (std::fabs(static_cast<double>(rounded)) > std::fabs(exact)) {
        rounded = std::nextafter(rounded, 0.0F);
    }
    return rounded;
}

/// `a × b`, `a + b` and the square root of `a`, rounded toward zero.
[[nodiscard]] inline float mul(float a, float b) { return towardZero(static_cast<double>(a) * static_cast<double>(b)); }
[[nodiscard]] inline float add(float a, float b) { return towardZero(static_cast<double>(a) + static_cast<double>(b)); }
[[nodiscard]] inline float sqrt(float a) { return towardZero(std::sqrt(static_cast<double>(a))); }

/// The length of the horizontal vector (`x`, `y`): sqrt(x × x + y × y), each step rounded toward zero.
[[nodiscard]] inline float length(float x, float y) { return ps2::sqrt(add(mul(x, x), mul(y, y))); }

} // namespace coney::ps2
