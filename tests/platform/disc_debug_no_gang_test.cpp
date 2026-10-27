// SPDX-License-Identifier: GPL-3.0-or-later

// Checks against the player's own disc that a fighter the AI fighters page spawns while every gang slot is in use is
// made with no gang and the play mode says so (docs/guides/debug-menu.md): in the sandbox's fight yard, with the
// gangs' 32 slots taken, one ally and one neutral are spawned (the layout's fighters already made the enemies' gang,
// which has room). It runs only when the environment variable CONEY_DISC names the disc and skips otherwise.

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "ai/ai_config.h"
#include "ai/ai_humans.h"
#include "ai/fighter_side.h"
#include "ai/gangs.h"
#include "characters/character_types.h"
#include "core/game_timer.h"
#include "debug/play_controls.h"
#include "fileio/wad.h"
#include "gamemodes/game_mode_stack.h"
#include "gamemodes/level_start.h"
#include "platform/play_level_mode.h"
#include "platform/render_engine.h"
#include "platform/sandbox_play_scenery.h"
#include "sandbox/sandbox_world.h"
#include "scripting/config_strings.h"
#include "support/disc_play_fixtures.h"
#include "support/test_paths.h"

TEST_CASE("a fighter spawned with every gang slot in use has no gang, and the play mode says so",
          "[disc][debug][fighters]") {
    // Expected: unverified, #924 (Coney's own debug tool)
    std::optional<coney::io::Wad> wad = coney::test::openDisc();
    if (!wad) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    auto engine = coney::platform::RenderEngine::start(coney::platform::RenderBackend::Null, {});
    REQUIRE(engine.has_value());
    // The sandbox's setup as main makes it: level99's configuration.
    const coney::LevelScriptRun scriptRun =
        coney::runLevelScriptAlone(coney::script::wadScriptSource(*wad), "level99", 1, {});
    coney::platform::PlayerSetup setup;
    setup.ai = coney::ai::aiConfigFrom(scriptRun.recorded);
    setup.types = coney::characters::CharacterTypes::fromRecorded(scriptRun.recorded);
    auto world = coney::sandbox::SandboxWorld::load(coney::test::assetsDir() / "sandbox", "combat");
    REQUIRE(world.has_value());
    std::vector<std::string> lines;
    auto made = coney::platform::createPlayInSandbox(
        **engine, *wad, std::move(*world), std::nullopt, [&lines](std::string_view line) { lines.emplace_back(line); },
        setup);
    REQUIRE(made.has_value());
    coney::platform::PlayLevelMode& mode = **made;
    coney::GameModeStack modes;
    coney::GameTimer timer;
    timer.setFixedStep(true);
    modes.push(mode);
    (void)modes.runUntilEmpty(timer, {}, 10);

    // A level script's gangs take every slot.
    coney::ai::Gangs& gangs = mode.fighters().brains().gangs();
    for (int i = 0; gangs.create(21, "Script" + std::to_string(i)) >= 0; ++i) {
    }
    lines.clear();
    for (const coney::ai::FighterSide side : coney::ai::kFighterSides) {
        REQUIRE(mode.spawnFighterAs(coney::debug::spotAhead(mode.playerFeet(), mode.playerHeadingDegrees(), 4.0F),
                                    mode.playerHeadingDegrees() + 180.0F, {}, side));
    }
    // Each of the two is made with no gang, and the mode said so once for each.
    std::size_t told = 0;
    for (const std::string& line : lines) {
        told += line.find("no free gang slot") != std::string::npos ? 1U : 0U;
    }
    std::size_t gangless = 0;
    for (const coney::ai::AiHuman& entry : mode.fighters().humans()) {
        const coney::ai::Brain* brain = mode.fighters().brainOf(*entry.human);
        gangless += entry.debugSpawned && brain != nullptr && brain->gang() == nullptr ? 1U : 0U;
    }
    CHECK(gangless == 2);
    CHECK(told == 2);
}
