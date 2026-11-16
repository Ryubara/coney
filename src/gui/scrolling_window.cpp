// SPDX-License-Identifier: GPL-3.0-or-later
#include "gui/scrolling_window.h"

#include <algorithm>

namespace coney::gui {

void ScrollingWindow::setup(const ScrollingWindowSetup& setup, std::size_t count) {
    m_setup = setup;
    m_reveals.assign(count, 1.0F);
    m_heights.assign(count, 0.0F);
    m_first = 0;
    m_count = std::min(setup.maxVisible, count);
    m_fit.reset();
    m_state = ScrollState::None;
}

void ScrollingWindow::moveUp(std::size_t /*selected*/) {}

void ScrollingWindow::moveDown(std::size_t /*selected*/) {}

void ScrollingWindow::update(const Entries& /*entries*/) {}

std::size_t ScrollingWindow::drawn() const { return m_count; }

void ScrollingWindow::setReveal(std::size_t index, float reveal) {
    m_reveals.at(index) = std::clamp(reveal, 0.0F, 1.0F);
}

} // namespace coney::gui
