// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <expected>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

// @coney-only the console's settings (docs/guides/console.md#settings); the original has no options to change.

namespace coney::platform {

/// One setting the console reads and changes for this session (`get`, `set`, `settings`): the options a player has on
/// the command line and in the options menu, changed live and never written to a file.
struct GameSetting {
    std::string name; ///< As `get` and `set` spell it: lower case, words joined by `_` (`fps_cap`).
    std::string help; ///< One line: what it is and what it takes.
    /// The value now, as `set` takes it back.
    std::function<std::string()> get;
    /// Checks `value` and, when the setting takes it, applies it for this session; returns the value as it now reads,
    /// or what was wrong with `value`.
    std::function<std::expected<std::string, std::string>(std::string_view value)> set;
};

/// A setting that is `on` or `off`.
[[nodiscard]] GameSetting boolSetting(std::string name, std::string help, std::function<bool()> get,
                                      std::function<void(bool)> set);

/// A setting that is a whole number from `min` to `max`.
[[nodiscard]] GameSetting wholeSetting(std::string name, std::string help, int min, int max, std::function<int()> get,
                                       std::function<void(int)> set);

/// A setting that is a number from `min` to `max`, written as plainly as it reads (`0.5`).
[[nodiscard]] GameSetting numberSetting(std::string name, std::string help, double min, double max,
                                        std::function<double()> get, std::function<void(double)> set);

/// A setting that is one of `choices`.
[[nodiscard]] GameSetting choiceSetting(std::string name, std::string help, std::vector<std::string> choices,
                                        std::function<std::string()> get, std::function<void(std::string_view)> set);

} // namespace coney::platform
