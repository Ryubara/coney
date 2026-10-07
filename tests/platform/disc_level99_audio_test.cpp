// SPDX-License-Identifier: GPL-3.0-or-later

// Checks against the player's own disc that the first mission's intro scene is heard: `level99` at checkpoint 1 plays
// as `--play-level level99 --checkpoint 1` plays it, headless, with the game's sound mixed offline (no device), through
// the intro scene `l99_c1`. Its soundtrack (docs/research/sound.md#scene-sound) must be prepared as the scene loads,
// started by its event 13, play on a real voice at a level above zero, and the mix must not be silent while it plays.
// It runs only when the environment variable CONEY_DISC names the disc and skips otherwise; it prints counts and levels
// only (LEGAL.md). With CONEY_AUDIO_WAV set to a path (outside the repository), it also writes the whole mix there as
// a 48 kHz stereo WAV file, for a person to listen to.

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <expected>
#include <fstream>
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

// Writes `samples` (interleaved 16-bit stereo at the output rate) as a WAV file at `path`; a person listens to it.
void writeWav(const char* path, std::span<const std::int16_t> samples) {
    std::ofstream out(path, std::ios::binary);
    const auto put32 = [&out](std::uint32_t v) {
        const std::array<char, 4> b{static_cast<char>(v & 0xffU), static_cast<char>((v >> 8U) & 0xffU),
                                    static_cast<char>((v >> 16U) & 0xffU), static_cast<char>(v >> 24U)};
        out.write(b.data(), b.size());
    };
    const auto put16 = [&out](std::uint16_t v) {
        const std::array<char, 2> b{static_cast<char>(v & 0xffU), static_cast<char>(v >> 8U)};
        out.write(b.data(), b.size());
    };
    const auto bytes = static_cast<std::uint32_t>(samples.size() * 2);
    const auto rate = static_cast<std::uint32_t>(coney::audio::kOutputRate);
    out.write("RIFF", 4);
    put32(36 + bytes);
    out.write("WAVEfmt ", 8);
    put32(16);
    put16(1); // PCM
    put16(2); // stereo
    put32(rate);
    put32(rate * 4);
    put16(4);
    put16(16);
    out.write("data", 4);
    put32(bytes);
    for (const std::int16_t s : samples) {
        put16(static_cast<std::uint16_t>(s));
    }
}

} // namespace

TEST_CASE("the disc's level99 intro scene plays its soundtrack into the mix", "[disc][story][audio]") {
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
    coney::GameplayMode::LevelLoader loader =
        [&renderer, &wad, &budget, &scripts, &sounds,
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
        (*mode)->setSounds(&sounds);
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

    // The pad at rest throughout: the intro is not skipped.
    auto input = coney::parseInputScript("");
    REQUIRE(input.has_value());
    coney::ScriptedInput pad(std::move(*input));
    coney::GameModeStack stack;
    stack.setInput(&pad);
    stack.push(gameplay);
    coney::GameTimer timer;
    timer.setFixedStep(true);

    // Each frame (one step in lockstep) the sound runs as AudioOutput::endFrame() runs it, and the step's block is
    // measured: the steps the soundtrack plays on a real voice, and of them the ones whose mix is not silent.
    constexpr int kAudible = 64; // a peak above this, of 32,768, is heard
    const char* wavPath = SDL_getenv("CONEY_AUDIO_WAV");
    std::vector<std::int16_t> mix;
    int soundtrackSteps = 0;
    int audibleSoundtrackSteps = 0;
    float loudestSent = 0.0F;
    int peak = 0;
    const auto endFrame = [&]() {
        game.update();
        sounds.update(1000.0F / 30.0F);
        const std::span<const std::int16_t> block = device.pullStep();
        mixer.collect();
        if (wavPath != nullptr && *wavPath != '\0') {
            mix.insert(mix.end(), block.begin(), block.end());
        }
        const coney::audio::SoundHandle scene = soundEngine.sceneSound();
        const std::optional<std::array<float, 2>> sent = soundEngine.sentVolumes(scene);
        if (!scene.valid() || soundEngine.isVirtual(scene) || !sent || (*sent)[0] + (*sent)[1] <= 0.0F) {
            return true;
        }
        ++soundtrackSteps;
        loudestSent = std::max({loudestSent, (*sent)[0], (*sent)[1]});
        int blockPeak = 0;
        for (const std::int16_t s : block) {
            blockPeak = std::max(blockPeak, std::abs(static_cast<int>(s)));
        }
        peak = std::max(peak, blockPeak);
        audibleSoundtrackSteps += blockPeak > kAudible ? 1 : 0;
        return true;
    };
    // 2,026 frames of scene after the level's start.
    stack.runUntilEmpty(timer, endFrame, 2100);
    if (wavPath != nullptr && *wavPath != '\0') {
        writeWav(wavPath, mix);
    }
    const auto said = [&log](std::string_view line) {
        return std::ranges::any_of(log, [line](const std::string& l) { return l.starts_with(line); });
    };
    if (!said("scene sound: started")) {
        for (const std::string& line : log) {
            UNSCOPED_INFO(line);
        }
    }
    CHECK(scripts.scripts().errors() == 0);
    CHECK(said("scene sound: started"));
    CHECK_FALSE(said("scene sound: nothing prepared"));
    // The soundtrack plays most of the scene's 67.5 s on a real voice, and the mix is heard through it.
    CHECK(soundtrackSteps > 1500);
    CHECK(audibleSoundtrackSteps > soundtrackSteps / 2);
    CHECK(peak > 1000);
    std::printf("  level99 checkpoint 1: intro soundtrack on a voice for %d steps, %d of them audible; peak %d, "
                "loudest volume sent %.2f; %llu frames mixed\n",
                soundtrackSteps, audibleSoundtrackSteps, peak, static_cast<double>(loudestSent),
                static_cast<unsigned long long>(device.frames()));
}
