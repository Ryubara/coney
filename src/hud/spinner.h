// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>

#include "graphics/render_device.h"
#include "hud/hud_canvas.h"
#include "hud/hud_layout.h"

namespace coney::hud {

/// The spinner's `part_page0` rectangle, size (an overlay height), place (default video mode), set-up colour, and the
/// loading pulse's colour ((170, 43, 43) × 1.3) and cycle: transparent over the first half, back over the second.
inline constexpr std::size_t kSpinnerRect = 92;
inline constexpr float kSpinnerSize = 0.09F;
inline constexpr GuiPoint kSpinnerPlace{0.95F, 0.83F};
inline constexpr graphics::Rgba kSpinnerColour{191, 191, 191, 255};
inline constexpr graphics::Rgba kPulseColour{221, 56, 56, 255};
inline constexpr std::uint64_t kPulseCycleMs = 2200;

/// The HUD's "spinner" (HUD `+0xe050`, level loading's blinking element `0x0060e890`): an upright sprite that the
/// memory-card screen, the preload indicator and the HUD's own fade branch show while something loads. It never turns:
/// the rate `HUD_SetSpinner` is given feeds a global nothing reads. Drawn in the HUD's normal branch, after the
/// instruction arrow, while shown, in the colour the last pulse left on it (the set-up's grey before any).
///
/// Research: docs/research/hud.md#hud-spinner
class Spinner {
  public:
    /// `HUD_SetSpinner(rate, hud, on)`: shown or not (the rate is dead).
    /// @orig 0x001b24d0 HUD_SetSpinner (unknown)
    void set(bool on) { m_shown = on; }
    [[nodiscard]] bool shown() const { return m_shown; }
    /// The colour the widget holds now.
    [[nodiscard]] graphics::Rgba colour() const { return m_colour; }

    /// The pulse's colour at clock time `ms`: kPulseColour, its alpha falling linearly to 0 over the first 1,100 ms of
    /// each 2,200 ms and rising back over the second.
    [[nodiscard]] static graphics::Rgba pulseColour(std::uint64_t ms);
    /// `LoadScreen_DrawPulse`: the widget takes the pulse's colour at `ms` (which stays on it) and is drawn.
    /// **Coney's stand-in**: `ms` is the caller's clock, as Coney keeps no real-time clock.
    /// @orig 0x001613f0 LoadScreen_DrawPulse (unknown)
    void drawPulse(const HudCanvas& canvas, std::uint64_t ms);
    /// The normal branch's draw (`BaseWidget_Render`): the widget in its colour while shown.
    void render(const HudCanvas& canvas) const;

  private:
    // Adds the sprite in `colour`.
    static void addSprite(const HudCanvas& canvas, graphics::Rgba colour);

    bool m_shown = false;
    graphics::Rgba m_colour = kSpinnerColour;
};

} // namespace coney::hud
