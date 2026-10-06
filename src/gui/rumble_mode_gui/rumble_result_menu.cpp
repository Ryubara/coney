// SPDX-License-Identifier: GPL-3.0-or-later
#include "gui/rumble_mode_gui/rumble_result_menu.h"

#include <algorithm>
#include <string>
#include <vector>

#include "gui/colour_table.h"
#include "gui/text_layout.h"

namespace coney::gui {

std::string_view RumbleResultMenu::string(std::uint32_t id) const {
    return m_strings != nullptr ? m_strings->get(id) : "";
}

void RumbleResultMenu::playCue(int cue) const {
    if (m_playCue) {
        m_playCue(cue);
    }
}

void RumbleResultMenu::open(std::string_view winner, std::string_view reason, bool fromFrontEnd, std::uint64_t nowMs) {
    m_open = true;
    m_choice.reset();
    m_openedMs = nowMs;
    m_fromFrontEnd = fromFrontEnd;
    m_choicesShown = false;
    m_takesInput = false;

    // Coney's stand-in look for the two lines (not on the page): centred and white, the winner in big_font.
    m_winner.setup(TextWidgetSetup{.x = 0.5F,
                                   .y = kWinnerY,
                                   .scale = 1.0F,
                                   .colour = graphics::kWhite,
                                   .alignment = TextAlignment::Centre,
                                   .fontSlot = kBigFontSlot});
    m_winner.setText(winner.empty() ? string(kNoWinner) : winner);
    m_reason.setup(TextWidgetSetup{.x = 0.5F,
                                   .y = kReasonY,
                                   .scale = 1.0F,
                                   .colour = graphics::kWhite,
                                   .alignment = TextAlignment::Centre,
                                   .fontSlot = kTextFontSlot});
    m_reason.setText(reason);

    m_usage.place(0.5F, kUsageY, false);
    m_usage.setLegend(string(kUsage));
    fillGrid(false, nowMs);
}

void RumbleResultMenu::fillGrid(bool second, std::uint64_t nowMs) {
    m_second = second;
    m_grid.setup(
        OptionGridSetup{.y = kGridY, .rows = std::vector<std::size_t>{2}, .centreX = 0.5F, .playCue = [this](int cue) {
                            playCue(cue);
                        }});
    const auto add = [this](std::uint32_t text, int id, bool separator) {
        (void)m_grid.addItem(OptionGridItem{.text = std::string(string(text)),
                                            .code = id,
                                            .separator = separator,
                                            .enabled = true,
                                            .colour = kDimGrey,
                                            .scale = kItemScale,
                                            .fontSlot = kGridFontSlot});
    };
    if (second) {
        add(kRumbleMenu, kRumbleMenuId, true);
        add(m_fromFrontEnd ? kQuitFrontEnd : kQuitInGame, kQuitId, false);
    } else {
        add(kReplay, kReplayId, true);
        add(kMore, kMoreId, false);
    }
    m_grid.select(0);
    m_grid.takeFocus(m_input, nowMs);
}

void RumbleResultMenu::update(const GuiFrame& frame) {
    if (!m_open) {
        return;
    }
    // The choices appear after the delay and fade in; input waits for the fade's end.
    const std::uint64_t since = frame.timeMs >= m_openedMs ? frame.timeMs - m_openedMs : 0;
    m_choicesShown = since >= kChoicesDelayMs;
    const float fade =
        m_choicesShown
            ? std::min(1.0F, static_cast<float>(since - kChoicesDelayMs) / static_cast<float>(kChoicesFadeMs))
            : 0.0F;
    m_grid.setFade(fade);
    m_usage.setFade(fade);
    const bool takesInput = since >= kChoicesDelayMs + kChoicesFadeMs;
    if (takesInput && !m_takesInput) {
        m_grid.takeFocus(m_input, frame.timeMs);
    }
    m_takesInput = takesInput;

    m_winner.update(frame);
    m_reason.update(frame);
    m_grid.update(frame);
    m_usage.update(frame);
    if (m_choice || !m_takesInput || frame.pad == nullptr) {
        return;
    }
    const std::optional<MenuCommand> command = m_input.dispatch(*frame.pad, frame.timeMs);
    if (!command || *command == MenuCommand::Back) {
        return;
    }
    if (*command != MenuCommand::Accept) {
        (void)m_grid.handle(*command);
        return;
    }
    const std::optional<int> id = m_grid.handle(*command);
    if (!id) {
        return;
    }
    playCue(kAcceptCue);
    switch (*id) {
    case kMoreId:
        fillGrid(true, frame.timeMs);
        break;
    case kReplayId:
        m_choice = RumbleResultChoice::Replay;
        break;
    case kRumbleMenuId:
        m_choice = RumbleResultChoice::RumbleMenu;
        break;
    default:
        m_choice = RumbleResultChoice::Quit;
        break;
    }
}

void RumbleResultMenu::render(const GuiCanvas& canvas) const {
    if (!m_open || !visible()) {
        return;
    }
    m_winner.render(canvas);
    m_reason.render(canvas);
    if (m_choicesShown) {
        m_grid.render(canvas);
        m_usage.render(canvas);
    }
}

} // namespace coney::gui
