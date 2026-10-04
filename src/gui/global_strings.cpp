// SPDX-License-Identifier: GPL-3.0-or-later
#include "gui/global_strings.h"

#include <utility>

namespace coney::gui {

std::string_view GlobalStrings::get(StringTable table, std::uint32_t id) const {
    const auto& strings = tableOf(table);
    const auto it = strings.find(id);
    return it == strings.end() ? std::string_view() : std::string_view(it->second);
}

void GlobalStrings::set(StringTable table, std::uint32_t id, std::string text) {
    m_tables[static_cast<std::size_t>(table)][id] = std::move(text);
}

} // namespace coney::gui
