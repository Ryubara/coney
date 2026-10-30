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
#include "gamemodes/game_mode_stack.h"
#include "platform/audio_output.h"
#include "platform/game_session.h"
#include "platform/log_output.h"
#include "platform/net_play.h"
#include "platform/render_engine.h"
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

/// Starts the renderer a run draws with (docs/engine/renderer.md#migration): librw's NULL device when `headless`,
/// else `renderer` (chooseRenderer()) in a window as `desc` asks. A GPU renderer that cannot start (no Vulkan,
/// Direct3D 12 or Metal device opens) is a warning in the run log, saying why, and the run draws with GL3 instead; a
/// started one says its driver. Fails as RenderEngine::start() does for the renderer it ends with.
[[nodiscard]] std::expected<std::unique_ptr<RenderEngine>, Error> startRenderer(RendererKind renderer, bool headless,
                                                                                const WindowDesc& desc);

/// The end of a run on the GPU renderer: how many of the frames it presented reached the screen, and with which
/// present mode, for CI's smoke run (docs/guides/building.md#what-ci-runs). Nothing for the other renderers.
void printRendererSummary(const RenderEngine& renderer);

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

/// Network play from the command line (docs/guides/multiplayer.md): `--net-name`, then hosting (`--host`) or joining
/// (`--join`) from the start. False when one fails (the log says why), which ends the run as a bad argument does.
[[nodiscard]] bool startNetPlay(NetPlay& net, const Options& options);

/// Network play's part of the main loop: each step receives before it and sends after it (FrameHooks::beforeStep and
/// afterStep), and `realTime` says whether the frame pacer passes it the real time (NetPlay::advance() from
/// waitForFrame); without one (test mode) each step passes a fixed step's time, so a run is the same every time.
void addNetPlayHooks(FrameHooks& hooks, NetPlay& net, bool realTime);

/// The end of network play: the summary line when the run hosted or joined, then the leave, waiting a little real time
/// for the other games to get this one's Bye (NetPlay::shutdown()).
void finishNetPlay(NetPlay& net, const Options& options);

} // namespace coney::platform
