// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/play_scenery.h"

#include <algorithm>
#include <array>
#include <format>
#include <utility>

#include "camera/camera_lens.h"
#include "core/game_timer.h"
#include "world/view_frustum.h"
#include "world/world_streamer.h"

namespace coney::platform {

namespace {

// The stand-in start drops from this high above the first part's sectors.
constexpr float kStandInDrop = 500.0F;
// A level's character light shines down and away from the usual camera side (game axes, z up): Coney's choice.
constexpr anim::Vec3 kLevelLightDirection{0.3F, 0.5F, -0.8F};
// How far above a sandbox spawn point the player is dropped onto the ground from.
constexpr float kSpawnDrop = 2.0F;

} // namespace

world::Vec3 toRenderWare(anim::Vec3 game) { return world::Vec3{game.x, game.z, -game.y}; }

anim::Vec3 directionToRenderWare(anim::Vec3 game) { return anim::Vec3{game.x, game.z, -game.y}; }

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
                       std::function<void(std::string_view)> print) {
    // The scenery, as LoadLevel reads it; a level needs its level file for the ground.
    auto scenery = loadLevelScenery(engine, wad, name, budget, print);
    if (!scenery) {
        return std::unexpected(std::move(scenery.error()));
    }
    if (!scenery->level || !scenery->level->collision) {
        return fail(ErrorCode::NotFound, std::format("{} has no level file to stand on", name));
    }
    const std::optional<human::PlayerStart> researched = human::researchedPlayerStart(name);
    const human::PlayerStart start =
        researched ? *researched : playLevelStandInStart(*scenery->set, *scenery->level->collision);
    std::unique_ptr<LevelPlayScenery> made(
        new LevelPlayScenery(std::move(*scenery), budget, start, researched.has_value(), std::move(print)));
    made->m_name = std::string(name);
    return made;
}

LevelPlayScenery::LevelPlayScenery(LevelScenery scenery, world::SectorBudget& budget, const human::PlayerStart& start,
                                   bool researched, std::function<void(std::string_view)> print)
    : m_scenery(std::move(scenery)), m_budget(budget), m_start(start), m_researched(researched),
      m_renderer(WorldViewerMode::kAmbient), m_print(std::move(print)),
      m_drawDistance(camera::kPlayerCameraLens.farClip) {}

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
}

void LevelPlayScenery::findVisible(const WorldView& newest) {
    const world::ViewFrustum frustum(newest.pose, newest.halfWidth, newest.halfHeight, newest.nearClip,
                                     newest.drawDistance);
    for (world::StreamedWorld* streamed : m_scenery.set->worlds()) {
        streamed->findVisibleSectors(frustum, true);
    }
}

void LevelPlayScenery::draw(RenderEngine& engine, const WorldView& view, std::uint64_t nowMs,
                            const std::function<void()>& drawObjects) {
    // The world draws the sectors its own view sees (WorldRenderer::render), with the characters among the objects.
    m_renderer.render(engine, *m_scenery.set, m_scenery.level.get(), view, WorldViewerMode::kFogColour, m_pending,
                      nowMs, drawObjects);
}

anim::Vec3 LevelPlayScenery::lightDirection() const { return kLevelLightDirection; }

std::string LevelPlayScenery::summary() const {
    return std::format("; parts read {}, freed {}, failed {}", m_loads, m_unloads, m_failures);
}

std::vector<debug::Place> PlayScenery::places() const {
    const human::PlayerStart s = start();
    return {debug::Place{startSource(), s.position, s.headingDegrees}};
}

std::expected<void, Error> PlayScenery::setExtras(const RenderEngine& /*engine*/,
                                                  const std::vector<sandbox::Primitive>& /*extra*/) {
    return fail(ErrorCode::InvalidArgument, "objects can be spawned in a sandbox only, for now");
}

std::expected<std::unique_ptr<SandboxPlayScenery>, Error>
SandboxPlayScenery::create(const RenderEngine& engine, sandbox::SandboxWorld world,
                           const std::optional<std::string>& spawn) {
    if (world.collision() == nullptr) {
        return fail(ErrorCode::NotFound, "the sandbox layout has nothing solid to stand on");
    }
    // The spawn point, dropped onto the ground below it as a level's start is.
    const auto& spawns = world.layout().spawns;
    const auto found =
        spawn ? std::ranges::find_if(spawns, [&spawn](const auto& s) { return s.name == *spawn; }) : spawns.begin();
    if (found == spawns.end()) {
        std::string known;
        for (const sandbox::SpawnPoint& s : spawns) {
            known += (known.empty() ? "" : ", ") + s.name;
        }
        return fail(ErrorCode::NotFound, std::format("the sandbox layout has no spawn point \"{}\" (there are: {})",
                                                     spawn.value_or(""), known));
    }
    raycast::Vec3 point{found->position.x, found->position.y, found->position.z + kSpawnDrop};
    if (!raycast::dropToGround(*world.collision(), kSpawnDrop * 2.0F, point)) {
        point.z = found->position.z;
    }
    const human::PlayerStart start{.position = anim::Vec3{point.x, point.y, point.z},
                                   .headingDegrees = found->headingDegrees};
    auto renderer = SandboxRenderer::create(engine, world);
    if (!renderer) {
        return std::unexpected(std::move(renderer.error()));
    }
    std::string name = found->name;
    return std::unique_ptr<SandboxPlayScenery>(
        new SandboxPlayScenery(std::move(world), std::move(*renderer), start, std::move(name)));
}

SandboxPlayScenery::SandboxPlayScenery(sandbox::SandboxWorld world, std::unique_ptr<SandboxRenderer> renderer,
                                       const human::PlayerStart& start, std::string spawn)
    : m_world(std::move(world)), m_made(m_world.layout()), m_renderer(std::move(renderer)), m_start(start),
      m_spawn(std::move(spawn)) {}

std::vector<debug::Place> SandboxPlayScenery::places() const {
    std::vector<debug::Place> places;
    for (const sandbox::SpawnPoint& spawn : m_world.layout().spawns) {
        places.push_back(debug::Place{spawn.name, spawn.position, spawn.headingDegrees});
    }
    return places;
}

std::expected<void, Error> SandboxPlayScenery::setExtras(const RenderEngine& engine,
                                                         const std::vector<sandbox::Primitive>& extra) {
    // Build the new world and its renderer first, so a failure leaves the scenery as it was.
    sandbox::SandboxLayout layout = m_made;
    layout.primitives.insert(layout.primitives.end(), extra.begin(), extra.end());
    auto world = sandbox::SandboxWorld::build(std::move(layout), m_world.folder());
    if (!world) {
        return std::unexpected(std::move(world.error()));
    }
    if (world->collision() == nullptr) {
        return fail(ErrorCode::InvalidArgument, "the sandbox would have nothing solid to stand on");
    }
    auto renderer = SandboxRenderer::create(engine, *world);
    if (!renderer) {
        return std::unexpected(std::move(renderer.error()));
    }
    m_world = std::move(*world);
    m_renderer = std::move(*renderer);
    return {};
}

void SandboxPlayScenery::draw(RenderEngine& engine, const WorldView& view, std::uint64_t /*nowMs*/,
                              const std::function<void()>& drawObjects) {
    m_renderer->render(engine, view, drawObjects);
}

std::string SandboxPlayScenery::summary() const {
    return std::format("; sandbox \"{}\", {} collision triangles", m_world.layout().title,
                       m_world.collision()->triangles().size());
}

} // namespace coney::platform
