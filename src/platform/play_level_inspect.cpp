// SPDX-License-Identifier: GPL-3.0-or-later
// The play mode's side of the Entity inspector and of the Debug draw page's collision shapes
// (docs/guides/debug-menu.md#entity-inspector): the humans and objects it lists, what it knows of each, and the lines
// it adds to the scene. Coney's own debug tooling, read-only: nothing here changes how the game plays.
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <vector>

#include "debug/human_shapes.h"
#include "debug/human_watch.h"
#include "debug/object_watch.h"
#include "platform/play_level_mode.h"

namespace coney::platform {

namespace {

// The inspector's ids: player 1 is 1; the others are a kind's base plus their index in the mode's lists (an AI human
// keeps its index once removed, so its id lasts as long as it does) or, for a world object, its handle.
constexpr debug::EntityId kPlayerId = 1;
constexpr debug::EntityId kTargetBase = 0x10000;
constexpr debug::EntityId kAiBase = 0x20000;
constexpr debug::EntityId kDoorBase = 0x30000;
constexpr debug::EntityId kPaneBase = 0x40000;
constexpr debug::EntityId kRecordBase = 0x100000000ULL;
// The size of each indexed kind's range.
constexpr debug::EntityId kRange = 0x10000;

// The most collision triangles the classified wireframe draws in one frame: the plain wireframe's budget.
constexpr std::size_t kMaxClassifiedTriangles = 6000;
// How high the inspector's marker stands over the selection, in metres: over a standing human's head.
constexpr float kHighlightHeight = 2.4F;
// The selection's colour for an object's collision triangles.
constexpr graphics::Rgba kSelectedTriangle{255, 255, 255, 255};

// The index `id` names in the kind starting at `base`; nothing when it is another kind's.
std::optional<std::size_t> indexIn(debug::EntityId id, debug::EntityId base) {
    if (id < base || id >= base + kRange) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(id - base);
}

// A world object's id from its handle (a whole number in the world objects' handle space).
debug::EntityId recordId(double handle) {
    return kRecordBase + static_cast<debug::EntityId>(static_cast<std::uint32_t>(handle));
}

// Adds a collision triangle of `mesh` as three lines, when the index is in range.
void addTriangle(debug::DebugLines& lines, const raycast::CollisionMesh& mesh, std::uint32_t index,
                 graphics::Rgba colour) {
    const auto triangles = mesh.triangles();
    if (index >= triangles.size()) {
        return;
    }
    const auto vertices = mesh.vertices();
    const raycast::CollisionTriangle& triangle = triangles[index];
    const raycast::Vec3 a = vertices[triangle.vertices[0]];
    const raycast::Vec3 b = vertices[triangle.vertices[1]];
    const raycast::Vec3 c = vertices[triangle.vertices[2]];
    lines.add({a.x, a.y, a.z}, {b.x, b.y, b.z}, colour);
    lines.add({b.x, b.y, b.z}, {c.x, c.y, c.z}, colour);
    lines.add({c.x, c.y, c.z}, {a.x, a.y, a.z}, colour);
}

} // namespace

std::vector<debug::EntityRef> PlayLevelMode::Inspector::entities() const {
    const PlayLevelMode& mode = m_mode;
    std::vector<debug::EntityRef> list;
    // Player 1 first, then the targets, the AI humans and the objects, each in the mode's own order.
    list.push_back(debug::EntityRef{.id = kPlayerId,
                                    .kind = debug::EntityKind::Player,
                                    .label = std::format("player 1 ({})", mode.m_model),
                                    .position = mode.m_player->human().position()});
    for (std::size_t i = 0; i < mode.m_targets.size(); ++i) {
        list.push_back(debug::EntityRef{.id = kTargetBase + i,
                                        .kind = debug::EntityKind::Human,
                                        .label = std::format("target {}", i + 1),
                                        .position = mode.m_targets[i].human->position()});
    }
    if (mode.m_ai) {
        const auto& humans = mode.m_ai->humans();
        for (std::size_t i = 0; i < humans.size(); ++i) {
            if (humans[i].removed) {
                continue;
            }
            const ai::Brain* brain = mode.m_ai->brainOf(*humans[i].human);
            list.push_back(debug::EntityRef{
                .id = kAiBase + i,
                .kind = debug::EntityKind::Human,
                .label = std::format("AI {} {}", i + 1, brain != nullptr ? debug::brainTypeName(brain->type()) : ""),
                .position = humans[i].human->position()});
        }
    }
    if (mode.m_objects != nullptr) {
        const auto doors = mode.m_objects->doors.doors();
        for (std::size_t i = 0; i < doors.size(); ++i) {
            if (!doors[i].removed) {
                list.push_back(debug::EntityRef{.id = kDoorBase + i,
                                                .kind = debug::EntityKind::Object,
                                                .label = std::format("door {} {}", i + 1, doors[i].type),
                                                .position = doors[i].position});
            }
        }
        const auto panes = mode.m_objects->glass.panes();
        for (std::size_t i = 0; i < panes.size(); ++i) {
            list.push_back(debug::EntityRef{.id = kPaneBase + i,
                                            .kind = debug::EntityKind::Object,
                                            .label = std::format("pane {}{}", i + 1, panes[i].broken ? " broken" : ""),
                                            .position = panes[i].centre});
        }
    }
    // The world objects whose object exists now.
    if (mode.m_records != nullptr) {
        for (const world_objects::SpawnRecord& record : mode.m_records->all()) {
            if (record.live && !record.removed) {
                list.push_back(debug::EntityRef{
                    .id = recordId(record.handle),
                    .kind = debug::EntityKind::Object,
                    .label = std::format("object {}", record.typeName),
                    .position = anim::Vec3{record.position[0], record.position[1], record.position[2]}});
            }
        }
    }
    return list;
}

std::vector<debug::WatchGroup> PlayLevelMode::Inspector::describe(debug::EntityId id) const {
    const PlayLevelMode& mode = m_mode;
    if (id == kPlayerId) {
        const debug::HumanFacts facts{.id = id,
                                      .role = "player 1",
                                      .model = mode.m_model,
                                      .type = mode.m_type,
                                      .brain = mode.m_ai ? &mode.m_ai->playerBrain() : nullptr,
                                      .frozen = mode.m_frozen};
        return debug::describeHuman(mode.m_player->human(), facts);
    }
    if (const auto index = indexIn(id, kTargetBase); index && *index < mode.m_targets.size()) {
        const debug::HumanFacts facts{
            .id = id, .role = "target", .model = mode.m_model, .type = std::nullopt, .brain = nullptr, .frozen = false};
        return debug::describeTarget(*mode.m_targets[*index].human, facts);
    }
    if (const auto index = indexIn(id, kAiBase); index && mode.m_ai && *index < mode.m_ai->humans().size()) {
        const ai::AiHuman& entry = mode.m_ai->humans()[*index];
        if (entry.removed) {
            return {};
        }
        // The model its mesh is skinned as, when the scripts' cast named one.
        std::string model;
        if (*index < mode.m_fighterMeshes.size()) {
            for (const auto& [name, loaded] : mode.m_castCharacters) {
                if (loaded.character.get() == mode.m_fighterMeshes[*index].character) {
                    model = name;
                }
            }
        }
        const debug::HumanFacts facts{.id = id,
                                      .role = "AI human",
                                      .model = model.empty() && mode.m_levelNumber == 0 ? mode.m_model : model,
                                      .type = std::nullopt,
                                      .brain = mode.m_ai->brainOf(*entry.human),
                                      .frozen = false};
        return debug::describeHuman(*entry.human, facts);
    }
    if (mode.m_objects != nullptr) {
        const auto doors = mode.m_objects->doors.doors();
        if (const auto index = indexIn(id, kDoorBase); index && *index < doors.size()) {
            return debug::describeDoor(doors[*index], id);
        }
        const auto panes = mode.m_objects->glass.panes();
        if (const auto index = indexIn(id, kPaneBase); index && *index < panes.size()) {
            return debug::describePane(panes[*index], id);
        }
    }
    if (mode.m_records != nullptr && id >= kRecordBase) {
        for (const world_objects::SpawnRecord& record : mode.m_records->all()) {
            if (recordId(record.handle) == id) {
                return debug::describeSpawnRecord(record, id);
            }
        }
    }
    return {};
}

void PlayLevelMode::addInspectorLines(debug::DebugLines& lines, anim::Vec3 feet) const {
    const debug::DebugDrawOptions& options = *m_debugDraw;
    const raycast::CollisionMesh& mesh = m_scenery->collision();
    // The collision wireframe coloured by the walking sweep's own classing (human::wallClassOf()).
    if (options.wallClasses) {
        debug::addCollisionWireframe(lines, mesh, feet, options.collisionRadius, true, kMaxClassifiedTriangles);
    }
    // The humans' shapes: every human's, or the player's and the selection's alone.
    const debug::ShapeLayers layers{.bodies = options.shapes, .strikes = options.strikes, .contacts = options.contacts};
    if (layers.bodies || layers.strikes || layers.contacts) {
        debug::addHumanShapes(lines, m_player->human(), layers);
        if (m_ai) {
            const auto& humans = m_ai->humans();
            for (std::size_t i = 0; i < humans.size(); ++i) {
                if (!humans[i].removed && (options.shapesAllHumans || options.inspected == kAiBase + i)) {
                    debug::addHumanShapes(lines, *humans[i].human, layers);
                }
            }
        }
    }
    // The selection: a marker over it, and an object's own collision triangles.
    if (options.highlight && options.inspected != debug::kNoEntity) {
        const std::vector<debug::EntityRef> list = m_inspector.entities();
        if (const debug::EntityRef* entity = debug::findEntity(list, options.inspected); entity != nullptr) {
            debug::addHighlight(lines, entity->position, kHighlightHeight);
        }
        if (m_objects != nullptr) {
            const auto doors = m_objects->doors.doors();
            if (const auto index = indexIn(options.inspected, kDoorBase); index && *index < doors.size()) {
                for (const std::uint32_t triangle : doors[*index].triangles) {
                    addTriangle(lines, mesh, triangle, kSelectedTriangle);
                }
            }
            const auto panes = m_objects->glass.panes();
            if (const auto index = indexIn(options.inspected, kPaneBase); index && *index < panes.size()) {
                for (const std::uint32_t triangle : panes[*index].triangles) {
                    addTriangle(lines, mesh, triangle, kSelectedTriangle);
                }
            }
        }
    }
}

} // namespace coney::platform
