// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/studio_level_renderer.h"

#include <algorithm>
#include <format>
#include <utility>

#include <rw.h>

#include "platform/world_renderer.h"

namespace coney::platform {

namespace {

// A game point or direction (z up) in RenderWare's axes, as the sandbox is drawn: (x, y, z) -> (x, z, -y).
world::Vec3 toRenderWare(anim::Vec3 v) { return world::Vec3{v.x, v.z, -v.y}; }

} // namespace

StudioLevelRenderer::~StudioLevelRenderer() {
    // The renderer's atomics before the engine goes, then the camera.
    m_renderer.reset();
    destroyCamera();
}

std::uint64_t StudioLevelRenderer::draw(const studio::LevelDocument& level, const studio::LevelView& view) {
    if (!m_engine.drawsPixels()) {
        return 0;
    }
    // 1. The level's sandbox, built once per opened level, and the buffers at the panel's size.
    if (level.serial() != m_serial && !build(level)) {
        return 0;
    }
    if (m_renderer == nullptr) {
        return 0;
    }
    const int width = std::max(1, static_cast<int>(view.width));
    const int height = std::max(1, static_cast<int>(view.height));
    if ((width != m_width || height != m_height) && !resize(width, height)) {
        return 0;
    }
    // 2. The camera at the Level view's pose and view window, the far clip at the fog's end as the sandbox viewer's.
    const sandbox::Lighting& lighting = level.layout().lighting;
    const WorldView worldView{.pose = world::CameraPose{.position = toRenderWare(view.pose.position),
                                                        .forward = toRenderWare(view.pose.forward),
                                                        .up = toRenderWare(view.pose.up),
                                                        .right = toRenderWare(view.pose.right)},
                              .halfWidth = view.halfWidth,
                              .halfHeight = view.halfHeight,
                              .nearClip = view.nearClip,
                              .drawDistance = lighting.fogEnd,
                              .fogStart = 0.5F};
    placeWorldCamera(m_camera, worldView);
    m_camera->fogPlane = std::min(lighting.fogStart, lighting.fogEnd * 0.99F);
    // 3. Cleared to the sky and drawn with the world pass's states and the sky-coloured fog, as `coney --sandbox`.
    const graphics::Rgba sky = skyColour(lighting);
    rw::RGBA clear = rw::makeRGBA(sky.r, sky.g, sky.b, 255);
    m_camera->clear(&clear, rw::Camera::CLEARIMAGE | rw::Camera::CLEARZ);
    m_camera->beginUpdate();
    rw::SetRenderState(rw::FOGENABLE, 1);
    rw::SetRenderState(rw::FOGCOLOR, static_cast<rw::uint32>(sky.r) | static_cast<rw::uint32>(sky.g) << 8U |
                                         static_cast<rw::uint32>(sky.b) << 16U | 0xFF000000U);
    rw::SetRenderState(rw::SRCBLEND, rw::BLENDSRCALPHA);
    rw::SetRenderState(rw::DESTBLEND, rw::BLENDINVSRCALPHA);
    m_renderer->drawGeometry(m_engine);
    rw::SetRenderState(rw::FOGENABLE, 0);
    m_camera->endUpdate();
    // librw leaves the texture's frame buffer bound; Dear ImGui draws into the window's.
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    const auto* native = PLUGINOFFSET(rw::gl3::Gl3Raster, m_camera->frameBuffer, rw::gl3::nativeRasterOffset);
    return native->texid;
}

bool StudioLevelRenderer::build(const studio::LevelDocument& level) {
    m_serial = level.serial();
    m_renderer.reset();
    m_world.reset();
    m_error.clear();
    auto world = sandbox::SandboxWorld::build(level.layout(), level.path().parent_path());
    if (!world) {
        m_error = world.error().message;
        return false;
    }
    m_world = std::make_unique<sandbox::SandboxWorld>(std::move(*world));
    auto renderer = SandboxRenderer::create(m_engine, *m_world);
    if (!renderer) {
        m_error = renderer.error().message;
        return false;
    }
    m_renderer = std::move(*renderer);
    return true;
}

bool StudioLevelRenderer::resize(int width, int height) {
    destroyCamera();
    m_camera = rw::Camera::create();
    m_camera->setFrame(rw::Frame::create());
    m_camera->frameBuffer =
        rw::Raster::create(width, height, 32, static_cast<rw::int32>(rw::Raster::CAMERATEXTURE) | rw::Raster::C8888);
    m_camera->zBuffer = rw::Raster::create(width, height, 0, rw::Raster::ZBUFFER);
    if (m_camera->frameBuffer == nullptr || m_camera->zBuffer == nullptr) {
        m_error = std::format("could not make a {}x{} frame buffer for the Level view", width, height);
        destroyCamera();
        return false;
    }
    m_width = width;
    m_height = height;
    return true;
}

void StudioLevelRenderer::destroyCamera() noexcept {
    if (m_camera == nullptr) {
        return;
    }
    // The buffers and the frame first: Camera::destroy leaves them alone.
    if (m_camera->frameBuffer != nullptr) {
        m_camera->frameBuffer->destroy();
    }
    if (m_camera->zBuffer != nullptr) {
        m_camera->zBuffer->destroy();
    }
    rw::Frame* frame = m_camera->getFrame();
    m_camera->setFrame(nullptr);
    if (frame != nullptr) {
        frame->destroy();
    }
    m_camera->destroy();
    m_camera = nullptr;
    m_width = 0;
    m_height = 0;
}

} // namespace coney::platform
