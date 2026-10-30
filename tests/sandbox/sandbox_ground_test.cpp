// SPDX-License-Identifier: GPL-3.0-or-later
#include "sandbox/sandbox_ground.h"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "core/error.h"
#include "sandbox/sandbox_layout.h"
#include "sandbox/sandbox_world.h"

using Catch::Approx;
using coney::sandbox::SandboxGround;
using coney::sandbox::SandboxWorld;

namespace {

// A synthetic yard: a floor whose top is at z = 0, a 1 m block, two spawns (one above the floor, one 10 m up in
// the air over nothing within 4 m), a target and a fighter.
constexpr std::string_view kYard = R"(
title yard
box at=0,0,-0.5 size=40,40,0.5
box at=5,0,0 size=2,2,1
spawn start at=0,0,1.5 heading=90
spawn block at=5,0,1.8 heading=0
spawn high at=-5,0,10 heading=180
target dummy at=0,3,0 heading=180 health=900
fighter brawler at=0,-6,0 heading=45
)";

// The world of `text`, which must build.
SandboxWorld worldOf(std::string_view text) {
    auto layout = coney::sandbox::parseSandboxLayout(text);
    REQUIRE(layout.has_value());
    auto world = SandboxWorld::build(std::move(*layout));
    REQUIRE(world.has_value());
    return std::move(*world);
}

// The ground of `text` at `spawn`, which must be made.
std::unique_ptr<SandboxGround> groundOf(std::string_view text, const std::optional<std::string>& spawn = {}) {
    auto ground = SandboxGround::create(worldOf(text), spawn);
    REQUIRE(ground.has_value());
    return std::move(*ground);
}

} // namespace

TEST_CASE("a sandbox ground starts the player at the first spawn, dropped onto the ground") {
    // Expected: docs/guides/sandbox.md#ground (the first spawn, dropped from 2 m above it onto what is below)
    const auto ground = groundOf(kYard);
    CHECK(ground->start().position.x == Approx(0.0F));
    CHECK(ground->start().position.y == Approx(0.0F));
    CHECK(ground->start().position.z == Approx(0.0F).margin(1e-4));
    CHECK(ground->start().headingDegrees == Approx(90.0F));
    CHECK(ground->startSource() == "spawn point start");
    CHECK(ground->name() == "sandbox yard");
    CHECK(!ground->collision().triangles().empty());
}

TEST_CASE("a sandbox ground starts at the spawn named, on top of what is below it") {
    // Expected: docs/guides/sandbox.md#ground (`--spawn NAME`; the block's top is 1 m up)
    const auto ground = groundOf(kYard, std::string("block"));
    CHECK(ground->start().position.x == Approx(5.0F));
    CHECK(ground->start().position.z == Approx(1.0F).margin(1e-4));
    CHECK(ground->startSource() == "spawn point block");
}

TEST_CASE("a sandbox spawn with no ground within its drop stays where the layout puts it") {
    // Expected: docs/guides/sandbox.md#ground (nothing within twice the 2 m drop: kept where it is)
    const auto ground = groundOf(kYard, std::string("high"));
    CHECK(ground->start().position.z == Approx(10.0F));
    CHECK(ground->start().headingDegrees == Approx(180.0F));
}

TEST_CASE("a sandbox ground refuses an unknown spawn and names the known ones") {
    // Expected: docs/guides/sandbox.md#ground
    auto ground = SandboxGround::create(worldOf(kYard), std::string("nowhere"));
    REQUIRE(!ground.has_value());
    CHECK(ground.error().code == coney::ErrorCode::NotFound);
    CHECK(ground.error().message.find("start, block, high") != std::string::npos);
}

TEST_CASE("a sandbox ground refuses a layout with nothing solid") {
    // Expected: docs/guides/sandbox.md#ground
    auto ground = SandboxGround::create(worldOf("spawn start at=0,0,0\n"), std::nullopt);
    REQUIRE(!ground.has_value());
    CHECK(ground.error().code == coney::ErrorCode::NotFound);
}

TEST_CASE("a sandbox ground's places, targets and fighters are the layout's") {
    // Expected: docs/guides/sandbox.md#ground (the spawns as the layout puts them, not dropped)
    const auto ground = groundOf(kYard);
    const std::vector<coney::human::Place> places = ground->places();
    REQUIRE(places.size() == 3);
    CHECK(places[0].name == "start");
    CHECK(places[0].feet.z == Approx(1.5F));
    CHECK(places[2].name == "high");
    CHECK(places[2].headingDegrees == Approx(180.0F));
    const std::vector<coney::world::SceneTarget> targets = ground->targets();
    REQUIRE(targets.size() == 1);
    CHECK(targets[0].feet.y == Approx(3.0F));
    CHECK(targets[0].headingDegrees == Approx(180.0F));
    CHECK(targets[0].health == 900);
    const std::vector<coney::human::Place> fighters = ground->fighters();
    REQUIRE(fighters.size() == 1);
    CHECK(fighters[0].name == "brawler");
    CHECK(fighters[0].feet.y == Approx(-6.0F));
    CHECK(fighters[0].headingDegrees == Approx(45.0F));
}

TEST_CASE("a rebuilt sandbox ground keeps its start and refuses a world with nothing solid") {
    // Expected: docs/guides/sandbox.md#ground (the Spawner's rebuild keeps the start)
    auto ground = groundOf(kYard, std::string("block"));
    REQUIRE(ground->setWorld(worldOf("title rebuilt\nbox at=0,0,-0.5 size=10,10,0.5\n")).has_value());
    CHECK(ground->name() == "sandbox rebuilt");
    CHECK(ground->start().position.z == Approx(1.0F).margin(1e-4));
    CHECK(ground->startSource() == "spawn point block");
    auto refused = ground->setWorld(worldOf("spawn start at=0,0,0\n"));
    REQUIRE(!refused.has_value());
    CHECK(refused.error().code == coney::ErrorCode::InvalidArgument);
    CHECK(ground->name() == "sandbox rebuilt");
}
