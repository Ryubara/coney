// SPDX-License-Identifier: GPL-3.0-or-later
// The Entity inspector (docs/guides/debug-menu.md#entity-inspector): its selection rules, the watch groups it shows of
// a human and of the objects, and the page driven through the menu model over a fake play mode. No game, no disc.
#include <algorithm>
#include <cstddef>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "characters/anim_set.h"
#include "debug/debug_session.h"
#include "debug/entity_inspect.h"
#include "debug/human_watch.h"
#include "debug/menu_model.h"
#include "debug/object_watch.h"
#include "debug/play_controls.h"
#include "human/human.h"
#include "human/human_flags.h"
#include "support/collision_fixtures.h"
#include "support/human_fixtures.h"

using Catch::Approx;
using coney::anim::Vec3;
using coney::debug::DebugServices;
using coney::debug::DebugSession;
using coney::debug::EntityId;
using coney::debug::EntityKind;
using coney::debug::EntityRef;
using coney::debug::MenuItem;
using coney::debug::MenuPage;
using coney::debug::Place;
using coney::debug::TunableRegistry;
using coney::debug::WatchGroup;

namespace {

// Three entities: the player at the origin, a human 2 m ahead (+y) and an object 5 m to the right (+x).
std::vector<EntityRef> scene() {
    return {EntityRef{.id = 1, .kind = EntityKind::Player, .label = "player 1", .position = Vec3{0.0F, 0.0F, 0.0F}},
            EntityRef{.id = 7, .kind = EntityKind::Human, .label = "AI 1 gang", .position = Vec3{0.0F, 2.0F, 0.0F}},
            EntityRef{.id = 9, .kind = EntityKind::Object, .label = "door 1", .position = Vec3{5.0F, 0.0F, 0.0F}}};
}

// The scene's entities, each described by one group naming it.
class FakeInspect final : public coney::debug::EntityInspect {
  public:
    [[nodiscard]] std::vector<EntityRef> entities() const override { return list; }
    [[nodiscard]] std::vector<WatchGroup> describe(EntityId id) const override {
        const EntityRef* entity = coney::debug::findEntity(list, id);
        if (entity == nullptr) {
            return {};
        }
        return {WatchGroup{.name = "Identity",
                           .lines = {{.name = "Label", .value = entity->label}, {.name = "Asked", .value = "yes"}}},
                WatchGroup{.name = "Transform",
                           .lines = {{.name = "Feet", .value = coney::debug::vecText(entity->position)}}}};
    }
    std::vector<EntityRef> list = scene();
};

// A play mode with the fake inspector: the player at the origin, the camera behind him looking along +y.
class FakePlay final : public coney::debug::PlayControls {
  public:
    [[nodiscard]] std::string sceneName() const override { return "sandbox test"; }
    [[nodiscard]] Vec3 playerFeet() const override { return feet; }
    [[nodiscard]] float playerHeadingDegrees() const override { return heading; }
    [[nodiscard]] float playerSpeed() const override { return 0.0F; }
    [[nodiscard]] std::string playerState() const override { return "standing"; }
    void teleport(const Place& place) override {
        feet = place.feet;
        heading = place.headingDegrees;
    }
    [[nodiscard]] std::vector<Place> places() const override { return {}; }
    [[nodiscard]] bool playerFrozen() const override { return false; }
    void setPlayerFrozen(bool /*on*/) override {}
    [[nodiscard]] Vec3 cameraEye() const override { return {0.0F, -4.0F, 1.5F}; }
    [[nodiscard]] Vec3 cameraTarget() const override { return {0.0F, 0.0F, 1.2F}; }
    void resetCamera() override {}
    [[nodiscard]] bool freeCamera() const override { return false; }
    void setFreeCamera(bool /*on*/) override {}
    [[nodiscard]] bool canSpawn() const override { return false; }
    std::expected<void, coney::Error> spawn(const coney::sandbox::Primitive& /*primitive*/) override { return {}; }
    [[nodiscard]] std::size_t spawnedCount() const override { return 0; }
    std::expected<void, coney::Error> clearSpawned() override { return {}; }
    [[nodiscard]] const coney::debug::EntityInspect* inspect() const override {
        return withInspect ? &inspector : nullptr;
    }

    [[nodiscard]] bool canFreezeEntity(EntityId id) const override {
        const EntityRef* entity = coney::debug::findEntity(inspector.list, id);
        return entity != nullptr && entity->kind != EntityKind::Object;
    }
    [[nodiscard]] bool entityFrozen(EntityId id) const override {
        return std::ranges::find(frozenIds, id) != frozenIds.end();
    }
    void setEntityFrozen(EntityId id, bool on) override {
        std::erase(frozenIds, id);
        if (on && canFreezeEntity(id)) {
            frozenIds.push_back(id);
        }
    }
    std::vector<EntityId> frozenIds;
    FakeInspect inspector;
    bool withInspect = true;
    Vec3 feet{0.0F, 0.0F, 0.0F};
    float heading = 0.0F;
};

// The services of a session over `play` (null: no player).
DebugServices servicesOver(FakePlay* play) {
    DebugServices services;
    services.play = [play]() -> coney::debug::PlayControls* { return play; };
    return services;
}

// The item labelled `label` on `page`, which must exist.
const MenuItem& itemOn(const MenuPage& page, std::string_view label) {
    const MenuItem* item = page.find(label);
    INFO(label);
    REQUIRE(item != nullptr);
    return *item;
}

// Runs the action `label` on `page`.
void run(const MenuPage& page, std::string_view label) {
    const MenuItem& item = itemOn(page, label);
    REQUIRE(item.run);
    item.run();
}

// The watch `label`'s value on `page`.
std::string watch(const MenuPage& page, std::string_view label) {
    const MenuItem& item = itemOn(page, label);
    REQUIRE(item.watch);
    return item.watch();
}

// The synthetic character and its anim set, held together for a test.
struct TestCharacter {
    coney::characters::CharacterData data = coney::test::locomotionData();
    coney::characters::AnimSet anims{data, nullptr};
};

} // namespace

TEST_CASE("the inspector picks the nearest, the next and the looked-at entity", "[debug]") {
    const std::vector<EntityRef> entities = scene();
    // Nearest, the player himself left out.
    CHECK(coney::debug::nearestEntity(entities, Vec3{}, 1) == 7);
    CHECK(coney::debug::nearestEntity(entities, Vec3{4.0F, 0.0F, 0.0F}, 1) == 9);
    CHECK(coney::debug::nearestEntity(entities, Vec3{}) == 1);
    CHECK(coney::debug::nearestEntity({}, Vec3{}) == coney::debug::kNoEntity);
    // Next and previous wrap round; a selection not in the list starts at an end.
    CHECK(coney::debug::stepEntity(entities, 1, 1) == 7);
    CHECK(coney::debug::stepEntity(entities, 9, 1) == 1);
    CHECK(coney::debug::stepEntity(entities, 1, -1) == 9);
    CHECK(coney::debug::stepEntity(entities, 42, 1) == 1);
    CHECK(coney::debug::stepEntity(entities, 42, -1) == 9);
    CHECK(coney::debug::stepEntity({}, 1, 1) == coney::debug::kNoEntity);
    // Looked at: within the angle of the view, the most direct first.
    const Vec3 eye{0.0F, -4.0F, 1.0F};
    CHECK(coney::debug::lookedAtEntity(entities, eye, Vec3{0.0F, 0.0F, 1.0F}, 12.0F, 1) == 7);
    CHECK(coney::debug::lookedAtEntity(entities, eye, Vec3{5.0F, 0.0F, 1.0F}, 12.0F, 1) == 9);
    CHECK(coney::debug::lookedAtEntity(entities, eye, Vec3{-5.0F, 0.0F, 1.0F}, 12.0F, 1) == coney::debug::kNoEntity);
    CHECK(coney::debug::lookedAtEntity(entities, eye, eye, 12.0F) == coney::debug::kNoEntity);
}

TEST_CASE("watch groups flatten to lines and find a value by group and name", "[debug]") {
    const std::vector<WatchGroup> groups{
        WatchGroup{.name = "Identity", .lines = {{.name = "Role", .value = "player 1"}}},
        WatchGroup{.name = "Meters",
                   .lines = {{.name = "Health", .value = "900/900"}, {.name = "Rage", .value = "0"}}}};
    CHECK(coney::debug::flatten(groups) ==
          std::vector<std::string>{"Identity / Role: player 1", "Meters / Health: 900/900", "Meters / Rage: 0"});
    CHECK(coney::debug::watchValue(groups, "Meters", "Health") == std::optional<std::string>("900/900"));
    CHECK_FALSE(coney::debug::watchValue(groups, "Identity", "Health").has_value());
    CHECK(coney::debug::vecText(Vec3{1.0F, -2.5F, 0.25F}) == "(1.00, -2.50, 0.25)");
}

TEST_CASE("a human is described in every group from the engine's own values", "[debug][human]") {
    const TestCharacter character;
    const auto mesh = coney::test::makeMesh(coney::test::floorAt(0.0F, 0.0F, 80.0F, 0.0F, 80.0F));
    coney::human::Human human(character.anims, coney::human::AnimSlots::player(), coney::test::identityBind());
    human.spawn(mesh.get(), Vec3{40.0F, 40.0F, 0.0F}, 0.0F);
    const coney::debug::HumanFacts facts{
        .id = 1, .role = "player 1", .model = "rembrandt", .type = 32, .brain = nullptr, .frozen = true};
    const std::vector<WatchGroup> groups = coney::debug::describeHuman(human, facts);
    std::vector<std::string> names;
    for (const WatchGroup& group : groups) {
        names.push_back(group.name);
        CHECK_FALSE(group.lines.empty());
    }
    // No brain, no AI group.
    CHECK(names == std::vector<std::string>{"Identity", "Transform", "Locomotion", "Animation", "State flags", "Meters",
                                            "Physics", "Combat"});
    CHECK(coney::debug::watchValue(groups, "Identity", "Model") == std::optional<std::string>("rembrandt"));
    CHECK(coney::debug::watchValue(groups, "Identity", "Character type") == std::optional<std::string>("32"));
    CHECK(coney::debug::watchValue(groups, "Transform", "Feet").value_or("").starts_with("(40.00, 40.00, "));
    CHECK(coney::debug::watchValue(groups, "Locomotion", "Frozen") == std::optional<std::string>("yes"));
    CHECK(coney::debug::watchValue(groups, "Locomotion", "Support") == std::optional<std::string>("grounded"));
    CHECK(coney::debug::watchValue(groups, "Physics", "Walking sphere") ==
          std::optional<std::string>(std::format("r {:.4f} m, centre {:.4f} m up", human.walkingSphereRadius(),
                                                 human.walkingSphereCentreHeight())));
    CHECK(coney::debug::watchValue(groups, "Physics", "Contacts") == std::optional<std::string>("0"));
}

TEST_CASE("the human flags are named, and a bit the code does not name is shown as hex", "[debug][human]") {
    namespace f = coney::human::flag;
    CHECK(coney::debug::humanFlagNames(0) == "-");
    CHECK(coney::debug::humanFlagNames(f::kGod | f::kRevivable) == "Revivable God");
    std::uint64_t named = 0;
    for (const std::uint64_t bit : {f::kFastClimber, f::kRevivable, f::kWoundable, f::kGod, f::kKiller, f::kUngrabbable,
                                    f::kAutoCombat, f::kNoTarget, f::kWheelchair}) {
        named |= bit;
    }
    // The highest bit no flag uses.
    const std::uint64_t spare = 1ULL << 63U;
    CHECK(coney::debug::humanFlagNames(spare).starts_with("0x"));
    CHECK(coney::debug::humanFlagNames(named).find("0x") == std::string::npos);
}

TEST_CASE("doors, panes and world objects are described in Identity, Transform and Object", "[debug]") {
    coney::world_objects::Door door;
    door.handle = 12;
    door.type = "dyn_door_swinging";
    door.doorClass = coney::world_objects::DoorClass::Swinging;
    door.state = coney::world_objects::door_state::kOpen;
    door.hitpoints = 3;
    door.maxHitpoints = 5;
    const std::vector<WatchGroup> doorGroups = coney::debug::describeDoor(door, 0x30000);
    CHECK(coney::debug::watchValue(doorGroups, "Identity", "Kind") == std::optional<std::string>("door"));
    CHECK(coney::debug::watchValue(doorGroups, "Object", "State") == std::optional<std::string>("open (5)"));
    CHECK(coney::debug::watchValue(doorGroups, "Object", "Hitpoints") == std::optional<std::string>("3/5"));
    coney::world_objects::GlassPane pane;
    pane.width = 2.0F;
    pane.height = 1.5F;
    pane.broken = true;
    const std::vector<WatchGroup> paneGroups = coney::debug::describePane(pane, 0x40000);
    CHECK(coney::debug::watchValue(paneGroups, "Object", "Size") == std::optional<std::string>("2.00 x 1.50 m"));
    CHECK(coney::debug::watchValue(paneGroups, "Object", "Broken") == std::optional<std::string>("yes"));
    coney::world_objects::SpawnRecord record;
    record.handle = 40;
    record.typeName = "dyn_crate";
    record.position = {1.0F, 2.0F, 3.0F};
    record.live = true;
    record.owned = true;
    const std::vector<WatchGroup> recordGroups = coney::debug::describeSpawnRecord(record, 0x100000028ULL);
    CHECK(coney::debug::watchValue(recordGroups, "Identity", "Type") == std::optional<std::string>("dyn_crate"));
    CHECK(coney::debug::watchValue(recordGroups, "Transform", "Position") ==
          std::optional<std::string>("(1.00, 2.00, 3.00)"));
    CHECK(coney::debug::watchValue(recordGroups, "Object", "State") == std::optional<std::string>("live owned"));
}

TEST_CASE("the Entity inspector page says so without a mode that lists entities", "[debug]") {
    TunableRegistry tunables;
    DebugSession none(tunables, servicesOver(nullptr), nullptr);
    const auto page = none.model().openPage("Entity inspector");
    REQUIRE(page != nullptr);
    REQUIRE(page->items().size() == 1);
    CHECK(page->items().front().label == "No entities");
    FakePlay play;
    play.withInspect = false;
    DebugSession without(tunables, servicesOver(&play), nullptr);
    CHECK(without.model().openPage("Entity inspector")->items().front().label == "No entities");
}

TEST_CASE("the Entity inspector page selects, names, watches and teleports to an entity", "[debug]") {
    TunableRegistry tunables;
    FakePlay play;
    DebugSession session(tunables, servicesOver(&play), nullptr);
    const auto page = session.model().openPage("Entity inspector");
    REQUIRE(page != nullptr);
    CHECK(watch(*page, "Selected") == "-");
    CHECK(watch(*page, "Entities") == "2 humans, 1 objects");
    // Nearest leaves the player out.
    run(*page, "Nearest to player");
    CHECK(session.debugDraw().inspected == 7);
    CHECK(watch(*page, "Selected") == "AI 1 gang (2.0 m)");
    CHECK(session.cornerLines().back() == "Inspecting: AI 1 gang (2.0 m)");
    run(*page, "Next");
    CHECK(session.debugDraw().inspected == 9);
    run(*page, "Previous");
    run(*page, "Previous");
    CHECK(session.debugDraw().inspected == 1);
    // The camera looks along +y from behind the player: the human ahead.
    run(*page, "Looked at");
    CHECK(session.debugDraw().inspected == 7);
    // A group's page shows the selection's lines live.
    const auto identity = itemOn(*page, "Identity").open();
    REQUIRE(identity != nullptr);
    CHECK(watch(*identity, "Label") == "AI 1 gang");
    play.inspector.list[1].label = "AI 1 renamed";
    CHECK(watch(*identity, "Label") == "AI 1 renamed");
    // A group the selection does not have.
    const auto meters = itemOn(*page, "Meters").open();
    REQUIRE(meters != nullptr);
    CHECK(meters->items().front().label == "Nothing");
    // The list chooses any.
    const auto choose = itemOn(*page, "Choose").open();
    REQUIRE(choose != nullptr);
    REQUIRE(choose->items().size() == 3U);
    REQUIRE(choose->items().back().run);
    choose->items().back().run();
    CHECK(session.debugDraw().inspected == 9);
    // Teleport: 1.5 m from it on the player's side, facing it.
    run(*page, "Teleport player to it");
    CHECK(play.feet.x == Approx(3.5F));
    CHECK(play.feet.y == Approx(0.0F).margin(1e-5));
    CHECK(play.heading == Approx(-90.0F));
    // Highlight off hides the corner line; Clear drops the selection.
    const MenuItem& highlight = itemOn(*page, "Highlight");
    REQUIRE(highlight.setBool);
    highlight.setBool(false);
    CHECK_FALSE(session.debugDraw().highlight);
    for (const std::string& line : session.cornerLines()) {
        CHECK_FALSE(line.starts_with("Inspecting"));
    }
    run(*page, "Clear");
    CHECK(session.debugDraw().inspected == coney::debug::kNoEntity);
}

TEST_CASE("the Debug draw page has the collision-shape switches", "[debug]") {
    TunableRegistry tunables;
    DebugSession session(tunables, DebugServices{}, nullptr);
    const auto page = session.model().openPage("Debug draw");
    REQUIRE(page != nullptr);
    for (const std::string_view label :
         {"Wall classes", "Shapes", "Shapes on all humans", "Strike shapes", "Contacts"}) {
        const MenuItem& item = itemOn(*page, label);
        REQUIRE(item.setBool);
        item.setBool(true);
    }
    const coney::debug::DebugDrawOptions& draw = session.debugDraw();
    CHECK(draw.wallClasses);
    CHECK(draw.shapes);
    CHECK(draw.shapesAllHumans);
    CHECK(draw.strikes);
    CHECK(draw.contacts);
}

TEST_CASE("the Entity inspector page freezes and unfreezes the selected human, and refuses an object", "[debug]") {
    TunableRegistry tunables;
    FakePlay play;
    DebugSession session(tunables, servicesOver(&play), nullptr);
    const auto page = session.model().openPage("Entity inspector");
    REQUIRE(page != nullptr);
    const MenuItem& freeze = itemOn(*page, "Freeze selection");
    REQUIRE(freeze.getBool);
    REQUIRE(freeze.setBool);
    // Nothing selected: nothing to freeze, and it reads off.
    CHECK_FALSE(freeze.getBool());
    freeze.setBool(true);
    CHECK(play.frozenIds.empty());
    // A human other than the player freezes, shows it, and lets go.
    session.debugDraw().inspected = 7;
    freeze.setBool(true);
    CHECK(freeze.getBool());
    CHECK(play.frozenIds == std::vector<EntityId>{7});
    freeze.setBool(false);
    CHECK_FALSE(freeze.getBool());
    CHECK(play.frozenIds.empty());
    // An object cannot be frozen: it stays off, and the log says so.
    session.debugDraw().inspected = 9;
    freeze.setBool(true);
    CHECK_FALSE(freeze.getBool());
    CHECK(play.frozenIds.empty());
    CHECK(session.log().last(1).back().find("cannot be frozen") != std::string::npos);
}
