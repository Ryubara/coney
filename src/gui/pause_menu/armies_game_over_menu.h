// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string_view>
#include <utility>

#include "graphics/render_device.h"
#include "gui/global_strings.h"
#include "gui/menu_input.h"
#include "gui/option_grid.h"
#include "gui/text_widget.h"
#include "gui/usage_info.h"
#include "gui/widget.h"

namespace coney::gui {

/// How the Armies of the Night game-over screen ended.
enum class ArmiesGameOverChoice : std::uint8_t {
    Continue = 0, ///< Start pressed with a credit left: the closing players play on (`+0x60` with `+0x7ac` / `+0x7b0`).
    Replay = 1,   ///< The end phase's first item (`+0x7a4`): the first stage again.
    Quit = 2,     ///< Its second item (`+0x7a8`): out of the bonus game.
};

/// The Armies of the Night game-over screen (`GameMenu` `0x00623040`), which mode 0xd shows (`HUDLaunchANGameOver`,
/// or the stage's timer running out).
///
/// - **Countdown phase** (open()): the title, string 0xce ("CONTINUE?"), font slot 6 at scale 2.0, centred at
///   (0.5, 0.45); below it a digit at scale 5.0 at (0.5, 0.6) counting 9 down to 0, one step every 1,000 ms; the usage
///   line 0xd6 at (0.5, 0.8); all in (150, 30, 30). Start (pressStart()) ends the screen with a credit, or starts the
///   end phase without one; so does the count passing 0.
/// - **End phase**: the text 0xcf at scale 3.0 at (0.5, 0.5), a grid of 0xdb : 0xdc (scale 1.15, font slot 3) at
///   y 0.57, the usage line 0x1c at (0.5, 0.62), all fading in over 2,000 ms; only then does accept take an item
///   (cue 8).
///
/// Positions are the NTSC defaults (`0x0050ebf0`-`0x0050ec24`). **Coney's stand-in**: the original moves some for
/// other video modes (display flags 2, 4 and 8) in ANGameOver_Open, and Coney has only the default one.
///
/// Research: docs/research/pause.md#an-game-over
/// @orig 0x001cef10 ANGameOver_Open (unknown)
class ArmiesGameOverMenu : public Widget {
  public:
    /// The global strings it shows.
    static constexpr std::uint32_t kTitleString = 0xce;
    static constexpr std::uint32_t kCountdownUsageString = 0xd6;
    static constexpr std::uint32_t kEndString = 0xcf;
    static constexpr std::uint32_t kReplayString = 0xdb;
    static constexpr std::uint32_t kQuitString = 0xdc;
    static constexpr std::uint32_t kEndUsageString = 0x1c;
    /// The colour of every part (`0x006239c0`).
    static constexpr graphics::Rgba kColour{150, 30, 30, 255};
    /// The countdown's first digit and its step (`0x0050ec0c`), and the end phase's fade-in (`0x0050ec28`).
    static constexpr int kCountdownFrom = 9;
    static constexpr std::uint64_t kStepMs = 1000;
    static constexpr std::uint64_t kEndFadeMs = 2000;
    /// The front-end cue of Start and of accept.
    static constexpr int kCue = 8;
    /// The layout (NTSC).
    static constexpr float kTitleY = 0.45F;
    static constexpr float kTitleScale = 2.0F;
    static constexpr float kDigitY = 0.6F;
    static constexpr float kDigitScale = 5.0F;
    static constexpr float kCountdownUsageY = 0.8F;
    static constexpr float kEndY = 0.5F;
    static constexpr float kEndScale = 3.0F;
    static constexpr float kGridY = 0.57F;
    static constexpr float kEndUsageY = 0.62F;

    /// Shows strings from `strings` (which must outlive the menu; null shows empty texts).
    void setStrings(const GlobalStrings* strings) { m_strings = strings; }
    /// Sends front-end sound cues to `playCue` (empty: none).
    void setSoundSink(std::function<void(int cue)> playCue) { m_playCue = std::move(playCue); }

    /// Opens the countdown phase at `nowMs`: the digit at 9, no choice.
    /// @orig 0x001cef10 ANGameOver_Open (unknown)
    void open(std::uint64_t nowMs);

    /// Start from a player in the countdown phase: cue 8; with a credit (`haveCredit`) the screen ends with
    /// ArmiesGameOverChoice::Continue, without one the end phase starts at `nowMs`. Nothing in the end phase.
    /// @orig 0x001cf468 ANGameOver_Update (unknown)
    void pressStart(bool haveCredit, std::uint64_t nowMs);

    /// One frame: the countdown's step (the end phase once 0 has shown for a step), the end phase's fade and, once it
    /// is in, the grid's moves and accept from `frame`'s pad.
    /// @orig 0x001cf468 ANGameOver_Update (unknown)
    /// @orig 0x001cf3a8 ANGameOver_OnCommand (unknown)
    void update(const GuiFrame& frame) override;
    /// The countdown's title, digit and usage line, or the end phase's text, grid and usage line.
    /// @orig 0x001cfc78 ANGameOver_Render (unknown)
    void render(const GuiCanvas& canvas) const override;

    /// Whether the screen is open.
    [[nodiscard]] bool isOpen() const { return m_open; }
    /// Whether the end phase has started (`+0x960`).
    [[nodiscard]] bool ending() const { return m_ending; }
    /// The digit the countdown shows (`+0x968`).
    [[nodiscard]] int count() const { return m_count; }
    /// When the end phase started, ms (`+0x700`).
    [[nodiscard]] std::uint64_t endStartMs() const { return m_endStartMs; }
    /// The choice once the screen has ended; nothing while it is open.
    [[nodiscard]] std::optional<ArmiesGameOverChoice> choice() const { return m_choice; }
    /// The grid of the end phase.
    [[nodiscard]] const OptionGrid& grid() const { return m_grid; }
    /// The digit's widget.
    [[nodiscard]] const TextWidget& digit() const { return m_digit; }
    /// The title's widget (0xce in the countdown, 0xcf in the end phase).
    [[nodiscard]] const TextWidget& title() const { return m_title; }

  private:
    // String `id`, or empty without strings.
    [[nodiscard]] std::string_view string(std::uint32_t id) const;
    // Plays `cue`, if there is a sink.
    void playCue(int cue) const;
    // The end phase from `nowMs`: the texts and the grid placed, faded out.
    void startEnd(std::uint64_t nowMs);

    const GlobalStrings* m_strings = nullptr;
    std::function<void(int cue)> m_playCue;
    MenuInput m_input;
    TextWidget m_title;
    TextWidget m_digit;
    OptionGrid m_grid;
    UsageInfo m_usage;
    std::optional<ArmiesGameOverChoice> m_choice;
    std::uint64_t m_stepMs = 0;     // +0x964: when the digit last changed
    std::uint64_t m_endStartMs = 0; // +0x700
    int m_count = kCountdownFrom;   // +0x968
    bool m_open = false;
    bool m_ending = false;       // +0x960
    bool m_acceptsInput = false; // +0x64: the end phase has faded in
};

} // namespace coney::gui
