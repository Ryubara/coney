// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

#include "gui/base_widget.h"
#include "gui/profile_management_gui/pm_shared.h"
#include "gui/screen_flow_controller.h"
#include "gui/text_widget.h"

namespace coney::gui {

/// The profile manager's first screen, "press START": the game's logo and a blinking prompt. START on the HUD player's
/// pad leads to the main menu (PM_Mode) with front-end sound cue 9; 70 s without input call the Lua function
/// `Menu.playMovie(2)` (the attract movie).
///
/// - The prompt is global string 0x76; its alpha ramps 0 → 255 and back in alternate 1,500 ms halves, counted from the
///   screen's entry, while the profile manager is not finishing.
/// - START is read with the auto-repeating query, as the original does; START is not a d-pad bit, so that is its plain
///   press.
///
/// Coney's choices: the sprite is rectangle 0 of `menu_system` (the logo, from viewing the sheet); the layout is
/// PmLayout's (the logo keeps its rectangle's shape); "activity" that restarts the idle time is any button held or a
/// stick off centre on the HUD player's pad (the original reads two HUD fields, `0x005fdeb8` `+0x1d4` and `+0x1d8`, not
/// on the page); after the movie call the idle time starts again; the prompt is drawn in font slot 2 at scale 1.
///
/// Research: docs/research/frontend.md#profile-manager
class PmGreet final : public ScreenFlowState {
  public:
    /// Result: on to PM_Mode.
    static constexpr int kToMode = 0;
    /// Result: on to PM_NoSpace (not produced by Coney: no memory card).
    static constexpr int kToNoSpace = 1;
    /// Result: on to PM_TooManyProfiles (not produced by Coney: no memory card).
    static constexpr int kToTooManyProfiles = 2;
    /// The prompt's global string.
    static constexpr std::uint32_t kPromptString = 0x76;
    /// Half the prompt's blink: the time it takes to fade in, and to fade out.
    static constexpr std::uint64_t kBlinkHalfMs = 1500;
    /// Idle time before the attract movie.
    static constexpr std::uint64_t kIdleMs = 70'000;
    /// The front-end sound cue START plays.
    static constexpr int kStartCue = 9;
    /// The `menu_system` rectangle the logo is.
    static constexpr std::size_t kLogoRect = 0;

    /// A screen over `shared`, which must outlive it.
    explicit PmGreet(PmShared& shared);

    [[nodiscard]] std::string_view name() const override { return "PM_Greet"; }

    /// Makes the widgets and starts the blink and the idle time.
    /// @orig 0x00207d48 PM_Greet_Enter (PM_Greet.cpp)
    /// @orig 0x002079a0 PM_Greet::Init (PM_Greet.cpp)
    void enter(ScreenFlowController& flow) override;

    /// One frame: blink, START, idle; then draws. Returns kToMode on START, kStay otherwise.
    /// @orig 0x00207dd0 PM_Greet_Update (PM_Greet.cpp)
    /// @orig 0x00207e28 PM_Greet::Update (PM_Greet.cpp)
    /// @orig 0x00208288 PM_Greet::Render (PM_Greet.cpp)
    int update() override;

    /// Releases the widgets.
    /// @orig 0x00207da0 PM_Greet_Exit (PM_Greet.cpp)
    void exit() override;

    /// The prompt's alpha at game time `timeMs` for a screen entered at `enteredMs`: 0 → 255 over the first half,
    /// 255 → 0 over the second, repeating.
    [[nodiscard]] static std::uint8_t promptAlpha(std::uint64_t enteredMs, std::uint64_t timeMs);

    /// The prompt widget.
    [[nodiscard]] const TextWidget& prompt() const { return m_prompt; }
    /// The logo widget.
    [[nodiscard]] const BaseWidget& logo() const { return m_logo; }

  private:
    // The logo's GUI height for a GUI width of `width`, keeping its rectangle's shape; `width` without a sheet.
    [[nodiscard]] float logoHeight(float width) const;

    PmShared& m_shared;
    BaseWidget m_logo;
    TextWidget m_prompt;
    std::uint64_t m_enteredMs = 0;
    std::uint64_t m_lastActivityMs = 0;
};

} // namespace coney::gui
