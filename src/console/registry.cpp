// SPDX-License-Identifier: GPL-3.0-or-later
// A stub: the signatures with default results, so the tests compile and fail (the fails-first test commit).
#include "console/registry.h"

namespace coney::console {

CommandArgs::CommandArgs(std::span<const ArgSpec>, std::vector<std::string> values) : m_values(std::move(values)) {}

std::string_view CommandArgs::get(std::string_view) const { return {}; }

std::expected<CommandId, Error> Registry::addBuiltin(const CommandSpec&, Handler) {
    return fail(ErrorCode::InvalidArgument, "not implemented");
}

std::expected<CommandId, Error> Registry::addModCommand(std::string_view, const CommandSpec&, Handler) {
    return fail(ErrorCode::InvalidArgument, "not implemented");
}

std::size_t Registry::removeMod(std::string_view) { return 0; }

std::expected<CommandId, Error> Registry::resolve(std::string_view) const {
    return fail(ErrorCode::NotFound, "not implemented");
}

const Command* Registry::find(CommandId) const { return nullptr; }

std::vector<std::string> Registry::names() const { return {}; }

std::vector<const Command*> Registry::commands() const { return {}; }

} // namespace coney::console
