// SPDX-License-Identifier: GPL-3.0-or-later
// The Entity inspector page, the one entity panel, and the Debug draw page's collision-shape items
// (docs/guides/debug-menu.md#entity-inspector): the selection at the top, then the groups, each with the selection's
// live values and, where the mode can edit it, its editable values inline (src/debug/pages_entity_edit.cpp). Coney's
// own debug tooling: nothing here changes how the game plays until an edit is made.
#include <array>
#include <cmath>
#include <format>
#include <memory>
#include <numbers>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "debug/debug_pages.h"
#include "debug/debug_session.h"
#include "debug/entity_edit.h"
#include "debug/entity_inspect.h"
#include "debug/play_controls.h"

namespace coney::debug {

namespace {

// What the page shows when no mode lists entities.
constexpr std::string_view kNoEntities = "no entities: --play-level, or a sandbox from the Levels page";
// One group of the page: its name, and whether it holds edits alone (shown only where the mode can edit).
struct GroupInfo {
    std::string_view name;
    bool editsOnly;
};
// The groups the page shows, in order; an entity has the ones that apply to it.
constexpr std::array<GroupInfo, 13> kGroups{{{"Identity", false},
                                             {"Transform", false},
                                             {"Locomotion", false},
                                             {"Animation", false},
                                             {"State flags", false},
                                             {"Meters", false},
                                             {"Stats", true},
                                             {"AI", false},
                                             {"Physics", false},
                                             {"Combat", false},
                                             {"Object", false},
                                             {"Mesh", false},
                                             {"Lifecycle", true}}};
// What a live value is called beside an edit of the same name ("Health" the edit, "Health now" the value).
constexpr std::string_view kNowSuffix = " now";
// How far off the camera's view an entity may be for Looked at to pick it, in degrees.
constexpr float kLookDegrees = 12.0F;
// How far from the entity Teleport player to it puts the player, in metres.
constexpr float kTeleportGap = 1.5F;

// The mode's entity list, or null when there is no mode or it lists none.
const EntityInspect* inspectorOf(const DebugSession& session) {
    const PlayControls* play = session.play();
    return play != nullptr ? play->inspect() : nullptr;
}

// The entities now; empty without an inspector.
std::vector<EntityRef> entitiesOf(const DebugSession& session) {
    const EntityInspect* inspect = inspectorOf(session);
    return inspect != nullptr ? inspect->entities() : std::vector<EntityRef>{};
}

// Selects `id` (kNoEntity clears) and prints what was selected.
void select(DebugSession& session, EntityId id) {
    session.debugDraw().inspected = id;
    const std::vector<EntityRef> entities = entitiesOf(session);
    const EntityRef* entity = findEntity(entities, id);
    session.print(entity != nullptr ? std::format("inspector: {} (entity {})", entity->label, entity->id)
                                    : std::string("inspector: nothing selected"));
}

// One line on the selection: its label and how far it is from the player.
std::string selectedText(const DebugSession& session) {
    const PlayControls* play = session.play();
    const std::vector<EntityRef> entities = entitiesOf(session);
    const EntityRef* entity = findEntity(entities, session.debugDraw().inspected);
    if (play == nullptr || entity == nullptr) {
        return "-";
    }
    return std::format("{} ({:.1f} m)", entity->label, anim::distance(entity->position, play->playerFeet()));
}

// The watch value `name` of group `group` of the selection now; "-" when it has none.
std::string selectedValue(const DebugSession& session, std::string_view group, std::string_view name) {
    const EntityInspect* inspect = inspectorOf(session);
    if (inspect == nullptr) {
        return "-";
    }
    const std::vector<WatchGroup> groups = inspect->describe(session.debugDraw().inspected);
    return watchValue(groups, group, name).value_or("-");
}

// A group's page: the selection's edits in `group` where the mode can edit it, then one live watch per line the
// selection has in `group` when the page opens; a line named like an edit is the value now beside it.
void fillGroup(MenuPage& page, DebugSession& session, std::string_view group) {
    addEntityEdits(page, session, group);
    const EntityInspect* inspect = inspectorOf(session);
    const std::vector<WatchGroup> groups =
        inspect != nullptr ? inspect->describe(session.debugDraw().inspected) : std::vector<WatchGroup>{};
    for (const WatchGroup& candidate : groups) {
        if (candidate.name != group) {
            continue;
        }
        for (const WatchLine& line : candidate.lines) {
            std::string label = page.find(line.name) != nullptr ? line.name + std::string(kNowSuffix) : line.name;
            page.add(watchItem(std::move(label), [&session, group = std::string(group), name = line.name] {
                return selectedValue(session, group, name);
            }));
        }
    }
    if (page.items().empty()) {
        page.add(watchItem("Nothing", [] { return std::string("none for this entity"); }));
    }
}

// The Choose page: every entity, choosing one selects it.
void fillChoose(MenuPage& page, DebugSession& session) {
    for (const EntityRef& entity : entitiesOf(session)) {
        // The id keeps the labels unique on the page.
        page.add(actionItem(std::format("{}: {}", entity.id, entity.label),
                            [&session, id = entity.id] { select(session, id); }));
    }
    if (page.items().empty()) {
        page.add(watchItem("No entities", [] { return std::string(kNoEntities); }));
    }
}

// Puts the player kTeleportGap from the selection, on his side of it, facing it.
void teleportToSelected(DebugSession& session) {
    PlayControls* play = session.play();
    const std::vector<EntityRef> entities = entitiesOf(session);
    const EntityRef* entity = findEntity(entities, session.debugDraw().inspected);
    if (play == nullptr || entity == nullptr) {
        session.print("inspector: nothing selected");
        return;
    }
    anim::Vec3 away = anim::subtract(play->playerFeet(), entity->position);
    away.z = 0.0F;
    away = anim::length(away) > 1e-3F ? anim::normalise(away) : anim::Vec3{0.0F, -1.0F, 0.0F};
    const anim::Vec3 spot = anim::add(entity->position, anim::scale(away, kTeleportGap));
    // Facing the entity: heading 0 faces +y, anticlockwise from above.
    const float heading = std::atan2(away.x, -away.y) * 180.0F / std::numbers::pi_v<float>;
    play->teleport(Place{entity->label, spot, heading});
}

// Whether the selection is frozen, or will be: a queued freeze shows at once, as every queued edit does.
bool selectionFrozen(const DebugSession& session) {
    const PlayControls* play = session.play();
    const EntityId id = session.debugDraw().inspected;
    if (const auto queued = session.entityEditor().queuedValue(id, EditOp::Frozen)) {
        return *queued != 0.0;
    }
    return play != nullptr && play->entityFrozen(id);
}

// Freezes the selection or lets it go; says so in the log when nothing is selected or it cannot be frozen. Where the
// mode edits its entities the freeze is the editor's frozen edit, queued like the others: it lands between two steps,
// leaves its run-log line and is refused on a network client. A mode that only inspects freezes at once.
void freezeSelected(DebugSession& session, bool on) {
    PlayControls* play = session.play();
    const EntityId id = session.debugDraw().inspected;
    if (play == nullptr || id == kNoEntity) {
        session.print("inspector: nothing selected");
    } else if (!play->canFreezeEntity(id)) {
        session.print("inspector: an object cannot be frozen, only a human");
    } else if (canEditEntities(session)) {
        session.entityEditor().queue(EntityCommand{.entity = id, .op = EditOp::Frozen, .values = {on ? 1.0 : 0.0}});
    } else {
        play->setEntityFrozen(id, on);
        session.print(std::format("inspector: entity {} {}", id, on ? "frozen" : "let go"));
    }
}

// The callbacks below copy strings and call through std::function; all they can throw is a failed allocation, which
// ends the program either way.
// NOLINTBEGIN(bugprone-exception-escape)

// The Entity inspector page's items: the selection and what acts on it, the edit queue, the groups, the logs.
void fillInspector(MenuPage& page, DebugSession& session) {
    if (inspectorOf(session) == nullptr) {
        page.add(watchItem("No entities", [] { return std::string(kNoEntities); }));
        return;
    }
    page.add(watchItem("Selected", [&session] { return selectedText(session); }));
    page.add(watchItem("Entities", [&session] {
        const std::vector<EntityRef> entities = entitiesOf(session);
        std::size_t objects = 0;
        for (const EntityRef& entity : entities) {
            objects += entity.kind == EntityKind::Object ? 1 : 0;
        }
        return std::format("{} humans, {} objects", entities.size() - objects, objects);
    }));
    page.add(actionItem("Nearest to player", [&session] {
                 const PlayControls* play = session.play();
                 const std::vector<EntityRef> entities = entitiesOf(session);
                 // Not the player himself: the nearest other entity.
                 const EntityId player =
                     entities.empty() || entities.front().kind != EntityKind::Player ? kNoEntity : entities.front().id;
                 select(session, play != nullptr ? nearestEntity(entities, play->playerFeet(), player) : kNoEntity);
             }).withHelp("The entity nearest the player, not counting him."));
    page.add(actionItem(
        "Next", [&session] { select(session, stepEntity(entitiesOf(session), session.debugDraw().inspected, 1)); }));
    page.add(actionItem("Previous", [&session] {
        select(session, stepEntity(entitiesOf(session), session.debugDraw().inspected, -1));
    }));
    page.add(actionItem("Looked at", [&session] {
                 const PlayControls* play = session.play();
                 const std::vector<EntityRef> entities = entitiesOf(session);
                 const EntityId player =
                     entities.empty() || entities.front().kind != EntityKind::Player ? kNoEntity : entities.front().id;
                 select(session, play != nullptr ? lookedAtEntity(entities, play->cameraEye(), play->cameraTarget(),
                                                                  kLookDegrees, player)
                                                 : kNoEntity);
             }).withHelp("The entity nearest the middle of the camera's view."));
    page.add(submenuItem("Choose", [&session] {
                 auto sub = std::make_shared<MenuPage>("Choose");
                 sub->setRebuild([&session](MenuPage& p) { fillChoose(p, session); });
                 return sub;
             }).withHelp("Every human and object in the scene."));
    page.add(actionItem("Clear", [&session] { select(session, kNoEntity); }));
    DebugDrawOptions& draw = session.debugDraw();
    page.add(toggleItem(
                 "Highlight", [&draw] { return draw.highlight; }, [&draw](bool on) { draw.highlight = on; }))
        .withHelp("Marks the selection in the scene, and names it under the corner lines.");
    page.add(toggleItem(
                 "Freeze selection", [&session] { return selectionFrozen(session); },
                 [&session](bool on) { freezeSelected(session, on); }))
        .withHelp("Holds the selected human still: it does not move, animate or act, and the others still hit it. "
                  "The player ignores the pad, as on the Player page.");
    page.add(actionItem("Teleport player to it", [&session] { teleportToSelected(session); }))
        .withHelp("Puts the player 1.5 m from it, facing it.");
    page.add(actionItem("Log everything", [&session] {
                 const EntityInspect* inspect = inspectorOf(session);
                 if (inspect == nullptr) {
                     return;
                 }
                 for (const std::string& line : flatten(inspect->describe(session.debugDraw().inspected))) {
                     session.print(line);
                 }
             }).withHelp("Prints every value of the selection to the log."));
    const bool edits = canEditEntities(session);
    if (edits) {
        addEntityEditHeader(page, session);
    }
    // One submenu per group, filled from the selection when opened.
    for (const GroupInfo& group : kGroups) {
        if (group.editsOnly && !edits) {
            continue;
        }
        page.add(submenuItem(std::string(group.name), [&session, name = group.name] {
            auto sub = std::make_shared<MenuPage>(std::string(name));
            sub->setRebuild([&session, name](MenuPage& p) { fillGroup(p, session, name); });
            return sub;
        }));
    }
    page.add(logItem("Log", [&session] { return session.log().last(3); }));
    if (edits) {
        page.add(entityEditLogItem(session));
    }
}

} // namespace

void addEntityInspectorPage(DebugSession& session) {
    session.model().addPage(
        "Entity inspector", [&session](MenuPage& page) { fillInspector(page, session); },
        "Select a human or object, watch everything Coney knows about it and edit it: transform, meters, stats, "
        "model, delete. Edits land between steps.");
    // The Entity editor was a page of its own until it became part of this one (#1053).
    session.model().addFormerTitle("Entity editor", "Entity inspector");
}

void addShapeDrawItems(MenuPage& page, DebugDrawOptions& draw) {
    const auto toggle = [&page](std::string label, bool& flag, std::string help) {
        page.add(toggleItem(
                     std::move(label), [&flag] { return flag; }, [&flag](bool on) { flag = on; }))
            .withHelp(std::move(help));
    };
    toggle("Wall classes", draw.wallClasses,
           "Colours the collision wireframe as the walking sweep sees it: walls red, low or thin faces yellow, floors "
           "green, ceilings blue, disabled grey.");
    toggle("Shapes", draw.shapes,
           "Each human's walking sphere (cyan; air sphere magenta when airborne), capsule, ground probe and climb.");
    toggle("Shapes on all humans", draw.shapesAllHumans,
           "Off: only on the player and the Entity inspector's selection.");
    toggle("Strike shapes", draw.strikes,
           "The strike shapes switched on (red) and the spine and head they hit (yellow).");
    toggle("Contacts", draw.contacts,
           "Where the walking sphere touched what it slid along this step, and the normals.");
}

void addMeshViewItems(MenuPage& page, DebugDrawOptions& draw) {
    MeshViewOptions& mesh = draw.mesh;
    // A scope choice over `scope`; `withOff` lists Off first, else the choice starts at the selection.
    const auto scopeChoice = [&page](std::string label, MeshScope& scope, bool withOff, std::string help) {
        std::vector<std::string> names{"selection", "all humans", "everything"};
        const std::size_t first = withOff ? 0 : 1;
        if (withOff) {
            names.insert(names.begin(), "off");
        }
        page.add(choiceItem(
                     std::move(label), std::move(names),
                     [&scope, first] {
                         const auto index = static_cast<std::size_t>(scope);
                         return index >= first ? index - first : 0;
                     },
                     [&scope, first](std::size_t index) { scope = static_cast<MeshScope>(index + first); }))
            .withHelp(std::move(help));
    };
    scopeChoice("Model wireframe", mesh.wireframe, true,
                "The models' own triangles as lines over the shaded scene, following the animation: the inspector's "
                "selection (white), every human (green) or everything drawn (objects orange, scenery grey).");
    page.add(choiceItem(
                 "Mesh view", {"shaded", "wireframe only", "by material", "by draw call", "by LOD"},
                 [&mesh] { return static_cast<std::size_t>(mesh.view); },
                 [&mesh](std::size_t index) { mesh.view = static_cast<MeshShading>(index); }))
        .withHelp("Shaded draws as the game does; the others draw the models of Mesh view on as their edges alone, or "
                  "flat and unlit, one colour per material, per draw call, or per level of detail and fade.");
    scopeChoice("Mesh view on", mesh.viewScope, false, "Which models the mesh view applies to.");
    page.add(toggleItem(
                 "Normals", [&mesh] { return mesh.normals; }, [&mesh](bool on) { mesh.normals = on; }))
        .withHelp("The selection's vertex normals, as short blue lines from the posed model.");
    page.add(toggleItem(
                 "Bone axes", [&mesh] { return mesh.boneAxes; }, [&mesh](bool on) { mesh.boneAxes = on; }))
        .withHelp("The selection's bones: each one's axes (x red, y green, z blue) and a line to its parent.");
}

// NOLINTEND(bugprone-exception-escape)

std::string inspectedCornerLine(const DebugSession& session) {
    if (!session.debugDraw().highlight || session.debugDraw().inspected == kNoEntity) {
        return {};
    }
    const std::string text = selectedText(session);
    return text == "-" ? std::string{} : "Inspecting: " + text;
}

} // namespace coney::debug
