// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <expected>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "console/command_spec.h"

// @coney-only the Coney console's argument types (docs/guides/console.md#argument-types); the original has none.

namespace coney::console {

/// The values an argument of a game kind (a mission, a checkpoint, a level, a mod, a setting, a command) may take
/// now, given the arguments before it on the line (a checkpoint's mission): the hosts' lists, which the check and the
/// completion read. Nothing when no list is known, and then any well-formed value passes the check and completion
/// offers nothing. Never called for Int, Number, Bool, Text or Choice.
using ValueSource =
    std::function<std::optional<std::vector<std::string>>(ArgType type, std::span<const std::string> earlier)>;

/// Checks `value` against `arg`'s type: a whole number for Int, Mission and Checkpoint (a mission and a checkpoint
/// also at least 1), a decimal for Number, `on` or `off` for Bool, one of its choices for Choice, a name for Level,
/// Mod, Setting and Command, anything for Text; and, for a game kind, one of `values`' list when it gives one. The
/// error is one line naming the argument and what it takes: `"count": expected a whole number, got "two"`.
[[nodiscard]] std::expected<void, std::string>
checkArg(const ArgSpec& arg, std::string_view value, std::span<const std::string> earlier, const ValueSource& values);

/// The values that complete `prefix` for `arg`, sorted and without repeats: `on` and `off` for Bool, the choices for
/// Choice, `values`' list for a game kind; nothing for Int, Number and Text, which have no list.
[[nodiscard]] std::vector<std::string> completeArg(const ArgSpec& arg, std::string_view prefix,
                                                   std::span<const std::string> earlier, const ValueSource& values);

} // namespace coney::console
