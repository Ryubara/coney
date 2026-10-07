// SPDX-License-Identifier: GPL-3.0-or-later
// The radar's map disc and the placement of its blips (docs/research/hud.md#the-radar-on-screen,
// docs/research/gui.md#fn-radarhud).
#include "hud/radar.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace coney::hud {

namespace {

// The camera's right and forward in the game's plan (headings: 0 faces +y, positive to the left).
struct PlanAxes {
    float rightX, rightY, forwardX, forwardY;
};

PlanAxes axesOf(float heading) {
    const float c = std::cos(heading);
    const float s = std::sin(heading);
    return PlanAxes{c, s, -s, c};
}

// `colour` with alpha `alpha`.
graphics::Rgba withAlpha(graphics::Rgba colour, std::uint8_t alpha) {
    colour.a = alpha;
    return colour;
}

} // namespace

float radarZoomStep(const RadarState& radar, float speed, std::uint32_t ms) {
    const float fraction = std::min(std::max(speed, 0.0F), kRadarFullSpeed) / kRadarFullSpeed;
    const float target = (radar.rest + ((radar.fast - radar.rest) * fraction)) * radar.zoomScale;
    const float k = std::min(kRadarZoomRate * static_cast<float>(ms), 1.0F);
    return radar.zoom + ((target - radar.zoom) * k);
}

RadarOffset radarBlipOffset(const RadarView& view, float zoom, anim::Vec3 at) {
    // The offset in the camera's frame: across to the right, along the view up.
    const PlanAxes axes = axesOf(view.cameraHeading);
    const float dx = at.x - view.position.x;
    const float dy = at.y - view.position.y;
    const float across = (dx * axes.rightX) + (dy * axes.rightY);
    const float along = (dx * axes.forwardX) + (dy * axes.forwardY);
    const float distance = std::hypot(across, along);
    if (zoom <= 0.0F) {
        return {};
    }
    if (distance < zoom) {
        return RadarOffset{across * kRadarBlipReach / zoom, along * kRadarBlipReach / zoom};
    }
    // Beyond the zoom: on the edge along its direction.
    const float edge = kRadarEdge * kRadarBlipReach / distance;
    return RadarOffset{across * edge, along * edge};
}

std::array<float, 2> radarMapUv(const RadarMap& map, const RadarView& view, float zoom, float px, float py) {
    // The disc point's world position: `zoom` metres per radius, across and along the camera's view.
    const PlanAxes axes = axesOf(view.cameraHeading);
    const float x = view.position.x + (zoom * ((px * axes.rightX) + (py * axes.forwardX)));
    const float y = view.position.y + (zoom * ((px * axes.rightY) + (py * axes.forwardY)));
    if (map.scale <= 0.0F) {
        return {0.0F, 0.0F};
    }
    return {(x + map.offsetX) / map.scale, (map.offsetY - y) / map.scale};
}

std::array<float, 2> radarDotSize(const graphics::UvRect& uv, float texWidth, float texHeight, float size) {
    if (texWidth <= 0.0F) {
        return {0.0F, 0.0F};
    }
    return {2.0F * size * std::abs(uv.u1 - uv.u0),
            2.0F * size * std::abs(uv.v1 - uv.v0) * (texHeight / texWidth) * kRadarDotHeight};
}

void addRadarDisc(graphics::SpriteBatch& batch, graphics::OverlayPoint centre, float rx, float ry,
                  const RadarState& radar, graphics::Rgba colour) {
    // A corner of the disc at `share` of R in direction `angle` (0 to the right, counter-clockwise), its texture
    // coordinates from the map under it.
    const auto corner = [&](float share, float angle, std::uint8_t alpha) {
        const float px = share * std::cos(angle);
        const float py = share * std::sin(angle);
        const std::array<float, 2> uv = radarMapUv(radar.map, radar.view, radar.zoom, px, py);
        return graphics::OverlayVertex{graphics::OverlayPoint{centre.x + (px * rx), centre.y + (py * ry), centre.z},
                                       uv[0], uv[1], withAlpha(colour, alpha)};
    };
    const graphics::OverlayVertex middle = corner(0.0F, 0.0F, colour.a);
    const float step = 2.0F * std::numbers::pi_v<float> / static_cast<float>(kRadarSegments);
    // The segments start at the top (π/2) and go round.
    for (int i = 0; i < kRadarSegments; ++i) {
        const float a0 = (std::numbers::pi_v<float> / 2.0F) + (step * static_cast<float>(i));
        const float a1 = a0 + step;
        const graphics::OverlayVertex inner0 = corner(kRadarFilled, a0, colour.a);
        const graphics::OverlayVertex inner1 = corner(kRadarFilled, a1, colour.a);
        const graphics::OverlayVertex outer0 = corner(1.0F, a0, 0);
        const graphics::OverlayVertex outer1 = corner(1.0F, a1, 0);
        // The filled part, then the soft edge's two triangles.
        batch.addTriangle(middle, inner0, inner1);
        batch.addTriangle(inner0, outer0, outer1);
        batch.addTriangle(inner0, outer1, inner1);
    }
}

bool radarBlipShown(const RadarBlip& blip, std::uint64_t updates) {
    if (blip.type == 5 || blip.type == 6 || blip.type == 8) {
        return false;
    }
    if (blip.flashCountdown > 0 || blip.flashing) {
        return (updates / kRadarFlashPeriod) % 2 == 0;
    }
    return true;
}

} // namespace coney::hud
