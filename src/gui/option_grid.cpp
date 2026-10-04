// SPDX-License-Identifier: GPL-3.0-or-later
#include "gui/option_grid.h"

#include <memory>

#include "core/assert.h"

namespace coney::gui {

void OptionGrid::setup(const OptionGridLayout& layout) {
    m_layout = layout;
    m_items.clear();
    m_selected = 0;
}

bool OptionGrid::addItem(std::string_view text, int code) {
    if (m_items.size() >= kMaxItems) {
        return false;
    }
    auto item = std::make_unique<Item>();
    item->text.setText(text);
    item->code = code;
    m_items.push_back(std::move(item));
    restyle();
    return true;
}

void OptionGrid::select(std::size_t index) {
    CONEY_ASSERT(index < m_items.size());
    m_selected = index;
    restyle();
}

void OptionGrid::takeFocus(MenuInput& input, std::uint64_t nowMs) { input.focus(nowMs); }

std::optional<int> OptionGrid::handle(MenuCommand command) {
    if (m_items.empty()) {
        return std::nullopt;
    }
    switch (command) {
    case MenuCommand::Up:
        select(m_selected == 0 ? m_items.size() - 1 : m_selected - 1);
        return std::nullopt;
    case MenuCommand::Down:
        select(m_selected + 1 == m_items.size() ? 0 : m_selected + 1);
        return std::nullopt;
    case MenuCommand::Accept:
        return m_items[m_selected]->code;
    case MenuCommand::Left:
    case MenuCommand::Right:
    case MenuCommand::Back:
        break;
    }
    return std::nullopt;
}

void OptionGrid::update(const GuiFrame& frame) {
    for (const auto& item : m_items) {
        item->text.update(frame);
    }
}

void OptionGrid::render(const GuiCanvas& canvas) const {
    if (!visible()) {
        return;
    }
    for (const auto& item : m_items) {
        item->text.render(canvas);
    }
}

int OptionGrid::code(std::size_t index) const {
    CONEY_ASSERT(index < m_items.size());
    return m_items[index]->code;
}

const TextWidget& OptionGrid::item(std::size_t index) const {
    CONEY_ASSERT(index < m_items.size());
    return m_items[index]->text;
}

void OptionGrid::restyle() {
    for (std::size_t i = 0; i < m_items.size(); ++i) {
        TextWidget& text = m_items[i]->text;
        const bool selected = i == m_selected;
        text.centreOn(m_layout.centreX, m_layout.top + static_cast<float>(i) * m_layout.rowGap, m_layout.boxWidth);
        text.style().scale = selected ? m_layout.selectedScale : m_layout.scale;
        text.style().colour = selected ? m_layout.selectedColour : m_layout.colour;
    }
}

} // namespace coney::gui
