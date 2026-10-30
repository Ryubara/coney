// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <format>
#include <limits>
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
/// @coney-only tolua's argument reading and result pushing, which the original's generated wrappers do
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
/// @coney-only tolua's argument reading and result pushing, which the original's generated wrappers do
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

/// Whether argument `i` is missing or nil. Only for an argument that reads the same either way (a string or a
/// callback, tolua's NULL) or that a binding tests for a type itself; a number or boolean with a default reads an
/// explicit nil as 0, so it goes through numberOr() or booleanArg() (docs/research/scripting.md#optional-arguments).
/// @coney-only tolua's argument reading and result pushing, which the original's generated wrappers do
[[nodiscard]] inline bool absent(std::span<const Value> args, std::size_t i) {
    return i >= args.size() || args[i].isNil();
}

/// Argument `i` as a number with a default: `fallback` only when the script left it out (past the top of the
/// stack); anything else is converted as number() does, so an explicit nil reads 0.
/// The tolua reader at 0x004091c8 (middleware the original links, so no @orig tag).
/// @coney-only tolua's argument reading
[[nodiscard]] inline double numberOr(std::span<const Value> args, std::size_t i, double fallback) {
    // The top of the stack is the argument count: an index within it is converted whatever its type.
    if (i >= args.size()) {
        return fallback;
    }
    return number(args, i);
}

/// A number as the original converts it to an unsigned 32-bit integer: NaN, negative numbers and anything under 1
/// give 0, 2^32 and above (and +infinity) 0xffffffff, the rest is truncated.
/// The soft-float conversion at 0x0042c420 (middleware the original links, so no @orig tag).
/// @coney-only the compiler runtime's conversion
[[nodiscard]] inline std::uint32_t toUnsigned(double value) {
    // NaN, a zero and a negative number all read 0 (the original tests the class, then the sign).
    if (std::isnan(value) || value <= 0.0 || std::signbit(value)) {
        return 0;
    }
    // At or past 2^32 it saturates; +infinity is past it.
    constexpr double kTwoTo32 = 4294967296.0;
    if (value >= kTwoTo32) {
        return std::numeric_limits<std::uint32_t>::max();
    }
    // Under 1 the exponent is negative and the result 0, which truncation gives too.
    return static_cast<std::uint32_t>(std::trunc(value));
}

/// A number as the original converts it to a signed 32-bit integer: NaN and anything within 1 of 0 give 0, the rest
/// is truncated toward 0 and saturates at the int range (infinity too).
/// The soft-float conversion at 0x0042c388 (middleware the original links, so no @orig tag).
/// @coney-only the compiler runtime's conversion
[[nodiscard]] inline std::int32_t toSigned(double value) {
    // NaN and a zero read 0.
    if (std::isnan(value) || value == 0.0) {
        return 0;
    }
    // A magnitude of 2^31 or more saturates by its sign, so -2^31 itself gives the minimum too.
    constexpr double kTwoTo31 = 2147483648.0;
    if (std::fabs(value) >= kTwoTo31) {
        return value < 0.0 ? std::numeric_limits<std::int32_t>::min() : std::numeric_limits<std::int32_t>::max();
    }
    return static_cast<std::int32_t>(std::trunc(value));
}

/// Argument `i` as tolua reads an unsigned integer with a default: numberOr(), then toUnsigned().
/// @coney-only tolua's argument reading and result pushing, which the original's generated wrappers do
[[nodiscard]] inline std::uint32_t unsignedArg(std::span<const Value> args, std::size_t i, double fallback = 0.0) {
    return toUnsigned(numberOr(args, i, fallback));
}

/// Argument `i` as a handle: unsignedArg() with no default, as the double the hosts take.
/// @coney-only tolua's argument reading and result pushing, which the original's generated wrappers do
[[nodiscard]] inline double handleArg(std::span<const Value> args, std::size_t i) {
    return static_cast<double>(unsignedArg(args, i));
}

/// Argument `i` as tolua reads a signed integer with a default: numberOr(), then toSigned().
/// @coney-only tolua's argument reading and result pushing, which the original's generated wrappers do
[[nodiscard]] inline std::int32_t signedArg(std::span<const Value> args, std::size_t i, double fallback = 0.0) {
    return toSigned(numberOr(args, i, fallback));
}

/// Argument `i` as tolua reads a float with a default: numberOr(), narrowed to a float.
/// @coney-only tolua's argument reading and result pushing, which the original's generated wrappers do
[[nodiscard]] inline float floatArg(std::span<const Value> args, std::size_t i, double fallback = 0.0) {
    return static_cast<float>(numberOr(args, i, fallback));
}

/// Argument `i` as a boolean with a default: `fallback` only when the script left it out, false for an explicit nil
/// (Lua 4's false), else whether the number (number()'s conversion) is not 0.
/// The tolua reader at 0x00409318 (middleware the original links, so no @orig tag).
/// @coney-only tolua's argument reading
[[nodiscard]] inline bool booleanArg(std::span<const Value> args, std::size_t i, bool fallback = false) {
    // Past the top of the stack: the default.
    if (i >= args.size()) {
        return fallback;
    }
    // A nil is false; anything else is converted and compared with 0.
    if (args[i].isNil()) {
        return false;
    }
    return number(args, i) != 0.0;
}

/// Argument `i` as a position: a table of three numbers at t[1]..t[3]; nothing for anything else.
/// @coney-only tolua's argument reading and result pushing, which the original's generated wrappers do
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
/// @coney-only tolua's argument reading and result pushing, which the original's generated wrappers do
[[nodiscard]] inline Results none() { return std::vector<Value>{}; }
/// One number.
/// @coney-only tolua's argument reading and result pushing, which the original's generated wrappers do
[[nodiscard]] inline Results number(double value) { return std::vector<Value>{Value(value)}; }
/// A boolean as tolua pushes it: the number 1 for true, nil for false (Lua 4.0 has no booleans).
/// @coney-only tolua's argument reading and result pushing, which the original's generated wrappers do
[[nodiscard]] inline Results boolean(bool value) { return std::vector<Value>{value ? Value(1.0) : Value()}; }

} // namespace coney::script::binding
