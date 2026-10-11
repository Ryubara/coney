// SPDX-License-Identifier: GPL-3.0-or-later
#include "gui/pause_menu/control_menu.h"

#include <algorithm>
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

// The pad picture's centre x (overlay units), confirmed (code) at `0x001e4130`.
constexpr float kPictureX = 0.5F;

} // namespace

ControlLayout ControlLayout::forMode(Language language, bool widescreen, bool flag02) {
    ControlLayout layout;
    if (flag02 && !widescreen) {
        // The second table, `0x0050f000`: label 1 at 0.22 for every language but English, the picture shorter.
        layout.leftColumnX = 0.24F;
        layout.rightColumnX = 0.76F;
        layout.rows = {0.385F, 0.415F, 0.477F, 0.51F, 0.545F, 0.575F, 0.625F};
        layout.labelOneX = language == Language::English ? layout.leftColumnX : 0.22F;
        if (language == Language::German) {
            layout.labelOneX = 0.20F;
        }
        layout.pictureY = 0.55F;
        layout.pictureHeight = 0.34F;
    } else {
        // The table `0x0050ef20`: label 1 moved for German and French.
        if (language == Language::German) {
            layout.labelOneX = 0.215F;
        }
    }
    if (language == Language::French) {
        layout.labelOneX = 0.16F;
    }
    if (flag02 && widescreen) {
        layout.pictureY = 0.575F;
    }
    // 16:9 (flag 0x04 without 0x08): a narrower picture and a longer list.
    if (widescreen) {
        layout.pictureWidth = 0.5F;
        layout.visibleTips = 10;
        layout.tipClipHeight = 0.58F;
        layout.tipSpacing = 0.007F;
    } else if (flag02) {
        layout.visibleTips = 9;
        layout.tipSpacing = 0.007F;
    }
    return layout;
}

ControlMenu::ControlMenu() { m_picture.setVisible(true); }

std::string_view ControlMenu::string(std::uint32_t id) const { return m_strings != nullptr ? m_strings->get(id) : ""; }

void ControlMenu::playCue(int cue) const {
    if (m_playCue) {
        m_playCue(cue);
    }
}

void ControlMenu::open(const GlobalStrings* strings, MenuInput& input, std::function<void(int cue)> playCue,
                       std::uint64_t nowMs, const ControlLayout& layout) {
    namespace s = control_strings;
    m_strings = strings;
    m_playCue = std::move(playCue);
    m_layout = layout;
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
    // The pages' title (ControlPage_Init).
    m_title.setup(TextWidgetSetup{
        .x = kTitleX, .y = kTitleY, .scale = kEntrySize, .colour = kMenuGrey, .fontSlot = kTitleFontSlot});

    // ControllerPage_Init: the picture and the labels on each side of it, from the layout's table; a label that
    // wraps with `<CR>` grows downward from its row into the gap under it.
    m_picture.setup(BaseWidgetSetup{
        .x = kPictureX, .y = layout.pictureY, .height = layout.pictureHeight, .width = layout.pictureWidth});
    for (std::size_t i = 0; i < kControllerLabels; ++i) {
        const float y = layout.rows.at(i);
        TextWidget& left = m_labels.at(i);
        left.setup(TextWidgetSetup{.x = i == 1 ? layout.labelOneX : layout.leftColumnX,
                                   .y = y,
                                   .scale = kLabelSize,
                                   .colour = kMenuGrey,
                                   .alignment = TextAlignment::Right,
                                   .fontSlot = kLabelFontSlot});
        left.setText(string(s::kLeftColumn + static_cast<std::uint32_t>(i)));
        TextWidget& right = m_labels.at(kControllerLabels + i);
        right.setup(TextWidgetSetup{
            .x = layout.rightColumnX, .y = y, .scale = kLabelSize, .colour = kMenuGrey, .fontSlot = kLabelFontSlot});
        right.setText(string(s::kRightColumn + static_cast<std::uint32_t>(i)));
    }

    // TutorialPage_Init: the tips, the first focused, the layout's count visible.
    for (std::size_t i = 0; i < kTutorialTips; ++i) {
        m_tipTitles.at(i).setup(TextWidgetSetup{.x = kTipsX,
                                                .y = kTipsY,
                                                .scale = kTipSize,
                                                .colour = kDimGrey,
                                                .fontSlot = kTipFontSlot,
                                                .wrapWidth = kTipWrap});
        m_tipTitles.at(i).setText(string(s::kTipTitles + static_cast<std::uint32_t>(i)));
    }
    m_description.setup(TextWidgetSetup{.x = kTipsX,
                                        .y = kTipsY,
                                        .scale = kTipSize,
                                        .colour = kMenuGrey,
                                        .fontSlot = kTipFontSlot,
                                        .wrapWidth = kTipWrap});
    m_tips.setup(ScrollingMenuSetup{.visible = layout.visibleTips,
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
    m_reveal = 0.0F;
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
    // The focused tip's text unfolds once it has been focused long enough, a step a frame (cue 0xd as it starts).
    if (m_page == ControlPage::Tutorial && !m_descriptionShown && frame.timeMs - m_focusMs >= kDescriptionDelayMs) {
        m_descriptionShown = true;
        playCue(kRevealCue);
    }
    if (m_descriptionShown && m_reveal < 1.0F) {
        m_reveal = std::min(1.0F, m_reveal + kRevealStep);
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
        // The visible tips stacked from the list's place, `tipSpacing` apart, the focused one's text unfolding
        // under it; only tips that end inside the clip height are drawn. A text's y is its first line's centre, so
        // the next item's centre is one height further down.
        float y = kTipsY;
        const std::size_t first = m_tips.firstVisible();
        for (std::size_t i = first; i < first + m_tips.visibleCount(); ++i) {
            TextWidget& tip = m_tipTitles.at(i);
            const float height = tip.layout(canvas).height;
            if (y + height - kTipsY > m_layout.tipClipHeight) {
                break;
            }
            tip.style().y = y;
            tip.render(canvas);
            y += height;
            if (i == m_tips.selected() && m_descriptionShown) {
                y += kDescriptionGap;
                m_description.style().y = y;
                if (m_reveal >= 1.0F) {
                    m_description.render(canvas);
                }
                y += m_description.layout(canvas).height * m_reveal;
            }
            y += m_layout.tipSpacing;
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
            if (i == m_tips.selected() && m_reveal >= 1.0F) {
                out.push_back(m_description.text());
            }
        }
        break;
    }
    }
    return out;
}

} // namespace coney::gui
