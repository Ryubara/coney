// SPDX-License-Identifier: GPL-3.0-or-later
// The Entity editor page (docs/guides/debug-menu.md#entity-editor) driven through the menu model over a fake play mode,
// as a front end drives it: an item queues an edit, the input gate applies the queue between two steps and writes the
// run-log line, a reset returns a value to the configured one, the page refuses without a selection, and a network
// client's edits are refused. No game, no disc.
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "debug/debug_session.h"
#include "debug/entity_edit.h"
#include "debug/entity_inspect.h"
#include "debug/menu_model.h"
#include "debug/net_controls.h"
#include "debug/play_controls.h"

using Catch::Approx;
using coney::anim::Vec3;
using coney::debug::DebugServices;
using coney::debug::DebugSession;
using coney::debug::EditOp;
using coney::debug::EntityCommand;
using coney::debug::EntityId;
using coney::debug::EntityKind;
using coney::debug::EntityRef;
using coney::debug::MenuItem;
using coney::debug::MenuPage;
using coney::debug::TunableRegistry;
using coney::debug::WatchGroup;

namespace {

// One fake entity: what the editor can change.
struct FakeState {
    Vec3 feet;
    double heading = 0.0;
    double health = 100.0;
    double stamina = 100.0;
    double power = 100.0;
    double rage = 0.0;
    bool invincible = false;
    double scale = 1.0;
    bool collisionFollows = true;
    double animRate = 1.0;
    double speedScale = 1.0;
    bool frozen = false;
    bool killed = false;
    bool deleted = false;
};

// The scene's three entities (player 1, a human, an object) and the target the editor edits them through.
class FakeWorld final : public coney::debug::EntityInspect, public coney::debug::EntityEditTarget {
  public:
    FakeWorld() {
        states[1] = FakeState{.feet = Vec3{0.0F, 0.0F, 0.0F}};
        states[7] = FakeState{.feet = Vec3{0.0F, 2.0F, 0.0F}, .scale = 1.25};
        list = {EntityRef{.id = 1, .kind = EntityKind::Player, .label = "player 1", .position = states[1].feet},
                EntityRef{.id = 7, .kind = EntityKind::Human, .label = "AI 1 gang", .position = states[7].feet},
                EntityRef{.id = 9, .kind = EntityKind::Object, .label = "door 1", .position = Vec3{5.0F, 0.0F, 0.0F}}};
    }
    [[nodiscard]] std::vector<EntityRef> entities() const override {
        std::vector<EntityRef> live;
        for (EntityRef entity : list) {
            const auto found = states.find(entity.id);
            if (found != states.end()) {
                if (found->second.deleted) {
                    continue;
                }
                entity.position = found->second.feet;
            }
            live.push_back(entity);
        }
        return live;
    }
    [[nodiscard]] std::vector<WatchGroup> describe(EntityId id) const override {
        const EntityRef* entity = coney::debug::findEntity(list, id);
        return entity == nullptr
                   ? std::vector<WatchGroup>{}
                   : std::vector<WatchGroup>{WatchGroup{.name = "Identity", .lines = {{"Label", entity->label}}}};
    }

    std::expected<void, coney::Error> applyEdit(const EntityCommand& command) override {
        const auto found = states.find(command.entity);
        if (found == states.end() || found->second.deleted) {
            return coney::fail(coney::ErrorCode::NotFound, "that entity is gone");
        }
        FakeState& state = found->second;
        const double value = command.values[0];
        switch (command.op) {
        case EditOp::Teleport:
            state.feet = Vec3{static_cast<float>(command.values[0]), static_cast<float>(command.values[1]),
                              static_cast<float>(command.values[2])};
            break;
        case EditOp::Heading:
            state.heading = value;
            break;
        case EditOp::DropToGround:
            state.feet.z = 0.0F;
            break;
        case EditOp::Health:
            state.health = value;
            break;
        case EditOp::Stamina:
            state.stamina = value;
            break;
        case EditOp::Power:
            state.power = value;
            break;
        case EditOp::Rage:
            state.rage = value;
            break;
        case EditOp::Invincible:
            state.invincible = value != 0.0;
            break;
        case EditOp::Kill:
            state.killed = true;
            break;
        case EditOp::Scale:
            state.scale = value;
            break;
        case EditOp::ScaleCollision:
            state.collisionFollows = value != 0.0;
            break;
        case EditOp::AnimRate:
            state.animRate = value;
            break;
        case EditOp::SpeedScale:
            state.speedScale = value;
            break;
        case EditOp::Frozen:
            state.frozen = value != 0.0;
            break;
        case EditOp::Delete:
            if (command.entity == 1) {
                return coney::fail(coney::ErrorCode::InvalidArgument, "player 1 cannot be deleted yet");
            }
            state.deleted = true;
            break;
        }
        return {};
    }
    [[nodiscard]] std::optional<std::array<double, 3>> configuredValues(EntityId entity, EditOp op) const override {
        const auto found = states.find(entity);
        if (found == states.end()) {
            return std::nullopt;
        }
        switch (op) {
        case EditOp::Health:
        case EditOp::Stamina:
        case EditOp::Power:
            return std::array<double, 3>{100.0, 0.0, 0.0};
        case EditOp::Rage:
        case EditOp::Invincible:
        case EditOp::Frozen:
            return std::array<double, 3>{0.0, 0.0, 0.0};
        case EditOp::Scale:
            return std::array<double, 3>{entity == 7 ? 1.25 : 1.0, 0.0, 0.0};
        case EditOp::ScaleCollision:
        case EditOp::AnimRate:
        case EditOp::SpeedScale:
            return std::array<double, 3>{1.0, 0.0, 0.0};
        default:
            return std::nullopt;
        }
    }
    [[nodiscard]] std::optional<double> currentValue(EntityId entity, EditOp op) const override {
        const auto found = states.find(entity);
        if (found == states.end()) {
            return std::nullopt;
        }
        const FakeState& state = found->second;
        switch (op) {
        case EditOp::Heading:
            return state.heading;
        case EditOp::Health:
            return state.health;
        case EditOp::Stamina:
            return state.stamina;
        case EditOp::Power:
            return state.power;
        case EditOp::Rage:
            return state.rage;
        case EditOp::Invincible:
            return state.invincible ? 1.0 : 0.0;
        case EditOp::Scale:
            return state.scale;
        case EditOp::ScaleCollision:
            return state.collisionFollows ? 1.0 : 0.0;
        case EditOp::AnimRate:
            return state.animRate;
        case EditOp::SpeedScale:
            return state.speedScale;
        case EditOp::Frozen:
            return state.frozen ? 1.0 : 0.0;
        default:
            return std::nullopt;
        }
    }

    std::vector<EntityRef> list;
    std::map<EntityId, FakeState> states;
};

// A play mode over the fake world; the camera looks at (0, 0, 1.2) from behind the player.
class FakePlay final : public coney::debug::PlayControls {
  public:
    [[nodiscard]] std::string sceneName() const override { return "sandbox test"; }
    [[nodiscard]] Vec3 playerFeet() const override { return Vec3{}; }
    [[nodiscard]] float playerHeadingDegrees() const override { return 0.0F; }
    [[nodiscard]] float playerSpeed() const override { return 0.0F; }
    [[nodiscard]] std::string playerState() const override { return "standing"; }
    void teleport(const coney::debug::Place& /*place*/) override {}
    [[nodiscard]] std::vector<coney::debug::Place> places() const override { return {}; }
    [[nodiscard]] bool playerFrozen() const override { return false; }
    void setPlayerFrozen(bool /*on*/) override {}
    [[nodiscard]] Vec3 cameraEye() const override { return {0.0F, -4.0F, 1.5F}; }
    [[nodiscard]] Vec3 cameraTarget() const override { return {3.0F, 4.0F, 1.2F}; }
    void resetCamera() override {}
    [[nodiscard]] bool freeCamera() const override { return false; }
    void setFreeCamera(bool /*on*/) override {}
    [[nodiscard]] bool canSpawn() const override { return false; }
    std::expected<void, coney::Error> spawn(const coney::sandbox::Primitive& /*primitive*/) override { return {}; }
    [[nodiscard]] std::size_t spawnedCount() const override { return 0; }
    std::expected<void, coney::Error> clearSpawned() override { return {}; }
    [[nodiscard]] const coney::debug::EntityInspect* inspect() const override { return &world; }
    [[nodiscard]] coney::debug::EntityEditTarget* entityEdits() override { return withEdits ? &world : nullptr; }

    FakeWorld world;
    bool withEdits = true;
};

// A network of one role.
class FakeNet final : public coney::debug::NetControls {
  public:
    [[nodiscard]] coney::debug::NetStatus status() const override {
        coney::debug::NetStatus status;
        status.role = role;
        return status;
    }
    [[nodiscard]] std::string playerName() const override { return "P"; }
    bool setPlayerName(const std::string& /*name*/) override { return true; }
    [[nodiscard]] std::uint16_t port() const override { return 1; }
    void setPort(std::uint16_t /*port*/) override {}
    [[nodiscard]] std::string address() const override { return "a"; }
    void setAddress(const std::string& /*address*/) override {}
    bool host() override { return true; }
    bool join() override { return true; }
    void disconnect() override {}
    [[nodiscard]] coney::debug::NetSimulation simulation() const override { return {}; }
    void setSimulation(const coney::debug::NetSimulation& /*value*/) override {}
    [[nodiscard]] std::vector<std::string> log() const override { return {}; }
    coney::debug::NetRole role = coney::debug::NetRole::Offline;
};

// A session over `play` and `net` (either may be null), its run-log lines kept in `lines`.
struct Rig {
    explicit Rig(FakePlay* play, FakeNet* net = nullptr) {
        DebugServices services;
        services.play = [play]() -> coney::debug::PlayControls* { return play; };
        if (net != nullptr) {
            services.net = [net]() -> coney::debug::NetControls* { return net; };
        }
        session = std::make_unique<DebugSession>(tunables, services, nullptr);
        session->entityEditor().setLog([this](std::string_view line) { lines.emplace_back(line); });
    }
    // The gate's between-steps work, as the game runs it once per frame.
    void step() { (void)session->gate().sample(frame++); }
    // The page `title` of the editor, opened as a front end opens it; submenus by their labels.
    std::shared_ptr<MenuPage> open(std::initializer_list<std::string_view> path) {
        std::shared_ptr<MenuPage> page = session->model().openPage("Entity editor");
        REQUIRE(page != nullptr);
        for (const std::string_view label : path) {
            const MenuItem* item = page->find(label);
            INFO(label);
            REQUIRE(item != nullptr);
            REQUIRE(item->open);
            page = item->open();
            REQUIRE(page != nullptr);
        }
        return page;
    }
    TunableRegistry tunables;
    std::unique_ptr<DebugSession> session;
    std::vector<std::string> lines;
    std::uint64_t frame = 0;
};

// The item `label` of `page`, which must exist.
const MenuItem& itemOn(const MenuPage& page, std::string_view label) {
    const MenuItem* item = page.find(label);
    INFO(label);
    REQUIRE(item != nullptr);
    return *item;
}

} // namespace

TEST_CASE("the Entity editor page says so without a play mode that lists and edits entities",
          "[debug][entity-editor]") {
    // Expected: unverified, #723
    Rig none(nullptr);
    const auto noPlay = none.session->model().openPage("Entity editor");
    REQUIRE(noPlay != nullptr);
    REQUIRE(noPlay->items().size() == 1);
    CHECK(noPlay->items().front().label == "No entities");

    FakePlay play;
    play.withEdits = false;
    Rig readOnly(&play);
    const auto page = readOnly.session->model().openPage("Entity editor");
    REQUIRE(page != nullptr);
    CHECK(page->items().front().label == "No entities");
}

TEST_CASE("an item queues the edit, which lands when the gate runs the between-steps work, and the run log says so",
          "[debug][entity-editor]") {
    // Expected: unverified, #723
    FakePlay play;
    Rig rig(&play);
    rig.session->debugDraw().inspected = 7;
    const auto meters = rig.open({"Meters"});

    const MenuItem& health = itemOn(*meters, "Health");
    REQUIRE(health.setNumber);
    CHECK(health.getNumber() == Approx(100.0));
    health.setNumber(40.0);
    // Queued: the item shows the value at once; the game's state has not moved.
    CHECK(health.getNumber() == Approx(40.0));
    CHECK(play.world.states[7].health == 100.0);
    CHECK(rig.session->entityEditor().pending() == 1);

    rig.step();
    CHECK(play.world.states[7].health == 40.0);
    CHECK(rig.session->entityEditor().pending() == 0);
    REQUIRE(rig.lines.size() == 1);
    CHECK(rig.lines[0] == "entity edit: applied 7 health 40");
    CHECK(health.getNumber() == Approx(40.0));
    // Another entity is untouched.
    CHECK(play.world.states[1].health == 100.0);
}

TEST_CASE("each group edits the selection: meters, stats, control and the transform", "[debug][entity-editor]") {
    // Expected: unverified, #723
    FakePlay play;
    Rig rig(&play);
    rig.session->debugDraw().inspected = 7;

    const auto meters = rig.open({"Meters"});
    itemOn(*meters, "Stamina").setNumber(25.0);
    itemOn(*meters, "Power").setNumber(50.0);
    itemOn(*meters, "Rage").setNumber(75.0);
    itemOn(*meters, "Invincible").setBool(true);
    itemOn(*meters, "Kill").run();

    const auto stats = rig.open({"Stats"});
    itemOn(*stats, "Scale").setNumber(3.0);
    itemOn(*stats, "Scale collision with mesh").setBool(false);
    itemOn(*stats, "Animation rate").setNumber(0.5);
    itemOn(*stats, "Speed scale").setNumber(2.0);

    const auto control = rig.open({"Control"});
    itemOn(*control, "Frozen").setBool(true);

    const auto transform = rig.open({"Transform"});
    itemOn(*transform, "Heading").setNumber(90.0);
    itemOn(*transform, "X").setNumber(4.0);
    itemOn(*transform, "Drop to ground").run();

    // Nothing moved before the step boundary.
    FakeState& state = play.world.states[7];
    CHECK(state.stamina == 100.0);
    CHECK_FALSE(state.killed);
    CHECK(state.scale == 1.25);
    CHECK(rig.session->entityEditor().pending() == 13);

    rig.step();
    CHECK(state.stamina == 25.0);
    CHECK(state.power == 50.0);
    CHECK(state.rage == 75.0);
    CHECK(state.invincible);
    CHECK(state.killed);
    CHECK(state.scale == 3.0);
    CHECK_FALSE(state.collisionFollows);
    CHECK(state.animRate == 0.5);
    CHECK(state.speedScale == 2.0);
    CHECK(state.frozen);
    CHECK(state.heading == 90.0);
    // The X edit kept the other two coordinates.
    CHECK(state.feet.x == Approx(4.0F));
    CHECK(state.feet.y == Approx(2.0F));
    CHECK(rig.lines.size() == 13);
}

TEST_CASE("a number item resets to the value the entity is configured with", "[debug][entity-editor]") {
    // Expected: unverified, #723
    FakePlay play;
    Rig rig(&play);
    rig.session->debugDraw().inspected = 7;
    const auto stats = rig.open({"Stats"});
    const MenuItem& scale = itemOn(*stats, "Scale");
    // Entity 7 is configured at 1.25, not 1.
    REQUIRE(scale.defaultValue.has_value());
    CHECK(*scale.defaultValue == Approx(1.25));
    scale.setNumber(6.0);
    rig.step();
    REQUIRE(play.world.states[7].scale == 6.0);

    CHECK(coney::debug::resetItem(scale));
    rig.step();
    CHECK(play.world.states[7].scale == 1.25);
    // The reset is an ordinary edit in the log.
    CHECK(rig.lines.back() == "entity edit: applied 7 scale 1.25");
}

TEST_CASE("Reset selection queues the configured value of every field that has one", "[debug][entity-editor]") {
    // Expected: unverified, #723
    FakePlay play;
    Rig rig(&play);
    rig.session->debugDraw().inspected = 7;
    FakeState& state = play.world.states[7];
    state.health = 3.0;
    state.stamina = 3.0;
    state.invincible = true;
    state.scale = 9.0;
    state.collisionFollows = false;
    state.animRate = 4.0;
    state.speedScale = 5.0;
    state.frozen = true;
    itemOn(*rig.open({}), "Reset selection").run();
    rig.step();
    CHECK(state.health == 100.0);
    CHECK(state.stamina == 100.0);
    CHECK_FALSE(state.invincible);
    CHECK(state.scale == 1.25);
    CHECK(state.collisionFollows);
    CHECK(state.animRate == 1.0);
    CHECK(state.speedScale == 1.0);
    CHECK_FALSE(state.frozen);
    // A position is not reset: there is no configured one.
    CHECK(state.feet.y == Approx(2.0F));
}

TEST_CASE("Move to camera queues a teleport to the point the camera looks at", "[debug][entity-editor]") {
    // Expected: unverified, #723
    FakePlay play;
    Rig rig(&play);
    rig.session->debugDraw().inspected = 7;
    itemOn(*rig.open({"Transform"}), "Move to camera").run();
    rig.step();
    CHECK(play.world.states[7].feet.x == Approx(3.0F));
    CHECK(play.world.states[7].feet.y == Approx(4.0F));
    CHECK(rig.lines.back() == "entity edit: applied 7 teleport 3 4 1.2");
}

TEST_CASE("delete goes through the target, which refuses player 1 with a reason", "[debug][entity-editor]") {
    // Expected: unverified, #723
    FakePlay play;
    Rig rig(&play);
    rig.session->debugDraw().inspected = 1;
    itemOn(*rig.open({"Lifecycle"}), "Delete").run();
    rig.step();
    CHECK_FALSE(play.world.states[1].deleted);
    REQUIRE(rig.lines.size() == 1);
    CHECK(rig.lines[0] == "entity edit: refused 1 delete: player 1 cannot be deleted yet");

    rig.session->debugDraw().inspected = 7;
    itemOn(*rig.open({"Lifecycle"}), "Delete").run();
    rig.step();
    CHECK(play.world.states[7].deleted);
    CHECK(rig.lines.back() == "entity edit: applied 7 delete");
    // The entity is gone from the scene.
    CHECK(coney::debug::findEntity(play.world.entities(), 7) == nullptr);
}

TEST_CASE("without a selection an edit queues nothing and the log says why", "[debug][entity-editor]") {
    // Expected: unverified, #723
    FakePlay play;
    Rig rig(&play);
    rig.session->debugDraw().inspected = coney::debug::kNoEntity;
    const auto meters = rig.open({"Meters"});
    itemOn(*meters, "Health").setNumber(10.0);
    itemOn(*meters, "Kill").run();
    CHECK(rig.session->entityEditor().pending() == 0);
    // The page's own header reads the same.
    CHECK(itemOn(*rig.open({}), "Selected").watch() == "-");
}

TEST_CASE("a network client's edits are refused, the host's land", "[debug][entity-editor]") {
    // Expected: unverified, #723 (docs/guides/network-architecture.md#checklist)
    FakePlay play;
    FakeNet net;
    Rig rig(&play, &net);
    rig.session->debugDraw().inspected = 7;
    const auto meters = rig.open({"Meters"});

    net.role = coney::debug::NetRole::Client;
    itemOn(*meters, "Health").setNumber(10.0);
    CHECK(rig.session->entityEditor().pending() == 0);
    rig.step();
    CHECK(play.world.states[7].health == 100.0);
    REQUIRE(rig.lines.size() == 1);
    CHECK(rig.lines[0] == "entity edit: refused 7 health 10: only the host edits in a network session");

    net.role = coney::debug::NetRole::Host;
    itemOn(*meters, "Health").setNumber(10.0);
    rig.step();
    CHECK(play.world.states[7].health == 10.0);
    // Offline plays alone, so edits land too.
    net.role = coney::debug::NetRole::Offline;
    itemOn(*meters, "Health").setNumber(20.0);
    rig.step();
    CHECK(play.world.states[7].health == 20.0);
}

TEST_CASE("the page's Log shows the last edits and the Queued line counts the waiting ones", "[debug][entity-editor]") {
    // Expected: unverified, #723
    FakePlay play;
    Rig rig(&play);
    rig.session->debugDraw().inspected = 7;
    const auto page = rig.open({});
    CHECK(itemOn(*page, "Queued").watch() == "0 waiting");
    itemOn(*rig.open({"Meters"}), "Health").setNumber(10.0);
    CHECK(itemOn(*page, "Queued").watch() == "1 waiting");
    rig.step();
    CHECK(itemOn(*page, "Queued").watch() == "0 waiting");
    const MenuItem& log = itemOn(*page, "Log");
    REQUIRE(log.lines);
    REQUIRE(log.lines().size() == 1);
    CHECK(log.lines()[0] == "entity edit: applied 7 health 10");
}
