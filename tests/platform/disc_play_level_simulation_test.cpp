// SPDX-License-Identifier: GPL-3.0-or-later

// Checks against the player's own disc that the play mode's simulation is the core base the platform's mode derives
// from (coney::PlayLevelSimulation, src/gamemodes/play_level_simulation.h, the simulation out of src/platform:
// docs/guides/network-architecture.md#sub-missions): `--play-level level99` played headless, the player walked
// forward for two seconds by a scripted pad, and what the simulation holds (player 1, the fighters, the throw aim, the
// scenery's name) read through the base, which builds without librw or SDL, agrees with what the platform's mode
// reports. It runs only when the environment variable CONEY_DISC names the disc and skips otherwise; it prints counts
// only (LEGAL.md).

#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>
#include <utility>

#include <catch2/catch_test_macros.hpp>

#include "core/game_timer.h"
#include "core/input_script.h"
#include "fileio/wad.h"
#include "gamemodes/game_mode.h"
#include "gamemodes/game_mode_stack.h"
#include "gamemodes/play_level_simulation.h"
#include "human/player.h"
#include "platform/play_level_mode.h"
#include "platform/render_engine.h"
#include "support/disc_play_fixtures.h"
#include "world/sector_budget.h"

namespace {

// Two seconds of game steps at 30 a second.
constexpr std::uint64_t kSteps = 60;
// The stick full forward from the first step to the last.
constexpr std::string_view kWalk = "1 stick left 0 100\n";

} // namespace

TEST_CASE("the disc's level99: the play mode's simulation is the core PlayLevelSimulation the platform's mode adds "
          "drawing to",
          "[disc][play][network]") {
    std::optional<coney::io::Wad> wad = coney::test::openDisc();
    if (!wad) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    auto engine = coney::platform::RenderEngine::start(coney::platform::RenderBackend::Null, {});
    REQUIRE(engine.has_value());
    coney::world::SectorBudget budget(coney::world::kSectorPoolSize);
    auto mode = coney::platform::PlayLevelMode::create(**engine, *wad, "level99", budget, [](std::string_view) {});
    REQUIRE(mode.has_value());

    // Expected: docs/guides/network-architecture.md#sub-missions (the level's simulation is the core base the
    // platform's mode derives from; every value below is the platform mode's own, read through both).
    // The platform's mode is a core simulation: everything the simulation holds is reachable through the base.
    coney::GameMode& asMode = **mode;
    const auto* simulation = dynamic_cast<const coney::PlayLevelSimulation*>(&asMode);
    REQUIRE(simulation != nullptr);

    auto script = coney::parseInputScript(kWalk);
    REQUIRE(script.has_value());
    coney::ScriptedInput input(std::move(*script));
    coney::GameModeStack stack;
    stack.push(asMode);
    stack.setInput(&input);
    coney::GameTimer timer;
    timer.setFixedStep(true);
    const std::uint64_t ran = stack.runUntilEmpty(timer, {}, kSteps);
    CHECK(ran == kSteps);

    // The base's player is the platform mode's: the same human, moved by the pad.
    const coney::human::Player& player = simulation->player();
    CHECK(&player == &(*mode)->player());
    const coney::anim::Vec3 feet = player.human().position();
    const coney::anim::Vec3 reported = (*mode)->playerFeet();
    CHECK(feet.x == reported.x);
    CHECK(feet.y == reported.y);
    CHECK(feet.z == reported.z);
    CHECK((*mode)->stats().travelled > 1.0F);
    // A level with no scripts brings no fighters, and nobody aims a throw.
    CHECK(&simulation->fighters() == &(*mode)->fighters());
    CHECK(simulation->fighterCount() == 0);
    CHECK_FALSE(simulation->throwAim().active());
    CHECK(simulation->sceneName() == (*mode)->sceneName());
    CHECK(simulation->sceneName() == "level99");
}
