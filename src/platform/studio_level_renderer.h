// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <expected>
#include <memory>
#include <string>

#include "core/error.h"
#include "platform/render_engine.h"
#include "platform/sandbox_renderer.h"
#include "studio/level_camera.h"
#include "studio/level_document.h"

namespace rw {
struct Camera;
}

namespace coney::platform {

/// Coney Studio's Level view drawn by the engine: the open level's sandbox layout through a librw camera whose frame
/// buffer is a texture (an OpenGL frame buffer object) the panel shows with Dear ImGui, the technology design's "the
/// engine's frame is one call away" (docs/guides/studio/technology.md#gui-choice). The sandbox is built and drawn as
/// `coney --sandbox` draws it (SandboxRenderer, baked light, fog), so the Level view shows what Coney shows.
///
/// Rebuilt when another level is opened, and its buffers when the panel changes size. Needs a running OpenGL
/// RenderEngine and must be destroyed before it stops. Coney's own; the original has no editor.
class StudioLevelRenderer {
  public:
    /// A renderer drawing with `engine`, which must outlive it.
    explicit StudioLevelRenderer(RenderEngine& engine) : m_engine(engine) {}
    /// Frees the sandbox's atomics and the camera with its buffers.
    ~StudioLevelRenderer();
    StudioLevelRenderer(const StudioLevelRenderer&) = delete;
    StudioLevelRenderer& operator=(const StudioLevelRenderer&) = delete;
    StudioLevelRenderer(StudioLevelRenderer&&) = delete;
    StudioLevelRenderer& operator=(StudioLevelRenderer&&) = delete;

    /// Draws `level` through `view` into the texture and returns its OpenGL name, or 0 when nothing could be drawn
    /// (the NULL backend, or a level whose sandbox could not be built: `lastError()` says why). Leaves the default
    /// frame buffer bound.
    std::uint64_t draw(const studio::LevelDocument& level, const studio::LevelView& view);
    /// Why the last level could not be drawn; empty when it could.
    [[nodiscard]] const std::string& lastError() const { return m_error; }

  private:
    // Builds the sandbox and its renderer for `level`; false (with m_error) when it cannot.
    bool build(const studio::LevelDocument& level);
    // Makes the camera's buffers `width` × `height`; false when librw cannot.
    bool resize(int width, int height);
    // Destroys the camera and its buffers.
    void destroyCamera() noexcept;

    RenderEngine& m_engine;
    std::uint64_t m_serial = 0; // the LevelDocument::serial() built for; 0 for none
    std::unique_ptr<sandbox::SandboxWorld> m_world;
    std::unique_ptr<SandboxRenderer> m_renderer;
    rw::Camera* m_camera = nullptr; // owned, with its frame and buffers
    int m_width = 0;
    int m_height = 0;
    std::string m_error;
};

} // namespace coney::platform
