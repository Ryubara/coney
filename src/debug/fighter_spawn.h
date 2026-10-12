// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "animation/anim_math.h"

namespace coney::debug {

/// The most fighters one click of the AI fighters page spawns.
inline constexpr int kMaxFighterAmount = 50;

/// A small random generator of the debug menus' own (xorshift32), for picking a random model: it never draws from the
/// game's table (GameRandom), so spawning fighters does not move the game's random numbers, and it gives the same
/// sequence on every platform, which the standard library's distributions do not.
class SpawnRandom {
  public:
    /// A generator from `seed`; a seed of 0 counts as 1 (xorshift is stuck at 0).
    explicit SpawnRandom(std::uint32_t seed) : m_state(seed == 0 ? 1U : seed) {}
    /// The next raw number.
    [[nodiscard]] std::uint32_t next();
    /// A number in [0, n); 0 when `n` is 0.
    [[nodiscard]] std::size_t below(std::size_t n);

  private:
    std::uint32_t m_state;
};

/// The models for `amount` fighters. `choice` is the model picker's index: 0 is Random (each fighter gets its own
/// model, drawn from `models` with `random`), a larger one is `models[choice - 1]` for every fighter. With no `models`
/// (a mode that does not know the configuration's types) every entry is empty, which the play mode reads as "the scene's
/// own character". A choice past the end counts as Random. An amount below 1 gives none; above kMaxFighterAmount, that
/// many.
[[nodiscard]] std::vector<std::string> pickFighterModels(std::span<const std::string> models, std::size_t choice,
                                                         int amount, SpawnRandom& random);

/// Where `amount` fighters stand: rows of five, `distance` metres in front of a player whose feet are `playerFeet`,
/// facing `playerHeadingDegrees` (0 faces +y), each row 1.2 m further out and each fighter 1.2 m from the next
/// across, the row centred on the player's line. The first is exactly `distance` ahead when it stands alone.
[[nodiscard]] std::vector<anim::Vec3> fighterSpots(anim::Vec3 playerFeet, float playerHeadingDegrees, float distance,
                                                   int amount);

} // namespace coney::debug
