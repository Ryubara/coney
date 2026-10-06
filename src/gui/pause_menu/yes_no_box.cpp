// SPDX-License-Identifier: GPL-3.0-or-later
#include "gui/pause_menu/yes_no_box.h"

#include <format>
#include <string>

#include "gui/colour_table.h"
#include "gui/text_layout.h"

namespace coney::gui {

namespace {

// A colour as the `<COLOR rrggbbaa>` tag takes it.
std::string colourTag(graphics::Rgba colour) {
    return std::format("<COLOR {:02X}{:02X}{:02X}{:02X}>", colour.r, colour.g, colour.b, colour.a);
}

} // namespace

void YesNoBox::open(std::string_view question, std::string_view yes, std::string_view no) {
    m_question.setup(TextWidgetSetup{.x = m_placement.questionX,
                                     .y = m_placement.questionY,
                                     .scale = 1.0F,
                                     .colour = kMenuGrey,
                                     .alignment = TextAlignment::Left,
                                     .fontSlot = kTextFontSlot});
    m_question.setText(question);
    m_answers.setup(TextWidgetSetup{.x = m_placement.answersX,
                                    .y = m_placement.answersY,
                                    .scale = 1.15F,
                                    .colour = kMenuGrey,
                                    .alignment = TextAlignment::Left,
                                    .fontSlot = kBigFontSlot});
    m_yes = yes;
    m_no = no;
    // No is selected when the box opens (confirmed at runtime).
    m_yesSelected = false;
    m_open = true;
    refreshAnswers();
}

std::optional<YesNoAnswer> YesNoBox::handle(MenuCommand command) {
    if (!m_open) {
        return std::nullopt;
    }
    switch (command) {
    case MenuCommand::Up:
    case MenuCommand::Down:
        playCue(kRefusedCue);
        return std::nullopt;
    case MenuCommand::Left:
    case MenuCommand::Right:
        m_yesSelected = !m_yesSelected;
        playCue(kMoveCue);
        refreshAnswers();
        return std::nullopt;
    case MenuCommand::Accept:
        playCue(kAcceptCue);
        m_open = false;
        return m_yesSelected ? YesNoAnswer::Yes : YesNoAnswer::No;
    case MenuCommand::Back:
        playCue(kBackCue);
        m_open = false;
        return YesNoAnswer::Back;
    }
    return std::nullopt;
}

void YesNoBox::setFade(float fade) {
    m_question.setFade(fade);
    m_answers.setFade(fade);
}

void YesNoBox::update(const GuiFrame& frame) {
    m_question.update(frame);
    m_answers.update(frame);
}

void YesNoBox::render(const GuiCanvas& canvas) const {
    if (!m_open || !visible()) {
        return;
    }
    m_question.render(canvas);
    m_answers.render(canvas);
}

void YesNoBox::refreshAnswers() {
    // One marked-up line, "Yes / No", each answer in its selection colour.
    const graphics::Rgba yes = m_yesSelected ? kSelectedGrey : kDimGrey;
    const graphics::Rgba no = m_yesSelected ? kDimGrey : kSelectedGrey;
    m_answers.setText(std::format("{}{}</COLOR> / {}{}</COLOR>", colourTag(yes), m_yes, colourTag(no), m_no));
}

void YesNoBox::playCue(int cue) const {
    if (m_playCue) {
        m_playCue(cue);
    }
}

} // namespace coney::gui
