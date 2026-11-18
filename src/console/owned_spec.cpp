// SPDX-License-Identifier: GPL-3.0-or-later
#include "console/owned_spec.h"

namespace coney::console {

OwnedSpec::OwnedSpec(const CommandSpec& spec) : m_spec(spec) {
    m_spec.name = keep(spec.name);
    m_spec.group = keep(spec.group);
    m_spec.summary = keep(spec.summary);
    m_spec.available = keep(spec.available);
    m_args.reserve(spec.args.size());
    for (const ArgSpec& arg : spec.args) {
        ArgSpec copy = arg;
        copy.name = keep(arg.name);
        copy.help = keep(arg.help);
        copy.defaultValue = keep(arg.defaultValue);
        std::vector<std::string_view>& choices = m_choices.emplace_back();
        for (const std::string_view choice : arg.choices) {
            choices.push_back(keep(choice));
        }
        copy.choices = choices;
        m_args.push_back(copy);
    }
    m_spec.args = m_args;
}

std::string_view OwnedSpec::keep(std::string_view text) { return m_text.emplace_back(text); }

} // namespace coney::console
