// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>

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
/// Research: docs/research/gui.md#widget-classes
/// @orig 0x001a0fd0 Bar::Bar (unknown)
class Bar : public Widget {
  public:
    /// A bar drawing rectangle `rect` of `batch`'s sheet. `batch` (not owned) must outlive the bar; null draws nothing.
    Bar(graphics::SpriteBatch* batch, std::size_t rect) : m_batch(batch), m_rect(rect) {}

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

    /// Adds the back, then the fill (when not empty). Nothing when hidden, without a batch or past the sheet.
    /// @orig 0x001a1138 Bar_Render (unknown)
    void render(const GuiCanvas& canvas) const override;

  private:
    graphics::SpriteBatch* m_batch;
    std::size_t m_rect;
    BarSetup m_setup;
    float m_fill = 0.0F;
};

} // namespace coney::gui
