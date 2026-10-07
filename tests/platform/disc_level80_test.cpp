// SPDX-License-Identifier: GPL-3.0-or-later

// Checks against the player's own disc that the second story mission's player-side mechanics play
// (docs/missions/level80.md): at checkpoint 1 the Warrior command menu, held open with R2 and steered up with the
// right stick, gives the follow order the script waits for (`WCSetCallback("CommandIssued")`,
// docs/research/hud.md#warrior-command-menu); at checkpoint 2 the two cuffed Warriors offer the uncuff prompt and the
// L1-R1 mash frees each, which the script hears as event 17 and moves on to checkpoint 3
// (docs/research/crimes.md#uncuffing). It runs only when the environment variable CONEY_DISC names the disc and skips
// otherwise; it prints counts only (LEGAL.md).

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "core/game_random.h"
#include "core/game_timer.h"
#include "core/pad.h"
#include "core/pads.h"
#include "fileio/executable.h"
#include "fileio/wad.h"
#include "gamemodes/game_mode_stack.h"
#include "gamemodes/gameplay_mode.h"
#include "gamemodes/level_start.h"
#include "gui/global_strings.h"
#include "hud/hud.h"
#include "human/player.h"
#include "platform/play_level_mode.h"
#include "platform/render_engine.h"
#include "scenes/scene_disc.h"
#include "scenes/scene_list.h"
#include "scenes/scene_player.h"
#include "scripting/config_strings.h"
#include "scripting/lua_value.h"
#include "scripting/script_system.h"
#include "warriors/inventory.h"
#include "world/sector_budget.h"
#include "world_objects/flags.h"

#include "support/disc_play_fixtures.h"

namespace {

// A pad the test drives frame by frame: the buttons held and the right stick in [-1, 1] with y up.
class DrivenPad final : public coney::InputSource {
  public:
    std::uint16_t buttons = 0;
    float rightX = 0.0F;
    float rightY = 0.0F;

    coney::PortSamples sample(std::uint64_t /*frame*/) override {
        coney::PortSamples samples{};
        coney::PadSample& pad = samples[0];
        pad.connected = true;
        pad.buttons = buttons;
        pad.sticks[0] = raw(rightX);
        pad.sticks[1] = raw(-rightY);
        return samples;
    }

  private:
    // The raw byte libpad would give for `value`: 0-255 round the centre.
    static std::uint8_t raw(float value) {
        constexpr float kCentre = 127.5F;
        return static_cast<std::uint8_t>(std::lround(kCentre + (std::clamp(value, -1.0F, 1.0F) * kCentre)));
    }
};

// level80 loaded at `checkpoint` as `--play-level level80 --checkpoint N` loads it, the play mode drawing the
// scripts' HUD, and stepped by a driven pad.
struct Level80 {
    std::vector<std::string> log;
    std::vector<std::uint32_t> table;
    coney::world::SectorBudget budget{coney::world::kSectorPoolSize};
    std::unique_ptr<coney::LevelScripts> scripts;
    std::unique_ptr<coney::GameplayMode> gameplay;
    DrivenPad pad;
    coney::GameModeStack stack;
    coney::GameTimer timer;

    Level80(coney::platform::RenderEngine& renderer, const coney::io::Wad& wad, int checkpoint) {
        coney::LevelScriptOptions options;
        if (auto words =
                coney::io::readExecutableWords(wad.disc(), coney::GameRandom::kExecutableName,
                                               coney::GameRandom::kTableAddress, coney::GameRandom::kTableSize)) {
            table = std::move(*words);
            options.randomTable = table;
        }
        const auto print = [this](std::string_view line) { log.emplace_back(line); };
        scripts = std::make_unique<coney::LevelScripts>(coney::script::wadScriptSource(wad), "level80", checkpoint,
                                                        print, options);
        coney::GameplayMode::LevelLoader inner = coney::test::playLoader(renderer, wad, budget, print);
        coney::GameplayMode::LevelLoader loader =
            [this,
             inner](const coney::LevelStart& start,
                    const coney::ScriptedCast& cast) -> std::expected<std::unique_ptr<coney::GameMode>, coney::Error> {
            auto mode = inner(start, cast);
            if (mode) {
                if (auto* play = dynamic_cast<coney::platform::PlayLevelMode*>(mode->get())) {
                    play->useHud(scripts->hud());
                }
            }
            return mode;
        };
        gameplay = std::make_unique<coney::GameplayMode>(renderer, scripts->scripts(), scripts->context(),
                                                         scripts->state(), scripts->humans(), scripts->flags(),
                                                         scripts->recorded(), std::move(loader), print);
        gameplay->setLevel("level80");
        // The scenes play from the disc, so a skip ends them as in play.
        auto sceneList = coney::scenes::loadSceneList(wad);
        REQUIRE(sceneList.has_value());
        gameplay->setSceneMaker([&wad, list = std::move(*sceneList)] {
            return std::make_unique<coney::scenes::SceneSystem>(list, coney::scenes::wadSceneSource(wad),
                                                                coney::scenes::SceneSystem::ScriptCall{});
        });
        stack.setInput(&pad);
        stack.push(*gameplay);
        timer.setFixedStep(true);
    }

    // Steps `frames` frames.
    void run(std::uint64_t frames) { stack.runUntilEmpty(timer, {}, frames); }

    // The play mode once the level is loaded.
    [[nodiscard]] coney::platform::PlayLevelMode* play() const {
        return dynamic_cast<coney::platform::PlayLevelMode*>(gameplay->level());
    }

    // Steps until `done` holds, at most `frames` frames; returns whether it held.
    template <typename Done> bool runUntil(Done done, int frames) {
        for (int i = 0; i < frames; ++i) {
            if (done()) {
                return true;
            }
            run(1);
        }
        return done();
    }

    // Whether a logged line starts with `prefix`.
    [[nodiscard]] bool logged(std::string_view prefix) const {
        return std::ranges::any_of(log, [prefix](const std::string& line) { return line.starts_with(prefix); });
    }

    // The scripts' state's checkpoint.
    [[nodiscard]] double checkPoint() const { return scripts->state().checkPoint; }
};

// Skips any scene by tapping cross every second until player 1 has the pad, at most `frames` frames.
bool skipToPad(Level80& level, int frames) {
    constexpr int kTapEvery = 30;
    for (int i = 0; i < frames; i += kTapEvery) {
        if (i > 0 && level.play() != nullptr && level.play()->player().padControlled()) {
            return true;
        }
        level.pad.buttons = coney::pad::kCross;
        level.run(1);
        level.pad.buttons = 0;
        level.run(kTapEvery - 1);
    }
    return false;
}

} // namespace

TEST_CASE("the disc's level80: R2 and the right stick up give the follow order the script waits for",
          "[disc][story][commands]") {
    std::optional<coney::io::Wad> wad = coney::test::openDisc();
    if (!wad) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    auto engine = coney::platform::RenderEngine::start(coney::platform::RenderBackend::Null, {});
    REQUIRE(engine.has_value());
    Level80 level(**engine, *wad, 1);

    // The intro, skipped, until the script asks for Let's Go and waits for it (WCSetCallback).
    constexpr int kWait = 30 * 60;
    int frame = 0;
    REQUIRE(level.runUntil(
        [&level, &frame] {
            if (level.scripts->state().story.commandCallback.empty()) {
                constexpr int kTapEvery = 30;
                level.pad.buttons = (++frame % kTapEvery == 0) ? coney::pad::kCross : 0;
                return false;
            }
            return true;
        },
        kWait));
    level.pad.buttons = 0;
    CHECK(level.scripts->state().story.commandCallback == "CommandIssued");
    coney::hud::WarCommandDisplay& display = level.scripts->hud().warCommands(0);

    // R2 held opens the menu; the right stick pushed up at 95 % picks slot 0, follow; R2's release gives it.
    level.pad.buttons = coney::pad::kR2;
    level.run(2);
    CHECK(display.shown());
    level.pad.rightY = 0.95F;
    level.run(6);
    CHECK(display.highlight() == 0);
    level.pad.buttons = 0;
    level.run(1);
    level.pad.rightY = 0.0F;
    level.run(5);

    // The dispatcher took it: player 1's last command is follow and the script's callback ran and cleared itself.
    CHECK(display.issued());
    CHECK(level.scripts->state().characters.lastWarriorCommand.at(0) == 0);
    CHECK(level.scripts->state().story.commandCallback.empty());
    CHECK(level.scripts->scripts().errors() == 0);
    std::printf("  level80 commands: follow given, callback %s\n",
                level.scripts->state().story.commandCallback.empty() ? "ran" : "pending");
}

TEST_CASE("the disc's level80: the cuffed Warriors offer the uncuff prompt and the mash frees both",
          "[disc][story][uncuff]") {
    std::optional<coney::io::Wad> wad = coney::test::openDisc();
    if (!wad) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    auto engine = coney::platform::RenderEngine::start(coney::platform::RenderBackend::Null, {});
    REQUIRE(engine.has_value());
    Level80 level(**engine, *wad, 2);
    constexpr int kWait = 30 * 60;
    REQUIRE(
        level.runUntil([&level] { return level.play() != nullptr && level.play()->player().padControlled(); }, kWait));

    // Player 1 put inside the volume box the script's StartSnowCowboy listens on, then at the goto marker it shows
    // (ObjectiveEnter), then the scene skipped: Snow and Cowboy are cuffed at their flags.
    const coney::world_objects::WorldFlags& flags = level.scripts->flags();
    constexpr double kSnowFlag = 20;
    constexpr double kCowboyFlag = 21;
    const std::array<float, 3> sceneBox{-191.0F, 213.0F, 1.3F};
    const std::array<float, 3> marker{-195.8F, 226.6F, 1.3F};
    constexpr std::uint64_t kSettle = 60;
    level.play()->teleportPlayer(coney::world_objects::Placement{.position = sceneBox, .headingDegrees = 0.0F});
    level.run(kSettle);
    level.play()->teleportPlayer(coney::world_objects::Placement{.position = marker, .headingDegrees = 0.0F});
    int frame = 0;
    const bool arrested = level.runUntil(
        [&level, &frame] {
            constexpr int kTapEvery = 30;
            level.pad.buttons = (++frame % kTapEvery == 0) ? coney::pad::kCross : 0;
            return level.logged("uncuff: human 231 arrested");
        },
        kWait);
    if (!arrested) {
        const coney::anim::Vec3 at = level.play()->player().human().position();
        UNSCOPED_INFO("player at " << at.x << " " << at.y << " " << at.z);
        for (const std::string& line : level.log) {
            UNSCOPED_INFO(line);
        }
    }
    REQUIRE(arrested);
    level.pad.buttons = 0;
    REQUIRE(skipToPad(level, kWait));
    const std::string uncuff(level.scripts->context().strings->get(2));
    REQUIRE_FALSE(uncuff.empty());

    // Each in turn: player 1 next to him, the prompt, triangle, the mash of L1 and R1 a few frames each.
    int freed = 0;
    for (const auto& [flag, offset] : {std::pair{kSnowFlag, std::array<float, 2>{-1.1F, -0.15F}},
                                       std::pair{kCowboyFlag, std::array<float, 2>{0.55F, -0.9F}}}) {
        const coney::world_objects::WorldFlag* at = flags.find(flag);
        REQUIRE(at != nullptr);
        std::array<float, 3> place = at->position;
        place[0] += offset[0];
        place[1] += offset[1];
        level.play()->teleportPlayer(coney::world_objects::Placement{.position = place, .headingDegrees = 0.0F});
        level.run(3);
        CHECK(level.scripts->hud().actionPrompt(0) == uncuff);
        level.pad.buttons = coney::pad::kTriangle;
        level.run(1);
        level.pad.buttons = 0;
        level.run(1);
        CHECK(level.play()->player().human().animator().animId() == 325);
        CHECK(level.scripts->hud().actionPrompt(0).empty());
        const std::size_t before = level.log.size();
        constexpr int kMashRounds = 60;
        constexpr std::uint64_t kHold = 3;
        for (int round = 0; round < kMashRounds; ++round) {
            level.pad.buttons = (round % 2 == 0) ? coney::pad::kL1 : coney::pad::kR1;
            level.run(kHold);
            if (std::ranges::any_of(level.log.begin() + static_cast<std::ptrdiff_t>(before), level.log.end(),
                                    [](const std::string& line) { return line.starts_with("uncuff: freed"); })) {
                ++freed;
                break;
            }
        }
        level.pad.buttons = 0;
        level.run(10);
        CHECK(level.play()->player().human().animator().animId() == 332);
        level.run(60);
    }
    CHECK(freed == 2);
    // Both freed: the script's event-17 handlers ran and the mission moved on to checkpoint 3.
    REQUIRE(level.runUntil([&level] { return level.checkPoint() >= 3; }, kWait));
    CHECK(level.scripts->scripts().errors() == 0);
    std::printf("  level80 uncuff: %d freed, checkpoint %g\n", freed, level.checkPoint());
}
