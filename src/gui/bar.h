// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <optional>

#include "graphics/render_device.h"
#include "graphics/sprite_batch.h"
#include "gui/colour_table.h"
#include "gui/widget.h"

namespace coney::gui {

/// How a Bar is set up (`0x001a0fd0(width, height, 0.5, widget, position, backColour, spriteWord, 1, 0)`).
struct BarSetup {
    float x = 0.0F;                             ///< GUI x of the bar's left edge.
    float y = 0.5F;                             ///< GUI y of its centre.
    float width = 0.6F;                         ///< Width in overlay units.
    float height = 0.025F;                      ///< Height in overlay units.
    graphics::Rgba backColour{64, 64, 64, 255}; ///< The empty part.
    graphics::Rgba fillColour = kMenuRed;       ///< The filled part (`+0x28`).
};

/// A meter of two sprites of one sheet rectangle: the back at full width, the fill over it from the left edge to the
/// fill fraction (`+0x38`). PM_Light's brightness bar and the Rumble gang screen's two bars.
///
/// The left edge at the position is inferred from the runtime (PM_Light's bar starts at x 0); the fill drawn from that
/// edge is Coney's reading of "fill red to v / 100".
///
/// With end caps (setCaps(), the HUD meter `HudBar`'s four rectangles, as the pause Stats screen's bars draw them,
/// docs/research/hud.md#the-rage-meter): left to right, the left cap in the back colour, the body over the inner width
/// (the width less the left cap's) in the back colour, the fill over the inner width × the fill, the right cap in the
/// back colour just past the width. Each cap is as wide as its rectangle's texels make it at the bar's height; the
/// stretched body and fill are inset by one texel so that filtering stays inside them.
///
/// Research: docs/research/gui.md#widget-classes
/// @orig 0x001a0fd0 Bar::Bar (unknown)
class Bar : public Widget {
  public:
    /// A bar drawing rectangle `rect` of `batch`'s sheet. `batch` (not owned) must outlive the bar; null draws nothing.
    Bar(graphics::SpriteBatch* batch, std::size_t rect) : m_batch(batch), m_rect(rect), m_fillRect(rect) {}
    /// Draws the fill from rectangle `rect` from now on (the HUD bars' body and fill are two rectangles).
    void setFillRect(std::size_t rect) { m_fillRect = rect; }
    /// Draws end caps from rectangles `left` and `right` from now on (the meter's form above).
    void setCaps(std::size_t left, std::size_t right) { m_caps = std::array<std::size_t, 2>{left, right}; }

    /// Draws into `batch` from now on (null draws nothing).
    void setBatch(graphics::SpriteBatch* batch) { m_batch = batch; }
    /// Places, sizes and colours the bar.
    void setup(const BarSetup& setup) { m_setup = setup; }
    /// The fill fraction, clamped to [0, 1].
    void setFill(float fraction);
    /// The fill fraction.
    [[nodiscard]] float fill() const { return m_fill; }
    /// The set-up.
    [[nodiscard]] const BarSetup& setupValues() const { return m_setup; }

    /// Adds the back, then the fill (when not empty), with the caps when set. Nothing when hidden, without a batch or
    /// past the sheet.
    /// @orig 0x001a1138 Bar_Render (unknown)
    void render(const GuiCanvas& canvas) const override;

  private:
    graphics::SpriteBatch* m_batch;
    std::size_t m_rect;
    std::size_t m_fillRect;                           // the fill's rectangle; the back's unless set
    std::optional<std::array<std::size_t, 2>> m_caps; // the left and right caps' rectangles, when drawn
    BarSetup m_setup;
    float m_fill = 0.0F;
};

} // namespace coney::gui
