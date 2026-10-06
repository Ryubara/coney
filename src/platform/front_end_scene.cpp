// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/front_end_scene.h"

#include <array>
#include <cmath>
#include <format>
#include <numbers>
#include <utility>

#include "core/game_timer.h"
#include "platform/world_viewer_mode.h"
#include "world/view_frustum.h"
#include "world/world_streamer.h"

namespace coney::platform {

namespace {

// The ambient light the world renderer lights atomics with: the LightManager's constant offset, as the world viewer.
constexpr float kAmbient = 40.0F / 255.0F;

// A point in the game's axes (z up) in RenderWare's (y up).
world::Vec3 renderWareOf(float x, float y, float z) { return world::Vec3{x, z, -y}; }

// `v` scaled to length 1.
world::Vec3 normalised(world::Vec3 v) {
    const float length = std::sqrt((v.x * v.x) + (v.y * v.y) + (v.z * v.z));
    return length > 0.0F ? world::Vec3{v.x / length, v.y / length, v.z / length} : v;
}

// The cross product a × b.
world::Vec3 cross(world::Vec3 a, world::Vec3 b) {
    return world::Vec3{(a.y * b.z) - (a.z * b.y), (a.z * b.x) - (a.x * b.z), (a.x * b.y) - (a.y * b.x)};
}

} // namespace

WorldView frontEndSceneView() {
    const world::Vec3 eye =
        renderWareOf(FrontEndWorldScene::kCameraX, FrontEndWorldScene::kCameraY, FrontEndWorldScene::kCameraZ);
    const world::Vec3 hub =
        renderWareOf(FrontEndWorldScene::kHubX, FrontEndWorldScene::kHubY, FrontEndWorldScene::kHubZ);
    // Towards the hub, turned about the up axis (RenderWare's y) to the left.
    const world::Vec3 toHub = normalised(world::Vec3{hub.x - eye.x, hub.y - eye.y, hub.z - eye.z});
    const float turn = -FrontEndWorldScene::kHubYawOffsetDegrees * std::numbers::pi_v<float> / 180.0F;
    const world::Vec3 forward = normalised(world::Vec3{(toHub.x * std::cos(turn)) - (toHub.z * std::sin(turn)), toHub.y,
                                                       (toHub.x * std::sin(turn)) + (toHub.z * std::cos(turn))});
    // RenderWare's camera frame: right = forward × world up, up = right × forward.
    const world::Vec3 right = normalised(cross(forward, world::Vec3{0.0F, 1.0F, 0.0F}));
    const world::Vec3 up = cross(right, forward);
    // The lens's view window on the 4:3 picture: tan(fov / 2) across and three quarters of it high.
    const float halfWidth = std::tan(FrontEndWorldScene::kFieldOfViewDegrees * std::numbers::pi_v<float> / 360.0F);
    return WorldView{.pose = world::CameraPose{.position = eye, .forward = forward, .up = up, .right = right},
                     .halfWidth = halfWidth,
                     .halfHeight = halfWidth * 0.75F,
                     .nearClip = FrontEndWorldScene::kNearClip,
                     .drawDistance = FrontEndWorldScene::kFarClip};
}

std::expected<std::unique_ptr<FrontEndWorldScene>, Error>
FrontEndWorldScene::create(RenderEngine& engine, const io::Wad& wad, std::string_view name,
                           const std::function<void(std::string_view)>& print) {
    auto scene = std::unique_ptr<FrontEndWorldScene>(new FrontEndWorldScene(engine));
    auto scenery = loadLevelScenery(engine, wad, name, scene->m_budget, print);
    if (!scenery) {
        return std::unexpected(std::move(scenery.error()));
    }
    scene->m_set = std::move(scenery->set);
    scene->m_level = std::move(scenery->level);
    // InitLevel's preload round the camera, before the first frame.
    const std::array<world::Vec3, 1> cameras{scene->m_view.pose.position};
    const world::PreloadResult preload =
        world::preloadWorlds(scene->m_set->worlds(), cameras, kFarClip, scene->m_budget, *scene->m_set, 0);
    print(std::format("front end: {} preloaded {} parts ({} failed)\n", name, preload.loaded, preload.failed));
    scene->update(0);
    return scene;
}

FrontEndWorldScene::FrontEndWorldScene(RenderEngine& engine)
    : m_engine(engine), m_renderer(kAmbient), m_view(frontEndSceneView()) {}

void FrontEndWorldScene::update(std::uint64_t nowMs) {
    // One streaming decision from the last step's visibility, then this step's visibility pass.
    const std::array<world::Vec3, 1> cameras{m_view.pose.position};
    (void)world::updateStreaming(m_set->worlds(), cameras, m_view.drawDistance, m_budget, *m_set, nowMs);
    m_pendingDistance = world::nearestPendingDistance(m_set->worlds(), cameras);
    const world::ViewFrustum frustum(m_view.pose, m_view.halfWidth, m_view.halfHeight, m_view.nearClip,
                                     m_view.drawDistance);
    for (world::StreamedWorld* world : m_set->worlds()) {
        world->findVisibleSectors(frustum, true);
    }
}

void FrontEndWorldScene::render(const RenderTime& time, const std::function<void()>& overlay) {
    const std::uint64_t nowMs = time.gameTicks / (GameTimer::kTicksPerSecond / 1000);
    m_renderer.render(m_engine, *m_set, m_level.get(), m_view, kBackground, m_pendingDistance, nowMs, {}, overlay);
}

} // namespace coney::platform
