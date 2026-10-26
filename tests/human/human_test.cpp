// SPDX-License-Identifier: GPL-3.0-or-later
#include "human/human.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <numbers>
#include <optional>
#include <string_view>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "characters/anim_set.h"
#include "combat/commands.h"
#include "combat/meters.h"
#include "combat/player_combat.h"
#include "human/body.h"
#include "human/carried_prop.h"
#include "human/fighter.h"
#include "human/human_animator.h"
#include "human/human_sounds.h"
#include "human/player.h"
#include "human/victim.h"
#include "support/collision_fixtures.h"
#include "support/fight_fixtures.h"
#include "support/human_fixtures.h"

using Catch::Approx;
using coney::anim::Vec3;
using coney::human::AnimState;
using coney::human::Human;
using coney::human::HumanInput;

namespace {

// The camera the tests turn the stick with: looking along +y, so stick up walks along +y.
constexpr Vec3 kAlongY{0.0F, 1.0F, 0.0F};

// A stick at `x`, `y` seen from a camera looking along +y.
HumanInput stick(float x, float y) {
    return HumanInput{.stickX = x, .stickY = y, .cameraForward = kAlongY, .targets = {}};
}

// The synthetic character and its anim set, held together for a test.
struct TestCharacter {
    coney::characters::CharacterData data = coney::test::locomotionData();
    coney::characters::AnimSet anims{data, nullptr};
};

// A human of `character` spawned at `at` on `mesh`.
Human spawnHuman(const TestCharacter& character, const coney::raycast::CollisionMesh* mesh, Vec3 at) {
    Human human(character.anims, coney::human::AnimSlots::player(), coney::test::identityBind());
    human.spawn(mesh, at, 0.0F);
    return human;
}

} // namespace

TEST_CASE("the anim set gives each clip its range-flag rate and its root-motion speed", "[human]") {
    const TestCharacter character;
    CHECK(character.anims.rate(408) == Approx(1.0F));  // flag 0x1000
    CHECK(character.anims.rate(413) == Approx(0.75F)); // no flag
    CHECK(character.anims.speed(408) == Approx(1.5F));
    CHECK(character.anims.speed(410) == Approx(7.5F));
    const coney::human::Speeds speeds = coney::human::speedsOf(character.anims, coney::human::AnimSlots::player());
    CHECK(speeds.walk == Approx(1.5F));
    CHECK(speeds.sprint == Approx(10.0F));
    CHECK(coney::human::HumanAnimator::clipsMissing(character.anims, coney::human::AnimSlots::player()) == 0);
}

TEST_CASE("a human is created 0.01 above the ground and then stands exactly on it", "[human]") {
    const TestCharacter character;
    const auto mesh = coney::test::makeMesh(coney::test::floorAt(2.0F, 0.0F, 80.0F, 0.0F, 80.0F));
    Human human = spawnHuman(character, mesh.get(), Vec3{40.0F, 40.0F, 2.8F});
    CHECK(human.position().z == Approx(2.01F));
    human.step(stick(0.0F, 0.0F), mesh.get());
    CHECK(human.position().z == Approx(2.0F));
    CHECK_FALSE(human.airborne());
    CHECK(human.animator().state() == AnimState::Idle);
}

TEST_CASE("a walk starts with the walk start's root motion, then walks at the walk speed whatever the stick",
          "[human]") {
    const TestCharacter character;
    const auto mesh = coney::test::makeMesh(coney::test::floorAt(0.0F, 0.0F, 80.0F, 0.0F, 80.0F));
    for (const float deflection : {0.3F, 0.6F, 0.9F}) {
        Human human = spawnHuman(character, mesh.get(), Vec3{40.0F, 10.0F, 0.0F});
        human.step(stick(0.0F, deflection), mesh.get()); // the first update picks the move state
        CHECK(human.animator().startClipPlaying());
        human.step(stick(0.0F, deflection), mesh.get());
        // The walk start's own root velocity, 1.0 at its rate of 0.75, is the only velocity.
        CHECK(human.speed() == Approx(0.75F));
        for (int i = 0; i < 40; ++i) {
            human.step(stick(0.0F, deflection), mesh.get());
        }
        CHECK(!human.animator().startClipPlaying());
        CHECK(human.speed() == Approx(1.5F));
        CHECK(human.gait() == coney::human::Gait::Walk);
        CHECK(human.animator().animId() == 408U);
        CHECK(human.heading() == Approx(0.0F).margin(1e-5));
        CHECK(human.position().y > 10.0F);
        CHECK(human.position().x == Approx(40.0F).margin(1e-4));
    }
}

TEST_CASE("a full stick runs: the run start, then 0.8 m/s more each update up to the run speed", "[human]") {
    const TestCharacter character;
    const auto mesh = coney::test::makeMesh(coney::test::floorAt(0.0F, 0.0F, 80.0F, 0.0F, 80.0F));
    Human human = spawnHuman(character, mesh.get(), Vec3{40.0F, 5.0F, 0.0F});
    human.step(stick(0.0F, 1.0F), mesh.get());
    CHECK(human.animator().animId() == 414U);
    float last = 0.0F;
    for (int i = 0; i < 40; ++i) {
        human.step(stick(0.0F, 1.0F), mesh.get());
        if (!human.animator().startClipPlaying() && human.speed() < 7.5F && last > 0.0F) {
            CHECK(human.speed() - last <= 0.8F + 1e-4F);
        }
        last = human.speed();
    }
    CHECK(human.speed() == Approx(7.5F));
    CHECK(human.gait() == coney::human::Gait::Run);
    CHECK(human.animator().gaitValue() == Approx(2.0F));
    // Let go: the run skids into the run stop (417), which slides the body on and then gives way to the idle.
    human.step(stick(0.0F, 0.0F), mesh.get());
    CHECK(human.animator().state() == AnimState::RunStop);
    CHECK(human.traversal() == coney::human::Traversal::RunStop);
    for (int i = 0; i < 60; ++i) {
        human.step(stick(0.0F, 0.0F), mesh.get());
    }
    CHECK(human.animator().state() == AnimState::Idle);
    CHECK(human.speed() < 0.5F);
}

TEST_CASE("letting go during the run start goes to the idle at once, whatever the clip's speed", "[human]") {
    const TestCharacter character;
    const auto mesh = coney::test::makeMesh(coney::test::floorAt(0.0F, 0.0F, 80.0F, 0.0F, 80.0F));
    Human human = spawnHuman(character, mesh.get(), Vec3{40.0F, 5.0F, 0.0F});
    human.step(stick(0.0F, 1.0F), mesh.get());
    human.step(stick(0.0F, 1.0F), mesh.get());
    REQUIRE(human.animator().startClipPlaying());
    REQUIRE(human.speed() > 0.5F);
    // The release: the idle replaces the run start (the idle builder's fade over a start clip), so its root motion
    // stops carrying the body (docs/research/characters.md#clip-selection).
    human.step(stick(0.0F, 0.0F), mesh.get());
    CHECK(human.animator().state() == AnimState::Idle);
    const Vec3 at = human.position();
    for (int i = 0; i < 10; ++i) {
        human.step(stick(0.0F, 0.0F), mesh.get());
    }
    CHECK(std::hypot(human.position().x - at.x, human.position().y - at.y) < 0.2F);
}

TEST_CASE("the update a walk start begins does not move the body; the clip moves it from the next", "[human]") {
    const TestCharacter character;
    const auto mesh = coney::test::makeMesh(coney::test::floorAt(0.0F, 0.0F, 80.0F, 0.0F, 80.0F));
    Human human = spawnHuman(character, mesh.get(), Vec3{40.0F, 5.0F, 0.0F});
    const Vec3 start = human.position();
    human.step(stick(0.0F, 0.6F), mesh.get());
    CHECK(human.animator().animId() == 413U);
    CHECK(std::hypot(human.position().x - start.x, human.position().y - start.y) == 0.0F);
    human.step(stick(0.0F, 0.6F), mesh.get());
    CHECK(human.position().y > start.y);
}

TEST_CASE("a human turns toward the stick at no more than its turn limit", "[human]") {
    const TestCharacter character;
    const auto mesh = coney::test::makeMesh(coney::test::floorAt(0.0F, 0.0F, 80.0F, 0.0F, 80.0F));
    Human human = spawnHuman(character, mesh.get(), Vec3{40.0F, 40.0F, 0.0F});
    // Stick right: a heading of -90°.
    float last = human.heading();
    for (int i = 0; i < 30; ++i) {
        human.step(stick(0.6F, 0.0F), mesh.get());
        CHECK(std::abs(human.heading() - last) <= coney::human::maxTurn(coney::human::Gait::Walk) + 1e-5F);
        last = human.heading();
    }
    CHECK(human.heading() == Approx(-std::numbers::pi_v<float> / 2.0F).margin(1e-4));
}

TEST_CASE("walking off a ledge falls with gravity from the second airborne update and lands below", "[human]") {
    const TestCharacter character;
    // A ledge 3 m high ending at y = 20, the ground below it.
    const auto mesh = coney::test::makeMesh(coney::test::join(coney::test::floorAt(3.0F, 0.0F, 80.0F, 0.0F, 20.0F),
                                                              coney::test::floorAt(0.0F, 0.0F, 80.0F, 0.0F, 80.0F)));
    Human human = spawnHuman(character, mesh.get(), Vec3{40.0F, 17.0F, 3.0F});
    int airborneUpdates = 0;
    float firstFall = 1.0F;
    float secondFall = 1.0F;
    for (int i = 0; i < 120; ++i) {
        human.step(stick(0.0F, 0.5F), mesh.get());
        if (!human.airborne()) {
            continue;
        }
        // The vertical speed after the first and second airborne updates (the update that found no ground is the
        // first, and its own z speed is still 0).
        ++airborneUpdates;
        if (airborneUpdates == 2) {
            firstFall = human.velocity().z;
        } else if (airborneUpdates == 3) {
            secondFall = human.velocity().z;
        }
    }
    CHECK(airborneUpdates > 2);
    CHECK(firstFall == 0.0F);
    CHECK(secondFall == Approx(-15.68F / 30.0F));
    CHECK_FALSE(human.airborne());
    CHECK(human.position().z == Approx(0.0F).margin(1e-4));
    CHECK(human.lastLandingSpeed() < -1.0F);
    CHECK(human.velocity().z == 0.0F);
}

TEST_CASE("a step up to 1 m is climbed by the ground snap, a deeper drop is a fall", "[human]") {
    const TestCharacter character;
    const auto mesh = coney::test::makeMesh(coney::test::join(coney::test::floorAt(0.0F, 0.0F, 80.0F, 0.0F, 20.0F),
                                                              coney::test::floorAt(0.4F, 0.0F, 80.0F, 20.0F, 80.0F)));
    Human human = spawnHuman(character, mesh.get(), Vec3{40.0F, 18.0F, 0.0F});
    for (int i = 0; i < 60; ++i) {
        human.step(stick(0.0F, 0.5F), mesh.get());
        CHECK(!human.airborne());
    }
    CHECK(human.position().y > 20.0F);
    CHECK(human.position().z == Approx(0.4F));
}

TEST_CASE("a wall stops the body a radius away and lets it slide along", "[human]") {
    const TestCharacter character;
    // A wall across x = 50 facing -x, from the ground up.
    const auto mesh =
        coney::test::makeMesh(coney::test::join(coney::test::floorAt(0.0F, 0.0F, 80.0F, 0.0F, 80.0F),
                                                coney::test::wallFacingMinusX(50.0F, 0.0F, 80.0F, -1.0F, 5.0F)));
    Human human = spawnHuman(character, mesh.get(), Vec3{45.0F, 40.0F, 0.0F});
    // Walk straight into it (stick right), then diagonally (up and right): the body slides along +y.
    for (int i = 0; i < 150; ++i) {
        human.step(stick(1.0F, 0.0F), mesh.get());
    }
    CHECK(human.position().x == Approx(50.0F - coney::human::playerWalkingRadius(1.0F)).margin(0.02));
    // Walking into it head-on leaves no speed: the slid velocity is all the next update starts from.
    CHECK(human.speed() < 0.9F);
    const float y = human.position().y;
    for (int i = 0; i < 30; ++i) {
        human.step(stick(0.6F, 0.6F), mesh.get());
    }
    CHECK(human.position().x <= 50.0F - coney::human::playerWalkingRadius(1.0F) + 0.02F);
    CHECK(human.position().y > y + 0.5F);
}

TEST_CASE("a move longer than the body's reach meets the wall from where it starts, not past it", "[human]") {
    const TestCharacter character;
    const auto mesh =
        coney::test::makeMesh(coney::test::join(coney::test::floorAt(0.0F, 0.0F, 80.0F, 0.0F, 80.0F),
                                                coney::test::wallFacingMinusX(50.0F, 0.0F, 80.0F, -1.0F, 5.0F)));
    Human human = spawnHuman(character, mesh.get(), Vec3{49.0F, 40.0F, 0.0F});
    human.step(stick(0.0F, 0.0F), mesh.get());
    // A 2 m push in one update would carry the sphere past the wall's plane; the sweep stops it 0.01 m short of the
    // touch (docs/research/physics-bodies.md#u2-00347c08-physicsmesh-sweepcapsule).
    human.addPush(Vec3{2.0F, 0.0F, 0.0F});
    human.step(stick(0.0F, 0.0F), mesh.get());
    CHECK(human.position().x == Approx(50.0F - coney::human::playerWalkingRadius(1.0F) - 0.01F).margin(0.002));
}

TEST_CASE("running into a wall at a steep angle brakes the player to the slid speed", "[human]") {
    const TestCharacter character;
    const auto mesh =
        coney::test::makeMesh(coney::test::join(coney::test::floorAt(0.0F, 0.0F, 80.0F, 0.0F, 80.0F),
                                                coney::test::wallFacingMinusX(50.0F, 0.0F, 80.0F, -1.0F, 5.0F)));
    Human human = spawnHuman(character, mesh.get(), Vec3{40.0F, 20.0F, 0.0F});
    // Run along +x into the wall (stick full right), then hold the stick 20° off the wall's normal: the speed falls
    // toward 0.8 k / (1 - k), k = sin 20°, rather than sliding at the run's speed.
    const float k = std::sin(20.0F * std::numbers::pi_v<float> / 180.0F);
    for (int i = 0; i < 90; ++i) {
        human.step(stick(std::cos(20.0F * std::numbers::pi_v<float> / 180.0F), k), mesh.get());
    }
    CHECK(human.speed() < 2.0F * 0.8F * k / (1.0F - k));
}

TEST_CASE("the stick is camera-relative: up walks away from the camera", "[human]") {
    const TestCharacter character;
    const auto mesh = coney::test::makeMesh(coney::test::floorAt(0.0F, 0.0F, 80.0F, 0.0F, 80.0F));
    Human human = spawnHuman(character, mesh.get(), Vec3{40.0F, 40.0F, 0.0F});
    // The camera looks along -x: stick up walks along -x.
    for (int i = 0; i < 60; ++i) {
        human.step(HumanInput{.stickX = 0.0F, .stickY = 0.6F, .cameraForward = Vec3{-1.0F, 0.0F, -0.2F}, .targets = {}},
                   mesh.get());
    }
    CHECK(human.position().x < 38.0F);
    CHECK(human.position().y == Approx(40.0F).margin(0.3));
    CHECK(human.heading() == Approx(std::numbers::pi_v<float> / 2.0F).margin(1e-3));
}

TEST_CASE("a fall lands on the first update that starts 0.17 m or more below the floor, falling through until then",
          "[human]") {
    // Ledges of several heights put the floor at different phases of the fall's steps (docs/research/characters.md
    // #falling): whatever the phase, the last airborne update ends 0.17 m or more under the floor, and the one before
    // it ends less than that under it (or above it).
    const TestCharacter character;
    for (const float height : {1.0F, 1.3F, 1.6F, 2.0F, 2.45F}) {
        const auto mesh =
            coney::test::makeMesh(coney::test::join(coney::test::floorAt(height, 0.0F, 80.0F, 0.0F, 20.0F),
                                                    coney::test::floorAt(0.0F, 0.0F, 80.0F, 0.0F, 80.0F)));
        Human human = spawnHuman(character, mesh.get(), Vec3{40.0F, 17.0F, height});
        std::vector<float> airborneEnds;
        for (int i = 0; i < 150 && (airborneEnds.empty() || human.airborne()); ++i) {
            human.step(stick(0.0F, 0.35F), mesh.get());
            if (human.airborne()) {
                airborneEnds.push_back(human.position().z);
            }
        }
        REQUIRE(airborneEnds.size() >= 2);
        CHECK_FALSE(human.airborne());
        CHECK(human.position().z == Approx(0.0F).margin(1e-4));
        CHECK(-airborneEnds.back() >= Human::kLandingDepth);
        CHECK(-airborneEnds[airborneEnds.size() - 2] < Human::kLandingDepth);
    }
}

TEST_CASE("a walk let go fades into the idle for 5 updates: pushed again at once, the walk start waits for its end",
          "[human]") {
    const TestCharacter character;
    const auto mesh = coney::test::makeMesh(coney::test::floorAt(0.0F, 0.0F, 80.0F, 0.0F, 80.0F));
    Human human = spawnHuman(character, mesh.get(), Vec3{40.0F, 10.0F, 0.0F});
    for (int update = 0; update < 40; ++update) {
        human.step(stick(0.0F, 0.6F), mesh.get());
    }
    REQUIRE(human.animator().gaitBlendPlaying());
    // Let go: the idle at once, its fade holding 0x10000000 (docs/research/tasks.md#locomotion-gate).
    human.step(stick(0.0F, 0.0F), mesh.get());
    CHECK(human.animator().state() == AnimState::Idle);
    CHECK(human.animator().idleFading());
    // Pushed to the side at once: 4 updates turn him on the spot and do not move him, nor start the walk.
    const Vec3 stood = human.position();
    for (int update = 1; update <= 4; ++update) {
        const float before = human.heading();
        human.step(stick(0.6F, 0.0F), mesh.get());
        INFO("update " << update);
        CHECK(human.animator().animId() == 388U);
        CHECK(human.heading() < before);
        CHECK(std::hypot(human.position().x - stood.x, human.position().y - stood.y) < 1e-4F);
        CHECK(human.stickHeld());
    }
    // The 5th: the fade is over and the walk start begins (at runtime 388 for 5 updates, then 413).
    human.step(stick(0.6F, 0.0F), mesh.get());
    CHECK_FALSE(human.animator().idleFading());
    CHECK(human.animator().animId() == 413U);
}

TEST_CASE("a run let go without a skid keeps turning toward the stick's last angle while the idle fades in",
          "[human]") {
    const TestCharacter character;
    const auto mesh = coney::test::makeMesh(coney::test::floorAt(0.0F, 0.0F, 80.0F, 0.0F, 80.0F));
    Human human = spawnHuman(character, mesh.get(), Vec3{40.0F, 10.0F, 0.0F});
    // A walk with the stick 60° to the right (a walk never skids): the body lags the stick by the turn limit, then
    // the stick is let go after one update of turning.
    for (int update = 0; update < 30; ++update) {
        human.step(stick(0.0F, 0.6F), mesh.get());
    }
    human.step(stick(0.52F, 0.3F), mesh.get());
    const float stickHeading = std::atan2(-0.52F, 0.3F);
    REQUIRE(human.heading() > stickHeading + 0.05F);
    human.step(stick(0.0F, 0.0F), mesh.get());
    const float released = human.heading();
    REQUIRE(human.animator().idleFading());
    // While the fade holds 0x10000000 he turns on toward the stick's last angle (eased from a fresh start), never past
    // it; once it is over he stays put.
    float last = released;
    for (int update = 0; update < 4; ++update) {
        human.step(stick(0.0F, 0.0F), mesh.get());
        CHECK(human.heading() < last);
        CHECK(human.heading() >= stickHeading - 1e-4F);
        last = human.heading();
    }
    human.step(stick(0.0F, 0.0F), mesh.get());
    human.step(stick(0.0F, 0.0F), mesh.get());
    CHECK(human.heading() == last);
}

TEST_CASE("only an airborne human's body touches what the level gives it, at the push-out sphere", "[human]") {
    const TestCharacter character;
    const auto mesh = coney::test::makeMesh(coney::test::floorAt(0.0F, 0.0F, 80.0F, 0.0F, 80.0F));
    Human human = spawnHuman(character, mesh.get(), Vec3{40.0F, 40.0F, 3.0F});
    std::vector<float> heights;
    const Human::BodyContact contact = [&heights](Human& touching, Vec3 centre, float radius) {
        CHECK(radius > 0.0F);
        CHECK(centre.z == Approx(touching.position().z + radius + 0.05F).margin(0.5F));
        heights.push_back(centre.z);
    };
    human.setBodyContact(&contact);
    // Spawned 3 m up it falls: each airborne update asks; once landed, standing or walking, none does.
    for (int i = 0; i < 40 && heights.size() < 3; ++i) {
        human.step(stick(0.0F, 0.0F), mesh.get());
    }
    CHECK(heights.size() >= 3);
    for (int i = 0; i < 60; ++i) {
        human.step(stick(0.0F, 0.0F), mesh.get());
    }
    REQUIRE_FALSE(human.airborne());
    const std::size_t landed = heights.size();
    for (int i = 0; i < 30; ++i) {
        human.step(stick(0.0F, 1.0F), mesh.get());
    }
    CHECK(heights.size() == landed);
}

TEST_CASE("a human made another character in place is the human made as that character", "[human][debug]") {
    // Two characters: the synthetic one, and one whose walk is faster and whose class hits harder.
    const TestCharacter first;
    std::vector<coney::test::LocomotionClip> clips = coney::test::locomotionClips();
    for (coney::test::LocomotionClip& clip : clips) {
        if (clip.id == 408) {
            clip.speed = 2.0F;
        }
    }
    const coney::characters::CharacterData secondData = coney::test::locomotionData(clips);
    const coney::characters::AnimSet secondAnims{secondData, nullptr};
    const coney::combat::AnimRangeList ranges = coney::test::fightRanges();
    const std::vector<std::int16_t> secondDamage(64, 40); // every row of the class table
    coney::human::FighterProfile secondProfile;
    secondProfile.powerClass.powerMax = 600;

    const auto mesh = coney::test::makeMesh(coney::test::floorAt(0.0F, 0.0F, 80.0F, 0.0F, 80.0F));
    // Made as the first, played a little, then made the second where it stands.
    Human changed(first.anims, coney::human::AnimSlots::player(), coney::test::identityBind(), 1.0F, &ranges);
    changed.spawn(mesh.get(), Vec3{10.0F, 10.0F, 0.0F}, 0.0F);
    for (int i = 0; i < 10; ++i) {
        changed.step(stick(0.0F, 1.0F), mesh.get());
    }
    changed.changeCharacter(secondAnims, &ranges, secondDamage, 50, secondProfile);
    changed.spawn(mesh.get(), changed.position(), 0.0F);

    // Made as the second from the start.
    Human fresh(secondAnims, coney::human::AnimSlots::player(), coney::test::identityBind(), 1.0F, &ranges,
                secondDamage, 50);
    fresh.setFighterProfile(secondProfile);
    fresh.spawn(mesh.get(), changed.position(), 0.0F);

    CHECK(&changed.anims() == &fresh.anims());
    REQUIRE(changed.ranges() != nullptr);
    CHECK(changed.ranges() != &ranges); // its own copy, with the class's damage
    CHECK(changed.ranges()->damage(12) == fresh.ranges()->damage(12));
    CHECK(changed.ranges()->damage(12) == 20); // the class table's 40 at 50%
    CHECK(changed.fighterProfile().powerClass.powerMax == 600);
    CHECK(changed.health().value() == fresh.health().value());
    CHECK(changed.health().maximum() == fresh.health().maximum());
    CHECK(changed.animator().state() == AnimState::Idle);
    // It walks as the second character does.
    for (int i = 0; i < 30; ++i) {
        changed.step(stick(0.0F, 0.5F), mesh.get());
        fresh.step(stick(0.0F, 0.5F), mesh.get());
    }
    CHECK(changed.position().y == Approx(fresh.position().y));
    CHECK(changed.speed() == Approx(fresh.speed()));
}

TEST_CASE("a placement (Human_SetTransform) keeps the health, its maximum, the meters, a stun and the clip",
          "[human][placement]") {
    // Issue #234: level99's P3.DealerPoizo teleports the player, hurt to 35 %, to the dealer; a placement that filled
    // his health left the flash nothing to heal, so d-pad right played no clip and no sound. Human_SetTransform
    // (0x0023d440) and the motion reset after it (Human_ResetMotion, 0x0023f158) write none of these.
    const coney::test::FightCharacter character;
    coney::test::Fight fight(character, 1.5F);
    fight.run("5 tap circle\n", 40);
    REQUIRE(fight.human().fighter().pairStage() == coney::human::PairStage::Attached);
    // HuSetMaxHealth's maximum, hurt below 30 % of it and out of power: the held target escapes and the player goes
    // down, stunned.
    fight.human().fighter().health() = coney::combat::Health(1200);
    fight.human().fighter().health().apply(1000);
    fight.human().fighter().combat().power().set(0);
    fight.human().setRageFraction(0.5F);
    fight.run("", 2);
    Human& human = fight.human();
    REQUIRE(human.fighter().victim().stunned());
    const int health = human.health().value();
    const int power = human.fighter().combat().power().value();
    const int rage = human.fighter().combat().rage().value();
    const int stamina = human.stamina().value();
    const AnimState state = human.animator().state();
    REQUIRE(rage > 0);
    REQUIRE(state != AnimState::Idle);

    human.place(nullptr, Vec3{20.0F, 30.0F, 0.5F}, 90.0F);
    human.resetMotion();
    CHECK(human.position().x == Approx(20.0F));
    CHECK(human.position().y == Approx(30.0F));
    CHECK(human.position().z == Approx(0.5F)); // no ground given (a human in a scene): no drop
    CHECK(human.heading() == Approx(std::numbers::pi_v<float> / 2.0F));
    CHECK(human.health().value() == health);
    CHECK(human.health().maximum() == 1200);
    CHECK(human.fighter().combat().power().value() == power);
    CHECK(human.fighter().combat().rage().value() == rage);
    CHECK(human.stamina().value() == stamina);
    CHECK(human.fighter().victim().stunned());
    CHECK(human.animator().state() == state);
    // Out of health stays out of health: a placement wakes no one.
    human.fighter().health().set(0);
    human.place(nullptr, Vec3{25.0F, 30.0F, 0.0F}, 0.0F);
    CHECK(human.health().depleted());
}

TEST_CASE("a placement keeps the stamina and the clip; the motion reset stops the move, not the clip",
          "[human][placement]") {
    const TestCharacter character;
    const auto mesh = coney::test::makeMesh(coney::test::floorAt(0.0F, 0.0F, 80.0F, 0.0F, 80.0F));
    Human human = spawnHuman(character, mesh.get(), Vec3{40.0F, 5.0F, 0.0F});
    // A sprint drains the stamina.
    for (int i = 0; i < 45; ++i) {
        human.step(HumanInput{.stickX = 0.0F,
                              .stickY = 1.0F,
                              .cameraForward = kAlongY,
                              .sprintHeld = true,
                              .actionPressed = false,
                              .command = coney::combat::command::kNone,
                              .buttons = 0,
                              .targets = {}},
                   mesh.get());
    }
    const int stamina = human.stamina().value();
    const AnimState state = human.animator().state();
    REQUIRE(stamina < human.stamina().maximum());
    REQUIRE(state != AnimState::Idle);
    REQUIRE(human.speed() > 1.0F);

    // Teleport's placement alone (Object_Teleport): the move goes on.
    human.place(mesh.get(), Vec3{40.0F, 60.0F, 0.0F}, 180.0F);
    CHECK(human.stamina().value() == stamina);
    CHECK(human.animator().state() == state);
    CHECK(human.speed() > 1.0F);
    // TeleportToFlag's motion reset after it: the move velocity is zero, the clip plays on.
    human.resetMotion();
    CHECK(human.speed() == Approx(0.0F));
    CHECK(human.animator().state() == state);
    CHECK(human.stamina().value() == stamina);
}

TEST_CASE("Coney's walk stand-in (settle) stands the human idle and still with his health and meters kept",
          "[human][placement]") {
    const TestCharacter character;
    const auto mesh = coney::test::makeMesh(coney::test::floorAt(0.0F, 0.0F, 80.0F, 0.0F, 80.0F));
    Human human = spawnHuman(character, mesh.get(), Vec3{40.0F, 5.0F, 0.0F});
    for (int i = 0; i < 45; ++i) {
        human.step(HumanInput{.stickX = 0.0F,
                              .stickY = 1.0F,
                              .cameraForward = kAlongY,
                              .sprintHeld = true,
                              .actionPressed = false,
                              .command = coney::combat::command::kNone,
                              .buttons = 0,
                              .targets = {}},
                   mesh.get());
    }
    human.setHealthPercent(35.0F);
    const int health = human.health().value();
    const int stamina = human.stamina().value();
    REQUIRE(human.animator().state() != AnimState::Idle);
    human.settle();
    CHECK(human.animator().state() == AnimState::Idle);
    CHECK(human.speed() == Approx(0.0F));
    CHECK(human.health().value() == health);
    CHECK(human.stamina().value() == stamina);
}

TEST_CASE("a placement drops the feet onto a hit at most 0.5 below, with no gap; otherwise it stays where put",
          "[human][placement]") {
    const TestCharacter character;
    const auto mesh = coney::test::makeMesh(coney::test::floorAt(0.0F, 0.0F, 80.0F, 0.0F, 80.0F));
    Human human = spawnHuman(character, mesh.get(), Vec3{40.0F, 40.0F, 0.0F});
    // 0.3 above the floor: the 0.5 ray straight down hits it, and the feet go onto the hit.
    human.place(mesh.get(), Vec3{30.0F, 30.0F, 0.3F}, 0.0F);
    CHECK(human.position().z == Approx(0.0F).margin(1e-5));
    // 0.7 above: the ray ends short of the floor, so the point stands as given (a fall follows).
    human.place(mesh.get(), Vec3{30.0F, 30.0F, 0.7F}, 0.0F);
    CHECK(human.position().z == Approx(0.7F));
    // Below the floor nothing is found looking down: as given.
    human.place(mesh.get(), Vec3{30.0F, 30.0F, -0.2F}, 0.0F);
    CHECK(human.position().z == Approx(-0.2F));
}

TEST_CASE("a placement makes the new point the human's last ground, where a human stuck in the air goes back to",
          "[human][placement]") {
    const TestCharacter character;
    const auto mesh = coney::test::makeMesh(coney::test::floorAt(0.0F, 0.0F, 80.0F, 0.0F, 80.0F));
    Human human = spawnHuman(character, mesh.get(), Vec3{10.0F, 10.0F, 0.0F});
    REQUIRE(human.lastGround().x == Approx(10.0F));
    human.place(mesh.get(), Vec3{60.0F, 50.0F, 0.3F}, 0.0F);
    CHECK(human.lastGround().x == Approx(60.0F));
    CHECK(human.lastGround().y == Approx(50.0F));
    CHECK(human.lastGround().z == Approx(0.0F).margin(1e-5));
}

TEST_CASE("Coney's put-back snaps as a creation does, so a start a little under the floor is found from 1 m above",
          "[human][placement]") {
    const TestCharacter character;
    const auto mesh = coney::test::makeMesh(coney::test::floorAt(0.0F, 0.0F, 80.0F, 0.0F, 80.0F));
    Human human = spawnHuman(character, mesh.get(), Vec3{10.0F, 10.0F, 0.0F});
    human.setHealthPercent(35.0F);
    const int health = human.health().value();
    human.putBack(mesh.get(), Vec3{30.0F, 30.0F, -0.2F}, 0.0F);
    CHECK(human.position().z == Approx(0.01F).margin(1e-5));
    CHECK(human.lastGround().z == Approx(0.01F).margin(1e-5));
    CHECK(human.health().value() == health);
}

TEST_CASE("a player handed over stands afresh as the chosen human, not stunned, with the chosen human's meters",
          "[human][placement]") {
    // Human_MakePlayer (0x00229c40) makes the chosen human player 1 as it is; Coney moves its one player human onto it,
    // so the old body's stun and clip must not carry over.
    const coney::test::FightCharacter character;
    coney::test::Fight fight(character, 1.5F);
    fight.run("5 tap circle\n", 40);
    fight.human().fighter().health().apply(fight.human().health().value() - 10);
    fight.human().fighter().combat().power().set(0);
    fight.run("", 2);
    Human& player = fight.human();
    REQUIRE(player.fighter().victim().stunned());
    const TestCharacter chosenCharacter;
    Human chosen = spawnHuman(chosenCharacter, nullptr, Vec3{45.0F, 40.0F, 0.0F});
    chosen.fighter().health() = coney::combat::Health(700, 1000);

    coney::human::makePlayerOf(player, chosen, 0);
    CHECK_FALSE(player.fighter().victim().stunned());
    CHECK(player.animator().state() == AnimState::Idle);
    CHECK(player.health().value() == 700);
    CHECK(player.health().maximum() == 1000);
    CHECK(player.fighter().combat().power().value() == player.fighter().combat().power().maximum());
}

TEST_CASE("a human made (Human_Init) starts full: the creation's spawn fills the health, the meters and the stamina",
          "[human][placement]") {
    const TestCharacter character;
    const auto mesh = coney::test::makeMesh(coney::test::floorAt(0.0F, 0.0F, 80.0F, 0.0F, 80.0F));
    Human human = spawnHuman(character, mesh.get(), Vec3{40.0F, 40.0F, 0.0F});
    human.setHealthPercent(35.0F);
    REQUIRE(human.health().value() < human.health().maximum());
    // Creation's snap: from 1 m above, 0.01 over the floor.
    human.spawn(mesh.get(), Vec3{20.0F, 30.0F, 0.6F}, 0.0F);
    CHECK(human.health().value() == human.health().maximum());
    CHECK(human.stamina().value() == human.stamina().maximum());
    CHECK(human.position().z == Approx(0.01F).margin(1e-5));
}

TEST_CASE("a hurt player handed over takes the chosen human's own health, scaled as Human_MakePlayer scales it",
          "[human][placement]") {
    // HuChangePlayerGang makes the chosen AI human player 1: its own health, not the old player's, times its Warrior
    // class's percentage, with full power and stamina (0x00229c40). Coney moves its one player human onto the chosen
    // one, so it takes the meters over.
    const TestCharacter character;
    const auto mesh = coney::test::makeMesh(coney::test::floorAt(0.0F, 0.0F, 80.0F, 0.0F, 80.0F));
    Human player = spawnHuman(character, mesh.get(), Vec3{40.0F, 40.0F, 0.0F});
    Human chosen = spawnHuman(character, mesh.get(), Vec3{45.0F, 40.0F, 0.0F});
    player.fighter().health() = coney::combat::Health(900);
    player.fighter().health().set(90); // the old player, hurt
    player.fighter().combat().power().set(0);
    for (int i = 0; i < 45; ++i) { // a sprint drains his stamina
        player.step(HumanInput{.stickX = 0.0F,
                               .stickY = 1.0F,
                               .cameraForward = kAlongY,
                               .sprintHeld = true,
                               .actionPressed = false,
                               .command = coney::combat::command::kNone,
                               .buttons = 0,
                               .targets = {}},
                    mesh.get());
    }
    REQUIRE(player.stamina().value() < player.stamina().maximum());
    chosen.fighter().health() = coney::combat::Health(700, 1000);
    chosen.fighter().combat().rage().set(3);

    SECTION("a class with no percentage: the chosen human's health as it is") {
        coney::human::takeMetersOf(player, chosen, 0);
        CHECK(player.health().value() == 700);
        CHECK(player.health().maximum() == 1000);
    }
    SECTION("a class of 90 %: each scaled and rounded") {
        coney::human::takeMetersOf(player, chosen, 90);
        CHECK(player.health().value() == 630);
        CHECK(player.health().maximum() == 900);
    }
    CHECK(player.fighter().combat().power().value() == player.fighter().combat().power().maximum());
    CHECK(player.stamina().value() == player.stamina().maximum());
    CHECK(player.fighter().combat().rage().value() == 3);
}

TEST_CASE("a clip's action event (0x41) is reported in the update that passes it, once", "[human]") {
    const TestCharacter character;
    const auto mesh = coney::test::makeMesh(coney::test::floorAt(0.0F, 0.0F, 80.0F, 0.0F, 80.0F));
    Human human = spawnHuman(character, mesh.get(), Vec3{40.0F, 40.0F, 0.0F});
    human.step(stick(0.0F, 0.0F), mesh.get());
    // 665 SPECIAL_FLASH's shape: 40 frames, the action event at frame 19.
    coney::anim::AnimClip flash;
    flash.duration = 40.0F / 30.0F;
    flash.events.push_back(
        coney::anim::ClipEvent{.frame = 19, .type = 0x41, .word = 0, .position = {}, .rotation = {}});
    human.playScripted(flash, 665, 1.0F, 0.0F, coney::human::HeldFlags{.held = 0x2000});
    int fired = 0;
    int firedAt = 0;
    for (int update = 1; update <= 30; ++update) {
        human.step(stick(0.0F, 0.0F), mesh.get());
        if (human.actionEvent().has_value()) {
            CHECK(human.actionEvent() == std::optional<std::uint32_t>{665U});
            ++fired;
            firedAt = update;
        }
    }
    CHECK(fired == 1);
    CHECK(firedAt >= 19);
    CHECK(firedAt <= 20);
}

TEST_CASE("a slow-motion event on a clip's last frame is reported in the update that hands the clip over", "[human]") {
    // Expected: docs/research/slow-motion.md#events, confirmed (code): the events of the clip the update passes fire
    // up to its end when it hands over to the next task, and slow motion is switched by the ones that fire
    // (`Anim_FireFrameEvents`), not by the next task's clip.
    const TestCharacter character;
    const auto mesh = coney::test::makeMesh(coney::test::floorAt(0.0F, 0.0F, 80.0F, 0.0F, 80.0F));
    Human human = spawnHuman(character, mesh.get(), Vec3{40.0F, 40.0F, 0.0F});
    human.step(stick(0.0F, 0.0F), mesh.get());
    // 10 frames at rate 1: on at frame 2, off on frame 10, the update that ends it and gives way to the idle.
    coney::anim::AnimClip clip;
    clip.duration = 10.0F / 30.0F;
    clip.events.push_back(coney::anim::ClipEvent{.frame = 2, .type = 0x2e, .word = 0, .position = {}, .rotation = {}});
    clip.events.push_back(coney::anim::ClipEvent{.frame = 10, .type = 0x2f, .word = 0, .position = {}, .rotation = {}});
    human.playScripted(clip, 665, 1.0F, 0.0F, coney::human::HeldFlags{});
    std::vector<std::uint16_t> events;
    for (int update = 1; update <= 20; ++update) {
        human.step(stick(0.0F, 0.0F), mesh.get());
        if (const std::optional<std::uint16_t> event = human.slowMotionEvent()) {
            events.push_back(*event);
        }
    }
    CHECK(events == std::vector<std::uint16_t>{0x2e, 0x2f});
}

TEST_CASE("leaving normal mode by the script or a scene's bind is handed on once to end slow motion", "[human]") {
    // Expected: docs/research/slow-motion.md#off, confirmed (code) (`Human_ResetForMode`, whatever its
    // argument).
    const TestCharacter character;
    const auto mesh = coney::test::makeMesh(coney::test::floorAt(0.0F, 0.0F, 80.0F, 0.0F, 80.0F));
    Human human = spawnHuman(character, mesh.get(), Vec3{40.0F, 40.0F, 0.0F});
    CHECK_FALSE(human.takeSlowMotionReset());
    human.setNormalMode(false);
    CHECK(human.takeSlowMotionReset());
    CHECK_FALSE(human.takeSlowMotionReset());
    human.resetForJoin();
    CHECK(human.takeSlowMotionReset());
}

TEST_CASE("an overlay clip plays at its anim's rate: the flash with no rate flag, at 0.75, is used 25 updates in",
          "[human][flash]") {
    // Human_PushOverlayClip (0x0025ade8) builds its task with AnimTask5_Construct (0x00106240), which takes the rate
    // from the clip's Anim Range List flags (Anim_RateMultiplier, 0x00104a38): no flag, 0.75. So 665's frame-19 event
    // falls 25 updates after the update the clip started on, and the clip's 40 frames last 53 updates (0 to 52), as
    // measured in the original (docs/research/combat-rage.md#flash; the end rule: formats/animation.md#clip-end).
    const TestCharacter character;
    const auto mesh = coney::test::makeMesh(coney::test::floorAt(0.0F, 0.0F, 80.0F, 0.0F, 80.0F));
    Human human = spawnHuman(character, mesh.get(), Vec3{40.0F, 40.0F, 0.0F});
    human.step(stick(0.0F, 0.0F), mesh.get());
    REQUIRE(human.anims().rate(665) == Approx(0.75F));
    coney::anim::AnimClip flash;
    flash.duration = 40.0F / 30.0F;
    flash.events.push_back(
        coney::anim::ClipEvent{.frame = 19, .type = 0x41, .word = 0, .position = {}, .rotation = {}});
    // Pushed during update 0's actions, as the d-pad press is handled.
    human.pushOverlayClip(flash, 665, coney::human::HeldFlags{.held = 0x2000});
    int firedAt = -1;
    int lastPlaying = 0;
    for (int update = 1; update <= 70; ++update) {
        human.step(stick(0.0F, 0.0F), mesh.get());
        if (human.actionEvent().has_value()) {
            firedAt = update;
        }
        if (human.animator().animId() == 665U) {
            lastPlaying = update;
        }
    }
    CHECK(firedAt == 25);
    CHECK(lastPlaying == 52);
}

TEST_CASE("an overlay clip has no fade: it plays at full weight from its first update, the older tasks gone",
          "[human][flash]") {
    // Human_PushOverlayClip (0x0025ade8) passes 0 for both of AnimTask5_Construct's fades, so the flash snaps in where
    // a fade would blend it over the idle (docs/research/combat-rage.md#flash).
    const TestCharacter character;
    const auto mesh = coney::test::makeMesh(coney::test::floorAt(0.0F, 0.0F, 80.0F, 0.0F, 80.0F));
    Human human = spawnHuman(character, mesh.get(), Vec3{40.0F, 40.0F, 0.0F});
    human.step(stick(0.0F, 0.0F), mesh.get());
    coney::anim::AnimClip flash;
    flash.duration = 40.0F / 30.0F;
    human.pushOverlayClip(flash, 665, coney::human::HeldFlags{.held = 0x2000});
    // Nothing is left to fade out: the clip is the one layer, before and after its first update.
    CHECK(human.animator().tasks().layerCount() == 1U);
    human.step(stick(0.0F, 0.0F), mesh.get());
    CHECK(human.animator().animId() == 665U);
    CHECK(human.animator().tasks().layerCount() == 1U);
}

TEST_CASE("a human's power meter, stamina, health and rage come from its fighter profile", "[human]") {
    const TestCharacter character;
    const auto mesh = coney::test::makeMesh(coney::test::floorAt(0.0F, 0.0F, 80.0F, 0.0F, 80.0F));
    Human human = spawnHuman(character, mesh.get(), Vec3{40.0F, 40.0F, 0.0F});
    // By default the player's class 64 (Rembrandt in the street): 400 power, 135 stamina, 900 health, rage 78.
    CHECK(human.fighter().combat().power().maximum() == 400);
    CHECK(human.stamina().maximum() == 135);
    CHECK(human.health().maximum() == 900);
    CHECK(human.fighter().combat().rage().maximum() == 78);

    // A street civilian's class: 200 power, 95 stamina, its type's 600 health.
    coney::combat::PowerClass civilian = coney::combat::kCivilianPowerClass;
    civilian.staminaMax = 95;
    civilian.staminaRefillPerSecond = 20;
    human.setFighterProfile(coney::human::FighterProfile{.player = false, .powerClass = civilian, .health = 600});
    CHECK(human.fighter().combat().power().maximum() == 200);
    CHECK(human.stamina().maximum() == 95);
    CHECK(human.stamina().value() == 95);
    CHECK(human.health().maximum() == 600);
    // Spawned again it keeps them: a spawn fills the class's stamina, not the player's.
    human.spawn(mesh.get(), Vec3{40.0F, 40.0F, 0.0F}, 0.0F);
    CHECK(human.stamina().maximum() == 95);

    // Cleon as a player: his health scaled to 1530 and his Warrior class's rage of 120.
    human.setFighterProfile(coney::human::FighterProfile{
        .player = true,
        .powerClass = coney::combat::kPlayerPowerClass,
        .health = 1530,
        .bossClass = false,
        .rage =
            coney::combat::RageClass{.maximum = 120, .gainPercent = 162, .decayPercent = 110, .drainPercent = 168}});
    CHECK(human.health().maximum() == 1530);
    CHECK(human.fighter().combat().rage().maximum() == 120);
}

namespace {

// A clip event of `type` at `frame` whose text is `name` (the prop events' object name).
coney::anim::ClipEvent namedEvent(std::uint16_t frame, std::uint16_t type, std::string_view name) {
    coney::anim::ClipEvent event{.frame = frame, .type = type, .word = 0, .position = {}, .rotation = {}};
    std::ranges::copy(name, event.text.begin());
    return event;
}

// The shape of Rembrandt's 59 `gen_grab_front_power10_p2` (docs/research/combat-grabs.md#power-move-props): 70 frames;
// the can made on frame 0, moved to attach point 19 on frame 1, the spray stopped on frame 27, the can removed on 64.
coney::anim::AnimClip powerMoveClip() {
    coney::anim::AnimClip clip;
    clip.duration = 70.0F / 30.0F;
    clip.events.push_back(namedEvent(0, coney::human::kEventPropCreate, "s_spraycan"));
    coney::anim::ClipEvent place{.frame = 1,
                                 .type = coney::human::kEventPropPlace,
                                 .word = 19,
                                 .position = Vec3{0.1F, 0.2F, 0.3F},
                                 .rotation = coney::anim::Quat{0.0F, 0.0F, 0.6F, 0.8F}};
    clip.events.push_back(place);
    clip.events.push_back(coney::anim::ClipEvent{
        .frame = 27, .type = coney::human::kEventStopHeldSound, .word = 0, .position = {}, .rotation = {}});
    clip.events.push_back(namedEvent(64, coney::human::kEventPropRemove, "s_spraycan"));
    return clip;
}

} // namespace

TEST_CASE("a clip's prop events make the can, move it to the left hand and remove it at its frames", "[human]") {
    const TestCharacter character;
    const auto mesh = coney::test::makeMesh(coney::test::floorAt(0.0F, 0.0F, 80.0F, 0.0F, 80.0F));
    Human human(character.anims, coney::human::AnimSlots::player(), coney::test::identityBind(), 0.97F);
    human.spawn(mesh.get(), Vec3{40.0F, 40.0F, 0.0F}, 0.0F);
    human.step(stick(0.0F, 0.0F), mesh.get());
    REQUIRE_FALSE(human.carriedProp().has_value());
    const coney::anim::AnimClip clip = powerMoveClip();
    human.playScripted(clip, 59, 1.0F, 0.0F, coney::human::HeldFlags{.held = 0x2000});

    // Frame 0: the can, `dyn_` + the event's name, at attach point 0x13 with its type's offset (no place yet).
    human.step(stick(0.0F, 0.0F), mesh.get());
    REQUIRE(human.carriedProp().has_value());
    const coney::human::CarriedProp made = human.carriedProp().value_or(coney::human::CarriedProp{});
    CHECK(made.typeName == "dyn_s_spraycan");
    CHECK(made.point == coney::human::kCarryPoint);
    // Frame 1: attach point 19 at the event's pose, the position × the human's scale.
    int removedAt = 0;
    for (int update = 2; update <= 70 && removedAt == 0; ++update) {
        human.step(stick(0.0F, 0.0F), mesh.get());
        if (update == 3) {
            REQUIRE(human.carriedProp().has_value());
            const coney::human::CarriedProp placed = human.carriedProp().value_or(coney::human::CarriedProp{});
            CHECK(placed.point == 19);
            REQUIRE(placed.place.has_value());
            const coney::human::PropPlace place = placed.place.value_or(coney::human::PropPlace{});
            CHECK(place.position.x == Approx(0.097F));
            CHECK(place.position.z == Approx(0.291F));
            CHECK(place.rotation.z == Approx(0.6F));
        }
        if (!human.carriedProp().has_value()) {
            removedAt = update;
        }
    }
    // Frame 64: removed (the update that passes it).
    CHECK(removedAt >= 64);
    CHECK(removedAt <= 66);
}

TEST_CASE("a remove event naming another type leaves the carried prop; a cinematic's reset removes it", "[human]") {
    const TestCharacter character;
    const auto mesh = coney::test::makeMesh(coney::test::floorAt(0.0F, 0.0F, 80.0F, 0.0F, 80.0F));
    Human human = spawnHuman(character, mesh.get(), Vec3{40.0F, 40.0F, 0.0F});
    human.step(stick(0.0F, 0.0F), mesh.get());
    coney::anim::AnimClip clip;
    clip.duration = 20.0F / 30.0F;
    clip.events.push_back(namedEvent(0, coney::human::kEventPropCreate, "s_spraycan"));
    clip.events.push_back(namedEvent(5, coney::human::kEventPropRemove, "bottle"));
    human.playScripted(clip, 59, 1.0F, 0.0F, coney::human::HeldFlags{.held = 0x2000});
    for (int update = 0; update < 10; ++update) {
        human.step(stick(0.0F, 0.0F), mesh.get());
    }
    REQUIRE(human.carriedProp().has_value());
    human.resetForJoin();
    CHECK_FALSE(human.carriedProp().has_value());
}

TEST_CASE("clip event 0x45 reaches the sounds as a stop of the held sound, once", "[human]") {
    const TestCharacter character;
    const auto mesh = coney::test::makeMesh(coney::test::floorAt(0.0F, 0.0F, 80.0F, 0.0F, 80.0F));
    Human human = spawnHuman(character, mesh.get(), Vec3{40.0F, 40.0F, 0.0F});
    human.reportSounds(true);
    human.step(stick(0.0F, 0.0F), mesh.get());
    static_cast<void>(human.takeSounds());
    const coney::anim::AnimClip clip = powerMoveClip();
    human.playScripted(clip, 59, 1.0F, 0.0F, coney::human::HeldFlags{.held = 0x2000});
    int stops = 0;
    for (int update = 0; update < 40; ++update) {
        human.step(stick(0.0F, 0.0F), mesh.get());
        for (const coney::human::HumanSound& sound : human.takeSounds()) {
            stops += sound.kind == coney::human::HumanSound::Kind::StopHeld ? 1 : 0;
        }
    }
    CHECK(stops == 1);
}
