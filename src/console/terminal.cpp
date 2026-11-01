// SPDX-License-Identifier: GPL-3.0-or-later
// A stub: the signatures with default results, so the tests compile and fail (the fails-first test commit).
#include "console/terminal.h"

namespace coney::console {

void Terminal::print(std::string_view, LineKind, std::string_view) {}

void Terminal::remember(std::string_view) {}

std::optional<std::string_view> Terminal::previous() { return std::nullopt; }

std::optional<std::string_view> Terminal::next() { return std::nullopt; }

void Terminal::loadHistory(std::string_view) {}

std::string Terminal::historyText() const { return {}; }

} // namespace coney::console
