// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>

#include "graphics/render_device.h"
#include "graphics/sprite_batch.h"
#include "gui/widget.h"

namespace coney::gui {

/// Where a sprite widget sits, in GUI units (x to the right, y down, the screen about [0, 1]²).
struct WidgetRect {
    float x = 0.5F;      ///< The centre's x.
    float y = 0.5F;      ///< The centre's y.
    float width = 0.0F;  ///< GUI width (converted to the overlay camera's space by × W / H).
    float height = 0.0F; ///< GUI height.
};

/// A sprite widget: one rectangle of a sprite sheet, drawn each frame as one sprite of the batch over that sheet, with
/// a black drop shadow under it.
///
/// The shadow is the same sprite offset by (0.0025, 0.004) GUI units, black, at the widget's alpha × 128 / 255
/// (docs/research/graphics.md#2d-drawing).
///
/// Coney's choice: the rectangle is the sprite's centre and size; the original's setup takes a rectangle whose
/// reference point is not on the page.
///
/// Research: docs/research/gui.md#a-frame-of-2d
/// @orig 0x001a1bf8 BaseWidget::BaseWidget (BaseWidget.cpp)
class BaseWidget : public Widget {
  public:
    /// The alpha of the shadow at full widget alpha: 128 of 255.
    static constexpr int kShadowAlpha = 128;

    /// A widget showing rectangle `rect` of `batch`'s sheet. `batch` (not owned) must outlive the widget; null draws
    /// nothing.
    BaseWidget(graphics::SpriteBatch* batch, std::size_t rect) : m_batch(batch), m_rect(rect) {}

    /// Draws into `batch` (not owned; null draws nothing) from now on: for a widget whose sheet is loaded after it was
    /// made.
    void setBatch(graphics::SpriteBatch* batch) { m_batch = batch; }

    /// Places the widget and sets its colour (alpha included) and whether it casts a shadow.
    void setup(const WidgetRect& rect, graphics::Rgba colour, bool shadow);

    /// Adds the shadow, then the sprite, to the batch. Draws nothing when hidden, without a batch, or when the sheet
    /// has no rectangle `rect`.
    /// @orig 0x001a2690 BaseWidget_AddSprite (BaseWidget.cpp)
    void render(const GuiCanvas& canvas) const override;

    /// Where the widget is.
    [[nodiscard]] const WidgetRect& rect() const { return m_place; }
    /// The widget's colour.
    [[nodiscard]] graphics::Rgba colour() const { return m_colour; }
    /// Sets the colour (alpha fades the sprite and its shadow).
    void setColour(graphics::Rgba colour) { m_colour = colour; }

  private:
    graphics::SpriteBatch* m_batch;
    std::size_t m_rect;
    WidgetRect m_place;
    graphics::Rgba m_colour = graphics::kWhite;
    bool m_shadow = true;
};

} // namespace coney::gui
