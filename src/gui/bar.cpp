// SPDX-License-Identifier: GPL-3.0-or-later
#include "gui/bar.h"

#include <algorithm>

#include "graphics/overlay_camera.h"
#include "graphics/screen.h"

namespace coney::gui {

namespace {

// An overlay width as a GUI width (a GUI width is stretched by 640 / 448 into the overlay camera's space).
float toGuiWidth(float overlayWidth) { return overlayWidth * graphics::kLogicalHeight / graphics::kLogicalWidth; }

// The overlay width of `rect` of `sheet` drawn `height` tall with square texels; 0 without a texture.
float capWidth(const graphics::SpriteSheet& sheet, std::size_t rect, float height) {
    if (!sheet.texture || sheet.texture->width() == 0 || sheet.texture->height() == 0) {
        return 0.0F;
    }
    const graphics::UvRect& uv = sheet.page.rect(rect);
    const float texels = (uv.u1 - uv.u0) * static_cast<float>(sheet.texture->width());
    const float rows = (uv.v1 - uv.v0) * static_cast<float>(sheet.texture->height());
    return rows > 0.0F ? height * texels / rows : 0.0F;
}

// `uv` moved in by one texel on each side, so that a stretched rectangle's filtering stays inside it.
graphics::UvRect insetOneTexel(graphics::UvRect uv, const graphics::SpriteSheet& sheet) {
    if (!sheet.texture || sheet.texture->width() == 0 || sheet.texture->height() == 0) {
        return uv;
    }
    const float du = 1.0F / static_cast<float>(sheet.texture->width());
    const float dv = 1.0F / static_cast<float>(sheet.texture->height());
    if (uv.u1 - uv.u0 > 2.0F * du) {
        uv.u0 += du;
        uv.u1 -= du;
    }
    if (uv.v1 - uv.v0 > 2.0F * dv) {
        uv.v0 += dv;
        uv.v1 -= dv;
    }
    return uv;
}

} // namespace

void Bar::setFill(float fraction) { m_fill = std::clamp(fraction, 0.0F, 1.0F); }

void Bar::render(const GuiCanvas& /*canvas*/) const {
    const std::size_t rects = m_batch != nullptr ? m_batch->sheet().page.rects.size() : 0;
    if (!visible() || m_batch == nullptr || m_rect >= rects || m_fillRect >= rects ||
        (m_caps && (m_caps->at(0) >= rects || m_caps->at(1) >= rects))) {
        return;
    }
    const graphics::SpriteSheet& sheet = m_batch->sheet();
    // A part `width` overlay units wide whose left edge is `left` GUI units right of the bar's, centred on its middle.
    const auto part = [&](float left, float width, const graphics::UvRect& uv, graphics::Rgba colour) {
        return graphics::Sprite{
            graphics::OverlayCamera::guiToOverlay(m_setup.x + left + toGuiWidth(width) / 2.0F, m_setup.y), width,
            m_setup.height, uv, colour};
    };
    if (!m_caps) {
        m_batch->addSprite(part(0.0F, m_setup.width, sheet.page.rect(m_rect), m_setup.backColour));
        if (m_fill > 0.0F) {
            m_batch->addSprite(part(0.0F, m_setup.width * m_fill, sheet.page.rect(m_fillRect), m_setup.fillColour));
        }
        return;
    }
    // The meter's form: left cap, body over the inner width, fill over its share, right cap past the width.
    const float leftCap = capWidth(sheet, m_caps->at(0), m_setup.height);
    const float rightCap = capWidth(sheet, m_caps->at(1), m_setup.height);
    const float inner = std::max(m_setup.width - leftCap, 0.0F);
    const float bodyLeft = toGuiWidth(leftCap);
    m_batch->addSprite(part(0.0F, leftCap, sheet.page.rect(m_caps->at(0)), m_setup.backColour));
    m_batch->addSprite(part(bodyLeft, inner, insetOneTexel(sheet.page.rect(m_rect), sheet), m_setup.backColour));
    if (m_fill > 0.0F) {
        m_batch->addSprite(
            part(bodyLeft, inner * m_fill, insetOneTexel(sheet.page.rect(m_fillRect), sheet), m_setup.fillColour));
    }
    m_batch->addSprite(
        part(bodyLeft + toGuiWidth(inner), rightCap, sheet.page.rect(m_caps->at(1)), m_setup.backColour));
}

} // namespace coney::gui
