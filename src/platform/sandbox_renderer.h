// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <expected>
#include <functional>
#include <memory>
#include <vector>

#include "core/error.h"
#include "graphics/render_device.h"
#include "platform/render_engine.h"
#include "platform/world_renderer.h"
#include "sandbox/sandbox_world.h"

// librw's types, declared rather than included: <rw.h> brings in SDL and the OpenGL loader.
namespace rw {
struct Atomic;
struct Texture;
} // namespace rw

namespace coney::platform {

/// Draws a sandbox (src/sandbox/sandbox_world.h) with librw, the way the world renderer draws a level's scenery: in
/// RenderWare's axes (a game point (x, y, z) is drawn at (x, z, -y), as toRenderWare() turns it), through a WorldView,
/// with Z test and write, back faces culled and linear fog in the sky colour.
///
/// The light is the sandbox's baked vertex colours (sandbox::bakeLighting()): the geometry carries them as librw's
/// prelighting and no lighting flag, so librw's stock GL3 pipeline draws texture × vertex colour × fog and needs no
/// custom shader. The textures (Kenney's prototype textures, assets/sandbox) are mipmapped, trilinear, repeated and
/// anisotropically filtered (up to 8×), so the 1 m grid stays crisp far off and at grazing angles.
///
/// A mesh of more than 65535 vertices is split into several atomics (librw's GL3 index buffers are 16-bit). Owns its
/// atomics, geometry, frames and textures; needs a running RenderEngine (either backend) and must be destroyed before
/// it stops. Coney's own feature: docs/guides/sandbox.md.
class SandboxRenderer {
  public:
    /// The geometry of `world` as librw atomics and, when `engine` draws pixels, its layout's textures read from the
    /// layout's folder. Fails with ErrorCode::NotFound for a texture file that is missing and ErrorCode::Invalid for
    /// one librw cannot read as a PNG.
    [[nodiscard]] static std::expected<std::unique_ptr<SandboxRenderer>, Error>
    create(const RenderEngine& engine, const sandbox::SandboxWorld& world);
    ~SandboxRenderer();
    SandboxRenderer(const SandboxRenderer&) = delete;
    SandboxRenderer& operator=(const SandboxRenderer&) = delete;
    SandboxRenderer(SandboxRenderer&&) = delete;
    SandboxRenderer& operator=(SandboxRenderer&&) = delete;

    /// One frame through `view`: the window cleared to the sky colour, the sandbox drawn with the layout's fog (from
    /// its start to the view's draw distance), then `drawObjects` (the player, say) when given, in the same camera with
    /// the render states left on, and the frame presented. With the NULL backend the frame is begun and presented and
    /// nothing is drawn.
    void render(RenderEngine& engine, const WorldView& view, const std::function<void()>& drawObjects = {});

    /// The atomics the mesh was split into.
    [[nodiscard]] std::size_t atomicCount() const { return m_atomics.size(); }
    /// The textures loaded (none with the NULL backend).
    [[nodiscard]] std::size_t textureCount() const;

  private:
    SandboxRenderer() = default;

    // Splits the mesh into atomics of at most 65535 vertices, one material per texture each uses.
    void buildAtomics(const sandbox::SandboxWorld& world);

    std::vector<rw::Atomic*> m_atomics;   // owned, each with its geometry and frame
    std::vector<rw::Texture*> m_textures; // owned; one per layout texture, null when not loaded
    graphics::Rgba m_sky;
    float m_fogStart = 0.0F;
};

/// The sky colour of a sandbox layout as a clear colour.
[[nodiscard]] graphics::Rgba skyColour(const sandbox::Lighting& lighting);

} // namespace coney::platform
