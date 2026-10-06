// SPDX-License-Identifier: GPL-3.0-or-later
#include "gui/profile_management_gui/pm_mode.h"

#include <optional>

#include "gui/profile_management_gui/pm_widgets.h"

namespace coney::gui {

void PmMode::enter(ScreenFlowController& /*flow*/) {
    const PmLayout& layout = m_shared.layout;
    if (m_shared.video.flag02) {
        // No extras: one row of two.
        pm::setupGrid(m_grid, m_shared, layout.gridFor(1), {2});
        m_grid.addItem(pm::item(m_shared.string(kStoryString), kStory, true));
        m_grid.addItem(pm::item(m_shared.string(kQuickRumbleString), kQuickRumble));
    } else {
        pm::setupGrid(m_grid, m_shared, layout.gridFor(2), {2, 1});
        m_grid.addItem(pm::item(m_shared.string(kStoryString), kStory, true));
        m_grid.addItem(pm::item(m_shared.string(kExtrasString), kExtras));
        m_grid.addItem(pm::item(m_shared.string(kQuickRumbleString), kQuickRumble));
    }
    m_grid.select(0);
    m_grid.takeFocus(m_shared.input, m_shared.frame.timeMs);
    pm::setupUsage(m_usage, m_shared);
}

int PmMode::update() {
    const GuiFrame& frame = m_shared.frame;
    int result = kStay;
    // Input waits while the screen is not clear (a fade not finished).
    if (frame.pad != nullptr && !m_shared.fadeNotClear()) {
        if (const std::optional<MenuCommand> command = m_shared.input.dispatch(*frame.pad, frame.timeMs)) {
            if (*command == MenuCommand::Back) {
                m_shared.cue(pm::kBackCue);
                result = kToGreet;
            } else if (const std::optional<int> chosen = m_grid.handle(*command)) {
                result = choose(*chosen);
            }
        }
    }
    m_grid.update(frame);
    m_usage.update(frame);
    m_grid.render(m_shared.canvas);
    m_usage.render(m_shared.canvas);
    return result;
}

int PmMode::choose(int code) {
    m_shared.cue(pm::kAcceptCue);
    switch (code) {
    case kStory:
        // Two pads or more ask how many players first.
        return m_shared.connectedPads >= 2 ? kToNumPlayers : kStory;
    case kExtras:
        return kExtras;
    case kReloadProfiles:
        m_shared.call(kReloadProfilesFunction);
        return kStay;
    default:
        // Any other code (quick rumble): the Lua function the profile manager was started with; the menu stays.
        m_shared.call(m_shared.onRumble);
        return kStay;
    }
}

void PmMode::exit() {
    m_grid.shutdown();
    m_usage.shutdown();
}

} // namespace coney::gui
