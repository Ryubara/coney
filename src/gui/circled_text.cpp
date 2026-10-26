// SPDX-License-Identifier: GPL-3.0-or-later
#include "gui/circled_text.h"

#include <algorithm>

#include "graphics/screen.h"

namespace coney::gui {

namespace {

// An overlay width for a width in GUI widths (BaseWidget's sizes are in overlay units: GUI heights).
float overlayWidth(float guiWidth) { return guiWidth * graphics::kLogicalWidth / graphics::kLogicalHeight; }

// A GUI width for an overlay width.
float guiWidth(float overlay) { return overlay * graphics::kLogicalHeight / graphics::kLogicalWidth; }

} // namespace

CircledText::CircledText() { setup(CircledTextSetup{}); }

void CircledText::setBack(graphics::SpriteBatch* batch, std::size_t rect) {
    m_back.setBatch(batch);
    m_back.setRect(rect);
}

void CircledText::setCircleSprite(graphics::SpriteBatch* batch, std::size_t rect) {
    for (BaseWidget& circle : m_circles) {
        circle.setBatch(batch);
        circle.setRect(rect);
    }
}

void CircledText::setup(const CircledTextSetup& setup) {
    m_setup = setup;
    // The texts in the menus' font; the characters as big as the text.
    m_text.setup(TextWidgetSetup{.scale = setup.textSize, .colour = kDimGrey, .fontSlot = kFontSlot});
    for (TextWidget& character : m_chars) {
        character.setup(TextWidgetSetup{
            .scale = setup.textSize, .colour = kMenuGrey, .alignment = TextAlignment::Centre, .fontSlot = kFontSlot});
    }
    m_reveal = 1.0F;
}

void CircledText::setText(std::string_view text) { m_text.setText(text); }

void CircledText::setCircled(std::string_view characters) {
    m_used = std::min(characters.size(), kCircles);
    for (std::size_t i = 0; i < kCircles; ++i) {
        m_chars.at(i).setText(i < m_used ? characters.substr(i, 1) : std::string_view{});
    }
}

void CircledText::setCharColour(graphics::Rgba colour) {
    for (TextWidget& character : m_chars) {
        character.style().colour = colour;
    }
}

void CircledText::setReveal(float reveal) { m_reveal = std::clamp(reveal, 0.0F, 1.0F); }

void CircledText::update(const GuiFrame& frame) {
    m_text.update(frame);
    for (TextWidget& character : m_chars) {
        character.update(frame);
    }
}

void CircledText::render(const GuiCanvas& canvas) const {
    if (!visible() || m_reveal <= 0.0F) {
        return;
    }
    const CircledTextSetup& s = m_setup;
    // The back sprite over the rectangle, its height × the reveal.
    m_back.setup(BaseWidgetSetup{.x = s.x,
                                 .y = s.y,
                                 .height = height(),
                                 .width = overlayWidth(s.width),
                                 .colour = m_backColour,
                                 .anchor = SpriteAnchor::Left});
    m_back.render(canvas);

    // The text in the focused colour when focused.
    m_text.style().colour = m_focused ? kSelectedGrey : kDimGrey;

    // The circles left to right from the left edge, each step circleW / 3; the text after the last.
    const float step = guiWidth(s.circleWidth) / kStepDivisor;
    float x = s.x + s.circleOffset + (guiWidth(s.circleWidth) / 2.0F);
    for (std::size_t i = 0; i < m_used; ++i) {
        m_circles.at(i).setup(BaseWidgetSetup{
            .x = x, .y = s.y, .height = s.circleHeight * m_reveal, .width = s.circleWidth, .colour = m_circleColour});
        m_circles.at(i).render(canvas);
        m_chars.at(i).style().x = x;
        m_chars.at(i).style().y = s.y;
        m_chars.at(i).render(canvas);
        x += step;
    }
    const float textX = m_used > 0 ? x - step + (guiWidth(s.circleWidth) / 2.0F) : s.x + s.circleOffset;
    m_text.style().x = textX + s.textOffset;
    m_text.style().y = s.y;
    m_text.render(canvas);
}

} // namespace coney::gui
