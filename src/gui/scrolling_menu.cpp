// SPDX-License-Identifier: GPL-3.0-or-later
#include "gui/scrolling_menu.h"

#include <algorithm>
#include <utility>

namespace coney::gui {

void ScrollingMenu::setup(ScrollingMenuSetup setup, std::size_t count) {
    m_setup = std::move(setup);
    m_count = count;
    m_selected = 0;
    m_lastMoveMs.reset();
}

void ScrollingMenu::select(std::size_t index) { m_selected = m_count == 0 ? 0 : std::min(index, m_count - 1); }

void ScrollingMenu::takeFocus(MenuInput& input, std::uint64_t nowMs) {
    input.focus(nowMs);
    input.setDpadRepeat(true);
    m_input = &input;
    m_lastMoveMs.reset();
}

std::optional<std::size_t> ScrollingMenu::handle(MenuCommand command, std::uint64_t nowMs) {
    if (m_count == 0) {
        return std::nullopt;
    }
    switch (command) {
    case MenuCommand::Up:
    case MenuCommand::Down:
        // At least kInputGapMs between two moves.
        if (m_lastMoveMs && nowMs - *m_lastMoveMs < kInputGapMs) {
            return std::nullopt;
        }
        m_lastMoveMs = nowMs;
        move(command == MenuCommand::Up ? -1 : 1);
        return std::nullopt;
    case MenuCommand::Accept:
        return m_selected;
    case MenuCommand::Left:
    case MenuCommand::Right:
    case MenuCommand::Back:
        return std::nullopt;
    }
    return std::nullopt;
}

std::size_t ScrollingMenu::firstVisible() const {
    const std::size_t shown = visibleCount();
    if (shown == 0 || m_count <= shown) {
        return 0;
    }
    // The cursor in the middle row, the window clamped to the list.
    const std::size_t middle = shown / 2;
    const std::size_t first = m_selected > middle ? m_selected - middle : 0;
    return std::min(first, m_count - shown);
}

std::size_t ScrollingMenu::visibleCount() const { return std::min(m_count, m_setup.visible); }

void ScrollingMenu::move(int step) {
    // No wrap: at either end the cursor stays, with the refused cue and the d-pad's plain query.
    const bool atEnd = step < 0 ? m_selected == 0 : m_selected + 1 >= m_count;
    if (atEnd) {
        playCue(kRefusedCue);
        if (m_input != nullptr) {
            m_input->setDpadRepeat(false);
        }
        return;
    }
    m_selected = step < 0 ? m_selected - 1 : m_selected + 1;
    playCue(m_setup.moveCue);
    if (m_input != nullptr) {
        m_input->setDpadRepeat(true);
    }
}

void ScrollingMenu::playCue(int cue) const {
    if (m_setup.playCue) {
        m_setup.playCue(cue);
    }
}

} // namespace coney::gui
