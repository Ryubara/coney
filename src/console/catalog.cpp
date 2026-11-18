// SPDX-License-Identifier: GPL-3.0-or-later
#include "console/catalog.h"

#include <algorithm>
#include <format>

namespace coney::console {

const CommandSpec* findSpec(std::span<const CommandSpec> catalog, std::string_view name) {
    const auto found = std::ranges::find(catalog, name, &CommandSpec::name);
    return found == catalog.end() ? nullptr : &*found;
}

std::expected<std::size_t, Error> registerBuiltins(Registry& registry, std::span<const CommandSpec> catalog,
                                                   std::span<const BuiltinHandler> handlers) {
    // A handler with no entry is a command the catalog does not describe: refused before anything registers.
    for (const BuiltinHandler& handler : handlers) {
        if (findSpec(catalog, handler.name) == nullptr) {
            return fail(ErrorCode::InvalidArgument,
                        std::format(R"(the console handler "{}" has no entry in its catalog)", handler.name));
        }
    }
    std::size_t registered = 0;
    for (const CommandSpec& spec : catalog) {
        const auto handler = std::ranges::find(handlers, spec.name, &BuiltinHandler::name);
        if (handler == handlers.end()) {
            continue;
        }
        if (auto added = registry.addBuiltin(spec, handler->handler); !added) {
            return std::unexpected(std::move(added.error()));
        }
        ++registered;
    }
    return registered;
}

} // namespace coney::console
