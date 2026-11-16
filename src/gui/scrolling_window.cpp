// SPDX-License-Identifier: GPL-3.0-or-later
#include "gui/scrolling_window.h"

#include <algorithm>

namespace coney::gui {

namespace {

// From this clip height up the menu has no clip (`ScrollingMenu_Setup`'s 10,000 is one).
constexpr float kNoClip = 5000.0F;

} // namespace

void ScrollingWindow::setup(const ScrollingWindowSetup& setup, std::size_t count) {
    // Each entry is added at reveal 1; the window starts at the top, as long as the list allows, still.
    m_setup = setup;
    m_reveals.assign(count, 1.0F);
    m_heights.assign(count, 0.0F);
    m_first = 0;
    m_count = std::min(setup.maxVisible, count);
    m_fit.reset();
    m_state = ScrollState::None;
}

void ScrollingWindow::moveUp(std::size_t selected) {
    // The middle row: half the window, rounded down, one higher with evenRows (unsigned, as the original's: a window of
    // one has none).
    std::size_t half = (m_count - 1) >> 1U;
    if (m_setup.evenRows) {
        half = (m_count >> 1U) - 1;
    }
    // Above the window, or above the middle row with entries above it: the window starts one higher, one longer.
    if (selected < m_first || (selected - m_first < half && m_first != 0)) {
        m_state = ScrollState::Up;
        ++m_count;
        --m_first;
    }
}

void ScrollingWindow::moveDown(std::size_t selected) {
    std::size_t half = m_count >> 1U;
    if (m_setup.evenRows) {
        half = (m_count >> 1U) - 1;
    }
    // Below the middle row with entries left below the window: one more entry, the top one to go.
    if (selected - m_first > half && m_first + m_count < m_reveals.size()) {
        ++m_count;
        m_state = ScrollState::Down;
    }
}

void ScrollingWindow::update(const Entries& entries) {
    const std::size_t total = m_reveals.size();
    if (total == 0) {
        return;
    }
    // 1. The scroll's step. Down and HideTop hide the top entry (at 0 the window starts one lower), Up and RevealTop
    // reveal it; Down and Up also give the window's last entry the rest of the top one's reveal.
    const std::size_t top = m_first;
    const std::size_t last = std::min(m_first + m_count - 1, total - 1);
    const ScrollState scroll = m_state;
    if (scroll == ScrollState::Down || scroll == ScrollState::HideTop) {
        if (m_reveals.at(top) > 0.0F) {
            setReveal(top, m_reveals.at(top) - m_setup.step);
        }
        if (m_reveals.at(top) == 0.0F) {
            m_state = ScrollState::None;
            ++m_first;
            --m_count;
        }
    } else if (scroll == ScrollState::Up || scroll == ScrollState::RevealTop) {
        if (m_reveals.at(top) < 1.0F) {
            setReveal(top, m_reveals.at(top) + m_setup.step);
        }
        if (m_reveals.at(top) == 1.0F) {
            m_state = ScrollState::None;
            --m_count;
        }
    }
    if (scroll == ScrollState::Down || scroll == ScrollState::Up) {
        setReveal(last, 1.0F - m_reveals.at(top));
    }

    // 2. The window: `maxVisible` entries, one more while scrolling, cut to the list's end.
    m_count = m_state == ScrollState::None ? m_setup.maxVisible : m_setup.maxVisible + 1;
    if (m_first + m_count > total) {
        m_count = total - m_first;
    }

    // 3. Each entry placed at the stacked heights of those above it, an entry's height its full height × its reveal.
    float offset = 0.0F;
    for (std::size_t i = m_first; i < m_first + m_count; ++i) {
        m_heights.at(i) = entries.layOut(i, offset, m_reveals.at(i));
        offset += m_heights.at(i) * m_reveals.at(i);
    }

    // 4. The clip: the entries whose stacked heights stay under it, and the first past it when it is focused. A still
    // window is stacked with every reveal back at 1.
    m_fit.reset();
    if (m_setup.clipHeight >= kNoClip) {
        return;
    }
    float stacked = 0.0F;
    for (std::size_t i = m_first; i < m_first + m_count; ++i) {
        if (m_state == ScrollState::None) {
            setReveal(i, 1.0F);
        }
        stacked += m_heights.at(i) * m_reveals.at(i);
        if (stacked > m_setup.clipHeight) {
            m_fit = (i - m_first) + (entries.focused(i) ? 1 : 0);
            break;
        }
    }
    const std::size_t shown = std::min(m_fit.value_or(m_count), m_count);
    float drawnHeight = 0.0F;
    for (std::size_t i = m_first; i < m_first + shown; ++i) {
        drawnHeight += m_heights.at(i) * m_reveals.at(i);
    }
    if (m_state != ScrollState::None) {
        return;
    }

    // 5. A still window scrolls when the clip asks: the entry above it, measured whole and put back at 0, is revealed
    // when it fits under the clip and fewer than `maxVisible` are drawn; a focused last drawn entry that runs past the
    // clip, not the first, hides the top one.
    float above = 0.0F;
    if (m_first != 0) {
        const std::size_t index = m_first - 1;
        setReveal(index, 1.0F);
        above = entries.layOut(index, std::nullopt, 1.0F);
        setReveal(index, 0.0F);
        (void)entries.layOut(index, std::nullopt, 0.0F);
    }
    if (drawnHeight + above < m_setup.clipHeight) {
        if (m_first != 0 && shown < m_setup.maxVisible) {
            m_state = ScrollState::RevealTop;
            --m_first;
        }
        return;
    }
    if (shown == 0) {
        return;
    }
    const std::size_t lastDrawn = m_first + shown - 1;
    if (lastDrawn != m_first && drawnHeight > m_setup.clipHeight && entries.focused(lastDrawn)) {
        m_state = ScrollState::HideTop;
    }
}

std::size_t ScrollingWindow::drawn() const {
    // The entries the clip lets through, one more while the one above is being revealed, never past the list's end.
    std::size_t count = std::min(m_fit.value_or(m_count), m_count);
    if (m_state == ScrollState::RevealTop) {
        ++count;
    }
    return std::min(count, m_reveals.size() - std::min(m_first, m_reveals.size()));
}

void ScrollingWindow::setReveal(std::size_t index, float reveal) {
    // Clamped to 0-1, anything not at least 0 being 0.
    m_reveals.at(index) = reveal >= 0.0F ? std::min(reveal, 1.0F) : 0.0F;
}

} // namespace coney::gui
