// SPDX-License-Identifier: GPL-3.0-or-later
// The Entity editor page (docs/guides/debug-menu.md#entity-editor): edits the Entity inspector's selection. Every item
// queues an edit (src/debug/entity_edit.h) that the input gate applies between two steps and writes to the run log.
// Coney's own debug tooling: nothing here changes how the game plays until an edit is made.
#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <memory>
#include <optional>
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

// What the page shows when the mode lists no entities or cannot edit them.
constexpr std::string_view kNoEntities = "no editable entities: --play-level, or a sandbox from the Levels page";
// How many journal lines the page's Log shows.
constexpr std::size_t kLogLines = 4;

// The mode's edit target, or null.
EntityEditTarget* targetOf(const DebugSession& session) {
    PlayControls* play = session.play();
    return play != nullptr ? play->entityEdits() : nullptr;
}

// The entities now; empty without an inspector.
std::vector<EntityRef> entitiesOf(const DebugSession& session) {
    const PlayControls* play = session.play();
    const EntityInspect* inspect = play != nullptr ? play->inspect() : nullptr;
    return inspect != nullptr ? inspect->entities() : std::vector<EntityRef>{};
}

// A value to the millimetre: a position read off a float prints as `1.2`, not `1.2000000476837158`, in the run log.
double toMillimetres(double value) { return std::round(value * 1000.0) / 1000.0; }

// Queues `command` for the selection; says so in the log when nothing is selected. Returns whether it was queued.
bool queueForSelection(DebugSession& session, EditOp op, std::array<double, 3> values = {}) {
    const EntityId id = session.debugDraw().inspected;
    if (id == kNoEntity) {
        session.print("entity editor: nothing selected");
        return false;
    }
    return session.entityEditor().queue(EntityCommand{.entity = id, .op = op, .values = values});
}

// The first number of `op` on the selection now: the queued one if an edit waits, else the entity's own; `fallback`
// when neither is known.
double valueNow(const DebugSession& session, EditOp op, double fallback) {
    const EntityId id = session.debugDraw().inspected;
    if (const auto queued = session.entityEditor().queuedValue(id, op)) {
        return *queued;
    }
    const EntityEditTarget* target = targetOf(session);
    const std::optional<double> current = target != nullptr ? target->currentValue(id, op) : std::nullopt;
    return current.value_or(fallback);
}

// The selection's feet now: the queued teleport's if one waits, else where the inspector lists it.
anim::Vec3 feetNow(const DebugSession& session) {
    const EntityId id = session.debugDraw().inspected;
    if (const auto queued = session.entityEditor().queuedCommand(id, EditOp::Teleport)) {
        return anim::Vec3{static_cast<float>(queued->values[0]), static_cast<float>(queued->values[1]),
                          static_cast<float>(queued->values[2])};
    }
    const std::vector<EntityRef> entities = entitiesOf(session);
    const EntityRef* entity = findEntity(entities, id);
    return entity != nullptr ? entity->position : anim::Vec3{};
}

// A number item over `op` of the selection: the value the selection has or will have, a queued edit on a change, a
// reset to the value it is configured with.
MenuItem editNumber(DebugSession& session, std::string label, EditOp op, double min, double max, double step,
                    std::string units, std::string help) {
    MenuItem item = numberItem(
        std::move(label), [&session, op, min] { return valueNow(session, op, min); },
        [&session, op](double value) {
            // To four decimals: a step taken in float prints as `1.47`, not `1.4700000286102295`, in the run log.
            queueForSelection(session, op, {std::round(value * 10000.0) / 10000.0, 0.0, 0.0});
        },
        min, max, step, false);
    item.units = std::move(units);
    item.help = std::move(help);
    const EntityEditTarget* target = targetOf(session);
    const auto configured = target != nullptr ? target->configuredValues(session.debugDraw().inspected, op)
                                              : std::optional<std::array<double, 3>>{};
    if (configured) {
        item.defaultValue = (*configured)[0];
    }
    return item;
}

// A toggle item over `op` of the selection.
MenuItem editToggle(DebugSession& session, std::string label, EditOp op, std::string help) {
    return toggleItem(
               std::move(label), [&session, op] { return valueNow(session, op, 0.0) != 0.0; },
               [&session, op](bool on) { queueForSelection(session, op, {on ? 1.0 : 0.0, 0.0, 0.0}); })
        .withHelp(std::move(help));
}

// A coordinate item: changing it queues a teleport with that axis changed and the other two as they are or will be.
MenuItem editAxis(DebugSession& session, std::string label, std::size_t axis) {
    MenuItem item = numberItem(
        std::move(label),
        [&session, axis] {
            const anim::Vec3 feet = feetNow(session);
            return static_cast<double>(axis == 0 ? feet.x : (axis == 1 ? feet.y : feet.z));
        },
        [&session, axis](double value) {
            const anim::Vec3 feet = feetNow(session);
            std::array<double, 3> at{feet.x, feet.y, feet.z};
            at.at(axis) = value;
            queueForSelection(session, EditOp::Teleport,
                              {toMillimetres(at[0]), toMillimetres(at[1]), toMillimetres(at[2])});
        },
        -10000.0, 10000.0, 0.5, false);
    item.units = "m";
    item.help = "Teleports the selection; finds the ground.";
    return item;
}

// The Transform group.
void fillTransform(MenuPage& page, DebugSession& session) {
    page.add(editAxis(session, "X", 0));
    page.add(editAxis(session, "Y", 1));
    page.add(editAxis(session, "Z", 2));
    page.add(editNumber(session, "Heading", EditOp::Heading, 0.0, 360.0, 5.0, "deg",
                        "0 faces +y, anticlockwise from above."));
    page.add(actionItem("Drop to ground", [&session] { queueForSelection(session, EditOp::DropToGround); }))
        .withHelp("Feet onto the ground below.");
    page.add(actionItem("Move to camera",
                        [&session] {
                            const PlayControls* play = session.play();
                            if (play == nullptr) {
                                return;
                            }
                            const anim::Vec3 look = play->cameraTarget();
                            queueForSelection(session, EditOp::Teleport,
                                              {toMillimetres(look.x), toMillimetres(look.y), toMillimetres(look.z)});
                        }))
        .withHelp("To the point the camera looks at.");
}

// The Meters group.
void fillMeters(MenuPage& page, DebugSession& session) {
    page.add(editNumber(session, "Health", EditOp::Health, 1.0, 100.0, 5.0, "%",
                        "Percent of the maximum; Kill is the way to 0."));
    page.add(editNumber(session, "Stamina", EditOp::Stamina, 0.0, 100.0, 5.0, "%", "Percent of the maximum."));
    page.add(editNumber(session, "Power", EditOp::Power, 0.0, 100.0, 5.0, "%", "Percent of the maximum."));
    page.add(editNumber(session, "Rage", EditOp::Rage, 0.0, 100.0, 5.0, "%", "Percent of the maximum."));
    page.add(editToggle(session, "Invincible", EditOp::Invincible, "God mode: hits do not hurt it."));
    page.add(actionItem("Kill", [&session] { queueForSelection(session, EditOp::Kill); }))
        .withHelp("Empties the health: a death.");
}

// The Stats group.
void fillStats(MenuPage& page, DebugSession& session) {
    page.add(editNumber(session, "Scale", EditOp::Scale, 0.05, 20.0, 0.05, "x",
                        "Body scale; the collision follows unless switched off."));
    page.add(editToggle(session, "Scale collision with mesh", EditOp::ScaleCollision,
                        "Off: the collision keeps its configured size."));
    page.add(editNumber(session, "Animation rate", EditOp::AnimRate, 0.0, 4.0, 0.05, "x",
                        "How fast every clip plays; 0 holds them."));
    page.add(editNumber(session, "Speed scale", EditOp::SpeedScale, 0.0, 10.0, 0.05, "x",
                        "Multiplies this human's gait speeds."));
}

// Queues the reset of every field of the selection that has a configured value.
void resetSelection(DebugSession& session) {
    const EntityId id = session.debugDraw().inspected;
    const EntityEditTarget* target = targetOf(session);
    if (id == kNoEntity || target == nullptr) {
        session.print("entity editor: nothing selected");
        return;
    }
    for (std::size_t i = 0; i < kEditOpCount; ++i) {
        session.entityEditor().queueReset(id, static_cast<EditOp>(i), *target);
    }
}

// One line on the selection: its label.
std::string selectedText(const DebugSession& session) {
    const std::vector<EntityRef> entities = entitiesOf(session);
    const EntityRef* entity = findEntity(entities, session.debugDraw().inspected);
    return entity != nullptr ? entity->label : std::string("-");
}

// A group's submenu, filled by `fill` from the selection when it opens.
MenuItem groupItem(DebugSession& session, std::string label, void (*fill)(MenuPage&, DebugSession&)) {
    return submenuItem(label, [&session, label, fill] {
        auto sub = std::make_shared<MenuPage>(label);
        sub->setRebuild([&session, fill](MenuPage& p) { fill(p, session); });
        return sub;
    });
}

// The callbacks below copy strings and call through std::function; all they can throw is a failed allocation, which
// ends the program either way.
// NOLINTBEGIN(bugprone-exception-escape)

// The Entity editor page's items.
void fillEditor(MenuPage& page, DebugSession& session) {
    PlayControls* play = session.play();
    if (play == nullptr || play->inspect() == nullptr || play->entityEdits() == nullptr) {
        page.add(watchItem("No entities", [] { return std::string(kNoEntities); }));
        return;
    }
    page.add(watchItem("Selected", [&session] { return selectedText(session); }))
        .withHelp("The Entity inspector's selection; pick one there.");
    page.add(watchItem("Queued", [&session] { return std::format("{} waiting", session.entityEditor().pending()); }))
        .withHelp("Edits that land between the next two steps.");
    page.add(groupItem(session, "Transform", fillTransform));
    page.add(groupItem(session, "Meters", fillMeters));
    page.add(groupItem(session, "Stats", fillStats));
    page.add(groupItem(session, "Control", [](MenuPage& p, DebugSession& s) {
        p.add(editToggle(s, "Frozen", EditOp::Frozen, "The inspector's freeze."));
    }));
    page.add(groupItem(session, "Lifecycle", [](MenuPage& p, DebugSession& s) {
        p.add(actionItem("Delete", [&s] {
             queueForSelection(s, EditOp::Delete);
         })).withHelp("The game's own removal; player 1 is refused.");
    }));
    page.add(actionItem("Reset selection", [&session] { resetSelection(session); }))
        .withHelp("Every field back to its configured value.");
    page.add(logItem("Log", [&session] {
        const std::vector<std::string>& journal = session.entityEditor().journal();
        const std::size_t from = journal.size() > kLogLines ? journal.size() - kLogLines : 0;
        return std::vector<std::string>(journal.begin() + static_cast<std::ptrdiff_t>(from), journal.end());
    }));
}

// NOLINTEND(bugprone-exception-escape)

} // namespace

void addEntityEditorPage(DebugSession& session) {
    session.model().addPage(
        "Entity editor", [&session](MenuPage& page) { fillEditor(page, session); },
        "Change the Entity inspector's selection: transform, meters, stats, delete. Edits land between steps.");
}

} // namespace coney::debug
