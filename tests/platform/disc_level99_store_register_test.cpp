// SPDX-License-Identifier: GPL-3.0-or-later

// Checks against the player's own disc that the store's cash register in `level99` breaks, opens its drawer and spills
// money that player 1 takes by walking over it, and that triangle lifts it whole, its drawer gone
// (docs/research/script-types.md#dyn-cashreg, docs/research/player-state.md#walk-over). Checkpoint 2 is played through
// the level setup Story, `--play-level` and the debug menu's jumps share (tests/support/disc_play_fixtures.h),
// headless, its scene skipped; player 1 is put in front of the register. It runs only when the environment variable
// CONEY_DISC names the disc and skips otherwise; it prints counts only (LEGAL.md).

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <format>
#include <functional>
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

namespace {

// level99's checkpoint 2 as the story reaches it, with the game's random table, over the shared level setup, headless
// and at a fixed step, under the pad `pad`; run to frame 500 (its scene skipped by the pad), with the store's register.
class StoreRun {
  public:
    StoreRun(coney::io::Wad& wad, const std::string& pad)
        : m_budget(coney::world::kSectorPoolSize), m_wad(wad),
          m_print([this](std::string_view line) { log.emplace_back(line); }) {
        auto engine = coney::platform::RenderEngine::start(coney::platform::RenderBackend::Null, {});
        REQUIRE(engine.has_value());
        m_engine = std::move(*engine);
        coney::LevelScriptOptions options;
        if (auto words =
                coney::io::readExecutableWords(wad.disc(), coney::GameRandom::kExecutableName,
                                               coney::GameRandom::kTableAddress, coney::GameRandom::kTableSize)) {
            m_table = std::move(*words);
            options.randomTable = m_table;
        }
        scripts =
            std::make_unique<coney::LevelScripts>(coney::script::wadScriptSource(wad), "level99", 2, m_print, options);
        auto list = coney::scenes::loadSceneList(wad);
        REQUIRE(list.has_value());
        m_sceneList = std::move(*list);
        m_gameplay = std::make_unique<coney::GameplayMode>(
            *m_engine, scripts->scripts(), scripts->context(), scripts->state(), scripts->humans(), scripts->flags(),
            scripts->recorded(), coney::test::playLoader(*m_engine, wad, m_budget, m_print), m_print);
        m_gameplay->setLevel("level99");
        m_gameplay->setSceneMaker([this] {
            return std::make_unique<coney::scenes::SceneSystem>(*m_sceneList, coney::scenes::wadSceneSource(m_wad),
                                                                coney::scenes::SceneSystem::ScriptCall{});
        });
        auto input = coney::parseInputScript(pad);
        REQUIRE(input.has_value());
        m_input = std::make_unique<coney::ScriptedInput>(std::move(*input));
        stack.setInput(m_input.get());
        stack.push(*m_gameplay);
        m_timer.setFixedStep(true);
        run(kStartFrames);
        play = dynamic_cast<coney::platform::PlayLevelMode*>(m_gameplay->level());
        REQUIRE(play != nullptr);
        till = live("dyn_cashreg");
        REQUIRE(till != nullptr);
    }

    // Runs `frames` more frames.
    void run(int frames) { stack.runUntilEmpty(m_timer, {}, static_cast<std::uint64_t>(frames)); }
    // The last live record of `type`; null for none.
    [[nodiscard]] const coney::world_objects::SpawnRecord* live(std::string_view type) const {
        const coney::world_objects::SpawnRecord* found = nullptr;
        for (const coney::world_objects::SpawnRecord& record : scripts->spawnRecords().all()) {
            if (record.typeName == type && !record.removed) {
                found = &record;
            }
        }
        return found;
    }

    static constexpr int kStartFrames = 500;
    std::vector<std::string> log;

  private:
    // Declared in the order they are made, so each goes before what it refers to.
    coney::world::SectorBudget m_budget;
    coney::io::Wad& m_wad;
    std::function<void(std::string_view)> m_print;
    std::unique_ptr<coney::platform::RenderEngine> m_engine;
    std::vector<std::uint32_t> m_table;
    std::optional<coney::scenes::SceneList> m_sceneList;

  public:
    std::unique_ptr<coney::LevelScripts> scripts;

  private:
    std::unique_ptr<coney::GameplayMode> m_gameplay;
    std::unique_ptr<coney::ScriptedInput> m_input;

  public:
    coney::GameModeStack stack;
    coney::platform::PlayLevelMode* play = nullptr;
    const coney::world_objects::SpawnRecord* till = nullptr; // the store's register (zone 26)

  private:
    coney::GameTimer m_timer;
};

} // namespace

TEST_CASE("the disc's level99 store: the cash register breaks, opens its drawer and its money is walked over",
          "[disc][story][objects]") {
    std::optional<coney::io::Wad> wad = coney::test::openDisc();
    if (!wad) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
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
    StoreRun store(*wad, pad);
    coney::platform::PlayLevelMode* play = store.play;
    const std::vector<std::string>& log = store.log;
    coney::LevelScripts& scripts = *store.scripts;
    const coney::world_objects::SpawnRecord* till = store.till;
    const double handle = till->handle;
    const coney::anim::Vec3 at{till->position[0], till->position[1], till->position[2]};
    std::printf("  level99 store: register at (%.2f, %.2f, %.2f), turn (%.2f, %.2f, %.2f, %.2f), in world %d\n", at.x,
                at.y, at.z, till->rotation[0], till->rotation[1], till->rotation[2], till->rotation[3],
                play->worldObjects().inWorld(handle) ? 1 : 0);
    // Player 1 just south of it, facing it (+y), within the square's reach; square until it breaks.
    play->teleport(coney::debug::Place{.name = "register", .feet = {at.x, at.y - 0.95F, 0.3F}, .headingDegrees = 0.0F});
    const std::string hit = std::format("objects: dyn_cashreg {:.0f} hit", handle);
    const auto isHit = [&hit](const std::string& line) { return line.starts_with(hit); };
    int frame = StoreRun::kStartFrames;
    bool broke = false;
    while (frame < kWalkFrame && !broke) {
        store.run(1);
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
    const auto live = [&store](std::string_view type) { return store.live(type); };
    CHECK(live("dyn_cashreg_b") != nullptr);

    // The drawer jumped 0.35 m out (north, behind the counter) and at its next update spilled the money 0.22 m above
    // it, $25-50.
    store.run(kWalkFrame - frame);
    const coney::world_objects::SpawnRecord* money = live("dyn_money");
    REQUIRE(money != nullptr);
    const double moneyHandle = money->handle;
    const auto dollars = static_cast<int>(money->money);
    std::printf("  level99 store: the drawer spilled $%d, %.2f m above the register base\n", dollars,
                money->position[2] - at.z);
    CHECK(dollars >= 25);
    CHECK(dollars <= 50);
    REQUIRE(live("dyn_cashreg_b") != nullptr);
    CHECK(money->position[1] > at.y + 0.3F);

    // Player 1, put behind the counter 1.3 m north of it facing south, walks back at it and takes it on touching it:
    // his money rises by its value.
    const int before = scripts.state().player.inventory.count(0, coney::item::kMoney);
    const coney::anim::Vec3 bills{money->position[0], money->position[1], money->position[2]};
    play->teleport(coney::debug::Place{
        .name = "behind the counter", .feet = {bills.x, bills.y + 1.3F, 0.3F}, .headingDegrees = 180.0F});
    float nearest = 1e9F;
    for (int step = 0; step < kWalkFrames + 30; ++step) {
        store.run(1);
        const coney::anim::Vec3 feet = play->player().human().position();
        nearest = std::min(nearest, std::hypot(feet.x - bills.x, feet.y - bills.y));
    }
    std::printf("  level99 store: player 1 came within %.2f m of the money in plan\n", nearest);
    const std::string taken = std::format("pickup: walked over object {:.0f}: item 2 x{}", moneyHandle, dollars);
    const bool took = std::ranges::any_of(log, [&taken](const std::string& line) { return line.starts_with(taken); });
    const int after = scripts.state().player.inventory.count(0, coney::item::kMoney);
    std::printf("  level99 store: money taken %d, player's money %d -> %d\n", took ? 1 : 0, before, after);
    CHECK(took);
    CHECK(after == before + dollars);
    const coney::world_objects::SpawnRecord* gone = scripts.spawnRecords().find(moneyHandle);
    CHECK((gone == nullptr || gone->removed));
    CHECK(scripts.scripts().errors() == 0);
}

TEST_CASE("the disc's level99 store: triangle takes the cash register's drawer away and lifts the register",
          "[disc][story][objects]") {
    std::optional<coney::io::Wad> wad = coney::test::openDisc();
    if (!wad) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    // The checkpoint's scene skipped (START, then cross on the skip prompt); then triangle once.
    constexpr int kTriangle = 540;
    constexpr int kEnd = 700;
    StoreRun store(*wad, std::format("300 tap start\n310 tap cross\n{} tap triangle\n", kTriangle));
    const double handle = store.till->handle;
    const coney::anim::Vec3 at{store.till->position[0], store.till->position[1], store.till->position[2]};
    REQUIRE(store.live("dyn_cashreg_b") != nullptr);
    // Player 1 just south of it, facing it (+y), within triangle's reach.
    store.play->teleport(
        coney::debug::Place{.name = "register", .feet = {at.x, at.y - 0.95F, 0.3F}, .headingDegrees = 0.0F});
    store.run(kEnd - StoreRun::kStartFrames);
    const auto has = [&store](const std::string& start) {
        return std::ranges::any_of(store.log, [&start](const std::string& line) { return line.starts_with(start); });
    };
    const bool used = has(std::format("objects: dyn_cashreg {:.0f} used, lifted", handle));
    const bool picked = has(std::format("pickup: object {:.0f} with clip 504", handle));
    const bool took = has(std::format("pickup: took object {:.0f}", handle));
    std::printf("  level99 store: register lifted %d, pick-up clip %d, in hand %d, drawers left %d\n", used ? 1 : 0,
                picked ? 1 : 0, took ? 1 : 0, store.live("dyn_cashreg_b") != nullptr ? 1 : 0);
    // Its message 0 deletes the drawer and asks to be picked up (0x14): the two-hand pick-up, then it is in hand.
    CHECK(used);
    CHECK(picked);
    CHECK(took);
    CHECK(store.live("dyn_cashreg_b") == nullptr);
    CHECK(store.scripts->scripts().errors() == 0);
}
