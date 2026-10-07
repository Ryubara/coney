// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/front_end_scene.h"

#include <array>
#include <format>
#include <utility>

#include <rw.h>

#include "camera/camera_lens.h"
#include "core/game_timer.h"
#include "platform/play_scenery.h"
#include "platform/world_viewer_mode.h"
#include "world/view_frustum.h"
#include "world/world_streamer.h"
#include "world_objects/object_list.h"

namespace coney::platform {

namespace {

// `v` turned by `q`.
anim::Vec3 rotate(anim::Quat q, anim::Vec3 v) { return anim::transformDirection(anim::matrixFromQuat(q), v); }

// A scene pose `alpha` of the way from `a` to `b`: the position lerped, the rotation slerped.
scenes::ScenePose blend(const scenes::ScenePose& a, const scenes::ScenePose& b, float alpha) {
    return scenes::ScenePose{.position = anim::lerp(a.position, b.position, alpha),
                             .rotation = anim::slerp(a.rotation, b.rotation, alpha)};
}

} // namespace

WorldView sceneCameraWorldView(const scenes::ScenePose& pose, const scenes::SceneLens& lens) {
    // The camera looks along its rotation's +y with +z up, carried into RenderWare's axes; right is forward × up, as
    // the play mode's stage takes a scene camera (docs/research/scenes.md#coneys-implementation).
    const anim::Vec3 forward = anim::normalise(directionToRenderWare(rotate(pose.rotation, {0.0F, 1.0F, 0.0F})));
    const anim::Vec3 up = anim::normalise(directionToRenderWare(rotate(pose.rotation, {0.0F, 0.0F, 1.0F})));
    const anim::Vec3 right = anim::normalise(anim::cross(forward, up));
    const camera::ViewWindow window = camera::viewWindow(
        camera::CameraLens{.fieldOfView = lens.fieldOfView, .nearClip = lens.nearClip, .farClip = lens.farClip});
    return WorldView{.pose = world::CameraPose{.position = toRenderWare(pose.position),
                                               .forward = world::Vec3{forward.x, forward.y, forward.z},
                                               .up = world::Vec3{up.x, up.y, up.z},
                                               .right = world::Vec3{right.x, right.y, right.z}},
                     .halfWidth = window.halfWidth,
                     .halfHeight = window.halfHeight,
                     .nearClip = lens.nearClip,
                     .drawDistance = lens.farClip};
}

std::expected<std::unique_ptr<FrontEndWorldScene>, Error>
FrontEndWorldScene::create(RenderEngine& engine, const io::Wad& wad, std::string_view name,
                           const std::function<void(std::string_view)>& print, FrontEndObjectSource objects) {
    auto scene = std::unique_ptr<FrontEndWorldScene>(new FrontEndWorldScene(engine, print));
    auto scenery = loadLevelScenery(engine, wad, name, scene->m_budget, print);
    if (!scenery) {
        return std::unexpected(std::move(scenery.error()));
    }
    scene->m_set = std::move(scenery->set);
    scene->m_level = std::move(scenery->level);
    // The dynamic objects' models come by the Object List; without it the scene draws none.
    scene->m_source = objects;
    if (objects.records != nullptr && objects.types != nullptr) {
        if (auto list = world_objects::loadObjectList(wad); list) {
            scene->m_objectList = std::make_unique<world_objects::ObjectList>(std::move(*list));
            scene->m_objects = std::make_unique<PlacedObjects>(wad, *scene->m_objectList, print, engine.drawsPixels());
        } else {
            print(std::format("front end: no dynamic objects: {}\n", list.error().message));
        }
    }
    // InitLevel's preload round the script's camera finds nothing within its far clip (it stands about 550 m from the
    // wheel); the world round the scene camera is preloaded when that camera starts.
    return scene;
}

FrontEndWorldScene::FrontEndWorldScene(RenderEngine& engine, std::function<void(std::string_view)> print)
    : m_engine(engine), m_print(std::move(print)), m_sceneLighting(m_lighting, std::nullopt, std::nullopt) {
    // The light manager as it starts (the world ambient at the brightness alone) until the scripts set the level's
    // lights, with the front end's black fog.
    m_lighting.fog.colour = kBackground;
    m_renderer.setLighting(&m_sceneLighting);
}

FrontEndWorldScene::~FrontEndWorldScene() = default;

void FrontEndWorldScene::update(std::uint64_t nowMs) {
    // The scene camera the scenes' update ended, started or moved: an end drops it, a start cuts to it, a move
    // becomes the newest step.
    if (m_cameraEnded) {
        m_camera.reset();
        m_cameraEnded = false;
    }
    if (m_cameraNext) {
        if (m_camera) {
            m_camera->commit();
            m_camera->current() = *m_cameraNext;
        } else {
            m_camera.emplace(*m_cameraNext);
            m_preloadPending = true;
        }
        m_cameraNext.reset();
    } else if (m_camera) {
        m_camera->commit();
    }

    // With the scene camera: the world round it preloaded when it has just started, then one streaming decision from
    // the last step's visibility and this step's visibility pass. The script's camera sees nothing to stream.
    if (m_camera) {
        const WorldView view = viewAt(1.0F);
        const std::array<world::Vec3, 1> cameras{view.pose.position};
        if (m_preloadPending) {
            m_preloadPending = false;
            const world::PreloadResult preload =
                world::preloadWorlds(m_set->worlds(), cameras, kPreloadRadius, m_budget, *m_set, nowMs);
            m_print(std::format("front end: preloaded {} parts round the scene camera ({} failed)\n", preload.loaded,
                                preload.failed));
        }
        (void)world::updateStreaming(m_set->worlds(), cameras, view.drawDistance, m_budget, *m_set, nowMs);
        m_pendingDistance = world::nearestPendingDistance(m_set->worlds(), cameras);
        const world::ViewFrustum frustum(view.pose, view.halfWidth, view.halfHeight, view.nearClip, view.drawDistance);
        for (world::StreamedWorld* world : m_set->worlds()) {
            world->findVisibleSectors(frustum, true);
        }
    }
    syncObjects();
}

std::optional<WorldView> FrontEndWorldScene::view() const {
    if (!m_camera) {
        return std::nullopt;
    }
    return viewAt(1.0F);
}

WorldView FrontEndWorldScene::viewAt(float alpha) const {
    const CameraState& a = m_camera->previous();
    const CameraState& b = m_camera->current();
    return sceneCameraWorldView(blend(a.pose, b.pose, alpha), b.lens);
}

void FrontEndWorldScene::syncObjects() {
    if (m_objects == nullptr) {
        return;
    }
    // Drop the objects whose records are gone (a fresh script state) or no longer live.
    const auto gone = [this](double handle) {
        const world_objects::SpawnRecord* record = m_source.records->find(handle);
        return record == nullptr || !record->live || record->removed;
    };
    for (auto it = m_placed.begin(); it != m_placed.end();) {
        if (gone(it->first)) {
            m_objects->remove(it->first);
            it = m_placed.erase(it);
        } else {
            ++it;
        }
    }
    std::erase_if(m_poses, [&gone](const auto& entry) { return gone(entry.first); });
    std::erase_if(m_hidden, [this](double handle) { return m_source.records->find(handle) == nullptr; });
    // Every live record's pose for this step: the scene's last, else its record's. A new object starts there.
    for (const world_objects::SpawnRecord& record : m_source.records->all()) {
        const world_objects::ObjectType* type = m_source.types->find(record.typeName);
        if (!record.live || record.removed || type == nullptr) {
            continue;
        }
        const auto given = m_poses.find(record.handle);
        const scenes::ScenePose pose =
            given != m_poses.end()
                ? given->second
                : scenes::ScenePose{.position = anim::Vec3{record.position[0], record.position[1], record.position[2]},
                                    .rotation = anim::Quat{record.rotation[0], record.rotation[1], record.rotation[2],
                                                           record.rotation[3]}};
        if (auto placed = m_placed.find(record.handle); placed != m_placed.end()) {
            placed->second.commit();
            placed->second.current() = pose;
        } else {
            m_placed.emplace(record.handle, Interpolated<scenes::ScenePose>(pose));
        }
        m_objects->place(record.handle, type->modelHash, pose.position, pose.rotation);
        m_objects->setVisible(record.handle, !m_hidden.contains(record.handle));
    }
}

void FrontEndWorldScene::placeObjects(float alpha) {
    if (m_objects == nullptr) {
        return;
    }
    for (const auto& [handle, poses] : m_placed) {
        const world_objects::SpawnRecord* record = m_source.records->find(handle);
        const world_objects::ObjectType* type = record != nullptr ? m_source.types->find(record->typeName) : nullptr;
        if (type == nullptr) {
            continue;
        }
        const scenes::ScenePose pose = blend(poses.previous(), poses.current(), alpha);
        m_objects->place(handle, type->modelHash, pose.position, pose.rotation);
    }
}

void FrontEndWorldScene::objectPose(double object, const scenes::ScenePose& pose) { m_poses[object] = pose; }

void FrontEndWorldScene::objectMessage(double object, int message) {
    // simple_object's show (0x12) and hide (0x13).
    if (message == 0x12) {
        m_hidden.erase(object);
    } else if (message == 0x13) {
        m_hidden.insert(object);
    }
    if (m_objects != nullptr) {
        m_objects->setVisible(object, !m_hidden.contains(object));
    }
}

void FrontEndWorldScene::objectRelease(double object) {
    // Back to the object manager: the record is unpinned, and the object stays where the scene left it.
    if (m_source.records != nullptr) {
        m_source.records->setPinned(object, false);
    }
}

void FrontEndWorldScene::lightSet(std::size_t /*index*/, const scenes::ScenePose& /*pose*/,
                                  const scenes::SceneLight& /*light*/) {
    ++m_sceneLights;
}

void FrontEndWorldScene::cameraBegin(const scenes::ScenePose& pose, const scenes::SceneLens& lens) {
    // The scene camera made current with no blend: a cut at the next update.
    m_camera.reset();
    m_cameraEnded = false;
    m_cameraNext = CameraState{.pose = pose, .lens = lens};
}

void FrontEndWorldScene::cameraPose(const scenes::ScenePose& pose, const scenes::SceneLens& lens) {
    m_cameraNext = CameraState{.pose = pose, .lens = lens};
}

void FrontEndWorldScene::cameraEnd(float /*blendSeconds*/) {
    // The script's camera current again: it sees nothing, so a blend back would show only black.
    m_cameraEnded = true;
    m_cameraNext.reset();
}

std::optional<scenes::ScenePose> FrontEndWorldScene::objectPoseOf(double handle) const {
    const auto found = m_poses.find(handle);
    return found != m_poses.end() ? std::optional<scenes::ScenePose>(found->second) : std::nullopt;
}

void FrontEndWorldScene::render(const RenderTime& time, const std::function<void()>& overlay) {
    // The script's camera: the black background, then the menus.
    if (!m_camera) {
        m_engine.beginFrame(kBackground);
        if (overlay) {
            overlay();
        }
        m_engine.present();
        return;
    }
    const std::uint64_t nowMs = time.gameTicks / (GameTimer::kTicksPerSecond / 1000);
    placeObjects(time.alpha);
    // The objects lit as a world object is (0x0017fd78, docs/research/lighting.md): the objects' lights for its squared
    // distance and the point lights it meets, the selection a human's draw makes. **Coney's choice**: the pulse
    // (object flag 0x80) and the small-radius exception are not applied; the wheel's parts are not small.
    const std::function<void()> drawObjects = [this] {
        if (m_objects != nullptr) {
            m_objects->draw([this](rw::Atomic* atomic) { m_sceneLighting.drawHumanAtomic(atomic, false); });
        }
    };
    m_renderer.render(m_engine, *m_set, m_level.get(), viewAt(time.alpha), m_pendingDistance, nowMs, drawObjects,
                      overlay);
}

} // namespace coney::platform
