// SPDX-License-Identifier: GPL-3.0-or-later
// Stub for the fails-first run of the Entity editor's tests: it compiles and returns defaults, and implements nothing.
#include "debug/entity_edit.h"

namespace coney::debug {

std::string_view editOpName(EditOp /*op*/) { return {}; }

std::optional<EditOp> editOpFromName(std::string_view /*name*/) { return std::nullopt; }

std::size_t editOpArity(EditOp /*op*/) { return 0; }

std::string commandText(const EntityCommand& /*command*/) { return {}; }

std::expected<EntityCommand, Error> parseCommand(std::string_view /*text*/) { return fail(ErrorCode::Invalid, "stub"); }

std::vector<EntityCommand> appliedEdits(std::span<const std::string> /*lines*/) { return {}; }

void EntityEditor::write(std::string /*line*/) {}

void EntityEditor::refuse(const EntityCommand& /*command*/, std::string_view /*reason*/) {}

bool EntityEditor::queue(const EntityCommand& /*command*/) { return false; }

bool EntityEditor::queueReset(EntityId /*entity*/, EditOp /*op*/, const EntityEditTarget& /*target*/) { return false; }

std::size_t EntityEditor::applyPending(EntityEditTarget* /*target*/) { return 0; }

std::optional<EntityCommand> EntityEditor::queuedCommand(EntityId /*entity*/, EditOp /*op*/) const {
    return std::nullopt;
}

std::optional<double> EntityEditor::queuedValue(EntityId /*entity*/, EditOp /*op*/) const { return std::nullopt; }

} // namespace coney::debug
