// SPDX-License-Identifier: GPL-3.0-or-later

// Checks against the player's own disc that a grab held when one of `level99`'s tutorial scenes starts leaves no human
// stuck in it: the level plays as `--play-level level99 --checkpoint 1` plays it, headless, driven through lessons
// 1-6 by the shared input script (lesson 7's scene is not skipped); then the player grabs a bum (circle tapped whenever
// he holds no one) until lesson 7's scene `l99_c7` takes him over, and once the scene has ended and given him back no
// human is grabbing, holding, held or mounted (docs/research/scenes.md#starting, docs/research/combat.md#grabbing). It
// runs only when the environment variable CONEY_DISC names the disc and skips otherwise; it prints counts only
// (LEGAL.md).

#include <array>
#include <cstdint>
#include <cstdio>
#include <expected>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <SDL3/SDL_stdinc.h>
#include <catch2/catch_test_macros.hpp>

#include "ai/ai_config.h"
#include "ai/ai_humans.h"
#include "characters/character_types.h"
#include "core/error.h"
#include "core/game_random.h"
#include "core/game_timer.h"
#include "core/input_script.h"
#include "core/pads.h"
#include "fileio/disc.h"
#include "fileio/executable.h"
#include "fileio/wad.h"
#include "gamemodes/game_mode_stack.h"
#include "gamemodes/gameplay_mode.h"
#include "gamemodes/level_start.h"
#include "human/human.h"
#include "human/player.h"
#include "platform/play_level_mode.h"
#include "platform/render_engine.h"
#include "scenes/scene_disc.h"
#include "scenes/scene_list.h"
#include "scenes/scene_player.h"
#include "scripting/config_strings.h"
#include "scripting/script_system.h"
#include "world/sector_budget.h"

#include "support/level99_lessons.h"

namespace {

using coney::test::kLessonsScript;

// The frame lesson 6 is passed by (its power move's extension lands about 2,210); lesson 7's scene `l99_c7` starts
// about 80 frames later, and the shared script's later lines (the skip at kLessonsEnd) are not played.
constexpr std::uint64_t kLessonSixDone = 2240;
// How long to wait for lesson 7's scene after lesson 6, and for it to end, in frames.
constexpr std::uint64_t kSceneWait = 3000;
constexpr std::uint64_t kSceneLength = 3000;
// Frames after the scene gives the player back before the humans are counted (the let-go clips play out).
constexpr int kSettle = 90;

// The disc named by CONEY_DISC, opened; nothing when it is not set.
std::optional<coney::io::Wad> openDisc() {
    const char* discPath = SDL_getenv("CONEY_DISC");
    if (discPath == nullptr || *discPath == '\0') {
        return std::nullopt;
    }
    auto disc = coney::io::Disc::open(discPath);
    REQUIRE(disc.has_value());
    auto wad = coney::io::Wad::open(std::move(*disc));
    REQUIRE(wad.has_value());
    return wad ? std::optional<coney::io::Wad>(std::move(*wad)) : std::nullopt;
}

// The pad: the lessons' script, then the lines the test adds as it goes, each from the frame it was added on.
class LivePad final : public coney::InputSource {
  public:
    explicit LivePad(std::vector<coney::InputEvent> events)
        : m_current(std::make_unique<coney::ScriptedInput>(std::move(events))) {}

    // From the next frame on, `text` plays (its frames are absolute).
    void play(std::string_view text) {
        auto events = coney::parseInputScript(text);
        REQUIRE(events.has_value());
        m_current = std::make_unique<coney::ScriptedInput>(std::move(*events));
    }

    // The next frame the loop will ask for.
    [[nodiscard]] std::uint64_t nextFrame() const { return m_next; }

    coney::PortSamples sample(std::uint64_t frame) override {
        m_next = frame + 1;
        return m_current->sample(frame);
    }

  private:
    std::unique_ptr<coney::ScriptedInput> m_current;
    std::uint64_t m_next = 0;
};

// The humans in a grab, counted by their side of it.
struct GrabCounts {
    int holding = 0; ///< Holding a victim (a grab, a tackle's mount, a mugging).
    int held = 0;    ///< Held or mounted by a grabber that drives them.
    int grabbed = 0; ///< Held in another human's grab on the victim side (the player's).

    [[nodiscard]] int total() const { return holding + held + grabbed; }
};

// Adds `human`'s part in a grab to `counts`.
void count(const coney::human::Human& human, GrabCounts& counts) {
    const coney::human::Fighter& fighter = human.fighter();
    counts.holding += fighter.held() != nullptr ? 1 : 0;
    counts.held += fighter.holdState().has_value() ? 1 : 0;
    counts.grabbed += fighter.grabbed() ? 1 : 0;
}

// The player and every AI human still in the level, counted.
GrabCounts grabCounts(const coney::platform::PlayLevelMode& play) {
    GrabCounts counts;
    count(play.player().human(), counts);
    for (const coney::ai::AiHuman& fighter : play.fighters().humans()) {
        if (!fighter.removed && fighter.human != nullptr) {
            count(*fighter.human, counts);
        }
    }
    return counts;
}

} // namespace

TEST_CASE("the disc's level99: a grab held as lesson 7's scene starts leaves no human stuck in it",
          "[disc][story][combat][scenes]") {
    std::optional<coney::io::Wad> wad = openDisc();
    if (!wad) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    auto engine = coney::platform::RenderEngine::start(coney::platform::RenderBackend::Null, {});
    REQUIRE(engine.has_value());
    coney::platform::RenderEngine& renderer = **engine;
    coney::world::SectorBudget budget(coney::world::kSectorPoolSize);
    std::vector<std::string> log;
    const auto print = [&log](std::string_view line) { log.emplace_back(line); };

    // The scripts as --play-level runs them, with the game's random table; the play mode draws the scripts' HUD.
    coney::LevelScriptOptions options;
    std::vector<std::uint32_t> table;
    if (auto words = coney::io::readExecutableWords(wad->disc(), coney::GameRandom::kExecutableName,
                                                    coney::GameRandom::kTableAddress, coney::GameRandom::kTableSize)) {
        table = std::move(*words);
        options.randomTable = table;
    }
    coney::LevelScripts scripts(coney::script::wadScriptSource(*wad), "level99", 1, print, options);
    coney::GameplayMode::LevelLoader loader =
        [&renderer, &wad, &budget, &scripts,
         &print](const coney::LevelStart& start,
                 const coney::ScriptedCast& cast) -> std::expected<std::unique_ptr<coney::GameMode>, coney::Error> {
        std::optional<coney::human::PlayerStart> playerStart;
        coney::platform::PlayerSetup setup;
        setup.ai = coney::ai::aiConfigFrom(scripts.recorded());
        setup.types = coney::characters::CharacterTypes::fromRecorded(scripts.recorded());
        if (start.player) {
            const coney::HumanCreation& player = *start.player;
            const std::array<float, 3> p = player.position.value_or(std::array<float, 3>{});
            playerStart = coney::human::PlayerStart{.position = coney::anim::Vec3{p[0], p[1], p[2]},
                                                    .headingDegrees = player.headingDegrees};
            setup.model = player.model.empty() ? std::string(coney::human::kPlayerModel) : player.model;
            setup.type = player.type;
        }
        auto mode = coney::platform::PlayLevelMode::create(renderer, *wad, start.level, budget, print, playerStart,
                                                           setup, &cast);
        if (!mode) {
            return std::unexpected(std::move(mode.error()));
        }
        (*mode)->useHud(scripts.hud());
        return std::unique_ptr<coney::GameMode>(std::move(*mode));
    };
    auto sceneList = coney::scenes::loadSceneList(*wad);
    REQUIRE(sceneList.has_value());
    coney::GameplayMode gameplay(renderer, scripts.scripts(), scripts.context(), scripts.state(), scripts.humans(),
                                 scripts.flags(), scripts.recorded(), std::move(loader), print);
    gameplay.setLevel("level99");
    // The level's scene system, kept to see when a cinematic plays.
    coney::scenes::SceneSystem* sceneSystem = nullptr;
    gameplay.setSceneMaker([&wad, &sceneList, &sceneSystem] {
        auto made = std::make_unique<coney::scenes::SceneSystem>(*sceneList, coney::scenes::wadSceneSource(*wad),
                                                                 coney::scenes::SceneSystem::ScriptCall{});
        sceneSystem = made.get();
        return made;
    });

    auto lessons = coney::parseInputScript(kLessonsScript);
    REQUIRE(lessons.has_value());
    LivePad pad(std::move(*lessons));
    coney::GameModeStack stack;
    stack.setInput(&pad);
    stack.push(gameplay);
    coney::GameTimer timer;
    timer.setFixedStep(true);
    const auto play = [&gameplay] { return dynamic_cast<const coney::platform::PlayLevelMode*>(gameplay.level()); };
    const auto step = [&] { stack.runUntilEmpty(timer, {}, 1); };

    // 1. Lessons 1-6.
    while (pad.nextFrame() < kLessonSixDone) {
        step();
    }
    REQUIRE(play() != nullptr);

    // 2. Until lesson 7's cinematic starts: circle tapped whenever the player holds no one, so a grab is under
    // way as the scene starts.
    GrabCounts atStart;
    bool started = false;
    std::uint64_t grabs = 0;
    while (pad.nextFrame() < kLessonSixDone + kSceneWait) {
        if (sceneSystem != nullptr && sceneSystem->cinematicActive()) {
            started = true;
            atStart = grabCounts(*play());
            break;
        }
        const std::uint64_t at = pad.nextFrame();
        if (play()->player().human().fighter().held() == nullptr && at % 10 == 0) {
            pad.play(std::format("{} tap circle\n", at));
            ++grabs;
        }
        step();
    }
    const std::uint64_t startedAt = pad.nextFrame();
    std::printf("  level99 lesson 7's scene started at frame %llu after %llu grabs: %d holding, %d held, %d grabbed\n",
                static_cast<unsigned long long>(startedAt), static_cast<unsigned long long>(grabs), atStart.holding,
                atStart.held, atStart.grabbed);
    REQUIRE(started);
    CHECK(atStart.holding + atStart.held > 0);

    // 3. The cinematic plays and ends; then the humans settle.
    while (pad.nextFrame() < startedAt + kSceneLength && sceneSystem->cinematicActive()) {
        step();
    }
    const std::uint64_t endedAt = pad.nextFrame();
    CHECK_FALSE(sceneSystem->cinematicActive());
    for (int i = 0; i < kSettle; ++i) {
        step();
    }
    const GrabCounts after = grabCounts(*play());
    std::printf("  level99 lesson 7's scene ended at frame %llu; %d frames later: %d holding, %d held, %d grabbed\n",
                static_cast<unsigned long long>(endedAt), kSettle, after.holding, after.held, after.grabbed);
    if (after.total() != 0) {
        for (const std::string& line : log) {
            UNSCOPED_INFO(line);
        }
    }
    CHECK(after.total() == 0);
    CHECK(scripts.scripts().errors() == 0);
}
