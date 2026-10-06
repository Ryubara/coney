// SPDX-License-Identifier: GPL-3.0-or-later
#include "gui/profile_management_gui/pm_screen.h"

#include <algorithm>
#include <numeric>
#include <utility>

#include "core/assert.h"

namespace coney::gui {

void PmChoices::set(std::vector<Item> items, std::vector<std::size_t> rowSizes, std::size_t selected) {
    m_items = std::move(items);
    if (rowSizes.empty()) {
        rowSizes.assign(m_items.size(), 1);
    }
    CONEY_ASSERT(std::accumulate(rowSizes.begin(), rowSizes.end(), std::size_t{0}) == m_items.size());
    m_rows = std::move(rowSizes);
    select(selected);
}

void PmChoices::select(std::size_t index) { m_selected = m_items.empty() ? 0 : std::min(index, m_items.size() - 1); }

std::optional<int> PmChoices::selectedCode() const {
    if (m_items.empty()) {
        return std::nullopt;
    }
    return m_items.at(m_selected).code;
}

std::pair<std::size_t, std::size_t> PmChoices::cell(std::size_t index) const {
    std::size_t first = 0;
    for (std::size_t row = 0; row < m_rows.size(); ++row) {
        if (index < first + m_rows.at(row)) {
            return {row, index - first};
        }
        first += m_rows.at(row);
    }
    return {0, 0};
}

std::size_t PmChoices::indexOf(std::size_t row, std::size_t column) const {
    std::size_t first = 0;
    for (std::size_t r = 0; r < row; ++r) {
        first += m_rows.at(r);
    }
    return first + std::min(column, m_rows.at(row) - 1);
}

bool PmChoices::move(MenuCommand command) {
    if (m_items.empty()) {
        return false;
    }
    const std::pair<std::size_t, std::size_t> at = cell(m_selected);
    const std::size_t row = at.first;
    const std::size_t column = at.second;
    // The item the direction leads to; the same item when it cannot move (or for accept and back).
    std::size_t next = m_selected;
    switch (command) {
    case MenuCommand::Left:
        if (column > 0) {
            next = m_selected - 1;
        }
        break;
    case MenuCommand::Right:
        if (column + 1 < m_rows.at(row)) {
            next = m_selected + 1;
        }
        break;
    case MenuCommand::Up:
        if (row > 0) {
            next = indexOf(row - 1, column);
        }
        break;
    case MenuCommand::Down:
        if (row + 1 < m_rows.size()) {
            next = indexOf(row + 1, column);
        }
        break;
    default:
        break;
    }
    if (next == m_selected) {
        return false;
    }
    m_selected = next;
    return true;
}

void PmScreen::enter(ScreenFlowController& /*flow*/) {
    m_shared.input.focus(m_shared.frame.timeMs);
    open();
}

int PmScreen::update() {
    const GuiFrame& frame = m_shared.frame;
    int result = poll();
    if (result == kStay && frame.pad != nullptr && !faded()) {
        if (const std::optional<MenuCommand> command = m_shared.input.dispatch(*frame.pad, frame.timeMs)) {
            result = handle(*command);
        }
    }
    draw();
    return result;
}

bool PmScreen::faded() const {
    return m_shared.fade != nullptr && (m_shared.fade->running() || m_shared.fade->level() != 0.0F);
}

void PmScreen::playSound(int cue) const {
    if (m_shared.playSound) {
        m_shared.playSound(cue);
    }
}

void PmScreen::callScript(std::string_view function, std::span<const double> args) const {
    if (m_shared.callScript && !function.empty()) {
        m_shared.callScript(function, args);
    }
}

int PmScreen::moveIn(PmChoices& choices, MenuCommand command) const {
    playSound(choices.move(command) ? pm_cue::kGridMove : pm_cue::kRefused);
    return kStay;
}

} // namespace coney::gui
