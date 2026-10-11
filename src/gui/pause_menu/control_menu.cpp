// SPDX-License-Identifier: GPL-3.0-or-later
#include "gui/pause_menu/control_menu.h"

#include <string>
#include <utility>
#include <vector>

#include "gui/colour_table.h"

namespace coney::gui {

namespace {

// The cues the screen plays (docs/research/pause.md#input).
constexpr int kMoveCue = 4;
constexpr int kAcceptCue = 8;
constexpr int kRefusedCue = 0xe;
constexpr int kBackCue = 0xf;
constexpr int kRevealCue = 0xd;

// The pad picture's place and size (overlay units), confirmed (code) at `0x001e4130`.
constexpr float kPictureX = 0.5F;
constexpr float kPictureY = 0.58F;
constexpr float kPictureWidth = 0.51F;
constexpr float kPictureHeight = 0.45F;

// Coney's stand-ins (docs/research/pause.md#open-questions): the label table `0x0050ef20` and the tips' place
// `0x0050eef0`/`0x0050eef4` are not given. The labels sit in two columns beside the picture, the tips under the
// title, one line apart, their text wrapped to the list's width.
constexpr float kLabelRightX = 0.24F; // the right-aligned labels end here
constexpr float kLabelLeftX = 0.76F;  // the left-aligned labels start here
constexpr float kLabelTop = 0.42F;
constexpr float kLabelStep = 0.05F;
constexpr float kLabelSize = 0.9F;
constexpr float kTipsTop = 0.35F;
constexpr float kTipStep = 0.04F;
constexpr float kTipSize = 1.0F;
constexpr float kTipWrap = 0.48F;
constexpr float kTipTextGap = 0.01F; // between a tip and its text, and after the text

} // namespace

ControlMenu::ControlMenu() { m_picture.setVisible(true); }

std::string_view ControlMenu::string(std::uint32_t id) const { return m_strings != nullptr ? m_strings->get(id) : ""; }

void ControlMenu::playCue(int cue) const {
    if (m_playCue) {
        m_playCue(cue);
    }
}

void ControlMenu::open(const GlobalStrings* strings, MenuInput& input, std::function<void(int cue)> playCue,
                       std::uint64_t nowMs) {
    namespace s = control_strings;
    m_strings = strings;
    m_playCue = std::move(playCue);
    m_selected = 0;
    m_page = ControlPage::None;
    m_nowMs = nowMs;

    // The two entries, left-aligned one step apart.
    for (std::size_t i = 0; i < m_entries.size(); ++i) {
        m_entries.at(i).setup(TextWidgetSetup{.x = kEntryX,
                                              .y = kEntryY + (kEntryStep * static_cast<float>(i)),
                                              .scale = kEntrySize,
                                              .colour = kMenuGrey});
        m_entries.at(i).setText(string(i == 0 ? s::kController : s::kTutorial));
    }
    colourEntries();
    // The pages' title (ControlPage_Init: size 1.3, `0x005fd310`); its place is Coney's stand-in.
    m_title.setup(TextWidgetSetup{.x = kEntryX, .y = kEntryY, .scale = kEntrySize, .colour = kMenuGrey});

    // ControllerPage_Init: the picture and the labels on each side of it.
    m_picture.setup(BaseWidgetSetup{.x = kPictureX, .y = kPictureY, .height = kPictureHeight, .width = kPictureWidth});
    for (std::size_t i = 0; i < kControllerLabels; ++i) {
        const float y = kLabelTop + (kLabelStep * static_cast<float>(i));
        TextWidget& right = m_labels.at(i);
        right.setup(TextWidgetSetup{
            .x = kLabelRightX, .y = y, .scale = kLabelSize, .colour = kMenuGrey, .alignment = TextAlignment::Right});
        right.setText(string(s::kRightLabels + static_cast<std::uint32_t>(i)));
        TextWidget& left = m_labels.at(kControllerLabels + i);
        left.setup(TextWidgetSetup{.x = kLabelLeftX, .y = y, .scale = kLabelSize, .colour = kMenuGrey});
        left.setText(string(s::kLeftLabels + static_cast<std::uint32_t>(i)));
    }

    // TutorialPage_Init: the tips, the first focused, ten visible from the pause menu.
    for (std::size_t i = 0; i < kTutorialTips; ++i) {
        m_tipTitles.at(i).setup(TextWidgetSetup{.x = kEntryX, .y = kTipsTop, .scale = kTipSize, .colour = kDimGrey});
        m_tipTitles.at(i).setText(string(s::kTipTitles + static_cast<std::uint32_t>(i)));
    }
    m_description.setup(
        TextWidgetSetup{.x = kEntryX, .y = kTipsTop, .scale = kTipSize, .colour = kMenuGrey, .wrapWidth = kTipWrap});
    m_tips.setup(ScrollingMenuSetup{.visible = kVisibleTips,
                                    .moveCue = kMoveCue,
                                    .playCue = [this](int cue) { this->playCue(cue); }},
                 kTutorialTips);
    m_tips.takeFocus(input, nowMs);
    focusTip(nowMs);
    setFade(m_fade);
}

void ControlMenu::colourEntries() {
    for (std::size_t i = 0; i < m_entries.size(); ++i) {
        m_entries.at(i).style().colour = i == m_selected ? kMenuGrey : kDimGrey;
    }
}

void ControlMenu::focusTip(std::uint64_t nowMs) {
    for (std::size_t i = 0; i < kTutorialTips; ++i) {
        m_tipTitles.at(i).style().colour = i == m_tips.selected() ? kTipFocus : kDimGrey;
    }
    m_description.setText(string(control_strings::kTipTexts + static_cast<std::uint32_t>(m_tips.selected())));
    m_focusMs = nowMs;
    m_descriptionShown = false;
    setFade(m_fade);
}

bool ControlMenu::handle(MenuCommand command, std::uint64_t nowMs) {
    // An open page takes every command; its back closes it.
    if (m_page == ControlPage::Controller) {
        if (controllerCommand(command)) {
            m_page = ControlPage::None;
        }
        return false;
    }
    if (m_page == ControlPage::Tutorial) {
        if (tutorialCommand(command, nowMs)) {
            m_page = ControlPage::None;
        }
        return false;
    }
    switch (command) {
    case MenuCommand::Up:
    case MenuCommand::Down: {
        const bool up = command == MenuCommand::Up;
        if ((up && m_selected == 0) || (!up && m_selected + 1 >= m_entries.size())) {
            playCue(kRefusedCue);
            return false;
        }
        m_selected = up ? m_selected - 1 : m_selected + 1;
        playCue(kMoveCue);
        colourEntries();
        return false;
    }
    case MenuCommand::Left:
    case MenuCommand::Right:
        playCue(kRefusedCue);
        return false;
    case MenuCommand::Accept:
        // Only the Controller page's opening has a cue.
        if (m_selected == 0) {
            playCue(kAcceptCue);
            m_page = ControlPage::Controller;
            m_title.setText(string(control_strings::kController));
        } else {
            m_page = ControlPage::Tutorial;
            m_title.setText(string(control_strings::kTutorial));
            focusTip(nowMs);
        }
        return false;
    case MenuCommand::Back:
        playCue(kBackCue);
        return true;
    }
    return false;
}

bool ControlMenu::controllerCommand(MenuCommand command) {
    switch (command) {
    case MenuCommand::Up:
    case MenuCommand::Down:
    case MenuCommand::Left:
    case MenuCommand::Right:
        playCue(kRefusedCue);
        return false;
    case MenuCommand::Accept:
        return false;
    case MenuCommand::Back:
        playCue(kBackCue);
        return true;
    }
    return false;
}

bool ControlMenu::tutorialCommand(MenuCommand command, std::uint64_t nowMs) {
    if (command == MenuCommand::Back) {
        playCue(kBackCue);
        return true;
    }
    const std::size_t before = m_tips.selected();
    (void)m_tips.handle(command, nowMs);
    if (m_tips.selected() != before) {
        focusTip(nowMs);
    }
    return false;
}

void ControlMenu::update(const GuiFrame& frame) {
    m_nowMs = frame.timeMs;
    // The focused tip's text, once it has been focused long enough.
    if (m_page == ControlPage::Tutorial && !m_descriptionShown && frame.timeMs - m_focusMs >= kDescriptionDelayMs) {
        m_descriptionShown = true;
        playCue(kRevealCue);
    }
    for (TextWidget& entry : m_entries) {
        entry.update(frame);
    }
    m_title.update(frame);
}

void ControlMenu::setFade(float fade) {
    m_fade = fade;
    for (TextWidget& entry : m_entries) {
        entry.setFade(fade);
    }
    m_title.setFade(fade);
    for (TextWidget& label : m_labels) {
        label.setFade(fade);
    }
    for (TextWidget& tip : m_tipTitles) {
        tip.setFade(fade);
    }
    m_description.setFade(fade);
    graphics::Rgba white = graphics::kWhite;
    white.a = static_cast<std::uint8_t>(255.0F * fade);
    m_picture.setColour(white);
}

void ControlMenu::render(const GuiCanvas& canvas) const {
    switch (m_page) {
    case ControlPage::None:
        for (const TextWidget& entry : m_entries) {
            entry.render(canvas);
        }
        return;
    case ControlPage::Controller:
        m_title.render(canvas);
        m_picture.render(canvas);
        for (const TextWidget& label : m_labels) {
            label.render(canvas);
        }
        return;
    case ControlPage::Tutorial: {
        m_title.render(canvas);
        // The visible tips stacked from the top, the shown text under the focused one, within the clip height.
        float y = kTipsTop;
        const std::size_t first = m_tips.firstVisible();
        for (std::size_t i = first; i < first + m_tips.visibleCount(); ++i) {
            // Only whole lines inside the clip height.
            if (y + kTipStep - kTipsTop > kTipClipHeight) {
                break;
            }
            TextWidget& tip = m_tipTitles.at(i);
            tip.style().y = y;
            tip.render(canvas);
            y += kTipStep;
            if (i == m_tips.selected() && m_descriptionShown) {
                m_description.style().y = y;
                m_description.render(canvas);
                y += m_description.layout(canvas).height + kTipTextGap;
            }
        }
        return;
    }
    }
}

std::vector<std::string> ControlMenu::lines() const {
    std::vector<std::string> out;
    switch (m_page) {
    case ControlPage::None:
        for (const TextWidget& entry : m_entries) {
            out.push_back(entry.text());
        }
        break;
    case ControlPage::Controller:
        out.push_back(m_title.text());
        for (const TextWidget& label : m_labels) {
            out.push_back(label.text());
        }
        break;
    case ControlPage::Tutorial: {
        out.push_back(m_title.text());
        const std::size_t first = m_tips.firstVisible();
        for (std::size_t i = first; i < first + m_tips.visibleCount(); ++i) {
            out.push_back(m_tipTitles.at(i).text());
            if (i == m_tips.selected() && m_descriptionShown) {
                out.push_back(m_description.text());
            }
        }
        break;
    }
    }
    return out;
}

} // namespace coney::gui
