// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <expected>
#include <memory>
#include <string>
#include <string_view>

#include "ai/ai_humans.h"
#include "combat/throw_aim.h"
#include "core/error.h"
#include "gamemodes/game_mode.h"
#include "human/human.h"
#include "human/player.h"
#include "raycast/collision_mesh.h"
#include "world_objects/flags.h"

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
    /// Stub: the script's teleport, which does nothing yet.
    virtual void teleportPlayer(const world_objects::Placement& /*placement*/,
                                world_objects::TeleportMotion /*motion*/) {}

  protected:
    /// Stub: no ground.
    [[nodiscard]] virtual const raycast::CollisionMesh* placeGround(double /*human*/) const { return nullptr; }
    /// Stub: loads nothing.
    [[nodiscard]] virtual std::expected<std::unique_ptr<human::PlayerCharacter>, Error>
    loadCastCharacter(std::string_view /*model*/) {
        return fail(ErrorCode::NotFound, "stub");
    }
    /// Stub: hears of no human.
    virtual void humanMade(const human::Human& /*human*/, const std::string& /*model*/) {}
    /// Stub: hears of no model change.
    virtual void changePlayerModel(const std::string& /*model*/) {}
};

} // namespace coney
