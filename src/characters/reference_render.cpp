// SPDX-License-Identifier: GPL-3.0-or-later
#include "characters/reference_render.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <format>
#include <limits>

#include "characters/character_rig.h"
#include "core/assert.h"

namespace coney::characters {

void bindPoseVertices(const CharacterModel& model, std::span<anim::Vec3> positions, std::span<anim::Vec3> normals) {
    CONEY_ASSERT(positions.size() == model.vertices.size() && normals.size() == model.vertices.size());
    for (std::size_t i = 0; i < model.vertices.size(); ++i) {
        positions[i] = anim::transformPoint(kClumpToPose, model.vertices[i].position);
        normals[i] = anim::transformDirection(kClumpToPose, model.vertices[i].normal);
    }
}

ReferenceView frameReference(std::span<const anim::Vec3> points) {
    // The fixed axes: the camera looks back towards the character, from in front (+y) turned towards +x, from above.
    const float cosPitch = std::cos(kReferencePitch);
    const anim::Vec3 toCamera{std::sin(kReferenceYaw) * cosPitch, std::cos(kReferenceYaw) * cosPitch,
                              std::sin(kReferencePitch)};
    ReferenceView view;
    view.forward = anim::scale(toCamera, -1.0F);
    view.right = anim::normalise(anim::cross(view.forward, anim::Vec3{0.0F, 0.0F, 1.0F}));
    view.up = anim::cross(view.right, view.forward);
    if (points.empty()) {
        view.target = {};
        view.position = anim::scale(toCamera, 3.0F);
        return view;
    }

    // Centre on the points' extent across the view (orthographic, along the fixed axes), which also sets their
    // depth range.
    float minRight = std::numeric_limits<float>::max();
    float maxRight = std::numeric_limits<float>::lowest();
    float minUp = minRight;
    float maxUp = maxRight;
    float minDepth = minRight;
    float maxDepth = maxRight;
    for (const anim::Vec3& p : points) {
        const float r = anim::dot(p, view.right);
        const float u = anim::dot(p, view.up);
        const float d = anim::dot(p, view.forward);
        minRight = std::min(minRight, r);
        maxRight = std::max(maxRight, r);
        minUp = std::min(minUp, u);
        maxUp = std::max(maxUp, u);
        minDepth = std::min(minDepth, d);
        maxDepth = std::max(maxDepth, d);
    }
    view.target = anim::add(
        anim::add(anim::scale(view.right, 0.5F * (minRight + maxRight)), anim::scale(view.up, 0.5F * (minUp + maxUp))),
        anim::scale(view.forward, 0.5F * (minDepth + maxDepth)));

    // The nearest distance that keeps every point inside the view: a point at offset (r, u, d) from the target is at
    // depth distance + d, and must have |r| and |u| at most `limit` times that depth.
    const float limit = kReferenceHalfView * (1.0F - 2.0F * kReferenceMargin);
    float distance = 0.0F;
    for (const anim::Vec3& p : points) {
        const anim::Vec3 offset = anim::subtract(p, view.target);
        const float extent = std::max(std::abs(anim::dot(offset, view.right)), std::abs(anim::dot(offset, view.up)));
        distance = std::max(distance, extent / limit - anim::dot(offset, view.forward));
    }
    view.position = anim::subtract(view.target, anim::scale(view.forward, distance));
    return view;
}

std::vector<std::uint8_t> downsampleRgba(std::span<const std::uint8_t> rgba, int size, int factor) {
    CONEY_ASSERT(size > 0 && factor > 0 && size % factor == 0);
    CONEY_ASSERT(rgba.size() == static_cast<std::size_t>(size) * static_cast<std::size_t>(size) * 4);
    const int out = size / factor;
    const auto samples = static_cast<std::uint32_t>(factor * factor);
    std::vector<std::uint8_t> result(static_cast<std::size_t>(out) * static_cast<std::size_t>(out) * 4);
    for (int y = 0; y < out; ++y) {
        for (int x = 0; x < out; ++x) {
            // Integer sums keep the result exact and the same on every machine.
            std::uint32_t alpha = 0;
            std::uint32_t red = 0;
            std::uint32_t green = 0;
            std::uint32_t blue = 0;
            for (int sy = 0; sy < factor; ++sy) {
                for (int sx = 0; sx < factor; ++sx) {
                    const std::size_t at =
                        ((static_cast<std::size_t>(y * factor + sy) * static_cast<std::size_t>(size)) +
                         static_cast<std::size_t>(x * factor + sx)) *
                        4;
                    const std::uint32_t a = rgba[at + 3];
                    red += rgba[at] * a;
                    green += rgba[at + 1] * a;
                    blue += rgba[at + 2] * a;
                    alpha += a;
                }
            }
            const std::size_t to =
                ((static_cast<std::size_t>(y) * static_cast<std::size_t>(out)) + static_cast<std::size_t>(x)) * 4;
            if (alpha != 0) {
                // Undo the alpha weighting with rounding to nearest.
                result[to] = static_cast<std::uint8_t>((red + alpha / 2) / alpha);
                result[to + 1] = static_cast<std::uint8_t>((green + alpha / 2) / alpha);
                result[to + 2] = static_cast<std::uint8_t>((blue + alpha / 2) / alpha);
                result[to + 3] = static_cast<std::uint8_t>((alpha + samples / 2) / samples);
            }
        }
    }
    return result;
}

std::string referenceFileName(std::uint32_t nameHash, std::string_view name) {
    if (!name.empty()) {
        return std::format("{}.png", name);
    }
    return std::format("{:08x}.png", nameHash);
}

} // namespace coney::characters
