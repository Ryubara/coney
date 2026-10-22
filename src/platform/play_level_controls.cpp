// SPDX-License-Identifier: GPL-3.0-or-later
// The debug menus' way into a play mode, and the Debug draw page's lines in its scene
// (docs/guides/debug-menu.md#debug-draw). Coney's own debug tooling, in the debug tools' target
// (docs/guides/building.md#debug-tools): nothing here changes how the game plays.
#include "platform/play_level_controls.h"

#include <cstddef>
#include <memory>
#include <numbers>
#include <utility>
#include <vector>

#include "human/human.h"
#include "platform/sandbox_play_scenery.h"
#include "raycast/collision_mesh.h"

namespace coney::platform {

namespace {

// The most collision triangles the Debug draw wireframe draws in one frame.
constexpr std::size_t kMaxWireTriangles = 6000;
// The size of a debug marker's cross and of a heading line, in metres.
constexpr float kMarkerSize = 0.3F;
constexpr float kHeadingLength = 1.0F;

} // namespace

std::vector<SceneLine> PlayLevelControls::sceneLines(const PlayLevelMode& mode, const debug::DebugDrawOptions& options,
                                                     const human::PlayerSnapshot& snapshot, float alpha) {
    debug::DebugLines lines;
    const anim::Vec3 feet = snapshot.feet;
    // The collision triangles whose first corner is near the feet, as a wireframe, up to a budget.
    if (options.collision && !options.wallClasses) {
        const raycast::CollisionMesh& mesh = mode.m_scenery->collision();
        const auto vertices = mesh.vertices();
        const float reach = options.collisionRadius * options.collisionRadius;
        std::size_t drawn = 0;
        for (const raycast::CollisionTriangle& triangle : mesh.triangles()) {
            const raycast::Vec3 a = vertices[triangle.vertices[0]];
            const float dx = a.x - feet.x;
            const float dy = a.y - feet.y;
            const float dz = a.z - feet.z;
            if (dx * dx + dy * dy + dz * dz > reach) {
                continue;
            }
            const raycast::Vec3 b = vertices[triangle.vertices[1]];
            const raycast::Vec3 c = vertices[triangle.vertices[2]];
            constexpr graphics::Rgba kWire{80, 220, 255, 255};
            lines.add({a.x, a.y, a.z}, {b.x, b.y, b.z}, kWire);
            lines.add({b.x, b.y, b.z}, {c.x, c.y, c.z}, kWire);
            lines.add({c.x, c.y, c.z}, {a.x, a.y, a.z}, kWire);
            if (++drawn == kMaxWireTriangles) {
                break;
            }
        }
    }
    // The player: a cross at the feet, the heading, and the velocity (a second's travel).
    const human::Human& player = mode.m_player->human();
    if (options.player) {
        constexpr graphics::Rgba kPlayer{255, 230, 60, 255};
        lines.cross(feet, kMarkerSize, kPlayer);
        const anim::Vec3 ahead = human::facing(snapshot.heading);
        lines.add(feet, anim::add(feet, anim::scale(ahead, kHeadingLength)), kPlayer);
        lines.add(feet, anim::add(feet, player.velocity()), graphics::Rgba{255, 120, 40, 255});
    }
    if (options.groundNormal) {
        lines.add(feet, anim::add(feet, player.groundNormal()), graphics::Rgba{120, 255, 120, 255});
    }
    // The follow camera: where it wants to be and what it looks at.
    if (options.camera) {
        constexpr graphics::Rgba kCamera{255, 80, 220, 255};
        lines.cross(mode.m_player->camera().wanted(), kMarkerSize, kCamera);
        lines.cross(snapshot.cameraTarget, kMarkerSize, kCamera);
        lines.add(snapshot.cameraEye, snapshot.cameraTarget, kCamera);
    }
    // The scene's places, each a cross and its heading.
    if (options.places) {
        constexpr graphics::Rgba kPlace{255, 255, 255, 255};
        for (const human::Place& place : mode.m_scenery->places()) {
            lines.cross(place.feet, kMarkerSize * 2.0F, kPlace);
            const anim::Vec3 ahead = human::facing(place.headingDegrees * std::numbers::pi_v<float> / 180.0F);
            lines.add(place.feet, anim::add(place.feet, anim::scale(ahead, kHeadingLength)), kPlace);
        }
    }
    // The collision shapes, the classified wireframe and the Entity inspector's highlight (play_level_inspect.cpp).
    addInspectorLines(mode, options, lines, snapshot, alpha);
    std::vector<SceneLine> out;
    out.reserve(lines.lines().size());
    for (const debug::DebugLine& line : lines.lines()) {
        out.push_back(SceneLine{.a = line.a, .b = line.b, .colour = line.colour});
    }
    return out;
}

SceneLines PlayLevelControls::sceneLinesFor(const debug::DebugDrawOptions& options) {
    return [&options](const PlayLevelMode& mode, const human::PlayerSnapshot& snapshot, float alpha) {
        return sceneLines(mode, options, snapshot, alpha);
    };
}

std::expected<void, Error> PlayLevelControls::spawn(const sandbox::Primitive& primitive) {
    std::vector<sandbox::Primitive> next = sandboxPrimitivesOf(m_mode->sceneExtras());
    next.push_back(primitive);
    return setSpawned(std::move(next));
}

std::expected<void, Error> PlayLevelControls::clearSpawned() { return setSpawned({}); }

std::expected<void, Error> PlayLevelControls::setSpawned(std::vector<sandbox::Primitive> primitives) {
    if (dynamic_cast<const SandboxPlayScenery*>(m_mode->m_scenery.get()) != nullptr) {
        // The sandbox rebuilds its own world from the primitives: they need no mesh or drawing of their own.
        const std::size_t count = primitives.size();
        return m_mode->setSceneExtras(SceneExtras{
            .solid = {},
            .draw = {},
            .count = count,
            .source =
                count == 0 ? nullptr : std::make_shared<const std::vector<sandbox::Primitive>>(std::move(primitives))});
    }
    auto extras = sandboxExtras(m_mode->m_engine, std::move(primitives));
    if (!extras) {
        return std::unexpected(std::move(extras.error()));
    }
    return m_mode->setSceneExtras(std::move(*extras));
}

void PlayLevelControls::stopAnim() { m_mode->m_player->human().stopDebugClip(); }

} // namespace coney::platform
