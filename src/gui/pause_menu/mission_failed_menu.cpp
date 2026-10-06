// SPDX-License-Identifier: GPL-3.0-or-later
#include "gui/pause_menu/mission_failed_menu.h"

#include <utility>

#include "gui/colour_table.h"
#include "gui/pause_menu/pause_items.h"
#include "gui/text_layout.h"

namespace coney::gui {

std::uint32_t MissionFailedMenu::titleRecord(Language language) {
    switch (static_cast<int>(language)) {
    case 1:
        return kTitleRecordLanguage1;
    case 2:
        return kTitleRecordLanguage2;
    default:
        return kTitleRecord;
    }
}

void MissionFailedMenu::setSoundSink(std::function<void(int cue)> playCue) {
    m_playCue = std::move(playCue);
    m_yesNo.setSoundSink(m_playCue);
}

std::string_view MissionFailedMenu::string(std::uint32_t id) const {
    return m_strings != nullptr ? m_strings->get(id) : "";
}

void MissionFailedMenu::playCue(int cue) const {
    if (m_playCue) {
        m_playCue(cue);
    }
}

void MissionFailedMenu::open(int level, std::string_view reason, std::uint64_t nowMs) {
    namespace s = pause_strings;
    m_open = true;
    m_firstFrame = true;
    m_outcome.reset();

    m_title.setup(BaseWidgetSetup{.x = 0.5F,
                                  .y = kTitleY,
                                  .height = kTitleSize,
                                  .colour = graphics::Rgba{kTitleGrey, kTitleGrey, kTitleGrey, 255}});

    m_reason.setup(TextWidgetSetup{.x = kReasonX,
                                   .y = kReasonFirstY,
                                   .scale = 1.0F,
                                   .colour = kMenuRed,
                                   .alignment = TextAlignment::Centre,
                                   .fontSlot = kBigFontSlot});
    m_reason.setText(reason.empty() ? string(s::kUnknownReason) : reason);

    // Rows of 1 and 2, or one row in the hangout.
    const bool hangout = level == kHangoutLevel;
    m_grid.setup(OptionGridSetup{.y = kGridY,
                                 .rows = hangout ? std::vector<std::size_t>{2} : std::vector<std::size_t>{1, 2},
                                 .centreX = 0.5F,
                                 .moveCue = PauseMenu::kMoveCue,
                                 .playCue = [this](int cue) { playCue(cue); }});
    const auto add = [this](std::uint32_t text, FailedItem code, bool separator) {
        (void)m_grid.addItem(OptionGridItem{.text = std::string(string(text)),
                                            .code = static_cast<int>(code),
                                            .separator = separator,
                                            .enabled = true,
                                            .colour = kDimGrey,
                                            .scale = PauseMenu::kGridScale,
                                            .fontSlot = PauseMenu::kGridFontSlot});
    };
    if (hangout) {
        add(s::kToHangoutFailed, FailedItem::ToHangout, true);
    } else {
        add(s::kLastCheckpoint, FailedItem::LastCheckpoint, false);
        add(s::kRestartLevelFailed, FailedItem::RestartLevel, true);
    }
    add(s::kQuitFailed, FailedItem::Quit, false);
    m_grid.select(0);
    m_grid.takeFocus(m_input, nowMs);

    m_usage.place(0.5F, kUsageY, false);
    m_usage.setLegend(string(s::kUsageFailed));

    m_yesNo.close();
    m_yesNo.place(YesNoPlacement{.questionX = 0.08F, .questionY = 0.75F, .answersX = 0.44F, .answersY = 0.87F});
}

void MissionFailedMenu::update(const GuiFrame& frame) {
    if (!m_open) {
        return;
    }
    // The reason moves to its place after the first frame.
    if (m_firstFrame) {
        m_firstFrame = false;
    } else {
        m_reason.style().y = kReasonY;
    }
    m_reason.update(frame);
    m_grid.update(frame);
    m_usage.update(frame);
    m_yesNo.update(frame);
    if (m_outcome || frame.pad == nullptr) {
        return;
    }
    const std::optional<MenuCommand> command = m_input.dispatch(*frame.pad, frame.timeMs);
    if (!command) {
        return;
    }
    // The Yes/No box takes every command while it is open.
    if (m_yesNo.isOpen()) {
        if (m_yesNo.handle(*command) == YesNoAnswer::Yes) {
            m_outcome = PauseOutcome::QuitToMainMenu;
        }
        return;
    }
    if (*command == MenuCommand::Back) {
        return;
    }
    if (*command != MenuCommand::Accept) {
        (void)m_grid.handle(*command);
        return;
    }
    const std::optional<int> code = m_grid.handle(*command);
    if (!code) {
        return;
    }
    playCue(PauseMenu::kAcceptCue);
    switch (static_cast<FailedItem>(*code)) {
    case FailedItem::LastCheckpoint:
        m_outcome = PauseOutcome::RestartCheckpoint;
        break;
    case FailedItem::RestartLevel:
        m_outcome = PauseOutcome::RestartLevel;
        break;
    case FailedItem::ToHangout:
        m_outcome = PauseOutcome::QuitToHangout;
        break;
    case FailedItem::Quit:
        m_yesNo.open(string(pause_strings::kQuitQuestion), string(pause_strings::kYes), string(pause_strings::kNo));
        break;
    }
}

void MissionFailedMenu::render(const GuiCanvas& canvas) const {
    if (!m_open || !visible()) {
        return;
    }
    m_title.render(canvas);
    m_reason.render(canvas);
    m_grid.render(canvas);
    m_usage.render(canvas);
    m_yesNo.render(canvas);
}

} // namespace coney::gui
