// SPDX-License-Identifier: GPL-3.0-or-later
// The Entity inspector page and the Debug draw page's collision-shape items (docs/guides/debug-menu.md
// #entity-inspector). Coney's own debug tooling: nothing here changes how the game plays.
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
#include "debug/entity_inspect.h"
#include "debug/play_controls.h"

namespace coney::debug {

namespace {

// What the page shows when no mode lists entities.
constexpr std::string_view kNoEntities = "no entities: --play-level, or a sandbox from the Levels page";
// The groups the inspector shows, in order; an entity has the ones that apply to it.
constexpr std::array<std::string_view, 10> kGroups{"Identity", "Transform", "Locomotion", "Animation", "State flags",
                                                   "Meters",   "AI",        "Physics",    "Combat",    "Object"};
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

// A group's page: one live watch per line the selection has in `group` when the page opens.
void fillGroup(MenuPage& page, DebugSession& session, std::string_view group) {
    const EntityInspect* inspect = inspectorOf(session);
    const std::vector<WatchGroup> groups =
        inspect != nullptr ? inspect->describe(session.debugDraw().inspected) : std::vector<WatchGroup>{};
    for (const WatchGroup& candidate : groups) {
        if (candidate.name != group) {
            continue;
        }
        for (const WatchLine& line : candidate.lines) {
            page.add(watchItem(line.name, [&session, group = std::string(group), name = line.name] {
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

// Freezes the selection or lets it go; says so in the log when nothing is selected or it cannot be frozen.
void freezeSelected(DebugSession& session, bool on) {
    PlayControls* play = session.play();
    const EntityId id = session.debugDraw().inspected;
    if (play == nullptr || id == kNoEntity) {
        session.print("inspector: nothing selected");
    } else if (!play->canFreezeEntity(id)) {
        session.print("inspector: an object cannot be frozen, only a human");
    } else {
        play->setEntityFrozen(id, on);
        session.print(std::format("inspector: entity {} {}", id, on ? "frozen" : "let go"));
    }
}

// The callbacks below copy strings and call through std::function; all they can throw is a failed allocation, which
// ends the program either way.
// NOLINTBEGIN(bugprone-exception-escape)

// The Entity inspector page's items.
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
                 "Freeze selection",
                 [&session] {
                     const PlayControls* play = session.play();
                     return play != nullptr && play->entityFrozen(session.debugDraw().inspected);
                 },
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
    // One submenu per group, filled from the selection when opened.
    for (const std::string_view group : kGroups) {
        page.add(submenuItem(std::string(group), [&session, group] {
            auto sub = std::make_shared<MenuPage>(std::string(group));
            sub->setRebuild([&session, group](MenuPage& p) { fillGroup(p, session, group); });
            return sub;
        }));
    }
    page.add(logItem("Log", [&session] { return session.log().last(3); }));
}

} // namespace

void addEntityInspectorPage(DebugSession& session) {
    session.model().addPage(
        "Entity inspector", [&session](MenuPage& page) { fillInspector(page, session); },
        "Select a human or object and watch everything Coney knows about it.");
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

// NOLINTEND(bugprone-exception-escape)

std::string inspectedCornerLine(const DebugSession& session) {
    if (!session.debugDraw().highlight || session.debugDraw().inspected == kNoEntity) {
        return {};
    }
    const std::string text = selectedText(session);
    return text == "-" ? std::string{} : "Inspecting: " + text;
}

} // namespace coney::debug
