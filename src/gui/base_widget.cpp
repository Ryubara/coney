// SPDX-License-Identifier: GPL-3.0-or-later
#include "gui/base_widget.h"

#include <cstdint>
#include <utility>

#include "graphics/font.h"
#include "graphics/overlay_camera.h"
#include "graphics/screen.h"

namespace coney::gui {

std::pair<float, float> BaseWidget::overlaySize() const {
    const float height = m_setup.height;
    float width = height;
    if (m_setup.width) {
        width = *m_setup.width;
    } else if (m_batch != nullptr && m_rect < m_batch->sheet().page.rects.size() &&
               m_batch->sheet().texture != nullptr) {
        // Keep the height; the width follows the rectangle's shape in texels.
        const graphics::UvRect& uv = m_batch->sheet().page.rect(m_rect);
        const float texelWidth = (uv.u1 - uv.u0) * static_cast<float>(m_batch->sheet().texture->width());
        const float texelHeight = (uv.v1 - uv.v0) * static_cast<float>(m_batch->sheet().texture->height());
        if (texelHeight > 0.0F) {
            width = height * texelWidth / texelHeight;
        }
    }
    if (m_setup.aspectFix) {
        width *= graphics::OverlayCamera::kViewAspect * kAspectFix;
    }
    return {width, height};
}

std::pair<float, float> BaseWidget::centre() const {
    // An overlay width is W / H GUI widths.
    const float guiWidth = overlaySize().first * graphics::kLogicalHeight / graphics::kLogicalWidth;
    switch (m_setup.anchor) {
    case SpriteAnchor::Left:
        return {m_setup.x + guiWidth / 2.0F, m_setup.y};
    case SpriteAnchor::Right:
        return {m_setup.x - guiWidth / 2.0F, m_setup.y};
    case SpriteAnchor::Centre:
        break;
    }
    return {m_setup.x, m_setup.y};
}

void BaseWidget::render(const GuiCanvas& /*canvas*/) const {
    if (!visible() || m_batch == nullptr || m_rect >= m_batch->sheet().page.rects.size()) {
        return;
    }
    const graphics::UvRect& uv = m_batch->sheet().page.rect(m_rect);
    const std::pair<float, float> size = overlaySize();
    // The sprite centred at a GUI point, with the widget's size and rectangle.
    const auto sprite = [&uv, size](float x, float y, graphics::Rgba colour) {
        return graphics::Sprite{graphics::OverlayCamera::guiToOverlay(x, y), size.first, size.second, uv, colour};
    };
    if (m_setup.shadow) {
        // The shadow record sits at the position itself, not shifted by the anchor.
        const auto alpha = static_cast<std::uint8_t>(static_cast<float>(m_setup.colour.a) * kShadowFactor);
        m_batch->addSprite(sprite(m_setup.x + graphics::kShadowOffsetX, m_setup.y + graphics::kShadowOffsetY,
                                  graphics::Rgba{0, 0, 0, alpha}));
    }
    const auto [x, y] = centre();
    m_batch->addSprite(sprite(x, y, m_setup.colour));
}

} // namespace coney::gui
