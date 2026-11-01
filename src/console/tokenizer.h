// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

#include "core/error.h"

// @coney-only the Coney console's line reader (docs/guides/console.md#lines); the original has no console.

namespace coney::console {

/// The longest line the terminal takes, in bytes; a longer one is refused, never cut.
inline constexpr std::size_t kMaxLineBytes = 512;

/// A line split into its words, as completion reads it: it also says whether the line ends in a separator (so the
/// next word is a new, empty one) and whether a quote is still open.
struct SplitLine {
    std::vector<std::string> words;
    bool trailingSpace = false; ///< The last character is a separator outside quotes.
    bool openQuote = false;     ///< A `"` was opened and not closed.
};

/// Splits a console line into its command name and arguments (docs/guides/console.md#lines): words separated by
/// spaces or tabs; a word in double quotes may hold spaces, and `\"` inside one is a quote. One command per line and
/// nothing is evaluated: `;` is text like any other. An empty or blank line gives no words. Fails with
/// ErrorCode::InvalidArgument for a line over kMaxLineBytes or a quote left open.
[[nodiscard]] std::expected<std::vector<std::string>, Error> tokenize(std::string_view line);

/// Splits a line as tokenize() does but never fails: an open quote runs to the end of the line, and the result says
/// so. For completion, which reads a line while it is being typed.
[[nodiscard]] SplitLine splitForCompletion(std::string_view line);

/// `word` as a line would spell it: unchanged when it has no space, tab or quote and is not empty, else in double
/// quotes with each `"` written `\"`. Completion writes candidates back into the line with it.
[[nodiscard]] std::string quoteWord(std::string_view word);

} // namespace coney::console
