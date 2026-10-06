// SPDX-License-Identifier: GPL-3.0-or-later
#include "gui/profile_management_gui/pm_extras.h"

#include <array>
#include <optional>

#include "gui/profile_management_gui/pm_widgets.h"

namespace coney::gui {

void PmExtras::enter(ScreenFlowController& /*flow*/) {
    pm::setupGrid(m_grid, m_shared, m_shared.layout.gridFor(1), {1});
    m_grid.addItem(pm::item(m_shared.string(kTrailerString), kTrailer));
    m_grid.select(0);
    m_grid.takeFocus(m_shared.input, m_shared.frame.timeMs);
    pm::setupUsage(m_usage, m_shared);
}

int PmExtras::update() {
    const GuiFrame& frame = m_shared.frame;
    int result = kStay;
    if (frame.pad != nullptr && !m_shared.fadeNotClear()) {
        if (const std::optional<MenuCommand> command = m_shared.input.dispatch(*frame.pad, frame.timeMs)) {
            if (*command == MenuCommand::Back) {
                m_shared.cue(pm::kBackCue);
                result = kBack;
            } else if (m_grid.handle(*command)) {
                // Accept: the trailer, through the script's movie function; the screen stays.
                m_shared.cue(pm::kAcceptCue);
                const std::array<double, 1> movie{kTrailerMovie};
                m_shared.call(kPlayMovieFunction, movie);
            }
        }
    }
    m_grid.update(frame);
    m_usage.update(frame);
    m_grid.render(m_shared.canvas);
    m_usage.render(m_shared.canvas);
    return result;
}

void PmExtras::exit() {
    m_grid.shutdown();
    m_usage.shutdown();
}

} // namespace coney::gui
