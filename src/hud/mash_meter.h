// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>

#include "hud/hud_canvas.h"

namespace coney::hud {

/// A player's mash meter (HUD `+0xebc0` + player × `0x430`): a button sprite, a bar filling with the mash and the
/// button glyphs blinking beside it. Only the uncuffing shows it (`Uncuff_BeginMash`, button id 1 and sprite word
/// `0x171`), so L1 and R1 alternate every 400 ms on either side of the button.
///
/// **Coney's readings**: the bar's back strip runs its whole width with the fill strip over it from the left end and
/// the two end caps outside it (how `HudBar_Draw` places its four rectangles is not on the page); the glyphs are drawn
/// at the text size as their height. **Coney's stand-in**: while the loaded `menu_system` page lacks rectangles 80-83,
/// the rage meter's `part_page0` rectangles draw the bar.
///
/// Research: docs/research/hud.md#mash-meter-layout
class MashMeter {
  public:
    /// The uncuffing's button sprite word: `part_page0` rectangle 369.
    static constexpr std::uint32_t kUncuffWord = 0x171;

    /// `HUD_MashMeterShow`: shows the meter with button id `button` (not 0) and sprite word `word`, empty.
    void show(int button, std::uint32_t word);
    /// `HUD_MashMeterHide`: the id, the word and the fill back to 0, so nothing is drawn.
    void hide();
    /// `MashMeter_SetFill`: the fill, clamped to 0-1 (the meter over its target).
    /// @orig 0x001a3338 MashMeter_SetFill (unknown)
    void setFill(float fill);

    /// Whether it is drawn (a button id other than 0).
    [[nodiscard]] bool shown() const { return m_button != 0; }
    /// The fill, 0-1.
    [[nodiscard]] float fill() const { return m_fill; }
    /// The glyph shown at game time `nowMs`: `0x9c` (R1) in the first 400 ms of every 800, `0xa0` (L1) in the second,
    /// for the uncuffing's word; for any other word `0x96` (triangle) in the second only; 0 for none.
    [[nodiscard]] char glyph(std::uint64_t nowMs) const;

    /// Adds player `player`'s meter at game time `nowMs`: the button sprite, the bar, then the blinking glyph.
    /// @orig 0x001a3360 MashMeter_Render (unknown)
    void render(const HudCanvas& canvas, std::size_t player, std::uint64_t nowMs) const;

  private:
    int m_button = 0;
    std::uint32_t m_word = 0;
    float m_fill = 0.0F;
};

} // namespace coney::hud
