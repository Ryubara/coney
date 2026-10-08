// SPDX-License-Identifier: GPL-3.0-or-later
#include "hud/crime_panel.h"

#include <cmath>
#include <numbers>

namespace coney::hud {

namespace {

// The overlay-space size of one logical pixel at depth 1, where the radar disc's centre is.
graphics::LogicalPoint pixelInOverlay() {
    static const graphics::LogicalPoint unit = graphics::OverlayCamera{}.unprojectSize({1.0F, 1.0F}, 1.0F);
    return unit;
}

// The point `radius` pixels from `centre` at angle `a` (0 straight below, screen y down), stretched by the video mode.
graphics::OverlayPoint arcPoint(graphics::OverlayPoint centre, float radius, float a) {
    const graphics::LogicalPoint unit = pixelInOverlay();
    return graphics::OverlayPoint{centre.x + (radius * kCrimeStretchX * std::sin(a) * unit.x),
                                  centre.y - (radius * kCrimeStretchY * std::cos(a) * unit.y), centre.z};
}

} // namespace

float CrimePanel::fillOf(float fraction) {
    if (fraction <= 0.0F) {
        return 0.0F;
    }
    return fraction > kCrimeFullAbove ? 1.0F : fraction;
}

void CrimePanel::update(float wanted, float second) {
    m_target = {fillOf(wanted), fillOf(second)};
    for (std::size_t i = 0; i < m_shown.size(); ++i) {
        const float gap = m_target.at(i) - m_shown.at(i);
        m_shown.at(i) = std::fabs(gap) > kCrimeSnap ? m_target.at(i) : m_shown.at(i) + (gap * kCrimeEase);
    }
}

void CrimePanel::renderArc(graphics::SpriteBatch& batch, graphics::OverlayPoint centre, float inner, float outer,
                           float endDegrees, float fill, graphics::Rgba colour) {
    // A strip of 17 vertex pairs from angle 0 over fill × the sweep: the inner vertex then the outer of each.
    const float step = endDegrees * std::numbers::pi_v<float> / 180.0F * fill / static_cast<float>(kCrimeSegments);
    const auto vertex = [&](int j, float radius) {
        return graphics::OverlayVertex{arcPoint(centre, radius, step * static_cast<float>(j)), 0.0F, 0.0F, colour};
    };
    for (int j = 0; j < kCrimeSegments; ++j) {
        const graphics::OverlayVertex a = vertex(j, inner);
        const graphics::OverlayVertex b = vertex(j, outer);
        const graphics::OverlayVertex c = vertex(j + 1, inner);
        const graphics::OverlayVertex d = vertex(j + 1, outer);
        batch.addTriangle(a, b, c);
        batch.addTriangle(b, d, c);
    }
}

void CrimePanel::render(graphics::SpriteBatch& batch, graphics::OverlayPoint centre) const {
    // The wanted pair, blue, up both sides; the second pair orange outside it, or at its radii when it did not draw.
    const bool wantedDrawn = m_target[0] > kCrimeArcMinimum || m_shown[0] > kCrimeArcMinimum;
    if (wantedDrawn) {
        renderArc(batch, centre, kCrimeInner, kCrimeMiddle, 180.0F, m_shown[0], kWantedColour);
        renderArc(batch, centre, kCrimeInner, kCrimeMiddle, -180.0F, m_shown[0], kWantedColour);
    }
    if (m_target[1] > kCrimeArcMinimum || m_shown[1] > kCrimeArcMinimum) {
        const float inner = wantedDrawn ? kCrimeMiddle : kCrimeInner;
        const float outer = wantedDrawn ? kCrimeOuter : kCrimeMiddle;
        renderArc(batch, centre, inner, outer, 180.0F, m_shown[1], kSecondTimerColour);
        renderArc(batch, centre, inner, outer, -180.0F, m_shown[1], kSecondTimerColour);
    }
}

} // namespace coney::hud
