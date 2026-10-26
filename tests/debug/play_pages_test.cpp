// SPDX-License-Identifier: GPL-3.0-or-later
// The pages over a play mode (Player, Camera, Spawner, HUD, Debug draw) and the Levels page's levels and sandboxes,
// driven through the menu model over a fake PlayControls: no game, no disc.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "ai/fighter_side.h"
#include "core/pad.h"
#include "core/pads.h"
#include "debug/debug_session.h"
#include "debug/fighter_spawn.h"
#include "debug/menu_model.h"
#include "debug/play_controls.h"
#include "hud/hud.h"
#include "human/climb.h"
#include "support/test_paths.h"

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
    [[nodiscard]] bool canNoclip() const override { return noclipHere; }
    [[nodiscard]] bool noclip() const override { return flying; }
    void setNoclip(bool on) override { flying = on; }
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

    // AI fighters: spawned ones are recorded with the model and side each was asked for ("" for the scene's own).
    [[nodiscard]] bool canSpawnFighter() const override { return true; }
    std::expected<void, coney::Error> spawnFighter(coney::anim::Vec3 at, float facing) override {
        return spawnFighterAs(at, facing, {}, coney::ai::FighterSide::Enemy);
    }
    [[nodiscard]] std::vector<std::string> fighterModels() const override { return models; }
    std::expected<void, coney::Error> spawnFighterAs(coney::anim::Vec3 at, float facing, std::string_view model,
                                                     coney::ai::FighterSide side) override {
        if (fighterLimit && fighters.size() >= *fighterLimit) {
            return std::unexpected(coney::Error{coney::ErrorCode::InvalidArgument, "no room"});
        }
        fighters.push_back(SpawnedFighter{at, facing, std::string(model), side});
        return {};
    }
    [[nodiscard]] std::size_t fighterCount() const override { return fighters.size(); }
    void clearFighters() override { fighters.clear(); }

    // The HUD the HUD page acts on, when `withHud`.
    [[nodiscard]] coney::hud::Hud* hud() override { return withHud ? &hudObject : nullptr; }

    coney::hud::Hud hudObject;
    bool withHud = false;
    coney::anim::Vec3 feet{0.0F, 0.0F, 0.0F};
    float heading = 0.0F;
    int teleports = 0;
    int resets = 0;
    bool frozen = false;
    bool noclipHere = true;
    bool flying = false;
    bool free = false;
    bool spawnable = true;
    std::vector<coney::sandbox::Primitive> spawned;
    std::vector<coney::debug::CharacterChoice> choices;
    int type = 32;
    int changes = 0;
    struct SpawnedFighter {
        coney::anim::Vec3 feet;
        float heading;
        std::string model;
        coney::ai::FighterSide side = coney::ai::FighterSide::Enemy;
    };
    std::vector<SpawnedFighter> fighters;
    std::vector<std::string> models;
    std::optional<std::size_t> fighterLimit;
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
    for (const std::string_view title : {"Player", "Camera", "Spawner", "AI fighters", "Animation player"}) {
        INFO(title);
        const auto page = session.model().openPage(title);
        REQUIRE(page != nullptr);
        REQUIRE(page->items().size() == 1);
        CHECK(page->items().front().label == "No player");
    }
    // The pages come after Levels, and Debug draw needs no player.
    const auto titles = session.model().pageTitles();
    CHECK(titles == std::vector<std::string>{"Time",
                                             "Tunables",
                                             "Natives",
                                             "Lua console",
                                             "Cheats",
                                             "Levels",
                                             "Missions",
                                             "Movies",
                                             "Player",
                                             "Camera",
                                             "Spawner",
                                             "AI fighters",
                                             "Animation player",
                                             "HUD",
                                             "Debug draw",
                                             "Entity inspector",
                                             "Display",
                                             "Audio",
                                             "Network",
                                             "Input",
                                             "Performance"});
    CHECK(session.model().openPage("Debug draw")->items().size() == 11);
}

TEST_CASE("the HUD page switches the HUD's parts, sets the panel's values and fires messages", "[debug][hud]") {
    TunableRegistry tunables;
    FakePlay play;
    DebugSession session(tunables, servicesOver(&play), nullptr);
    // A mode without a HUD says so.
    auto page = session.model().openPage("HUD");
    REQUIRE(page != nullptr);
    REQUIRE(page->items().size() == 1);
    play.withHud = true;
    page = session.model().openPage("HUD");
    REQUIRE(page != nullptr);
    coney::hud::Hud& hud = play.hudObject;
    itemOn(*page, "HUD shown").setBool(false);
    CHECK_FALSE(hud.visible());
    itemOn(*page, "HUD shown").setBool(true);
    itemOn(*page, "Force show panel").setBool(true);
    CHECK(hud.panel(0).forceShow());
    itemOn(*page, "Flash rage bar").setBool(true);
    CHECK(hud.panel(0).flashFrames() == 5);
    itemOn(*page, "Instruction arrow").setBool(true);
    CHECK(hud.arrow().on);
    itemOn(*page, "Money").setNumber(250.0);
    CHECK(hud.overrides(0).money == std::optional<int>(250));
    CHECK(itemOn(*page, "Money").getNumber() == 250.0);
    itemOn(*page, "Money").setNumber(-1.0);
    CHECK_FALSE(hud.overrides(0).money.has_value());
    itemOn(*page, "Give one of each item").run();
    CHECK(hud.overrides(0).items == std::optional<std::array<int, 4>>(std::array<int, 4>{1, 1, 1, 1}));
    itemOn(*page, "Fire an objective").run();
    CHECK(hud.scrollIn().messages().size() == 1);
    itemOn(*page, "Fire a hint").run();
    CHECK(hud.hints().showing().has_value());
    itemOn(*page, "Fire an announcement").run();
    CHECK(hud.centredAnnouncement().has_value());
    itemOn(*page, "Action prompt").setBool(true);
    CHECK(hud.actionPrompt(0) == "Debug action prompt");
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

    // A mode that cannot spawn: the page says so instead of offering objects.
    play.spawnable = false;
    const auto unable = session.model().openPage("Spawner");
    REQUIRE(unable->items().size() == 1);
    CHECK(unable->items().front().label == "Spawning");
    CHECK(unable->items().front().watch() == "not in this mode");
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
    // The crate and the wall are climbable walls and the fence a climbable fence, as the parkour course's are.
    // REQUIRE ends the test case when the name is missing; nothing after FAIL could be reached (MSVC C4702).
    const auto find = [](std::string_view name) {
        const std::vector<coney::debug::Spawnable>& all = coney::debug::spawnables();
        const auto it = std::find_if(all.begin(), all.end(), [name](const coney::debug::Spawnable& spawnable) {
            return spawnable.name == name;
        });
        REQUIRE(it != all.end());
        return it->primitive;
    };
    CHECK(find("Crate (1 m)").surface.flags == coney::human::kTriangleClimbable);
    CHECK(find("Wall (2.5 m)").surface.flags == coney::human::kTriangleClimbable);
    CHECK(find("Fence (1.2 m)").surface.material == coney::human::kClimbableMaterial);
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
    services.sandboxFolder = coney::test::assetsDir() / "sandbox";
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

TEST_CASE("the Coordinates toggle shows player 1's feet and heading, and says so with no player", "[debug]") {
    TunableRegistry tunables;
    FakePlay play;
    play.feet = {1.5F, -2.25F, 3.0F};
    play.heading = 90.0F;
    DebugSession session(tunables, servicesOver(&play), nullptr);
    CHECK(session.cornerLines().empty());
    const auto page = session.model().openPage("Display");
    REQUIRE(page != nullptr);
    itemOn(*page, "Coordinates").setBool(true);
    REQUIRE(session.cornerLines().size() == 1);
    CHECK(session.cornerLines().front() == "x 1.50  y -2.25  z 3.00  heading 90.0");
    // It follows the player.
    play.feet = {0.0F, 0.0F, 0.0F};
    CHECK(session.cornerLines().front() == "x 0.00  y 0.00  z 0.00  heading 90.0");

    DebugSession bare(tunables, servicesOver(nullptr), nullptr);
    bare.display().coordinates = true;
    CHECK(bare.cornerLines() == std::vector<std::string>{"coordinates: no player"});
}

namespace {

// An input source that gives back whatever the test put in `next`.
class FakeInput final : public coney::InputSource {
  public:
    coney::PortSamples next{};
    coney::PortSamples sample(std::uint64_t /*frame*/) override { return next; }
};

} // namespace

TEST_CASE("the Player page switches the noclip with a notice, and its hotkey (on by default) until switched off",
          "[debug][noclip]") {
    TunableRegistry tunables;
    FakePlay play;
    FakeInput input;
    DebugSession session(tunables, servicesOver(&play), &input);
    const auto page = session.model().openPage("Player");
    REQUIRE(page != nullptr);
    std::uint64_t frame = 0;
    // One frame through the gate with port 1 holding `buttons`; what the game sees of it.
    const auto frameHolding = [&](std::uint16_t buttons) {
        input.next[0].connected = true;
        input.next[0].buttons = buttons;
        return session.gate().sample(frame++)[0].buttons;
    };

    // The menu's toggle, with the notice under the corner lines.
    CHECK_FALSE(itemOn(*page, "Noclip").getBool());
    itemOn(*page, "Noclip").setBool(true);
    CHECK(play.flying);
    CHECK(itemOn(*page, "Noclip").getBool());
    CHECK(session.cornerLines() == std::vector<std::string>{"Noclip on"});
    itemOn(*page, "Noclip").setBool(false);
    CHECK_FALSE(play.flying);
    CHECK(session.cornerLines() == std::vector<std::string>{"Noclip off"});
    // The notice goes after its frames.
    for (int i = 0; i < DebugSession::kNoticeFrames; ++i) {
        (void)frameHolding(0);
    }
    CHECK(session.cornerLines().empty());

    // The hotkey is on by default: each press of the chord toggles the noclip, and the game never sees the chord.
    CHECK(itemOn(*page, "Noclip hotkey (R3 + D-pad left)").getBool());
    CHECK(frameHolding(coney::pad::kR3) == coney::pad::kR3);
    CHECK(frameHolding(coney::pad::kR3 | coney::pad::kLeft) == 0);
    CHECK(play.flying);
    CHECK(session.cornerLines() == std::vector<std::string>{"Noclip on"});
    CHECK(frameHolding(coney::pad::kR3 | coney::pad::kLeft) == 0);
    CHECK(play.flying);
    (void)frameHolding(0);
    CHECK(frameHolding(coney::pad::kLeft) == coney::pad::kLeft);
    CHECK(frameHolding(coney::pad::kLeft | coney::pad::kR3) == 0);
    CHECK_FALSE(play.flying);
    CHECK(session.cornerLines() == std::vector<std::string>{"Noclip off"});

    // Switched off, the chord is the game's again.
    (void)frameHolding(0);
    itemOn(*page, "Noclip hotkey (R3 + D-pad left)").setBool(false);
    CHECK(frameHolding(coney::pad::kR3 | coney::pad::kLeft) == (coney::pad::kR3 | coney::pad::kLeft));
    CHECK_FALSE(play.flying);
    (void)frameHolding(0);
    itemOn(*page, "Noclip hotkey (R3 + D-pad left)").setBool(true);

    // A mode that cannot noclip is left alone, and says so.
    (void)frameHolding(0);
    play.noclipHere = false;
    (void)frameHolding(coney::pad::kR3 | coney::pad::kLeft);
    CHECK_FALSE(play.flying);
}

TEST_CASE("the AI fighters page spawns the amount asked for, as the model picked", "[debug][fighters]") {
    TunableRegistry tunables;
    FakePlay play;
    play.models = {"model_a", "model_b", "model_c"};
    DebugSession session(tunables, servicesOver(&play), nullptr);
    const auto page = session.model().openPage("AI fighters");
    REQUIRE(page != nullptr);

    // The picker: Random first, then the models.
    const MenuItem& model = itemOn(*page, "Model");
    REQUIRE(model.choices.size() == 4);
    CHECK(model.choices[0] == "Random");
    CHECK(model.choices[2] == "model_b");
    const MenuItem& amount = itemOn(*page, "Amount");
    CHECK(amount.getNumber() == 1.0);

    // One click, a chosen model, three fighters: each is that model, all facing the player (heading 0, so 180).
    model.setChoice(2);
    amount.setNumber(3.0);
    itemOn(*page, "Spawn fighters").run();
    REQUIRE(play.fighters.size() == 3);
    for (const auto& fighter : play.fighters) {
        CHECK(fighter.model == "model_b");
        CHECK(fighter.heading == 180.0F);
    }
    // They stand apart, in a row across the player's line, 4 m ahead (+y).
    CHECK(play.fighters[0].feet.y == Catch::Approx(4.0F));
    CHECK(play.fighters[0].feet.x != play.fighters[1].feet.x);

    // Random: each gets its own draw from the list, so a run of them is not all one model.
    play.fighters.clear();
    model.setChoice(0);
    amount.setNumber(12.0);
    itemOn(*page, "Spawn fighters").run();
    REQUIRE(play.fighters.size() == 12);
    std::set<std::string> seen;
    for (const auto& fighter : play.fighters) {
        CHECK_FALSE(fighter.model.empty());
        seen.insert(fighter.model);
    }
    CHECK(seen.size() > 1);
}

TEST_CASE("the AI fighters page stops at the first fighter that cannot be spawned and says so", "[debug][fighters]") {
    TunableRegistry tunables;
    FakePlay play;
    play.fighterLimit = 2;
    DebugSession session(tunables, servicesOver(&play), nullptr);
    const auto page = session.model().openPage("AI fighters");
    REQUIRE(page != nullptr);
    // No model list here: only Random, which means the scene's own character.
    CHECK(itemOn(*page, "Model").choices == std::vector<std::string>{"Random"});
    itemOn(*page, "Amount").setNumber(5.0);
    itemOn(*page, "Spawn fighters").run();
    CHECK(play.fighters.size() == 2);
    CHECK(play.fighters[0].model.empty());
    CHECK(session.log().last(1).front() == "fighters: 2 spawned of 5");
}

TEST_CASE("spawning more fighters than one click places each batch beyond the last", "[debug][fighters]") {
    FakePlay play;
    play.models = {"model_a", "model_b"};
    coney::debug::SpawnRandom random(1);
    const coney::debug::SpawnedFighters spawned = coney::debug::spawnFighters(play, 0, 120, 4.0F, random);
    CHECK(spawned.made == 120);
    CHECK(spawned.asked == 120);
    CHECK_FALSE(spawned.error.has_value());
    REQUIRE(play.fighters.size() == 120);
    // Two clicks of 50 (ten rows each) and one of 20: no two fighters on one spot, the last row 24 rows out.
    std::set<std::pair<float, float>> spots;
    for (const auto& fighter : play.fighters) {
        spots.emplace(fighter.feet.x, fighter.feet.y);
        CHECK(fighter.heading == 180.0F);
    }
    CHECK(spots.size() == 120);
    CHECK(play.fighters[50].feet.y == Catch::Approx(4.0F + (10.0F * 1.2F)));
    CHECK(play.fighters.back().feet.y == Catch::Approx(4.0F + (23.0F * 1.2F)));
}

TEST_CASE("the AI fighters page spawns on the side picked, Enemy by default, allies behind the player",
          "[debug][fighters]") {
    // Expected: unverified, #924 (the page is Coney's own)
    TunableRegistry tunables;
    FakePlay play;
    DebugSession session(tunables, servicesOver(&play), nullptr);
    const auto page = session.model().openPage("AI fighters");
    REQUIRE(page != nullptr);

    // The picker lists the three sides, the player's enemies first and chosen.
    const MenuItem& side = itemOn(*page, "Side");
    REQUIRE(side.choices.size() == 3);
    CHECK(side.choices[0] == "Enemy");
    CHECK(side.choices[1] == "Ally");
    CHECK(side.choices[2] == "Neutral");
    CHECK(side.getChoice() == 0);
    itemOn(*page, "Amount").setNumber(2.0);
    itemOn(*page, "Spawn fighters").run();
    REQUIRE(play.fighters.size() == 2);
    for (const auto& fighter : play.fighters) {
        CHECK(fighter.side == coney::ai::FighterSide::Enemy);
        // Ahead of the player (heading 0 faces +y), facing him.
        CHECK(fighter.feet.y == Catch::Approx(4.0F));
        CHECK(fighter.heading == 180.0F);
    }

    // Set before Spawn fighters, each fighter takes the side chosen when it was made: allies stand 4 m behind him,
    // facing the way he does, and a neutral stands ahead as an enemy does.
    play.fighters.clear();
    side.setChoice(1);
    itemOn(*page, "Spawn fighters").run();
    side.setChoice(2);
    itemOn(*page, "Spawn fighters").run();
    REQUIRE(play.fighters.size() == 4);
    CHECK(play.fighters[0].side == coney::ai::FighterSide::Ally);
    CHECK(play.fighters[1].side == coney::ai::FighterSide::Ally);
    CHECK(play.fighters[0].feet.y == Catch::Approx(-4.0F));
    CHECK(play.fighters[0].heading == 0.0F);
    CHECK(play.fighters[2].side == coney::ai::FighterSide::Neutral);
    CHECK(play.fighters[2].feet.y == Catch::Approx(4.0F));
    CHECK(play.fighters[2].heading == 180.0F);

    // Clear fighters still removes every one of them.
    itemOn(*page, "Clear fighters").run();
    CHECK(play.fighters.empty());
}

TEST_CASE("allies spawned beyond one click's worth stand behind the rows before, each on one spot",
          "[debug][fighters]") {
    // Expected: unverified, #924 (Coney's own placement)
    FakePlay play;
    coney::debug::SpawnRandom random(1);
    const coney::debug::SpawnedFighters spawned =
        coney::debug::spawnFighters(play, 0, 60, 4.0F, random, coney::ai::FighterSide::Ally);
    CHECK(spawned.made == 60);
    REQUIRE(play.fighters.size() == 60);
    std::set<std::pair<float, float>> spots;
    for (const auto& fighter : play.fighters) {
        spots.emplace(fighter.feet.x, fighter.feet.y);
        CHECK(fighter.side == coney::ai::FighterSide::Ally);
        CHECK(fighter.heading == 0.0F);
        CHECK(fighter.feet.y < 0.0F);
    }
    CHECK(spots.size() == 60);
    // The second click's first row is beyond the ten of the first: 4 m, then ten rows of 1.2 m, behind him.
    CHECK(play.fighters[50].feet.y == Catch::Approx(-(4.0F + (10.0F * 1.2F))));
}
