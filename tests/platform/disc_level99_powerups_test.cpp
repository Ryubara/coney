// SPDX-License-Identifier: GPL-3.0-or-later

// Checks against the player's own disc that player 1 takes a power-up by walking into it, as the original's runtime
// check did (docs/research/player-state.md#walk-over): a `dyn_spraycan` put 2.5 m ahead of him, then the left stick at
// 60 % toward it. `level99` checkpoint 2 is played through the level setup Story, `--play-level` and the debug menu's
// jumps share (tests/support/disc_play_fixtures.h), headless, its scene skipped. It runs only when the environment
// variable CONEY_DISC names the disc and skips otherwise; it prints counts only (LEGAL.md).

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/game_random.h"
#include "core/game_timer.h"
#include "core/input_script.h"
#include "debug/play_controls.h"
#include "fileio/executable.h"
#include "fileio/wad.h"
#include "gamemodes/game_mode_stack.h"
#include "gamemodes/gameplay_mode.h"
#include "gamemodes/level_start.h"
#include "human/player.h"
#include "platform/play_level_mode.h"
#include "platform/render_engine.h"
#include "scenes/scene_disc.h"
#include "scenes/scene_list.h"
#include "scenes/scene_player.h"
#include "support/disc_play_fixtures.h"
#include "warriors/inventory.h"
#include "world/sector_budget.h"

TEST_CASE("the disc's level99: player 1 takes a spray can by walking into it", "[disc][story][objects]") {
    std::optional<coney::io::Wad> wad = coney::test::openDisc();
    if (!wad) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    auto engine = coney::platform::RenderEngine::start(coney::platform::RenderBackend::Null, {});
    REQUIRE(engine.has_value());
    coney::platform::RenderEngine& renderer = **engine;
    coney::world::SectorBudget budget(coney::world::kSectorPoolSize);
    std::vector<std::string> log;
    const auto print = [&log](std::string_view line) { log.emplace_back(line); };

    // The scripts as the story reaches checkpoint 2, with the game's random table, over the shared level setup.
    coney::LevelScriptOptions options;
    std::vector<std::uint32_t> table;
    if (auto words = coney::io::readExecutableWords(wad->disc(), coney::GameRandom::kExecutableName,
                                                    coney::GameRandom::kTableAddress, coney::GameRandom::kTableSize)) {
        table = std::move(*words);
        options.randomTable = table;
    }
    coney::LevelScripts scripts(coney::script::wadScriptSource(*wad), "level99", 2, print, options);
    auto sceneList = coney::scenes::loadSceneList(*wad);
    REQUIRE(sceneList.has_value());
    coney::GameplayMode gameplay(renderer, scripts.scripts(), scripts.context(), scripts.state(), scripts.humans(),
                                 scripts.flags(), scripts.recorded(),
                                 coney::test::playLoader(renderer, *wad, budget, print), print);
    gameplay.setLevel("level99");
    gameplay.setSceneMaker([&wad, &sceneList] {
        return std::make_unique<coney::scenes::SceneSystem>(*sceneList, coney::scenes::wadSceneSource(*wad),
                                                            coney::scenes::SceneSystem::ScriptCall{});
    });

    // The checkpoint's scene skipped (START, then cross on the skip prompt); then the left stick at 60 % up for 2 s.
    constexpr int kWalkFrame = 540;
    constexpr int kWalkFrames = 120;
    auto input = coney::parseInputScript(std::format(
        "300 tap start\n310 tap cross\n{} stick left 0 60\n{} stick left 0 0\n", kWalkFrame, kWalkFrame + kWalkFrames));
    REQUIRE(input.has_value());
    coney::ScriptedInput pad(std::move(*input));
    coney::GameModeStack stack;
    stack.setInput(&pad);
    stack.push(gameplay);
    coney::GameTimer timer;
    timer.setFixedStep(true);
    stack.runUntilEmpty(timer, {}, 500);
    auto* play = dynamic_cast<coney::platform::PlayLevelMode*>(gameplay.level());
    REQUIRE(play != nullptr);

    // Player 1 where he stands, turned north (the camera turns behind him), and a spray can 2.5 m north of his feet.
    const coney::anim::Vec3 start = play->player().human().position();
    play->teleport(coney::debug::Place{.name = "start", .feet = start, .headingDegrees = 0.0F});
    stack.runUntilEmpty(timer, {}, 20);
    const coney::anim::Vec3 feet = play->player().human().position();
    coney::world_objects::SpawnRecord record;
    record.handle = scripts.scripts().nextObjectHandle();
    record.typeName = "dyn_spraycan";
    record.position = {feet.x, feet.y + 2.5F, feet.z};
    REQUIRE(scripts.spawnRecords().add(record) != nullptr);
    const coney::anim::Vec3 can{record.position[0], record.position[1], record.position[2]};
    const int before = scripts.state().player.inventory.count(0, coney::item::kSprayPaint);

    // Walk at it until he takes it (or the stick is let go), noting how near he was then.
    const std::string taken = std::format("pickup: walked over object {:.0f}: item 3 x1", record.handle);
    const auto took = [&log, &taken] {
        return std::ranges::any_of(log, [&taken](const std::string& line) { return line.starts_with(taken); });
    };
    float takenAt = -1.0F;
    for (int frame = 520; frame < kWalkFrame + kWalkFrames + 30 && !took(); ++frame) {
        stack.runUntilEmpty(timer, {}, 1);
        if (took()) {
            const coney::anim::Vec3 at = play->player().human().position();
            takenAt = std::hypot(at.x - can.x, at.y - can.y);
        }
    }
    const int after = scripts.state().player.inventory.count(0, coney::item::kSprayPaint);
    std::printf("  level99 walk-over: spray can taken %d at %.2f m in plan; spray paint %d -> %d\n", took() ? 1 : 0,
                takenAt, before, after);
    CHECK(scripts.scripts().errors() == 0);
    // Taken on touching it (the runtime's 1.1 m), one charge more, and gone for good.
    REQUIRE(took());
    CHECK(takenAt <= 1.1F);
    CHECK(takenAt > 0.9F);
    CHECK(after == std::min(before + 1, 9));
    const coney::world_objects::SpawnRecord* gone = scripts.spawnRecords().find(record.handle);
    CHECK((gone == nullptr || gone->removed));
}
