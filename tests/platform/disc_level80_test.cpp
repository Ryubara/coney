// SPDX-License-Identifier: GPL-3.0-or-later

// Checks against the player's own disc that the second story mission's player-side mechanics play
// (docs/missions/level80.md): at checkpoint 1 the Warrior command menu, held open with R2 and steered up with the
// right stick, gives the follow order the script waits for (`WCSetCallback("CommandIssued")`,
// docs/research/hud.md#warrior-command-menu); at checkpoint 2 the two cuffed Warriors offer the uncuff prompt and the
// L1-R1 mash frees each, which the script hears as event 17 and moves on to checkpoint 3
// (docs/research/crimes.md#uncuffing); at checkpoint 3 an adaptive pad playthrough of the throw lesson, the store's
// front door, the crew's thefts under the wreck order and the back door asserts its hints, objectives and scenes in
// the original's order through checkpoint 4 (docs/research/scripting.md#level80, docs/research/objects.md#throws,
// docs/research/ai-code.md#t2-warrior-steal); and at checkpoint 1 the crew under the follow order runs in, stands
// while the player runs near and climbs the crates after him (docs/research/ai.md#warrior-follow). It runs only when
// the environment variable CONEY_DISC names the disc and skips otherwise; it prints counts only (LEGAL.md).

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

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "ai/brain.h"
#include "ai/brains.h"
#include "ai/crew.h"
#include "ai/goal.h"
#include "ai/move_action.h"
#include "ai/route_follower.h"
#include "ai/route_planner.h"
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
#include "graphics/throw_arc.h"
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
#include "world/path_map.h"
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
        // faces -x), for a think period. His goals are held meanwhile (the brain suspended, as BrSuspend does; his
        // thinks still run the prompt's test): moved off his formation slot by the teleport, his follow goal would
        // otherwise run him back to it, out of the swap's reach.
        warrior->setSuspended(false);
        REQUIRE(level.runUntil([warrior] { return warrior->human().gait() == coney::human::Gait::Standing; }, kWait));
        warrior->setSuspended(true);
        const coney::anim::Vec3 at = warrior->human().position();
        level.play()->teleportPlayer(
            coney::world_objects::Placement{.position = {at.x + 1.0F, at.y, at.z}, .headingDegrees = 90.0F});
        // He may still be slowing to a stop for a few steps; the test waits until he has stood for a whole think
        // period (his brain thinks one step in five).
        constexpr int kThinkPeriod = 6;
        int standing = 0;
        REQUIRE(level.runUntil(
            [warrior, &standing] {
                standing = warrior->human().gait() == coney::human::Gait::Standing ? standing + 1 : 0;
                return standing >= kThinkPeriod;
            },
            kWait));
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
    warrior->setSuspended(false);
}

namespace {

// The hints and objectives checkpoint 3 shows, by their keys in the level's text (docs/research/scripting.md#level80):
// the reference order of the analyst's run, steps 3.1 to 4.1.
constexpr std::array<std::string_view, 7> kHintKeys{"TT_5", "TT_25", "TT_26", "TT_27", "TT_28", "TT_29", "TT_7"};
constexpr std::array<std::string_view, 6> kObjectiveKeys{"MS_C3_1", "MS_C3_1a", "MS_C3_2",
                                                         "MS_C3_3", "MS_C3_4",  "MS_C4_1"};
// The store the crew robs: its object zone (its store front `storePharmacy01`'s group) and the thefts MS_C3_4 waits
// for (docs/research/scripting.md#level80, step 3.5).
constexpr std::uint32_t kStoreZone = 24;
constexpr std::ptrdiff_t kThefts = 10;
// The left stick's deflection for the walk through the store: a partial push, as a player walks.
constexpr float kPartialStick = 0.6F;
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

TEST_CASE("the disc's level80 checkpoint 3: Fox, Hold Up, the bottle thrown from the aim, the thefts, the back door",
          "[disc][story][throw][steal]") {
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
    // A hint the player can read: showing, and the HUD drawn (after a scene the HUD comes back a few frames after the
    // script has set the next text, and an R2 pressed while it is hidden gives the highlighted order at once).
    const auto hinted = [&shown, &level](std::string_view key) {
        return shown.hint == key && level.scripts->hud().visible() && !level.scripts->hud().hintsHidden();
    };
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
    // Walks player 1 towards `to` with the left stick at `deflection` (70 % unless given) until `done`, at most kWait
    // frames.
    const auto walkTo = [&level](coney::anim::Vec3 to, const std::function<bool()>& done, float deflection = 0.7F) {
        const bool reached = level.runUntil(
            [&] {
                if (done()) {
                    return true;
                }
                const auto [x, y] = stickTowards(*level.play(), to, deflection);
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
    // The arc's ribbon while he aims, before it meets the marker (docs/research/graphics.md#throw-arc): 36 vertices
    // with the first pair on his hand and faded in from it, 0.16 m wide level across the arc, grey while the aim
    // meets no target.
    {
        const coney::platform::PlayLevelMode& play = *level.play();
        const std::vector<coney::graphics::ThrowArcVertex> ribbon = play.throwArcRibbon(1.0F);
        REQUIRE(ribbon.size() == 36);
        const coney::anim::Vec3 hand = play.throwAim().releasePoint();
        CHECK((ribbon[0].position.x + ribbon[1].position.x) / 2.0F == Catch::Approx(hand.x).margin(1e-3));
        CHECK((ribbon[0].position.y + ribbon[1].position.y) / 2.0F == Catch::Approx(hand.y).margin(1e-3));
        CHECK((ribbon[0].position.z + ribbon[1].position.z) / 2.0F == Catch::Approx(hand.z).margin(1e-3));
        const coney::anim::Vec3 across = coney::anim::subtract(ribbon[1].position, ribbon[0].position);
        CHECK(std::hypot(across.x, across.y) == Catch::Approx(0.16F).margin(1e-4));
        CHECK(across.z == Catch::Approx(0.0F).margin(1e-4));
        CHECK(ribbon[0].rgba[3] == 0);
        CHECK(ribbon[2].rgba[3] >= 127); // then 127 plus up to 16 by how far along the arc
        CHECK(ribbon[2].rgba[3] <= 143);
        const bool already = play.player().human().script().aimedObject != 0;
        CHECK(ribbon[2].rgba[0] == (already ? 170 : 127));
    }

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
    // On the marker the ribbon is red: (170, 43, 43).
    {
        const std::vector<coney::graphics::ThrowArcVertex> ribbon = level.play()->throwArcRibbon(1.0F);
        REQUIRE(ribbon.size() == 36);
        CHECK(ribbon[2].rgba[0] == 170);
        CHECK(ribbon[2].rgba[1] == 43);
        CHECK(ribbon[2].rgba[2] == 43);
    }

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
    // The bottle's break clears the throw lesson's text and flushes its poll: no hint of it is left showing or
    // queued to come back in step 3.4 (owner checklist item 38: "throw a bottle" stayed up until the Mayhem order).
    const coney::hud::HintBox& box = level.scripts->hud().hints();
    CHECK_FALSE(box.showing().has_value());
    CHECK(box.queued().empty());
    const std::size_t hintsBefore = shown.order.size();
    REQUIRE(skipToPad(level, kWait));

    // 3.4: TT_7 asks for the Wreck'em order: R2 held, the right stick down-left. It is the next hint shown.
    REQUIRE(level.runUntil([&] { return hinted("TT_7"); }, kWait));
    {
        std::vector<std::string> between(shown.order.begin() + static_cast<std::ptrdiff_t>(hintsBefore),
                                         shown.order.end());
        std::erase_if(between, [](const std::string& key) { return !key.starts_with("TT_"); });
        CHECK(between == std::vector<std::string>{"TT_7"});
    }
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
    // The order cleared TT_7, and nothing older took its place.
    CHECK_FALSE(box.showing().has_value());
    CHECK(box.queued().empty());

    // Triangle at a lock starts the pick, and cross is pressed as each pin turns through the middle of its good band;
    // true once the dial is over.
    const auto pickLock = [&level, &dump] {
        level.pad.buttons = coney::pad::kTriangle;
        level.run(1);
        level.pad.buttons = 0;
        level.run(1);
        if (level.play()->lockPick() == nullptr) {
            dump();
            return false;
        }
        const bool over = level.runUntil(
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
        return over;
    };
    // The store's owned stock still on its shelves (zone 24, ObjSpawn flag 2).
    const auto stock = [&level] {
        return std::ranges::count_if(
            level.scripts->spawnRecords().all(),
            [](const coney::world_objects::SpawnRecord& r) { return r.zone == kStoreZone && r.owned && !r.removed; });
    };
    const auto stockBefore = stock();
    CHECK(stockBefore >= kThefts);

    // 3.5: at the front door (teleported to its marker's flag, as the analyst's run), the lock is picked.
    const coney::anim::Vec3 front = flagAt("fStoreFront");
    level.play()->teleportPlayer(
        coney::world_objects::Placement{.position = {front.x, front.y, front.z}, .headingDegrees = 183.0F});
    level.run(10);
    REQUIRE(pickLock());
    CHECK(level.logged("lock pick: picked"));
    // FrontDoorPicked issues the wreck order again: at the store front the crew steals (WarriorSteal), and the tenth
    // theft (message 7 on vStealTrigger) brings MS_C3_4 and the back door. The player only waits at the door, as in
    // the analyst's run.
    level.run(30);
    CHECK(level.scripts->state().characters.lastWarriorCommand.at(0) == 5);
    const bool stolen = level.runUntil([&shown] { return shown.objective == "MS_C3_4"; }, kWait);
    if (!stolen) {
        dump();
    }
    REQUIRE(stolen);
    const auto taken = stockBefore - stock();
    CHECK(taken >= kThefts);
    CHECK(level.play()->player().human().script().heldObject == 0.0); // the player took nothing himself

    // 3.6: the back door, walked to with the left stick at 60 % through the store, its lock picked; checkpoint 4
    // follows and chapter 4's first objective shows.
    const coney::anim::Vec3 back = flagAt("fStoreBack");
    // There once the lock's prompt is offered (a pickable door in reach).
    const auto atBackDoor = [&level] { return level.play()->promptOffer().lock.has_value(); };
    const bool reachedBack = walkTo(back, atBackDoor, kPartialStick);
    if (!reachedBack) {
        dump();
    }
    REQUIRE(reachedBack);
    level.run(10);
    REQUIRE(pickLock());
    const bool chapter4 =
        level.runUntil([&] { return level.checkPoint() == 4.0 && shown.objective == "MS_C4_1"; }, kWait);
    if (!chapter4) {
        dump();
    }
    REQUIRE(chapter4);

    // In order, as the analyst's run showed them (docs/research/scripting.md#level80); each counted the first time.
    const std::vector<std::string> expected{"MS_C3_1",  "l80_c6", "TT_5",    "MS_C3_1a", "TT_25",
                                            "MS_C3_2",  "TT_26",  "TT_27",   "TT_28",    "TT_29",
                                            "l80_c6_b", "TT_7",   "MS_C3_3", "MS_C3_4",  "MS_C4_1"};
    std::vector<std::string> seen;
    for (const std::string& key : shown.order) {
        if (std::ranges::find(expected, key) != expected.end() && std::ranges::find(seen, key) == seen.end()) {
            seen.push_back(key);
        }
    }
    CHECK(seen == expected);
    CHECK(level.scripts->scripts().errors() == 0);
    std::printf("  level80 checkpoint 3: %zu hints, objectives and scenes in order, %td of the store's stock stolen, "
                "checkpoint %.0f\n",
                seen.size(), taken, level.checkPoint());
}

namespace {

// level80 from "Let's Go" with the follow order given (R2 held, the right stick up at 95 %), and the script's staging
// of the street run out: the chief and the Warriors that follow him.
struct FollowingCrew {
    coney::ai::Brain* chief = nullptr;
    std::vector<coney::ai::Brain*> crew;
    std::vector<coney::anim::Vec3> atLetsGo; // the Warriors' feet when the order was asked for
};

// Runs `level` to the follow order and gives it; `watch` (may be empty) sees every frame from the order on.
FollowingCrew giveFollowOrder(Level80& level, const std::function<void(const FollowingCrew&)>& watch) {
    FollowingCrew found;
    // The intro, skipped, until the script asks for Let's Go.
    constexpr int kWait = 30 * 60;
    int frame = 0;
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
    REQUIRE(level.gameplay->brains() != nullptr);
    // The test moves player 1 and the Warriors and reads their moves, so it reaches into the brains gameplay owns.
    auto& brains = const_cast<coney::ai::Brains&>(*level.gameplay->brains());
    std::vector<coney::ai::Brain*> warriors;
    for (std::size_t i = 0; i < brains.size(); ++i) {
        coney::ai::Brain& brain = brains.at(i);
        if (brain.type() == coney::ai::BrainType::Player) {
            found.chief = &brain;
        } else if (brain.type() == coney::ai::BrainType::Warrior) {
            warriors.push_back(&brain);
        }
    }
    REQUIRE(found.chief != nullptr);
    // Before the order every Warrior counts, so the run-in is watched; after the staging only the followers do.
    found.crew = warriors;
    for (const coney::ai::Brain* warrior : warriors) {
        found.atLetsGo.push_back(warrior->human().position());
    }
    if (watch) {
        level.watch = [&found, watch] { watch(found); };
    }
    level.pad.buttons = coney::pad::kR2;
    level.run(2);
    level.pad.rightY = 0.95F;
    level.run(6);
    level.pad.buttons = 0;
    level.run(1);
    level.pad.rightY = 0.0F;
    // The run-in and the script's staging of the street.
    constexpr int kStaging = 15 * 30;
    level.run(kStaging);
    level.watch = nullptr;
    std::erase_if(found.crew, [](const coney::ai::Brain* warrior) { return warrior->following() == nullptr; });
    REQUIRE(found.crew.size() == 2);
    return found;
}

// Whether `warrior` follows a FollowFormation goal now.
bool followingFormation(coney::ai::Brain& warrior) {
    return warrior.findGoal(coney::ai::GoalType::FollowFormation) != nullptr;
}

} // namespace

TEST_CASE(
    "the disc's level80: the crew runs in after Let's Go, stands while the player runs near and climbs the crates",
    "[disc][story][follow]") {
    // The runtime check of docs/research/ai.md#warrior-follow (PCSX2, level80 at "Let's Go"): the Warriors ask
    // 12.81 m/s (1.25 x sprint) on their run-in, stand while the player runs within 9 m in a straight line and follow
    // once he is past it, and climb the crate stack by its 0x80 ledge links when he stands on it.
    std::optional<coney::io::Wad> wad = coney::test::openDisc();
    if (!wad) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    auto engine = coney::platform::RenderEngine::start(coney::platform::RenderBackend::Null, {});
    REQUIRE(engine.has_value());
    Level80 level(**engine, *wad, 1);

    // Every frame: the fastest speed a Warrior's follow move asks, whether one planned a route, whether one climbed a
    // 0x80 ledge leg, and each FollowFormation pushed while its Warrior stood within 9 m of the chief in a straight
    // line (none should be: there the follow goal stands).
    float fastest = 0.0F;
    bool routed = false;
    bool ledge = false;
    int pushedNear = 0;
    int pushedFar = 0;
    std::vector<bool> wasFollowing(8, false);
    const auto watch = [&](const FollowingCrew& at) {
        for (std::size_t k = 0; k < at.crew.size() && k < wasFollowing.size(); ++k) {
            coney::ai::Brain& warrior = *at.crew[k];
            const bool following = followingFormation(warrior);
            if (following && !wasFollowing[k]) {
                const coney::ai::RoutePlanner* planner = warrior.planner();
                const bool inLine =
                    planner == nullptr || planner->lineClear(warrior.human().position(), at.chief->human().position());
                ++((warrior.distanceTo(*at.chief) <= coney::ai::kCrewFollowDistance && inLine) ? pushedNear
                                                                                               : pushedFar);
            }
            wasFollowing[k] = following;
            const auto* move = dynamic_cast<const coney::ai::MoveAction*>(warrior.frontAction());
            if (move == nullptr || !following) {
                continue;
            }
            fastest = std::max(fastest, move->speed().value_or(0.0F));
            routed = routed || move->routed();
            const coney::ai::RouteFollower* route = move->follower();
            ledge = ledge || (route != nullptr && (route->legKind() & coney::ai::edge_flag::kJump) != 0 &&
                              warrior.human().traversal() == coney::human::Traversal::Climbing);
        }
    };
    FollowingCrew run = giveFollowOrder(level, watch);
    coney::ai::Brain* chief = run.chief;
    const float sprint = run.crew[0]->human().speeds().sprint;
    float startHeight = 0.0F;
    for (const coney::anim::Vec3 feet : run.atLetsGo) {
        startHeight = std::max(startHeight, feet.z - chief->human().position().z);
    }
    // **Known difference** (not checked): the original's Warriors planned a route down the landing's stairs; Coney's
    // walkable line passes from the landing to the street, so they run straight.
    std::printf("  level80 follow: run-in from %.1f m up asked up to %.2f m/s (1.25 x sprint %.2f), route %s\n",
                static_cast<double>(startHeight), static_cast<double>(fastest), static_cast<double>(1.25F * sprint),
                routed ? "planned" : "none");
    CHECK(fastest > 1.25F * sprint - 0.01F);

    // The player runs (stick 100 % ahead) for 4 s, then stops for 6 s.
    pushedNear = 0;
    pushedFar = 0;
    level.watch = [&] { watch(run); };
    level.pad.leftY = 1.0F;
    level.run(4ULL * 30);
    level.pad.leftY = 0.0F;
    level.run(6ULL * 30);
    std::printf("  level80 follow: while the player ran, %d follows began beyond 9 m or out of line, %d within\n",
                pushedFar, pushedNear);
    CHECK(pushedNear == 0);
    CHECK(pushedFar > 0);
    // Both stand once he stands: no Warrior circles a point left over from his run.
    for (coney::ai::Brain* warrior : run.crew) {
        CHECK(warrior->human().speed() < 0.1F);
        CHECK(warrior->distanceTo(*chief) < coney::ai::kCrewFollowDistance);
    }

    // The crate stack: the route nodes 860-865 whose 0x80 links climb onto it (docs/research/ai.md#warrior-follow).
    // The player is put on its top node and stands there.
    const coney::ai::RoutePlanner* planner = run.crew[0]->planner();
    REQUIRE(planner != nullptr);
    const coney::world::PathMap& map = planner->map();
    REQUIRE(map.nodes().size() > 865);
    std::uint32_t top = 860;
    bool ledgeLinks = false;
    for (std::uint32_t node = 860; node <= 865; ++node) {
        if (map.nodes()[node].position.z > map.nodes()[top].position.z) {
            top = node;
        }
        for (const coney::world::PathEdge& edge : map.edgesOf(node)) {
            ledgeLinks = ledgeLinks || (edge.flags & coney::ai::edge_flag::kJump) != 0;
        }
    }
    REQUIRE(ledgeLinks);
    const coney::anim::Vec3 stack = map.nodes()[top].position;
    chief->human().place(stack, chief->human().heading());
    level.run(2);
    float highest = -1000.0F;
    float lowest = 1000.0F;
    for (int second = 0; second < 20 && highest < stack.z - 0.5F; ++second) {
        level.run(30);
        for (const coney::ai::Brain* warrior : run.crew) {
            highest = std::max(highest, warrior->human().position().z);
            lowest = std::min(lowest, warrior->human().position().z);
        }
    }
    level.watch = nullptr;
    std::printf("  level80 follow: player on the crates %.1f m up; a Warrior climbed a ledge leg: %s; highest Warrior "
                "%.1f m, lowest %.1f m\n",
                static_cast<double>(stack.z), ledge ? "yes" : "no", static_cast<double>(highest),
                static_cast<double>(lowest));
    CHECK(ledge);
    CHECK(highest > stack.z - 0.5F);
    // Nobody fell off the street.
    CHECK(lowest > 0.0F);
    CHECK(level.scripts->scripts().errors() == 0);
}
