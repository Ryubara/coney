// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <vector>

#include "graphics/frame_description.h"

// librw's types, declared rather than included: <rw.h> brings in SDL and the OpenGL loader.
namespace rw {
struct Atomic;
struct Geometry;
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
/// (draw::renderAtomic(): the world's atomics, the objects' and the cars'), else on librw directly (the humans until
/// item 9 of docs/engine/renderer.md#order routes theirs).
void drawLitAtomic(rw::Atomic* atomic, rw::Atomic* layer, rw::World* lights, bool routed);

/// Sets a 3D draw's fixed states (graphics::SceneStates) on the draw layer, each only where it differs from the one in
/// force, so a draw whose states are those already set adds nothing to the draw stream.
void applySceneStates(const graphics::SceneStates& states);

/// The fixed states in force on the draw layer, as a 3D draw names them: what a pass that sets its states as it goes
/// (the world objects, the parked cars) draws in. graphics::SceneStates names fewer states than librw has, so some
/// read back as another, and applySceneStates() would then set that one for the draw: a source blend other than
/// SRCALPHA reads as no blend whatever the destination blend (applied as ONE, ZERO), a destination blend is not read
/// at all (applied as INVSRCALPHA with a source-alpha blend), an alpha-test function other than GREATEREQUAL or LESS
/// reads as Always, and a cull mode other than NONE or FRONT as Back. The objects' pass sets SRCALPHA over INVSRCALPHA
/// and the cars' passes keep the blend in force; the disc frames' draw-stream hashes show no state rewritten today
/// (docs/engine/renderer.md#comparing), and the GPU renderer, which reads only SceneStates, relies on that.
[[nodiscard]] graphics::SceneStates sceneStatesInForce();

/// Changes an atomic's materials as a 3D draw's graphics::Material3d says while it lives: a fade or a paint is kept, a
/// tint (the first material's colour and the modulation it turns on) is put back when it goes. The one place the GL3
/// renderer and the draws outside the frame description (the debug tools' mesh views) change them, so both draw the
/// same colours.
class MaterialChange {
  public:
    /// Changes `atomic`'s materials, which must outlive this.
    MaterialChange(rw::Atomic& atomic, const graphics::Material3d& material);
    ~MaterialChange();
    MaterialChange(const MaterialChange&) = delete;
    MaterialChange& operator=(const MaterialChange&) = delete;
    MaterialChange(MaterialChange&&) = delete;
    MaterialChange& operator=(MaterialChange&&) = delete;

  private:
    rw::Geometry* m_geometry;
    std::vector<std::array<std::uint8_t, 4>> m_own; // a tint's first material's own colour (RGBA), if any
    std::uint32_t m_flags = 0;                      // a tint's geometry flags before it
    bool m_restore = false;                         // a tint: put the colours and flags back
};

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
