// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "core/error.h"
#include "human/place.h"
#include "human/player.h"
#include "raycast/collision_mesh.h"
#include "sandbox/sandbox_world.h"
#include "world/level_ground.h"

namespace coney::sandbox {

/// A sandbox as the play mode's ground (docs/guides/sandbox.md): its collision mesh, the player starting at one of the
/// layout's spawn points dropped onto the ground below it, the spawn points as the places, and the layout's targets
/// and fighters. The simulation's half of the sandbox; platform::SandboxPlayScenery draws it.
class SandboxGround final : public world::LevelGround {
  public:
    /// How far above a spawn point the player is dropped onto the ground from, metres.
    static constexpr float kSpawnDrop = 2.0F;

    /// The ground of `world`, the player starting at spawn point `spawn` (the layout's first when unset), dropped onto
    /// the ground from kSpawnDrop above it (kept where it is when nothing is below within twice that). Fails with
    /// ErrorCode::NotFound for a layout with nothing solid and for an unknown spawn, the message naming the known ones.
    [[nodiscard]] static std::expected<std::unique_ptr<SandboxGround>, Error>
    create(SandboxWorld world, const std::optional<std::string>& spawn);

    [[nodiscard]] const raycast::CollisionMesh& collision() const override { return *m_world.collision(); }
    [[nodiscard]] human::PlayerStart start() const override { return m_start; }
    [[nodiscard]] std::string startSource() const override { return "spawn point " + m_spawn; }
    [[nodiscard]] std::string name() const override { return "sandbox " + m_world.layout().title; }
    /// The layout's spawn points, by name, where the layout puts them (not dropped).
    [[nodiscard]] std::vector<human::Place> places() const override;
    /// The layout's `target` lines.
    [[nodiscard]] std::vector<world::SceneTarget> targets() const override;
    /// The layout's `fighter` lines.
    [[nodiscard]] std::vector<human::Place> fighters() const override;

    /// The sandbox's world: its layout, collision and drawn mesh.
    [[nodiscard]] const SandboxWorld& world() const { return m_world; }
    /// Replaces the world with `world` (the layout rebuilt with added objects), keeping the start as it was. Fails with
    /// ErrorCode::InvalidArgument when `world` has nothing solid; the ground is then unchanged.
    std::expected<void, Error> setWorld(SandboxWorld world);

  private:
    SandboxGround(SandboxWorld world, const human::PlayerStart& start, std::string spawn);

    SandboxWorld m_world;
    human::PlayerStart m_start;
    std::string m_spawn; // the spawn point's name
};

} // namespace coney::sandbox
