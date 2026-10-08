// SPDX-License-Identifier: GPL-3.0-or-later
#include "hud/spinner.h"

namespace coney::hud {

graphics::Rgba Spinner::pulseColour(std::uint64_t ms) {
    // A triangle wave over the cycle: full at its start, clear at its middle, full again at its end.
    const std::uint64_t half = kPulseCycleMs / 2;
    const std::uint64_t t = ms % kPulseCycleMs;
    const float fade = t < half ? 1.0F - (static_cast<float>(t) / static_cast<float>(half))
                                : static_cast<float>(t - half) / static_cast<float>(half);
    return faded(kPulseColour, fade);
}

void Spinner::addSprite(const HudCanvas& canvas, graphics::Rgba colour) {
    graphics::SpriteBatch* parts = canvas.parts;
    if (parts == nullptr || kSpinnerRect >= parts->sheet().page.rects.size()) {
        return;
    }
    // Upright: the original zeroes its rotation before every draw.
    const graphics::UvRect uv = parts->sheet().page.rect(kSpinnerRect);
    parts->addSprite(guiSprite(kSpinnerPlace.x, kSpinnerPlace.y, squareTexelWidth(parts->sheet(), uv, kSpinnerSize),
                               kSpinnerSize, uv, colour));
}

void Spinner::drawPulse(const HudCanvas& canvas, std::uint64_t ms) {
    m_colour = pulseColour(ms);
    addSprite(canvas, m_colour);
}

void Spinner::render(const HudCanvas& canvas) const {
    if (m_shown) {
        addSprite(canvas, m_colour);
    }
}

} // namespace coney::hud
