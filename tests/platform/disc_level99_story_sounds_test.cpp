// SPDX-License-Identifier: GPL-3.0-or-later

// Checks against the player's own disc that level99's first lessons ask for the sounds the original asks for there
// (a differential run against the PS2 found them missing): STORY from boot with a new profile, as a player reaches
// the first mission, with the game's sound mixed offline, then lessons 1-6 on the pad (tests/support/level99_lessons.h)
// from the frame gameplay starts. Each tutorial hint plays its interface cue (`vags/misc/bleep50`,
// docs/research/hud.md#hint-box); the Rudy intro scene `l99_t1` plays its humans' footstep shuffles from their role
// clips (docs/research/sound.md#anim-sounds); holding L1 at a target frames the fight and starts the angry breathing
// (docs/research/sound.md#warriors-functions). It runs only when the environment variable CONEY_DISC names the disc
// and skips otherwise; it prints counts only (LEGAL.md).

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "audio/game_sound.h"
#include "audio/mixer.h"
#include "audio/offline_device.h"
#include "audio/sound_bank.h"
#include "audio/sound_data.h"
#include "audio/sound_engine.h"
#include "audio/sound_player.h"
#include "core/chunk_system.h"
#include "core/error.h"
#include "core/game_timer.h"
#include "core/input_script.h"
#include "core/name_hash.h"
#include "fileio/wad.h"
#include "gamemodes/game_mode_stack.h"
#include "gamemodes/gameplay_mode.h"
#include "gamemodes/legal_screen_mode.h"
#include "gamemodes/level_start.h"
#include "gamemodes/start_up_flow.h"
#include "gui/global_strings.h"
#include "platform/play_level_mode.h"
#include "platform/render_engine.h"
#include "platform/sprite_sheets.h"
#include "platform/texture_dictionary.h"
#include "scenes/scene_disc.h"
#include "scenes/scene_list.h"
#include "scenes/scene_player.h"
#include "scripting/config_strings.h"
#include "support/disc_play_fixtures.h"
#include "support/level99_lessons.h"
#include "world/sector_budget.h"

namespace {

// The sounds this test listens for, by name (their hashes are what the engine is asked for).
constexpr std::string_view kHintBleep = "vags/misc/bleep50";
constexpr std::array<std::string_view, 3> kShuffles{"vags/footsteps/shuffle1", "vags/footsteps/shuffle2",
                                                    "vags/footsteps/shuffle3"};
constexpr std::string_view kBreathing = "vags/character/angry_breathe_01";

// How long the lessons run after gameplay starts: to the end of lesson 6's moves and a little more.
constexpr std::uint64_t kLessonFrames = coney::test::kLessonsEnd + 60;
// The longest the menus take to start gameplay.
constexpr std::uint64_t kMenuLimit = 1500;

// One sound asked for: the frame, its hash and whether a cinematic was playing.
struct Asked {
    std::uint64_t frame = 0;
    std::uint32_t hash = 0;
    bool inCinematic = false;
};

// The plays of `name` in `asked`; with `cinematic`, only those asked for while a cinematic played.
std::size_t count(const std::vector<Asked>& asked, std::string_view name, bool cinematic = false) {
    const std::uint32_t hash = coney::crc32(name);
    return static_cast<std::size_t>(std::ranges::count_if(
        asked, [hash, cinematic](const Asked& a) { return a.hash == hash && (!cinematic || a.inCinematic); }));
}

} // namespace

TEST_CASE("the disc's STORY level99: hints bleep, the Rudy scene's humans shuffle, a lock-on breathes",
          "[disc][story][audio]") {
    std::optional<coney::io::Wad> wad = coney::test::openDisc();
    if (!wad) {
        SKIP("CONEY_DISC is not set: no disc to check");
    }
    const coney::io::Wad& theWad = *wad;
    // What main sets up: the chunk handlers, the headless renderer, the sector budget.
    coney::chunk::ChunkHandlerTable handlers = coney::chunk::ChunkHandlerTable::withDefaults();
    coney::platform::addTextureDictionaryHandlers(handlers);
    coney::platform::addSpriteSheetHandlers(handlers);
    auto engine = coney::platform::RenderEngine::start(coney::platform::RenderBackend::Null, {});
    REQUIRE(engine.has_value());
    coney::platform::RenderEngine& renderer = **engine;
    coney::gui::GlobalStrings strings;
    coney::world::SectorBudget budget(coney::world::kSectorPoolSize);
    std::vector<std::string> log;
    const auto print = [&log](std::string_view line) { log.emplace_back(line); };

    // The game's sound as main makes it in test mode, mixed offline; every play the engine is asked for is noted.
    coney::audio::Mixer mixer;
    coney::audio::OfflineDevice device(mixer);
    coney::audio::SoundPlayer sounds(mixer);
    coney::audio::GameSound game(sounds, print);
    auto tables = coney::audio::loadSoundTables(theWad);
    REQUIRE(tables.has_value());
    auto files = coney::audio::DiscSoundFiles::open(theWad.disc());
    REQUIRE(files.has_value());
    sounds.attach(std::make_unique<coney::audio::SoundEngine>(
        mixer, std::move(*tables), std::move(*files),
        [&theWad](std::string_view name, const coney::audio::SoundTables& soundTables) {
            return coney::audio::loadSoundBank(theWad, name, soundTables);
        },
        coney::audio::SoundEngine::RandomRange{}));
    std::vector<Asked> asked;
    std::uint64_t frame = 0;  // the steps run, which the pads are sampled by
    bool inCinematic = false; // a cinematic played at the start of the step
    sounds.engine()->setPlayObserver([&asked, &frame, &inCinematic](std::uint32_t hash) {
        asked.push_back(Asked{.frame = frame, .hash = hash, .inCinematic = inCinematic});
    });
    coney::GameModeStack stack;

    // The start-up flow as main makes it: gameplay loads the level as the play mode with the sound and the flow's HUD;
    // the scripts, the front end and the HUD play through the game's sound.
    std::optional<coney::StartUpFlow> flow;
    const coney::GameplayMode::LevelLoader story = coney::test::playLoader(renderer, theWad, budget, print);
    coney::GameplayMode::LevelLoader loadLevel =
        [&story, &sounds,
         &flow](const coney::LevelStart& start,
                const coney::ScriptedCast& cast) -> std::expected<std::unique_ptr<coney::GameMode>, coney::Error> {
        auto mode = story(start, cast);
        if (!mode) {
            return mode;
        }
        if (auto* play = dynamic_cast<coney::platform::PlayLevelMode*>(mode->get()); play != nullptr) {
            play->setSounds(&sounds);
            play->useHud(flow->hud());
        }
        return mode;
    };
    flow.emplace(
        renderer, stack,
        [&theWad, &handlers](std::string_view name) -> std::expected<coney::graphics::SpriteSheet, coney::Error> {
            return coney::platform::loadSpriteSheetResource(theWad, handlers, name, false);
        },
        strings, coney::LegalScreenSettings{}, print, coney::script::wadScriptSource(theWad), std::move(loadLevel));
    auto sceneList = coney::scenes::loadSceneList(theWad);
    REQUIRE(sceneList.has_value());
    const coney::GameplayMode::SceneMaker sceneMaker = [&theWad, &sceneList] {
        return std::make_unique<coney::scenes::SceneSystem>(*sceneList, coney::scenes::wadSceneSource(theWad),
                                                            coney::scenes::SceneSystem::ScriptCall{});
    };
    flow->gameplay().setSceneMaker(sceneMaker);
    flow->levelFlow().setScenes(sceneMaker, &flow->context());
    flow->context().sound = &game;
    game.connect(&flow->scripts(), &flow->context());
    flow->services().attachAudio(&game);
    flow->hud().setSoundOutput([&sounds](std::string_view name) { sounds.play(name); });

    // STORY with a new profile through the real screens, until gameplay starts.
    auto menus = coney::loadInputScript(std::string(CONEY_TEST_SUPPORT_DIR) + "/story_new_profile.txt");
    REQUIRE(menus.has_value());
    coney::ScriptedInput menuPad(std::move(menus).value_or(std::vector<coney::InputEvent>{}));
    stack.setInput(&menuPad);
    flow->start();
    coney::GameTimer timer;
    timer.setFixedStep(true);
    // Each frame the sound runs as AudioOutput::endFrame() runs it.
    const auto frames = [&](std::uint64_t steps) {
        for (std::uint64_t i = 0; i < steps; ++i) {
            const coney::scenes::SceneSystem* scenes = flow->context().scenes;
            inCinematic = scenes != nullptr && scenes->cinematicActive();
            stack.runUntilEmpty(timer, {}, 1);
            ++frame;
            game.update();
            sounds.update(1000.0F / 30.0F);
            device.pullStep();
        }
    };
    std::uint64_t waited = 0;
    while (waited < kMenuLimit && stack.topId() != coney::GameplayMode::kId) {
        frames(1);
        ++waited;
    }
    REQUIRE(stack.topId() == coney::GameplayMode::kId);

    // Lessons 1-6 on the pad, their frames counted from gameplay's start as --play-level counts them.
    const std::uint64_t start = frame;
    auto lessons = coney::parseInputScript(coney::test::kLessonsScript);
    REQUIRE(lessons.has_value());
    std::vector<coney::InputEvent> shifted = std::move(lessons).value_or(std::vector<coney::InputEvent>{});
    for (coney::InputEvent& event : shifted) {
        event.frame += start;
    }
    coney::ScriptedInput lessonPad(std::move(shifted));
    stack.setInput(&lessonPad);
    frames(kLessonFrames);

    const std::size_t bleeps = count(asked, kHintBleep);
    std::size_t shuffles = 0;
    for (const std::string_view name : kShuffles) {
        shuffles += count(asked, name, true);
    }
    const std::size_t breaths = count(asked, kBreathing);
    UNSCOPED_INFO(
        std::format("gameplay from frame {}: {} hint bleeps, {} shuffles in cinematics, {} breaths, {} plays asked",
                    start, bleeps, shuffles, breaths, asked.size()));
    // The other sounds the differential run named, for the log: a body's fall, the punch to a mounted head, the big
    // punch to the body, a step on aluminium (their triggers are wired; whether the pad's run reaches them varies).
    UNSCOPED_INFO(std::format("hitground2 {}, punch15 {}, sound_116 {}, alumstep_04 {}",
                              count(asked, "vags/thuds/hitground2"), count(asked, "vags/punches/anim_specific/punch15"),
                              count(asked, "vags/punches/anim_specific/sound_116"),
                              count(asked, "vags/footsteps/alumstep_04")));
    // Every tutorial hint bleeps: lessons 1-6 show more than ten.
    CHECK(bleeps >= 10);
    // The Rudy scene's humans shuffle into place from their role clips (the original asks for ten shuffles by the
    // end of lesson 1, eight of them in the scene).
    CHECK(shuffles >= 4);
    // Lesson 4 holds L1 at the target twice: the breathing starts.
    CHECK(breaths >= 1);
}
