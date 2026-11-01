// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <string>

#include "ai/ai_humans.h"
#include "combat/throw_aim.h"
#include "gamemodes/game_mode.h"
#include "human/player.h"

namespace coney {

/// Stub: the level's simulation, to be split out of platform::PlayLevelMode (#1273).
class PlayLevelSimulation : public GameMode {
  public:
    /// Player 1.
    [[nodiscard]] virtual const human::Player& player() const = 0;
    /// The AI fighters.
    [[nodiscard]] virtual const ai::AiHumans& fighters() const = 0;
    /// Player 1's throw aim.
    [[nodiscard]] virtual const combat::ThrowAimState& throwAim() const = 0;
    /// The scenery's name.
    [[nodiscard]] virtual std::string sceneName() const = 0;
    /// How many AI humans there are in the world.
    [[nodiscard]] virtual std::size_t fighterCount() const = 0;
};

} // namespace coney
