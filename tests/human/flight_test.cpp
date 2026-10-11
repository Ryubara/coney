// SPDX-License-Identifier: GPL-3.0-or-later
// The debug noclip's flight (Human::setFlight(), human::noclipFlight()): the stick flies the human across the ground
// turned by the camera, the climb raises and lowers him, nothing collides or pulls him down, he faces where he flies,
// and ending the flight drops him with the normal physics. Coney's own tool (docs/guides/debug-menu.md#noclip).
#include <cstddef>
#include <cstdint>
#include <optional>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "characters/anim_set.h"
#include "core/pad.h"
#include "human/human.h"
#include "human/player.h"
#include "support/collision_fixtures.h"
#include "support/human_fixtures.h"

using Catch::Approx;
using coney::anim::Vec3;
using coney::human::Flight;
using coney::human::Human;
using coney::human::HumanInput;
namespace pad = coney::pad;

namespace {

// The camera looking along +y, and one looking along +x.
constexpr Vec3 kAlongY{0.0F, 1.0F, 0.0F};
constexpr Vec3 kAlongX{1.0F, 0.0F, 0.0F};

// A stick at `x`, `y` seen from a camera looking along `camera`.
HumanInput stick(float x, float y, Vec3 camera = kAlongY) {
    return HumanInput{.stickX = x, .stickY = y, .cameraForward = camera, .targets = {}};
}

// The synthetic character and its anim set, held together for a test.
struct TestCharacter {
    coney::characters::CharacterData data = coney::test::locomotionData();
    coney::characters::AnimSet anims{data, nullptr};
};

// A human of `character` spawned at `at` on `mesh`, facing +y.
Human spawnHuman(const TestCharacter& character, const coney::raycast::CollisionMesh* mesh, Vec3 at) {
    Human human(character.anims, coney::human::AnimSlots::player(), coney::test::identityBind());
    human.spawn(mesh, at, 0.0F);
    return human;
}

// A pad holding `buttons`, with `r2` and `l2` the triggers' pressures.
coney::Pad padHolding(std::uint16_t buttons, std::uint8_t r2 = 0, std::uint8_t l2 = 0) {
    coney::PadSample sample;
    sample.connected = true;
    sample.buttons = buttons;
    sample.pressure.at(static_cast<std::size_t>(pad::Pressure::R2)) = r2;
    sample.pressure.at(static_cast<std::size_t>(pad::Pressure::L2)) = l2;
    coney::Pad result;
    result.update(sample);
    return result;
}

} // namespace

TEST_CASE("the noclip flies at 6 m/s, R1 four times faster, R2 up and L2 down by their pull", "[human][noclip]") {
    const Flight still = coney::human::noclipFlight(padHolding(0));
    CHECK(still.speed == Approx(6.0F));
    CHECK(still.climb == Approx(0.0F));
    CHECK(coney::human::noclipFlight(padHolding(pad::kR1)).speed == Approx(24.0F));
    // A full pull either way, analog when the trigger reports its pressure.
    CHECK(coney::human::noclipFlight(padHolding(pad::kR2, 255)).climb == Approx(6.0F));
    CHECK(coney::human::noclipFlight(padHolding(pad::kL2, 0, 255)).climb == Approx(-6.0F));
    CHECK(coney::human::noclipFlight(padHolding(0, 51)).climb == Approx(1.2F));
    CHECK(coney::human::noclipFlight(padHolding(pad::kR1 | pad::kR2, 255)).climb == Approx(24.0F));
    // A held trigger with no pressure (a script's, a digital pad's) is a full pull; a trigger at rest is none.
    CHECK(coney::human::noclipFlight(padHolding(pad::kL2)).climb == Approx(-6.0F));
    CHECK(coney::human::noclipFlight(padHolding(0, 4)).climb == Approx(0.0F));
    // Both pulled cancel.
    CHECK(coney::human::noclipFlight(padHolding(pad::kL2 | pad::kR2, 255, 255)).climb == Approx(0.0F));
}

TEST_CASE("a flying human goes where the stick points as the camera sees it, facing that way", "[human][noclip]") {
    const TestCharacter character;
    const auto mesh = coney::test::makeMesh(coney::test::floorAt(0.0F, 0.0F, 80.0F, 0.0F, 80.0F));
    Human human = spawnHuman(character, mesh.get(), Vec3{40.0F, 40.0F, 0.0F});
    human.step(stick(0.0F, 0.0F), mesh.get());
    human.setFlight(Flight{.speed = 6.0F, .climb = 0.0F});
    REQUIRE(human.flying());

    // Stick up with the camera looking along +x: 0.2 m along +x an update, at once, facing +x (heading -90°).
    human.step(stick(0.0F, 1.0F, kAlongX), mesh.get());
    CHECK(human.position().x == Approx(40.2F));
    CHECK(human.position().y == Approx(40.0F));
    CHECK(human.heading() == Approx(coney::human::headingOf(kAlongX)));
    for (int i = 0; i < 29; ++i) {
        human.step(stick(0.0F, 1.0F, kAlongX), mesh.get());
    }
    CHECK(human.position().x == Approx(46.0F));
    CHECK(human.speed() == Approx(6.0F));
    // Half a push flies at half the speed; stick right with the camera along +y flies along +x.
    human.step(stick(0.5F, 0.0F), mesh.get());
    CHECK(human.position().x == Approx(46.1F));
    // Let go, he hovers where he is, keeping his heading.
    const float heading = human.heading();
    human.step(stick(0.0F, 0.0F), mesh.get());
    CHECK(human.position().x == Approx(46.1F));
    CHECK(human.heading() == Approx(heading));
    CHECK(human.speed() == Approx(0.0F));
}

TEST_CASE("a flying human rises and descends by the climb, with no gravity and no fall", "[human][noclip]") {
    const TestCharacter character;
    const auto mesh = coney::test::makeMesh(coney::test::floorAt(0.0F, 0.0F, 80.0F, 0.0F, 80.0F));
    Human human = spawnHuman(character, mesh.get(), Vec3{40.0F, 40.0F, 0.0F});
    human.step(stick(0.0F, 0.0F), mesh.get());
    human.setFlight(Flight{.speed = 6.0F, .climb = 6.0F});
    for (int i = 0; i < 30; ++i) {
        human.step(stick(0.0F, 0.0F), mesh.get());
    }
    CHECK(human.position().z == Approx(6.0F));
    // Hovering two seconds 6 m up: no gravity, never airborne.
    human.setFlight(Flight{.speed = 6.0F, .climb = 0.0F});
    for (int i = 0; i < 60; ++i) {
        human.step(stick(0.0F, 0.0F), mesh.get());
        CHECK_FALSE(human.airborne());
    }
    CHECK(human.position().z == Approx(6.0F));
    human.setFlight(Flight{.speed = 6.0F, .climb = -3.0F});
    for (int i = 0; i < 30; ++i) {
        human.step(stick(0.0F, 0.0F), mesh.get());
    }
    CHECK(human.position().z == Approx(3.0F));
}

TEST_CASE("a flying human passes through walls and floors", "[human][noclip]") {
    const TestCharacter character;
    // A floor and a 3 m wall across his way at y = 42.
    const auto mesh =
        coney::test::makeMesh(coney::test::join(coney::test::floorAt(0.0F, 0.0F, 80.0F, 0.0F, 80.0F),
                                                coney::test::wallFacingMinusY(42.0F, 0.0F, 80.0F, 0.0F, 3.0F)));
    Human human = spawnHuman(character, mesh.get(), Vec3{40.0F, 40.0F, 0.0F});
    human.step(stick(0.0F, 0.0F), mesh.get());
    human.setFlight(Flight{.speed = 6.0F, .climb = 0.0F});
    for (int i = 0; i < 30; ++i) {
        human.step(stick(0.0F, 1.0F), mesh.get());
    }
    CHECK(human.position().y == Approx(46.0F));
    // Down through the floor, and on below it: not out of the world, not falling.
    human.setFlight(Flight{.speed = 6.0F, .climb = -6.0F});
    for (int i = 0; i < 15; ++i) {
        human.step(stick(0.0F, 0.0F), mesh.get());
    }
    CHECK(human.position().z == Approx(-3.0F));
    CHECK_FALSE(human.airborne());
    CHECK_FALSE(human.outOfWorld());
}

TEST_CASE("starting a flight stops a fall; ending one drops him from where he is", "[human][noclip]") {
    const TestCharacter character;
    const auto mesh = coney::test::makeMesh(coney::test::floorAt(0.0F, 0.0F, 80.0F, 0.0F, 80.0F));
    // 5 m up over the floor: he falls.
    Human human = spawnHuman(character, mesh.get(), Vec3{40.0F, 40.0F, 5.0F});
    human.step(stick(0.0F, 0.0F), mesh.get());
    human.step(stick(0.0F, 0.0F), mesh.get());
    REQUIRE(human.airborne());
    const float caught = human.position().z;
    human.setFlight(Flight{.speed = 6.0F, .climb = 0.0F});
    CHECK_FALSE(human.airborne());
    for (int i = 0; i < 10; ++i) {
        human.step(stick(0.0F, 0.0F), mesh.get());
    }
    CHECK(human.position().z == Approx(caught));

    // Off: the normal physics take him, he falls and lands on the floor.
    human.setFlight(std::nullopt);
    CHECK_FALSE(human.flying());
    human.step(stick(0.0F, 0.0F), mesh.get());
    CHECK(human.airborne());
    for (int i = 0; i < 90 && human.airborne(); ++i) {
        human.step(stick(0.0F, 0.0F), mesh.get());
    }
    CHECK_FALSE(human.airborne());
    CHECK(human.position().z == Approx(0.0F).margin(0.01F));
}

TEST_CASE("ending a flight just over the ground stands him on it", "[human][noclip]") {
    const TestCharacter character;
    const auto mesh = coney::test::makeMesh(coney::test::floorAt(0.0F, 0.0F, 80.0F, 0.0F, 80.0F));
    Human human = spawnHuman(character, mesh.get(), Vec3{40.0F, 40.0F, 0.0F});
    human.step(stick(0.0F, 0.0F), mesh.get());
    human.setFlight(Flight{.speed = 6.0F, .climb = 3.0F});
    human.step(stick(0.0F, 0.0F), mesh.get()); // 0.1 m up
    human.setFlight(std::nullopt);
    human.step(stick(0.0F, 0.0F), mesh.get());
    CHECK_FALSE(human.airborne());
    CHECK(human.position().z == Approx(0.0F).margin(1e-4F));
}
