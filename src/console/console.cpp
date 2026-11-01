// SPDX-License-Identifier: GPL-3.0-or-later
// A stub: the signatures with default results, so the tests compile and fail (the fails-first test commit).
#include "console/console.h"

namespace coney::console {

std::string usageLine(const CommandSpec&, std::string_view) { return {}; }

Console::Console(ConsoleSettings settings, ConsoleHosts hosts)
    : m_settings(settings), m_hosts(hosts), m_registry(settings.highest) {}

Outcome Console::submit(std::string_view, LineSource, PlayerSlot) { return Outcome::Queued; }

std::size_t Console::runQueued() { return 0; }

std::vector<std::string> Console::complete(std::string_view) const { return {}; }

void Console::refuse(std::string_view) {}

void Console::output(std::string_view, std::string_view) {}

void Console::run(CommandId, std::vector<std::string>, PlayerSlot) {}

std::optional<std::vector<std::string>> Console::values(ArgType, std::span<const std::string>) const {
    return std::nullopt;
}

} // namespace coney::console
