// SPDX-License-Identifier: GPL-3.0-or-later
#include "console/console.h"

#include <array>
#include <format>
#include <utility>

#include "console/arg_types.h"
#include "console/tokenizer.h"
#include "core/log.h"

namespace coney::console {

namespace {

// The prefix of the console's lines in the run log (docs/guides/run-log.md#categories: an engine line).
constexpr std::string_view kLogPrefix = "console: ";

// What a need's refusal says is missing.
std::string_view needPhrase(Need need) {
    switch (need) {
    case Need::Game:
        return "a game in play";
    case Need::Profile:
        return "an active profile";
    case Need::Level:
        return "a level in play";
    case Need::Disc:
        return "a disc";
    }
    return "a game in play";
}

constexpr std::array<Need, 4> kNeeds{Need::Game, Need::Profile, Need::Level, Need::Disc};

// Whether the values checked against an earlier spec still suit this one: the same number, each of its shape. The
// empty string `submit` stores for an optional argument left out with no default is no value to check: it fits as
// long as the argument is still optional with no default.
bool argsFit(const CommandSpec& spec, const std::vector<std::string>& values) {
    if (values.size() != spec.args.size()) {
        return false;
    }
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (values[i].empty() && spec.args[i].optional && spec.args[i].defaultValue.empty()) {
            continue;
        }
        const std::span<const std::string> earlier(values.data(), i);
        if (!checkArg(spec.args[i], values[i], earlier, ValueSource{})) {
            return false;
        }
    }
    return true;
}

} // namespace

std::string usageLine(const CommandSpec& spec, std::string_view name) {
    std::string line(name);
    for (const ArgSpec& arg : spec.args) {
        // A runtime-chosen format string cannot be checked at compile time, so each form is written out.
        line += arg.optional ? std::format(" [{}]", arg.name) : std::format(" <{}>", arg.name);
    }
    return line;
}

Console::Console(ConsoleSettings settings, ConsoleHosts hosts)
    : m_settings(settings), m_hosts(hosts), m_registry(settings.highest) {
    // A test-mode run touches no user folder: it neither reads nor writes the history.
    if (!m_settings.testMode && m_hosts.history != nullptr) {
        if (const std::optional<std::string> kept = m_hosts.history->load()) {
            m_terminal.loadHistory(*kept);
        }
    }
}

Outcome Console::submit(std::string_view line, LineSource source, PlayerSlot slot) {
    // In test mode the console reads no device: only the input script's lines come in.
    if (m_settings.testMode && source == LineSource::Typed) {
        return Outcome::Ignored;
    }
    auto words = tokenize(line);
    if (words && words->empty()) {
        return Outcome::Ignored;
    }
    // A typed line goes into the history, kept across runs, whatever becomes of it.
    if (source == LineSource::Typed) {
        m_terminal.remember(line);
        if (!m_settings.testMode && m_hosts.history != nullptr) {
            m_hosts.history->save(m_terminal.historyText());
        }
    }
    m_terminal.print(std::format("> {}", line), LineKind::Echo);
    logging::info(logging::Category::Engine, std::format("{}{}", kLogPrefix, line));
    if (!words) {
        refuse(words.error().message);
        return Outcome::Refused;
    }

    // The name.
    const std::string& name = words->front();
    const auto id = m_registry.resolve(name);
    if (!id) {
        refuse(id.error().message);
        return Outcome::Refused;
    }
    const Command& command = *m_registry.find(*id);
    const CommandSpec& spec = command.spec();

    // The scope against this machine's role: a guest never changes the session, and is told so before anything is
    // sent (docs/guides/studio/console.md#access).
    const SessionRole role = m_hosts.network != nullptr ? m_hosts.network->role() : SessionRole::Offline;
    if (spec.scope == Scope::Session && role == SessionRole::Guest) {
        refuse("only the host can do this in a network game");
        return Outcome::Refused;
    }

    // What must exist.
    for (const Need need : kNeeds) {
        const bool wanted = (spec.needs & needBit(need)) != 0;
        if (wanted && (m_hosts.session == nullptr || !m_hosts.session->has(need))) {
            refuse(std::format(R"("{}" needs {})", spec.name, needPhrase(need)));
            return Outcome::Refused;
        }
    }

    // The arguments: their number, then each against its type, with the defaults of those left out.
    const std::span<const std::string> given(words->begin() + 1, words->end());
    const std::string usage = std::format("usage: {}", usageLine(spec, name));
    if (given.size() > spec.args.size()) {
        refuse(std::format("too many arguments; {}", usage));
        return Outcome::Refused;
    }
    std::vector<std::string> args;
    const ValueSource valueSource = [this](ArgType type, std::span<const std::string> earlier) {
        return values(type, earlier);
    };
    for (std::size_t i = 0; i < spec.args.size(); ++i) {
        const ArgSpec& arg = spec.args[i];
        if (i >= given.size()) {
            if (!arg.optional) {
                refuse(std::format(R"(missing "{}"; {})", arg.name, usage));
                return Outcome::Refused;
            }
            args.emplace_back(arg.defaultValue);
            continue;
        }
        if (auto checked = checkArg(arg, given[i], args, valueSource); !checked) {
            refuse(std::format("{}; {}{}", checked.error(), usage,
                               arg.help.empty() ? "" : " (" + std::string(arg.help) + ")"));
            return Outcome::Refused;
        }
        args.push_back(given[i]);
    }

    // Run now, or between two steps.
    if (runsAtStepBoundary(spec.scope, spec.changes)) {
        m_queue.push(QueuedCommand{*id, std::move(args), slot, std::string(line)});
        return Outcome::Queued;
    }
    run(*id, std::move(args), slot);
    return Outcome::Ran;
}

std::size_t Console::runQueued() {
    std::size_t ran = 0;
    for (QueuedCommand& queued : m_queue.take()) {
        const Command* command = m_registry.find(queued.id);
        if (command == nullptr) {
            refuse(std::format(R"("{}" did not run: its command is no longer registered)", queued.line));
            continue;
        }
        // A mod may have registered the name again since the line was checked, with other arguments.
        if (!argsFit(command->spec(), queued.args)) {
            refuse(std::format(R"("{}" did not run: its command changed since it was entered)", queued.line));
            continue;
        }
        logging::info(logging::Category::Engine, std::format("{}ran {}", kLogPrefix, queued.line));
        run(queued.id, std::move(queued.args), queued.slot);
        ++ran;
    }
    return ran;
}

std::vector<std::string> Console::complete(std::string_view line) const {
    const SplitLine split = splitForCompletion(line);
    std::vector<std::string> completions;
    const bool naming = split.words.empty() || (split.words.size() == 1 && !split.trailingSpace);
    // The first word: the command names.
    if (naming) {
        const std::string prefix = split.words.empty() ? std::string() : split.words.front();
        for (const std::string& name : m_registry.names()) {
            if (name.starts_with(prefix)) {
                completions.push_back(name);
            }
        }
        return completions;
    }
    // An argument: its type's values, after the words before it.
    const auto id = m_registry.resolve(split.words.front());
    if (!id) {
        return completions;
    }
    const CommandSpec& spec = m_registry.find(*id)->spec();
    const std::size_t index = split.trailingSpace ? split.words.size() - 1 : split.words.size() - 2;
    if (index >= spec.args.size()) {
        return completions;
    }
    const std::span<const std::string> earlier(split.words.data() + 1, index);
    const std::string prefix = split.trailingSpace ? std::string() : split.words.back();
    const ValueSource source = [this](ArgType type, std::span<const std::string> before) {
        return values(type, before);
    };
    std::string head;
    for (std::size_t i = 0; i <= index; ++i) {
        head += quoteWord(split.words[i]) + " ";
    }
    for (const std::string& value : completeArg(spec.args[index], prefix, earlier, source)) {
        completions.push_back(head + quoteWord(value));
    }
    return completions;
}

void Console::refuse(std::string_view text) {
    m_terminal.print(text, LineKind::Error);
    logging::info(logging::Category::Engine, std::format("{}  {}", kLogPrefix, text));
}

void Console::output(std::string_view text, std::string_view source) {
    m_terminal.print(text, LineKind::Result, source);
    const std::string mod = source.empty() ? std::string() : std::format("[{}] ", source);
    logging::info(logging::Category::Engine, std::format("{}  {}{}", kLogPrefix, mod, text));
}

void Console::run(CommandId id, std::vector<std::string> args, PlayerSlot slot) {
    const Command& command = *m_registry.find(id);
    // Copies, so a handler that changes the registry (a mod's restart) cannot pull them from under itself.
    const Handler handler = command.handler;
    const std::string source = command.mod;
    const std::string name(command.spec().name);
    const CommandArgs commandArgs(command.spec().args, std::move(args));
    CommandContext context{commandArgs, slot, [this, &source](std::string_view text) { output(text, source); }};
    if (auto result = handler(context); !result) {
        refuse(std::format("{}: {}", name, result.error()));
    }
}

std::optional<std::vector<std::string>> Console::values(ArgType type, std::span<const std::string> earlier) const {
    switch (type) {
    case ArgType::Command:
        return m_registry.names();
    case ArgType::Mod:
        if (m_hosts.mods == nullptr) {
            return std::nullopt;
        }
        return m_hosts.mods->installedMods();
    default:
        if (m_hosts.session == nullptr) {
            return std::nullopt;
        }
        return m_hosts.session->values(type, earlier);
    }
}

} // namespace coney::console
