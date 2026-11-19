// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace coney::difficulty {

// The difficulty in force (GameState::difficulty, `W_GameState + 0x154`) as the difficulty scripts set it
// (docs/research/difficulty.md#scripts).
inline constexpr std::uint8_t kEasy = 0;   ///< SUCKER, `config_easy`.
inline constexpr std::uint8_t kNormal = 1; ///< SOLDIER, `config_normal`; the game state's start value.
inline constexpr std::uint8_t kHard = 2;   ///< HARDCORE SOLDIER, `config_hard`.
inline constexpr std::uint8_t kFury = 3;   ///< UNLEASH THE FURY, `config_fury`.
inline constexpr std::uint8_t kRumble = 4; ///< Every Rumble arena, `config_rumble`.

/// The health a flash leaves a human at `health` of `maximum`: `Flash_Use` asks `Human_Heal` for the full heal unless
/// the human is a `player` and `difficulty` (the difficulty in force) is above kHard, when `Human_Heal` adds half the
/// maximum, rounded down, and caps the sum at the maximum (docs/research/difficulty.md#flash).
/// @orig 0x00284280 Flash_Use (unknown)
[[nodiscard]] constexpr int flashHealth(int health, int maximum, bool player, std::uint8_t difficulty) {
    // The full heal for anyone but a player, and for a player up to hard.
    const bool full = !player || difficulty <= kHard;
    if (full) {
        return maximum;
    }
    // Human_Heal's partial heal (0x0022eb40): half the maximum on top, capped.
    const int healed = health + (maximum >> 1);
    return healed < maximum ? healed : maximum;
}

} // namespace coney::difficulty
