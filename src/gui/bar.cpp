// SPDX-License-Identifier: GPL-3.0-or-later
#include "gui/bar.h"

#include <algorithm>

#include "graphics/overlay_camera.h"
#include "graphics/screen.h"

namespace coney::gui {

// The fill, clamped to 0-1 as the generic bar's setter clamps it.
void Bar::setFill(float fraction) { m_fill = std::clamp(fraction, 0.0F, 1.0F); }

// The meter's sprites, in the original's order (docs/research/hud.md#the-rage-meter).
void Bar::render(const GuiCanvas& /*canvas*/) const {
    // A hidden bar draws nothing (the original's drawn flag `+0x50`, and the alpha its visible and active flags clear).
    const std::size_t rects = m_batch != nullptr ? m_batch->sheet().page.rects.size() : 0;
    if (!visible() || m_batch == nullptr || m_rect >= rects || m_fillRect >= rects ||
        (m_caps && (m_caps->at(0) >= rects || m_caps->at(1) >= rects))) {
        return;
    }
    const graphics::SpriteSheet& sheet = m_batch->sheet();
    // Overlay widths become GUI widths for placing: a GUI width is stretched by 640 / 448 into the overlay's space.
    const auto toGui = [](float overlayWidth) {
        return overlayWidth * graphics::kLogicalHeight / graphics::kLogicalWidth;
    };
    // A part `width` overlay units wide whose left edge is `left` overlay units right of the bar's, centred on its
    // middle.
    const auto part = [&](float left, float width, const graphics::UvRect& uv, graphics::Rgba colour) {
        return graphics::Sprite{
            graphics::OverlayCamera::guiToOverlay(m_setup.x + toGui(left) + (toGui(width) / 2.0F), m_setup.y), width,
            m_setup.height, uv, colour};
    };
    if (!m_caps) {
        // Coney's choice for the bars set up without caps (PM_Light's, the Rumble gang screen's): back and fill only,
        // a stand-in for the meter's form below until those screens' bars are compared.
        m_batch->addSprite(part(0.0F, m_setup.width, sheet.page.rect(m_rect), m_setup.backColour));
        if (m_fill > 0.0F) {
            m_batch->addSprite(part(0.0F, m_setup.width * m_fill, sheet.page.rect(m_fillRect), m_setup.fillColour));
        }
        return;
    }
    // The stretched body and fill are moved in by one texel left and right (not up and down) so that filtering stays
    // inside their rectangles; without a texture there is no texel to measure.
    const auto inset = [&sheet](graphics::UvRect uv) {
        if (sheet.texture && sheet.texture->width() != 0) {
            const float texel = 1.0F / static_cast<float>(sheet.texture->width());
            uv.u0 += texel;
            uv.u1 -= texel;
        }
        return uv;
    };
    // Stand-in: the second fill (`+0x3c`), the fill colour's blend (`+0x60`), the right-to-left form (`+0x5c`) and the
    // 0x02 video modes' offsets are not modelled; no bar Coney draws with caps uses them. The flash and the trail are
    // not drawn because every caller of the original passes 0 for both.
    //
    // A cap is as wide as its rectangle's u span, taken as overlay units; the inner width is the bar's less the left
    // cap. Left to right: the left cap from the left edge, the body centred on the bar's middle (so from half a cap
    // in), the fill from half a cap in over its share of the inner width, the right cap from half a cap short of the
    // width.
    const graphics::UvRect& leftUv = sheet.page.rect(m_caps->at(0));
    const graphics::UvRect& rightUv = sheet.page.rect(m_caps->at(1));
    const float leftCap = leftUv.u1 - leftUv.u0;
    const float rightCap = rightUv.u1 - rightUv.u0;
    const float inner = m_setup.width - leftCap;
    const float halfCap = leftCap / 2.0F;
    m_batch->addSprite(part(0.0F, leftCap, leftUv, m_setup.backColour));
    m_batch->addSprite(part(halfCap, inner, inset(sheet.page.rect(m_rect)), m_setup.backColour));
    m_batch->addSprite(part(halfCap, inner * m_fill, inset(sheet.page.rect(m_fillRect)), m_setup.fillColour));
    m_batch->addSprite(part(m_setup.width - halfCap, rightCap, rightUv, m_setup.backColour));
}

} // namespace coney::gui
