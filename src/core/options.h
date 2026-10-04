// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/error.h"

namespace coney {

/// Settings taken from the command line.
struct Options {
    /// Stop after this many frames; unset means run until the window closes. Tests and CI use it.
    std::optional<int> frameLimit;
    /// `--help` was given: print usageText() and exit 0.
    bool showHelp = false;
    /// `--disc`: the player's disc, a folder (a mounted disc) or an ISO 9660 image, exactly as typed.
    std::optional<std::string> discPath;
    /// `--load`, in the order given: WAD entries to load through the chunk system, each a name or a `0x` hash.
    /// Requires discPath.
    std::vector<std::string> loads;
};

/// Largest accepted `--frames` value: about 4.6 hours at 60 Hz, far beyond any test, and well inside `int`.
inline constexpr int kMaxFrameLimit = 1'000'000;

/// Parses the arguments after the program name. Never throws; a bad argument gives ErrorCode::InvalidArgument with
/// a one-line message naming the argument.
[[nodiscard]] std::expected<Options, Error> parseOptions(std::span<const std::string_view> args);

/// The usage text printed for `--help` and after an argument error. It ends with a newline. Print it by its size
/// (`%.*s` or fwrite), as with any string_view: callers must not rely on data() being null-terminated.
[[nodiscard]] std::string_view usageText();

} // namespace coney
