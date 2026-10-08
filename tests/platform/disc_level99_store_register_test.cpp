// SPDX-License-Identifier: GPL-3.0-or-later

// Checks against the player's own disc that the store's cash register in `level99` breaks, opens its drawer and spills
// money that player 1 takes by walking over it (docs/research/script-types.md#dyn-cashreg,
// docs/research/player-state.md#walk-over). Checkpoint 2 is played through the level setup Story, `--play-level` and
// the debug menu's jumps share (tests/support/disc_play_fixtures.h), headless, its scene skipped; player 1 is put in
// front of the register and presses square until it breaks. It runs only when the environment variable CONEY_DISC
// names the disc and skips otherwise; it prints counts only (LEGAL.md).

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
#include "scripting/config_strings.h"
#include "support/disc_play_fixtures.h"
#include "warriors/inventory.h"
#include "world/sector_budget.h"
#include "world_objects/props.h"

TEST_CASE("the disc's level99 store: the cash register breaks, opens its drawer and its money is walked over",
          "[disc][story][objects]") {
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

    // The checkpoint's scene skipped (START, then cross on the skip prompt); then square every 0.75 s; then the left
    // stick at 60 % up for 2 s.
    constexpr int kFirstSquare = 540;
    constexpr int kSquareEvery = 45;
    constexpr int kSquares = 12;
    constexpr int kWalkFrame = 1200;
    constexpr int kWalkFrames = 120;
    std::string pad = "300 tap start\n310 tap cross\n";
    for (int i = 0; i < kSquares; ++i) {
        pad += std::format("{} tap square\n", kFirstSquare + (i * kSquareEvery));
    }
    pad += std::format("{} stick left 0 60\n{} stick left 0 0\n", kWalkFrame, kWalkFrame + kWalkFrames);
    auto input = coney::parseInputScript(pad);
    REQUIRE(input.has_value());
    coney::ScriptedInput scripted(std::move(*input));
    coney::GameModeStack stack;
    stack.setInput(&scripted);
    stack.push(gameplay);
    coney::GameTimer timer;
    timer.setFixedStep(true);
    stack.runUntilEmpty(timer, {}, 500);
    auto* play = dynamic_cast<coney::platform::PlayLevelMode*>(gameplay.level());
    REQUIRE(play != nullptr);

    // The store's register (zone 26) and its type.
    const coney::world_objects::SpawnRecord* till = nullptr;
    for (const coney::world_objects::SpawnRecord& record : scripts.spawnRecords().all()) {
        if (record.typeName == "dyn_cashreg" && !record.removed) {
            till = &record;
        }
    }
    REQUIRE(till != nullptr);
    const double handle = till->handle;
    const coney::anim::Vec3 at{till->position[0], till->position[1], till->position[2]};
    std::printf("  level99 store: register at (%.2f, %.2f, %.2f), turn (%.2f, %.2f, %.2f, %.2f), in world %d\n", at.x,
                at.y, at.z, till->rotation[0], till->rotation[1], till->rotation[2], till->rotation[3],
                play->worldObjects().inWorld(handle) ? 1 : 0);
    // Player 1 just south of it, facing it (+y), within the square's reach; square until it breaks.
    play->teleport(coney::debug::Place{.name = "register", .feet = {at.x, at.y - 0.95F, 0.3F}, .headingDegrees = 0.0F});
    const std::string hit = std::format("objects: dyn_cashreg {:.0f} hit", handle);
    const auto isHit = [&hit](const std::string& line) { return line.starts_with(hit); };
    int frame = 500;
    bool broke = false;
    while (frame < kWalkFrame && !broke) {
        stack.runUntilEmpty(timer, {}, 1);
        ++frame;
        broke = std::ranges::any_of(
            log, [&isHit](const std::string& line) { return isHit(line) && line.ends_with(", broke\n"); });
    }
    const auto hits = std::ranges::count_if(log, isHit);
    std::printf("  level99 store: register broken %d by %td bare hits\n", broke ? 1 : 0, hits);
    CHECK(scripts.scripts().errors() == 0);
    // 16 hit points at 2 a bare hit: the eighth breaks it. It stays, with its broken model, and its drawer is out.
    REQUIRE(broke);
    CHECK(hits == 8);
    const coney::world_objects::SpawnRecord* broken = scripts.spawnRecords().find(handle);
    REQUIRE(broken != nullptr);
    CHECK_FALSE(broken->removed);
    CHECK(broken->model == coney::world_objects::kCashRegisterBrokenModel);
    const auto live = [&scripts](std::string_view type) {
        const coney::world_objects::SpawnRecord* found = nullptr;
        for (const coney::world_objects::SpawnRecord& record : scripts.spawnRecords().all()) {
            if (record.typeName == type && !record.removed) {
                found = &record;
            }
        }
        return found;
    };
    CHECK(live("dyn_cashreg_b") != nullptr);

    // One drawer update (1 s) later its money lies 0.22 m above it, $25-49.
    stack.runUntilEmpty(timer, {}, kWalkFrame - frame);
    const coney::world_objects::SpawnRecord* money = live("dyn_money");
    REQUIRE(money != nullptr);
    const double moneyHandle = money->handle;
    const auto dollars = static_cast<int>(money->money);
    std::printf("  level99 store: the drawer spilled $%d, %.2f m above the register\n", dollars,
                money->position[2] - at.z);
    CHECK(dollars >= 25);
    CHECK(dollars <= 49);

    // Player 1 walks north into the counter and takes it on touching it: his money rises by its value.
    const int before = scripts.state().player.inventory.count(0, coney::item::kMoney);
    const coney::anim::Vec3 bills{money->position[0], money->position[1], money->position[2]};
    float nearest = 1e9F;
    for (int step = 0; step < kWalkFrames + 30; ++step) {
        stack.runUntilEmpty(timer, {}, 1);
        const coney::anim::Vec3 feet = play->player().human().position();
        nearest = std::min(nearest, std::hypot(feet.x - bills.x, feet.y - bills.y));
    }
    std::printf("  level99 store: player 1 came within %.2f m of the money in plan\n", nearest);
    const std::string taken = std::format("objects: money {:.0f} taken, ${}", moneyHandle, dollars);
    const bool took = std::ranges::any_of(log, [&taken](const std::string& line) { return line.starts_with(taken); });
    const int after = scripts.state().player.inventory.count(0, coney::item::kMoney);
    std::printf("  level99 store: money taken %d, player's money %d -> %d\n", took ? 1 : 0, before, after);
    CHECK(took);
    CHECK(after == before + dollars);
    const coney::world_objects::SpawnRecord* gone = scripts.spawnRecords().find(moneyHandle);
    CHECK((gone == nullptr || gone->removed));
    CHECK(scripts.scripts().errors() == 0);
}
