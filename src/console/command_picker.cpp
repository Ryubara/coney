// SPDX-License-Identifier: GPL-3.0-or-later
#include "console/command_picker.h"

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <format>
#include <string_view>
#include <system_error>

#include "console/tokenizer.h"
#include "core/parse_number.h"

namespace coney::console {

namespace {

// The heading a command is listed under: its group, or for a mod's command without one the mod's name.
std::string_view groupOf(const Command& command) {
    if (!command.spec().group.empty()) {
        return command.spec().group;
    }
    return command.mod.empty() ? std::string_view("Other") : std::string_view(command.mod);
}

// The name a line uses for `command`: a built-in's own, a mod command's `<mod>:<name>`, which always resolves.
std::string lineName(const Command& command) {
    if (command.origin == Origin::Mod) {
        return std::format("{}:{}", command.mod, command.spec().name);
    }
    return std::string(command.spec().name);
}

} // namespace

// `value` moved by `step`, as a whole or a decimal number; an empty value, or one that does not read in full (a word,
// or a number with text after it), counts as 0.
std::string steppedNumber(std::string_view value, int step, bool whole) {
    if (whole) {
        // Read in full, as parseDecimal does: `12x` is no number, not 12.
        long long number = 0;
        const char* const end = value.data() + value.size();
        if (const auto read = std::from_chars(value.data(), end, number); read.ec != std::errc{} || read.ptr != end) {
            number = 0;
        }
        return std::to_string(number + step);
    }
    // Through parseDecimal: Apple's libc++ has no floating-point std::from_chars.
    const double number = parseDecimal(value).value_or(0.0);
    return std::format("{}", number + step);
}

void CommandPicker::reset() {
    m_page = PickerPage::Commands;
    m_selected = 0;
    m_command.reset();
    m_values.clear();
}

bool CommandPicker::apply(PickerInput input, PlayerSlot slot) {
    if (m_page == PickerPage::Arguments && chosen() == nullptr) {
        reset(); // its command went away (a mod stopped)
    }
    if (m_page == PickerPage::Commands) {
        const std::size_t count = commands().size();
        m_selected = count == 0 ? 0 : std::min(m_selected, count - 1);
        switch (input) {
        case PickerInput::Up:
            m_selected = m_selected > 0 ? m_selected - 1 : 0;
            break;
        case PickerInput::Down:
            m_selected = m_selected + 1 < count ? m_selected + 1 : m_selected;
            break;
        case PickerInput::Accept:
            choose(slot);
            break;
        case PickerInput::Back:
            return false;
        case PickerInput::Left:
        case PickerInput::Right:
            break;
        }
        return true;
    }
    // The arguments' rows, then the row that runs the line.
    const std::size_t args = chosen()->spec().args.size();
    // Back on the commands page, at the command that was chosen.
    const auto backToCommands = [this] {
        const std::vector<const Command*> listed = commands();
        const CommandId id = *m_command;
        const auto at = std::ranges::find_if(listed, [id](const Command* command) { return command->id == id; });
        reset();
        m_selected = at == listed.end() ? 0 : static_cast<std::size_t>(at - listed.begin());
    };
    switch (input) {
    case PickerInput::Up:
        m_selected = m_selected > 0 ? m_selected - 1 : 0;
        break;
    case PickerInput::Down:
        m_selected = std::min(m_selected + 1, args);
        break;
    case PickerInput::Left:
    case PickerInput::Right:
        if (m_selected < args) {
            stepValue(m_selected, input == PickerInput::Left ? -1 : 1);
        }
        break;
    case PickerInput::Accept:
        if (m_selected < args) {
            ++m_selected;
        } else {
            const std::string built = line();
            backToCommands();
            (void)m_console.submit(built, LineSource::Typed, slot);
        }
        break;
    case PickerInput::Back:
        backToCommands();
        break;
    }
    return true;
}

std::vector<PickerRow> CommandPicker::rows() const {
    std::vector<PickerRow> rows;
    if (m_page == PickerPage::Commands) {
        for (const Command* command : commands()) {
            rows.push_back(PickerRow{.label = lineName(*command),
                                     .value = std::string(groupOf(*command)),
                                     .help = std::string(command->spec().summary)});
        }
        return rows;
    }
    const Command* command = chosen();
    if (command == nullptr) {
        return rows;
    }
    const auto args = command->spec().args;
    for (std::size_t i = 0; i < args.size(); ++i) {
        rows.push_back(PickerRow{.label = std::string(args[i].name),
                                 .value = i < m_values.size() ? m_values[i] : std::string(),
                                 .help = std::string(args[i].help)});
    }
    rows.push_back(PickerRow{.label = "run", .value = line(), .help = "Runs the line"});
    return rows;
}

std::string CommandPicker::line() const {
    const Command* command = chosen();
    if (m_page != PickerPage::Arguments || command == nullptr) {
        return {};
    }
    std::string built = lineName(*command);
    // Arguments are by position, so the line stops at the first one left out.
    for (const std::string& value : m_values) {
        if (value.empty()) {
            break;
        }
        built += " " + quoteWord(value);
    }
    return built;
}

std::vector<const Command*> CommandPicker::commands() const {
    const std::vector<const Command*> all = m_console.registry().commands();
    std::vector<std::string_view> groups;
    for (const Command* command : all) {
        if (std::ranges::find(groups, groupOf(*command)) == groups.end()) {
            groups.push_back(groupOf(*command));
        }
    }
    std::vector<const Command*> listed;
    for (const std::string_view group : groups) {
        for (const Command* command : all) {
            if (groupOf(*command) == group) {
                listed.push_back(command);
            }
        }
    }
    return listed;
}

const Command* CommandPicker::chosen() const { return m_command ? m_console.registry().find(*m_command) : nullptr; }

std::vector<std::string> CommandPicker::choices(std::size_t index) const {
    const Command* command = chosen();
    if (command == nullptr) {
        return {};
    }
    std::string prefix = lineName(*command);
    for (std::size_t i = 0; i < index; ++i) {
        if (i >= m_values.size() || m_values[i].empty()) {
            return {}; // an argument before it is left out, so it cannot be given
        }
        prefix += " " + quoteWord(m_values[i]);
    }
    prefix += " ";
    // Completion writes each value back into the whole line; the value is its last word.
    std::vector<std::string> values;
    for (const std::string& candidate : m_console.complete(prefix)) {
        const SplitLine split = splitForCompletion(candidate);
        if (!split.words.empty()) {
            values.push_back(split.words.back());
        }
    }
    return values;
}

void CommandPicker::stepValue(std::size_t index, int step) {
    const auto args = chosen()->spec().args;
    const ArgSpec& arg = args[index];
    std::string& value = m_values[index];
    if (const std::vector<std::string> list = choices(index); !list.empty()) {
        const auto at = std::ranges::find(list, value);
        if (at == list.end()) {
            value = step > 0 ? list.front() : list.back();
        } else {
            const auto count = static_cast<std::ptrdiff_t>(list.size());
            value = list[static_cast<std::size_t>(((at - list.begin()) + step + count) % count)];
        }
    } else if (arg.type == ArgType::Int || arg.type == ArgType::Number) {
        value = steppedNumber(value, step, arg.type == ArgType::Int);
    } else {
        return; // text: the pad cannot type it yet
    }
    // The arguments after it may take other values now (a mission's checkpoints): back to where they start.
    for (std::size_t later = index + 1; later < m_values.size(); ++later) {
        m_values[later] = std::string(args[later].defaultValue);
    }
}

void CommandPicker::choose(PlayerSlot slot) {
    const std::vector<const Command*> listed = commands();
    if (m_selected >= listed.size()) {
        return;
    }
    const Command& command = *listed[m_selected];
    if (command.spec().args.empty()) {
        (void)m_console.submit(lineName(command), LineSource::Typed, slot);
        return;
    }
    m_command = command.id;
    m_page = PickerPage::Arguments;
    m_selected = 0;
    m_values.assign(command.spec().args.size(), std::string());
    // Each argument starts at its default; a required one without a default at the first value its list offers.
    for (std::size_t i = 0; i < m_values.size(); ++i) {
        const ArgSpec& arg = command.spec().args[i];
        if (!arg.defaultValue.empty()) {
            m_values[i] = std::string(arg.defaultValue);
        } else if (!arg.optional) {
            const std::vector<std::string> list = choices(i);
            m_values[i] = list.empty() ? std::string() : list.front();
        }
    }
}

} // namespace coney::console
