// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

#include "gui/menu_input.h"
#include "gui/text_widget.h"
#include "gui/widget.h"

namespace coney::gui {

/// What the Yes/No box ended with: the codes its callback receives (2 Yes, 3 No, 4 back).
enum class YesNoAnswer : std::uint8_t {
    Yes = 2,
    No = 3,
    Back = 4,
};

/// Where a Yes/No box puts its question (left edge, first line's centre) and its answers (left edge, centre).
struct YesNoPlacement {
    float questionX = 0.26F;
    float questionY = 0.55F;
    float answersX = 0.6F;
    float answersY = 0.75F;
};

/// The pause and mission-failed menus' Yes/No box (menu `+0x1bd0`): a question, then "Yes" "/" "No" with **No
/// selected** when it opens. While it is open the menu hands it every command: up and down play cue 0xe (nothing to
/// move to), left and right move between Yes and No, accept plays cue 8 and answers Yes or No, back answers Back. The
/// box closes on any answer; the owner reads answer() once.
///
/// Coney's stand-ins (docs/research/pause.md#open-questions): the texts' fonts and sizes are not on the page, so the
/// question is drawn in the text font at size 1.0 and the answers in `big_font` at 1.15, both kMenuGrey; the selected
/// answer is kSelectedGrey and the other kDimGrey (the grid's colours); left and right play the grid's move cue 4 and
/// back cue 0xf.
///
/// Research: docs/research/pause.md#the-items-screens
/// @orig 0x001d6738 YesNoBox_Setup (unknown)
class YesNoBox : public Widget {
  public:
    /// Cues: a refused move, a move, accept, back.
    static constexpr int kRefusedCue = 0xe;
    static constexpr int kMoveCue = 4;
    static constexpr int kAcceptCue = 8;
    static constexpr int kBackCue = 0xf;

    /// Places the box's texts.
    void place(const YesNoPlacement& placement) { m_placement = placement; }
    /// Sends cues to `playCue` (empty: none).
    void setSoundSink(std::function<void(int cue)> playCue) { m_playCue = std::move(playCue); }

    /// Opens the box asking `question`, the answers labelled `yes` and `no`, No selected.
    void open(std::string_view question, std::string_view yes, std::string_view no);
    /// Closes it without an answer.
    void close() { m_open = false; }
    /// Whether the box is open.
    [[nodiscard]] bool isOpen() const { return m_open; }

    /// Acts on one command (the menu passes every command while the box is open); returns the answer it ends with.
    /// @orig 0x001d6c10 YesNoBox_OnCommand (unknown)
    std::optional<YesNoAnswer> handle(MenuCommand command);

    /// Whether Yes is selected.
    [[nodiscard]] bool yesSelected() const { return m_yesSelected; }
    /// The menu's alpha (0 to 1) the texts are drawn with.
    void setFade(float fade);

    /// Advances the texts' time.
    void update(const GuiFrame& frame) override;
    /// Draws the question and the answers while open.
    void render(const GuiCanvas& canvas) const override;

    /// The question's widget.
    [[nodiscard]] const TextWidget& question() const { return m_question; }
    /// The answers' widget (one marked-up text).
    [[nodiscard]] const TextWidget& answers() const { return m_answers; }

  private:
    // Rebuilds the answers' text with the selection's colours.
    void refreshAnswers();
    // Plays `cue`, if there is a sink.
    void playCue(int cue) const;

    TextWidget m_question;
    TextWidget m_answers;
    YesNoPlacement m_placement;
    std::function<void(int cue)> m_playCue;
    std::string m_yes;
    std::string m_no;
    bool m_open = false;
    bool m_yesSelected = false;
};

} // namespace coney::gui
