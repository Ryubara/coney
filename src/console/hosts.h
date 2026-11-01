// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
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
// gives the session's the player commands' calls).

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
