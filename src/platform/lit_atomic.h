// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "graphics/frame_description.h"

// librw's types, declared rather than included: <rw.h> brings in SDL and the OpenGL loader.
namespace rw {
struct Atomic;
struct Light;
struct World;
} // namespace rw

namespace coney::platform {

/// The first passes' alpha test reference, 0x40 of the GS's 0x80, in librw's 0-255
/// (docs/research/rendering.md#shared-state).
inline constexpr std::uint32_t kFirstPassAlphaRef = 128;

/// Sets librw light `light` (of `values`' type) to `values`: its colour, radius, a spot light's cone, and its frame
/// (which it must have) placed at the light's position with its `at` axis along the light's direction. The one way
/// Coney turns a chosen light into a librw light, so the scene lighting's rig and the GL3 renderer's light the same.
void placeLight(rw::Light& light, const graphics::SceneLight& values);

/// librw's light type for `type`.
[[nodiscard]] std::int32_t rwLightType(graphics::LightType type);

/// Draws `atomic` with librw's lighting from `lights`, a world that holds only its chosen lights: librw lights an
/// atomic's point lights only when the atomic is in the current world, so it is put there for the draw and both are
/// put back after. A first pass (librw's alpha test GEQUAL as the render states stand) also tests the GS's alpha
/// reference, so a fainter pixel is still blended but writes no Z (docs/research/rendering.md#shared-state). Then
/// `layer`, the atomic's dual layer when not null (docs/research/rendering.md#dual): blended by its texture's alpha
/// with no alpha test, writing Z, at the alpha of the atomic's first material. `routed` draws through the draw layer
/// (draw::renderAtomic(), the world's atomics), else on librw directly (objects and humans until items 8 and 9 of
/// docs/engine/renderer.md#order route theirs).
void drawLitAtomic(rw::Atomic* atomic, rw::Atomic* layer, rw::World* lights, bool routed);

/// A librw world holding a list of chosen lights (graphics::SceneLight), for drawing an atomic lit by exactly those
/// with drawLitAtomic(). Its lights are made on first use and kept from draw to draw, each set again by placeLight().
/// Needs librw running; must be destroyed before it stops. Not copyable or movable: librw keeps pointers.
class LightRig {
  public:
    LightRig();
    ~LightRig();
    LightRig(const LightRig&) = delete;
    LightRig& operator=(const LightRig&) = delete;
    LightRig(LightRig&&) = delete;
    LightRig& operator=(LightRig&&) = delete;

    /// Puts `lights` into the world, in their order, in place of the last ones.
    void use(std::span<const graphics::SceneLight> lights);
    /// The world the lights are in.
    [[nodiscard]] rw::World* world() const { return m_world; }

  private:
    rw::World* m_world = nullptr;     // owned
    std::vector<rw::Light*> m_lights; // owned, with their frames; the first m_inWorld of them are in the world
    std::size_t m_inWorld = 0;
};

} // namespace coney::platform
