// SPDX-License-Identifier: GPL-3.0-or-later
#include "ai/give_way.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <numbers>
#include <vector>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "ai/ai_config.h"
#include "ai/brain.h"
#include "ai/brains.h"
#include "ai/gangs.h"
#include "ai/scripted_brains.h"
#include "human/fighter_clips.h"
#include "human/human.h"
#include "human/humans.h"
#include "human/locomotion.h"
#include "human/step_control.h"
#include "support/collision_fixtures.h"
#include "support/fight_fixtures.h"
#include "support/human_fixtures.h"
#include "world_objects/flags.h"

// Giving way (docs/research/ai.md#giving-way) and the step control (docs/research/characters.md#step-control): the
// step's clip by angle, the step played on the human, who may give way, the sector a GiveWay steps to, and the ask
// passed on to a neighbour when no sector is free. Synthetic humans on a floor.

using Catch::Approx;
using coney::ai::Brain;
using coney::ai::BrainType;
using coney::ai::FightSettings;
using coney::anim::Vec3;
using coney::human::Human;
namespace ai = coney::ai;
namespace clips = coney::human::clips;

namespace {

constexpr float kPi = std::numbers::pi_v<float>;

// Degrees as radians.
constexpr float rad(float degrees) { return degrees * kPi / 180.0F; }

// The fight clips plus a forward step (401: 0.6 s, 1 m/s root velocity) and a left step (399: 0.6 s, none).
std::vector<coney::test::LocomotionClip> stepClips() {
    std::vector<coney::test::LocomotionClip> all = coney::test::fightClips();
    all.push_back({.id = clips::kStepForward, .speed = 0.6F, .duration = 0.6F, .rootVelocity = 1.0F, .rangeFlags = 0});
    all.push_back({.id = clips::kStepLeft, .speed = 0.0F, .duration = 0.6F, .rootVelocity = 0.0F, .rangeFlags = 0});
    return all;
}

// Humans on an 80 m floor, each with a brain, stepped together with their brains.
struct Scene {
    coney::test::FightCharacter character{stepClips()};
    std::unique_ptr<coney::raycast::CollisionMesh> mesh =
        coney::test::makeMesh(coney::test::floorAt(0.0F, 0.0F, 80.0F, 0.0F, 80.0F));
    std::vector<std::unique_ptr<Human>> humans;
    coney::human::Humans step;
    ai::Brains brains;

    Scene() { step.setBrains(brains.hook()); }

    // A human with its feet at (x, y) facing `headingDegrees` (0 is +y), with a brain of `type`.
    Brain& add(float x, float y, float headingDegrees, BrainType type = BrainType::Gang) {
        humans.push_back(std::make_unique<Human>(character.anims, coney::human::AnimSlots::player(),
                                                 coney::test::identityBind(), 1.0F, &character.ranges));
        Human& made = *humans.back();
        const bool player = type == BrainType::Player;
        made.setFighterProfile(
            coney::human::FighterProfile{.player = player, .powerClass = ai::kWarriorPowerClass, .health = 1400});
        made.spawn(mesh.get(), Vec3{x, y, 0.0F}, headingDegrees);
        step.add(made, player);
        return brains.add(made, type, FightSettings{}, static_cast<std::uint32_t>(humans.size()));
    }

    void run(int steps) {
        for (int k = 0; k < steps; ++k) {
            step.update(mesh.get());
        }
    }
};

// The front action as a GiveWay, or null.
const ai::GiveWayAction* frontGiveWay(Brain& brain) { return dynamic_cast<ai::GiveWayAction*>(brain.frontAction()); }

} // namespace

TEST_CASE("a step's clip goes by the heading's angle off the facing, and ends with the clip along the heading",
          "[ai][give-way]") {
    using coney::human::stepClipFor;
    // Within 22.5°: forward, facing the heading.
    CHECK(stepClipFor(rad(20.0F), 0.0F, false, 0).clip == clips::kStepForward);
    CHECK(stepClipFor(rad(20.0F), 0.0F, false, 0).endFacing == Approx(rad(20.0F)));
    // 112.5° or more: backward, facing away from it.
    CHECK(stepClipFor(rad(150.0F), 0.0F, false, 0).clip == clips::kStepBackward);
    CHECK(stepClipFor(rad(150.0F), 0.0F, false, 0).endFacing == Approx(rad(-30.0F)));
    CHECK(stepClipFor(rad(-120.0F), 0.0F, false, 0).clip == clips::kStepBackward);
    // Between, to the side the heading lies on, a quarter turn from it.
    CHECK(stepClipFor(rad(60.0F), 0.0F, false, 0).clip == clips::kStepLeft);
    CHECK(stepClipFor(rad(60.0F), 0.0F, false, 0).endFacing == Approx(rad(-30.0F)));
    CHECK(stepClipFor(rad(-90.0F), 0.0F, false, 0).clip == clips::kStepRight);
    CHECK(stepClipFor(rad(-90.0F), 0.0F, false, 0).endFacing == Approx(0.0F).margin(1e-6));
    // A boost above 0 dashes; the fight stance shuffles whatever the boost.
    CHECK(stepClipFor(0.0F, 0.0F, false, 1).clip == clips::kDashForward);
    CHECK(stepClipFor(kPi, 0.0F, false, 1).clip == clips::kDashBackward);
    CHECK(stepClipFor(rad(90.0F), 0.0F, false, 1).clip == clips::kDashLeft);
    CHECK(stepClipFor(rad(-90.0F), 0.0F, false, 1).clip == clips::kDashRight);
    CHECK(stepClipFor(0.0F, 0.0F, true, 1).clip == clips::kStanceStepForward);
    CHECK(stepClipFor(kPi, 0.0F, true, 0).clip == clips::kStanceStepBackward);
    CHECK(stepClipFor(rad(90.0F), 0.0F, true, 0).clip == clips::kStanceStepLeft);
    CHECK(stepClipFor(rad(-90.0F), 0.0F, true, 0).clip == clips::kStanceStepRight);
}

TEST_CASE("a TakeStep plays one step clip that moves the body by its root, then ends", "[ai][give-way]") {
    Scene scene;
    Brain& stepper = scene.add(40.0F, 40.0F, 0.0F);
    REQUIRE(stepper.queueAction(std::make_unique<ai::TakeStepAction>(0.0F)));
    scene.run(1);
    CHECK(stepper.human().animator().animId() == clips::kStepForward);
    CHECK(stepper.human().stepHeld());
    // A second step cannot enter while one waits; none waits now that the clip plays.
    CHECK_FALSE(stepper.human().stepControlWaiting());
    scene.run(40);
    CHECK(stepper.actionCount() == 0);
    CHECK_FALSE(stepper.human().stepHeld());
    // Forward by about the clip's 0.6 m, and no farther.
    CHECK(stepper.human().position().y == Approx(40.6F).margin(0.15F));
    CHECK(stepper.human().position().x == Approx(40.0F).margin(0.01F));
}

TEST_CASE("a side step turns the human to its end facing over half the clip", "[ai][give-way]") {
    Scene scene;
    Brain& stepper = scene.add(40.0F, 40.0F, 0.0F);
    // 60° to the left: the left step, ending 30° to the right of where he faced.
    REQUIRE(stepper.queueAction(std::make_unique<ai::TakeStepAction>(rad(60.0F))));
    scene.run(1);
    CHECK(stepper.human().animator().animId() == clips::kStepLeft);
    scene.run(12);
    CHECK(stepper.human().heading() == Approx(rad(-30.0F)).margin(0.01F));
    scene.run(30);
    CHECK(stepper.actionCount() == 0);
    CHECK(stepper.human().heading() == Approx(rad(-30.0F)).margin(0.01F));
}

TEST_CASE("a step control is refused while one waits, and its abort is refused while the clip plays",
          "[ai][give-way]") {
    Scene scene;
    Brain& stepper = scene.add(40.0F, 40.0F, 0.0F);
    REQUIRE(stepper.human().enterStepControl(0.0F, 0));
    CHECK_FALSE(stepper.human().enterStepControl(1.0F, 0));
    stepper.human().leaveStepControl();
    CHECK_FALSE(stepper.human().stepControlWaiting());

    REQUIRE(stepper.queueAction(std::make_unique<ai::TakeStepAction>(0.0F)));
    scene.run(1);
    REQUIRE(stepper.human().stepHeld());
    CHECK_FALSE(stepper.clearActions());
    CHECK(stepper.actionCount() == 1);
}

TEST_CASE("a standing AI gives way into the sector straight off the mover's line, once", "[ai][give-way]") {
    Scene scene;
    Brain& mover = scene.add(40.0F, 38.0F, 0.0F);
    Brain& stander = scene.add(40.3F, 40.0F, 0.0F);
    const Vec3 point = mover.human().position();
    const Vec3 step{0.0F, 1.0F, 0.0F};
    REQUIRE(ai::pushAside(mover, stander, point, step));
    // Straight off the line is to his right: sector 6, a heading of 270°.
    const ai::GiveWayAction* action = frontGiveWay(stander);
    REQUIRE(action != nullptr);
    CHECK(coney::human::wrapAngle(action->heading()) == Approx(rad(-90.0F)));
    CHECK(action->mover() == &mover);
    CHECK_FALSE(action->boost());
    CHECK(action->delayMs() >= 0);
    CHECK(action->delayMs() < ai::kGiveWayDelayRange);
    CHECK(stander.givingWay());
    CHECK_FALSE(stander.pushingAside());
    CHECK_FALSE(mover.pushingAside());
    // Asked again while that lives, he says yes and queues nothing more.
    CHECK(ai::pushAside(mover, stander, point, step));
    CHECK(stander.actionCount() == 1);
    // The step runs and ends (the set has no right step, so nothing plays); the mark goes with the action.
    scene.run(60);
    CHECK(stander.actionCount() == 0);
    CHECK_FALSE(stander.givingWay());
}

TEST_CASE("only an idle AI that is no threat gives way", "[ai][give-way]") {
    Scene scene;
    Brain& player = scene.add(40.0F, 38.0F, 0.0F, BrainType::Player);
    Brain& stander = scene.add(40.3F, 40.0F, 0.0F);
    const Vec3 step{0.0F, 1.0F, 0.0F};
    // A player's brain does not give way.
    CHECK_FALSE(ai::giveWayTo(stander, player, stander.human().position(), step));
    // Enemies do not.
    ai::Gangs& gangs = scene.brains.gangs();
    const int a = gangs.create(1, "a");
    const int b = gangs.create(2, "b");
    REQUIRE(a >= 0);
    REQUIRE(b >= 0);
    gangs.addMember(a, player);
    gangs.addMember(b, stander);
    gangs.makeEnemies(a, b);
    CHECK(ai::isThreat(stander, player));
    CHECK(ai::isThreat(player, stander));
    CHECK_FALSE(ai::giveWayTo(player, stander, player.human().position(), step));
    gangs.removeMember(stander);
    CHECK_FALSE(ai::isThreat(stander, player));
    // Two players threaten each other.
    Brain& other = scene.add(20.0F, 20.0F, 0.0F, BrainType::Player);
    CHECK(ai::isThreat(player, other));
    // Not idle: a step clip held on the record.
    REQUIRE(stander.human().enterStepControl(0.0F, 0));
    scene.run(1);
    REQUIRE(stander.human().stepHeld());
    CHECK_FALSE(stander.human().idleUnderControl());
    CHECK_FALSE(ai::giveWayTo(player, stander, player.human().position(), step));
    scene.run(40);
    REQUIRE(stander.human().idleUnderControl());
    // Idle again, he gives way to the walking (not jogging) player with a step, not a dash.
    REQUIRE(ai::giveWayTo(player, stander, player.human().position(), step));
    REQUIRE(frontGiveWay(stander) != nullptr);
    CHECK_FALSE(frontGiveWay(stander)->boost());
}

TEST_CASE("a GiveWay with the boost dashes: the turn boost is raised for the step and put back after",
          "[ai][give-way]") {
    Scene scene;
    Brain& mover = scene.add(40.0F, 38.0F, 0.0F);
    Brain& stander = scene.add(40.0F, 40.0F, 0.0F);
    REQUIRE(stander.queueAction(std::make_unique<ai::GiveWayAction>(stander, 0.0F, mover, true, 0)));
    CHECK(stander.givingWay());
    scene.run(1);
    CHECK(stander.turnBoost() == 1);
    // No dash clip in the set: nothing plays, the step ends and the boost goes back.
    scene.run(2);
    CHECK(stander.actionCount() == 0);
    CHECK(stander.turnBoost() == 0);
    CHECK_FALSE(stander.givingWay());
}

TEST_CASE("with no free sector the stander asks the human in the first one to make room", "[ai][give-way]") {
    Scene scene;
    Brain& mover = scene.add(40.0F, 38.0F, 0.0F);
    Brain& stander = scene.add(40.3F, 40.0F, 0.0F);
    // A ring 1 m round him in every sector giving way tries (6, 7, 0, 5, 1, 2, 4; sector 3 is never tried).
    std::array<Brain*, 8> ring{};
    for (const int k : {6, 7, 0, 5, 1, 2, 4}) {
        const Vec3 way = coney::human::facing(rad(45.0F * static_cast<float>(k)));
        ring[static_cast<std::size_t>(k)] = &scene.add(40.3F + way.x, 40.0F + way.y, 0.0F);
    }
    const Vec3 step{0.0F, 1.0F, 0.0F};
    REQUIRE(ai::pushAside(mover, stander, mover.human().position(), step));
    // He queues nothing; the one in sector 6 steps away for him.
    CHECK(stander.actionCount() == 0);
    const ai::GiveWayAction* action = frontGiveWay(*ring[6]);
    REQUIRE(action != nullptr);
    CHECK(action->mover() == &stander);
    for (Brain* brain : {&mover, &stander, ring[6]}) {
        CHECK_FALSE(brain->pushingAside());
    }
}

TEST_CASE("ActGiveWay has the human give way to the other along his facing, unless its queue is full",
          "[ai][give-way]") {
    Scene scene;
    coney::world_objects::WorldFlags flags;
    coney::ai::ScriptedBrains scripted(scene.brains, flags);
    Brain& stander = scene.add(40.3F, 40.0F, 0.0F);
    Brain& mover = scene.add(40.0F, 38.0F, 0.0F);
    scripted.bind(1.0, stander);
    scripted.bind(2.0, mover);
    // Himself, or an unknown handle: nothing.
    scripted.actGiveWay(1.0, 1.0);
    scripted.actGiveWay(1.0, 9.0);
    CHECK(stander.actionCount() == 0);
    // The mover faces +y from (40, 38): the stander steps off that line to his right.
    scripted.actGiveWay(1.0, 2.0);
    const ai::GiveWayAction* action = frontGiveWay(stander);
    REQUIRE(action != nullptr);
    CHECK(coney::human::wrapAngle(action->heading()) == Approx(rad(-90.0F)));
    CHECK(action->mover() == &mover);

    // With 8 actions queued he is not asked.
    Brain& busy = scene.add(60.0F, 60.0F, 0.0F);
    scripted.bind(3.0, busy);
    for (std::size_t k = 0; k < ai::kGiveWayQueueLimit; ++k) {
        REQUIRE(busy.queueAction(std::make_unique<ai::TakeStepAction>(0.0F, 1000)));
    }
    scripted.actGiveWay(3.0, 2.0);
    CHECK_FALSE(busy.givingWay());
}
