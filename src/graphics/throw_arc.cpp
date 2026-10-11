// SPDX-License-Identifier: GPL-3.0-or-later
#include "graphics/throw_arc.h"

#include <algorithm>
#include <cmath>

#include "core/interpolation.h"

namespace coney::graphics {

std::vector<ThrowArcVertex> throwArcStrip(std::span<const anim::Vec3> points, bool hit) {
    std::vector<ThrowArcVertex> strip;
    if (points.size() < 2) {
        return strip;
    }
    // Across the arc: level and square to its direction in plan (the arc stays in one vertical plane); x for an arc
    // straight up.
    const float dx = points.back().x - points.front().x;
    const float dy = points.back().y - points.front().y;
    const float length = std::hypot(dx, dy);
    const anim::Vec3 half =
        length > 0.0F ? anim::Vec3{dy / length * (kThrowArcWidth / 2.0F), -dx / length * (kThrowArcWidth / 2.0F), 0.0F}
                      : anim::Vec3{kThrowArcWidth / 2.0F, 0.0F, 0.0F};
    const std::array<std::uint8_t, 4> colour = hit ? kThrowArcHitColour : kThrowArcColour;
    strip.reserve(points.size() * 2);
    const auto last = static_cast<float>(points.size() - 1);
    for (std::size_t i = 0; i < points.size(); ++i) {
        // Faded in from the hand: alpha 0 at the first point, full from kThrowArcFadePoints on.
        std::array<std::uint8_t, 4> rgba = colour;
        const float fade = std::min(1.0F, static_cast<float>(i) / static_cast<float>(kThrowArcFadePoints));
        rgba[3] = static_cast<std::uint8_t>(std::lround(static_cast<float>(colour[3]) * fade));
        const float v = static_cast<float>(i) / last;
        // The left edge, then the right: half the width either side of the point.
        strip.push_back(ThrowArcVertex{.position = anim::subtract(points[i], half), .u = 0.0F, .v = v, .rgba = rgba});
        strip.push_back(ThrowArcVertex{.position = anim::add(points[i], half), .u = 1.0F, .v = v, .rgba = rgba});
    }
    return strip;
}

std::vector<anim::Vec3> blendThrowArc(std::span<const anim::Vec3> previous, std::span<const anim::Vec3> newest,
                                      float alpha) {
    std::vector<anim::Vec3> out(newest.begin(), newest.end());
    if (alpha >= 1.0F || previous.size() != newest.size()) {
        return out;
    }
    for (std::size_t i = 0; i < out.size(); ++i) {
        out[i] = anim::Vec3{lerp(previous[i].x, newest[i].x, alpha), lerp(previous[i].y, newest[i].y, alpha),
                            lerp(previous[i].z, newest[i].z, alpha)};
    }
    return out;
}

} // namespace coney::graphics
