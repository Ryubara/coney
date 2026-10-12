// SPDX-License-Identifier: GPL-3.0-or-later
#include "graphics/throw_arc.h"

#include <algorithm>

#include "core/interpolation.h"

namespace coney::graphics {

std::vector<ThrowArcVertex> throwArcStrip(std::span<const anim::Vec3> points, anim::Vec3 right, bool cut, bool hit) {
    std::vector<ThrowArcVertex> strip;
    if (points.size() < 2) {
        return strip;
    }
    const std::size_t last = points.size() - 1;
    // The points before the contact: all of them for an arc that met nothing, else the contact (the last point) is
    // not one of them.
    const std::size_t count = cut ? last : points.size();
    const anim::Vec3 half = anim::scale(right, kThrowArcHalfWidth);
    const std::array<std::uint8_t, 3>& colour = hit ? kThrowArcHitColour : kThrowArcColour;
    strip.reserve(kThrowArcPairs * 2);
    for (std::size_t pair = 0; pair < kThrowArcPairs; ++pair) {
        // Pair 0 on the hand, the next ones on their points up to the cut, the rest on the last point before the
        // contact; the last pair is where the arc ended.
        std::size_t index = 0;
        if (pair == kThrowArcPairs - 1) {
            index = last;
        } else if (pair > 0) {
            index = pair < count ? pair : count - 1;
        }
        // Faded in from the hand: alpha 0 at pair 0, then 127 plus up to 16 by how far along the arc the pair is.
        std::uint8_t alpha = 0;
        if (pair > 0) {
            const std::size_t along = std::min(pair, count);
            alpha = static_cast<std::uint8_t>(kThrowArcAlphaBase +
                                              ((static_cast<std::size_t>(kThrowArcAlphaRise) * along) / count));
        }
        const std::array<std::uint8_t, 4> rgba{colour[0], colour[1], colour[2], alpha};
        const float v = kThrowArcFirstV - static_cast<float>(pair);
        // The left vertex, then the right: half the width either side of the point.
        strip.push_back(
            ThrowArcVertex{.position = anim::subtract(points[index], half), .u = 0.0F, .v = v, .rgba = rgba});
        strip.push_back(ThrowArcVertex{.position = anim::add(points[index], half), .u = 1.0F, .v = v, .rgba = rgba});
    }
    return strip;
}

std::vector<std::uint16_t> throwArcIndices() {
    std::vector<std::uint16_t> indices;
    indices.reserve(kThrowArcTriangles * 3);
    for (std::size_t pair = 0; pair + 1 < kThrowArcPairs; ++pair) {
        const auto a = static_cast<std::uint16_t>(pair * 2);
        indices.insert(indices.end(), {a, static_cast<std::uint16_t>(a + 1), static_cast<std::uint16_t>(a + 2),
                                       static_cast<std::uint16_t>(a + 1), static_cast<std::uint16_t>(a + 3),
                                       static_cast<std::uint16_t>(a + 2)});
    }
    return indices;
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
