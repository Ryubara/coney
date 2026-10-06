// SPDX-License-Identifier: GPL-3.0-or-later
#include "gui/profile_management_gui/pm_list_screen.h"

#include "gui/text_layout.h"

namespace coney::gui {

int PmListScreen::handle(MenuCommand command) {
    switch (command) {
    case MenuCommand::Back:
        playSound(pm_cue::kBack);
        return kBack;
    case MenuCommand::Accept:
        if (const std::optional<int> code = m_choices.selectedCode()) {
            playSound(pm_cue::kAccept);
            return accept(*code);
        }
        return kStay;
    default:
        return direction(command);
    }
}

void PmListScreen::open() {
    m_title.clear();
    m_titleY.reset();
    m_gridY.reset();
    setUp();
    // The look: the title over the grid, both placed by the grid's row count unless the screen says otherwise.
    const std::size_t rows = m_choices.rows().size();
    if (!m_title.empty()) {
        placePmText(m_titleText, m_title, pm_look::kX, m_titleY.value_or(pm_look::titleY(rows)), pm_look::kTitleScale,
                    pm_look::kRed, kBigFontSlot);
    }
    m_grid.build(m_choices, m_gridY.value_or(pm_look::gridY(rows)));
    placePmUsage(m_usage, m_shared);
}

void PmListScreen::close() {
    closeExtras();
    m_grid.clear();
    m_titleText.shutdown();
    m_usage.shutdown();
}

void PmListScreen::draw() {
    const GuiFrame& frame = m_shared.frame;
    if (!m_title.empty()) {
        m_titleText.update(frame);
        m_titleText.render(m_shared.canvas);
    }
    m_grid.render(m_choices, m_shared.canvas);
    m_usage.update(frame);
    m_usage.render(m_shared.canvas);
    drawExtras();
}

} // namespace coney::gui
