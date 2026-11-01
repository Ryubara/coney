// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "core/error.h"

// The few pieces of TOML Studio's own small files need (the project manifest, the preferences): trimmed lines, quoted
// strings with their two escapes, and a `line N:` error. Not a TOML parser: each file reads the subset it writes.
// Coney's own; the original has no such files.

namespace coney::studio::toml {

/// `text` without blanks at either end.
inline std::string_view trim(std::string_view text) {
    const auto first = text.find_first_not_of(" \t\r");
    if (first == std::string_view::npos) {
        return {};
    }
    const auto last = text.find_last_not_of(" \t\r");
    return text.substr(first, last - first + 1);
}

/// A quoted value with its escapes (\" and \\) undone, and how many characters of `text` it took, quotes included;
/// nothing when the closing quote is missing.
inline std::optional<std::pair<std::string, std::size_t>> readString(std::string_view text) {
    std::string out;
    for (std::size_t i = 1; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '"') {
            return std::pair{out, i + 1};
        }
        if (c == '\\' && i + 1 < text.size()) {
            out += text[++i];
        } else {
            out += c;
        }
    }
    return std::nullopt;
}

/// `text` as a quoted TOML string, `"` and `\` escaped.
inline std::string quoted(std::string_view text) {
    std::string out = "\"";
    for (const char c : text) {
        if (c == '"' || c == '\\') {
            out += '\\';
        }
        out += c;
    }
    return out + "\"";
}

/// A `line N: ...` failure.
inline std::unexpected<Error> lineError(std::size_t line, std::string_view what) {
    return fail(ErrorCode::Invalid, std::format("line {}: {}", line, what));
}

} // namespace coney::studio::toml
