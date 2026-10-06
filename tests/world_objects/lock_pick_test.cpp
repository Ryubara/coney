// SPDX-License-Identifier: GPL-3.0-or-later
// Lock picking (docs/research/crimes.md#lockpick): the dial's pins, speeds and directions, the judged bands, a miss
// resetting the pins, the outcome at the door, the callbacks and the break-in.
#include "world_objects/lock_pick.h"

#include <cmath>
#include <numbers>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "support/object_fixtures.h"
#include "world_objects/doors.h"

using coney::world_objects::LockPick;
using coney::world_objects::LockPickDial;
using coney::world_objects::LockPickHandlers;
using coney::world_objects::LockPickState;
using coney::world_objects::PinPress;

namespace {

// Steps `dial` `n` frames.
void steps(LockPickDial& dial, int n) {
    for (int i = 0; i < n; ++i) {
        dial.step();
    }
}

} // namespace

TEST_CASE("the dial: only the current pin turns, by 0.1 rad times its speed", "[world_objects][lockpick]") {
    LockPickDial dial(1);
    steps(dial, 5);
    CHECK(dial.pins()[0] == Catch::Approx(std::numbers::pi + 0.5).margin(1e-4));
    CHECK(dial.pins()[1] == Catch::Approx(std::numbers::pi));
    // Difficulty 2 turns the first pin backwards, wrapping below 0.
    LockPickDial hard(2);
    steps(hard, 32);
    CHECK(hard.pins()[0] == Catch::Approx(std::numbers::pi - 3.2 + 2.0 * std::numbers::pi).margin(1e-4));
    CHECK(LockPickDial(7).difficulty() == 2);
}

TEST_CASE("three good presses open the lock; a miss starts over", "[world_objects][lockpick]") {
    LockPickDial dial(0);
    CHECK(dial.press() == PinPress::Miss); // π is in no band
    CHECK(dial.good() == 0);
    steps(dial, 24); // 5.54: good
    CHECK(dial.press() == PinPress::Good);
    CHECK(dial.currentPin() == 1);
    steps(dial, 3); // the second pin at π + 0.6: a miss puts every pin back
    CHECK(dial.press() == PinPress::Miss);
    CHECK(dial.good() == 0);
    CHECK(dial.pins()[0] == Catch::Approx(std::numbers::pi));
    steps(dial, 24);
    CHECK(dial.press() == PinPress::Good);
    steps(dial, 12); // speed 2
    CHECK(dial.press() == PinPress::Good);
    steps(dial, 8); // speed 3
    CHECK(dial.press() == PinPress::Good);
    CHECK(dial.state() == LockPickState::Succeeded);
    CHECK_FALSE(dial.perfect());
}

TEST_CASE("perfect presses all the way make a perfect pick", "[world_objects][lockpick]") {
    LockPickDial dial(0);
    steps(dial, 28);
    CHECK(dial.press() == PinPress::Perfect);
    steps(dial, 14);
    CHECK(dial.press() == PinPress::Perfect);
    steps(dial, 10);
    CHECK(dial.press() == PinPress::Perfect);
    CHECK(dial.perfect());
}

TEST_CASE("a pick at a door: callbacks, the door opened and the break-in", "[world_objects][lockpick]") {
    coney::test::ObjectWorldFixture fixture;
    coney::world_objects::Doors doors;
    const coney::world_objects::ObjectTypeInfo info{.className = "dyn_door_swinging", .hitpoints = 100};
    const double door =
        doors
            .spawn(
                fixture.handle(),
                coney::world_objects::DoorSpawn{.type = "dyn_door_storeb", .position = {4.0F, 2.0F, 0.0F}}, &info,
                [&fixture] { return fixture.handle(); }, fixture.world)
            .handle;
    const LockPickHandlers handlers{.start = "OnStart", .stop = "OnStop", .success = "OnWin", .stageFail = "OnFail"};
    LockPick pick(handlers, 5.0, {4.0F, -1.0F, 0.0F}, door, 0, fixture.world);
    REQUIRE(fixture.services.scripts.size() == 1);
    CHECK(fixture.services.scripts[0].function == "OnStart");
    CHECK(pick.press(doors, fixture.world) == PinPress::Miss);
    CHECK(fixture.services.clicks == 1);
    CHECK(fixture.services.scripts.back().function == "OnFail");
    for (const int frames : {24, 12, 8}) {
        for (int i = 0; i < frames; ++i) {
            pick.step();
        }
        pick.press(doors, fixture.world);
    }
    CHECK(pick.state() == LockPickState::Succeeded);
    CHECK(fixture.services.scripts.back().function == "OnWin");
    CHECK_FALSE(doors.find(door)->pickable);
    CHECK(doors.find(door)->state == coney::world_objects::door_state::kOpening);
    REQUIRE(fixture.services.crimes.size() == 1);
    CHECK(fixture.services.crimes[0].offender == 5.0);
}

TEST_CASE("abandoned picks: the stop callback, and the third is a break-in", "[world_objects][lockpick]") {
    coney::test::ObjectWorldFixture fixture;
    coney::world_objects::Doors doors;
    const coney::world_objects::ObjectTypeInfo info{.className = "dyn_door_swinging", .hitpoints = 100};
    const double door =
        doors
            .spawn(
                fixture.handle(),
                coney::world_objects::DoorSpawn{.type = "dyn_door_storeb", .position = {4.0F, 2.0F, 0.0F}}, &info,
                [&fixture] { return fixture.handle(); }, fixture.world)
            .handle;
    const LockPickHandlers handlers{.stop = "OnStop"};
    for (int attempt = 1; attempt <= 3; ++attempt) {
        LockPick pick(handlers, 5.0, {}, door, 1, fixture.world);
        pick.abandon(doors, fixture.world);
        CHECK(pick.state() == LockPickState::Abandoned);
        CHECK(fixture.services.crimes.size() == (attempt == 3 ? 1U : 0U));
    }
    CHECK(fixture.services.scripts.size() == 3);
    CHECK(doors.find(door)->pickable);
}
