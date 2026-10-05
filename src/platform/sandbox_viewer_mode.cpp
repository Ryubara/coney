// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/sandbox_viewer_mode.h"

#include <format>
#include <numbers>
#include <utility>

#include "camera/camera_lens.h"
#include "gamemodes/game_mode_stack.h"

namespace coney::platform {

namespace {

constexpr float kDegrees = std::numbers::pi_v<float> / 180.0F;

} // namespace

world::DebugCamera debugCameraAt(const sandbox::Viewpoint& view, float speed) {
    world::DebugCamera camera(world::Vec3{view.position.x, view.position.z, -view.position.y}, speed);
    // The game's heading looks along (-sin ψ, cos ψ, 0); in RenderWare's axes that is the debug camera's yaw ψ + π.
    camera.setOrientation(view.yawDegrees * kDegrees + std::numbers::pi_v<float>, view.pitchDegrees * kDegrees);
    return camera;
}

WorldView sandboxView(const world::CameraPose& pose, graphics::Extent size, const sandbox::Lighting& lighting) {
    // The player camera's view window on a 4:3 picture; another shape keeps its height (as the world viewer does).
    const camera::ViewWindow window = camera::viewWindow(camera::kPlayerCameraLens);
    const float aspect =
        size.height > 0 ? static_cast<float>(size.width) / static_cast<float>(size.height) : 4.0F / 3.0F;
    return WorldView{.pose = pose,
                     .halfWidth = window.halfHeight * aspect,
                     .halfHeight = window.halfHeight,
                     .nearClip = camera::kPlayerCameraLens.nearClip,
                     .drawDistance = lighting.fogEnd};
}

std::expected<std::unique_ptr<SandboxViewerMode>, Error>
SandboxViewerMode::create(RenderEngine& engine, sandbox::SandboxWorld world,
                          std::function<void(std::string_view)> print) {
    auto renderer = SandboxRenderer::create(engine, world);
    if (!renderer) {
        return std::unexpected(std::move(renderer.error()));
    }
    return std::unique_ptr<SandboxViewerMode>(
        new SandboxViewerMode(engine, std::move(world), std::move(*renderer), std::move(print)));
}

SandboxViewerMode::SandboxViewerMode(RenderEngine& engine, sandbox::SandboxWorld world,
                                     std::unique_ptr<SandboxRenderer> renderer,
                                     std::function<void(std::string_view)> print)
    : m_engine(engine), m_world(std::move(world)), m_renderer(std::move(renderer)), m_print(std::move(print)),
      m_camera(debugCameraAt(m_world.layout().views.front(), kFlySpeed)) {}

void SandboxViewerMode::goToView(std::size_t index) {
    const auto& views = m_world.layout().views;
    m_view = index % views.size();
    m_camera.reset(debugCameraAt(views[m_view], kFlySpeed)); // a jump, not a move: nothing to blend from
}

ModeResult SandboxViewerMode::update(GameModeStack& stack, const FrameTime& frame) {
    // The camera render() blends moves on a step.
    m_camera.commit();
    const Pad& pad = stack.pads().port(0);
    const std::size_t count = m_world.layout().views.size();
    if ((pad.pressed() & pad::kTriangle) != 0 || (pad.pressed() & pad::kSquare) != 0) {
        goToView((pad.pressed() & pad::kTriangle) != 0 ? m_view + 1 : m_view + count - 1);
        m_print(std::format("frame {}: view {}\n", frame.index, m_world.layout().views[m_view].name));
    } else {
        m_camera.current().update(pad, static_cast<float>(frame.seconds));
    }
    ++m_frames;
    return ModeResult::Stay;
}

void SandboxViewerMode::render(const RenderTime& time) {
    // The camera `alpha` of the way between its last two steps: its position, and its turn the short way round.
    const world::DebugCamera& from = m_camera.previous();
    const world::DebugCamera& to = m_camera.current();
    world::DebugCamera between(world::Vec3{lerp(from.position().x, to.position().x, time.alpha),
                                           lerp(from.position().y, to.position().y, time.alpha),
                                           lerp(from.position().z, to.position().z, time.alpha)});
    between.setOrientation(lerpAngle(from.yaw(), to.yaw(), time.alpha), lerp(from.pitch(), to.pitch(), time.alpha));
    m_renderer->render(m_engine, sandboxView(between.pose(), m_engine.frameSize(), m_world.layout().lighting));
}

std::string SandboxViewerMode::summary() const {
    const sandbox::SandboxWorld& w = m_world;
    const world::Vec3 p = m_camera.current().position();
    return std::format("sandbox: {} frames, layout \"{}\": {} primitives, {} vertices, {} triangles in {} atomics, {} "
                       "collision triangles, {} textures; view {}, camera at ({:.2f}, {:.2f}, {:.2f})\n",
                       m_frames, w.layout().title, w.layout().primitives.size(), w.mesh().vertices.size(),
                       w.mesh().triangles.size(), m_renderer->atomicCount(),
                       w.collision() != nullptr ? w.collision()->triangles().size() : 0, m_renderer->textureCount(),
                       w.layout().views[m_view].name, p.x, -p.z, p.y);
}

} // namespace coney::platform
