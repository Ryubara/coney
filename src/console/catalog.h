// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <expected>
#include <span>
#include <string_view>

#include "console/command_spec.h"
#include "console/registry.h"
#include "core/error.h"

// @coney-only the Coney console's command catalog (docs/guides/console.md#catalog); the original has no console.

namespace coney::console {

/// The player set: every entry of `src/console/commands.toml`, in the file's order, as `coney-tools console
/// render-cpp` writes it into commands_table.cpp. Compiled into the game library, so every build has it.
[[nodiscard]] std::span<const CommandSpec> playerCatalog();

/// The spec named `name` in `catalog`, or null.
[[nodiscard]] const CommandSpec* findSpec(std::span<const CommandSpec> catalog, std::string_view name);

/// A built-in command's code, bound to its catalog entry by name.
struct BuiltinHandler {
    std::string_view name;
    Handler handler;
};

/// Registers each entry of `catalog` that has a handler in `handlers` and returns how many it registered; an entry
/// with no handler yet stays out of the registry (unknown when typed). Fails with ErrorCode::InvalidArgument when a
/// handler names no entry, or as Registry::addBuiltin() does.
[[nodiscard]] std::expected<std::size_t, Error>
registerBuiltins(Registry& registry, std::span<const CommandSpec> catalog, std::span<const BuiltinHandler> handlers);

} // namespace coney::console
