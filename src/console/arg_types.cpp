// SPDX-License-Identifier: GPL-3.0-or-later
#include "console/arg_types.h"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <format>
#include <system_error>

#include "core/parse_number.h"

namespace coney::console {

namespace {

// Whether `text` is a whole number that fits 64 bits: an optional `-`, then digits only.
bool isWholeNumber(std::string_view text) {
    std::int64_t value = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    return !text.empty() && parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size();
}

// Whether `text` is a whole number of at least 1, as a mission's or a checkpoint's number is.
bool isCount(std::string_view text) { return isWholeNumber(text) && text.front() != '-' && text != "0"; }

// Whether `text` is spelled as a name may be: letters, digits and `-_.:/`, at least one.
bool isName(std::string_view text) {
    return !text.empty() && std::ranges::all_of(text, [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' ||
               c == '.' || c == ':' || c == '/';
    });
}

// Whether `type` is one of the game's kinds, whose values come from a host.
bool isGameKind(ArgType type) {
    return type == ArgType::Mission || type == ArgType::Checkpoint || type == ArgType::Level || type == ArgType::Mod ||
           type == ArgType::Setting || type == ArgType::Command;
}

// The words of a fixed list, joined for a message: `record, status, save`.
template <typename List> std::string joined(const List& list) {
    std::string text;
    for (const auto& item : list) {
        text += text.empty() ? "" : ", ";
        text += item;
    }
    return text;
}

// The well-formedness check of `type`, before any list: what the value must look like.
std::expected<void, std::string> checkShape(const ArgSpec& arg, std::string_view value) {
    const auto refuse = [&](std::string_view expected) {
        return std::unexpected(std::format(R"("{}": expected {}, got "{}")", arg.name, expected, value));
    };
    switch (arg.type) {
    case ArgType::Int:
        return isWholeNumber(value) ? std::expected<void, std::string>() : refuse("a whole number");
    case ArgType::Number:
        return parseDecimal(value) ? std::expected<void, std::string>() : refuse("a number");
    case ArgType::Bool:
        return value == "on" || value == "off" ? std::expected<void, std::string>() : refuse("on or off");
    case ArgType::Text:
        return {};
    case ArgType::Choice:
        if (std::ranges::find(arg.choices, value) != arg.choices.end()) {
            return {};
        }
        return refuse(std::format("one of {}", joined(arg.choices)));
    case ArgType::Mission:
    case ArgType::Checkpoint:
        return isCount(value) ? std::expected<void, std::string>() : refuse("a number from 1");
    case ArgType::Level:
    case ArgType::Mod:
    case ArgType::Setting:
    case ArgType::Command:
        return isName(value) ? std::expected<void, std::string>()
                             : refuse(std::format("a {} name", argTypeName(arg.type)));
    }
    return {};
}

} // namespace

std::expected<void, std::string> checkArg(const ArgSpec& arg, std::string_view value,
                                          std::span<const std::string> earlier, const ValueSource& values) {
    if (auto shape = checkShape(arg, value); !shape) {
        return shape;
    }
    if (!isGameKind(arg.type) || !values) {
        return {};
    }
    const std::optional<std::vector<std::string>> list = values(arg.type, earlier);
    if (!list || std::ranges::find(*list, value) != list->end()) {
        return {};
    }
    if (list->empty()) {
        return std::unexpected(std::format(R"("{}": there is no {} to give now)", arg.name, argTypeName(arg.type)));
    }
    return std::unexpected(std::format(R"("{}": "{}" is not one of {})", arg.name, value, joined(*list)));
}

std::vector<std::string> completeArg(const ArgSpec& arg, std::string_view prefix, std::span<const std::string> earlier,
                                     const ValueSource& values) {
    std::vector<std::string> all;
    if (arg.type == ArgType::Bool) {
        all = {"on", "off"};
    } else if (arg.type == ArgType::Choice) {
        all.assign(arg.choices.begin(), arg.choices.end());
    } else if (isGameKind(arg.type) && values) {
        all = values(arg.type, earlier).value_or(std::vector<std::string>{});
    }
    std::vector<std::string> matches;
    for (std::string& value : all) {
        if (value.starts_with(prefix)) {
            matches.push_back(std::move(value));
        }
    }
    // Numbers in their order (2 before 10), names alphabetically.
    std::ranges::sort(matches, [](const std::string& a, const std::string& b) {
        if (isCount(a) && isCount(b) && a.size() != b.size()) {
            return a.size() < b.size();
        }
        return a < b;
    });
    const auto repeats = std::ranges::unique(matches);
    matches.erase(repeats.begin(), repeats.end());
    return matches;
}

} // namespace coney::console
