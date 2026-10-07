// SPDX-License-Identifier: GPL-3.0-or-later

// Checks against the player's own disc that every level99 scene with a soundtrack is heard, however the level's other
// streams and the last scene's soundtrack fall when it loads (docs/research/sound.md#scene-sound). level99 plays from
// checkpoint 1 as `--play-level level99 --checkpoint 1` plays it, headless, with the game's sound mixed offline: its
// intro scene `l99_c1` first, then each other `l99_` scene with a soundtrack in turn through the `--scene` test aid,
// a different number of steps after the last cinematic ends. Asked for within a few steps, the last scene's
// soundtrack still plays on (it outlives its scene), so the new one waits as pending for its stream pair and the
// cinematic's start waits for it. Each soundtrack must start on a real voice and the mix be heard through it. It runs
// only when the environment variable CONEY_DISC names the disc and skips otherwise; it prints counts only (LEGAL.md).

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <SDL3/SDL_stdinc.h>
#include <catch2/catch_test_macros.hpp>

#include "ai/ai_config.h"
#include "audio/audio_format.h"
#include "audio/game_sound.h"
#include "audio/mixer.h"
#include "audio/offline_device.h"
#include "audio/sound_bank.h"
#include "audio/sound_data.h"
#include "audio/sound_engine.h"
#include "audio/sound_player.h"
#include "audio/sound_stream.h"
#include "characters/character_types.h"
#include "core/error.h"
#include "core/game_random.h"
#include "core/game_timer.h"
#include "core/input_script.h"
#include "fileio/disc.h"
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
#include "scenes/scene_record.h"
#include "scripting/config_strings.h"
#include "scripting/script_system.h"
#include "world/sector_budget.h"

namespace {

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

// One scene to hear, and what was heard of it.
struct SceneRun {
    std::string name;
    std::uint32_t frames = 0; // the scene's length, 1/30 s frames
    bool cinematic = false;   // its cinematic has been seen running
    bool overlapped = false;  // asked for while the last scene's soundtrack still played
    std::size_t firstLog = 0; // the log's length when it was asked to play
    int voiceSteps = 0;       // steps its soundtrack played on a real voice at a volume above zero
    int audibleSteps = 0;     // of them, the steps whose mix was not silent
};

} // namespace

TEST_CASE("every level99 scene with a soundtrack is heard, whatever holds the stream channels",
          "[disc][scenes][audio]") {
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

    // The game's sound as main makes it in test mode: the engine over the disc's tables, streams and banks, mixed by
    // the offline device one step at a time.
    coney::audio::Mixer mixer;
    coney::audio::OfflineDevice device(mixer);
    coney::audio::SoundPlayer sounds(mixer);
    coney::audio::GameSound game(sounds, print);
    auto tables = coney::audio::loadSoundTables(*wad);
    REQUIRE(tables.has_value());
    auto files = coney::audio::DiscSoundFiles::open(wad->disc());
    REQUIRE(files.has_value());
    const coney::io::Wad& banks = *wad;
    sounds.attach(std::make_unique<coney::audio::SoundEngine>(
        mixer, std::move(*tables), std::move(*files),
        [&banks](std::string_view name, const coney::audio::SoundTables& soundTables) {
            return coney::audio::loadSoundBank(banks, name, soundTables);
        },
        coney::audio::SoundEngine::RandomRange{}));
    coney::audio::SoundEngine& soundEngine = *sounds.engine();

    // The scripts as --play-level runs them, with the game's random table and the game's sound.
    coney::LevelScriptOptions options;
    std::vector<std::uint32_t> table;
    if (auto words = coney::io::readExecutableWords(wad->disc(), coney::GameRandom::kExecutableName,
                                                    coney::GameRandom::kTableAddress, coney::GameRandom::kTableSize)) {
        table = std::move(*words);
        options.randomTable = table;
    }
    coney::LevelScripts scripts(coney::script::wadScriptSource(*wad), "level99", 1, print, options);
    scripts.context().sound = &game;
    game.connect(&scripts.scripts(), &scripts.context());
    coney::platform::PlayLevelMode* playMode = nullptr;
    coney::GameplayMode::LevelLoader loader =
        [&renderer, &wad, &budget, &scripts, &sounds, &print,
         &playMode](const coney::LevelStart& start,
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
        (*mode)->setSounds(&sounds);
        playMode = mode->get();
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

    // The scenes: level99's own with a soundtrack, the intro (which the level plays itself) first.
    std::vector<SceneRun> runs;
    const coney::scenes::SceneRecordSource source = coney::scenes::wadSceneSource(*wad);
    for (const coney::scenes::SceneListEntry& entry : sceneList->entries()) {
        if (!entry.name.starts_with("l99_")) {
            continue;
        }
        auto bytes = source(entry.name);
        auto header = bytes ? coney::scenes::parseSceneHeader(*bytes)
                            : std::expected<coney::scenes::SceneHeader, coney::Error>(std::unexpected(bytes.error()));
        if (header && coney::scenes::soundtrackOf(*header)) {
            runs.push_back(SceneRun{.name = header->name, .frames = header->frames});
        }
    }
    std::ranges::sort(runs, [](const SceneRun& a, const SceneRun& b) {
        return a.name == "l99_c1" ? b.name != "l99_c1" : (b.name != "l99_c1" && a.name < b.name);
    });
    REQUIRE(runs.size() > 1);
    REQUIRE(runs.front().name == "l99_c1");

    // The pad at rest throughout: no scene is skipped.
    auto input = coney::parseInputScript("");
    REQUIRE(input.has_value());
    coney::ScriptedInput pad(std::move(*input));
    coney::GameModeStack stack;
    stack.setInput(&pad);
    stack.push(gameplay);
    coney::GameTimer timer;
    timer.setFixedStep(true);

    // Each frame: the scene due is asked for (after its wait), the sound runs as AudioOutput::endFrame() runs it, and
    // the step's block is measured against the scene playing.
    constexpr int kAudible = 64;                                          // a peak above this, of 32,768, is heard
    constexpr std::array<std::uint32_t, 6> kWaits{1, 170, 2, 430, 5, 75}; // steps after a cinematic's end
    constexpr std::uint32_t kGiveUp = 600; // steps past a scene's length when its cinematic never ran
    std::size_t current = 0;
    std::uint32_t frame = 0;
    std::uint32_t sceneStart = 0; // the frame the current scene was asked for (the intro: 0)
    std::uint32_t nextAt = 0;     // the frame the next scene is asked for, 0 while one plays
    std::vector<std::string> failures;
    const auto endFrame = [&]() {
        // The next scene, once the wait after the last is over.
        if (nextAt != 0 && frame >= nextAt) {
            nextAt = 0;
            ++current;
            if (current >= runs.size()) {
                return false;
            }
            SceneRun& run = runs[current];
            run.firstLog = log.size();
            run.overlapped = soundEngine.isPlaying(soundEngine.sceneSound());
            sceneStart = frame;
            REQUIRE(playMode != nullptr);
            if (auto played = playMode->playScene(run.name); !played) {
                failures.push_back(run.name + ": " + played.error().message);
            }
        }
        game.update();
        sounds.update(1000.0F / 30.0F);
        const std::span<const std::int16_t> block = device.pullStep();
        mixer.collect();
        ++frame;
        if (nextAt != 0) {
            return true;
        }
        SceneRun& run = runs[current];
        const coney::audio::SoundHandle scene = soundEngine.sceneSound();
        const std::optional<std::array<float, 2>> sent = soundEngine.sentVolumes(scene);
        if (scene.valid() && !soundEngine.isVirtual(scene) && sent && (*sent)[0] + (*sent)[1] > 0.0F) {
            ++run.voiceSteps;
            int blockPeak = 0;
            for (const std::int16_t s : block) {
                blockPeak = std::max(blockPeak, std::abs(static_cast<int>(s)));
            }
            run.audibleSteps += blockPeak > kAudible ? 1 : 0;
        }
        // The next scene some steps after this one's cinematic ends.
        run.cinematic = run.cinematic || soundEngine.cinematic();
        if ((run.cinematic && !soundEngine.cinematic()) || frame >= sceneStart + run.frames + kGiveUp) {
            nextAt = frame + kWaits.at(current % kWaits.size());
        }
        return true;
    };
    std::uint64_t limit = 0;
    for (const SceneRun& run : runs) {
        limit += run.frames + kGiveUp + 600;
    }
    stack.runUntilEmpty(timer, endFrame, limit);

    // Each scene's soundtrack started on a voice ("started", not "virtually" nor "nothing"), played on it for most
    // of the scene, and was heard; some scenes were asked for while the last soundtrack still played.
    CHECK(scripts.scripts().errors() == 0);
    CHECK(failures.empty());
    for (const std::string& failure : failures) {
        UNSCOPED_INFO(failure);
    }
    CHECK(current >= runs.size() - 1);
    CHECK(std::ranges::count_if(runs, &SceneRun::overlapped) >= 1);
    for (const SceneRun& run : runs) {
        const std::size_t to = &run == &runs.back() ? log.size() : std::min(log.size(), (&run + 1)->firstLog);
        const bool started = std::any_of(log.begin() + static_cast<std::ptrdiff_t>(std::min(run.firstLog, to)),
                                         log.begin() + static_cast<std::ptrdiff_t>(to),
                                         [](const std::string& line) { return line == "scene sound: started\n"; });
        std::printf("  %s: %u frames%s; soundtrack %s, on a voice %d steps, %d of them audible\n", run.name.c_str(),
                    run.frames, run.overlapped ? ", asked for while the last soundtrack played" : "",
                    started ? "started" : "NOT started", run.voiceSteps, run.audibleSteps);
        INFO(run.name);
        CHECK(started);
        CHECK(run.voiceSteps > static_cast<int>(run.frames) / 2);
        CHECK(run.audibleSteps > run.voiceSteps / 2);
    }
}
