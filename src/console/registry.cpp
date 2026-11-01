// SPDX-License-Identifier: GPL-3.0-or-later
#include "console/registry.h"

#include <algorithm>
#include <format>
#include <limits>
#include <utility>

#include "core/assert.h"

namespace coney::console {

namespace {

// The edit distance between two names (insertions, deletions and substitutions), for "did you mean".
std::size_t editDistance(std::string_view a, std::string_view b) {
    std::vector<std::size_t> row(b.size() + 1);
    for (std::size_t j = 0; j <= b.size(); ++j) {
        row[j] = j;
    }
    for (std::size_t i = 1; i <= a.size(); ++i) {
        std::size_t diagonal = row[0];
        row[0] = i;
        for (std::size_t j = 1; j <= b.size(); ++j) {
            const std::size_t above = row[j];
            row[j] = std::min({row[j] + 1, row[j - 1] + 1, diagonal + (a[i - 1] == b[j - 1] ? 0U : 1U)});
            diagonal = above;
        }
    }
    return row[b.size()];
}

// The farthest a known name may be from a typed one to be offered instead.
constexpr std::size_t kSuggestDistance = 2;

} // namespace

// Keeps the spec's argument names beside the values, one for one.
CommandArgs::CommandArgs(std::span<const ArgSpec> specs, std::vector<std::string> values)
    : m_values(std::move(values)) {
    CONEY_ASSERT(specs.size() == m_values.size());
    for (const ArgSpec& spec : specs) {
        m_names.emplace_back(spec.name);
    }
}

std::string_view CommandArgs::get(std::string_view name) const {
    const auto found = std::ranges::find(m_names, name);
    CONEY_ASSERT(found != m_names.end());
    return m_values[static_cast<std::size_t>(found - m_names.begin())];
}

std::expected<CommandId, Error> Registry::addBuiltin(const CommandSpec& spec, Handler handler) {
    if (!isCommandName(spec.name, kBuiltinNameMax)) {
        return fail(ErrorCode::InvalidArgument,
                    std::format(R"("{}" is not a command name: lower-case letters, digits, - and _)", spec.name));
    }
    if (spec.access > m_highest) {
        return fail(
            ErrorCode::InvalidArgument,
            std::format(R"("{}" is a developer command, and this build holds player commands only)", spec.name));
    }
    const bool taken = std::ranges::any_of(m_commands, [&](const std::unique_ptr<Command>& command) {
        return command && command->spec().name == spec.name;
    });
    if (taken) {
        return fail(ErrorCode::InvalidArgument, std::format(R"("{}" is registered already)", spec.name));
    }
    return store(Origin::Builtin, {}, spec, std::move(handler));
}

std::expected<CommandId, Error> Registry::addModCommand(std::string_view mod, const CommandSpec& spec,
                                                        Handler handler) {
    if (!isCommandName(spec.name, kModCommandNameMax)) {
        return fail(ErrorCode::InvalidArgument,
                    std::format(R"("{}" is not a command name: at most {} lower-case letters, digits, - and _)",
                                spec.name, kModCommandNameMax));
    }
    const bool builtinName = std::ranges::any_of(m_commands, [&](const std::unique_ptr<Command>& command) {
        return command && command->origin == Origin::Builtin && command->spec().name == spec.name;
    });
    if (builtinName) {
        return fail(ErrorCode::InvalidArgument, std::format(R"("{}" is a built-in command's name)", spec.name));
    }
    CommandSpec playerSpec = spec;
    playerSpec.access = Access::Player;
    // The same mod registering its name again replaces the command in place, keeping its id.
    std::size_t count = 0;
    for (std::unique_ptr<Command>& command : m_commands) {
        if (!command || command->mod != mod) {
            continue;
        }
        if (command->spec().name == spec.name) {
            command->owned = std::make_unique<const OwnedSpec>(playerSpec);
            command->handler = std::move(handler);
            return command->id;
        }
        ++count;
    }
    if (count >= kModCommandsMax) {
        return fail(ErrorCode::InvalidArgument,
                    std::format(R"("{}" has {} commands, the most a mod may register)", mod, kModCommandsMax));
    }
    return store(Origin::Mod, mod, playerSpec, std::move(handler));
}

std::size_t Registry::removeMod(std::string_view mod) {
    std::size_t removed = 0;
    for (std::unique_ptr<Command>& command : m_commands) {
        if (command && command->origin == Origin::Mod && command->mod == mod) {
            command.reset();
            ++removed;
        }
    }
    return removed;
}

std::expected<CommandId, Error> Registry::resolve(std::string_view name) const {
    // `<mod>:<name>` names one mod's command.
    if (const std::size_t colon = name.find(':'); colon != std::string_view::npos) {
        const std::string_view mod = name.substr(0, colon);
        const std::string_view bare = name.substr(colon + 1);
        for (const Command* command : modCommands(bare)) {
            if (command->mod == mod) {
                return command->id;
            }
        }
    } else if (const Command* command = builtin(name)) {
        return command->id;
    } else if (const std::vector<const Command*> mods = modCommands(name); mods.size() == 1) {
        return mods.front()->id;
    } else if (mods.size() > 1) {
        std::vector<std::string> both;
        for (const Command* each : mods) {
            both.push_back(std::format("{}:{}", each->mod, name));
        }
        std::ranges::sort(both);
        std::string list;
        for (const std::string& each : both) {
            list += list.empty() ? each : ", " + each;
        }
        return fail(ErrorCode::InvalidArgument, std::format(R"("{}" is ambiguous: {})", name, list));
    }
    // Unknown: offer the closest present name, when one is close.
    std::string closest;
    std::size_t best = std::numeric_limits<std::size_t>::max();
    for (const std::string& known : names()) {
        const std::size_t distance = editDistance(name, known);
        if (distance <= kSuggestDistance && distance < best) {
            best = distance;
            closest = known;
        }
    }
    if (closest.empty()) {
        return fail(ErrorCode::NotFound, std::format(R"(unknown command "{}"; help lists the commands)", name));
    }
    return fail(ErrorCode::NotFound,
                std::format(R"(unknown command "{}"; did you mean "{}"? help lists the commands)", name, closest));
}

const Command* Registry::find(CommandId id) const {
    if (id.value == 0 || id.value > m_commands.size()) {
        return nullptr;
    }
    const Command* command = m_commands[id.value - 1].get();
    return command != nullptr && present(*command) ? command : nullptr;
}

std::vector<std::string> Registry::names() const {
    std::vector<std::string> list;
    for (const Command* command : commands()) {
        const std::string_view name = command->spec().name;
        if (command->origin == Origin::Builtin || modCommands(name).size() == 1) {
            list.emplace_back(name);
        } else {
            list.push_back(std::format("{}:{}", command->mod, name));
        }
    }
    std::ranges::sort(list);
    return list;
}

std::vector<const Command*> Registry::commands() const {
    std::vector<const Command*> list;
    for (const std::unique_ptr<Command>& command : m_commands) {
        if (command && present(*command)) {
            list.push_back(command.get());
        }
    }
    return list;
}

bool Registry::present(const Command& command) const {
    const std::string_view feature = command.spec().available;
    return feature.empty() || std::ranges::find(m_features, feature) != m_features.end();
}

const Command* Registry::builtin(std::string_view name) const {
    for (const std::unique_ptr<Command>& command : m_commands) {
        if (command && command->origin == Origin::Builtin && command->spec().name == name && present(*command)) {
            return command.get();
        }
    }
    return nullptr;
}

std::vector<const Command*> Registry::modCommands(std::string_view name) const {
    std::vector<const Command*> list;
    for (const std::unique_ptr<Command>& command : m_commands) {
        if (command && command->origin == Origin::Mod && command->spec().name == name && present(*command)) {
            list.push_back(command.get());
        }
    }
    return list;
}

CommandId Registry::store(Origin origin, std::string_view mod, const CommandSpec& spec, Handler handler) {
    const CommandId id{static_cast<std::uint32_t>(m_commands.size() + 1)};
    auto command = std::make_unique<Command>();
    command->id = id;
    command->origin = origin;
    command->mod = std::string(mod);
    command->owned = std::make_unique<const OwnedSpec>(spec);
    command->handler = std::move(handler);
    m_commands.push_back(std::move(command));
    return id;
}

} // namespace coney::console
