// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "gui/base_widget.h"
#include "gui/profile_management_gui/pm_shared.h"
#include "gui/screen_flow_controller.h"
#include "gui/text_widget.h"

namespace coney::gui {

/// The profile manager's first screen, "press START": the game's logo and a blinking prompt.
///
/// - **Logo:** `menu_system` rectangle 0, tinted kMenuRed, its own batch at depth 11,000 (PmShared::frontSprites),
///   its **left edge** at the layout's x, centred on y 0.2, 0.33 overlay units high (0.23 and 0.27 with the flag 0x02
///   alone), its width from the rectangle's shape, no shadow.
/// - **Prompt:** global string 0x76 at (x, the one-row grid y 0.81), size 1.15, kMenuRed, `big_font`, left-aligned.
///   It blinks: phase 1 ramps its alpha 0 → 255 over kBlinkPeriodMs, phase 0 back to 0, then the phase flips. While
///   the screen fade runs or is not clear the prompt is fully lit, the phase is forced to 1, its timer restarts and so
///   does the idle clock.
/// - **START** (the auto-repeating query) leads to PM_Mode (result 0) with cue 9, and the screen stops drawing; it is
///   ignored while the attract flag is set and a fade runs. No back.
/// - **Idle:** after kIdleMs without a fade the screen sets its attract flag (`+0xac`, never cleared here), restarts
/// the
///   clock and calls the Lua function `Menu.playMovie(2)`, the attract movie.
///
/// Coney's choices: the clocks are game time, not the original's real time; the original also resets the Lua pad
/// handlers on entry, which Coney does not have yet.
///
/// Research: docs/research/frontend.md#pm-screens
class PmGreet final : public ScreenFlowState {
  public:
    /// Result: on to PM_Mode.
    static constexpr int kToMode = 0;
    /// Result: on to PM_NoSpace (Xbox only; never produced).
    static constexpr int kToNoSpace = 1;
    /// Result: on to PM_TooManyProfiles (Xbox only; never produced).
    static constexpr int kToTooManyProfiles = 2;
    /// The prompt's global string.
    static constexpr std::uint32_t kPromptString = 0x76;
    /// The blink's period (`+0x98`): the time a ramp up, or down, takes.
    static constexpr std::uint64_t kBlinkPeriodMs = 1500;
    /// Time without a screen fade before the attract movie.
    static constexpr std::uint64_t kIdleMs = 70'000;
    /// The Lua function the attract movie is asked from, and its argument: entry 2 of `Menu.movies`, `L1_IN`.
    static constexpr std::string_view kPlayMovieFunction = "Menu.playMovie";
    static constexpr double kAttractMovie = 2.0;
    /// The front-end sound cue START plays.
    static constexpr int kStartCue = 9;
    /// The `menu_system` rectangle the logo is (sprite word `0x30000`).
    static constexpr std::size_t kLogoRect = 0;
    /// The logo's centre y (`0x0050f628`) and height in overlay units (`0x0050f624`), default and with flag 0x02.
    static constexpr float kLogoY = 0.2F;
    static constexpr float kLogoHeight = 0.33F;
    static constexpr float kLogoYFlag02 = 0.23F;
    static constexpr float kLogoHeightFlag02 = 0.27F;

    /// A screen over `shared`, which must outlive it.
    explicit PmGreet(PmShared& shared);

    [[nodiscard]] std::string_view name() const override { return "PM_Greet"; }

    /// Makes the widgets and starts the blink and the idle clock.
    /// @orig 0x00207d48 PM_Greet_Enter (PM_Greet.cpp)
    /// @orig 0x002079a0 PM_Greet::Init (PM_Greet.cpp)
    void enter(ScreenFlowController& flow) override;

    /// One frame: blink, idle, START; then draws (unless START was taken). Returns kToMode on START, kStay otherwise.
    /// @orig 0x00207dd0 PM_Greet_Update (PM_Greet.cpp)
    /// @orig 0x00207e28 PM_Greet::Update (PM_Greet.cpp)
    /// @orig 0x00208288 PM_Greet::Render (PM_Greet.cpp)
    int update() override;

    /// Releases the widgets.
    /// @orig 0x00207da0 PM_Greet_Exit (PM_Greet.cpp)
    void exit() override;

    /// The prompt's alpha for `elapsedMs` into a phase: phase 1 ramps 0 → 255, phase 0 255 → 0, over kBlinkPeriodMs.
    [[nodiscard]] static std::uint8_t blinkAlpha(bool rising, std::uint64_t elapsedMs);
    /// The same blink as one clock: the alpha at game time `timeMs` for a blink started (rising) at `enteredMs`, for
    /// prompts that blink the same way (PM_NumPlayers' `0x77`).
    [[nodiscard]] static std::uint8_t promptAlpha(std::uint64_t enteredMs, std::uint64_t timeMs);

    /// The prompt widget.
    [[nodiscard]] const TextWidget& prompt() const { return m_prompt; }
    /// The logo widget.
    [[nodiscard]] const BaseWidget& logo() const { return m_logo; }
    /// Whether the attract movie has been asked for since the screen was entered (`+0xac`).
    [[nodiscard]] bool attracting() const { return m_attract; }

  private:
    PmShared& m_shared;
    BaseWidget m_logo;
    TextWidget m_prompt;
    bool m_rising = true;             // the blink's phase (`+0x9c`)
    std::uint64_t m_phaseStartMs = 0; // when the phase began
    std::uint64_t m_idleSinceMs = 0;  // the idle clock (`+0xa8`)
    bool m_attract = false;           // `+0xac`
};

} // namespace coney::gui
