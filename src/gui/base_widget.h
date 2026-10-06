// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>

#include "graphics/render_device.h"
#include "graphics/sprite_batch.h"
#include "gui/widget.h"

namespace coney::gui {

/// Which point of a sprite widget its position gives (`+0xc0`).
enum class SpriteAnchor : std::uint8_t {
    Centre = 0, ///< The sprite's centre is at the position.
    Left = 1,   ///< The sprite's left edge is at the position (it moves right by half its width).
    Right = 2,  ///< The sprite's right edge is at the position.
};

/// How a sprite widget is set up (the original's setup, slot `+0x68`, `0x001a1db8`, as far as the menus use it).
struct BaseWidgetSetup {
    float x = 0.5F;                           ///< GUI x of the anchor point.
    float y = 0.5F;                           ///< GUI y of the sprite's centre.
    float height = 0.1F;                      ///< Height in overlay units (1.1 is the screen's height).
    std::optional<float> width{};             ///< Width in overlay units; unset: the height × the rectangle's shape.
    graphics::Rgba colour = graphics::kWhite; ///< Multiplies the sprite; its alpha fades it.
    SpriteAnchor anchor = SpriteAnchor::Centre;
    bool aspectFix = false; ///< `+0xe0`: the width × the view aspect (1.45) × 0.80357 more.
    bool shadow = false;    ///< A shadow record (`+0xec`); the original's setup makes none.
};

/// A sprite widget: one rectangle of a sprite sheet, drawn each frame as one sprite of the batch over that sheet.
///
/// - **Size** in overlay units, not GUI units (1.1 is the screen's height, 1.595 its width in the default mode): the
///   height is kept and the width is the height × the rectangle's pixel shape ((u1 − u0) × texture width over
///   (v1 − v0) × texture height), times the view aspect × 0.80357 more with aspectFix; or a width given outright.
/// - **Anchor**: the position is the centre, the left edge or the right edge, applied when drawing.
/// - **Shadow** (only when asked for; the original's setup makes none): the sprite in black at the position +
///   (0.0025, 0.004) **without** the anchor's shift, at the widget's alpha × 0.502.
///
/// The original's optional timed fade (`+0xf0`-`+0xf8`) is not used by the menus Coney has; not implemented.
///
/// Research: docs/research/gui.md#widget-classes
/// @orig 0x001a1bf8 BaseWidget::BaseWidget (BaseWidget.cpp)
class BaseWidget : public Widget {
  public:
    /// The shadow's alpha factor: 0.502 of the widget's.
    static constexpr float kShadowFactor = 0.502F;
    /// The aspect fix's factor on top of the view aspect.
    static constexpr float kAspectFix = 0.80357F;

    /// A widget showing rectangle `rect` of `batch`'s sheet. `batch` (not owned) must outlive the widget; null draws
    /// nothing.
    BaseWidget(graphics::SpriteBatch* batch, std::size_t rect) : m_batch(batch), m_rect(rect) {}

    /// Draws into `batch` (not owned; null draws nothing) from now on: for a widget whose sheet is loaded after it was
    /// made.
    void setBatch(graphics::SpriteBatch* batch) { m_batch = batch; }
    /// Shows rectangle `rect` of the sheet from now on (the low half of the original's sprite word).
    void setRect(std::size_t rect) { m_rect = rect; }

    /// Places, sizes and colours the widget.
    /// @orig 0x001a1db8 BaseWidget_Setup (BaseWidget.cpp)
    void setup(const BaseWidgetSetup& setup) { m_setup = setup; }

    /// Adds the shadow (when set up with one), then the sprite, to the batch. Draws nothing when hidden, without a
    /// batch, or when the sheet has no rectangle `rect`.
    /// @orig 0x001a2690 BaseWidget_AddSprite (BaseWidget.cpp)
    void render(const GuiCanvas& canvas) const override;

    /// The set-up.
    [[nodiscard]] const BaseWidgetSetup& setupValues() const { return m_setup; }
    /// Sets the colour (alpha fades the sprite and its shadow).
    void setColour(graphics::Rgba colour) { m_setup.colour = colour; }
    /// The colour.
    [[nodiscard]] graphics::Rgba colour() const { return m_setup.colour; }

    /// The sprite's size in overlay units (width, height) as it is drawn: the width from the rectangle's shape unless
    /// set. A widget without a batch or rectangle has the height as its width.
    [[nodiscard]] std::pair<float, float> overlaySize() const;
    /// The sprite's centre in GUI units, after the anchor's shift.
    [[nodiscard]] std::pair<float, float> centre() const;

  private:
    graphics::SpriteBatch* m_batch;
    std::size_t m_rect;
    BaseWidgetSetup m_setup;
};

} // namespace coney::gui
