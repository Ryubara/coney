// SPDX-License-Identifier: GPL-3.0-or-later
#include "gui/pause_menu/armies_game_over_menu.h"

#include <algorithm>
#include <string>

#include "gui/pause_menu/pause_menu.h"
#include "gui/text_layout.h"

namespace coney::gui {

std::string_view ArmiesGameOverMenu::string(std::uint32_t id) const {
    return m_strings != nullptr ? m_strings->get(id) : "";
}

void ArmiesGameOverMenu::playCue(int cue) const {
    if (m_playCue) {
        m_playCue(cue);
    }
}

void ArmiesGameOverMenu::open(std::uint64_t nowMs) {
    m_open = true;
    m_ending = false;
    m_acceptsInput = false;
    m_choice.reset();
    m_count = kCountdownFrom;
    m_stepMs = nowMs;
    const auto setUp = [](TextWidget& widget, float y, float scale) {
        widget.setup(TextWidgetSetup{.x = 0.5F,
                                     .y = y,
                                     .scale = scale,
                                     .colour = kColour,
                                     .alignment = TextAlignment::Centre,
                                     .fontSlot = kBigFontSlot});
        widget.setFade(1.0F);
    };
    setUp(m_title, kTitleY, kTitleScale);
    m_title.setText(string(kTitleString));
    setUp(m_digit, kDigitY, kDigitScale);
    m_digit.setText(std::to_string(m_count));
    m_usage.place(0.5F, kCountdownUsageY, false);
    m_usage.setLegend(string(kCountdownUsageString));
    m_usage.setFade(1.0F);
    // The end phase's grid, one row: Replay : Quit, the first selected.
    m_grid.setup(OptionGridSetup{.y = kGridY,
                                 .rows = {2},
                                 .centreX = 0.5F,
                                 .moveCue = PauseMenu::kMoveCue,
                                 .playCue = [this](int cue) { playCue(cue); }});
    const auto add = [this](std::uint32_t text, ArmiesGameOverChoice code, bool separator) {
        (void)m_grid.addItem(OptionGridItem{.text = std::string(string(text)),
                                            .code = static_cast<int>(code),
                                            .separator = separator,
                                            .enabled = true,
                                            .colour = kColour,
                                            .scale = PauseMenu::kGridScale,
                                            .fontSlot = PauseMenu::kGridFontSlot});
    };
    add(kReplayString, ArmiesGameOverChoice::Replay, true);
    add(kQuitString, ArmiesGameOverChoice::Quit, false);
    m_grid.select(0);
    m_grid.takeFocus(m_input, nowMs);
}

void ArmiesGameOverMenu::pressStart(bool haveCredit, std::uint64_t nowMs) {
    if (!m_open || m_ending || m_choice) {
        return;
    }
    playCue(kCue);
    if (haveCredit) {
        m_choice = ArmiesGameOverChoice::Continue;
        return;
    }
    startEnd(nowMs);
}

void ArmiesGameOverMenu::startEnd(std::uint64_t nowMs) {
    m_ending = true;
    m_endStartMs = nowMs;
    m_title.style().y = kEndY;
    m_title.style().scale = kEndScale;
    m_title.setText(string(kEndString));
    m_usage.place(0.5F, kEndUsageY, false);
    m_usage.setLegend(string(kEndUsageString));
    m_title.setFade(0.0F);
    m_grid.setFade(0.0F);
    m_usage.setFade(0.0F);
}

void ArmiesGameOverMenu::update(const GuiFrame& frame) {
    if (!m_open || m_choice) {
        return;
    }
    if (!m_ending) {
        // The count steps once a second; after 0 has shown a step, the end phase.
        if (m_stepMs + kStepMs < frame.timeMs) {
            if (m_count == 0) {
                startEnd(frame.timeMs);
            } else {
                --m_count;
                m_stepMs = frame.timeMs;
            }
        }
        m_digit.setText(std::to_string(m_count));
    }
    if (m_ending) {
        // The parts fade in over the end fade; input only once they are in.
        if (m_endStartMs + kEndFadeMs < frame.timeMs) {
            m_acceptsInput = true;
            m_title.setFade(1.0F);
            m_grid.setFade(1.0F);
            m_usage.setFade(1.0F);
        } else {
            const float in = static_cast<float>(frame.timeMs - m_endStartMs) / static_cast<float>(kEndFadeMs);
            const float fade = std::clamp(in, 0.0F, 1.0F);
            m_title.setFade(fade);
            m_grid.setFade(fade);
            m_usage.setFade(fade);
        }
        m_grid.update(frame);
    }
    m_title.update(frame);
    m_digit.update(frame);
    m_usage.update(frame);
    if (!m_ending || !m_acceptsInput || frame.pad == nullptr) {
        return;
    }
    const std::optional<MenuCommand> command = m_input.dispatch(*frame.pad, frame.timeMs);
    if (!command) {
        return;
    }
    const std::optional<int> code = m_grid.handle(*command);
    if (*command != MenuCommand::Accept || !code) {
        return;
    }
    playCue(kCue);
    m_choice = static_cast<ArmiesGameOverChoice>(*code);
}

void ArmiesGameOverMenu::render(const GuiCanvas& canvas) const {
    if (!m_open || m_choice || !visible()) {
        return;
    }
    m_title.render(canvas);
    if (m_ending) {
        m_grid.render(canvas);
    } else {
        m_digit.render(canvas);
    }
    m_usage.render(canvas);
}

} // namespace coney::gui
