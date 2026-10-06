// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "gui/menu_input.h"
#include "gui/option_grid.h"
#include "gui/text_widget.h"
#include "gui/usage_info.h"
#include "gui/widget.h"

namespace coney::gui {

/// Where a timed message sits (the last argument of `0x001c6fc8`).
enum class MessageStyle : std::uint8_t {
    Centre = 0, ///< Centred at (0.5, 0.5), grey, `big_font`.
    Corner = 1, ///< At (`0x0050ea24`, 0.9), (134, 26, 26), font slot 4: the autosave notice.
};

/// The message box (the one at `0x005e5840`): the memory-card mode's timed messages and choice dialogs.
///
/// - **Timed message** (`0x001c6fc8`): a text for a time, then the box is done.
/// - **Choice dialog** (`0x001c7128`): the message, a one-row OptionGrid centred at y kChoiceY whose items are
///   kDimGrey (the selected one kSelectedGrey; a single choice gets an empty label), and the usage line (global
///   string 0x1d with one choice, 0x23 with two) centred at y kUsageY. Accept plays cue kAcceptCue and ends the
///   dialog with that choice; the original runs the choice's callback once the sound has finished (inferred), Coney
///   at once (it has no sound to wait for).
///
/// Coney's choices where the page is silent: the message's lines are centred as a block on y 0.5 (the measured format
/// question's four lines span 0.44 to 0.57); the choices are drawn at font scale 1.0 in part_page0 with kMoveCue for a
/// move; back does nothing; Corner's x (`0x0050ea24`, not read) is 0.5, centred; font slot 4 is Coney's text font;
/// the pad is the HUD player's (the original gives player 1 the first pad pressed while the box is open).
///
/// Research: docs/research/frontend.md#message-box
/// @orig 0x001c6a20 MessageBox::MessageBox (unknown)
class MessageBox : public Widget {
  public:
    /// The GUI y of the choices (`0x0050ea2c`) and of the usage line (`0x0050ea30`).
    static constexpr float kChoiceY = 0.745F;
    static constexpr float kUsageY = 0.8F;
    /// The usage line's string with one choice, and with two.
    static constexpr std::uint32_t kUsageOneChoice = 0x1d;
    static constexpr std::uint32_t kUsageTwoChoices = 0x23;
    /// The cue of accept.
    static constexpr int kAcceptCue = 8;
    /// The cue of a move among the choices (the grid's default).
    static constexpr int kMoveCue = 4;
    /// The colour of a Corner message.
    static constexpr graphics::Rgba kCornerColour{134, 26, 26, 255};

    /// Shows `text` in `style` for `durationMs` of game time from `nowMs`; the box is done after it.
    /// @orig 0x001c6fc8 MessageBox_ShowTimed (unknown)
    void showTimed(std::string_view text, std::uint64_t durationMs, MessageStyle style, std::uint64_t nowMs);

    /// Shows `text` with the choices `labels` (one or two), `defaultChoice` selected, and the usage line `usage`;
    /// `input` takes the focus at `nowMs` and must outlive the dialog.
    /// @orig 0x001c7128 MessageBox_ShowChoice (unknown)
    void showChoice(std::string_view text, const std::vector<std::string>& labels, std::size_t defaultChoice,
                    std::string_view usage, MenuInput& input, std::uint64_t nowMs);

    /// Sends front-end sound cues to `playCue` from now on (empty: none).
    void setSoundSink(std::function<void(int cue)> playCue) { m_playCue = std::move(playCue); }

    /// One frame: a timed message ends at its time; a dialog reads `frame`'s pad through the input it was shown with.
    /// @orig 0x001c7630 MessageBox_Update (unknown)
    void update(const GuiFrame& frame) override;

    /// Draws the message, and the choices and the usage line of a dialog. Nothing when closed.
    /// @orig 0x001c76a8 MessageBox_Render (unknown)
    void render(const GuiCanvas& canvas) const override;

    /// Whether a message or a dialog is showing.
    [[nodiscard]] bool open() const { return m_open; }
    /// The choice the last dialog ended with, until the next show; nothing for a timed message or an open dialog.
    [[nodiscard]] std::optional<std::size_t> chosen() const { return m_chosen; }
    /// Closes the box at once.
    void close() { m_open = false; }

    /// The message widget.
    [[nodiscard]] const TextWidget& message() const { return m_message; }
    /// The choices.
    [[nodiscard]] const OptionGrid& choices() const { return m_choices; }
    /// The usage line.
    [[nodiscard]] const UsageInfo& usage() const { return m_usage; }
    /// Whether the open box is a dialog.
    [[nodiscard]] bool dialog() const { return m_dialog; }

  private:
    // Sets the message's text and style.
    void setMessage(std::string_view text, MessageStyle style);
    // Plays `cue`, if the box has a way to.
    void playCue(int cue) const;

    TextWidget m_message;
    OptionGrid m_choices;
    UsageInfo m_usage;
    MenuInput* m_input = nullptr;
    std::function<void(int cue)> m_playCue;
    std::optional<std::size_t> m_chosen;
    std::uint64_t m_endMs = 0;
    bool m_open = false;
    bool m_dialog = false;
};

} // namespace coney::gui
