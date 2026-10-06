// SPDX-License-Identifier: GPL-3.0-or-later
// The pages over a play mode (Player, Camera, Spawner, Debug draw) and the Levels page's levels and sandboxes, driven
// through the menu model over a fake PlayControls: no game, no disc.
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "debug/debug_session.h"
#include "debug/menu_model.h"
#include "debug/play_controls.h"

using coney::debug::DebugServices;
using coney::debug::DebugSession;
using coney::debug::MenuItem;
using coney::debug::MenuPage;
using coney::debug::Place;
using coney::debug::TunableRegistry;

namespace {

// A play mode that only keeps what the pages asked of it.
class FakePlay final : public coney::debug::PlayControls {
  public:
    [[nodiscard]] std::string sceneName() const override { return "sandbox test"; }
    [[nodiscard]] coney::anim::Vec3 playerFeet() const override { return feet; }
    [[nodiscard]] float playerHeadingDegrees() const override { return heading; }
    [[nodiscard]] float playerSpeed() const override { return 2.5F; }
    [[nodiscard]] std::string playerState() const override { return "walk, clip 3, grounded"; }
    void teleport(const Place& place) override {
        feet = place.feet;
        heading = place.headingDegrees;
        ++teleports;
    }
    [[nodiscard]] std::vector<Place> places() const override {
        return {Place{"start", {1.0F, 2.0F, 0.0F}, 90.0F}, Place{"lane", {10.0F, 0.0F, 0.0F}, 0.0F}};
    }
    [[nodiscard]] bool playerFrozen() const override { return frozen; }
    void setPlayerFrozen(bool on) override { frozen = on; }
    [[nodiscard]] coney::anim::Vec3 cameraEye() const override { return {0.0F, -3.0F, 1.5F}; }
    [[nodiscard]] coney::anim::Vec3 cameraTarget() const override { return {0.0F, 0.0F, 1.5F}; }
    void resetCamera() override { ++resets; }
    [[nodiscard]] bool freeCamera() const override { return free; }
    void setFreeCamera(bool on) override { free = on; }
    [[nodiscard]] bool canSpawn() const override { return spawnable; }
    std::expected<void, coney::Error> spawn(const coney::sandbox::Primitive& primitive) override {
        spawned.push_back(primitive);
        return {};
    }
    [[nodiscard]] std::size_t spawnedCount() const override { return spawned.size(); }
    std::expected<void, coney::Error> clearSpawned() override {
        spawned.clear();
        return {};
    }
    [[nodiscard]] std::vector<coney::debug::CharacterChoice> characterChoices() const override { return choices; }
    [[nodiscard]] int playerType() const override { return type; }
    [[nodiscard]] std::string characterState() const override { return "type " + std::to_string(type); }
    // Becomes `wanted`, except type 99, which fails as a type without a model would.
    std::expected<void, coney::Error> changeCharacter(int wanted) override {
        if (wanted == 99) {
            return std::unexpected(coney::Error{coney::ErrorCode::NotFound, "no model"});
        }
        type = wanted;
        ++changes;
        return {};
    }

    coney::anim::Vec3 feet{0.0F, 0.0F, 0.0F};
    float heading = 0.0F;
    int teleports = 0;
    int resets = 0;
    bool frozen = false;
    bool free = false;
    bool spawnable = true;
    std::vector<coney::sandbox::Primitive> spawned;
    std::vector<coney::debug::CharacterChoice> choices;
    int type = 32;
    int changes = 0;
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

} // namespace

TEST_CASE("the play pages say so when no player plays", "[debug]") {
    TunableRegistry tunables;
    DebugSession session(tunables, servicesOver(nullptr), nullptr);
    for (const std::string_view title : {"Player", "Camera", "Spawner", "AI fighters"}) {
        INFO(title);
        const auto page = session.model().openPage(title);
        REQUIRE(page != nullptr);
        REQUIRE(page->items().size() == 1);
        CHECK(page->items().front().label == "No player");
    }
    // The pages come after Levels, and Debug draw needs no player.
    const auto titles = session.model().pageTitles();
    CHECK(titles == std::vector<std::string>{"Time", "Tunables", "Natives", "Lua console", "Cheats", "Levels", "Player",
                                             "Camera", "Spawner", "AI fighters", "Debug draw", "Display", "Audio",
                                             "Input"});
    CHECK(session.model().openPage("Debug draw")->items().size() == 6);
}

TEST_CASE("the Player page shows the player, freezes it and teleports it", "[debug]") {
    TunableRegistry tunables;
    FakePlay play;
    play.feet = {3.0F, 4.0F, 0.5F};
    DebugSession session(tunables, servicesOver(&play), nullptr);
    const auto page = session.model().openPage("Player");
    REQUIRE(page != nullptr);
    CHECK(itemOn(*page, "Scene").watch() == "sandbox test");
    CHECK(itemOn(*page, "Feet").watch() == "(3.00, 4.00, 0.50)");
    CHECK(itemOn(*page, "Speed").watch() == "2.50 m/s");
    CHECK(itemOn(*page, "Movement").watch() == "walk, clip 3, grounded");
    itemOn(*page, "Frozen").setBool(true);
    CHECK(play.frozen);

    // The scene's places, then a typed spot starting where the player stands.
    const auto teleport = itemOn(*page, "Teleport").open();
    REQUIRE(teleport != nullptr);
    itemOn(*teleport, "lane").run();
    CHECK(play.feet.x == 10.0F);
    itemOn(*teleport, "X").setNumber(-7.0);
    itemOn(*teleport, "Heading").setNumber(45.0);
    itemOn(*teleport, "Go to X Y Z").run();
    CHECK(play.feet.x == -7.0F);
    CHECK(play.feet.y == 4.0F);
    CHECK(play.heading == 45.0F);

    // Save, move away, come back.
    itemOn(*page, "Save this spot").run();
    itemOn(*teleport, "start").run();
    CHECK(play.feet.x == 1.0F);
    itemOn(*page, "Back to saved spot").run();
    CHECK(play.feet.x == -7.0F);
    CHECK(play.teleports == 4);

    // The speed is a channel, sampled once a step.
    session.model().sampleChannels();
    const coney::debug::TimeSeries* speed = session.model().channel("Player/Speed");
    REQUIRE(speed != nullptr);
    CHECK(speed->latest() == 2.5F);
}

TEST_CASE("the Camera page resets the camera, switches the free camera and shows the follow values", "[debug]") {
    TunableRegistry tunables;
    float lag = 0.22F;
    tunables.add("Follow camera", "Position lag", &lag).range(0, 1, 0.01);
    FakePlay play;
    DebugSession session(tunables, servicesOver(&play), nullptr);
    const auto page = session.model().openPage("Camera");
    REQUIRE(page != nullptr);
    CHECK(itemOn(*page, "Distance").watch() == "3.00 m");
    itemOn(*page, "Reset behind player").run();
    CHECK(play.resets == 1);
    itemOn(*page, "Free camera").setBool(true);
    CHECK(play.free);
    const auto values = itemOn(*page, "Follow camera values").open();
    REQUIRE(values != nullptr);
    CHECK(values->find("Position lag") != nullptr);
}

TEST_CASE("the Spawner page puts objects in front of the player and clears them", "[debug]") {
    TunableRegistry tunables;
    FakePlay play;
    play.feet = {0.0F, 0.0F, 1.0F};
    play.heading = 90.0F; // facing -x
    DebugSession session(tunables, servicesOver(&play), nullptr);
    const auto page = session.model().openPage("Spawner");
    REQUIRE(page != nullptr);
    itemOn(*page, "Distance ahead").setNumber(4.0);
    for (const coney::debug::Spawnable& spawnable : coney::debug::spawnables()) {
        itemOn(*page, spawnable.name).run();
    }
    REQUIRE(play.spawned.size() == coney::debug::spawnables().size());
    const coney::sandbox::Primitive& crate = play.spawned.front();
    CHECK(crate.base.x == Catch::Approx(-4.0F).margin(1e-5));
    CHECK(crate.base.y == Catch::Approx(0.0F).margin(1e-5));
    CHECK(crate.base.z == 1.0F);
    CHECK(crate.yawDegrees == 90.0F);
    CHECK(itemOn(*page, "Spawned").watch() == std::to_string(play.spawned.size()));
    itemOn(*page, "Clear spawned").run();
    CHECK(play.spawned.empty());

    // A level cannot spawn yet: the page says so instead of offering objects.
    play.spawnable = false;
    const auto level = session.model().openPage("Spawner");
    REQUIRE(level->items().size() == 1);
    CHECK(level->items().front().label == "Spawning");
}

TEST_CASE("every spawnable object is a primitive a sandbox can build", "[debug][sandbox]") {
    for (const coney::debug::Spawnable& spawnable : coney::debug::spawnables()) {
        INFO(spawnable.name);
        const coney::sandbox::Primitive& p = spawnable.primitive;
        CHECK(p.size.z > 0.0F);
        CHECK(p.size.x > 0.0F);
        CHECK(p.size.y > 0.0F);
        CHECK(p.surface.solid);
    }
    const coney::anim::Vec3 ahead = coney::debug::spotAhead({1.0F, 1.0F, 2.0F}, 0.0F, 3.0F);
    CHECK(ahead.x == Catch::Approx(1.0F));
    CHECK(ahead.y == Catch::Approx(4.0F));
    CHECK(ahead.z == 2.0F);
}

TEST_CASE("the Debug draw page switches the session's debug lines", "[debug]") {
    TunableRegistry tunables;
    DebugSession session(tunables, DebugServices{}, nullptr);
    const auto page = session.model().openPage("Debug draw");
    REQUIRE(page != nullptr);
    itemOn(*page, "Collision").setBool(true);
    itemOn(*page, "Collision radius").setNumber(20.0);
    itemOn(*page, "Places").setBool(true);
    CHECK(session.debugDraw().collision);
    CHECK(session.debugDraw().collisionRadius == 20.0F);
    CHECK(session.debugDraw().places);
    CHECK_FALSE(session.debugDraw().player);
}

TEST_CASE("the Levels page lists the sandbox layouts and asks for the one chosen", "[debug][sandbox]") {
    TunableRegistry tunables;
    DebugServices services;
    services.sandboxFolder = std::filesystem::path(CONEY_ASSETS_DIR) / "sandbox";
    std::vector<std::string> asked;
    services.loadSandbox = [&asked](std::string_view name) {
        asked.emplace_back(name);
        return true;
    };
    DebugSession session(tunables, services, nullptr);
    const auto levels = session.model().openPage("Levels");
    REQUIRE(levels != nullptr);
    const auto layouts = itemOn(*levels, "Sandbox layouts").open();
    REQUIRE(layouts != nullptr);
    REQUIRE_FALSE(layouts->items().empty());
    const MenuItem& first = layouts->items().front();
    first.run();
    CHECK(asked == std::vector<std::string>{first.label});
    CHECK(layouts->find("parkour") != nullptr);
}

TEST_CASE("the Levels page lists the play mode's levels and asks for the one chosen", "[debug]") {
    TunableRegistry tunables;
    DebugServices services;
    services.playableLevels = [] { return std::vector<std::string>{"level2", "level99"}; };
    std::vector<std::string> asked;
    services.loadLevel = [&asked](std::string_view name) {
        asked.emplace_back(name);
        return true;
    };
    DebugSession session(tunables, services, nullptr);
    const auto levels = session.model().openPage("Levels");
    REQUIRE(levels != nullptr);
    CHECK(levels->find("level2") != nullptr);
    itemOn(*levels, "level99").run();
    CHECK(asked == std::vector<std::string>{"level99"});
}

TEST_CASE("the Player page changes the player's character type through a choice and an action", "[debug]") {
    TunableRegistry tunables;
    FakePlay play;
    DebugSession session(tunables, servicesOver(&play), nullptr);
    // Without the configuration's types there is nothing to choose.
    auto page = session.model().openPage("Player");
    CHECK(itemOn(*page, "Character").watch() == "type 32");
    CHECK(itemOn(*page, "Character type").kind == coney::debug::ItemKind::Watch);
    CHECK(page->find("Change character") == nullptr);

    play.choices = {{30, "warr_re"}, {32, "warr_re_cv"}, {40, "warr_ty_cv"}, {99, "broken"}};
    page = session.model().openPage("Player");
    const MenuItem& type = itemOn(*page, "Character type");
    REQUIRE(type.kind == coney::debug::ItemKind::Choice);
    CHECK(type.choices.size() == 4);
    // It starts at the player's own type.
    CHECK(coney::debug::valueText(type) == "32 warr_re_cv");
    coney::debug::adjustItem(type, 1, coney::debug::StepSize::Normal);
    CHECK(coney::debug::valueText(type) == "40 warr_ty_cv");
    itemOn(*page, "Change character").run();
    CHECK(play.type == 40);
    CHECK(play.changes == 1);
    CHECK(session.log().last(1).back() == "player: now type 40");
    // The pick is kept across builds of the page; a failure leaves the player as he was and says why.
    page = session.model().openPage("Player");
    CHECK(coney::debug::valueText(itemOn(*page, "Character type")) == "40 warr_ty_cv");
    coney::debug::adjustItem(itemOn(*page, "Character type"), 1, coney::debug::StepSize::Normal);
    itemOn(*page, "Change character").run();
    CHECK(play.type == 40);
    CHECK(session.log().last(1).back() == "player: type 99: no model");
}
