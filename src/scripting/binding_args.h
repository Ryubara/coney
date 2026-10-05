// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <expected>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "core/error.h"
#include "scripting/lua_value.h"
#include "scripting/lua_vm.h"

// The tolua argument and result conventions the bindings share (docs/research/scripting.md#argument-and-result-
// conventions): how a binding reads its arguments and pushes its results. Coney's own helpers; header-only.

namespace coney::script::binding {

/// What a binding returns: its results, or an error that stops the script.
using Results = std::expected<std::vector<Value>, Error>;

/// Argument `i` (0-based) as a number: 0 when absent or not convertible, as tolua reads a missing argument.
[[nodiscard]] inline double number(std::span<const Value> args, std::size_t i) {
    if (i >= args.size()) {
        return 0.0;
    }
    if (const std::optional<double> value = args[i].number()) {
        return *value;
    }
    if (const std::optional<std::string_view> text = args[i].string()) {
        return parseLuaNumber(*text).value_or(0.0);
    }
    return 0.0;
}

/// Argument `i` as a string: a string, a number's text, or empty (tolua's NULL) for anything else.
[[nodiscard]] inline std::string string(std::span<const Value> args, std::size_t i) {
    if (i >= args.size()) {
        return {};
    }
    if (const std::optional<std::string_view> text = args[i].string()) {
        return std::string(*text);
    }
    if (const std::optional<double> value = args[i].number()) {
        return std::format("{:.16g}", *value);
    }
    return {};
}

/// Argument `i` as a position: a table of three numbers at t[1]..t[3]; nothing for anything else.
[[nodiscard]] inline std::optional<std::array<float, 3>> position(std::span<const Value> args, std::size_t i) {
    if (i >= args.size() || args[i].table() == nullptr) {
        return std::nullopt;
    }
    const Table& table = *args[i].table();
    std::array<float, 3> position{};
    for (std::size_t axis = 0; axis < position.size(); ++axis) {
        const std::optional<double> value = table.get(Value(static_cast<double>(axis + 1))).number();
        if (!value) {
            return std::nullopt;
        }
        position.at(axis) = static_cast<float>(*value);
    }
    return position;
}

/// No results.
[[nodiscard]] inline Results none() { return std::vector<Value>{}; }
/// One number.
[[nodiscard]] inline Results number(double value) { return std::vector<Value>{Value(value)}; }
/// A boolean as tolua pushes it: the number 1 for true, nil for false (Lua 4.0 has no booleans).
[[nodiscard]] inline Results boolean(bool value) { return std::vector<Value>{value ? Value(1.0) : Value()}; }

} // namespace coney::script::binding
