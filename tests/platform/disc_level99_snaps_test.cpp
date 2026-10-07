// SPDX-License-Identifier: GPL-3.0-or-later

// Checks against the player's own disc that lesson 7 of `level99`'s combat tutorial, the snaps, can be passed: the
// level plays as `--play-level level99 --checkpoint 1` plays it, headless, driven by an input script through lessons
// 1-6; once the lesson arms its callback the second wave stands round the player (its four follow slots 1 m around
// him, docs/research/ai.md#level99-snaps), three snaps (square with the stick at full deflection to a bum's side one
// update before, from rest) land and score, `P1.SnapsDone` runs and lesson 8 arms its own callback
// (docs/research/scripting.md#level99-lessons). It runs only when the environment variable CONEY_DISC names the disc
// and skips otherwise; it prints counts only (LEGAL.md).

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <expected>
#include <format>
#include <memory>
#include <numbers>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <SDL3/SDL_stdinc.h>
#include <catch2/catch_test_macros.hpp>

#include "ai/ai_config.h"
#include "ai/ai_humans.h"
#include "ai/brain.h"
#include "ai/gangs.h"
#include "characters/character_types.h"
#include "combat/anim_ids.h"
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
#include "hud/hud.h"
#include "human/human.h"
#include "human/locomotion.h"
#include "human/player.h"
#include "platform/play_level_mode.h"
#include "platform/render_engine.h"
#include "scenes/scene_disc.h"
#include "scenes/scene_list.h"
#include "scenes/scene_player.h"
#include "scripting/config_strings.h"
#include "scripting/lua_value.h"
#include "scripting/lua_vm.h"
#include "scripting/script_system.h"
#include "world/sector_budget.h"

namespace {

namespace id = coney::combat::anim_id;

// Lessons 1-6 of checkpoint 1, frame by frame from the level's start: the intro skipped, the walk to the sparring
// ground, then each lesson's moves at partial stick deflections (mission 1's play-through script).
constexpr std::string_view kLessonsScript =
    "0 stick left 0 0\n100 tap cross\n135 stick left 19 67\n150 stick left 18 68\n153 stick left 17 68\n"
    "159 stick left 16 68\n165 stick left 15 68\n171 stick left 14 69\n180 stick left 13 69\n"
    "186 stick left 12 69\n195 stick left 9 69\n198 stick left 4 70\n201 stick left -2 70\n"
    "204 stick left -8 70\n207 stick left -10 69\n210 stick left -12 69\n225 stick left -11 69\n"
    "240 stick left -10 69\n255 stick left -9 69\n270 stick left -5 42\n294 stick left -4 42\n"
    "297 stick left 0 0\n317 stick left 9 -69\n329 stick left 10 -69\n341 stick left 11 -69\n"
    "347 stick left 12 -69\n356 stick left 13 -69\n362 stick left 14 -69\n368 stick left 15 -68\n"
    "374 stick left 16 -68\n380 stick left 17 -68\n383 stick left 18 -68\n389 stick left 19 -67\n"
    "395 stick left 20 -67\n398 stick left 21 -67\n401 stick left 22 -67\n404 stick left 22 -66\n"
    "407 stick left 23 -66\n410 stick left 14 -39\n413 stick left 15 -39\n419 stick left 16 -39\n"
    "422 stick left 17 -39\n425 stick left 17 -38\n428 stick left 20 -37\n431 stick left 0 0\n"
    "516 tap cross\n706 stick left 10 39\n712 stick left 11 39\n724 stick left 0 0\n724 tap square\n"
    "730 tap cross\n736 tap cross\n742 tap cross\n748 tap cross\n754 tap cross\n760 tap cross\n"
    "766 tap circle\n772 tap circle\n778 tap circle\n784 tap circle\n790 tap circle\n796 tap circle\n"
    "802 tap circle\n808 tap circle\n814 tap circle\n820 tap circle\n826 tap square\n832 tap cross\n"
    "838 tap cross\n844 tap cross\n850 tap cross\n856 press circle\n870 release circle\n"
    "966 press circle\n980 release circle\n1076 tap square\n1082 tap cross\n1088 tap cross\n"
    "1094 tap cross\n1100 tap cross\n1106 tap cross\n1112 tap cross\n1118 tap cross\n1124 tap cross\n"
    "1130 tap cross\n1136 tap cross\n1142 tap cross\n1148 tap l2\n1154 tap l2\n1160 tap l2\n1166 tap l2\n"
    "1172 tap l2\n1178 tap l2\n1184 press l1\n1259 release l1\n1355 press l1\n1430 release l1\n"
    "1526 tap square\n1536 tap square\n1592 tap square\n1602 tap square\n1612 tap square\n"
    "1668 stick left 29 28\n1674 stick left 0 0\n1674 tap square\n1684 tap cross\n1740 tap cross\n"
    "1750 tap cross\n1806 tap square\n1816 tap square\n1826 tap cross\n1882 stick left 4 40\n"
    "1888 stick left 0 0\n1888 tap square cross\n1894 stick left 9 39\n1900 stick left 12 38\n"
    "1912 stick left 10 39\n1918 stick left 8 39\n1942 stick left 7 39\n1954 stick left 6 39\n"
    "1960 stick left 6 40\n1972 stick left 0 0\n1972 tap cross circle\n1978 tap cross circle\n"
    "1984 tap cross circle\n1990 tap cross circle\n1996 tap cross circle\n2002 tap cross circle\n"
    "2008 tap cross circle\n2014 tap square cross\n2020 tap square cross\n2048 tap cross\n"
    "2076 tap cross\n2150 tap circle\n2156 tap circle\n2162 tap circle\n2168 tap circle\n"
    "2174 tap circle\n2180 tap square cross\n2208 tap cross\n2236 tap cross\n2310 tap square cross\n"
    "2338 tap cross\n2366 tap cross\n";

// The frame the lesson's moves end on: lesson 7's scene starts after it.
constexpr std::uint64_t kLessonsEnd = 2366;
// How long to wait for lesson 7's callback after that, and for lesson 8's after the third snap, in frames.
constexpr std::uint64_t kArmWait = 3000;
constexpr std::uint64_t kNextWait = 900;
// The second wave settles round the player within this distance, metres (1.0-1.4 m in the original).
constexpr float kSettled = 1.5F;

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

// The pad: the lessons' script, then the scripts the test adds once it sees where the bums stand, each from the
// frame it was added on.
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

// One bum of the second wave as the test sees it: where it stands from the player.
struct Bum {
    const coney::human::Human* human = nullptr;
    float distance = 0.0F; ///< Metres, in plan.
    float bearing = 0.0F;  ///< Radians from the player's facing, positive to the left.
};

// The standing members of the gang named `gang` with health left, nearest first.
std::vector<Bum> bumsOf(const coney::platform::PlayLevelMode& play, std::string_view gang) {
    const coney::human::Human& player = play.player().human();
    std::vector<Bum> bums;
    const coney::ai::Brains& brains = play.fighters().brains();
    for (std::size_t i = 0; i < brains.size(); ++i) {
        const coney::ai::Brain& brain = brains.at(i);
        if (brain.gang() == nullptr || brain.gang()->name() != gang || brain.human().health().depleted()) {
            continue;
        }
        const coney::anim::Vec3 to = coney::anim::subtract(brain.human().position(), player.position());
        bums.push_back(Bum{.human = &brain.human(),
                           .distance = std::hypot(to.x, to.y),
                           .bearing = coney::human::wrapAngle(coney::human::headingOf(to) - player.heading())});
    }
    std::ranges::sort(bums, {}, &Bum::distance);
    return bums;
}

// The pad's left stick, in percent, that points the player at `bum` in the world: the stick is read against the
// camera's forward, x to its right.
std::pair<int, int> stickTowards(const coney::platform::PlayLevelMode& play, const Bum& bum) {
    const coney::anim::Vec3 to = coney::anim::subtract(bum.human->position(), play.player().human().position());
    coney::anim::Vec3 forward = coney::anim::subtract(play.cameraTarget(), play.cameraEye());
    const float length = std::hypot(forward.x, forward.y);
    forward = {forward.x / length, forward.y / length, 0.0F};
    const coney::anim::Vec3 right{forward.y, -forward.x, 0.0F};
    const float along = std::hypot(to.x, to.y);
    const float x = ((to.x * right.x) + (to.y * right.y)) / along;
    const float y = ((to.x * forward.x) + (to.y * forward.y)) / along;
    return {static_cast<int>(std::lround(x * 100.0F)), static_cast<int>(std::lround(y * 100.0F))};
}

// Whether `animId` is one of the snaps lesson 7 counts.
bool isSnap(int animId) { return animId == id::kSnapRight || animId == id::kSnapLeft || animId == id::kSnapBack; }

} // namespace

TEST_CASE("the disc's level99 lesson 7: the second wave surrounds the player and three snaps pass it",
          "[disc][story][combat]") {
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
    gameplay.setSceneMaker([&wad, &sceneList] {
        return std::make_unique<coney::scenes::SceneSystem>(*sceneList, coney::scenes::wadSceneSource(*wad),
                                                            coney::scenes::SceneSystem::ScriptCall{});
    });

    auto lessons = coney::parseInputScript(kLessonsScript);
    REQUIRE(lessons.has_value());
    LivePad pad(std::move(*lessons));
    coney::GameModeStack stack;
    stack.setInput(&pad);
    stack.push(gameplay);
    coney::GameTimer timer;
    timer.setFixedStep(true);
    coney::hud::Hud& hud = scripts.hud();
    // One frame; the player's strikes that frame go into `snaps` and `others`.
    int snaps = 0;
    int others = 0;
    const auto step = [&] {
        stack.runUntilEmpty(timer, {}, 1);
        if (const auto* play = dynamic_cast<const coney::platform::PlayLevelMode*>(gameplay.level())) {
            for (const int strike : play->player().human().fighter().strikes()) {
                ++(isSnap(strike) ? snaps : others);
            }
        }
    };

    // 1. Lessons 1-6, then lesson 7's scene and set-up, until P1.StartSnaps arms the snaps' callback.
    while (pad.nextFrame() < kLessonsEnd + kArmWait && hud.tutorialCallback() != "P1.Snaps") {
        step();
    }
    const auto* play = dynamic_cast<const coney::platform::PlayLevelMode*>(gameplay.level());
    if (play == nullptr || hud.tutorialCallback() != "P1.Snaps") {
        for (const std::string& line : log) {
            UNSCOPED_INFO(line);
        }
    }
    REQUIRE(play != nullptr);
    REQUIRE(hud.tutorialCallback() == "P1.Snaps");
    const std::uint64_t armedAt = pad.nextFrame();
    snaps = 0;
    others = 0;

    // P1.SnapsDone, counted as it runs.
    coney::script::LuaVm& vm = scripts.scripts().vm();
    const coney::script::Value p1 = vm.global("P1");
    REQUIRE(p1.table() != nullptr);
    const coney::script::Value done = p1.table()->field("SnapsDone");
    int doneCalls = 0;
    auto counted = std::make_shared<coney::script::Function>();
    counted->native = [&vm, &doneCalls, done](std::span<const coney::script::Value> args) {
        ++doneCalls;
        return vm.call(done, args);
    };
    REQUIRE(p1.table()
                ->set(coney::script::Value("SnapsDone"),
                      coney::script::Value(std::shared_ptr<const coney::script::Function>(counted)))
                .has_value());

    // 2. Two seconds to settle: the four bums stand round the player, within kSettled of him.
    for (int i = 0; i < 60; ++i) {
        step();
    }
    const std::vector<Bum> around = bumsOf(*play, "CombatEnemy2");
    const auto near = std::ranges::count_if(around, [](const Bum& bum) { return bum.distance <= kSettled; });
    std::string where;
    for (const Bum& bum : around) {
        where +=
            std::format(" {:.2f} m at {:.0f} deg;", bum.distance, bum.bearing * 180.0F / std::numbers::pi_v<float>);
    }
    std::printf("  level99 lesson 7 armed at frame %llu: %zu bums,%s\n", static_cast<unsigned long long>(armedAt),
                around.size(), where.c_str());
    CHECK(around.size() == 4);
    CHECK(near == 4);

    // 3. Snaps: each time the stick at full deflection towards a bum off the player's front, from rest, one update
    // before square, then at rest again; the next one once the snap has played out.
    int tries = 0;
    while (doneCalls == 0 && tries < 8) {
        ++tries;
        const std::vector<Bum> bums = bumsOf(*play, "CombatEnemy2");
        const coney::human::Combatant* current = play->player().human().fighter().target();
        const auto pick = std::ranges::find_if(bums, [current](const Bum& bum) {
            return std::fabs(bum.bearing) > std::numbers::pi_v<float> / 3.0F && bum.distance <= 2.0F &&
                   static_cast<const void*>(bum.human) != static_cast<const void*>(current);
        });
        if (pick == bums.end()) {
            for (int i = 0; i < 30; ++i) {
                step();
            }
            continue;
        }
        const auto [x, y] = stickTowards(*play, *pick);
        const std::uint64_t at = pad.nextFrame();
        pad.play(std::format("{} stick left {} {}\n{} tap square\n{} stick left 0 0\n", at, x, y, at + 1, at + 2));
        for (int i = 0; i < 45; ++i) {
            step();
        }
    }
    std::printf("  level99 lesson 7: %d tries, %d snaps landed, %d other strikes, P1.SnapsDone ran %d time(s)\n", tries,
                snaps, others, doneCalls);
    CHECK(snaps >= 3);
    CHECK(doneCalls == 1);

    // 4. Lesson 8 (throws) arms its own callback.
    const std::uint64_t doneAt = pad.nextFrame();
    while (pad.nextFrame() < doneAt + kNextWait &&
           (hud.tutorialCallback() == "P1.Snaps" || hud.tutorialCallback().empty())) {
        step();
    }
    CHECK(hud.tutorialCallback() != "P1.Snaps");
    CHECK_FALSE(hud.tutorialCallback().empty());
    CHECK(scripts.scripts().errors() == 0);
    std::printf("  level99 lesson 8 armed %llu frames after lesson 7 ended\n",
                static_cast<unsigned long long>(pad.nextFrame() - doneAt));
}
