// SPDX-License-Identifier: GPL-3.0-or-later
// The Entity inspector over a play mode and the Debug draw page's collision shapes
// (docs/guides/debug-menu.md#entity-inspector): the humans and objects it lists, what it knows of each, freezing one,
// and the lines it adds to the scene. Coney's own debug tooling, in the debug tools' target
// (docs/guides/building.md#debug-tools): read-only but for freezing, nothing here changes how the game plays.
#include <cstddef>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "debug/human_shapes.h"
#include "debug/human_watch.h"
#include "debug/object_watch.h"
#include "platform/play_level_controls.h"

namespace coney::platform {

namespace {

// The inspector's ids (PlayLevelControls::kInspectPlayerId and the bases after it).
constexpr debug::EntityId kPlayerId = PlayLevelControls::kInspectPlayerId;
constexpr debug::EntityId kTargetBase = PlayLevelControls::kInspectTargetBase;
constexpr debug::EntityId kAiBase = PlayLevelControls::kInspectAiBase;
constexpr debug::EntityId kDoorBase = PlayLevelControls::kInspectDoorBase;
constexpr debug::EntityId kPaneBase = PlayLevelControls::kInspectPaneBase;
constexpr debug::EntityId kRecordBase = PlayLevelControls::kInspectRecordBase;

// The most collision triangles the classified wireframe draws in one frame: the plain wireframe's budget.
constexpr std::size_t kMaxClassifiedTriangles = 6000;
// How high the inspector's marker stands over the selection, in metres: over a standing human's head.
constexpr float kHighlightHeight = 2.4F;
// The selection's colour for an object's collision triangles.
constexpr graphics::Rgba kSelectedTriangle{255, 255, 255, 255};

// The index `id` names in the kind starting at `base`; nothing when it is another kind's.
std::optional<std::size_t> indexIn(debug::EntityId id, debug::EntityId base) {
    return PlayLevelControls::inspectIndexIn(id, base);
}

// `groups` with the Mesh group after them, when there is one.
std::vector<debug::WatchGroup> withMesh(std::vector<debug::WatchGroup> groups, std::optional<debug::WatchGroup> mesh) {
    if (mesh) {
        groups.push_back(std::move(*mesh));
    }
    return groups;
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

std::vector<debug::EntityRef> PlayLevelControls::Inspector::entities() const {
    return m_controls.m_mode != nullptr ? entitiesOf(*m_controls.m_mode) : std::vector<debug::EntityRef>{};
}

std::vector<debug::WatchGroup> PlayLevelControls::Inspector::describe(debug::EntityId id) const {
    return m_controls.m_mode != nullptr ? describeIn(*m_controls.m_mode, id) : std::vector<debug::WatchGroup>{};
}

std::vector<debug::EntityRef> PlayLevelControls::entitiesOf(const PlayLevelMode& mode) {
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

std::vector<debug::WatchGroup> PlayLevelControls::describeIn(const PlayLevelMode& mode, debug::EntityId id) {
    if (id == kPlayerId) {
        const debug::HumanFacts facts{.id = id,
                                      .role = "player 1",
                                      .model = mode.m_model,
                                      .type = mode.m_type,
                                      .brain = mode.m_ai ? &mode.m_ai->playerBrain() : nullptr,
                                      .frozen = mode.m_frozen};
        return withMesh(debug::describeHuman(mode.m_player->human(), facts), meshGroupOf(mode, id));
    }
    if (const auto index = indexIn(id, kTargetBase); index && *index < mode.m_targets.size()) {
        const debug::HumanFacts facts{.id = id,
                                      .role = "target",
                                      .model = modelOf(mode, id),
                                      .type = std::nullopt,
                                      .brain = nullptr,
                                      .frozen = mode.m_targets[*index].frozen};
        return withMesh(debug::describeTarget(*mode.m_targets[*index].human, facts), meshGroupOf(mode, id));
    }
    if (const auto index = indexIn(id, kAiBase); index && mode.m_ai && *index < mode.m_ai->humans().size()) {
        const ai::AiHuman& entry = mode.m_ai->humans()[*index];
        if (entry.removed) {
            return {};
        }
        const debug::HumanFacts facts{.id = id,
                                      .role = "AI human",
                                      .model = modelOf(mode, id),
                                      .type = std::nullopt,
                                      .brain = mode.m_ai->brainOf(*entry.human),
                                      .frozen = mode.m_player->humans().held(*entry.human)};
        return withMesh(debug::describeHuman(*entry.human, facts), meshGroupOf(mode, id));
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

human::Human* PlayLevelControls::humanOf(debug::EntityId id) const {
    if (m_mode == nullptr) {
        return nullptr;
    }
    if (id == kPlayerId) {
        return &m_mode->m_player->human();
    }
    if (const auto index = aiIndexOf(id)) {
        return m_mode->m_ai->humans()[*index].human.get();
    }
    return nullptr;
}

std::optional<std::size_t> PlayLevelControls::aiIndexOf(debug::EntityId id) const {
    if (m_mode == nullptr || !m_mode->m_ai) {
        return std::nullopt;
    }
    const auto index = indexIn(id, kAiBase);
    if (!index || *index >= m_mode->m_ai->humans().size() || m_mode->m_ai->humans()[*index].removed) {
        return std::nullopt;
    }
    return index;
}

bool PlayLevelControls::canFreezeEntity(debug::EntityId id) const {
    const PlayLevelMode& mode = *m_mode;
    if (id == kPlayerId) {
        return true;
    }
    if (const auto index = indexIn(id, kTargetBase)) {
        return *index < mode.m_targets.size();
    }
    if (const auto index = indexIn(id, kAiBase)) {
        return mode.m_ai && *index < mode.m_ai->humans().size() && !mode.m_ai->humans()[*index].removed;
    }
    return false;
}

bool PlayLevelControls::entityFrozen(debug::EntityId id) const {
    const PlayLevelMode& mode = *m_mode;
    if (id == kPlayerId) {
        return mode.m_frozen;
    }
    if (const auto index = indexIn(id, kTargetBase); index && *index < mode.m_targets.size()) {
        return mode.m_targets[*index].frozen;
    }
    if (const auto index = indexIn(id, kAiBase); index && canFreezeEntity(id)) {
        return mode.m_player->humans().held(*mode.m_ai->humans()[*index].human);
    }
    return false;
}

void PlayLevelControls::setEntityFrozen(debug::EntityId id, bool frozen) {
    if (!canFreezeEntity(id)) {
        return;
    }
    PlayLevelMode& mode = *m_mode;
    if (id == kPlayerId) {
        mode.m_frozen = frozen;
    } else if (const auto targetIndex = indexIn(id, kTargetBase)) {
        mode.m_targets[*targetIndex].frozen = frozen;
    } else if (const auto aiIndex = indexIn(id, kAiBase)) {
        mode.m_player->humans().setHeld(*mode.m_ai->humans()[*aiIndex].human, frozen);
    }
}

anim::Vec3 PlayLevelControls::drawnOffsetOf(const PlayLevelMode& mode, debug::EntityId id, float alpha) {
    // The models are drawn `alpha` of the way from the step before the newest to the newest (render()); a human's
    // shapes are built at its newest feet, so the offset is the distance between the two.
    const auto offsetOf = [alpha](const human::Combatant& human, const human::TargetSnapshot& previous,
                                  const human::TargetSnapshot& current) {
        return anim::subtract(human::interpolate(previous, current, alpha).feet, human.position());
    };
    if (const auto index = indexIn(id, kTargetBase); index && *index < mode.m_targets.size()) {
        const human::TargetHuman& target = *mode.m_targets[*index].human;
        return offsetOf(target, target.previous(), target.current());
    }
    if (const auto index = indexIn(id, kAiBase); index && mode.m_ai && *index < mode.m_ai->humans().size()) {
        const ai::AiHuman& fighter = mode.m_ai->humans()[*index];
        return offsetOf(*fighter.human, fighter.previous, fighter.current);
    }
    if (id == kPlayerId) {
        return anim::subtract(human::interpolate(mode.m_player->previous(), mode.m_player->current(), alpha).feet,
                              mode.m_player->human().position());
    }
    return {};
}

void PlayLevelControls::addInspectorLines(const PlayLevelMode& mode, const debug::DebugDrawOptions& options,
                                          debug::DebugLines& lines, const human::PlayerSnapshot& snapshot,
                                          float alpha) {
    const raycast::CollisionMesh& mesh = mode.m_scenery->collision();
    const anim::Vec3 feet = snapshot.feet;
    // The player's shapes move to where his model is drawn (his snapshot, which a scene may pose); the others' by
    // drawnOffsetOf().
    const anim::Vec3 playerOffset = anim::subtract(snapshot.feet, mode.m_player->human().position());
    // The collision wireframe coloured by the walking sweep's own classing (human::wallClassOf()).
    if (options.wallClasses) {
        debug::addCollisionWireframe(lines, mesh, feet, options.collisionRadius, true, kMaxClassifiedTriangles);
    }
    // The humans' shapes: every human's, or the player's and the selection's alone.
    const debug::ShapeLayers layers{.bodies = options.shapes, .strikes = options.strikes, .contacts = options.contacts};
    if (layers.bodies || layers.strikes || layers.contacts) {
        debug::addHumanShapes(lines, mode.m_player->human(), layers, playerOffset);
        if (mode.m_ai) {
            const auto& humans = mode.m_ai->humans();
            for (std::size_t i = 0; i < humans.size(); ++i) {
                if (!humans[i].removed && (options.shapesAllHumans || options.inspected == kAiBase + i)) {
                    debug::addHumanShapes(lines, *humans[i].human, layers, drawnOffsetOf(mode, kAiBase + i, alpha));
                }
            }
        }
    }
    // The selection's normals and bone axes (play_level_mesh.cpp).
    addMeshLines(mode, options, lines, snapshot, alpha);
    // The selection: a marker over it, and an object's own collision triangles.
    if (options.highlight && options.inspected != debug::kNoEntity) {
        const std::vector<debug::EntityRef> list = entitiesOf(mode);
        if (const debug::EntityRef* entity = debug::findEntity(list, options.inspected); entity != nullptr) {
            // The marker follows the model, not the newest step.
            const anim::Vec3 offset =
                options.inspected == kPlayerId ? playerOffset : drawnOffsetOf(mode, options.inspected, alpha);
            const anim::Vec3 drawnAt = anim::add(entity->position, offset);
            debug::addHighlight(lines, drawnAt, kHighlightHeight);
        }
        if (mode.m_objects != nullptr) {
            const auto doors = mode.m_objects->doors.doors();
            if (const auto index = indexIn(options.inspected, kDoorBase); index && *index < doors.size()) {
                for (const std::uint32_t triangle : doors[*index].triangles) {
                    addTriangle(lines, mesh, triangle, kSelectedTriangle);
                }
            }
            const auto panes = mode.m_objects->glass.panes();
            if (const auto index = indexIn(options.inspected, kPaneBase); index && *index < panes.size()) {
                for (const std::uint32_t triangle : panes[*index].triangles) {
                    addTriangle(lines, mesh, triangle, kSelectedTriangle);
                }
            }
        }
    }
}

} // namespace coney::platform
