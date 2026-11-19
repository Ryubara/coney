// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <expected>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "console/command_spec.h"
#include "console/console.h"
#include "console/hosts.h"
#include "core/input_script.h"
#include "core/options.h"
#include "gamemodes/game_mode_stack.h"
#include "platform/game_settings.h"
#include "platform/net_play.h"

namespace coney::platform {

/// What the running game gives the console's session host (docs/guides/console.md#in-the-game): the entry points set
/// it once the game they run exists. Each part may be empty, and the console then acts as if the game had none of it:
/// no need is met, there is no level, no setting, no mission.
struct GameConsoleLinks {
    /// Whether `need` is met now: a game session, an active profile, a level in play, a disc.
    std::function<bool(console::Need need)> has;
    /// Coney's version and build, the disc's region and the renderer (`version`).
    std::function<console::BuildInfo()> buildInfo;
    /// The level in play (`where`); empty text or an empty function: none.
    std::function<std::string()> levelName;
    /// The settings `get`, `set` and `settings` reach, in the order they are listed.
    std::vector<GameSetting> settings;
    /// Writes the run log's ring into the log file and returns the line to show (`bugreport`).
    std::function<std::string()> writeBugReport;
    /// The missions the active profile has completed, and the start of one at a checkpoint: Coney's mission select
    /// (`missions`, `mission`). Stand-in: a guest's `missions` reads the guest's own profile, where the design has it
    /// list the host's, read-only (docs/guides/studio/console.md#coop).
    std::function<std::vector<console::CompletedMission>()> completedMissions;
    std::function<std::expected<std::string, std::string>(int mission, int checkpoint)> startMission;
    /// The Coney features built, which let the commands that wait for them in (`mission-select`).
    std::vector<std::string> builtFeatures;
};

/// The game's console as both entry points set it up (docs/guides/console.md#in-the-game): the console's model with
/// the player catalog's commands (and, from the debug tools' entry point, Developer access for their set), the game's
/// session host and network play's host interface, the history file outside test mode, the input script's `console`
/// lines and the queue run before each simulation step. No front end yet: lines come from the input script.
class GameConsole {
  public:
    /// A console for a run with `options`, taking its network commands to `net` (which must outlive it) and taking
    /// commands up to `highest`. In test mode it keeps no history: `console-history.txt` is neither read nor written.
    /// `links` is what the game gives it, and setLinks() gives it later.
    GameConsole(const Options& options, NetPlay& net, console::Access highest, GameConsoleLinks links = {});

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

    /// Gives the console what the running game offers, in place of what it had; the entry points call it once the
    /// game, the renderer and the sound exist, which come after the console in their set-up.
    void setLinks(GameConsoleLinks links);

    /// Whether `quit` ran: the game ends at the next frame (addHooks() makes `beginFrame` say so).
    [[nodiscard]] bool quitRequested() const;

    /// Runs the queued commands before each step, after what `hooks.beforeStep` already does (network play's
    /// receive), so a step never sees the game change half-way, and ends the run at the frame after `quit` ran, as
    /// closing the window does.
    void addHooks(FrameHooks& hooks);

  private:
    // The game's session, as the console's commands reach it.
    class Session;
    // Network play's role and its host, join and leave, as the console's commands reach them.
    class NetworkRole;
    // console-history.txt in the user's data folder.
    class HistoryFile;

    std::unique_ptr<Session> m_session;
    std::unique_ptr<NetworkRole> m_network;
    std::unique_ptr<HistoryFile> m_history;
    std::unique_ptr<console::Console> m_console;
};

/// Where the console keeps its history: `console-history.txt` in SDL's preference folder for Coney, beside
/// `coney.log` (docs/guides/run-log.md#outputs); nothing when SDL cannot give that folder.
[[nodiscard]] std::optional<std::filesystem::path> consoleHistoryPath();

} // namespace coney::platform
