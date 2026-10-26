// SPDX-License-Identifier: GPL-3.0-or-later
#include "debug/entity_edit.h"

#include <algorithm>
#include <charconv>
#include <format>
#include <utility>

#include "core/parse_number.h"

namespace coney::debug {

namespace {

// One op's name and how many numbers it takes, indexed by EditOp.
struct OpInfo {
    std::string_view name;
    std::size_t arity;
};
constexpr std::array<OpInfo, kEditOpCount> kOps{{
    {"teleport", 3},
    {"heading", 1},
    {"drop-to-ground", 0},
    {"health", 1},
    {"stamina", 1},
    {"power", 1},
    {"rage", 1},
    {"invincible", 1},
    {"kill", 0},
    {"scale", 1},
    {"scale-collision", 1},
    {"anim-rate", 1},
    {"speed-scale", 1},
    {"frozen", 1},
    {"delete", 0},
}};

// The prefix every line of the editor carries, which appliedEdits() looks for.
constexpr std::string_view kApplied = "entity edit: applied ";

// The words of `text`, split at spaces.
std::vector<std::string_view> words(std::string_view text) {
    std::vector<std::string_view> list;
    while (!text.empty()) {
        const std::size_t start = text.find_first_not_of(' ');
        if (start == std::string_view::npos) {
            break;
        }
        text.remove_prefix(start);
        const std::size_t end = text.find(' ');
        list.push_back(text.substr(0, end));
        text.remove_prefix(end == std::string_view::npos ? text.size() : end);
    }
    return list;
}

} // namespace

std::string_view editOpName(EditOp op) { return kOps.at(static_cast<std::size_t>(op)).name; }

std::optional<EditOp> editOpFromName(std::string_view name) {
    const auto found = std::ranges::find(kOps, name, &OpInfo::name);
    return found == kOps.end() ? std::nullopt
                               : std::optional<EditOp>(static_cast<EditOp>(std::distance(kOps.begin(), found)));
}

std::size_t editOpArity(EditOp op) { return kOps.at(static_cast<std::size_t>(op)).arity; }

std::string commandText(const EntityCommand& command) {
    std::string text = std::format("{} {}", command.entity, editOpName(command.op));
    for (std::size_t i = 0; i < editOpArity(command.op); ++i) {
        text += std::format(" {}", command.values.at(i));
    }
    return text;
}

std::expected<EntityCommand, Error> parseCommand(std::string_view text) {
    const std::vector<std::string_view> parts = words(text);
    if (parts.size() < 2) {
        return fail(ErrorCode::Invalid, std::format("entity edit: '{}' is not '<entity> <op> <numbers>'", text));
    }
    EntityCommand command;
    const auto [end, error] = std::from_chars(parts[0].data(), parts[0].data() + parts[0].size(), command.entity);
    if (error != std::errc() || end != parts[0].data() + parts[0].size()) {
        return fail(ErrorCode::Invalid, std::format("entity edit: '{}' is not an entity id", parts[0]));
    }
    const std::optional<EditOp> op = editOpFromName(parts[1]);
    if (!op) {
        return fail(ErrorCode::Invalid, std::format("entity edit: '{}' is not an edit", parts[1]));
    }
    command.op = *op;
    if (parts.size() - 2 != editOpArity(*op)) {
        return fail(ErrorCode::Invalid, std::format("entity edit: {} takes {} numbers, not {}", parts[1],
                                                    editOpArity(*op), parts.size() - 2));
    }
    for (std::size_t i = 0; i < editOpArity(*op); ++i) {
        const std::optional<double> value = parseDecimal(parts[i + 2]);
        if (!value) {
            return fail(ErrorCode::Invalid, std::format("entity edit: '{}' is not a number", parts[i + 2]));
        }
        command.values.at(i) = *value;
    }
    return command;
}

std::vector<EntityCommand> appliedEdits(std::span<const std::string> lines) {
    std::vector<EntityCommand> edits;
    for (const std::string& line : lines) {
        const std::size_t at = line.find(kApplied);
        if (at == std::string::npos) {
            continue;
        }
        if (auto command = parseCommand(std::string_view(line).substr(at + kApplied.size())); command) {
            edits.push_back(*command);
        }
    }
    return edits;
}

void EntityEditor::write(std::string line) {
    if (m_log) {
        m_log(line);
    }
    m_journal.push_back(std::move(line));
    if (m_journal.size() > kJournalLines) {
        m_journal.erase(m_journal.begin());
    }
}

void EntityEditor::refuse(const EntityCommand& command, std::string_view reason) {
    write(std::format("entity edit: refused {}: {}", commandText(command), reason));
}

bool EntityEditor::queue(const EntityCommand& command) {
    if (m_mayEdit && !m_mayEdit()) {
        refuse(command, "only the host edits in a network session");
        return false;
    }
    m_pending.push_back(command);
    return true;
}

bool EntityEditor::queueReset(EntityId entity, EditOp op, const EntityEditTarget& target) {
    const std::optional<std::array<double, 3>> configured = target.configuredValues(entity, op);
    if (!configured) {
        return false;
    }
    return queue(EntityCommand{.entity = entity, .op = op, .values = *configured});
}

std::size_t EntityEditor::applyPending(EntityEditTarget* target) {
    // Taken first: an edit's own log line must not find the queue half-emptied.
    std::vector<EntityCommand> queued = std::exchange(m_pending, {});
    std::size_t applied = 0;
    for (const EntityCommand& command : queued) {
        if (m_mayEdit && !m_mayEdit()) {
            refuse(command, "only the host edits in a network session");
        } else if (target == nullptr) {
            refuse(command, "nothing is playing");
        } else if (const auto result = target->applyEdit(command); !result) {
            refuse(command, result.error().message);
        } else {
            write(std::format("{}{}", kApplied, commandText(command)));
            ++applied;
        }
    }
    return applied;
}

std::optional<EntityCommand> EntityEditor::queuedCommand(EntityId entity, EditOp op) const {
    for (auto it = m_pending.rbegin(); it != m_pending.rend(); ++it) {
        if (it->entity == entity && it->op == op) {
            return *it;
        }
    }
    return std::nullopt;
}

std::optional<double> EntityEditor::queuedValue(EntityId entity, EditOp op) const {
    const std::optional<EntityCommand> command = queuedCommand(entity, op);
    return command ? std::optional<double>(command->values[0]) : std::nullopt;
}

} // namespace coney::debug
