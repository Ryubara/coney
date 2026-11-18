// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "console/console.h"
#include "console/hosts.h"
#include "core/input_script.h"
#include "core/options.h"
#include "gamemodes/game_mode_stack.h"
#include "platform/net_play.h"

namespace coney::platform {

/// The game's console as both entry points set it up (docs/guides/console.md#in-the-game): the console's model with
/// the player catalog's commands (and, from the debug tools' entry point, Developer access for their set), network
/// play's host interface, the history file outside test mode, the input script's `console` lines and the queue run
/// before each simulation step. No front end yet: lines come from the input script.
class GameConsole {
  public:
    /// A console for a run with `options`, reading network play's role from `net` (which must outlive it) and taking
    /// commands up to `highest`. In test mode it keeps no history: `console-history.txt` is neither read nor written.
    GameConsole(const Options& options, const NetPlay& net, console::Access highest);

    GameConsole(const GameConsole&) = delete;
    GameConsole& operator=(const GameConsole&) = delete;
    GameConsole(GameConsole&&) = delete;
    GameConsole& operator=(GameConsole&&) = delete;
    ~GameConsole();

    /// The model.
    [[nodiscard]] console::Console& console() { return *m_console; }

    /// Hands `script`'s `console` lines to the console, each for the player of its port. The console must outlive the
    /// script's sampling, as it does when both are the entry point's.
    void listenTo(ScriptedInput& script);

    /// Runs the queued commands before each step, after what `hooks.beforeStep` already does (network play's
    /// receive), so a step never sees the game change half-way.
    void addHooks(FrameHooks& hooks);

  private:
    // Network play's role, as the console's scope rules read it.
    class NetworkRole;
    // console-history.txt in the user's data folder.
    class HistoryFile;

    std::unique_ptr<NetworkRole> m_network;
    std::unique_ptr<HistoryFile> m_history;
    std::unique_ptr<console::Console> m_console;
};

/// Where the console keeps its history: `console-history.txt` in SDL's preference folder for Coney, beside
/// `coney.log` (docs/guides/run-log.md#outputs); nothing when SDL cannot give that folder.
[[nodiscard]] std::optional<std::filesystem::path> consoleHistoryPath();

} // namespace coney::platform
