// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <functional>
#include <string>
#include <vector>

#include "core/frame_clock.h"
#include "core/renderer_choice.h"
#include "platform/audio_output.h"
#include "platform/frame_pacer.h"
#include "platform/game_console.h"
#include "platform/game_session.h"
#include "platform/game_settings.h"
#include "platform/render_engine.h"

// @coney-only what the console takes from the running game (docs/guides/console.md#in-the-game); the original has no
// console.

namespace coney::platform {

/// What the console asks of the game over the disc, read once a call: whether there is one and which level is in play.
/// Held apart from GameSession so the links are tested without a disc; sessionFacts() reads a real one.
struct SessionFacts {
    /// Whether a game session exists (the game and the disc are then met).
    bool exists = false;
    /// Whether a level is in play (the level is then met).
    bool inPlay = false;
    /// The level in play, empty when none.
    std::string level;
};

/// What `session` says now: nothing when there is none, else whether a level is in play and its name.
[[nodiscard]] SessionFacts sessionFacts(GameSession* session);

/// The running game's parts the console's commands reach, as both entry points have them once the game, the renderer
/// and the sound exist. Each pointer may be null, and what depends on it is then left out; none is owned, and all must
/// outlive the console's use of the links.
struct GameConsoleSources {
    /// Whether this is the build with the debug tools (`version` says which).
    bool debugTools = false;
    RendererKind rendererKind = RendererKind::Gl3;
    /// The renderer: its vsync is a setting in a window.
    RenderEngine* renderer = nullptr;
    /// The game over the disc as it is now (a session may start after the console does); empty, or null, without one.
    std::function<SessionFacts()> session;
    /// The frame pacer and the clock it paces, whose cap is a setting; null in test mode, which has no real clock.
    FramePacer* pacer = nullptr;
    FrameClock* clock = nullptr;
    /// The sound output, whose master volume is a setting; null without sound.
    AudioOutput* audio = nullptr;
    /// Writes the run log's ring into the log file and returns the line to show; empty: the logger's own dump.
    std::function<std::string()> writeBugReport;
};

/// The vsync setting over `renderer`: `on` or `off`, applied at once. playerSettings() offers it in a window only.
[[nodiscard]] GameSetting vsyncSetting(RenderEngine& renderer);

/// The settings a player has at the console, over the parts that exist: `fps_cap` (with a frame pacer), `vsync` (in a
/// window), `volume` (with sound) and `display_preset`. Each applies at once and writes nothing.
[[nodiscard]] std::vector<GameSetting> playerSettings(const GameConsoleSources& sources);

/// What the console takes from the game: the needs, the version and build, the level in play, the settings and the bug
/// report. The mission select's calls stay empty and `mission-select` is not among the built features, so `missions`
/// and `mission` stay out of the game until Coney has one.
[[nodiscard]] GameConsoleLinks makeGameConsoleLinks(const GameConsoleSources& sources);

} // namespace coney::platform
