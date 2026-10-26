// SPDX-License-Identifier: GPL-3.0-or-later
#include "gui/circled_text.h"

#include <algorithm>

#include "graphics/overlay_camera.h"

namespace coney::gui {

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
    if (m_setUp) {
        return;
    }
    m_setup = setup;
    // The text and the characters in the menus' font and the normal colour; the characters without a shadow. They are
    // centred on their circles: the original starts each one half its measured width left of the circle's centre.
    m_text.setup(TextWidgetSetup{.scale = setup.textSize, .colour = kDimGrey, .fontSlot = kFontSlot});
    for (TextWidget& character : m_chars) {
        character.setup(TextWidgetSetup{
            .scale = setup.textSize, .colour = kDimGrey, .alignment = TextAlignment::Centre, .fontSlot = kFontSlot});
        character.style().shadowAlpha = 0;
    }
    m_backColour = kBackColour;
    m_circleColour = kCircleColour;
    m_reveal = 1.0F;
    m_setUp = true;
}

void CircledText::setText(std::string_view text) { m_text.setText(text); }

void CircledText::setCircled(std::string_view characters) {
    // Stand-in: the count is capped at the two circles there are; the original writes past them.
    m_used = std::min(characters.size(), kCircles);
    for (std::size_t i = 0; i < m_used; ++i) {
        m_chars.at(i).setText(characters.substr(i, 1));
    }
}

void CircledText::setCharColour(graphics::Rgba colour) {
    for (TextWidget& character : m_chars) {
        character.style().colour = colour;
    }
}

void CircledText::setReveal(float reveal) { m_reveal = reveal >= 0.0F ? std::min(reveal, 1.0F) : 0.0F; }

void CircledText::update(const GuiFrame& frame) {
    if (!visible()) {
        return;
    }
    const CircledTextSetup& s = m_setup;
    // The back sprite over the rectangle, its height × the reveal with its top edge kept. Its size is set after the
    // widget's update, through the device's conversion, so the GUI width is stretched into overlay units.
    // Stand-in: drawn whole at that height, where the original also cuts its texture rectangle by the reveal.
    m_back.setup(BaseWidgetSetup{.x = s.x,
                                 .y = s.y - ((s.height - height()) / 2.0F),
                                 .height = height(),
                                 .width = graphics::OverlayCamera::guiWidthToOverlay(s.width),
                                 .colour = m_backColour,
                                 .anchor = SpriteAnchor::Left});

    // The circles from x0, each adding the step before and after itself (circleW is added to GUI x as it is); each
    // character centred on its circle at the row's y. Stand-in: the texts are drawn whole (no reveal in Coney's text).
    const float step = s.circleWidth / kStepDivisor;
    const float shownHeight = s.circleHeight * m_reveal;
    float x = s.x + s.circleOffset;
    float textX = x;
    for (std::size_t i = 0; i < m_used; ++i) {
        const float centre = x + step;
        TextWidget& character = m_chars.at(i);
        character.style().x = centre;
        character.style().y = s.y;
        character.update(frame);
        // The circle's size is set after its update, without the conversion, so it is drawn circleW wide as given and
        // not refitted to the rectangle's shape; the top edge stays put as the reveal shrinks it.
        // Stand-in: drawn whole, where the original also cuts its texture rectangle by the reveal.
        m_circles.at(i).setup(
            BaseWidgetSetup{.x = centre,
                            .y = s.y - ((s.circleHeight - shownHeight) / 2.0F) + (s.circleHeight * kCircleDrop),
                            .height = shownHeight,
                            .width = s.circleWidth,
                            .colour = m_circleColour});
        textX = centre + (s.circleWidth / 2.0F);
        x = centre + step;
    }
    m_text.style().x = textX;
    m_text.style().y = s.y;
    m_text.update(frame);
}

void CircledText::render(const GuiCanvas& canvas) const {
    if (!visible() || m_reveal <= 0.0F) {
        return;
    }
    m_back.render(canvas);
    m_text.style().colour = m_focused ? kSelectedGrey : kDimGrey;
    m_text.render(canvas);
    // Each circle and its character; a character that is not black takes the focused colour, focused or not.
    for (std::size_t i = 0; i < m_used; ++i) {
        m_circles.at(i).render(canvas);
        TextWidget& character = m_chars.at(i);
        if (character.style().colour != colourAt(0x005fd260)) {
            character.style().colour = kSelectedGrey;
        }
        character.render(canvas);
    }
}

} // namespace coney::gui
