// SPDX-License-Identifier: GPL-3.0-or-later
#include "human/human.h"

#include <cmath>
#include <memory>
#include <numbers>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "characters/anim_set.h"
#include "human/body.h"
#include "human/human_animator.h"
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
            CHECK(*human.actionEvent() == 665U);
            ++fired;
            firedAt = update;
        }
    }
    CHECK(fired == 1);
    CHECK(firedAt >= 19);
    CHECK(firedAt <= 20);
}
