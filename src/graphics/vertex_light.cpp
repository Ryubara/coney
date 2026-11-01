// SPDX-License-Identifier: GPL-3.0-or-later
#include "graphics/vertex_light.h"

#include <algorithm>
#include <cmath>

namespace coney::graphics {

namespace {

// The dot product of `a` and `b`.
// @coney-only a vector helper of the CPU reference
float dot(world::Vec3 a, world::Vec3 b) { return (a.x * b.x) + (a.y * b.y) + (a.z * b.z); }

// `a` − `b`.
// @coney-only a vector helper of the CPU reference
world::Vec3 minus(world::Vec3 a, world::Vec3 b) { return world::Vec3{a.x - b.x, a.y - b.y, a.z - b.z}; }

// Adds `colour` × `share` to `sum`'s colour channels.
// @coney-only a colour helper of the CPU reference
void addScaled(LightColour& sum, const LightColour& colour, float share) {
    sum.r += colour.r * share;
    sum.g += colour.g * share;
    sum.b += colour.b * share;
}

// The share of a point or spot light `light` at a vertex at `position` facing `normal`: how far the vertex faces the
// lamp, max(0, n · −L), times the linear falloff max(0, 1 − d / radius); for a spot light, 0 outside its cone.
// @coney-only the CPU reference of VU1's point-light term (docs/research/lighting.md#world)
float pointShare(const Vu1Light& light, world::Vec3 position, world::Vec3 normal) {
    // Stand-in (lighting.md#q-zero-radius): the falloff divides by the radius, and what VU1 makes of 0 is not known.
    if (light.radius <= 0.0F) {
        return 0.0F;
    }
    const world::Vec3 towards = minus(light.position, position);
    const float distance = std::sqrt(dot(towards, towards));
    // Stand-in: a vertex at the light's centre has no direction to it.
    if (distance <= 0.0F || distance >= light.radius) {
        return 0.0F;
    }
    // Stand-in (lighting.md#q-spot-light): a hard-edged cone round the light's direction.
    if (light.type == LightType::Spot && -dot(towards, light.direction) / distance < std::cos(light.coneAngle)) {
        return 0.0F;
    }
    const float facing = std::max(0.0F, dot(normal, towards) / distance);
    return facing * (1.0F - (distance / light.radius));
}

// One channel on the lighting scale as the GS takes it: × 128, truncated, within 0-128.
// @coney-only the CPU reference's GS truncation (docs/research/lighting.md#vertex-reference)
std::uint8_t gsChannel(float value) {
    const float scaled = std::clamp(value * 128.0F, 0.0F, 128.0F);
    return static_cast<std::uint8_t>(scaled);
}

} // namespace

Vu1Light vu1Light(const LightRecord& record) {
    // Every light carries its colour and type; a directional light adds the way it travels, a point light its place
    // and radius, a spot light both and its cone.
    Vu1Light light{.type = record.desc.type, .colour = record.current};
    switch (record.desc.type) {
    case LightType::Ambient:
        break;
    case LightType::Directional:
        light.direction = record.desc.direction;
        break;
    case LightType::Spot:
        light.direction = record.desc.direction;
        light.coneAngle = record.desc.coneAngle;
        [[fallthrough]];
    case LightType::Point:
        light.position = record.desc.position;
        light.radius = record.desc.radius;
        break;
    }
    return light;
}

std::vector<Vu1Light> vu1Lights(const LightManager& manager, const LightSelection& selection) {
    std::vector<Vu1Light> block;
    block.reserve(selection.count);
    for (const std::uint16_t index : selection.lights()) {
        block.push_back(vu1Light(manager.record(index)));
    }
    // Stand-in: the original closes the block with its DMA tag and sends it to VU1; the reference lights on the CPU.
    return block;
}

LightColour lightAtVertex(world::Vec3 position, world::Vec3 normal, std::span<const Vu1Light> lights) {
    LightColour sum{0.0F, 0.0F, 0.0F, 0.0F};
    for (const Vu1Light& light : lights) {
        switch (light.type) {
        case LightType::Ambient:
            addScaled(sum, light.colour, 1.0F);
            break;
        case LightType::Directional:
            // The light travels along its direction, so a surface facing against it is lit.
            addScaled(sum, light.colour, std::max(0.0F, -dot(normal, light.direction)));
            break;
        case LightType::Point:
        case LightType::Spot:
            addScaled(sum, light.colour, pointShare(light, position, normal));
            break;
        }
    }
    return sum;
}

LightColour referenceColour(const ReferenceVertex& vertex, std::span<const Vu1Light> lights, LightColour material) {
    const LightColour light = lightAtVertex(vertex.position, vertex.normal, lights);
    // The prelighting and the lights add, the sum clamps at 1.0 (no overbright), and only then the material
    // multiplies: a dark material stays dark under any light.
    auto channel = [](float prelight, float lit, float factor) { return std::min(1.0F, prelight + lit) * factor; };
    return LightColour{channel(vertex.prelight.r, light.r, material.r), channel(vertex.prelight.g, light.g, material.g),
                       channel(vertex.prelight.b, light.b, material.b), material.a};
}

std::vector<GsColour> lightVertices(std::span<const ReferenceVertex> vertices, std::span<const Vu1Light> lights,
                                    LightColour material) {
    std::vector<GsColour> colours;
    colours.reserve(vertices.size());
    for (const ReferenceVertex& vertex : vertices) {
        colours.push_back(toGs(referenceColour(vertex, lights, material)));
    }
    return colours;
}

GsColour toGs(const LightColour& colour) {
    return GsColour{gsChannel(colour.r), gsChannel(colour.g), gsChannel(colour.b), gsChannel(colour.a)};
}

LightColour prelightFromPacked(const std::array<std::uint8_t, 4>& rgba) {
    // The colour bytes are on the GS's scale, 0x80 = 1.0 (lighting.md#vertex-reference, the sky box's 30 + 29 = 59);
    // alpha is not lit or clamped, only passed on as an opacity, so it keeps the byte's own 0-255 range.
    constexpr float kOne = 128.0F;
    return LightColour{static_cast<float>(rgba[0]) / kOne, static_cast<float>(rgba[1]) / kOne,
                       static_cast<float>(rgba[2]) / kOne, static_cast<float>(rgba[3]) / 255.0F};
}

LightColour materialFromRgba(const std::array<std::uint8_t, 4>& rgba) {
    constexpr float kFull = 255.0F;
    return LightColour{static_cast<float>(rgba[0]) / kFull, static_cast<float>(rgba[1]) / kFull,
                       static_cast<float>(rgba[2]) / kFull, static_cast<float>(rgba[3]) / kFull};
}

} // namespace coney::graphics
