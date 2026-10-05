// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>
#include <string_view>
#include <vector>

#include "graphics/font.h"
#include "graphics/overlay_camera.h"
#include "graphics/render_device.h"
#include "graphics/sprite_batch.h"

namespace coney::gui {

/// Draws single lines of text in logical pixels for the pad debug menu, in one of two fonts: the game's own text font
/// when a disc is loaded, or Coney's built-in bitmap font without one. The view lays out with lineHeight() and
/// measure() and does not know which it has.
class DebugTextPainter {
  public:
    virtual ~DebugTextPainter() = default;
    DebugTextPainter() = default;
    DebugTextPainter(const DebugTextPainter&) = delete;
    DebugTextPainter& operator=(const DebugTextPainter&) = delete;
    DebugTextPainter(DebugTextPainter&&) = delete;
    DebugTextPainter& operator=(DebugTextPainter&&) = delete;

    /// The height of a line of text, logical pixels.
    [[nodiscard]] virtual float lineHeight() const = 0;
    /// The width of `text`, logical pixels.
    [[nodiscard]] virtual float measure(std::string_view text) const = 0;
    /// Queues `text` with its top-left corner at (x, y), logical pixels.
    virtual void text(std::string_view text, float x, float y, graphics::Rgba colour) = 0;
    /// Draws what was queued and forgets it.
    virtual void flush(graphics::RenderDevice& device) = 0;
};

/// The built-in bitmap font (graphics/bitmap_font.h) at `scale` logical pixels per font pixel, as flat quads.
class BitmapTextPainter final : public DebugTextPainter {
  public:
    /// A painter at `scale` (1: 6 x 9 logical pixels a character).
    explicit BitmapTextPainter(float scale = 1.0F) : m_scale(scale) {}

    [[nodiscard]] float lineHeight() const override;
    [[nodiscard]] float measure(std::string_view text) const override;
    void text(std::string_view text, float x, float y, graphics::Rgba colour) override;
    void flush(graphics::RenderDevice& device) override;

  private:
    float m_scale;
    std::vector<graphics::LogicalQuad> m_quads;
};

/// A font of the game (the text font, `part_page0`) through a sprite batch and the 2D pass, as the game draws its own
/// text (docs/research/gui.md#text), sized so a line is `lineHeight` logical pixels. Proportional, with no shadow.
class GameFontPainter final : public DebugTextPainter {
  public:
    /// The most glyphs a frame holds.
    static constexpr std::size_t kCapacity = 8192;
    /// The batch's depth: above the game's own text and menus (docs/research/gui.md#draw-order).
    static constexpr float kDepth = 12000.0F;

    /// Draws with `font` at a line height of `lineHeight` logical pixels.
    GameFontPainter(graphics::Font font, float lineHeight);

    [[nodiscard]] float lineHeight() const override { return m_lineHeight; }
    [[nodiscard]] float measure(std::string_view text) const override;
    void text(std::string_view text, float x, float y, graphics::Rgba colour) override;
    void flush(graphics::RenderDevice& device) override;

  private:
    graphics::Font m_font;
    float m_lineHeight;
    graphics::OverlayCamera m_camera;
    graphics::FontMetrics m_metrics;
    float m_guiToLogicalX = 1.0F;    // logical pixels per GUI unit across
    float m_guiToLogicalY = 1.0F;    // and down
    graphics::LogicalPoint m_origin; // GUI (0, 0) on the logical screen
    graphics::SpriteBatch m_batch;
    graphics::OverlayPass m_pass;
};

} // namespace coney::gui
