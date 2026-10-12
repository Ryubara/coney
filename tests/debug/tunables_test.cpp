// SPDX-License-Identifier: GPL-3.0-or-later
// The tunables registry: one-line registration, clamping, changes that land only between steps, and the overrides
// file round trip (docs/guides/debug-menu.md#tunables).
#include "debug/tunables.h"

#include <filesystem>
#include <string>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "camera/follow_camera.h"
#include "debug/game_tunables.h"
#include "human/locomotion.h"
#include "scenes/letterbox.h"

using coney::debug::TunableRegistry;
using coney::debug::TunableType;

TEST_CASE("a tunable registers in one line with its range, units and the variable's value as default", "[debug]") {
    TunableRegistry registry;
    float runSpeed = 6.5F;
    registry.add("Movement", "Run speed", &runSpeed).range(0, 20, 0.5).units("m/s").describe("Top speed on foot.");
    auto* tunable = registry.find("Movement/Run speed");
    REQUIRE(tunable != nullptr);
    CHECK(tunable->type() == TunableType::Float);
    CHECK(tunable->defaultValue() == 6.5);
    CHECK(tunable->format(6.5) == "6.5 m/s");
    CHECK(tunable->clamp(30.0) == 20.0);
    CHECK(registry.categories() == std::vector<std::string>{"Movement"});
}

TEST_CASE("a set value waits for applyPending, which the game runs between steps", "[debug]") {
    TunableRegistry registry;
    int deadZone = 10;
    bool god = false;
    registry.add("Input", "Dead zone", &deadZone).range(0, 64, 1);
    registry.add("Cheats", "God", &god);
    CHECK(registry.set("Input/Dead zone", 12.6));
    CHECK(registry.set("Cheats/God", 1));
    CHECK_FALSE(registry.set("Input/Nope", 1));
    // Not written yet: a step in progress keeps its values.
    CHECK(deadZone == 10);
    CHECK(registry.value("Input/Dead zone") == 13.0); // rounded for an int, queued
    CHECK(registry.applyPending() == 2);
    CHECK(deadZone == 13);
    CHECK(god);
    // Clamped to the range.
    registry.set("Input/Dead zone", 500);
    registry.applyPending();
    CHECK(deadZone == 64);
    registry.resetAll();
    registry.applyPending();
    CHECK(deadZone == 10);
    CHECK_FALSE(god);
}

TEST_CASE("overrides save the changed values and load back, also before the tunable registers", "[debug]") {
    TunableRegistry first;
    float gravity = 9.8F;
    int fov = 65;
    first.add("World", "Gravity", &gravity).range(0, 30, 0.1);
    first.add("Camera", "FOV", &fov).range(30, 120, 1);
    first.set("World/Gravity", 4.5);
    const std::string text = first.saveText();
    CHECK(text.find("World/Gravity = 4.5") != std::string::npos);
    CHECK(text.find("Camera/FOV") == std::string::npos); // at its default

    // A second run: the file is read before the subsystem registers; the override waits for it.
    TunableRegistry second;
    REQUIRE(second.loadText(text) == 1);
    CHECK(second.saveText() == text);
    float gravity2 = 9.8F;
    second.add("World", "Gravity", &gravity2).range(0, 30, 0.1);
    CHECK(second.pending() == 1);
    second.applyPending();
    CHECK(gravity2 == 4.5F);

    // Through a file.
    const auto path = std::filesystem::temp_directory_path() / "coney-tunables-test.ini";
    REQUIRE(first.save(path.string()));
    TunableRegistry third;
    CHECK(third.load(path.string()) == 1);
    std::filesystem::remove(path);
    CHECK_FALSE(third.load(path.string()));
}

TEST_CASE("a bad overrides line is refused and nothing is queued", "[debug]") {
    TunableRegistry registry;
    int value = 1;
    registry.add("A", "B", &value).range(0, 10, 1);
    const auto loaded = registry.loadText("A/B = 3\nnot a line\n");
    REQUIRE_FALSE(loaded);
    CHECK(loaded.error().message.find("line 2") != std::string::npos);
    CHECK(registry.pending() == 0);
    CHECK(registry.loadText("# comment\nA/B = on\n") == 1);
}

TEST_CASE("removing a tunable drops it and its queued change", "[debug]") {
    TunableRegistry registry;
    int a = 0;
    int b = 0;
    registry.add("Cat", "A", &a);
    registry.add("Cat", "B", &b);
    registry.set("Cat/A", 0);
    registry.remove("Cat/A");
    CHECK(registry.pending() == 0);
    CHECK(registry.removeCategory("Cat") == 1);
    CHECK(registry.size() == 0);
}

TEST_CASE("the game's movement and camera tunables edit the values the game reads", "[debug]") {
    TunableRegistry registry;
    coney::debug::registerGameTunables(registry);
    CHECK(registry.inCategory("Movement").size() == 12);
    CHECK(registry.inCategory("Body").size() == 5);
    CHECK(registry.inCategory("Sprint").size() == 3);
    CHECK(registry.inCategory("Jump").size() == 4);
    CHECK(registry.inCategory("Climb").size() == 11);
    CHECK(registry.inCategory("Display").size() == 1);
    REQUIRE(registry.find("Movement/Run threshold") != nullptr);
    // Each traversal tunable starts at the original's value.
    REQUIRE(registry.find("Body/Step height") != nullptr);
    CHECK(registry.find("Body/Step height")->defaultValue() == Catch::Approx(0.25));
    REQUIRE(registry.find("Sprint/Drain") != nullptr);
    CHECK(registry.find("Sprint/Drain")->defaultValue() == Catch::Approx(20.0));
    REQUIRE(registry.find("Jump/Up speed") != nullptr);
    CHECK(registry.find("Jump/Up speed")->defaultValue() == Catch::Approx(5.5));
    REQUIRE(registry.find("Climb/High probe") != nullptr);
    CHECK(registry.find("Climb/High probe")->defaultValue() == Catch::Approx(1.7));
    CHECK(registry.find("Movement/Run threshold")->defaultValue() == Catch::Approx(coney::human::kRunThreshold));
    CHECK(registry.find("Follow camera/Position lag")->defaultValue() ==
          Catch::Approx(coney::camera::FollowCamera::kPositionLag));
    registry.set("Movement/Run threshold", 0.5);
    registry.set("Follow camera/Leash far", 5.0);
    registry.applyPending();
    CHECK(coney::human::locomotionTuning().runThreshold == 0.5F);
    CHECK(coney::camera::followDefaults().leashFar == 5.0F);
    // Back to the researched values, so other tests see the game as it is.
    registry.resetAll();
    registry.applyPending();
    CHECK(coney::human::locomotionTuning().runThreshold == coney::human::kRunThreshold);
    CHECK(coney::camera::followDefaults().leashFar == 5.3F);
}

TEST_CASE("the cutscene letterbox setting is on by default and the Display tunable turns the bars off", "[debug]") {
    TunableRegistry registry;
    coney::debug::registerGameTunables(registry);
    const coney::debug::Tunable* letterbox = registry.find("Display/Cutscene letterbox");
    REQUIRE(letterbox != nullptr);
    CHECK(letterbox->defaultValue() == 1.0);
    CHECK(coney::scenes::letterboxSettings().drawn);
    registry.set("Display/Cutscene letterbox", 0.0);
    registry.applyPending();
    CHECK_FALSE(coney::scenes::letterboxSettings().drawn);
    registry.resetAll();
    registry.applyPending();
    CHECK(coney::scenes::letterboxSettings().drawn);
}
