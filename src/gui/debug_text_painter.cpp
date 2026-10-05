// SPDX-License-Identifier: GPL-3.0-or-later
#include "gui/debug_text_painter.h"

#include <utility>

#include "graphics/bitmap_font.h"

namespace coney::gui {

float BitmapTextPainter::lineHeight() const { return static_cast<float>(graphics::BitmapFont::kCellHeight) * m_scale; }

float BitmapTextPainter::measure(std::string_view text) const { return graphics::BitmapFont::measure(text, m_scale); }

void BitmapTextPainter::text(std::string_view text, float x, float y, graphics::Rgba colour) {
    // The glyph sits a font pixel below the top of its cell, so a line's two spare rows split above and below.
    graphics::BitmapFont::draw(m_quads, text, x, y + m_scale, m_scale, colour);
}

void BitmapTextPainter::flush(graphics::RenderDevice& device) {
    if (!m_quads.empty()) {
        device.drawQuads(nullptr, m_quads);
    }
    m_quads.clear();
}

GameFontPainter::GameFontPainter(graphics::Font font, float lineHeight)
    : m_font(std::move(font)), m_lineHeight(lineHeight), m_batch(m_font.sheet(), kCapacity, kDepth) {
    // GUI coordinates map linearly onto the logical screen (a fixed depth through the overlay camera): find the map
    // from two corners, then the font scale whose glyph height is the line height.
    m_origin = m_camera.guiToLogical(0.0F, 0.0F);
    const graphics::LogicalPoint far = m_camera.guiToLogical(1.0F, 1.0F);
    m_guiToLogicalX = far.x - m_origin.x;
    m_guiToLogicalY = far.y - m_origin.y;
    const float unitHeight = graphics::fontMetrics(1.0F).height * m_guiToLogicalY;
    m_metrics = graphics::fontMetrics(lineHeight / unitHeight);
}

float GameFontPainter::measure(std::string_view text) const {
    return m_font.measure(text, m_metrics, graphics::kFontProportional) * m_guiToLogicalX;
}

void GameFontPainter::text(std::string_view text, float x, float y, graphics::Rgba colour) {
    std::vector<graphics::Sprite> sprites;
    const float guiX = (x - m_origin.x) / m_guiToLogicalX;
    const float guiY = (y + m_lineHeight * 0.5F - m_origin.y) / m_guiToLogicalY;
    m_font.draw(sprites, text, guiX, guiY, m_metrics, graphics::kFontProportional, colour);
    for (const graphics::Sprite& sprite : sprites) {
        m_batch.addSprite(sprite);
    }
}

void GameFontPainter::flush(graphics::RenderDevice& device) {
    m_pass.queue(m_batch);
    m_pass.render(device, m_camera);
}

} // namespace coney::gui
