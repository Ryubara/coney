// SPDX-License-Identifier: GPL-3.0-or-later

// Checks against the player's own disc that the second story mission's player-side mechanics play
// (docs/missions/level80.md): at checkpoint 1 the Warrior command menu, held open with R2 and steered up with the
// right stick, gives the follow order the script waits for (`WCSetCallback("CommandIssued")`,
// docs/research/hud.md#warrior-command-menu); at checkpoint 2 the two cuffed Warriors offer the uncuff prompt and the
// L1-R1 mash frees each, which the script hears as event 17 and moves on to checkpoint 3
// (docs/research/crimes.md#uncuffing); at checkpoint 3 an adaptive pad playthrough of the throw lesson and the store's
// front door asserts its hints, objectives and scenes in the original's order (docs/research/scripting.md#level80,
// docs/research/objects.md#throws). It runs only when the environment variable CONEY_DISC names the disc and skips
// otherwise; it prints counts only (LEGAL.md).

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "ai/brain.h"
#include "ai/brains.h"
#include "ai/swap_prompt.h"
#include "combat/throw_aim.h"
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
#include "hud/hint_box.h"
#include "hud/hud.h"
#include "hud/messages.h"
#include "human/human.h"
#include "human/locomotion.h"
#include "human/player.h"
#include "human/script_state.h"
#include "platform/play_level_mode.h"
#include "platform/render_engine.h"
#include "scenes/scene_cache.h"
#include "scenes/scene_disc.h"
#include "scenes/scene_list.h"
#include "scenes/scene_player.h"
#include "scripting/config_strings.h"
#include "scripting/lua_value.h"
#include "scripting/lua_vm.h"
#include "scripting/script_bindings.h"
#include "scripting/script_system.h"
#include "warriors/inventory.h"
#include "world/sector_budget.h"
#include "world_objects/flags.h"

#include "support/disc_play_fixtures.h"

namespace {

// A pad the test drives frame by frame: the buttons held and the two analog sticks in [-1, 1] with y up.
class DrivenPad final : public coney::InputSource {
  public:
    std::uint16_t buttons = 0;
    float rightX = 0.0F;
    float rightY = 0.0F;
    float leftX = 0.0F;
    float leftY = 0.0F;

    coney::PortSamples sample(std::uint64_t /*frame*/) override {
        coney::PortSamples samples{};
        coney::PadSample& pad = samples[0];
        pad.connected = true;
        pad.buttons = buttons;
        pad.sticks[0] = raw(rightX);
        pad.sticks[1] = raw(-rightY);
        pad.sticks[2] = raw(leftX);
        pad.sticks[3] = raw(-leftY);
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

    // Called after every frame run() steps (empty: nothing).
    std::function<void()> watch;

    // Steps `frames` frames, one at a time so that watch sees each.
    void run(std::uint64_t frames) {
        for (std::uint64_t i = 0; i < frames; ++i) {
            stack.runUntilEmpty(timer, {}, 1);
            if (watch) {
                watch();
            }
        }
    }

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
    // A local, not written inside REQUIRE: clang-tidy reads the whole lambda as part of REQUIRE's condition.
    const auto tapUntilAsked = [&level, &frame] {
        if (level.scripts->state().story.commandCallback.empty()) {
            constexpr int kTapEvery = 30;
            ++frame;
            level.pad.buttons = (frame % kTapEvery == 0) ? coney::pad::kCross : 0;
            return false;
        }
        return true;
    };
    REQUIRE(level.runUntil(tapUntilAsked, kWait));
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

    // The script then stages the street (GangBrFlush(0), the Warriors walked to their flags) and, at EndStartCam,
    // adds the crew to gang 0 again, which gives the Warriors the follow back (docs/research/ai.md#warrior-follow).
    constexpr int kStaging = 15 * 30;
    level.run(kStaging);
    int following = 0;
    const auto& brains = level.play()->fighters().brains();
    for (std::size_t b = 0; b < brains.size(); ++b) {
        const coney::ai::Brain& brain = brains.at(b);
        if (brain.type() != coney::ai::BrainType::Player && brain.following() != nullptr) {
            ++following;
        }
    }
    CHECK(following == 2);

    // Opened again, the menu gives its order when play resumes under a closing pause menu, R2 still held.
    level.pad.buttons = coney::pad::kR2;
    level.run(2);
    REQUIRE(display.shown());
    CHECK_FALSE(display.issued());
    level.gameplay->resume();
    CHECK(display.issued());
    level.pad.buttons = 0;
    level.run(60);
    // Opened again, it closes at once when the chief is knocked out.
    level.pad.buttons = coney::pad::kR2;
    level.run(2);
    REQUIRE(display.shown());
    REQUIRE(level.gameplay->brains() != nullptr);
    // The test knocks player 1 out, so it writes to a brain gameplay owns.
    auto& allBrains = const_cast<coney::ai::Brains&>(*level.gameplay->brains());
    coney::ai::Brain* chief = nullptr;
    for (std::size_t i = 0; i < allBrains.size() && chief == nullptr; ++i) {
        if (allBrains.at(i).type() == coney::ai::BrainType::Player) {
            chief = &allBrains.at(i);
        }
    }
    REQUIRE(chief != nullptr);
    chief->human().script().knockedOut = true;
    level.run(1);
    CHECK_FALSE(display.shown());
    chief->human().script().knockedOut = false;
    level.pad.buttons = 0;
    std::printf("  level80 commands: follow given, callback %s, %d Warriors following after the staging\n",
                level.scripts->state().story.commandCallback.empty() ? "ran" : "pending", following);
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
            // Both cuffed (their handles depend on how many handles the scripts took before them).
            constexpr long kCuffed = 2;
            return std::ranges::count_if(level.log, [](const std::string& line) {
                       return line.starts_with("uncuff: human ") && line.contains(" arrested");
                   }) >= kCuffed;
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
    // Each cuffed Warrior wears the cuffs icon over his head (docs/research/crimes.md#arrest).
    const auto cuffsIcons = [&level] {
        int count = 0;
        if (const coney::ai::Brains* brains = level.gameplay->brains(); brains != nullptr) {
            for (std::size_t i = 0; i < brains->size(); ++i) {
                count += brains->at(i).human().script().icon == "dyn_cuffs" ? 1 : 0;
            }
        }
        return count;
    };
    CHECK(cuffsIcons() == 2);
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
    CHECK(cuffsIcons() == 0);
    // Both freed: the script's event-17 handlers ran and the mission moved on to checkpoint 3.
    REQUIRE(level.runUntil([&level] { return level.checkPoint() >= 3; }, kWait));
    CHECK(level.scripts->scripts().errors() == 0);
    std::printf("  level80 uncuff: %d freed, checkpoint %g\n", freed, level.checkPoint());

    // The talk prompt and the swap: with an object in player 1's hand (a stand-in handle: the test gives him one) a
    // freed Warrior in his gang, 1 m in front of him, offers the swap prompt, GSTRING.HUD 0xe, from his brain's think;
    // triangle hands it over; with both hands empty, nothing.
    REQUIRE(level.gameplay->brains() != nullptr);
    // The test gives player 1 the object, so it writes to a brain gameplay owns.
    auto& brains = const_cast<coney::ai::Brains&>(*level.gameplay->brains());
    coney::ai::Brain* warrior = nullptr;
    coney::ai::Brain* player = nullptr;
    for (std::size_t i = 0; i < brains.size(); ++i) {
        coney::ai::Brain& brain = brains.at(i);
        if (warrior == nullptr && brain.type() == coney::ai::BrainType::Warrior && brain.gang() != nullptr) {
            warrior = &brain;
        }
        if (player == nullptr && brain.type() == coney::ai::BrainType::Player) {
            player = &brain;
        }
    }
    REQUIRE(warrior != nullptr);
    REQUIRE(player != nullptr);
    const auto facingHim = [&level, warrior] {
        // Once he stands (the freed Warriors walk off at first), 1 m east of him, facing west (heading 90 degrees
        // faces -x), for a think or two.
        REQUIRE(level.runUntil([warrior] { return warrior->human().gait() == coney::human::Gait::Standing; }, kWait));
        const coney::anim::Vec3 at = warrior->human().position();
        level.play()->teleportPlayer(
            coney::world_objects::Placement{.position = {at.x + 1.0F, at.y, at.z}, .headingDegrees = 90.0F});
        level.run(6);
    };
    constexpr double kHeld = 9999.0;
    player->human().script().heldObject = kHeld;
    facingHim();
    CHECK(warrior->human().script().talkable);
    CHECK(level.scripts->hud().actionPrompt(0) == level.scripts->context().strings->get(coney::ai::kSwapPlayerHolds));
    // Triangle swaps: the object passes to him at once, and his prompt becomes "he holds" (0xd).
    level.pad.buttons = coney::pad::kTriangle;
    level.run(1);
    level.pad.buttons = 0;
    level.run(1);
    CHECK(level.logged("swap: with human"));
    CHECK(warrior->human().script().heldObject == kHeld);
    CHECK(player->human().script().heldObject == 0.0);
    facingHim();
    CHECK(level.scripts->hud().actionPrompt(0) == level.scripts->context().strings->get(coney::ai::kSwapWarriorHolds));
    // With neither holding anything, nothing.
    warrior->human().script().heldObject = 0.0;
    facingHim();
    CHECK_FALSE(warrior->human().script().talkable);
    CHECK(level.scripts->hud().actionPrompt(0).empty());
}

namespace {

// The hints and objectives checkpoint 3 shows, by their keys in the level's text (docs/research/scripting.md#level80):
// the reference order of the analyst's run, steps 3.1 to 3.4.
constexpr std::array<std::string_view, 7> kHintKeys{"TT_5", "TT_25", "TT_26", "TT_27", "TT_28", "TT_29", "TT_7"};
constexpr std::array<std::string_view, 5> kObjectiveKeys{"MS_C3_1", "MS_C3_1a", "MS_C3_2", "MS_C3_3", "MS_C3_4"};
constexpr std::array<std::string_view, 2> kSceneNames{"l80_c6", "l80_c6_b"};

// What a run showed, in order: each hint, objective and scene when it appears, by its key (a text the scripts did not
// name is skipped).
struct Shown {
    std::vector<std::string> order;
    std::string hint;
    std::string objective;
    std::vector<std::string> scenes;
    std::string hintText;      // the hint box's text last frame
    std::string objectiveText; // objective slot 0's text last frame

    // Records this frame's hint, objective line and scenes.
    void watch(Level80& level) {
        const coney::hud::Hud& hud = level.scripts->hud();
        coney::script::LuaVm& vm = level.scripts->scripts().vm();
        // The key among `keys` whose text the level's scripts hold, as a global or a field of a global table.
        const auto keyOf = [&vm](std::string_view text, std::span<const std::string_view> keys) -> std::string {
            const auto find = [text, keys](const coney::script::Table& table, const auto& self,
                                           int depth) -> std::string {
                coney::script::Value key;
                while (const auto entry = table.next(key)) {
                    key = entry->first;
                    const std::optional<std::string_view> name = entry->first.string();
                    if (const std::optional<std::string_view> value = entry->second.string();
                        name && value && *value == text && std::ranges::find(keys, *name) != keys.end()) {
                        return std::string(*name);
                    }
                    if (entry->second.type() == coney::script::Value::Type::Table && depth > 0) {
                        if (std::string found = self(*entry->second.table(), self, depth - 1); !found.empty()) {
                            return found;
                        }
                    }
                }
                return {};
            };
            return find(vm.globals(), find, 1);
        };
        // A text is looked up when it changes; the hint's key is cleared while none shows.
        const std::optional<coney::hud::Hint>& showing = hud.hints().showing();
        const std::string shownHint = showing ? showing->text : std::string();
        if (shownHint != hintText) {
            hintText = shownHint;
            hint = shownHint.empty() ? std::string() : keyOf(shownHint, kHintKeys);
            if (!hint.empty()) {
                order.push_back(hint);
            }
        }
        const std::optional<coney::hud::ChecklistLine>& line = hud.checklist().slots[0];
        if (const std::string shownLine = line ? line->text : std::string(); shownLine != objectiveText) {
            objectiveText = shownLine;
            if (std::string key = shownLine.empty() ? std::string() : keyOf(shownLine, kObjectiveKeys); !key.empty()) {
                objective = key;
                order.push_back(key);
            }
        }
        if (coney::scenes::SceneSystem* system = level.scripts->context().scenes; system != nullptr) {
            for (const std::string_view name : kSceneNames) {
                const std::optional<std::uint32_t> id = system->idOf(name);
                if (id && system->state(*id) == coney::scenes::SceneState::Playing &&
                    std::ranges::find(scenes, name) == scenes.end()) {
                    scenes.emplace_back(name);
                    order.emplace_back(name);
                }
            }
        }
    }
};

// The left stick, in [-1, 1] at `deflection`, that walks player 1 towards `to` under the camera shown: the stick is
// read against the camera's forward, x to its right.
std::pair<float, float> stickTowards(const coney::platform::PlayLevelMode& play, coney::anim::Vec3 to,
                                     float deflection) {
    const coney::anim::Vec3 way = coney::anim::subtract(to, play.player().human().position());
    coney::anim::Vec3 forward = coney::anim::subtract(play.cameraTarget(), play.cameraEye());
    const float length = std::hypot(forward.x, forward.y);
    const float along = std::hypot(way.x, way.y);
    if (length <= 0.0F || along <= 0.0F) {
        return {0.0F, 0.0F};
    }
    forward = {forward.x / length, forward.y / length, 0.0F};
    const coney::anim::Vec3 right{forward.y, -forward.x, 0.0F};
    return {deflection * ((way.x * right.x) + (way.y * right.y)) / along,
            deflection * ((way.x * forward.x) + (way.y * forward.y)) / along};
}

// The stick value in [-1, 1] that reads as `steps` past the throw aim's dead zone (sign kept), so the aim turns or
// pitches by steps × 0.000589 rad a frame.
float aimStick(int steps) {
    constexpr int kLess = 25;
    if (steps == 0) {
        return 0.0F;
    }
    const int offset = (steps > 0 ? 1 : -1) * (std::abs(steps) + kLess);
    return static_cast<float>(offset) / 127.5F;
}

} // namespace

TEST_CASE("the disc's level80 checkpoint 3: Fox, the Hold Up order, the bottle thrown from the aim, then the store",
          "[disc][story][throw]") {
    std::optional<coney::io::Wad> wad = coney::test::openDisc();
    if (!wad) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    auto engine = coney::platform::RenderEngine::start(coney::platform::RenderBackend::Null, {});
    REQUIRE(engine.has_value());
    Level80 level(**engine, *wad, 3);
    Shown shown;
    level.watch = [&level, &shown] { shown.watch(level); };
    constexpr int kWait = 30 * 60;
    const auto padded = [&level] { return level.play() != nullptr && level.play()->player().padControlled(); };
    const auto hinted = [&shown](std::string_view key) { return shown.hint == key; };
    // On a failure: where player 1 stands, what showed and the log.
    const auto dump = [&level, &shown] {
        const coney::anim::Vec3 at = level.play()->player().human().position();
        UNSCOPED_INFO("player at " << at.x << " " << at.y << " " << at.z);
        for (const std::string& key : shown.order) {
            UNSCOPED_INFO("shown " << key);
        }
        UNSCOPED_INFO("script errors " << level.scripts->scripts().errors());
        for (const auto& [name, count] : level.scripts->scripts().vm().nilCallsByName()) {
            UNSCOPED_INFO("nil call " << name << " x" << count);
        }
        for (const std::string& line : level.log) {
            UNSCOPED_INFO(line);
        }
    };
    REQUIRE(level.runUntil(padded, kWait));
    // A flag's place, by its name (the scripts' global holds its handle).
    const auto flagAt = [&level](std::string_view name) {
        const std::optional<double> handle = level.scripts->scripts().vm().global(std::string(name)).number();
        REQUIRE(handle.has_value());
        const coney::world_objects::WorldFlag* flag = level.scripts->flags().find(*handle);
        REQUIRE(flag != nullptr);
        return coney::anim::Vec3{flag->position[0], flag->position[1], flag->position[2]};
    };
    // Walks player 1 towards `to` with the left stick at 70 % until `done`, at most kWait frames.
    const auto walkTo = [&level](coney::anim::Vec3 to, const std::function<bool()>& done) {
        const bool reached = level.runUntil(
            [&] {
                if (done()) {
                    return true;
                }
                const auto [x, y] = stickTowards(*level.play(), to, 0.7F);
                level.pad.leftX = x;
                level.pad.leftY = y;
                return false;
            },
            kWait);
        level.pad.leftX = 0.0F;
        level.pad.leftY = 0.0F;
        return reached;
    };

    // 3.1: from Fox's gate (the analyst's run teleported to each objective too), walked into his box with the stick;
    // his scene plays and is skipped.
    const coney::anim::Vec3 fox = flagAt("fFoxTrigger");
    // The chapter's Setup fades in and places the crew first: the teleport waits for its objective.
    REQUIRE(level.runUntil([&shown] { return shown.objective == "MS_C3_1"; }, kWait));
    level.run(30);
    const coney::anim::Vec3 gate = flagAt("fFoxGate");
    level.play()->teleportPlayer(
        coney::world_objects::Placement{.position = {gate.x, gate.y, gate.z}, .headingDegrees = 0.0F});
    level.run(5);
    const bool atFox = walkTo(fox, [&level] { return !level.play()->player().padControlled(); });
    if (!atFox) {
        dump();
    }
    REQUIRE(atFox);
    REQUIRE(skipToPad(level, kWait));

    // 3.2: TT_5 asks for the Hold Up order: R2 held, the right stick down at 95 %, R2 let go.
    REQUIRE(level.runUntil([&] { return hinted("TT_5"); }, kWait));
    level.pad.buttons = coney::pad::kR2;
    level.run(2);
    level.pad.rightY = -0.95F;
    level.run(6);
    level.pad.buttons = 0;
    level.run(1);
    level.pad.rightY = 0.0F;
    level.run(5);
    CHECK(level.scripts->state().characters.lastWarriorCommand.at(0) == 3);

    // 3.3: up on the roof by the bottles (teleported, as the analyst's run), walked into the throw lesson's box; the
    // script walks him to his spot and locks the stick.
    const coney::anim::Vec3 bottles = flagAt("fBottles");
    level.play()->teleportPlayer(
        coney::world_objects::Placement{.position = {bottles.x + 4.0F, bottles.y, bottles.z}, .headingDegrees = 90.0F});
    level.run(5);
    const bool inLesson = walkTo(bottles, [&] { return hinted("TT_25"); });
    if (!inLesson) {
        dump();
    }
    REQUIRE(inLesson);
    REQUIRE(level.runUntil([&] { return hinted("TT_26"); }, kWait));
    // Triangle at the beer pile: a bottle in hand.
    level.pad.buttons = coney::pad::kTriangle;
    level.run(1);
    level.pad.buttons = 0;
    const bool holding = level.runUntil([&] { return hinted("TT_27"); }, kWait);
    if (!holding) {
        dump();
    }
    REQUIRE(holding);
    // L1: the aim.
    level.pad.buttons = coney::pad::kL1;
    level.run(1);
    level.pad.buttons = 0;
    REQUIRE(level.runUntil([&] { return hinted("TT_28"); }, kWait));
    REQUIRE(level.play()->throwAim().active());

    // The stick brings the arc onto the marker, as a player does: it turns him to face it, then pitches the aim down
    // from the start, a few steps a frame, until the arc meets it (HuIsAimingAt).
    const coney::anim::Vec3 target = flagAt("fBottleTarget");
    const bool aimed = level.runUntil(
        [&] {
            const coney::platform::PlayLevelMode& play = *level.play();
            if (play.player().human().script().aimedObject != 0) {
                level.pad.leftX = 0.0F;
                level.pad.leftY = 0.0F;
                return true;
            }
            const coney::combat::ThrowAimState& aim = play.throwAim();
            const float off = coney::human::wrapAngle(
                coney::human::headingOf(coney::anim::subtract(target, aim.releasePoint())) - aim.heading());
            // Right turns clockwise (the heading falls); the stick pulled down lowers the pitch.
            constexpr int kMostSteps = 60;
            constexpr float kFacing = 0.02F;
            constexpr int kPitchSteps = 4;
            const int turn =
                std::clamp(static_cast<int>(std::lround(-off / coney::combat::kAimRate)), -kMostSteps, kMostSteps);
            level.pad.leftX = aimStick(turn);
            level.pad.leftY = std::abs(off) < kFacing ? -aimStick(kPitchSteps) : 0.0F;
            return false;
        },
        kWait);
    if (!aimed) {
        dump();
    }
    REQUIRE(aimed);
    REQUIRE(level.runUntil([&] { return hinted("TT_29"); }, kWait));

    // Cross throws from the aim; the bottle breaks by the marker, the cops are distracted and Fox's second scene plays.
    level.pad.buttons = coney::pad::kCross;
    level.run(1);
    level.pad.buttons = 0;
    const bool distracted = level.runUntil(
        [&] { return std::ranges::find(shown.scenes, std::string("l80_c6_b")) != shown.scenes.end(); }, kWait);
    if (!distracted) {
        dump();
    }
    REQUIRE(distracted);
    REQUIRE(skipToPad(level, kWait));

    // 3.4: TT_7 asks for the Wreck'em order: R2 held, the right stick down-left.
    REQUIRE(level.runUntil([&] { return hinted("TT_7"); }, kWait));
    level.pad.buttons = coney::pad::kR2;
    level.run(2);
    level.pad.rightX = -0.67F;
    level.pad.rightY = -0.67F;
    level.run(6);
    level.pad.buttons = 0;
    level.run(1);
    level.pad.rightX = 0.0F;
    level.pad.rightY = 0.0F;
    level.run(5);
    // The order taken: the callback ran and cleared itself (the script then issues command 0, and 5 again later).
    CHECK(level.scripts->state().story.commandCallback.empty());
    const bool store = level.runUntil([&] { return shown.objective == "MS_C3_3"; }, kWait);
    if (!store) {
        dump();
    }
    REQUIRE(store);

    // 3.5: at the front door (teleported to its marker's flag, as the analyst's run), triangle starts the lock pick
    // and cross is pressed as each pin turns through the middle of its good band.
    const coney::anim::Vec3 front = flagAt("fStoreFront");
    level.play()->teleportPlayer(
        coney::world_objects::Placement{.position = {front.x, front.y, front.z}, .headingDegrees = 183.0F});
    level.run(10);
    level.pad.buttons = coney::pad::kTriangle;
    level.run(1);
    level.pad.buttons = 0;
    level.run(1);
    const coney::world_objects::LockPick* pick = level.play()->lockPick();
    if (pick == nullptr) {
        dump();
    }
    REQUIRE(pick != nullptr);
    const bool picked = level.runUntil(
        [&] {
            const coney::world_objects::LockPick* running = level.play()->lockPick();
            if (running == nullptr) {
                return true;
            }
            // The good band wraps through 0: from goodHigh up to 2π and from 0 to goodLow, each a margin inside.
            constexpr float kMargin = 0.35F;
            const coney::world_objects::LockPickDial& dial = running->dial();
            const coney::world_objects::PinBands& bands =
                coney::world_objects::kPinBands.at(static_cast<std::size_t>(dial.difficulty()));
            const float angle = dial.pins().at(static_cast<std::size_t>(dial.currentPin()));
            const bool inBand = angle >= bands.goodHigh + kMargin || angle <= bands.goodLow - kMargin;
            level.pad.buttons = inBand && level.pad.buttons == 0 ? coney::pad::kCross : 0;
            return false;
        },
        kWait);
    level.pad.buttons = 0;
    REQUIRE(picked);
    CHECK(level.logged("lock pick: picked"));
    // FrontDoorPicked ran: it issues the wreck order again for the crew's thefts. **Known gap**: the crew's Steal
    // tactic stands (ai/story_tactics.h), so the ten thefts MS_C3_4 waits for, and the back door, do not follow yet.
    level.run(30);
    CHECK(level.scripts->state().characters.lastWarriorCommand.at(0) == 5);

    // In order, as the analyst's run showed them (docs/research/scripting.md#level80); each counted the first time.
    const std::vector<std::string> expected{"MS_C3_1", "l80_c6", "TT_5",  "MS_C3_1a", "TT_25", "MS_C3_2", "TT_26",
                                            "TT_27",   "TT_28",  "TT_29", "l80_c6_b", "TT_7",  "MS_C3_3"};
    std::vector<std::string> seen;
    for (const std::string& key : shown.order) {
        if (std::ranges::find(expected, key) != expected.end() && std::ranges::find(seen, key) == seen.end()) {
            seen.push_back(key);
        }
    }
    CHECK(seen == expected);
    CHECK(level.scripts->scripts().errors() == 0);
    std::printf("  level80 checkpoint 3: %zu hints, objectives and scenes in order, last objective %s\n", seen.size(),
                shown.objective.c_str());
}
