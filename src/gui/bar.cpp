// SPDX-License-Identifier: GPL-3.0-or-later
#include "gui/bar.h"

#include <algorithm>

#include "graphics/overlay_camera.h"
#include "graphics/screen.h"

namespace coney::gui {

void Bar::setFill(float fraction) { m_fill = std::clamp(fraction, 0.0F, 1.0F); }

void Bar::render(const GuiCanvas& /*canvas*/) const {
    if (!visible() || m_batch == nullptr || m_rect >= m_batch->sheet().page.rects.size()) {
        return;
    }
    const graphics::UvRect& uv = m_batch->sheet().page.rect(m_rect);
    // A part `width` overlay units wide from the left edge, centred on its own middle.
    const auto part = [&](float width, graphics::Rgba colour) {
        const float guiWidth = width * graphics::kLogicalHeight / graphics::kLogicalWidth;
        return graphics::Sprite{graphics::OverlayCamera::guiToOverlay(m_setup.x + guiWidth / 2.0F, m_setup.y), width,
                                m_setup.height, uv, colour};
    };
    m_batch->addSprite(part(m_setup.width, m_setup.backColour));
    if (m_fill > 0.0F) {
        m_batch->addSprite(part(m_setup.width * m_fill, m_setup.fillColour));
    }
}

} // namespace coney::gui
