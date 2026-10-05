// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <span>
#include <string_view>

namespace coney::debug {

/// How an argument is edited and passed: the masterlist's Lua type, refined.
enum class NativeArgType : std::uint8_t {
    Number,      ///< A real number (`lua: number`, a float or a double in C).
    Integer,     ///< A whole number (`ctype: int` or `unsigned`).
    Handle,      ///< A number that names an object (a human, a gang, a camera): its description says handle.
    Boolean,     ///< true or false; Lua 4.0 passes true as 1 and false as nil.
    String,      ///< A string (a name, a callback's name).
    NumberTable, ///< A table of numbers (`elem: number`), `count` of them when fixed; the binding may write into it.
    StringTable, ///< A table of strings.
    Userdata,    ///< A tolua object; the debug menus pass nil.
};

/// One argument of a binding.
struct NativeArg {
    std::string_view name;         ///< The masterlist's name for it.
    NativeArgType type;            ///< How it is edited and passed.
    std::string_view defaultValue; ///< The value it takes when left off, as text (`true`, `1`); empty for none.
    std::uint16_t count;           ///< For a table: its fixed element count; 0 when not fixed.
};

/// What a result is.
enum class NativeResultType : std::uint8_t { Number, Boolean, String, Usertype };

/// One binding's signature: what the debug menus need to build an argument editor and call it. The table is
/// generated from the masterlist (research/bindings/*.yaml) by `coney-tools natives cpp`
/// (docs/guides/coney-tools.md#natives); a binding registered twice keeps the registration Lua uses (`main`).
struct NativeSignature {
    std::string_view name;                     ///< The Lua global's name.
    std::string_view category;                 ///< The masterlist category (`ai`, `hud`, ...).
    std::span<const NativeArg> args;           ///< In the order Lua passes them.
    std::span<const NativeResultType> results; ///< What the binding pushes back.
    std::uint8_t overloads;                    ///< Earlier registrations of the same name (usually 0).
};

/// Every binding of the masterlist, by category (in the reference's page order) and by name within a category.
[[nodiscard]] std::span<const NativeSignature> nativeSignatures();

/// The signature of the binding `name`, or null.
[[nodiscard]] const NativeSignature* findNativeSignature(std::string_view name);

/// The name of an argument type, for the menus (`number`, `handle`).
[[nodiscard]] std::string_view nativeArgTypeName(NativeArgType type);

} // namespace coney::debug
