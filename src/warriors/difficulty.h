// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace coney::difficulty {

// The difficulty in force (GameState::difficulty, `W_GameState + 0x154`) as the difficulty scripts set it.
inline constexpr std::uint8_t kEasy = 0;
inline constexpr std::uint8_t kNormal = 1;
inline constexpr std::uint8_t kHard = 2;
inline constexpr std::uint8_t kFury = 3;
inline constexpr std::uint8_t kRumble = 4;

// Stub for the test commit: the full heal Coney always gave.
[[nodiscard]] constexpr int flashHealth(int /*health*/, int maximum, bool /*player*/, std::uint8_t /*difficulty*/) {
    return maximum;
}

} // namespace coney::difficulty
