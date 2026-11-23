// SPDX-License-Identifier: GPL-3.0-or-later
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <optional>

#include <catch2/catch_test_macros.hpp>

#include "ai/brain.h"
#include "ai/gangs.h"
#include "ai/move_to_human_action.h"
#include "animation/anim_math.h"
#include "combat/combat_tuning.h"
#include "human/fighter.h"
#include "human/human.h"
#include "human/locomotion.h"
#include "support/ai_fixtures.h"

// The fight's band-keeping move (MoveMelee): backing off when too near, closing in when too far, circling to a free
// side when someone else stands in the way, and settling in the band. Synthetic clips; game time is the steps run.
// Research: docs/research/ai-moving.md#move-melee

using coney::ai::Brain;
using coney::ai::MoveToHumanAction;
using coney::anim::Vec3;
using coney::test::AiScene;

namespace {

// A band move from `mover` to its target, queued; the action it queued.
const MoveToHumanAction* queueBand(Brain& mover, float nearM, float farM, std::uint32_t limitMs = 2000) {
    REQUIRE(mover.queueAction(std::make_unique<MoveToHumanAction>(limitMs, nearM, farM)));
    return dynamic_cast<const MoveToHumanAction*>(mover.frontAction());
}

// Puts `brain` alone in a gang whose class-role lookup answers `role` for him (6 and 7 fight with no stance).
void giveRole(AiScene& scene, Brain& brain, int role) {
    coney::ai::Gangs& gangs = scene.brains.gangs();
    gangs.addMember(gangs.create(3, "movers"), brain);
    gangs.setLookups(coney::ai::GangLookups{.role = [&brain, role](const Brain& who) -> std::optional<int> {
                                                return &who == &brain ? std::optional<int>(role) : std::nullopt;
                                            },
                                            .fleeShare = {}});
}

} // namespace

TEST_CASE("a band move from too near backs off for ten updates and settles in the band", "[ai][move]") {
    // Expected: ai-moving.md#move-melee, confirmed (code)
    AiScene scene;
    Brain& player = scene.player();
    Brain& mover = scene.add(Vec3{40.0F, 41.5F, 0.0F}, 180.0F);
    mover.setTarget(&player);
    mover.setFightStance(true); // as the fight goal takes it
    const MoveToHumanAction* band = queueBand(mover, 4.0F, 4.75F, 6000);
    REQUIRE(band != nullptr);
    scene.run(1);
    CHECK(band->radialSteps() == -9);
    bool done = false;
    for (int k = 0; k < 180 && !done; ++k) {
        scene.run(1);
        done = mover.actionCount() == 0;
    }
    CHECK(done);
    CHECK(mover.distanceTo(player) > 2.75F);
}

TEST_CASE("a band move from too far closes in, and one already in the band ends at once", "[ai][move]") {
    // Expected: ai-moving.md#move-melee, confirmed (code)
    AiScene scene;
    Brain& player = scene.player();
    Brain& far = scene.add(Vec3{40.0F, 50.0F, 0.0F}, 180.0F);
    Brain& settled = scene.add(Vec3{44.4F, 40.0F, 0.0F}, 90.0F);
    for (Brain* brain : {&far, &settled}) {
        brain->setTarget(&player);
        brain->setFightStance(true);
    }
    const MoveToHumanAction* closing = queueBand(far, 4.0F, 4.75F);
    REQUIRE(closing != nullptr);
    REQUIRE(queueBand(settled, 4.0F, 4.75F) != nullptr);
    const float before = far.distanceTo(player);
    scene.run(1);
    CHECK(settled.actionCount() == 0);
    CHECK(closing->radialSteps() == 9);
    scene.run(20);
    CHECK(far.distanceTo(player) < before);
}

TEST_CASE("a band move out of the fight lock strafes facing its man at the stance walk", "[ai][move]") {
    // Expected: ai-moving.md#move-melee, confirmed (code)
    AiScene scene;
    Brain& player = scene.player();
    // 3.5 m off, beyond the lock, so the fighter holds no target, and facing away from him: the move turns him to his
    // man while the feet strafe, never at the run.
    Brain& mover = scene.add(Vec3{40.0F, 43.5F, 0.0F}, 0.0F);
    mover.setTarget(&player);
    REQUIRE(mover.human().fighter().lockTarget() == nullptr);
    mover.setFightStance(true); // as the fight goal takes it: the stance walks round him beyond the lock's range
    CHECK(mover.human().fighter().lockTarget() == &player.human());
    REQUIRE(queueBand(mover, 0.68F, 0.91F) != nullptr);
    const float before = mover.distanceTo(player);
    const float stanceWalk = coney::combat::combatTuning().combatWalkSpeed;
    for (int k = 0; k < 15; ++k) {
        scene.run(1);
        CHECK(mover.human().speed() <= stanceWalk + 0.01F);
    }
    const Vec3 to = coney::anim::subtract(player.human().position(), mover.human().position());
    CHECK(std::fabs(coney::human::wrapAngle(coney::human::headingOf(to) - mover.human().heading())) < 0.5F);
    CHECK(mover.distanceTo(player) < before);
}

TEST_CASE("a band move blocked by another man in the way circles to a side instead", "[ai][move]") {
    // Expected: ai-moving.md#move-melee, confirmed (code)
    AiScene scene;
    Brain& player = scene.player();
    // The mover 7 m off, a third man 1 m in front of him on the line to the player.
    Brain& mover = scene.add(Vec3{40.0F, 47.0F, 0.0F}, 180.0F);
    scene.add(Vec3{40.0F, 46.0F, 0.0F}, 0.0F);
    mover.setTarget(&player);
    mover.setFightStance(true);
    const MoveToHumanAction* band = queueBand(mover, 4.0F, 4.75F);
    REQUIRE(band != nullptr);
    scene.run(1);
    CHECK(band->radialSteps() == 0);
    CHECK(std::abs(band->sideSteps()) == 9);
}

TEST_CASE("a band move out of the fight stance closes at the run, in it at the stance walk", "[ai][move]") {
    // Expected: ai-moving.md#move-melee, confirmed (code)
    AiScene scene;
    Brain& player = scene.player();
    // 7 m off, beyond the lock: out of the stance the radial step closes at the run through the locomotion; in it
    // the stance walks him (Human_UpdateControl).
    Brain& loose = scene.add(Vec3{33.0F, 40.0F, 0.0F}, 90.0F);
    Brain& stance = scene.add(Vec3{47.0F, 40.0F, 0.0F}, 270.0F);
    for (Brain* brain : {&loose, &stance}) {
        brain->setTarget(&player);
        REQUIRE(queueBand(*brain, 4.0F, 4.75F) != nullptr);
    }
    loose.setFightStance(false);
    stance.setFightStance(true);
    const float stanceWalk = coney::combat::combatTuning().combatWalkSpeed;
    float looseFastest = 0.0F;
    float stanceFastest = 0.0F;
    for (int k = 0; k < 30; ++k) {
        scene.run(1);
        looseFastest = std::max(looseFastest, loose.human().speed());
        stanceFastest = std::max(stanceFastest, stance.human().speed());
    }
    CHECK(looseFastest > stanceWalk);
    CHECK(stanceFastest <= stanceWalk + 0.01F);
}

TEST_CASE("a band move with no stance leans only a little off the line to its man when it circles", "[ai][move]") {
    // Expected: ai-moving.md#move-melee, confirmed (code)
    AiScene scene;
    Brain& player = scene.player();
    Brain& mover = scene.add(Vec3{40.0F, 47.0F, 0.0F}, 180.0F);
    scene.add(Vec3{40.0F, 46.0F, 0.0F}, 0.0F);
    giveRole(scene, mover, 6);
    REQUIRE(coney::ai::fightsWithoutStance(mover));
    mover.setTarget(&player);
    const MoveToHumanAction* band = queueBand(mover, 4.0F, 4.75F);
    REQUIRE(band != nullptr);
    scene.run(1);
    REQUIRE(std::abs(band->sideSteps()) == 9);
    REQUIRE(mover.human().record().move.has_value());
    const Vec3 to = coney::anim::subtract(player.human().position(), mover.human().position());
    const float heading = mover.human().record().move->heading; // NOLINT(bugprone-unchecked-optional-access)
    const float lean = std::fabs(coney::human::wrapAngle(heading - coney::human::headingOf(to)));
    // About atan(1 / 7): the unit side axis added to the 7 m offset.
    CHECK(lean > 0.05F);
    CHECK(lean < 0.25F);
}

TEST_CASE("an aborted band move with no stance turns to a live man, not to a dead one", "[ai][move]") {
    // Expected: ai-moving.md#move-melee, confirmed (code)
    AiScene scene;
    Brain& player = scene.player();
    Brain& mover = scene.add(Vec3{40.0F, 41.5F, 0.0F}, 90.0F);
    giveRole(scene, mover, 7);
    mover.setTarget(&player);
    REQUIRE(queueBand(mover, 4.0F, 4.75F, 6000) != nullptr);
    scene.run(1);
    REQUIRE(mover.clearActions());
    CHECK(mover.human().record().move.has_value());

    REQUIRE(queueBand(mover, 4.0F, 4.75F, 6000) != nullptr);
    scene.run(1);
    player.human().fighter().health().set(0);
    REQUIRE(mover.clearActions());
    CHECK_FALSE(mover.human().record().move.has_value());
}

TEST_CASE("a re-target during a band move turns the fight stance to the new man, and a forgotten one leaves it",
          "[ai][move]") {
    // Expected: ai-moving.md#move-melee, confirmed (code): the stance is the human's state round the brain's target
    // (+0x124), which the fight goal enters each update (Human_EnterFightStance 0x0022fe80)
    AiScene scene;
    Brain& first = scene.add(Vec3{40.0F, 44.0F, 0.0F}, 0.0F);
    Brain& second = scene.add(Vec3{44.0F, 40.0F, 0.0F}, 0.0F);
    Brain& mover = scene.add(Vec3{40.0F, 40.0F, 0.0F}, 0.0F);
    mover.setTarget(&first);
    mover.setFightStance(true); // as the fight goal takes it
    REQUIRE(queueBand(mover, 4.0F, 4.75F, 6000) != nullptr);
    scene.run(1);
    REQUIRE(mover.human().fighter().lockTarget() == &first.human());
    // The re-target while the move runs: the stance walks round the new man at once.
    mover.setTarget(&second);
    CHECK(mover.human().fighter().lockTarget() == &second.human());
    // The man leaves the scene: the stance holds no one.
    mover.forget(second);
    CHECK(mover.human().fighter().lockTarget() != &second.human());
}

TEST_CASE("a holder displaced from a full slot list during a band move leaves the fight stance", "[ai][move]") {
    // Expected: ai-moving.md#move-melee, confirmed (code): the stance is the human's state round the brain's target
    // (+0x124); Brain_ClaimAttackSlot 0x0028df30 takes the farthest holder's slot and his target, so his stance holds
    // no one (a Coney-only hand-off, Brain::handFightStance)
    AiScene scene;
    Brain& player = scene.player();
    player.setAttackSlotCount(1);
    Brain& holder = scene.add(Vec3{40.0F, 44.5F, 0.0F}, 180.0F);
    holder.setTarget(&player);
    REQUIRE(holder.hasAttackSlot());
    holder.setFightStance(true); // as the fight goal takes it
    REQUIRE(queueBand(holder, 4.0F, 4.75F, 6000) != nullptr);
    scene.run(1);
    REQUIRE(holder.human().fighter().lockTarget() == &player.human());
    // A nearer attacker takes the one slot: the holder loses his target, and his stance goes with it.
    Brain& nearer = scene.add(Vec3{41.5F, 40.0F, 0.0F}, 270.0F);
    nearer.setTarget(&player);
    REQUIRE(nearer.hasAttackSlot());
    REQUIRE(holder.target() == nullptr);
    CHECK(holder.human().fighter().lockTarget() != &player.human());
}
