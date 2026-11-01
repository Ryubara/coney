// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "console/command_queue.h"
#include "console/hosts.h"
#include "console/registry.h"
#include "console/terminal.h"

// @coney-only the Coney console (docs/guides/console.md); the original has no console.

namespace coney::console {

/// Where a line comes from.
enum class LineSource : std::uint8_t {
    Typed,  ///< The front end: a player typed it or built it with the pad picker.
    Script, ///< The input script's `console` action (docs/guides/running-input.md#input-scripts).
};

/// What became of a submitted line.
enum class Outcome : std::uint8_t {
    Ignored, ///< Nothing to do: a blank line, or a typed one in test mode.
    Refused, ///< Not run: the terminal says why.
    Ran,     ///< Run at once (a local command that only reads).
    Queued,  ///< Checked and waiting for the step boundary.
};

/// How a console is set up.
struct ConsoleSettings {
    /// A test-mode run: lines come only from the input script, and no history is read or written
    /// (docs/guides/console.md#test-mode).
    bool testMode = false;
    /// The highest access level its registry takes: Developer in a build with the debug tools.
    Access highest = Access::Player;
};

/// The hosts a console reaches the game through; each may be null (no game session, no mods loader, no network
/// play), and the console then acts as if nothing it would ask for exists: no need is met, no list is known, and the
/// game is offline.
struct ConsoleHosts {
    const SessionHost* session = nullptr;
    const ModsHost* mods = nullptr;
    const NetworkHost* network = nullptr;
    HistoryStore* history = nullptr;
};

/// The console's model: a line in, checked and run now or at the step boundary, its output in the terminal and the
/// run log (docs/guides/console.md). The front end and the input script feed it lines; the platform calls
/// runQueued() between two simulation steps.
class Console {
  public:
    /// A console with an empty registry; outside test mode it loads the history from `hosts.history`.
    Console(ConsoleSettings settings, ConsoleHosts hosts);

    /// Where the built-ins and the mods' commands are registered.
    [[nodiscard]] Registry& registry() { return m_registry; }
    [[nodiscard]] const Registry& registry() const { return m_registry; }
    /// The output and the history.
    [[nodiscard]] Terminal& terminal() { return m_terminal; }
    [[nodiscard]] const Terminal& terminal() const { return m_terminal; }
    /// How many commands wait for the step boundary.
    [[nodiscard]] std::size_t queued() const { return m_queue.size(); }

    /// Takes a line for `slot`'s player: echoes it, checks its name, the scope against this machine's role, its needs
    /// and arguments, then runs it at once or queues it. A typed line also goes into the history, saved at once
    /// outside test mode. In test mode a typed line is ignored: lines come only from the input script.
    Outcome submit(std::string_view line, LineSource source, PlayerSlot slot = 0);

    /// Runs the queued commands in the order they were entered: the step boundary. Returns how many ran (a command
    /// removed since it was queued is reported and does not run).
    std::size_t runQueued();

    /// The completions of the word being typed at the end of `line`: command names for the first word, then the
    /// values of the argument it stands for, each as the whole line would read with it.
    [[nodiscard]] std::vector<std::string> complete(std::string_view line) const;

  private:
    // Prints an error line, and to the run log.
    void refuse(std::string_view text);
    // Prints a command's output line, and to the run log.
    void output(std::string_view text, std::string_view source);
    // Runs a checked command now.
    void run(CommandId id, std::vector<std::string> args, PlayerSlot slot);
    // The values a game kind's argument may take, from the hosts and the registry.
    [[nodiscard]] std::optional<std::vector<std::string>> values(ArgType type,
                                                                 std::span<const std::string> earlier) const;

    ConsoleSettings m_settings;
    ConsoleHosts m_hosts;
    Registry m_registry;
    Terminal m_terminal;
    CommandQueue m_queue;
};

/// A command's usage line: `mission <mission> [checkpoint]`.
[[nodiscard]] std::string usageLine(const CommandSpec& spec, std::string_view name);

} // namespace coney::console
