// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

// Play in the prototype: *Run in Coney*, the technology design's child-process path
// (docs/guides/studio/technology.md#play-session, build issue 11), until Play runs in Studio's own process. Studio
// starts `coney` on the open level: played as Rembrandt from a spawn point when a disc is known, or flown round with
// the sandbox's free camera without one. Coney's own; the original has no editor.

namespace coney::studio {

/// What Run in Coney is asked to start.
struct RunRequest {
    std::filesystem::path coney;        ///< The `coney` executable.
    std::filesystem::path level;        ///< The level's layout file.
    std::optional<std::string> disc{};  ///< The player's disc (`--disc`); without one the level is flown round.
    std::optional<std::string> spawn{}; ///< The spawn point to start at; the level's first when unset.
    std::optional<std::filesystem::path> assets{}; ///< Coney's own assets folder (`--assets`), when not beside `coney`.
};

/// The command line for `request`, the program first: `coney --disc D --play-level sandbox:<level> [--spawn S]`, or
/// `coney --sandbox <level>` without a disc (a spawn needs a disc and is left out).
[[nodiscard]] std::vector<std::string> runInConeyCommand(const RunRequest& request);

/// The path of `coney` beside the executable `studio` (`coney.exe` on Windows, where `studio` ends in `.exe`).
[[nodiscard]] std::filesystem::path coneyBeside(const std::filesystem::path& studio);

} // namespace coney::studio
