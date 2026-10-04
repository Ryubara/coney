// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include "core/error.h"
#include "scripting/lua_chunk.h"

namespace coney::script {

class Table;
struct Function;

/// A Lua 4.0 value: nil, a number, a string, a table or a function. Tables and functions are shared by reference, as
/// in Lua; numbers and strings are copied.
class Value {
  public:
    /// What a value holds.
    enum class Type : std::uint8_t { Nil, Number, String, Table, Function };

    /// nil.
    Value() = default;
    /// A number.
    // NOLINTNEXTLINE(google-explicit-constructor, hicpp-explicit-conversions): a number is a value, as in Lua
    Value(double number) : m_value(number) {}
    /// A string (raw bytes).
    // NOLINTNEXTLINE(google-explicit-constructor, hicpp-explicit-conversions): a string is a value, as in Lua
    Value(std::string text) : m_value(std::move(text)) {}
    /// A table; null makes nil.
    // NOLINTNEXTLINE(google-explicit-constructor, hicpp-explicit-conversions): a table is a value, as in Lua
    Value(std::shared_ptr<Table> table);
    /// A function; null makes nil.
    // NOLINTNEXTLINE(google-explicit-constructor, hicpp-explicit-conversions): a function is a value, as in Lua
    Value(std::shared_ptr<const Function> function);

    /// What the value holds.
    [[nodiscard]] Type type() const { return static_cast<Type>(m_value.index()); }
    /// True for nil.
    [[nodiscard]] bool isNil() const { return type() == Type::Nil; }
    /// The number, or nothing for another type.
    [[nodiscard]] std::optional<double> number() const;
    /// The string, or nothing for another type. The view lives as long as this value.
    [[nodiscard]] std::optional<std::string_view> string() const;
    /// The table, or null for another type.
    [[nodiscard]] const std::shared_ptr<Table>& table() const;
    /// The function, or null for another type.
    [[nodiscard]] const std::shared_ptr<const Function>& function() const;

    /// Lua's raw equality: same type, and equal numbers or strings, or the same table or function.
    friend bool operator==(const Value& a, const Value& b) { return a.m_value == b.m_value; }

  private:
    std::variant<std::monostate, double, std::string, std::shared_ptr<Table>, std::shared_ptr<const Function>> m_value;
};

/// A function a script can call that is written in C++: a binding such as `CfgHUDMessage`. It receives the call's
/// arguments and returns its results, or an error that stops the script.
using NativeFunction = std::function<std::expected<std::vector<Value>, Error>(std::span<const Value> args)>;

/// A Lua function value: a native binding, or a Lua function (a prototype of a loaded chunk with its upvalues, which
/// Lua 4.0 copies when the closure is made).
struct Function {
    NativeFunction native;                 ///< Set for a binding.
    std::shared_ptr<const LuaProto> chunk; ///< Keeps the chunk that holds `proto` alive.
    const LuaProto* proto = nullptr;       ///< Set for a Lua function.
    std::vector<Value> upvalues;           ///< Lua 4.0 upvalues: values frozen when the closure was made.
};

/// A Lua table without tag methods. Iteration (next()) visits keys in the order they were first set, which keeps a
/// script's `for k, v in t` deterministic: Coney's choice; Lua 4.0 visits them in its hash order, which nothing may
/// rely on.
class Table {
  public:
    /// The value at `key`; nil when absent (including a nil key).
    [[nodiscard]] Value get(const Value& key) const;
    /// Sets `key` to `value` (nil removes it from iteration). Fails with ErrorCode::Invalid for a nil key or a NaN.
    [[nodiscard]] std::expected<void, Error> set(const Value& key, Value value);
    /// The value at the string key `key`: a field, as `t.key` reads it.
    [[nodiscard]] Value field(std::string_view key) const { return get(Value(std::string(key))); }

    /// The entry after `key` with a non-nil value, or the first one for a nil key; nothing at the end, or when `key`
    /// was never set. Lua's `next`.
    [[nodiscard]] std::optional<std::pair<Value, Value>> next(const Value& key) const;

    /// Entries with a non-nil value.
    [[nodiscard]] std::size_t size() const;

  private:
    // What a key is compared and hashed by: the number (with -0 as 0), the string, or the object's address.
    using Key = std::variant<double, std::string, const void*>;
    static std::optional<Key> keyOf(const Value& key);

    std::vector<std::pair<Value, Value>> m_entries; // in the order keys were first set
    std::unordered_map<Key, std::size_t> m_index;   // key -> position in m_entries
};

} // namespace coney::script
