// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <expected>
#include <memory>
#include <optional>
#include <string_view>

#include "audio/object_sounds.h"
#include "core/error.h"
#include "core/options.h"
#include "fileio/wad.h"
#include "platform/audio_output.h"
#include "platform/game_session.h"
#include "platform/log_output.h"
#include "platform/window.h"

namespace coney::platform {

// What both of Coney's entry points do the same way: the release build's (main_release.cpp, the game alone) and the
// debug tools' (main.cpp), so the game a player runs is set up as the one developers and tests run
// (docs/guides/building.md#debug-tools).

/// Writes `text` to stdout by its size and flushes, so each line shows at once even when stdout is a pipe: for the
/// usage text and the disc's line, which are not log lines.
void printText(std::string_view text);

/// Writes `text` to the run log, each line in the category its prefix names (logging::routeOf()): how the subsystems'
/// text callbacks reach the log, each line keeping its text (docs/guides/run-log.md).
void logText(std::string_view text);

/// A failure the run reports and stops or goes on without: an engine error in the run log, `coney: ` and `text`.
void logError(std::string_view text);

/// Something the run goes on without: an engine warning in the run log, `coney: ` and `text`.
void logWarning(std::string_view text);

/// Starts the run log's outputs (docs/guides/run-log.md): the console, and the log file, `--log-file` or `coney.log`
/// in the user's data folder (the previous run's kept as `coney.prev.log`); a test-mode run touches no user folder,
/// so it has a file only when given one. A default file that cannot be opened leaves the console, with a warning;
/// fails only when the `--log-file` given cannot be opened.
[[nodiscard]] std::expected<std::unique_ptr<LogOutput>, Error> startRunLog(const Options& options);

/// Opens the disc `options` names (`--disc`) and its archive and prints how many entries it lists; nothing without
/// `--disc`. Fails as io::Disc::open() and io::Wad::open() do, the message naming the path.
[[nodiscard]] std::expected<std::optional<io::Wad>, Error> openGameDisc(const Options& options);

/// The window `options` asks for: one that takes the keyboard focus unless test mode or `--no-activate` says not to,
/// on the display `--window-display` (else CONEY_WINDOW_DISPLAY) names. It views `options`, which must outlive
/// RenderEngine::start().
[[nodiscard]] WindowDesc gameWindow(const Options& options);

/// The game session's settings from `options`: the language, `--skip-movies`, the profiles' folder (none in test
/// mode) and the memory card check's message time.
[[nodiscard]] GameSessionSettings gameSessionSettings(const Options& options);

/// Starts the sound output (docs/research/sound.md#coneys-implementation): SDL's playback device, or in test mode the
/// offline one, and the game's sound from `wad` when there is one; null with `--no-audio`, or when no device opens
/// (the run stays silent and says so in the run log).
[[nodiscard]] std::unique_ptr<AudioOutput> startGameSound(const Options& options, const io::Wad* wad);

/// The end of a run that played the game: the movies played (when any), then in test mode what its humans asked to
/// be heard and what the glass and doors played. Counts only.
void printGameSummary(GameSession& session, AudioOutput* audio, const audio::ObjectSounds& objectSounds, bool testMode);

} // namespace coney::platform
