// SPDX-License-Identifier: GPL-3.0-or-later
// The level's core simulation base (docs/guides/network-architecture.md#sub-missions): the shape of
// PlayLevelSimulation's platform hooks, without the game. The base itself needs a character loaded from the disc to be
// built; tests/platform/disc_play_level_simulation_test.cpp covers that, and tests/hud/hud_owner_test.cpp its HUD.
#include <cstdint>
#include <expected>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>

#include <catch2/catch_test_macros.hpp>

#include "core/error.h"
#include "gamemodes/game_mode.h"
#include "gamemodes/gameplay_mode.h"
#include "gamemodes/play_level_simulation.h"

using coney::PlayLevelSimulation;

namespace {

// A presentation that answers every hook of the simulation: it compiles only while the hooks keep these signatures
// (each is marked override), and they are all a platform must add to make the base a mode.
class HookedSimulation final : public PlayLevelSimulation {
  public:
    // A mode id from Coney's own range.
    [[nodiscard]] std::uint32_t id() const override { return 0x1ffU; }
    // No step: the shape is what is tested.
    coney::ModeResult update(coney::GameModeStack& /*stack*/, const coney::FrameTime& /*frame*/) override {
        return coney::ModeResult::Stay;
    }
    // The script's teleport has not moved to the core yet: the platform's mode still answers it.
    void teleportPlayer(const coney::world_objects::Placement& /*placement*/,
                        coney::world_objects::TeleportMotion /*motion*/) override {}

  protected:
    // No ground for anyone.
    [[nodiscard]] const coney::raycast::CollisionMesh* placeGround(double /*human*/) const override { return nullptr; }
    // Nothing loads without the disc.
    [[nodiscard]] std::expected<std::unique_ptr<coney::human::PlayerCharacter>, coney::Error>
    loadCastCharacter(std::string_view /*model*/) override {
        return coney::fail(coney::ErrorCode::NotFound, "no disc");
    }
    // Nothing is drawn.
    void humanMade(const coney::human::Human& /*human*/, const std::string& /*model*/) override {}
    // Nothing is drawn.
    void changePlayerModel(const std::string& /*model*/) override {}
};

} // namespace

// Expected: docs/guides/network-architecture.md#sub-missions (Coney's own split; the original has none)
TEST_CASE("the core simulation is an abstract mode the platform completes with its hooks alone") {
    // Checked when the test runs, not when it compiles, so a base of another shape builds and fails here.
    CHECK(std::is_abstract_v<PlayLevelSimulation>);
    CHECK(std::is_base_of_v<coney::GameMode, PlayLevelSimulation>);
    CHECK(std::is_base_of_v<coney::ScriptedPlayer, PlayLevelSimulation>);
    CHECK_FALSE(std::is_copy_constructible_v<PlayLevelSimulation>);
    CHECK_FALSE(std::is_move_constructible_v<PlayLevelSimulation>);
    // The four hooks, the mode's id, its step and the script's teleport are all a presentation must give.
    CHECK_FALSE(std::is_abstract_v<HookedSimulation>);
}
