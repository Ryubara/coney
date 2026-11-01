// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <vector>

#include "graphics/light_manager.h"
#include "world/world_streams.h"

namespace coney::graphics {

/// One light as the PS2 pipeline's light block holds it for VU1: what the original queues for each selected light.
/// Colours are on the lighting scale, where 1.0 is the GS's 0x80.
struct Vu1Light {
    LightType type = LightType::Ambient;
    LightColour colour;     ///< The colour the light shines with now (brightness, offset and flicker applied).
    world::Vec3 position;   ///< Point and spot lights, RenderWare's axes.
    world::Vec3 direction;  ///< Directional and spot lights: the way the light travels (unit).
    float radius = 0.0F;    ///< Point and spot lights, metres, as given (the block holds no inverse).
    float coneAngle = 0.0F; ///< Spot lights: the cone's half angle, radians (the block holds −cos of it, inferred).
};

/// A vertex to light: where it is, which way it faces and its baked colour.
struct ReferenceVertex {
    world::Vec3 position;                         ///< RenderWare's axes, the same space as the lights.
    world::Vec3 normal;                           ///< Unit; a zero normal gets no directional or point light.
    LightColour prelight{0.0F, 0.0F, 0.0F, 1.0F}; ///< The prelighting, 1.0 = 0x80 (prelightFromPacked()).
};

/// A vertex colour as it reaches the GS: 0x80 (128) is 1.0, and nothing goes above it.
struct GsColour {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
    std::uint8_t a = 0;

    friend bool operator==(const GsColour&, const GsColour&) = default;
};

/// The light-block entry of `record`: its type, the colour it shines with now (`record.current`), its direction,
/// position and radius, as RenderWare's PS2 light upload (`0x00476cc8`, middleware Coney replaces) queues them.
/// @coney-only Coney's reading of RenderWare's light block (docs/research/lighting.md#vertex-reference)
[[nodiscard]] Vu1Light vu1Light(const LightRecord& record);

/// The light block of one atomic: `selection`'s records of `manager`, in the selection's order. In the game, lights
/// reach the renderer through SceneLighting::useLights (librw's lighting); this block feeds the CPU reference.
/// @orig 0x0017e810 LightManager_UploadSelected (LightManager.cpp), docs/research/lighting.md#vertex-reference
[[nodiscard]] std::vector<Vu1Light> vu1Lights(const LightManager& manager, const LightSelection& selection);

/// The light `lights` add at a vertex at `position` facing `normal`: each ambient's colour, each directional
/// light's times max(0, n · −L), each point light's times max(0, n · −L) × max(0, 1 − d / radius). Alpha is 0.
/// **Stand-ins** for what the VU1 code does where the research is silent: a point light of radius 0 and a vertex at a
/// light's centre add nothing; a spot light lights as a point light inside its cone and not outside it.
/// @coney-only the CPU reference of the PS2 pipeline's VU1 lighting, which has no EE function to cite; the formula is
/// docs/research/lighting.md#world
[[nodiscard]] LightColour lightAtVertex(world::Vec3 position, world::Vec3 normal, std::span<const Vu1Light> lights);

/// The colour the PS2 gives `vertex` lit by `lights` with `material` (0-1 a channel): min(1, prelight + light) ×
/// material per channel, alpha the material's, on the lighting scale (1.0 = 0x80). Exact in floats; toGs() truncates.
/// @coney-only the CPU reference of the PS2 pipeline's VU1 lighting (docs/research/lighting.md#vertex-reference)
[[nodiscard]] LightColour referenceColour(const ReferenceVertex& vertex, std::span<const Vu1Light> lights,
                                          LightColour material);

/// Every vertex of `vertices` lit by `lights` with `material`, as the GS receives them.
/// @coney-only the CPU reference of the PS2 pipeline's VU1 lighting (docs/research/lighting.md#vertex-reference)
[[nodiscard]] std::vector<GsColour> lightVertices(std::span<const ReferenceVertex> vertices,
                                                  std::span<const Vu1Light> lights, LightColour material);

/// A colour on the lighting scale as the GS receives it: × 128, truncated, kept within 0-128.
/// @coney-only the CPU reference's GS rounding (docs/research/lighting.md#vertex-reference)
[[nodiscard]] GsColour toGs(const LightColour& colour);

/// A packed prelighting colour as the disc stores it (0x80 = 1.0) on the lighting scale.
/// @coney-only the CPU reference's input (docs/research/lighting.md#vertex-reference)
[[nodiscard]] LightColour prelightFromPacked(const std::array<std::uint8_t, 4>& rgba);

/// A material or model colour (`HuColor`, 0-255 a channel) as the 0-1 factor the lit colour is multiplied by.
/// @coney-only the CPU reference's input (docs/research/lighting.md#vertex-reference)
[[nodiscard]] LightColour materialFromRgba(const std::array<std::uint8_t, 4>& rgba);

} // namespace coney::graphics
