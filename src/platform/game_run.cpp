// SPDX-License-Identifier: GPL-3.0-or-later
// What both of Coney's entry points do the same way (docs/guides/building.md#debug-tools).
#include "platform/game_run.h"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <format>
#include <optional>
#include <string>
#include <utility>

#include "core/frame_clock.h"
#include "core/log.h"
#include "core/log_filter.h"
#include "fileio/disc.h"
#include "gamemodes/memory_card_mode.h"
#include "movies/movie_mode.h"
#include "platform/frame_pacer.h"
#include "platform/profile_folder.h"

namespace coney::platform {

void printText(std::string_view text) {
    std::fwrite(text.data(), 1, text.size(), stdout);
    std::fflush(stdout);
}

void logText(std::string_view text) { logging::writeRouted(text); }

void logError(std::string_view text) { logging::error(logging::Category::Engine, std::format("coney: {}", text)); }

void logWarning(std::string_view text) { logging::warn(logging::Category::Engine, std::format("coney: {}", text)); }

std::expected<std::unique_ptr<LogOutput>, Error> startRunLog(const Options& options) {
    logging::LogFilter filter;
    for (const logging::Category category : options.logVerbose) {
        filter.setVerbose(category);
    }
    // The file: the one given, else the default one outside test mode (none when SDL cannot name the folder).
    std::optional<std::filesystem::path> path;
    if (const std::optional<std::string> given = options.logFile; given) {
        path = std::filesystem::path(*given);
    } else if (!isTestMode(options)) {
        if (std::filesystem::path folder = defaultLogPath(); !folder.empty()) {
            path = std::move(folder);
        }
    }
    auto output = LogOutput::start(path, filter, !options.logFile.has_value());
    if (output || options.logFile) {
        return output;
    }
    // The default file could not be opened: the console alone, and say why.
    const std::string why = output.error().message;
    output = LogOutput::start(std::nullopt, filter, false);
    logWarning(std::format("{}; logging to the console only", why));
    return output;
}

std::expected<std::optional<io::Wad>, Error> openGameDisc(const Options& options) {
    // A local copy: clang-tidy cannot prove that an optional inside `options` stays set between the check and the use.
    const std::optional<std::string> discPath = options.discPath;
    if (!discPath) {
        return std::optional<io::Wad>{};
    }
    auto disc = io::Disc::open(*discPath);
    if (!disc) {
        return std::unexpected(std::move(disc.error()));
    }
    auto opened = io::Wad::open(std::move(*disc));
    if (!opened) {
        return fail(opened.error().code, std::format("{}: {}", *discPath, opened.error().message));
    }
    printText(std::format("{}: WARRIORS.DIR lists {} entries\n", *discPath, opened->index().entries().size()));
    return std::optional<io::Wad>(std::move(*opened));
}

WindowDesc gameWindow(const Options& options) {
    WindowDesc desc;
    // Runs started by agents and tools (test mode, --no-activate) must not take the focus from someone typing.
    desc.activate = activatesWindow(options);
    // `--window-display` (else CONEY_WINDOW_DISPLAY): agents put their runs on a display nobody works on.
    // A view of `options`' own string, which outlives the start (a copy here would not).
    if (options.windowDisplay.has_value()) {
        desc.display = *options.windowDisplay;
    }
    return desc;
}

GameSessionSettings gameSessionSettings(const Options& options) {
    GameSessionSettings settings;
    settings.language = options.language;
    settings.skipMovies = options.skipMovies;
    // The saved profiles: the player's folder, or none in test mode (docs/research/save.md#coney).
    settings.profiles = profileFolder(options);
    settings.cardCheckingMs = MemoryCardMode::kCheckingMessageMs;
    return settings;
}

std::unique_ptr<AudioOutput> startGameSound(const Options& options, const io::Wad* wad) {
    if (options.noAudio) {
        return nullptr;
    }
    // SDL's playback device, or in test mode the offline one, which opens no device and mixes a fixed step of frames
    // per frame. A device that cannot open leaves the run silent, never stopped.
    const bool testMode = isTestMode(options);
    auto started = AudioOutput::start(testMode ? AudioSink::Offline : AudioSink::Device);
    if (!started) {
        logWarning(std::format("{}; running without sound", started.error().message));
        return nullptr;
    }
    std::unique_ptr<AudioOutput> audio = std::move(*started);
    if (!testMode) {
        logText(audio->startLine());
    }
    // The game's sound (docs/research/sound.md): the tables, banks and streams from the disc.
    if (wad != nullptr) {
        if (auto soundData = audio->startEngine(*wad); !soundData) {
            logWarning(std::format("{}; the game's sounds stay silent", soundData.error().message));
        }
    }
    audio->game().setLog(logText);
    return audio;
}

void printGameSummary(GameSession& session, AudioOutput* audio, const audio::ObjectSounds& objectSounds,
                      bool testMode) {
    if (session.movies().counts().movies > 0) {
        const movies::MovieMode::Counts& counts = session.movies().counts();
        logText(std::format("movies: {} played ({} skipped, {} failed), {} frames decoded, {} shown, {} sound "
                            "samples, {} captions\n",
                            counts.movies, counts.skipped, counts.failed, counts.framesDecoded, counts.framesShown,
                            counts.samples, counts.captionsShown));
    }
    // A game played in test mode, from the story or a level: what its humans asked to be heard and what the glass and
    // doors played (counts only).
    if (audio != nullptr && testMode) {
        logText(audio->game().summary());
        logText(std::format("object sounds: {} played, {} material pairs, {} unplayed\n", objectSounds.played(),
                            objectSounds.materialPairs(), objectSounds.unplayed()));
    }
}

bool startNetPlay(NetPlay& net, const Options& options) {
    if (const std::optional<std::string> name = options.netName; name && !net.setPlayerName(*name)) {
        return false;
    }
    if (const std::optional<NetEndpoint> host = options.netHost; host) {
        net.setPort(host->port);
        if (!net.hostOn(host->address)) {
            return false;
        }
    }
    if (const std::optional<NetEndpoint> join = options.netJoin; join) {
        net.setAddress(join->address);
        net.setPort(join->port);
        if (!net.join()) {
            return false;
        }
    }
    return true;
}

void addNetPlayHooks(FrameHooks& hooks, NetPlay& net, bool realTime) {
    // The game's fixed step: 30 a second (docs/guides/conventions.md#update-and-render).
    constexpr std::uint64_t kStepNs = FrameClock::kNanosecondsPerSecond / 30;
    hooks.beforeStep = [&net, realTime] {
        if (!realTime) {
            net.advance(kStepNs);
        }
        net.beforeStep();
    };
    hooks.afterStep = [&net] { net.afterStep(); };
}

void finishNetPlay(NetPlay& net, const Options& options) {
    if (options.netHost || options.netJoin) {
        logText(net.summary());
    }
    // This is after the last frame, so a short real wait does no harm, even in test mode; a pacer at 100 frames a
    // second times it, the one real clock Coney reads, and is made only when a transport is still closing.
    std::optional<FramePacer> closePacer;
    net.shutdown([&closePacer] {
        if (!closePacer) {
            closePacer.emplace(100);
            (void)closePacer->waitForFrame(); // the first call only starts the count
        }
        return closePacer->waitForFrame();
    });
}

} // namespace coney::platform
