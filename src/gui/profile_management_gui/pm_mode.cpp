// SPDX-License-Identifier: GPL-3.0-or-later
#include "gui/profile_management_gui/pm_mode.h"

#include <optional>

namespace coney::gui {

void PmMode::enter(ScreenFlowController& /*flow*/) {
    const PmLayout& layout = m_shared.layout;
    m_grid.init();
    m_grid.setup(OptionGridLayout{
        .centreX = 0.5F,
        .top = layout.menuTop,
        .rowGap = layout.menuRowGap,
        .boxWidth = layout.textBoxWidth,
        .scale = layout.textScale,
        .selectedScale = 1.15F,
        .colour = graphics::Rgba{160, 160, 160, 255},
        .selectedColour = graphics::kWhite,
    });
    m_grid.addItem(m_shared.string(kStoryString), kStory);
    if (!m_shared.europe) {
        m_grid.addItem(m_shared.string(kExtrasString), kExtras);
    }
    m_grid.addItem(m_shared.string(kQuickRumbleString), kQuickRumble);
    m_grid.select(0);
    m_grid.takeFocus(m_shared.input, m_shared.frame.timeMs);
    m_usage.init();
    m_usage.setLegend(m_shared.string(UsageInfo::kMenuUsageString), layout.usageY);
}

int PmMode::update() {
    const GuiFrame& frame = m_shared.frame;
    int result = kStay;
    // Input waits while the screen is not clear (a fade not finished).
    const bool faded = m_shared.fade != nullptr && m_shared.fade->level() != 0.0F;
    if (frame.pad != nullptr && !faded) {
        if (const std::optional<MenuCommand> command = m_shared.input.dispatch(*frame.pad, frame.timeMs)) {
            if (*command == MenuCommand::Back) {
                result = kToGreet;
                playSound(kBackCue);
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
    playSound(kAcceptCue);
    switch (code) {
    case kStory:
        // Two pads or more ask how many players first.
        return m_shared.connectedPads >= 2 ? kToNumPlayers : kStory;
    case kExtras:
        return kExtras;
    case kReloadProfiles:
        callScript(kReloadProfilesFunction);
        return kStay;
    default:
        // Any other code (quick rumble): the Lua function the profile manager was started with; the menu stays.
        callScript(m_shared.onRumble);
        return kStay;
    }
}

void PmMode::playSound(int cue) const {
    if (m_shared.playSound) {
        m_shared.playSound(cue);
    }
}

void PmMode::callScript(std::string_view function) const {
    if (m_shared.callScript && !function.empty()) {
        m_shared.callScript(function, {});
    }
}

void PmMode::exit() {
    m_grid.shutdown();
    m_usage.shutdown();
}

} // namespace coney::gui
