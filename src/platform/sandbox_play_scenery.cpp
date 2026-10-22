// SPDX-License-Identifier: GPL-3.0-or-later
// The sandbox as the play mode's scenery (docs/guides/sandbox.md), and the objects of sandbox primitives that the
// debug menus' Spawner puts in a level or a sandbox: Coney's own test worlds, in the debug tools' target
// (docs/guides/building.md#debug-tools).
#include "platform/sandbox_play_scenery.h"

#include <algorithm>
#include <cstddef>
#include <format>
#include <utility>

#include "raycast/collision_builder.h"
#include "raycast/collision_mesh.h"
#include "sandbox/sandbox_mesh.h"

namespace coney::platform {

namespace {

// How far above a sandbox spawn point the player is dropped onto the ground from.
constexpr float kSpawnDrop = 2.0F;

} // namespace

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

std::vector<human::Place> SandboxPlayScenery::places() const {
    std::vector<human::Place> places;
    for (const sandbox::SpawnPoint& spawn : m_world.layout().spawns) {
        places.push_back(human::Place{spawn.name, spawn.position, spawn.headingDegrees});
    }
    return places;
}

std::vector<SceneTarget> SandboxPlayScenery::targets() const {
    std::vector<SceneTarget> targets;
    for (const sandbox::TargetPoint& point : m_world.layout().targets) {
        targets.push_back(
            SceneTarget{.feet = point.position, .headingDegrees = point.headingDegrees, .health = point.health});
    }
    return targets;
}

std::vector<human::Place> SandboxPlayScenery::fighters() const {
    std::vector<human::Place> fighters;
    for (const sandbox::FighterPoint& point : m_world.layout().fighters) {
        fighters.push_back(human::Place{point.name, point.position, point.headingDegrees});
    }
    return fighters;
}

std::expected<void, Error> SandboxPlayScenery::setExtras(const RenderEngine& engine, SceneExtras extras) {
    // Build the new world and its renderer first, so a failure leaves the scenery as it was.
    const std::vector<sandbox::Primitive> extra = sandboxPrimitivesOf(extras);
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
    // The rebuilt world holds and draws them: only the count and the source are kept.
    m_extras = SceneExtras{.solid = {}, .draw = {}, .count = extras.count, .source = std::move(extras.source)};
    return {};
}

void SandboxPlayScenery::draw(RenderEngine& engine, const WorldView& view, std::uint64_t nowMs,
                              const std::function<void()>& drawObjects, const std::function<void()>& afterWorlds) {
    // The manager's viewport pass (the ambients and the cull) before anything is lit with it.
    if (m_lighting != nullptr) {
        m_lighting->beginFrame(view.pose, view.nearClip, view.drawDistance, nowMs);
    }
    // The sandbox has one world, so what follows the worlds comes straight after the objects.
    m_renderer->render(engine, view, [&drawObjects, &afterWorlds] {
        if (drawObjects) {
            drawObjects();
        }
        if (afterWorlds) {
            afterWorlds();
        }
    });
}

std::string SandboxPlayScenery::summary() const {
    return std::format("; sandbox \"{}\", {} collision triangles", m_world.layout().title,
                       m_world.collision()->triangles().size());
}

std::expected<std::unique_ptr<PlayLevelMode>, Error> createPlayInSandbox(RenderEngine& engine, const io::Wad& wad,
                                                                         sandbox::SandboxWorld world,
                                                                         const std::optional<std::string>& spawn,
                                                                         std::function<void(std::string_view)> print,
                                                                         const PlayerSetup& setup) {
    auto scenery = SandboxPlayScenery::create(engine, std::move(world), spawn);
    if (!scenery) {
        return std::unexpected(std::move(scenery.error()));
    }
    PlayerSetup used = setup;
    used.snapToGround = true;
    return PlayLevelMode::createWith(engine, wad, std::move(*scenery), std::move(print), used);
}

std::expected<SceneExtras, Error> sandboxExtras(const RenderEngine& engine,
                                                std::vector<sandbox::Primitive> primitives) {
    if (primitives.empty()) {
        return SceneExtras{};
    }
    sandbox::SandboxLayout layout;
    layout.primitives = primitives;
    auto world = sandbox::SandboxWorld::build(std::move(layout));
    if (!world) {
        return std::unexpected(std::move(world.error()));
    }
    auto renderer = SandboxRenderer::create(engine, *world);
    if (!renderer) {
        return std::unexpected(std::move(renderer.error()));
    }
    std::vector<raycast::BuildTriangle> solid = sandbox::sandboxCollisionTriangles(world->layout());
    // The renderer is shared by the drawing hook, which a scenery may copy.
    std::shared_ptr<SandboxRenderer> drawn = std::move(*renderer);
    const std::size_t count = primitives.size();
    return SceneExtras{.solid = std::move(solid),
                       .draw = [drawn](RenderEngine& on) { drawn->drawGeometry(on); },
                       .count = count,
                       .source = std::make_shared<const std::vector<sandbox::Primitive>>(std::move(primitives))};
}

std::vector<sandbox::Primitive> sandboxPrimitivesOf(const SceneExtras& extras) {
    if (!extras.source) {
        return {};
    }
    return *std::static_pointer_cast<const std::vector<sandbox::Primitive>>(extras.source);
}

} // namespace coney::platform
