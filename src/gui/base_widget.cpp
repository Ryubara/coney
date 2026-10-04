// SPDX-License-Identifier: GPL-3.0-or-later
#include "gui/base_widget.h"

#include <cstdint>

#include "graphics/font.h"
#include "graphics/overlay_camera.h"

namespace coney::gui {

void BaseWidget::setup(const WidgetRect& rect, graphics::Rgba colour, bool shadow) {
    m_place = rect;
    m_colour = colour;
    m_shadow = shadow;
}

void BaseWidget::render(const GuiCanvas& /*canvas*/) const {
    if (!visible() || m_batch == nullptr || m_rect >= m_batch->sheet().page.rects.size()) {
        return;
    }
    const graphics::UvRect& uv = m_batch->sheet().page.rect(m_rect);
    const float width = graphics::OverlayCamera::guiWidthToOverlay(m_place.width);
    // The sprite at a GUI point, with the widget's size and rectangle.
    const auto sprite = [&](float x, float y, graphics::Rgba colour) {
        return graphics::Sprite{graphics::OverlayCamera::guiToOverlay(x, y), width, m_place.height, uv, colour};
    };
    if (m_shadow) {
        const auto alpha = static_cast<std::uint8_t>(m_colour.a * kShadowAlpha / 255);
        m_batch->addSprite(sprite(m_place.x + graphics::kShadowOffsetX, m_place.y + graphics::kShadowOffsetY,
                                  graphics::Rgba{0, 0, 0, alpha}));
    }
    m_batch->addSprite(sprite(m_place.x, m_place.y, m_colour));
}

} // namespace coney::gui
