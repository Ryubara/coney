// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/play_scenery.h"

#include <algorithm>
#include <array>
#include <format>
#include <utility>

#include "camera/camera_lens.h"
#include "core/assert.h"
#include "core/game_timer.h"
#include "world/view_frustum.h"
#include "world/world_streamer.h"

namespace coney::platform {

namespace {

// The stand-in start drops from this high above the first part's sectors.
constexpr float kStandInDrop = 500.0F;
// A level's character light shines down and away from the usual camera side (game axes, z up): Coney's choice.
constexpr anim::Vec3 kLevelLightDirection{0.3F, 0.5F, -0.8F};

} // namespace

world::Vec3 toRenderWare(anim::Vec3 game) { return world::Vec3{game.x, game.z, -game.y}; }

anim::Vec3 directionToRenderWare(anim::Vec3 game) { return anim::Vec3{game.x, game.z, -game.y}; }

WorldView worldViewOf(const camera::CameraView& view, float aspect, float drawDistance) {
    const anim::Vec3 forward = anim::normalise(directionToRenderWare(camera::viewForward(view)));
    const anim::Vec3 up = anim::normalise(directionToRenderWare(camera::viewUp(view)));
    const anim::Vec3 right = anim::normalise(anim::cross(forward, up));
    const camera::ViewWindow window = camera::viewWindow(
        camera::CameraLens{.fieldOfView = view.fieldOfView, .nearClip = view.nearClip, .farClip = view.farClip});
    return WorldView{.pose = world::CameraPose{.position = toRenderWare(view.position),
                                               .forward = world::Vec3{forward.x, forward.y, forward.z},
                                               .up = world::Vec3{up.x, up.y, up.z},
                                               .right = world::Vec3{right.x, right.y, right.z}},
                     .halfWidth = window.halfHeight * aspect,
                     .halfHeight = window.halfHeight,
                     .nearClip = view.nearClip,
                     .drawDistance = drawDistance};
}

human::PlayerStart playLevelStandInStart(const WorldSet& set, const raycast::CollisionMesh& mesh) {
    // The world viewer's start is in RenderWare's axes: back to the game's, (x, y, z) -> (x, -z, y).
    const world::Vec3 top = viewerStartPosition(*set.worlds().front());
    raycast::Vec3 point{top.x, -top.z, top.y + 1.0F};
    if (!raycast::dropToGround(mesh, kStandInDrop, point)) {
        point.z = top.y;
    }
    return human::PlayerStart{.position = anim::Vec3{point.x, point.y, point.z}, .headingDegrees = 0.0F};
}

std::expected<std::unique_ptr<LevelPlayScenery>, Error>
LevelPlayScenery::load(RenderEngine& engine, const io::Wad& wad, std::string_view name, world::SectorBudget& budget,
                       std::function<void(std::string_view)> print, std::optional<human::PlayerStart> scriptStart) {
    // The scenery, as LoadLevel reads it; a level needs its level file for the ground.
    auto scenery = loadLevelScenery(engine, wad, name, budget, print);
    if (!scenery) {
        return std::unexpected(std::move(scenery.error()));
    }
    if (!scenery->level || !scenery->level->collision) {
        return fail(ErrorCode::NotFound, std::format("{} has no level file to stand on", name));
    }
    // The start: the level script's player 1, else the researched one, else Coney's stand-in.
    human::PlayerStart start;
    std::string source;
    if (scriptStart) {
        start = *scriptStart;
        source = "the level script's start";
    } else if (const std::optional<human::PlayerStart> researched = human::researchedPlayerStart(name); researched) {
        start = *researched;
        source = "the level's start";
    } else {
        start = playLevelStandInStart(*scenery->set, *scenery->level->collision);
        source = "Coney's stand-in start";
    }
    std::unique_ptr<LevelPlayScenery> made(
        new LevelPlayScenery(std::move(*scenery), budget, start, std::move(source), std::move(print)));
    made->m_name = std::string(name);
    // The path data for the AI's routes; a level whose data does not decode plays with straight moves.
    if (auto paths = world::PathMap::decode(made->m_scenery.level->pathData); paths) {
        made->m_paths = std::move(*paths);
    } else {
        made->m_print(std::format("path data: {}; AI moves go straight\n", paths.error().message));
    }
    return made;
}

LevelPlayScenery::LevelPlayScenery(LevelScenery scenery, world::SectorBudget& budget, const human::PlayerStart& start,
                                   std::string startSource, std::function<void(std::string_view)> print)
    : m_scenery(std::move(scenery)), m_budget(budget), m_start(start), m_startSource(std::move(startSource)),
      m_print(std::move(print)), m_drawDistance(camera::kPlayerCameraLens.farClip) {}

void LevelPlayScenery::preload(world::Vec3 camera) {
    const std::array<world::Vec3, 1> cameras{camera};
    const world::PreloadResult preload =
        world::preloadWorlds(m_scenery.set->worlds(), cameras, m_drawDistance, m_budget, *m_scenery.set, 0);
    m_unloads += preload.unloaded;
    m_failures += preload.failed;
    m_print(std::format("preload: {} parts read, {} freed, {} failed; {} atomics resident\n", preload.loaded,
                        preload.unloaded, preload.failed, m_scenery.set->residentAtomics()));
}

void LevelPlayScenery::step(world::Vec3 camera, const FrameTime& frame) {
    // One streaming decision around the camera, then the draw distance.
    WorldSet& set = *m_scenery.set;
    const std::array<world::Vec3, 1> cameras{camera};
    const std::uint64_t nowMs = frame.gameTicks / (GameTimer::kTicksPerSecond / 1000);
    const world::StreamStep streamed =
        world::updateStreaming(set.worlds(), cameras, m_drawDistance, m_budget, set, nowMs);
    switch (streamed.result) {
    case world::StreamResult::Loaded:
        ++m_loads;
        break;
    case world::StreamResult::Unloaded:
        ++m_unloads;
        break;
    case world::StreamResult::Failed:
        ++m_failures;
        m_print(std::format("frame {}: part {} failed: {}\n", frame.index, streamed.part, streamed.error));
        break;
    case world::StreamResult::NoRoom:
    case world::StreamResult::Idle:
        break;
    }
    m_pending = world::nearestPendingDistance(set.worlds(), cameras);
    m_drawDistance = world::adjustDrawDistance(m_drawDistance,
                                               world::DrawDistanceInputs{.pending = m_pending,
                                                                         .farClip = camera::kPlayerCameraLens.farClip,
                                                                         .seconds = static_cast<float>(frame.seconds),
                                                                         .frameRate = 30.0F,
                                                                         .viewports = 1,
                                                                         .lowRateMode = false});
    // The debug tools' world edits, on what this step loaded.
    if (m_edits) {
        m_edits->afterStep(m_editSteps++, set, m_scenery.level->collision.get());
    }
}

void LevelPlayScenery::setEditHook(const std::shared_ptr<WorldEditHook>& hook) { m_edits = hook; }

void LevelPlayScenery::findVisible(const WorldView& newest) {
    const world::ViewFrustum frustum(newest.pose, newest.halfWidth, newest.halfHeight, newest.nearClip,
                                     newest.drawDistance);
    for (world::StreamedWorld* streamed : m_scenery.set->worlds()) {
        streamed->findVisibleSectors(frustum, true);
    }
}

void LevelPlayScenery::draw(RenderEngine& engine, const WorldView& view, std::uint64_t nowMs,
                            const std::function<void()>& drawObjects, const std::function<void()>& afterWorlds) {
    // The world draws the sectors its own view sees (WorldRenderer::render), with the characters among the objects;
    // the debug menus' spawned objects go first among those, in the world's camera, states and fog.
    if (!m_extras.draw && !m_edits) {
        m_renderer.render(engine, *m_scenery.set, m_scenery.level.get(), view, m_pending, nowMs, drawObjects, {},
                          afterWorlds);
        return;
    }
    // The debug tools' world edits draw their pieces of each world first after it (WorldEditHook::drawAfterWorld()).
    const auto withExtras = [this, &engine, &drawObjects] {
        if (m_edits) {
            m_edits->drawAfterWorld(0, m_renderer.lighting());
        }
        if (m_extras.draw) {
            m_extras.draw(engine);
        }
        if (drawObjects) {
            drawObjects();
        }
    };
    const auto withEdits = [this, &afterWorlds] {
        if (m_edits) {
            m_edits->drawAfterWorld(1, m_renderer.lighting());
        }
        if (afterWorlds) {
            afterWorlds();
        }
    };
    const world::LevelObject* level = m_edits && !m_edits->drawsBackground() ? nullptr : m_scenery.level.get();
    m_renderer.render(engine, *m_scenery.set, level, view, m_pending, nowMs, withExtras, {}, withEdits);
}

std::expected<void, Error> LevelPlayScenery::setExtras(const RenderEngine& /*engine*/, SceneExtras extras) {
    raycast::CollisionMesh& mesh = *m_scenery.level->collision;
    // The old objects' triangles out, the new ones in; on a refusal the old ones go back (they fitted before, so they
    // fit again).
    mesh.clearExtraTriangles();
    if (!extras.solid.empty()) {
        if (auto added = mesh.addExtraTriangles(extras.solid); !added) {
            const bool restored = mesh.addExtraTriangles(m_extras.solid).has_value();
            CONEY_ASSERT(restored);
            return added;
        }
    }
    m_extras = std::move(extras);
    return {};
}

anim::Vec3 LevelPlayScenery::lightDirection() const { return kLevelLightDirection; }

std::string LevelPlayScenery::summary() const {
    return std::format("; parts read {}, freed {}, failed {}", m_loads, m_unloads, m_failures) +
           (m_extras.solid.empty() ? std::string{}
                                   : std::format(", spawned collision triangles {}", m_extras.solid.size()));
}

// By value, as the overrides take the extras over; this one, for a scenery that has no room for them, drops them.
// NOLINTNEXTLINE(performance-unnecessary-value-param)
std::expected<void, Error> PlayScenery::setExtras(const RenderEngine& /*engine*/, SceneExtras /*extras*/) {
    return fail(ErrorCode::InvalidArgument, "objects cannot be spawned here");
}

const SceneExtras& PlayScenery::extras() const {
    static const SceneExtras kNone;
    return kNone;
}

} // namespace coney::platform
