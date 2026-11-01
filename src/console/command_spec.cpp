// SPDX-License-Identifier: GPL-3.0-or-later
// A stub: the signatures with default results, so the tests compile and fail (the fails-first test commit).
#include "console/command_spec.h"

namespace coney::console {

std::string_view accessName(Access) { return {}; }

std::string_view scopeName(Scope) { return {}; }

std::string_view argTypeName(ArgType) { return {}; }

std::string_view needName(Need) { return {}; }

bool isCommandName(std::string_view, std::size_t) { return false; }

} // namespace coney::console
