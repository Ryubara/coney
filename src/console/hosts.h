// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <expected>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "console/command_spec.h"

// @coney-only the Coney console's host interfaces (docs/guides/console.md#hosts); the original has no console.

namespace coney::console {

// The narrow interfaces through which the console reaches the game (docs/guides/studio/console.md#boundary). The
// console declares them and the game implements them beside what they implement, so a command added later cannot
// reach further without a change here, which review sees. Each grows with the commands that need it (build issue 2
// gave the session's and network play's the player commands' calls).

/// What `version` prints: Coney's version and the build it is, the disc's region and the renderer.
struct BuildInfo {
    std::string version;    ///< Coney's version (`0.1.0`).
    std::string build;      ///< Which build: `release build`, or the debug tools'.
    std::string discRegion; ///< The disc's region (`NTSC-U`); empty when the run has no disc.
    std::string renderer;   ///< The renderer's name (`gl3`, `gpu`).
};

/// One setting the game lets the console read and change for this session (the options a player already has).
struct SettingInfo {
    std::string name;  ///< As `get` and `set` spell it (`fps_cap`).
    std::string value; ///< Its value now, as `set` takes it back.
    std::string help;  ///< One line saying what it is and what it takes.
};

/// A mission the active profile has completed: its number in the story and how many checkpoints it has.
struct CompletedMission {
    int mission = 0;
    int checkpoints = 1;
};

/// The game session's side: what exists now and the lists the game's argument kinds complete from.
class SessionHost {
  public:
    SessionHost() = default;
    SessionHost(const SessionHost&) = delete;
    SessionHost& operator=(const SessionHost&) = delete;
    SessionHost(SessionHost&&) = delete;
    SessionHost& operator=(SessionHost&&) = delete;
    virtual ~SessionHost() = default;

    /// Whether `need` is met now: a game session, an active profile, a level in play, a disc.
    [[nodiscard]] virtual bool has(Need need) const = 0;
    /// The values a Mission, Checkpoint, Level or Setting argument may take now (a checkpoint's of the mission in
    /// `earlier`), or nothing when the game keeps no such list.
    [[nodiscard]] virtual std::optional<std::vector<std::string>>
    values(ArgType type, std::span<const std::string> earlier) const = 0;

    // What the player commands need beyond that. Each has a default that says the game gives nothing, so a host
    // that does not need a call (a test's fake) leaves it out, and a command that needs it says so when it runs.

    /// Coney's version, build, the disc's region and the renderer; nothing when the host does not know.
    [[nodiscard]] virtual std::optional<BuildInfo> buildInfo() const { return std::nullopt; }
    /// The level in play (`where`); nothing between levels, or when the host does not know.
    [[nodiscard]] virtual std::optional<std::string> levelName() const { return std::nullopt; }
    /// The settings, with their values now, in the order `settings` lists them.
    [[nodiscard]] virtual std::vector<SettingInfo> settings() const { return {}; }
    /// Changes the setting `name` to `value` for this session only (nothing is written to a file) and returns its
    /// value as it now reads, or what was wrong with `value`.
    [[nodiscard]] virtual std::expected<std::string, std::string> setSetting(std::string_view name,
                                                                             std::string_view value) {
        (void)value;
        return std::unexpected(std::format(R"(no setting named "{}")", name));
    }
    /// Writes the run log's last 30 s into the log file and returns the line to show (`bugreport`); empty when the
    /// host has no log file of its own, and the command then dumps through the logger.
    [[nodiscard]] virtual std::string writeBugReport() { return {}; }
    /// Asks the game to end through its normal exit, as closing the window does (`quit`); false when this run cannot.
    virtual bool requestQuit() { return false; }
    /// The missions the active profile has completed, in the story's order; empty without a profile.
    [[nodiscard]] virtual std::vector<CompletedMission> completedMissions() const { return {}; }
    /// Starts a completed mission at a checkpoint, as the mission select starts it, and returns the line to show.
    [[nodiscard]] virtual std::expected<std::string, std::string> startMission(int mission, int checkpoint) {
        (void)mission;
        (void)checkpoint;
        return std::unexpected(std::string("this game cannot start a mission from the console yet"));
    }
};

/// The mods loader's side.
class ModsHost {
  public:
    ModsHost() = default;
    ModsHost(const ModsHost&) = delete;
    ModsHost& operator=(const ModsHost&) = delete;
    ModsHost(ModsHost&&) = delete;
    ModsHost& operator=(ModsHost&&) = delete;
    virtual ~ModsHost() = default;

    /// The installed mods' names, which a Mod argument completes from.
    [[nodiscard]] virtual std::vector<std::string> installedMods() const = 0;
};

/// What this machine is in a network session, which decides who may run a session command
/// (docs/guides/studio/console.md#access).
enum class SessionRole : std::uint8_t {
    Offline, ///< No network session: the one player may run everything.
    Host,    ///< Hosting: the host's line changes the session.
    Guest,   ///< Joined to a host: a session command is refused here, before anything is sent.
};

/// Network play's side.
class NetworkHost {
  public:
    NetworkHost() = default;
    NetworkHost(const NetworkHost&) = delete;
    NetworkHost& operator=(const NetworkHost&) = delete;
    NetworkHost(NetworkHost&&) = delete;
    NetworkHost& operator=(NetworkHost&&) = delete;
    virtual ~NetworkHost() = default;

    /// This machine's role now.
    [[nodiscard]] virtual SessionRole role() const = 0;

    /// Hosts a game on `port` (the default port when empty); returns the line to show, or why it cannot.
    [[nodiscard]] virtual std::expected<std::string, std::string> host(std::optional<int> port) {
        (void)port;
        return std::unexpected(std::string("network play is not available in this run"));
    }
    /// Joins the game at `address` (`host` or `host:port`); returns the line to show, or why it cannot.
    [[nodiscard]] virtual std::expected<std::string, std::string> join(std::string_view address) {
        (void)address;
        return std::unexpected(std::string("network play is not available in this run"));
    }
    /// Leaves the session: a guest as the Player 2 quit does, the host by ending it. False when there was none.
    virtual bool leave() { return false; }
};

/// Where the history is kept across runs: `console-history.txt` in the user's data folder. A test-mode console never
/// calls it (docs/guides/console.md#test-mode).
class HistoryStore {
  public:
    HistoryStore() = default;
    HistoryStore(const HistoryStore&) = delete;
    HistoryStore& operator=(const HistoryStore&) = delete;
    HistoryStore(HistoryStore&&) = delete;
    HistoryStore& operator=(HistoryStore&&) = delete;
    virtual ~HistoryStore() = default;

    /// The kept history, or nothing when there is none.
    [[nodiscard]] virtual std::optional<std::string> load() = 0;
    /// Keeps `text` in place of the history kept before.
    virtual void save(std::string_view text) = 0;
};

} // namespace coney::console
