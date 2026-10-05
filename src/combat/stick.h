// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cmath>
#include <cstdint>
#include <numbers>

// The left stick as combat reads it: a length and an angle from the player's facing, and which side of the player it
// points to. Combat takes the stick already turned into the player's frame (x to the player's right, y ahead), as the
// original compares the camera-turned stick with the facing; the caller does the turning.
// Research: docs/research/combat.md#attacks, docs/research/combat.md#throws

namespace coney::combat {

/// A stick in the player's frame.
struct Stick {
    float x = 0.0F; ///< To the player's right, -1 to 1.
    float y = 0.0F; ///< Ahead of the player, -1 to 1.

    /// The stick's length.
    [[nodiscard]] float magnitude() const { return std::hypot(x, y); }
    /// Degrees from the facing, -180 to 180, positive to the right.
    [[nodiscard]] float angleDegrees() const { return std::atan2(x, y) * 180.0F / std::numbers::pi_v<float>; }
};

/// Which way a stick points from the player.
enum class Side : std::uint8_t { Front, Right, Rear, Left };

/// The side an angle from the facing falls on: within 45° ahead, beyond 135° behind, otherwise right or left. A
/// throw picks its clip this way; a snap attack wants any side but the front.
/// **Coney choice**: exactly 45° is still the front and exactly 135° still a side (the research gives "within 45°"
/// and "beyond 135°").
[[nodiscard]] inline Side sideOf(float angleDegrees) {
    const float off = std::fabs(angleDegrees);
    if (off <= 45.0F) {
        return Side::Front;
    }
    if (off > 135.0F) {
        return Side::Rear;
    }
    return angleDegrees > 0.0F ? Side::Right : Side::Left;
}

/// A small seeded random source for combat's coin flips (which grab strike, where the mugging target moves), so a run
/// with the same seed and input is the same run. A linear congruential generator, the same on every platform.
class CombatRandom {
  public:
    /// Starts the sequence from `seed`.
    explicit CombatRandom(std::uint32_t seed = 1) : m_state(seed) {}
    /// The next 32-bit value.
    std::uint32_t next() {
        m_state = (m_state * 1664525U) + 1013904223U;
        return m_state;
    }
    /// A value in [0, 1), from the high bits (the low bits of this generator are weak).
    float unit() { return static_cast<float>(next() >> 8U) / 16777216.0F; }
    /// True or false, evenly.
    bool coin() { return (next() >> 31U) != 0; }

  private:
    std::uint32_t m_state;
};

} // namespace coney::combat
